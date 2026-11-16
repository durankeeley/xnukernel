#!/bin/sh
# find-stale-path.sh -- (Recovery) find files that still contain the build-time mount path "tmp/sysmnt" (kernelmanagerd resolves kext bundles through it).
O=/tmp/find-stale-path.out; : > $O
{
DATA="/Volumes/Tahoe - Data"; VOL=/private/tmp/sysmnt
umount /Volumes/Tahoe 2>/dev/null; mkdir -p $VOL; mount | grep -q " $VOL " || mount -o nobrowse,rdonly -t apfs /dev/disk3s4 $VOL
PRE=$(ls -d /Volumes/Preboot/*/boot/*/System/Library/Caches/com.apple.kernelcaches 2>/dev/null | head -1)
echo "preboot kernelcaches dir: $PRE"
for d in "$DATA/private/var/db/KernelExtensionManagement" "$DATA/private/var/db" "$DATA/Library/Preferences" "$DATA/Library/StagedExtensions" "$DATA/Library/KernelCollections" "$VOL/System/Library/KernelCollections" /Volumes/Preboot; do
  echo "== grep in $d"
  grep -rlI --exclude-dir=diagnostics --exclude-dir=uuidtext "tmp/sysmnt" "$d" 2>/dev/null | head -15
  grep -rl "tmp/sysmnt" "$d" --include='*.kc*' --include='*.plist' --include='*.db' --include='*.sqlite*' 2>/dev/null | head -10
done
echo "== KernelExtensionManagement"; ls -laR "$DATA/private/var/db/KernelExtensionManagement" 2>&1 | head -40
echo "== Library/KernelCollections"; ls -la "$DATA/Library/KernelCollections" 2>&1 | head
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/find-stale-path.out
echo uploaded
