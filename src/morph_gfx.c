// src/morph_gfx.c —— 见 morph_gfx.h。坐标照网页小样:场地横 0..240,地面 y = GROUND;人和小怪 16×16 放大两倍。
#include "morph_gfx.h"

#include <stdio.h>
#include <string.h>

#include "kit_sfx.h"
#include "morph_art.h"
#include "morph_skin.h"
#include "morph_texts.h"
#include "ui_kit.h"

#define GROUND 262
#define HUD_H 30
#define HINT_Y 290
#define NUMS 8
#define SPARKS 4

// ---------- 一张图:记着上次画成什么样,没变就不碰 LVGL ----------
typedef struct {
    lv_obj_t *o;
    const lv_image_dsc_t *src;
    int16_t x, y, rot;
    uint16_t scale;
    uint32_t tint;
    uint8_t tint_opa;
    bool shown;
} spr_t;

static lv_obj_t *s_scr;
static morph_gfx_page_t s_kind;

static void spr_new(spr_t *s, lv_obj_t *parent) {
    memset(s, 0, sizeof(*s));
    s->o = lv_image_create(parent);
    lv_image_set_antialias(s->o, false);
    lv_obj_add_flag(s->o, LV_OBJ_FLAG_HIDDEN);
}
static void spr_show(spr_t *s, bool on) {
    if (s->shown == on) return;
    s->shown = on;
    if (on) lv_obj_remove_flag(s->o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s->o, LV_OBJ_FLAG_HIDDEN);
}
// 放大 sc/256 倍(256 = 原样,真卡上原样画最快),画在 (cx, bottom) 上:cx 是横向中点,bottom 是放大后的底边
static void spr_put_scaled(spr_t *s, const lv_image_dsc_t *d, int sc, int cx, int bottom) {
    if (!d) { spr_show(s, false); return; }
    int w = d->header.w, h = d->header.h;
    if (s->src != d) {
        s->src = d;
        lv_image_set_src(s->o, d);
        lv_obj_set_size(s->o, w, h);
    }
    if (s->scale != sc) { s->scale = (uint16_t)sc; lv_image_set_scale(s->o, (uint32_t)sc); }
    int x = cx - w / 2, y = bottom - h / 2 - h * sc / 512;
    if (x != s->x || y != s->y) { s->x = (int16_t)x; s->y = (int16_t)y; lv_obj_set_pos(s->o, x, y); }
    spr_show(s, true);
}
static void spr_put(spr_t *s, const lv_image_dsc_t *d, int k, int cx, int bottom) { spr_put_scaled(s, d, 256 * k, cx, bottom); }
static void spr_tint(spr_t *s, uint32_t color, uint8_t opa) {
    if (s->tint == color && s->tint_opa == opa) return;
    s->tint = color; s->tint_opa = opa;
    lv_obj_set_style_image_recolor(s->o, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(s->o, opa, 0);
}
static void spr_rot(spr_t *s, int rot) {
    if (s->rot == rot) return;
    s->rot = (int16_t)rot;
    lv_image_set_rotation(s->o, rot);
}

// ---------- 文字、条、方块 ----------
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, uint32_t color, int x, int y, int w, bool center) {
    lv_obj_t *t = uk_text(parent, f, color, "");
    if (w) { lv_obj_set_width(t, w); lv_obj_set_style_text_align(t, center ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0); }
    lv_obj_set_pos(t, x, y);
    return t;
}
static void show(lv_obj_t *o, bool on) {
    if (!o) return;
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
typedef struct { lv_obj_t *bg, *fill; int16_t w, cur; uint32_t color; } bar_t;
static void bar_new(bar_t *b, lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    b->bg = uk_rect(parent, x, y, w, h, 0x000000);
    b->fill = uk_rect(parent, x, y, w, h, color);
    b->w = (int16_t)w; b->cur = (int16_t)w; b->color = color;
}
static void bar_set(bar_t *b, int v, int max, uint32_t color) {
    int w = max > 0 ? b->w * (v < 0 ? 0 : v > max ? max : v) / max : 0;
    if (w != b->cur) { b->cur = (int16_t)w; lv_obj_set_width(b->fill, w); show(b->fill, w > 0); }
    if (color != b->color) { b->color = color; lv_obj_set_style_bg_color(b->fill, lv_color_hex(color), 0); }
}
static void bar_show(bar_t *b, bool on) { show(b->bg, on); show(b->fill, on && b->cur > 0); }
// 一块会挪的色块(冰块、冰盾、雷劈):记着位置,没变不碰
typedef struct { lv_obj_t *o; int16_t x, y, w; uint32_t color; } box_t;
static void box_new(box_t *b, lv_obj_t *parent, int w, int h, uint32_t color, uint8_t opa) {
    b->o = uk_rect(parent, 0, 0, w, h, color);
    lv_obj_set_style_bg_opa(b->o, opa, 0);
    b->x = b->y = 0; b->w = (int16_t)w; b->color = color;
    show(b->o, false);
}
static void box_at(box_t *b, int x, int y) {
    if (x != b->x || y != b->y) { b->x = (int16_t)x; b->y = (int16_t)y; lv_obj_set_pos(b->o, x, y); }
    show(b->o, true);
}
// 宽度、颜色也会变的(光柱、冲击波、落雷)
static void box_put(box_t *b, int cx, int y, int w, uint32_t color) {
    if (w != b->w) { b->w = (int16_t)w; lv_obj_set_width(b->o, w); }
    if (color != b->color) { b->color = color; lv_obj_set_style_bg_color(b->o, lv_color_hex(color), 0); }
    box_at(b, cx - w / 2, y);
}

// ---------- 名字、颜色 ----------
static const char *const FORM_NAME[FORM_COUNT] = { TX_F_NORMAL, TX_F_ICE, TX_F_VOLT, TX_F_SPRING, TX_F_MAGNET, TX_F_ROCKET };
static const char *const SKILL_NAME[FORM_COUNT] = { TX_SK_NORMAL, TX_SK_ICE, TX_SK_VOLT, TX_SK_SPRING, TX_SK_MAGNET, TX_SK_ROCKET };
static const char *const ULT_NAME[FORM_COUNT] = { TX_UL_NORMAL, TX_UL_ICE, TX_UL_VOLT, TX_UL_SPRING, TX_UL_MAGNET, TX_UL_ROCKET };
static const char *const SUP_NAME[FORM_COUNT] = { TX_SU_NORMAL, TX_SU_ICE, TX_SU_VOLT, TX_SU_SPRING, TX_SU_MAGNET, TX_SU_ROCKET };
static const uint32_t FORM_COLOR[FORM_COUNT] = { 0xFF7043, 0xF48FB1, 0xFDD835, 0xB0BEC5, 0xE53935, 0xFFA726 };
static const char *const SAY_TEXT[SAY_COUNT] = {
    TX_S_START, TX_S_ELITE, TX_S_BOSS, TX_S_ARMOR_CRACK, TX_S_ARMOR_BREAK, TX_S_STUN_EAT, TX_S_STUN_HIT,
    TX_S_NO_ENERGY, TX_S_RELAY, TX_S_MORPH, TX_S_PERFECT, TX_S_PARRY, TX_S_PARRY_ARMOR, TX_S_LOST_FORM, TX_S_DOWN,
    TX_S_ALL_DOWN, TX_S_REVIVED, TX_S_SUMMON, TX_S_LOW, TX_S_COMBO,
    TX_S_GUARD, TX_S_GUARD_BREAK, TX_S_TURN, TX_S_TRAPPED, TX_S_FREED, TX_S_SHIELDED, TX_S_COIL_DOWN, TX_S_COIL_BACK,
    TX_S_COIL_OPEN, TX_S_PHASE2, TX_S_RAGE, TX_S_REFLECT, TX_S_WAVE,
};
// 合体大招的名字:[小的变身][大的变身]
static const char *const COMBO_NAME[FORM_COUNT][FORM_COUNT] = {
    { TX_CO_NN, TX_CO_NI, TX_CO_NV, TX_CO_NS, TX_CO_NM, TX_CO_NR },
    { NULL, TX_CO_II, TX_CO_IV, TX_CO_IS, TX_CO_IM, TX_CO_IR },
    { NULL, NULL, TX_CO_VV, TX_CO_VS, TX_CO_VM, TX_CO_VR },
    { NULL, NULL, NULL, TX_CO_SS, TX_CO_SM, TX_CO_SR },
    { NULL, NULL, NULL, NULL, TX_CO_MM, TX_CO_MR },
    { NULL, NULL, NULL, NULL, NULL, TX_CO_RR },
};
static const char *combo_name(uint8_t a, uint8_t b) {
    if (a > b) { uint8_t t = a; a = b; b = t; }
    return a < FORM_COUNT && b < FORM_COUNT ? COMBO_NAME[a][b] : TX_CO_NN;
}
static const char *const CH_NAME[MR_CHAPTERS] = { TX_CH1, TX_CH2, TX_CH3, TX_CH4 };
const char *morph_gfx_chapter_name(int ch) { return CH_NAME[ch >= 0 && ch < MR_CHAPTERS ? ch : 0]; }
const char *morph_gfx_hero_name(int p) { return p ? TX_DAD : TX_KID; }
static const char *foe_name(uint8_t type) {
    switch (type) {
        case ET_BEAR: return TX_E_BEAR;
        case ET_SNOWMAN: return TX_E_SNOWMAN;
        case ET_ICEKING: return TX_E_ICEKING;
        case ET_BIGVOLT: return TX_E_BIGVOLT;
        case ET_DYNAMO: return TX_E_DYNAMO;
        case ET_DEMON: return TX_E_DEMON;
        default: return TX_E_BIGCLOCK;
    }
}
// 每关的样子:天、Boss 战的天、楼、窗、地面染色(透明度 0 = 原样草地)、地图底色
typedef struct { uint32_t sky, boss_sky, bld, win, ground; uint8_t ground_opa; uint32_t map; } look_t;
static const look_t LOOK[MR_CHAPTERS] = {
    { 0x5C6BC0, 0x4A148C, 0x3949AB, 0xFFE082, 0, 0, 0x33691E },
    { 0x81D4FA, 0x0277BD, 0xE1F5FE, 0xF48FB1, 0xFFFFFF, LV_OPA_60, 0x0288D1 },
    { 0x37474F, 0x212121, 0x546E7A, 0xFDD835, 0x795548, LV_OPA_50, 0x455A64 },
    { 0x4A148C, 0x1A0033, 0x311B92, 0xE53935, 0x6A1B9A, LV_OPA_50, 0x4A148C },
};
static const look_t *look(const morph_run_t *r) { return &LOOK[r->chapter < MR_CHAPTERS ? r->chapter : 0]; }
static const uint8_t FOE_IMG[ET_COUNT] = {
    [ET_CLOCK] = MF_CLOCK, [ET_ICE] = MF_ICE, [ET_VOLT] = MF_VOLT, [ET_BIGCLOCK] = MF_CLOCK, [ET_BEAR] = MF_BEAR,
    [ET_SNOW] = MF_SNOW, [ET_FROG] = MF_FROG, [ET_SNOWMAN] = MF_SNOW, [ET_ICEKING] = MF_ICEKING, [ET_MAGNET] = MF_MAGNET,
    [ET_ROCKET] = MF_ROCKET, [ET_BIGVOLT] = MF_VOLT, [ET_DYNAMO] = MF_DYNAMO, [ET_COIL] = MF_COIL, [ET_DEMON] = MF_DEMON,
};

morph_gfx_page_t morph_gfx_page_for(const morph_run_t *r) {
    if (r->phase == RUN_BATTLE) return GP_BATTLE;
    if (r->phase == RUN_MAP || r->phase == RUN_EVENT) return GP_MAP;
    return GP_HUB;
}

// =====================================================================================================
// 战斗页
// =====================================================================================================
typedef struct { lv_obj_t *l; int32_t t; int16_t x, y; } num_t;
static struct {
    spr_t hero[2], aura[2][2], vfx[MBT_MAXF], foe[MBT_MAXF], dizzy[MBT_MAXF], shot[MBT_MAXSHOT], fshot[MBT_MAXFSHOT], star[MBT_MAXSTAR],
        spark[SPARKS];
    int32_t spark_t[SPARKS];
    bar_t fhp[MBT_MAXF];
    lv_obj_t *pip[3];
    lv_obj_t *name[2], *form[2];
    bar_t hp[2], en[2][3];
    bar_t boss; lv_obj_t *boss_name;
    lv_obj_t *say, *combo, *hint[2], *slow, *slow_l, *hi5[2];
    box_t ice[2], guard, bolt_mark, bolt, zap, pillar[2], vbolt[3], ring[2];
    lv_obj_t *dim;               // 大招开头场地暗一下(超必杀、合体闪变身的颜色)
    uint8_t from_form[2];        // 变身前一半还画原来的样子
    int32_t bolt_t, zap_t;
    int8_t form_now[2];          // 顶上一条显示的变身(-1 = 还没写)
    int32_t say_t;
    num_t num[NUMS];
    lv_obj_t *cine_a, *cine_b, *cine_c;
    int32_t anim;
} B;

static void hud_hero(int p, int nh) {
    int x0 = nh == 2 ? 22 + p * 108 : 22;   // 屏幕四角是圆的,左上角往里让一点
    int w = nh == 2 ? 96 : 150;
    B.name[p] = label(s_scr, UK_F12, p ? 0x90CAF9 : 0xEF9A9A, x0, 1, 0, false);
    uk_set_text(B.name[p], morph_gfx_hero_name(p));
    B.form[p] = label(s_scr, UK_F12, FORM_COLOR[0], x0 + 30, 1, 0, false);
    bar_new(&B.hp[p], s_scr, x0, 15, w, 5, 0x43A047);
    int ew = (w - 4) / 3;
    for (int k = 0; k < 3; k++) bar_new(&B.en[p][k], s_scr, x0 + k * (ew + 2), 22, ew, 4, 0xFFB300);
}

static void build_battle(lv_obj_t *scr, const morph_run_t *r) {
    memset(&B, 0, sizeof(B));
    B.form_now[0] = B.form_now[1] = -1;
    const look_t *lk = look(r);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1D1B22), 0);
    bool boss = r->node[r->r][r->i].type == NODE_BOSS;
    // 背景:天、楼、地面(背景不动,只建一次)
    uk_rect(scr, 0, HUD_H, 240, GROUND - HUD_H, boss ? lk->boss_sky : lk->sky);
    uk_rect(scr, 0, 150, 40, GROUND - 150, lk->bld);
    uk_rect(scr, 50, 120, 34, GROUND - 120, lk->bld);
    uk_rect(scr, 150, 135, 46, GROUND - 135, lk->bld);
    uk_rect(scr, 206, 110, 34, GROUND - 110, lk->bld);
    for (int i = 0; i < 6; i++) uk_rect(scr, 58 + (i % 2) * 14, 130 + (i / 2) * 26, 8, 10, lk->win);
    lv_obj_t *g = lv_image_create(scr);
    lv_image_set_src(g, morph_art_tile(MT_IMG_TOP));
    lv_obj_set_size(g, 240, HINT_Y - GROUND);
    lv_image_set_inner_align(g, LV_IMAGE_ALIGN_TILE);
    lv_obj_set_pos(g, 0, GROUND);
    if (lk->ground_opa) {
        lv_obj_set_style_image_recolor(g, lv_color_hex(lk->ground), 0);
        lv_obj_set_style_image_recolor_opa(g, lk->ground_opa, 0);
    }
    B.dim = uk_rect(scr, 0, HUD_H, 240, GROUND - HUD_H, 0x000000);
    lv_obj_set_style_bg_opa(B.dim, LV_OPA_40, 0);
    show(B.dim, false);
    // 场地上的东西(建的先后 = 叠的先后)
    for (int p = 0; p < 2; p++) box_new(&B.pillar[p], scr, 28, GROUND - HUD_H, FORM_COLOR[0], LV_OPA_50);
    for (int i = 0; i < 3; i++) box_new(&B.vbolt[i], scr, 8, GROUND - HUD_H, 0xFFF59D, LV_OPA_COVER);
    for (int p = 0; p < 2; p++) box_new(&B.ring[p], scr, 20, 5, 0xFFFFFF, LV_OPA_80);
    box_new(&B.bolt_mark, scr, 44, 4, 0xFFEB3B, LV_OPA_COVER);
    box_new(&B.zap, scr, 240, 8, 0xFFEB3B, LV_OPA_70);
    for (int i = 0; i < MBT_MAXF; i++) {
        spr_new(&B.foe[i], scr); spr_new(&B.dizzy[i], scr);
        bar_new(&B.fhp[i], scr, 0, 0, 20, 2, 0xE53935); bar_show(&B.fhp[i], false);
    }
    box_new(&B.guard, scr, 5, 40, 0x80DEEA, LV_OPA_80);
    for (int i = 0; i < 3; i++) { B.pip[i] = uk_rect(scr, 0, 0, 7, 4, 0xCFD8DC); show(B.pip[i], false); }
    for (int p = 0; p < 2; p++) { spr_new(&B.aura[p][0], scr); spr_new(&B.aura[p][1], scr); }
    for (int p = 0; p < 2; p++) spr_new(&B.hero[p], scr);
    for (int p = 0; p < 2; p++) box_new(&B.ice[p], scr, 30, 38, 0xB3E5FC, LV_OPA_60);
    for (int i = 0; i < MBT_MAXSHOT; i++) spr_new(&B.shot[i], scr);
    for (int i = 0; i < MBT_MAXFSHOT; i++) spr_new(&B.fshot[i], scr);
    for (int i = 0; i < MBT_MAXSTAR; i++) spr_new(&B.star[i], scr);
    for (int i = 0; i < MBT_MAXF; i++) spr_new(&B.vfx[i], scr);
    for (int i = 0; i < SPARKS; i++) spr_new(&B.spark[i], scr);
    box_new(&B.bolt, scr, 8, GROUND - HUD_H, 0xFFF59D, LV_OPA_COVER);
    for (int i = 0; i < NUMS; i++) { B.num[i].l = label(scr, UK_F12, 0xFFFFFF, 0, 0, 0, false); show(B.num[i].l, false); }
    for (int p = 0; p < 2; p++) { B.hi5[p] = label(scr, UK_F12, 0xFFEB3B, 0, 0, 0, false); uk_set_text(B.hi5[p], TX_HIGHFIVE); show(B.hi5[p], false); }
    // 慢动作的罩子
    B.slow = uk_rect(scr, 0, HUD_H, 240, GROUND - HUD_H, 0x4FC3F7);
    lv_obj_set_style_bg_opa(B.slow, LV_OPA_20, 0);
    show(B.slow, false);
    B.slow_l = label(scr, UK_F12, 0xB3E5FC, 0, 70, 240, true);
    uk_set_text(B.slow_l, TX_SLOW);
    show(B.slow_l, false);
    // 顶上一条、Boss 血条、底下提示
    uk_rect(scr, 0, 0, 240, HUD_H, 0x111111);
    for (int p = 0; p < r->nh; p++) hud_hero(p, r->nh);
    bar_new(&B.boss, scr, 20, 34, 200, 5, 0xE53935); bar_show(&B.boss, false);
    B.boss_name = label(scr, UK_F12, 0xFFFFFF, 0, 40, 240, true);
    show(B.boss_name, false);
    B.combo = label(scr, UK_F12, 0xFFEB3B, 120, 56, 116, false);
    lv_obj_set_style_text_align(B.combo, LV_TEXT_ALIGN_RIGHT, 0);
    B.say = label(scr, UK_F24, 0xFFFFFF, 0, 84, 240, true);
    uk_rect(scr, 0, HINT_Y, 240, 320 - HINT_Y, 0x111111);
    if (r->nh == 2) {
        B.hint[0] = label(scr, UK_F12, 0xEF9A9A, 0, HINT_Y + 8, 120, true);
        B.hint[1] = label(scr, UK_F12, 0x90CAF9, 120, HINT_Y + 8, 120, true);
    } else {
        B.hint[0] = label(scr, UK_F12, 0xFFFFFF, 0, HINT_Y + 8, 240, true);
    }
    // 大招的名字(天上一条,招在场上打)
    B.cine_a = label(scr, UK_F12, 0xFFFFFF, 0, 40, 240, true);
    B.cine_b = label(scr, UK_F24, 0xFFFFFF, 0, 54, 240, true);
    B.cine_c = label(scr, UK_F12, 0xFFEB3B, 0, 84, 240, true);
    show(B.cine_a, false); show(B.cine_b, false); show(B.cine_c, false);
}

