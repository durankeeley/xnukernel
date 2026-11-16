#!/bin/sh
# hs-install.sh -- run in the High Sierra Recovery Terminal (scripts/hs-recovery.sh opens it, with HS_ESD=ESD-exfat.img attached).
# Stage 1 of an offline 10.13.6 install: erase the blank 64 GB disk, give the Install app the SharedSupport folder it expects
# (the exFAT media holds InstallESD.dmg, BaseSystem.dmg, InstallInfo.plist), run startosinstall. The guest restarts into stage 2.
# Progress is uploaded to the host agent server (build/agent/files/hs-install.out).
O=/tmp/hs-install.out; : > $O
up() { curl -s -T $O http://10.0.2.2:8000/upload/hs-install.out > /dev/null 2>&1; }
( while true; do sleep 20; up; done ) &
UP=$!
{
# the installer's package signatures expired in 2019: install with the clock set back
date 1220120018
echo "== erase"
# the disk numbers change with the attached media: pick the blank 64 GB disk by size
D=$(diskutil list | grep -E "\*64\.4 GB" | awk '{print $NF}'); echo "target disk: $D"
[ -n "$D" ] || { echo "no 64 GB target disk"; exit 1; }
diskutil eraseDisk JHFS+ Macintosh GPT $D || exit 1
echo "== SharedSupport"
mount -uw /
APP="/Install macOS High Sierra.app"
V=$(ls -d /Volumes/ESD* 2>/dev/null | head -1); echo "source volume: $V"; ls $V
rm -rf "$APP/Contents/SharedSupport"; ln -s $V "$APP/Contents/SharedSupport"
echo "== startosinstall"
"$APP/Contents/Resources/startosinstall" --volume /Volumes/Macintosh --agreetolicense --nointeraction
echo "startosinstall exit $?"
} >> $O 2>&1
sleep 5; up; kill $UP
