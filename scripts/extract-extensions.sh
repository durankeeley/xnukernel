#!/bin/bash
# extract-extensions.sh <InstallESD.dmg> <out-dir> -- unpack System/Library/Extensions from the OS payload (Packages/Core.pkg) of an installer.
# The installer's own BaseSystem has a stripped kext folder, so a prelinked kernel for the installer must be built from this full set.
# Needs: qemu-img, a Linux hfsplus module (sudo), python3, cpio. Takes about 6 minutes for 10.13.6 (321 kexts, 1.1 GB).
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
ESD=$(readlink -f "${1:?usage: extract-extensions.sh InstallESD.dmg out-dir}"); OUT=${2:?}
RAW=$(mktemp /tmp/esd.XXXX.img); trap 'sudo -n umount /mnt/esd-ro 2>/dev/null; [ -n "${L:-}" ] && sudo -n losetup -d $L; rm -f $RAW' EXIT
qemu-img convert -f dmg -O raw "$ESD" "$RAW"
sudo -n modprobe hfsplus; L=$(sudo -n losetup -fP --show -r "$RAW"); sudo -n mkdir -p /mnt/esd-ro
P=$(ls ${L}p* | tail -1); sudo -n mount -t hfsplus -o ro "$P" /mnt/esd-ro
mkdir -p "$OUT"; cd "$OUT"
python3 "$W/scripts/xarpbzx.py" /mnt/esd-ro/Packages/Core.pkg 2>/dev/null | cpio -idm './System/Library/Extensions/*' 2>&1 | tail -1
echo "$(ls "$OUT/System/Library/Extensions" | wc -l) kexts in $OUT/System/Library/Extensions"
