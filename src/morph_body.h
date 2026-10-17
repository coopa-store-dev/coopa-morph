// morph_body.h —— 一个人。纯 C、确定的:同样的起始状态 + 同样的键 + 同样的步数 → 一模一样的结果(联机靠这个)。
#pragma once
#include <stdint.h>
#include "morph_level.h"

#define MB_SUB 16          // 位置 1/16 像素
#define MB_STEP_MS 10
#define MB_W 20            // 身子宽(像素),x 是脚底中点
#define MB_H 28
#define MB_FLAPS 5
// 数值在 morph_body.c 顶上(走 62.5 像素/秒、跳最高约 79 像素、扑腾约 219 像素/秒向上、
// 扑腾过以后下落封顶 87.5 像素/秒,没扑腾过封顶 300 像素/秒)。

typedef enum { MB_WALK = 0, MB_AIR, MB_EDGE, MB_WALL } morph_body_st_t;  // EDGE:坑边停住;WALL:顶着墙停住
typedef enum { MK_TURN = 0, MK_JUMP } morph_key_t;                          // ▼ 掉头、▲ 跳 / 扑腾
enum { MB_EV_LAND = 1u << 0, MB_EV_STOP = 1u << 1, MB_EV_PIT = 1u << 2, MB_EV_GO = 1u << 3 };  // mb_step 的返回

typedef struct {
    int32_t x, y;        // 1/16 像素,脚底中点
    int16_t vy;          // 1/16 像素每步,向下为正
    int8_t dir;          // +1 朝右,-1 朝左
    uint8_t st, flaps;   // morph_body_st_t;这次在空中扑腾了几下
    int32_t safe_x, safe_y;  // 最近一次稳稳站着的地方(掉坑后从这里冒出来)
} morph_body_t;

void mb_init(morph_body_t *b, int x_px, int y_px, int8_t dir);
void mb_key(morph_body_t *b, const morph_level_t *L, uint8_t key);  // morph_key_t
unsigned mb_step(morph_body_t *b, const morph_level_t *L);          // 走一步(10 ms),返回 MB_EV_* 的组合
unsigned mb_run(morph_body_t *b, const morph_level_t *L, uint32_t ms, uint32_t *acc);  // 按 10 ms 一步补齐,余数留在 acc
