#!/bin/bash
# make-install-disk.sh <profile> [out.img] -- build ONE bootable install disk that carries OpenCore, the installer and our kernel,
# the way a single install USB stick used to be made. Boot the result with e.g. HS_USB=<out.img> scripts/boot-highsierra.sh,
# or write it to a stick with dd.
#
# What is version specific lives in scripts/install-disk/<profile>.conf (installer files, kernel, boot-args, how to reach Recovery's Terminal).
# This script stages the inputs on an exFAT disk, boots an installer in the VM with the blank target and the staging disk attached, opens
# its Terminal and runs scripts/guest/install-disk-build.sh there (the BaseSystem copy needs macOS: ditto, hdiutil, kextcache).
#
#   scripts/make-install-disk.sh highsierra
#   scripts/make-install-disk.sh highsierra /data/hs-usb.img      FORCE=1 overwrites an existing image
# Env: OSXKVM (OSX-KVM checkout, default ../../OSX-KVM), BOOT_SCRIPT, plus anything the profile reads (INSTALLER_DIR, KERNEL, ...).
set -eu
W=$(cd "$(dirname "$0")/.." && pwd); cd "$W"
P=${1:?usage: make-install-disk.sh <profile> [out.img]}
. scripts/install-disk/$P.conf
[ "$KIND" = basesystem-packages ] || { echo "profile $P (KIND=$KIND) is not implemented, see the comments in scripts/install-disk/$P.conf"; exit 1; }
OSXKVM=${OSXKVM:-$(cd "$W/../.." 2>/dev/null && pwd)/OSX-KVM}; [ -d "$OSXKVM" ] || OSXKVM=$(cd "$W/.." && pwd)/OSX-KVM
OUT=$(readlink -m "${2:-$OSXKVM/install-$NAME.img}")
STAGE=$OSXKVM/stage-$NAME.img
for f in "$INSTALLER_DIR/BaseSystem.dmg" "$INSTALLER_DIR/BaseSystem.chunklist" "$ESD_DMG" "$KERNEL" "$BOOTSTRAP_BASE" "$OSXKVM/OpenCore/OpenCore.qcow2"; do
  [ -e "$f" ] || { echo "missing: $f"; exit 1; }
done
[ -d "$EXTENSIONS_DIR" ] || { echo "missing: $EXTENSIONS_DIR (scripts/extract-extensions.sh $ESD_DMG <dir>)"; exit 1; }
[ ! -e "$OUT" ] || [ -n "${FORCE:-}" ] || { echo "$OUT exists (FORCE=1 to overwrite)"; exit 1; }

