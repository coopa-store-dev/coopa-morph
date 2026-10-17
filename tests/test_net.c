// tests/test_net.c —— 两份 morph_net_t 经假链路一起打:链路照 kit_link(停等:一条到了、确认回来下一条才出发;
// 排着最多 8 条,满了 send 返回 false;断线时排着的都没了)。两边各有一个按真人手速乱按的「人」。
// 查:两边算出来的每一步一样(同一步的校验一样、'C' 对出来不一样的次数是 0)、能打完一趟、断线 3 秒连回来接着算。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "morph_net.h"

typedef struct { uint8_t m[MN_MSG_MAX]; size_t len; uint32_t at; } pkt_t;
typedef struct { pkt_t q[8]; int n; uint32_t ready; uint32_t delay, jitter; } pipe_t;
static pipe_t ab, ba;
static uint32_t now, rs = 99;
static morph_net_t A, B;
static uint32_t rr(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

static bool psend(void *ctx, const void *m, size_t len) {
    pipe_t *p = ctx;
    if (p->n == 8) return false;
    uint32_t one = p->delay + (p->jitter ? rr() % (2 * p->jitter + 1) : 0) - p->jitter;
    uint32_t dep = now > p->ready ? now : p->ready;
    pkt_t *k = &p->q[p->n++];
    memcpy(k->m, m, len); k->len = len; k->at = dep + one;
    p->ready = dep + 2 * one;   // 停等:确认回来才发下一条
    return true;
}
static void deliver(pipe_t *p, morph_net_t *to) {
    while (p->n && p->q[0].at <= now) {
        pkt_t k = p->q[0];
        memmove(p->q, p->q + 1, sizeof(pkt_t) * (size_t)(p->n - 1));
        p->n--;
        morph_mn_on_msg(to, k.m, k.len);
    }
}

typedef struct { uint32_t next, longat; uint8_t lk; } bot_t;
static void bot(morph_net_t *n, bot_t *b) {
    if (b->longat && now >= b->longat) { morph_mn_key(n, b->lk, true); b->longat = 0; }
    if (now < b->next) return;
    b->next = now + 250 + rr() % 250;
    static const uint8_t K[5] = { MBT_UP, MBT_OK, MBT_DOWN, MBT_OK, MBT_OK };
    uint8_t k = K[rr() % 5];
    morph_mn_key(n, k, false);
    if (k != MBT_UP && rr() % 5 == 0) { b->longat = now + 500; b->lk = k; }
}

static uint32_t same_steps, compared, t_start, hitch, last_a, last_t;
static void run_ms(uint32_t ms, bool keys, bot_t *ba_, bot_t *bb) {
    for (uint32_t t = 0; t < ms; t += 17) {
        now += 17;
        if (keys) { bot(&A, ba_); bot(&B, bb); }
        morph_mn_tick(&A, 17);
        morph_mn_tick(&B, 17);
        if (A.phase == MN_PLAY) {   // 画面停住超过 50 ms 算一次卡顿
            if (A.step != last_a) { if (now - last_t > 50 + 17) hitch++; last_a = A.step; last_t = now; }
        } else last_t = now;
        deliver(&ab, &B); deliver(&ba, &A);
        if (A.step == B.step && A.phase == MN_PLAY && B.phase == MN_PLAY) {
            compared++;
            if (morph_mn_crc(&A.run) != morph_mn_crc(&B.run)) same_steps++;   // 不一样的次数
        }
        if (A.run.phase >= RUN_WIN && B.run.phase >= RUN_WIN) return;
    }
}

static void game(uint32_t delay, uint32_t jitter, bool drop) {
    memset(&ab, 0, sizeof(ab)); memset(&ba, 0, sizeof(ba));
    ab.delay = ba.delay = delay; ab.jitter = ba.jitter = jitter;
    morph_mn_init(&A, psend, &ab, 4242);
    morph_mn_init(&B, psend, &ba, 777);
    morph_mn_connected(&A, true);
    morph_mn_connected(&B, false);
    bot_t x = { 0 }, y = { 0 };
    run_ms(500, false, &x, &y);
    assert(A.phase == MN_PICK && B.phase == MN_PICK);
    morph_mn_pick(&B, 0);   // 被连方先选库巴
    run_ms(2000, false, &x, &y);
    assert(A.phase == MN_PLAY && B.phase == MN_PLAY && A.me == 1 && B.me == 0);
    same_steps = compared = hitch = 0; t_start = now; last_a = A.step; last_t = now;
    if (drop) {
        run_ms(60000, true, &x, &y);
        ab.n = ba.n = 0;   // 断线:排着的都没了
        morph_mn_lost(&A); morph_mn_lost(&B);
        uint32_t sa = A.step, sb = B.step;
        run_ms(3000, true, &x, &y);
        assert(A.phase == MN_PAUSED && B.phase == MN_PAUSED && A.step == sa && B.step == sb);
        morph_mn_connected(&A, true); morph_mn_connected(&B, false);
    }
    run_ms(40 * 60 * 1000, true, &x, &y);
    printf("test_net: 单程 %u±%u ms%s:%u 步、比了 %u 次、不一样 %u 次、校验对出不一样 %u/%u 次;"
           "交出去 %u/%u 条、被拒 %u/%u;真时间 %u s 算了 %u s、卡顿 %u 次、来回 %u ms、延迟 %u 步;结果 %u/%u\n",
           delay, jitter, drop ? "、中间断线 3 秒" : "", A.step, compared, same_steps, A.desync, B.desync, A.tx, B.tx,
           A.refused, B.refused, (now - t_start) / 1000, A.step / 100, hitch, A.rtt, A.delay, A.run.phase, B.run.phase);
    assert(compared > 100 && same_steps == 0 && A.desync == 0 && B.desync == 0);
    assert(A.run.phase >= RUN_WIN && A.run.phase == B.run.phase);
}

int main(void) {
    game(35, 10, false);
    game(70, 30, false);
    game(70, 30, true);
    // 选人撞了:两边都选库巴,主动方算
    memset(&ab, 0, sizeof(ab)); memset(&ba, 0, sizeof(ba));
    ab.delay = ba.delay = 50;
    morph_mn_init(&A, psend, &ab, 1); morph_mn_init(&B, psend, &ba, 2);
    morph_mn_connected(&A, true); morph_mn_connected(&B, false);
    bot_t x = { 0 }, y = { 0 };
    run_ms(300, false, &x, &y);
    morph_mn_pick(&A, 0); morph_mn_pick(&B, 0);
    run_ms(2000, false, &x, &y);
    assert(A.me == 0 && B.me == 1 && A.phase == MN_PLAY && B.phase == MN_PLAY);
    // 对方重启过(连回来时不在打)→ 这边 MN_BAD
    morph_mn_lost(&A); morph_mn_lost(&B);
    ab.n = ba.n = 0;
    morph_mn_init(&B, psend, &ba, 2);
    morph_mn_connected(&A, true); morph_mn_connected(&B, false);
    run_ms(1000, false, &x, &y);
    assert(A.phase == MN_BAD);
    // 乱消息不崩
    for (int i = 0; i < 20000; i++) {
        uint8_t m[MN_MSG_MAX];
        size_t len = rr() % (MN_MSG_MAX + 1);
        for (size_t k = 0; k < len; k++) m[k] = (uint8_t)rr();
        if (len) m[0] = "HRSIQC"[rr() % 6];
        morph_mn_on_msg(&B, m, len);
        morph_mn_tick(&B, 17);
    }
    puts("test_net: ok");
    return 0;
}
