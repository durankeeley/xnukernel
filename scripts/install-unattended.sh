#!/bin/bash
# WORK IN PROGRESS, NOT FINISHED. The stages before this were each run by hand (docs/INSTALL.md), this script chains them and has only got as far as
# erasing the target and starting the installer from the Recovery Terminal. Known gaps: starting the installer after the Terminal is open (Recovery has
# no open and no nohup; try InstallAssistant_springboard), the background watcher below was never seen to patch stage 2, and the kernel is not
# copied to the new volume this way (the proven way is single-user mode, scripts/guest/hs-single.sh).
# install-unattended.sh <profile> -- install macOS onto a blank disk from the single install disk (make-install-disk.sh), on our kernel and an AMD
# CPU model the whole way, with nothing typed by hand: Recovery, erase, installer stage 1, stage 2, first boot, Setup Assistant. High Sierra only.
#   scripts/install-unattended.sh highsierra
# Env: CPU (default Opteron_G2,vendor=AuthenticAMD,kvm=off), HS_USB (install disk), HS_DISK (BLANK target, qcow2, default ../OSX-KVM/mac_hdd_ng.img),
#      GDBPORT (open the gdb stub). Screenshots of every stage go to build/shots/unattended/, the setup screens to build/shots/setup/.
# What it works around (details in docs/INSTALL.md):
#   - Recovery stops taking input once an install exists on the target, so the target is patched while stage 1 runs: a background watcher started
#     in the Terminal (guest/hs-target-watch.sh) points stage 2 at our kernel and copies our kernel onto the new volume as soon as stage 1 has
#     written its boot files. Nothing has to be typed after stage 1.
#   - keys right after a keyboard hot-plug are often dropped, and the OpenCore picker redraws slowly. The install disk's OpenCore therefore has
#     a scan policy that leaves only the USB entry on a blank target, and every picker step is "Return, wait, Return".
#   - a fresh QEMU is started for every stage: input stops working after an in-VM restart that followed hot-plugged USB devices.
#   - stage 1 runs with the clock in 2018 (the installer's certificates expired in 2019), stage 2 and later with the current date.
cd "$(dirname "$0")/.."
P=${1:?usage: install-unattended.sh <profile>}; . scripts/install-disk/$P.conf
[ "$KIND" = basesystem-packages ] || { echo "profile $P is not implemented"; exit 1; }
OSXKVM=${OSXKVM:-$(cd .. && pwd)/OSX-KVM}
export CPU=${CPU:-Opteron_G2,vendor=AuthenticAMD,kvm=off} HS_USB=${HS_USB:-$OSXKVM/HS-USB.img} HS_DISK=${HS_DISK:-$OSXKVM/mac_hdd_ng.img}
S=build/shots/unattended; mkdir -p $S build/agent/files
shot() { scripts/vm-shot.sh $S/$1.png >/dev/null 2>&1; }
log() { echo "[$(date +%H:%M:%S)] $*"; }
M=/tmp/hs-monitor.sock; m(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:$M >/dev/null 2>&1; }
click() { for i in 1 2 3 4 5 6 7 8; do m "mouse_move -300 -300"; sleep 0.15; done   # relative mouse: ~1.75x too far, so send 0.572 of the point
  m "mouse_move $(( $1*572/1000 )) $(( $2*572/1000 ))"; sleep 0.8; m "mouse_button 1"; sleep 0.2; m "mouse_button 0"; }
rclick() { m "mouse_move $(( $1*572/1000 )) $(( $2*572/1000 ))"; sleep 0.8; m "mouse_button 1"; sleep 0.2; m "mouse_button 0"; }   # move from where the pointer is (an open menu must not be swept over)
t() { scripts/vm.sh hotkbd; scripts/vm.sh key ret; scripts/vm.sh type "$1"; scripts/vm.sh key ret; }
boots() { grep -ac 'BdsDxe: starting' /tmp/hs-serial.log 2>/dev/null; }
killvm() { for p in $(pgrep -x qemu-system-x86); do kill $p; done; sleep 3; }
startvm() {   # startvm [HS_DATE]: a fresh QEMU with the install disk, the target and the agent server
  killvm; rm -f $M; : > /tmp/hs-serial.log
  ss -ltn 2>/dev/null | grep -q ':8000 ' || (setsid nohup python3 scripts/agent-server.py > /tmp/agent.log 2>&1 < /dev/null &)
  (HS_DATE=$1 setsid nohup scripts/boot-highsierra.sh > /tmp/boot-vm.log 2>&1 < /dev/null &)
  until [ "$(boots)" -ge 1 ]; do sleep 2; done; sleep 40; }
