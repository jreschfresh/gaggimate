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


def sub_alt(text, olds, make, label):
    """Try several anchors (different upstream versions); make(old) returns the replacement."""
    for old in olds:
        if old in text:
            return text.replace(old, make(old), 1)
    missing.append(label)
    return text


# ---- DefaultUI.h
h = sub(h, "#include <display/ui/default/eez/structs.h>\n",
        "#include <display/ui/default/eez/structs.h>\n#include <display/ui/default/jungle/JungleUI.h>\n", "h:include")
h = sub_alt(h, ["    void updateState();\n    bool isProShot();", "    void updateState();\n    void updateSystemStatus();"], lambda old: """    void updateState();
    // Jungle look (reduced Brew / Water / Steam / Grind screens, see jungle/JungleUI.h)
    void updateJungle();
    ScreensEnum activeScreen() const;
    JungleUI jungle;
    bool jungleShown = false;
    ScreensEnum jungleScreen = ScreensEnum::SCREEN_ID_BREW_SCREEN;
    volatile bool brewEndPending = false;
    unsigned long brewEndAt = 0;
""" + old[len("    void updateState();\n"):], "h:members")

# ---- DefaultUI.cpp
c = sub_alt(c, ["#include <display/ui/default/eez/images.h>\n", "#include <display/ui/default/eez/ui.h>\n"],
            lambda old: "#include <display/ui/default/eez/actions.h>\n" + old, "cpp:include")
