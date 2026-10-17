// src/morph_run.c —— 见 morph_run.h。第 1 关的规则照网页小样 proto/game.html。
#include "morph_run.h"

#include <string.h>

#define PARTS_BATTLE 1
#define PARTS_ELITE 3
#define PARTS_TREASURE 2
#define PARTS_BOSS 6
#define PARTS_FIRST 6        // 第一次打通这关另给
#define RESULT_MS 900        // 结果出来多久以后 ● 才认(别把正在狂按的 ● 当成回营地)

const mr_chapter_t morph_chapters[MR_CHAPTERS] = {
    { { ET_CLOCK, ET_ICE, ET_VOLT }, 3, { ET_BIGCLOCK }, 1, ET_BEAR, { FORM_ICE, FORM_VOLT }, 2, 100, 100 },
    { { ET_ICE, ET_SNOW, ET_FROG, ET_CLOCK }, 4, { ET_SNOWMAN, ET_BIGCLOCK }, 2, ET_ICEKING, { FORM_ICE, FORM_SPRING }, 2, 115, 110 },
    { { ET_VOLT, ET_MAGNET, ET_ROCKET, ET_CLOCK }, 4, { ET_BIGVOLT, ET_SNOWMAN }, 2, ET_DYNAMO, { FORM_MAGNET, FORM_ROCKET, FORM_VOLT }, 3, 130, 120 },
    { { ET_SNOW, ET_FROG, ET_MAGNET, ET_ROCKET, ET_ICE, ET_VOLT }, 6, { ET_BIGVOLT, ET_SNOWMAN, ET_BIGCLOCK }, 3, ET_DEMON,
      { FORM_ICE, FORM_VOLT, FORM_SPRING, FORM_MAGNET, FORM_ROCKET }, 5, 145, 130 },
};

static uint32_t rnd(morph_run_t *r) {
    r->rng = r->rng * 1103515245u + 12345u;
    return r->rng >> 8;
}

int morph_mr_upg_cost(uint8_t lv) {
    static const int8_t COST[MR_UPG_MAX] = { 4, 8, 14 };
    return lv < MR_UPG_MAX ? COST[lv] : -1;
}

void morph_mr_profile_clean(mr_profile_t *p) {
    if (p->unlocked < 1) p->unlocked = 1;
    if (p->unlocked > MR_CHAPTERS) p->unlocked = MR_CHAPTERS;
    p->cleared &= (1u << MR_CHAPTERS) - 1;
    for (int k = 0; k < UPG_COUNT; k++) if (p->lv[k] > MR_UPG_MAX) p->lv[k] = MR_UPG_MAX;
    if (p->parts > 999) p->parts = 999;
}

uint8_t morph_mr_open(const morph_run_t *r) {
    uint8_t n = r->prof[0].unlocked;
    if (r->nh == 2 && r->prof[1].unlocked > n) n = r->prof[1].unlocked;
    return n;
}

