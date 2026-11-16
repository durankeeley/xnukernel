#!/usr/bin/env python3
"""vtable-diff.py <_ZTV symbol> [stock-kernel] [our-kernel] -- diff the virtual-slot names of a class vtable between the stock KDK kernel and ours."""
import struct, subprocess, bisect, sys, difflib
S = sys.argv[2] if len(sys.argv) > 2 else 'build/kdk-real/kdk/System/Library/Kernels/kernel.development'
O = sys.argv[3] if len(sys.argv) > 3 else 'out/kernel.tahoe-unstripped.x86_64'
def vtable(path, symname):
    d = open(path, 'rb').read()
    _, _, _, _, nc, _, _, _ = struct.unpack('<8I', d[:32]); off = 32; segs = []
    for _ in range(nc):
        cmd, cs = struct.unpack('<2I', d[off:off+8])
        if cmd == 0x19:
            vm, vs, fo, fs = struct.unpack('<4Q', d[off+24:off+56]); segs.append((vm, vs, fo))
        off += cs
    syms = []
    for l in subprocess.run(['llvm-nm', '-n', path], capture_output=True, text=True).stdout.splitlines():
        p = l.split()
        if len(p) == 3 and p[1] in 'TtWwSsDd': syms.append((int(p[0], 16), p[2]))
    addrs = [a for a, _ in syms]
    def name(a):
        i = bisect.bisect_right(addrs, a) - 1
        return syms[i][1] if i >= 0 and a - syms[i][0] < 0x4000 and a >= 0xffffff8000000000 else hex(a)
    start = [a for a, n in syms if n == symname][0]
    nxt = min(a for a, n in syms if a > start)
    out = []
    for vm, vs, fo in segs:
        if vm <= start < vm + vs:
            for a in range(start, nxt, 8):
                out.append(struct.unpack('<Q', d[fo+a-vm:fo+a-vm+8])[0])
    return [name(x) for x in out]
a, b = vtable(S, sys.argv[1]), vtable(O, sys.argv[1])
print('stock slots %d, ours %d' % (len(a), len(b)))
for l in difflib.unified_diff(a, b, 'stock', 'ours', lineterm='', n=1):
    print(l)
