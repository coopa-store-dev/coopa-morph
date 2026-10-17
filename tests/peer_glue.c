// tests/peer_glue.c —— Mac 冒充队友(外壳的 tools/link_peer.py morph)用的 C 接口:一个 morph_play_t 当主动方。
// 接口和 coopa-fight / coopa-bomb 的 tests/peer_glue.c 一样(peer_init / peer_connected / peer_lost / peer_msg /
// peer_tick / peer_press / peer_holding / peer_over / peer_take / peer_status),link_peer.py 照同一个样子加载。
// Mac 永远是主动连的一方(关卡时钟听 Mac 的),时间由 Python 推(peer_tick)。
// Mac 这边的人自己按真人节奏乱按 ▲ ▼(间隔 200–1500 ms,▲ 两成 ▼ 一成的比例),在 peer_tick 里按,
// 所以 peer_holding() 一直是 0(link_peer.py 不用自己按)。发出去的消息先进这里的队列,由 Python 取走。
// link_peer.py 只编 src/morph_play.c 和这个文件,其余的源文件在这里直接包含进来
// (tests/Makefile 编 test_peer_glue 时就不再单独列它们)。
#include <stdio.h>
#include <string.h>

#include "morph_play.h"
#include "morph_levels.h"

#include "../src/morph_body.c"
#include "../src/morph_level.c"
#include "../src/morph_levels.c"

// 放得下一帧里最多交出去的几条;Python 那边 link_proto 排满(8 条,和卡上 kit_link 一样)就不再取,
// 这里很快也满,send 返回 false:morph_play 照卡上 send 被拒的样子处理拥堵
#define PEER_Q 4
static morph_play_t s_p;
static uint8_t s_q[PEER_Q][MP_MSG_MAX];
static size_t s_len[PEER_Q];
static int s_head, s_cnt;
static uint32_t s_rng = 4242, s_next_key;
static bool s_hands = true;

static bool to_py(void *ctx, const void *m, size_t len) {
    (void)ctx;
    if (s_cnt == PEER_Q) return false;
    int i = (s_head + s_cnt) % PEER_Q;
    memcpy(s_q[i], m, len);
    s_len[i] = len;
    s_cnt++;
    return true;
}

static uint32_t glue_rnd(void) {
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng >> 8;
}

void peer_init(uint32_t seed) {
    s_rng = seed ? seed : 4242;
    s_hands = true;
    morph_mp_init(&s_p, to_py, NULL, &morph_level_test);
}

void peer_connected(void) {
    s_head = s_cnt = 0;
    s_next_key = s_p.now + 200 + glue_rnd() % 1301;
    morph_mp_connected(&s_p, true);
}

void peer_lost(void) { morph_mp_lost(&s_p); }
void peer_msg(const uint8_t *m, size_t len) { morph_mp_on_msg(&s_p, m, len); }

// 推时间;Mac 这边的人到点就按一下(只在 MP_PLAY 里按,和卡上一样)
void peer_tick(uint32_t dt) {
    morph_mp_tick(&s_p, dt);
    if (s_hands && s_p.phase == MP_PLAY && s_p.now >= s_next_key) {
        morph_mp_key(&s_p, glue_rnd() % 3 ? MK_JUMP : MK_TURN);
        s_next_key = s_p.now + 200 + glue_rnd() % 1301;
    }
}
int peer_press(void) { return 0; }  // 人自己按(见 peer_tick)
int peer_holding(void) { return 0; }
int peer_over(void) { return 0; }   // 试验关没有「打完」

size_t peer_take(uint8_t *buf) {
    morph_mp_tick(&s_p, 0);  // 不推时间,只把排着的(刚才被拒的)交出去
    if (!s_cnt) return 0;
    size_t len = s_len[s_head];
    memcpy(buf, s_q[s_head], len);
    s_head = (s_head + 1) % PEER_Q;
    s_cnt--;
    return len;
}

// 一行:clock rtt tx rx refused 对方的 x y、Mac 自己的人的 x y st(像素,取整;st = morph_body_st_t)。
// 对方 = 卡上的人(Mac 这边照它发来的状态重建的);和卡上状态行的 peer_x peer_y peer_st 对着看
int peer_status(char *buf, size_t len) {
    return snprintf(buf, len, "clock=%lu rtt=%u tx=%lu rx=%lu refused=%lu peer_x=%d peer_y=%d me_x=%d me_y=%d me_st=%u",
                    (unsigned long)s_p.clock, (unsigned)s_p.rtt, (unsigned long)s_p.tx, (unsigned long)s_p.rx,
                    (unsigned long)s_p.refused, (int)(s_p.peer.x >> 4), (int)(s_p.peer.y >> 4), (int)(s_p.me.x >> 4),
                    (int)(s_p.me.y >> 4), (unsigned)s_p.me.st);
}

// 只给测试用(link_peer.py 不用):停 / 开 Mac 这边的人乱按;取两个人的完整状态
void peer_hands(int on) { s_hands = on != 0; }
void peer_bodies(morph_body_t *me, morph_body_t *peer, uint32_t *clock, int *ok) {
    *me = s_p.me;
    *peer = s_p.peer;
    *clock = s_p.clock;
    *ok = s_p.peer_ok;
}
