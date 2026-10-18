// tests/test_run.c —— 一趟冒险跑通:乱按的「人」(按真人手速,一秒约 3 下、偶尔按住)一个人 / 两个人把四关各打很多趟,
// 不崩、能走到 Boss、有输有赢,打印每关的通关率和一趟多长;另外几条规则单测(▼● 技能、▼▲ 超必杀、合体、拉人、吞怪变身、
// 冰盾、线圈、冰块、弹反雪球、营地升级和开关)。
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "morph_run.h"

static uint32_t s = 12345;
static uint32_t r32(void) { s = s * 1664525u + 1013904223u; return s >> 8; }

// 一个乱按的人:每 250–450 ms 按一下,1/5 是按住(先来按下,0.5 秒后来长按)
typedef struct { int32_t next, longat; uint8_t longkey; } bot_t;
static void bot(morph_run_t *r, bot_t *b, int p, int32_t now) {
    if (b->longat && now >= b->longat) { morph_mr_input(r, p, b->longkey, true); b->longat = 0; }
    if (now < b->next) return;
    b->next = now + 250 + (int32_t)(r32() % 200);
    static const uint8_t K[5] = { MBT_UP, MBT_OK, MBT_DOWN, MBT_OK, MBT_OK };
    uint8_t k = K[r32() % 5];
    morph_mr_input(r, p, k, false);
    if (k != MBT_UP && r32() % 5 == 0) { b->longat = now + 500; b->longkey = k; }
}

// 一个会玩的人(像个上手了的孩子):看到怪抬手、雪球飞来就翻滚(反应 0.2–0.4 秒,三次里漏一次),
// 晕的怪吞、队友倒了拉、冻住狂按、能量满了放超必杀、有甲蓄力,别的时候点 ●
static void smart(morph_run_t *r, bot_t *b, int p, int32_t now) {
    if (b->longat && now >= b->longat) { morph_mr_input(r, p, b->longkey, true); b->longat = 0; }
    if (now < b->next) return;
    b->next = now + 200 + (int32_t)(r32() % 200);
    if (r->phase != RUN_BATTLE) { if (r->phase == RUN_MAP) morph_mr_input(r, p, r32() % 3 ? MBT_OK : MBT_DOWN, false); return; }
    const morph_battle_t *bt = &r->bt;
    const mbt_hero_t *h = &bt->h[p];
    if (bt->cine_on || h->hp <= 0) return;
    if (h->st == HS_TRAP) { morph_mr_input(r, p, MBT_OK, false); b->next = now + 150; return; }
    bool danger = false, armor = false, boss = false;
    for (int i = 0; i < MBT_MAXF; i++) {
        const mbt_foe_t *f = &bt->f[i];
        if (!f->used || f->hp <= 0) continue;
        const mbt_foe_def_t *d = &morph_foes[f->type];
        boss |= d->boss;
        float dx = f->x - h->x; if (dx < 0) dx = -dx;
        if ((f->armor || morph_mbt_guarding(f)) && dx < 60) armor = true;
        if (f->st == FS_TELE && f->tele - f->t < 450 &&
            (dx < d->reach + d->w / 2 + 20 || f->atk == FA_TRAP || (f->atk == FA_BOLT && (f->atk_x - h->x) * (f->atk_x - h->x) < 900)))
            danger = true;
    }
    for (int i = 0; i < MBT_MAXFSHOT; i++)
        if (bt->fshot[i].used && !bt->fshot[i].refl && (bt->fshot[i].x - h->x) * (bt->fshot[i].x - h->x) < 50 * 50) danger = true;
    if (danger && r32() % 3) { morph_mr_input(r, p, MBT_DOWN, false); return; }
    mbt_hint_t hint = morph_mbt_hint(bt, p);
    if (hint == HINT_EAT || hint == HINT_REVIVE) { morph_mr_input(r, p, MBT_OK, false); b->longat = now + 500; b->longkey = MBT_OK; b->next = now + 1300; return; }
    if (hint == HINT_HIGHFIVE) { morph_mr_input(r, p, MBT_UP, false); return; }
    if (h->en >= MBT_EN_MAX) { morph_mr_input(r, p, MBT_DOWN, false); morph_mr_input(r, p, MBT_UP, false); return; }
    if (h->en >= 200 || (h->en >= 100 && boss)) { morph_mr_input(r, p, MBT_UP, false); return; }
    if (armor && r32() % 2) { morph_mr_input(r, p, MBT_OK, false); b->longat = now + 500; b->longkey = MBT_OK; b->next = now + 1000; return; }
    if (r32() % 6 == 0) { morph_mr_input(r, p, MBT_DOWN, false); morph_mr_input(r, p, MBT_OK, false); return; }   // 技能
    morph_mr_input(r, p, MBT_OK, false);
}