# state of the steam-screen cool-down and the water-screen tea preset
c = sub_alt(c, ["#include <display/ui/default/eez/ui.h>\n", "#include <display/ui/default/eez/images.h>\n"],
            lambda old: old + """
static bool g_coolMode = false;    // steam screen: cool-down through the steam wand is on
static float g_coolStart = 0.0f;   // temperature when it was switched on
static unsigned long g_coolPumpAt = 0; // when the 5 s cooling pump run started (0 = not yet)
static bool g_coolPumpDone = false;    // the 5 s pump run is over: only the open steam wand cools now
static int g_savedWaterTemp = 25;  // water target to restore afterwards
static bool g_teaUsed = false;     // water screen: 80 degree preset was chosen
""", "cpp:statics")
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
            brewEndAt = ::millis();
            brewEndPending = true;
            rerender = true;
        }
    });
    pluginManager->on("controller:brew:clear",''', "cpp:brewend")
c = sub(c, """        handleScreenChange();
        currentScreen = static_cast<ScreensEnum>(eez_flow_get_current_screen());""", """        if (brewEndPending) {
            // The final amount stays on screen for 2 seconds, then it slides on to Steam.
            const ScreensEnum shown = activeScreen();
            if (!(jungleShown && (shown == SCREEN_ID_STATUS_SCREEN || shown == SCREEN_ID_BREW_SCREEN))) {
                brewEndPending = false;
            } else if (::millis() - brewEndAt >= 2000) {
                brewEndPending = false;
                controller->clear();
                action_on_steam_screen(nullptr);
            }
        }
        // Steam screen: snowflake = cool down with water through the steam wand until the brew temperature is reached.
        if (jungleShown && jungleScreen == SCREEN_ID_STEAM_SCREEN) {
            if (jungle.consumeCoolToggle()) {
                if (g_coolMode) {
                    g_coolMode = false;
                    controller->getSettings().setTargetWaterTemp(g_savedWaterTemp);
                    controller->deactivate();
                    action_on_steam_screen(nullptr); // back to normal steam
                } else {
                    g_coolMode = true;
                    g_coolPumpAt = 0;
                    g_coolPumpDone = false;
                    g_coolStart = controller->getCurrentTemp();
                    g_savedWaterTemp = controller->getSettings().getTargetWaterTemp();
                    controller->deactivate();
                    controller->setMode(MODE_WATER);
                    controller->setTargetTemp(25.0f); // heater off, the pump pushes cold water into the boiler
                    rerender = true;
                }
            }
        } else {
            jungle.consumeCoolToggle();
        }
        if (g_coolMode) {
            const bool onSteam = jungleShown && jungleScreen == SCREEN_ID_STEAM_SCREEN;
            if (!onSteam) { // the steam screen was left: back to normal, the next steam is a regular one
                g_coolMode = false;
                controller->getSettings().setTargetWaterTemp(g_savedWaterTemp);
            } else if (controller->getCurrentTemp() > profileManager->getSelectedProfile().temperature + 1.0f &&
                       controller->getMode() == MODE_WATER && !g_coolPumpDone && g_coolPumpAt == 0 && !controller->isActive()) {
                controller->activate(); // the short steam-fill pump run ends first, then the cooling pump runs for 5 s
                g_coolPumpAt = ::millis();
            } else if (g_coolPumpAt != 0 && !g_coolPumpDone && ::millis() - g_coolPumpAt >= 5000) {
                g_coolPumpDone = true; // 5 s of cold water are in, the rest cools through the open steam wand
                controller->deactivate();
            } else if (controller->getCurrentTemp() <= profileManager->getSelectedProfile().temperature + 1.0f) {
                g_coolMode = false;
                controller->getSettings().setTargetWaterTemp(g_savedWaterTemp);
                controller->deactivate();
                action_on_brew_screen(nullptr); // cool enough: on to brewing
            }
        }
        // Water screen: 25 / 80 degree presets (80 = tea). Leaving the water screen sets 25 again.
        {
            const bool onWater = jungleShown && jungleScreen == SCREEN_ID_WATER_SCREEN;
            const int preset = jungle.consumeWaterPreset();
            if (preset > 0 && onWater) {
                controller->setTargetTemp(static_cast<float>(preset));
                g_teaUsed = preset != 25;
            }
            if (!onWater && g_teaUsed && !g_coolMode) {
                controller->getSettings().setTargetWaterTemp(25);
                g_teaUsed = false;
            }
        }
        handleScreenChange();
        currentScreen = activeScreen();
        updateJungle();""", "cpp:loop")
c = sub(c, "    if (rerender) {\n        rerender = false;\n        lastRender = now;",
        "    if ((brewEndPending || (jungleShown && jungleScreen == SCREEN_ID_STEAM_SCREEN)) && diff > RERENDER_INTERVAL_ACTIVE) {\n        rerender = true;\n    }\n\n    if (rerender) {\n        rerender = false;\n        lastRender = now;", "cpp:holdtick")
c = sub_alt(c, ["    ui_init();\n    gaugeSetpointsInside", "    ui_init();\n    setupState();"],
            lambda old: "    ui_init();\n    jungle.init();\n" + old[len("    ui_init();\n"):], "cpp:setup")
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
c = sub(c, "        case MODE_WATER:\n            changeScreen(SCREEN_ID_WATER_SCREEN);\n            break;",
        "        case MODE_WATER:\n            if (!g_coolMode) {\n                changeScreen(SCREEN_ID_WATER_SCREEN);\n            }\n            break;", "cpp:watermode")
# optional: only newer upstream versions animate the gauge ticks here
c = c.replace("if (currentScreen == SCREEN_ID_STATUS_SCREEN && currentScreen == targetScreen)",
              "if (!jungleShown && currentScreen == SCREEN_ID_STATUS_SCREEN && currentScreen == targetScreen)", 1)
c = sub_alt(c, ["// The chart needs pressure data, so it only applies to Pro profiles", "void DefaultUI::handleScreenChange() {"], lambda old: '''ScreensEnum DefaultUI::activeScreen() const {
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
        v.hold = brewEndPending;
        v.shot = v.active;
        v.shotFrac = brewProcess.elapsed_percentage() / 100.0f;
        strlcpy(v.timeText, brewProcess.elapsed_time(), sizeof(v.timeText));
        if (v.showWeight && brewProcess.profile_target_weight() > 0.0f) {
            v.shotFrac = v.weight / brewProcess.profile_target_weight();
        }
        {
            // Ready = brew temperature held within +-0.5 C for 1 minute (restarts when it leaves the band or the screen was away).
            static unsigned long readySince = 0, readyLast = 0;
            const unsigned long nowMs = ::millis();
            if (nowMs - readyLast > 6000UL) {
                readySince = 0;
            }
            readyLast = nowMs;
            const float dev = v.temp > v.fillTarget ? v.temp - v.fillTarget : v.fillTarget - v.temp;
            if (dev <= 0.5f && controller->getMode() == MODE_BREW) {
                if (readySince == 0) {
                    readySince = nowMs;
                }
            } else {
                readySince = 0;
            }
            v.ready = readySince != 0 && nowMs - readySince >= 60000UL;
        }
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
        v.cool = g_coolMode;
        v.coolStart = g_coolStart;
        v.coolTarget = profileManager->getSelectedProfile().temperature;
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

''' + old, "cpp:funcs")

# upstream differences between versions: weight member name, dual boiler support
if "double activeWeight" not in h:
    c = c.replace("activeWeight", "bluetoothWeight")
if "int dualBoiler" not in h:
    c = c.replace("        if (dualBoiler) {\n            v.temp = controller->getCurrentSteamTemp();\n            v.fillTarget = controller->getTargetSteamTemp();\n        } else {\n            v.fillTarget = controller->getTargetTemp();\n        }\n",
                  "        v.fillTarget = controller->getTargetTemp();\n")

# ---- Plus Jakarta Sans instead of the built-in Montserrat in the EEZ screens (optional)
import re
screens = os.path.join(ui, "eez", "screens.c")
sc = None
if os.path.exists(screens):
    sc = open(screens).read()
    sc2 = re.sub(r"&lv_font_montserrat_(14|16|18|20|24|34)\b", r"&pj_font_\1", sc)
    if sc2 != sc:
        k = sc2.find("#include")
        sc = sc2[:k] + "#include <display/ui/default/jungle/pj_fonts.h>\n" + sc2[k:]
    else:
        sc = None

# ---- Controller tweaks (optional, each one is skipped when its anchor is not found)
#  1. Steam heats the last degrees faster: stronger proportional gain in steam mode only.
#  2. After more than 1 h of standby (or a fresh boot) a 3 s flush runs once when the brew temperature is reached.
ctl = os.path.join(repo, "src/display/core/Controller.cpp")
ctl_txt = None
if os.path.exists(ctl):
    t = open(ctl).read()
    done = []

    def ctl_sub(old, new, label):
        global t
        if old in t and new not in t:
            t = t.replace(old, new, 1)
            done.append(label)

    ctl_sub("void Controller::loopLogic() {\n",
            "// [MOD] windup guard: the integral term piles up during a long warm-up and keeps the heater on far past the target.\n"
            "// Below (target - 12 C) the integral is switched off (changing the gains resets it on the controller board).\n"
            "float g_pidKiScale = 2.0f;\n\nvoid Controller::loopLogic() {\n", "windup flag")
    ctl_sub("    // Check if steam is ready\n",
            "    { // [MOD] windup guard\n"
            "        float scale = g_pidKiScale;\n"
            "        if (mode != MODE_BREW || isActive() || currentTemp >= getTargetTemp() - 12.f) {\n"
            "            scale = 2.0f;\n"
            "        } else if (currentTemp > 5.f) {\n"
            "            scale = 0.0f;\n"
            "        }\n"
            "        if (scale != g_pidKiScale) {\n"
            "            g_pidKiScale = scale;\n"
            "            setPidSettings();\n"
            "        }\n"
            "    }\n"
            "    // Check if steam is ready\n", "windup guard")
    ctl_sub("    comms.sendPidSettings(pid[0], pid[1], pid[2], pid[3]);\n",
            "    if (mode == MODE_STEAM) {\n        pid[0] *= 2.5f; // [MOD] steam Kp boost: last degrees heat up faster, overshoot matters little here\n    }\n"
            "    if (mode == MODE_BREW) {\n        pid[0] *= 5.4f; // [MOD] brew: stronger P and stronger D (anticipates the ~25 s sensor lag), I off far below the target\n        pid[2] *= 6.25f;\n        pid[1] *= g_pidKiScale;\n    }\n"
            "    comms.sendPidSettings(pid[0], pid[1], pid[2], pid[3]);\n", "steam Kp")
    if "[MOD] auto flush" not in t:
        a1 = "void Controller::loopLogic() {\n"
        a2 = "    steamReady = false;\n\n    updateLastAction();\n"
        a3 = "    const int duration = settings.getFlushDuration();\n"
        a4 = "    // Check if steam is ready\n"
        if all(x in t for x in (a1, a2, a3, a4)):
            t = t.replace(a1, """// [MOD] auto flush: after >1 h of standby (or a fresh boot) one 3 s flush runs when the brew temperature is reached.
static unsigned long g_standbySince = 0;
static bool g_autoFlushArmed = false;
static int g_flushOverrideS = 0;

""" + a1, 1)
            t = t.replace(a2, """    if (mode == MODE_STANDBY && previousMode != MODE_STANDBY) {
        g_standbySince = millis(); // [MOD] auto flush
    }
    if (previousMode == MODE_STANDBY && mode == MODE_BREW && currentTemp < 60.f && (g_standbySince == 0 || millis() - g_standbySince > 3600000UL)) {
        g_autoFlushArmed = true; // [MOD] auto flush
    } else if (mode != MODE_BREW) {
        g_autoFlushArmed = false;
    }
""" + a2, 1)
            t = t.replace(a3, "    const int duration = g_flushOverrideS > 0 ? g_flushOverrideS : settings.getFlushDuration();\n", 1)
            t = t.replace(a4, """    if (g_autoFlushArmed) { // [MOD] auto flush
        if (mode != MODE_BREW || isActive()) {
            g_autoFlushArmed = false;
        } else if (currentTemp > 30.f && currentTemp + 25.f >= getTargetTemp()) {
            g_autoFlushArmed = false;
            g_flushOverrideS = 3;
            onFlush();
            g_flushOverrideS = 0;
        }
    }

""" + a4, 1)
            done.append("auto flush")
    if done:
        ctl_txt = t
        print("Controller tweaks applied: " + ", ".join(done))

if missing:
    print("Jungle look NOT applied, anchors missing: " + ", ".join(missing))
    sys.exit(1)

dst = os.path.join(ui, "jungle")
os.makedirs(dst, exist_ok=True)
for f in os.listdir(os.path.join(mod, "jungle")):
    shutil.copy(os.path.join(mod, "jungle", f), dst)
open(hp, "w").write(h)
open(cp, "w").write(c)
if sc is not None:
    open(screens, "w").write(sc)
if ctl_txt is not None:
    open(ctl, "w").write(ctl_txt)
print("Jungle look applied.")

# ---- Flight log (optional, never fatal): black box on the machine, see mod/flightlog/FlightLog.cpp
try:
    fl_src = os.path.join(mod, "flightlog")
    pdir = os.path.join(repo, "src/display/plugins")
    ctl_p = os.path.join(repo, "src/display/core/Controller.cpp")
    web_p = os.path.join(pdir, "WebUIPlugin.cpp")
    ct = open(ctl_p).read()
    wt = open(web_p).read()
    a_reg = "    pluginManager->registerPlugin(&ShotHistory);\n"
    a_route = '    server.on("/api/scales/list",'
    if os.path.isdir(fl_src) and a_reg in ct and a_route in wt and "FlightLog" not in ct:
        for f in ("FlightLog.h", "FlightLog.cpp"):
            shutil.copy(os.path.join(fl_src, f), pdir)
        ct = ct.replace(a_reg, a_reg + "    pluginManager->registerPlugin(&FlightLog); // [MOD] flight log\n", 1)
        ct = ct.replace("#include", "#include <display/plugins/FlightLog.h>\n#include", 1)
        wt = wt.replace(a_route, "    FlightLog.registerRoutes(server); // [MOD] flight log\n" + a_route, 1)
        wt = wt.replace("#include", "#include <display/plugins/FlightLog.h>\n#include", 1)
        open(ctl_p, "w").write(ct)
        open(web_p, "w").write(wt)
        print("Flight log applied.")
    else:
        print("Flight log skipped (anchors missing or already applied).")
except Exception as e:
    print("Flight log skipped: %s" % e)
