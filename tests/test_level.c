// test_level.c —— 关卡格子:越界、坑、出生点。
#include <stdio.h>
#include "morph_level.h"
#include "morph_levels.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void t_bounds(void) {
    const morph_level_t *L = &morph_level_test;
    CHECK(morph_ml_tile(L, -1, 3) == MT_SOLID);
    CHECK(morph_ml_tile(L, L->cols, 3) == MT_SOLID);
    CHECK(morph_ml_tile(L, 5, -1) == MT_EMPTY);
    CHECK(morph_ml_tile(L, 5, ML_ROWS) == MT_EMPTY);
}

static void t_pit(void) {
    const morph_level_t *L = &morph_level_test;
    // 一、二、三格宽的坑,每一列都是坑
    for (int c = 10; c <= 10; c++) CHECK(morph_ml_pit(L, c, 6));
    for (int c = 15; c <= 16; c++) CHECK(morph_ml_pit(L, c, 6));
    for (int c = 21; c <= 23; c++) CHECK(morph_ml_pit(L, c, 6));
    // 坑旁边的平地、台阶往下那一列不是坑
    CHECK(!morph_ml_pit(L, 9, 6));
    CHECK(!morph_ml_pit(L, 11, 6));
    CHECK(!morph_ml_pit(L, 31, 5));   // 一格台阶(28-30 列)往下一列,脚下是地面
    CHECK(!morph_ml_pit(L, 29, 5));   // 台阶顶上
    // 单向平台(48-50 列在坑上):站在平台上不是坑;掉到平台下面才是坑
    for (int c = 48; c <= 50; c++) {
        CHECK(morph_ml_tile(L, c, 7) == MT_EMPTY);
        CHECK(!morph_ml_pit(L, c, 4));
        CHECK(morph_ml_pit(L, c, 6));
    }
}

static void t_spawn(void) {
    const morph_level_t *L = &morph_level_test;
    CHECK(L->spawn_y % ML_TILE == 0);
    CHECK(morph_ml_tile(L, L->spawn_x / ML_TILE, L->spawn_y / ML_TILE) == MT_SOLID);
    CHECK(morph_ml_tile(L, L->spawn_x / ML_TILE, L->spawn_y / ML_TILE - 1) == MT_EMPTY);
}

int main(void) {
    t_bounds(); t_pit(); t_spawn();
    if (fails) { printf("test_level: %d FAIL\n", fails); return 1; }
    printf("test_level: ok\n");
    return 0;
}