// 第 ch 关打 n 趟,升级都是 lv 级;good = 会玩的人(否则乱按)
static void runs(int nh, int ch, int lv, int n, bool good, int *wins, int *boss, double *min_avg) {
    static morph_run_t r;
    *wins = *boss = 0;
    double ms_sum = 0;
    for (int k = 0; k < n; k++) {
        mr_profile_t pf[2] = { { 4, 0, { 0 }, 0 }, { 4, 0, { 0 }, 0 } };
        for (int p = 0; p < 2; p++) memset(pf[p].lv, lv, UPG_COUNT);
        morph_mr_start(&r, nh, 1000 + (uint32_t)k * 7 + (uint32_t)ch, pf);
        r.chapter = (uint8_t)ch;
        for (int p = 0; p < nh; p++) morph_mr_input(&r, p, MBT_OK, false);   // 光标一开始就在「出发」
        assert(r.phase == RUN_MAP);
        bot_t b[2] = { 0 };
        int32_t now = 0;
        bool saw_boss = false;
        while (r.phase != RUN_WIN && r.phase != RUN_OVER && now < 40 * 60 * 1000) {
            for (int p = 0; p < nh; p++) { if (good) smart(&r, &b[p], p, now); else bot(&r, &b[p], p, now); }
            if (r.phase == RUN_EVENT && r.event_t > 700) morph_mr_input(&r, 0, MBT_OK, false);
            morph_mr_tick(&r, 17);
            now += 17;
            if (r.phase == RUN_BATTLE && r.node[r.r][r.i].type == NODE_BOSS) saw_boss = true;
            mbt_ev_t e;
            if (r.phase == RUN_BATTLE) while (morph_mbt_event(&r.bt, &e)) {}
        }
        assert(r.phase == RUN_WIN || r.phase == RUN_OVER);
        *wins += r.phase == RUN_WIN;
        *boss += saw_boss;
        ms_sum += now;
    }
    *min_avg = ms_sum / n / 60000.0;
}

static morph_battle_t B;
static void tick(int ms) { for (int t = 0; t < ms; t += 10) morph_mbt_tick(&B, 10); }
static void start(const uint8_t *types, int n, int nh, int16_t en0, int16_t en1) {
    mbt_carry_t c[2] = { { 100, en0, FORM_NORMAL, 100, 0, 0 }, { 100, en1, FORM_NORMAL, 100, 0, 0 } };
    mbt_setup_t st = { types, n, NULL, 0, 100, 100, 100, c, nh, 7 };
    morph_mbt_start(&B, &st);
    tick(1100);   // 进场无敌过掉
}
static void one(uint8_t type, int nh, int16_t en0, int16_t en1) { start(&type, 1, nh, en0, en1); }
static int find(uint8_t type) {
    for (int i = 0; i < MBT_MAXF; i++) if (B.f[i].used && B.f[i].type == type) return i;
    return -1;
}

static void t_skill_super_combo(void) {
    one(ET_BEAR, 1, 300, 0);
    morph_mbt_input(&B, 0, MBT_DOWN, false); tick(100); morph_mbt_input(&B, 0, MBT_UP, false);
    assert(B.cine_on && B.cine.kind == CINE_SUPER);
    tick(2400);
    assert(!B.cine_on && B.f[0].hp == B.f[0].max - 150 && B.f[0].armor == 0);   // 超必杀 150、铁甲全碎
    one(ET_BEAR, 2, 100, 200);
    morph_mbt_input(&B, 0, MBT_UP, false); assert(B.h[0].pend && !B.cine_on);
    tick(300); morph_mbt_input(&B, 1, MBT_UP, false);
    assert(B.cine_on && B.cine.kind == CINE_COMBO && B.cine.bars == 3);
    one(ET_BEAR, 2, 100, 200);
    morph_mbt_input(&B, 0, MBT_UP, false); tick(900);   // 队友不接:0.8 秒后自己放
    assert(B.cine_on && B.cine.kind == CINE_ULT && B.cine.lv == 1);
    one(ET_CLOCK, 1, 0, 0);
    B.h[0].x = B.f[0].x - 30;
    morph_mbt_input(&B, 0, MBT_DOWN, false); tick(150); morph_mbt_input(&B, 0, MBT_OK, false);
    assert(B.h[0].st == HS_SKILL);
}

