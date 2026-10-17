// src/morph_keys.h —— 外壳的按键 → morph_key_t。纯 C(宿主测),不包含外壳头文件。
#pragma once
#include <stdbool.h>
#include <stdint.h>

// key / long_press 照 kit_input_t(key:0 = ▲,1 = ▼,2 = ●)。
// ▲ → MK_JUMP;▼ → MK_TURN;▼ 长按 → -1(按下那一下已经掉过头;计划 2 给「吐掉能力」);● / ● 长按 → -1(计划 2)。
// -1 = 不算。
int morph_key_of(uint8_t key, bool long_press);
