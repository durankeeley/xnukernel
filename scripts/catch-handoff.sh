#!/bin/bash
# catch-handoff.sh -- stop the VM at "HANDOFF TO XNU", dump low physical RAM to /tmp/mem.bin and print the kernel slide (phys offset of the Mach-O header - 0x200000).
S=/tmp/hs-monitor.sock; L=/tmp/hs-serial.log
m(){ printf '%s\n' "$1" | socat - UNIX-CONNECT:$S 2>/dev/null; }
for i in $(seq 1 ${1:-6000}); do grep -aq "HANDOFF TO XNU" $L 2>/dev/null && break; sleep 0.05; done
m stop >/dev/null; sleep 0.3
rm -f /tmp/mem.bin; m "pmemsave 0 0x24000000 /tmp/mem.bin" >/dev/null
for i in $(seq 1 120); do sz=$(stat -c %s /tmp/mem.bin 2>/dev/null || echo 0); [ "$sz" -ge 603979776 ] && break; sleep 1; done
python3 - <<'PY'
import mmap
f=open('/tmp/mem.bin','rb'); mm=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
magic=b'\xcf\xfa\xed\xfe\x07\x00\x00\x01'
for off in range(0x200000,len(mm)-8,0x200000):
    if mm[off:off+8]==magic:
        print("MACHO at phys 0x%x  slide=0x%x filetype=%d ncmds=%d"%(off,off-0x200000,int.from_bytes(mm[off+12:off+16],'little'),int.from_bytes(mm[off+16:off+20],'little')))
PY
