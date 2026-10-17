// src/morph_app.c —— 库巴变变变的入口表和流程(计划 1:试验关,照 coopa-fight 的 fight_app.c):
//   首页「两人联机 / 回菜单」→ 找队友(kit_link,频道 MORPH_CH)→ 试验关。主动方是爸爸、被连方是库巴
//   (计划 1 临时,选人在计划 2);断线时停在试验关上,盖一行「等队友回来」。
// 按键:首页 ▲▼ 选、● 确定;找队友页 ● 回首页;试验关 ▲ 跳 / 扑腾、▼ 掉头(morph_keys.h),● 和长按不算。
// 一局和联机在 morph_play.c;画面在 morph_view.c。计划 1 没有存档。
// 调试通道(devlink BOS1/2/3 → admin 'a' 'b' 'c',只在前台时认):'a' 一个人进试验关、'b' 摄像机步长 1 → 2 → 4 → 8 → 1、
// 'c' 云和草丛开 / 关。
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "kit_app.h"
#include "kit_art.h"
#include "kit_link.h"
#include "morph_art.h"
#include "morph_keys.h"
#include "morph_levels.h"
#include "morph_play.h"
#include "morph_texts.h"
#include "morph_view.h"
#include "ui_kit.h"

#define MORPH_CH 0x7AC4u  // 联机频道(库巴大作战 0x7AC1、传炸弹 0x7AC2、格斗 0x7AC3)
#define STAT_MS 2000      // 状态行的帧时间、刷屏时间按这么长一段统计
#define CH_KID 0
#define CH_DAD 1

typedef enum { PG_TITLE = 0, PG_LINK, PG_GAME } page_t;

static morph_play_t s_play;
static page_t s_page;
static uint8_t s_sel;          // 首页光标:0 两人联机、1 回菜单
static bool s_up;
static uint32_t s_session;
static bool s_view;            // 试验关的画面建着
static uint8_t s_view_me;      // 按哪个人建的
static char s_note[160];       // 找队友页上次画的内容(没变就不重画)
static uint8_t s_cam = 1;      // 摄像机步长(实验)
static bool s_decor = true;    // 云和草丛(实验)
// 状态行的统计:这一段(STAT_MS)正在攒的、上一段的结果
static uint32_t s_win_ms, s_win_n, s_win_sum, s_win_max;
static uint16_t s_dt_avg, s_dt_max;
static bool s_hooked;          // 刷屏回调挂着
static int64_t s_refr_at;
static bool s_refr_drew;
static uint32_t s_draw_n, s_draw_sum, s_draw_max;  // 微秒
static uint32_t s_draw_avg_us, s_draw_max_us;

static bool link_send(void *ctx, const void *m, size_t n) {
    (void)ctx;
    return kit_link_send(m, n);
}

static long px(int32_t sub) { return (long)(sub >= 0 ? sub / MB_SUB : -((-sub + MB_SUB - 1) / MB_SUB)); }

// ---- 刷屏时间 ----
// 外壳的 frame(dt) 是游戏任务两次醒来之间的时间;LVGL 在另一个任务(esp_lvgl_port)里重画、往屏上送,
// 只是和游戏任务抢同一把锁,所以 dt 只在「重画 + 游戏这一帧」超过 20 ms 时才变长,看不出重画本身多久。
// 这里另量:一次重画从 LV_EVENT_REFR_START 到 LV_EVENT_REFR_READY(只算真画了东西的那些;
// 最后一块往屏上送的 DMA 可能还没传完)。回调在 LVGL 任务里、拿着锁调,和 frame 不会同时跑。
static void on_refr(lv_event_t *e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_REFR_START) {
        s_refr_at = esp_timer_get_time();
        s_refr_drew = false;
    } else if (c == LV_EVENT_RENDER_START) {
        s_refr_drew = true;
    } else if (c == LV_EVENT_REFR_READY && s_refr_drew) {
        uint32_t us = (uint32_t)(esp_timer_get_time() - s_refr_at);
        s_draw_n++;
        s_draw_sum += us;
        if (us > s_draw_max) s_draw_max = us;
    }
}

static void hook(bool on) {
    lv_display_t *d = lv_display_get_default();
    if (on == s_hooked || !d) return;
    if (on) {
        lv_display_add_event_cb(d, on_refr, LV_EVENT_REFR_START, NULL);
        lv_display_add_event_cb(d, on_refr, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(d, on_refr, LV_EVENT_REFR_READY, NULL);
    } else {
        lv_display_remove_event_cb_with_user_data(d, on_refr, NULL);
    }
    s_hooked = on;
}

static void stats(uint32_t dt) {
    s_win_ms += dt;
    s_win_n++;
    s_win_sum += dt;
    if (dt > s_win_max) s_win_max = dt;
    if (s_win_ms < STAT_MS) return;
    s_dt_avg = (uint16_t)((s_win_sum + s_win_n / 2) / s_win_n);
    s_dt_max = (uint16_t)s_win_max;
    s_draw_avg_us = s_draw_n ? s_draw_sum / s_draw_n : 0;
    s_draw_max_us = s_draw_max;
    s_win_ms = s_win_n = s_win_sum = s_win_max = 0;
    s_draw_n = s_draw_sum = s_draw_max = 0;
}

// ---- 画面 ----

static void drop_view(void) {
    if (!s_view) return;
    morph_mv_destroy();
    s_view = false;
}

static lv_obj_t *centered(lv_obj_t *scr, const lv_font_t *f, uint32_t color, const char *s, int y) {
    lv_obj_t *t = uk_text(scr, f, color, s);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, y);
    return t;
}

