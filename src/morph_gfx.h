// src/morph_gfx.h —— 画面:地图页(点、路、走着的人、分叉提示、弹出的休息 / 宝箱 / 打赢)和战斗页
// (人、怪、帽子、血条能量条、伤害数字、火花、一句话、连击、慢动作、大招特写)。照 morph_run_t 的状态画,
// 每帧只改变了的东西(位置、图、文字没变就不碰 LVGL)。图全是 flash 里的常量图(morph_art.h)。
#pragma once

#include <stdint.h>

#include "lvgl.h"
#include "morph_run.h"

void morph_gfx_map(lv_obj_t *scr, const morph_run_t *r);     // 建地图页
void morph_gfx_battle(lv_obj_t *scr, const morph_run_t *r);  // 建战斗页
void morph_gfx_update(const morph_run_t *r, uint32_t dt);    // 每帧
void morph_gfx_battle_event(const morph_run_t *r, const mbt_ev_t *e);  // 战斗事件:数字、火花、一句话、声音
void morph_gfx_destroy(void);
uint16_t morph_gfx_obj_count(void);
const char *morph_gfx_hero_name(int p);
