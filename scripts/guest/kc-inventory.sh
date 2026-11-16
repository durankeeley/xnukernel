#!/bin/sh
# kc-inventory.sh -- list every kernel collection file (Preboot, live volume, backup) with size, date and UUID/metadata.
O=/tmp/kc-inventory.out; : > $O
{
VOL=/tmp/sysmnt; DATA="/Volumes/Tahoe - Data"; KCDIR=$VOL/System/Library/KernelCollections
umount /Volumes/Tahoe 2>/dev/null; diskutil mount disk3s2 2>&1 | tail -1
mkdir -p $VOL; mount | grep -q " $VOL " || mount -o nobrowse -t apfs /dev/disk3s4 $VOL
PREBOOT=$(ls -d /Volumes/Preboot/*/boot/System/Library/KernelCollections | head -1)
inv() { f="$1"; [ -f "$f" ] || return; echo "-- $f"; ls -la "$f" | awk '{print "   size",$5,$6,$7,$8}'
  case "$f" in *Boot*) kmutil inspect -B "$f" --show-kernel-uuid-only --no-header 2>&1 | grep -v KextExclude | head -2;;
               *System*) kmutil inspect -S "$f" --show-collection-metadata --no-header 2>&1 | grep -v KextExclude | head -6;; esac; }
echo "===== PREBOOT ($PREBOOT)"; for f in "$PREBOOT"/*.kc "$PREBOOT"/*.kc.*; do case "$f" in *.im4m|*.elides) continue;; esac; inv "$f"; done
echo "===== LIVE VOLUME"; for f in "$KCDIR"/*.kc "$KCDIR"/*.kc.*; do case "$f" in *.elides) continue;; esac; inv "$f"; done
echo "===== BACKUP"; for f in "$DATA"/kcbackup/*; do inv "$f"; done
echo "===== snapshots"; diskutil apfs listSnapshots disk3s4 2>&1 | grep -E "Name:|XID|Purgeable" | head -12
echo "===== Preboot dir listing"; ls -la "$PREBOOT" | grep -v im4m
echo "===== other places a KC may live"; find $VOL/System/Library -maxdepth 3 -name '*.kc*' 2>/dev/null | head; ls $VOL/System/Library/Kernels 2>/dev/null
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/kc-inventory.out
echo uploaded