static void say_text(const char *s) {
    uk_set_text(B.say, s);
    show(B.say, true);
    B.say_t = 1300;
}

static num_t *free_num(void) {
    for (int i = 0; i < NUMS; i++) if (B.num[i].t <= 0) return &B.num[i];
    return &B.num[0];
}

void morph_gfx_battle_event(const morph_run_t *r, const mbt_ev_t *e) {
    if (!s_scr || s_kind != GP_BATTLE) return;
    char buf[64];
    switch (e->type) {
        case BE_HIT: {
            num_t *n = free_num();
            if (e->v == 0) snprintf(buf, sizeof(buf), "%s", TX_IMMUNE);
            else snprintf(buf, sizeof(buf), "%d", e->v);
            uk_set_text(n->l, buf);
            lv_obj_set_style_text_color(n->l, lv_color_hex(e->a & 4 || !e->v ? 0x9E9E9E : e->a & 2 ? 0x4FC3F7 : 0xFFFFFF), 0);
            n->t = 800; n->x = e->x; n->y = (int16_t)(GROUND + e->y - 10);
            show(n->l, true);
            for (int i = 0; i < SPARKS && e->v; i++)
                if (B.spark_t[i] <= 0) { B.spark_t[i] = 150; spr_put(&B.spark[i], e->a & 1 ? morph_art_fx2(MX_SPARK) : morph_art_fx(MX_SPARK), 1, e->x, GROUND + e->y + 10); break; }
            sfx_play(SFX_HIT);
            break;
        }
        case BE_PHIT: {
            num_t *n = free_num();
            snprintf(buf, sizeof(buf), e->v < 0 ? "+%d" : "%d", e->v < 0 ? -e->v : e->v);
            uk_set_text(n->l, buf);
            lv_obj_set_style_text_color(n->l, lv_color_hex(e->v < 0 ? 0x81C784 : 0xEF5350), 0);
            n->t = 800; n->x = e->x; n->y = GROUND - 50;
            show(n->l, true);
            if (e->v > 0) sfx_play(SFX_HIT);
            break;
        }
        case BE_SAY: {
            const char *t = e->a < SAY_COUNT ? SAY_TEXT[e->a] : "";
            if (r->nh == 1 && e->a == SAY_GUARD) t = TX_S_GUARD1;
            if (r->nh == 1 && e->a == SAY_COIL_DOWN) t = TX_S_COIL_DOWN1;
            if (e->a == SAY_BOSS || e->a == SAY_ELITE) {   // 写上是谁
                uint8_t type = ET_BIGCLOCK;
                for (int i = 0; i < MBT_MAXF; i++)
                    if (r->bt.f[i].used && (morph_foes[r->bt.f[i].type].boss || morph_foes[r->bt.f[i].type].elite)) type = r->bt.f[i].type;
                snprintf(buf, sizeof(buf), t, foe_name(type));
            } else if (e->a == SAY_MORPH) snprintf(buf, sizeof(buf), TX_S_MORPH, morph_gfx_hero_name(e->who), FORM_NAME[e->v % FORM_COUNT]);
            else if (e->a == SAY_DOWN || e->a == SAY_REVIVED || e->a == SAY_TRAPPED || e->a == SAY_FREED)
                snprintf(buf, sizeof(buf), t, morph_gfx_hero_name(e->who));
            else snprintf(buf, sizeof(buf), "%s", t);
            say_text(buf);
            if (e->a == SAY_PERFECT || e->a == SAY_PARRY || e->a == SAY_PARRY_ARMOR || e->a == SAY_REFLECT) sfx_play(SFX_PERFECT);
            if (e->a == SAY_MORPH || e->a == SAY_COIL_OPEN || e->a == SAY_GUARD_BREAK) sfx_play(SFX_LEVEL_UP);
            if (e->a == SAY_BOSS || e->a == SAY_RAGE || e->a == SAY_PHASE2) sfx_play(SFX_CHAIN);
            break;
        }
        case BE_ZAP:
            if (e->a == FA_BOLT) { B.bolt_t = 220; box_at(&B.bolt, e->x - 4, HUD_H); sfx_play(SFX_HIT); }
            else if (e->a == FA_RING) {
                B.zap_t = 250;
                lv_obj_set_width(B.zap.o, 112); lv_obj_set_style_bg_color(B.zap.o, lv_color_hex(0xFFEB3B), 0);
                box_at(&B.zap, e->x - 56, GROUND - 8);
                sfx_play(SFX_HIT);
            } else if (e->a == FA_TRAP) {
                B.zap_t = 400;
                lv_obj_set_width(B.zap.o, 240); lv_obj_set_style_bg_color(B.zap.o, lv_color_hex(0xB3E5FC), 0);
                box_at(&B.zap, 0, GROUND - 8);
                sfx_play(SFX_DODGE);
            } else sfx_play(SFX_WHOOSH);
            break;
        case BE_CINE: sfx_play(SFX_CHAIN); break;
        case BE_BOOM: sfx_play(SFX_HIT); break;
        case BE_WHOOSH: sfx_play(SFX_WHOOSH); break;
        case BE_SHIELD: if (e->a) sfx_play(SFX_DODGE); break;
        default: break;
    }
}

