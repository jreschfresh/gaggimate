#include "FlightLog.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <display/core/Controller.h>
#include <display/core/PluginManager.h>
#include <esp_system.h>
#include <time.h>

FlightLogPlugin FlightLog;

static const char *segPath(uint8_t s) { return s ? "/fl1.bin" : "/fl0.bin"; }

static int16_t q(float v, float scale) {
    float x = v * scale;
    if (x > 32767.f) x = 32767.f;
    if (x < -32768.f) x = -32768.f;
    return (int16_t)lroundf(x);
}

FS &FlightLogPlugin::fs() {
    if (controller && controller->isSDCard()) return SD_MMC;
    return LittleFS;
}

void FlightLogPlugin::setup(Controller *c, PluginManager *pm) {
    controller = c;
    // continue in the newer segment (the one with the larger last-modified time is unknown on LittleFS; use the bigger non-full one)
    size_t s0 = 0, s1 = 0;
    if (fs().exists(segPath(0))) { File f = fs().open(segPath(0), "r"); s0 = f.size(); f.close(); }
    if (fs().exists(segPath(1))) { File f = fs().open(segPath(1), "r"); s1 = f.size(); f.close(); }
    // a full segment is followed by the other one; otherwise keep appending to the one that is not full
    if (s0 > 0 && s0 < SEG_MAX_BYTES && (s1 == 0 || s1 >= SEG_MAX_BYTES || s0 >= s1)) {
        curSeg = 0; curSize = s0;
    } else if (s1 > 0 && s1 < SEG_MAX_BYTES) {
        curSeg = 1; curSize = s1;
    } else {
        curSeg = s0 >= SEG_MAX_BYTES ? 1 : 0;
        curSize = 0;
    }
    segOpen = true;
    if (curSize == 0) startSegment(curSeg);

    pm->on("controller:brew:start", [this](Event const &) { event(1, 0); });
    pm->on("controller:brew:prestart", [this](Event const &) { event(2, 0); });
    pm->on("controller:mode:change", [this](Event const &) { event(3, 0); });
    pm->on("controller:wifi:connect", [this](Event const &) { wifi = true; event(4, 1); });
    pm->on("controller:wifi:disconnect", [this](Event const &) { wifi = false; event(4, 0); });
    pm->on("controller:bluetooth:connect", [this](Event const &) { scale = true; event(5, 1); });
    pm->on("controller:bluetooth:disconnect", [this](Event const &) { scale = false; event(5, 0); });
    pm->on("controller:volumetric-measurement:bluetooth:change", [this](Event const &e) { weight = e.getFloat("value"); });
    pm->on("settings:changed", [this](Event const &) { event(6, 0); });
    pm->on("ota:update:start", [this](Event const &) { flush(); event(7, 0); flush(); });
    pm->on("controller:ready", [this](Event const &) { event(8, 0); });

    esp_reset_reason_t rr = esp_reset_reason();
    anchor();
    event(9, (int16_t)rr); // boot, value = reset reason (1 power-on, 3 software, 4 panic, 5/6/7 watchdog, 9 brownout)
}

