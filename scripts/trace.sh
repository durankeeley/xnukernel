#!/bin/bash
# trace.sh [kernel-for-symbols]   Boot the installed *.xcross kernel under TCG with exception logging,
# then symbolize the first exceptions. Uses slide=0 so addresses are unslid.  Needs iterate.sh step 1 done.
W=$(cd "$(dirname "$0")/.." && pwd); S=$W/scripts; K=${1:-$W/out/kernel.latest}; T=$W/build/qemu-trace.log
pgrep -x qemu-system-x86 >/dev/null && { printf 'quit\n' | socat -t1 - UNIX-CONNECT:/tmp/hs-monitor.sock >/dev/null 2>&1; sleep 3; }
$S/oc-set.sh "-v debug=0x8 serial=3 keepsyms=1 kcsuffix=xcross slide=100 ${EXTRA_ARGS:-}" 0x7f >/dev/null
rm -f /tmp/hs-monitor.sock "$T" /tmp/hs-serial.log $W/build/qemu-trace.fifo; mkfifo $W/build/qemu-trace.fifo
(setsid nohup $S/trace-filter.py "$T" < $W/build/qemu-trace.fifo > /dev/null 2>&1 &)
(setsid nohup $S/boot-debug.sh > /tmp/hs-qemu.log 2>&1 &); sleep 5
$S/vm.sh waitpicker 400 && { sleep 3; $S/vm.sh pickmac; }
n=0; until ! pgrep -x qemu-system-x86 >/dev/null || [ $n -ge ${TRACE_WAIT:-60} ]; do sleep 10; n=$((n+1)); done
pgrep -x qemu-system-x86 >/dev/null && echo "[trace] still running after $((n*10))s (no triple fault)" || echo "[trace] qemu exited: $(grep -c Triple $T) triple fault(s)"
grep "^ *[0-9]*: v=" "$T" | grep -v "v=ff\|v=20\|v=2[0-9a-f] " | head -${NEXC:-8} | while read -r l; do
  ip=$(echo "$l" | sed 's/.*pc=\([0-9a-f]*\).*/0x\1/'); echo "$(echo "$l" | cut -c1-75) CR2=$(echo "$l" | grep -o 'CR2=[0-9a-f]*') => $($S/addr2sym.py $K ${SLIDE:-0xc800000} $ip)"; done
