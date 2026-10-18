// src/morph_app.c —— 库巴变变变的入口表和流程:首页 → 一个人玩 / 两人联机(找队友 → 选人)→ 营地 ↔ 一关(地图 ↔ 战斗,
// morph_run.c;联机时两边各跑一份,morph_net.c 只传按键)。营地里「回首页」回首页。画面在 morph_gfx.c。
// 按键:首页 ▲▼ 选、● 确定;找队友页 ● 回首页;选人页 ▲ 库巴、▼ 爸爸、● 回首页;冒险里三个键原样交给 morph_run
// (联机时交给 morph_net,晚几步两边一起生效;▼ ● 的长按也交);联机断了在等队友时 ● 回首页。
// 存档:saves 分区 "morph"/"save"(开到第几关、打通过哪关、升级、零件、第一次变身 / 合体),一变就存。
// 调试通道(devlink BOS1/2/3/4 → admin 'a' 'b' 'c' 'd',只在前台时认):'a' 一个人进营地(已经在一个人的营地里:
// 四关全开 + 零件 +50,会存档)、'b' 自己能量加满(战斗里站着时顺便换下一个变身)、'c' 这场的怪全打到剩 1 血、'd' 直接走到 Boss 前一排(模拟器 / 真卡自测用)。
#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "kit_app.h"
#include "kit_art.h"
#include "kit_link.h"
#include "kit_sfx.h"
#include "kit_store.h"
#include "morph_art.h"
#include "morph_gfx.h"
#include "morph_net.h"
#include "morph_run.h"
#include "morph_texts.h"
#include "ui_kit.h"

#define STAT_MS 2000      // 状态行的帧时间、刷屏时间按这么长一段统计
#define MORPH_CH 0x7AC4u  // 联机频道(库巴大作战 0x7AC1、传炸弹 0x7AC2、格斗 0x7AC3)
#define SAVE_NS "morph"   // = coopa.toml 的 id
#define SAVE_KEY "save"
#define SAVE_VER 1

typedef enum { PG_TITLE = 0, PG_RUN, PG_LINK, PG_PICK } page_t;

typedef struct {
    uint8_t ver;
    uint8_t flags;           // 位 0 变过身、位 1 放过合体大招
    uint8_t pad[2];
    mr_profile_t prof;
} morph_save_t;

static morph_save_t s_save;
static morph_run_t s_run;      // 一个人玩
static morph_net_t s_net;      // 两人联机(里面有一份 run)
static bool s_link;            // 现在是联机
static page_t s_page;
static uint8_t s_sel;          // 首页光标:0 一个人玩、1 两人联机、2 回菜单
static bool s_up;
static uint32_t s_session;
static char s_note[160];       // 找队友 / 选人页上次画的内容(没变就不重画)
static uint32_t s_toast_ms;
static int8_t s_shown = -1;    // 现在建的是哪一页画面(morph_gfx_page_t;-1 没建)
// 状态行的统计
static uint32_t s_win_ms, s_win_n, s_win_sum, s_win_max;
static uint16_t s_dt_avg, s_dt_max;
static bool s_hooked;
static int64_t s_refr_at;
static bool s_refr_drew;
static uint32_t s_draw_n, s_draw_sum, s_draw_max, s_draw_max_us, s_draw_n_win, s_draw_sum_us;

// ---- 刷屏时间(照计划 1:frame 的 dt 不含 LVGL 重画,另量 REFR_START → REFR_READY) ----
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
    s_win_ms += dt; s_win_n++; s_win_sum += dt;
    if (dt > s_win_max) s_win_max = dt;
    if (s_win_ms < STAT_MS) return;
    s_dt_avg = (uint16_t)((s_win_sum + s_win_n / 2) / s_win_n);
    s_dt_max = (uint16_t)s_win_max;
    s_draw_max_us = s_draw_max; s_draw_n_win = s_draw_n; s_draw_sum_us = s_draw_sum;
    s_win_ms = s_win_n = s_win_sum = s_win_max = 0;
    s_draw_n = s_draw_sum = s_draw_max = 0;
}