// 大招在场上一下一下打:近的先挨,远的后挨;冲 / 跳过去的人真的到了怪跟前
static void t_cine_field(void) {
    uint8_t three[3] = { ET_CLOCK, ET_CLOCK, ET_CLOCK };
    start(three, 3, 1, 100, 0);
    B.f[0].x = 120; B.f[1].x = 160; B.f[2].x = 220; B.h[0].x = 40;
    B.h[0].form = FORM_VOLT;
    morph_mbt_input(&B, 0, MBT_UP, false);
    assert(B.cine_on && B.cine.n == 3 && B.cine.hits == 2 && B.cine.tgt[0] == 0 && B.cine.tgt[2] == 2);
    tick(morph_mbt_cine_hit0(&B.cine, 0));
    assert(B.f[0].hp < B.f[0].max && B.f[2].hp == B.f[2].max);
    tick(B.cine.end - B.cine.t);
    assert(!B.cine_on && B.f[2].hp < B.f[2].max && B.h[0].x == 40);
    one(ET_BEAR, 1, 300, 0);
    B.h[0].form = FORM_SPRING; B.h[0].x = 30; B.f[0].x = 200;
    morph_mbt_input(&B, 0, MBT_DOWN, false); tick(100); morph_mbt_input(&B, 0, MBT_UP, false);
    assert(B.cine_on && B.cine.kind == CINE_SUPER && B.cine.n == 1);
    tick(B.cine.end);
    assert(!B.cine_on && fabsf(B.h[0].x - B.f[0].x) < 40 && B.f[0].hp == B.f[0].max - 150);
}

static void t_eat_revive(void) {
    uint8_t two[2] = { ET_ICE, ET_BEAR };   // 留一只熊,吞完冰淇淋怪这场还没打完
    start(two, 2, 2, 0, 0);
    int bear = find(ET_BEAR), ice = find(ET_ICE);
    B.f[bear].x = 230; B.f[bear].summon_t = 999999; B.f[bear].freeze_t = 999999;   // 熊冻在远处别来捣乱
    B.f[ice].stun_t = 3000; B.f[ice].st = FS_STUN; B.h[0].x = B.f[ice].x - 20;
    morph_mbt_input(&B, 0, MBT_OK, true);
    assert(B.h[0].st == HS_SWALLOW);
    tick(1300);
    assert(B.h[0].form == FORM_ICE && B.h[0].st != HS_MORPH);
    B.h[1].hp = 0; B.h[1].st = HS_DOWN; B.h[1].x = B.h[0].x + 10;
    morph_mbt_input(&B, 0, MBT_OK, true);
    assert(B.h[0].st == HS_REVIVE);
    tick(900);
    assert(B.h[1].hp == 35);
}

// 像玩家那样吞:连点 ● 把冰淇淋怪打晕(晕着拳脚打不死)、按住 ●(先来一次点、再来长按)吞掉变身
static void t_eat_like_player(void) {
    one(ET_ICE, 1, 0, 0);
    B.h[0].x = B.f[0].x - 24;
    for (int i = 0; i < 12 && B.h[0].form == FORM_NORMAL; i++) {
        morph_mbt_input(&B, 0, MBT_OK, false); tick(300);
        morph_mbt_input(&B, 0, MBT_OK, false); tick(500);
        morph_mbt_input(&B, 0, MBT_OK, true); tick(1300);
    }
    assert(B.h[0].form == FORM_ICE);
}

