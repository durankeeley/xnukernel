#!/bin/bash
# killpat.sh <pattern> -- kill processes whose command line matches <pattern>, never this script or its parent shell.
# (pkill -f from a shell whose own command line contains the pattern kills that shell; run this script instead.)
pat=$1
[ -n "$pat" ] || { echo "usage: killpat.sh <pattern>"; exit 2; }
for pid in $(pgrep -f -- "$pat"); do
	[ "$pid" = "$$" ] && continue
	[ "$pid" = "$PPID" ] && continue
	# also skip any ancestor of this script (the tool's own shell)
	a=$PPID; skip=0
	while [ "$a" -gt 1 ] 2>/dev/null; do [ "$a" = "$pid" ] && skip=1 && break; a=$(ps -o ppid= -p "$a" 2>/dev/null | tr -d ' '); [ -z "$a" ] && break; done
	[ $skip = 1 ] || kill "$pid" 2>/dev/null && echo "killed $pid"
done
exit 0
