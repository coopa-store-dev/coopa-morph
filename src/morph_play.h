// morph_play.h —— 一局:关卡时钟、自己的人、对方的人(照对方发来的状态重建),和联机消息。纯 C,
// 发送走回调(卡上是 kit_link_send);send == NULL 是一个人试走(不联机,计划 1 的实验用)。
//
// 各算各的人:自己的人每 10 ms(关卡时钟)走一步;按键、落地、站住、掉坑、接着走的时候,把它的完整状态
// 连同「这是关卡时钟哪一刻的」发给对方,没事每 MP_SYNC_MS 补一条。对方拿这 6 样(x y vy dir st flaps)
// 用同一套 morph_mb_step 从那一刻算到自己的 clock,就是一模一样的人。
//
// 关卡时钟:主动方说了算。被连方收到 'G' 先照抄(晚了一个单程),马上 ping,第一次回来直接对上,
// 以后每次最多挪 MP_NUDGE_MS,差超过 MP_JUMP_MS 直接跳;来回比平时长一半以上(路上卡了一下)的那次不对。
//
// 消息(第一字节类型,多字节小端,每条 ≤ MP_MSG_MAX):
//   'H' 协议版本                     连上后双方各发一次;版本不一样两边都停在 MP_WAIT
//   'G' 关卡时钟(4)                  主动方:开始 / 连回来接着打(被连方把 clock 设成它)
//   'Q' 编号 来回(2)                 被连方 ping(每 MP_PING_MS 一次,链路估计空了再静 MP_PING_QUIET 才发;
//                                    'G' 以后的第一次估计空了就发);
//                                    带上被连方量到的平滑来回(0 = 还没量),主动方用它估计链路忙不忙
//   'A' 编号 关卡时钟(4) 等了多久(2)  主动方回:交出去那一刻的时钟、'Q' 到了以后等了多久(毫秒)。主动方等自己的链路
//                                    估计空了才交(交出去马上就走,不在链路里排队);被连方从量到的来回里扣掉
//                                    「等了多久」,用「时钟 + 剩下的一半」估计主动方现在的时钟
//   'B' 序号 时钟/10ms(3) x(3) y(2) vy(2) 打包(2:位 0 dir(1 = 朝右)、位 1-2 st、位 3-5 flaps)
//                                    自己的人的完整状态,正好 MP_B_LEN 字节
//
// 发法(照格斗 fight_game.h):'H' 'G' 不能丢,send 被拒就留着下一帧再交;'B' 只留「该发了」这件事,
// 链路估计空下来(量到的来回 × 9/8)才交给 send,交的时候现打包最新的状态,所以链路里不会堆旧状态。
// 收法:'B' 序号不比收到过的新(按 uint8 回绕比较)→ 丢掉;否则 peer = 这条状态,从它的时钟算到自己的 clock;
// 它的时钟比自己的 clock 还新 → 先原样摆着,等 clock 追上再往前算。
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "morph_body.h"

#define MP_PROTO 1
#define MP_MSG_MAX 16
#define MP_PING_MS 2000
#define MP_PING_QUIET 300    // 被连方:链路估计空了再静这么久才 ping(量到的来回不带排队)
#define MP_SYNC_MS 2000      // 没事多久补一条 'B'
#define MP_NUDGE_MS 5        // 被连方对时钟每次最多挪多少
#define MP_JUMP_MS 200       // 差超过这么多直接跳
#define MP_RTT_GUESS 300     // 还没量到来回时当它多长(宁长勿短:估短了链路会排满)
#define MP_BUSY_MAX 600      // send 被拒(拥堵)后一片的估计最多加到这么久
#define MP_BUSY_RUN 64       // 连着交出去这么多条才把拥堵的估计往量到的来回收 1 ms
#define MP_LINK_Q 8          // kit_link 最多排几片
#define MP_B_LEN 14

typedef enum { MP_WAIT = 0, MP_PLAY, MP_PAUSED } morph_phase_t;  // PAUSED:断线了,等队友回来
typedef bool (*mp_send_fn)(void *ctx, const void *msg, size_t len);  // false = 现在发不出去

typedef struct {
    mp_send_fn send; void *ctx;
    const morph_level_t *L;
    bool initiator, up, solo;
    uint8_t phase;              // morph_phase_t
    uint32_t clock;             // 关卡时钟(毫秒):主动方说了算,被连方靠 ping 对齐;PAUSED 时不走
    morph_body_t me, peer;      // peer:对方最近一条状态从那一刻算到 clock
    bool peer_ok;               // 收到过对方的状态
    uint8_t tx_seq, rx_seq;
    uint16_t rtt;               // 平滑来回(毫秒),0 = 还没量
    uint32_t tx, rx, refused;   // 统计:交出去几条、收到几条、send 被拒几次(测试和状态行用)

    uint8_t proto;              // 自己说的协议版本(morph_mp_init 设成 MP_PROTO;测试改它模拟版本不一样的卡)
    bool hello;                 // 这次连接对方的 'H' 版本对上了
    uint32_t now;               // 毫秒,一直走(链路计时用;clock 会停、会挪)
    uint32_t me_at, peer_at;    // me / peer 是关卡时钟哪一刻的状态(10 ms 的整数倍)

    uint8_t keep[2][MP_MSG_MAX], keep_len[2], keep_n;  // 'H' 'G':按顺序交,被拒就留着
    bool state_due;             // 自己的人有事,'B' 等链路空了就发
    uint32_t state_at;          // 上一条 'B' 交出去的时刻(now)
    uint32_t link_free;         // 估计链路什么时候空下来(now)
    uint32_t hand_at;           // 最近一次交给 send 的时刻(now)
    uint16_t busy;              // 估计链路一片要多久;0 = 还没定
    uint8_t busy_run;

    uint8_t ping_id; bool ping_wait; uint32_t ping_at, ping_next;  // 被连方
    bool clock_ok;              // 被连方:这次连接 ping 对过时钟了('G' 以后第一次直接对上)
    bool ans; uint8_t ans_id; uint32_t ans_at;                     // 主动方:要回的 'A'、收到 'Q' 的时刻(now)
} morph_play_t;

void morph_mp_init(morph_play_t *p, mp_send_fn send, void *ctx, const morph_level_t *L);
void morph_mp_connected(morph_play_t *p, bool initiator);   // 新的一次连接
void morph_mp_lost(morph_play_t *p);                        // 断了 → MP_PAUSED
void morph_mp_on_msg(morph_play_t *p, const uint8_t *m, size_t n);
void morph_mp_tick(morph_play_t *p, uint32_t dt_ms);        // 推时钟、走自己的人、重建对方、交消息
void morph_mp_key(morph_play_t *p, uint8_t key);            // morph_key_t,自己的
