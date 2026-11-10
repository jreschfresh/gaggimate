#include "JungleUI.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <display/ui/default/eez/actions.h>
#include <src/draw/sw/lv_draw_sw_blend.h>

LV_FONT_DECLARE(jungle_font_104)
LV_FONT_DECLARE(jungle_font_80)
LV_FONT_DECLARE(jungle_font_22)
LV_FONT_DECLARE(pj_font_24)
LV_FONT_DECLARE(pj_font_16)
LV_IMG_DECLARE(jungle_icon_drop)
LV_IMG_DECLARE(jungle_icon_cup)
LV_IMG_DECLARE(jungle_icon_x)
LV_IMG_DECLARE(jungle_icon_check)
LV_IMG_DECLARE(jungle_icon_wisp)

namespace {

constexpr int SIZE = 480;
constexpr int BTN_SIZE = 120;
constexpr int SMALL_BTN = 56;

struct Theme {
    uint32_t top;    // gradient colour at the top of the display
    uint32_t bottom; // gradient colour at the bottom
    uint32_t text;   // text and outlines
    uint32_t button; // big start / stop button
    uint32_t glyph;  // play triangle / stop square on the button
};

const Theme THEMES[JM_COUNT] = {
    {0xE5850F, 0xC32D11, 0xFFEEDE, 0xFFEEDE, 0xC32D11}, // brew: orange
    {0x35B6E8, 0x123A9C, 0xFFEEDE, 0xFFEEDE, 0x123A9C}, // water: blue
    {0xFF9EC4, 0xC2185B, 0xFFEEDE, 0xFFEEDE, 0xC2185B}, // steam: pink
    {0x7CC47F, 0x1F6B3A, 0xFFEEDE, 0xFFEEDE, 0x1F6B3A}, // grind: green
};

// Screen order for swiping, left to right.
const int ORDER[JM_COUNT] = {JM_WATER, JM_BREW, JM_STEAM, JM_GRIND};

JungleUI *g_ui = nullptr;
bool g_active[JM_COUNT] = {false};
int g_fx = 0; // optional animations (JungleUI::setEffects)
int g_cups = 1; // brew screen: number of cups planned before steaming (1 = a single brew, pressing the cups button counts up)

lv_color_t col(uint32_t c) { return lv_color_hex(c); }

lv_obj_t *plain(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, col(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

void setText(lv_obj_t *l, const char *text) {
    if (l != nullptr && strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

// Places a label so that its text is centred on (cx, cy).
void centerLabel(lv_obj_t *l, int cx, int cy, int width) {
    const lv_font_t *font = lv_obj_get_style_text_font(l, LV_PART_MAIN);
    const int h = lv_font_get_line_height(font);
    lv_obj_set_size(l, width, h);
    lv_obj_set_pos(l, cx - width / 2, cy - h / 2);
}

// Filled triangle with softly rounded corners: the polygon plus round-capped lines along its edges.
void roundedTriangle(lv_draw_ctx_t *ctx, lv_color_t color, const lv_point_t *p, int rounding) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa = LV_OPA_COVER;
    d.radius = 0;
    d.border_width = 0;
    lv_draw_polygon(ctx, &d, p, 3);
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = color;
    l.opa = LV_OPA_COVER;
    l.width = rounding;
    l.round_start = 1;
    l.round_end = 1;
    for (int i = 0; i < 3; i++) {
        lv_draw_line(ctx, &l, &p[i], &p[(i + 1) % 3]);
    }
}

void playGlyphDraw(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    // Triangle in a 48 x 48 box, pointing right, with soft corners.
    lv_point_t p[3] = {{(lv_coord_t)(a.x1 + 18), (lv_coord_t)(a.y1 + 13)},
                       {(lv_coord_t)(a.x1 + 34), (lv_coord_t)(a.y1 + 24)},
                       {(lv_coord_t)(a.x1 + 18), (lv_coord_t)(a.y1 + 35)}};
    roundedTriangle(ctx, lv_obj_get_style_text_color(o, LV_PART_MAIN), p, 14);
}

// One display row in the colour (r, g, b: 0..255), with a fine ordered dither. The 16-bit display has only 32 / 64 steps
// per colour channel; without the dither a long gradient shows as bands with visible edges. The dither only ever moves a
// pixel by one colour step, so no grain is visible.
void ditherRow(lv_draw_ctx_t *ctx, int y, int x1, int w, float r8, float g8, float b8) {
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    static lv_color_t line[SIZE];
    if (w > SIZE) {
        w = SIZE;
    }
    const float r = r8 * (31.0f / 255.0f), g = g8 * (63.0f / 255.0f), bl = b8 * (31.0f / 255.0f);
    lv_color_t px[4];
    for (int k = 0; k < 4; k++) {
        const float th = (bayer[y & 3][k] + 0.5f) / 16.0f;
        const int r5 = static_cast<int>(r + th), g6 = static_cast<int>(g + th), b5 = static_cast<int>(bl + th);
        px[k] = lv_color_make((r5 > 31 ? 31 : r5) << 3, (g6 > 63 ? 63 : g6) << 2, (b5 > 31 ? 31 : b5) << 3);
    }
    for (int x = 0; x < w; x++) {
        line[x] = px[(x1 + x) & 3];
    }
    lv_draw_sw_blend_dsc_t b;
    lv_memset_00(&b, sizeof(b));
    b.src_buf = line;
    b.opa = LV_OPA_COVER;
    b.blend_mode = LV_BLEND_MODE_NORMAL;
    b.mask_res = LV_DRAW_MASK_RES_FULL_COVER;
    lv_area_t row = {static_cast<lv_coord_t>(x1), static_cast<lv_coord_t>(y), static_cast<lv_coord_t>(x1 + w - 1), static_cast<lv_coord_t>(y)};
    b.blend_area = &row;
    lv_draw_sw_blend(ctx, &b);
}

// Vertical gradient from the style's bg_color (top) to bg_grad_color (bottom), drawn row by row.
void gradDraw(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_area_t clip;
    if (!_lv_area_intersect(&clip, &a, ctx->clip_area)) {
        return;
    }
    const lv_color32_t c0 = {.full = lv_color_to32(lv_obj_get_style_bg_color(o, LV_PART_MAIN))};
    const lv_color32_t c1 = {.full = lv_color_to32(lv_obj_get_style_bg_grad_color(o, LV_PART_MAIN))};
    const float h = static_cast<float>(lv_area_get_height(&a) - 1);
    for (int y = clip.y1; y <= clip.y2; y++) {
        const float t = h > 0.0f ? static_cast<float>(y - a.y1) / h : 0.0f;
        ditherRow(ctx, y, clip.x1, lv_area_get_width(&clip), c0.ch.red + (c1.ch.red - c0.ch.red) * t,
                  c0.ch.green + (c1.ch.green - c0.ch.green) * t, c0.ch.blue + (c1.ch.blue - c0.ch.blue) * t);
    }
}

// Coffee colours of the shot: dark brown at the bottom, over mid brown, to cream at the top of the display. Only the
// lowest rows are drawn, up to the height stored in the object's user data.
void coffeeDraw(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    const int height = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(o)));
    if (height <= 0) {
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    a.y1 = static_cast<lv_coord_t>(a.y2 - height + 1);
    lv_area_t clip;
    if (!_lv_area_intersect(&clip, &a, ctx->clip_area)) {
        return;
    }
    static const float dark[3] = {0x2B, 0x16, 0x08}, mid[3] = {0x8A, 0x5A, 0x33}, cream[3] = {0xF1, 0xE2, 0xC6};
    for (int y = clip.y1; y <= clip.y2; y++) {
        const float t = static_cast<float>(SIZE - 1 - y) / static_cast<float>(SIZE - 1); // 0 at the bottom, 1 at the top
        const float *c0 = t < 0.5f ? dark : mid;
        const float *c1 = t < 0.5f ? mid : cream;
        const float u = t < 0.5f ? t / 0.5f : (t - 0.5f) / 0.5f;
        ditherRow(ctx, y, clip.x1, lv_area_get_width(&clip), c0[0] + (c1[0] - c0[0]) * u, c0[1] + (c1[1] - c0[1]) * u,
                  c0[2] + (c1[2] - c0[2]) * u);
    }
}

// Steam wisp: rises by 56 pixels while it fades in and out again (v runs from 0 to 1000).
void wispAnimCb(void *obj, int32_t v) {
    lv_obj_t *o = static_cast<lv_obj_t *>(obj);
    lv_obj_set_style_translate_y(o, static_cast<lv_coord_t>(-v * 56 / 1000), LV_PART_MAIN);
    const int opa = v < 300 ? v * 150 / 300 : (1000 - v) * 150 / 700;
    lv_obj_set_style_img_opa(o, static_cast<lv_opa_t>(opa), LV_PART_MAIN);
}

void mkBar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *b = plain(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_MAIN); // round ends
    lv_obj_set_style_bg_color(b, col(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
}

// Round outlined button with a minus or a plus.
lv_obj_t *roundButton(lv_obj_t *parent, int x, int y, uint32_t color, bool plus) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, SMALL_BTN, SMALL_BTN);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, col(color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 15);
    const int c = SMALL_BTN / 2;
    mkBar(b, c - 10 - 2, c - 1 - 2, 20, 3, color);
    if (plus) {
        mkBar(b, c - 1 - 2, c - 10 - 2, 3, 20, color);
    }
    return b;
}

// Round outlined button with an icon (cross / check mark on the question overlay).
lv_obj_t *iconButton(lv_obj_t *parent, int x, int y, uint32_t color, const lv_img_dsc_t *icon) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, SMALL_BTN + 8, SMALL_BTN + 8);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, col(color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, col(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 15);
    lv_obj_t *i = lv_img_create(b);
    lv_img_set_src(i, icon);
    lv_obj_clear_flag(i, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_img_recolor(i, col(color), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(i);
    return b;
}

void hide(lv_obj_t *o, bool hidden) {
    if (o == nullptr || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == hidden) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

// Dark / light colour for a round +/- button (outline and bars).
void recolorButton(lv_obj_t *btn, uint32_t color) {
    if (btn == nullptr) {
        return;
    }
    lv_obj_set_style_border_color(btn, col(color), LV_PART_MAIN);
    const uint32_t n = lv_obj_get_child_cnt(btn);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_set_style_bg_color(lv_obj_get_child(btn, i), col(color), LV_PART_MAIN);
    }
}

int findOrderIndex(int mode) {
    for (int i = 0; i < JM_COUNT; i++) {
        if (ORDER[i] == mode) {
            return i;
        }
    }
    return 1;
}

void goToMode(int mode) {
    switch (mode) {
    case JM_WATER:
        action_on_water_screen(nullptr);
        break;
    case JM_STEAM:
        action_on_steam_screen(nullptr);
        break;
    case JM_GRIND:
        action_on_grind_screen(nullptr);
        break;
    default:
        action_on_brew_screen(nullptr);
        break;
    }
}

void gestureCb(lv_event_t *e) {
    if (g_ui == nullptr) {
        return;
    }
    const int mode = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == nullptr) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT && dir != LV_DIR_TOP && dir != LV_DIR_BOTTOM) {
        return;
    }
    lv_indev_wait_release(indev);
    if (g_active[mode] && mode != JM_STEAM) {
        return; // never leave a running brew / water / grind by accident (Steam may always be left)
    }
    if (dir == LV_DIR_TOP) {
        action_on_standby(e); // swipe up: standby (the Adam picture)
        return;
    }
    if (dir == LV_DIR_BOTTOM) {
        action_on_menu_click(e); // swipe down: the old menu (Info, Profile, ...)
        return;
    }
    const int step = dir == LV_DIR_LEFT ? 1 : -1;
    int idx = findOrderIndex(mode) + step;
    if (idx >= 0 && idx < JM_COUNT && ORDER[idx] == JM_GRIND && !g_ui->grindAvailable) {
        idx = -1; // no grinder: Steam is the last screen
    }
    if (idx < 0 || idx >= JM_COUNT) {
        return;
    }
    goToMode(ORDER[idx]);
}

void startCb(lv_event_t *e) {
    const int mode = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        switch (mode) {
        case JM_BREW:
            if (g_active[mode]) {
                if (g_ui != nullptr) {
                    g_ui->requestStop();
                }
                action_on_brew_cancel(e);
            } else {
                action_on_brew_start(e);
            }
            break;
        case JM_WATER:
            action_on_simple_process_toggle(e);
            break;
        case JM_GRIND:
            action_on_grind_toggle(e);
            break;
        default:
            break;
        }
    } else if (code == LV_EVENT_LONG_PRESSED && mode == JM_BREW && !g_active[mode]) {
        action_on_flush(e);
    }
}