// ---- 存档 ----
static bool save_valid(const void *blob) { return ((const morph_save_t *)blob)->ver == SAVE_VER; }
static void save_load(void) {
    static morph_save_t scratch;   // 不放在任务栈上
    if (!kit_sv_blob_load(SAVE_NS, SAVE_KEY, &s_save, sizeof(s_save), save_valid, &scratch)) {
        memset(&s_save, 0, sizeof(s_save));
        s_save.ver = SAVE_VER;
    }
    morph_mr_profile_clean(&s_save.prof);
}
static void save_store(void) { kit_sv_blob_store(SAVE_NS, SAVE_KEY, &s_save, sizeof(s_save)); }

static morph_run_t *run(void) { return s_link ? &s_net.run : &s_run; }
static int me(void) { return s_link && s_net.me > 0 ? 1 : 0; }

// 冒险里自己那份存档变了(升级、零件、开关)就存
static void save_sync(void) {
    const mr_profile_t *p = &run()->prof[me()];
    if (!memcmp(p, &s_save.prof, sizeof(*p))) return;
    s_save.prof = *p;
    save_store();
}

// ---- 页面 ----
static lv_obj_t *centered(lv_obj_t *scr, const lv_font_t *f, uint32_t color, const char *s, int y) {
    lv_obj_t *t = uk_text(scr, f, color, s);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, y);
    return t;
}
// 大头:两倍人物图再放大 scale 倍
static void figure(lv_obj_t *scr, uint8_t ch, uint8_t pose, bool left, int x, int y, int scale) {
    const lv_image_dsc_t *d = morph_art_pose2(ch, pose, left);
    lv_obj_t *o = lv_image_create(scr);
    lv_image_set_src(o, d);
    lv_obj_set_size(o, d->header.w, d->header.h);
    lv_image_set_scale(o, (uint32_t)(256 * scale));
    lv_image_set_antialias(o, false);
    lv_obj_set_pos(o, x + (d->header.w * scale - d->header.w) / 2, y + (d->header.h * scale - d->header.h) / 2);
}

static void show_title(void) {
    morph_gfx_destroy();
    s_page = PG_TITLE;
    s_shown = -1;
    s_note[0] = 0;
    lv_obj_t *scr = uk_screen_new(UK_WALL);
    centered(scr, UK_F36, UK_RED, TX_TITLE, 20);
    centered(scr, UK_F12, UK_INK, TX_SUB, 66);
    figure(scr, 0, MA_HEAVY, false, 44, 88, 2);
    figure(scr, 1, MA_GUARD, true, 132, 88, 2);
    const char *items[3] = { TX_SOLO, TX_LINK, TX_BACK };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *p = uk_pill(scr, UK_F24, s_sel == i ? UK_RED : UK_LINE, UK_WHITE, items[i]);
        lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 170 + i * 44);
    }
    uk_screen_swap(scr);
}

// 找队友 / 选人 / 出错:两个大头、一行大字、两行小字、底下提示(内容没变就不重画)
static void note(const char *big, uint32_t color, const char *l1, const char *l2, const char *hint) {
    char key[sizeof(s_note)];
    snprintf(key, sizeof(key), "%s|%s|%s|%s|%lx", big, l1, l2, hint, (unsigned long)color);
    if (!strcmp(key, s_note)) return;
    snprintf(s_note, sizeof(s_note), "%s", key);
    morph_gfx_destroy();
    s_shown = -1;
    lv_obj_t *scr = uk_screen_new(UK_WALL);
    figure(scr, 0, MA_IDLE, false, 40, 36, 2);
    figure(scr, 1, MA_IDLE, true, 136, 36, 2);
    centered(scr, UK_F24, color, big, 120);
    centered(scr, UK_F12, UK_INK, l1, 170);
    centered(scr, UK_F12, UK_INK, l2, 200);
    centered(scr, UK_F12, UK_INK, hint, 270);
    uk_screen_swap(scr);
}

