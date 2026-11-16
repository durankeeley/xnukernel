#!/bin/sh
# hs-usb-build.sh -- run in the High Sierra Recovery Terminal with the blank 11 GB image (HS-USB.img) and the exFAT staging media attached.
# Builds the classic single bootable install disk, but with OpenCore and our kernel on it:
#   GPT: EFI partition (OpenCore) + one HFS+ volume holding the restored BaseSystem, the OS packages (System/Installation/Packages),
#   BaseSystem.dmg + chunklist at the root, and kernel.xcross / prelinkedkernel.xcross.
# Progress goes to the host agent server (build/agent/files/hs-usb.out).
O=/tmp/hs-usb.out; : > $O
up() { curl -s -T $O http://10.0.2.2:8000/upload/hs-usb.out > /dev/null 2>&1; }
( while true; do sleep 20; up; done ) &
UP=$!
{
V=$(ls -d /Volumes/ESD* | head -1); echo "staging: $V"
D=$(diskutil list | grep -E "\*11\.8 GB" | awk '{print $NF}'); echo "usb disk: $D"; [ -n "$D" ] || { echo "no 11.8 GB disk"; exit 1; }
echo "== partition"
if [ -d /Volumes/USB ]; then echo "already partitioned"; else diskutil partitionDisk $D GPT JHFS+ USB 100% || exit 1; fi
T=/Volumes/USB; [ -d $T ] && [ "$T" != / ] || { echo "no USB volume"; exit 1; }
echo "== mount the OS payload and the BaseSystem image"
[ -d /Volumes/InstallESD/Packages ] || hdiutil attach -nobrowse -noverify -shadow $V/esd.shadow -mountpoint /Volumes/InstallESD $V/InstallESD.dmg | tail -1
[ -d /Volumes/BSsrc/System ] || hdiutil attach -nobrowse -noverify -readonly -mountpoint /Volumes/BSsrc $V/BaseSystem.dmg | tail -1
echo "== copy the BaseSystem onto the disk (asr restore is not supported here, ditto keeps xattrs and permissions)"
ditto /Volumes/BSsrc $T 2>&1 | tail -3
[ -d "$T/System/Library/CoreServices" ] || { echo "BaseSystem copy failed"; exit 1; }
du -sh $T | tail -1
echo "== Packages and BaseSystem.dmg"
rm -rf "$T/System/Installation/Packages"; mkdir -p "$T/System/Installation/Packages"   # replaces the alias to the ESD
cp -R /Volumes/InstallESD/Packages/. "$T/System/Installation/Packages/"
cp $V/BaseSystem.dmg $V/BaseSystem.chunklist $V/AppleDiagnostics.dmg $V/AppleDiagnostics.chunklist "$T/"
ls -la "$T/System/Installation/Packages" | head
echo "== kernel and prelinked kernel"
W=$V/work; mkdir -p $W
curl -s -o $W/kernel.xcross http://10.0.2.2:8000/files/kernel.xcross
kextcache -z -a x86_64 -K $W/kernel.xcross -no-authentication -all-loaded -c $W/pk.xcross $V/Extensions-full 2>&1 | grep -v "dependency '" | tail -5
mkdir -p "$T/System/Library/Kernels" "$T/System/Library/PrelinkedKernels"
cp $W/kernel.xcross "$T/System/Library/Kernels/kernel.xcross"; cp $W/pk.xcross "$T/System/Library/PrelinkedKernels/prelinkedkernel.xcross"
ls -la "$T/System/Library/Kernels" "$T/System/Library/PrelinkedKernels"
echo "== OpenCore on the EFI partition"
diskutil mount ${D}s1; cp -R $V/OC-EFI/EFI /Volumes/EFI/; ls -R /Volumes/EFI | head -12
sync; echo "HS-USB-DONE"
} >> $O 2>&1
sleep 3; up; kill $UP
