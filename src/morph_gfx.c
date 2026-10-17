// src/morph_gfx.c —— 见 morph_gfx.h。坐标照网页小样:场地横 0..240,地面 y = GROUND;人和小怪 16×16 放大两倍。
#include "morph_gfx.h"

#include <stdio.h>
#include <string.h>

#include "kit_sfx.h"
#include "morph_art.h"
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
static bool s_battle;   // 现在建的是战斗页(否则是地图页)

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

// ---------- 文字、条 ----------
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

// ---------- 名字 ----------
static const char *const FORM_NAME[FORM_COUNT] = { TX_F_NORMAL, TX_F_ICE, TX_F_VOLT };
static const char *const SKILL_NAME[FORM_COUNT] = { TX_SK_NORMAL, TX_SK_ICE, TX_SK_VOLT };
static const char *const ULT_NAME[FORM_COUNT] = { TX_UL_NORMAL, TX_UL_ICE, TX_UL_VOLT };
static const char *const SUP_NAME[FORM_COUNT] = { TX_SU_NORMAL, TX_SU_ICE, TX_SU_VOLT };
static const uint32_t FORM_COLOR[FORM_COUNT] = { 0xFF7043, 0xF48FB1, 0xFDD835 };
static const char *const SAY_TEXT[SAY_COUNT] = {
    TX_S_START, TX_S_ELITE, TX_S_BOSS, TX_S_ARMOR_CRACK, TX_S_ARMOR_BREAK, TX_S_STUN_EAT, TX_S_STUN_HIT,
    TX_S_NO_ENERGY, TX_S_RELAY, TX_S_MORPH, TX_S_PERFECT, TX_S_PARRY, TX_S_PARRY_ARMOR, TX_S_LOST_FORM, TX_S_DOWN,
    TX_S_ALL_DOWN, TX_S_REVIVED, TX_S_SUMMON, TX_S_LOW, TX_S_COMBO,
};
const char *morph_gfx_hero_name(int p) { return p ? TX_DAD : TX_KID; }
static const char *combo_name(uint8_t a, uint8_t b) {
    if (a > b) { uint8_t t = a; a = b; b = t; }
    if (a == b) return a == FORM_NORMAL ? TX_CO_NN : a == FORM_ICE ? TX_CO_II : TX_CO_VV;
    if (a == FORM_NORMAL) return b == FORM_ICE ? TX_CO_NI : TX_CO_NV;
    return TX_CO_IV;
}

