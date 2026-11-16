#!/bin/bash
# tahoe-debug.sh -- run alongside a Tahoe VM started with GDBPORT=1234 (see boot-tahoe.sh), before pressing Return in the OC picker.
# Stops the VM at the XNU handoff, dumps low RAM, derives the KASLR slide from 'panic' code bytes, attaches gdb, breaks on panic and prints bt.
cd "$(dirname "$0")/.."
K=${K:-out/kernel.tahoe-unstripped.x86_64}; S=/tmp/hs-monitor.sock; L=/tmp/hs-serial.log
m(){ printf '%s\n' "$1" | socat -t ${2:-5} - UNIX-CONNECT:$S 2>/dev/null | tr -d '\r'; }
for i in $(seq 1 12000); do grep -aq "HANDOFF TO XNU" $L 2>/dev/null && break; sleep 0.02; done
m stop >/dev/null; echo "stopped; $(m 'info registers' | grep -aoE 'RIP=[0-9a-f]+' | head -1)"
rm -f /tmp/mem.bin; m 'pmemsave 0 603979776 "/tmp/mem.bin"' 90 >/dev/null
SLIDE=$(python3 - "$K" <<'PY'
import sys, struct, subprocess, mmap
k = sys.argv[1]; d = open(k, 'rb').read()
nc = struct.unpack('<8I', d[:32])[4]; off = 32; segs = []
for _ in range(nc):
    cmd, cs = struct.unpack('<2I', d[off:off+8])
    if cmd == 0x19:
        vm, vs, fo, fs = struct.unpack('<4Q', d[off+24:off+56]); segs.append((vm, vs, fo))
    off += cs
pa = [int(l.split()[0], 16) for l in subprocess.run(['llvm-nm', k], capture_output=True, text=True).stdout.splitlines() if l.split()[-1] == '_panic'][0]
fo = [fo + pa - vm for vm, vs, fo in segs if vm <= pa < vm + vs][0]
b = d[fo:fo+48]
f = open('/tmp/mem.bin', 'rb'); mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
i = mm.find(b); print(hex(i - (pa - 0xffffff8000000000)) if i >= 0 else "none")
PY
)
echo "slide=$SLIDE"
[ "$SLIDE" = none ] && exit 1
GDBTIMEOUT=${GDBTIMEOUT:-420} ${GDBCMD:-scripts/gdb-panic.sh} "$K" "$SLIDE" 2>&1 | grep -v "DW_FORM_GNU_str_index"
