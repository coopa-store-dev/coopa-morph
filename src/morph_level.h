// morph_level.h —— 关卡格子(纯 C,宿主可测)。世界坐标 = 屏幕像素。
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ML_TILE 32      // 一格多少屏幕像素(16×16 的图放大两倍)
#define ML_ROWS 8       // 关卡高 8 格(256 像素,竖屏中间那块)

typedef enum { MT_EMPTY = 0, MT_SOLID, MT_PLAT } morph_tile_t;  // MT_PLAT:只能从上面站,从下面跳得穿

typedef struct {
    const char *name;
    uint16_t cols;
    const uint8_t *tiles;     // ML_ROWS × cols,一行接一行,第 0 行在最上面
    int16_t spawn_x, spawn_y; // 出生点(脚底中点,像素)
} morph_level_t;

// 左右越界 = MT_SOLID(关卡两头是墙);上面越界、下面越界 = MT_EMPTY
morph_tile_t morph_ml_tile(const morph_level_t *L, int col, int row);
// 这一列从 foot_row 往下一直到底都没有能站的格子(实心和单向平台都能站)
bool morph_ml_pit(const morph_level_t *L, int col, int foot_row);
