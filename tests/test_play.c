// test_play.c —— 一局和联机:握手和版本、关卡时钟对齐、各算各的人跟得上(真人手速、延迟抖动、卡一下)、
// 一个人试走、断线续上、对方重启、旧的 / 将来的 'B'、乱消息不崩。
// 「停手后两边一致」按 Ruling 1:停手 1 秒后一直跑到四个人(两边的 me、peer)都停在坑边或墙边再比 6 样全等;
// 还在走的时候只查 |x 差| ≤ (|两边 clock 差| / 10 + 1) 像素。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "duo.h"
#include "morph_levels.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const morph_level_t *L = &morph_level_test;

static bool same(const morph_body_t *a, const morph_body_t *b) {
    return a->x == b->x && a->y == b->y && a->vy == b->vy && a->dir == b->dir && a->st == b->st &&
           a->flaps == b->flaps;
}

static bool stopped(const morph_body_t *b) { return b->st == MB_EDGE || b->st == MB_WALL; }

static uint32_t clock_diff(void) {
    return D.a.clock > D.b.clock ? D.a.clock - D.b.clock : D.b.clock - D.a.clock;
}

// 先设好延迟再连上,跑到两边都 MP_PLAY(最多 1 秒)
static void begin(uint32_t delay, uint32_t jitter) {
    duo_init();
    duo_delay(delay, jitter);
    duo_connect();
    for (int t = 0; D.a.phase != MP_PLAY || D.b.phase != MP_PLAY; t += 10) {
        if (t >= 1000) { CHECK(!"两边 1 秒内没进 MP_PLAY"); return; }
        duo_step();
    }
}

// 真人节奏:每边下一次按键的时刻,间隔 200–1500 ms(一秒不超过 5 下),▲ 两成 ▼ 一成的比例
static uint32_t next_key[2];

static void humans_start(void) {
    for (int s = 0; s < 2; s++) next_key[s] = D.now + 200 + test_rnd() % 1301;
}

static void humans(void) {
    for (int s = 0; s < 2; s++) {
        if (D.now < next_key[s]) continue;
        press(s, test_rnd() % 3 ? MK_JUMP : MK_TURN);
        next_key[s] = D.now + 200 + test_rnd() % 1301;
    }
}

// x 的宽松界(Ruling 1):|x 差| ≤ (|两边 clock 差| / 10 + 1) 像素。两边差着这么多步,所以只有「这么多步之内
// 有一个要掉出关卡底」的时候不查:落后的那个还在往坑里掉,领先的那个已经冒出来了,x 差一个坑宽。
static int lag_steps(void) { return (int)(clock_diff() / 10 + 1); }

static bool pit_soon(const morph_body_t *b) {
    morph_body_t c = *b;
    for (int i = 0; i < lag_steps(); i++)
        if (mb_step(&c, L) & MB_EV_PIT) return true;
    return false;
}

static bool x_ok(const morph_body_t *p, const morph_body_t *m) {
    if (pit_soon(p) || pit_soon(m)) return true;
    int32_t d = p->x - m->x, lim = lag_steps() * MB_SUB;
    return d <= lim && d >= -lim;
}

// 这一步超界的对数(a.peer↔b.me、b.peer↔a.me)
static int x_bad(void) { return !x_ok(&D.a.peer, &D.b.me) + !x_ok(&D.b.peer, &D.a.me); }

// 两边都停手(调用的人已经停了):先跑 1 秒,再一直跑到四个人都停在坑边或墙边(最多 15 秒),再跑 1 秒
// 让路上的消息都到,比 6 样全等
static void settle(void) {
    run(1000);
    int bad_x = 0, t = 0;
    for (; t < 15000; t += 10) {
        if (stopped(&D.a.me) && stopped(&D.b.me) && stopped(&D.a.peer) && stopped(&D.b.peer)) break;
        duo_step();
        bad_x += x_bad();
    }
    CHECK(t < 15000);
    CHECK(bad_x == 0);
    run(1000);
    CHECK(D.a.peer_ok && D.b.peer_ok);
    CHECK(same(&D.a.peer, &D.b.me));
    CHECK(same(&D.b.peer, &D.a.me));
    if (!same(&D.a.peer, &D.b.me) || !same(&D.b.peer, &D.a.me))
        printf("  a.me x%d y%d st%d  b.peer x%d y%d st%d | b.me x%d y%d st%d  a.peer x%d y%d st%d\n", (int)D.a.me.x,
               (int)D.a.me.y, D.a.me.st, (int)D.b.peer.x, (int)D.b.peer.y, D.b.peer.st, (int)D.b.me.x, (int)D.b.me.y,
               D.b.me.st, (int)D.a.peer.x, (int)D.a.peer.y, D.a.peer.st);
}

