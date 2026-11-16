#!/bin/bash
# install-gui.sh <profile> -- run the macOS installer GUI end to end on the single install disk (scripts/make-install-disk.sh), from the keyboard
# plus a relative mouse for the disk picker: boot the disk, open Terminal and show the kernel and processor, erase the target, then walk the
# installer screens (welcome, license, disk, install). Screenshots go to build/shots/<profile>/. Stage 1 only: the guest then restarts into stage 2.
# Env: CPU (QEMU CPU model), HS_DATE (guest clock, default 2018-12-20T12:00:00, see boot-highsierra.sh), HS_USB (the disk image).
cd "$(dirname "$0")/.."
P=${1:?usage: install-gui.sh <profile>}; . scripts/install-disk/$P.conf
S=build/shots/$P; mkdir -p $S build/agent/files
export HS_DATE=${HS_DATE:-2018-12-20T12:00:00}
USB=${HS_USB:-$HOME/Downloads/OSX-KVM/install-$NAME.img}; export HS_USB=$USB
shot() { scripts/vm-shot.sh $S/$1.png 2>/dev/null; }
poke() { scripts/vm.sh key shift; sleep 4; }
changed() { python3 -W ignore - "$1" "$2" <<'PY'
import sys
from PIL import Image, ImageChops
a = Image.open(sys.argv[1]).convert('L'); b = Image.open(sys.argv[2]).convert('L')
bb = ImageChops.difference(a, b).point(lambda p: 255 if p > 40 else 0).getbbox()
sys.exit(0 if bb else 1)
PY
}
wait_ui() {   # wait until a light GUI window is on screen
  for i in $(seq 1 60); do sleep 15; poke; shot _p
    python3 -W ignore - $S/_p.png <<'PY' && return 0
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('L'); d = list(im.crop((240, 100, 1040, 680)).resize((80, 58)).getdata())
sys.exit(0 if sum(1 for p in d if p > 170) / len(d) > 0.2 else 1)
PY
  done; return 1
}
mouse() { printf '%s\n' "$1" | socat - UNIX-CONNECT:/tmp/hs-monitor.sock > /dev/null 2>&1; }
click_at_steps() {   # reset to the top-left corner, then N steps of (50,45): about 78 px right and 70 px down per step
  for i in $(seq 1 40); do mouse "mouse_move -127 -127"; sleep 0.03; done; sleep 1
  for i in $(seq 1 $1); do mouse "mouse_move 50 45"; sleep 0.15; done; sleep 1
  mouse "mouse_button 1"; sleep 0.2; mouse "mouse_button 0"; sleep 3
}
for p in $(pgrep -x qemu-system-x86); do kill $p; done; sleep 3
rm -f /tmp/hs-monitor.sock; : > /tmp/hs-serial.log
cp scripts/guest/cpu-info.sh scripts/guest/erase-target.sh build/agent/files/
ss -ltn 2>/dev/null | grep -q ':8000 ' || (setsid nohup python3 scripts/agent-server.py > /tmp/agent.log 2>&1 < /dev/null &)
(setsid nohup ${BOOT_SCRIPT:-scripts/boot-highsierra.sh} > /tmp/boot-vm.log 2>&1 < /dev/null &)
sleep 5; sudo -n renice -8 -p $(pgrep -x qemu-system-x86) > /dev/null
until grep -aq "BdsDxe: starting" /tmp/hs-serial.log 2>/dev/null; do sleep 2; done
sleep 30; shot 01-picker; for k in $PICKER_KEYS; do scripts/vm.sh key $k; sleep 1; done
wait_ui || { echo "no UI"; exit 1; }; shot 02-utilities
echo "== terminal: kernel and processor, erase the target"
scripts/vm.sh hotkbd; for k in $TERMINAL_KEYS; do scripts/vm.sh key $k; sleep 0.8; done; sleep 8
scripts/vm.sh key esc; scripts/vm.sh key ctrl-f2; sleep 2; for k in right right right right right down down ret; do scripts/vm.sh key $k; sleep 0.6; done; sleep 3   # Window > Zoom
scripts/vm.sh type "curl -s -o /tmp/ci.sh http://10.0.2.2:8000/files/cpu-info.sh; sh /tmp/ci.sh"; scripts/vm.sh key ret; sleep 8; shot 03-terminal-cpu-info
scripts/vm.sh key meta_l-k; scripts/vm.sh type "curl -s -o /tmp/et.sh http://10.0.2.2:8000/files/erase-target.sh; sh /tmp/et.sh"; scripts/vm.sh key ret; sleep 25; shot 04-erase-target
scripts/vm.sh key meta_l-q; sleep 3
echo "== installer GUI"
scripts/vm.sh key down; sleep 1; scripts/vm.sh key down; sleep 1; scripts/vm.sh key tab; sleep 1; scripts/vm.sh key spc   # Reinstall macOS, Continue
wait_ui || { echo "installer window never appeared"; exit 1; }; shot 05-welcome
scripts/vm.sh key tab; sleep 1; scripts/vm.sh key spc; sleep 10; poke; shot 06-license          # Continue
scripts/vm.sh key tab; sleep 1; scripts/vm.sh key tab; sleep 1; scripts/vm.sh key spc; sleep 6; poke   # Agree
scripts/vm.sh key tab; sleep 1; scripts/vm.sh key spc; sleep 10; poke; shot 07-disk                # Agree in the sheet
mouse "device_add usb-mouse,id=relmouse,bus=ehci.0"; sleep 4
click_at_steps 7; poke; shot 08-disk-selected                                                       # the Macintosh disk
for i in 1 2; do mouse "mouse_move 50 45"; sleep 0.15; done; sleep 1; mouse "mouse_button 1"; sleep 0.2; mouse "mouse_button 0"   # Install
sleep 20; poke; shot 09-installing
echo "installing; the guest restarts into stage 2 when stage 1 is done. Screenshots: $S"
