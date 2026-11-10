#!/bin/bash
# Host build of the Jungle UI against LVGL 8.4 (same lv_conf as the firmware). Usage: build.sh <patched-firmware-tree>
set -e
GT=$1; HERE=$(cd $(dirname $0) && pwd); B=$HERE/build; mkdir -p $B/inc/display/ui/default/eez $B/obj
# lv_conf: the firmware's file with host malloc and a host tick
sed -e 's|<esp32-hal-psram.h>//<stdlib.h>|<stdlib.h>|' -e 's|ps_malloc|malloc|' -e 's|ps_realloc|realloc|' \
    -e 's|#define LV_TICK_CUSTOM 1|#define LV_TICK_CUSTOM 0|' $GT/src/display/lv_conf.h > $B/inc/lv_conf.h
J=$GT/src/display/ui/default/jungle
# stub actions.h
{ echo '#pragma once'; echo '#include <lvgl.h>'; echo '#ifdef __cplusplus'; echo 'extern "C" {'; echo '#endif';
  grep -oh "action_on_[a-z_0-9]*" $J/JungleUI.cpp | sort -u | sed 's/.*/void &(lv_event_t *e);/';
  echo '#ifdef __cplusplus'; echo '}'; echo '#endif'; } > $B/inc/display/ui/default/eez/actions.h
{ echo '#include <display/ui/default/eez/actions.h>'; grep -oh "action_on_[a-z_0-9]*" $J/JungleUI.cpp | sort -u | sed 's/.*/void &(lv_event_t *e) { (void)e; }/'; } > $B/stubs.c
CF="-O1 -w -DLV_CONF_INCLUDE_SIMPLE -I$B/inc -I$HERE/lvgl -I$HERE"
if [ ! -f $B/liblvgl.a ] || [ $B/inc/lv_conf.h -nt $B/liblvgl.a ]; then
  rm -f $B/obj/*.o
  find $HERE/lvgl/src -name '*.c' | while read f; do o=$B/obj/$(echo $f | md5sum | cut -c1-12).o; echo "$f $o"; done > $B/list
  cat $B/list | xargs -P 8 -L 1 bash -c 'gcc '"$CF"' -c $0 -o $1'
  ar rcs $B/liblvgl.a $B/obj/*.o
fi
EXTRA=""
for f in $J/*.c $GT/src/display/ui/default/eez/images/ui_image_standby.c; do [ -f "$f" ] && { o=$B/x_$(basename $f).o; gcc $CF -I$J -c $f -o $o; EXTRA="$EXTRA $o"; }; done
gcc $CF -c $B/stubs.c -o $B/stubs.o
g++ -std=c++17 $CF -I$J -c $J/JungleUI.cpp -o $B/JungleUI.o
g++ -std=c++17 $CF -I$J -c $HERE/main.cpp -o $B/main.o
g++ $B/main.o $B/JungleUI.o $B/stubs.o $EXTRA $B/liblvgl.a -lm -o $B/sim
echo built