static uint8_t hero_pose(const mbt_hero_t *h, int32_t anim) {
    switch (h->st) {
        case HS_WALK: return (anim / 160) % 2 ? MA_WALK1 : MA_WALK2;
        case HS_ATK: return h->t < 200 ? MA_JAB : MA_IDLE;
        case HS_CHARGE: return h->t < 100 ? MA_WINDUP : MA_HEAVY;
        case HS_CHASE: return h->t < 160 ? MA_JUMP : MA_HEAVY;
        case HS_SHIELD: return MA_GUARD;
        case HS_SKILL: return h->form == FORM_SPRING && h->t < 300 ? MA_JUMP : MA_SPECIAL;
        case HS_SWALLOW: return MA_SPECIAL;
        case HS_REVIVE: return MA_WINDUP;
        case HS_MORPH: case HS_ULT: return MA_WIN;
        case HS_HURT: case HS_TRAP: return MA_HIT;
        case HS_DOWN: return MA_KO;
        case HS_ROLL: return MA_JUMP;
        default: return MA_IDLE;
    }
}

static void update_hint(const morph_battle_t *b, int p) {
    char buf[48];
    const mbt_hero_t *h = &b->h[p];
    switch (morph_mbt_hint(b, p)) {
        case HINT_EAT: snprintf(buf, sizeof(buf), "%s", TX_H_EAT); break;
        case HINT_REVIVE: snprintf(buf, sizeof(buf), "%s", TX_H_REVIVE); break;
        case HINT_RELAY: snprintf(buf, sizeof(buf), "%s", TX_H_RELAY); break;
        case HINT_HIGHFIVE: snprintf(buf, sizeof(buf), "%s", TX_H_HIGHFIVE); break;
        case HINT_SUPER: snprintf(buf, sizeof(buf), TX_H_SUPER, SUP_NAME[h->form]); break;
        case HINT_ULT: snprintf(buf, sizeof(buf), TX_H_ULT, ULT_NAME[h->form]); break;
        case HINT_WAIT_UP: snprintf(buf, sizeof(buf), "%s", TX_H_WAIT_UP); break;
        case HINT_BREAK_OUT: snprintf(buf, sizeof(buf), "%s", TX_H_BREAK_OUT); break;
        case HINT_RESCUE: snprintf(buf, sizeof(buf), "%s", TX_H_RESCUE); break;
        default: snprintf(buf, sizeof(buf), b->nh == 2 ? TX_H_NONE2 : TX_H_NONE, SKILL_NAME[h->form]); break;
    }
    uk_set_text(B.hint[p], buf);
}

