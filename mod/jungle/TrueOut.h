#pragma once
// TrueOut: the real amount in the cup of the last shot, including what drips after the pump has stopped.
// Feed every scale reading from the start of the shot. Header-only and free of Arduino code (host test:
// mod/tools/trueout_test.cpp, replays recorded shots).
//
// What the recordings show (Bookoo Themis Mini): about 1 s after the stop the scale reports 0 g with the cup still on
// it and counts the remaining drops from there (+0.1 .. +0.2 g); a lifted cup shows as a jump of minus the cup weight.
#include <math.h>

struct TrueOut {
    static constexpr float JUMP = 5.0f;      // more than this between two readings is not coffee
    static constexpr float ZERO_BAND = 2.0f; // this close to 0 right after a jump down = zeroed with the cup on it
    static constexpr float MIN_CUP = 5.0f;   // ... and only if there was something to zero
    static constexpr unsigned long AFTER_MS = 6000;  // drops count for this long after the stop
    static constexpr unsigned long CONFIRM_MS = 400; // ... and only if the higher reading stays (a touch does not count)

    float base = 0.0f, last = 0.0f, total = 0.0f, cand = -1.0f;
    unsigned long candAt = 0;
    bool started = false, frozen = false;

    void reset() { *this = TrueOut(); }

    // shotEnded: the pump has stopped; msSinceEnd: time since then (ignored while the shot runs)
    float feed(float w, bool shotEnded, unsigned long msSinceEnd = 0) {
        if (frozen) {
            return total;
        }
        if (shotEnded && msSinceEnd > AFTER_MS) {
            frozen = true;
            return total;
        }
        if (!started) {
            started = true;
            last = w;
            total = w;
            return total;
        }
        if (fabsf(w - last) > JUMP) {
            if (!shotEnded) {
                base = 0.0f; // tare at the start of the shot: count from the new zero
            } else if (last > MIN_CUP && fabsf(w) < ZERO_BAND) {
                base += last; // the scale zeroed itself with the cup still standing: later drops come on top
            } else {
                frozen = true; // cup lifted or something put down: the amount stays as it was
                return total;
            }
        }
        last = w;
        const float t = base + w;
        if (!shotEnded) {
            total = t;
        } else if (t > total) { // after the stop the amount can only grow, and only by readings that stay
            if (cand < 0.0f || t < cand) {
                if (cand < 0.0f) {
                    candAt = msSinceEnd;
                }
                cand = t;
            }
            if (msSinceEnd - candAt >= CONFIRM_MS) {
                total = cand;
                cand = -1.0f;
            }
        } else {
            cand = -1.0f;
        }
        return total;
    }
};
