// src/morph_view.c —— 见 morph_view.h。
#include "morph_view.h"

#include <string.h>

#include "morph_art.h"
#include "morph_texts.h"
#include "ui_kit.h"

#define SKY 0x9ED8F0
#define POOL 40        // 格子段 + 装饰最多几个对象(一屏 8 行 × 9 列,实际十几个)
#define DECOR_L 2      // 往屏幕左边多看几列找装饰(云比一格宽,锚点在左边)
#define CLOUD_EVERY 7  // 每 7 列一朵云(第 2 列起),高低交替
#define BUSH_EVERY 5   // 每 5 列一丛草(第 3 列起),长在地面顶上
#define NONE 0xFF
#define FIG 16         // 人物原图 16×16,放大两倍
#define STEP_PX 8      // 走 8 像素换一下脚

typedef struct {
    int16_t x, y, w, h;  // 世界坐标(像素)
    uint8_t img;         // morph_tile_img_t
} seg_t;

enum { PEER = 0, ME = 1 };  // 自己画在上面

static const morph_level_t *s_L;
static lv_obj_t *s_parent, *s_root, *s_state, *s_world, *s_wait, *s_fig[2];
static lv_obj_t *s_obj[POOL];
static seg_t s_seg[POOL];
static bool s_used[POOL];
static uint8_t s_ch[2];
static int32_t s_cam;             // 摄像机左边(1/16 像素),平滑地跟
static int s_cam_px;              // 画面上的摄像机左边(像素):和 s_cam 差够 s_step 才挪
static int s_c0 = -1, s_c1 = -1;  // 现在摆着的列;-1 = 下一帧重摆
static bool s_fresh;              // 刚建好:第一帧摄像机直接对上
static uint16_t s_objs;
static uint8_t s_step = 1;
static bool s_decor = true;

static int fdiv(int32_t a, int32_t b) { return (int)(a >= 0 ? a / b : -((-a + b - 1) / b)); }

// 没变就不调 LVGL(挪一次就要重画那一块)
static void pos(lv_obj_t *o, int x, int y) {
    if (lv_obj_get_style_x(o, 0) != x || lv_obj_get_style_y(o, 0) != y) lv_obj_set_pos(o, x, y);
}

static uint16_t count(lv_obj_t *o) {
    uint16_t n = 1;
    uint32_t k = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < k; i++) n = (uint16_t)(n + count(lv_obj_get_child(o, (int32_t)i)));
    return n;
}

// (col, row) 这一格画哪张图;NONE = 不画。实心格:底下一路实心到底的是地面(最上面那格是地面顶),否则是悬空的砖
static uint8_t cell_img(int col, int row) {
    if (col < 0 || col >= s_L->cols) return NONE;  // 关卡外(摄像机不出关卡,不画)
    morph_tile_t t = morph_ml_tile(s_L, col, row);
    if (t == MT_EMPTY) return NONE;
    if (t == MT_PLAT) return MT_IMG_PLAT;
    for (int r = row + 1; r < ML_ROWS; r++)
        if (morph_ml_tile(s_L, col, r) != MT_SOLID) return MT_IMG_BRICK;
    return morph_ml_tile(s_L, col, row - 1) == MT_SOLID ? MT_IMG_FILL : MT_IMG_TOP;
}

static int add(seg_t *w, int n, int x, int y, int wd, int ht, uint8_t img) {
    if (n >= POOL) return n;  // 放不下就不画(状态行的对象数会顶在上限,看得出来)
    w[n] = (seg_t){ (int16_t)x, (int16_t)y, (int16_t)wd, (int16_t)ht, img };
    return n + 1;
}

