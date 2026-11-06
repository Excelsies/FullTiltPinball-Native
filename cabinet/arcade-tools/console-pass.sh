#!/bin/sh
# Real-screen retest of games on emulators that need the local console, then fix what still fails.
# Run on tty1:  sudo openvt -f -c 1 -s -w -- sudo -u pi ~/arcade-tools/console-pass.sh
T=/home/pi/arcade-tools; R=/home/pi/launch-test
python3 $T/launch_test.py --console --results $R/console-results.csv --systems n64,nds,dreamcast,ports
python3 $T/launch_test.py --console --results $R/console-results.csv --systems arcade --roms "$(cat $R/console-arcade.txt)"
python3 $T/fix_failures.py --console --results $R/console-results.csv --out $R/console-fix-results.csv
echo "CONSOLE PASS DONE"
