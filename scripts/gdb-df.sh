#!/bin/bash
# gdb-df.sh [kernel] [slide-hex] -- break on panic_double_fault64, walk the interrupted frame-pointer chain and symbolize kernel text.
K=${1:-out/kernel.tahoe-unstripped.x86_64}; SL=${2:-0x0}
cat > /tmp/df.py <<'PY'
import gdb
inf = gdb.selected_inferior()
def rd(a):
    return int.from_bytes(bytes(inf.read_memory(a, 8)), 'little')
sp = int(gdb.parse_and_eval('$rdi'))
base = sp + 16                      # x86_saved_state_t: 16-byte flavor header, then x86_saved_state64
names = ['rdi','rsi','rdx','r10','r8','r9','cr2','r15','r14','r13','r12','r11','rbp','rbx','rcx','rax']
regs = {n: rd(base + 8*i) for i, n in enumerate(names)}
print('saved state: ' + ' '.join('%s=%#x' % (n, regs[n]) for n in ('rbp','rax','rbx','rcx','cr2')))
isf = base + 16*8 + 16
print('isf words:', ' '.join('%#x' % rd(isf + 8*i) for i in range(7)))
rip = [rd(isf + 8*i) for i in range(7)]
fp = regs['rbp']
print('frames (return addresses) from interrupted rbp %#x:' % fp)
for i in range(70):
    try:
        ret = rd(fp + 8); nxt = rd(fp)
    except Exception as e:
        print('  stop:', e); break
    s = gdb.execute('info symbol %#x' % ret, to_string=True).strip().split(' in section')[0]
    print('  #%02d %#x  %s' % (i, ret, s))
    if nxt <= fp or nxt - fp > 0x10000: break
    fp = nxt
PY
exec timeout ${GDBTIMEOUT:-420} gdb -q -batch "$K" -ex "set architecture i386:x86-64" -ex "set pagination off" \
  -ex "symbol-file $K -o $SL" -ex "target remote :${GDBPORT:-1234}" -ex "hbreak panic_double_fault64" \
  -ex "continue" -ex "python exec(open('/tmp/df.py').read())" -ex "x/8i \$rip" 2>&1 | grep -v DW_FORM
