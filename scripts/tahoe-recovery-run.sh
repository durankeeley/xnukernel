#!/bin/bash
# tahoe-recovery-run.sh <guest-script.sh> [--no-halt] -- boot Recovery, run build/agent/files/<script> in its Terminal, print the <script>.out it uploads, then halt the guest.
cd "$(dirname "$0")/.."
S=$1; F=build/agent/files; OUT=$F/${S%.sh}.out
pkill -x qemu-system-x86 2>/dev/null; sleep 2
pgrep -f agent-server >/dev/null || (nohup python3 scripts/agent-server.py >/tmp/agent.log 2>&1 &)
rm -f $OUT
ok=0
for attempt in 1 2 3; do
  scripts/tahoe-recovery.sh && { ok=1; break; }
  echo "recovery terminal attempt $attempt failed; restarting guest"; pkill -x qemu-system-x86; sleep 3
done
[ $ok = 1 ] || { echo "never appeared"; exit 1; }
sleep 3; scripts/vm.sh hotkbd; scripts/vm.sh key ret; scripts/vm.sh type " "; sleep 1
scripts/vm.sh type "curl -s -o /tmp/rr.sh http://10.0.2.2:8000/files/$S; sh /tmp/rr.sh"; sleep 2; scripts/vm.sh key ret
for i in $(seq 1 300); do [ -s $OUT ] && break; sleep 10; done
[ -s $OUT ] || { echo "guest script never uploaded output"; exit 1; }
cat $OUT
[ "$2" = "--no-halt" ] && exit 0
sleep 3; scripts/vm.sh key ret; scripts/vm.sh type " "; sleep 1; scripts/vm.sh type "sync; umount /tmp/sysmnt; umount /Volumes/Preboot; sync; halt"; sleep 2; scripts/vm.sh key ret
for i in $(seq 1 60); do pgrep -x qemu-system-x86 >/dev/null || break; sleep 5; done
echo "recovery-run done"
