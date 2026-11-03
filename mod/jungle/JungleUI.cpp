#include "JungleUI.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <display/ui/default/eez/actions.h>

LV_FONT_DECLARE(jungle_font_104)
LV_FONT_DECLARE(jungle_font_80)
LV_FONT_DECLARE(jungle_font_22)

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
    {0xEAD9B8, 0xB9976A, 0x3A2410, 0x3A2410, 0xEAD9B8}, // steam: beige
    {0x7CC47F, 0x1F6B3A, 0xFFEEDE, 0xFFEEDE, 0x1F6B3A}, // grind: green
};

// Screen order for swiping, left to right.
const int ORDER[JM_COUNT] = {JM_WATER, JM_BREW, JM_STEAM, JM_GRIND};

JungleUI *g_ui = nullptr;
bool g_active[JM_COUNT] = {false};

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

void playGlyphDraw(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_obj_get_style_text_color(o, LV_PART_MAIN);
    d.bg_opa = LV_OPA_COVER;
    d.radius = 0;
    d.border_width = 0;
    // Triangle in a 48 x 48 box, pointing right.
    lv_point_t p[3] = {{(lv_coord_t)(a.x1 + 12), (lv_coord_t)(a.y1 + 5)},
                       {(lv_coord_t)(a.x1 + 42), (lv_coord_t)(a.y1 + 24)},
                       {(lv_coord_t)(a.x1 + 12), (lv_coord_t)(a.y1 + 43)}};
    lv_draw_polygon(ctx, &d, p, 3);
}

void mkBar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *b = plain(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 2, LV_PART_MAIN);
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
    lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_60, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 15);
    const int c = SMALL_BTN / 2;
    mkBar(b, c - 10 - 2, c - 1 - 2, 20, 3, color);
    if (plus) {
        mkBar(b, c - 1 - 2, c - 10 - 2, 3, 20, color);
    }
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
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT && dir != LV_DIR_TOP) {
        return;
    }
    lv_indev_wait_release(indev);
    if (g_active[mode]) {
        return; // never leave a running process by accident
    }
    if (dir == LV_DIR_TOP) {
        action_on_menu_click(e);
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
    lv_obj_set_style_bg_opa(p.grad, LV_OPA_COVER, LV_PART_MAIN);
    setFill(p, SIZE);

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
        lv_obj_add_flag(p.center, LV_OBJ_FLAG_CLICKABLE); // long press tares the scale
        lv_obj_add_event_cb(p.center, tareCb, LV_EVENT_LONG_PRESSED, nullptr);
    }

    if (mode != JM_BREW) {
        p.minus = roundButton(p.scr, 48, 187, t.text, false);
        p.plus = roundButton(p.scr, 376, 187, t.text, true);
        lv_obj_add_event_cb(p.minus, minusCb, LV_EVENT_CLICKED, ud);
        lv_obj_add_event_cb(p.plus, plusCb, LV_EVENT_CLICKED, ud);
        if (mode == JM_STEAM) { // the dark backing would look muddy on the beige
            lv_obj_set_style_bg_opa(p.minus, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(p.plus, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(p.minus, LV_OPA_20, LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(p.plus, LV_OPA_20, LV_PART_MAIN | LV_STATE_PRESSED);
        }
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
        lv_obj_set_style_radius(p.stop, 5, LV_PART_MAIN);
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
        lv_obj_set_style_bg_opa(p.flush, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.flush, LV_OPA_60, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(p.flush, 15);
        lv_obj_add_event_cb(p.flush, flushCb, LV_EVENT_PRESSED, nullptr);
        lv_obj_t *drop = label(p.flush, &lv_font_montserrat_24, t.text);
        lv_label_set_text(drop, LV_SYMBOL_TINT);
        lv_obj_center(drop);

        // Warning confirmation (the controller asks before it starts a brew with an error-level warning).
        p.confirm = plain(p.scr);
        lv_obj_set_size(p.confirm, SIZE, SIZE);
        lv_obj_add_flag(p.confirm, LV_OBJ_FLAG_CLICKABLE); // swallows touches behind it
        lv_obj_set_style_bg_color(p.confirm, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(p.confirm, LV_OPA_90, LV_PART_MAIN);
        p.confirmLabel = label(p.confirm, &lv_font_montserrat_24, 0xFFEEDE);
        lv_obj_set_size(p.confirmLabel, 320, LV_SIZE_CONTENT);
        lv_label_set_long_mode(p.confirmLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(p.confirmLabel, 80, 120);
        const int ids[2] = {0, 1};
        for (int i : ids) {
            lv_obj_t *b = roundButton(p.confirm, i == 0 ? 130 : 294, 330, 0xFFEEDE, i == 1);
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

void JungleUI::show(int mode) {
    if (mode < 0 || mode >= JM_COUNT || pages[mode].scr == nullptr) {
        mode = JM_BREW;
    }
    if (shownMode == mode && lv_scr_act() == pages[mode].scr) {
        return;
    }
    shownMode = mode;
    lv_scr_load(pages[mode].scr);
}

bool JungleUI::isShown() const {
    if (shownMode < 0) {
        return false;
    }
    return lv_scr_act() == pages[shownMode].scr;
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
    g_active[v.mode] = v.active;
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
    setFill(p, static_cast<int>(lroundf(frac * SIZE)));

    if (v.mode == JM_STEAM) {
        // The beige steam colour needs dark text, but the part of the screen that is still black needs light text.
        const int topY = SIZE - p.fillHeight;
        const int mask = (topY <= 30 ? 1 : 0) | (topY <= 175 ? 2 : 0) | (topY <= 190 ? 4 : 0);
        if (mask != p.coverMask) {
            p.coverMask = mask;
            const uint32_t light = 0xEAD9B8;
            const uint32_t dark = 0x3A2410;
            lv_obj_set_style_text_color(p.temp, col(mask & 1 ? dark : light), LV_PART_MAIN);
            lv_obj_set_style_text_color(p.center, col(mask & 2 ? dark : light), LV_PART_MAIN);
            recolorButton(p.minus, mask & 4 ? dark : light);
            recolorButton(p.plus, mask & 4 ? dark : light);
        }
    }

    char buf[32];
    if (v.pressureAvailable && v.mode != JM_STEAM) {
        snprintf(buf, sizeof(buf), "%d \xC2\xB0""C", deg);
        setText(p.temp, buf);
        lv_obj_set_x(p.temp, 130);
        snprintf(buf, sizeof(buf), "%.1f bar", v.pressure);
        setText(p.bar, buf);
        hide(p.bar, false);
    } else {
        snprintf(buf, sizeof(buf), "%d \xC2\xB0""C", deg);
        setText(p.temp, buf);
        lv_obj_set_x(p.temp, 180); // centred when it stands alone
        hide(p.bar, true);
    }

    if (v.mode == JM_BREW) {
        if (v.showWeight) {
            snprintf(buf, sizeof(buf), "%.1f", v.weight);
            setText(p.center, buf);
            hide(p.center, false);
        } else {
            hide(p.center, true);
        }
    } else {
        setText(p.center, v.centerText);
        if (v.showCenter) {
            hide(p.center, false);
        } else {
            hide(p.center, true);
        }
    }

    if (p.start != nullptr) {
        if (v.active) {
            hide(p.play, true);
            hide(p.stop, false);
        } else {
            hide(p.play, false);
            hide(p.stop, true);
        }
    }
    if (p.flush != nullptr) {
        if (v.active) {
            hide(p.flush, true);
        } else {
            hide(p.flush, false);
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