// 冰淇淋大王:正面挨打只掉一点,背后打得多;蓄力斩打正面 → 冰盾碎
static void t_guard(void) {
    one(ET_ICEKING, 2, 0, 0);
    mbt_foe_t *k = &B.f[0];
    k->freeze_t = 999999;   // 别动
    k->face = 1;            // 朝右:右边是正面
    B.h[1].x = k->x + 30; B.h[1].face = -1;
    B.h[0].x = 20;
    int hp = k->hp;
    morph_mbt_input(&B, 1, MBT_OK, false); tick(200);
    int front = hp - k->hp;
    hp = k->hp;
    B.h[1].x = 20; B.h[0].x = k->x - 30; B.h[0].face = 1; B.h[0].chain_t = 0;
    morph_mbt_input(&B, 0, MBT_OK, false); tick(200);
    int back = hp - k->hp;
    assert(front == 1 && back == 8);   // 正面 6×0.15 → 1;背面 6×1.3 → 8
    B.h[0].x = k->x + 30; B.h[0].face = -1;
    morph_mbt_input(&B, 0, MBT_OK, true); tick(300);
    assert(k->guard_t > 0 && !morph_mbt_guarding(k));
}

// 发电大魔像:线圈没全坏打不动;两个线圈 3 秒内都打坏 → 不无敌;只坏一个 → 过 3 秒又起来
static void t_coils(void) {
    one(ET_DYNAMO, 2, 0, 0);
    int d = find(ET_DYNAMO);
    int c0 = -1, c1 = -1;
    for (int i = 0; i < MBT_MAXF; i++) if (B.f[i].used && B.f[i].type == ET_COIL) { if (c0 < 0) c0 = i; else c1 = i; }
    assert(d >= 0 && c0 >= 0 && c1 >= 0 && morph_mbt_shielded(&B, d));
    B.f[d].freeze_t = 999999; B.f[d].summon_t = 999999;
    int hp = B.f[d].hp;
    B.h[0].x = B.f[d].x - 25; B.h[0].face = 1;
    morph_mbt_input(&B, 0, MBT_OK, false); tick(200);
    assert(B.f[d].hp == hp);   // 无敌
    B.f[c0].hp = 1; B.h[0].x = B.f[c0].x + 20; B.h[0].face = -1; B.h[0].chain_t = 0;
    morph_mbt_input(&B, 0, MBT_OK, false); tick(200);
    assert(B.f[c0].hp == 0 && morph_mbt_shielded(&B, d));
    tick(3200);
    assert(B.f[c0].hp == B.f[c0].max);   // 另一个没跟上:又充好电
    B.f[c0].hp = 1; B.f[c1].hp = 1;
    B.h[0].x = B.f[c0].x + 20; B.h[0].face = -1; B.h[1].x = B.f[c1].x - 20; B.h[1].face = 1;
    B.h[0].chain_t = B.h[1].chain_t = 0;
    morph_mbt_input(&B, 0, MBT_OK, false); morph_mbt_input(&B, 1, MBT_OK, false); tick(200);
    assert(!morph_mbt_shielded(&B, d) && B.f[d].open_t > 0);
    tick(10500);
    assert(morph_mbt_shielded(&B, d));   // 10 秒后线圈又起来
}

// 冰块:自己狂按 ● 6 下出来;队友蓄力斩一下就打碎;4 秒没出来扣血自己出来
static void t_trap(void) {
    one(ET_CLOCK, 2, 0, 0);
    B.f[0].freeze_t = 999999; B.f[0].x = 230;
    B.h[0].st = HS_TRAP; B.h[0].trap_t = 4000; B.h[0].trap_hp = 6; B.h[0].t = 0;
    for (int i = 0; i < 6; i++) { morph_mbt_input(&B, 0, MBT_OK, false); tick(50); }
    assert(B.h[0].st != HS_TRAP);
    B.h[0].st = HS_TRAP; B.h[0].trap_t = 4000; B.h[0].trap_hp = 6;
    B.h[1].x = B.h[0].x + 20; B.h[1].st = HS_IDLE; B.h[1].face = -1;
    morph_mbt_input(&B, 1, MBT_OK, true); tick(300);
    assert(B.h[0].st != HS_TRAP);
    B.h[0].st = HS_TRAP; B.h[0].trap_t = 4000; B.h[0].trap_hp = 6; B.h[0].hp = 100;
    B.h[1].x = 200;
    tick(4200);
    assert(B.h[0].st != HS_TRAP && B.h[0].hp == 82);
}

