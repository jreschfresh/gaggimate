#!/usr/bin/env python3
"""Adds the "Jungle" display look (reduced Brew / Water / Steam / Grind screens, swipe between modes,
auto-switch to Steam after a shot) to an upstream GaggiMate checkout.

usage: apply_jungle.py <upstream-checkout> <mod-dir>

Works with exact-text anchors in DefaultUI.h / DefaultUI.cpp. If an anchor is missing (upstream moved the
code) nothing is written and the script exits with 1, so the build can carry on without the Jungle look.
"""
import os
import shutil
import sys

repo, mod = sys.argv[1], sys.argv[2]
ui = os.path.join(repo, "src/display/ui/default")
hp, cp = os.path.join(ui, "DefaultUI.h"), os.path.join(ui, "DefaultUI.cpp")
h, c = open(hp).read(), open(cp).read()
missing = []


def sub(text, old, new, label):
    if old not in text:
        missing.append(label)
        return text
    return text.replace(old, new, 1)


# ---- DefaultUI.h
h = sub(h, "#include <display/ui/default/eez/structs.h>\n",
        "#include <display/ui/default/eez/structs.h>\n#include <display/ui/default/jungle/JungleUI.h>\n", "h:include")
h = sub(h, "    void updateState();\n    bool isProShot();", """    void updateState();
    // Jungle look (reduced Brew / Water / Steam / Grind screens, see jungle/JungleUI.h)
    void updateJungle();
    ScreensEnum activeScreen() const;
    JungleUI jungle;
    bool jungleShown = false;
    ScreensEnum jungleScreen = ScreensEnum::SCREEN_ID_BREW_SCREEN;
    volatile bool brewEndPending = false;
    bool isProShot();""", "h:members")

# ---- DefaultUI.cpp
c = sub(c, "#include <display/ui/default/eez/images.h>\n",
        "#include <display/ui/default/eez/actions.h>\n#include <display/ui/default/eez/images.h>\n", "cpp:include")
c = sub(c, "if (eez_flow_get_current_screen() == SCREEN_ID_STATUS_SCREEN) {",
        "if (activeScreen() == SCREEN_ID_STATUS_SCREEN) {", "cpp:status")
c = sub(c, "if (eez_flow_get_current_screen() != SCREEN_ID_BREW_SCREEN)",
        "if (activeScreen() != SCREEN_ID_BREW_SCREEN)", "cpp:brew")
c = sub(c, "if (eez_flow_get_current_screen() == SCREEN_ID_STANDBY_SCREEN && !controller",
        "if (activeScreen() == SCREEN_ID_STANDBY_SCREEN && !controller", "cpp:standby")
c = sub(c, '    pluginManager->on("controller:brew:clear",', '''    // After a shot the Jungle UI jumps on to Steam (a manual stop and flushes are left alone).
    pluginManager->on("controller:brew:end", [this](Event const &) {
        if (jungle.consumeStopRequested()) {
            return;
        }
        std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
        if (controller->getLastProcess() != nullptr) { // a flush clears itself before the event arrives
            brewEndPending = true;
            rerender = true;
        }
    });
    pluginManager->on("controller:brew:clear",''', "cpp:brewend")
c = sub(c, """        handleScreenChange();
        currentScreen = static_cast<ScreensEnum>(eez_flow_get_current_screen());""", """        if (brewEndPending) {
            brewEndPending = false;
            const ScreensEnum shown = activeScreen();
            if (jungleShown && (shown == SCREEN_ID_STATUS_SCREEN || shown == SCREEN_ID_BREW_SCREEN)) {
                controller->clear();
                action_on_steam_screen(nullptr);
            }
        }
        handleScreenChange();
        currentScreen = activeScreen();
        updateJungle();""", "cpp:loop")
