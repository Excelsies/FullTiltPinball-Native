#!/bin/bash
# Testing aid: run the Game controls entry as runcommand would (joystick helper on), log its exit code.
# usage: gc_test.sh SYSTEM EMULATOR ROM COMMAND
/opt/retropie/admin/joy2key/joy2key start
bash "/opt/retropie/configs/all/runcommand-menu/Game controls.sh" "$@" </dev/tty 2>>/tmp/gc-test.err
echo "exit=$?" > /tmp/gc-test.rc
/opt/retropie/admin/joy2key/joy2key stop