// ---------- 大招在场上打(时间线见 mbt_cine_t;这里只照着画) ----------
static int cine_k(const mbt_cine_t *c) {   // 当前打到第几个目标:已经开始飞过去的最后一个(和规则一样算)
    int k = 0;
    if (c->step) while (k + 1 < c->n && morph_mbt_cine_hit0(c, k + 1) - c->lead <= c->t) k++;
    return k;
}
#define ROCKET_LIFT 40
#define HOP_H 46
static int rocket_lift(const mbt_cine_t *c) {
    int32_t a = c->t, z = c->end - c->t;
    return a < c->intro ? (int)(a * ROCKET_LIFT / c->intro) : z < 300 ? (int)(z * ROCKET_LIFT / 300) : ROCKET_LIFT;
}
// 放招的人这一刻的动作和离地多高
static uint8_t cine_pose(const mbt_cine_t *c, uint8_t form, int *lift) {
    *lift = form == FORM_ROCKET ? rocket_lift(c) : 0;
    if (c->t < c->intro) return MA_WIN;
    if (form == FORM_ICE || form == FORM_VOLT || form == FORM_MAGNET || form == FORM_ROCKET) return MA_SPECIAL;
    if (!c->n) return MA_IDLE;
    int32_t tt = c->t - morph_mbt_cine_hit0(c, cine_k(c));
    if (tt < -c->lead) return MA_IDLE;
    if (tt < 0) {   // 冲 / 跳过去
        if (form == FORM_SPRING) {
            int32_t u = (tt + c->lead) * 1000 / c->lead;
            *lift = (int)(4 * HOP_H * u * (1000 - u) / 1000000);
        }
        return MA_JUMP;
    }
    if (tt <= (c->hits - 1) * c->sub + 60) return form == FORM_SPRING ? MA_HEAVY : (tt / c->sub) % 2 ? MA_HEAVY : MA_JAB;
    return MA_IDLE;
}

static void update_cine(const morph_battle_t *b) {
    bool on = b->cine_on;
    const mbt_cine_t *c = &b->cine;
    show(B.cine_a, on); show(B.cine_b, on); show(B.cine_c, on);
    show(B.dim, on && c->t < c->intro);
    uint32_t vfx_on = 0, bolt_on = 0, ring_on = 0;
    if (on) {
        char buf[32];
        bool big = c->kind != CINE_ULT;
        if (c->kind == CINE_ULT) {
            uk_set_text(B.cine_a, morph_gfx_hero_name(c->p));
            uk_set_text(B.cine_b, ULT_NAME[c->form]);
            snprintf(buf, sizeof(buf), TX_CINE_LV, c->lv);
            uk_set_text(B.cine_c, buf);
        } else {
            uint32_t flash = (c->t / 100) % 2 ? FORM_COLOR[c->form] : c->kind == CINE_COMBO ? FORM_COLOR[c->form2] : 0x000000;
            lv_obj_set_style_bg_color(B.dim, lv_color_hex(flash), 0);
            uk_set_text(B.cine_a, c->kind == CINE_SUPER ? TX_CINE_SUPER : TX_CINE_COMBO);
            uk_set_text(B.cine_b, c->kind == CINE_SUPER ? SUP_NAME[c->form] : combo_name(c->form, c->form2));
            uk_set_text(B.cine_c, c->kind == CINE_COMBO ? TX_CINE_PAIR : c->low ? TX_CINE_LOW : morph_gfx_hero_name(c->p));
        }
        lv_obj_set_style_text_color(B.cine_b, lv_color_hex(c->kind == CINE_ULT ? FORM_COLOR[c->form] : 0xFFFFFF), 0);
        int32_t tend = (c->hits - 1) * c->sub;
        for (int o = 0; o < (c->kind == CINE_COMBO ? 2 : 1); o++) {
            uint8_t form = o ? c->form2 : c->form;
            const mbt_hero_t *h = &b->h[o ? c->q : c->p];
            int hx = (int)h->x;
            if (form == FORM_MAGNET && c->n) {   // 冲击波从人身上一圈圈推出去
                int32_t tt = c->t - morph_mbt_cine_hit0(c, 0);
                if (tt >= 0 && tt < 320) {
                    box_put(&B.ring[o], hx, GROUND - 6, 20 + (int)tt * 200 / 320, (tt / 60) % 2 ? 0xE53935 : 0x1E88E5);
                    ring_on |= 1u << o;
                }
            }
            for (int k = 0; k < c->n; k++) {
                const mbt_foe_t *f = &b->f[c->tgt[k]];
                int fx = (int)f->x, vi = o ? MBT_MAXF - 1 - k : k;
                int32_t tt = c->t - morph_mbt_cine_hit0(c, k);
                spr_t *v = &B.vfx[vi];
                if (form == FORM_ICE && tt >= -c->lead && tt < tend + 160) {   // 冰块从天上砸下来,砸中就压在怪身上
                    int bottom = tt < 0 ? GROUND - (int)(-tt * 200 / c->lead) : GROUND;
                    spr_put_scaled(v, morph_art_fx2(MX_ICEBLOCK), big ? 448 : 320, fx, bottom);
                    spr_tint(v, 0, 0);
                    vfx_on |= 1u << vi;
                } else if (form == FORM_ROCKET && tt >= -c->lead && tt < tend + 120) {   // 火箭从天上的人飞过去,炸开
                    if (tt < 0) {
                        int32_t u = tt + c->lead, y0 = GROUND - rocket_lift(c) - 24, y1 = GROUND - 8;
                        spr_put_scaled(v, morph_art_fx2(MX_FIREBALL), big ? 320 : 256, hx + (int)((fx - hx) * u / c->lead),
                                       (int)(y0 + (y1 - y0) * u / c->lead));
                        spr_tint(v, 0, 0);
                    } else {
                        spr_put_scaled(v, morph_art_fx2(MX_SPARK), (big ? 512 : 384) + (int)((tt / 40) % 2) * 96, fx, GROUND);
                        spr_tint(v, 0xFF7043, LV_OPA_50);
                    }
                    vfx_on |= 1u << vi;
                } else if (form == FORM_VOLT) {   // 每一下一道雷劈下来
                    for (int j = 0; j < c->hits; j++) {
                        int32_t d = tt - j * c->sub;
                        if (d >= -30 && d < 40) {
                            box_put(&B.vbolt[k % 3], fx, HUD_H, big ? 14 : 8, (d / 20) % 2 ? 0xFFFFFF : 0xFFF59D);
                            bolt_on |= 1u << (k % 3);
                        }
                    }
                } else if (form == FORM_SPRING && tt >= 0 && tt < 200) {   // 落地砸出一圈灰
                    box_put(&B.ring[o], fx, GROUND - 5, 16 + (int)tt * (big ? 70 : 50) / 200, 0xD7CCC8);
                    ring_on |= 1u << o;
                }
            }
        }
    }
    for (int i = 0; i < MBT_MAXF; i++) if (!(vfx_on >> i & 1)) spr_show(&B.vfx[i], false);
    for (int i = 0; i < 3; i++) if (!(bolt_on >> i & 1)) show(B.vbolt[i].o, false);
    for (int i = 0; i < 2; i++) if (!(ring_on >> i & 1)) show(B.ring[i].o, false);
}

