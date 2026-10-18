// src/morph_gfx.h —— 画面:营地页(选关、升级、出发;一关打完的结果盖在上面)、地图页(点、路、走着的人、分叉提示、
// 弹出的休息 / 宝箱 / 打赢)和战斗页(人(变身换装见 morph_skin.h,身边的冰晶 / 电火花 / 铁块 / 喷火,变身时的光柱)、怪、血条能量条、伤害数字、
// 火花、一句话、连击、慢动作、大招在场上打(冰块、落雷、火箭、冲击波)、Boss 的冰盾 / 线圈 / 雷劈 / 冰块)。照 morph_run_t 的状态画,每帧只改变了的东西(位置、图、文字没变就不碰 LVGL)。
// 图是 flash 里的常量图(morph_art.h),人是换好装的(morph_skin.h)。每关的天、楼、地面颜色不一样。
#pragma once

#include <stdint.h>

#include "lvgl.h"
#include "morph_run.h"

typedef enum { GP_HUB = 0, GP_MAP, GP_BATTLE } morph_gfx_page_t;
morph_gfx_page_t morph_gfx_page_for(const morph_run_t *r);   // 这个阶段该建哪一页
void morph_gfx_build(lv_obj_t *scr, const morph_run_t *r);  // 建那一页(营地上两个人的光标都标出来)
void morph_gfx_update(const morph_run_t *r, uint32_t dt);    // 每帧
void morph_gfx_battle_event(const morph_run_t *r, const mbt_ev_t *e);  // 战斗事件:数字、火花、一句话、声音
void morph_gfx_destroy(void);
uint16_t morph_gfx_obj_count(void);
const char *morph_gfx_hero_name(int p);
const char *morph_gfx_chapter_name(int ch);
