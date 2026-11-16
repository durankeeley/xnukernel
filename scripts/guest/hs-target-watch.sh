#!/bin/sh
# hs-target-watch.sh (UNTESTED, part of the unfinished scripts/install-unattended.sh) -- start this in the Recovery Terminal BEFORE the installer, in the background:
#   sh /tmp/watch.sh > /tmp/watch.log 2>&1 &    (Recovery has no nohup)
# It waits until installer stage 1 has written the stage 2 boot files to the target, then runs hs-target.sh (our kernel into stage 2, the kernel and
# agent onto the new volume) and keeps re-running it every 30 s, which is harmless, until the machine restarts. This way nothing has to be typed
# after stage 1, when Recovery stops taking input.
V=/Volumes/Macintosh; B="$V/macOS Install Data/Locked Files/Boot Files"
curl -s -o /tmp/ht.sh http://10.0.2.2:8000/files/hs-target.sh
n=0
while [ $n -lt 720 ]; do
  [ -f "$B/com.apple.Boot.plist" ] && [ -f "$B/prelinkedkernel" -o -f "$B/prelinkedkernel.xcross" ] && break
  n=$((n+1)); sleep 5
done
sleep 20
while true; do sh /tmp/ht.sh "$V"; sleep 30; done
