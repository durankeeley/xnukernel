#!/bin/sh
# hs-kernel.sh [volume] -- run in the High Sierra Recovery Terminal. Builds a prelinked kernel around our kernel from the kexts of the
# environment it runs in (or of an installed volume) and installs both files with the .xcross suffix.
#   hs-kernel.sh            the Recovery / installer environment itself (/ is made writable)
#   hs-kernel.sh /Volumes/Macintosh    an installed system
O=/tmp/hs-kernel.out; : > $O
{
R=${1:-/}
# Recovery's /tmp is a few MB of RAM: work on the writable exFAT media instead
W=$(ls -d /Volumes/ESD* 2>/dev/null | head -1)/work; mkdir -p $W; df -h / $W | cat
[ "$R" = / ] && mount -uw /
curl -s -o $W/kernel.xcross http://10.0.2.2:8000/files/kernel.xcross; ls -l $W/kernel.xcross
if [ "$R" = / ]; then
  # the installer volume has only a stripped copy of the kexts (Apple's own prelinked kernel there was built from more), so use the
  # full set from the OS payload (Core.pkg), staged on the exFAT media as Extensions-full
  X=$(ls -d /Volumes/ESD* | head -1)/Extensions-full
  # -all-loaded keeps the cache to the kexts this environment loaded (about Apple's 29 MB), resolved from the full set
  kextcache -z -a x86_64 -K $W/kernel.xcross -no-authentication -all-loaded -c $W/pk.xcross $X 2>&1 | grep -v "dependency '" | tail -25
else
  kextcache -z -a x86_64 -K $W/kernel.xcross -c $W/pk.xcross "$R/System/Library/Extensions"
fi
echo "kextcache exit $?"; ls -l $W/pk.xcross
mkdir -p "$R/System/Library/Kernels" "$R/System/Library/PrelinkedKernels"
cp $W/kernel.xcross "$R/System/Library/Kernels/kernel.xcross"; chmod 755 "$R/System/Library/Kernels/kernel.xcross"
cp $W/pk.xcross "$R/System/Library/PrelinkedKernels/prelinkedkernel.xcross"; chmod 644 "$R/System/Library/PrelinkedKernels/prelinkedkernel.xcross"
ls -l "$R/System/Library/Kernels" "$R/System/Library/PrelinkedKernels"
sync; echo "HS-KERNEL-DONE"
} >> $O 2>&1
curl -s -T $O http://10.0.2.2:8000/upload/hs-kernel.out
