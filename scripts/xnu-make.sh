#!/bin/bash
# xnu-make.sh -- run XNU's make on Linux with the cross toolchain. The same script is in every branch; scripts/version.conf holds
# what differs per macOS version (SDK version, kernel version string, extra make variables).
#   scripts/xnu-make.sh installhdrs
#   scripts/xnu-make.sh -Otarget -k -j$(nproc)          builds the RELEASE kernel
# Env: XNU_SRC (src/<name>, default xnu), XNU_OBJ (build/<name>), XCROSS_MARCH (CPU baseline, e.g. k8-sse3 or amdfam10),
#      XCROSS_LD (linker, default Apple ld64), XCROSS_SDK (SDK dir, default toolchain/sdk)
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
T=$W/toolchain
. "$W/scripts/version.conf"
export PATH=$T/bin:$PATH
export SDKROOT=${XCROSS_SDK:-$T/sdk}
export XCROSS_SDKVER
# kernelmanagerd derives the kernel variant from the version string suffix (Tahoe): version.conf can set a default object root
[ -n "${KERNEL_BUILD_OBJROOT_DEFAULT:-}" ] && export KERNEL_BUILD_OBJROOT=${KERNEL_BUILD_OBJROOT-$KERNEL_BUILD_OBJROOT_DEFAULT}
[ -n "${RC_DARWIN_KERNEL_VERSION_DEFAULT:-}" ] && export RC_DARWIN_KERNEL_VERSION=${RC_DARWIN_KERNEL_VERSION:-$RC_DARWIN_KERNEL_VERSION_DEFAULT}
# host tools that XNU builds and runs during the build (SETUP/*)
HOST="HOST_CC=$T/bin/host-cc HOST_FLEX=$(command -v flex) HOST_BISON=$(command -v bison) \
      HOST_GM4=$(command -v m4) HOST_CODESIGN=$T/bin/noop HOST_CODESIGN_ALLOCATE=$T/bin/noop \
      HOST_SDKROOT=/"
[ -n "${MAKE_EXTRA:-}" ] && HOST="$HOST $(eval echo "$MAKE_EXTRA")"
# values the makefiles normally get from macOS's sysctl / sw_vers
MACHINE="SYSCTL_HW_PHYSICALCPU=$(nproc) SYSCTL_HW_LOGICALCPU=$(nproc) \
	 MEMORY_SIZE=$(( $(awk '/MemTotal/{print $2}' /proc/meminfo) * 1024 )) HOST_OS_VERSION=$HOST_OS_VERSION"
export XCROSS_MARCH="${XCROSS_MARCH-}"
export XCROSS_LD="${XCROSS_LD-$T/bin/ld64-apple}"
XSRC=${XNU_SRC:-xnu}
XOBJ=${XNU_OBJ:-$XNU_OBJ_DEFAULT}
cd "$W/src/$XSRC"
exec make XCRUN="$T/bin/xcrun" SDKROOT="$SDKROOT" PLATFORM=MacOSX SDKVERSION=$XCROSS_SDKVER \
	ARCH_CONFIGS=X86_64 KERNEL_CONFIGS=RELEASE \
	DO_CTFCONVERT=0 DO_CTFMERGE=0 \
	PYTHON=true \
	SRCROOT="$W/src/$XSRC" OBJROOT="$W/build/$XOBJ/obj" SYMROOT="$W/build/$XOBJ/sym" \
	DSTROOT="$W/build/$XOBJ/dst" $HOST $MACHINE "$@"
