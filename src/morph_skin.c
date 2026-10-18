// src/morph_skin.c —— 见 morph_skin.h。原图的颜色都是调色板里的(tools/gen_art.py),按颜色认出头发、衣服、领带、裤子。
#include "morph_skin.h"

#include <string.h>

#include "morph_art.h"
#include "morph_battle.h"

#define SRC 32          // flash 里的原图是两倍大的 32×32
#define TOP (MSK_H - 16)  // 原图放在第 TOP 行往下

#define C565(v) ((uint16_t)(((((v) >> 16) & 0xFF) >> 3) << 11 | ((((v) >> 8) & 0xFF) >> 2) << 5 | (((v) & 0xFF) >> 3)))
// 原图里认得出的颜色(gen_art.py 的调色板):库巴的 z / Z / t / T / j、爸爸的 B / J,两人的头发 K、描边 k
enum { R_OTHER = 0, R_HAIR, R_SUIT, R_SHADE, R_TIE, R_PANTS };
static int role(uint16_t c) {
    switch (c) {
        case C565(0x1E1A1E): return R_HAIR;
        case C565(0x7E2433): case C565(0x4A7BD0): return R_SUIT;
        case C565(0x5C1824): return R_SHADE;
        case C565(0xB8333F): case C565(0x6E5A5E): return R_TIE;
        case C565(0x3C3F52): case C565(0x3A5A9A): return R_PANTS;
        default: return R_OTHER;
    }
}
#define OUTLINE C565(0x2B1B17)

// 换装:x、y 是格子(原图朝右),top = 头发最上面一行,cx = 头的中线
static uint16_t dress(uint8_t form, int r, uint16_t c, int x, int y, int top, int cx) {
    switch (form) {
        case FORM_ICE:   // 粉白冰淇淋:头发一圈圈卷,衣服粉色,裤子是蛋筒格子
            if (r == R_HAIR) return ((x + y) / 2) % 2 ? C565(0xFFFFFF) : C565(0xF29CC8);
            if (r == R_SUIT) return C565(0xF8BBD0);
            if (r == R_SHADE) return C565(0xF48FB1);
            if (r == R_TIE) return C565(0xFFFFFF);
            if (r == R_PANTS) return (x + y) % 2 ? C565(0xD9A066) : C565(0xB07840);
            break;
        case FORM_VOLT:   // 头发竖起来变黄,衣服黄黑条纹
            if (r == R_HAIR) return x % 3 ? C565(0xFFEB3B) : C565(0xF9A825);
            if (r == R_SUIT) return y % 3 ? C565(0xFDD835) : C565(0x212121);
            if (r == R_SHADE) return C565(0xF9A825);
            if (r == R_TIE || r == R_PANTS) return C565(0x212121);
            break;
        case FORM_SPRING:   // 一身绿,青蛙帽子(腿另外换成弹簧)
            if (r == R_HAIR) return y > top + 2 ? C565(0x2E7D32) : C565(0x43A047);
            if (r == R_SUIT) return C565(0x66BB6A);
            if (r == R_SHADE) return C565(0x388E3C);
            if (r == R_TIE) return C565(0xC5E1A5);
            if (r == R_PANTS) return C565(0x2E7D32);
            break;
        case FORM_MAGNET:   // 衣服一半红一半蓝,银头盔
            if (r == R_HAIR) return y == top ? C565(0xECEFF1) : C565(0xB0BEC5);
            if (r == R_SUIT) return x < cx ? C565(0xE53935) : C565(0x1E88E5);
            if (r == R_SHADE) return x < cx ? C565(0xB71C1C) : C565(0x1565C0);
            if (r == R_TIE) return C565(0xECEFF1);
            if (r == R_PANTS) return C565(0x37474F);
            break;
        case FORM_ROCKET:   // 红白宇航服,白头盔
            if (r == R_HAIR) return y == top ? C565(0xFFFFFF) : C565(0xE0E0E0);
            if (r == R_SUIT) return C565(0xFAFAFA);
            if (r == R_SHADE || r == R_TIE) return C565(0xE53935);
            if (r == R_PANTS) return C565(0xB0BEC5);
            break;
        default: break;
    }
    return c;
}

static const uint8_t TOP_FX[FORM_COUNT] = { 0, MX_TOP_ICE, MX_TOP_VOLT, MX_TOP_SPRING, MX_TOP_MAGNET, MX_TOP_ROCKET };