// 冒险里:营地 / 地图 / 战斗换了就重建那一页
static void sync_screen(void) {
    int want = morph_gfx_page_for(run());
    if (want == s_shown) return;
    s_note[0] = 0;
    morph_gfx_destroy();
    uk_screen_swap(uk_screen_new(0x000000));   // 先换成空屏把旧页删掉,别让两页同时占着 LVGL 的内存
    lv_obj_t *scr = uk_screen_new(0x000000);
    morph_gfx_build(scr, run());
    uk_screen_swap(scr);
    s_shown = (int8_t)want;
}

static void start_solo(void) {
    kit_link_stop();
    s_link = false; s_up = false;
    mr_profile_t pf[2] = { s_save.prof, s_save.prof };
    morph_mr_start(&s_run, 1, esp_random() | 1, pf);
    s_page = PG_RUN;
    s_shown = -1;
    sync_screen();
    sfx_play(SFX_START);
}

static bool link_send(void *ctx, const void *m, size_t n) {
    (void)ctx;
    return kit_link_send(m, n);
}

static void to_title(void) {
    kit_link_stop();
    s_link = false; s_up = false;
    show_title();
}

static void to_link(void) {
    morph_mn_init(&s_net, link_send, NULL, esp_random() | 1, &s_save.prof);
    s_link = true; s_up = false;
    s_page = PG_LINK;
    s_note[0] = 0;
    kit_link_start(MORPH_CH);   // 内存不够时状态是 KIT_LINK_NO_MEMORY,找队友页照着说
}

// 联机:收发消息、断线、推进度
static void link_frame(uint32_t dt) {
    kit_link_state_t st = kit_link_state();
    if (st == KIT_LINK_CONNECTED) {
        if (!s_up || kit_link_session() != s_session) {
            s_up = true;
            s_session = kit_link_session();
            morph_mn_connected(&s_net, kit_link_initiator());
        }
        uint8_t msg[KIT_LINK_MSG_MAX];
        size_t n;
        while ((n = kit_link_recv(msg, sizeof(msg))) > 0) morph_mn_on_msg(&s_net, msg, n);
    } else if (s_up) {
        s_up = false;
        morph_mn_lost(&s_net);
    }
    morph_mn_tick(&s_net, dt);
}

static void link_pages(void) {
    kit_link_state_t st = kit_link_state();
    char who[32];
    switch (s_net.phase) {
        case MN_IDLE:
            s_page = PG_LINK;
            if (st == KIT_LINK_NO_MEMORY) note(TX_NO_MEM, UK_RED, TX_NO_MEM2, "", TX_HOME_HINT);
            else note(TX_SEARCH, UK_INK, TX_SEARCH2, TX_SEARCH3, TX_HOME_HINT);
            break;
        case MN_PICK:
            s_page = PG_PICK;
            if (s_net.me < 0) note(TX_PICK, UK_RED, TX_PICK_KID, TX_PICK_DAD, TX_HOME_HINT);
            else {
                snprintf(who, sizeof(who), TX_PICK_ME, morph_gfx_hero_name(s_net.me));
                note(who, s_net.me ? UK_BLUE : UK_RED, TX_PICK_WAIT, "", TX_HOME_HINT);
            }
            break;
        case MN_BAD:
            s_page = PG_LINK;
            note(TX_BAD, UK_RED, TX_BAD2, "", TX_HOME_HINT);
            break;
        default:   // 在打(或者断线等队友)
            if (s_page != PG_RUN) { s_page = PG_RUN; s_shown = -1; sfx_play(SFX_START); }
            break;
    }
}

static void morph_enter(const kit_links_t *links) {
    (void)links;
    hook(true);
    save_load();
    s_sel = 0;
    show_title();
}

static void morph_leave(void) {
    morph_gfx_destroy();
    kit_link_stop();
    s_link = false; s_up = false;
    hook(false);
    s_page = PG_TITLE;
    s_shown = -1;
}