c = sub(c, "    ui_init();\n    gaugeSetpointsInside", "    ui_init();\n    jungle.init();\n    gaugeSetpointsInside", "cpp:setup")
c = sub(c, """        eez_flow_set_screen(targetScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0);
""", """        const bool toJungle = targetScreen == SCREEN_ID_BREW_SCREEN || targetScreen == SCREEN_ID_STATUS_SCREEN ||
                              targetScreen == SCREEN_ID_STEAM_SCREEN || targetScreen == SCREEN_ID_WATER_SCREEN ||
                              targetScreen == SCREEN_ID_GRIND_SCREEN;
        if (toJungle) {
            int jungleMode = JM_BREW;
            if (targetScreen == SCREEN_ID_STEAM_SCREEN)
                jungleMode = JM_STEAM;
            else if (targetScreen == SCREEN_ID_WATER_SCREEN)
                jungleMode = JM_WATER;
            else if (targetScreen == SCREEN_ID_GRIND_SCREEN)
                jungleMode = JM_GRIND;
            jungleShown = true;
            jungleScreen = targetScreen;
            jungle.grindAvailable = grindAvailable;
            jungle.show(jungleMode);
            rerender = true;
            return;
        }
        jungleShown = false;
        eez_flow_set_screen(targetScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0);
""", "cpp:screenchange")
c = sub(c, "if (currentScreen == SCREEN_ID_STATUS_SCREEN && currentScreen == targetScreen)",
        "if (!jungleShown && currentScreen == SCREEN_ID_STATUS_SCREEN && currentScreen == targetScreen)", "cpp:tick")
c = sub(c, "// The chart needs pressure data, so it only applies to Pro profiles", '''ScreensEnum DefaultUI::activeScreen() const {
    if (jungleShown && jungle.isShown()) {
        return jungleScreen;
    }
    return static_cast<ScreensEnum>(eez_flow_get_current_screen());
}

void DefaultUI::updateJungle() {
    if (!jungleShown || !jungle.isShown()) {
        jungleShown = false;
        return;
    }
    const ::Settings &settings = controller->getSettings();
    JungleView v;
    v.mode = JM_BREW;
    if (jungleScreen == SCREEN_ID_STEAM_SCREEN)
        v.mode = JM_STEAM;
    else if (jungleScreen == SCREEN_ID_WATER_SCREEN)
        v.mode = JM_WATER;
    else if (jungleScreen == SCREEN_ID_GRIND_SCREEN)
        v.mode = JM_GRIND;
    v.pressure = pressure;
    v.pressureAvailable = pressureAvailable;
    v.active = v.mode == JM_GRIND ? controller->isGrindActive() : controller->isActive();
    v.grindAvailable = grindAvailable;
    v.temp = controller->getCurrentTemp();
    char buf[16];
    switch (v.mode) {
    case JM_BREW:
        v.fillTarget = profileManager->getSelectedProfile().temperature;
        v.showWeight = controller->isVolumetricAvailable();
        v.weight = static_cast<float>(activeWeight);
        break;
    case JM_WATER:
        v.fillTarget = controller->getTargetTemp();
        snprintf(buf, sizeof(buf), "%d\\xC2\\xB0", static_cast<int>(controller->getTargetTemp()));
        strlcpy(v.centerText, buf, sizeof(v.centerText));
        break;
    case JM_STEAM:
        if (dualBoiler) {
            v.temp = controller->getCurrentSteamTemp();
            v.fillTarget = controller->getTargetSteamTemp();
        } else {
            v.fillTarget = controller->getTargetTemp();
        }
        snprintf(buf, sizeof(buf), "%.1f", pressure);
        strlcpy(v.centerText, buf, sizeof(v.centerText));
        v.showCenter = pressureAvailable;
        break;
    default:
        v.fillTarget = 0.0f;
        if (controller->isVolumetricAvailable() && settings.isVolumetricTarget()) {
            snprintf(buf, sizeof(buf), "%.1f", settings.getTargetGrindVolume());
        } else {
            formatDuration(settings.getTargetGrindDuration(), buf, sizeof(buf));
        }
        strlcpy(v.centerText, buf, sizeof(v.centerText));
        break;
    }
    v.confirmVisible = brewConfirmVisible && v.mode == JM_BREW;
    if (v.confirmVisible) {
        const String labels = controller->getWarnings().getLabels();
        strlcpy(v.confirmText, labels.c_str(), sizeof(v.confirmText));
    }
    jungle.update(v);
}

// The chart needs pressure data, so it only applies to Pro profiles''', "cpp:funcs")

if missing:
    print("Jungle look NOT applied, anchors missing: " + ", ".join(missing))
    sys.exit(1)

dst = os.path.join(ui, "jungle")
os.makedirs(dst, exist_ok=True)
for f in os.listdir(os.path.join(mod, "jungle")):
    shutil.copy(os.path.join(mod, "jungle", f), dst)
open(hp, "w").write(h)
open(cp, "w").write(c)
print("Jungle look applied.")
