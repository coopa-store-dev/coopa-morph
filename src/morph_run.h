// src/morph_run.h —— 一趟冒险:营地(选关、用零件升级)→ 这一关的地图(自动走、分叉选路)→ 路上的战斗 / 精英 / 休息 /
// 宝箱 → 这关的 Boss → 回营地。四关:玩具工厂、冰淇淋冷库、电池发电站、玩具魔王城;打通一关开下一关。
// 纯 C,宿主可测;两个人时两边各跑一份(morph_net.c),所以营地里的光标、升级也在这里算。
// 地图 10 排:第 0 排起点,1–8 排每排 2–3 个点,第 9 排 Boss。走到有分叉的点停下:▲▼ 选、● 出发;只有一条路自己走。
// 营地:▲▼ 挪自己的光标、● 选。「选关」● 换下一个开了的关;升级项 ● 花零件升一级;「出发」两个人都按了才走;
// 「回首页」只记一下(quit[p]),回不回由外面(morph_app.c)办。打完一关(赢 / 全倒)停在结果上,● 回营地。
// 每个人的存档(开到第几关、零件、升级)在 prof[];两个人时 prof[0] 是库巴那张卡的、prof[1] 是爸爸那张卡的。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "morph_battle.h"

#define MR_ROWS 10
#define MR_COLS 3
#define MR_WALK_MS 1100
#define MR_CHAPTERS 4
#define MR_UPG_MAX 3

typedef enum { NODE_START = 0, NODE_BATTLE, NODE_ELITE, NODE_REST, NODE_TREASURE, NODE_BOSS, NODE_COUNT } mr_node_t;
typedef enum { RUN_MAP = 0, RUN_EVENT, RUN_BATTLE, RUN_WIN, RUN_OVER, RUN_HUB } mr_phase_t;
// 地图上弹出的一块(休息、宝箱、打赢);● 关掉
typedef enum { EVT_REST = 0, EVT_TREASURE, EVT_WON, EVT_WON_ELITE } mr_event_t;
// 升级项(顺序 = mr_profile_t.lv)
typedef enum { UPG_HP = 0, UPG_ATK, UPG_ULT, UPG_EN, UPG_COUNT } mr_upg_t;
// 营地的项
typedef enum { HUB_CHAPTER = 0, HUB_HP, HUB_ATK, HUB_ULT, HUB_EN, HUB_GO, HUB_QUIT, HUB_COUNT } mr_hub_item_t;
// 营地里刚发生的事(画面写一句话)
typedef enum { HM_NONE = 0, HM_BOUGHT, HM_POOR, HM_MAXED, HM_WAIT_MATE } mr_hub_msg_t;

typedef struct {
    uint8_t unlocked;            // 开到第几关(1..MR_CHAPTERS)
    uint8_t cleared;             // 位 c = 第 c+1 关打通过
    uint8_t lv[UPG_COUNT];       // 0..MR_UPG_MAX
    uint16_t parts;              // 零件
} mr_profile_t;

typedef struct { uint8_t type; uint8_t next;  /* 位 j = 能走到下一排第 j 个 */ int16_t x, y; } mr_node_info_t;

typedef struct {
    uint8_t nh;                         // 几个人
    uint8_t phase;                      // mr_phase_t
    uint8_t chapter;                    // 这一关(0..MR_CHAPTERS-1);营地里是选着的那关
    uint8_t played;                     // 刚打的是哪关(结果上写)
    mr_profile_t prof[2];
    // 营地
    uint8_t hub_sel[2]; bool ready[2], quit[2];
    uint8_t hub_msg[2]; uint8_t hub_item[2];   // 每个人最后一句话(mr_hub_msg_t)和说的是哪一项
    uint16_t gained;                    // 这一关拿到的零件(结果上写)
    bool first_clear;                   // 这次是第一次打通这关
    // 地图
    mr_node_info_t node[MR_ROWS][MR_COLS];
    uint8_t ncol[MR_ROWS];
    uint8_t r, i;                       // 现在在哪个点
    uint8_t choice;                     // 分叉时选的是 next 里第几条
    bool walking; uint8_t to; int32_t walk_t;
    uint8_t event; int32_t event_t;     // RUN_EVENT / RUN_WIN / RUN_OVER 时
    uint8_t treasure_form[2];           // 宝箱给的变身(画面写出来)
    mbt_carry_t carry[2];
    morph_battle_t bt;
    uint32_t rng;
} morph_run_t;

// 一关的样子
typedef struct {
    uint8_t pool[6], npool;             // 普通战斗里的小怪
    uint8_t elites[3], nelite;
    uint8_t boss;
    uint8_t forms[5], nform;            // 宝箱给的变身
    uint16_t hp_base, dmg_pct;
} mr_chapter_t;
extern const mr_chapter_t morph_chapters[MR_CHAPTERS];

void morph_mr_start(morph_run_t *r, int nh, uint32_t seed, const mr_profile_t prof[2]);   // 从营地开始
void morph_mr_input(morph_run_t *r, int p, uint8_t key, bool long_press);
void morph_mr_tick(morph_run_t *r, uint32_t ms);
uint8_t morph_mr_next_count(const morph_run_t *r);   // 现在这个点能往下走几条
int morph_mr_next_col(const morph_run_t *r, int k);  // 第 k 条通到下一排第几个
int morph_mr_upg_cost(uint8_t lv);                   // 从 lv 升到 lv+1 要几个零件(满级 -1)
uint8_t morph_mr_open(const morph_run_t *r);         // 现在能选的关数(两个人取开得多的那个)
void morph_mr_profile_clean(mr_profile_t *p);        // 读到的存档规整到合法范围
