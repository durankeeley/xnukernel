#!/usr/bin/env python3
"""profile-tcg.py <hotblocks.txt> <kernel> [slide-hex] [top-N]
Aggregates the TCG hot-block dump (scripts/qemu-plugin/hotblocks.c) into guest-instruction counts per kernel function,
and splits the total into kernel / user / firmware."""
import sys, subprocess, bisect, collections
args = sys.argv[1:]
base = None
if args[0] == '--diff':		# --diff <earlier.txt> <later.txt> <kernel> ...: only what ran between the two snapshots
    base = args[1]; args = args[2:]
dump, kernel = args[0], args[1]
sys.argv = [sys.argv[0]] + args
slide = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0xc800000
top = int(sys.argv[4]) if len(sys.argv) > 4 else 40
syms = []
for l in subprocess.run(['llvm-nm', '-n', '--defined-only', kernel], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3 and p[1] in 'tTsSdDbBaA' and int(p[0], 16) >= 0xffffff8000000000: syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
per_fn = collections.Counter(); cat = collections.Counter(); total = 0
prev = {}
if base:
    for l in open(base):
        v, n, c = l.split(); prev[(v, n)] = prev.get((v, n), 0) + int(c)
for l in open(dump):
    v, n, c = l.split(); c = int(c) - (prev.get((v, n), 0) if base else 0)
    if c <= 0: continue
    v = int(v, 16); n = int(n); ins = n * c; total += ins
    if v >= 0xffffff0000000000:
        cat['kernel'] += ins
        u = v - slide; i = bisect.bisect_right(addrs, u) - 1
        per_fn[syms[i][1] if i >= 0 else '?'] += ins
    elif v < 0x100000000: cat['firmware/low memory'] += ins
    else: cat['user'] += ins
print(f'total guest instructions: {total:,}')
for k, v in cat.most_common(): print(f'  {k:22s} {v:>16,}  {100*v/total:5.1f}%')
print(f'\ntop {top} kernel functions:')
for f, v in per_fn.most_common(top): print(f'  {v:>16,}  {100*v/total:5.1f}%  {f}')
