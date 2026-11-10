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
    bool cool = false;       // steam screen: cool-down mode (water through the steam wand) is on
    float coolStart = 0.0f;  // temperature when the cool-down was switched on
    float coolTarget = 0.0f; // brew temperature the cool-down aims for
    bool ready = true;       // brew screen: temperature held within +-0.5 C for 1 min (start button is drawn as an empty ring until then)
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
    // Snowflake on the steam screen / 25 or 80 degree buttons on the water screen.
    bool consumeCoolToggle();
    int consumeWaterPreset(); // 0 = nothing pressed, otherwise the temperature
    void requestCoolToggle() { coolToggle = true; }
    void requestWaterPreset(int t) { waterPreset = t; }
    void requestStop() { stopRequested = true; }
    bool grindAvailable = false;
    void screenLoaded(); // called by the LVGL load events
    // Optional animations, bit mask: 1 = start button fills softly when ready, 2 = coffee rises smoothly, 4 = steam wisps.
    void setEffects(int fx);

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
        lv_obj_t *multi = nullptr;      // brew screen: two cups (several brews planned)
        lv_obj_t *multiGlyph = nullptr;
        lv_obj_t *multiLabel = nullptr; // count of finished brews
        int multiState = -1;
        lv_obj_t *confirm = nullptr;
        lv_obj_t *confirmLabel = nullptr;
        lv_obj_t *coffee = nullptr; // brew screen: the coffee colours that rise with the shot (drawn up to coffeeH)
        int coffeeH = 0;
        int coffeeTargetH = -1;
        lv_obj_t *wisps = nullptr; // steam screen: wisps rising from the surface of the fill
        lv_obj_t *time = nullptr;
        lv_obj_t *snow = nullptr;      // steam screen: snowflake button
        lv_obj_t *snowGlyph = nullptr;
        lv_obj_t *preset[2] = {nullptr, nullptr}; // water screen: 25 / 80 degree buttons
        lv_obj_t *presetLabel[2] = {nullptr, nullptr};
        int coolState = -1;
        int readyState = -1;
        int presetState = -1;
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
    bool coolToggle = false;
    int waterPreset = 0;
    bool loadPending = false;
    uint32_t loadStarted = 0;

    void build(int mode);
    void setFill(Page &p, int height);
    void setFillAnimated(Page &p, int height);
    static void fillAnimCb(void *var, int32_t v);
    void setCoffee(Page &p, int height);
    static void coffeeAnimCb(void *var, int32_t v);
    static void readyAnimCb(void *var, int32_t v);
};

#endif
