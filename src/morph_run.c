// src/morph_run.c —— 见 morph_run.h。规则照网页小样 proto/game.html。
#include "morph_run.h"

#include <string.h>

static uint32_t rnd(morph_run_t *r) {
    r->rng = r->rng * 1103515245u + 12345u;
    return r->rng >> 8;
}

static void gen_map(morph_run_t *r) {
    memset(r->node, 0, sizeof(r->node));
    r->ncol[0] = 1;
    r->node[0][0].type = NODE_START;
    static const uint8_t POOL[6] = { NODE_BATTLE, NODE_BATTLE, NODE_BATTLE, NODE_ELITE, NODE_REST, NODE_TREASURE };
    for (int row = 1; row <= 5; row++) {
        r->ncol[row] = (uint8_t)(row == 1 ? 2 : 2 + rnd(r) % 2);
        for (int i = 0; i < r->ncol[row]; i++) {
            uint8_t t = row == 1 ? NODE_BATTLE : POOL[rnd(r) % 6];
            if (row == 5 && i == 0) t = NODE_REST;   // boss 前总有个地方歇脚
            r->node[row][i].type = t;
        }
    }
    r->ncol[6] = 1;
    r->node[6][0].type = NODE_BOSS;
    for (int row = 0; row < MR_ROWS; row++)
        for (int i = 0; i < r->ncol[row]; i++) {
            r->node[row][i].x = (int16_t)(20 + (2 * i + 1) * 100 / r->ncol[row]);
            r->node[row][i].y = (int16_t)(278 - row * 36);
        }
    for (int row = 0; row < MR_ROWS - 1; row++) {
        int a = r->ncol[row], b = r->ncol[row + 1];
        for (int i = 0; i < a; i++) {
            mr_node_info_t *nd = &r->node[row][i];
            if (a == 1) { nd->next = (uint8_t)((1u << b) - 1); continue; }   // 起点通到下一排所有点
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

void morph_mr_start(morph_run_t *r, int nh, uint32_t seed) {
    memset(r, 0, sizeof(*r));
    r->rng = seed ? seed : 1;
    r->nh = (uint8_t)(nh == 2 ? 2 : 1);
    for (int p = 0; p < 2; p++) r->carry[p] = (mbt_carry_t){ 100, 0, FORM_NORMAL };
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

static void begin_battle(morph_run_t *r, const uint8_t *types, int n) {
    int pct = (100 + 12 * r->r) * (r->nh == 2 ? 2 : 1);   // 两个人:怪的血 ×2
    morph_mbt_start(&r->bt, types, n, pct, r->nh == 2 ? 200 : 100, r->carry, r->nh, rnd(r) | 1);
    r->phase = RUN_BATTLE;
}

static void arrive(morph_run_t *r) {
    uint8_t t = r->node[r->r][r->i].type;
    if (t == NODE_BATTLE) {
        static const uint8_t POOL[3] = { ET_CLOCK, ET_ICE, ET_VOLT };
        uint8_t ty[4];
        int n = (r->r < 3 ? 2 : 3) + (r->nh == 2);   // 两个人多一只
        for (int k = 0; k < n; k++) ty[k] = POOL[rnd(r) % 3];
        begin_battle(r, ty, n);
    } else if (t == NODE_ELITE) {
        uint8_t ty[3] = { ET_BIGCLOCK, rnd(r) % 2 ? ET_ICE : ET_VOLT, ET_CLOCK };
        begin_battle(r, ty, 2 + (r->nh == 2));
    } else if (t == NODE_BOSS) {
        uint8_t ty[1] = { ET_BEAR };
        begin_battle(r, ty, 1);
    } else if (t == NODE_REST) {
        for (int p = 0; p < r->nh; p++) {
            int hp = r->carry[p].hp < 35 ? 35 : r->carry[p].hp;
            r->carry[p].hp = (int16_t)(hp + 40 > 100 ? 100 : hp + 40);
        }
        r->phase = RUN_EVENT; r->event = EVT_REST; r->event_t = 0;
    } else if (t == NODE_TREASURE) {
        for (int p = 0; p < r->nh; p++) {
            r->treasure_form[p] = rnd(r) % 2 ? FORM_ICE : FORM_VOLT;
            r->carry[p].form = r->treasure_form[p];
            r->carry[p].en = (int16_t)(r->carry[p].en + 100 > MBT_EN_MAX ? MBT_EN_MAX : r->carry[p].en + 100);
        }
        r->phase = RUN_EVENT; r->event = EVT_TREASURE; r->event_t = 0;
    }
}

void morph_mr_input(morph_run_t *r, int p, uint8_t key, bool lp) {
    switch (r->phase) {
        case RUN_BATTLE: morph_mbt_input(&r->bt, p, key, lp); break;
        case RUN_EVENT:
            if (key == MBT_OK && !lp && r->event_t > 600) r->phase = RUN_MAP;
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

void morph_mr_tick(morph_run_t *r, uint32_t ms) {
    switch (r->phase) {
        case RUN_EVENT: r->event_t += (int32_t)ms; break;
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
            if (r->bt.result < 0) { r->phase = RUN_OVER; break; }
            if (r->bt.result > 0 && !r->bt.cine_on) {   // 打赢:带走血、能量、变身;倒下的人回 35 血
                for (int p = 0; p < r->nh; p++) {
                    const mbt_hero_t *h = &r->bt.h[p];
                    r->carry[p] = (mbt_carry_t){ h->hp > 0 ? h->hp : 35, h->en, h->form };
                }
                uint8_t t = r->node[r->r][r->i].type;
                if (t == NODE_BOSS) { r->phase = RUN_WIN; break; }
                if (t == NODE_ELITE)
                    for (int p = 0; p < r->nh; p++)
                        r->carry[p].en = (int16_t)(r->carry[p].en + 100 > MBT_EN_MAX ? MBT_EN_MAX : r->carry[p].en + 100);
                r->phase = RUN_EVENT; r->event = t == NODE_ELITE ? EVT_WON_ELITE : EVT_WON; r->event_t = 0;
            }
            break;
        default: break;
    }
}