static uint8_t s_buf[2][MSK_W * MSK_H * 3] __attribute__((aligned(4)));
static lv_image_dsc_t s_dsc[2];
static int32_t s_key[2] = { -1, -1 };

const lv_image_dsc_t *morph_skin(int slot, uint8_t ch, uint8_t pose, bool left, uint8_t form, bool *changed) {
    slot &= 1;
    if (form >= FORM_COUNT) form = FORM_NORMAL;
    int32_t key = ch | pose << 1 | (left ? 1 : 0) << 6 | form << 7;
    if (changed) *changed = key != s_key[slot];
    lv_image_dsc_t *d = &s_dsc[slot];
    if (key == s_key[slot]) return d;
    s_key[slot] = key;

    static uint16_t col[MSK_H][MSK_W];
    static uint8_t al[MSK_H][MSK_W];
    memset(col, 0, sizeof(col));
    memset(al, 0, sizeof(al));
    const lv_image_dsc_t *src = morph_art_pose2(ch, pose, false);
    const uint8_t *sp = src->data;
    uint16_t o[16][16];
    uint8_t oa[16][16];
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int i = (y * 2) * SRC + x * 2;
            o[y][x] = (uint16_t)(sp[i * 2] | sp[i * 2 + 1] << 8);
            oa[y][x] = sp[SRC * SRC * 2 + i];
        }
    // 头发最上面一行和那行的中点(头顶的东西对着它放)
    int top = -1, cx = 6;
    for (int y = 0; y < 16 && top < 0; y++) {
        int x0 = -1, x1 = -1;
        for (int x = 0; x < 16; x++)
            if (oa[y][x] && role(o[y][x]) == R_HAIR) { if (x0 < 0) x0 = x; x1 = x; }
        if (x0 >= 0) { top = y; cx = (x0 + x1 + 1) / 2; }
    }
    // 弹簧:最后一排裤子往下拉成 4 行弹簧,身子整个往上挪
    bool ko = pose == MA_KO, spring = form == FORM_SPRING && !ko;
    int legs = -1;
    if (spring)
        for (int y = 15; y >= 8 && legs < 0; y--)
            for (int x = 0; x < 16; x++) if (oa[y][x] && role(o[y][x]) == R_PANTS) { legs = y; break; }
    int shift = legs >= 0 ? 16 - legs : 0;   // 原图第 legs 行往上的东西整体上移这么多
    int rows = legs >= 0 ? legs : 16;
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < 16; x++) {
            if (!oa[y][x]) continue;
            int cy = TOP + y - shift;
            if (cy < 0) continue;
            col[cy][x] = form ? dress(form, role(o[y][x]), o[y][x], x, y, top, cx) : o[y][x];
            al[cy][x] = 0xFF;
        }
    if (legs >= 0)
        for (int k = 0; k < 4; k++)
            for (int x = 0; x < 16; x++) {
                if (!oa[legs][x]) continue;
                int cy = MSK_H - 4 + k;
                col[cy][x] = role(o[legs][x]) == R_PANTS ? (k % 2 ? C565(0x7E8494) : C565(0xCFD8DC)) : OUTLINE;
                al[cy][x] = 0xFF;
            }
    // 头顶的东西:最底下一行压在头发上面那行描边上
    if (form && !ko && top >= 0) {
        const lv_image_dsc_t *t = morph_art_fx(TOP_FX[form]);
        int w = t->header.w, h = t->header.h;
        int bx = cx - w / 2, by = TOP + top - shift - 1 - (h - 1);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int i = y * w + x, px = bx + x, py = by + y;
                if (!t->data[w * h * 2 + i] || px < 0 || px >= MSK_W || py < 0 || py >= MSK_H) continue;
                col[py][px] = (uint16_t)(t->data[i * 2] | t->data[i * 2 + 1] << 8);
                al[py][px] = 0xFF;
            }
    }
    uint8_t *b = s_buf[slot];
    for (int y = 0; y < MSK_H; y++)
        for (int x = 0; x < MSK_W; x++) {
            int sx = left ? MSK_W - 1 - x : x, i = y * MSK_W + x;
            b[i * 2] = (uint8_t)col[y][sx];
            b[i * 2 + 1] = (uint8_t)(col[y][sx] >> 8);
            b[MSK_W * MSK_H * 2 + i] = al[y][sx];
        }
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565A8;
    d->header.w = MSK_W;
    d->header.h = MSK_H;
    d->header.stride = MSK_W * 2;
    d->data_size = sizeof(s_buf[slot]);
    d->data = b;
    return d;
}