// 'B' 照线上的格式自己打包(不用实现里的)
static size_t pack_b(uint8_t *m, uint8_t seq, uint32_t at_ms, const morph_body_t *b) {
    uint32_t t = at_ms / 10, x = (uint32_t)b->x;
    uint16_t y = (uint16_t)b->y, vy = (uint16_t)b->vy;
    uint16_t bits = (uint16_t)((b->dir > 0 ? 1u : 0u) | (unsigned)b->st << 1 | (unsigned)b->flaps << 3);
    uint8_t v[MP_B_LEN] = { 'B', seq, (uint8_t)t, (uint8_t)(t >> 8), (uint8_t)(t >> 16),
                            (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)(x >> 16), (uint8_t)y, (uint8_t)(y >> 8),
                            (uint8_t)vy, (uint8_t)(vy >> 8), (uint8_t)bits, (uint8_t)(bits >> 8) };
    memcpy(m, v, sizeof v);
    return sizeof v;
}

static void t_hello(void) {
    begin(70, 20);
    CHECK(D.a.phase == MP_PLAY && D.b.phase == MP_PLAY);
    CHECK(!D.a.solo && !D.b.solo);

    duo_init();
    D.b.proto = 2;
    duo_delay(70, 20);
    duo_connect();
    run(2000);
    CHECK(D.a.phase == MP_WAIT && D.b.phase == MP_WAIT);
    CHECK(D.a.clock == 0 && D.b.clock == 0);
    CHECK(D.ab.msgs['G'] == 0);

    // a 那边的链路一开始一条都排不进:'H' 'G' 被拒也留着,能排了按顺序交
    duo_init();
    D.ab.cap = 0;
    duo_delay(70, 0);
    duo_connect();
    run(500);
    CHECK(D.a.refused > 0 && D.ab.msgs['H'] == 0);
    CHECK(D.b.phase == MP_WAIT);
    D.ab.cap = 8;
    run(1000);
    CHECK(D.ab.msgs['H'] == 1 && D.ab.msgs['G'] == 1);
    CHECK(D.a.phase == MP_PLAY && D.b.phase == MP_PLAY);
}

static void t_clock(void) {
    test_seed(11);
    begin(70, 20);
    run(5000);
    printf("  t_clock: |a.clock - b.clock| = %u ms, rtt a %u b %u\n", (unsigned)clock_diff(), D.a.rtt, D.b.rtt);
    CHECK(clock_diff() <= 15);
    CHECK(D.b.rtt > 0 && D.a.rtt > 0);
}

static void t_follow(void) {
    test_seed(23);
    begin(140, 40);
    humans_start();
    uint32_t tx0[2] = { D.a.tx, D.b.tx }, max_ps[2] = { 0, 0 }, max_off = 0;
    for (int t = 10; t <= 60000; t += 10) {
        humans();
        if (t % 7000 == 0) duo_stall(t % 14000 ? &D.ab : &D.ba);
        duo_step();
        if (t >= 3000 && clock_diff() > max_off) max_off = clock_diff();
        if (t % 1000 == 0)
            for (int s = 0; s < 2; s++) {
                uint32_t d = side(s)->tx - tx0[s];
                if (d > max_ps[s]) max_ps[s] = d;
                tx0[s] = side(s)->tx;
            }
    }
    printf("  t_follow: tx a %u (%.1f/s, max %u in 1 s) b %u (%.1f/s, max %u); B a->b %u b->a %u; "
           "refused a %u b %u; max clock offset %u ms; rtt a %u b %u; queue max %d / %d\n",
           (unsigned)D.a.tx, D.a.tx / 60.0, (unsigned)max_ps[0], (unsigned)D.b.tx, D.b.tx / 60.0,
           (unsigned)max_ps[1], (unsigned)D.ab.msgs['B'], (unsigned)D.ba.msgs['B'], (unsigned)D.a.refused,
           (unsigned)D.b.refused, (unsigned)max_off, D.a.rtt, D.b.rtt, D.ab.max_n, D.ba.max_n);
    CHECK(D.a.refused == 0 && D.b.refused == 0 && D.ab.refused == 0 && D.ba.refused == 0);
    CHECK(max_ps[0] <= 7 && max_ps[1] <= 7);
    CHECK(D.ab.msgs['B'] > 60 && D.ba.msgs['B'] > 60);
    settle();
    CHECK(D.a.refused == 0 && D.b.refused == 0);
}

// 刚连上 / 连回来的时候(被连方第一次 ping 回来时钟往前跳 200 多 ms):不按键的这段里每一步都在 x 宽松界之内。
// 按着键的时候查不了这条界:对方的键在路上那一个单程里两边本来就差着。所以按一阵、断开、连回来、停手,
// 从这次连接收到第一条 'B' 起查 8 秒。
static int jump_bad;
static int32_t jump_max_dx;   // 查的那些步里 |x 差| 最大多少(1/16 像素)
static uint32_t jump_max_cd;  // 两边 clock 最大差

