#!/bin/sh
# Builds and runs music_backend_check.cpp against the hook build's objects (rebuild build-hook first;
# needs a build with fluidsynth: CMake's "Music: fluidsynth"). Run from the repo root. Audio goes to
# SDL's dummy driver. Usage: music_backend_check.sh <soundfont.sf2> <out-dir>
set -e
sf="$1"; out="$2"
[ -n "$sf" ] && [ -n "$out" ] || { echo "usage: $0 <soundfont.sf2> <out-dir>"; exit 2; }
mkdir -p "$out/data/SOUND"
obj=build-hook/CMakeFiles/SpaceCadetPinball.dir/SpaceCadetPinball
objs=$(find "$obj" -name '*.o' ! -name SpaceCadetPinball.cpp.o)
flags="-std=gnu++11 -O1 -DFT_TEST_HOOK=1 -I SpaceCadetPinball $(sdl2-config --cflags) $(pkg-config --cflags fluidsynth)"
wrap="-Wl,--wrap=new_fluid_synth,--wrap=fluid_synth_set_gain,--wrap=fluid_synth_write_s16"
wrap="$wrap,--wrap=fluid_synth_system_reset,--wrap=fluid_player_play,--wrap=Mix_HookMusic"
c++ $flags -include pch.h -c re/test/native/music_backend_check.cpp -o "$out/check.o"
c++ "$out/check.o" $objs -o "$out/check" $wrap $(sdl2-config --libs) -lSDL2_mixer \
    $(pkg-config --libs fluidsynth) -lpthread
"$out/check" "$sf" "$out/data"
