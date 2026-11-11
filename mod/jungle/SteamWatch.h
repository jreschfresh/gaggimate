#pragma once
// SteamWatch: tells from the pressure alone that milk was steamed and the steam valve is closed again.
// Feed one pressure sample per second while the machine is in steam mode; feed() returns true once, WAIT_S seconds
// after the valve was recognised as closed. Header-only and free of Arduino code so the same file runs in the
// host test (mod/tools/steamwatch_test.cpp) against recorded flight-log data.
//
// What the recordings show (steam target 111 and 114 C):
//  - after a shot the sensor keeps the locked-in brew pressure (8.6 bar, slowly falling) until the wand is opened
//  - valve closed: 1.3-1.8 bar, steady
//  - short purge before steaming (about 10 s): falls by 0.3-0.6 bar, to about 0.7-0.8 of the level before
//  - steaming (22-27 s): falls steadily to 0.6-0.8 bar, less than half of the level before
//  - valve closed again: jumps up by about 0.15 bar, then rises with the boiler temperature
struct SteamWatch {
    static constexpr float TRAPPED = 2.4f;      // above this the sensor still shows pressure locked in after a shot
    static constexpr float RELEASE_JUMP = 0.5f; // a fall this large out of the locked-in range = wand opened
    static constexpr float OPEN_DROP = 0.22f;   // fall within OPEN_WINDOW seconds that means "valve opened"
    static constexpr int OPEN_WINDOW = 5;
    static constexpr float RECOVER_FAST = 0.15f; // back above the lowest point by this much for RECOVER_FAST_S s = closed
    static constexpr int RECOVER_FAST_S = 3;
    static constexpr float RECOVER_SLOW = 0.10f; // ... or by this much for RECOVER_SLOW_S s
    static constexpr int RECOVER_SLOW_S = 8;
    static constexpr int MIN_OPEN_S = 14;       // shorter openings are purges, not steaming
    static constexpr float MAX_RATIO = 0.62f;   // steaming pulls the pressure below this share of the level before
    static constexpr int WAIT_S = 5;            // wait after "closed" before standby (Jakob: 5 s)

    enum State { IDLE, CLOSED, OPEN };
    State state = IDLE;
    float hist[OPEN_WINDOW] = {0};
    int histN = 0, histPos = 0;
    float last3[3] = {0};
    int n3 = 0, pos3 = 0;
    float prev = -1.0f;
    int t = 0;
    int tOpen = 0, tMin = 0, refPending = 0, fastCnt = 0, slowCnt = 0, fireAt = -1;
    float ref = 0.0f, pMin = 0.0f;
    bool lastSteamed = false; // result of the last recognised closing (for the log / tests)

    void reset() { *this = SteamWatch(); }

    bool feed(float p) {
        t++;
        if (p > TRAPPED) { // locked-in shot pressure: nothing can be read from it
            state = IDLE;
            histN = histPos = n3 = pos3 = 0;
            fireAt = -1;
            prev = p;
            return false;
        }
        last3[pos3] = p;
        pos3 = (pos3 + 1) % 3;
        if (n3 < 3)
            n3++;
        float avg = 0.0f;
        for (int i = 0; i < n3; i++)
            avg += last3[i];
        avg /= n3;

        bool entered = false;
        if (state == IDLE) {
            if (prev > TRAPPED && prev - p >= RELEASE_JUMP) { // released through the wand: the valve is open now
                open(0.0f, 2, avg);
                entered = true;
            } else {
                state = CLOSED;
                histN = histPos = 0;
            }
        }
        if (state == CLOSED) {
            float mx = 0.0f;
            for (int i = 0; i < histN; i++)
                mx = hist[i] > mx ? hist[i] : mx;
            if (histN > 0 && p <= mx - OPEN_DROP) {
                open(mx, 0, avg);
                entered = true;
            } else {
                push(p);
            }
        }
        if (state == OPEN && !entered) {
            if (refPending > 0) { // level right after the release, the very first sample is still on its way down
                refPending--;
                ref = p > ref ? p : ref;
            }
            if (avg < pMin) {
                pMin = avg;
                tMin = t;
                fastCnt = slowCnt = 0;
            } else {
                fastCnt = avg >= pMin + RECOVER_FAST ? fastCnt + 1 : 0;
                slowCnt = avg >= pMin + RECOVER_SLOW ? slowCnt + 1 : 0;
            }
            if (fastCnt >= RECOVER_FAST_S || slowCnt >= RECOVER_SLOW_S) { // rising again: the valve is closed
                lastSteamed = (tMin - tOpen) >= MIN_OPEN_S && ref > 0.0f && pMin <= ref * MAX_RATIO;
                state = CLOSED;
                histN = histPos = 0;
                push(p);
                fireAt = lastSteamed ? t + WAIT_S : -1;
            }
        }
        prev = p;
        if (fireAt >= 0 && state == CLOSED && t >= fireAt) {
            fireAt = -1;
            return true;
        }
        return false;
    }

  private:
    void open(float level, int pending, float avg) {
        state = OPEN;
        tOpen = t;
        tMin = t;
        ref = level;
        refPending = pending;
        pMin = avg;
        fastCnt = slowCnt = 0;
        fireAt = -1; // opened again: a pending standby is off
    }
    void push(float p) {
        hist[histPos] = p;
        histPos = (histPos + 1) % OPEN_WINDOW;
        if (histN < OPEN_WINDOW)
            histN++;
    }
};
