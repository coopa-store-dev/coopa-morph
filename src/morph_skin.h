// src/morph_skin.h —— 变身的样子:全身换装。衣服、领带、裤子、头发换颜色和花样,头顶长出变身的东西
// (冰淇淋卷尖、电池尖刺、青蛙眼睛、磁极头盔、火箭尾翼),弹簧变身两条腿变成弹簧。
// 每个人一块 MSK_W×MSK_H 的 RGB565A8(静态内存,两人一共 2 KB 出头):从 flash 里两倍大的原图隔一个点取一个,
// 换装,画上头顶的东西,朝左就翻过来。画面上放大两倍画(lv_image_set_scale 512)。
// 原图贴着底下 16 行放,上面多出的几行给头顶的东西,所以底边和原图对齐。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define MSK_W 16
#define MSK_H 22

// slot = 0 / 1(两个人各一块;同一时刻只有一页在画,地图页和战斗页共用)。
// 和上次要的一样就直接返回;换了内容 *changed = true(图的指针不变,调用的要让 LVGL 重画那张图)。
const lv_image_dsc_t *morph_skin(int slot, uint8_t ch, uint8_t pose, bool facing_left, uint8_t form, bool *changed);
