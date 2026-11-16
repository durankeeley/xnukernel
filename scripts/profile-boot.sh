#!/bin/bash
# profile-boot.sh <am2|am3> [t1-seconds] [t2-seconds] [top-N]
# Boots under TCG with the hot-block plugin, snapshots the counters at t1 and t2 (touch trigger file), then prints where the guest spent
# its instructions overall and between the snapshots (steady state). Symbols from KERNEL_SYMS (default out/kernel.amd-k8.x86_64).
W=$(cd "$(dirname "$0")/.." && pwd); S=$W/scripts; T1=${2:-120}; T2=${3:-170}; K=${KERNEL_SYMS:-$W/out/kernel.amd-k8.x86_64}
export QEMU_PLUGIN="$S/qemu-plugin/hotblocks.so,out=/tmp/hot.txt"; rm -f /tmp/hot.txt /tmp/hot1.txt /tmp/hot2.txt
( setsid nohup $S/tcg-amd-boot.sh ${1:-am3} $((T2 + 60)) > $W/build/profile-boot.log 2>&1 & )
sleep 30; until pgrep -x qemu-system-x86 >/dev/null; do sleep 2; done
sleep $T1;           touch /tmp/hot.txt.dump; until [ ! -e /tmp/hot.txt.dump ]; do sleep 1; done; cp /tmp/hot.txt /tmp/hot1.txt
sleep $((T2 - T1));  touch /tmp/hot.txt.dump; until [ ! -e /tmp/hot.txt.dump ]; do sleep 1; done; cp /tmp/hot.txt /tmp/hot2.txt
printf 'quit\n' | socat -t1 - UNIX-CONNECT:/tmp/hs-monitor.sock >/dev/null 2>&1
echo "=== whole run (snapshot 2)"; $S/profile-tcg.py /tmp/hot2.txt "$K" 0xc800000 12
echo; echo "=== steady state: between $T1 s and $T2 s"; $S/profile-tcg.py --diff /tmp/hot1.txt /tmp/hot2.txt "$K" 0xc800000 ${4:-30}