// 列 c0..c1 里看得见的段:每行连着的同种格子(整段,可以伸出屏幕)+ 装饰
static int visible(seg_t *w, int c0, int c1) {
    int n = 0;
    for (int r = 0; r < ML_ROWS; r++) {
        for (int c = c0; c <= c1;) {
            uint8_t img = cell_img(c, r);
            if (img == NONE) {
                c++;
                continue;
            }
            int a = c, b = c;
            while (cell_img(a - 1, r) == img) a--;
            while (cell_img(b + 1, r) == img) b++;
            n = add(w, n, a * ML_TILE, r * ML_TILE, (b - a + 1) * ML_TILE, ML_TILE, img);
            c = b + 1;
        }
    }
    if (!s_decor) return n;
    const lv_image_dsc_t *cloud = morph_art_tile(MT_IMG_CLOUD), *bush = morph_art_tile(MT_IMG_BUSH);
    for (int c = c0 - DECOR_L; c <= c1; c++) {
        if (c < 0 || c >= s_L->cols) continue;
        int row = (c / CLOUD_EVERY) % 2;  // 云在第 0 / 1 行,那两格空着才放
        if (c % CLOUD_EVERY == 2 && morph_ml_tile(s_L, c, row) == MT_EMPTY && morph_ml_tile(s_L, c + 1, row) == MT_EMPTY)
            n = add(w, n, c * ML_TILE, row * ML_TILE + 4, cloud->header.w, cloud->header.h, MT_IMG_CLOUD);
        if (c % BUSH_EVERY == 3) {
            int r = 0;
            while (r < ML_ROWS && morph_ml_tile(s_L, c, r) == MT_EMPTY) r++;
            if (r > 0 && r < ML_ROWS && cell_img(c, r) == MT_IMG_TOP)
                n = add(w, n, c * ML_TILE, r * ML_TILE - bush->header.h, bush->header.w, bush->header.h, MT_IMG_BUSH);
        }
    }
    return n;
}

// 段由左上角和图认出来(每行的段是连着的整段,左上角一样就是同一段)
static int find(const seg_t *w, int n, const seg_t *s) {
    for (int k = 0; k < n; k++)
        if (w[k].x == s->x && w[k].y == s->y && w[k].img == s->img) return k;
    return -1;
}

// 摆列 c0..c1:还看得见的段不动;看不见的对象收回来,给新进来的段用
static void lay_out(int c0, int c1) {
    seg_t want[POOL];
    bool have[POOL] = { false };
    int n = visible(want, c0, c1);
    for (int i = 0; i < POOL; i++) {
        if (!s_used[i]) continue;
        int k = find(want, n, &s_seg[i]);
        if (k >= 0) {
            have[k] = true;
            continue;
        }
        s_used[i] = false;
        uk_show(s_obj[i], false);
    }
    int i = 0;
    for (int k = 0; k < n; k++) {
        if (have[k]) continue;
        while (i < POOL && s_used[i]) i++;
        if (i == POOL) break;
        lv_obj_t *o = s_obj[i];
        if (!o) {  // 用到才建;放在人后面(世界容器的最前面)
            o = s_obj[i] = lv_image_create(s_world);
            lv_obj_move_to_index(o, 0);
            lv_image_set_inner_align(o, LV_IMAGE_ALIGN_TILE);
        }
        const lv_image_dsc_t *d = morph_art_tile(want[k].img);
        if (lv_image_get_src(o) != d) lv_image_set_src(o, d);
        if (lv_obj_get_style_width(o, 0) != want[k].w || lv_obj_get_style_height(o, 0) != want[k].h)
            lv_obj_set_size(o, want[k].w, want[k].h);
        pos(o, want[k].x, want[k].y);
        uk_show(o, true);
        s_seg[i] = want[k];
        s_used[i] = true;
    }
    s_c0 = c0;
    s_c1 = c1;
}

static int32_t cam_target(const morph_body_t *b) {
    int32_t lead = b->dir > 0 ? UK_W / 3 : UK_W * 2 / 3;
    int32_t t = b->x - lead * MB_SUB, max = ((int32_t)s_L->cols * ML_TILE - UK_W) * MB_SUB;
    if (max < 0) max = 0;
    return t < 0 ? 0 : t > max ? max : t;
}

static void camera(const morph_body_t *b, uint32_t dt) {
    int32_t t = cam_target(b);
    if (s_fresh) {
        s_cam = t;
    } else {
        int32_t v = (int32_t)(MV_CAM_V * MB_SUB * dt / 1000), d = t - s_cam;
        if (v < 1) v = 1;
        s_cam += d > v ? v : d < -v ? -v : d;
    }
    int px = (int)(s_cam / MB_SUB), moved = px - s_cam_px;
    // 差够一步才挪;停下来了(追上了)就对齐,别差几个像素停着
    if (s_fresh || moved >= s_step || moved <= -s_step || (s_cam == t && moved)) s_cam_px = px;
    pos(s_world, -s_cam_px, MV_BAR_H);
    int c0 = s_cam_px / ML_TILE, c1 = (s_cam_px + UK_W - 1) / ML_TILE;
    if (c0 != s_c0 || c1 != s_c1) lay_out(c0, c1);
}

