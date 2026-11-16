#!/bin/bash
# tahoe-cycle.sh -- install the freshly built kernel into the Tahoe guest and boot it:
#   Recovery -> (guest) swap kernel + rebuild BootKernelExtensions.kc.development -> halt -> boot Tahoe with kcsuffix=development.
# Output: /tmp/tahoe-cycle.log ; kernel serial console: /tmp/hs-serial.log.   Usage: scripts/tahoe-cycle.sh [extra boot-args]
cd "$(dirname "$0")/.."
F=build/agent/files
pkill -x qemu-system-x86 2>/dev/null; sleep 2
pgrep -f agent-server >/dev/null || (nohup python3 scripts/agent-server.py >/tmp/agent.log 2>&1 &)
cp build/xnu-tahoe/obj/RELEASE_X86_64/kernel.unstripped out/kernel.tahoe-unstripped.x86_64
cp out/kernel.tahoe-unstripped.x86_64 $F/kernel.xcross2
rm -f $F/kmutil23.out
scripts/tahoe-recovery.sh
sleep 3
scripts/vm.sh key ctrl-u; scripts/vm.sh type " "; sleep 1   # the first keystroke after idle gets lost
scripts/vm.sh type "curl -s http://10.0.2.2:8000/files/kmutil23.sh | sh"; sleep 2; scripts/vm.sh key ret
for i in $(seq 1 90); do [ -s $F/kmutil23.out ] && break; sleep 10; done
[ -s $F/kmutil23.out ] || { echo "guest script never uploaded output" ; exit 1; }
cat $F/kmutil23.out
sleep 3; scripts/vm.sh key ctrl-u; scripts/vm.sh type " "; sleep 1; scripts/vm.sh type "sync; umount /tmp/sysmnt; umount /Volumes/Preboot; sync; halt"; sleep 2; scripts/vm.sh key ret
for i in $(seq 1 60); do pgrep -x qemu-system-x86 >/dev/null || break; sleep 5; done
pgrep -x qemu-system-x86 >/dev/null && { echo "guest did not halt"; exit 1; }
scripts/tahoe-boot-test.sh "$@"
