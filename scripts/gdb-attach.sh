#!/bin/bash
# gdb-attach.sh [-k kernel] [-s slide|auto] [-p port] [-x gdb-commands-file] [-- extra gdb args]
# Attach gdb to the running VM's gdb stub (start the VM with GDBPORT=1234, e.g. GDBPORT=1234 scripts/boot-highsierra.sh) with the kernel's symbols and
# source lines, plus the xcross commands (xslide, xcpus, xthreads, xpanic: scripts/gdb/xcross.py). Works for every kernel built here.
#   -k   kernel.unstripped of the build that is running. Default: the newest build/*/obj/RELEASE_X86_64/kernel.unstripped
#   -s   KASLR slide: a hex value (boot-arg slide=100 gives 0xc800000) or auto (default: find the Mach-O header in guest memory)
#   -x   a file of gdb commands to run after attaching (batch mode, then detach), e.g. -x /dev/stdin for a here-document
# Under KVM use hbreak (hardware breakpoints), plain break does not work there. After attaching the guest is stopped; "continue" or "detach" resumes it.
cd "$(dirname "$0")/.."
K=; SL=auto; PORT=${GDBPORT:-1234}; X=
while getopts "k:s:p:x:" o; do case $o in k) K=$OPTARG;; s) SL=$OPTARG;; p) PORT=$OPTARG;; x) X=$OPTARG;; esac; done; shift $((OPTIND - 1))
[ -n "$K" ] || K=$(ls -t build/*/obj/RELEASE_X86_64/kernel.unstripped 2>/dev/null | head -1)
[ -f "$K" ] || { echo "no kernel.unstripped found, pass -k"; exit 1; }
args=(-q "$K" -ex "set architecture i386:x86-64" -ex "set pagination off" -ex "python XKERNEL='$(readlink -f "$K")'" -ex "source scripts/gdb/xcross.py")
if [ "$SL" = auto ]; then
  args+=(-ex "target remote :$PORT" -ex "xslide")
else
  args+=(-ex "symbol-file $K -o $SL" -ex "target remote :$PORT")
fi
[ -n "$X" ] && args=(-batch "${args[@]}" -x "$X" -ex detach)
exec gdb "${args[@]}" "$@"
