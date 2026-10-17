// test_body.c —— 人的运动:自动走、掉头、坑边停、墙、台阶、跳、扑腾、单向平台、大步长、确定性。
// 测试关(levels/test.txt):1 格坑 10 列;3 格坑 21-23 列;一格台阶 28-30 列;两格墙 34 列;
// 单向平台 40-43 列(第 5 行,下面是地);出生点 (80, 224)。一格 32 像素,地面顶 y = 224。
#include <stdio.h>
#include <string.h>
#include "morph_body.h"
#include "morph_levels.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const morph_level_t *L = &morph_level_test;
#define PX(v) ((v) * MB_SUB)   // 像素 → 1/16 像素
#define GROUND PX(224)

static unsigned run_ms(morph_body_t *b, const morph_level_t *lv, uint32_t ms) {
    uint32_t acc = 0;
    return mb_run(b, lv, ms, &acc);
}

// 一步一步走,直到返回里有 want 的某一位(最多 max_ms);返回这一路所有事件的组合
static unsigned run_until(morph_body_t *b, const morph_level_t *lv, unsigned want, uint32_t max_ms) {
    unsigned all = 0;
    for (uint32_t t = 0; t < max_ms; t += MB_STEP_MS) {
        unsigned ev = mb_step(b, lv);
        all |= ev;
        if (ev & want) break;
    }
    return all;
}

static void t_walk(void) {
    morph_body_t b;
    mb_init(&b, 80, 224, 1);
    int32_t x0 = b.x;
    run_ms(&b, L, 1000);
    CHECK(b.st == MB_WALK);
    CHECK(b.y == GROUND);
    CHECK(b.x - x0 >= PX(55) && b.x - x0 <= PX(65));
}

static void t_turn(void) {
    morph_body_t b;
    mb_init(&b, 80, 224, 1);
    run_ms(&b, L, 200);
    int32_t xt = b.x;
    mb_key(&b, L, MK_TURN);
    CHECK(b.dir == -1);
    run_ms(&b, L, 500);
    CHECK(b.x < xt);
}

static void t_edge(void) {
    morph_body_t b;
    mb_init(&b, 80, 224, 1);
    unsigned ev = run_until(&b, L, MB_EV_STOP, 10000);
    CHECK(ev & MB_EV_STOP);
    CHECK(b.st == MB_EDGE);
    CHECK(b.y == GROUND);
    CHECK(b.x >= PX(9 * 32) && b.x < PX(10 * 32));                  // 在坑边那格(9 列)之内
    CHECK(b.x + PX(MB_W / 2) - PX(10 * 32) <= PX(MB_W / 2));        // 悬空不超过半个身宽
    int32_t xe = b.x;
    ev = run_ms(&b, L, 2000);
    CHECK(b.x == xe && b.st == MB_EDGE);
    CHECK(!(ev & (MB_EV_PIT | MB_EV_STOP | MB_EV_GO)));
    // 停着的掉头后接着走
    mb_key(&b, L, MK_TURN);
    run_ms(&b, L, 200);
    CHECK(b.st == MB_WALK && b.x < xe);
}

static void t_step_down(void) {
    morph_body_t b;
    mb_init(&b, 29 * 32 + 16, 192, 1);   // 一格台阶顶上,朝右
    unsigned ev = run_until(&b, L, MB_EV_LAND, 3000);
    CHECK(ev & MB_EV_LAND);
    CHECK(!(ev & (MB_EV_STOP | MB_EV_PIT)));
    CHECK(b.y == GROUND && b.st == MB_WALK);
    CHECK(b.x > PX(31 * 32));
}

static void t_wall(void) {
    morph_body_t b;
    // 两格高的墙(34 列)
    mb_init(&b, 31 * 32 + 16, 224, 1);
    unsigned ev = run_until(&b, L, MB_EV_STOP, 5000);
    CHECK(ev & MB_EV_STOP);
    CHECK(b.st == MB_WALL);
    CHECK(b.x + PX(MB_W / 2) == PX(34 * 32));
    run_ms(&b, L, 1000);
    CHECK(b.st == MB_WALL && b.x + PX(MB_W / 2) == PX(34 * 32));
    // 一格高的台阶(28 列)
    mb_init(&b, 25 * 32 + 16, 224, 1);
    ev = run_until(&b, L, MB_EV_STOP, 5000);
    CHECK(ev & MB_EV_STOP);
    CHECK(b.st == MB_WALL);
    CHECK(b.x + PX(MB_W / 2) == PX(28 * 32));
    mb_key(&b, L, MK_JUMP);
    CHECK(b.st == MB_AIR);
    ev = run_until(&b, L, MB_EV_LAND, 3000);
    CHECK(ev & MB_EV_LAND);
    CHECK(b.y == PX(192) && b.st == MB_WALK);   // 站上台阶
}