pick_default() { scripts/vm.sh hotkbd; scripts/vm.sh key ret; sleep 15; scripts/vm.sh key ret; }   # the default entry, the first Return can be dropped
bright() { python3 -W ignore - "$1" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('L'); sys.exit(0 if sum(im.resize((64, 40)).getdata()) / 2560 > 90 else 1)
PY
}
terminal_open() { python3 -W ignore - "$1" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('L'); d = list(im.crop((60, 100, 600, 400)).getdata()); sys.exit(0 if sum(1 for p in d if p > 245) / len(d) > 0.6 else 1)
PY
}
recovery() {  # recovery <HS_DATE>: boot Recovery from the install disk and open its Terminal with the mouse (3 tries, a fresh QEMU each)
  for try in 1 2 3; do
    startvm "$1"; pick_default
    rm -f /tmp/u.png; for i in $(seq 1 40); do sleep 15; rm -f /tmp/u.png; scripts/vm-shot.sh /tmp/u.png >/dev/null 2>&1; [ -f /tmp/u.png ] && bright /tmp/u.png && break; done
    sleep 15; scripts/vm.sh wake; sleep 2
    click 287 11; sleep 5; rclick 51 105; sleep 10         # Utilities, then straight down to Terminal (338,116)
    scripts/vm-shot.sh /tmp/u.png >/dev/null 2>&1; terminal_open /tmp/u.png && return 0
    log "Terminal did not open (try $try)"; done; return 1; }
verify_target() {   # verify_target <label>: look at the target volume from the host, read only (the VM must not be running)
  killvm; sudo -n modprobe nbd max_part=8; sudo -n qemu-nbd -d /dev/nbd0 >/dev/null 2>&1; sudo -n qemu-nbd -r -c /dev/nbd0 -f qcow2 "$HS_DISK"; sleep 2
  sudo -n mkdir -p /mnt/hsverify
  if sudo -n mount -t hfsplus -o ro,force /dev/nbd0p2 /mnt/hsverify 2>/dev/null; then
    echo "== target volume after $1"
    sudo -n ls -l "/mnt/hsverify/System/Library/Kernels" "/mnt/hsverify/System/Library/PrelinkedKernels" 2>&1 | grep -E "xcross|No such"
    sudo -n grep -a -A1 -E "Kernel Cache|Kernel Flags" "/mnt/hsverify/macOS Install Data/Locked Files/Boot Files/com.apple.Boot.plist" 2>/dev/null | grep -a "string"
    sudo -n ls -l /mnt/hsverify/Library/LaunchDaemons/com.xcross.agent.plist 2>&1 | grep -E "xcross|No such"
    sudo -n umount /mnt/hsverify
  else echo "(could not mount the target volume read only)"; fi
  sudo -n qemu-nbd -d /dev/nbd0 >/dev/null 2>&1; }
cp scripts/guest/{cpu-info,erase-target,hs-target,hs-target-watch}.sh scripts/guest-agent.sh build/agent/files/

log "== stage 1: Recovery on $CPU (clock 2018-12-20)"
recovery 2018-12-20T12:00:00 || { echo "no Terminal"; exit 1; }
t "curl -s -o /tmp/ci.sh http://10.0.2.2:8000/files/cpu-info.sh; sh /tmp/ci.sh"; sleep 12; shot 01-terminal-cpu-info
t "curl -s -o /tmp/et.sh http://10.0.2.2:8000/files/erase-target.sh; sh /tmp/et.sh"; sleep 45; shot 02-erase-target
t "curl -s -o /tmp/watch.sh http://10.0.2.2:8000/files/hs-target-watch.sh; nohup sh /tmp/watch.sh > /tmp/watch.log 2>&1 &"; sleep 3
click 800 300; sleep 2; click 485 285; sleep 2; click 880 494; sleep 40; shot 03-installer-welcome   # the Utilities window comes forward: Reinstall macOS, Continue
click 640 628; sleep 12; shot 04-license      # Continue
click 685 628; sleep 6                        # Agree
click 790 184; sleep 12; shot 05-disk         # Agree in the sheet
click 560 495; sleep 3; click 685 628; sleep 40; shot 06-installing   # the Macintosh disk, Install
n0=$(boots); log "stage 1 running, waiting for the restart"
for i in $(seq 1 240); do [ "$(boots)" -gt "$n0" ] && break; sleep 15; done
[ "$(boots)" -gt "$n0" ] || { echo "stage 1 did not finish"; exit 1; }
log "stage 1 finished"; verify_target "stage 1" | tee $S/target-after-stage1.txt

log "== stage 2 (current date, $CPU)"
startvm "$(date +%Y-%m-%dT%H:%M:%S)"; pick_default       # the default entry is the macOS Installer
sleep 200; shot 07-stage2
n0=$(boots)
for i in $(seq 1 240); do [ "$(boots)" -gt "$n0" ] && break; sleep 15; done
[ "$(boots)" -gt "$n0" ] || { echo "stage 2 did not finish"; exit 1; }
log "stage 2 finished"; verify_target "stage 2" | tee $S/target-after-stage2.txt

log "== first boot of the installed system on our kernel"
startvm ""; pick_default                                  # the default entry is Macintosh
sleep 240; scripts/vm.sh wake; sleep 3; shot 08-first-boot
log "== Setup Assistant"
scripts/hs-setup-assistant.sh; scripts/vm.sh wake; sleep 30; click 688 643; sleep 60; scripts/vm.sh wake; sleep 3; shot 09-desktop   # Express Set Up: Continue
if scripts/agent-run.sh 'uname -v; sysctl machdep.cpu.brand_string hw.cpufrequency' 40 2>&1 | tee $S/guest.txt | grep -q AMD; then log "kernel check: ok"; else log "agent did not answer (the installer may have removed it)"; fi
log "done. Screenshots: $S"