// =====================================================================================================
// 战斗页
// =====================================================================================================
typedef struct { lv_obj_t *l; int32_t t; int16_t x, y; } num_t;
static struct {
    lv_obj_t *world;             // 场地上的东西都挂在它下面
    spr_t hero[2], hat[2], foe[MBT_MAXF], dizzy[MBT_MAXF], shot[MBT_MAXSHOT], star[MBT_MAXSTAR], spark[SPARKS];
    int32_t spark_t[SPARKS];
    bar_t fhp[MBT_MAXF];
    lv_obj_t *pip[3];
    lv_obj_t *name[2], *form[2];
    bar_t hp[2], en[2][3];
    bar_t boss; lv_obj_t *boss_name;
    lv_obj_t *say, *combo, *hint[2], *slow, *slow_l, *hi5[2];
    int8_t form_now[2];          // 顶上一条显示的变身(-1 = 还没写)
    int32_t say_t;
    num_t num[NUMS];
    lv_obj_t *cine, *cine_a, *cine_b, *cine_c;
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

void morph_gfx_battle(lv_obj_t *scr, const morph_run_t *r) {
    morph_gfx_destroy();
    s_scr = scr; s_battle = true;
    memset(&B, 0, sizeof(B));
    B.form_now[0] = B.form_now[1] = -1;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1D1B22), 0);
    bool boss = r->node[r->r][r->i].type == NODE_BOSS;
    // 背景:天、玩具工厂的楼、地面(背景不动,只建一次)
    uk_rect(scr, 0, HUD_H, 240, GROUND - HUD_H, boss ? 0x4A148C : 0x5C6BC0);
    uk_rect(scr, 0, 150, 40, GROUND - 150, 0x3949AB);
    uk_rect(scr, 50, 120, 34, GROUND - 120, 0x3949AB);
    uk_rect(scr, 150, 135, 46, GROUND - 135, 0x3949AB);
    uk_rect(scr, 206, 110, 34, GROUND - 110, 0x3949AB);
    for (int i = 0; i < 6; i++) uk_rect(scr, 58 + (i % 2) * 14, 130 + (i / 2) * 26, 8, 10, 0xFFE082);
    lv_obj_t *g = lv_image_create(scr);
    lv_image_set_src(g, morph_art_tile(MT_IMG_TOP));
    lv_obj_set_size(g, 240, HINT_Y - GROUND);
    lv_image_set_inner_align(g, LV_IMAGE_ALIGN_TILE);
    lv_obj_set_pos(g, 0, GROUND);
    // 场地上的东西
    B.world = lv_obj_create(scr);
    lv_obj_remove_style_all(B.world);
    lv_obj_set_size(B.world, 240, GROUND + 8);
    lv_obj_set_pos(B.world, 0, 0);
    for (int i = 0; i < MBT_MAXF; i++) {
        spr_new(&B.foe[i], B.world); spr_new(&B.dizzy[i], B.world);
        bar_new(&B.fhp[i], B.world, 0, 0, 20, 2, 0xE53935); bar_show(&B.fhp[i], false);
    }
    for (int i = 0; i < 3; i++) { B.pip[i] = uk_rect(B.world, 0, 0, 7, 4, 0xCFD8DC); show(B.pip[i], false); }
    for (int p = 0; p < 2; p++) { spr_new(&B.hero[p], B.world); spr_new(&B.hat[p], B.world); }
    for (int i = 0; i < MBT_MAXSHOT; i++) spr_new(&B.shot[i], B.world);
    for (int i = 0; i < MBT_MAXSTAR; i++) spr_new(&B.star[i], B.world);
    for (int i = 0; i < SPARKS; i++) spr_new(&B.spark[i], B.world);
    for (int i = 0; i < NUMS; i++) { B.num[i].l = label(B.world, UK_F12, 0xFFFFFF, 0, 0, 0, false); show(B.num[i].l, false); }
    for (int p = 0; p < 2; p++) { B.hi5[p] = label(B.world, UK_F12, 0xFFEB3B, 0, 0, 0, false); uk_set_text(B.hi5[p], TX_HIGHFIVE); show(B.hi5[p], false); }
    // 慢动作的罩子
    B.slow = uk_rect(scr, 0, HUD_H, 240, GROUND - HUD_H, 0x4FC3F7);
    lv_obj_set_style_bg_opa(B.slow, LV_OPA_20, 0);
    show(B.slow, false);
    B.slow_l = label(scr, UK_F12, 0xB3E5FC, 0, 70, 240, true);
    uk_set_text(B.slow_l, TX_SLOW);
    show(B.slow_l, false);
    // 顶上一条、boss 血条、底下提示
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
    // 大招特写(盖满全屏)
    B.cine = uk_rect(scr, 0, 0, 240, 320, 0x000000);
    B.cine_a = label(scr, UK_F12, 0xFFFFFF, 0, 112, 240, true);
    B.cine_b = label(scr, UK_F36, 0xFFFFFF, 0, 140, 240, true);
    B.cine_c = label(scr, UK_F12, 0xFFEB3B, 0, 196, 240, true);
    show(B.cine, false); show(B.cine_a, false); show(B.cine_b, false); show(B.cine_c, false);
}

static void say_text(const char *s) {
    uk_set_text(B.say, s);
    show(B.say, true);
    B.say_t = 1300;
}

