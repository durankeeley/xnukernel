#!/bin/bash
# stop-all.sh -- stop the Tahoe VM and any pipeline/helper scripts that drive it.
# The patterns live in this file (not on the caller's command line), so a caller's shell is never matched.
for pat in 'tahoe-kc-install' 'tahoe-recovery' 'tahoe-boot-test' 'tahoe-cycle' 'catch-kernel' 'catch-handoff' 'tahoe-debug' 'df-analyze' 'gdb-df' 'gdb-panic'; do
	for pid in $(pgrep -f "$pat"); do
		[ "$pid" = "$$" ] || [ "$pid" = "$PPID" ] || kill "$pid" 2>/dev/null
	done
done
pkill -x qemu-system-x86 2>/dev/null
sleep 3
echo "remaining helper/vm processes:"
pgrep -af 'qemu-system|tahoe-' | grep -v "stop-all" | cut -c1-80
echo "(end)"
