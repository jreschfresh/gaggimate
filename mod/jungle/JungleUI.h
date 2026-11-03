#ifndef JUNGLEUI_H
#define JUNGLEUI_H

#include <lvgl.h>

// "Jungle" look: reduced Brew / Water / Steam / Grind screens. The background is filled from the bottom,
// one step per degree, and the screens are changed by swiping left / right.

constexpr int SHOT_STEPS = 20;

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
    bool shot = false;       // brew screen: a shot is running, the coffee colours rise with the progress
    float shotFrac = 0.0f;   // 0..1 progress of the shot (grams / target grams, or time)
    char timeText[12] = ""; // brew screen: elapsed time of the shot
    bool hold = false;       // the shot just ended: show the final amount for a moment, no buttons
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
    void screenLoaded(); // called by the LVGL load events

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
        lv_obj_t *stripes[SHOT_STEPS] = {nullptr}; // brew screen: coffee-coloured steps, one per 1/20 of the shot
        lv_obj_t *time = nullptr;
        bool steamReached = false; // steam screen: the target temperature was reached once -> pressure view
        int steamBarMode = -1;
        int fillTargetH = -1;
        int shotSteps = -1;
        int shotMask = -1;
        int fillHeight = -1;
        int coverMask = -1; // steam screen: which texts sit on the colour (dark) or on black (light)
    };
    Page pages[JM_COUNT];
    int shownMode = -1;
    bool stopRequested = false;
    bool loadPending = false;
    uint32_t loadStarted = 0;

    void build(int mode);
    void setFill(Page &p, int height);
    void setFillAnimated(Page &p, int height);
    static void fillAnimCb(void *var, int32_t v);
};

#endif
