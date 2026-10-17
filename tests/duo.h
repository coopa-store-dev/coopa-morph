// tests/duo.h —— 两个 morph_play_t 用假管道接起来(a 是主动连的一方)。管道照 kit_link:一次连接之内可靠、按序;
// 停等(一片到了、确认走回来,下一片才出发);每个方向可设单程延迟和抖动;「卡一下」= 这一刻起 400 ms 不送;
// 排着(含路上的)满 cap 条 send 返回 false;断线时排着的都没了。
// 时间由 run() 推:每 10 ms 两边各 tick 一次,然后把到了的消息送到。(照 coopa-fight/tests/duo)
#pragma once

#include "morph_play.h"

#define PIPE_N 64
#define DUO_STALL_MS 400
typedef struct {
    uint8_t q[PIPE_N][MP_MSG_MAX];
    size_t len[PIPE_N];
    int head, n, cap, max_n;
    uint32_t refused;        // send 因为排满返回 false 几次
    bool up;
    uint32_t delay, jitter;  // 单程(毫秒),每片 delay ± jitter
    bool flying;             // 队头那片已经出发
    uint32_t ready;          // 下一片最早什么时候出发(上一片的确认回来)
    uint32_t arrive;         // 队头那片什么时候到
    uint32_t stall_until;
    uint32_t msgs[128];      // 送到的每种消息几条
    uint8_t last_b[MP_MSG_MAX];  // 最近送到的一条 'B'(原样)
} pipe_t;

typedef struct {
    morph_play_t a, b;
    pipe_t ab, ba;
    uint32_t now;
} duo_t;

extern duo_t D;

uint32_t test_rnd(void);  // 测试自己用的伪随机(按键、抖动)
void test_seed(uint32_t s);
bool duo_send(void *ctx, const void *m, size_t len);
void duo_init(void);
void duo_connect(void);
void duo_drop(void);
void duo_delay(uint32_t ms, uint32_t jitter);  // 两个方向都设
void duo_stall(pipe_t *p);                     // 这个方向这一刻起 DUO_STALL_MS 不送
void flow(void);                               // 把到了时间的消息送到
void duo_step(void);                           // 10 ms:两边各 tick 一次,再 flow
void run(uint32_t ms);
morph_play_t *side(int s);                     // 0 = a(主动方),1 = b
void press(int s, uint8_t key);                // 这一边按一下(马上把能发的发出去)
