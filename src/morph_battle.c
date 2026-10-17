// src/morph_battle.c —— 见 morph_battle.h。第 1 关的数值照网页小样 proto/game.html(用户试过、调过的那一版)。
#include "morph_battle.h"

#include <math.h>
#include <string.h>

#define L_EDGE 8
#define R_EDGE 232
#define DT 0.01f
#define DOWN_COMBO_MS 350   // ▼ 后多久内按 ● / ▲ 算技能 / 超必杀
#define PEND_MS 800         // 等队友击掌
#define RELAY_MS 1000       // 队友打飞的怪多久内能接力
#define AGGRO_MS 5000       // 冰盾 Boss 盯一个人多久转身
#define OPEN_MS 10000       // 线圈全坏了,Boss 多久不无敌
#define TRAP_MS 4000        // 冻在冰块里最多多久
#define TRAP_TAPS 6         // 自己敲几下出来

#define NO ET_COUNT
const mbt_foe_def_t morph_foes[ET_COUNT] = {
    //                 名字       血 晕 够得着 伤害 速度 宽 高 变身        甲 精英   Boss   招        标记                    帮手
    [ET_CLOCK] = { "clock", 44, 36, 24, 11, 34, 16, 20, -1, 0, false, false, FA_SLAP, 0, { NO, NO } },
    [ET_ICE] = { "ice", 40, 32, 24, 11, 30, 14, 22, FORM_ICE, 0, false, false, FA_SLAP, 0, { NO, NO } },
    [ET_VOLT] = { "volt", 38, 32, 24, 12, 40, 18, 12, FORM_VOLT, 0, false, false, FA_SLAP, 0, { NO, NO } },
    [ET_BIGCLOCK] = { "bigclock", 170, 70, 30, 17, 28, 26, 30, -1, 1, true, false, FA_SLAP, 0, { NO, NO } },
    [ET_BEAR] = { "bear", 520, 140, 46, 22, 26, 40, 46, -1, 3, false, true, FA_SLAP, 0, { ET_ICE, ET_VOLT } },
    [ET_SNOW] = { "snow", 36, 30, 130, 8, 30, 14, 18, -1, 0, false, false, FA_SHOOT, FD_RANGED, { NO, NO } },
    [ET_FROG] = { "frog", 42, 32, 90, 12, 36, 16, 14, FORM_SPRING, 0, false, false, FA_LEAP, 0, { NO, NO } },
    [ET_SNOWMAN] = { "snowman", 190, 80, 140, 13, 24, 26, 30, -1, 2, true, false, FA_SHOOT, FD_RANGED, { NO, NO } },
    [ET_ICEKING] = { "iceking", 600, 150, 46, 20, 24, 40, 46, -1, 0, false, true, FA_SLAP, FD_GUARD, { ET_SNOW, ET_FROG } },
    [ET_MAGNET] = { "magnet", 46, 34, 110, 11, 30, 16, 16, FORM_MAGNET, 0, false, false, FA_PULL, 0, { NO, NO } },
    [ET_ROCKET] = { "rocket", 40, 30, 140, 9, 26, 16, 16, FORM_ROCKET, 0, false, false, FA_SHOOT, FD_RANGED, { NO, NO } },
    [ET_BIGVOLT] = { "bigvolt", 200, 80, 34, 16, 30, 26, 20, -1, 2, true, false, FA_RING, 0, { NO, NO } },
    [ET_DYNAMO] = { "dynamo", 520, 150, 46, 22, 22, 40, 48, -1, 0, false, true, FA_SLAP, FD_COILS, { ET_VOLT, ET_MAGNET } },
    [ET_COIL] = { "coil", 60, 999, 0, 0, 0, 16, 26, -1, 0, false, false, FA_NONE, FD_OBJECT, { NO, NO } },
    [ET_DEMON] = { "demon", 800, 170, 48, 24, 28, 44, 48, -1, 0, false, true, FA_SLAP, FD_GUARD, { ET_FROG, ET_ROCKET } },
};
#undef NO
static const int16_t ULT_DMG[4] = { 0, 30, 55, 85 };
static const uint8_t ULT_BREAK[4] = { 0, 0, 1, 2 };
#define SUPER_DMG 150
#define COMBO_DMG 200

enum { HIT_KNOCK = 1, HIT_BREAK = 2, HIT_BIG = 4, HIT_FLAT = 8 };
enum { HR_MISS = 0, HR_PERFECT, HR_PARRY, HR_HIT };   // 怪打人的结果

