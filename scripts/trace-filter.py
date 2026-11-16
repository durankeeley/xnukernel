#!/usr/bin/env python3
"""Reads QEMU '-d int,cpu_reset' log on stdin; writes only real exceptions (not v=20..2f IRQs) with their
register dump, plus 'Triple fault' lines, to argv[1]. Avoids multi-GB logs."""
import sys, re
out = open(sys.argv[1], 'w', buffering=1)
keep = 0
pat = re.compile(r'^\s*\d+: v=([0-9a-f]+)')
for line in sys.stdin:
    m = pat.match(line)
    if m:
        keep = 0 if int(m.group(1), 16) in range(0x20, 0x30) or m.group(1) == 'ff' else 26
    if 'Triple fault' in line or 'check_exception' in line and keep:
        out.write(line)
    elif keep:
        out.write(line); keep -= 1
