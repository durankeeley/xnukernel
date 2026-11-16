#!/usr/bin/env python3
"""isa-census.py <file-or-dir>... -- count instructions in Mach-O x86_64 binaries that a K10 (Phenom II, family 10h) cannot execute.
K10 has: SSE..SSE3, SSE4a, ABM (popcnt/lzcnt), CMPXCHG16B, RDTSCP, 3DNow!Prefetch. It lacks SSSE3, SSE4.1/4.2, AVX*, FMA, BMI1/2, F16C, MOVBE,
AES-NI, PCLMULQDQ, SHA, RDRAND/RDSEED, XSAVE/XGETBV, ADX, FSGSBASE, INVPCID, CLFLUSHOPT/CLWB. Mnemonic-based (llvm-objdump), so data-in-text
can add noise; counts are indicative, not exact."""
import subprocess, sys, os, re, collections, multiprocessing as mp
SSSE3={'pshufb','palignr','pabsb','pabsw','pabsd','phaddw','phaddd','phaddsw','phsubw','phsubd','phsubsw','pmaddubsw','pmulhrsw','psignb','psignw','psignd'}
SSE41={'ptest','pmovzxbw','pmovzxbd','pmovzxbq','pmovzxwd','pmovzxwq','pmovzxdq','pmovsxbw','pmovsxbd','pmovsxbq','pmovsxwd','pmovsxwq','pmovsxdq','pblendw','pblendvb',
 'blendps','blendpd','blendvps','blendvpd','pextrb','pextrd','pextrq','pinsrb','pinsrd','pinsrq','extractps','insertps','pmulld','pmuldq','pminsb','pminsd','pminuw','pminud',
 'pmaxsb','pmaxsd','pmaxuw','pmaxud','roundps','roundpd','roundss','roundsd','dpps','dppd','mpsadbw','packusdw','pcmpeqq','phminposuw','movntdqa'}
SSE42={'pcmpestri','pcmpestrm','pcmpistri','pcmpistrm','pcmpgtq','crc32','crc32b','crc32w','crc32l','crc32q'}
BMI={'andn','bextr','blsi','blsmsk','blsr','tzcnt','bzhi','mulx','pdep','pext','rorx','sarx','shlx','shrx'}
MISC={'movbe':'MOVBE','rdrand':'RDRAND','rdseed':'RDSEED','adcx':'ADX','adox':'ADX','pclmulqdq':'PCLMUL','xsave':'XSAVE','xsavec':'XSAVE','xsaveopt':'XSAVE','xsaves':'XSAVE',
 'xrstor':'XSAVE','xrstors':'XSAVE','xgetbv':'XSAVE','xsetbv':'XSAVE','rdfsbase':'FSGSBASE','rdgsbase':'FSGSBASE','wrfsbase':'FSGSBASE','wrgsbase':'FSGSBASE','invpcid':'INVPCID',
 'clflushopt':'CLFLUSHOPT','clwb':'CLWB','rdpid':'RDPID','vzeroupper':'AVX','vzeroall':'AVX'}
AES={'aesenc','aesenclast','aesdec','aesdeclast','aeskeygenassist','aesimc'}
SHA={'sha1rnds4','sha1nexte','sha1msg1','sha1msg2','sha256rnds2','sha256msg1','sha256msg2'}
def classify(m):
    if m in SSSE3: return 'SSSE3'
    if m in SSE41: return 'SSE4.1'
    if m in SSE42: return 'SSE4.2'
    if m in BMI: return 'BMI'
    if m in AES: return 'AES-NI'
    if m in SHA: return 'SHA'
    if m in MISC: return MISC[m]
    if m.startswith('v') and len(m) > 3 and not m.startswith(('verr','verw')): return 'AVX/AVX2/FMA'   # VEX-encoded
    return None
def scan(path):
    """Stream llvm-objdump output instead of capturing it (some kexts disassemble to hundreds of MB)."""
    c = collections.Counter(); tot = 0
    try:
        pr = subprocess.Popen(['llvm-objdump', '-d', '--no-show-raw-insn', '--macho', '--arch=x86_64', path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors='replace')
        for l in pr.stdout:
            p = l.split('\t')
            if len(p) >= 2 and p[0].rstrip().endswith(':'):
                m = p[1].strip().split(None, 1)
                tot += 1
                if m:
                    k = classify(m[0])
                    if k: c[k] += 1
        pr.wait()
    except Exception:
        return path, None
    return path, (tot, c)
if __name__ == '__main__':
    files = []
    for a in sys.argv[1:]:
        if os.path.isdir(a):
            for dp, dn, fn in os.walk(a):
                for f in fn:
                    p = os.path.join(dp, f)
                    if os.path.isfile(p) and not p.endswith(('.plist', '.dSYM', '.yml')) and '.dSYM/' not in p and '_kasan' not in p: files.append(p)
        else: files.append(a)
    with mp.Pool(6) as pool: res = list(pool.imap_unordered(scan, files, chunksize=1))
    agg = collections.Counter(); nbin = collections.Counter(); alltot = 0; bad = []
    for p, r in res:
        if not r: continue
        tot, c = r; alltot += tot
        for k, v in c.items(): agg[k] += v; nbin[k] += 1
        if c: bad.append((sum(c.values()), p, tot, dict(c)))
    print('binaries scanned: %d, instructions: %d' % (len(res), alltot))
    for k, v in agg.most_common(): print('  %-14s %9d instrs in %4d binaries' % (k, v, nbin[k]))
    print('top binaries by unsupported-instruction count:')
    for n, p, tot, c in sorted(bad, reverse=True)[:int(os.environ.get('TOP', '15'))]:
        print('  %7d of %8d  %s  %s' % (n, tot, os.path.relpath(p), ' '.join('%s=%d' % kv for kv in sorted(c.items(), key=lambda x: -x[1])[:4])))
