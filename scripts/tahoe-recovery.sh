#!/bin/bash
# tahoe-recovery.sh -- boot the Tahoe Recovery installer media and open a Terminal (no kcsuffix). Ends with a focused Terminal; type with scripts/vm.sh type.
cd "$(dirname "$0")/.."
scripts/oc-set.sh '-v keepsyms=1 debug=0x100 serial=3' 0x87f 2>&1 | tail -1 | tee /tmp/tahoe-recovery.ocset
: > /tmp/hs-serial.log
(INSTALL=1 nohup scripts/boot-tahoe.sh >/tmp/boot-tahoe.log 2>&1 &)
sleep 4; sudo -n renice -8 -p $(pgrep -x qemu-system-x86) >/dev/null
until grep -aq "BdsDxe: starting" /tmp/hs-serial.log 2>/dev/null; do sleep 2; done
sleep 35; for k in left left left right; do scripts/vm.sh key $k; sleep 1; done; sleep 3; scripts/vm.sh key ret   # home to the first entry, then one step to 'macOS Base System'
# wait for Recovery's window (the 'Recovery' menu bar title) -- poll the screenshot for the Continue button area brightness
for i in $(seq 1 120); do sleep 5; scripts/vm-shot.sh /tmp/rec.png >/dev/null 2>&1
  python3 -W ignore - <<'PY' && break
from PIL import Image
im=Image.open('/tmp/rec.png').convert('L'); import sys
# Recovery main window has a light-gray 'Continue' button near (820,526)
sys.exit(0 if im.getpixel((820,526))>35 and im.getpixel((640,176))>25 else 1)
PY
done
# open Terminal via the Utilities menu; verify the Terminal window (dark blue-gray body) appears, retry up to 4 times
term_up(){ scripts/vm-shot.sh /tmp/rec.png >/dev/null 2>&1; python3 -W ignore - <<'PY'
from PIL import Image
import sys
im=Image.open('/tmp/rec.png').convert('RGB'); r,g,b=im.getpixel((470,300))
sys.exit(0 if (30<=r<=45 and 36<=g<=52 and 48<=b<=66) else 1)
PY
}
sleep 8
for try in 1 2 3 4; do
  scripts/vm.sh hotkbd; scripts/vm.sh key ctrl-f2; sleep 2; for k in right right right right down down down; do scripts/vm.sh key $k; sleep 0.5; done; sleep 1; scripts/vm.sh key ret
  for w in $(seq 1 24); do sleep 5; term_up && break 2; done
  scripts/vm.sh key esc; sleep 1; scripts/vm.sh key esc; sleep 2
done
term_up || { echo "terminal never appeared"; exit 1; }
sleep 3
echo "terminal requested"
