#!/usr/bin/env python3
"""stack-scan.py KERNEL SLIDE DUMP:BASE... -- recover kernel call stacks from a raw physical-memory dump (no DWARF needed).
A stack frame is a (saved rbp, return address) pair: rbp points into the kernel-stack region and the return address is in kernel/kext
text. Frames are grouped per 4 KiB page (lowest address = innermost call) and symbolised against the kernel's symbol table; kext return
addresses (beyond kernel text) are printed as raw addresses.  Prints the pages whose stacks mention a function matching PATTERN (env).
  scripts/stack-scan.py build/.../kernel.unstripped 0xC800000 /tmp/ram-lo.bin:0 /tmp/ram-hi.bin:0x100000000"""
import sys, os, re, bisect, subprocess
import numpy as np

K = sys.argv[1]; SLIDE = int(sys.argv[2], 0)
PAT = re.compile(os.environ.get('PATTERN', r'thread_block|IOLockSleep|IOService|IOCatalogue|OSKext|IORegistry|lck_mtx_sleep|IORecursiveLockSleep|waitForService'))
nm = subprocess.run(['llvm-nm', '-n', '--defined-only', K], capture_output=True, text=True).stdout
syms = [(int(l.split()[0], 16) + SLIDE, l.split()[2]) for l in nm.splitlines() if len(l.split()) >= 3 and l.split()[1] in 'tT']
addrs = [a for a, _ in syms]
text_lo = syms[0][0]; text_hi = max(a for a in addrs) + 0x4000   # SLIDE here is the file->runtime text delta (kernel collection layout), e.g. 0xc8e8000
CODE_LO, CODE_HI = text_lo, text_lo + 0x30000000          # kernel text + the kernel-collection kexts that follow
STK_LO, STK_HI = 0xffffffc000000000, 0xffffffe000000000   # kernel stack region seen at rsp=0xffffffd3...

def name(a):
    if a < text_hi:
        i = bisect.bisect_right(addrs, a) - 1
        return '%s+%#x' % (syms[i][1], a - syms[i][0])
    return 'KEXT %#x (page off %#x)' % (a, a & 0xfff)

pages = {}
for spec in sys.argv[3:]:
    path, base = spec.rsplit(':', 1); base = int(base, 0)
    mm = np.memmap(path, dtype=np.uint64, mode='r')
    CH = 1 << 23
    for s in range(0, len(mm) - 1, CH):
        a = np.asarray(mm[s:s + CH + 1])
        sp = (a[:-1] >= STK_LO) & (a[:-1] < STK_HI) & (a[1:] >= CODE_LO) & (a[1:] < CODE_HI)
        for i in np.nonzero(sp)[0]:
            pa = base + (s + int(i)) * 8
            pages.setdefault(pa >> 12, []).append((pa & 0xfff, int(a[i + 1])))
    print('scanned', path, 'pages with frames so far:', len(pages), file=sys.stderr)

LOW12 = [int(x,16) for x in os.environ.get('LOW12','').split(',') if x]
shown = 0
for pg in sorted(pages):
    fr = sorted(pages[pg])
    if len(fr) < 2: continue
    names = [name(r) for _, r in fr]
    if LOW12:
        if not any((r & 0xfff) in LOW12 and r >= text_hi for _, r in fr): continue
    elif not any(PAT.search(n) for n in names): continue
    print('--- page %#x (%d frames, innermost first)' % (pg << 12, len(fr)))
    for (off, r), n in zip(fr, names): print('   %#05x  %s' % (off, n))
    shown += 1
    if shown >= int(os.environ.get('MAXPAGES', '60')): break
print('pages shown:', shown, 'of', len(pages))
