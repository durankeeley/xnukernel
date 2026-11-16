#!/bin/bash
# tcg-amd-boot.sh <am2|am3|am3plus> [seconds] [extra boot-args]
# Boots the installed *.xcross kernel under TCG with an AMD CPU model that lacks SSSE3/SSE4, with opemu_log=1, and
# reports the OPEMU lines from the kernel serial log plus a few screenshots. TCG is slow: allow 20+ minutes for userland.
W=$(cd "$(dirname "$0")/.." && pwd); S=$W/scripts; F=$W/build/frames; mkdir -p $F; rm -f $F/*
case ${1:-am2} in
  am2)     CPU='Opteron_G2,hypervisor=on,kvm=off,vmware-cpuid-freq=on,tsc-frequency=2000000000' ;;
  am3)     CPU='phenom,family=16,model=4,stepping=2,hypervisor=on,kvm=off,vmware-cpuid-freq=on,tsc-frequency=2000000000' ;;
  am3plus) CPU='Opteron_G4,hypervisor=on,kvm=off,vmware-cpuid-freq=on,tsc-frequency=2000000000' ;;
  *) echo "usage: $0 am2|am3|am3plus [seconds] [boot-args]"; exit 1 ;;
esac
export CPU
ARGS="-v debug=0x8 serial=3 keepsyms=1 kcsuffix=xcross slide=100 opemu_log=1 ${3:-}"
pgrep -x qemu-system-x86 >/dev/null && { printf 'quit\n' | socat -t1 - UNIX-CONNECT:/tmp/hs-monitor.sock >/dev/null 2>&1; sleep 3; }
$S/oc-set.sh "$ARGS" 0x7f >/dev/null
rm -f /tmp/hs-monitor.sock /tmp/hs-serial.log
(setsid nohup $S/boot-tcg.sh > /tmp/hs-qemu.log 2>&1 &); sleep 5
$S/vm.sh waitpicker 600 && { sleep 3; $S/vm.sh pickmac; } || { echo "picker not seen"; exit 1; }
t0=$(date +%s); i=0; last=0
while [ $(( $(date +%s) - t0 )) -lt ${2:-1500} ]; do
  sleep 20; i=$((i+1)); timeout 30 $S/vm.sh shot 2>/dev/null; cp /tmp/hs.png $F/f$(printf %03d $i).png
  pgrep -x qemu-system-x86 >/dev/null || { echo "[tcg] QEMU exited after $(( $(date +%s) - t0 ))s"; break; }
  c=$(tr -d '\033\r' < /tmp/hs-serial.log 2>/dev/null | grep -a -o '([0-9]* so far)' | tail -1 | tr -dc 0-9); c=${c:-0}
  [ "$c" != "$last" ] && { el=$(( $(date +%s) - t0 )); echo "[tcg] ${el}s: $c emulated ssse3/sse4 instructions (+$(( (c - ${last:-0}) / 20 ))/s)"; last=$c; }
done
echo "== OPEMU lines in the kernel serial log:"; tr -d '\033\r' < /tmp/hs-serial.log | grep -a "OPEMU" | head -40
echo "== guest reached: $(tr -d '\033\r' < /tmp/hs-serial.log | grep -a -c 'IOConsoleUsers') IOConsoleUsers lines, $(wc -c < /tmp/hs-serial.log) serial bytes"
