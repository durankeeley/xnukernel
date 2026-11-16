#!/usr/bin/env bash
# Boot macOS under QEMU TCG (software CPU emulation: unlike KVM it really raises #UD for instructions the CPU model lacks).
# usage: [CPU=<qemu cpu>] [GDBPORT=1234] ./boot-highsierra.sh [extra qemu args]
#   GDBPORT: expose the QEMU gdb stub (works with KVM and TCG) for stepping / breakpoints in the kernel
# Needs: echo 1 | sudo tee /sys/module/kvm/parameters/ignore_msrs
cd "${OSXKVM:-$(cd "$(dirname "$0")/../.." && pwd)/OSX-KVM}" || exit 1
MY_OPTIONS="+ssse3,+sse4.2,+popcnt,+avx,+aes,+xsave,+xsaveopt,check"
exec qemu-system-x86_64 \
  -accel tcg,thread=multi -m 4096 -cpu "${CPU:-Penryn,kvm=off,vendor=GenuineIntel,+invtsc,vmware-cpuid-freq=on,$MY_OPTIONS}" \
  -machine q35 \
  -device usb-ehci,id=ehci -device usb-kbd,bus=ehci.0 -device usb-tablet,bus=ehci.0 \
  -smp 4,cores=2,sockets=1 \
  -device isa-applesmc,osk="ourhardworkbythesewordsguardedpleasedontsteal(c)AppleComputerInc" \
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS-1920x1080.fd \
  -smbios type=2 \
  -device ich9-intel-hda -device hda-duplex \
  -device ich9-ahci,id=sata \
  -drive id=OpenCoreBoot,if=none,snapshot=on,format=qcow2,file=OpenCore/OpenCore.qcow2 \
  -device ide-hd,bus=sata.2,drive=OpenCoreBoot \
  -drive id=InstallMedia,if=none,file=BaseSystem.img,format=raw \
  -device ide-hd,bus=sata.3,drive=InstallMedia \
  -drive id=MacHDD,if=none,file=mac_hdd_ng.img,format=qcow2 \
  -device ide-hd,bus=sata.4,drive=MacHDD \
  -drive id=ESD,if=none,file=InstallESD.img,format=raw,snapshot=on \
  -device ide-hd,bus=sata.5,drive=ESD \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 -device vmxnet3,netdev=net0,id=net0,mac=52:54:00:c9:18:27 \
  ${GDBPORT:+-gdb tcp::$GDBPORT} \
  ${QEMU_PLUGIN:+-plugin $QEMU_PLUGIN} \
  -monitor unix:/tmp/hs-monitor.sock,server,nowait \
  -serial file:/tmp/hs-serial.log \
  -device vmware-svga "$@"
