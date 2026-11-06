# Building

Install devel packages for `SDL2` and `SDL2_mixer`.\

Compile with CMake; tested with GCC 10, Clang 11.\
To cross-compile for Windows, install a 64-bit version of mingw and its `SDL2` and `SDL2_mixer` distributions, then use the `mingwcc.cmake` toolchain.

```bash
# On Debian and Ubuntu
sudo apt install cmake build-essential ninja-build libsdl2-dev libsdl2-mixer-dev libsdl2-mixer-2.0-0 libsdl2-2.0-0 fluidsynth libfluidsynth-dev

# On Fedora
sudo dnf install cmake ninja-build SDL2 SDL2-devel SDL2_mixer SDL2_mixer-devel fluidsynth fluidsynth-libs fluidsynth-devel mscore-fonts g++

# Build
cmake -GNinja .
ninja
```

With fluidsynth's development files (`libfluidsynth-dev`, `fluidsynth-devel`) CMake reports
`Music: fluidsynth` and the game plays music through one fluidsynth synthesizer of its own; without
them it uses SDL_mixer's MIDI backends. Both use the SoundFont list SDL_mixer uses.

**If you can't hear the background music**: You need to export the following variable before launching the game like this:
`export SDL_SOUNDFONTS=/path/to/your/soundfont` (several files separated by `:`)

Fluidsynth will install a default soundfont in the following path:
On Fedora: `/usr/share/soundfonts/FluidR3_GM.sf2`
On Ubuntu/Debian: `/usr/share/sounds/sf2/FluidR3_GM.sf2`
