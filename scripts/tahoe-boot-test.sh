#!/bin/bash
# tahoe-boot-test.sh [extra boot-args] -- boot Tahoe on KVM and serial console to /tmp/hs-serial.log.
#   Which kernel boots depends on the installed collections: guest/kc-build.sh (custom) or guest/kc-restore-stock.sh (stock).
cd "$(dirname "$0")/.."
KCS="auxKCAtBoot=false"   # which kernel boots is decided by the installed collection files (spec B4), not by kcsuffix
scripts/oc-set.sh "-v keepsyms=1 debug=0x100 serial=3 $KCS $*" 0x87f 2>&1 | tail -1
: > /tmp/hs-serial.log
(setsid nohup scripts/boot-tahoe.sh >/tmp/boot-tahoe.log 2>&1 < /dev/null &)
sleep 4; sudo -n renice -8 -p $(pgrep -x qemu-system-x86) >/dev/null
until grep -aq "BdsDxe: starting" /tmp/hs-serial.log 2>/dev/null; do sleep 2; done
sleep 30; scripts/vm.sh key right; sleep 4; : > /tmp/hs-serial.log; scripts/vm.sh key ret
echo "boot requested"
