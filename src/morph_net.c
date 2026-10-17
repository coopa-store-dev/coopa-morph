// src/morph_net.c —— 见 morph_net.h。
#include "morph_net.h"

#include <string.h>

#define ACC_MAX 120      // 卡住以后最多一口气补算多少毫秒
#define RTT_GUESS 100    // 还没量到来回时当它多少

static void put24(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); }
static uint32_t get24(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }
static void put32(uint8_t *p, uint32_t v) { put24(p, v); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return get24(p) | (uint32_t)p[3] << 24; }

static uint32_t rtt_est(const morph_net_t *n) { return n->rtt ? n->rtt : RTT_GUESS; }
static bool tx(morph_net_t *n, const uint8_t *m, size_t len) {
    if (!n->up || !n->send || n->now_ms < n->free_at) return false;
    if (!n->send(n->ctx, m, len)) { n->refused++; n->free_at = n->now_ms + rtt_est(n) / 2; return false; }
    n->tx++;
    n->last_send_ms = n->now_ms;
    uint32_t busy = rtt_est(n) * 9 / 8;
    n->free_at = n->now_ms + (busy < 30 ? 30 : busy);
    return true;
}

void morph_mn_init(morph_net_t *n, mn_send_fn send, void *ctx, uint32_t seed) {
    memset(n, 0, sizeof(*n));
    n->send = send; n->ctx = ctx; n->seed = seed ? seed : 1;
    n->me = n->peer_role = -1;
    n->phase = MN_IDLE;
    n->delay = MN_DELAY;
}

static bool playing(const morph_net_t *n) { return n->phase == MN_PLAY || n->phase == MN_PAUSED; }

void morph_mn_connected(morph_net_t *n, bool initiator) {
    n->up = true; n->initiator = initiator; n->peer_hello = false;
    n->free_at = 0;
    n->hello_due = true; n->query_due = true;
    if (n->phase == MN_IDLE) n->phase = MN_PICK;
    if (n->phase == MN_PICK && n->me >= 0) n->pick_due = true;
    if (playing(n)) n->phase = MN_PAUSED;   // 等两边对过「收到第几步」再接着算
}

void morph_mn_lost(morph_net_t *n) {
    n->up = false;
    if (n->phase == MN_PLAY) n->phase = MN_PAUSED;
}

static void start_run(morph_net_t *n, uint32_t seed) {
    morph_mr_start(&n->run, 2, seed);
    n->step = 0; n->acc = 0;
    n->front = n->delay; n->peer_front = 0; n->sent_front = 0;
    n->hist_n = 0; n->pend_n = 0; n->unsent = 0; n->rx_seq = 0;
    n->chk_n = 0; n->peer_chk = false;
    n->phase = MN_PLAY;
}

static void maybe_start(morph_net_t *n) {
    if (n->phase != MN_PICK || !n->initiator || n->me < 0 || n->peer_role != 1 - n->me) return;
    n->start_due = true;
    start_run(n, n->seed);
}

void morph_mn_pick(morph_net_t *n, int role) {
    if (n->phase != MN_PICK || n->me >= 0 || role < 0 || role > 1) return;
    if (n->peer_role == role) role = 1 - role;   // 对方已经当了这个
    n->me = (int8_t)role;
    n->pick_due = true;
    maybe_start(n);
}

static void add_pend(morph_net_t *n, uint32_t step, uint8_t p, uint8_t key, bool lp) {
    if (n->pend_n == MN_PEND) return;   // 满了(不该发生:一秒最多几个键、只排 120 ms)
    n->pend[n->pend_n++] = (mn_key_t){ step, p, key, lp };
}

void morph_mn_key(morph_net_t *n, uint8_t key, bool lp) {
    if (n->phase != MN_PLAY || n->me < 0) return;
    uint32_t s = n->step + n->delay;
    if (s < n->front) s = n->front;
    n->front = s;
    mn_key_t k = { s, (uint8_t)n->me, key, lp };
    n->hist[n->hist_n % MN_HIST] = k;
    n->hist_n++;
    if (n->unsent < MN_HIST) n->unsent++;
    add_pend(n, s, k.p, key, lp);
}

uint16_t morph_mn_crc(const morph_run_t *r) {
    uint32_t h = 2166136261u;
#define MIX(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    MIX(r->phase); MIX(r->r); MIX(r->i); MIX(r->choice); MIX(r->walking); MIX(r->walk_t);
    for (int p = 0; p < 2; p++) { MIX(r->carry[p].hp); MIX(r->carry[p].en); MIX(r->carry[p].form); }
    if (r->phase == RUN_BATTLE) {
        const morph_battle_t *b = &r->bt;
        MIX(b->now); MIX(b->rng); MIX(b->combo);
        for (int p = 0; p < b->nh; p++) { MIX((int32_t)(b->h[p].x * 16)); MIX(b->h[p].hp); MIX(b->h[p].en); MIX(b->h[p].st); MIX(b->h[p].form); }
        for (int i = 0; i < MBT_MAXF; i++) if (b->f[i].used) { MIX(b->f[i].type); MIX((int32_t)(b->f[i].x * 16)); MIX(b->f[i].hp); MIX(b->f[i].st); }
    }
#undef MIX
    return (uint16_t)(h ^ (h >> 16));
}

