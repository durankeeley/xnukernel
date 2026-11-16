#!/bin/bash
# iterate.sh [kernel]   Full test cycle for a freshly built kernel (default: build/xnu/obj/.../kernel.unstripped)
#  1. boot the STOCK system, install kernel + rebuilt prelinked kernel as *.xcross
#  2. reboot with kcsuffix=xcross (-no-reboot so a triple fault stops QEMU), wait, collect result
# Results: build/iterate/{screen.png,serial.log,status.txt}
set -u
W=$(cd "$(dirname "$0")/.." && pwd); S=$W/scripts; A=$W/build/agent; O=$W/build/iterate; mkdir -p $O
KERNEL=${1:-$W/build/xnu/obj/RELEASE_X86_64/kernel.unstripped}
WAIT=${WAIT:-120}
stopvm(){ pgrep -x qemu-system-x86 >/dev/null && { printf 'quit\n' | socat -t1 - UNIX-CONNECT:/tmp/hs-monitor.sock >/dev/null 2>&1; sleep 3; }; }
startvm(){ rm -f /tmp/hs-monitor.sock /tmp/hs-serial.log; (setsid nohup $S/boot-highsierra.sh "$@" > /tmp/hs-qemu.log 2>&1 &); sleep 5
           $S/vm.sh waitpicker 400 && { sleep 3; $S/vm.sh pickmac; }; }
pgrep -af agent-server.py >/dev/null || (setsid nohup $S/agent-server.py > $A/server.log 2>&1 &)
cp "$KERNEL" $W/out/kernel.latest; cp "$KERNEL" $A/files/kernel.xcross
echo "== 1. stock boot + prelink"
stopvm; $S/oc-set.sh 'keepsyms=1' 0x7f >/dev/null; startvm
[ -n "${PRE_CMD:-}" ] && { echo "== pre-command in the guest: ${PRE_CMD:0:60}..."; $S/guest-run.sh "$PRE_CMD" ${PRE_TIMEOUT:-1500}; }
$S/guest-run.sh 'set -e; cd /tmp; curl -s -o kernel.xcross http://10.0.2.2:8000/files/kernel.xcross
 kextcache -z -a x86_64 -K /tmp/kernel.xcross -all-loaded -c /tmp/pk2 /System/Library/Extensions > /tmp/kc2.log 2>&1 || true
 test -s /tmp/pk2
 cp /tmp/pk2 /System/Library/PrelinkedKernels/prelinkedkernel.xcross; cp /tmp/kernel.xcross /System/Library/Kernels/kernel.xcross
 chmod 644 /System/Library/PrelinkedKernels/prelinkedkernel.xcross; chmod 755 /System/Library/Kernels/kernel.xcross
 grep -c "couldn.t find" /tmp/kc2.log || true; curl -s -T /tmp/kc2.log http://10.0.2.2:8000/upload/kc2.log; sync; sync; echo installed' 900 | tee $O/install.txt
echo "== 2. boot our kernel"
stopvm; $S/oc-set.sh "${BOOTARGS:--v debug=0x8 serial=3 keepsyms=1 kcsuffix=xcross}" 0x7f >/dev/null; startvm -no-reboot
sleep $WAIT; { pgrep -x qemu-system-x86 >/dev/null && echo "qemu: RUNNING after ${WAIT}s (no triple fault)" || echo "qemu: EXITED (triple fault / shutdown)"; } | tee $O/status.txt
timeout 30 $S/vm.sh shot 2>/dev/null; cp /tmp/hs.png $O/screen.png; cp /tmp/hs-serial.log $O/serial.log 2>/dev/null
tail -c 1500 $O/serial.log | tr -d '\033\r' | tail -12
