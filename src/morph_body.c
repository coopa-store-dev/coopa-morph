// morph_body.c —— 一个人的运动。全整数(1/16 像素、10 ms 一步),两张卡算出来一模一样。
#include <string.h>
#include "morph_body.h"

// 数值(1/16 像素每步 / 每步²;一步 10 ms):
#define WALK_V    10   // 62.5 像素/秒(60 不是整数格;约 3.8 秒一屏)。空中水平速度也是它
#define GRAV       2   // 1250 像素/秒²
#define JUMP_V    72   // 450 像素/秒,最高约 79 像素
#define FLAP_V    35   // 218.75 像素/秒
#define FALL_MAX  48   // 300 像素/秒(没扑腾过)
#define FLOAT_MAX 14   // 87.5 像素/秒(扑腾过,飘)

#define T  (ML_TILE * MB_SUB)   // 一格多少 1/16 像素
#define HW (MB_W / 2 * MB_SUB)  // 半个身宽
#define BH (MB_H * MB_SUB)      // 身高

static int fdiv(int32_t a) { return (int)(a >= 0 ? a / T : -((-a + T - 1) / T)); }  // 向下取整的 a / T

static bool grounded(uint8_t st) { return st != MB_AIR; }

// 身子在 x 时盖住的列
static int col_lo(int32_t x) { return fdiv(x - HW); }
static int col_hi(int32_t x) { return fdiv(x + HW - 1); }

// 身子在 x 时,row 这一行盖住的列里有没有实心(want_plat:单向平台也算)
static bool row_has(const morph_level_t *L, int32_t x, int row, bool want_plat) {
    for (int c = col_lo(x); c <= col_hi(x); c++) {
        morph_tile_t t = morph_ml_tile(L, c, row);
        if (t == MT_SOLID || (want_plat && t == MT_PLAT)) return true;
    }
    return false;
}

// 脚底 y 时,身子高度内 col 这一列有没有实心
static bool col_blocked(const morph_level_t *L, int col, int32_t y) {
    for (int r = fdiv(y - BH); r <= fdiv(y - 1); r++)
        if (morph_ml_tile(L, col, r) == MT_SOLID) return true;
    return false;
}

// 按 dir 往前挪 WALK_V。前面那列是墙(或 pit_check 时是坑)→ 停在那列边上,返回 MB_WALL / MB_EDGE;否则 MB_WALK。
static uint8_t advance(morph_body_t *b, const morph_level_t *L, bool pit_check) {
    int32_t nx = b->x + b->dir * WALK_V;
    int fc = b->dir > 0 ? col_hi(nx) : col_lo(nx);
    uint8_t st = MB_WALK;
    if (col_blocked(L, fc, b->y)) st = MB_WALL;
    else if (pit_check && morph_ml_pit(L, fc, fdiv(b->y))) st = MB_EDGE;
    if (st != MB_WALK) {   // 贴到那列边上,不往回退
        if (b->dir > 0) { int32_t e = (int32_t)fc * T - HW;       if (nx > e) nx = e; if (nx < b->x) nx = b->x; }
        else            { int32_t e = (int32_t)(fc + 1) * T + HW; if (nx < e) nx = e; if (nx > b->x) nx = b->x; }
    }
    b->x = nx;
    return st;
}

void morph_mb_init(morph_body_t *b, int x_px, int y_px, int8_t dir) {
    memset(b, 0, sizeof *b);   // 连填充字节一起清零,两边整块比较才一样
    b->x = x_px * MB_SUB;
    b->y = y_px * MB_SUB;
    b->dir = dir;
    b->st = MB_WALK;
}

void morph_mb_key(morph_body_t *b, const morph_level_t *L, uint8_t key) {
    (void)L;
    if (key == MK_TURN) {
        b->dir = (int8_t)-b->dir;
        if (b->st == MB_EDGE || b->st == MB_WALL) b->st = MB_WALK;
    } else if (key == MK_JUMP) {
        if (grounded(b->st)) {
            b->st = MB_AIR;
            b->vy = -JUMP_V;
            b->flaps = 0;
        } else if (b->flaps < MB_FLAPS) {
            b->flaps++;
            if (b->vy > -FLAP_V) b->vy = -FLAP_V;
        }
    }
}

static unsigned step_ground(morph_body_t *b, const morph_level_t *L) {
    uint8_t was = b->st;
    b->st = advance(b, L, true);
    if (was == MB_WALK && b->st != MB_WALK) return MB_EV_STOP;
    if (was != MB_WALK && b->st == MB_WALK) return MB_EV_GO;
    return 0;
}

// 只看 x、dir、关卡:从 x 那列往 dir 的反方向找第一列不是坑的,站到这列最上面那块(实心或平台)的顶上,
// 贴着坑那一边(dir 那边)不悬空。关卡两头是墙,找不到就停在最边上那列。
static void respawn(morph_body_t *b, const morph_level_t *L) {
    int c = fdiv(b->x);
    if (c < 0) c = 0;
    if (c >= L->cols) c = L->cols - 1;
    while (morph_ml_pit(L, c, 0) && c - b->dir >= 0 && c - b->dir < L->cols) c -= b->dir;
    int r = 0;
    while (r < ML_ROWS - 1 && morph_ml_tile(L, c, r) == MT_EMPTY) r++;
    b->x = b->dir > 0 ? (int32_t)(c + 1) * T - HW : (int32_t)c * T + HW;
    b->y = (int32_t)r * T;
}

static unsigned step_air(morph_body_t *b, const morph_level_t *L) {
    advance(b, L, false);   // 撞墙水平停,竖直照落
    int16_t cap = b->flaps ? FLOAT_MAX : FALL_MAX;
    b->vy = (int16_t)(b->vy + GRAV);
    if (b->vy > cap) b->vy = cap;
    int32_t ny = b->y + b->vy;
    if (b->vy < 0) {   // 往上:头碰实心停住;单向平台穿过去
        int row = fdiv(ny - BH);
        if (row < fdiv(b->y - BH) && row_has(L, b->x, row, false)) {
            ny = (int32_t)(row + 1) * T + BH;
            b->vy = 0;
        }
    } else {           // 往下:脚从上面碰到实心或平台的顶就站住
        int row = fdiv(ny);
        int32_t top = (int32_t)row * T;
        if (b->y <= top && row_has(L, b->x, row, true)) {
            b->y = top;
            b->vy = 0;
            b->flaps = 0;
            b->st = MB_WALK;
            return MB_EV_LAND;
        }
    }
    b->y = ny;
    if (b->y - BH >= ML_ROWS * T) {   // 掉出关卡底 → 回到坑边
        respawn(b, L);
        b->vy = 0;
        b->flaps = 0;
        b->st = MB_WALK;
        return MB_EV_PIT;
    }
    return 0;
}

unsigned morph_mb_step(morph_body_t *b, const morph_level_t *L) {
    if (grounded(b->st)) {
        if (b->y % T == 0 && row_has(L, b->x, fdiv(b->y), true)) return step_ground(b, L);
        b->st = MB_AIR;   // 脚下没了(走下台阶)→ 掉下去
        b->vy = 0;
        b->flaps = 0;
    }
    return step_air(b, L);
}

unsigned morph_mb_run(morph_body_t *b, const morph_level_t *L, uint32_t ms, uint32_t *acc) {
    unsigned ev = 0;
    *acc += ms;
    while (*acc >= MB_STEP_MS) {
        *acc -= MB_STEP_MS;
        ev |= morph_mb_step(b, L);
    }
    return ev;
}
