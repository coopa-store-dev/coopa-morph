// src/morph_battle.c —— 见 morph_battle.h。数值照网页小样 proto/game.html(用户试过、调过的那一版)。
#include "morph_battle.h"

#include <math.h>
#include <string.h>

#define L_EDGE 8
#define R_EDGE 232
#define DT 0.01f
#define DOWN_COMBO_MS 350   // ▼ 后多久内按 ● / ▲ 算技能 / 超必杀
#define PEND_MS 800         // 等队友击掌
#define RELAY_MS 1000       // 队友打飞的怪多久内能接力

const mbt_foe_def_t morph_foes[ET_COUNT] = {
    [ET_CLOCK] = { "clock", 44, 36, 24, 11, 34, 16, 20, -1, 0, false, false },
    [ET_ICE] = { "ice", 40, 32, 24, 11, 30, 14, 22, FORM_ICE, 0, false, false },
    [ET_VOLT] = { "volt", 38, 32, 24, 12, 40, 18, 12, FORM_VOLT, 0, false, false },
    [ET_BIGCLOCK] = { "bigclock", 170, 70, 30, 17, 28, 26, 30, -1, 1, true, false },
    [ET_BEAR] = { "bear", 520, 140, 46, 22, 26, 40, 46, -1, 3, false, true },
};
static const int16_t ULT_DMG[4] = { 0, 30, 55, 85 };
static const uint8_t ULT_BREAK[4] = { 0, 0, 1, 2 };
#define SUPER_DMG 150
#define COMBO_DMG 200

enum { HIT_KNOCK = 1, HIT_BREAK = 2, HIT_BIG = 4, HIT_FLAT = 8 };

