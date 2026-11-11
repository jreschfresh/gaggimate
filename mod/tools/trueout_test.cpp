// Host test for mod/jungle/TrueOut.h: replays recorded shots ("<label> <ms since start> <ended 0/1> <ms since end> <weight>").
// build: g++ -std=c++17 -I mod/jungle -o /tmp/trueout_test mod/tools/trueout_test.cpp
#include "TrueOut.h"
#include <cstdio>
#include <cstring>
int main(int argc, char **argv) {
    FILE *f = argc > 1 ? fopen(argv[1], "r") : stdin;
    char label[64], cur[64] = "";
    long ms, since;
    int ended;
    float w, atStop = 0, total = 0;
    TrueOut o;
    bool have = false;
    auto flush = [&]() {
        if (have)
            printf("%-28s at stop %5.1f g   true out %5.1f g%s\n", cur, atStop, total, o.frozen ? "" : "  (still counting)");
    };
    while (fscanf(f, "%63s %ld %d %ld %f", label, &ms, &ended, &since, &w) == 5) {
        if (strcmp(label, cur) != 0) {
            flush();
            strcpy(cur, label);
            o.reset();
            have = true;
            atStop = 0;
        }
        total = o.feed(w, ended != 0, since);
        if (!ended)
            atStop = w;
    }
    flush();
    return 0;
}
