#!/bin/bash
# kvm-trap-trace.sh [cpu-model]   Boot the installed *.xcross kernel under KVM (paused), attach gdb with HARDWARE breakpoints on the
# kernel's trap handlers (they work on KVM and before the kernel is in memory) and log every trap (trap number, RIP, error code, CR2)
# until the guest dies. Result: build/kvm-trace.log (last lines = what led to the triple fault).
W=$(cd "$(dirname "$0")/.." && pwd); S=$W/scripts; K=${KERNEL_SYMS:-$W/out/kernel.amd-k8.x86_64}
export CPU=${1:-phenom,family=16,model=4,stepping=2,kvm=off}
pgrep -x qemu-system-x86 >/dev/null && { printf 'quit\n' | socat -t1 - UNIX-CONNECT:/tmp/hs-monitor.sock >/dev/null 2>&1; sleep 3; }
$S/oc-set.sh "-v debug=0x8 serial=3 keepsyms=1 kcsuffix=xcross slide=100" 0x7f >/dev/null
rm -f /tmp/hs-monitor.sock /tmp/hs-serial.log
cat > /tmp/kvmtrace.gdb <<GDB
set pagination off
set confirm off
set architecture i386:x86-64
symbol-file $K -o 0xc800000
target remote :1234
hbreak kernel_trap
commands
silent
printf "KTRAP trapno=%d rip=%lx err=%lx cr2=%lx\\n", ((x86_saved_state_t*)state)->uss.ss_64.isf.trapno, ((x86_saved_state_t*)state)->uss.ss_64.isf.rip, ((x86_saved_state_t*)state)->uss.ss_64.isf.err, ((x86_saved_state_t*)state)->uss.ss_64.cr2
continue
end
hbreak user_trap
commands
silent
printf "UTRAP trapno=%d rip=%lx\\n", ((x86_saved_state_t*)saved_state)->uss.ss_64.isf.trapno, ((x86_saved_state_t*)saved_state)->uss.ss_64.isf.rip
continue
end
hbreak panic
commands
silent
printf "PANIC: %s\\n", \$rdi
bt 8
continue
end
continue
GDB
(GDBPORT=1234 setsid nohup $S/boot-highsierra.sh -S -no-reboot > /tmp/hs-qemu.log 2>&1 &)
sleep 5
(setsid nohup gdb -q -batch -x /tmp/kvmtrace.gdb > $W/build/kvm-trace.log 2>&1 &)
sleep 3
$S/vm.sh waitpicker 120 && { sleep 3; $S/vm.sh pickmac; }
for i in $(seq 1 40); do pgrep -x qemu-system-x86 >/dev/null || break; sleep 3; done
sleep 2; pgrep -x qemu-system-x86 >/dev/null && echo "[trace] qemu still running (no death within 2 min)" || echo "[trace] qemu exited"