static uint32_t rnd(morph_battle_t *b) {
    b->rng = b->rng * 1103515245u + 12345u;
    return b->rng >> 8;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t dec(int32_t v) { return v > MBT_STEP ? v - MBT_STEP : 0; }
static int32_t decm(int32_t v, int32_t ms) { return v > ms ? v - ms : 0; }

static void ev(morph_battle_t *b, uint8_t type, uint8_t who, uint8_t a, int v, float x, float y) {
    if (b->ev_n == MBT_EVQ) {  // 满了丢最旧的:画面上少一个数字,不影响规则
        b->ev_head = (uint8_t)((b->ev_head + 1) % MBT_EVQ);
        b->ev_n--;
    }
    mbt_ev_t *e = &b->evq[(b->ev_head + b->ev_n++) % MBT_EVQ];
    e->type = type; e->who = who; e->a = a; e->v = (int16_t)v; e->x = (int16_t)x; e->y = (int16_t)y;
}
static void say(morph_battle_t *b, uint8_t what, int who, int v) { ev(b, BE_SAY, (uint8_t)who, what, v, 0, 0); }
// 常冒的那几句(冰盾挡住、无敌)隔一会儿才说一次
static void say_now_and_then(morph_battle_t *b, uint8_t what) {
    if (b->say_cd > 0) return;
    b->say_cd = 2500;
    say(b, what, 0, 0);
}

bool morph_mbt_event(morph_battle_t *b, mbt_ev_t *out) {
    if (!b->ev_n) return false;
    *out = b->evq[b->ev_head];
    b->ev_head = (uint8_t)((b->ev_head + 1) % MBT_EVQ);
    b->ev_n--;
    return true;
}

static const mbt_foe_def_t *def(const mbt_foe_t *f) { return &morph_foes[f->type]; }
static bool foe_alive(const mbt_foe_t *f) { return f->used && f->hp > 0; }
static bool is_coil(const mbt_foe_t *f) { return f->type == ET_COIL; }
static bool small(const mbt_foe_t *f) { return !def(f)->boss && !def(f)->elite && !is_coil(f); }
int morph_mbt_alive(const morph_battle_t *b) {
    int n = 0;
    for (int i = 0; i < MBT_MAXF; i++) n += foe_alive(&b->f[i]) && !is_coil(&b->f[i]);
    return n;
}
static bool standing(const mbt_hero_t *h) { return h->present && h->hp > 0; }
static bool free_hero(const mbt_hero_t *h) { return standing(h) && h->st != HS_TRAP; }
static mbt_hero_t *mate(morph_battle_t *b, int p) { return b->nh == 2 ? &b->h[1 - p] : NULL; }
static float pmul(const mbt_hero_t *h) { return 1.0f + 0.15f * h->atk_lv; }
static float umul(const mbt_hero_t *h) { return 1.0f + 0.2f * h->ult_lv; }
static int fdmg(const morph_battle_t *b, int dmg) { return (dmg * b->dmg_pct + 50) / 100; }

// 线圈 / 冰盾这两套机关现在开着没有
static bool has_coils(const mbt_foe_t *f) { return (def(f)->flags & FD_COILS) || (f->type == ET_DEMON && f->phase == 1); }
static bool has_guard(const mbt_foe_t *f) { return (def(f)->flags & FD_GUARD) && !(f->type == ET_DEMON && f->phase != 0); }
bool morph_mbt_guarding(const mbt_foe_t *f) { return foe_alive(f) && has_guard(f) && f->guard_t <= 0; }
bool morph_mbt_shielded(const morph_battle_t *b, int fi) {
    const mbt_foe_t *f = &b->f[fi];
    if (!foe_alive(f) || !has_coils(f)) return false;
    for (int i = 0; i < MBT_MAXF; i++) if (is_coil(&b->f[i]) && foe_alive(&b->f[i])) return true;
    return false;
}

static int spawn(morph_battle_t *b, uint8_t type, float x, int pct) {
    for (int i = 0; i < MBT_MAXF; i++) {
        mbt_foe_t *f = &b->f[i];
        if (f->used && (f->hp > 0 || is_coil(f))) continue;
        if (f->used && f->flash > 0) continue;  // 刚死、还在闪
        const mbt_foe_def_t *d = &morph_foes[type];
        memset(f, 0, sizeof(*f));
        f->used = true; f->type = type; f->x = x; f->face = -1;
        f->hp = f->max = (int16_t)(type == ET_COIL ? (b->nh == 2 ? 80 : 45) : d->hp * pct / 100);
        f->st = FS_WALK; f->wait = 600 + (int32_t)(rnd(b) % 800);
        f->armor = d->armor; f->summon_t = 10000; f->knock_by = -1; f->trap_cd = 5000;
        return i;
    }
    return -1;
}
static void spawn_coils(morph_battle_t *b) {
    spawn(b, ET_COIL, L_EDGE + 10, 100);
    spawn(b, ET_COIL, R_EDGE - 10, 100);
}

void morph_mbt_start(morph_battle_t *b, const mbt_setup_t *s) {
    memset(b, 0, sizeof(*b));
    b->rng = s->seed ? s->seed : 1;
    b->nh = (uint8_t)(s->nh == 2 ? 2 : 1);
    b->dmg_pct = (uint16_t)(s->dmg_pct > 0 ? s->dmg_pct : 100);
    for (int i = 0; i < s->nwave && i < MBT_MAXF; i++) b->wave[b->nwave++] = s->wave[i];
    b->wave_pct = (int16_t)s->hp_pct;
    bool boss = false, elite = false;
    for (int i = 0; i < s->n && i < MBT_MAXF; i++) {
        boss |= morph_foes[s->types[i]].boss;
        elite |= morph_foes[s->types[i]].elite;
    }
    for (int p = 0; p < b->nh; p++) {
        mbt_hero_t *h = &b->h[p];
        const mbt_carry_t *c = &s->carry[p];
        h->present = true; h->x = 40.0f + p * 26; h->face = 1;
        if (boss && p == 1) { h->x = R_EDGE - 30; h->face = -1; }   // 打 Boss 时两个人一边一个
        h->maxhp = c->maxhp > 0 ? c->maxhp : 100;
        h->hp = (int16_t)clampi(c->hp, 0, h->maxhp); h->en = c->en; h->form = c->form < FORM_COUNT ? c->form : FORM_NORMAL;
        h->atk_lv = c->atk_lv; h->ult_lv = c->ult_lv;
        h->st = h->hp > 0 ? HS_IDLE : HS_DOWN; h->inv = 1000;
        h->last_down = h->roll_start = -10000;
    }
    for (int i = 0; i < s->n && i < MBT_MAXF; i++) {
        const mbt_foe_def_t *d = &morph_foes[s->types[i]];
        float x = d->boss ? MBT_W / 2 : (i % 2) ? L_EDGE + 14 : R_EDGE - 14 - i * 8;
        if (boss && !d->boss) x = (i % 2) ? L_EDGE + 30 : R_EDGE - 30;
        spawn(b, s->types[i], x, d->boss ? s->boss_pct : s->hp_pct);
    }
    for (int i = 0; i < MBT_MAXF; i++)
        if (b->f[i].used && (def(&b->f[i])->flags & FD_COILS)) { spawn_coils(b); break; }
    say(b, boss ? SAY_BOSS : elite ? SAY_ELITE : SAY_START, 0, 0);
}

// 人自己走过去打的那只:最近的、打得动的(Boss 无敌时去打线圈)
static int target_foe(morph_battle_t *b, float x) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < MBT_MAXF; i++) {
        if (!foe_alive(&b->f[i]) || morph_mbt_shielded(b, i)) continue;
        float d = fabsf(b->f[i].x - x);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static int nearest_hero(morph_battle_t *b, float x) {
    int best = -1; float bd = 1e9f;
    for (int p = 0; p < b->nh; p++) {
        if (!free_hero(&b->h[p])) continue;
        float d = fabsf(b->h[p].x - x);
        if (d < bd) { bd = d; best = p; }
    }
    if (best < 0)   // 都冻住了:还是盯着最近的
        for (int p = 0; p < b->nh; p++)
            if (standing(&b->h[p]) && fabsf(b->h[p].x - x) < bd) { bd = fabsf(b->h[p].x - x); best = p; }
    return best;
}

static void gain(mbt_hero_t *h, int v) { h->en = (int16_t)clampi(h->en + v, 0, MBT_EN_MAX); }

// 打怪。p = 谁打的(-1 没人),from = 从哪边打过来(冰盾看正面背面);stun 加晕
static void hit_from(morph_battle_t *b, int p, int fi, float dmg, int stun, unsigned fl, float from) {
    mbt_foe_t *f = &b->f[fi];
    const mbt_foe_def_t *d = def(f);
    if (f->hp <= 0) return;
    if (morph_mbt_shielded(b, fi)) {   // 线圈没全坏:打不动
        f->flash = 60;
        ev(b, BE_HIT, (uint8_t)fi, 4, 0, f->x, f->y - d->h);
        say_now_and_then(b, SAY_SHIELDED);
        return;
    }
    float m = 1;
    uint8_t a = (fl & HIT_BIG) ? 1 : 0;
    if (b->slow_t > 0) m *= 2;
    if (f->stun_t > 0 && !(fl & HIT_FLAT)) m *= 1.5f;
    if (morph_mbt_guarding(f) && !(fl & HIT_FLAT)) {
        if ((from - f->x) * f->face > 0) {   // 打在冰盾上
            if (fl & HIT_BREAK) { f->guard_t = b->nh == 2 ? 2500 : 3500; say(b, SAY_GUARD_BREAK, 0, 0); }
            else { m *= 0.15f; a |= 4; say_now_and_then(b, SAY_GUARD); }
        } else {
            m *= 1.3f;   // 背后
        }
    }
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
    ev(b, BE_HIT, (uint8_t)fi, (uint8_t)(a | (m >= 2 ? 2 : 0)), dd, f->x, f->y - d->h);
    b->combo++; b->combo_t = 1600;
    if (p >= 0) gain(&b->h[p], 4);
    if (!is_coil(f) && f->stun_t <= 0 && f->freeze_t <= 0) {
        f->stun_g = (int16_t)(f->stun_g + stun);
        if (f->stun_g >= d->stun) {
            f->stun_g = 0; f->stun_t = d->boss ? 2500 : 3200; f->st = FS_STUN; f->atk = 0; f->y = 0;
            say(b, d->boss || d->elite ? SAY_STUN_HIT : SAY_STUN_EAT, 0, 0);
        }
    }
    if ((fl & HIT_KNOCK) && small(f)) {
        f->vx = (p >= 0 ? b->h[p].face : 1) * 120.0f; f->vy = -200; f->st = FS_AIR; f->atk = 0;
        f->knock_by = (int8_t)p; f->knock_at = b->now;
    } else if (f->st == FS_TELE && (fl & HIT_KNOCK) && !d->boss) {
        f->st = FS_WALK; f->atk = 0; f->wait = 800;
    }
    int stop = (fl & HIT_BIG) ? 90 : 50;
    if (b->stop_t < stop) b->stop_t = stop;
    if ((fl & HIT_BIG) && b->shake_t < 200) b->shake_t = 200;
    if (f->hp <= 0) {
        f->hp = 0;
        ev(b, BE_KILL, (uint8_t)fi, 0, 0, f->x, f->y);
        if (is_coil(f)) {
            f->open_t = b->nh == 2 ? 3000 : 12000;
            say(b, SAY_COIL_DOWN, 0, 0);
        }
    }
}
static void hit(morph_battle_t *b, int p, int fi, float dmg, int stun, unsigned fl) {
    hit_from(b, p, fi, dmg, stun, fl, p >= 0 ? b->h[p].x : b->f[fi].x + b->f[fi].face);
}

// ---------------- 人 ----------------
static void set_st(mbt_hero_t *h, uint8_t s) { h->st = s; h->t = 0; h->hit_done = false; }
static bool can_act(const mbt_hero_t *h) {
    return h->st == HS_IDLE || h->st == HS_WALK || (h->st == HS_ATK && h->t > 160) || (h->st == HS_ROLL && h->t > 240) ||
           h->st == HS_SHIELD;
}
static void face_to(mbt_hero_t *h, float x) { h->face = x > h->x ? 1 : -1; }
static void face_foe(morph_battle_t *b, mbt_hero_t *h) {
    int fi = target_foe(b, h->x);
    if (fi >= 0) face_to(h, b->f[fi].x);
}
static void start_shield(mbt_hero_t *h) { set_st(h, HS_SHIELD); h->shield_t = 1000; h->shield_age = 0; }

static void skill(morph_battle_t *b, int p) {
    mbt_hero_t *h = &b->h[p];
    h->skill_cd = 1400; face_foe(b, h); set_st(h, HS_SKILL);
    if (h->form == FORM_ICE || h->form == FORM_ROCKET) {
        for (int i = 0; i < MBT_MAXSHOT; i++) {
            if (b->shot[i].used) continue;
            b->shot[i] = (mbt_shot_t){ true, h->form, h->x + h->face * 10, h->face * (h->form == FORM_ROCKET ? 260.0f : 200.0f),
                                       1200, (int8_t)p };
            break;
        }
    } else if (h->form == FORM_VOLT) {
        for (int i = 0; i < MBT_MAXF; i++)
            if (foe_alive(&b->f[i]) && fabsf(b->f[i].x - h->x) < 54) hit(b, p, i, 12 * pmul(h), 20, 0);
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
        if (foe_alive(f) && f->stun_t > 0 && small(f) && fabsf(f->x - x) < 44) return i;
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
    if (h->st == HS_TRAP) {   // 冻住了:● 敲冰块
        if (key == MBT_OK) h->trap_hp--;
        return;
    }
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
        if (!(q && q->st == HS_TRAP && fabsf(q->x - h->x) < 34)) face_foe(b, h);
        set_st(h, HS_ATK);
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
        if (!(q && q->st == HS_TRAP && fabsf(q->x - h->x) < 34)) face_foe(b, h);
        set_st(h, HS_CHARGE);
        return;
    }
    if (key == MBT_UP) {
        if (!(h->st == HS_IDLE || h->st == HS_WALK || h->st == HS_ATK || h->st == HS_SHIELD || h->st == HS_ROLL)) return;
        if (b->now - h->last_down < DOWN_COMBO_MS && h->en >= MBT_EN_MAX) {   // 超必杀
            h->en = 0; h->pend = false;
            b->cine_on = true;
            b->cine = (mbt_cine_t){ .kind = CINE_SUPER, .form = h->form, .lv = 3, .p = (int8_t)p, .q = -1,
                                    .low = h->hp * 10 < h->maxhp * 3 };
            h->inv = 3000; set_st(h, HS_ULT);
            ev(b, BE_CINE, (uint8_t)p, CINE_SUPER, 3, h->x, 0);
            return;
        }
        if (h->en < 100) { say(b, SAY_NO_ENERGY, p, 0); return; }
        if (q && q->pend && free_hero(q)) {   // 队友在等击掌 → 合体大招
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
        if (q && free_hero(q) && q->en >= 100) { h->pend = true; h->pend_t = 0; h->pend_en = h->en; return; }
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
// 打队友身上的冰块
static void chip_ice(morph_battle_t *b, int p, int taps) {
    mbt_hero_t *h = &b->h[p], *q = mate(b, p);
    if (q && q->st == HS_TRAP && fabsf(q->x - h->x) < 34) q->trap_hp = (int8_t)(q->trap_hp - taps);
}

static void free_from_ice(morph_battle_t *b, int p) {
    mbt_hero_t *h = &b->h[p];
    set_st(h, HS_IDLE); h->inv = 800; h->trap_t = 0;
    say(b, SAY_FREED, p, 0);
}

// 怪打人:sx = 从哪边打过来(举盾要朝着那边)。冻住的人有冰块挡着
static int hurt_hero(morph_battle_t *b, int p, float sx, int dmg) {
    mbt_hero_t *h = &b->h[p];
    if (!standing(h) || h->st == HS_TRAP) return HR_MISS;
    if (h->st == HS_ROLL && h->inv > 0) {
        if (b->now - h->roll_start < 220 && b->slow_t <= 0) {
            b->slow_t = 2000; gain(h, 50); say(b, SAY_PERFECT, p, 0);
            return HR_PERFECT;
        }
        return HR_MISS;
    }
    if (h->inv > 0 || h->st == HS_ULT) return HR_MISS;
    if (h->st == HS_SHIELD && (sx - h->x) * h->face >= 0) {
        if (h->shield_age < 200) {
            gain(h, 40);
            b->stop_t = 100; b->shake_t = 150;
            ev(b, BE_SHIELD, (uint8_t)p, 1, 0, h->x, 0);
            return HR_PARRY;
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
    return HR_HIT;
}
// 近身招打到人:弹反让怪踉跄(破一块甲、冰盾碎),完美闪避也让冰盾碎
static void melee_hurt(morph_battle_t *b, int p, mbt_foe_t *f, int dmg) {
    int r = hurt_hero(b, p, f->x, dmg);
    if (r == HR_PARRY) {
        f->st = FS_STAGGER; f->t = 0; f->atk = 0; f->y = 0; f->stun_g = (int16_t)(f->stun_g + 30);
        if (f->armor) { f->armor--; say(b, SAY_PARRY_ARMOR, p, 0); }
        else say(b, SAY_PARRY, p, 0);
    }
    if ((r == HR_PARRY || r == HR_PERFECT) && morph_mbt_guarding(f)) { f->guard_t = 3000; say(b, SAY_GUARD_BREAK, 0, 0); }
}
static void trap_hero(morph_battle_t *b, int p, mbt_foe_t *f) {
    mbt_hero_t *h = &b->h[p];
    if (!standing(h) || h->st == HS_TRAP || h->st == HS_ULT) return;
    if (h->st == HS_ROLL && h->inv > 0) {
        if (b->now - h->roll_start < 220 && b->slow_t <= 0) { b->slow_t = 2000; gain(h, 50); say(b, SAY_PERFECT, p, 0); }
        if (morph_mbt_guarding(f)) f->guard_t = 3000;
        return;
    }
    if (h->inv > 0) return;
    set_st(h, HS_TRAP); h->trap_t = TRAP_MS; h->trap_hp = TRAP_TAPS; h->pend = false;
    say(b, SAY_TRAPPED, p, 0);
}

static void step_hero(morph_battle_t *b, int p) {
    mbt_hero_t *h = &b->h[p], *q = mate(b, p);
    if (!h->present) return;
    h->t += MBT_STEP; h->inv = dec(h->inv); h->chain_t = dec(h->chain_t);
    h->skill_cd = dec(h->skill_cd); h->flash = dec(h->flash);
    if (h->pend) {
        h->pend_t += MBT_STEP;
        if (h->pend_t > PEND_MS) { fire_ult(b, p); if (b->cine_on) return; }
    }
    if (h->hp <= 0) { h->st = HS_DOWN; return; }
    int tg = target_foe(b, h->x), hits[MBT_MAXF], n;
    float pm = pmul(h);
    switch (h->st) {
        case HS_IDLE:
        case HS_WALK:
            if (q && q->st == HS_TRAP && q->hp > 0) {   // 队友冻住了:先过去救
                face_to(h, q->x);
                if (fabsf(q->x - h->x) > 18) { h->st = HS_WALK; h->x += h->face * 70 * DT; }
                else h->st = HS_IDLE;
            } else if (tg >= 0) {
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
                    hit(b, p, hits[i], DMG[h->chain] * pm, h->chain == 3 ? 16 : 8, h->chain == 3 ? (HIT_KNOCK | HIT_BIG) : 0);
                chip_ice(b, p, h->chain == 3 ? 3 : 2);
                h->chain_t = 400;
            }
            if (h->t > 320) set_st(h, HS_IDLE);
            break;
        case HS_CHARGE:
            if (!h->hit_done && h->t > 100) {
                h->hit_done = true; h->x += h->face * 10; if (b->shake_t < 200) b->shake_t = 200;
                n = front_foes(b, h, 38, hits);
                for (int i = 0; i < n; i++) hit(b, p, hits[i], 24 * pm, 34, HIT_KNOCK | HIT_BIG | HIT_BREAK);
                chip_ice(b, p, TRAP_TAPS);
            }
            if (h->t > 450) set_st(h, HS_IDLE);
            break;
        case HS_CHASE: {   // 接力:跳到空中的怪那里砸一下,怪再被打飞(两人能轮流接)
            mbt_foe_t *f = &b->f[h->what_idx];
            if (foe_alive(f)) h->x += (f->x - h->face * 8 - h->x) * (DT * 14 > 1 ? 1 : DT * 14);
            if (!h->hit_done && h->t > 160) {
                h->hit_done = true;
                if (foe_alive(f)) { say(b, SAY_RELAY, p, 0); hit(b, p, h->what_idx, 16 * pm, 14, HIT_KNOCK | HIT_BIG); gain(h, 10); }
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
                    for (int i = 0; i < n; i++) hit(b, p, hits[i], 14 * pm, 16, HIT_BIG);
                }
            } else if (h->form == FORM_SPRING) {   // 弹跳砸地:跳过去,落地一圈
                if (h->t < 300) { h->x += h->face * 170 * DT; if (h->inv < 50) h->inv = 50; }
                else if (!h->hit_done) {
                    h->hit_done = true;
                    if (b->shake_t < 200) b->shake_t = 200;
                    for (int i = 0; i < MBT_MAXF; i++)
                        if (foe_alive(&b->f[i]) && fabsf(b->f[i].x - h->x) < 40 && b->f[i].y > -40)
                            hit(b, p, i, 14 * pm, 22, HIT_KNOCK | HIT_BIG);
                }
                if (h->t > 420) set_st(h, HS_IDLE);
                break;
            } else if (h->form == FORM_MAGNET && !h->hit_done && h->t > 80) {   // 吸过来:附近的小怪、能力星吸到跟前
                h->hit_done = true;
                int k = 0;
                for (int i = 0; i < MBT_MAXF; i++) {
                    mbt_foe_t *f = &b->f[i];
                    if (!foe_alive(f) || !small(f) || fabsf(f->x - h->x) > 130) continue;
                    f->x = clampf(h->x + h->face * (20 + 8 * k++), L_EDGE, R_EDGE);
                    hit(b, p, i, 6 * pm, 25, 0);
                }
                for (int i = 0; i < MBT_MAXSTAR; i++)
                    if (b->star[i].used && fabsf(b->star[i].x - h->x) < 130) b->star[i].x = h->x + h->face * 10;
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
                    if (form == -2) { h->hp = (int16_t)clampi(h->hp + 15, 0, h->maxhp); ev(b, BE_PHIT, (uint8_t)p, 1, -15, h->x, 0); }
                    set_st(h, HS_IDLE);
                }
            }
            break;
        }
        case HS_REVIVE:
            if (!q || q->hp > 0) { set_st(h, HS_IDLE); break; }
            if (h->t > 800) {
                q->hp = (int16_t)(q->maxhp * 35 / 100); set_st(q, HS_IDLE); q->inv = 1500;
                say(b, SAY_REVIVED, 1 - p, 0);
                set_st(h, HS_IDLE);
            }
            break;
        case HS_TRAP:
            h->trap_t -= MBT_STEP;
            if (h->trap_hp <= 0) free_from_ice(b, p);
            else if (h->trap_t <= 0) {   // 没人救、没敲开:冻伤一下自己出来
                int d = fdmg(b, 18);
                h->hp = (int16_t)(h->hp - d); h->flash = 150;
                ev(b, BE_PHIT, (uint8_t)p, 0, d, h->x, 0);
                if (h->hp <= 0) {
                    h->hp = 0; set_st(h, HS_DOWN);
                    bool any = false;
                    for (int k = 0; k < b->nh; k++) any |= standing(&b->h[k]);
                    say(b, any ? SAY_DOWN : SAY_ALL_DOWN, p, 0);
                } else {
                    free_from_ice(b, p);
                }
            }
            break;
        case HS_MORPH: if (h->t > 600) set_st(h, HS_IDLE); break;
        case HS_HURT: h->x -= h->face * 60 * DT; if (h->t > 350) set_st(h, HS_IDLE); break;
        case HS_DOWN: if (h->hp > 0) set_st(h, HS_IDLE); break;
        default: break;
    }
    h->x = clampf(h->x, L_EDGE + 6, R_EDGE - 6);
}

// ---------------- 怪 ----------------
static bool rage(const mbt_foe_t *f) { return f->type == ET_DEMON && f->phase == 2; }

// Boss 挑招;far = 离盯着的人远
static uint8_t boss_move(morph_battle_t *b, mbt_foe_t *f, bool far) {
    int r = (int)(rnd(b) % 100);
    bool trap_ok = f->trap_cd <= 0;
    switch (f->type) {
        case ET_BEAR: return far || r < 35 ? FA_DASH : FA_SLAP;
        case ET_ICEKING:
            if (trap_ok && r < 30) return FA_TRAP;
            return far || r < 55 ? FA_SHOOT : FA_SLAP;
        case ET_DYNAMO:
            if (far) return r < 60 ? FA_BOLT : FA_DASH;
            return r < 35 ? FA_BOLT : r < 55 ? FA_DASH : FA_SLAP;
        case ET_DEMON:
            if (f->phase == 0) {
                if (trap_ok && r < 25) return FA_TRAP;
                return far || r < 50 ? FA_DASH : FA_SLAP;
            }
            if (f->phase == 1) return far ? (r < 50 ? FA_BOLT : FA_SHOOT) : r < 35 ? FA_BOLT : r < 55 ? FA_SHOOT : FA_SLAP;
            if (trap_ok && r < 20) return FA_TRAP;
            if (far) return r < 60 ? FA_BOLT : FA_DASH;
            return r < 40 ? FA_BOLT : r < 65 ? FA_DASH : FA_SLAP;
        default: return FA_SLAP;
    }
}

static void start_attack(morph_battle_t *b, mbt_foe_t *f, const mbt_hero_t *tg, bool far) {
    const mbt_foe_def_t *d = def(f);
    f->st = FS_TELE; f->t = 0; f->atk_done = false; f->atk_mask = 0;
    f->atk = d->boss ? boss_move(b, f, far) : d->atk;
    static const int16_t TELE[] = { [FA_SLAP] = 800, [FA_DASH] = 1000, [FA_LEAP] = 600, [FA_PULL] = 700,
                                    [FA_RING] = 900, [FA_BOLT] = 1000, [FA_TRAP] = 1100, [FA_SHOOT] = 700 };
    f->tele = TELE[f->atk];
    if (rage(f)) f->tele = f->tele * 3 / 4;
    f->atk_dir = f->face; f->atk_from = f->x; f->atk_x = tg->x;
    if (f->atk == FA_TRAP) f->trap_cd = rage(f) ? 7000 : 9000;
}

static void shoot(morph_battle_t *b, mbt_foe_t *f) {
    const mbt_foe_def_t *d = def(f);
    for (int i = 0; i < MBT_MAXFSHOT; i++) {
        mbt_fshot_t *s = &b->fshot[i];
        if (s->used) continue;
        bool rocket = f->type == ET_ROCKET || f->type == ET_DEMON;
        *s = (mbt_fshot_t){ .used = true, .big = d->boss || d->elite, .kind = rocket ? 1 : 0, .by = -1,
                            .x = f->x + f->face * d->w / 2.0f, .vx = f->face * (rocket ? 85.0f : 110.0f), .t = 3000,
                            .dmg = (int16_t)fdmg(b, d->dmg) };
        break;
    }
    ev(b, BE_ZAP, 0, FA_SHOOT, 0, f->x, 0);
}

static void step_foe(morph_battle_t *b, int fi, float dt, int32_t ms) {
    mbt_foe_t *f = &b->f[fi];
    const mbt_foe_def_t *d = def(f);
    if (!f->used || is_coil(f)) return;   // 线圈不动,在 step_coils 里管
    f->flash = decm(f->flash, ms);
    if (f->hp <= 0) { if (!f->flash) f->used = false; return; }
    f->guard_t = decm(f->guard_t, ms); f->trap_cd = decm(f->trap_cd, ms);
    if (f->type == ET_DEMON) {   // 魔王换段
        if (f->phase == 0 && f->hp * 3 < f->max * 2) {
            f->phase = 1; f->guard_t = 0; spawn_coils(b);
            say(b, SAY_PHASE2, 0, 0);
        } else if (f->phase == 1 && f->hp * 3 < f->max) {
            f->phase = 2; f->open_t = 0;
            say(b, SAY_RAGE, 0, 0);
        }
    }
    if (f->st == FS_AIR) {
        f->x += f->vx * dt; f->vy += 520 * dt; f->y += f->vy * dt;
        if (f->y >= 0) { f->y = 0; f->vy = 0; f->vx = 0; f->st = f->stun_t > 0 ? FS_STUN : FS_WALK; f->wait = 500; f->knock_by = -1; }
        f->x = clampf(f->x, L_EDGE, R_EDGE);
        return;
    }
    if (f->freeze_t > 0) { f->freeze_t = decm(f->freeze_t, ms); return; }
    if (f->stun_t > 0) {
        f->stun_t = decm(f->stun_t, ms);
        if (!f->stun_t) { f->st = FS_WALK; f->wait = 400; }
        return;
    }
    // 盯着谁:冰盾 Boss 两个人时轮流盯(另一个人去打背后),别的盯最近的
    int tp = nearest_hero(b, f->x);
    if (has_guard(f) && b->nh == 2) {
        f->aggro_t += ms;
        int other = 1 - f->aggro;
        bool cur_ok = free_hero(&b->h[f->aggro]), other_ok = free_hero(&b->h[other]);
        if ((!cur_ok && other_ok) || (f->aggro_t >= AGGRO_MS && other_ok && f->st == FS_WALK)) {
            f->aggro = (int8_t)other; f->aggro_t = 0;
            if (cur_ok) say(b, SAY_TURN, 0, 0);
        }
        if (standing(&b->h[f->aggro])) tp = f->aggro;
    }
    if (tp < 0) return;
    mbt_hero_t *tg = &b->h[tp];
    f->t += ms;
    if (f->st != FS_ATK || f->atk != FA_LEAP) f->face = tg->x > f->x ? 1 : -1;
    float gap = fabsf(tg->x - f->x) - d->w / 2.0f;
    float speed = d->speed * (rage(f) ? 1.4f : 1.0f);
    if (d->boss && d->summon[0] != ET_COUNT) {
        f->summon_t -= ms;
        if (f->summon_t <= 0 && morph_mbt_alive(b) < 3) {
            f->summon_t = rage(f) ? 8000 : 11000;
            spawn(b, d->summon[rnd(b) % 2], f->x < 120 ? R_EDGE - 10 : L_EDGE + 10, 100);
            say(b, SAY_SUMMON, 0, 0);
        }
    }
    switch (f->st) {
        case FS_WALK: {
            bool in_range;
            if (d->flags & FD_RANGED) {   // 远的怪:保持距离
                if (gap < 40 && f->x > L_EDGE + 20 && f->x < R_EDGE - 20) f->x -= f->face * speed * 0.6f * dt;
                else if (gap > d->reach) f->x += f->face * speed * dt;
                in_range = gap <= d->reach;
            } else {
                if (gap > d->reach - 4) f->x += f->face * speed * dt;
                in_range = gap <= d->reach - 4;
            }
            if (in_range || d->boss) {
                f->wait -= ms;
                if (f->wait <= 0) start_attack(b, f, tg, gap > d->reach - 4);
            }
            break;
        }
        case FS_TELE:
            if (f->atk == FA_BOLT && f->t < f->tele / 2) f->atk_x = tg->x;   // 雷劈先跟着人,后半段定住(看准了躲)
            if (f->t >= f->tele) { f->st = FS_ATK; f->t = 0; }
            break;
        case FS_ATK: {
            int dmg = fdmg(b, d->dmg);
            bool end = false;
            switch (f->atk) {
                case FA_DASH:   // 冲撞:横冲过去,撞到谁算谁
                    f->x += f->atk_dir * 170 * dt;
                    for (int p = 0; p < b->nh; p++)
                        if (standing(&b->h[p]) && !(f->atk_mask & (1u << p)) && fabsf(b->h[p].x - f->x) < d->w / 2.0f + 6) {
                            f->atk_mask |= (uint8_t)(1u << p);
                            melee_hurt(b, p, f, dmg);
                        }
                    end = f->t > 1000 || f->x <= L_EDGE + d->w / 2.0f || f->x >= R_EDGE - d->w / 2.0f;
                    break;
                case FA_LEAP: {   // 跳起来砸人站的地方
                    float u = f->t / 450.0f;
                    if (u < 1) {
                        f->x = f->atk_from + (f->atk_x - f->atk_from) * u;
                        f->y = -160 * u * (1 - u);
                    } else if (!f->atk_done) {
                        f->atk_done = true; f->y = 0;
                        for (int p = 0; p < b->nh; p++)
                            if (standing(&b->h[p]) && fabsf(b->h[p].x - f->x) < d->w / 2.0f + 12) melee_hurt(b, p, f, dmg);
                    }
                    end = f->t > 600;
                    break;
                }
                case FA_PULL:   // 磁铁:先把人吸过来,再拍
                    if (f->t < 700) {
                        for (int p = 0; p < b->nh; p++) {
                            mbt_hero_t *h = &b->h[p];
                            if (!free_hero(h) || h->inv > 0 || h->st == HS_ULT || fabsf(h->x - f->x) > 130) continue;
                            if (fabsf(h->x - f->x) > 20) h->x += (f->x > h->x ? 1 : -1) * 55 * dt;
                        }
                    } else if (!f->atk_done) {
                        f->atk_done = true;
                        for (int p = 0; p < b->nh; p++)
                            if (standing(&b->h[p]) && fabsf(b->h[p].x - f->x) - d->w / 2.0f < 28) melee_hurt(b, p, f, dmg);
                    }
                    end = f->t > 900;
                    break;
                case FA_RING:   // 放电圈:前后一圈
                    if (!f->atk_done && f->t > 20) {
                        f->atk_done = true;
                        ev(b, BE_ZAP, 0, FA_RING, 0, f->x, 0);
                        for (int p = 0; p < b->nh; p++)
                            if (standing(&b->h[p]) && fabsf(b->h[p].x - f->x) < d->reach + 22) melee_hurt(b, p, f, dmg);
                    }
                    end = f->t > 500;
                    break;
                case FA_BOLT:   // 雷劈:标出来的那一小块
                    if (!f->atk_done && f->t > 20) {
                        f->atk_done = true;
                        ev(b, BE_ZAP, 0, FA_BOLT, 0, f->atk_x, 0);
                        for (int p = 0; p < b->nh; p++) {
                            mbt_hero_t *h = &b->h[p];
                            if (standing(h) && fabsf(h->x - f->atk_x) < 22) {
                                int r = hurt_hero(b, p, h->x + h->face, dmg);
                                if (r == HR_PARRY) say(b, SAY_PARRY, p, 0);
                            }
                        }
                    }
                    end = f->t > 400;
                    break;
                case FA_TRAP:   // 冰冻地面:没翻滚躲开的都冻住
                    if (!f->atk_done && f->t > 20) {
                        f->atk_done = true;
                        ev(b, BE_ZAP, 0, FA_TRAP, 0, f->x, 0);
                        for (int p = 0; p < b->nh; p++) trap_hero(b, p, f);
                    }
                    end = f->t > 600;
                    break;
                case FA_SHOOT:
                    if (!f->atk_done && f->t > 20) { f->atk_done = true; shoot(b, f); }
                    end = f->t > 400;
                    break;
                default:   // 拍
                    if (!f->atk_done && f->t > 20) {
                        f->atk_done = true;
                        for (int p = 0; p < b->nh; p++) {
                            mbt_hero_t *h = &b->h[p];
                            if (standing(h) && (h->x - f->x) * f->face > -8 && fabsf(h->x - f->x) - d->w / 2.0f < d->reach + 4)
                                melee_hurt(b, p, f, dmg);
                        }
                        if (d->boss && b->shake_t < 250) b->shake_t = 250;
                    }
                    end = f->t > 500;
                    break;
            }
            if (end && f->st == FS_ATK) {
                int base = f->atk == FA_DASH ? 1200 : d->boss ? 600 : (d->flags & FD_RANGED) ? 1700 : 900;
                f->st = FS_WALK; f->y = 0; f->atk = 0;
                f->wait = base + (int32_t)(rnd(b) % 900);
                if (rage(f)) f->wait = f->wait * 3 / 5;
            }
            break;
        }
        case FS_STAGGER:
            if (f->t > 1200) { f->st = FS_WALK; f->wait = 300; }
            break;
        default: break;
    }
    f->x = clampf(f->x, L_EDGE + d->w / 2.0f, R_EDGE - d->w / 2.0f);
}

// 线圈:坏了的过一会儿又充好电;两个都坏了 → Boss 9 秒不无敌,然后两个一起起来。Boss 没了 / 魔王换段 → 线圈收掉
static void step_coils(morph_battle_t *b) {
    int bi = -1;
    for (int i = 0; i < MBT_MAXF; i++) if (foe_alive(&b->f[i]) && has_coils(&b->f[i])) bi = i;
    int alive = 0, down = 0;
    for (int i = 0; i < MBT_MAXF; i++) {
        mbt_foe_t *c = &b->f[i];
        if (!c->used || !is_coil(c)) continue;
        if (bi < 0) { c->used = false; continue; }
        c->flash = dec(c->flash);
        if (c->hp > 0) alive++; else down++;
    }
    if (bi < 0) return;
    mbt_foe_t *boss = &b->f[bi];
    bool back = false;
    if (boss->open_t > 0) {
        boss->open_t = dec(boss->open_t);
        back = boss->open_t == 0;
    } else if (!alive && down) {
        boss->open_t = OPEN_MS;
        boss->stun_t = 2000; boss->st = FS_STUN; boss->atk = 0; boss->y = 0;
        say(b, SAY_COIL_OPEN, 0, 0);
        return;
    }
    for (int i = 0; i < MBT_MAXF; i++) {
        mbt_foe_t *c = &b->f[i];
        if (!c->used || !is_coil(c) || c->hp > 0) continue;
        if (!back && boss->open_t > 0) continue;
        c->open_t = dec(c->open_t);
        if (back || c->open_t == 0) {
            c->hp = c->max;
            if (!back) say(b, SAY_COIL_BACK, 0, 0);
        }
    }
    if (back) say(b, SAY_COIL_BACK, 0, 0);
}

// ---------------- 特写放完:大招 / 超必杀 / 合体 ----------------
static void boom(morph_battle_t *b) {
    mbt_cine_t c = b->cine;
    b->cine_on = false;
    b->shake_t = c.kind == CINE_ULT ? 500 : 900; b->stop_t = 150;
    mbt_hero_t *h = &b->h[c.p];
    float um = c.kind == CINE_COMBO ? (umul(h) + umul(&b->h[c.q])) / 2 : umul(h);
    bool gather = c.form == FORM_MAGNET || (c.kind == CINE_COMBO && c.form2 == FORM_MAGNET);
    bool bounce = c.form == FORM_SPRING || (c.kind == CINE_COMBO && c.form2 == FORM_SPRING);
    bool freeze = c.form == FORM_ICE || (c.kind == CINE_COMBO && c.form2 == FORM_ICE);
    int k = 0;
    for (int i = 0; i < MBT_MAXF; i++) {
        mbt_foe_t *f = &b->f[i];
        if (!foe_alive(f)) continue;
        if (gather && small(f)) f->x = clampf(h->x + h->face * (24 + 10 * k++), L_EDGE, R_EDGE);   // 磁铁:全吸过来
        if (c.kind == CINE_ULT) {
            int n = c.form == FORM_NORMAL ? 4 + c.lv * 2 : c.form == FORM_VOLT ? c.lv + 1 : c.form == FORM_ROCKET ? c.lv * 3
                  : c.form == FORM_SPRING ? 3 : c.form == FORM_MAGNET ? 2 : 1;
            if (c.form == FORM_NORMAL && fabsf(f->x - h->x) >= 70 + c.lv * 30) continue;   // 百裂拳打不到远处
            int brk = ULT_BREAK[c.lv] + (c.form == FORM_MAGNET);
            if (f->armor && brk) {
                f->armor = (uint8_t)(f->armor > brk ? f->armor - brk : 0);
                say(b, f->armor ? SAY_ARMOR_CRACK : SAY_ARMOR_BREAK, 0, 0);
            }
            for (int j = 0; j < n; j++)
                hit(b, c.p, i, (float)ULT_DMG[c.lv] * um / n, 24 / n,
                    HIT_FLAT | (j == n - 1 ? HIT_BIG | (bounce ? HIT_KNOCK : 0) : 0));
            if (freeze && f->hp > 0) f->freeze_t = 1500 + c.lv * 1000;
        } else {
            float total = (c.kind == CINE_SUPER ? SUPER_DMG * (c.low ? 2 : 1) : COMBO_DMG + 30 * (c.bars > 2 ? c.bars - 2 : 0)) * um;
            f->armor = 0;
            for (int j = 0; j < 6; j++) hit(b, c.p, i, total / 6, 0, HIT_FLAT | (j == 5 ? HIT_BIG : 0));
            if (f->hp > 0 && !is_coil(f)) {
                f->stun_g = 0; f->stun_t = def(f)->boss ? 3000 : 4000; f->st = FS_STUN; f->atk = 0; f->y = 0;
                if (has_guard(f)) f->guard_t = 4000;
            }
            if (freeze && f->hp > 0) f->freeze_t = 2000;
        }
    }
    if (c.kind == CINE_SUPER && c.low) say(b, SAY_LOW, c.p, 0);
    if (c.kind == CINE_COMBO) say(b, SAY_COMBO, c.p, 0);
    ev(b, BE_BOOM, (uint8_t)c.p, c.kind, 0, h->x, 0);
    for (int p = 0; p < b->nh; p++) if (b->h[p].st == HS_ULT) set_st(&b->h[p], HS_IDLE);
}

static void step_shots(morph_battle_t *b, float fdt, int32_t fms) {
    for (int i = 0; i < MBT_MAXSHOT; i++) {   // 人放的
        mbt_shot_t *s = &b->shot[i];
        if (!s->used) continue;
        s->x += s->vx * DT; s->t -= MBT_STEP;
        for (int k = 0; k < MBT_MAXF; k++) {
            mbt_foe_t *f = &b->f[k];
            if (!foe_alive(f) || fabsf(f->x - s->x) >= def(f)->w / 2.0f + 3 || f->y <= -30) continue;
            float pm = s->by >= 0 ? pmul(&b->h[s->by]) : 1;
            if (s->form == FORM_ROCKET) {   // 火箭:炸一圈
                for (int j = 0; j < MBT_MAXF; j++)
                    if (foe_alive(&b->f[j]) && fabsf(b->f[j].x - s->x) < 36) hit_from(b, s->by, j, 16 * pm, 14, HIT_BIG, s->x - s->vx);
            } else {
                hit_from(b, s->by, k, 10 * pm, 10, 0, s->x - s->vx);
                if (f->hp > 0 && !is_coil(f)) f->freeze_t = def(f)->boss ? 800 : 1800;
            }
            s->t = 0;
            break;
        }
        if (s->t <= 0 || s->x < 0 || s->x > MBT_W) s->used = false;
    }
    for (int i = 0; i < MBT_MAXFSHOT; i++) {   // 怪扔的
        mbt_fshot_t *s = &b->fshot[i];
        if (!s->used) continue;
        s->x += s->vx * (s->refl ? DT : fdt); s->t -= s->refl ? MBT_STEP : fms;
        float w = s->big ? 12 : 8;
        if (!s->refl) {
            for (int p = 0; p < b->nh; p++) {
                mbt_hero_t *h = &b->h[p];
                if (!standing(h) || fabsf(h->x - s->x) >= w) continue;
                int r = hurt_hero(b, p, s->x - s->vx, s->dmg);
                if (r == HR_PARRY) {   // 弹回去
                    s->refl = true; s->vx = -s->vx * 1.6f; s->by = (int8_t)p; s->t = 2000;
                    say(b, SAY_REFLECT, p, 0);
                } else if (r == HR_HIT) {
                    s->used = false;
                }
                break;
            }
        } else {
            for (int k = 0; k < MBT_MAXF; k++) {
                mbt_foe_t *f = &b->f[k];
                if (!foe_alive(f) || fabsf(f->x - s->x) >= def(f)->w / 2.0f + 4 || f->y <= -30) continue;
                hit_from(b, s->by, k, s->dmg * 2.0f, 30, HIT_BIG | HIT_KNOCK, s->x - s->vx);
                s->used = false;
                break;
            }
        }
        if (s->t <= 0 || s->x < 0 || s->x > MBT_W) s->used = false;
    }
}

static void step(morph_battle_t *b) {
    b->now += MBT_STEP;
    b->shake_t = dec(b->shake_t);
    b->say_cd = dec(b->say_cd);
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
    step_coils(b);
    step_shots(b, fms / 1000.0f, fms);
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
    if (morph_mbt_alive(b)) return;
    if (b->nwave) {   // 第二波从两边进来
        for (int i = 0; i < b->nwave; i++) spawn(b, b->wave[i], (i % 2) ? L_EDGE + 10 : R_EDGE - 10 - (i / 2) * 12, b->wave_pct);
        b->nwave = 0;
        say(b, SAY_WAVE, 0, 0);
        return;
    }
    b->result = 1;
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
    if (h->st == HS_TRAP) return HINT_BREAK_OUT;
    if (q && q->st == HS_TRAP && q->hp > 0) return HINT_RESCUE;
    if (q && q->present && q->hp <= 0 && fabsf(q->x - h->x) < 36) return HINT_REVIVE;
    if (stunned_near(m, h->x) >= 0 || star_near(m, h->x) >= 0) return HINT_EAT;
    if (q && q->pend && h->en >= 100) return HINT_HIGHFIVE;
    if (relay_target(m, p) >= 0) return HINT_RELAY;
    if (h->en >= MBT_EN_MAX) return HINT_SUPER;
    if (h->en >= 100) return HINT_ULT;
    return HINT_NONE;
}