static bool morph_input(const kit_input_t *in) {
    if (s_page == PG_RUN) {
        if (s_link && s_net.phase == MN_PAUSED) {   // 断线等队友:● 回首页
            if (in->key == KIT_KEY_OK && !in->long_press) to_title();
            return false;
        }
        if (s_link) morph_mn_key(&s_net, (uint8_t)in->key, in->long_press);
        else morph_mr_input(&s_run, 0, (uint8_t)in->key, in->long_press);
        return false;
    }
    if (in->long_press) return false;
    if (s_page == PG_LINK) {
        if (in->key == KIT_KEY_OK) to_title();
        return false;
    }
    if (s_page == PG_PICK) {
        if (in->key == KIT_KEY_OK) to_title();
        else morph_mn_pick(&s_net, in->key == KIT_KEY_UP ? 0 : 1);
        return false;
    }
    if (in->key == KIT_KEY_OK) {
        if (s_sel == 2) return true;   // 回菜单
        if (s_sel == 0) start_solo();
        else to_link();
        return false;
    }
    s_sel = (uint8_t)((s_sel + (in->key == KIT_KEY_UP ? 2 : 1)) % 3);
    show_title();
    return false;
}

static void morph_frame(uint32_t dt) {
    stats(dt);
    if (s_page == PG_TITLE) return;
    if (s_link) {
        link_frame(dt);
        link_pages();
        if (s_page != PG_RUN) return;
        if (s_net.phase == MN_PAUSED) {   // 断线:画面停着,隔一会儿冒一次「等队友回来」
            s_toast_ms += dt;
            if (s_toast_ms >= 900) { s_toast_ms = 0; uk_toast(TX_WAIT_PEER, UK_INK, 1000); }
        }
    } else {
        morph_mr_tick(&s_run, dt);
    }
    morph_run_t *r = run();
    save_sync();
    if (r->quit[me()]) { to_title(); return; }
    sync_screen();
    if (r->phase == RUN_BATTLE) {
        mbt_ev_t e;
        uint8_t flags = s_save.flags;
        while (morph_mbt_event(&r->bt, &e)) {
            if (e.type == BE_SAY && e.a == SAY_MORPH && e.who == me()) flags |= 1;
            if (e.type == BE_CINE && e.a == CINE_COMBO) flags |= 2;
            morph_gfx_battle_event(r, &e);
        }
        if (flags != s_save.flags) { s_save.flags = flags; save_store(); }
    }
    morph_gfx_update(r, dt);
}

static unsigned morph_busy(void) { return s_page == PG_RUN && run()->phase != RUN_HUB ? KIT_BUSY_TIMING : 0; }

static bool morph_won(void) { return (s_save.prof.cleared >> (MR_CHAPTERS - 1)) & 1; }

// 里程碑(位 = coopa.toml 里的顺序):第一次变身、第一次合体技、打倒四个 Boss
static uint8_t morph_milestones(void) {
    return (uint8_t)((s_save.flags & 3) | (s_save.prof.cleared & 0x0F) << 2);
}

static void morph_save_now(void) { save_store(); }

static kit_admin_t morph_admin(int cmd, bool active) {
    if (!active) return KIT_ADMIN_NOT_MINE;
    switch (cmd) {
        // 下面几条只给一个人玩用(联机时两边各算各的,改一边会对不上)
        case 'a':
            if (s_page == PG_RUN && !s_link && s_run.phase == RUN_HUB) {
                s_run.prof[0].unlocked = MR_CHAPTERS;
                s_run.prof[0].parts = (uint16_t)(s_run.prof[0].parts + 50);
            } else {
                start_solo();
            }
            break;
        case 'b':
            if (s_page == PG_RUN && !s_link) {
                s_run.carry[0].en = MBT_EN_MAX;
                mbt_hero_t *h = &s_run.bt.h[0];
                h->en = MBT_EN_MAX;
                if (s_run.phase == RUN_BATTLE && !s_run.bt.cine_on && (h->st == HS_IDLE || h->st == HS_WALK)) {   // 换下一个变身(看变身那一下)
                    h->form = (uint8_t)((h->form + 1) % FORM_COUNT);
                    h->st = HS_MORPH; h->t = 0; h->inv = 5000;   // 自测时别被打掉
                }
            }
            break;
        case 'c':
            if (s_page == PG_RUN && !s_link && s_run.phase == RUN_BATTLE)
                for (int i = 0; i < MBT_MAXF; i++) if (s_run.bt.f[i].used && s_run.bt.f[i].hp > 0) s_run.bt.f[i].hp = 1;
            break;
        case 'd':
            if (s_page == PG_RUN && !s_link && s_run.phase == RUN_MAP && !s_run.walking) { s_run.r = MR_ROWS - 2; s_run.i = 0; }
            break;
        default: return KIT_ADMIN_NOT_MINE;
    }
    return KIT_ADMIN_DONE;
}

