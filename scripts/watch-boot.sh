#!/bin/bash
# watch-boot.sh [seconds] [boot-args]   Boot the installed *.xcross kernel under KVM (-no-reboot), save a frame every 5s
# to build/frames/, report when QEMU exits. Boot-args default: -v debug=0x8 serial=3 keepsyms=1 kcsuffix=xcross slide=0
W=$(cd "$(dirname "$0")/.." && pwd); S=$W/scripts; F=$W/build/frames; mkdir -p $F; rm -f $F/*
pgrep -x qemu-system-x86 >/dev/null && { printf 'quit\n' | socat -t1 - UNIX-CONNECT:/tmp/hs-monitor.sock >/dev/null 2>&1; sleep 3; }
$S/oc-set.sh "${2:--v debug=0x8 serial=3 keepsyms=1 kcsuffix=xcross slide=100}" 0x7f >/dev/null
rm -f /tmp/hs-monitor.sock /tmp/hs-serial.log; (setsid nohup $S/boot-highsierra.sh -no-reboot > /tmp/hs-qemu.log 2>&1 &); sleep 4
$S/vm.sh waitpicker 180 && { sleep 3; $S/vm.sh pickmac; } || echo '[watch] picker never seen'
t0=$(date +%s); i=0
while [ $(( $(date +%s) - t0 )) -lt ${1:-180} ]; do
  i=$((i+1)); timeout 15 $S/vm.sh shot 2>/dev/null; cp /tmp/hs.png $F/f$(printf %03d $i).png
  pgrep -x qemu-system-x86 >/dev/null || { echo "[watch] QEMU exited after $(( $(date +%s) - t0 ))s (frame $i)"; exit 0; }
  sleep 5
done; echo "[watch] still running after ${1:-180}s ($i frames)"
