#include "morph_level.h"

morph_tile_t morph_ml_tile(const morph_level_t *L, int col, int row) {
    if (col < 0 || col >= L->cols) return MT_SOLID;
    if (row < 0 || row >= ML_ROWS) return MT_EMPTY;
    return (morph_tile_t)L->tiles[row * L->cols + col];
}

bool morph_ml_pit(const morph_level_t *L, int col, int foot_row) {
    if (foot_row < 0) foot_row = 0;
    for (int r = foot_row; r < ML_ROWS; r++)
        if (morph_ml_tile(L, col, r) != MT_EMPTY) return false;
    return col >= 0 && col < L->cols;  // 越界的列是墙,不是坑
}
