// src/morph_art.h —— 人物、格子、装饰的像素画(tools/gen_art.py 从 art/*.txt 生成,勿手改)。
// 全是常量图,放在游戏块(flash)里,LVGL 直接读,不进堆。
// 人物 16×16 的字符画生成时放大两倍,RGB565A8,原图朝右,facing_left 给水平翻转的那份。下标 ch:0 库巴、1 爸爸。
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
//   MT_IMG_TOP:地面顶(草;战斗页平铺,每关染不同颜色)
//   MF_CLOCK:发条兵(大发条兵也用它)(16×16)
//   MF_ICE:冰淇淋怪(16×16)
//   MF_VOLT:电池虫(大电池兵也用它)(16×16)
//   MF_BEAR:发条铁皮熊(24×24)
//   MF_SNOW:雪球兵(大雪人也用它)(16×16)
//   MF_FROG:弹簧蛙(16×16)
//   MF_MAGNET:磁铁怪(16×16)
//   MF_ROCKET:烟花炮(16×16)
//   MF_COIL:线圈(16×16)
//   MF_ICEKING:冰淇淋大王(24×24)
//   MF_DYNAMO:发电大魔像(24×24)
//   MF_DEMON:玩具魔王(24×24)
//   MX_TOP_ICE:冰淇淋变身头顶
//   MX_TOP_VOLT:电池变身头顶
//   MX_STAR:晕的星星
//   MX_SPARK:打中的火花
//   MX_ICEBALL:冰球
//   MX_TOP_SPRING:弹簧变身头顶
//   MX_TOP_MAGNET:磁铁变身头顶
//   MX_TOP_ROCKET:火箭变身头顶
//   MX_SNOWBALL:雪球
//   MX_FIREBALL:火球
//   MX_CRYSTAL:身边的小冰晶
//   MX_BIT:身边的小铁块
//   MX_FLAME:背后喷的火
//   MX_ICEBLOCK:大招砸下来的冰块
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

typedef enum { MA_IDLE = 0, MA_WALK1, MA_WALK2, MA_JAB, MA_WINDUP, MA_HEAVY, MA_GUARD, MA_HIT, MA_KO, MA_SPECIAL, MA_WIN, MA_JUMP, MA_POSES } morph_pose_t;
typedef enum { MT_IMG_TOP = 0, MT_IMG_COUNT } morph_tile_img_t;
typedef enum { MF_CLOCK = 0, MF_ICE, MF_VOLT, MF_BEAR, MF_SNOW, MF_FROG, MF_MAGNET, MF_ROCKET, MF_COIL, MF_ICEKING, MF_DYNAMO, MF_DEMON, MF_COUNT } morph_foe_img_t;
typedef enum { MX_TOP_ICE = 0, MX_TOP_VOLT, MX_STAR, MX_SPARK, MX_ICEBALL, MX_TOP_SPRING, MX_TOP_MAGNET, MX_TOP_ROCKET, MX_SNOWBALL, MX_FIREBALL, MX_CRYSTAL, MX_BIT, MX_FLAME, MX_ICEBLOCK, MX_COUNT } morph_fx_t;

const lv_image_dsc_t *morph_art_tile(uint8_t t);                                    // 越界返回 NULL
const lv_image_dsc_t *morph_art_fx(uint8_t x);                                      // 越界返回 NULL
// 人物只有生成时已放大两倍的图(战斗页原样画,真卡上省一大截刷屏时间;地图、首页上缩放着画);越界返回库巴站立
const lv_image_dsc_t *morph_art_pose2(uint8_t ch, uint8_t pose, bool facing_left);
// 怪只有两倍图:frame 0 / 1(只画了一帧的两帧一样);越界返回发条兵
const lv_image_dsc_t *morph_art_foe2(uint8_t f, uint8_t frame, bool facing_left);
const lv_image_dsc_t *morph_art_fx2(uint8_t x);
