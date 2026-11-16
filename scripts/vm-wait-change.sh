#!/bin/bash
# vm-wait-change.sh <out.png> [max_seconds] -- nudge the guest (shift key) and screenshot until the picture differs from the previous capture (guest redraws lazily).
O=${1:-/tmp/wc.png}; MAX=${2:-120}; cd "$(dirname "$0")/.."
scripts/vm-shot.sh /tmp/wc-prev.png >/dev/null 2>&1; P=$(md5sum /tmp/wc-prev.png | cut -c1-32); t=0
while [ $t -lt $MAX ]; do
  sleep 8; t=$((t+8)); scripts/vm.sh key shift; sleep 4; t=$((t+4))
  scripts/vm-shot.sh $O >/dev/null 2>&1; N=$(md5sum $O | cut -c1-32)
  [ "$N" != "$P" ] && { sleep 6; scripts/vm-shot.sh $O >/dev/null 2>&1; echo "changed after ${t}s"; exit 0; }
done
echo "no change after ${MAX}s"; exit 1