static void compare(morph_net_t *n) {
    if (!n->peer_chk) return;
    for (int i = 0; i < 4; i++)
        if (n->chk_step[i] == n->peer_chk_step && n->chk_step[i]) {
            if (n->chk_crc[i] != n->peer_chk_crc) n->desync++;
            n->peer_chk = false;
            return;
        }
}

void morph_mn_on_msg(morph_net_t *n, const uint8_t *m, size_t len) {
    if (!len) return;
    n->rx++;
    switch (m[0]) {
        case 'H':
            if (len < 2 || m[1] != MN_PROTO) { n->phase = MN_BAD; return; }
            n->peer_hello = true;
            break;
        case 'R':
            if (len < 2 || m[1] > 1 || n->phase != MN_PICK) return;
            n->peer_role = (int8_t)m[1];
            if (n->me < 0 || (n->me == n->peer_role && !n->initiator)) {   // 没选的、撞了的被连方:当另一个
                n->me = (int8_t)(1 - n->peer_role);
                n->pick_due = true;
            }
            maybe_start(n);
            break;
        case 'S':
            if (len < 6 || n->initiator || m[5] > 1) return;
            if (n->phase == MN_PICK || n->phase == MN_IDLE) {
                n->me = (int8_t)(1 - m[5]);
                n->peer_role = (int8_t)m[5];
                start_run(n, get32(m + 1));
            }
            break;
        case 'Q': {
            if (len < 5) return;
            bool peer_playing = m[4] != 0;
            if (playing(n) && !peer_playing) { n->phase = MN_BAD; return; }   // 对方重启过
            if (!playing(n)) return;
            uint32_t from = get24(m + 1);   // 对方收到我 < from 的全部键了
            uint8_t k = 0;
            for (uint16_t i = 0; i < n->hist_n && i < MN_HIST; i++) {
                const mn_key_t *h = &n->hist[(uint16_t)(n->hist_n - 1 - i) % MN_HIST];
                if (h->step < from) break;
                k++;
            }
            n->unsent = k;               // 那以后的键重发(序号让对方丢掉重复的)
            n->sent_front = from;
            if (n->phase == MN_PAUSED && n->up) n->phase = MN_PLAY;
            break;
        }
        case 'I': {
            if (len < 8 || !playing(n)) return;
            uint32_t f = get24(m + 1);
            uint8_t seq = m[4];
            n->peer_mnum = m[5]; n->peer_mnum_at = n->now_ms;
            uint32_t at = n->sent_at[m[6]], held = (uint32_t)m[7] * 4;   // 对方收到的我最新那条:量一次来回(扣掉对方压着的)
            if (at && n->now_ms >= at + held) {
                uint32_t sample = n->now_ms - at - held;
                if (sample > 2000) sample = 2000;
                n->rtt = (uint16_t)(n->rtt ? (n->rtt * 3 + sample) / 4 : sample);
                n->sent_at[m[6]] = 0;
            }
            int cnt = (int)(len - 8) / 2;
            for (int i = 0; i < cnt; i++, seq++) {
                if ((int8_t)(seq - n->rx_seq) < 0) continue;   // 收过了
                uint32_t s = f - m[8 + 2 * i];
                uint8_t code = m[9 + 2 * i];
                if (s >= n->step) add_pend(n, s, (uint8_t)(1 - n->me), code & 3, (code & 4) != 0);
                n->rx_seq = (uint8_t)(seq + 1);
            }
            if (f > n->peer_front) n->peer_front = f;
            break;
        }
        case 'C':
            if (len < 6) return;
            n->peer_chk_step = get24(m + 1);
            n->peer_chk_crc = (uint16_t)(m[4] | m[5] << 8);
            n->peer_chk = true;
            compare(n);
            break;
        default: break;
    }
}

static void apply_keys(morph_net_t *n) {
    for (int p = 0; p < 2; p++)   // 同一步先库巴、再爸爸;同一个人按排进来的先后
        for (uint8_t i = 0; i < n->pend_n; i++)
            if (n->pend[i].step == n->step && n->pend[i].p == p) morph_mr_input(&n->run, p, n->pend[i].key, n->pend[i].lp);
    uint8_t w = 0;
    for (uint8_t i = 0; i < n->pend_n; i++)
        if (n->pend[i].step > n->step) n->pend[w++] = n->pend[i];
    n->pend_n = w;
}

