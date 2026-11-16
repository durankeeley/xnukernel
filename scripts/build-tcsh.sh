#!/bin/bash
# build-tcsh.sh -- build tcsh into toolchain/tcsh. XNU's SETUP/config/doconf is a csh script and macOS has csh, Linux does not.
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
[ -d "$W/src/tcsh" ] || "$W/scripts/fetch-deps.sh"
mkdir -p "$W/build/tcsh" && cd "$W/build/tcsh"
"$W/src/tcsh/configure" --prefix="$W/toolchain/tcsh" > configure.log 2>&1
make -j"$(nproc)" > make.log 2>&1
make install > install.log 2>&1
ln -sf ../tcsh/bin/tcsh "$W/toolchain/bin/tcsh"
ln -sf ../tcsh/bin/tcsh "$W/toolchain/bin/csh"
echo "built $W/toolchain/tcsh/bin/tcsh"