void flushCb(lv_event_t *e) {
    if (!g_active[JM_BREW]) {
        action_on_flush(e);
    }
}

void multiCb(lv_event_t *e) {
    (void)e;
    if (!g_active[JM_BREW]) {
        g_cups = g_cups % 6 + 1; // 1 -> 2 -> ... -> 6 -> 1
    }
}

void tareCb(lv_event_t *e) { action_on_volumetric_hold(e); }

void minusCb(lv_event_t *e) {
    const int mode = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (mode == JM_GRIND) {
        action_on_grind_time_lower(e);
    } else {
        action_on_steam_temp_lower(e); // same call the old Water / Steam screens used
    }
}

void plusCb(lv_event_t *e) {
    const int mode = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (mode == JM_GRIND) {
        action_on_grind_time_raise(e);
    } else {
        action_on_steam_temp_raise(e);
    }
}

// Snowflake: three lines through the centre, each arm with a small fork near its tip.
void snowDraw(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const float cx = (a.x1 + a.x2) / 2.0f;
    const float cy = (a.y1 + a.y2) / 2.0f;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = lv_obj_get_style_text_color(o, LV_PART_MAIN);
    l.opa = LV_OPA_COVER;
    l.width = 4;
    l.round_start = 1;
    l.round_end = 1;
    const float R = 26.0f;
    for (int i = 0; i < 6; i++) {
        const float ang = i * 3.14159265f / 3.0f - 3.14159265f / 2.0f;
        const float dx = cosf(ang), dy = sinf(ang);
        lv_point_t p0 = {(lv_coord_t)lroundf(cx), (lv_coord_t)lroundf(cy)};
        lv_point_t p1 = {(lv_coord_t)lroundf(cx + dx * R), (lv_coord_t)lroundf(cy + dy * R)};
        lv_draw_line(ctx, &l, &p0, &p1);
        // fork at 60 % of the arm
        const float bx = cx + dx * R * 0.62f, by = cy + dy * R * 0.62f;
        for (int s = -1; s <= 1; s += 2) {
            const float fa = ang + s * 0.75f;
            lv_point_t f0 = {(lv_coord_t)lroundf(bx), (lv_coord_t)lroundf(by)};
            lv_point_t f1 = {(lv_coord_t)lroundf(bx + cosf(fa) * 9.0f), (lv_coord_t)lroundf(by + sinf(fa) * 9.0f)};
            lv_draw_line(ctx, &l, &f0, &f1);
        }
    }
}

