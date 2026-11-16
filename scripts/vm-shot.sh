#!/bin/bash
# vm-shot.sh <out.png>: screendump to a private path (the monitor-based vm.sh shot shares /tmp/hs.png and races with other callers)
rm -f /tmp/my.ppm   # a stale dump from an earlier call must never pass for a new one
printf 'screendump /tmp/my.ppm\n' | socat - UNIX-CONNECT:/tmp/hs-monitor.sock >/dev/null 2>&1; sleep 1
[ -f /tmp/my.ppm ] && python3 -c "from PIL import Image; Image.open('/tmp/my.ppm').save('$1')"
