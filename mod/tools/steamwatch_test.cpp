// Host test for mod/jungle/SteamWatch.h: replays recorded steam sessions (one line per second: "<label> <seconds> <pressure>").
// build: g++ -std=c++17 -I mod/jungle -o /tmp/steamwatch_test mod/tools/steamwatch_test.cpp
#include "SteamWatch.h"
#include <cstdio>
#include <cstring>
int main(int argc, char **argv) {
    FILE *f = argc > 1 ? fopen(argv[1], "r") : stdin;
    char label[64], cur[64] = "";
    float s, p;
    SteamWatch w;
    SteamWatch::State st = SteamWatch::IDLE;
    while (fscanf(f, "%63s %f %f", label, &s, &p) == 3) {
        if (strcmp(label, cur) != 0) {
            strcpy(cur, label);
            w.reset();
            st = SteamWatch::IDLE;
            printf("== %s\n", cur);
        }
        const bool fire = w.feed(p);
        if (w.state != st) {
            if (w.state == SteamWatch::OPEN)
                printf("  %5.0f s  valve OPEN   (p=%.2f, level before=%.2f)\n", s, p, w.ref);
            else if (st == SteamWatch::OPEN)
                printf("  %5.0f s  valve CLOSED (p=%.2f, lowest=%.2f, level before=%.2f, open for %d s) -> %s\n", s, p, w.pMin, w.ref,
                       w.tMin - w.tOpen, w.lastSteamed ? "STEAMED" : "purge, ignored");
            st = w.state;
        }
        if (fire)
            printf("  %5.0f s  >>> STANDBY\n", s);
    }
    return 0;
}
