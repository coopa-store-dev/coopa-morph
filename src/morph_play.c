// morph_play.c —— 见 morph_play.h。
#include "morph_play.h"

#include <string.h>

#define MP_CLOCK_MAX (0xFFFFFFu * 10u)  // 'B' 里的时钟 / 10 ms 只有 3 字节:'G' 'A' 说的时钟比它大是乱的
#define MP_STALE_MS 10000               // 人的状态比 clock 旧这么多就不补算了(clock 跳过、乱消息),原样摆到现在
#define MP_RTT_MAX 2000

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put24(uint8_t *p, uint32_t v) { put16(p, v); p[2] = (uint8_t)(v >> 16); }
static void put32(uint8_t *p, uint32_t v) { put24(p, v); p[3] = (uint8_t)(v >> 24); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get24(const uint8_t *p) { return get16(p) | (uint32_t)p[2] << 16; }
static uint32_t get32(const uint8_t *p) { return get24(p) | (uint32_t)p[3] << 24; }

static uint32_t floor_step(uint32_t t) { return t - t % MB_STEP_MS; }

// ---- 发送 ----

// 链路一片要多久的基数:量到的平滑来回再多 1/8(照格斗:平均起来不低估,多 1/8 免得正好卡在满负荷上越排越长)
static uint16_t busy_base(const morph_play_t *p) {
    uint32_t r = p->rtt ? p->rtt : MP_RTT_GUESS;
    return (uint16_t)(r + r / 8u);
}

// 交给 send。成功了就当链路又要忙一片的时间(停等:一片的确认回来才发下一片)。send 被拒 = 链路里已经排满
// MP_LINK_Q 片:一片的估计加到 3/2(这一次拥堵只加一次),等排着的都送完;之后连着成功 MP_BUSY_RUN 次才往
// 量到的基数收回 1 ms,下次 ping 重新量。(照 fight_game.c 的 hand)
static bool hand(morph_play_t *p, const uint8_t *m, size_t n) {
    if (!p->busy) p->busy = busy_base(p);
    if (!p->send(p->ctx, m, n)) {
        p->refused++;
        if (p->now >= p->link_free) {
            uint32_t up = p->busy * 3u / 2u;
            if (up > MP_BUSY_MAX) up = p->busy > MP_BUSY_MAX ? p->busy : MP_BUSY_MAX;
            p->busy = (uint16_t)up;
            p->link_free = p->now + (uint32_t)MP_LINK_Q * p->busy;
        }
        p->busy_run = 0;
        return false;
    }
    p->tx++;
    p->hand_at = p->now;
    uint32_t from = p->link_free > p->now ? p->link_free : p->now;
    p->link_free = from + p->busy;
    if (++p->busy_run >= MP_BUSY_RUN) {
        p->busy_run = 0;
        if (p->busy > busy_base(p)) p->busy--;
    }
    return true;
}

// 'H' 'G' 不能丢:排着,按顺序交,被拒就留着下一帧再交
static void keep(morph_play_t *p, const uint8_t *m, size_t n) {
    if (p->keep_n == 2) return;  // 一次连接最多 'H' 和 'G' 各一条
    memcpy(p->keep[p->keep_n], m, n);
    p->keep_len[p->keep_n++] = (uint8_t)n;
}

// 'B':自己的人现在的状态(交的时候现打包)
static void send_state(morph_play_t *p) {
    const morph_body_t *b = &p->me;
    uint8_t m[MP_B_LEN] = { 'B', (uint8_t)(p->tx_seq + 1) };
    put24(m + 2, p->me_at / 10u);
    put24(m + 5, (uint32_t)b->x);
    put16(m + 8, (uint16_t)b->y);
    put16(m + 10, (uint16_t)b->vy);
    put16(m + 12, (b->dir > 0 ? 1u : 0u) | (uint32_t)(b->st & 3u) << 1 | (uint32_t)(b->flaps & 7u) << 3);
    if (!hand(p, m, sizeof m)) return;
    p->tx_seq++;
    p->state_due = false;
    p->state_at = p->now;
}

static void pump(morph_play_t *p) {
    if (!p->up || p->solo) return;
    while (p->keep_n) {
        if (p->keep[0][0] == 'G') put32(p->keep[0] + 1, p->clock);  // 交的时候的时钟
        if (!hand(p, p->keep[0], p->keep_len[0])) return;
        p->keep_n--;
        memcpy(p->keep[0], p->keep[1], sizeof p->keep[0]);
        p->keep_len[0] = p->keep_len[1];
    }
    if (p->phase != MP_PLAY || p->now < p->link_free) return;  // 下面这些只在估计链路空下来时交
    if (p->ans) {  // 主动方回 ping:现在的时钟、'Q' 到了以后等了多久(被连方从来回里扣掉)
        uint8_t m[8] = { 'A', p->ans_id };
        uint32_t held = p->now - p->ans_at;
        put32(m + 2, p->clock);
        put16(m + 6, held > UINT16_MAX ? UINT16_MAX : held);
        if (hand(p, m, sizeof m)) p->ans = false;
        return;
    }
    // 被连方 ping:平时等链路估计空了再静 MP_PING_QUIET(量到的来回才不带排队)、排在 'B' 后面;
    // 'G' 以后还没对过时钟时(落后一个单程)估计空了就 ping,排在 'B' 前面
    bool ping = !p->initiator && !p->ping_wait && p->now >= p->ping_next &&
                (!p->clock_ok || p->now >= p->link_free + MP_PING_QUIET);
    if (!(ping && !p->clock_ok) && (p->state_due || p->now - p->state_at >= MP_SYNC_MS)) {
        send_state(p);
    } else if (ping) {
        uint8_t m[4] = { 'Q', (uint8_t)(p->ping_id + 1) };
        put16(m + 2, p->rtt);
        if (!hand(p, m, sizeof m)) return;
        p->ping_id++;
        p->ping_wait = true;
        p->ping_at = p->now;
        p->ping_next = p->now + MP_PING_MS;
    }
}

// ---- 关卡时钟和人 ----

// 把人从 *at 一步一步算到 clock。旧得太多(clock 跳过、乱消息)就不补算,原样摆到现在;是自己的人就重发一条
// (对方手里那条的时刻对不上了)。返回 MB_EV_* 的组合
static unsigned follow(morph_play_t *p, morph_body_t *b, uint32_t *at) {
    if (*at + MP_STALE_MS < p->clock) {
        *at = floor_step(p->clock);
        if (b == &p->me) p->state_due = true;
    }
    unsigned ev = 0;
    while (*at + MB_STEP_MS <= p->clock) {
        ev |= mb_step(b, p->L);
        *at += MB_STEP_MS;
    }
    return ev;
}

// 两个人都算到 clock;自己的人有事就该发 'B'
static void catch_up(morph_play_t *p) {
    if (follow(p, &p->me, &p->me_at) & (MB_EV_LAND | MB_EV_STOP | MB_EV_PIT | MB_EV_GO)) p->state_due = true;
    if (p->peer_ok) follow(p, &p->peer, &p->peer_at);
}

// 被连方改自己的时钟,两个人马上算到新的 clock。往前跳:自己的人照常一步一步补算(和已经发出去的 'B' 对得上;
// 跳得太远由 follow 原样摆到现在并重发)。往回跳超过 MP_JUMP_MS(主动方重启过):不停着等 clock 追上,
// 自己的人就算「现在」的,时刻变了,重发一条
static void set_clock(morph_play_t *p, uint32_t c) {
    if (c + MP_JUMP_MS < p->clock) {
        p->me_at = floor_step(c);
        p->state_due = true;
    }
    p->clock = c;
    catch_up(p);
}

// ---- 收 ----

static void on_hello(morph_play_t *p, const uint8_t *m, size_t n) {
    if (n < 2 || p->hello) return;
    if (m[1] != p->proto) {
        p->phase = MP_WAIT;
        return;
    }
    p->hello = true;
    if (!p->initiator) return;  // 被连方等 'G'
    uint8_t g[5] = { 'G' };  // 时钟交的时候再填
    keep(p, g, sizeof g);
    p->phase = MP_PLAY;
    p->state_due = true;
}

static void on_go(morph_play_t *p, const uint8_t *m, size_t n) {
    if (p->initiator || !p->hello || n < 5) return;
    uint32_t c = get32(m + 1);
    if (c > MP_CLOCK_MAX) return;
    set_clock(p, c);
    p->phase = MP_PLAY;
    p->clock_ok = false;  // 'G' 晚到了一个单程:第一次 ping 回来直接对上
    p->state_due = true;
    p->ping_next = p->now;
}

static void on_ping(morph_play_t *p, const uint8_t *m, size_t n) {
    if (!p->initiator || p->phase != MP_PLAY || n < 4) return;
    p->ans = true;
    p->ans_id = m[1];
    p->ans_at = p->now;
    uint32_t r = get16(m + 2);
    if (r) {
        p->rtt = (uint16_t)(r > MP_RTT_MAX ? MP_RTT_MAX : r);
        p->busy = busy_base(p);  // 量到新的了:拥堵的估计作废
    }
}

static void on_ack(morph_play_t *p, const uint8_t *m, size_t n) {
    if (p->initiator || p->phase != MP_PLAY || n < 8 || !p->ping_wait || m[1] != p->ping_id) return;
    p->ping_wait = false;
    uint32_t s = p->now - p->ping_at, held = get16(m + 6);
    s = s > held ? s - held : 0;  // 真在路上的来回
    if (s > MP_RTT_MAX) s = MP_RTT_MAX;
    if (!s) s = 1;
    // 比平时的来回长一半以上:路上卡了一下,两个方向卡得不一样长,对时钟不准(差得多还会误跳),这次不对时钟
    bool clean = !p->rtt || s <= p->rtt + p->rtt / 2u;
    p->rtt = (uint16_t)(p->rtt ? (int32_t)p->rtt + ((int32_t)s - (int32_t)p->rtt) / 4 : (int32_t)s);  // 新的占 1/4
    p->busy = busy_base(p);
    if (p->hand_at == p->ping_at && p->link_free > p->now) p->link_free = p->now;  // 'Q' 是最后一片:回答到了,链路空了
    uint32_t c = get32(m + 2);
    if (!clean || c > MP_CLOCK_MAX) return;
    uint32_t est = c + s / 2;  // 主动方现在的时钟:它交出 'A' 时的时钟 + 单程
    int32_t diff = (int32_t)(est - p->clock);
    if (!p->clock_ok || diff > MP_JUMP_MS || diff < -MP_JUMP_MS)
        set_clock(p, est);
    else
        set_clock(p, p->clock + (uint32_t)(diff > MP_NUDGE_MS ? MP_NUDGE_MS : diff < -MP_NUDGE_MS ? -MP_NUDGE_MS : diff));
    p->clock_ok = true;
}

static void on_state(morph_play_t *p, const uint8_t *m, size_t n) {
    if (!p->hello || n < MP_B_LEN) return;
    uint8_t seq = m[1];
    if ((int8_t)(uint8_t)(seq - p->rx_seq) <= 0) return;  // 不比收到过的新
    uint32_t bits = get16(m + 12), x = get24(m + 5);
    if ((bits >> 3 & 7u) > MB_FLAPS) return;
    p->rx_seq = seq;
    morph_body_t b;
    mb_init(&b, 0, 0, 1);
    b.x = (int32_t)(x ^ 0x800000u) - 0x800000;  // 3 字节有符号
    b.y = (int16_t)get16(m + 8);
    b.vy = (int16_t)get16(m + 10);
    b.dir = bits & 1u ? 1 : -1;
    b.st = (uint8_t)(bits >> 1 & 3u);
    b.flaps = (uint8_t)(bits >> 3 & 7u);
    p->peer = b;
    p->peer_at = get24(m + 2) * 10u;
    p->peer_ok = true;
    follow(p, &p->peer, &p->peer_at);  // 比 clock 新就先原样摆着
}

// ---- 对外 ----

void mp_init(morph_play_t *p, mp_send_fn send, void *ctx, const morph_level_t *L) {
    memset(p, 0, sizeof *p);
    p->send = send;
    p->ctx = ctx;
    p->L = L;
    p->proto = MP_PROTO;
    p->solo = send == NULL;
    p->phase = p->solo ? MP_PLAY : MP_WAIT;
    mb_init(&p->me, L->spawn_x, L->spawn_y, 1);
    p->peer = p->me;
}

void mp_connected(morph_play_t *p, bool initiator) {
    if (p->solo) return;
    p->initiator = initiator;
    p->up = true;
    p->hello = false;
    if (p->phase == MP_PLAY) p->phase = MP_PAUSED;  // 没收到 lost 就又连上了:照样先对一下
    p->keep_n = 0;
    p->tx_seq = p->rx_seq = 0;
    p->state_due = false;
    p->link_free = p->hand_at = p->state_at = p->now;
    p->busy = 0;  // 来回留着(多半还是同一张卡),拥堵的估计重来
    p->busy_run = 0;
    p->ping_wait = false;
    p->ping_next = p->now;
    p->clock_ok = false;
    p->ans = false;
    uint8_t h[2] = { 'H', p->proto };
    keep(p, h, sizeof h);
    pump(p);
}

void mp_lost(morph_play_t *p) {
    if (p->solo) return;
    p->up = false;
    p->hello = false;
    p->keep_n = 0;
    p->ans = false;
    p->ping_wait = false;
    if (p->phase == MP_PLAY) p->phase = MP_PAUSED;
}

void mp_on_msg(morph_play_t *p, const uint8_t *m, size_t n) {
    if (!n) return;
    p->rx++;
    switch (m[0]) {
    case 'H': on_hello(p, m, n); break;
    case 'G': on_go(p, m, n); break;
    case 'Q': on_ping(p, m, n); break;
    case 'A': on_ack(p, m, n); break;
    case 'B': on_state(p, m, n); break;
    default: break;
    }
    pump(p);
}

void mp_tick(morph_play_t *p, uint32_t dt_ms) {
    p->now += dt_ms;
    if (p->phase == MP_PLAY) {
        p->clock += dt_ms;
        catch_up(p);
    }
    pump(p);
}

void mp_key(morph_play_t *p, uint8_t key) {
    if (p->phase != MP_PLAY || key > MK_JUMP) return;
    mb_key(&p->me, p->L, key);
    p->state_due = true;
    pump(p);
}