void FlightLogPlugin::anchor() {
    FlightRec r{};
    r.ms = millis();
    r.type = 1;
    time_t now;
    time(&now);
    uint32_t epoch = (uint32_t)now;
    memcpy(&r.ct, &epoch, 4);                              // ct+tt = epoch seconds (valid when > 1.6e9)
    uint32_t freeKb = (LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024;
    memcpy(&r.pr, &freeKb, 4);                             // pr+tp = free KB on the flash partition
    push(r);
}

void FlightLogPlugin::event(uint8_t code, int16_t value) {
    FlightRec r{};
    r.ms = millis();
    r.type = 2;
    r.mode = controller ? controller->getMode() : 0;
    r.flags = code;
    r.extra = value;
    if (controller) {
        r.ct = q(controller->getCurrentTemp(), 100);
        r.tt = q(controller->getTargetTemp(), 100);
    }
    push(r);
}

void FlightLogPlugin::push(const FlightRec &r) {
    if (disabled) return;
    if (count >= BUF_RECS) flush();
    if (count < BUF_RECS) buf[count++] = r;
}

bool FlightLogPlugin::freeSpaceLow() {
    if (controller->isSDCard()) return false;
    size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    return (total > used ? total - used : 0) < MIN_FREE_BYTES;
}

void FlightLogPlugin::startSegment(uint8_t seg) {
    File f = fs().open(segPath(seg), "w"); // truncates
    if (f) f.close();
    curSeg = seg;
    curSize = 0;
}

void FlightLogPlugin::flush() {
    if (disabled || count == 0) return;
    lastFlush = millis();
    size_t bytes = count * sizeof(FlightRec);
    if (curSize + bytes > SEG_MAX_BYTES) {
        startSegment(curSeg ? 0 : 1); // the other segment is overwritten: the oldest data goes first
    }
    if (freeSpaceLow()) {
        // give space back to the shot history: drop the OLDER segment first, then restart the current one
        uint8_t other = curSeg ? 0 : 1;
        if (fs().exists(segPath(other))) {
            fs().remove(segPath(other));
        } else if (curSize > 0) {
            startSegment(curSeg);
        }
        if (freeSpaceLow()) { // still low: stop writing for now (retry on next flush)
            count = 0;
            return;
        }
    }
    File f = fs().open(segPath(curSeg), "a");
    if (!f) { count = 0; return; }
    size_t w = f.write((const uint8_t *)buf, bytes);
    f.close();
    curSize += w;
    count = 0;
}

void FlightLogPlugin::loop() {
    if (!controller || disabled) return;
    unsigned long now = millis();
    int mode = controller->getMode();
    bool active = controller->isActive();
    float ct = controller->getCurrentTemp();
    float tt = controller->getTargetTemp();
    bool hot = mode != 0 && (active || fabsf(ct - tt) > 2.0f); // heating up / process running: 1 Hz
    unsigned long interval = mode == 0 ? 30000UL : (hot ? 1000UL : 5000UL);
    if (mode != lastMode) {
        lastMode = mode;
        lastSample = 0; // sample immediately on a mode change
        if (mode == 0) { /* standby: persist now */ }
    }
    if (lastSample == 0 || now - lastSample >= interval) {
        lastSample = now ? now : 1;
        FlightRec r{};
        r.ms = now;
        r.type = 0;
        r.mode = mode;
        r.flags = (active ? 1 : 0) | (controller->isBrewActive() ? 2 : 0) | (scale ? 4 : 0) | (wifi ? 8 : 0) | (controller->isReady() ? 16 : 0);
        r.ct = q(ct, 100);
        r.tt = q(tt, 100);
        r.pr = q(controller->getCurrentPressure(), 100);
        r.tp = q(controller->getTargetPressure(), 100);
        r.pumpFlow = q(controller->getCurrentPumpFlow(), 100);
        r.puckFlow = q(controller->getCurrentPuckFlow(), 100);
        r.heater = q(controller->getCurrentHeaterPower(), 10);
        r.pumpPower = q(controller->getCurrentPumpPower(), 10);
        r.weight = q(weight, 10);
        r.volume = q(controller->getCurrentCoffeeVolume(), 10);
        r.tof = (int16_t)controller->getTofDistance();
        r.extra = (int16_t)WiFi.RSSI();
        push(r);
    }
    if (count > 0 && (now - lastFlush >= 60000UL || (mode == 0 && count > 0 && now - lastFlush >= 15000UL))) flush();
}

void FlightLogPlugin::registerRoutes(AsyncWebServer &server) {
    server.on("/api/log/info", [this](AsyncWebServerRequest *request) {
        String s = "{\"recSize\":32,\"cur\":" + String(curSeg) + ",\"buffered\":" + String((unsigned)count) + ",\"seg\":[";
        for (int i = 0; i < 2; i++) {
            size_t sz = 0;
            if (fs().exists(segPath(i))) { File f = fs().open(segPath(i), "r"); sz = f.size(); f.close(); }
            s += String((unsigned)sz);
            if (i == 0) s += ",";
        }
        s += "],\"freeKB\":" + String((unsigned)((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024)) + ",\"uptimeMs\":" + String((unsigned long)millis()) + "}";
        request->send(200, "application/json", s);
    });
    server.on("/api/log", [this](AsyncWebServerRequest *request) {
        int seg = request->hasParam("seg") ? request->getParam("seg")->value().toInt() : 0;
        if (seg < 0 || seg > 1 || !fs().exists(segPath(seg))) {
            request->send(404, "text/plain", "no such segment");
            return;
        }
        request->send(fs(), segPath(seg), "application/octet-stream");
    });
}
