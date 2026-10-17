// tests/duo.c —— 见 duo.h。
#include "duo.h"

#include <assert.h>
#include <string.h>

#include "morph_levels.h"

duo_t D;
static uint32_t s_test = 7;

uint32_t test_rnd(void) {
    s_test = s_test * 1664525u + 1013904223u;
    return s_test >> 8;
}

void test_seed(uint32_t s) { s_test = s; }

bool duo_send(void *ctx, const void *m, size_t len) {
    pipe_t *p = ctx;
    assert(len >= 1 && len <= MP_MSG_MAX);
    if (!p->up) return false;
    if (p->n >= p->cap) {
        p->refused++;
        return false;
    }
    int i = (p->head + p->n) % PIPE_N;
    memcpy(p->q[i], m, len);
    p->len[i] = len;
    p->n++;
    if (p->n > p->max_n) p->max_n = p->n;
    return true;
}

static void pipe_reset(pipe_t *p) {
    p->n = p->head = 0;
    p->flying = false;
    p->ready = p->stall_until = D.now;
}

void duo_init(void) {
    memset(&D, 0, sizeof D);
    mp_init(&D.a, duo_send, &D.ab, &morph_level_test);
    mp_init(&D.b, duo_send, &D.ba, &morph_level_test);
    D.ab.cap = D.ba.cap = 8;
}

void duo_connect(void) {
    pipe_reset(&D.ab);
    pipe_reset(&D.ba);
    D.ab.up = D.ba.up = true;
    mp_connected(&D.a, true);
    mp_connected(&D.b, false);
}

void duo_drop(void) {
    D.ab.up = D.ba.up = false;
    pipe_reset(&D.ab);
    pipe_reset(&D.ba);
    mp_lost(&D.a);
    mp_lost(&D.b);
}

void duo_delay(uint32_t ms, uint32_t jitter) {
    D.ab.delay = D.ba.delay = ms;
    D.ab.jitter = D.ba.jitter = jitter;
}

void duo_stall(pipe_t *p) { p->stall_until = D.now + DUO_STALL_MS; }

static uint32_t one_way(const pipe_t *p) {
    if (!p->jitter) return p->delay;
    int32_t d = (int32_t)p->delay + (int32_t)(test_rnd() % (2 * p->jitter + 1)) - (int32_t)p->jitter;
    return d > 0 ? (uint32_t)d : 0;
}

static bool deliver(pipe_t *p, const pipe_t *back, morph_play_t *to) {
    if (!p->n) return false;
    if (!p->flying) {
        uint32_t dep = p->ready > D.now ? p->ready : D.now;
        p->arrive = dep + one_way(p);
        p->flying = true;
    }
    uint32_t at = p->arrive > p->stall_until ? p->arrive : p->stall_until;
    if (D.now < at) return false;
    uint8_t m[MP_MSG_MAX];
    size_t len = p->len[p->head];
    memcpy(m, p->q[p->head], len);
    p->head = (p->head + 1) % PIPE_N;
    p->n--;
    p->flying = false;
    p->ready = at + back->delay;  // 确认走回来,下一片才出发
    p->msgs[m[0] & 127]++;
    if (m[0] == 'B') memcpy(p->last_b, m, len);
    mp_on_msg(to, m, len);
    return true;
}

void flow(void) {
    for (bool any = true; any;) {
        any = false;
        while (deliver(&D.ab, &D.ba, &D.b)) any = true;
        while (deliver(&D.ba, &D.ab, &D.a)) any = true;
    }
}

void duo_step(void) {
    D.now += 10;
    mp_tick(&D.a, 10);
    mp_tick(&D.b, 10);
    flow();
}

void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) duo_step();
}

morph_play_t *side(int s) { return s ? &D.b : &D.a; }

void press(int s, uint8_t key) {
    mp_key(side(s), key);
    flow();
}
