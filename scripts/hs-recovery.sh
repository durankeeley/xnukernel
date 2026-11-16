#!/bin/bash
# hs-recovery.sh -- boot the High Sierra installer (Recovery) and open a Terminal in it. Env as for boot-highsierra.sh (CPU, HS_BASE, HS_ESD, ...).
# Ends with a focused Terminal: type with scripts/vm.sh type. Run guest scripts with: curl -s -o /tmp/x.sh http://10.0.2.2:8000/files/<name>; sh /tmp/x.sh
cd "$(dirname "$0")/.."
for p in $(pgrep -x qemu-system-x86); do kill $p; done; sleep 2
ss -ltn 2>/dev/null | grep -q ':8000 ' || (setsid nohup python3 scripts/agent-server.py > /tmp/agent-hs.log 2>&1 </dev/null &)
rm -f /tmp/hs-monitor.sock; : > /tmp/hs-serial.log
(setsid nohup scripts/boot-highsierra.sh "$@" > /tmp/boot-hs.log 2>&1 </dev/null &)
sleep 5; sudo -n renice -8 -p $(pgrep -x qemu-system-x86) > /dev/null
until grep -aq "BdsDxe: starting" /tmp/hs-serial.log 2>/dev/null; do sleep 2; done
sleep 30; scripts/vm.sh key right; sleep 1; scripts/vm.sh key ret       # picker: EFI, OS X Base System
for i in $(seq 1 60); do sleep 15; scripts/vm-shot.sh /tmp/hsrec.png 2>/dev/null
  python3 -W ignore - <<'PY' && break
from PIL import Image
import sys
im = Image.open('/tmp/hsrec.png').convert('L').resize((64, 40))
sys.exit(0 if sum(im.getdata()) / 2560 > 90 else 1)
PY
done
sleep 5; scripts/vm.sh hotkbd; scripts/vm.sh key ctrl-f2; sleep 2
for k in right right right right down down down down ret; do scripts/vm.sh key $k; sleep 0.7; done   # Utilities > Terminal
sleep 10; echo "terminal requested"
