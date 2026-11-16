#!/bin/sh
# kc-restore-stock.sh -- put the stock Boot/System collections back (unsuffixed), move any .development collections out of the way, snapshot.
O=/tmp/kc-restore-stock.out; : > $O
{
VOL=/tmp/sysmnt; DATA="/Volumes/Tahoe - Data"; KCDIR=$VOL/System/Library/KernelCollections
umount /Volumes/Tahoe 2>/dev/null; diskutil mount disk3s2 2>&1 | tail -1
mkdir -p $VOL; mount | grep -q " $VOL " || mount -o nobrowse -t apfs /dev/disk3s4 $VOL
PREBOOT=$(ls -d /Volumes/Preboot/*/boot/System/Library/KernelCollections | head -1)
mkdir -p "$DATA/kcbackup/off"
for e in BootKernelExtensions.kc.elides SystemKernelExtensions.kc.elides; do [ -f "$DATA/kcbackup/$e" ] || cp "$KCDIR/$e" "$DATA/kcbackup/" 2>/dev/null; done   # stock elides too (first run only)

for f in "$KCDIR"/*.development* "$PREBOOT"/*.development*; do [ -e "$f" ] && mv "$f" "$DATA/kcbackup/off/$(basename "$f")"; done
cp "$DATA/kcbackup/BootKernelExtensions.kc" "$DATA/kcbackup/SystemKernelExtensions.kc" $KCDIR/ && cp "$DATA/kcbackup/BootKernelExtensions.kc" "$PREBOOT/"
cp "$DATA/kcbackup/BootKernelExtensions.kc.elides" "$DATA/kcbackup/SystemKernelExtensions.kc.elides" $KCDIR/ 2>/dev/null
echo "stock collections restored"
bless --folder "$VOL/System/Library/CoreServices" --bootefi --create-snapshot 2>&1 | tail -3
ls -la $KCDIR "$PREBOOT" | grep -v im4m
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/kc-restore-stock.out
echo uploaded
