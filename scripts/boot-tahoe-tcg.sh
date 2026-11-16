#!/usr/bin/env bash
# Boot macOS Tahoe (26) in QEMU/KVM via OpenCore (OSX-KVM tree). Same monitor/serial paths as boot-highsierra.sh so vm.sh works.
#   scripts/boot-tahoe.sh                       normal boot (installer media attached while BaseSystem.img exists and INSTALL=1)
#   INSTALL=1 scripts/boot-tahoe.sh             attach BaseSystem.img (recovery installer)
#   GDBPORT=1234 scripts/boot-tahoe.sh          also open QEMU's gdb stub (add -S to wait)
# Needs: echo 1 | sudo tee /sys/module/kvm/parameters/ignore_msrs
cd "$(dirname "$0")/../../OSX-KVM" || exit 1
MY_OPTIONS="+ssse3,+sse4.2,+popcnt,+avx,+avx2,+aes,+xsave,+xsaveopt"
args=(
  -accel tcg,thread=multi -m "${RAM:-8192}" -cpu "${CPU:-Skylake-Client,-hle,-rtm,vendor=GenuineIntel,$MY_OPTIONS}"
  -machine q35
  -device usb-ehci,id=ehci -device usb-kbd,bus=ehci.0 -device usb-tablet,bus=ehci.0
  -smp 4,cores=2,sockets=1
  -device isa-applesmc,osk="ourhardworkbythesewordsguardedpleasedontsteal(c)AppleComputerInc"
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE_4M.fd
  -drive if=pflash,format=raw,file="${OVMF_VARS:-OVMF_VARS-1920x1080.fd}"
  -smbios type=2
  -device ich9-intel-hda -device hda-duplex
  -device ich9-ahci,id=sata
  -drive id=OpenCoreBoot,if=none,snapshot=on,format=qcow2,file=OpenCore/OpenCore.qcow2
  -device ide-hd,bus=sata.2,drive=OpenCoreBoot
  -drive id=MacHDD,if=none,file="${DISK:-mac_tahoe.qcow2}",format=qcow2
  -device ide-hd,bus=sata.4,drive=MacHDD
  -netdev user,id=net0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=net0,id=net0,mac=52:54:00:c9:18:27
  -monitor unix:/tmp/hs-monitor.sock,server,nowait
  -serial file:/tmp/hs-serial.log
  -device vmware-svga
)
if [ -n "${INSTALL:-}" ]; then
  args+=(-drive id=InstallMedia,if=none,file=BaseSystem.img,format=raw -device ide-hd,bus=sata.3,drive=InstallMedia)
fi
[ -n "${GDBPORT:-}" ] && args+=(-gdb "tcp::$GDBPORT")
exec qemu-system-x86_64 "${args[@]}" "$@"
