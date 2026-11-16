#!/bin/bash
# opemu-avxtest.sh [iterations] [v]  -- compile osfmk/OPEMU/opemu_avx.c on the host and compare it with the host CPU's real AVX2/BMI
# instructions (needs AVX2 + BMI2). With a second argument, also lists forms the engine does not implement yet.
set -eu
W=$(cd "$(dirname "$0")/.." && pwd); T=$W/scripts/opemu-avxtest; O=$W/build/opemu-avxtest; SRC=${OPEMU_SRC:-$W/src/xnu/osfmk/OPEMU}
mkdir -p "$O"
clang -O1 -w -I"$SRC" -c "$SRC/opemu_avx.c" -o "$O/opemu_avx.o"
[ -f "$SRC/opemu_avx_fp.c" ] && clang -O1 -w -msse4.2 -I"$SRC" -c "$SRC/opemu_avx_fp.c" -o "$O/opemu_avx_fp.o"
clang -O1 -w -mavx2 -I"$SRC" -c "$T/avxtest.c" -o "$O/avxtest.o"
clang -c "$T/run_native.S" -o "$O/run_native.o"
clang "$O"/*.o -o "$O/avxtest"
"$O/avxtest" "${1:-20}" ${2:+v}
