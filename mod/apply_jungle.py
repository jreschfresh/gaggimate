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
#include <display/ui/default/jungle/TrueOut.h>
static TrueOut g_out;              // true amount of the running / last shot (drops after the stop included)
static bool g_shotRunning = false; // a real shot (no flush) is running
static bool g_shotValid = false;   // g_shotMs / g_shotGrams hold the result of the last shot
static bool g_shotHasWeight = false;
static unsigned long g_shotStartMs = 0, g_shotEndMs = 0, g_shotMs = 0, g_shotTickAt = 0;
static float g_shotGrams = 0.0f;
""", "cpp:statics")
# Steam: standby by itself once the milk is steamed and the valve is closed again (optional, never fatal).
# The recognition lives in jungle/SteamWatch.h and is tested against recorded sessions (mod/tools/steamwatch_test.cpp).
_sw_anchor = "void DefaultUI::loop() {\n    const unsigned long now = ::millis();\n    const unsigned long diff = now - lastRender;\n"
if _sw_anchor in c and "static float g_shotGrams = 0.0f;\n" in c:
    c = c.replace("static float g_shotGrams = 0.0f;\n",
                  "static float g_shotGrams = 0.0f;\n"
                  "#include <display/ui/default/jungle/SteamWatch.h>\n"
                  "static SteamWatch g_steamWatch;    // steam mode: recognises \"milk steamed, valve closed again\" from the pressure\n"
                  "static unsigned long g_steamWatchAt = 0;\n", 1)
    c = c.replace(_sw_anchor, _sw_anchor + """
    // [MOD] Steam: one pressure sample per second goes to SteamWatch; 5 s after the valve is closed again the machine
    // goes to standby. A short purge before steaming is ignored. Not during the cool-down through the wand.
    if (controller->getMode() != MODE_STEAM || g_coolMode || !pressureAvailable) {
        if (g_steamWatch.t != 0) {
            g_steamWatch.reset();
        }
        g_steamWatchAt = now;
    } else if (now - g_steamWatchAt >= 1000) {
        g_steamWatchAt = now;
        if (g_steamWatch.feed(controller->getCurrentPressure())) {
            g_steamWatch.reset();
            controller->activateStandby();
        }
    }

    // [MOD] True shot time (start incl. pre-infusion .. pump stop) and true amount out (drops after the stop included).
    // Sampled 20 times a second; a flush is not a shot. The result is shown by updateJungle().
    if (now - g_shotTickAt >= 50) {
        g_shotTickAt = now;
        bool brewing = false;
        unsigned long started = 0, finished = 0;
        {
            std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
            Process *p = controller->getProcess();
            if (p != nullptr && p->getType() == MODE_BREW && p->isActive()) {
                auto *bp = static_cast<BrewProcess *>(p);
                if (!bp->isUtility()) {
                    brewing = true;
                    started = bp->processStarted;
                }
            } else if (g_shotRunning) {
                Process *lp = controller->getLastProcess();
                if (lp != nullptr && lp->getType() == MODE_BREW) {
                    auto *bp = static_cast<BrewProcess *>(lp);
                    if (bp->finished > 0 && bp->processStarted == g_shotStartMs) {
                        finished = bp->finished;
                    }
                }
            }
        }
        const bool scale = controller->isVolumetricAvailable();
        const float w = static_cast<float>(activeWeight);
        if (brewing) {
            if (!g_shotRunning || started != g_shotStartMs) {
                g_shotRunning = true;
                g_shotValid = false;
                g_shotStartMs = started;
                g_shotHasWeight = scale;
                g_shotGrams = 0.0f;
                g_out.reset();
            }
            g_shotMs = now - started;
            if (g_shotHasWeight && scale) {
                g_shotGrams = g_out.feed(w, false);
            }
        } else if (g_shotRunning) { // the pump has just stopped (a manual stop leaves `finished` at 0)
            g_shotRunning = false;
            g_shotEndMs = finished > 0 ? finished : now;
            if (finished > 0) {
                g_shotMs = finished - g_shotStartMs;
            }
            g_shotValid = g_shotMs >= 7500; // aborted starts are not shots
        } else if (g_shotValid) {
            if (controller->getMode() == MODE_STANDBY) {
                g_shotValid = false; // the result is gone with the next standby
            } else if (g_shotHasWeight && scale) {
                g_shotGrams = g_out.feed(w, true, now - g_shotEndMs);
            }
        }
    }
