// src/morph_run.h —— 一趟冒险:玩具工厂地图(自动走、分叉选路)→ 路上的战斗 / 精英 / 休息 / 宝箱 → 发条铁皮熊。
// 纯 C,宿主可测;血、能量、变身在关和关之间带着走。画面在 morph_gfx.c。
// 地图 7 排:第 0 排起点,1–5 排每排 2–3 个点,第 6 排 boss。走到有分叉的点停下:▲▼ 选、● 出发;只有一条路自己走。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "morph_battle.h"

#define MR_ROWS 7
#define MR_COLS 3
#define MR_WALK_MS 1100

typedef enum { NODE_START = 0, NODE_BATTLE, NODE_ELITE, NODE_REST, NODE_TREASURE, NODE_BOSS, NODE_COUNT } mr_node_t;
typedef enum { RUN_MAP = 0, RUN_EVENT, RUN_BATTLE, RUN_WIN, RUN_OVER } mr_phase_t;
// 地图上弹出的一块(休息、宝箱、打赢);● 关掉
typedef enum { EVT_REST = 0, EVT_TREASURE, EVT_WON, EVT_WON_ELITE } mr_event_t;

typedef struct { uint8_t type; uint8_t next;  /* 位 j = 能走到下一排第 j 个 */ int16_t x, y; } mr_node_info_t;

typedef struct {
    uint8_t nh;                         // 几个人
    uint8_t phase;                      // mr_phase_t
    mr_node_info_t node[MR_ROWS][MR_COLS];
    uint8_t ncol[MR_ROWS];
    uint8_t r, i;                       // 现在在哪个点
    uint8_t choice;                     // 分叉时选的是 next 里第几条
    bool walking; uint8_t to; int32_t walk_t;
    uint8_t event; int32_t event_t;     // RUN_EVENT 时
    uint8_t treasure_form[2];           // 宝箱给的变身(画面写出来)
    mbt_carry_t carry[2];
    morph_battle_t bt;
    uint32_t rng;
} morph_run_t;

void morph_mr_start(morph_run_t *r, int nh, uint32_t seed);
void morph_mr_input(morph_run_t *r, int p, uint8_t key, bool long_press);
void morph_mr_tick(morph_run_t *r, uint32_t ms);
uint8_t morph_mr_next_count(const morph_run_t *r);   // 现在这个点能往下走几条
int morph_mr_next_col(const morph_run_t *r, int k);  // 第 k 条通到下一排第几个
