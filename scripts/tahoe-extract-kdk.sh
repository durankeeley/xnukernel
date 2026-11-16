#!/bin/bash
# tahoe-extract-kdk.sh <KDK_26.7.3_25G342.kdk.dmg> -- unpack the Kernel Debug Kit into build/kdk-real/kdk (needs 7z, python3, cpio; ~5 GB).
# tahoe-kc-install.sh takes the kext executables that the volume lacks from here. Download the dmg for your guest's build from
# https://github.com/dortania/KdkSupportPkg/releases (the release name is the macOS build, 25G342 for 26.7.3).
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
DMG=$(readlink -f "${1:?usage: tahoe-extract-kdk.sh KDK.dmg}")
T=$W/build/kdk-real/tmp; K=$W/build/kdk-real/kdk
rm -rf "$T"; mkdir -p "$T" "$K"; cd "$T"
7z x -y "$DMG" > /dev/null
7z x -y -opkg "Kernel Debug Kit/KernelDebugKit.pkg" > /dev/null
python3 "$W/scripts/pbzx.py" pkg/KDK.pkg/Payload | (cd "$K" && cpio -idm 2> /dev/null)
cd "$W"; rm -rf "$T"
ls "$K/System/Library/Extensions" | head -3
echo "KDK unpacked into $K"
