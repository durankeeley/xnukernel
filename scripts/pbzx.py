#!/usr/bin/env python3
"""Decode an Apple pbzx payload to stdout (pipe into cpio -idm)."""
import sys, struct, lzma
f = open(sys.argv[1], 'rb'); out = sys.stdout.buffer
assert f.read(4) == b'pbzx'
f.read(8)
while True:
    h = f.read(16)
    if len(h) < 16: break
    flags, ln = struct.unpack('>QQ', h)
    d = f.read(ln)
    out.write(lzma.decompress(d) if d[:6] == b'\xfd7zXZ\x00' else d)