// 变身的人身边一直带着的东西:冰晶往上飘、电火花乱跳、小铁块绕着转、背后喷火
static void aura(int p, uint8_t form, int hx, int bottom, bool left, bool on) {
    spr_t *a = B.aura[p];
    for (int i = 0; i < 2; i++) {
        int32_t t = B.anim + i * 700;
        bool vis = on;
        if (!on) {
        } else if (form == FORM_ICE) {
            int32_t ph = t % 1400;
            vis = ph < 1100;
            if (vis) spr_put(&a[i], morph_art_fx(MX_CRYSTAL), 1, hx + (i ? 13 : -13) + (int)(ph / 200 % 2), bottom - 6 - (int)(ph * 36 / 1100));
        } else if (form == FORM_VOLT) {
            int32_t k = B.anim / 80 + i * 3;
            vis = k % 3 != 2;
            if (vis) spr_put(&a[i], morph_art_fx(MX_SPARK), 1, hx + (int)((k * 37 + i * 11) % 32) - 16, bottom - 6 - (int)((k * 53 + i * 7) % 36));
        } else if (form == FORM_MAGNET) {
            int ang = (int)((B.anim * 360 / 900 + i * 180) % 360);
            spr_put(&a[i], morph_art_fx(MX_BIT), 1, hx + lv_trigo_cos((int16_t)ang) * 18 / 32767,
                    bottom - 22 + lv_trigo_sin((int16_t)ang) * 6 / 32767);
        } else if (form == FORM_ROCKET && i == 0) {
            spr_put_scaled(&a[i], morph_art_fx2(MX_FLAME), (B.anim / 60) % 2 ? 256 : 210, hx + (left ? 9 : -9), bottom - 2);
        } else {
            vis = false;
        }
        if (!vis) spr_show(&a[i], false);
    }
}