static void flush(morph_net_t *n) {
    uint8_t m[MN_MSG_MAX];
    if (n->hello_due) { m[0] = 'H'; m[1] = MN_PROTO; if (tx(n, m, 2)) n->hello_due = false; else return; }
    if (n->query_due) {
        m[0] = 'Q'; put24(m + 1, playing(n) ? n->peer_front : 0); m[4] = playing(n);
        if (tx(n, m, 5)) n->query_due = false; else return;
    }
    if (n->pick_due && n->me >= 0) { m[0] = 'R'; m[1] = (uint8_t)n->me; if (tx(n, m, 2)) n->pick_due = false; else return; }
    if (n->start_due) {
        m[0] = 'S'; put32(m + 1, n->seed); m[5] = (uint8_t)n->me;
        if (tx(n, m, 6)) n->start_due = false; else return;
    }
    if (n->phase != MN_PLAY) return;
    if (n->chk_due) {
        uint8_t i = (uint8_t)((n->chk_n + 3) % 4);
        m[0] = 'C'; put24(m + 1, n->chk_step[i]); m[4] = (uint8_t)n->chk_crc[i]; m[5] = (uint8_t)(n->chk_crc[i] >> 8);
        if (tx(n, m, 6)) n->chk_due = false; else return;
    }
    if (!n->unsent && n->front == n->sent_front) return;
    if (n->now_ms < n->free_at) return;
    // 带最老的几个没发的键;进度线不能越过还没带上的键,也不能离带上的第一个键太远(往回几步只有一个字节)
    uint8_t k = n->unsent > 4 ? 4 : n->unsent;
    uint16_t first = (uint16_t)(n->hist_n - n->unsent);
    uint32_t f = n->front;
    if (n->unsent > k) { uint32_t s = n->hist[(uint16_t)(first + k) % MN_HIST].step; if (s < f) f = s; }
    if (k) { uint32_t lim = n->hist[first % MN_HIST].step + 255; if (lim < f) f = lim; }
    m[0] = 'I'; put24(m + 1, f); m[4] = (uint8_t)first;
    uint8_t num = (uint8_t)(n->mnum + 1);
    uint32_t held = (n->now_ms - n->peer_mnum_at) / 4;
    m[5] = num; m[6] = n->peer_mnum; m[7] = (uint8_t)(held > 255 ? 255 : held);
    for (uint8_t i = 0; i < k; i++) {
        const mn_key_t *h = &n->hist[(uint16_t)(first + i) % MN_HIST];
        m[8 + 2 * i] = (uint8_t)(f - h->step);
        m[9 + 2 * i] = (uint8_t)(h->key | (h->lp ? 4 : 0));
    }
    uint32_t t0 = n->now_ms;
    if (tx(n, m, 8 + 2 * (size_t)k)) {
        n->unsent = (uint8_t)(n->unsent - k); n->sent_front = f;
        n->mnum = num; n->sent_at[num] = t0 ? t0 : 1;
    }
}

void morph_mn_tick(morph_net_t *n, uint32_t ms) {
    n->now_ms += ms;
    if (n->phase == MN_PLAY) {
        uint32_t d = (rtt_est(n) * 2 + 20) / MBT_STEP;   // 自己的键晚多少步:对方收到进度线最慢要「等链路空(一个来回)+ 单程」
        n->delay = (uint8_t)(d < 8 ? 8 : d > 30 ? 30 : d);
        if (n->step + n->delay > n->front) n->front = n->step + n->delay;
        // 快追上对方的进度线了就放慢一点(慢慢走比走走停停看着顺)
        uint32_t lim0 = n->front < n->peer_front ? n->front : n->peer_front;
        n->acc += (int32_t)(lim0 > n->step + 3 ? ms : ms * 2 / 3);
        while (n->acc >= MBT_STEP) {
            uint32_t lim = n->front < n->peer_front ? n->front : n->peer_front;
            if (n->step >= lim) break;
            apply_keys(n);
            morph_mr_tick(&n->run, MBT_STEP);
            n->step++;
            n->acc -= MBT_STEP;
            if (n->step % MN_CHECK_STEPS == 0) {
                uint8_t i = (uint8_t)(n->chk_n % 4);
                n->chk_step[i] = n->step; n->chk_crc[i] = morph_mn_crc(&n->run);
                n->chk_n++; n->chk_due = true;
                compare(n);
            }
            if (n->step + n->delay > n->front) n->front = n->step + n->delay;
        }
        if (n->acc >= MBT_STEP) n->stall_ms += ms;   // 等对方的键
        if (n->acc > ACC_MAX) n->acc = ACC_MAX;
    }
    flush(n);
}
