// tests/test_run.c —— 一趟冒险跑通:乱按的「人」(按真人手速,一秒约 3 下、偶尔按住)一个人 / 两个人打很多趟,
// 不崩、能走到 boss、有输有赢;另外几条规则单测(▼● 技能、▼▲ 超必杀、合体、拉人、吞怪变身)。
#include <assert.h>
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

static void runs(int nh, int n, int *wins, int *boss) {
    static morph_run_t r;
    *wins = *boss = 0;
    for (int k = 0; k < n; k++) {
        morph_mr_start(&r, nh, 1000 + (uint32_t)k);
        bot_t b[2] = { 0 };
        int32_t now = 0;
        bool saw_boss = false;
        while (r.phase != RUN_WIN && r.phase != RUN_OVER && now < 40 * 60 * 1000) {
            for (int p = 0; p < nh; p++) bot(&r, &b[p], p, now);
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
    }
}

static morph_battle_t B;
static void tick(int ms) { for (int t = 0; t < ms; t += 10) morph_mbt_tick(&B, 10); }
static void one(uint8_t type, int nh, int16_t en0, int16_t en1) {
    mbt_carry_t c[2] = { { 100, en0, FORM_NORMAL }, { 100, en1, FORM_NORMAL } };
    morph_mbt_start(&B, &type, 1, 100, 100, c, nh, 7);
    tick(1100);   // 进场无敌过掉
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

static void t_eat_revive(void) {
    uint8_t two[2] = { ET_ICE, ET_BEAR };   // 留一只熊,吞完冰淇淋怪这场还没打完
    mbt_carry_t c[2] = { { 100, 0, FORM_NORMAL }, { 100, 0, FORM_NORMAL } };
    morph_mbt_start(&B, two, 2, 100, 100, c, 2, 7);
    tick(1100);
    B.f[1].x = 230; B.f[1].summon_t = 999999; B.f[1].freeze_t = 999999;   // 熊冻在远处别来捣乱
    B.f[0].stun_t = 3000; B.f[0].st = FS_STUN; B.h[0].x = B.f[0].x - 20;
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

int main(void) {
    t_skill_super_combo();
    t_eat_revive();
    int w, b;
    runs(1, 40, &w, &b);
    printf("test_run: 一个人乱按 40 趟,打到 boss %d、通关 %d\n", b, w);
    assert(b > 0 && w > 0 && w < 40);
    runs(2, 40, &w, &b);
    printf("test_run: 两个人乱按 40 趟,打到 boss %d、通关 %d\n", b, w);
    assert(b > 0 && w > 0 && w < 40);
    puts("test_run: ok");
    return 0;
}
