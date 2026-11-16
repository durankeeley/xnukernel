# xcross.py -- kernel-aware gdb commands for the xnu kernels built here (High Sierra and Tahoe, x86_64). Loaded by scripts/gdb-attach.sh.
#   xslide              find the KASLR slide of the running kernel and reload the symbols at it (needs a booted kernel, not a firmware stage)
#   xcpus               what every CPU is doing: RIP symbol and whether it is idle (all idle = the work is in blocked threads)
#   xthreads [pattern]  blocked kernel threads: process, pid, state, wait event, and the kernel symbols on its stack, innermost first.
#                       The optional pattern keeps threads whose stack contains it, e.g. xthreads IOService  or  xthreads hfs
#   xps                 every process: pid, name, thread count
#   xbt <name|pid>      frame-pointer backtrace of that process's blocked kernel threads (from the context saved on the kernel stack)
#   xpanic              the panic string, if the kernel panicked
# All of them read guest memory through the QEMU gdb stub (works under KVM and TCG). Output is plain text so it can be pasted into an issue.
import gdb

KSTACK = 16384
_inf = lambda: gdb.selected_inferior()


def rd(addr, n):
    try:
        return bytes(_inf().read_memory(addr & 0xffffffffffffffff, n))
    except (gdb.MemoryError, gdb.error):
        return None


def u64(a):
    b = rd(a, 8) if a else None
    return int.from_bytes(b, 'little') if b else None


_symcache = {}


def sym(a):
    if a in _symcache:
        return _symcache[a]
    try:
        s = gdb.execute('info symbol 0x%x' % (a & 0xffffffffffffffff), to_string=True).strip()
    except gdb.error:
        s = ''
    r = None if (not s or s.startswith('No symbol')) else s.split(' in section')[0]
    _symcache[a] = r
    return r


def cstr(a, n=17):
    b = rd(a, n)
    return b.split(b'\0')[0].decode('ascii', 'replace') if b else '?'


