#!/bin/bash
# Start RetroPie joy2key on this terminal, report whether it stays up, stop it.
/opt/retropie/admin/joy2key/joy2key start > /tmp/j2k.log 2>&1
echo "rc=$?" >> /tmp/j2k.log
sleep 2
pgrep -a -f joy2key_ >> /tmp/j2k.log || echo "joy2key not running" >> /tmp/j2k.log
/opt/retropie/admin/joy2key/joy2key stop
