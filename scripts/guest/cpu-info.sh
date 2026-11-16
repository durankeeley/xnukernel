#!/bin/sh
# cpu-info.sh -- print what a test screenshot should show: which kernel booted, which processor it sees, and what that processor can do.
# Works in Recovery and on an installed system (only sysctl, ls, grep). Uploads a copy to the host agent server (build/agent/files/cpu-info.out).
O=/tmp/cpu-info.out
{
echo "== kernel"
sysctl kern.version kern.osproductversion
sysctl kern.bootargs 2>/dev/null
echo "kernel files:"; ls /System/Library/Kernels /System/Library/PrelinkedKernels 2>/dev/null | grep -v "^$"
echo "== processor"
sysctl machdep.cpu.brand_string machdep.cpu.vendor machdep.cpu.family machdep.cpu.model machdep.cpu.stepping hw.physicalcpu hw.logicalcpu hw.cputype hw.cpusubtype
echo "== capabilities as the kernel reports them (CPUID features, then what userland sees in hw.optional)"
sysctl machdep.cpu.features machdep.cpu.extfeatures machdep.cpu.leaf7_features 2>/dev/null
# one call per name: sysctl stops at the first name this macOS does not have
for k in sse3 supplementalsse3 sse4_1 sse4_2 popcnt aes avx1_0 avx2_0 x86_64; do sysctl hw.optional.$k 2>/dev/null; done
} > $O 2>&1
cat $O
curl -s -T $O http://10.0.2.2:8000/upload/cpu-info.out > /dev/null 2>&1