""", 1)
    print("Steam auto-standby and shot result applied.")
else:
    print("Steam auto-standby and shot result NOT applied (anchor missing).")
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
        // [MOD] True amount: follows the scale during the shot, afterwards the drops are added and the result stays
        // when the scale zeroes itself or the cup is lifted (jungle/TrueOut.h, fed in loop()).
        if (g_shotHasWeight && g_out.started && ((v.active && g_shotRunning) || (v.hold && g_shotValid))) {
            v.weight = g_shotGrams;
        }
        v.shot = v.active;
        v.shotFrac = brewProcess.elapsed_percentage() / 100.0f;
        // [MOD] True shot time: from the start (pre-infusion included) to the pump stop, tenths once it is over
        if (g_shotRunning) {
            snprintf(v.timeText, sizeof(v.timeText), "%lu s", g_shotMs / 1000UL);
        } else if (g_shotValid) {
            snprintf(v.timeText, sizeof(v.timeText), "%.1f s", static_cast<float>(g_shotMs) / 1000.0f);
        } else {
            strlcpy(v.timeText, brewProcess.elapsed_time(), sizeof(v.timeText));
        }
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
    // [MOD] The last shot stays readable: true time and true amount, on the steam screen and on the idle brew screen
    v.resultText[0] = 0;
    if (g_shotValid && !g_shotRunning && (v.mode == JM_STEAM || (v.mode == JM_BREW && !v.active && !v.hold))) {
        if (g_shotHasWeight && g_out.started) {
            snprintf(v.resultText, sizeof(v.resultText), "%.1f s    %.1f g", static_cast<float>(g_shotMs) / 1000.0f, g_shotGrams);
        } else {
            snprintf(v.resultText, sizeof(v.resultText), "%.1f s", static_cast<float>(g_shotMs) / 1000.0f);
        }
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
            "float g_pidKiScale = 2.0f;\nfloat g_pidKdScale = 2.3f;\n\nvoid Controller::loopLogic() {\n", "windup flag")
    ctl_sub("    // Check if steam is ready\n",
            "    { // [MOD] windup guard\n"
            "        float scale = g_pidKiScale;\n"
            "        if (mode != MODE_BREW || isActive() || currentTemp >= getTargetTemp() - 1.5f) {\n"
            "            scale = 2.0f;\n"
            "        } else if (currentTemp > 5.f) {\n"
            "            scale = 0.0f;\n"
            "        }\n"
            "        // [MOD] near the target the strong D term turns sensor steps into heat pulses that hold the boiler ~1 C too high\n"
            "        float kd = (mode == MODE_BREW && !isActive() && currentTemp >= getTargetTemp() - 1.5f) ? 0.3f : 2.3f;\n"
            "        if (scale != g_pidKiScale || kd != g_pidKdScale) {\n"
            "            g_pidKiScale = scale;\n"
            "            g_pidKdScale = kd;\n"
            "            setPidSettings();\n"
            "        }\n"
            "    }\n"
            "    // Check if steam is ready\n", "windup guard")
    ctl_sub("    comms.sendPidSettings(pid[0], pid[1], pid[2], pid[3]);\n",
            "    if (mode == MODE_STEAM) {\n        pid[0] *= 2.5f; // [MOD] steam Kp boost: last degrees heat up faster, overshoot matters little here\n    }\n"
            "    if (mode == MODE_BREW) {\n        pid[0] *= 3.0f; // [MOD] brew: stronger P and stronger D (anticipates the ~25 s sensor lag), I off far below the target\n        pid[2] *= g_pidKdScale;\n        pid[1] *= g_pidKiScale;\n    }\n"
            "    comms.sendPidSettings(pid[0], pid[1], pid[2], pid[3]);\n", "steam Kp")
    # [MOD] heat governor: the boiler needs ~10 s + ~33 s lag until heat shows at the sensor. The display estimates the heat
    # still on its way (from the reported heater power) and lowers the boiler setpoint a little so the PID backs off in time.
    ctl_sub('const String LOG_TAG = F("Controller");\n',
            'const String LOG_TAG = F("Controller");\n'
            "// [MOD] heat governor (values fitted on logged heat-ups: delay 10 s, lag 33 s, net gain ~0.4-0.5 C per full-power second incl. losses; fitted on 3 logged heat-ups)\n"
            "static float g_govDrop = 0.f;\n"
            "static float g_govU[10] = {0};\n"
            "static int g_govIdx = 0;\n"
            "static float g_govF = 0.f;\n"
            "static unsigned long g_govLast = 0;\n"
            "static float g_govTLast = 0.f;\n"
            "static float g_govRate = 0.f;\n"
            "static bool g_govBrake = false;\n"
            "static void govUpdate(float hpPercent, float temp, float target, bool enabled) {\n"
            "    unsigned long now = millis();\n"
            "    if (now - g_govLast < 1000UL) {\n"
            "        return;\n"
            "    }\n"
            "    g_govLast = now;\n"
            "    if (g_govTLast < 1.f) {\n"
            "        g_govTLast = temp;\n"
            "    }\n"
            "    g_govRate += ((temp - g_govTLast) - g_govRate) / 5.f;\n"
            "    g_govTLast = temp;\n"
            "    float delayed = g_govU[g_govIdx];\n"
            "    g_govU[g_govIdx] = std::max(0.f, std::min(1.f, hpPercent / 100.f));\n"
            "    g_govIdx = (g_govIdx + 1) % 10;\n"
            "    g_govF += (delayed - g_govF) / 33.f;\n"
            "    float buffered = 0.f;\n"
            "    for (int i = 0; i < 10; i++) {\n"
            "        buffered += g_govU[i];\n"
            "    }\n"
            "    float pending = 0.5f * (buffered + g_govF * 33.f);\n"
            "    float loss = 1.2f * (temp - 20.f) / 75.f;\n"
            "    float predicted = temp + pending - loss;\n"
            "    float drop = 0.f;\n"
            "    if (enabled && target > 0.f && temp > 30.f) {\n"
            "        drop = 15.f * std::max(0.f, predicted - (target - 0.4f));\n"
            "        // latch: after a fast climb the boiler coasts ~11 C further (measured on 4 heat-ups), so heating stops 12 C below the target\n"
            "        if (!g_govBrake && temp >= target - 12.f && temp < target - 3.f && g_govRate >= 0.12f) {\n"
            "            g_govBrake = true;\n"
            "        }\n"
            "        if (g_govBrake && (g_govRate < 0.04f || temp < target - 14.f || temp >= target)) {\n"
            "            g_govBrake = false;\n"
            "        }\n"
            "        if (g_govBrake) {\n"
            "            drop = 12.f;\n"
            "        }\n"
            "    } else {\n"
            "        g_govBrake = false;\n"
            "    }\n"
            "    drop = std::min(drop, 12.f);\n"
            "    g_govDrop = std::round(drop * 10.f) / 10.f;\n"
            "}\n"
            "float govDropNow() { return g_govDrop; }\n", "governor helper")
    ctl_sub("        this->currentHeaterPower = heaterPower;\n",
            "        this->currentHeaterPower = heaterPower;\n        govUpdate(heaterPower, currentTemp, getTargetTemp(), mode == MODE_BREW); // [MOD] heat governor\n",
            "governor update")
    ctl_sub("    bool altRelayActive = false;\n    if (active && proc->isAltRelayActive()) {",
            "    if (!active && mode == MODE_BREW && targetTemp > .0f && g_govDrop > 0.f) {\n"
            "        targetTemp -= g_govDrop; // [MOD] heat governor\n    }\n\n"
            "    bool altRelayActive = false;\n    if (active && proc->isAltRelayActive()) {", "governor apply")
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

# --- Standby picture: the two hands slowly move towards each other and apart again -------------------------------------
# The picture is split into a background plate (the wall without the hands) and two hand sprites (mod/standby/*.png,
# made by mod/tools/standby_layers.py). Optional: on any problem the still picture from patch.py stays as it is.
try:
    from PIL import Image

    sb = os.path.join(mod, "standby")
    scr_p = os.path.join(ui, "eez", "screens.c")
    img_p = os.path.join(ui, "eez", "images", "ui_image_standby.c")
    layers = {l.split()[0]: [int(v) for v in l.split()[1:]] for l in open(os.path.join(sb, "standby_layers.txt")) if l.strip()}

    def rgb565(px, alpha):
        rows = []
        w, h = px.size
        d = px.load()
        for y in range(h):
            r = []
            for x in range(w):
                p = d[x, y]
                v = ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3)
                r.append("0x%02x,0x%02x," % (v & 255, v >> 8) + ("0x%02x," % p[3] if alpha else ""))
            rows.append("    " + "".join(r))
        return "\n".join(rows)

    bg = Image.open(os.path.join(sb, "standby_bg.png")).convert("RGB")
    assert bg.size == (480, 480)
    bg_c = (
        '#include "lvgl.h"\n\n// Standby picture, background plate (the wall without the hands). Generated by mod/apply_jungle.py.\n'
        "#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP != 0\n#error standby picture is generated for RGB565 without byte swap\n#endif\n"
        "const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t img_standby_map[] = {\n" + rgb565(bg, False) + "\n};\n\n"
        "const lv_img_dsc_t img_standby = {\n    .header.cf = LV_IMG_CF_TRUE_COLOR,\n    .header.always_zero = 0,\n    .header.reserved = 0,\n"
        "    .header.w = 480,\n    .header.h = 480,\n    .data_size = 480 * 480 * 2,\n    .data = img_standby_map,\n};\n"
    )
    an = '#include "lvgl.h"\n\n// Standby picture: hand sprites and their slow back-and-forth. Generated by mod/apply_jungle.py.\n'
    for side in ("left", "right"):
        im = Image.open(os.path.join(sb, "standby_%s.png" % side)).convert("RGBA")
        x0, y0, w, h = layers[side]
        assert im.size == (w, h)
        an += (
            "static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t standby_%s_map[] = {\n" % side + rgb565(im, True) + "\n};\n"
            "static const lv_img_dsc_t standby_%s = {\n    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n    .header.always_zero = 0,\n"
            "    .header.reserved = 0,\n    .header.w = %d,\n    .header.h = %d,\n    .data_size = %d,\n    .data = standby_%s_map,\n};\n\n"
            % (side, w, h, w * h * 3, side)
        )
    an += """static void standby_left_x(void *obj, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)obj, (lv_coord_t)v, LV_PART_MAIN); }