// 常量图放大 scale 倍,(x, y) 是放大后的左上角(控件保持原图大小、以中心为轴放大)
static void figure(lv_obj_t *scr, uint8_t ch, bool left, int x, int y, int scale) {
    const lv_image_dsc_t *d = morph_art_pose(ch, MA_IDLE, left);
    lv_obj_t *o = lv_image_create(scr);
    lv_image_set_src(o, d);
    lv_obj_set_size(o, d->header.w, d->header.h);
    lv_image_set_scale(o, (uint32_t)(256 * scale));
    lv_image_set_antialias(o, false);
    lv_obj_set_pos(o, x + (d->header.w * scale - d->header.w) / 2, y + (d->header.h * scale - d->header.h) / 2);
}

static void show_title(void) {
    drop_view();
    s_page = PG_TITLE;
    s_note[0] = 0;
    lv_obj_t *scr = uk_screen_new(UK_WALL);
    centered(scr, UK_F36, UK_RED, TX_TITLE, 28);
    centered(scr, UK_F12, UK_INK, TX_SUB, 76);
    figure(scr, CH_KID, false, 44, 104, 4);
    figure(scr, CH_DAD, true, 132, 104, 4);
    const char *items[2] = { TX_LINK, TX_BACK };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *p = uk_pill(scr, UK_F24, s_sel == i ? UK_RED : UK_LINE, UK_WHITE, items[i]);
        lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 196 + i * 44);
    }
    uk_screen_swap(scr);
}

static void note(const char *big, uint32_t color, const char *l1, const char *l2) {
    char key[sizeof(s_note)];
    snprintf(key, sizeof(key), "%s|%s|%s|%lx", big, l1, l2, (unsigned long)color);
    if (!strcmp(key, s_note)) return;
    snprintf(s_note, sizeof(s_note), "%s", key);
    drop_view();
    lv_obj_t *scr = uk_screen_new(UK_WALL);
    figure(scr, CH_KID, false, 40, 36, 4);
    figure(scr, CH_DAD, true, 136, 36, 4);
    centered(scr, UK_F24, color, big, 120);
    centered(scr, UK_F12, UK_INK, l1, 170);
    centered(scr, UK_F12, UK_INK, l2, 200);
    centered(scr, UK_F12, UK_INK, TX_HOME_HINT, 270);
    uk_screen_swap(scr);
}

static void show_link(kit_link_state_t st) {
    if (st == KIT_LINK_NO_MEMORY) note(TX_NO_MEM, UK_RED, TX_NO_MEM2, "");
    else note(TX_SEARCH, UK_INK, TX_SEARCH2, TX_SEARCH3);
}

// 试验关:计划 1 主动方是爸爸、被连方和一个人试走是库巴
static void show_game(void) {
    uint8_t me = s_play.solo || !s_play.initiator ? CH_KID : CH_DAD;
    if (s_view && me == s_view_me) return;
    drop_view();
    s_note[0] = 0;
    lv_obj_t *scr = uk_screen_new(UK_WALL);
    morph_mv_create(scr, s_play.L, me, me == CH_KID ? CH_DAD : CH_KID);
    uk_screen_swap(scr);
    s_view = true;
    s_view_me = me;
}

// ---- 流程 ----

static void to_title(void) {
    kit_link_stop();
    s_up = false;
    show_title();
}

static void to_link(void) {
    morph_mp_init(&s_play, link_send, NULL, &morph_level_test);
    s_up = false;
    s_page = PG_LINK;
    s_note[0] = 0;
    kit_link_start(MORPH_CH);  // 内存不够时状态是 KIT_LINK_NO_MEMORY,找队友页照着说
}

static void to_solo(void) {
    kit_link_stop();
    s_up = false;
    morph_mp_init(&s_play, NULL, NULL, &morph_level_test);
    drop_view();  // 重建:摄像机直接对上出生点
    s_page = PG_GAME;
}

// 联机:收发消息、断线,推一局
static void link_frame(uint32_t dt) {
    kit_link_state_t st = kit_link_state();
    if (st == KIT_LINK_CONNECTED) {
        if (!s_up || kit_link_session() != s_session) {
            s_up = true;
            s_session = kit_link_session();
            morph_mp_connected(&s_play, kit_link_initiator());
        }
        uint8_t msg[KIT_LINK_MSG_MAX];
        size_t n;
        while ((n = kit_link_recv(msg, sizeof(msg))) > 0) morph_mp_on_msg(&s_play, msg, n);
    } else if (s_up) {
        s_up = false;
        morph_mp_lost(&s_play);
    }
    morph_mp_tick(&s_play, dt);
}