static void gen_map(morph_run_t *r) {
    memset(r->node, 0, sizeof(r->node));
    memset(r->ncol, 0, sizeof(r->ncol));
    r->ncol[0] = 1;
    r->node[0][0].type = NODE_START;
    static const uint8_t POOL[7] = { NODE_BATTLE, NODE_BATTLE, NODE_BATTLE, NODE_BATTLE, NODE_ELITE, NODE_REST, NODE_TREASURE };
    for (int row = 1; row <= MR_ROWS - 2; row++) {
        r->ncol[row] = (uint8_t)(row == 1 ? 2 : 2 + rnd(r) % 2);
        for (int i = 0; i < r->ncol[row]; i++) {
            uint8_t t = row <= 2 ? NODE_BATTLE : POOL[rnd(r) % 7];
            if (row == MR_ROWS - 2 && i == 0) t = NODE_REST;   // Boss 前总有个地方歇脚
            if (row == 4 && i == r->ncol[row] - 1) t = NODE_ELITE;   // 半路总有一只精英(可以绕开)
            r->node[row][i].type = t;
        }
    }
    r->ncol[MR_ROWS - 1] = 1;
    r->node[MR_ROWS - 1][0].type = NODE_BOSS;
    for (int row = 0; row < MR_ROWS; row++)
        for (int i = 0; i < r->ncol[row]; i++) {
            r->node[row][i].x = (int16_t)(20 + (2 * i + 1) * 100 / r->ncol[row]);
            r->node[row][i].y = (int16_t)(276 - row * 26);
        }
    for (int row = 0; row < MR_ROWS - 1; row++) {
        int a = r->ncol[row], b = r->ncol[row + 1];
        for (int i = 0; i < a; i++) {
            mr_node_info_t *nd = &r->node[row][i];
            if (a == 1) { nd->next = (uint8_t)((1u << b) - 1); continue; }   // 起点通到下一排所有点
            if (b == 1) { nd->next = 1; continue; }
            int j = (i * (b - 1) * 2 + (a - 1)) / (2 * (a - 1));               // 四舍五入的 i*(b-1)/(a-1)
            nd->next |= (uint8_t)(1u << j);
            if (j + 1 < b && rnd(r) % 2) nd->next |= (uint8_t)(1u << (j + 1));
        }
        for (int j = 0; j < b; j++) {   // 下一排每个点都要有路通过来
            bool in = false;
            for (int i = 0; i < a; i++) in |= (r->node[row][i].next >> j) & 1;
            if (in) continue;
            int best = 0;
            for (int i = 1; i < a; i++) {
                int d0 = r->node[row][best].x - r->node[row + 1][j].x, d1 = r->node[row][i].x - r->node[row + 1][j].x;
                if ((d1 < 0 ? -d1 : d1) < (d0 < 0 ? -d0 : d0)) best = i;
            }
            r->node[row][best].next |= (uint8_t)(1u << j);
        }
    }
}

static void to_hub(morph_run_t *r) {
    r->phase = RUN_HUB;
    for (int p = 0; p < 2; p++) { r->ready[p] = false; r->hub_msg[p] = HM_NONE; }
}

void morph_mr_start(morph_run_t *r, int nh, uint32_t seed, const mr_profile_t prof[2]) {
    memset(r, 0, sizeof(*r));
    r->rng = seed ? seed : 1;
    r->nh = (uint8_t)(nh == 2 ? 2 : 1);
    for (int p = 0; p < 2; p++) { r->prof[p] = prof[p]; morph_mr_profile_clean(&r->prof[p]); }
    r->chapter = (uint8_t)(morph_mr_open(r) - 1);
    for (int p = 0; p < 2; p++) r->hub_sel[p] = HUB_GO;
    to_hub(r);
}

static void begin_chapter(morph_run_t *r) {
    for (int p = 0; p < 2; p++) {
        const mr_profile_t *pf = &r->prof[p];
        int16_t maxhp = (int16_t)(100 + 20 * pf->lv[UPG_HP]);
        r->carry[p] = (mbt_carry_t){ maxhp, (int16_t)(50 * pf->lv[UPG_EN]), FORM_NORMAL, maxhp, pf->lv[UPG_ATK], pf->lv[UPG_ULT] };
    }
    r->gained = 0; r->first_clear = false;
    r->r = r->i = 0; r->choice = 0; r->walking = false;
    gen_map(r);
    r->phase = RUN_MAP;
}

uint8_t morph_mr_next_count(const morph_run_t *r) {
    if (r->r >= MR_ROWS - 1) return 0;
    uint8_t n = 0, m = r->node[r->r][r->i].next;
    while (m) { n += m & 1; m >>= 1; }
    return n;
}
int morph_mr_next_col(const morph_run_t *r, int k) {
    uint8_t m = r->node[r->r][r->i].next;
    for (int j = 0; j < MR_COLS; j++)
        if ((m >> j) & 1) { if (!k--) return j; }
    return 0;
}

static void add_parts(morph_run_t *r, int n) {
    for (int p = 0; p < r->nh; p++) r->prof[p].parts = (uint16_t)(r->prof[p].parts + n > 999 ? 999 : r->prof[p].parts + n);
    r->gained = (uint16_t)(r->gained + n);
}

