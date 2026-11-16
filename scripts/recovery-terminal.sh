#!/bin/bash
# recovery-terminal.sh <profile> [extra qemu args] -- boot an installer (Recovery) in the VM and open its Terminal from the keyboard.
# Uses PICKER_KEYS and TERMINAL_KEYS from scripts/install-disk/<profile>.conf, BOOT_SCRIPT (default scripts/boot-highsierra.sh) and the env of that
# script (CPU, HS_BASE, HS_ESD, HS_USB ...). Ends with a focused Terminal: type with scripts/vm.sh type.
cd "$(dirname "$0")/.."
. scripts/install-disk/${1:?usage: recovery-terminal.sh <profile> [qemu args]}.conf; shift
for p in $(pgrep -x qemu-system-x86); do kill $p; done; sleep 2
ss -ltn 2>/dev/null | grep -q ':8000 ' || (setsid nohup python3 scripts/agent-server.py > /tmp/agent.log 2>&1 </dev/null &)
rm -f /tmp/hs-monitor.sock; : > /tmp/hs-serial.log
(setsid nohup ${BOOT_SCRIPT:-scripts/boot-highsierra.sh} "$@" > /tmp/boot-vm.log 2>&1 </dev/null &)
sleep 5; sudo -n renice -8 -p $(pgrep -x qemu-system-x86) > /dev/null
until grep -aq "BdsDxe: starting" /tmp/hs-serial.log 2>/dev/null; do sleep 2; done
sleep 30; scripts/vm.sh hotkbd; for k in $PICKER_KEYS; do scripts/vm.sh key $k; sleep 1; done
for i in $(seq 1 60); do sleep 15; scripts/vm-shot.sh /tmp/rec.png 2>/dev/null
  python3 -W ignore - <<'PY' && break
from PIL import Image
import sys
im = Image.open('/tmp/rec.png').convert('L').resize((64, 40))
sys.exit(0 if sum(im.getdata()) / 2560 > 90 else 1)
PY
done
sleep 5; scripts/vm.sh wake; scripts/vm.sh hotkbd
for k in $TERMINAL_KEYS; do scripts/vm.sh key $k; sleep 0.8; done
sleep 10; echo "terminal requested"
