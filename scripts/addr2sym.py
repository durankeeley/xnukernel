#!/usr/bin/env python3
"""addr2sym.py <kernel> <slide-hex|auto:<rip>,<rip>...> <addr>...   Map runtime addresses to kernel symbols.
usage: addr2sym.py kernel 0x10000000 0xffffff80105be91f ..."""
import subprocess, sys, bisect
k=sys.argv[1]; slide=int(sys.argv[2],16)
syms=[]
for l in subprocess.run(['llvm-nm','-n','--defined-only',k],capture_output=True,text=True).stdout.splitlines():
    p=l.split()
    if len(p)==3 and p[1] in 'tTsSdDbBaA': syms.append((int(p[0],16),p[2]))
addrs=[s[0] for s in syms]
for a in sys.argv[3:]:
    u=int(a,16)-slide
    i=bisect.bisect_right(addrs,u)-1
    print(f'{a} -> {syms[i][1]}+{u-syms[i][0]:#x}' if i>=0 else f'{a} -> ?')