static void connect_window(void) {
    int t = 0;
    while (t < 8000 && (!D.a.rx_seq || !D.b.rx_seq)) { duo_step(); t += 10; }
    CHECK(D.a.rx_seq && D.b.rx_seq);
    for (; t < 8000; t += 10) {
        duo_step();
        jump_bad += x_bad();
        if (clock_diff() > jump_max_cd) jump_max_cd = clock_diff();
        if (!pit_soon(&D.a.peer) && !pit_soon(&D.b.me) && abs(D.a.peer.x - D.b.me.x) > jump_max_dx)
            jump_max_dx = abs(D.a.peer.x - D.b.me.x);
        if (!pit_soon(&D.b.peer) && !pit_soon(&D.a.me) && abs(D.b.peer.x - D.a.me.x) > jump_max_dx)
            jump_max_dx = abs(D.b.peer.x - D.a.me.x);
    }
}

static void t_jump(void) {
    jump_bad = 0;
    jump_max_dx = 0;
    jump_max_cd = 0;
    for (uint32_t seed = 100; seed < 140; seed++) {
        test_seed(seed);
        begin(140, 40);
        connect_window();   // 刚连上
        humans_start();
        for (int t = 0; t < 6000; t += 10) { humans(); duo_step(); }
        duo_drop();
        run(1000);
        duo_connect();
        connect_window();   // 连回来
    }
    printf("  t_jump: 40 seeds x 2 connects: %d steps over the x bound; max |x diff| %.2f px, max clock diff %u ms\n",
           jump_bad, jump_max_dx / (double)MB_SUB, (unsigned)jump_max_cd);
    CHECK(jump_bad == 0);
}

// 人有事(这里是走到坑边站住)马上要发 'B',不等 2 秒的补发
static void t_events(void) {
    test_seed(17);
    begin(70, 20);
    int t = 0;
    for (; t < 10000 && D.b.me.st != MB_EDGE; t += 10) duo_step();
    CHECK(D.b.me.st == MB_EDGE);
    CHECK(D.b.state_due || D.b.state_at == D.b.now);   // 站住的这一步就该发了(或者已经交出去)
    for (t = 0; t < 500 && (D.ba.last_b[12] >> 1 & 3) != MB_EDGE; t += 10) duo_step();
    CHECK(t < 500);   // 一个来回多一点就到了对面
}

static void t_solo(void) {
    morph_play_t p;
    mp_init(&p, NULL, NULL, L);
    CHECK(p.solo && p.phase == MP_PLAY);
    int32_t x0 = p.me.x;
    for (int i = 0; i < 50; i++) mp_tick(&p, 10);
    CHECK(p.clock == 500);
    CHECK(p.me.x > x0 && p.me.st == MB_WALK);
    mp_key(&p, MK_JUMP);
    CHECK(p.me.st == MB_AIR);
    for (int i = 0; i < 10; i++) mp_tick(&p, 10);
    CHECK(p.me.y < L->spawn_y * MB_SUB);
    mp_key(&p, MK_TURN);
    CHECK(p.me.dir == -1);
    for (int i = 0; i < 200; i++) mp_tick(&p, 10);
    CHECK(!p.peer_ok);
    CHECK(p.tx == 0);
    // 时间按任意步长给也照样每 10 ms 一步
    morph_play_t q;
    mp_init(&q, NULL, NULL, L);
    for (int i = 0; i < 100; i++) mp_tick(&q, 7);
    CHECK(q.clock == 700);
    morph_body_t b;
    mb_init(&b, L->spawn_x, L->spawn_y, 1);
    uint32_t acc = 0;
    mb_run(&b, L, 700, &acc);
    CHECK(same(&q.me, &b));
}

