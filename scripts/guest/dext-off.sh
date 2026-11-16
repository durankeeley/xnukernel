#!/bin/sh
# dext-off.sh [pattern...] -- move DriverKit extensions (default: the HID dext) out of /System/Library/DriverExtensions and snapshot.
# DriverKit does not work with the iig stand-in, so dexts crash-loop; removing the HID dext lets the in-kernel HID kexts claim USB HID devices.
O=/tmp/dext-off.out; : > $O
{
VOL=/tmp/sysmnt; DATA="/Volumes/Tahoe - Data"; DX=$VOL/System/Library/DriverExtensions
umount /Volumes/Tahoe 2>/dev/null; mkdir -p $VOL; mount | grep -q " $VOL " || mount -o nobrowse -t apfs /dev/disk3s4 $VOL
mkdir -p "$DATA/dext-off"
echo "DriverExtensions on the volume:"; ls $DX | head -60
PATS=${*:-AppleUserHIDDrivers}
for p in $PATS; do for d in $DX/*$p*; do [ -e "$d" ] && { mv "$d" "$DATA/dext-off/"; echo "moved $(basename $d)"; }; done; done
bless --folder "$VOL/System/Library/CoreServices" --bootefi --create-snapshot 2>&1 | tail -3
ls $DX | wc -l
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/dext-off.out
echo uploaded
