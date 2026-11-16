#!/bin/sh
# kc-uuids.sh -- (Recovery) print LC_UUID, size and mtime of every Boot/System/Auxiliary kernel collection on the system volume and in Preboot.
O=/tmp/kc-uuids.out; : > $O
{
VOL=/private/tmp/sysmnt; umount /Volumes/Tahoe 2>/dev/null; mkdir -p $VOL; mount | grep -q " $VOL " || mount -o nobrowse,rdonly -t apfs /dev/disk3s4 $VOL
diskutil mount disk3s2 >/dev/null 2>&1
uuid() { /usr/bin/perl -e 'open(F,"<",$ARGV[0]) or exit; binmode F; read(F,$b,65536); ($m,$c,$s,$ft,$n,$sz)=unpack("L6",$b); $o=32; for(1..$n){ ($cmd,$cs)=unpack("L2",substr($b,$o,8)); if($cmd==0x1b){ print uc(unpack("H*",substr($b,$o+8,16))); last } last if $cs==0; $o+=$cs }' "$1"; }
for f in $(find $VOL/System/Library/KernelCollections /Volumes/Preboot "/Volumes/Tahoe - Data/kcbackup" \( -name 'BootKernelExtensions.kc*' -o -name 'SystemKernelExtensions.kc*' -o -name 'AuxiliaryKernelExtensions.kc*' \) ! -name '*.elides' 2>/dev/null); do
  echo "$(uuid "$f")  $(stat -f '%z %Sm' -t '%m-%d %H:%M' "$f")  $f"
done
echo "== snapshots"; diskutil apfs listSnapshots disk3s4 2>&1 | grep -E "Name:|XID|Purgeable" | tail -8
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/kc-uuids.out
echo uploaded
