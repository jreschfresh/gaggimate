# Host simulator for the Jungle screens

Renders the real UI code with LVGL 8.4 on a PC (same `lv_conf.h` as the firmware, 16-bit colour), so screens can be
checked as pictures before a firmware build.

    git clone --depth 1 --branch v8.4.0 https://github.com/lvgl/lvgl.git      # next to build.sh
    git clone --depth 1 --branch <tag> https://github.com/jniebuhr/gaggimate.git tree
    (cd tree && python3 ../../../patch.py ../../../gaggimate_standby.png . /tmp/p.png && python3 ../../../apply_jungle.py . ../../..)
    ./build.sh tree && mkdir -p out && ./build/sim out && python3 topng.py out round

`main.cpp` lists the rendered states (brew heating / ready / shot, water, steam, grind, question overlay, standby
animation frames). It also catches compile errors in `JungleUI.cpp` before the CI build does.
