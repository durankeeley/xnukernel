#!/bin/bash
# kext-undef.sh <kexts-dir> <stock-kernel> <our-kernel> <outdir>
# Lists kernel symbols that stock kexts import (resolved by the stock kernel) but our kernel lacks.
set -eu
K=$1; STOCK=$2; OURS=$3; OUT=$4; mkdir -p "$OUT"
llvm-nm -j --defined-only "$STOCK" 2>/dev/null | sort -u > "$OUT/stock.def"
llvm-nm -j --defined-only "$OURS"  2>/dev/null | sort -u > "$OUT/ours.def"
: > "$OUT/kext-undef.raw"
find "$K" -path '*/Contents/MacOS/*' -type f | while read -r f; do
  llvm-nm -u -j "$f" 2>/dev/null | sed "s|\$|\t${f#$K/System/Library/Extensions/}|"
done > "$OUT/kext-undef.raw" || true
cut -f1 "$OUT/kext-undef.raw" | sort -u > "$OUT/kext-undef.sym"
comm -12 "$OUT/kext-undef.sym" "$OUT/stock.def" > "$OUT/need-from-kernel.sym"
comm -23 "$OUT/need-from-kernel.sym" "$OUT/ours.def" > "$OUT/missing-in-ours.sym"
wc -l "$OUT"/*.sym
