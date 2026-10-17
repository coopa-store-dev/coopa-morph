// src/morph_keys.c —— 见 morph_keys.h。
#include "morph_keys.h"

#include "morph_body.h"

enum { KEY_UP = 0, KEY_DOWN = 1 };  // kit_key_t 的值(kit_app.h)

int morph_key_of(uint8_t key, bool long_press) {
    if (long_press) return -1;
    if (key == KEY_UP) return MK_JUMP;
    if (key == KEY_DOWN) return MK_TURN;
    return -1;
}
