#!/bin/bash
# gdb-panic.sh [kernel] [slide-hex] -- attach to a Tahoe VM started with GDBPORT=1234 (boot-arg slide=1), break on panic, dump string + backtrace.
K=${1:-out/kernel.tahoe-unstripped.x86_64}; SL=${2:-0x200000}
exec timeout ${GDBTIMEOUT:-420} gdb -q -batch "$K" -ex "set architecture i386:x86-64" -ex "set pagination off" \
  -ex "symbol-file $K -o $SL" -ex "target remote :${GDBPORT:-1234}" -ex "hbreak panic" -ex "hbreak Debugger" -ex "hbreak panic_trap" -ex "hbreak kernel_trap" \
  -ex "continue" -ex "bt 25" -ex "info registers rip rsp rdi cr2" -ex "x/s \$rdi" -ex "x/s \$rsi"