static void begin_battle(morph_run_t *r, const uint8_t *types, int n, const uint8_t *wave, int nwave) {
    const mr_chapter_t *c = &morph_chapters[r->chapter];
    mbt_setup_t s = { types, n, wave, nwave, (c->hp_base + 6 * r->r) * (r->nh == 2 ? 2 : 1), r->nh == 2 ? 200 : 100,
                      c->dmg_pct * (r->nh == 2 ? 100 : 85) / 100, r->carry, r->nh, rnd(r) | 1 };   // 一个人怪下手轻一点
    morph_mbt_start(&r->bt, &s);
    r->phase = RUN_BATTLE;
}

static void arrive(morph_run_t *r) {
    const mr_chapter_t *c = &morph_chapters[r->chapter];
    uint8_t t = r->node[r->r][r->i].type;
    if (t == NODE_BATTLE) {
        uint8_t ty[4], w[4];
        int n = (r->r < 3 ? 2 : 3) + (r->nh == 2);   // 两个人多一只
        int nw = r->r < 3 ? 0 : 2 + (r->nh == 2);    // 往后打完一波还有一波
        for (int k = 0; k < n; k++) ty[k] = c->pool[rnd(r) % c->npool];
        for (int k = 0; k < nw; k++) w[k] = c->pool[rnd(r) % c->npool];
        begin_battle(r, ty, n, w, nw);
    } else if (t == NODE_ELITE) {
        uint8_t ty[3] = { c->elites[rnd(r) % c->nelite], c->pool[rnd(r) % c->npool], c->pool[rnd(r) % c->npool] };
        begin_battle(r, ty, 2 + (r->nh == 2), NULL, 0);
    } else if (t == NODE_BOSS) {
        uint8_t ty[1] = { c->boss };
        begin_battle(r, ty, 1, NULL, 0);
    } else if (t == NODE_REST) {
        for (int p = 0; p < r->nh; p++) {
            int m = r->carry[p].maxhp, lo = m * 35 / 100;
            int hp = r->carry[p].hp < lo ? lo : r->carry[p].hp;
            r->carry[p].hp = (int16_t)(hp + 40 > m ? m : hp + 40);
        }
        r->phase = RUN_EVENT; r->event = EVT_REST; r->event_t = 0;
    } else if (t == NODE_TREASURE) {
        for (int p = 0; p < r->nh; p++) {
            r->treasure_form[p] = c->forms[rnd(r) % c->nform];
            r->carry[p].form = r->treasure_form[p];
            r->carry[p].en = (int16_t)(r->carry[p].en + 100 > MBT_EN_MAX ? MBT_EN_MAX : r->carry[p].en + 100);
        }
        add_parts(r, PARTS_TREASURE);
        r->phase = RUN_EVENT; r->event = EVT_TREASURE; r->event_t = 0;
    }
}

static void hub_input(morph_run_t *r, int p, uint8_t key) {
    uint8_t *sel = &r->hub_sel[p];
    if (key == MBT_UP || key == MBT_DOWN) {
        *sel = (uint8_t)((*sel + (key == MBT_UP ? HUB_COUNT - 1 : 1)) % HUB_COUNT);
        r->ready[p] = false; r->hub_msg[p] = HM_NONE;
        return;
    }
    mr_profile_t *pf = &r->prof[p];
    r->hub_item[p] = *sel;
    switch (*sel) {
        case HUB_CHAPTER:
            r->chapter = (uint8_t)((r->chapter + 1) % morph_mr_open(r));
            r->ready[0] = r->ready[1] = false;
            r->hub_msg[p] = HM_NONE;
            break;
        case HUB_HP: case HUB_ATK: case HUB_ULT: case HUB_EN: {
            int u = *sel - HUB_HP, cost = morph_mr_upg_cost(pf->lv[u]);
            if (cost < 0) r->hub_msg[p] = HM_MAXED;
            else if (pf->parts < cost) r->hub_msg[p] = HM_POOR;
            else { pf->parts = (uint16_t)(pf->parts - cost); pf->lv[u]++; r->hub_msg[p] = HM_BOUGHT; }
            break;
        }
        case HUB_GO:
            r->ready[p] = true;
            if (r->nh == 1 || r->ready[1 - p]) begin_chapter(r);
            else r->hub_msg[p] = HM_WAIT_MATE;
            break;
        case HUB_QUIT: r->quit[p] = true; break;
        default: break;
    }
}