static uint32_t rnd(morph_battle_t *b) {
    b->rng = b->rng * 1103515245u + 12345u;
    return b->rng >> 8;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t dec(int32_t v) { return v > MBT_STEP ? v - MBT_STEP : 0; }

static void ev(morph_battle_t *b, uint8_t type, uint8_t who, uint8_t a, int v, float x, float y) {
    if (b->ev_n == MBT_EVQ) {  // 满了丢最旧的:画面上少一个数字,不影响规则
        b->ev_head = (uint8_t)((b->ev_head + 1) % MBT_EVQ);
        b->ev_n--;
    }
    mbt_ev_t *e = &b->evq[(b->ev_head + b->ev_n++) % MBT_EVQ];
    e->type = type; e->who = who; e->a = a; e->v = (int16_t)v; e->x = (int16_t)x; e->y = (int16_t)y;
}
static void say(morph_battle_t *b, uint8_t what, int who, int v) { ev(b, BE_SAY, (uint8_t)who, what, v, 0, 0); }

bool morph_mbt_event(morph_battle_t *b, mbt_ev_t *out) {
    if (!b->ev_n) return false;
    *out = b->evq[b->ev_head];
    b->ev_head = (uint8_t)((b->ev_head + 1) % MBT_EVQ);
    b->ev_n--;
    return true;
}

static const mbt_foe_def_t *def(const mbt_foe_t *f) { return &morph_foes[f->type]; }
static bool foe_alive(const mbt_foe_t *f) { return f->used && f->hp > 0; }
int morph_mbt_alive(const morph_battle_t *b) {
    int n = 0;
    for (int i = 0; i < MBT_MAXF; i++) n += foe_alive(&b->f[i]);
    return n;
}
static bool standing(const mbt_hero_t *h) { return h->present && h->hp > 0; }
static mbt_hero_t *mate(morph_battle_t *b, int p) { return b->nh == 2 ? &b->h[1 - p] : NULL; }

static int spawn(morph_battle_t *b, uint8_t type, float x, int pct) {
    for (int i = 0; i < MBT_MAXF; i++) {
        mbt_foe_t *f = &b->f[i];
        if (f->used && f->hp > 0) continue;
        if (f->used && f->flash > 0) continue;  // 刚死、还在闪
        const mbt_foe_def_t *d = &morph_foes[type];
        memset(f, 0, sizeof(*f));
        f->used = true; f->type = type; f->x = x; f->face = -1;
        f->hp = f->max = (int16_t)(d->hp * pct / 100);
        f->st = FS_WALK; f->wait = 600 + (int32_t)(rnd(b) % 800);
        f->armor = d->armor; f->summon_t = 10000; f->knock_by = -1;
        return i;
    }
    return -1;
}

void morph_mbt_start(morph_battle_t *b, const uint8_t *types, int n, int hp_pct, int boss_pct, const mbt_carry_t *carry,
                     int nh, uint32_t seed) {
    memset(b, 0, sizeof(*b));
    b->rng = seed ? seed : 1;
    b->nh = (uint8_t)(nh == 2 ? 2 : 1);
    for (int p = 0; p < b->nh; p++) {
        mbt_hero_t *h = &b->h[p];
        h->present = true; h->x = 40.0f + p * 26; h->face = 1;
        h->hp = carry[p].hp; h->en = carry[p].en; h->form = carry[p].form;
        h->st = h->hp > 0 ? HS_IDLE : HS_DOWN; h->inv = 1000;
        h->last_down = h->roll_start = -10000;
    }
    bool boss = false, elite = false;
    for (int i = 0; i < n && i < MBT_MAXF; i++) {
        const mbt_foe_def_t *d = &morph_foes[types[i]];
        boss |= d->boss; elite |= d->elite;
        spawn(b, types[i], (i % 2) ? L_EDGE + 14 : R_EDGE - 14 - i * 8, d->boss ? boss_pct : hp_pct);
    }
    say(b, boss ? SAY_BOSS : elite ? SAY_ELITE : SAY_START, 0, 0);
}

static int nearest_foe(morph_battle_t *b, float x) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < MBT_MAXF; i++) {
        if (!foe_alive(&b->f[i])) continue;
        float d = fabsf(b->f[i].x - x);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static int nearest_hero(morph_battle_t *b, float x) {
    int best = -1; float bd = 1e9f;
    for (int p = 0; p < b->nh; p++) {
        if (!standing(&b->h[p])) continue;
        float d = fabsf(b->h[p].x - x);
        if (d < bd) { bd = d; best = p; }
    }
    return best;
}

static void gain(mbt_hero_t *h, int v) { h->en = (int16_t)clampi(h->en + v, 0, MBT_EN_MAX); }

// 打怪。p = 谁打的(-1 没人);stun 加晕
static void hit(morph_battle_t *b, int p, int fi, float dmg, int stun, unsigned fl) {
    mbt_foe_t *f = &b->f[fi];
    const mbt_foe_def_t *d = def(f);
    if (f->hp <= 0) return;
    float m = 1;
    if (b->slow_t > 0) m *= 2;
    if (f->stun_t > 0 && !(fl & HIT_FLAT)) m *= 1.5f;
    if (f->armor) {
        if (fl & HIT_BREAK) {
            f->armor--;
            say(b, f->armor ? SAY_ARMOR_CRACK : SAY_ARMOR_BREAK, 0, 0);
            if (b->shake_t < 250) b->shake_t = 250;
        } else {
            m *= 0.3f;
        }
    }
    int dd = (int)lroundf(dmg * m);
    if (dd < 1) dd = 1;
    f->hp = (int16_t)(f->hp - dd);
    f->flash = 120;
    ev(b, BE_HIT, (uint8_t)fi, (uint8_t)(((fl & HIT_BIG) ? 1 : 0) | (m >= 2 ? 2 : 0)), dd, f->x, f->y - d->h);
    b->combo++; b->combo_t = 1600;
    if (p >= 0) gain(&b->h[p], 4);
    if (f->stun_t <= 0 && f->freeze_t <= 0) {
        f->stun_g = (int16_t)(f->stun_g + stun);
        if (f->stun_g >= d->stun) {
            f->stun_g = 0; f->stun_t = d->boss ? 2500 : 3200; f->st = FS_STUN; f->atk = 0;
            say(b, d->boss || d->elite ? SAY_STUN_HIT : SAY_STUN_EAT, 0, 0);
        }
    }
    if ((fl & HIT_KNOCK) && !d->boss && !d->elite) {
        f->vx = (p >= 0 ? b->h[p].face : 1) * 120.0f; f->vy = -200; f->st = FS_AIR; f->atk = 0;
        f->knock_by = (int8_t)p; f->knock_at = b->now;
    } else if (f->st == FS_TELE && (fl & HIT_KNOCK)) {
        f->st = FS_WALK; f->atk = 0; f->wait = 800;
    }
    int stop = (fl & HIT_BIG) ? 90 : 50;
    if (b->stop_t < stop) b->stop_t = stop;
    if ((fl & HIT_BIG) && b->shake_t < 200) b->shake_t = 200;
    if (f->hp <= 0) { f->hp = 0; ev(b, BE_KILL, (uint8_t)fi, 0, 0, f->x, f->y); }
}

// ---------------- 人 ----------------
static void set_st(mbt_hero_t *h, uint8_t s) { h->st = s; h->t = 0; h->hit_done = false; }
static bool can_act(const mbt_hero_t *h) {
    return h->st == HS_IDLE || h->st == HS_WALK || (h->st == HS_ATK && h->t > 160) || (h->st == HS_ROLL && h->t > 240) ||
           h->st == HS_SHIELD;
}
static void face_to(mbt_hero_t *h, float x) { h->face = x > h->x ? 1 : -1; }
static void face_foe(morph_battle_t *b, mbt_hero_t *h) {
    int fi = nearest_foe(b, h->x);
    if (fi >= 0) face_to(h, b->f[fi].x);
}
static void start_shield(mbt_hero_t *h) { set_st(h, HS_SHIELD); h->shield_t = 1000; h->shield_age = 0; }

static void skill(morph_battle_t *b, int p) {
    mbt_hero_t *h = &b->h[p];
    h->skill_cd = 1400; face_foe(b, h); set_st(h, HS_SKILL);
    if (h->form == FORM_ICE) {
        for (int i = 0; i < MBT_MAXSHOT; i++) {
            if (b->shot[i].used) continue;
            b->shot[i] = (mbt_shot_t){ true, h->x + h->face * 10, h->face * 200.0f, 1200, (int8_t)p };
            break;
        }
    } else if (h->form == FORM_VOLT) {
        for (int i = 0; i < MBT_MAXF; i++)
            if (foe_alive(&b->f[i]) && fabsf(b->f[i].x - h->x) < 54) hit(b, p, i, 12, 20, 0);
    }
}

static void fire_ult(morph_battle_t *b, int p) {
    mbt_hero_t *h = &b->h[p];
    int lv = h->en / 100;
    if (lv < 1) { h->pend = false; return; }
    h->en = 0; h->pend = false;
    b->cine_on = true;
    b->cine = (mbt_cine_t){ .kind = CINE_ULT, .form = h->form, .lv = (uint8_t)lv, .p = (int8_t)p, .q = -1 };
    h->inv = 2000; set_st(h, HS_ULT);
    ev(b, BE_CINE, (uint8_t)p, CINE_ULT, lv, h->x, 0);
}

static int stunned_near(morph_battle_t *b, float x) {
    for (int i = 0; i < MBT_MAXF; i++) {
        const mbt_foe_t *f = &b->f[i];
        if (foe_alive(f) && f->stun_t > 0 && !def(f)->boss && !def(f)->elite && fabsf(f->x - x) < 44) return i;
    }
    return -1;
}
static int star_near(morph_battle_t *b, float x) {
    for (int i = 0; i < MBT_MAXSTAR; i++) if (b->star[i].used && fabsf(b->star[i].x - x) < 44) return i;
    return -1;
}
static int relay_target(morph_battle_t *b, int p) {
    for (int i = 0; i < MBT_MAXF; i++) {
        const mbt_foe_t *f = &b->f[i];
        if (foe_alive(f) && f->st == FS_AIR && f->knock_by >= 0 && f->knock_by != p && b->now - f->knock_at < RELAY_MS &&
            fabsf(f->x - b->h[p].x) < 80)
            return i;
    }
    return -1;
}

void morph_mbt_input(morph_battle_t *b, int p, uint8_t key, bool lp) {
    if (p < 0 || p >= b->nh || b->result || b->cine_on) return;
    mbt_hero_t *h = &b->h[p], *q = mate(b, p);
    if (!standing(h)) return;
    if (key == MBT_DOWN && !lp) {
        h->last_down = b->now;
        if (!can_act(h)) return;
        face_foe(b, h); set_st(h, HS_ROLL); h->inv = 280; h->roll_start = b->now;
        ev(b, BE_WHOOSH, (uint8_t)p, 0, 0, h->x, 0);
        return;
    }
    if (key == MBT_DOWN && lp) {
        if (h->st == HS_ROLL) h->want_shield = true;
        else if (can_act(h)) { start_shield(h); ev(b, BE_SHIELD, (uint8_t)p, 0, 0, h->x, 0); }
        return;
    }
    if (key == MBT_OK && !lp) {
        if (!can_act(h) && h->st != HS_ROLL) return;
        if (b->now - h->last_down < DOWN_COMBO_MS && h->skill_cd <= 0) { skill(b, p); return; }
        if (h->st == HS_ROLL) return;
        int ri = relay_target(b, p);
        if (ri >= 0) {
            face_to(h, b->f[ri].x); set_st(h, HS_CHASE); h->what_kind = 1; h->what_idx = (int8_t)ri; h->inv = 400;
            return;
        }
        h->chain = (uint8_t)(h->st == HS_ATK && h->chain_t > 0 && h->chain < 3 ? h->chain + 1 : 1);
        face_foe(b, h); set_st(h, HS_ATK);
        ev(b, BE_WHOOSH, (uint8_t)p, 1, 0, h->x, 0);
        return;
    }
    if (key == MBT_OK && lp) {
        if (h->st != HS_IDLE && h->st != HS_WALK && h->st != HS_ATK) return;
        if (q && q->present && q->hp <= 0 && fabsf(q->x - h->x) < 36) {
            face_to(h, q->x); set_st(h, HS_REVIVE); h->what_kind = 3;
            return;
        }
        int fi = stunned_near(b, h->x), si = fi < 0 ? star_near(b, h->x) : -1;
        if (fi >= 0 || si >= 0) {
            face_to(h, fi >= 0 ? b->f[fi].x : b->star[si].x);
            set_st(h, HS_SWALLOW); h->what_kind = fi >= 0 ? 1 : 2; h->what_idx = (int8_t)(fi >= 0 ? fi : si); h->inv = 1200;
            return;
        }
        face_foe(b, h); set_st(h, HS_CHARGE);
        return;
    }
    if (key == MBT_UP) {
        if (!(h->st == HS_IDLE || h->st == HS_WALK || h->st == HS_ATK || h->st == HS_SHIELD || h->st == HS_ROLL)) return;
        if (b->now - h->last_down < DOWN_COMBO_MS && h->en >= MBT_EN_MAX) {   // 超必杀
            h->en = 0; h->pend = false;
            b->cine_on = true;
            b->cine = (mbt_cine_t){ .kind = CINE_SUPER, .form = h->form, .lv = 3, .p = (int8_t)p, .q = -1, .low = h->hp < 30 };
            h->inv = 3000; set_st(h, HS_ULT);
            ev(b, BE_CINE, (uint8_t)p, CINE_SUPER, 3, h->x, 0);
            return;
        }
        if (h->en < 100) { say(b, SAY_NO_ENERGY, p, 0); return; }
        if (q && q->pend && standing(q)) {   // 队友在等击掌 → 合体大招
            int qp = 1 - p;
            uint8_t bars = (uint8_t)(q->pend_en / 100 + h->en / 100);
            q->en = 0; h->en = 0; q->pend = false;
            b->cine_on = true;
            b->cine = (mbt_cine_t){ .kind = CINE_COMBO, .form = h->form, .form2 = q->form, .bars = bars,
                                    .p = (int8_t)p, .q = (int8_t)qp };
            h->inv = q->inv = 3000; set_st(h, HS_ULT); set_st(q, HS_ULT);
            ev(b, BE_CINE, (uint8_t)p, CINE_COMBO, bars, h->x, 0);
            return;
        }
        if (q && standing(q) && q->en >= 100) { h->pend = true; h->pend_t = 0; h->pend_en = h->en; return; }
        fire_ult(b, p);
    }
}

static int front_foes(morph_battle_t *b, const mbt_hero_t *h, float reach, int *out) {
    int n = 0;
    for (int i = 0; i < MBT_MAXF; i++) {
        const mbt_foe_t *f = &b->f[i];
        if (!foe_alive(f) || f->y < -40) continue;
        if ((f->x - h->x) * h->face > -6 && fabsf(f->x - h->x) - def(f)->w / 2.0f < reach) out[n++] = i;
    }
    return n;
}

static void hurt_hero(morph_battle_t *b, int p, mbt_foe_t *f, int dmg) {
    mbt_hero_t *h = &b->h[p];
    if (!standing(h)) return;
    if (h->st == HS_ROLL && h->inv > 0) {
        if (b->now - h->roll_start < 220 && b->slow_t <= 0) { b->slow_t = 2000; gain(h, 50); say(b, SAY_PERFECT, p, 0); }
        return;
    }
    if (h->inv > 0 || h->st == HS_ULT) return;
    if (h->st == HS_SHIELD && (f->x - h->x) * h->face > 0) {
        if (h->shield_age < 200) {
            gain(h, 40);
            f->st = FS_STAGGER; f->t = 0; f->atk = 0; f->stun_g = (int16_t)(f->stun_g + 30);
            if (f->armor) { f->armor--; say(b, SAY_PARRY_ARMOR, p, 0); } else say(b, SAY_PARRY, p, 0);
            b->stop_t = 100; b->shake_t = 150;
            ev(b, BE_SHIELD, (uint8_t)p, 1, 0, h->x, 0);
            return;
        }
        dmg = (dmg + 2) / 5;
    }
    h->hp = (int16_t)(h->hp - dmg); h->flash = 150; gain(h, 10); h->pend = false;
    b->combo = 0;
    if (b->shake_t < 150) b->shake_t = 150;
    ev(b, BE_PHIT, (uint8_t)p, 0, dmg, h->x, 0);
    if (h->st != HS_SHIELD) set_st(h, HS_HURT);
    if (h->form != FORM_NORMAL) {   // 挨打掉能力:星星弹出来,3 秒内吞回来
        for (int i = 0; i < MBT_MAXSTAR; i++) {
            if (b->star[i].used) continue;
            b->star[i] = (mbt_star_t){ true, h->x, -20, -h->face * 60.0f, -160, 3000, h->form };
            break;
        }
        h->form = FORM_NORMAL;
        say(b, SAY_LOST_FORM, p, 0);
    }
    if (h->hp <= 0) {
        h->hp = 0; set_st(h, HS_DOWN);
        bool any = false;
        for (int k = 0; k < b->nh; k++) any |= standing(&b->h[k]);
        say(b, any ? SAY_DOWN : SAY_ALL_DOWN, p, 0);
    }
}

static void step_hero(morph_battle_t *b, int p) {
    mbt_hero_t *h = &b->h[p];
    if (!h->present) return;
    h->t += MBT_STEP; h->inv = dec(h->inv); h->chain_t = dec(h->chain_t);
    h->skill_cd = dec(h->skill_cd); h->flash = dec(h->flash);
    if (h->pend) {
        h->pend_t += MBT_STEP;
        if (h->pend_t > PEND_MS) { fire_ult(b, p); if (b->cine_on) return; }
    }
    if (h->hp <= 0) { h->st = HS_DOWN; return; }
    int tg = nearest_foe(b, h->x), hits[MBT_MAXF], n;
    switch (h->st) {
        case HS_IDLE:
        case HS_WALK:
            if (tg >= 0) {
                mbt_foe_t *f = &b->f[tg];
                face_to(h, f->x);
                float gap = fabsf(f->x - h->x) - def(f)->w / 2.0f;
                if ((gap > 20 && f->stun_t <= 0) || gap > 30) { h->st = HS_WALK; h->x += h->face * 70 * DT; }
                else h->st = HS_IDLE;
            } else {
                h->st = HS_IDLE;
            }
            break;
        case HS_ATK:
            if (!h->hit_done && h->t > 80) {
                static const int8_t DMG[4] = { 0, 6, 6, 11 };
                h->hit_done = true; h->x += h->face * 4;
                n = front_foes(b, h, 30, hits);
                for (int i = 0; i < n; i++)
                    hit(b, p, hits[i], DMG[h->chain], h->chain == 3 ? 16 : 8, h->chain == 3 ? (HIT_KNOCK | HIT_BIG) : 0);
                h->chain_t = 400;
            }
            if (h->t > 320) set_st(h, HS_IDLE);
            break;
        case HS_CHARGE:
            if (!h->hit_done && h->t > 100) {
                h->hit_done = true; h->x += h->face * 10; if (b->shake_t < 200) b->shake_t = 200;
                n = front_foes(b, h, 38, hits);
                for (int i = 0; i < n; i++) hit(b, p, hits[i], 24, 34, HIT_KNOCK | HIT_BIG | HIT_BREAK);
            }
            if (h->t > 450) set_st(h, HS_IDLE);
            break;
        case HS_CHASE: {   // 接力:跳到空中的怪那里砸一下,怪再被打飞(两人能轮流接)
            mbt_foe_t *f = &b->f[h->what_idx];
            if (foe_alive(f)) h->x += (f->x - h->face * 8 - h->x) * (DT * 14 > 1 ? 1 : DT * 14);
            if (!h->hit_done && h->t > 160) {
                h->hit_done = true;
                if (foe_alive(f)) { say(b, SAY_RELAY, p, 0); hit(b, p, h->what_idx, 16, 14, HIT_KNOCK | HIT_BIG); gain(h, 10); }
            }
            if (h->t > 400) set_st(h, HS_IDLE);
            break;
        }
        case HS_ROLL:
            if (h->t < 300) h->x -= h->face * 150 * DT;
            if (h->t > 340) {
                if (h->want_shield) { h->want_shield = false; start_shield(h); ev(b, BE_SHIELD, (uint8_t)p, 0, 0, h->x, 0); }
                else set_st(h, HS_IDLE);
            }
            break;
        case HS_SHIELD:
            h->shield_age += MBT_STEP; h->shield_t -= MBT_STEP;
            if (h->shield_t <= 0) set_st(h, HS_IDLE);
            break;
        case HS_SKILL:
            if (h->form == FORM_NORMAL && h->t < 180) {   // 冲刺斩
                h->x += h->face * 380 * DT; if (h->inv < 50) h->inv = 50;
                if (!h->hit_done && h->t > 80) {
                    h->hit_done = true;
                    n = front_foes(b, h, 34, hits);
                    for (int i = 0; i < n; i++) hit(b, p, hits[i], 14, 16, HIT_BIG);
                }
            }
            if (h->t > 320) set_st(h, HS_IDLE);
            break;
        case HS_SWALLOW: {
            float *wx = h->what_kind == 1 ? &b->f[h->what_idx].x : &b->star[h->what_idx].x;
            float k = DT * 8 > 1 ? 1 : DT * 8;
            *wx += (h->x + h->face * 8 - *wx) * k;
            if (h->t > 550) {
                int form = -1;
                if (h->what_kind == 1) {
                    mbt_foe_t *f = &b->f[h->what_idx];
                    if (foe_alive(f)) { form = def(f)->form; f->hp = 0; f->flash = 0; ev(b, BE_KILL, (uint8_t)h->what_idx, 1, 0, f->x, 0); }
                    if (form < 0) form = -2;   // 吞了没能力的:回血
                } else if (b->star[h->what_idx].used) {
                    form = b->star[h->what_idx].form; b->star[h->what_idx].used = false;
                }
                if (form >= 0) {
                    h->form = (uint8_t)form; set_st(h, HS_MORPH); h->inv = 700;
                    say(b, SAY_MORPH, p, form);
                } else {
                    if (form == -2) { h->hp = (int16_t)clampi(h->hp + 15, 0, 100); ev(b, BE_PHIT, (uint8_t)p, 1, -15, h->x, 0); }
                    set_st(h, HS_IDLE);
                }
            }
            break;
        }
        case HS_REVIVE: {
            mbt_hero_t *q = mate(b, p);
            if (!q || q->hp > 0) { set_st(h, HS_IDLE); break; }
            if (h->t > 800) {
                q->hp = 35; set_st(q, HS_IDLE); q->inv = 1500;
                say(b, SAY_REVIVED, 1 - p, 0);
                set_st(h, HS_IDLE);
            }
            break;
        }
        case HS_MORPH: if (h->t > 600) set_st(h, HS_IDLE); break;
        case HS_HURT: h->x -= h->face * 60 * DT; if (h->t > 350) set_st(h, HS_IDLE); break;
        case HS_DOWN: if (h->hp > 0) set_st(h, HS_IDLE); break;
        default: break;
    }
    h->x = clampf(h->x, L_EDGE + 6, R_EDGE - 6);
}

// ---------------- 怪 ----------------
static void start_attack(morph_battle_t *b, mbt_foe_t *f) {
    f->st = FS_TELE; f->t = 0; f->atk_done = false; f->atk_mask = 0;
    if (def(f)->boss && rnd(b) % 100 < 35) { f->atk = 2; f->tele = 1000; f->atk_dir = f->face; }
    else { f->atk = 1; f->tele = 800; }
}

static void step_foe(morph_battle_t *b, int fi, float dt, int32_t ms) {
    mbt_foe_t *f = &b->f[fi];
    const mbt_foe_def_t *d = def(f);
    if (!f->used) return;
    f->flash = f->flash > ms ? f->flash - ms : 0;
    if (f->hp <= 0) { if (!f->flash) f->used = false; return; }
    if (f->st == FS_AIR) {
        f->x += f->vx * dt; f->vy += 520 * dt; f->y += f->vy * dt;
        if (f->y >= 0) { f->y = 0; f->vy = 0; f->vx = 0; f->st = f->stun_t > 0 ? FS_STUN : FS_WALK; f->wait = 500; f->knock_by = -1; }
        f->x = clampf(f->x, L_EDGE, R_EDGE);
        return;
    }
    if (f->freeze_t > 0) { f->freeze_t = f->freeze_t > ms ? f->freeze_t - ms : 0; return; }
    if (f->stun_t > 0) {
        f->stun_t = f->stun_t > ms ? f->stun_t - ms : 0;
        if (!f->stun_t) { f->st = FS_WALK; f->wait = 400; }
        return;
    }
    int tp = nearest_hero(b, f->x);
    if (tp < 0) return;
    mbt_hero_t *tg = &b->h[tp];
    f->t += ms; f->face = tg->x > f->x ? 1 : -1;
    float gap = fabsf(tg->x - f->x) - d->w / 2.0f;
    if (d->boss) {
        f->summon_t -= ms;
        if (f->summon_t <= 0 && morph_mbt_alive(b) < 3) {
            f->summon_t = 11000;
            spawn(b, rnd(b) % 2 ? ET_ICE : ET_VOLT, f->x < 120 ? R_EDGE - 10 : L_EDGE + 10, 100);
            say(b, SAY_SUMMON, 0, 0);
        }
    }
    switch (f->st) {
        case FS_WALK:
            if (gap > d->reach - 4) f->x += f->face * d->speed * dt;
            else { f->wait -= ms; if (f->wait <= 0) start_attack(b, f); }
            break;
        case FS_TELE:
            if (f->t >= f->tele) { f->st = FS_ATK; f->t = 0; }
            break;
        case FS_ATK:
            if (f->atk == 2) {   // 熊冲撞:横冲过去,撞到谁算谁
                f->x += f->atk_dir * 170 * dt;
                for (int p = 0; p < b->nh; p++)
                    if (standing(&b->h[p]) && !(f->atk_mask & (1u << p)) && fabsf(b->h[p].x - f->x) < d->w / 2.0f + 6) {
                        f->atk_mask |= (uint8_t)(1u << p);
                        hurt_hero(b, p, f, 20);
                    }
                if (f->t > 1000 || f->x <= L_EDGE + d->w / 2.0f || f->x >= R_EDGE - d->w / 2.0f) { f->st = FS_WALK; f->wait = 1200; f->atk = 0; }
            } else {
                if (!f->atk_done && f->t > 20) {
                    f->atk_done = true;
                    for (int p = 0; p < b->nh; p++) {
                        mbt_hero_t *h = &b->h[p];
                        if (standing(h) && (h->x - f->x) * f->face > -8 && fabsf(h->x - f->x) - d->w / 2.0f < d->reach + 4)
                            hurt_hero(b, p, f, d->dmg);
                    }
                    if (d->boss && b->shake_t < 250) b->shake_t = 250;
                }
                if (f->t > 500) { f->st = FS_WALK; f->wait = (d->boss ? 600 : 900) + (int32_t)(rnd(b) % 900); f->atk = 0; }
            }
            break;
        case FS_STAGGER:
            if (f->t > 1200) { f->st = FS_WALK; f->wait = 300; }
            break;
        default: break;
    }
    f->x = clampf(f->x, L_EDGE + d->w / 2.0f, R_EDGE - d->w / 2.0f);
}

// ---------------- 特写放完:大招 / 超必杀 / 合体 ----------------
static void boom(morph_battle_t *b) {
    mbt_cine_t c = b->cine;
    b->cine_on = false;
    b->shake_t = c.kind == CINE_ULT ? 500 : 900; b->stop_t = 150;
    mbt_hero_t *h = &b->h[c.p];
    for (int i = 0; i < MBT_MAXF; i++) {
        mbt_foe_t *f = &b->f[i];
        if (!foe_alive(f)) continue;
        if (c.kind == CINE_ULT) {
            int n = c.form == FORM_NORMAL ? 4 + c.lv * 2 : c.form == FORM_VOLT ? c.lv + 1 : 1;
            if (c.form == FORM_NORMAL && fabsf(f->x - h->x) >= 70 + c.lv * 30) continue;   // 百裂拳打不到远处
            if (f->armor && ULT_BREAK[c.lv]) {
                f->armor = (uint8_t)(f->armor > ULT_BREAK[c.lv] ? f->armor - ULT_BREAK[c.lv] : 0);
                say(b, f->armor ? SAY_ARMOR_CRACK : SAY_ARMOR_BREAK, 0, 0);
            }
            for (int k = 0; k < n; k++) hit(b, c.p, i, (float)ULT_DMG[c.lv] / n, 24 / n, HIT_FLAT | (k == n - 1 ? HIT_BIG : 0));
            if (c.form == FORM_ICE && f->hp > 0) f->freeze_t = 1500 + c.lv * 1000;
        } else {
            float total = c.kind == CINE_SUPER ? SUPER_DMG * (c.low ? 2 : 1) : COMBO_DMG + 30 * (c.bars > 2 ? c.bars - 2 : 0);
            f->armor = 0;
            for (int k = 0; k < 6; k++) hit(b, c.p, i, total / 6, 0, HIT_FLAT | (k == 5 ? HIT_BIG : 0));
            if (f->hp > 0) { f->stun_g = 0; f->stun_t = def(f)->boss ? 3000 : 4000; f->st = FS_STUN; f->atk = 0; }
            if ((c.form == FORM_ICE || (c.kind == CINE_COMBO && c.form2 == FORM_ICE)) && f->hp > 0) f->freeze_t = 2000;
        }
    }
    if (c.kind == CINE_SUPER && c.low) say(b, SAY_LOW, c.p, 0);
    if (c.kind == CINE_COMBO) say(b, SAY_COMBO, c.p, 0);
    ev(b, BE_BOOM, (uint8_t)c.p, c.kind, 0, h->x, 0);
    for (int p = 0; p < b->nh; p++) if (b->h[p].st == HS_ULT) set_st(&b->h[p], HS_IDLE);
}

static void step(morph_battle_t *b) {
    b->now += MBT_STEP;
    b->shake_t = dec(b->shake_t);
    if (b->cine_on) {
        b->cine.t += MBT_STEP;
        if (b->cine.t > (b->cine.kind == CINE_ULT ? 1300 : 2200)) boom(b);
        return;
    }
    if (b->stop_t > 0) { b->stop_t = dec(b->stop_t); return; }   // 打中顿一下
    b->combo_t = dec(b->combo_t);
    if (!b->combo_t) b->combo = 0;
    for (int p = 0; p < b->nh; p++) step_hero(b, p);
    if (b->cine_on) return;
    bool slow = b->slow_t > 0;
    b->slow_t = dec(b->slow_t);
    int32_t fms = slow ? 3 : MBT_STEP;   // 慢动作:怪的时间只走 30%
    for (int i = 0; i < MBT_MAXF; i++) step_foe(b, i, fms / 1000.0f, fms);
    for (int i = 0; i < MBT_MAXSHOT; i++) {
        mbt_shot_t *s = &b->shot[i];
        if (!s->used) continue;
        s->x += s->vx * DT; s->t -= MBT_STEP;
        for (int k = 0; k < MBT_MAXF; k++) {
            mbt_foe_t *f = &b->f[k];
            if (foe_alive(f) && fabsf(f->x - s->x) < def(f)->w / 2.0f + 3 && f->y > -30) {
                hit(b, s->by, k, 10, 10, 0);
                f->freeze_t = def(f)->boss ? 800 : 1800;
                s->t = 0;
                break;
            }
        }
        if (s->t <= 0 || s->x < 0 || s->x > MBT_W) s->used = false;
    }
    for (int i = 0; i < MBT_MAXSTAR; i++) {
        mbt_star_t *s = &b->star[i];
        if (!s->used) continue;
        s->t -= MBT_STEP; s->x += s->vx * DT; s->vy += 500 * DT; s->y += s->vy * DT;
        if (s->y >= -6) { s->y = -6; s->vy = -s->vy * 0.5f; s->vx *= 0.7f; }
        s->x = clampf(s->x, 12, MBT_W - 12);
        if (s->t <= 0) s->used = false;
    }
    bool any = false;
    for (int p = 0; p < b->nh; p++) any |= standing(&b->h[p]);
    if (!any) { b->result = -1; return; }
    if (!morph_mbt_alive(b)) b->result = 1;
}

void morph_mbt_tick(morph_battle_t *b, uint32_t ms) {
    if (ms > 100) ms = 100;
    b->acc += (int32_t)ms;
    while (b->acc >= MBT_STEP && !b->result) {
        b->acc -= MBT_STEP;
        step(b);
    }
}

mbt_hint_t morph_mbt_hint(const morph_battle_t *b, int p) {
    morph_battle_t *m = (morph_battle_t *)b;   // 只读;下面的查找函数不改东西
    const mbt_hero_t *h = &b->h[p], *q = b->nh == 2 ? &b->h[1 - p] : NULL;
    if (h->hp <= 0) return HINT_WAIT_UP;
    if (q && q->present && q->hp <= 0 && fabsf(q->x - h->x) < 36) return HINT_REVIVE;
    if (stunned_near(m, h->x) >= 0 || star_near(m, h->x) >= 0) return HINT_EAT;
    if (q && q->pend && h->en >= 100) return HINT_HIGHFIVE;
    if (relay_target(m, p) >= 0) return HINT_RELAY;
    if (h->en >= MBT_EN_MAX) return HINT_SUPER;
    if (h->en >= 100) return HINT_ULT;
    return HINT_NONE;
}
