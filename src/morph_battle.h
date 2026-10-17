// src/morph_battle.h —— 一场战斗:一到两个人对一群怪(纯 C,宿主可测;画面在 morph_gfx.c 照这里的状态画)。
// 站位只有横向:场地 0..MBT_W 像素,人自己走近最近的怪、自己转身,玩家只管出什么招。
// 三个键(和卡上一样只有按下和 ● ▼ 按住 0.5 秒的长按):
//   ● 点:三连击(第三下打飞)      ● 按住:蓄力斩(破一块甲);旁边有晕的怪 / 掉的能力星:吞掉变身;队友倒了:拉起来
//   ▼ 点:翻滚(怪出手前一瞬间翻 = 完美闪避,怪慢下来 2 秒、伤害翻倍)   ▼ 按住:翻滚完举盾(刚举起挨打 = 弹反)
//   ▼ 后 0.35 秒内 ●:技能(随变身)   ▲:大招(能量 1–3 格)   满 3 格 ▼ 后 0.35 秒内 ▲:超必杀
//   两个人:队友刚打飞的怪 1 秒内按 ● = 接力追打;一个人 ▲ 后 0.8 秒内队友也 ▲ = 合体大招
// 被冻进冰块:狂按 ● 挣脱;队友走过来打冰块也能救出来。
// Boss 的配合打法:
//   冰淇淋大王(FD_GUARD):正面冰盾(打正面只掉一点),只盯一个人、每 5 秒转身盯另一个;蓄力斩打正面 / 弹反 / 完美闪避 = 冰盾碎几秒
//   发电大魔像(FD_COILS):场地两头的线圈没全坏就无敌;一个线圈坏了,另一个要在几秒内也打坏(两个人 3 秒、一个人 12 秒),
//     不然坏的那个又充好电;两个都坏了 → 10 秒不无敌,然后线圈又起来
//   玩具魔王:血 > 2/3 冰盾,2/3–1/3 叫出线圈,< 1/3 发怒(更快、什么招都用)
// 时间按 MBT_STEP 毫秒一步走;位置是浮点像素(一场的东西很少,软浮点够快;联机时两边同一份程序算,结果一样)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MBT_W 240
#define MBT_STEP 10
#define MBT_MAXF 8
#define MBT_MAXSHOT 4
#define MBT_MAXFSHOT 4
#define MBT_MAXSTAR 2
#define MBT_EVQ 48
#define MBT_EN_MAX 300

