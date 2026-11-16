#!/usr/bin/env bash
# Boot macOS High Sierra in QEMU/KVM via OpenCore (OSX-KVM tree, next to this repo or OSXKVM=).
# usage: [CPU=<qemu cpu>] [GDBPORT=1234] scripts/boot-highsierra.sh [extra qemu args]
#   HS_DISK   the install target, default mac_hdd_ng.img (qcow2)
#   HS_BASE   installer / Recovery image (raw), default BaseSystem-hs.img. Attached when the file exists, set HS_BASE=none to leave it out
#   HS_ESD    InstallESD image (raw, read only), default InstallESD.img. Attached when the file exists, HS_ESD=none to leave it out
#   HS_USB    a single bootable install disk image (raw): OpenCore on its EFI partition, installer and kernel on its HFS+ volume. Boots from it alone
#             (no separate OpenCore, BaseSystem or ESD disk), like the classic one-USB-stick install
#   HS_DATE   guest clock at start, e.g. 2018-12-20T12:00:00. Apple's 10.13 installer certificates expired in 2019, so the installer needs a
#             clock before that or it stops with "The installer resources have expired"
#   GDBPORT   expose the QEMU gdb stub (works with KVM and TCG) for stepping / breakpoints in the kernel
# Needs: echo 1 | sudo tee /sys/module/kvm/parameters/ignore_msrs
cd "${OSXKVM:-$(cd "$(dirname "$0")/../.." && pwd)/OSX-KVM}" || exit 1
MY_OPTIONS="+ssse3,+sse4.2,+popcnt,+avx,+aes,+xsave,+xsaveopt,check"
args=(
  -enable-kvm -m "${RAM:-4096}" -cpu "${CPU:-Penryn,kvm=off,vendor=GenuineIntel,+invtsc,vmware-cpuid-freq=on,$MY_OPTIONS}"
  -machine q35
  -device usb-ehci,id=ehci -device usb-kbd,bus=ehci.0 -device usb-tablet,bus=ehci.0
  -smp 4,cores=2,sockets=1
  -device "isa-applesmc,osk=ourhardworkbythesewordsguardedpleasedontsteal(c)AppleComputerInc"
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE_4M.fd
  -drive if=pflash,format=raw,file=OVMF_VARS-1920x1080.fd
  -smbios type=2
  -device ich9-intel-hda -device hda-duplex
  -device ich9-ahci,id=sata
  -drive id=MacHDD,if=none,file="${HS_DISK:-mac_hdd_ng.img}",format=qcow2
  -device ide-hd,bus=sata.4,drive=MacHDD
  -netdev user,id=net0,hostfwd=tcp::2222-:22 -device vmxnet3,netdev=net0,id=net0,mac=52:54:00:c9:18:27
  -monitor unix:/tmp/hs-monitor.sock,server,nowait
  -serial file:/tmp/hs-serial.log
  -device vmware-svga
)
[ -n "${HS_DATE:-}" ] && args+=(-rtc "base=$HS_DATE,clock=vm")
if [ -n "${HS_USB:-}" ]; then
  args+=(-drive id=USB,if=none,file="$HS_USB",format=raw -device ide-hd,bus=sata.2,drive=USB,bootindex=0)
  HS_BASE=none; HS_ESD=none
else
  args+=(-drive id=OpenCoreBoot,if=none,snapshot=on,format=qcow2,file=OpenCore/OpenCore.qcow2 -device ide-hd,bus=sata.2,drive=OpenCoreBoot)
fi
BASE=${HS_BASE:-BaseSystem-hs.img}; ESD=${HS_ESD:-InstallESD.img}
[ "$BASE" != none ] && [ -f "$BASE" ] && args+=(-drive id=InstallMedia,if=none,file="$BASE",format=raw -device ide-hd,bus=sata.3,drive=InstallMedia)
[ "$ESD" != none ] && [ -f "$ESD" ] && args+=(-drive id=ESD,if=none,file="$ESD",format=raw,snapshot=on -device ide-hd,bus=sata.5,drive=ESD)
[ -n "${GDBPORT:-}" ] && args+=(-gdb "tcp::$GDBPORT")
exec qemu-system-x86_64 "${args[@]}" "$@"
