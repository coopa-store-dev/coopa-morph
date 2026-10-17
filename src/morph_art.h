// src/morph_art.h —— 人物、格子、装饰的像素画(tools/gen_art.py 从 art/*.txt 生成,勿手改)。
// 全是常量图,放在游戏块(flash)里,LVGL 直接读,不进堆。
// 人物 16×16 RGB565A8,原图朝右,facing_left 给水平翻转的那份;画面上 lv_image_set_scale 放大两倍。下标 ch:0 库巴、1 爸爸。
// 格子和装饰生成时已放大 2 倍(平铺时 LVGL 不放大),关卡格子正好 ML_TILE×ML_TILE;全不透明的是 RGB565。
//   MA_IDLE:站着
//   MA_WALK1:走(迈开腿)
//   MA_WALK2:走(两腿并拢)
//   MA_JAB:出拳
//   MA_WINDUP:蓄力(手往后拉)
//   MA_HEAVY:蓄力斩(砸出去)
//   MA_GUARD:举盾
//   MA_HIT:挨打
//   MA_KO:倒下
//   MA_SPECIAL:技能
//   MA_WIN:赢了
//   MA_JUMP:跳(接力追打)
//   MT_IMG_TOP:地面顶(草)
//   MT_IMG_FILL:地面里(土)
//   MT_IMG_BRICK:砖(悬空的实心格)
//   MT_IMG_PLAT:单向平台(木板)
//   MT_IMG_CLOUD:云(装饰)
//   MT_IMG_BUSH:草丛(装饰)
//   MF_CLOCK:发条兵(大发条兵也用它)(16×16)
//   MF_ICE:冰淇淋怪(16×16)
//   MF_VOLT:电池虫(16×16)
//   MF_BEAR:发条铁皮熊(24×24)
//   MX_HAT_ICE:冰淇淋帽子
//   MX_HAT_VOLT:电池帽子
//   MX_STAR:晕的星星
//   MX_SPARK:打中的火花
//   MX_ICEBALL:冰球
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

typedef enum { MA_IDLE = 0, MA_WALK1, MA_WALK2, MA_JAB, MA_WINDUP, MA_HEAVY, MA_GUARD, MA_HIT, MA_KO, MA_SPECIAL, MA_WIN, MA_JUMP, MA_POSES } morph_pose_t;
typedef enum { MT_IMG_TOP = 0, MT_IMG_FILL, MT_IMG_BRICK, MT_IMG_PLAT, MT_IMG_CLOUD, MT_IMG_BUSH, MT_IMG_COUNT } morph_tile_img_t;
typedef enum { MF_CLOCK = 0, MF_ICE, MF_VOLT, MF_BEAR, MF_COUNT } morph_foe_img_t;
typedef enum { MX_HAT_ICE = 0, MX_HAT_VOLT, MX_STAR, MX_SPARK, MX_ICEBALL, MX_COUNT } morph_fx_t;

const lv_image_dsc_t *morph_art_pose(uint8_t ch, uint8_t pose, bool facing_left);  // 越界返回库巴站立
const lv_image_dsc_t *morph_art_tile(uint8_t t);                                    // 越界返回 NULL
const lv_image_dsc_t *morph_art_foe(uint8_t f, uint8_t frame, bool facing_left);    // frame 0 / 1;越界返回发条兵
const lv_image_dsc_t *morph_art_fx(uint8_t x);                                      // 越界返回 NULL