static void morph_enter(const kit_links_t *links) {
    (void)links;
    hook(true);
    s_sel = 0;
    show_title();
}

static void morph_leave(void) {
    drop_view();
    hook(false);
    s_page = PG_TITLE;
}

static bool morph_input(const kit_input_t *in) {
    if (s_page == PG_GAME) {
        int k = morph_key_of((uint8_t)in->key, in->long_press);
        if (k >= 0) morph_mp_key(&s_play, (uint8_t)k);
        return false;
    }
    if (in->long_press) return false;
    if (s_page == PG_TITLE) {
        if (in->key == KIT_KEY_OK) {
            if (s_sel == 1) return true;  // 回菜单
            to_link();
            return false;
        }
        s_sel ^= 1;  // 两项:▲▼ 都是换一项
        show_title();
        return false;
    }
    if (in->key == KIT_KEY_OK) to_title();  // 找队友页:● 回首页
    return false;
}

static void morph_frame(uint32_t dt) {
    stats(dt);
    if (s_page == PG_TITLE) return;
    if (s_play.solo) morph_mp_tick(&s_play, dt);
    else link_frame(dt);
    if (s_play.phase == MP_WAIT) {  // 还在找、还在对暗号
        s_page = PG_LINK;
        show_link(kit_link_state());
        return;
    }
    s_page = PG_GAME;
    show_game();
    morph_mv_update(&s_play, dt);
}

// 在关卡里走着不熄屏;断线等队友时照常熄(连着的时候外壳本来就不熄)
static unsigned morph_busy(void) { return s_page == PG_GAME && s_play.phase == MP_PLAY ? KIT_BUSY_TIMING : 0; }

static kit_admin_t morph_admin(int cmd, bool active) {
    if (!active) return KIT_ADMIN_NOT_MINE;
    switch (cmd) {
        case 'a': to_solo(); break;
        case 'b':
            s_cam = s_cam >= 8 ? 1 : (uint8_t)(s_cam * 2);
            morph_mv_set_cam_step(s_cam);
            break;
        case 'c':
            s_decor = !s_decor;
            morph_mv_set_decor(s_decor);
            break;
        default: return KIT_ADMIN_NOT_MINE;
    }
    return KIT_ADMIN_DONE;
}

// 调试通道状态行(格式冻结,模拟器脚本和真卡脚本在读;全是整数,空格分开):
// MO <页> <阶段> <一个人> <主动方> <x> <y> <状态> <朝向> <对方 x> <对方 y> <对方状态> <关卡时钟> <来回>
//    <帧 dt 平均> <帧 dt 最大> <对象数> <摄像机步长> <交出去> <收到> <被拒> <刷屏平均微秒> <刷屏最长微秒>
// 页 = page_t,阶段 = morph_phase_t,状态 = morph_body_st_t,朝向 1 / -1;坐标是像素(脚底中点,向下取整)。
// dt、刷屏都是上一段 2 秒的;最后两项是 Task 4 核对后加的:frame(dt) 不含 LVGL 重画(见 on_refr 上面)。
static size_t morph_status(char *buf, size_t len) {
    const morph_play_t *p = &s_play;
    int n = snprintf(buf, len, "MO %d %u %u %u %ld %ld %u %d %ld %ld %u %lu %u %u %u %u %u %lu %lu %lu %lu %lu\n",
                     (int)s_page, (unsigned)p->phase, (unsigned)p->solo, (unsigned)p->initiator, px(p->me.x),
                     px(p->me.y), (unsigned)p->me.st, (int)p->me.dir, px(p->peer.x), px(p->peer.y),
                     (unsigned)p->peer.st, (unsigned long)p->clock, (unsigned)p->rtt, (unsigned)s_dt_avg,
                     (unsigned)s_dt_max, (unsigned)morph_mv_obj_count(), (unsigned)s_cam, (unsigned long)p->tx,
                     (unsigned long)p->rx, (unsigned long)p->refused, (unsigned long)s_draw_avg_us,
                     (unsigned long)s_draw_max_us);
    return n < 0 ? 0 : ((size_t)n < len ? (size_t)n : len - 1);
}

static const kit_menu_card_t MORPH_CARD = {
    .title = TX_TITLE,
    .sub = TX_SUB,
    .art = { SPR_KID, SPR_DAD },
};

const kit_app_t morph_app = {
    .id = "morph",
    .sprite_budget = 0,  // 图都是游戏块里的常量图(morph_art.h),不用精灵会话
    .enter = morph_enter,
    .input = morph_input,
    .frame = morph_frame,
    .leave = morph_leave,
    .busy = morph_busy,
    .status = morph_status,
    .admin = morph_admin,
    .card = &MORPH_CARD,
};
