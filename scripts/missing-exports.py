#!/usr/bin/env python3
"""missing-exports.py -- symbols that x86_64 KDK kexts import and the stock kernel exports, but our kernel does not define.
Writes /tmp/missing-exports.txt (one mangled symbol per line) and prints a summary grouped by class/prefix."""
import subprocess, sys, glob, os, collections
KDK='build/kdk-real/kdk/System/Library'
STOCK=KDK+'/Kernels/kernel.development'; OURS=sys.argv[1] if len(sys.argv)>1 else 'out/kernel.tahoe-unstripped.x86_64'
def nm(args):
    return subprocess.run(['llvm-nm']+args,capture_output=True,text=True).stdout.splitlines()
def defined(path):
    s=set()
    for l in nm(['--defined-only','--extern-only',path]):
        p=l.split()
        if len(p)==3: s.add(p[2])
    return s
st=defined(STOCK); ou=defined(OURS)
# undefined symbols of every x86_64 kext binary
und=collections.defaultdict(set)
for f in glob.glob(KDK+'/Extensions/**/Contents/MacOS/*',recursive=True)+glob.glob(KDK+'/Extensions/System.kext/PlugIns/*/*'):
    if f.endswith(('_kasan','_development')) or not os.path.isfile(f): continue
    r=subprocess.run(['llvm-nm','--undefined-only','--arch=x86_64',f],capture_output=True,text=True)
    if not r.stdout.strip(): r=subprocess.run(['llvm-nm','--undefined-only',f],capture_output=True,text=True)
    out=r.stdout.splitlines()
    for l in out:
        p=l.split()
        if p and not p[-1].endswith(':'): und[p[-1]].add(os.path.basename(f))
need=[s for s in und if s in st and s not in ou]
open('/tmp/missing-exports.txt','w').write('\n'.join(sorted(need)))
print('kext-imported symbols exported by stock but missing in ours: %d (of %d imported)'%(len(need),len(und)))
imp_missing_both=[s for s in und if s not in st and s not in ou]
print('imported symbols exported by neither (provided by other kexts): %d'%len(imp_missing_both))
import re
g=collections.Counter()
for s in need:
    m=re.match(r'__ZN?K?(\d+)([A-Za-z_0-9]+)',s)
    g[m.group(2)[:int(m.group(1))] if m else s[:20]]+=1
for k,v in g.most_common(40): print('%5d  %s'%(v,k))