// 从坑边起跳,flap_ms > 0 时每隔 flap_ms 扑腾一下、扑满 MB_FLAPS 下;返回落地或掉坑之前所有事件
static unsigned jump_from_edge(morph_body_t *b, uint32_t flap_ms) {
    mb_key(b, L, MK_JUMP);
    unsigned ev = 0;
    if (flap_ms)
        for (int i = 0; i < MB_FLAPS; i++) {
            ev |= run_ms(b, L, flap_ms);
            mb_key(b, L, MK_JUMP);
        }
    return ev | run_until(b, L, MB_EV_LAND | MB_EV_PIT, 15000);
}

static void t_jump_pits(void) {
    morph_body_t b;
    // 一格坑:不扑腾过得去
    mb_init(&b, 80, 224, 1);
    run_until(&b, L, MB_EV_STOP, 10000);
    CHECK(b.st == MB_EDGE);
    unsigned ev = jump_from_edge(&b, 0);
    CHECK(ev & MB_EV_LAND);
    CHECK(!(ev & MB_EV_PIT));
    CHECK(b.y == GROUND && b.x > PX(11 * 32 - MB_W / 2));

    // 三格坑:不扑腾过不去,掉下去回到坑边
    mb_init(&b, 18 * 32 + 16, 224, 1);
    run_until(&b, L, MB_EV_STOP, 10000);
    CHECK(b.st == MB_EDGE);
    int32_t ex = b.x, ey = b.y;
    ev = jump_from_edge(&b, 0);
    CHECK(ev & MB_EV_PIT);
    CHECK(!(ev & MB_EV_LAND));
    CHECK(b.x == ex && b.y == ey && b.x == b.safe_x && b.y == b.safe_y);
    CHECK(b.dir == 1 && b.vy == 0 && b.flaps == 0);
    CHECK(b.st != MB_AIR);

    // 三格坑:每隔 250 ms 扑腾,扑满 5 下 → 过得去
    mb_init(&b, 18 * 32 + 16, 224, 1);
    run_until(&b, L, MB_EV_STOP, 10000);
    ev = jump_from_edge(&b, 250);
    CHECK(ev & MB_EV_LAND);
    CHECK(!(ev & MB_EV_PIT));
    CHECK(b.y == GROUND && b.x > PX(24 * 32 - MB_W / 2));
}

static void t_flap_cap(void) {
    morph_body_t b;
    mb_init(&b, 80, 224, 1);
    mb_key(&b, L, MK_JUMP);
    for (int i = 0; i < 8; i++) {
        run_ms(&b, L, 100);
        CHECK(b.st == MB_AIR);
        int16_t vy0 = b.vy;
        mb_key(&b, L, MK_JUMP);
        if (i < MB_FLAPS) {
            CHECK(b.flaps == i + 1);
            CHECK(b.vy < 0);
        } else {
            CHECK(b.flaps == MB_FLAPS);
            CHECK(b.vy == vy0);   // 第 6 下以后不算
        }
    }
    CHECK(b.flaps == MB_FLAPS);
}

static void t_platform(void) {
    morph_body_t b;
    mb_init(&b, 40 * 32 + 16, 224, 1);   // 单向平台(40-43 列,顶 y = 160)正下方
    mb_key(&b, L, MK_JUMP);
    unsigned ev = run_until(&b, L, MB_EV_LAND, 3000);
    CHECK(ev & MB_EV_LAND);
    CHECK(b.y == PX(160) && b.st == MB_WALK);
    // 站稳一会儿还在平台上
    run_ms(&b, L, 100);
    CHECK(b.y == PX(160) && b.st == MB_WALK);
}