enum { MBT_UP = 0, MBT_DOWN = 1, MBT_OK = 2 };                  // 和 kit_key_t 一样
enum { FORM_NORMAL = 0, FORM_ICE, FORM_VOLT, FORM_SPRING, FORM_MAGNET, FORM_ROCKET, FORM_COUNT };
enum {
    ET_CLOCK = 0, ET_ICE, ET_VOLT, ET_BIGCLOCK, ET_BEAR,           // 第 1 关 玩具工厂
    ET_SNOW, ET_FROG, ET_SNOWMAN, ET_ICEKING,                       // 第 2 关 冰淇淋冷库
    ET_MAGNET, ET_ROCKET, ET_BIGVOLT, ET_DYNAMO, ET_COIL,           // 第 3 关 电池发电站
    ET_DEMON,                                                       // 第 4 关 玩具魔王城
    ET_COUNT,
};
typedef enum {
    HS_IDLE = 0, HS_WALK, HS_ATK, HS_CHARGE, HS_CHASE, HS_ROLL, HS_SHIELD, HS_SKILL, HS_SWALLOW, HS_REVIVE,
    HS_MORPH, HS_HURT, HS_DOWN, HS_ULT, HS_TRAP,
} mbt_hero_st_t;
typedef enum { FS_WALK = 0, FS_TELE, FS_ATK, FS_STAGGER, FS_STUN, FS_AIR } mbt_foe_st_t;
// 怪的招(mbt_foe_t.atk)
typedef enum { FA_NONE = 0, FA_SLAP, FA_DASH, FA_LEAP, FA_PULL, FA_RING, FA_BOLT, FA_TRAP, FA_SHOOT } mbt_foe_atk_t;
typedef enum { CINE_ULT = 0, CINE_SUPER, CINE_COMBO } mbt_cine_kind_t;
// 画面上的一句话(文字在 morph_texts.h,who / a 是参数)
typedef enum {
    SAY_START = 0, SAY_ELITE, SAY_BOSS, SAY_ARMOR_CRACK, SAY_ARMOR_BREAK, SAY_STUN_EAT, SAY_STUN_HIT, SAY_NO_ENERGY,
    SAY_RELAY, SAY_MORPH, SAY_PERFECT, SAY_PARRY, SAY_PARRY_ARMOR, SAY_LOST_FORM, SAY_DOWN, SAY_ALL_DOWN,
    SAY_REVIVED, SAY_SUMMON, SAY_LOW, SAY_COMBO,
    SAY_GUARD, SAY_GUARD_BREAK, SAY_TURN, SAY_TRAPPED, SAY_FREED, SAY_SHIELDED, SAY_COIL_DOWN, SAY_COIL_BACK,
    SAY_COIL_OPEN, SAY_PHASE2, SAY_RAGE, SAY_REFLECT, SAY_WAVE,
    SAY_COUNT,
} mbt_say_t;
// 给画面 / 声音 / 联机的事件
typedef enum {
    BE_HIT = 0,   // who = 怪下标,v = 伤害,a = 1 重击 / 2 慢动作里 / 4 被挡住(冰盾、无敌),x / y = 位置
    BE_PHIT,      // who = 人,v = 伤害(0 = 挡住,负 = 回血)
    BE_SAY,       // a = mbt_say_t,who = 人(SAY_MORPH / DOWN / REVIVED / TRAPPED / FREED 用),v = 变身(SAY_MORPH)
    BE_CINE,      // 大招 / 超必杀 / 合体开始特写(看 b->cine)
    BE_BOOM,      // 特写放完、伤害打出去:a = mbt_cine_kind_t
    BE_KILL,      // who = 怪下标
    BE_SHIELD,    // who = 人(举盾 / 弹反 a = 1)
    BE_WHOOSH,    // who = 人(翻滚、出拳)
    BE_ZAP,       // 怪的远招打出去(雷劈、冰冻地面、扔雪球、放电圈):a = mbt_foe_atk_t,x = 位置
} mbt_ev_type_t;
typedef struct { uint8_t type, who, a; int16_t v, x, y; } mbt_ev_t;

typedef struct {
    float x; int8_t face; bool present;
    int16_t hp, maxhp, en; uint8_t form, st;
    uint8_t atk_lv, ult_lv;    // 升级:拳头(普通招 +15% 一级)、大招(+20% 一级)
    int32_t t;                 // 这个状态过了多久(毫秒)
    uint8_t chain; int32_t chain_t;
    bool hit_done, want_shield;
    int32_t inv, shield_t, shield_age, skill_cd, flash;
    int32_t last_down, roll_start;   // 战斗时钟(毫秒)
    bool pend; int32_t pend_t; int16_t pend_en;   // 等队友击掌
    int8_t what_kind, what_idx;      // 吞 / 拉 / 追的对象:1 怪、2 能力星、3 队友
    int32_t trap_t; int8_t trap_hp;  // 冻在冰块里:还剩多久、还要敲几下
} mbt_hero_t;

typedef struct {
    bool used; uint8_t type;
    float x, y, vx, vy;        // y ≤ 0:离地多高(打飞、跳起来时)
    int8_t face;
    int16_t hp, max, stun_g;
    int32_t stun_t, freeze_t, t, wait, flash, summon_t;
    uint8_t st, armor;
    uint8_t atk;               // mbt_foe_atk_t:正在出的招
    int32_t tele; bool atk_done; int8_t atk_dir; uint8_t atk_mask;
    float atk_x, atk_from;     // 雷劈的位置 / 跳的起点
    int8_t knock_by; int32_t knock_at;
    int32_t guard_t;           // 冰盾碎了还剩多久(0 = 盾在)
    int8_t aggro; int32_t aggro_t;   // 冰盾 Boss 盯着谁、盯了多久
    int32_t open_t;            // 线圈 Boss:线圈全坏了,还剩多久不无敌;线圈自己:坏了还剩多久充好电
    int32_t trap_cd;           // 冰冻地面的冷却
    uint8_t phase;             // 魔王:0 冰盾、1 线圈、2 发怒
} mbt_foe_t;

