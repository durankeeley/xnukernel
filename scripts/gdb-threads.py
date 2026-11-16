# gdb-threads.py -- run inside gdb attached to a Tahoe VM (QEMU gdb stub) with symbols loaded at the right slide.
# Walks the kernel's global `threads` queue and, for every thread whose kernel stack contains an IOKit/kext return address,
# prints the thread, its wait event and each stack word that resolves to a kernel symbol (kext words are printed as raw
# addresses with their page offset so they can be matched against a kext disassembly).
#   gdb -q -batch KERNEL -ex "symbol-file KERNEL -o SLIDE" -ex "target remote :1234" -ex "source scripts/gdb-threads.py"
import gdb, re

inf = gdb.selected_inferior()

def rd(addr, n):
    try:
        return bytes(inf.read_memory(addr, n))
    except gdb.MemoryError:
        return None

def u64(addr):
    b = rd(addr, 8)
    return int.from_bytes(b, 'little') if b else None

def sym(a):
    try:
        s = gdb.execute('info symbol 0x%x' % a, to_string=True).strip()
    except gdb.error:
        return None
    if s.startswith('No symbol'):
        return None
    return s.split(' in section')[0]

head = gdb.parse_and_eval('&threads')
thr_t = gdb.lookup_type('struct thread').pointer()
off_threads = [f.bitpos // 8 for f in gdb.lookup_type('struct thread').fields() if f.name == 'threads'][0]

cur = int(head['next']) if False else None
q = gdb.parse_and_eval('threads')
nxt = int(q['next'])
seen = 0
out = []
while nxt != int(head) and seen < 4000:
    t = gdb.Value(nxt - off_threads).cast(thr_t)
    seen += 1
    nxt = int(gdb.parse_and_eval('((queue_chain_t*)%d)->next' % nxt))
    try:
        ks = int(t['kernel_stack'])
        state = int(t['state'])
        wev = int(t['wait_event'])
        tid = int(t['thread_id'])
    except gdb.error:
        continue
    if not ks:
        continue
    words = []
    for i in range(0, 16384 // 8):
        v = u64(ks + i * 8)
        if v is None:
            break
        words.append(v)
    # candidate return addresses: kernel text (symbolized) or kext text (high addr, not in kernel symbols)
    frames = []
    for i, v in enumerate(words):
        if v < 0xffffff8000000000 or v >= 0xfffffffffffff000:
            continue
        s = sym(v)
        if s:
            if re.search(r'(thread_block|lck_|IOLock|IOService|IOWorkLoop|IOCommandGate|IOEventSource|mach_msg|semaphore|waitq|ipc_mqueue|IORecursive|OSKext|IOKit|IOPM|IOPlat|vfs|vnode|apfs|IORegistry|IOCatalogue)', s):
                frames.append('%s' % s)
        elif (v & 0xfff) and 0xffffff8000000000 <= v < 0xffffff9000000000 and False:
            pass
    if any(re.search(r'IOService|IOCatalogue|OSKext|IORegistry|IOLockSleep|IORecursive', f) for f in frames):
        out.append((tid, hex(state), hex(wev), ks, frames))

for tid, st, wev, ks, frames in out:
    print('=== thread %d state %s wait_event %s kstack %#x' % (tid, st, wev, ks))
    for f in frames[:30]:
        print('   ', f)
print('threads walked: %d, IOKit-related: %d' % (seen, len(out)))