def fields(typename):
    return {f.name: f.bitpos // 8 for f in gdb.lookup_type(typename).fields() if f.name}


class XSlide(gdb.Command):
    """Find the kernel's KASLR slide by looking for the Mach-O header and reload the symbols at that slide."""
    def __init__(self):
        super().__init__('xslide', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            base = int(gdb.parse_and_eval('(unsigned long)&_mh_execute_header')) & 0xffffffffffffffff
        except gdb.error:
            print('no _mh_execute_header symbol: load the kernel with symbol-file first (scripts/gdb-attach.sh does)')
            return
        kernel = globals().get('XKERNEL')
        for step in range(0, 0x20000000 // 0x100000):
            cand = base + step * 0x100000
            if rd(cand, 4) == b'\xcf\xfa\xed\xfe':
                print('slide = %#x' % (step * 0x100000))
                if kernel:
                    gdb.execute('symbol-file %s -o %#x' % (kernel, step * 0x100000))
                else:
                    print('set XKERNEL (python XKERNEL="path") to reload the symbols automatically')
                return
        print('Mach-O header not found within 512 MB of the unslid address: is the kernel running yet?')


class XCpus(gdb.Command):
    """Show each CPU's RIP and whether it is in the idle loop."""
    def __init__(self):
        super().__init__('xcpus', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        for th in _inf().threads():
            th.switch()
            pc = int(gdb.parse_and_eval('(unsigned long)$pc')) & 0xffffffffffffffff
            s = sym(pc) or '?'
            print('cpu %d  rip %#x  %s%s' % (th.num - 1, pc, s, '   (idle)' if ('idle' in s or 'hlt' in s.lower() or 'machine_idle' in s) else ''))


class XThreads(gdb.Command):
    """xthreads [pattern] -- list blocked kernel threads with their kernel stack symbols."""
    def __init__(self):
        super().__init__('xthreads', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        want = arg.strip()
        off = fields('struct thread'); toff = fields('struct task')
        try:
            poff = fields('struct proc')
        except gdb.error:
            poff = {}
        # In these xnu trees the queue links hold pointers to the OBJECTS (not to the link field) and the list ends at the head's address,
        # so the generic container_of walk does not work: next = *(object + offsetof(link)).
        head = int(gdb.parse_and_eval('(unsigned long)&threads')) & 0xffffffffffffffff
        t = u64(head); seen = shown = 0
        total = int(gdb.parse_and_eval('threads_count')) if gdb.lookup_global_symbol('threads_count') else 0
        while t and t != head and seen < 20000:
            seen += 1
            nxt = u64(t + off['threads'])
            ks = u64(t + off['kernel_stack'])
            task = u64(t + off['task']); name, pid = '?', -1
            try:
                proc = u64(task + toff['bsd_info'])
                if proc and poff:
                    name = cstr(proc + poff['p_comm'])
                    pid = int.from_bytes(rd(proc + poff['p_pid'], 4) or b'\xff\xff\xff\xff', 'little', signed=True)
            except (KeyError, TypeError):
                pass
            t_cur = t; t = nxt
            if not ks:
                continue                       # on a CPU, or a user thread that is in user space
            frames = []
            for i in range(KSTACK // 8):
                v = u64(ks + i * 8)
                if v and 0xffffff8000000000 <= v < 0xffffff9000000000 + 0x40000000:
                    sy = sym(v)
                    if sy and not sy.startswith(('thread_invoke', 'call_continuation', 'Switch_context', 'thread_block_reason')) and sy not in frames:
                        frames.append(sy)
            if not frames or (want and not any(want in f for f in frames)):
                continue
            shown += 1
            state = int.from_bytes(rd(t_cur + off['state'], 4) or b'\0\0\0\0', 'little')
            wev = u64(t_cur + off['wait_event']) or 0
            print('=== %s[%d]  state %#x  wait_event %#x%s' % (name, pid, state, wev, ('  (' + sym(wev) + ')') if wev and sym(wev) else ''))
            for f in frames[:14]:
                print('      ', f)
        print('threads walked: %d (threads_count %d), with kernel stacks: %d' % (seen, total, shown))


class XPs(gdb.Command):
    """xps -- list all processes: pid, name, number of threads."""
    def __init__(self):
        super().__init__('xps', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        toff = fields('struct task'); poff = fields('struct proc')
        head = int(gdb.parse_and_eval('(unsigned long)&tasks')) & 0xffffffffffffffff
        t = u64(head); n = 0
        while t and t != head and n < 5000:     # task links hold object pointers, see xthreads
            n += 1; cur = t; t = u64(cur + toff['tasks'])
            proc = u64(cur + toff['bsd_info'])
            name, pid = ('kernel_task', 0)
            if proc:
                name = cstr(proc + poff['p_comm'])
                pid = int.from_bytes(rd(proc + poff['p_pid'], 4) or b'\xff' * 4, 'little', signed=True)
            nthr = int.from_bytes(rd(cur + toff['thread_count'], 4) or b'\0' * 4, 'little') if 'thread_count' in toff else -1
            print('%5d  %-20s threads %d' % (pid, name, nthr))
        print('%d tasks' % n)


class XBt(gdb.Command):
    """xbt <process-name or pid> -- frame-pointer backtrace of that process's blocked kernel threads, from the context saved at the top of the kernel stack."""
    def __init__(self):
        super().__init__('xbt', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        want = arg.strip()
        off = fields('struct thread'); toff = fields('struct task'); poff = fields('struct proc')
        iks = fields('struct x86_kernel_state')
        iks_size = gdb.lookup_type('struct x86_kernel_state').sizeof
        head = int(gdb.parse_and_eval('(unsigned long)&threads')) & 0xffffffffffffffff
        t = u64(head); n = 0
        while t and t != head and n < 20000:
            n += 1; cur = t; t = u64(cur + off['threads'])
            task = u64(cur + off['task']); proc = u64(task + toff['bsd_info']) if task else None
            if not proc:
                continue
            name = cstr(proc + poff['p_comm']); pid = int.from_bytes(rd(proc + poff['p_pid'], 4) or b'\xff' * 4, 'little', signed=True)
            if want not in (name, str(pid)):
                continue
            ks = u64(cur + off['kernel_stack'])
            if not ks:
                continue
            st = ks + KSTACK - iks_size
            rip = u64(st + iks['k_rip']); rbp = u64(st + iks['k_rbp'])
            print('=== %s[%d] thread %#x  wait_event %#x' % (name, pid, cur, u64(cur + off['wait_event']) or 0))
            print('   #0  %#x  %s' % (rip or 0, sym(rip) if rip else '?'))
            i = 1
            while rbp and i < 40:
                ret = u64(rbp + 8); nb = u64(rbp)
                if not ret or not (0xffffff8000000000 <= ret):
                    break
                print('   #%d  %#x  %s' % (i, ret, sym(ret) or '?'))
                if not nb or nb <= rbp:
                    break
                rbp = nb; i += 1


class XPanic(gdb.Command):
    """Print the kernel's panic string."""
    def __init__(self):
        super().__init__('xpanic', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            p = int(gdb.parse_and_eval('(unsigned long)panicstr'))
        except gdb.error:
            print('no panicstr symbol'); return
        print(cstr(p, 512) if p else 'no panic (panicstr is NULL)')


XSlide(); XCpus(); XThreads(); XBt(); XPs(); XPanic()
