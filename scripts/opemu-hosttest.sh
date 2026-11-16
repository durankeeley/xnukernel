#!/bin/bash
# opemu-hosttest.sh [iterations]   Compile the REAL OPEMU emulation sources (ssse3.c, sse3.c, sse42.c, opemu_math.c) on the host
# and compare every emulated instruction with the host CPU's native instruction on random + edge-case inputs.
# Host CPU must support SSSE3/SSE4.2/AVX-free SSE3 (any x86-64 since ~2010). Result: build/opemu-hosttest/report.txt
set -eu
W=$(cd "$(dirname "$0")/.." && pwd); T=$W/scripts/opemu-hosttest; O=$W/build/opemu-hosttest; SRC=${OPEMU_SRC:-$W/src/xnu/osfmk/OPEMU}
mkdir -p "$O"; cd "$T"; python3 gen_imm.py
CF="-D__UD_STANDALONE__=1 -std=gnu99 -fgnu89-inline -O1 -w -mssse3 -msse4.2 -mno-avx -mno-implicit-float -fno-strict-aliasing -I$T/shim -I$SRC -I$T -include $T/shim_types.h"
for f in ssse3 sse3 sse42 sse41 opemu_util opemu_math opemu_fast opemu_policy; do clang $CF -c "$SRC/$f.c" -o "$O/$f.o"; done
for u in "$SRC"/libudis86/*.c; do clang $CF -c "$u" -o "$O/ud_$(basename "$u" .c).o"; done
for f in ht_main ht_ssse3 ht_sse3 ht_sse41 ht_util ht_fast ht_decode; do clang $CF -c "$T/$f.c" -o "$O/$f.o"; done
clang -Wl,--allow-multiple-definition "$O"/*.o -lm -o "$O/opemu-hosttest"
"$O/opemu-hosttest" "${1:-20000}" | tee "$O/report.txt"
