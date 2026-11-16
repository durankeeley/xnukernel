#!/bin/bash
# sample-rip.sh [kernel] [slide-hex] [count]   Sample guest RIP via the QEMU monitor and symbolize (find hangs).
W=$(cd "$(dirname "$0")/.." && pwd); K=${1:-$W/out/kernel.latest}; SL=${2:-0xc800000}; N=${3:-8}
for i in $(seq 1 $N); do
  printf 'info registers\n' | socat -t2 - UNIX-CONNECT:/tmp/hs-monitor.sock 2>/dev/null | tr -d '\r' | grep -o "RIP=[0-9a-f]*" | head -1 | cut -d= -f2 | sed 's/^/0x/' | xargs -r $W/scripts/addr2sym.py $K $SL
  sleep 1
done | sort | uniq -c | sort -rn
