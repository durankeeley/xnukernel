#!/bin/bash
# df-analyze.sh -- after a (nested/double-fault) panic on the running VM: pause it, dump the kernel stack above the faulting RSP and
# symbolize kernel text with the slide derived from a RAM dump. Start the VM normally (boot-tahoe.sh); run this before/while it boots.
cd "$(dirname "$0")/.."
K=${K:-out/kernel.tahoe-unstripped.x86_64}; S=/tmp/hs-monitor.sock; L=/tmp/hs-serial.log
m(){ printf '%s\n' "$1" | socat -t ${2:-5} - UNIX-CONNECT:$S 2>/dev/null | tr -d '\r'; }
until grep -aqE "Double fault|^panic\(cpu" $L 2>/dev/null; do sleep 2; done; sleep 4
m stop >/dev/null
RSP=$(strings $L | grep -m1 -oE "RSP: 0x[0-9a-f]+" | awk '{print $2}'); RBP=$(strings $L | grep -m1 -oE "RBP: 0x[0-9a-f]+" | awk '{print $2}'); RIP=$(strings $L | grep -m1 -oE "RIP: 0x[0-9a-f]+" | awk '{print $2}')
echo "RSP=$RSP RBP=$RBP RIP=$RIP"
m "x /1024gx $RSP" 20 | grep -aE "^[0-9a-f]{16}:" > /tmp/stack.txt; wc -l < /tmp/stack.txt
rm -f /tmp/mem.bin; m 'pmemsave 0 805306368 "/tmp/mem.bin"' 300 >/dev/null
python3 - "$K" "$RIP" <<'PY'
import sys, struct, subprocess, mmap, bisect, re
k, rip = sys.argv[1], int(sys.argv[2], 16)
d = open(k, 'rb').read()
nc = struct.unpack('<8I', d[:32])[4]; off = 32; segs = []
for _ in range(nc):
    cmd, cs = struct.unpack('<2I', d[off:off+8])
    if cmd == 0x19:
        vm, vs, fo, fs = struct.unpack('<4Q', d[off+24:off+56]); segs.append((vm, vs, fo))
    off += cs
nm = subprocess.run(['llvm-nm', '-n', k], capture_output=True, text=True).stdout.splitlines()
syms = [(int(l.split()[0], 16), l.split()[2]) for l in nm if len(l.split()) == 3 and l.split()[1] in 'TtWw']
pa = [a for a, n in syms if n == '_panic'][0]
fo = [fo + pa - vm for vm, vs, fo in segs if vm <= pa < vm + vs][0]
b = d[fo:fo+48]
mm = mmap.mmap(open('/tmp/mem.bin', 'rb').fileno(), 0, access=mmap.ACCESS_READ)
i = mm.find(b)
slide = i - (pa - 0xffffff8000000000)
print('kernel slide = %#x' % slide)
addrs = [a for a, _ in syms]
def sym(a):
    u = a - slide
    j = bisect.bisect_right(addrs, u) - 1
    if j >= 0 and 0xffffff8000200000 <= u < 0xffffff8000c00000 and u - syms[j][0] < 0x10000:
        return '%s+%#x' % (syms[j][1], u - syms[j][0])
    return None
words = []
for l in open('/tmp/stack.txt'):
    p = l.split(':')
    a = int(p[0], 16)
    for n, w in enumerate(p[1].split()):
        words.append((a + 8*n, int(w, 16)))
print('RIP ->', sym(rip))
print('kernel-text return addresses on the overflowed stack (low->high address):')
last = None; run = 0
for a, w in words:
    s = sym(w)
    if s:
        if s == last: run += 1; continue
        if run: print('      ... x%d more' % run)
        run = 0; last = s
        print('  %#x: %s' % (a, s))
if run: print('      ... x%d more' % run)
PY
