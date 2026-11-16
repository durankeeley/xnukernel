#!/bin/bash
# tahoe-cpu-test.sh <k8|k10|intel> [--no-boot]
# Boots the installed Tahoe kernel on a QEMU CPU model, logs in as xnu, waits for the desktop and saves
#   docs/screenshots/tahoe-<cpu>-login.png      the login window
#   docs/screenshots/tahoe-<cpu>-desktop.png    the desktop after login
#   docs/tests/tahoe-<cpu>.txt                  CPU, kernel and OPEMU info read from the guest
# LOGIN_ONLY=1 stops after the login window screenshot and the sysctl output (the desktop takes a long time in the VM).
# --no-boot skips the boot and uses the VM that is already running. --after-login also skips the login (already typed by hand).
# Install the matching kernel first (K10: scripts/tahoe-build-k10.sh, K8: XCROSS_MARCH=k8-sse3, then scripts/tahoe-kc-install.sh).
cd "$(dirname "$0")/.."
cpu=${1:?usage: tahoe-cpu-test.sh k8|k10|intel [--no-boot]}
case $cpu in
  k10)   CPU='phenom,family=16,model=4,stepping=2,vendor=AuthenticAMD,kvm=off';;
  k8)    CPU='Opteron_G2,vendor=AuthenticAMD,kvm=off';;      # vendor must be forced or the host vendor leaks and the kernel panics
  intel) CPU='';;
  *) echo "unknown cpu $cpu"; exit 1;;
esac
S=docs/screenshots; T=docs/tests; mkdir -p $S $T
shot() { scripts/vm-shot.sh "$1" >/dev/null 2>&1; }
poke() { scripts/vm.sh key shift; sleep 10; }     # the guest redraws lazily, a key press makes it repaint

if [ "$2" != "--no-boot" ] && [ "$2" != "--after-login" ]; then
  kill $(pgrep -x qemu-system-x86) 2>/dev/null; sleep 3
  CPU="$CPU" scripts/tahoe-boot-test.sh | tail -1
fi

echo "waiting for the guest agent and loginwindow"
for i in $(seq 1 180); do
  scripts/agent-run.sh 'pgrep -x loginwindow' 6 2>/dev/null | grep -q '^[0-9]' && break
  sleep 10
done
sleep 60; poke; [ "$2" = "--after-login" ] || shot $S/tahoe-$cpu-login.png
echo "login window saved"
if [ -n "${LOGIN_ONLY:-}" ]; then
  { echo "# Tahoe on $cpu ($(date -u +%F))"; echo "qemu cpu: ${CPU:-default Skylake-Client}"
    scripts/agent-run.sh 'sw_vers; uname -a; sysctl machdep.cpu.brand_string machdep.cpu.vendor machdep.cpu.family machdep.cpu.model machdep.cpu.features hw.cpusubtype hw.ncpu; uptime' 60
    strings /tmp/hs-serial.log | grep -a 'panic(\|OPEMU' | sort | uniq -c | sort -rn | head -20; } > $T/tahoe-$cpu.txt
  cat $T/tahoe-$cpu.txt; exit 0
fi

if [ "$2" != "--after-login" ]; then
  scripts/vm.sh hotkbd                              # the guest USB keyboard drops key events after a while, a fresh one works
  for i in $(seq 1 14); do scripts/vm.sh key backspace; done
  scripts/vm.sh type xnuxnu; sleep 1; scripts/vm.sh key ret
fi
echo "logged in, waiting for the desktop (software rendering, this takes 10 to 30 minutes)"
for i in $(seq 1 30); do
  sleep 120; poke; shot /tmp/tahoe-poll.png
  # the desktop is up when the menu bar (top 24px) and the Dock (bottom 90px) both contain bright pixels
  python3 - <<'E' && break
import sys
from PIL import Image
im = Image.open('/tmp/tahoe-poll.png').convert('L')
w, h = im.size
def bright(box):
    c = im.crop(box); return sum(1 for p in c.getdata() if p > 200)
sys.exit(0 if bright((0, 0, w, 24)) > 150 and bright((w // 4, h - 90, 3 * w // 4, h)) > 300 else 1)
E
done
cp /tmp/tahoe-poll.png $S/tahoe-$cpu-desktop.png
echo "desktop saved"

{
  echo "# Tahoe on $cpu ($(date -u +%F))"
  echo "qemu cpu: ${CPU:-default Skylake-Client}"
  scripts/agent-run.sh 'sw_vers; uname -a; sysctl machdep.cpu.brand_string machdep.cpu.vendor machdep.cpu.family machdep.cpu.model machdep.cpu.features hw.cpusubtype hw.ncpu; sysctl machdep.cpu.leaf7_features 2>&1; uptime' 60
} > $T/tahoe-$cpu.txt
strings /tmp/hs-serial.log | grep -a 'panic(\|OPEMU' | sort | uniq -c | sort -rn | head -20 >> $T/tahoe-$cpu.txt
cat $T/tahoe-$cpu.txt