void morph_gfx_battle_event(const morph_run_t *r, const mbt_ev_t *e) {
    if (!s_battle) return;
    char buf[64];
    switch (e->type) {
        case BE_HIT: {
            num_t *n = &B.num[0];
            for (int i = 0; i < NUMS; i++) if (B.num[i].t <= 0) { n = &B.num[i]; break; }
            snprintf(buf, sizeof(buf), "%d", e->v);
            uk_set_text(n->l, buf);
            lv_obj_set_style_text_color(n->l, lv_color_hex(e->a & 2 ? 0x4FC3F7 : 0xFFFFFF), 0);
            n->t = 800; n->x = e->x; n->y = (int16_t)(GROUND + e->y - 10);
            show(n->l, true);
            for (int i = 0; i < SPARKS; i++)
                if (B.spark_t[i] <= 0) { B.spark_t[i] = 150; spr_put(&B.spark[i], e->a & 1 ? morph_art_fx2(MX_SPARK) : morph_art_fx(MX_SPARK), 1, e->x, GROUND + e->y + 10); break; }
            sfx_play(SFX_HIT);
            break;
        }
        case BE_PHIT: {
            num_t *n = &B.num[0];
            for (int i = 0; i < NUMS; i++) if (B.num[i].t <= 0) { n = &B.num[i]; break; }
            snprintf(buf, sizeof(buf), e->v < 0 ? "+%d" : "%d", e->v < 0 ? -e->v : e->v);
            uk_set_text(n->l, buf);
            lv_obj_set_style_text_color(n->l, lv_color_hex(e->v < 0 ? 0x81C784 : 0xEF5350), 0);
            n->t = 800; n->x = e->x; n->y = GROUND - 50;
            show(n->l, true);
            if (e->v > 0) sfx_play(SFX_HIT);
            break;
        }
        case BE_SAY:
            if (e->a == SAY_MORPH) snprintf(buf, sizeof(buf), TX_S_MORPH, morph_gfx_hero_name(e->who), FORM_NAME[e->v % FORM_COUNT]);
            else if (e->a == SAY_DOWN || e->a == SAY_REVIVED) snprintf(buf, sizeof(buf), SAY_TEXT[e->a], morph_gfx_hero_name(e->who));
            else snprintf(buf, sizeof(buf), "%s", e->a < SAY_COUNT ? SAY_TEXT[e->a] : "");
            say_text(buf);
            if (e->a == SAY_PERFECT || e->a == SAY_PARRY || e->a == SAY_PARRY_ARMOR) sfx_play(SFX_PERFECT);
            if (e->a == SAY_MORPH) sfx_play(SFX_LEVEL_UP);
            if (e->a == SAY_BOSS) sfx_play(SFX_CHAIN);
            break;
        case BE_CINE: sfx_play(SFX_CHAIN); break;
        case BE_BOOM: sfx_play(SFX_HIT); break;
        case BE_WHOOSH: sfx_play(SFX_WHOOSH); break;
        case BE_SHIELD: if (e->a) sfx_play(SFX_DODGE); break;
        default: break;
    }
    (void)r;
}

static uint8_t hero_pose(const mbt_hero_t *h, int32_t anim) {
    switch (h->st) {
        case HS_WALK: return (anim / 160) % 2 ? MA_WALK1 : MA_WALK2;
        case HS_ATK: return h->t < 200 ? MA_JAB : MA_IDLE;
        case HS_CHARGE: return h->t < 100 ? MA_WINDUP : MA_HEAVY;
        case HS_CHASE: return h->t < 160 ? MA_JUMP : MA_HEAVY;
        case HS_SHIELD: return MA_GUARD;
        case HS_SKILL: return MA_SPECIAL;
        case HS_SWALLOW: return MA_SPECIAL;
        case HS_REVIVE: return MA_WINDUP;
        case HS_MORPH: case HS_ULT: return MA_WIN;
        case HS_HURT: return MA_HIT;
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
        default: snprintf(buf, sizeof(buf), b->nh == 2 ? TX_H_NONE2 : TX_H_NONE, SKILL_NAME[h->form]); break;
    }
    uk_set_text(B.hint[p], buf);
}

static void update_cine(const morph_battle_t *b) {
    bool on = b->cine_on;
    show(B.cine, on); show(B.cine_a, on); show(B.cine_b, on); show(B.cine_c, on);
    if (!on) return;
    const mbt_cine_t *c = &b->cine;
    char buf[32];
    if (c->kind == CINE_ULT) {
        lv_obj_set_style_bg_color(B.cine, lv_color_hex(0x000000), 0);
        uk_set_text(B.cine_a, morph_gfx_hero_name(c->p));
        uk_set_text(B.cine_b, ULT_NAME[c->form]);
        snprintf(buf, sizeof(buf), TX_CINE_LV, c->lv);
        uk_set_text(B.cine_c, buf);
    } else {
        uint32_t flash = (c->t / 100) % 2 ? FORM_COLOR[c->form] : (c->kind == CINE_COMBO ? FORM_COLOR[c->form2] : 0x000000);
        lv_obj_set_style_bg_color(B.cine, lv_color_hex(flash), 0);
        uk_set_text(B.cine_a, c->kind == CINE_SUPER ? TX_CINE_SUPER : TX_CINE_COMBO);
        uk_set_text(B.cine_b, c->kind == CINE_SUPER ? SUP_NAME[c->form] : combo_name(c->form, c->form2));
        uk_set_text(B.cine_c, c->kind == CINE_COMBO ? TX_CINE_PAIR : c->low ? TX_CINE_LOW : morph_gfx_hero_name(c->p));
    }
    lv_obj_set_style_text_color(B.cine_b, lv_color_hex(c->kind == CINE_ULT ? FORM_COLOR[c->form] : 0xFFFFFF), 0);
}