static void update_battle(const morph_run_t *r, uint32_t dt) {
    const morph_battle_t *b = &r->bt;
    B.anim += (int32_t)dt;
    // 不震屏:整块场地一挪就是整屏重画(真卡上一次 60–70 ms),打中的顿一下和火花已经够有感觉
    // 人(变身的样子见 morph_skin;变身那一下:前一半转一圈、闪白、冒光柱,后一半换上新样子)
    for (int p = 0; p < b->nh; p++) {
        const mbt_hero_t *h = &b->h[p];
        bool left = h->face < 0;
        int hx = (int)h->x, lift = 0;
        uint8_t pose = hero_pose(h, B.anim);
        bool casting = b->cine_on && (b->cine.p == p || (b->cine.kind == CINE_COMBO && b->cine.q == p));
        if (casting) pose = cine_pose(&b->cine, b->cine.p == p ? b->cine.form : b->cine.form2, &lift);
        if (h->st == HS_CHASE) { int t = h->t > 400 ? 400 : h->t; lift = (t < 200 ? t : 400 - t) / 5; }
        if (h->st == HS_SKILL && h->form == FORM_SPRING && h->t < 300) lift = (int)(h->t * (300 - h->t) / 750);
        if (h->form == FORM_SPRING && (h->st == HS_IDLE || h->st == HS_WALK)) {   // 弹簧腿一弹一弹
            static const uint8_t BOUNCE[6] = { 0, 2, 4, 5, 4, 2 };
            lift += BOUNCE[(B.anim / 70) % 6];
        }
        bool morph = h->st == HS_MORPH, first = morph && h->t < 300;
        if (!morph) B.from_form[p] = h->form;
        uint8_t form = first ? B.from_form[p] : h->form;
        bool chg;
        spr_put_scaled(&B.hero[p], morph_skin(p, (uint8_t)p, pose, left, form, &chg), 512, hx, GROUND - lift);
        if (chg) lv_obj_invalidate(B.hero[p].o);
        spr_rot(&B.hero[p], h->st == HS_ROLL ? (int)(h->t * 12 % 3600) * (left ? -1 : 1) : first ? (int)(h->t * 12) : 0);
        uint32_t tint = 0; uint8_t opa = 0;
        if (h->flash > 0) { tint = 0xFFFFFF; opa = LV_OPA_70; }
        else if (first) { tint = 0xFFFFFF; opa = (h->t / 60) % 2 ? LV_OPA_80 : LV_OPA_30; }
        else if (morph && (h->t / 80) % 2) { tint = FORM_COLOR[h->form]; opa = LV_OPA_50; }
        else if (casting && b->cine.t < b->cine.intro && (b->cine.t / 80) % 2) { tint = FORM_COLOR[h->form]; opa = LV_OPA_60; }
        else if (h->st == HS_REVIVE) { tint = 0x81C784; opa = LV_OPA_40; }
        spr_tint(&B.hero[p], tint, opa);
        if (morph) box_put(&B.pillar[p], hx, HUD_H, 28 - (int)h->t * 26 / 600, FORM_COLOR[h->form]);
        else show(B.pillar[p].o, false);
        aura(p, form, hx, GROUND - lift, left, h->st != HS_DOWN && h->st != HS_ROLL && !first);
        if (h->st == HS_TRAP) box_at(&B.ice[p], hx - 15, GROUND - 36);
        else show(B.ice[p].o, false);
        show(B.hi5[p], h->pend);
        if (h->pend) lv_obj_set_pos(B.hi5[p], hx - 15, GROUND - 62);
        // 顶上一条
        if (B.form_now[p] != h->form) {
            B.form_now[p] = (int8_t)h->form;
            uk_set_text(B.form[p], FORM_NAME[h->form]);
            lv_obj_set_style_text_color(B.form[p], lv_color_hex(FORM_COLOR[h->form]), 0);
        }
        bar_set(&B.hp[p], h->hp, h->maxhp, h->hp * 10 < h->maxhp * 3 ? 0xE53935 : 0x43A047);
        for (int k = 0; k < 3; k++) bar_set(&B.en[p][k], h->en - k * 100, 100, h->en >= (k + 1) * 100 ? 0xFFB300 : 0xA0700F);
        update_hint(b, p);
    }
    // 怪
    int armored = -1, big = -1, guard = -1;
    bool bolt_tele = false;
    for (int i = 0; i < MBT_MAXF; i++) {
        const mbt_foe_t *f = &b->f[i];
        if (!f->used) { spr_show(&B.foe[i], false); spr_show(&B.dizzy[i], false); bar_show(&B.fhp[i], false); continue; }
        const mbt_foe_def_t *d = &morph_foes[f->type];
        bool coil = f->type == ET_COIL, one_frame = f->type == ET_ICEKING || f->type == ET_DYNAMO || f->type == ET_DEMON;
        int sc = (f->type == ET_BIGCLOCK || f->type == ET_SNOWMAN || f->type == ET_BIGVOLT) ? 384 : 256;   // 精英:两倍图再放大 1.5
        uint8_t frame = coil ? (uint8_t)((B.anim / 150) % 2) : f->st == FS_WALK ? (uint8_t)((B.anim / 220) % 2) : 0;
        int fx = (int)f->x, fy = GROUND + (int)f->y;
        if (one_frame && f->st == FS_WALK && (B.anim / 220) % 2) fy -= 2;   // 只画了一帧的 Boss:走路上下颠
        const lv_image_dsc_t *img = morph_art_foe2(FOE_IMG[f->type], frame, f->face < 0);
        spr_put_scaled(&B.foe[i], img, sc, fx, fy);
        bool shielded = morph_mbt_shielded(b, i);
        uint32_t tint = 0; uint8_t opa = 0;
        if (coil && f->hp <= 0) { tint = 0x212121; opa = LV_OPA_60; }
        else if (f->hp <= 0) { tint = 0xFFFFFF; opa = LV_OPA_COVER; }
        else if (f->flash > 0) { tint = 0xFFFFFF; opa = LV_OPA_70; }
        else if (f->st == FS_TELE && (f->t / 80) % 2 == 0) {
            tint = f->atk == FA_TRAP ? 0x4DD0E1 : f->atk == FA_BOLT ? 0xFFEB3B : 0xFF1744; opa = LV_OPA_70;
        }
        else if (f->freeze_t > 0) { tint = 0xB3E5FC; opa = LV_OPA_60; }
        else if (shielded) { tint = 0xFFF59D; opa = (B.anim / 200) % 2 ? LV_OPA_50 : LV_OPA_30; }
        else if (f->armor) { tint = 0xCFD8DC; opa = LV_OPA_30; }
        spr_tint(&B.foe[i], tint, opa);
        int top = fy - img->header.h * sc / 256 - 6;
        if (f->hp > 0 && f->stun_t > 0) spr_put(&B.dizzy[i], morph_art_fx(MX_STAR), 1, fx + (int)((B.anim / 60) % 21) - 10, top);
        else spr_show(&B.dizzy[i], false);
        // 小怪、线圈头上的小血条;坏了的线圈:黄条是「还剩多久充好电」
        bool down_coil = coil && f->hp <= 0 && f->open_t > 0;
        bool small = (!d->boss && !d->elite && f->hp > 0) || down_coil;
        if (small) {
            lv_obj_set_pos(B.fhp[i].bg, fx - 10, top - 4);
            lv_obj_set_pos(B.fhp[i].fill, fx - 10, top - 4);
            if (down_coil) bar_set(&B.fhp[i], f->open_t, b->nh == 2 ? 3000 : 12000, 0xFFEB3B);
            else bar_set(&B.fhp[i], f->hp, f->max, 0xE53935);
        }
        bar_show(&B.fhp[i], small);
        if (f->armor && f->hp > 0) armored = i;
        if ((d->boss || d->elite) && f->hp > 0) big = i;
        if (morph_mbt_guarding(f)) guard = i;
        if (f->hp > 0 && f->st == FS_TELE && f->atk == FA_BOLT) {
            bolt_tele = true;
            if ((B.anim / 100) % 2) box_at(&B.bolt_mark, (int)f->atk_x - 22, GROUND - 4);
            else show(B.bolt_mark.o, false);
        }
    }
    if (!bolt_tele) show(B.bolt_mark.o, false);
    if (guard >= 0) {
        const mbt_foe_t *f = &b->f[guard];
        box_at(&B.guard, (int)f->x + f->face * 24 - 2, GROUND + (int)f->y - 44);
    } else {
        show(B.guard.o, false);
    }
    for (int k = 0; k < 3; k++) {
        bool on = armored >= 0 && k < b->f[armored].armor;
        show(B.pip[k], on);
        if (on) lv_obj_set_pos(B.pip[k], (int)b->f[armored].x - 13 + k * 9, GROUND - 68);
    }
    bar_show(&B.boss, big >= 0); show(B.boss_name, big >= 0 && !b->cine_on);   // 放大招时那块地方写招式名
    if (big >= 0) {
        bar_set(&B.boss, b->f[big].hp, b->f[big].max, 0xE53935);
        uk_set_text(B.boss_name, foe_name(b->f[big].type));
    }
    if (B.bolt_t > 0) { B.bolt_t -= (int32_t)dt; if (B.bolt_t <= 0) show(B.bolt.o, false); }
    if (B.zap_t > 0) { B.zap_t -= (int32_t)dt; if (B.zap_t <= 0) show(B.zap.o, false); }
    for (int i = 0; i < MBT_MAXSHOT; i++) {
        if (b->shot[i].used)
            spr_put(&B.shot[i], morph_art_fx2(b->shot[i].form == FORM_ROCKET ? MX_FIREBALL : MX_ICEBALL), 1, (int)b->shot[i].x, GROUND - 12);
        else spr_show(&B.shot[i], false);
    }
    for (int i = 0; i < MBT_MAXFSHOT; i++) {
        const mbt_fshot_t *s = &b->fshot[i];
        if (!s->used) { spr_show(&B.fshot[i], false); continue; }
        spr_put_scaled(&B.fshot[i], morph_art_fx2(s->kind ? MX_FIREBALL : MX_SNOWBALL), s->big ? 384 : 256, (int)s->x, GROUND - 14);
        spr_tint(&B.fshot[i], s->refl ? 0xFFD54F : 0, s->refl ? LV_OPA_60 : 0);
    }
    for (int i = 0; i < MBT_MAXSTAR; i++) {
        if (b->star[i].used) {
            spr_put(&B.star[i], morph_art_fx2(MX_STAR), 1, (int)b->star[i].x, GROUND + (int)b->star[i].y);
            spr_tint(&B.star[i], FORM_COLOR[b->star[i].form], LV_OPA_50);
        } else {
            spr_show(&B.star[i], false);
        }
    }
    for (int i = 0; i < SPARKS; i++) {
        if (B.spark_t[i] <= 0) continue;
        B.spark_t[i] -= (int32_t)dt;
        if (B.spark_t[i] <= 0) spr_show(&B.spark[i], false);
    }
    for (int i = 0; i < NUMS; i++) {
        num_t *n = &B.num[i];
        if (n->t <= 0) continue;
        n->t -= (int32_t)dt;
        if (n->t <= 0) { show(n->l, false); continue; }
        lv_obj_set_pos(n->l, n->x - 8, n->y - (800 - n->t) / 30);
    }
    if (B.say_t > 0) { B.say_t -= (int32_t)dt; show(B.say, B.say_t > 0 && !b->cine_on); }
    char buf[24];
    if (b->combo > 1) { snprintf(buf, sizeof(buf), TX_COMBO_N, (unsigned)b->combo); uk_set_text(B.combo, buf); }
    show(B.combo, b->combo > 1);
    show(B.slow, b->slow_t > 0); show(B.slow_l, b->slow_t > 0);
    update_cine(b);
}

// =====================================================================================================
// 地图页
// =====================================================================================================
static lv_point_precise_t s_pts[MR_ROWS * MR_COLS * MR_COLS][2];
static struct {
    lv_obj_t *dot[MR_ROWS][MR_COLS], *ch[MR_ROWS][MR_COLS];
    spr_t hero[2];
    lv_obj_t *hint_bg, *hint, *desc, *panel, *pt, *p1, *p2, *pn;
    int8_t sel_r, sel_i;
    bool done[MR_ROWS][MR_COLS];   // 已经涂灰了
    int32_t anim;
} M;

static const char *const NODE_CH[NODE_COUNT] = { TX_N_START, TX_N_BATTLE, TX_N_ELITE, TX_N_REST, TX_N_TREASURE, TX_N_BOSS };
static const uint32_t NODE_COLOR[NODE_COUNT] = { 0x78909C, 0xE53935, 0x8E24AA, 0x43A047, 0xFB8C00, 0x212121 };
static const char *const NODE_DESC[NODE_COUNT] = { "", TX_D_BATTLE, TX_D_ELITE, TX_D_REST, TX_D_TREASURE, TX_D_BOSS };