void snowCb(lv_event_t *e) {
    (void)e;
    if (g_ui != nullptr) {
        g_ui->requestCoolToggle();
    }
}

void presetCb(lv_event_t *e) {
    const int t = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (g_ui != nullptr) {
        g_ui->requestWaterPreset(t);
    }
}

void loadedCb(lv_event_t *e) {
    (void)e;
    if (g_ui != nullptr) {
        g_ui->screenLoaded();
    }
}

void confirmBackCb(lv_event_t *e) { action_on_warning_back(e); }
void confirmOkCb(lv_event_t *e) { action_on_warning_ignore(e); }

} // namespace

void JungleUI::init() {
    g_ui = this;
    for (int m = 0; m < JM_COUNT; m++) {
        build(m);
    }
}

void JungleUI::build(int mode) {
    const Theme &t = THEMES[mode];
    Page &p = pages[mode];
    void *ud = reinterpret_cast<void *>(static_cast<intptr_t>(mode));

    p.scr = lv_obj_create(nullptr);
    lv_obj_remove_style_all(p.scr);
    lv_obj_clear_flag(p.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(p.scr, SIZE, SIZE);
    lv_obj_set_style_bg_color(p.scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p.scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(p.scr, gestureCb, LV_EVENT_GESTURE, ud);
    lv_obj_add_event_cb(p.scr, loadedCb, LV_EVENT_SCREEN_LOAD_START, nullptr);
    lv_obj_add_event_cb(p.scr, loadedCb, LV_EVENT_SCREEN_LOADED, nullptr);

    // Background: a clipping box at the bottom, with the full-size gradient inside it, so the colours
    // stay where they are and only more of them is uncovered.
    p.clip = plain(p.scr);
    lv_obj_set_size(p.clip, SIZE, SIZE);
    lv_obj_set_pos(p.clip, 0, 0);
    p.grad = plain(p.clip);
    lv_obj_set_size(p.grad, SIZE, SIZE);
    lv_obj_set_style_bg_color(p.grad, col(t.top), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(p.grad, col(t.bottom), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(p.grad, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p.grad, LV_OPA_TRANSP, LV_PART_MAIN); // drawn by gradDraw (the style only holds the two colours)
    lv_obj_add_event_cb(p.grad, gradDraw, LV_EVENT_DRAW_MAIN, nullptr);
    setFill(p, SIZE);

    if (mode == JM_BREW) {
        p.coffee = plain(p.scr);
        lv_obj_set_size(p.coffee, SIZE, SIZE);
        lv_obj_set_pos(p.coffee, 0, 0);
        lv_obj_add_event_cb(p.coffee, coffeeDraw, LV_EVENT_DRAW_MAIN, nullptr);
    }
    if (mode == JM_STEAM) {
        // Three wisps that rise from the surface of the fill (only shown with the steam effect switched on).
        p.wisps = plain(p.scr);
        lv_obj_set_size(p.wisps, SIZE, 130);
        lv_obj_add_flag(p.wisps, LV_OBJ_FLAG_HIDDEN);
        const int xs[3] = {150, 240, 322};
        const int delay[3] = {0, 1150, 2050};
        for (int i = 0; i < 3; i++) {
            lv_obj_t *w = lv_img_create(p.wisps);
            lv_img_set_src(w, &jungle_icon_wisp);
            lv_obj_clear_flag(w, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_img_recolor(w, col(0xFFD3E4), LV_PART_MAIN);
            lv_obj_set_style_img_recolor_opa(w, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_img_opa(w, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_pos(w, xs[i] - 15, 130 - 72);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, w);
            lv_anim_set_exec_cb(&a, wispAnimCb);
            lv_anim_set_values(&a, 0, 1000);
            lv_anim_set_time(&a, 3000);
            lv_anim_set_delay(&a, delay[i]);
            lv_anim_set_repeat_delay(&a, 500);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&a);
        }
    }

    // Top row: temperature (and pressure).
    p.temp = label(p.scr, &jungle_font_22, t.text);
    p.bar = label(p.scr, &jungle_font_22, t.text);
    centerLabel(p.temp, 190, 37, 120);
    centerLabel(p.bar, 290, 37, 120);

    // Centre value.
    const lv_font_t *bigFont = (mode == JM_BREW || mode == JM_STEAM) ? &jungle_font_104 : &jungle_font_80;
    p.center = label(p.scr, bigFont, t.text);
    centerLabel(p.center, SIZE / 2, mode == JM_BREW || mode == JM_STEAM ? 202 : 200, 300);
    if (mode == JM_BREW) {
        p.time = label(p.scr, &jungle_font_22, t.text);
        centerLabel(p.time, SIZE / 2, 284, 120);
        lv_obj_add_flag(p.time, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(p.center, LV_OBJ_FLAG_CLICKABLE); // long press tares the scale
        lv_obj_add_event_cb(p.center, tareCb, LV_EVENT_LONG_PRESSED, nullptr);
    }

    if (mode == JM_WATER) { // two temperature presets: 25 (cold) and 80 (tea)
        for (int i = 0; i < 2; i++) {
            lv_obj_t *b = lv_obj_create(p.scr);
            lv_obj_remove_style_all(b);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(b, SMALL_BTN + 8, SMALL_BTN + 8);
            lv_obj_set_pos(b, i == 0 ? 44 : 372, 183);
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_border_width(b, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(b, col(t.text), LV_PART_MAIN);
            lv_obj_set_style_bg_color(b, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_ext_click_area(b, 12);
            lv_obj_add_event_cb(b, presetCb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i == 0 ? 25 : 80)));
            lv_obj_t *lb = label(b, &jungle_font_22, t.text);
            setText(lb, i == 0 ? "25" : "80");
            lv_obj_center(lb);
            p.preset[i] = b;
            p.presetLabel[i] = lb;
        }
    }
    if (mode == JM_GRIND) {
        p.minus = roundButton(p.scr, 48, 187, t.text, false);
        p.plus = roundButton(p.scr, 376, 187, t.text, true);
        lv_obj_add_event_cb(p.minus, minusCb, LV_EVENT_CLICKED, ud);
        lv_obj_add_event_cb(p.plus, plusCb, LV_EVENT_CLICKED, ud);
    }

    if (mode == JM_STEAM) { // snowflake: cool down through the steam wand
        p.snow = lv_obj_create(p.scr);
        lv_obj_remove_style_all(p.snow);
        lv_obj_clear_flag(p.snow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(p.snow, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(p.snow, 96, 96);
        lv_obj_set_pos(p.snow, (SIZE - 96) / 2, 334);
        lv_obj_set_style_radius(p.snow, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(p.snow, 3, LV_PART_MAIN);
        lv_obj_set_style_border_color(p.snow, col(t.text), LV_PART_MAIN);
        lv_obj_set_style_bg_color(p.snow, col(t.text), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.snow, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_ext_click_area(p.snow, 20);
        lv_obj_add_event_cb(p.snow, snowCb, LV_EVENT_CLICKED, nullptr);
        p.snowGlyph = plain(p.snow);
        lv_obj_set_size(p.snowGlyph, 96, 96);
        lv_obj_center(p.snowGlyph);
        lv_obj_set_style_text_color(p.snowGlyph, col(t.text), LV_PART_MAIN);
        lv_obj_add_event_cb(p.snowGlyph, snowDraw, LV_EVENT_DRAW_MAIN_END, nullptr);
    }

    // Big start / stop button in the lower third (the steam screen has none: steam follows the machine switch).
    if (mode != JM_STEAM) {
        p.start = lv_obj_create(p.scr);
        lv_obj_remove_style_all(p.start);
        lv_obj_clear_flag(p.start, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(p.start, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(p.start, BTN_SIZE, BTN_SIZE);
        lv_obj_set_pos(p.start, (SIZE - BTN_SIZE) / 2, 322);
        lv_obj_set_style_radius(p.start, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(p.start, col(t.button), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.start, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.start, LV_OPA_70, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(p.start, 25);
        lv_obj_add_event_cb(p.start, startCb, LV_EVENT_SHORT_CLICKED, ud);
        lv_obj_add_event_cb(p.start, startCb, LV_EVENT_LONG_PRESSED, ud);

        p.play = plain(p.start);
        lv_obj_set_size(p.play, 48, 48);
        lv_obj_center(p.play);
        lv_obj_set_style_text_color(p.play, col(t.glyph), LV_PART_MAIN);
        lv_obj_add_event_cb(p.play, playGlyphDraw, LV_EVENT_DRAW_MAIN_END, nullptr);

        p.stop = plain(p.start);
        lv_obj_set_size(p.stop, 30, 30);
        lv_obj_center(p.stop);
        lv_obj_set_style_radius(p.stop, 9, LV_PART_MAIN);
        lv_obj_set_style_bg_color(p.stop, col(t.glyph), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.stop, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(p.stop, LV_OBJ_FLAG_HIDDEN);
    }

    if (mode == JM_BREW) {
        // Flush (drop) next to the start button.
        p.flush = lv_obj_create(p.scr);
        lv_obj_remove_style_all(p.flush);
        lv_obj_clear_flag(p.flush, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(p.flush, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(p.flush, SMALL_BTN, SMALL_BTN);
        lv_obj_set_pos(p.flush, 88, 354);
        lv_obj_set_style_radius(p.flush, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(p.flush, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(p.flush, col(t.text), LV_PART_MAIN);
        lv_obj_set_style_bg_color(p.flush, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.flush, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.flush, LV_OPA_60, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(p.flush, 15);
        lv_obj_add_event_cb(p.flush, flushCb, LV_EVENT_PRESSED, nullptr);
        lv_obj_t *drop = lv_img_create(p.flush);
        lv_img_set_src(drop, &jungle_icon_drop);
        lv_obj_clear_flag(drop, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_img_recolor(drop, col(t.text), LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(drop, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(drop, LV_ALIGN_CENTER, 0, -1);

        // Multi-brew (two cups) on the other side of the start button, with a counter of finished brews.
        p.multi = lv_obj_create(p.scr);
        lv_obj_remove_style_all(p.multi);
        lv_obj_clear_flag(p.multi, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(p.multi, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(p.multi, SMALL_BTN, SMALL_BTN);
        lv_obj_set_pos(p.multi, SIZE - 88 - SMALL_BTN, 354);
        lv_obj_set_style_radius(p.multi, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(p.multi, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(p.multi, col(t.text), LV_PART_MAIN);
        lv_obj_set_style_bg_color(p.multi, col(t.button), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.multi, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.multi, LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(p.multi, 15);
        lv_obj_add_event_cb(p.multi, multiCb, LV_EVENT_PRESSED, nullptr);
        p.multiGlyph = lv_img_create(p.multi);
        lv_img_set_src(p.multiGlyph, &jungle_icon_cup);
        lv_obj_clear_flag(p.multiGlyph, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_img_recolor(p.multiGlyph, col(t.text), LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(p.multiGlyph, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(p.multiGlyph, LV_ALIGN_LEFT_MID, 5, 0); // cup + number together sit centred in the circle
        p.multiLabel = label(p.multi, &pj_font_16, t.text);
        centerLabel(p.multiLabel, 41, SMALL_BTN / 2 - 4, 14); // inside the button, right of the cup, level with the bowl
        lv_label_set_text(p.multiLabel, "1");

        // Warning confirmation (the controller asks before it starts a brew with an error-level warning).
        p.confirm = plain(p.scr);
        lv_obj_set_size(p.confirm, SIZE, SIZE);
        lv_obj_add_flag(p.confirm, LV_OBJ_FLAG_CLICKABLE); // swallows touches behind it
        lv_obj_set_style_bg_color(p.confirm, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.confirm, 242, LV_PART_MAIN); // nearly opaque: the buttons behind it must not shine through
        p.confirmLabel = label(p.confirm, &pj_font_24, 0xFFEEDE);
        lv_obj_set_size(p.confirmLabel, 320, LV_SIZE_CONTENT);
        lv_label_set_long_mode(p.confirmLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(p.confirmLabel, 80, 120);
        const int ids[2] = {0, 1};
        for (int i : ids) {
            lv_obj_t *b = iconButton(p.confirm, i == 0 ? 126 : 290, 322, 0xFFEEDE, i == 0 ? &jungle_icon_x : &jungle_icon_check);
            lv_obj_add_event_cb(b, i == 0 ? confirmBackCb : confirmOkCb, LV_EVENT_CLICKED, nullptr);
        }
        lv_obj_add_flag(p.confirm, LV_OBJ_FLAG_HIDDEN);
    }
}

void JungleUI::setFill(Page &p, int height) {
    if (height < 0) {
        height = 0;
    }
    if (height > SIZE) {
        height = SIZE;
    }
    if (height == p.fillHeight) {
        return;
    }
    p.fillHeight = height;
    if (height == 0) {
        lv_obj_add_flag(p.clip, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(p.clip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(p.clip, SIZE, height);
    lv_obj_set_pos(p.clip, 0, SIZE - height);
    lv_obj_set_pos(p.grad, 0, height - SIZE);
}

void JungleUI::setEffects(int fx) { g_fx = fx; }

// Coffee height in pixels. Only the rows between the old and the new height are redrawn.
void JungleUI::setCoffee(Page &p, int height) {
    if (p.coffee == nullptr || height == p.coffeeH) {
        return;
    }
    const int lo = height < p.coffeeH ? height : p.coffeeH, hi = height < p.coffeeH ? p.coffeeH : height;
    p.coffeeH = height;
    lv_obj_set_user_data(p.coffee, reinterpret_cast<void *>(static_cast<intptr_t>(height)));
    lv_area_t a = {0, static_cast<lv_coord_t>(SIZE - hi), SIZE - 1, static_cast<lv_coord_t>(SIZE - lo - 1)};
    lv_obj_invalidate_area(p.coffee, &a);
}

void JungleUI::coffeeAnimCb(void *var, int32_t v) {
    if (g_ui != nullptr) {
        g_ui->setCoffee(*static_cast<Page *>(var), v);
    }
}

// The start button fills with its colour once the temperature is stable (v: 0..255).
void JungleUI::readyAnimCb(void *var, int32_t v) {
    Page *p = static_cast<Page *>(var);
    const Theme &bt = THEMES[JM_BREW];
    lv_obj_set_style_bg_opa(p->start, static_cast<lv_opa_t>(v), LV_PART_MAIN);
    lv_obj_set_style_text_color(p->play, col(v > 150 ? bt.glyph : bt.button), LV_PART_MAIN);
    if (v >= 255) {
        lv_obj_set_style_border_width(p->start, 0, LV_PART_MAIN);
    }
    lv_obj_invalidate(p->start);
}

void JungleUI::fillAnimCb(void *var, int32_t v) {
    Page *p = static_cast<Page *>(var);
    if (g_ui != nullptr) {
        g_ui->setFill(*p, v);
    }
}

void JungleUI::setFillAnimated(Page &p, int height) {
    if (height < 0)
        height = 0;
    if (height > SIZE)
        height = SIZE;
    if (height == p.fillTargetH) {
        return;
    }
    p.fillTargetH = height;
    lv_anim_del(&p, fillAnimCb);
    const int from = p.fillHeight < 0 ? height : p.fillHeight;
    if (from == height) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &p);
    lv_anim_set_exec_cb(&a, fillAnimCb);
    lv_anim_set_values(&a, from, height);
    lv_anim_set_time(&a, 350);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void JungleUI::show(int mode) {
    if (mode < 0 || mode >= JM_COUNT || pages[mode].scr == nullptr) {
        mode = JM_BREW;
    }
    if (shownMode == mode && (lv_scr_act() == pages[mode].scr || loadPending)) {
        return;
    }
    // Slide in from the side the screen sits on; fade in when we come from one of the old screens.
    lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_FADE_IN;
    uint32_t ms = 250;
    for (int m = 0; m < JM_COUNT; m++) {
        if (m != mode && lv_scr_act() == pages[m].scr) {
            anim = findOrderIndex(mode) > findOrderIndex(m) ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
            ms = 300;
        }
    }
    if (mode == JM_STEAM) {
        pages[JM_STEAM].steamReached = false;
        pages[JM_STEAM].steamBarMode = -1;
    }
    shownMode = mode;
    loadPending = true;
    loadStarted = lv_tick_get();
    lv_scr_load_anim(pages[mode].scr, anim, ms, 0, false);
}

void JungleUI::screenLoaded() { loadPending = false; }

bool JungleUI::isShown() const {
    if (shownMode < 0) {
        return false;
    }
    if (lv_scr_act() == pages[shownMode].scr) {
        return true;
    }
    return loadPending && lv_tick_elaps(loadStarted) < 600; // the slide has not started yet
}

bool JungleUI::consumeCoolToggle() {
    const bool r = coolToggle;
    coolToggle = false;
    return r;
}

int JungleUI::consumeWaterPreset() {
    const int r = waterPreset;
    waterPreset = 0;
    return r;
}

bool JungleUI::consumeStopRequested() {
    const bool r = stopRequested;
    stopRequested = false;
    return r;
}

void JungleUI::update(const JungleView &v) {
    if (v.mode < 0 || v.mode >= JM_COUNT) {
        return;
    }
    grindAvailable = v.grindAvailable;
    g_active[v.mode] = v.active || v.hold;
    if (v.mode == JM_STEAM) {
        g_cups = 1; // the planned brews are over once steaming starts
    }
    Page &p = pages[v.mode];

    // Background fill: one step per degree.
    float frac = 1.0f;
    const int deg = static_cast<int>(floorf(v.temp));
    if (v.mode == JM_STEAM) {
        const float lo = 50.0f; // everything below only shows the first step
        const float steps = v.fillTarget - lo + 1.0f;
        if (steps > 1.0f) {
            frac = fmaxf(static_cast<float>(deg) - lo + 1.0f, 1.0f) / steps;
        }
    } else if (v.mode != JM_GRIND && v.fillTarget > 0.0f) {
        frac = static_cast<float>(deg) / v.fillTarget;
    }
    if (frac > 1.0f) {
        frac = 1.0f;
    }
    if (frac < 0.0f) {
        frac = 0.0f;
    }
    const bool shotShown = v.mode == JM_BREW && (v.shot || v.hold);
    if (shotShown) {
        frac = 1.0f; // the orange stays complete, the coffee colours rise through it
    }
    int fillH = static_cast<int>(lroundf(frac * SIZE));
    bool steamBar = false;
    if (v.mode == JM_STEAM && v.cool) {
        // Cool-down: the fill rises from the dark red at the bottom towards the brew colour as the temperature falls.
        const float span = v.coolStart - v.coolTarget;
        float f = span > 1.0f ? (v.coolStart - v.temp) / span : 1.0f;
        f = fminf(fmaxf(f, 0.04f), 1.0f);
        fillH = static_cast<int>(lroundf(f * SIZE));
        setFillAnimated(p, fillH);
        hide(p.wisps, true);
    } else if (v.mode == JM_STEAM) {
        // First the steam boiler heats up (the fill follows the temperature). Once the target was reached, the
        // screen switches to the pressure view: the big number and the fill follow the bar value.
        if (v.fillTarget > 0.0f && v.temp >= v.fillTarget - 1.0f) {
            p.steamReached = true;
        } else if (v.temp < v.fillTarget - 20.0f) {
            p.steamReached = false;
        }
        steamBar = p.steamReached && v.pressureAvailable;
        if (steamBar) {
            const float full = 2.0f; // bar at which the display is completely filled
            float f = v.pressure / full;
            f = fminf(fmaxf(f, 0.04f), 1.0f);
            fillH = static_cast<int>(lroundf(f * SIZE));
        }
        if (steamBar != (p.steamBarMode == 1)) {
            p.steamBarMode = steamBar ? 1 : 0;
        }
        setFillAnimated(p, fillH);
        if (p.wisps != nullptr) {
            // steam rises from the surface while there is pressure and enough room above it
            const bool on = (g_fx & 4) && steamBar && v.pressure > 0.3f && p.fillHeight > 0 && p.fillHeight < SIZE - 110;
            hide(p.wisps, !on);
            if (on) {
                lv_obj_set_pos(p.wisps, 0, SIZE - p.fillHeight - 130 + 10);
            }
        }
    } else {
        setFill(p, fillH);
    }

    if (v.mode == JM_BREW) {
        int steps = 0;
        if (shotShown) {
            steps = static_cast<int>(floorf(fminf(fmaxf(v.shotFrac, 0.0f), 1.0f) * SHOT_STEPS + 0.001f));
        }
        // Height of the coffee colours: in 20 steps, or (effect 2) flowing with the shot.
        int target = steps * (SIZE / SHOT_STEPS);
        if ((g_fx & 2) && shotShown) {
            target = static_cast<int>(lroundf(fminf(fmaxf(v.shotFrac, 0.0f), 1.0f) * SIZE));
        }
        if (target != p.coffeeTargetH) {
            p.coffeeTargetH = target;
            lv_anim_del(&p, coffeeAnimCb);
            if ((g_fx & 2) && shotShown && target > p.coffeeH) {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, &p);
                lv_anim_set_exec_cb(&a, coffeeAnimCb);
                lv_anim_set_values(&a, p.coffeeH, target);
                lv_anim_set_time(&a, 400);
                lv_anim_set_path_cb(&a, lv_anim_path_linear);
                lv_anim_start(&a);
            } else {
                setCoffee(p, target);
            }
        }
        p.shotSteps = steps;
        // Light coffee colours at the top need dark text.
        const int mask = (steps >= 19 ? 1 : 0) | (steps >= 20 ? 2 : 0) | (shotShown ? 4 : 0);
        if (mask != p.shotMask) {
            p.shotMask = mask;
            const uint32_t light = 0xFFEEDE, dark = 0x3A2410;
            lv_obj_set_style_text_color(p.temp, col(mask & 1 ? dark : light), LV_PART_MAIN);
            lv_obj_set_style_text_color(p.bar, col(mask & 1 ? dark : light), LV_PART_MAIN);
            lv_obj_set_style_text_color(p.center, col(mask & 2 ? dark : light), LV_PART_MAIN);
            lv_obj_set_style_bg_color(p.stop, col(mask & 4 ? 0x5A2D12 : THEMES[JM_BREW].glyph), LV_PART_MAIN);
        }
    }

    if (v.mode == JM_STEAM) {
        // The pink fill carries the same light text as the other screens, so nothing needs recolouring.
    }

    if (v.mode == JM_STEAM && p.snow != nullptr) {
        const int cs = v.cool ? 1 : 0;
        if (cs != p.coolState) {
            p.coolState = cs;
            const Theme &t = THEMES[JM_STEAM];
            lv_obj_set_style_bg_opa(p.snow, cs ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_text_color(p.snowGlyph, col(cs ? 0x7A0C0C : t.text), LV_PART_MAIN);
            lv_obj_set_style_bg_color(p.grad, col(cs ? 0xE5850F : t.top), LV_PART_MAIN);
            lv_obj_set_style_bg_grad_color(p.grad, col(cs ? 0x7A0C0C : t.bottom), LV_PART_MAIN);
            lv_obj_invalidate(p.snowGlyph);
        }
    }
    if (v.mode == JM_WATER && p.preset[0] != nullptr) {
        const int sel = v.fillTarget >= 60.0f ? 1 : 0;
        if (sel != p.presetState) {
            p.presetState = sel;
            const Theme &t = THEMES[JM_WATER];
            // Tea (80 degrees) is green, cold water (25 degrees) stays blue.
            const uint32_t top = sel ? 0xA8D96B : t.top;
            const uint32_t bottom = sel ? 0x2E7D32 : t.bottom;
            lv_obj_set_style_bg_color(p.grad, col(top), LV_PART_MAIN);
            lv_obj_set_style_bg_grad_color(p.grad, col(bottom), LV_PART_MAIN);
            for (int i = 0; i < 2; i++) {
                lv_obj_set_style_bg_color(p.preset[i], i == sel ? col(t.text) : lv_color_black(), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(p.preset[i], i == sel ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_text_color(p.presetLabel[i], col(i == sel ? bottom : t.text), LV_PART_MAIN);
            }
            // the symbol on the start button takes the colour of the screen (blue for cold water, green for tea)
            if (p.play != nullptr && p.stop != nullptr) {
                lv_obj_set_style_text_color(p.play, col(bottom), LV_PART_MAIN);
                lv_obj_set_style_bg_color(p.stop, col(bottom), LV_PART_MAIN);
                lv_obj_invalidate(p.start);
            }
        }
    }

    char buf[32];
    // brew: one decimal place, so the real value is visible; the other screens show full degrees
    char tbuf[24];
    if (v.mode == JM_BREW) {
        snprintf(tbuf, sizeof(tbuf), "%.1f \xC2\xB0""C", v.temp);
    } else {
        snprintf(tbuf, sizeof(tbuf), "%d \xC2\xB0""C", deg);
    }
    if (v.pressureAvailable && v.mode != JM_STEAM) {
        setText(p.temp, tbuf);
        lv_obj_set_x(p.temp, 130);
        snprintf(buf, sizeof(buf), "%.1f bar", v.pressure);
        setText(p.bar, buf);
        hide(p.bar, false);
    } else {
        setText(p.temp, tbuf);
        lv_obj_set_x(p.temp, 180); // centred when it stands alone
        hide(p.bar, true);
    }

    hide(p.temp, v.mode == JM_STEAM && v.cool);

    if (v.mode == JM_BREW) {
        if (p.time != nullptr) {
            if (shotShown && v.timeText[0] != 0) {
                setText(p.time, v.timeText);
                hide(p.time, false);
            } else {
                hide(p.time, true);
            }
        }
        if (v.showWeight) {
            snprintf(buf, sizeof(buf), "%.1f", v.weight);
            setText(p.center, buf);
            hide(p.center, false);
        } else {
            hide(p.center, true);
        }
    } else {
        setText(p.center, v.centerText);
        if (v.mode == JM_STEAM && v.cool) {
            snprintf(buf, sizeof(buf), "%d\xC2\xB0", deg);
            setText(p.center, buf);
            hide(p.center, false);
        } else if (v.mode == JM_STEAM ? steamBar : v.showCenter) {
            hide(p.center, false);
        } else {
            hide(p.center, true);
        }
    }

    if (p.start != nullptr) {
        hide(p.start, v.hold);
        // Brew: until the temperature is stable, the button is an empty ring with a white symbol.
        const int rs = (v.mode == JM_BREW && !v.ready && !v.active) ? 1 : 0;
        if (rs != p.readyState) {
            const bool soft = (g_fx & 1) && v.mode == JM_BREW && p.readyState == 1 && rs == 0; // just became ready
            p.readyState = rs;
            const Theme &bt = THEMES[v.mode];
            lv_obj_set_style_border_color(p.start, col(bt.button), LV_PART_MAIN);
            lv_obj_set_style_border_opa(p.start, LV_OPA_COVER, LV_PART_MAIN);
            lv_anim_del(&p, readyAnimCb);
            if (soft) {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, &p);
                lv_anim_set_exec_cb(&a, readyAnimCb);
                lv_anim_set_values(&a, 0, 255);
                lv_anim_set_time(&a, 900);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
                lv_anim_start(&a);
            }
            if (!soft) {
                lv_obj_set_style_bg_opa(p.start, rs ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(p.start, rs ? 4 : 0, LV_PART_MAIN);
            }
            const uint32_t glyph = (v.mode == JM_WATER && v.fillTarget >= 60.0f) ? 0x2E7D32 : bt.glyph; // tea is green
            if (!soft) {
                lv_obj_set_style_text_color(p.play, col(rs ? bt.button : glyph), LV_PART_MAIN);
            }
            lv_obj_invalidate(p.start);
        }
        if (v.active) {
            hide(p.play, true);
            hide(p.stop, false);
        } else {
            hide(p.play, false);
            hide(p.stop, true);
        }
    }
    if (p.flush != nullptr) {
        if (v.active || v.hold) {
            hide(p.flush, true);
        } else {
            hide(p.flush, false);
        }
    }
    if (p.multi != nullptr) {
        hide(p.multi, v.active || v.hold);
        const int ms = g_cups;
        if (ms != p.multiState) {
            p.multiState = ms;
            const Theme &mt = THEMES[JM_BREW];
            const bool on = ms > 1;
            lv_obj_set_style_bg_opa(p.multi, on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_img_recolor(p.multiGlyph, col(on ? mt.glyph : mt.text), LV_PART_MAIN);
            lv_obj_set_style_text_color(p.multiLabel, col(on ? mt.glyph : mt.text), LV_PART_MAIN);
            char nb[8];
            snprintf(nb, sizeof(nb), "%d", ms);
            setText(p.multiLabel, nb);
            lv_obj_invalidate(p.multi);
        }
    }
    if (p.confirm != nullptr) {
        if (v.confirmVisible) {
            setText(p.confirmLabel, v.confirmText);
            hide(p.confirm, false);
        } else {
            hide(p.confirm, true);
        }
    }
}
