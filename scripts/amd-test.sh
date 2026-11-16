#!/bin/bash
# amd-test.sh <am2|am3|am3plus|intel> [seconds]   Boot the installed *.xcross kernel under a mimicked AMD CPU.
#   am2     K8   family 0Fh  (QEMU Opteron_G2: Athlon 64 X2 / Opteron 22xx class, SSE3, no SSSE3/SSE4)
#   am3     K10  family 10h  (QEMU phenom, model 4 = Phenom II: SSE3+SSE4a, no SSSE3/SSE4.1/4.2)
#   am3plus Bulldozer family 15h (QEMU Opteron_G4)
# The hypervisor bit stays ON: boot.efi only accepts the iMac19,1 board-id when it believes it runs in a VM. vendor=AuthenticAMD is
# required under KVM, which otherwise reports the HOST vendor (GenuineIntel) and sends the kernel down its Intel TSC path.
W=$(cd "$(dirname "$0")/.." && pwd)
case ${1:-am3} in
  am2)     export CPU='Opteron_G2,vendor=AuthenticAMD,kvm=off' ;;
  am3)     export CPU='phenom,family=16,model=4,stepping=2,vendor=AuthenticAMD,kvm=off' ;;
  am3plus) export CPU='Opteron_G4,vendor=AuthenticAMD,kvm=off' ;;
  intel)   unset CPU ;;
  *) echo "usage: $0 am2|am3|am3plus|intel [seconds]"; exit 1 ;;
esac
echo "[amd-test] CPU=${CPU:-<intel default>}"
exec $W/scripts/watch-boot.sh ${2:-240}
