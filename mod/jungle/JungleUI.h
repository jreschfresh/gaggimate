#ifndef JUNGLEUI_H
#define JUNGLEUI_H

#include <lvgl.h>

// "Jungle" look: reduced Brew / Water / Steam / Grind screens. The background is filled from the bottom,
// one step per degree, and the screens are changed by swiping left / right.

enum JungleMode { JM_BREW = 0, JM_WATER = 1, JM_STEAM = 2, JM_GRIND = 3, JM_COUNT = 4 };

struct JungleView {
    int mode = JM_BREW;
    float temp = 0.0f;       // current temperature in °C
    float fillTarget = 0.0f; // the background is full at this temperature
    float pressure = 0.0f;
    bool pressureAvailable = false;
    bool active = false; // a process runs (shows the stop button)
    bool showWeight = false;
    float weight = 0.0f;
    char centerText[16] = "";
    bool showCenter = true;
    bool grindAvailable = false;
    bool confirmVisible = false;
    char confirmText[96] = "";
};

class JungleUI {
  public:
    void init();
    // Loads the screen of the given mode (no-op when already shown).
    void show(int mode);
    // True while one of the Jungle screens is the active LVGL screen.
    bool isShown() const;
    void update(const JungleView &view);
    // Set by the stop button, so the UI can tell a manual stop from a finished shot.
    bool consumeStopRequested();
    void requestStop() { stopRequested = true; }
    bool grindAvailable = false;

  private:
    struct Page {
        lv_obj_t *scr = nullptr;
        lv_obj_t *clip = nullptr;
        lv_obj_t *grad = nullptr;
        lv_obj_t *temp = nullptr;
        lv_obj_t *bar = nullptr;
        lv_obj_t *center = nullptr;
        lv_obj_t *minus = nullptr;
        lv_obj_t *plus = nullptr;
        lv_obj_t *start = nullptr;
        lv_obj_t *play = nullptr;
        lv_obj_t *stop = nullptr;
        lv_obj_t *flush = nullptr;
        lv_obj_t *confirm = nullptr;
        lv_obj_t *confirmLabel = nullptr;
        int fillHeight = -1;
        int coverMask = -1; // steam screen: which texts sit on the colour (dark) or on black (light)
    };
    Page pages[JM_COUNT];
    int shownMode = -1;
    bool stopRequested = false;

    void build(int mode);
    void setFill(Page &p, int height);
};

#endif