static void build_map(lv_obj_t *scr, const morph_run_t *r) {
    memset(&M, 0, sizeof(M));
    M.sel_r = M.sel_i = -1;
    lv_obj_set_style_bg_color(scr, lv_color_hex(look(r)->map), 0);
    lv_obj_t *t = label(scr, UK_F12, 0xFFFFFF, 0, 4, 240, true);
    char buf[48];
    snprintf(buf, sizeof(buf), TX_CH_N, r->chapter + 1, CH_NAME[r->chapter]);
    uk_set_text(t, buf);
    int k = 0;
    for (int row = 0; row < MR_ROWS - 1; row++)
        for (int i = 0; i < r->ncol[row]; i++)
            for (int j = 0; j < r->ncol[row + 1]; j++) {
                if (!((r->node[row][i].next >> j) & 1)) continue;
                const mr_node_info_t *a = &r->node[row][i], *b = &r->node[row + 1][j];
                s_pts[k][0] = (lv_point_precise_t){ a->x, a->y };
                s_pts[k][1] = (lv_point_precise_t){ b->x, b->y };
                lv_obj_t *l = lv_line_create(scr);
                lv_line_set_points(l, s_pts[k], 2);
                lv_obj_set_style_line_width(l, 2, 0);
                lv_obj_set_style_line_color(l, lv_color_hex(0xBDBDBD), 0);
                k++;
            }
    for (int row = 0; row < MR_ROWS; row++)
        for (int i = 0; i < r->ncol[row]; i++) {
            const mr_node_info_t *n = &r->node[row][i];
            lv_obj_t *d = uk_rect(scr, n->x - 10, n->y - 10, 20, 20, NODE_COLOR[n->type]);
            lv_obj_set_style_radius(d, 10, 0);
            M.dot[row][i] = d;
            lv_obj_t *c = label(scr, UK_F12, 0xFFFFFF, n->x - 10, n->y - 7, 20, true);
            uk_set_text(c, NODE_CH[n->type]);
            M.ch[row][i] = c;
        }
    for (int p = 0; p < r->nh; p++) spr_new(&M.hero[p], scr);
    M.hint_bg = uk_rect(scr, 0, 286, 240, 34, 0x111111);
    M.desc = label(scr, UK_F12, 0xFFF59D, 0, 288, 240, true);
    M.hint = label(scr, UK_F12, 0xFFFFFF, 0, 304, 240, true);
    uk_set_text(M.hint, TX_FORK);
    M.panel = uk_rect(scr, 16, 104, 208, 112, 0x000000);
    lv_obj_set_style_bg_opa(M.panel, LV_OPA_80, 0);
    M.pt = label(scr, UK_F24, 0xFFEB3B, 16, 110, 208, true);
    M.p1 = label(scr, UK_F12, 0xFFFFFF, 16, 146, 208, true);
    M.p2 = label(scr, UK_F12, 0xFFFFFF, 16, 166, 208, true);
    M.pn = label(scr, UK_F12, 0xBBBBBB, 16, 194, 208, true);
    uk_set_text(M.pn, TX_EV_NEXT);
}

static void update_map(const morph_run_t *r, uint32_t dt) {
    M.anim += (int32_t)dt;
    const mr_node_info_t *nd = &r->node[r->r][r->i];
    int px = nd->x, py = nd->y;
    if (r->walking) {
        const mr_node_info_t *to = &r->node[r->r + 1][r->to];
        int t = r->walk_t > MR_WALK_MS ? MR_WALK_MS : r->walk_t;
        px = nd->x + (to->x - nd->x) * t / MR_WALK_MS;
        py = nd->y + (to->y - nd->y) * t / MR_WALK_MS;
    }
    int bob = r->walking ? (int)((M.anim / 80) % 2) * 3 : 0;
    for (int p = 0; p < r->nh; p++) {   // 换装的图原大画(地图上只两个人)
        bool chg;
        uint8_t pose = r->walking ? ((M.anim / 160) % 2 ? MA_WALK1 : MA_WALK2) : MA_IDLE;
        spr_put(&M.hero[p], morph_skin(p, (uint8_t)p, pose, false, r->carry[p].form, &chg), 1, px - (r->nh == 2 ? 7 : 0) + p * 14, py - 6 - bob);
        if (chg) lv_obj_invalidate(M.hero[p].o);
    }
    // 走过的点变灰,分叉时选中的点亮起来
    uint8_t n = morph_mr_next_count(r);
    bool fork = r->phase == RUN_MAP && !r->walking && n > 1;
    int sr = fork ? r->r + 1 : -1, si = fork ? morph_mr_next_col(r, r->choice) : -1;
    if (sr != M.sel_r || si != M.sel_i) {
        if (M.sel_r >= 0 && !M.done[M.sel_r][M.sel_i])
            lv_obj_set_style_bg_color(M.dot[M.sel_r][M.sel_i], lv_color_hex(NODE_COLOR[r->node[M.sel_r][M.sel_i].type]), 0);
        if (sr >= 0) lv_obj_set_style_bg_color(M.dot[sr][si], lv_color_hex(0xFFF59D), 0);
        M.sel_r = (int8_t)sr; M.sel_i = (int8_t)si;
    }
    for (int row = 0; row <= r->r; row++)
        for (int i = 0; i < r->ncol[row]; i++)
            if ((row < r->r || i == r->i) && !M.done[row][i]) {
                M.done[row][i] = true;
                lv_obj_set_style_bg_color(M.dot[row][i], lv_color_hex(0x9E9E9E), 0);
            }
    show(M.hint_bg, fork); show(M.hint, fork); show(M.desc, fork);
    if (fork) uk_set_text(M.desc, NODE_DESC[r->node[sr][si].type]);
    // 弹出来的一块
    bool ev = r->phase == RUN_EVENT;
    show(M.panel, ev); show(M.pt, ev); show(M.p1, ev); show(M.p2, ev); show(M.pn, ev && r->event_t > 600);
    if (!ev) return;
    char a[48], b[48];
    a[0] = b[0] = 0;
    const char *title = TX_EV_WON;
    switch (r->event) {
        case EVT_REST: title = TX_EV_REST; snprintf(a, sizeof(a), "%s", TX_EV_REST1); snprintf(b, sizeof(b), "%s", TX_EV_REST2); break;
        case EVT_TREASURE:
            title = TX_EV_TREASURE;
            snprintf(a, sizeof(a), TX_EV_TREASURE1, morph_gfx_hero_name(0), FORM_NAME[r->treasure_form[0]]);
            if (r->nh == 2) snprintf(b, sizeof(b), TX_EV_TREASURE1, morph_gfx_hero_name(1), FORM_NAME[r->treasure_form[1]]);
            break;
        case EVT_WON_ELITE: title = TX_EV_ELITE; snprintf(a, sizeof(a), "%s", TX_EV_ELITE1); break;
        default: snprintf(a, sizeof(a), "%s", TX_EV_WON1); break;
    }
    uk_set_text(M.pt, title); uk_set_text(M.p1, a); uk_set_text(M.p2, b);
}

// =====================================================================================================
// 营地页(一关打完的结果盖在上面)
// =====================================================================================================
#define HUB_Y0 56
#define HUB_DY 30
static struct {
    lv_obj_t *parts, *item[HUB_COUNT], *val[HUB_COUNT], *mark[2], *msg[2];
    lv_obj_t *panel, *pt, *p[4], *pn;
    char key[160];               // 上次画的内容(没变就不碰)
} H;

