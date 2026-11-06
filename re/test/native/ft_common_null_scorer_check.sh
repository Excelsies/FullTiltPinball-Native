#!/bin/sh
# Builds and runs ft_common_null_scorer_check.cpp. Run from the repo root after a hook build;
# needs build-hook/ objects. Usage: ft_common_null_scorer_check.sh <out-dir>
set -e
out="$1"; [ -n "$out" ] || { echo "usage: $0 <out-dir>"; exit 2; }
mkdir -p "$out"
obj=build-hook/CMakeFiles/SpaceCadetPinball.dir/SpaceCadetPinball
objs=$(find "$obj" -name '*.o' ! -name SpaceCadetPinball.cpp.o ! -name ft_common.cpp.o)
flags="-std=gnu++11 -O1 -DFT_TEST_HOOK=1 -I SpaceCadetPinball $(sdl2-config --cflags)"
c++ $flags -include pch.h -c SpaceCadetPinball/rules/ft_common.cpp -o "$out/ft_common.o"
c++ $flags -include pch.h -c re/test/native/ft_common_null_scorer_check.cpp -o "$out/check.o"
c++ "$out/check.o" "$out/ft_common.o" $objs -o "$out/check" $(sdl2-config --libs) -lSDL2_mixer
"$out/check"