static void t_big_dt(void) {
    morph_body_t a, b;
    uint32_t acc_a = 0, acc_b = 0;
    mb_init(&a, 80, 224, 1);
    mb_key(&a, L, MK_JUMP);
    b = a;
    mb_run(&a, L, 100, &acc_a);
    for (int i = 0; i < 10; i++) mb_run(&b, L, 10, &acc_b);
    CHECK(memcmp(&a, &b, sizeof a) == 0);
    CHECK(acc_a == 0 && acc_b == 0);
    // 不整 10 ms 的余数留在 acc 里
    mb_run(&a, L, 25, &acc_a);
    CHECK(acc_a == 5);
    mb_run(&a, L, 5, &acc_a);
    CHECK(acc_a == 0);
    for (int i = 0; i < 3; i++) mb_run(&b, L, 10, &acc_b);
    CHECK(memcmp(&a, &b, sizeof a) == 0);

    // 在最高处往下落,一次 100 ms 也停在地面上
    mb_init(&a, 80, 224, 1);
    mb_key(&a, L, MK_JUMP);
    for (int i = 0; i < 300 && a.vy < 0; i++) mb_step(&a, L);
    CHECK(a.vy >= 0 && a.st == MB_AIR);
    acc_a = 0;
    unsigned ev = 0;
    for (int i = 0; i < 20 && a.st == MB_AIR; i++) {
        ev |= mb_run(&a, L, 100, &acc_a);
        CHECK(a.y <= GROUND);
    }
    CHECK(ev & MB_EV_LAND);
    CHECK(a.y == GROUND && a.st == MB_WALK);
}

#define DET_STEPS (30000 / MB_STEP_MS)
static morph_body_t det_log[2][DET_STEPS];

static void det_run(morph_body_t *log) {
    uint32_t seed = 12345u;
    morph_body_t b;
    mb_init(&b, 80, 224, 1);
    uint32_t next = 0;
    for (int i = 0; i < DET_STEPS; i++) {
        uint32_t t = (uint32_t)i * MB_STEP_MS;
        if (t >= next) {
            seed = seed * 1103515245u + 12345u;
            mb_key(&b, L, (seed >> 16) & 1 ? MK_JUMP : MK_TURN);
            seed = seed * 1103515245u + 12345u;
            next = t + 200 + (seed >> 16) % 401;
        }
        mb_step(&b, L);
        log[i] = b;
    }
}

static void t_determinism(void) {
    det_run(det_log[0]);
    det_run(det_log[1]);
    int same = 1;
    for (int i = 0; i < DET_STEPS; i++)
        if (memcmp(&det_log[0][i], &det_log[1][i], sizeof(morph_body_t)) != 0) same = 0;
    CHECK(same);
    // 人一直留在关卡里(左右两头是墙)
    for (int i = 0; i < DET_STEPS; i++)
        if (det_log[0][i].x < PX(32) || det_log[0][i].x > PX(59 * 32)) { CHECK(0); break; }
}

// 前面的东西没了(计划 2 的冰块会变)→ 接着走,返回 MB_EV_GO
static void t_go(void) {
    static uint8_t tiles[ML_ROWS * 60];
    memcpy(tiles, morph_level_test.tiles, sizeof tiles);
    morph_level_t lv = morph_level_test;
    lv.tiles = tiles;
    morph_body_t b;
    mb_init(&b, 80, 224, 1);
    run_until(&b, &lv, MB_EV_STOP, 10000);
    CHECK(b.st == MB_EDGE);
    tiles[7 * 60 + 10] = MT_SOLID;   // 坑填上了
    unsigned ev = mb_step(&b, &lv);
    CHECK(ev & MB_EV_GO);
    CHECK(b.st == MB_WALK);
    tiles[6 * 60 + 13] = MT_SOLID;   // 前面多了一块
    ev = run_until(&b, &lv, MB_EV_STOP, 5000);
    CHECK(b.st == MB_WALL);
    tiles[6 * 60 + 13] = MT_EMPTY;   // 又没了
    ev = mb_step(&b, &lv);
    CHECK(ev & MB_EV_GO);
    CHECK(b.st == MB_WALK);
}

int main(void) {
    t_walk(); t_turn(); t_edge(); t_step_down(); t_wall(); t_jump_pits();
    t_flap_cap(); t_platform(); t_big_dt(); t_determinism(); t_go();
    if (fails) { printf("test_body: %d FAIL\n", fails); return 1; }
    printf("test_body: ok\n");
    return 0;
}