echo "== stage the inputs on an exFAT disk: $STAGE"
need=$(( $(du -sb "$INSTALLER_DIR/BaseSystem.dmg" "$ESD_DMG" "$EXTENSIONS_DIR" "$KERNEL" | awk '{s+=$1} END{print s}') + 600*1024*1024 ))
rm -f "$STAGE"; truncate -s $need "$STAGE"; mkfs.exfat -L ESD "$STAGE" > /dev/null
L=$(sudo -n losetup -f --show "$STAGE"); M=$(mktemp -d /tmp/stage.XXXX)
trap 'sudo -n umount "$M" 2>/dev/null; sudo -n losetup -d "$L" 2>/dev/null; sudo -n qemu-nbd -d /dev/nbd0 >/dev/null 2>&1; rmdir "$M" 2>/dev/null' EXIT
sudo -n mount -o uid=$(id -u),gid=$(id -g) "$L" "$M"
cp "$ESD_DMG" "$M/InstallESD.dmg"
for f in BaseSystem.dmg BaseSystem.chunklist AppleDiagnostics.dmg AppleDiagnostics.chunklist; do [ -f "$INSTALLER_DIR/$f" ] && cp "$INSTALLER_DIR/$f" "$M/"; done
# InstallInfo.plist the way gibMacOS patches it: the payload is now InstallESD.dmg and has no chunklist
python3 - "$INSTALLER_DIR/InstallInfo.plist" "$M/InstallInfo.plist" <<'PY'
import plistlib, sys
d = plistlib.load(open(sys.argv[1], 'rb')); pii = d['Payload Image Info']
pii['URL'] = pii['URL'].replace('InstallESDDmg.pkg', 'InstallESD.dmg'); pii['id'] = pii['id'].replace('com.apple.pkg.InstallESDDmg', 'com.apple.dmg.InstallESD')
pii.pop('chunklistURL', None); pii.pop('chunklistid', None)
plistlib.dump(d, open(sys.argv[2], 'wb'))
PY
cp -r "$EXTENSIONS_DIR" "$M/Extensions-full"
cp "$KERNEL" "$M/kernel.$KCSUFFIX"
# OpenCore: take the EFI folder from the OSX-KVM OpenCore image and set our NVRAM values
sudo -n modprobe nbd max_part=8; sudo -n qemu-nbd -d /dev/nbd0 >/dev/null 2>&1 || true
sudo -n qemu-nbd -c /dev/nbd0 -r "$OSXKVM/OpenCore/OpenCore.qcow2"; sleep 1
sudo -n mkdir -p /mnt/oc; sudo -n mount -o ro /dev/nbd0p1 /mnt/oc; mkdir -p "$M/OC-EFI"; cp -r /mnt/oc/EFI "$M/OC-EFI/"; sudo -n umount /mnt/oc; sudo -n qemu-nbd -d /dev/nbd0 > /dev/null
python3 - "$M/OC-EFI/EFI/OC/config.plist" "$BOOTARGS" "$CSR" <<'PY'
import plistlib, struct, sys
p, args, csr = sys.argv[1:4]
d = plistlib.load(open(p, 'rb')); g = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
d['NVRAM']['Add'][g]['boot-args'] = args
d['NVRAM']['Add'][g]['csr-active-config'] = struct.pack('<I', int(csr, 16))
d['Misc']['Boot']['Timeout'] = 0       # wait in the picker: after stage 1 the installer blesses its own volume and OpenCore would auto-boot it
d['Misc']['Security']['ScanPolicy'] = 0x10303   # APFS and HFS volumes on SATA only: no ESP, so no "EFI" entry. Picking an entry by arrow keys is unreliable and the EFI entry just restarts OpenCore, so the USB entry is the only one on a blank target and Return boots it
dl = d['NVRAM']['Delete'].setdefault(g, [])
for k in ('boot-args', 'csr-active-config'):
    if k not in dl: dl.append(k)
plistlib.dump(d, open(p, 'wb'), sort_keys=False)
PY
# the guest reads the profile values it needs; the target disk is found by its size as diskutil prints it
bytes=$(numfmt --from=iec "$DISK_SIZE"); label=$(awk -v b=$bytes 'BEGIN{printf "%.1f GB", b/1e9}')
printf 'NAME=%s\nKIND=%s\nKCSUFFIX=%s\nVOLNAME=%s\nDISK_LABEL="%s"\n' "$NAME" "$KIND" "$KCSUFFIX" "$VOLNAME" "$label" > "$M/profile.env"
sync; sudo -n umount "$M"; sudo -n losetup -d "$L"; L=; trap - EXIT; rmdir "$M"

echo "== blank target disk: $OUT ($DISK_SIZE)"
rm -f "$OUT"; truncate -s $DISK_SIZE "$OUT"
cp scripts/guest/install-disk-build.sh build/agent/files/ 2>/dev/null || { mkdir -p build/agent/files; cp scripts/guest/install-disk-build.sh build/agent/files/; }
rm -f build/agent/files/install-disk.out

echo "== boot the installer and open its Terminal"
export HS_BASE=$BOOTSTRAP_BASE HS_ESD=$STAGE
scripts/recovery-terminal.sh "$P" -drive id=NEWDISK,if=none,file="$OUT",format=raw -device ide-hd,bus=sata.1,drive=NEWDISK
scripts/vm.sh type "curl -s -o /tmp/idb.sh http://10.0.2.2:8000/files/install-disk-build.sh; sh /tmp/idb.sh"; scripts/vm.sh key ret
echo "== building (about 15 minutes), log: build/agent/files/install-disk.out"
for i in $(seq 1 180); do
  grep -q "INSTALL-DISK-DONE" build/agent/files/install-disk.out 2>/dev/null && break
  grep -qE "failed|no disk of size|no .* volume" build/agent/files/install-disk.out 2>/dev/null && { tail -5 build/agent/files/install-disk.out; echo "build failed"; exit 1; }
  sleep 10
done
grep -q "INSTALL-DISK-DONE" build/agent/files/install-disk.out || { echo "timed out"; exit 1; }
for p in $(pgrep -x qemu-system-x86); do kill $p; done; rm -f "$STAGE"
echo "done: $OUT"
echo "boot it:  HS_USB=$OUT scripts/boot-highsierra.sh      (CPU=... for an AMD model)"