static uint8_t pose_of(const morph_body_t *b) {
    switch (b->st) {
        case MB_AIR: return b->flaps ? MA_FLAP : b->vy < 0 ? MA_JUMP : MA_FALL;
        case MB_WALK: return (fdiv(b->x, MB_SUB * STEP_PX) & 1) ? MA_WALK2 : MA_WALK1;
        default: return MA_IDLE;
    }
}

static lv_obj_t *figure(uint8_t ch) {
    lv_obj_t *o = lv_image_create(s_world);
    lv_image_set_src(o, morph_art_pose(ch, MA_IDLE, false));
    lv_obj_set_size(o, FIG, FIG);
    lv_image_set_scale(o, 256 * 2);
    lv_image_set_antialias(o, false);
    return o;
}

// 放大两倍是以中心为轴:控件左上角在 (x - 8, y - 24),画出来 32×32 的脚底中点正好在 (x, y)
static void place(int i, const morph_body_t *b, bool on) {
    lv_obj_t *o = s_fig[i];
    uk_show(o, on);
    if (!on) return;
    const lv_image_dsc_t *d = morph_art_pose(s_ch[i], pose_of(b), b->dir < 0);
    if (lv_image_get_src(o) != d) lv_image_set_src(o, d);
    pos(o, fdiv(b->x, MB_SUB) - FIG / 2, fdiv(b->y, MB_SUB) - FIG * 3 / 2);
}

void morph_mv_create(lv_obj_t *parent, const morph_level_t *L, uint8_t me_ch, uint8_t peer_ch) {
    morph_mv_destroy();
    s_L = L;
    s_ch[ME] = me_ch;
    s_ch[PEER] = peer_ch;
    s_parent = parent;
    s_root = uk_rect(parent, 0, 0, UK_W, UK_H, UK_WAINSCOT);
    lv_obj_t *t = uk_text(s_root, UK_F12, UK_RED, TX_TITLE);
    lv_obj_set_pos(t, 28, 8);
    s_state = uk_text(s_root, UK_F12, UK_INK, "");
    lv_obj_align(s_state, LV_ALIGN_TOP_RIGHT, -28, 8);
    s_world = uk_rect(s_root, 0, MV_BAR_H, L->cols * ML_TILE, MV_LEVEL_H, SKY);
    s_fig[PEER] = figure(peer_ch);
    s_fig[ME] = figure(me_ch);
    s_wait = uk_pill(s_root, UK_F24, UK_INK, UK_WHITE, TX_WAIT_PEER);
    lv_obj_align(s_wait, LV_ALIGN_TOP_MID, 0, MV_BAR_H + 96);
    uk_show(s_wait, false);
    t = uk_text(s_root, UK_F24, UK_INK, TX_KEYS);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, MV_HINT_Y + 6);
    s_fresh = true;
    s_c0 = s_c1 = -1;
    s_objs = count(s_parent);
}

void morph_mv_update(const morph_play_t *p, uint32_t dt_ms) {
    if (!s_root) return;
    camera(&p->me, dt_ms);
    s_fresh = false;
    place(ME, &p->me, true);
    place(PEER, &p->peer, !p->solo && p->peer_ok);
    bool lost = !p->solo && p->phase == MP_PAUSED;
    uk_set_text(s_state, p->solo ? TX_ST_SOLO : p->phase == MP_PLAY ? TX_ST_PLAY : lost ? TX_ST_LOST : TX_ST_WAIT);
    uk_show(s_wait, lost);
    s_objs = count(s_parent);
}

void morph_mv_destroy(void) {
    if (s_root) lv_obj_delete(s_root);
    s_root = s_state = s_world = s_wait = s_parent = NULL;
    s_fig[0] = s_fig[1] = NULL;
    memset(s_obj, 0, sizeof s_obj);
    memset(s_used, 0, sizeof s_used);
    s_L = NULL;
    s_objs = 0;
}

void morph_mv_set_cam_step(uint8_t px) { s_step = px ? px : 1; }

void morph_mv_set_decor(bool on) {
    if (on == s_decor) return;
    s_decor = on;
    s_c0 = s_c1 = -1;  // 下一帧重摆
}

uint16_t morph_mv_obj_count(void) { return s_objs; }
