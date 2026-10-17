// tests/test_art.c —— 常量图:人物每个姿势两个方向都是 16×16 的 RGB565A8、朝左那张是朝右的水平翻转、姿势各不相同;
// 越界给库巴站立。格子图都不空;关卡格子(地面顶、地面里、砖、单向平台)正好一格大(LVGL 平铺时不放大,生成时放大好了)。
#include "morph_art.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CHARS 2  // 0 库巴、1 爸爸

// RGB565:w*h 个 RGB565;RGB565A8:先 w*h 个 RGB565,再 w*h 个透明度(0 / 255),至少一个不透明
static void check_head(const lv_image_dsc_t *d) {
    assert(d && d->data);
    assert(d->header.magic == LV_IMAGE_HEADER_MAGIC);
    assert(d->header.cf == LV_COLOR_FORMAT_RGB565A8 || d->header.cf == LV_COLOR_FORMAT_RGB565);
    assert(d->header.w > 0 && d->header.h > 0 && d->header.stride == d->header.w * 2u);
    const unsigned px = (unsigned)d->header.w * d->header.h;
    if (d->header.cf == LV_COLOR_FORMAT_RGB565) {
        assert(d->data_size == px * 2u);
        return;
    }
    assert(d->data_size == px * 3u);
    unsigned opaque = 0;
    const uint8_t *alpha = d->data + px * 2u;
    for (unsigned i = 0; i < px; i++) {
        assert(alpha[i] == 0 || alpha[i] == 0xFF);
        opaque += alpha[i] != 0;
    }
    assert(opaque > 0);
}

static void check_mirror(const lv_image_dsc_t *r, const lv_image_dsc_t *l) {
    const unsigned w = r->header.w, h = r->header.h;
    assert(l->header.w == w && l->header.h == h);
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            const unsigned a = y * w + x, b = y * w + (w - 1 - x);
            assert(r->data[a * 2] == l->data[b * 2] && r->data[a * 2 + 1] == l->data[b * 2 + 1]);
            assert(r->data[w * h * 2 + a] == l->data[w * h * 2 + b]);
        }
    }
}

#define PX (32 * 32 * 3)   // 人物两倍图 32×32 RGB565A8
static void test_poses(void) {
    for (uint8_t c = 0; c < CHARS; c++) {
        for (uint8_t p = 0; p < MA_POSES; p++) {
            const lv_image_dsc_t *r = morph_art_pose2(c, p, false), *l = morph_art_pose2(c, p, true);
            check_head(r);
            check_head(l);
            assert(r->header.cf == LV_COLOR_FORMAT_RGB565A8 && l->header.cf == LV_COLOR_FORMAT_RGB565A8);
            assert(r->header.w == 32 && r->header.h == 32 && r->data_size == PX);
            assert(r != l && r->data != l->data);
            check_mirror(r, l);
            for (uint8_t q = 0; q < p; q++) {  // 每个姿势都画了(不是拿别的姿势充数)
                assert(memcmp(r->data, morph_art_pose2(c, q, false)->data, PX) != 0);
            }
        }
        // 人是侧身的:站着那张和它的镜像不一样
        assert(memcmp(morph_art_pose2(c, MA_IDLE, false)->data, morph_art_pose2(c, MA_IDLE, true)->data, PX));
    }
    assert(memcmp(morph_art_pose2(0, MA_IDLE, false)->data, morph_art_pose2(1, MA_IDLE, false)->data, PX));
}

static void test_out_of_range(void) {
    assert(morph_art_pose2(CHARS, MA_JUMP, false) == morph_art_pose2(0, MA_IDLE, false));
    assert(morph_art_pose2(1, MA_POSES, true) == morph_art_pose2(0, MA_IDLE, true));
    assert(morph_art_pose2(255, 255, false) == morph_art_pose2(0, MA_IDLE, false));
    assert(morph_art_tile(MT_IMG_COUNT) == NULL);
    // 怪(两倍图):每只两帧、两个方向都在,朝左的是朝右的水平翻转;Boss 大图 48×48、别的 32×32;特效都在
    for (uint8_t f = 0; f < MF_COUNT; f++)
        for (uint8_t fr = 0; fr < 2; fr++) {
            const lv_image_dsc_t *r = morph_art_foe2(f, fr, false), *l = morph_art_foe2(f, fr, true);
            check_head(r);
            int w = r->header.w, h = r->header.h;
            bool big = f == MF_BEAR || f == MF_ICEKING || f == MF_DYNAMO || f == MF_DEMON;
            assert(w == (big ? 48 : 32) && h == w && l->header.w == w);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) assert(r->data[w * h * 2 + y * w + x] == l->data[w * h * 2 + y * w + (w - 1 - x)]);
        }
    assert(morph_art_foe2(MF_COUNT, 0, false) == morph_art_foe2(MF_CLOCK, 0, false));
    for (uint8_t x = 0; x < MX_COUNT; x++) assert(morph_art_fx(x) && morph_art_fx(x)->data_size > 0);
    assert(morph_art_fx(MX_COUNT) == NULL);
    assert(morph_art_tile(255) == NULL);
}

static void test_tiles(void) {
    for (uint8_t t = 0; t < MT_IMG_COUNT; t++) {
        const lv_image_dsc_t *d = morph_art_tile(t);
        check_head(d);
        for (uint8_t o = 0; o < t; o++) assert(d != morph_art_tile(o) && d->data != morph_art_tile(o)->data);
    }
    assert(morph_art_tile(MT_IMG_TOP)->header.w == 32 && morph_art_tile(MT_IMG_TOP)->header.h == 32);
}

int main(void) {
    test_poses();
    test_out_of_range();
    test_tiles();
    puts("test_art: ok");
    return 0;
}
