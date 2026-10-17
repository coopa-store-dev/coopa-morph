// tests/test_peer_glue.c —— Mac 对端的 C 接口(peer_glue.c)当主动方,和 duo.c 的 b(被连方,当卡)联机走 30 秒。
// 照 link_peer.py morph 那样用它:peer_tick 的 dt 不齐(Python 那边 10 ms 左右睡一次)、peer_take 取到没有为止、
// 卡发来的逐条 peer_msg。两边的人都按真人节奏乱按(间隔 200–1500 ms)。
// 停手后(Ruling 1)先跑 1 秒,再一直跑到四个人都停在坑边或墙边(最多 15 秒),比 x y vy dir st flaps 全等;
// 停手以后、还在走的时候只查 |x 差| ≤ (|两边 clock 差| / 10 + 1) 像素(快掉出关卡底的那几步不查)。refused == 0。
#include <stdio.h>
#include <stdlib.h>

#include "duo.h"
#include "morph_levels.h"

#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

void peer_init(uint32_t seed);
void peer_connected(void);
void peer_msg(const uint8_t *m, size_t len);
void peer_tick(uint32_t dt);
int peer_press(void);
int peer_holding(void);
int peer_over(void);
size_t peer_take(uint8_t *buf);
int peer_status(char *buf, size_t len);
void peer_hands(int on);
void peer_bodies(morph_body_t *me, morph_body_t *peer, uint32_t *clock, int *ok);

static const uint32_t DT[] = { 7, 13, 10, 16, 9, 11 };  // Python 那边每圈过了多久(合计是 10 的倍数)
static int step_n;
static uint32_t card_next;

// 一圈:glue 走 dt,卡走 dt;卡的人到点按;消息两边互相送(都是即时的,延迟在 duo_pipe 的测试里已经测过)
static void lap(bool card_hands) {
    uint32_t dt = DT[step_n++ % 6];
    peer_tick(dt);
    if (peer_holding()) peer_press();
    D.now += dt;
    morph_mp_tick(&D.b, dt);
    if (card_hands && D.b.phase == MP_PLAY && D.now >= card_next) {
        morph_mp_key(&D.b, test_rnd() % 3 ? MK_JUMP : MK_TURN);
        card_next = D.now + 200 + test_rnd() % 1301;
    }
    for (int again = 1; again;) {
        again = 0;
        uint8_t m[MP_MSG_MAX];
        size_t n;
        while ((n = peer_take(m)) > 0) { morph_mp_on_msg(&D.b, m, n); again = 1; }
        while (D.ba.n) {
            int i = D.ba.head;
            D.ba.head = (D.ba.head + 1) % PIPE_N;
            D.ba.n--;
            peer_msg(D.ba.q[i], D.ba.len[i]);
            again = 1;
        }
    }
    CHECK(D.ba.refused == 0);
}

static bool stopped(const morph_body_t *b) { return b->st == MB_EDGE || b->st == MB_WALL; }
static bool same(const morph_body_t *a, const morph_body_t *b) {
    return a->x == b->x && a->y == b->y && a->vy == b->vy && a->dir == b->dir && a->st == b->st &&
           a->flaps == b->flaps;
}

static bool pit_soon(const morph_body_t *b, int steps) {
    morph_body_t c = *b;
    for (int i = 0; i < steps; i++)
        if (morph_mb_step(&c, &morph_level_test) & MB_EV_PIT) return true;
    return false;
}

// 走着的时候的宽松界;返回 1 = 超界
static int x_bad(void) {
    morph_body_t me, peer;
    uint32_t clk;
    int ok;
    peer_bodies(&me, &peer, &clk, &ok);
    if (!ok || !D.b.peer_ok) return 0;
    uint32_t cd = clk > D.b.clock ? clk - D.b.clock : D.b.clock - clk;
    int steps = (int)(cd / 10 + 1);
    int bad = 0;
    const morph_body_t *pairs[2][2] = { { &peer, &D.b.me }, { &D.b.peer, &me } };
    for (int i = 0; i < 2; i++) {
        const morph_body_t *p = pairs[i][0], *q = pairs[i][1];
        if (pit_soon(p, steps) || pit_soon(q, steps)) continue;
        int32_t d = p->x - q->x;
        if (d > steps * MB_SUB || d < -steps * MB_SUB) bad++;
    }
    return bad;
}

int main(void) {
    duo_init();
    test_seed(31);
    D.ba.up = true;
    peer_init(7);
    peer_connected();
    morph_mp_connected(&D.b, false);
    card_next = 300;

    // 握手:两边进 MP_PLAY
    int t;
    for (t = 0; t < 1000 && D.b.phase != MP_PLAY; t += 10) lap(true);
    CHECK(D.b.phase == MP_PLAY);

    int bad_x = 0;
    for (t = 0; t < 30000; t += 10) lap(true);  // 按着键的时候对方的键在路上,两边本来就差着:不查 x

    // 停手:两边都不按
    peer_hands(0);
    for (t = 0; t < 1000; t += 10) lap(false);
    morph_body_t me, peer;
    uint32_t clk;
    int ok;
    for (t = 0; t < 15000; t += 10) {
        peer_bodies(&me, &peer, &clk, &ok);
        if (stopped(&me) && stopped(&peer) && stopped(&D.b.me) && stopped(&D.b.peer)) break;
        lap(false);
        bad_x += x_bad();
    }
    CHECK(t < 15000);
    CHECK(bad_x == 0);
    for (t = 0; t < 1000; t += 10) lap(false);  // 让路上的消息都到
    peer_bodies(&me, &peer, &clk, &ok);
    CHECK(ok && D.b.peer_ok);
    CHECK(same(&peer, &D.b.me));
    CHECK(same(&D.b.peer, &me));

    char st[200];
    peer_status(st, sizeof(st));
    printf("  %s(卡这边 tx %lu rx %lu refused %lu)\n", st, (unsigned long)D.b.tx, (unsigned long)D.b.rx,
           (unsigned long)D.b.refused);
    unsigned long c, tx, rx, refused;
    unsigned rtt;
    unsigned mst;
    int px, py, mx, my;
    CHECK(sscanf(st, "clock=%lu rtt=%u tx=%lu rx=%lu refused=%lu peer_x=%d peer_y=%d me_x=%d me_y=%d me_st=%u",
                 &c, &rtt, &tx, &rx, &refused, &px, &py, &mx, &my, &mst) == 10);
    CHECK(refused == 0 && D.b.refused == 0);
    CHECK(tx > 20 && rx > 20);
    CHECK(px == (int)(D.b.me.x >> 4) && py == (int)(D.b.me.y >> 4));
    CHECK(mx == (int)(me.x >> 4) && my == (int)(me.y >> 4) && mst == me.st);
    CHECK(!peer_over() && !peer_holding() && peer_press() == 0);
    puts("test_peer_glue: ok");
    return 0;
}
