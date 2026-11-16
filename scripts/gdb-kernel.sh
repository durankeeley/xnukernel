#!/bin/bash
# gdb-kernel.sh [kernel] [slide-hex] [-- extra gdb args]   Attach gdb to the running VM (start it with GDBPORT=1234 scripts/boot-highsierra.sh)
# Loads the kernel Mach-O symbols (with source lines) shifted by the KASLR slide (slide=100 -> 0xc800000).
# Examples inside gdb:  b tsc_init   |   b opemu_allow   |   bt   |   x/10i $rip   |   info registers   |   si / ni
K=${1:-out/kernel.latest}; SL=${2:-0xc800000}; shift 2 2>/dev/null
exec gdb -q "$K" -ex "set architecture i386:x86-64" -ex "set pagination off" \
     -ex "symbol-file $K -o $SL" -ex "target remote :${GDBPORT:-1234}" "$@"
