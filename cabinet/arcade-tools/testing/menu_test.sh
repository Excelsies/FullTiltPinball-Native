#!/bin/bash
# Testing aid: run a RetroPie menu script on this terminal as the RetroPie menu would
# (joystick helper on), logging its exit code. usage: menu_test.sh SCRIPT
/opt/retropie/admin/joy2key/joy2key start
bash "$1" </dev/tty 2>>/tmp/menu-test.err
echo "exit=$?" > /tmp/menu-test.rc
/opt/retropie/admin/joy2key/joy2key stop