static void standby_right_x(void *obj, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)obj, (lv_coord_t)-v, LV_PART_MAIN); }

static void standby_hand(lv_obj_t *parent, const lv_img_dsc_t *src, int x0, int y0, lv_anim_exec_xcb_t cb) {
    lv_obj_t *o = lv_img_create(parent);
    lv_img_set_src(o, src);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
    // same reference as the background picture: the centre of the screen
    lv_obj_align(o, LV_ALIGN_CENTER, x0 + src->header.w / 2 - 240, y0 + src->header.h / 2 - 240);
    lv_anim_t a; // the animation ends by itself when the object is deleted with its screen
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, -9, 4); // pixels towards the middle: from apart to almost touching
    lv_anim_set_time(&a, 4200);
    lv_anim_set_playback_delay(&a, 900);
    lv_anim_set_playback_time(&a, 4200);
    lv_anim_set_repeat_delay(&a, 1600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void jungle_standby_attach(lv_obj_t *parent) {
    standby_hand(parent, &standby_left, %d, %d, standby_left_x);
    standby_hand(parent, &standby_right, %d, %d, standby_right_x);
}
""" % (layers["left"][0], layers["left"][1], layers["right"][0], layers["right"][1])

    st = open(scr_p).read() if os.path.exists(scr_p) else ""
    anchor = re.search(r"lv_img_set_src\(obj, &img_standby\);\n\s*lv_img_set_zoom\(obj, 256\);\n", st)
    if anchor and "jungle_standby_attach" not in st:
        # sprites are created after the picture object is complete (next sibling = drawn right above the picture)
        end = st.index("}", anchor.end())
        st = st[:end + 1] + "\n        jungle_standby_attach(parent_obj); // [MOD] moving hands" + st[end + 1:]
        fn = "void create_screen_standby_screen()"
        st = st.replace(fn, "void jungle_standby_attach(lv_obj_t *parent); // [MOD] moving hands\n" + fn, 1)
        open(scr_p, "w").write(st)
        open(img_p, "w").write(bg_c)
        open(os.path.join(dst, "standby_anim.c"), "w").write(an)
        print("Standby animation applied.")
    elif os.environ.get("JUNGLE_SIM"):
        open(img_p, "w").write(bg_c)
        open(os.path.join(dst, "standby_anim.c"), "w").write(an)
        print("Standby animation files written (simulator).")
    else:
        print("Standby animation skipped (anchor missing or already applied).")
except Exception as e:
    print("Standby animation skipped: %s" % e)

# ---- Shot history: true amount and true shot time (optional, never fatal) ------------------------------------------------
# The stored final weight was read at the very end of the recording. A scale that zeroes itself after the shot (Bookoo,
# about 1 s after the timer stop) or a cup that is lifted early turned every shot into "0 g". Now the list stores the
# true amount (jungle/TrueOut.h: weight before the zeroing plus the drops counted afterwards; a lifted cup ends it) and
# as duration the true shot time (start .. pump stop) instead of the length of the recording.
try:
    sh_p = os.path.join(repo, "src/display/plugins/ShotHistoryPlugin.cpp")
    sh = open(sh_p).read()
    a1 = "        lastBluetoothWeight = btWeight;\n"
    a2 = "        float finalWeight = currentBluetoothWeight;\n"
    a3 = "    lastBluetoothWeight = 0.0f;\n    currentProfileName ="
    a4 = "uint16_t encodeUnsigned(float value, float scale, uint16_t maxValue) {"
    a5 = "            const float weightDiff = abs(currentBluetoothWeight - lastStableWeight);\n"
    a6 = "        header.durationMs = millis() - shotStart;\n"
    a7 = "            if (now - extendedRecordingStart >= EXTENDED_RECORDING_DURATION) {\n"
    a8 = "    if (recording && controller && controller->isVolumetricAvailable() && currentBluetoothWeight > 0) {\n"
    if all(sh.count(a) == 1 for a in (a1, a2, a3, a4, a5, a6, a7, a8)):
        sh = sh.replace(a4, "// [MOD] true amount and true time of the running shot (see mod/apply_jungle.py)\n"
                            "#include <display/ui/default/jungle/TrueOut.h>\n"
                            "static TrueOut g_histOut;\n"
                            "static bool g_histEnded = false;\n"
                            "static unsigned long g_histEndAt = 0, g_histBrewMs = 0;\n\n" + a4, 1)
        sh = sh.replace(a1, a1 +
                        "        // [MOD] every reading goes to the true-amount counter\n"
                        "        g_histOut.feed(currentBluetoothWeight, g_histEnded, g_histEnded ? millis() - g_histEndAt : 0);\n", 1)
        sh = sh.replace(a2, "        float finalWeight = g_histOut.total; // [MOD] true amount, not the reading after a tare / lifted cup\n", 1)
        sh = sh.replace(a3, "    lastBluetoothWeight = 0.0f;\n    g_histOut.reset(); // [MOD]\n    g_histEnded = false;\n    g_histBrewMs = 0;\n    currentProfileName =", 1)
        sh = sh.replace(a8, "    if (recording && !g_histEnded) { // [MOD] the pump has stopped: this is the true shot time\n"
                            "        g_histEnded = true;\n"
                            "        g_histEndAt = millis();\n"
                            "        g_histBrewMs = g_histEndAt - shotStart;\n"
                            "    }\n" + a8, 1)
        sh = sh.replace(a5, "            if (g_histOut.frozen) {\n"
                            "                extendedRecording = false; // [MOD] cup gone or counting window over: nothing more to measure\n"
                            "                return;\n"
                            "            }\n"
                            "            if (g_histOut.base > 0.0f) {\n"
                            "                return; // [MOD] the scale zeroed itself: keep counting the drops until TrueOut is done\n"
                            "            }\n" + a5, 1)
        sh = sh.replace(a6, "        header.durationMs = g_histBrewMs > 0 ? g_histBrewMs : millis() - shotStart; // [MOD] true shot time\n", 1)
        open(sh_p, "w").write(sh)
        print("True amount / shot time for the shot list applied.")
    else:
        print("True amount / shot time for the shot list skipped (anchors changed).")
except Exception as e:
    print("True amount / shot time for the shot list skipped: %s" % e)