// 调试通道状态行(全是整数,空格分开;外壳的缓冲 120 字节,别再加长):
// MO <页> <冒险阶段> <几个人> <第几排> <在走> <血> <能量> <变身> <人状态> <人 x> <活着的怪> <boss 血> <结果> <特写>
//    <对象数> <帧 dt 平均> <帧 dt 最大> <刷屏最长微秒> <刷屏次数> <联机阶段> <我是谁> <来回> <延迟步> <对不上> <步数/100>
//    <第几关> <零件>
// 页 = page_t,冒险阶段 = mr_phase_t,人状态 = mbt_hero_st_t(联机时是自己那个人);dt、刷屏是上一段 2 秒的;
// 联机阶段 = mn_phase_t(一个人玩时 0),我是谁 0 库巴 / 1 爸爸;第几关从 0 数。
static size_t morph_status(char *buf, size_t len) {
    const morph_run_t *r = run();
    const morph_battle_t *b = &r->bt;
    int m = me();
    bool fight = s_page == PG_RUN && r->phase == RUN_BATTLE;
    int boss_hp = 0;
    for (int i = 0; fight && i < MBT_MAXF; i++)
        if (b->f[i].used && (morph_foes[b->f[i].type].boss || morph_foes[b->f[i].type].elite)) boss_hp = b->f[i].hp;
    int n = snprintf(buf, len, "MO %d %u %u %u %u %d %d %u %u %d %d %d %d %d %u %u %u %lu %lu %u %d %u %u %u %lu %u %u\n",
                     (int)s_page, (unsigned)r->phase, (unsigned)r->nh, (unsigned)r->r, (unsigned)r->walking,
                     fight ? b->h[m].hp : r->carry[m].hp, fight ? b->h[m].en : r->carry[m].en,
                     (unsigned)(fight ? b->h[m].form : r->carry[m].form), (unsigned)(fight ? b->h[m].st : 0),
                     fight ? (int)b->h[m].x : 0, fight ? morph_mbt_alive(b) : 0, boss_hp, fight ? b->result : 0,
                     fight && b->cine_on ? 1 + b->cine.kind : 0, (unsigned)morph_gfx_obj_count(), (unsigned)s_dt_avg,
                     (unsigned)s_dt_max, (unsigned long)s_draw_max_us, (unsigned long)s_draw_n_win,
                     (unsigned)(s_link ? s_net.phase : 0), s_link ? s_net.me : -1, (unsigned)s_net.rtt,
                     (unsigned)s_net.delay, (unsigned)s_net.desync, (unsigned long)(s_net.step / 100),
                     (unsigned)r->chapter, (unsigned)r->prof[m].parts);
    return n < 0 ? 0 : ((size_t)n < len ? (size_t)n : len - 1);
}

static const kit_menu_card_t MORPH_CARD = {
    .title = TX_TITLE,
    .sub = TX_SUB,
    .art = { SPR_KID, SPR_DAD },
};

const kit_app_t morph_app = {
    .id = "morph",
    .sprite_budget = 0,   // 图都是游戏块里的常量图(morph_art.h),不用精灵会话
    .enter = morph_enter,
    .input = morph_input,
    .frame = morph_frame,
    .leave = morph_leave,
    .busy = morph_busy,
    .won = morph_won,
    .milestones = morph_milestones,
    .status = morph_status,
    .admin = morph_admin,
    .save_now = morph_save_now,
    .card = &MORPH_CARD,
};
