#!/bin/bash
# hs-stage2-test.sh <qemu-cpu-string> [wait-seconds] -- boot stage 2 of the High Sierra install (the "macOS Installer" entry on the single install disk)
# on a given QEMU CPU model and report whether it reaches the installer GUI or hangs on the verbose console. Used to find which CPU feature
# stage 2 needs. Needs the target disk in the state stage 1 leaves it in (copy it first: cp --reflink=auto mac_hdd_ng.img mac_hdd_stage1.img).
cd "$(dirname "$0")/.."
CPUSTR=${1:?usage: hs-stage2-test.sh <cpu> [seconds]}; W=${2:-200}
for p in $(pgrep -x qemu-system-x86); do kill $p; done; sleep 3
rm -f /tmp/hs-monitor.sock; : > /tmp/hs-serial.log
(HS_DATE=${HS_DATE:-2018-12-21T12:00:00} CPU="$CPUSTR" HS_USB=${HS_USB:-HS-USB.img} setsid nohup scripts/boot-highsierra.sh > /tmp/boot-hs.log 2>&1 < /dev/null &)
sleep 6; until grep -aq "BdsDxe: starting" /tmp/hs-serial.log 2>/dev/null; do sleep 2; done
sleep 30; scripts/vm.sh hotkbd; scripts/vm.sh key ret; sleep 15; scripts/vm.sh key ret   # the guest USB keyboard drops the first keys, so press twice
sleep $W
scripts/vm.sh key shift; sleep 4; scripts/vm-shot.sh /tmp/st2.png
python3 -W ignore - <<'PY'
from PIL import Image
im = Image.open('/tmp/st2.png').convert('L'); d = list(im.crop((240, 100, 1040, 680)).resize((80, 58)).getdata())
light = sum(1 for p in d if p > 170) / len(d)
mean = sum(Image.open('/tmp/st2.png').convert('L').resize((64, 40)).getdata()) / 2560
print('RESULT: installer GUI' if light > 0.25 else 'RESULT: still at the OpenCore picker (key not delivered)' if 8 < mean < 60 else 'RESULT: console (hung or still booting)')
PY
