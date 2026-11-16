#!/usr/bin/env python3
"""splitinfo-check.py <kernel> -- verify LC_SEGMENT_SPLIT_INFO (v2) covers every cross-segment RIP-relative reference in __text.
kmutil moves the kernel's segments when it builds a collection; any cross-segment reference missing from the split info breaks."""
import struct, subprocess, sys, re, collections
path = sys.argv[1]
d = open(path, 'rb').read()
_, _, _, ft, nc, _, fl, _ = struct.unpack('<8I', d[:32]); off = 32
secs = []; segs = []; si = None
for _ in range(nc):
    cmd, cs = struct.unpack('<2I', d[off:off+8])
    if cmd == 0x1e: si = struct.unpack('<2I', d[off+8:off+16])
    if cmd == 0x19:
        sn = d[off+8:off+24].split(b'\0')[0].decode(); vm, vs, fo, fs, _, _, nsec, _ = struct.unpack('<4Q2I2I', d[off+24:off+72])
        segs.append((sn, vm, vm + vs))
        so = off + 72
        for _ in range(nsec):
            nm = d[so:so+16].split(b'\0')[0].decode(); sg = d[so+16:so+32].split(b'\0')[0].decode()
            addr, size, foff = struct.unpack('<2QI', d[so+32:so+52])
            secs.append((sg, nm, addr, size, foff)); so += 80
    off += cs
blob = d[si[0]:si[0]+si[1]]
p = [1]
def u():
    r = 0; s = 0
    while True:
        x = blob[p[0]]; p[0] += 1; r |= (x & 0x7f) << s; s += 7
        if not x & 0x80: return r
assert blob[0] == 0x7f
n = u(); refs = collections.defaultdict(set); kinds = collections.Counter(); total = 0
for _ in range(n):
    fi = u(); ti = u(); cnt = u(); to_off = 0
    for _ in range(cnt):
        to_off += u(); fcount = u()
        for _ in range(fcount):
            kind = u(); dc = u(); fo_ = 0
            for _ in range(dc):
                fo_ += u(); refs[fi].add(fo_); kinds[kind] += 1; total += 1
print('split info: %d from/to section pairs, %d references, kinds %s' % (n, total, dict(kinds)))
def seg_of(a):
    for sn, lo, hi in segs:
        if lo <= a < hi: return sn
    return None
# find text section index
idx = [i for i, s in enumerate(secs) if s[0] == '__TEXT' and s[1] == '__text'][0]
text = secs[idx]
print('__TEXT,__text is section index %d (0-based) addr %#x size %#x; recorded offsets for it (0-based/1-based): %d / %d' % (idx, text[2], text[3], len(refs.get(idx, ())), len(refs.get(idx+1, ()))))
fi_use = idx if len(refs.get(idx, ())) >= len(refs.get(idx+1, ())) else idx + 1
rec = refs.get(fi_use, set())
out = subprocess.run(['llvm-objdump', '-d', '--macho', '--no-symbolic-operands', '--section=__TEXT,__text', path], capture_output=True, text=True).stdout.splitlines()
pat = re.compile(r'^([0-9a-f]+):\t((?:[0-9a-f]{2} ?)+)\t.*?(-?0x[0-9a-f]+)\(%rip\)')
cross = 0; covered = 0; missing = []
for l in out:
    m = pat.match(l)
    if not m: continue
    a = int(m.group(1), 16); raw = bytes.fromhex(m.group(2).replace(' ', '')); dv = int(m.group(3), 16)
    nxt = a + len(raw); tgt = nxt + dv; disp = dv & 0xffffffff
    pos = raw.find(struct.pack('<I', disp))
    if pos < 0: continue
    if seg_of(a) == seg_of(tgt): continue
    cross += 1
    foffs = (a + pos) - text[2]
    if foffs in rec: covered += 1
    elif len(missing) < 8: missing.append((hex(a), hex(tgt), seg_of(a), seg_of(tgt)))
print('cross-segment RIP-relative references: %d, covered by split info: %d (%.1f%%)' % (cross, covered, 100.0 * covered / max(cross, 1)))
for x in missing: print('  missing:', x)