typedef struct { bool used; uint8_t form; float x, vx; int32_t t; int8_t by; } mbt_shot_t;   // 人放的(冰球、火箭)
typedef struct { bool used, refl, big; uint8_t kind; int8_t by; float x, vx; int32_t t; int16_t dmg; } mbt_fshot_t;   // 怪扔的(kind 0 雪球、1 火箭;refl = 被弹反回去了,by = 谁弹的)
typedef struct { bool used; float x, y, vx, vy; int32_t t; uint8_t form; } mbt_star_t;
typedef struct { uint8_t kind, form, form2, lv, bars; int8_t p, q; bool low; int32_t t; } mbt_cine_t;

typedef struct {
    mbt_hero_t h[2];
    uint8_t nh;
    mbt_foe_t f[MBT_MAXF];
    mbt_shot_t shot[MBT_MAXSHOT];
    mbt_fshot_t fshot[MBT_MAXFSHOT];
    mbt_star_t star[MBT_MAXSTAR];
    bool cine_on; mbt_cine_t cine;
    int32_t now, acc, slow_t, stop_t, shake_t, combo_t, say_cd;
    uint16_t combo;
    uint16_t dmg_pct;          // 怪的伤害(关越往后越痛)
    uint8_t wave[MBT_MAXF], nwave; int16_t wave_pct;   // 第二波(打完第一波才来)
    uint32_t rng;
    int8_t result;             // 0 还在打、1 赢了、-1 全倒
    mbt_ev_t evq[MBT_EVQ]; uint8_t ev_head, ev_n;
} morph_battle_t;

// 一个人进场时带的东西(血、能量、变身、升级),打完再从 h[] 读回来
typedef struct { int16_t hp, en; uint8_t form; int16_t maxhp; uint8_t atk_lv, ult_lv; } mbt_carry_t;

// 一场怎么开:n 只怪(types);hp_pct 小怪、精英的血(越往后越厚);boss_pct Boss 的血;dmg_pct 怪的伤害
typedef struct {
    const uint8_t *types; int n;
    const uint8_t *wave; int nwave;    // 第二波(可以没有)
    int hp_pct, boss_pct, dmg_pct;
    const mbt_carry_t *carry; int nh;
    uint32_t seed;
} mbt_setup_t;

enum { FD_GUARD = 1, FD_COILS = 2, FD_RANGED = 4, FD_OBJECT = 8 };
typedef struct {
    const char *name; int16_t hp, stun, reach, dmg, speed, w, h; int8_t form; uint8_t armor; bool elite, boss;
    uint8_t atk;               // 平时的招(Boss 另外挑)
    uint8_t flags;
    uint8_t summon[2];         // Boss 叫来的帮手(ET_COUNT = 不叫)
} mbt_foe_def_t;
extern const mbt_foe_def_t morph_foes[ET_COUNT];

void morph_mbt_start(morph_battle_t *b, const mbt_setup_t *s);
void morph_mbt_input(morph_battle_t *b, int p, uint8_t key, bool long_press);
void morph_mbt_tick(morph_battle_t *b, uint32_t ms);
bool morph_mbt_event(morph_battle_t *b, mbt_ev_t *out);
int morph_mbt_alive(const morph_battle_t *b);      // 还活着的怪(线圈不算)
bool morph_mbt_shielded(const morph_battle_t *b, int fi);   // 这只怪现在无敌(线圈没全坏)
bool morph_mbt_guarding(const mbt_foe_t *f);       // 这只怪冰盾在
// 这个人现在能干什么(底下一行提示用):吞、拉、接力、击掌、超必杀、大招、平时
typedef enum {
    HINT_NONE = 0, HINT_EAT, HINT_REVIVE, HINT_RELAY, HINT_HIGHFIVE, HINT_SUPER, HINT_ULT, HINT_WAIT_UP,
    HINT_BREAK_OUT, HINT_RESCUE,
} mbt_hint_t;
mbt_hint_t morph_mbt_hint(const morph_battle_t *b, int p);