static void build_hub(lv_obj_t *scr, const morph_run_t *r) {
    memset(&H, 0, sizeof(H));
    lv_obj_set_style_bg_color(scr, lv_color_hex(look(r)->map), 0);
    lv_obj_t *t = label(scr, UK_F24, 0xFFFFFF, 0, 4, 240, true);
    uk_set_text(t, TX_HUB);
    H.parts = label(scr, UK_F12, 0xFFE082, 0, 34, 240, true);
    for (int i = 0; i < HUB_COUNT; i++) {
        lv_obj_t *bg = uk_rect(scr, 20, HUB_Y0 + i * HUB_DY, 200, 26, i == HUB_GO ? 0xC8302C : 0x000000);
        lv_obj_set_style_bg_opa(bg, i == HUB_GO ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_set_style_radius(bg, 6, 0);
        H.item[i] = label(scr, UK_F12, 0xFFFFFF, 28, HUB_Y0 + i * HUB_DY + 6, 0, false);
        H.val[i] = label(scr, UK_F12, 0xFFF59D, 150, HUB_Y0 + i * HUB_DY + 6, 64, false);
        lv_obj_set_style_text_align(H.val[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    uk_set_text(H.item[HUB_HP], TX_HUB_HP);
    uk_set_text(H.item[HUB_ATK], TX_HUB_ATK);
    uk_set_text(H.item[HUB_ULT], TX_HUB_ULT);
    uk_set_text(H.item[HUB_EN], TX_HUB_EN);
    uk_set_text(H.item[HUB_GO], TX_HUB_GO);
    uk_set_text(H.item[HUB_QUIT], TX_HUB_QUIT);
    for (int p = 0; p < r->nh; p++) {
        H.mark[p] = label(scr, UK_F12, p ? 0x90CAF9 : 0xEF9A9A, p ? 224 : 6, 0, 0, false);
        uk_set_text(H.mark[p], p ? TX_MARK_DAD : TX_MARK_KID);
        H.msg[p] = label(scr, UK_F12, p ? 0x90CAF9 : 0xEF9A9A, 0, 268 + p * 15, 240, true);
    }
    lv_obj_t *hint = label(scr, UK_F12, 0xDDDDDD, 0, 302, 240, true);
    uk_set_text(hint, TX_HUB_HINT);
    H.panel = uk_rect(scr, 12, 60, 216, 200, 0x000000);
    lv_obj_set_style_bg_opa(H.panel, LV_OPA_90, 0);
    lv_obj_set_style_radius(H.panel, 8, 0);
    H.pt = label(scr, UK_F24, 0xFFD54F, 12, 74, 216, true);
    for (int i = 0; i < 4; i++) H.p[i] = label(scr, UK_F12, 0xFFFFFF, 12, 116 + i * 22, 216, true);
    H.pn = label(scr, UK_F12, 0xBBBBBB, 12, 228, 216, true);
    uk_set_text(H.pn, TX_END_HINT);
}

static void hub_msg(const morph_run_t *r, int p, char *out, size_t n) {
    const mr_profile_t *pf = &r->prof[p];
    const char *who = r->nh == 2 ? morph_gfx_hero_name(p) : "";
    uint8_t it = r->hub_msg[p] == HM_NONE ? r->hub_sel[p] : r->hub_item[p];
    int u = it - HUB_HP;
    switch (r->hub_msg[p]) {
        case HM_BOUGHT: snprintf(out, n, TX_HM_BOUGHT, who, (unsigned)pf->lv[u]); return;
        case HM_POOR: snprintf(out, n, TX_HM_POOR, who, morph_mr_upg_cost(pf->lv[u])); return;
        case HM_MAXED: snprintf(out, n, TX_HM_MAXED, who); return;
        case HM_WAIT_MATE: snprintf(out, n, TX_HM_WAIT, who); return;
        default: break;
    }
    if (it >= HUB_HP && it <= HUB_EN) {
        int c = morph_mr_upg_cost(pf->lv[u]);
        if (c < 0) snprintf(out, n, TX_HM_MAXED, who);
        else snprintf(out, n, TX_HM_COST, who, c);
    } else if (it == HUB_CHAPTER) snprintf(out, n, "%s", TX_HM_CH);
    else if (it == HUB_GO) snprintf(out, n, "%s", r->ready[p] ? "" : TX_HM_GO);
    else out[0] = 0;
}

static void update_hub(const morph_run_t *r) {
    char key[sizeof(H.key)], buf[64];
    bool result = r->phase == RUN_WIN || r->phase == RUN_OVER;
    snprintf(key, sizeof(key), "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %lu", r->phase, r->chapter, r->hub_sel[0],
             r->hub_sel[1], r->ready[0], r->ready[1], r->hub_msg[0], r->hub_msg[1], r->prof[0].parts, r->prof[1].parts,
             r->prof[0].lv[0] | r->prof[0].lv[1] << 2 | r->prof[0].lv[2] << 4 | r->prof[0].lv[3] << 6,
             r->prof[1].lv[0] | r->prof[1].lv[1] << 2 | r->prof[1].lv[2] << 4 | r->prof[1].lv[3] << 6,
             r->prof[0].cleared | r->prof[1].cleared, r->hub_item[0] | r->hub_item[1] << 4,
             (unsigned long)(result && r->event_t > 900));
    if (!strcmp(key, H.key)) return;
    snprintf(H.key, sizeof(H.key), "%s", key);
    // 零件
    if (r->nh == 2) snprintf(buf, sizeof(buf), TX_PARTS2, (unsigned)r->prof[0].parts, (unsigned)r->prof[1].parts);
    else snprintf(buf, sizeof(buf), TX_PARTS, (unsigned)r->prof[0].parts);
    uk_set_text(H.parts, buf);
    bool done = ((r->prof[0].cleared | (r->nh == 2 ? r->prof[1].cleared : 0)) >> r->chapter) & 1;
    snprintf(buf, sizeof(buf), done ? TX_HUB_CH_DONE : TX_HUB_CH, r->chapter + 1, CH_NAME[r->chapter]);
    uk_set_text(H.item[HUB_CHAPTER], buf);
    for (int u = 0; u < UPG_COUNT; u++) {
        if (r->nh == 2) snprintf(buf, sizeof(buf), TX_LV2, (unsigned)r->prof[0].lv[u], (unsigned)r->prof[1].lv[u]);
        else snprintf(buf, sizeof(buf), TX_LV, (unsigned)r->prof[0].lv[u]);
        uk_set_text(H.val[HUB_HP + u], buf);
    }
    for (int p = 0; p < r->nh; p++) {
        show(H.mark[p], !result); show(H.msg[p], !result);
        lv_obj_set_pos(H.mark[p], p ? 224 : 6, HUB_Y0 + r->hub_sel[p] * HUB_DY + 6);
        lv_obj_set_style_text_color(H.mark[p], lv_color_hex(r->ready[p] ? 0xFFD54F : p ? 0x90CAF9 : 0xEF9A9A), 0);
        hub_msg(r, p, buf, sizeof(buf));
        uk_set_text(H.msg[p], buf);
    }
    // 结果
    show(H.panel, result); show(H.pt, result); show(H.pn, result && r->event_t > 900);
    for (int i = 0; i < 4; i++) show(H.p[i], result);
    if (!result) return;
    const mr_chapter_t *c = &morph_chapters[r->played];
    char l[4][64] = { "", "", "", "" };
    if (r->phase == RUN_WIN) {
        snprintf(buf, sizeof(buf), TX_WIN, foe_name(c->boss));
        lv_obj_set_style_text_color(H.pt, lv_color_hex(0xFFD54F), 0);
        if (r->played == MR_CHAPTERS - 1) snprintf(l[0], sizeof(l[0]), "%s", TX_WIN_ALL);
        else snprintf(l[0], sizeof(l[0]), TX_WIN2, CH_NAME[r->played]);
        snprintf(l[1], sizeof(l[1]), TX_GOT_PARTS, (unsigned)r->gained);
        if (r->first_clear) {
            snprintf(l[2], sizeof(l[2]), "%s", TX_FIRST_BONUS);
            if (r->played + 1 < MR_CHAPTERS) snprintf(l[3], sizeof(l[3]), TX_NEW_CH, CH_NAME[r->played + 1]);
        }
    } else {
        snprintf(buf, sizeof(buf), "%s", TX_OVER);
        lv_obj_set_style_text_color(H.pt, lv_color_hex(0xEF5350), 0);
        snprintf(l[0], sizeof(l[0]), "%s", TX_OVER2);
        snprintf(l[1], sizeof(l[1]), TX_GOT_PARTS, (unsigned)r->gained);
    }
    uk_set_text(H.pt, buf);
    for (int i = 0; i < 4; i++) uk_set_text(H.p[i], l[i]);
}

// =====================================================================================================
void morph_gfx_build(lv_obj_t *scr, const morph_run_t *r) {
    s_scr = scr;
    s_kind = morph_gfx_page_for(r);
    if (s_kind == GP_BATTLE) build_battle(scr, r);
    else if (s_kind == GP_MAP) build_map(scr, r);
    else build_hub(scr, r);
}

void morph_gfx_update(const morph_run_t *r, uint32_t dt) {
    if (!s_scr) return;
    if (s_kind == GP_BATTLE) update_battle(r, dt);
    else if (s_kind == GP_MAP) update_map(r, dt);
    else update_hub(r);
}

void morph_gfx_destroy(void) { s_scr = NULL; }   // 对象都挂在页面上,换页时外壳删掉整页

static uint16_t count(lv_obj_t *o) {
    uint16_t n = 1;
    uint32_t k = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < k; i++) n = (uint16_t)(n + count(lv_obj_get_child(o, (int32_t)i)));
    return n;
}
uint16_t morph_gfx_obj_count(void) { return s_scr ? count(s_scr) : 0; }
