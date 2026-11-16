#!/bin/bash
# tcg-fault.sh -- run with the VM started by: scripts/boot-tahoe-tcg.sh -d cpu_reset,int -D /tmp/qemu-int.log   (then pick Tahoe in OC).
# Stops at HANDOFF TO XNU, dumps RAM -> slide from 'panic' bytes, resumes, waits for the first Triple fault, then symbolises the first kernel #PF/#GP.
cd "$(dirname "$0")/.."
K=${K:-out/kernel.tahoe-unstripped.x86_64}; S=/tmp/hs-monitor.sock; L=/tmp/hs-serial.log; Q=/tmp/qemu-int.log
m(){ printf '%s\n' "$1" | socat -t ${2:-5} - UNIX-CONNECT:$S 2>/dev/null | tr -d '\r'; }
until grep -aq "HANDOFF TO XNU" $L 2>/dev/null; do sleep 0.05; done
m stop >/dev/null; rm -f /tmp/mem.bin; m 'pmemsave 0 805306368 "/tmp/mem.bin"' 300 >/dev/null
SLIDE=$(python3 - "$K" <<'PY'
import sys,mmap
d=open(sys.argv[1],'rb').read(); b=d[0x8ca35c:0x8ca35c+48]
f=open('/tmp/mem.bin','rb'); mm=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
i=mm.find(b); print(hex(i-(0xffffff8000aca35c-0xffffff8000000000)) if i>=0 else "none")
PY
)
echo "slide=$SLIDE"; [ "$SLIDE" = none ] && exit 1
m cont >/dev/null
until grep -aq "Triple fault" $Q 2>/dev/null; do sleep 2; done
sleep 1
python3 - "$SLIDE" "$Q" <<'PY'
import sys,re,subprocess
s=int(sys.argv[1],16); lines=open(sys.argv[2],errors='ignore').read().splitlines()
# last Triple fault -> walk back to the first kernel exception of the cluster
tf=max(i for i,l in enumerate(lines) if 'Triple fault' in l)
ex=[(i,l) for i,l in enumerate(lines[:tf]) if re.match(r'\s*\d+: v=(0[0-9a-f]|1[0-9a-f]) ',l) and 'pc=ffffff' in l]
first=ex[0] if ex else None
# find start of final run of kernel exceptions
k=len(ex)-1
while k>0 and ex[k-1][0]>ex[k][0]-40: k-=1
for i,l in ex[k:k+4]: print(l[:200])
rip=int(re.search(r'pc=([0-9a-f]+)',ex[k][1]).group(1),16)
print("first kernel exception pc=%x  unslid=%x"%(rip,rip-s))
open('/tmp/fault.txt','w').write("%x\n"%(rip-s))
PY
ADDR=$(cat /tmp/fault.txt)
gdb -q -batch "$K" -ex "info symbol 0x$ADDR" -ex "x/6i 0x$ADDR-12" 2>&1 | grep -v DW_FORM