static void update_battle(const morph_run_t *r, uint32_t dt) {
    const morph_battle_t *b = &r->bt;
    B.anim += (int32_t)dt;
    // 不震屏:整块场地一挪就是整屏重画(真卡上一次 60–70 ms),打中的顿一下和火花已经够有感觉
    // 人
    for (int p = 0; p < b->nh; p++) {
        const mbt_hero_t *h = &b->h[p];
        bool left = h->face < 0;
        int hx = (int)h->x, lift = 0;
        if (h->st == HS_CHASE) { int t = h->t > 400 ? 400 : h->t; lift = (t < 200 ? t : 400 - t) / 5; }
        spr_put(&B.hero[p], morph_art_pose2((uint8_t)p, hero_pose(h, B.anim), left), 1, hx, GROUND - lift);
        spr_rot(&B.hero[p], h->st == HS_ROLL ? (int)(h->t * 12 % 3600) * (left ? -1 : 1) : 0);
        uint32_t tint = 0; uint8_t opa = 0;
        if (h->flash > 0) { tint = 0xFFFFFF; opa = LV_OPA_70; }
        else if (h->st == HS_MORPH && (h->t / 80) % 2) { tint = FORM_COLOR[h->form]; opa = LV_OPA_60; }
        else if (h->st == HS_REVIVE) { tint = 0x81C784; opa = LV_OPA_40; }
        spr_tint(&B.hero[p], tint, opa);
        if (h->form != FORM_NORMAL && h->st != HS_DOWN && h->st != HS_ROLL)
            spr_put(&B.hat[p], morph_art_fx2(h->form == FORM_ICE ? MX_HAT_ICE : MX_HAT_VOLT), 1, hx + (left ? -1 : 1), GROUND - lift - 26);
        else spr_show(&B.hat[p], false);
        show(B.hi5[p], h->pend);
        if (h->pend) lv_obj_set_pos(B.hi5[p], hx - 15, GROUND - 62);
        // 顶上一条
        if (B.form_now[p] != h->form) {
            B.form_now[p] = (int8_t)h->form;
            uk_set_text(B.form[p], FORM_NAME[h->form]);
            lv_obj_set_style_text_color(B.form[p], lv_color_hex(FORM_COLOR[h->form]), 0);
        }
        bar_set(&B.hp[p], h->hp, 100, h->hp < 30 ? 0xE53935 : 0x43A047);
        for (int k = 0; k < 3; k++) bar_set(&B.en[p][k], h->en - k * 100, 100, h->en >= (k + 1) * 100 ? 0xFFB300 : 0xA0700F);
        update_hint(b, p);
    }
    // 怪
    int armored = -1;
    for (int i = 0; i < MBT_MAXF; i++) {
        const mbt_foe_t *f = &b->f[i];
        if (!f->used) { spr_show(&B.foe[i], false); spr_show(&B.dizzy[i], false); bar_show(&B.fhp[i], false); continue; }
        const mbt_foe_def_t *d = &morph_foes[f->type];
        uint8_t img = f->type == ET_ICE ? MF_ICE : f->type == ET_VOLT ? MF_VOLT : f->type == ET_BEAR ? MF_BEAR : MF_CLOCK;
        int k = f->type == ET_BIGCLOCK ? 3 : 2;   // 大发条兵是发条兵的三倍(只有它在画面上放大:1.5 × 两倍图)
        uint8_t frame = f->st == FS_WALK ? (uint8_t)((B.anim / 220) % 2) : 0;
        int fx = (int)f->x, fy = GROUND + (int)f->y;
        spr_put_scaled(&B.foe[i], morph_art_foe2(img, frame, f->face < 0), k * 128, fx, fy);
        uint32_t tint = 0; uint8_t opa = 0;
        if (f->hp <= 0) { tint = 0xFFFFFF; opa = LV_OPA_COVER; }
        else if (f->flash > 0) { tint = 0xFFFFFF; opa = LV_OPA_70; }
        else if (f->st == FS_TELE && (f->t / 80) % 2 == 0) { tint = 0xFF1744; opa = LV_OPA_70; }
        else if (f->freeze_t > 0) { tint = 0xB3E5FC; opa = LV_OPA_60; }
        else if (f->armor) { tint = 0xCFD8DC; opa = LV_OPA_30; }
        spr_tint(&B.foe[i], tint, opa);
        int top = fy - d->h * k / 2 - (f->type == ET_BEAR ? 24 : 12);
        if (f->hp > 0 && f->stun_t > 0) spr_put(&B.dizzy[i], morph_art_fx(MX_STAR), 1, fx + (int)((B.anim / 60) % 21) - 10, top);
        else spr_show(&B.dizzy[i], false);
        bool small = !d->boss && !d->elite && f->hp > 0;
        if (small) {
            lv_obj_set_pos(B.fhp[i].bg, fx - 10, top - 4);
            lv_obj_set_pos(B.fhp[i].fill, fx - 10, top - 4);
            bar_set(&B.fhp[i], f->hp, f->max, 0xE53935);
        }
        bar_show(&B.fhp[i], small);
        if (f->armor && f->hp > 0) armored = i;
        if ((d->boss || d->elite) && f->hp > 0) {
            bar_show(&B.boss, true);
            bar_set(&B.boss, f->hp, f->max, 0xE53935);
            show(B.boss_name, true);
            uk_set_text(B.boss_name, d->boss ? TX_E_BEAR : TX_E_BIGCLOCK);
        }
    }
    for (int k = 0; k < 3; k++) {
        bool on = armored >= 0 && k < b->f[armored].armor;
        show(B.pip[k], on);
        if (on) lv_obj_set_pos(B.pip[k], (int)b->f[armored].x - 13 + k * 9, GROUND - 68);
    }
    if (armored < 0) {
        bool any = false;
        for (int i = 0; i < MBT_MAXF; i++) any |= b->f[i].used && b->f[i].hp > 0 && (morph_foes[b->f[i].type].boss || morph_foes[b->f[i].type].elite);
        if (!any) { bar_show(&B.boss, false); show(B.boss_name, false); }
    }
    for (int i = 0; i < MBT_MAXSHOT; i++) {
        if (b->shot[i].used) spr_put(&B.shot[i], morph_art_fx2(MX_ICEBALL), 1, (int)b->shot[i].x, GROUND - 12);
        else spr_show(&B.shot[i], false);
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
    if (B.say_t > 0) { B.say_t -= (int32_t)dt; if (B.say_t <= 0) show(B.say, false); }
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
    lv_obj_t *line[MR_ROWS][MR_COLS][MR_COLS];
    spr_t hero[2];
    lv_obj_t *hint_bg, *hint, *desc, *panel, *pt, *p1, *p2, *pn;
    int8_t sel_r, sel_i;
    bool done[MR_ROWS][MR_COLS];   // 已经涂灰了
    int32_t anim;
} M;

static const char *const NODE_CH[NODE_COUNT] = { TX_N_START, TX_N_BATTLE, TX_N_ELITE, TX_N_REST, TX_N_TREASURE, TX_N_BOSS };
static const uint32_t NODE_COLOR[NODE_COUNT] = { 0x78909C, 0xE53935, 0x8E24AA, 0x43A047, 0xFB8C00, 0x212121 };
static const char *const NODE_DESC[NODE_COUNT] = { "", TX_D_BATTLE, TX_D_ELITE, TX_D_REST, TX_D_TREASURE, TX_D_BOSS };

void morph_gfx_map(lv_obj_t *scr, const morph_run_t *r) {
    morph_gfx_destroy();
    s_scr = scr; s_battle = false;
    memset(&M, 0, sizeof(M));
    M.sel_r = M.sel_i = -1;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x33691E), 0);
    lv_obj_t *t = label(scr, UK_F12, 0xFFFFFF, 0, 4, 240, true);
    uk_set_text(t, TX_MAP);
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
                lv_obj_set_style_line_color(l, lv_color_hex(0xA5D6A7), 0);
                M.line[row][i][j] = l;
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
    for (int p = 0; p < r->nh; p++)
        spr_put(&M.hero[p], morph_art_pose((uint8_t)p, r->walking ? ((M.anim / 160) % 2 ? MA_WALK1 : MA_WALK2) : MA_IDLE, false), 1,
                px - (r->nh == 2 ? 7 : 0) + p * 14, py - 6 - bob);
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

void morph_gfx_update(const morph_run_t *r, uint32_t dt) {
    if (!s_scr) return;
    if (s_battle) update_battle(r, dt);
    else update_map(r, dt);
}

void morph_gfx_destroy(void) { s_scr = NULL; }   // 对象都挂在页面上,换页时外壳删掉整页

static uint16_t count(lv_obj_t *o) {
    uint16_t n = 1;
    uint32_t k = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < k; i++) n = (uint16_t)(n + count(lv_obj_get_child(o, (int32_t)i)));
    return n;
}
uint16_t morph_gfx_obj_count(void) { return s_scr ? count(s_scr) : 0; }
