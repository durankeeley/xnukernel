#!/bin/bash
# tcg-time-to-login.sh <am2|am3|am3plus> [timeout-seconds]
# The benchmark that matters: how long does macOS take to reach the login window on a mimicked AMD CPU under TCG, with a quiet boot
# (no -v, no serial console: console output is very expensive under TCG). Detects the login window by screenshot.
W=$(cd "$(dirname "$0")/.." && pwd); S=$W/scripts
case ${1:-am3} in
  am2)     CPU='Opteron_G2,hypervisor=on,kvm=off,vmware-cpuid-freq=on,tsc-frequency=2000000000' ;;
  am3)     CPU='phenom,family=16,model=4,stepping=2,hypervisor=on,kvm=off,vmware-cpuid-freq=on,tsc-frequency=2000000000' ;;
  am3plus) CPU='Opteron_G4,hypervisor=on,kvm=off,vmware-cpuid-freq=on,tsc-frequency=2000000000' ;;
  *) echo "usage: $0 am2|am3|am3plus [timeout]"; exit 1 ;;
esac
export CPU
ARGS="keepsyms=1 kcsuffix=xcross slide=100 ${EXTRA_ARGS:-}"
pgrep -x qemu-system-x86 >/dev/null && { printf 'quit\n' | socat -t1 - UNIX-CONNECT:/tmp/hs-monitor.sock >/dev/null 2>&1; sleep 3; }
$S/oc-set.sh "$ARGS" 0x7f >/dev/null; rm -f /tmp/hs-monitor.sock /tmp/hs-serial.log
(setsid nohup $S/boot-tcg.sh > /tmp/hs-qemu.log 2>&1 &); sleep 5
$S/vm.sh waitpicker 600 && { sleep 3; $S/vm.sh pickmac; } || { echo "picker not seen"; exit 1; }
t0=$(date +%s)
if $S/vm.sh waitlogin ${2:-1800}; then echo "[time-to-login] $1: $(( $(date +%s) - t0 )) s from selecting the volume to the login window"; cp /tmp/hs.png $W/build/login-$1.png
else echo "[time-to-login] $1: login window not reached within ${2:-1800} s"; fi
printf 'quit\n' | socat -t1 - UNIX-CONNECT:/tmp/hs-monitor.sock >/dev/null 2>&1
