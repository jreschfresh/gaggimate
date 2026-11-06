#ifndef FLIGHTLOG_H
#define FLIGHTLOG_H

#include <ESPAsyncWebServer.h>
#include <display/core/Plugin.h>
#include <stdint.h>

// [MOD] Flight log: compact binary black box on the machine itself (two alternating segment files in LittleFS).
// 32-byte records, see FlightRec. Download: /api/log?seg=0|1 and /api/log/info.

#pragma pack(push, 1)
struct FlightRec {
    uint32_t ms;     // millis() since boot
    uint8_t type;    // 0 sample, 1 anchor (a=epoch s, b=boot count/free KB), 2 event (code in flags, value in extra)
    uint8_t mode;    // 0 standby, 1 brew, 2 steam, 3 water, 4 grind
    uint8_t flags;   // sample: bit0 active, bit1 brew active, bit2 scale connected, bit3 wifi, bit4 ready; event: code
    uint8_t rsv;
    int16_t ct;      // current temperature  (x100, raw sensor value)
    int16_t tt;      // target temperature   (x100)
    int16_t pr;      // pressure bar         (x100)
    int16_t tp;      // target pressure      (x100)
    int16_t pumpFlow;  // ml/s              (x100)
    int16_t puckFlow;  // ml/s              (x100)
    int16_t heater;    // heater power %    (x10)
    int16_t pumpPower; // pump power %      (x10)
    int16_t weight;  // bluetooth scale g   (x10)
    int16_t volume;  // coffee volume ml    (x10)
    int16_t tof;     // distance mm
    int16_t extra;   // wifi rssi / event value
};
#pragma pack(pop)
static_assert(sizeof(FlightRec) == 32, "FlightRec must be 32 bytes");

class Controller;
class PluginManager;

class FlightLogPlugin : public Plugin {
  public:
    void setup(Controller *controller, PluginManager *pm) override;
    void loop() override;
    void registerRoutes(AsyncWebServer &server);

  private:
    static constexpr size_t BUF_RECS = 64;
    static constexpr size_t SEG_MAX_BYTES = 640 * 1024; // per segment, two segments
    static constexpr size_t MIN_FREE_BYTES = 700 * 1024; // below this the log gives space back (shot history keeps 500 KB)

    Controller *controller = nullptr;
    FlightRec buf[BUF_RECS];
    size_t count = 0;
    unsigned long lastSample = 0;
    unsigned long lastFlush = 0;
    uint8_t curSeg = 0;
    size_t curSize = 0;
    bool segOpen = false;
    bool disabled = false;
    float weight = 0.f;
    bool scale = false;
    bool wifi = false;
    int lastMode = -1;

    void push(const FlightRec &r);
    void event(uint8_t code, int16_t value);
    void anchor();
    void flush();
    void startSegment(uint8_t seg);
    bool freeSpaceLow();
    FS &fs();
};

extern FlightLogPlugin FlightLog;

#endif
