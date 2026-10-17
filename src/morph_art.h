// src/morph_art.h —— 人物、格子、装饰的像素画(tools/gen_art.py 从 art/*.txt 生成,勿手改)。
// 全是常量图,放在游戏块(flash)里,LVGL 直接读,不进堆。
// 人物 16×16 RGB565A8,原图朝右,facing_left 给水平翻转的那份;画面上 lv_image_set_scale 放大两倍。下标 ch:0 库巴、1 爸爸。
// 格子和装饰生成时已放大 2 倍(平铺时 LVGL 不放大),关卡格子正好 ML_TILE×ML_TILE;全不透明的是 RGB565。
//   MA_IDLE:站着(坑边、墙边停住)
//   MA_WALK1:走(迈开腿)
//   MA_WALK2:走(两腿并拢)
//   MA_JUMP:跳(往上)
//   MA_FLAP:扑腾(鼓着腮帮子飘)
//   MA_FALL:往下掉
//   MT_IMG_TOP:地面顶(草)
//   MT_IMG_FILL:地面里(土)
//   MT_IMG_BRICK:砖(悬空的实心格)
//   MT_IMG_PLAT:单向平台(木板)
//   MT_IMG_CLOUD:云(装饰)
//   MT_IMG_BUSH:草丛(装饰)
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

typedef enum { MA_IDLE = 0, MA_WALK1, MA_WALK2, MA_JUMP, MA_FLAP, MA_FALL, MA_POSES } morph_pose_t;
typedef enum { MT_IMG_TOP = 0, MT_IMG_FILL, MT_IMG_BRICK, MT_IMG_PLAT, MT_IMG_CLOUD, MT_IMG_BUSH, MT_IMG_COUNT } morph_tile_img_t;

const lv_image_dsc_t *morph_art_pose(uint8_t ch, uint8_t pose, bool facing_left);  // 越界返回库巴站立
const lv_image_dsc_t *morph_art_tile(uint8_t t);                                    // 越界返回 NULL