// 雪球:刚举盾挨上 → 弹回去打怪
static void t_reflect(void) {
    one(ET_SNOW, 1, 0, 0);
    mbt_foe_t *f = &B.f[0];
    f->freeze_t = 999999; f->x = 200;
    B.h[0].x = 100; B.h[0].face = 1; B.h[0].st = HS_SHIELD; B.h[0].shield_age = 0; B.h[0].shield_t = 1000; B.h[0].inv = 0;
    B.fshot[0] = (mbt_fshot_t){ .used = true, .kind = 0, .by = -1, .x = 112, .vx = -110, .t = 3000, .dmg = 10 };
    int hp = f->hp;
    tick(1500);
    assert(B.h[0].hp == 100 && f->hp < hp);
}

// 营地:升级花零件、两个人都按出发才走;打通一关开下一关
static void t_hub(void) {
    static morph_run_t r;
    mr_profile_t pf[2] = { { 1, 0, { 0 }, 5 }, { 2, 1, { 3, 0, 0, 0 }, 20 } };
    morph_mr_start(&r, 2, 3, pf);
    assert(r.phase == RUN_HUB && morph_mr_open(&r) == 2 && r.chapter == 1);
    morph_mr_input(&r, 0, MBT_DOWN, false);   // 出发 → 回首页
    assert(r.hub_sel[0] == HUB_QUIT);
    for (int i = 0; i < 2; i++) morph_mr_input(&r, 0, MBT_DOWN, false);   // → 选关 → 血量
    assert(r.hub_sel[0] == HUB_HP);
    morph_mr_input(&r, 0, MBT_OK, false);
    assert(r.prof[0].lv[UPG_HP] == 1 && r.prof[0].parts == 1 && r.hub_msg[0] == HM_BOUGHT);
    morph_mr_input(&r, 0, MBT_OK, false);
    assert(r.prof[0].lv[UPG_HP] == 1 && r.hub_msg[0] == HM_POOR);
    r.hub_sel[1] = HUB_HP; morph_mr_input(&r, 1, MBT_OK, false);
    assert(r.hub_msg[1] == HM_MAXED && r.prof[1].parts == 20);
    r.hub_sel[0] = HUB_CHAPTER; morph_mr_input(&r, 0, MBT_OK, false);
    assert(r.chapter == 0);
    r.hub_sel[0] = r.hub_sel[1] = HUB_GO;
    morph_mr_input(&r, 0, MBT_OK, false);
    assert(r.phase == RUN_HUB && r.ready[0]);
    morph_mr_input(&r, 1, MBT_OK, false);
    assert(r.phase == RUN_MAP && r.carry[0].maxhp == 120 && r.carry[1].maxhp == 160);
    r.phase = RUN_BATTLE; r.r = MR_ROWS - 1; r.i = 0; r.bt.result = 1;
    morph_mr_tick(&r, 10);
    assert(r.phase == RUN_WIN && r.first_clear && r.prof[0].unlocked == 2 && r.prof[0].cleared == 1);
    assert(r.prof[0].parts == 1 + 6 + 6 && r.chapter == 1);
    morph_mr_tick(&r, 1000); morph_mr_input(&r, 1, MBT_OK, false);
    assert(r.phase == RUN_HUB);
}

int main(void) {
    t_skill_super_combo();
    t_cine_field();
    t_eat_revive();
    t_eat_like_player();
    t_guard();
    t_coils();
    t_trap();
    t_reflect();
    t_hub();
    static const int LV[MR_CHAPTERS] = { 0, 1, 2, 3 };   // 打到这关时大概升到几级
    for (int good = 0; good <= 1; good++)
        for (int nh = 1; nh <= 2; nh++)
            for (int ch = 0; ch < MR_CHAPTERS; ch++) {
                int w, b;
                double m;
                runs(nh, ch, LV[ch], 30, good, &w, &b, &m);
                printf("test_run: %s%s第 %d 关(升级 %d 级)30 趟:打到 Boss %d、通关 %d、平均一趟 %.1f 分钟\n",
                       good ? "会玩的" : "乱按的", nh == 1 ? "一个人" : "两个人", ch + 1, LV[ch], b, w, m);
                assert(b > 0);
            }
    puts("test_run: ok");
    return 0;
}
