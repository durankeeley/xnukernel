#!/bin/bash
# vm-ptr.sh corner | goto X Y | click   Pointer control for Tahoe VMs. The QEMU HID tablet does not move the pointer in macOS Tahoe,
# so hot-plug a relative mouse first:  echo "device_add usb-mouse,id=um1,bus=ehci.0" | socat - UNIX-CONNECT:/tmp/hs-monitor.sock
# SCALE (default 1.0; ~0.62 measured) compensates macOS pointer acceleration at slow speeds; accuracy is about +-10px, so verify with a screenshot.
S=/tmp/hs-monitor.sock; SC=${SCALE:-1.0}
m(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:$S >/dev/null; }
case $1 in
 corner) for i in 1 2 3 4 5 6; do m "mouse_move -1000 -1000"; sleep 0.12; done; sleep 0.5;;
 goto) $0 corner; X=$2; Y=$3; sx=$(python3 -c "print(round($X/$SC))"); sy=$(python3 -c "print(round($Y/${SCALEY:-$SC}))")
   n=$(( (sx>sy?sx:sy)/12 + 1 )); for ((i=0;i<n;i++)); do dx=$((sx/n)); dy=$((sy/n)); m "mouse_move $dx $dy"; sleep 0.12; done; sleep 0.5;;
 click) m "mouse_button 1"; sleep 0.15; m "mouse_button 0"; sleep 0.3;;
esac
