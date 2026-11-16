#!/bin/bash
# bootstrap.sh -- build everything the kernel build needs, from a fresh checkout. Identical in every branch: what differs per macOS
# version is the pinned dependency versions (scripts/deps.txt) and scripts/version.conf.
#   1. fetch the pinned sources           2. Apple ld64 for Linux          3. tcsh          4. MIG
#   5. the SDK: TargetConditionals.h, libplatform's os/internal headers (where the pinned release has them), availability.pl (AvailabilityVersions), the headers XNU
#      installs for itself (make installhdrs), then libfirehose_kernel.a built from libdispatch's source
# Afterwards build the kernel with scripts/xnu-make.sh (see README.md). Install the packages in REQUIREMENTS.md first.
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
cd "$W"
. scripts/version.conf
SDK=toolchain/sdk; OBJ=${XNU_OBJ:-$XNU_OBJ_DEFAULT}
scripts/fetch-deps.sh
[ -x toolchain/bin/ld64-apple ] || scripts/build-ld64.sh
[ -x toolchain/bin/tcsh ] || scripts/build-tcsh.sh
[ -x toolchain/mig/mig ] || scripts/build-mig.sh
mkdir -p "$SDK/usr/include" "$SDK/usr/local/include/os/internal" "$SDK/usr/local/libexec" build
chmod -R u+w "$SDK"
# TargetConditionals.h is hand written: the open source CarbonHeaders copy predates TARGET_OS_OSX and friends
cp toolchain/sdk-seed/usr/include/TargetConditionals.h "$SDK/usr/include/"
# libplatform releases that ship os/internal headers (the High Sierra era ones) install them where libdispatch and xnu look
for h in src/libplatform/private/os/internal/*.h; do [ -f "$h" ] && cp "$h" "$SDK/usr/local/include/os/internal/"; done
# bsd/sys/make_symbol_aliasing.sh runs availability.pl from the SDK
cp src/AvailabilityVersions/availability.pl "$SDK/usr/local/libexec/"; chmod +x "$SDK/usr/local/libexec/availability.pl"
scripts/xnu-make.sh installhdrs > build/installhdrs.log 2>&1 || { echo "installhdrs failed, see build/installhdrs.log"; exit 1; }
chmod -R u+w "$SDK"
cp -a "build/$OBJ/dst/." "$SDK/"
scripts/build-firehose.sh
echo "bootstrap done. Build the kernel with: scripts/xnu-make.sh -Otarget -k -j\$(nproc)"
