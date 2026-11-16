# xarpbzx.py pkg  -> decompressed cpio stream of top-level Payload on stdout
import sys, struct, zlib, lzma
import xml.etree.ElementTree as ET
f = open(sys.argv[1], 'rb')
magic, hlen, ver, tlen, ulen, alg = struct.unpack('>4sHHQQI', f.read(28))
f.seek(hlen); root = ET.fromstring(zlib.decompress(f.read(tlen)))
heap = hlen + tlen
for fe in root.find('toc').findall('file'):
    if fe.findtext('name') == 'Payload':
        d = fe.find('data'); off = int(d.findtext('offset')); ln = int(d.findtext('length')); break
else: sys.exit('no Payload')
f.seek(heap + off); end = heap + off + ln
assert f.read(4) == b'pbzx'; f.read(8)
out = sys.stdout.buffer
while f.tell() < end:
    usize, csize = struct.unpack('>QQ', f.read(16))
    data = f.read(csize)
    out.write(data if csize == usize else lzma.decompress(data))
