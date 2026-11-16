#!/bin/sh
# install-disk-build.sh -- guest side of scripts/make-install-disk.sh. Runs in the installer's Recovery Terminal, with attached:
#   the blank target disk, and the exFAT staging disk (label ESD) that holds profile.env, the installer files, the kexts, our kernel, OpenCore.
# KIND=basesystem-packages: GPT, EFI partition with OpenCore, one HFS+ volume with the BaseSystem, the installer app's SharedSupport folder
# (InstallESD.dmg, InstallInfo.plist, BaseSystem.dmg, chunklists: what createinstallmedia writes) and our kernel.
# Progress goes to the host agent server (build/agent/files/install-disk.out).
O=/tmp/install-disk.out; : > $O
up() { curl -s -T $O http://10.0.2.2:8000/upload/install-disk.out > /dev/null 2>&1; }
( while true; do sleep 20; up; done ) &
UP=$!
{
V=$(ls -d /Volumes/ESD* | head -1); echo "staging: $V"
. $V/profile.env; echo "profile: $NAME ($KIND)"
D=$(diskutil list | grep -F "*$DISK_LABEL" | awk '{print $NF}'); echo "target disk: $D ($DISK_LABEL)"; [ -n "$D" ] || { echo "no disk of size $DISK_LABEL"; exit 1; }
T=/Volumes/$VOLNAME
echo "== partition"
if [ -d $T ]; then echo "already partitioned"; else diskutil partitionDisk $D GPT JHFS+ $VOLNAME 100% || exit 1; fi
[ -d $T ] && [ "$T" != / ] || { echo "no $VOLNAME volume"; exit 1; }
echo "== mount the BaseSystem image"
[ -d /Volumes/BSsrc/System ] || hdiutil attach -nobrowse -noverify -readonly -mountpoint /Volumes/BSsrc $V/BaseSystem.dmg | tail -1
echo "== copy the BaseSystem (asr restore is not supported in Recovery; ditto keeps xattrs and permissions)"
[ -d "$T/System/Library/CoreServices" ] || ditto /Volumes/BSsrc $T 2>&1 | tail -3
[ -d "$T/System/Library/CoreServices" ] || { echo "BaseSystem copy failed"; exit 1; }
echo "== SharedSupport: the layout createinstallmedia makes (10.13's installer app reads this, not System/Installation/Packages)"
APP="$T/Install macOS High Sierra.app"; [ -d "$APP/Contents" ] || { echo "no installer app in the BaseSystem"; exit 1; }
rm -rf "$T/System/Installation/Packages"          # a copy of the packages from an earlier try, not used
S="$APP/Contents/SharedSupport"; rm -rf "$S"; mkdir -p "$S"
for f in InstallESD.dmg InstallInfo.plist BaseSystem.dmg BaseSystem.chunklist AppleDiagnostics.dmg AppleDiagnostics.chunklist; do cp $V/$f "$S/" || echo "missing $f"; done
ls -la "$S"
echo "== kernel and prelinked kernel (suffix .$KCSUFFIX)"
W=$V/work; mkdir -p $W
# -all-loaded keeps the cache to the kexts this running environment has loaded, resolved from the full set (a cache of every kext is 95 MB
# and boot.efi fails to read it; the installer's own kext folder is stripped and cannot be used)
kextcache -z -a x86_64 -K $V/kernel.$KCSUFFIX -no-authentication -all-loaded -c $W/pk.$KCSUFFIX $V/Extensions-full 2>&1 | grep -v "dependency '" | tail -5
mkdir -p "$T/System/Library/Kernels" "$T/System/Library/PrelinkedKernels"
cp $V/kernel.$KCSUFFIX "$T/System/Library/Kernels/kernel.$KCSUFFIX"; cp $W/pk.$KCSUFFIX "$T/System/Library/PrelinkedKernels/prelinkedkernel.$KCSUFFIX"
ls -la "$T/System/Library/Kernels" "$T/System/Library/PrelinkedKernels"
echo "== OpenCore on the EFI partition"
diskutil mount ${D}s1; cp -R $V/OC-EFI/EFI /Volumes/EFI/; ls -R /Volumes/EFI | head -12
sync; echo "INSTALL-DISK-DONE"
} >> $O 2>&1
sleep 3; up; kill $UP