void morph_mr_input(morph_run_t *r, int p, uint8_t key, bool lp) {
    if (p < 0 || p >= r->nh) return;
    switch (r->phase) {
        case RUN_BATTLE: morph_mbt_input(&r->bt, p, key, lp); break;
        case RUN_EVENT:
            if (key == MBT_OK && !lp && r->event_t > 600) r->phase = RUN_MAP;
            break;
        case RUN_WIN:
        case RUN_OVER:
            if (key == MBT_OK && !lp && r->event_t > RESULT_MS) to_hub(r);
            break;
        case RUN_HUB:
            if (!lp) hub_input(r, p, key);
            break;
        case RUN_MAP: {
            if (r->walking || lp) break;
            uint8_t n = morph_mr_next_count(r);
            if (n < 2) break;
            if (key == MBT_UP) r->choice = (uint8_t)((r->choice + n - 1) % n);
            else if (key == MBT_DOWN) r->choice = (uint8_t)((r->choice + 1) % n);
            else { r->walking = true; r->to = (uint8_t)morph_mr_next_col(r, r->choice); r->walk_t = 0; }
            break;
        }
        default: break;
    }
}

static void chapter_won(morph_run_t *r) {
    add_parts(r, PARTS_BOSS);
    uint8_t bit = (uint8_t)(1u << r->chapter);
    r->first_clear = false;
    for (int p = 0; p < r->nh; p++) r->first_clear |= !(r->prof[p].cleared & bit);
    if (r->first_clear) add_parts(r, PARTS_FIRST);
    for (int p = 0; p < r->nh; p++) {
        r->prof[p].cleared |= bit;
        if (r->prof[p].unlocked < r->chapter + 2 && r->chapter + 2 <= MR_CHAPTERS) r->prof[p].unlocked = (uint8_t)(r->chapter + 2);
    }
    r->phase = RUN_WIN; r->event_t = 0;
    if (r->chapter + 1 < morph_mr_open(r)) r->chapter++;   // 营地里接着选下一关
}

void morph_mr_tick(morph_run_t *r, uint32_t ms) {
    switch (r->phase) {
        case RUN_EVENT: case RUN_WIN: case RUN_OVER: r->event_t += (int32_t)ms; break;
        case RUN_MAP:
            if (r->walking) {
                r->walk_t += (int32_t)ms;
                if (r->walk_t >= MR_WALK_MS) {
                    r->walking = false; r->r++; r->i = r->to; r->choice = 0;
                    arrive(r);
                }
            } else if (morph_mr_next_count(r) == 1) {   // 只有一条路:自己走
                r->walking = true; r->to = (uint8_t)morph_mr_next_col(r, 0); r->walk_t = 0;
            }
            break;
        case RUN_BATTLE:
            morph_mbt_tick(&r->bt, ms);
            if (r->bt.result < 0) { r->phase = RUN_OVER; r->event_t = 0; break; }
            if (r->bt.result > 0 && !r->bt.cine_on) {   // 打赢:带走血、能量、变身;倒下的人回 35% 血
                for (int p = 0; p < r->nh; p++) {
                    const mbt_hero_t *h = &r->bt.h[p];
                    r->carry[p].hp = h->hp > 0 ? h->hp : (int16_t)(h->maxhp * 35 / 100);
                    r->carry[p].en = h->en; r->carry[p].form = h->form;
                }
                uint8_t t = r->node[r->r][r->i].type;
                if (t == NODE_BOSS) { chapter_won(r); break; }
                add_parts(r, t == NODE_ELITE ? PARTS_ELITE : PARTS_BATTLE);
                if (t == NODE_ELITE)
                    for (int p = 0; p < r->nh; p++)
                        r->carry[p].en = (int16_t)(r->carry[p].en + 100 > MBT_EN_MAX ? MBT_EN_MAX : r->carry[p].en + 100);
                r->phase = RUN_EVENT; r->event = t == NODE_ELITE ? EVT_WON_ELITE : EVT_WON; r->event_t = 0;
            }
            break;
        default: break;
    }
}
