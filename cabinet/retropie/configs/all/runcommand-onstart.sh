#!/bin/bash
# RetroPie runs this before every game: $1 system, $2 emulator, $3 ROM, $4 command.
# AdvanceMAME and Reicast choose players by device number, which changes between boots.
case "$2" in
    advmame*|reicast*) python3 /home/pi/arcade-tools/player_ports.py >/dev/null ;;
esac