static void t_drop(void) {
    test_seed(31);
    begin(70, 20);
    humans_start();
    for (int t = 0; t < 8000; t += 10) {
        humans();
        duo_step();
    }
    // 等到两边的人都在走(不在空中、没停住)再断
    for (int t = 0; t < 10000 && (D.a.me.st != MB_WALK || D.b.me.st != MB_WALK); t += 10) {
        if (stopped(&D.a.me)) press(0, MK_TURN);
        if (stopped(&D.b.me)) press(1, MK_TURN);
        duo_step();
    }
    CHECK(D.a.me.st == MB_WALK && D.b.me.st == MB_WALK);
    duo_drop();
    CHECK(D.a.phase == MP_PAUSED && D.b.phase == MP_PAUSED);
    morph_play_t a0 = D.a, b0 = D.b;
    run(3000);
    CHECK(D.a.phase == MP_PAUSED && D.b.phase == MP_PAUSED);
    CHECK(D.a.clock == a0.clock && D.b.clock == b0.clock);
    CHECK(same(&D.a.me, &a0.me) && same(&D.b.me, &b0.me));
    CHECK(same(&D.a.peer, &a0.peer) && same(&D.b.peer, &b0.peer));
    duo_connect();
    int t = 0;
    for (; t < 1000 && (D.a.phase != MP_PLAY || D.b.phase != MP_PLAY); t += 10) duo_step();
    CHECK(t < 1000);
    settle();   // 断线时路上的状态都丢了:连回来两边重发的那条要对得上
    // 再按一会儿,再停手比
    humans_start();
    for (int i = 0; i < 5000; i += 10) {
        humans();
        duo_step();
    }
    settle();
}

static void t_restart(void) {
    test_seed(47);
    begin(70, 20);
    humans_start();
    for (int t = 0; t < 10000; t += 10) {
        humans();
        duo_step();
    }
    uint32_t clock = D.a.clock;
    duo_drop();
    mp_init(&D.b, duo_send, &D.ba, L);   // 对方重启
    run(2000);
    duo_connect();
    for (int t = 0; t < 1000 && (D.a.phase != MP_PLAY || D.b.phase != MP_PLAY); t += 10) duo_step();
    CHECK(D.a.phase == MP_PLAY && D.b.phase == MP_PLAY);
    CHECK(D.b.clock >= clock);   // 重启的那边照主动方的时钟接着走
    settle();
    humans_start();
    for (int t = 0; t < 5000; t += 10) {
        humans();
        duo_step();
    }
    settle();
}

static void t_stale(void) {
    test_seed(53);
    begin(70, 20);
    run(1000);
    uint8_t old[MP_MSG_MAX];
    memcpy(old, D.ba.last_b, MP_B_LEN);
    CHECK(old[0] == 'B');
    press(1, MK_JUMP);
    run(1000);
    press(1, MK_TURN);
    run(1000);
    CHECK(D.ba.last_b[1] != old[1]);   // 后来又收到过新的
    morph_body_t peer = D.a.peer;
    uint32_t at = D.a.peer_at;
    mp_on_msg(&D.a, old, MP_B_LEN);
    CHECK(same(&D.a.peer, &peer) && D.a.peer_at == at);
    mp_on_msg(&D.a, D.ba.last_b, MP_B_LEN);   // 同一条再来一次也不要
    CHECK(same(&D.a.peer, &peer) && D.a.peer_at == at);
}

static void t_future(void) {
    test_seed(59);
    begin(70, 20);
    run(1000);
    morph_body_t s;
    mb_init(&s, 120, 224, 1);   // 平地上往右走
    uint8_t m[MP_MSG_MAX];
    pack_b(m, (uint8_t)(D.a.rx_seq + 1), D.a.clock + 200, &s);
    mp_on_msg(&D.a, m, MP_B_LEN);
    CHECK(D.a.peer_ok && same(&D.a.peer, &s));
    // 只推 a(不送 b 的消息):这 200 ms 里一直是那条状态
    for (int i = 0; i < 20; i++) {
        mp_tick(&D.a, 10);
        CHECK(same(&D.a.peer, &s));
    }
    // 之后接着往前算
    for (int i = 0; i < 10; i++) mp_tick(&D.a, 10);
    morph_body_t want = s;
    for (int i = 0; i < 10; i++) mb_step(&want, L);
    CHECK(same(&D.a.peer, &want));
    CHECK(D.a.peer.x > s.x);
}

static void t_garbage(void) {
    test_seed(61);
    begin(70, 20);
    run(1000);
    static const uint8_t types[] = { 'H', 'G', 'Q', 'A', 'B' };
    for (int i = 0; i < 10000; i++) {
        uint8_t m[MP_MSG_MAX];
        size_t n = test_rnd() % (MP_MSG_MAX + 1);
        for (size_t k = 0; k < n; k++) m[k] = (uint8_t)test_rnd();
        if (n && test_rnd() % 4) m[0] = types[test_rnd() % sizeof types];
        mp_on_msg(test_rnd() % 2 ? &D.a : &D.b, m, n);
        if (i % 10 == 0) duo_step();
    }
    run(1000);
    CHECK(D.a.rx > 0 && D.b.rx > 0);
}

int main(void) {
    t_hello(); t_clock(); t_follow(); t_jump(); t_events(); t_solo(); t_drop(); t_restart(); t_stale(); t_future(); t_garbage();
    if (fails) { printf("test_play: %d FAIL\n", fails); return 1; }
    printf("test_play: ok\n");
    return 0;
}
