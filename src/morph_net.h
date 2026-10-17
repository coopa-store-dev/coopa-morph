// src/morph_net.h —— 两张卡一起玩:两边各跑一份一模一样的 morph_run(同一个种子、同一份程序),只互相传按键。
// 做法是「延迟锁步」:自己按的键排在 MN_DELAY 步(120 ms)以后生效,连同「这一步以前我不会再有键了」(进度线)
// 一起发给对方;两边都只往前算到两条进度线里小的那条。同一步里先放库巴的键、再放爸爸的键。
// 所以两边算出来的每一步都一样(画面、伤害数字、声音都在自己卡上照算),链路上只有很少的按键消息。
// 纯 C:发送走回调(卡上是 kit_link_send),宿主测试把两份接在一起。
//
// 选人:连上以后两边都看到「▲ 我当库巴 ▼ 我当爸爸」,谁先按算谁,另一边自动当另一个;两边同时按了同一个,主动方算。
// 两边都定了,主动方发种子开始。
// 断线:两边停住(画面上「等队友回来」);连回来以后各自说「你的键我收到第几步了」,对方把那以后的键从记录里补发,
// 接着算,不用重来。对方卡重启过(没在打)→ 这边也回首页。
//
// 消息(第一字节是类型,多字节小端,每条 ≤ MN_MSG_MAX):
//   'H' 版本                         连上后各发一次
//   'R' 角色(0 库巴 / 1 爸爸)         选人
//   'S' 种子(4) 主动方的角色           主动方:开一趟
//   'I' 进度线(3) 第一个键的序号(1) 这条的编号(1) 收到你的最新编号(1) 那条在我这儿压了多久(1,4 ms 一格) 然后 0..4 个键:离进度线往回几步(1) 键(1:
//       位 0-1 键、位 2 长按)。键的序号:自己的第几个键,模 256,收到重复的按序号丢掉。编号 / 收到编号用来量来回。
// 链路是停等的(一条确认回来下一条才走),交得太快会全排在链路里越积越旧:按量到的来回控制,链路估计空了才交一条,
// 交的时候现打包最新的进度线。自己的键晚几步生效也按来回定(来回 × 2 + 20 ms,8–30 步);快追上对方进度线时放慢到 2/3。
//   'Q' 我收到你的进度线(3) 在打吗(1)   连上 / 连回来时各发一次(对方据此补发)
//   'C' 步数(3) 校验(2)                每 2 秒一次:两边对一下算出来的是不是一样(测试和状态行用)
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "morph_run.h"

#define MN_PROTO 1
#define MN_MSG_MAX 16
#define MN_DELAY 12          // 开头还没量到来回时,自己的键晚几步生效(10 ms 一步)
#define MN_HIST 128          // 自己的键记多少个(断线补发)
#define MN_PEND 64           // 还没轮到的键(两个人合起来)
#define MN_CHECK_STEPS 200   // 多少步对一次校验

typedef bool (*mn_send_fn)(void *ctx, const void *msg, size_t len);
typedef enum { MN_IDLE = 0, MN_PICK, MN_PLAY, MN_PAUSED, MN_BAD } mn_phase_t;

typedef struct { uint32_t step; uint8_t p, key; bool lp; } mn_key_t;

typedef struct {
    mn_send_fn send; void *ctx;
    bool initiator, up, peer_hello;
    uint8_t phase;              // mn_phase_t
    int8_t me, peer_role;       // 自己 / 对方选的角色,-1 = 还没选
    morph_run_t run;
    uint32_t step;              // 已经算到第几步
    int32_t acc;                // 还没算的毫秒
    uint32_t front;             // 自己的进度线:< front 的步不会再有自己的键
    uint32_t peer_front;        // 对方的进度线
    uint32_t sent_front;        // 发出去的最新进度线
    mn_key_t hist[MN_HIST]; uint16_t hist_n;   // 自己的键(环形,按步数增加)
    mn_key_t pend[MN_PEND]; uint8_t pend_n;    // 还没轮到的键(两个人的)
    uint8_t unsent;             // hist 里最后几个还没发出去
    uint8_t rx_seq;             // 对方下一个键的序号
    uint32_t seed;              // 主动方开一趟用的种子(init 时给)
    bool hello_due, pick_due, start_due, query_due;
    uint32_t last_send_ms, now_ms;
    uint32_t chk_step[4]; uint16_t chk_crc[4]; uint8_t chk_n;   // 自己最近几次校验(步数、值)
    uint32_t peer_chk_step; uint16_t peer_chk_crc; bool peer_chk; bool chk_due;
    uint16_t desync;            // 对出不一样几次(应该一直 0)
    uint8_t delay;              // 现在自己的键晚几步生效
    uint8_t mnum, peer_mnum;    // 自己发的 'I' 编号、收到对方最新的编号
    uint32_t peer_mnum_at;      // 收到对方最新那条的时刻
    uint32_t sent_at[256];      // 每个编号交出去的时刻
    uint16_t rtt;               // 平滑来回(毫秒,0 = 还没量到)
    uint32_t free_at;           // 估计链路什么时候空
    uint32_t tx, rx, refused, stall_ms;
} morph_net_t;

void morph_mn_init(morph_net_t *n, mn_send_fn send, void *ctx, uint32_t seed);
void morph_mn_connected(morph_net_t *n, bool initiator);
void morph_mn_lost(morph_net_t *n);
void morph_mn_on_msg(morph_net_t *n, const uint8_t *m, size_t len);
void morph_mn_pick(morph_net_t *n, int role);                  // 选人页按了 ▲(0)/ ▼(1)
void morph_mn_key(morph_net_t *n, uint8_t key, bool long_press);  // 打的时候自己按的键
void morph_mn_tick(morph_net_t *n, uint32_t ms);
uint16_t morph_mn_crc(const morph_run_t *r);                   // 这一刻的校验(两边应该一样)
