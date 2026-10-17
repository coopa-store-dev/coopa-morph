// src/morph_view.h —— 试验关的 LVGL 画面:上面 28 像素一条(游戏名、联机状态),中间 256 像素是关卡
// (ML_ROWS × ML_TILE,摄像机横着跟自己的人),底下一行按键提示;断线时关卡上盖一行「等队友回来」。
// 只画,不管流程;每帧照 morph_play_t 改(morph_app.c 调)。
//
// 摄像机:人在屏幕横向约 1/3 处,朝左时 2/3 处;转身时摄像机慢慢挪过去(最快 MV_CAM_V 像素/秒,比走得快)。
// 省对象:关卡里一段连着的同种格子是一个 lv_image(LV_IMAGE_ALIGN_TILE 平铺),云、草丛各一个;
// 这些对象都在一个跟着摄像机挪的「世界」容器里(摄像机动一下只挪容器),离开屏幕的回收给新进来的用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "morph_play.h"

#define MV_BAR_H 28                         // 顶上一条
#define MV_LEVEL_H (ML_ROWS * ML_TILE)      // 关卡 256 像素
#define MV_HINT_Y (MV_BAR_H + MV_LEVEL_H)   // 底下提示行从 284 开始
#define MV_CAM_V 160                        // 摄像机最快多少像素/秒

// 在 parent(一个空屏)上建画面;已经建着的先 mv_destroy。me_ch / peer_ch:0 库巴、1 爸爸。
void mv_create(lv_obj_t *parent, const morph_level_t *L, uint8_t me_ch, uint8_t peer_ch);
void mv_update(const morph_play_t *p, uint32_t dt_ms);
// 删掉画面的对象(屏幕本身不删)。没建也可以调。换屏之前、离开游戏之前都要调。
void mv_destroy(void);
void mv_set_cam_step(uint8_t px);  // 实验:摄像机一次至少挪几像素(1 / 2 / 4 / 8),默认 1;mv_destroy 后也留着
void mv_set_decor(bool on);        // 实验:云和草丛开关,默认开;mv_destroy 后也留着
uint16_t mv_obj_count(void);       // 现在屏幕上有几个 LVGL 对象(含屏幕;上一次 mv_create / mv_update 时数的,状态行用)
