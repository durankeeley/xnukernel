#!/bin/sh
# kc-resnap.sh -- (Recovery) mount the system volume read-write, create a fresh bootable snapshot of its CURRENT contents and
# report the snapshot list plus the collection UUIDs. Use when the "Will root to" snapshot is not the one holding the current collections.
O=/tmp/kc-resnap.out; : > $O
{
VOL=/private/tmp/sysmnt; umount /Volumes/Tahoe 2>/dev/null; umount $VOL 2>/dev/null; mkdir -p $VOL
mount -o nobrowse -t apfs /dev/disk3s4 $VOL
uuid() { /usr/bin/perl -e 'open(F,"<",$ARGV[0]) or exit; binmode F; read(F,$b,65536); ($m,$c,$s,$ft,$n,$sz)=unpack("L6",$b); $o=32; for(1..$n){ ($cmd,$cs)=unpack("L2",substr($b,$o,8)); if($cmd==0x1b){ print uc(unpack("H*",substr($b,$o+8,16))); last } last if $cs==0; $o+=$cs }' "$1"; }
for f in $VOL/System/Library/KernelCollections/BootKernelExtensions.kc $VOL/System/Library/KernelCollections/SystemKernelExtensions.kc; do echo "$(uuid $f)  $f"; done
echo "== before"; diskutil apfs listSnapshots disk3s4 2>&1 | grep -E "Name:|XID" | tail -6
bless --folder "$VOL/System/Library/CoreServices" --bootefi --create-snapshot 2>&1 | tail -3
echo "== after"; diskutil apfs listSnapshots disk3s4 2>&1 | grep -E "Name:|XID" | tail -6
sync
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/kc-resnap.out
echo uploaded
