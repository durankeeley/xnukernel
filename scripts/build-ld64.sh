#!/bin/bash
# build-ld64.sh -- build Apple's ld64 (cctools-port) for Linux into toolchain/bin/ld64-apple.
# Needs clang, llvm-config and libLTO (the llvm package), see REQUIREMENTS.md. lld cannot be used for the kernel link because it
# ignores -segaddr, -image_base and -seg_page_size.
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
[ -d "$W/src/cctools-port/cctools" ] || "$W/scripts/fetch-deps.sh"
# the pinned cctools-port predates LLVM 23: update its copies of the llvm-c headers
if git -C "$W/src/cctools-port" apply --check "$W/toolchain/patches/cctools-port-llvm23.patch" 2>/dev/null; then
	git -C "$W/src/cctools-port" apply "$W/toolchain/patches/cctools-port-llvm23.patch"
fi
mkdir -p "$W/build/cctools" && cd "$W/build/cctools"
"$W/src/cctools-port/cctools/configure" --prefix="$W/toolchain/cctools" --target=x86_64-apple-darwin17 \
    --with-llvm-config="$(command -v llvm-config)" > configure.log 2>&1
make -j"$(nproc)" -C libstuff > make-libstuff.log 2>&1
make -j"$(nproc)" -C ld64 > make-ld64.log 2>&1
cp ld64/src/ld/ld "$W/toolchain/bin/ld64-apple"
echo "built $W/toolchain/bin/ld64-apple"
# Apple's strip for the kernel: llvm-strip stops with "shared library is not yet supported" on the kernel Mach-O. Needs libmacho.
make -j"$(nproc)" -C libmacho > make-libmacho.log 2>&1
make -C misc strip > make-strip.log 2>&1
cp misc/strip "$W/toolchain/bin/strip-apple"
echo "built $W/toolchain/bin/strip-apple"
