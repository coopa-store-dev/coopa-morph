// tests/test_keys.c —— 外壳的按键 → morph_key_t:▲ 跳、▼ 掉头;▼ 长按、●、● 长按都不算(计划 2 再给)。
// 键值照 kit_input_t(0 = ▲,1 = ▼,2 = ●)。▼ 按住 0.5 秒来的长按不能再掉一次头(按下那一下已经掉过了)。
#include <assert.h>
#include <stdio.h>

#include "morph_body.h"
#include "morph_keys.h"

int main(void) {
    assert(morph_key_of(0, false) == MK_JUMP);
    assert(morph_key_of(1, false) == MK_TURN);
    assert(morph_key_of(1, true) == -1);
    assert(morph_key_of(2, false) == -1);
    assert(morph_key_of(2, true) == -1);
    assert(morph_key_of(0, true) == -1);  // 外壳不发 ▲ 的长按;万一来了也不再跳一次
    assert(morph_key_of(3, false) == -1);
    puts("test_keys: ok");
    return 0;
}
