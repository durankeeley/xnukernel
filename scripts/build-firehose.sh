#!/bin/bash
# build-firehose.sh -- build libfirehose_kernel.a from Apple's open source libdispatch (src/firehose/firehose_buffer.c), the way
# libdispatch's own libfirehose_kernel target does (xcodeconfig/libfirehose_kernel.xcconfig). The kernel links it for os_log.
# It also installs the target's private header (os/firehose_buffer_private.h, unifdef'd for the kernel) where the kernel build looks for it.
set -eu
W=$(cd "$(dirname "$0")/.." && pwd)
. "$W/scripts/version.conf"; export XCROSS_SDKVER
SDK=${XCROSS_SDK:-$W/toolchain/sdk}
LD=$W/src/libdispatch
KF=$SDK/System/Library/Frameworks/Kernel.framework/Versions/A
export PATH=$W/toolchain/bin:$PATH
mkdir -p "$SDK/usr/local/include/kernel/os" "$SDK/usr/local/lib/kernel" "$W/build/firehose"
# libdispatch installs the private header through unifdef (COPY_HEADERS_UNIFDEF_FLAGS in xcodeconfig/libfirehose_kernel.xcconfig)
unifdef -DKERNEL=1 -DOS_FIREHOSE_SPI=1 -DOS_VOUCHER_ACTIVITY_SPI_TYPES=1 -UOS_VOUCHER_ACTIVITY_SPI -o "$SDK/usr/local/include/kernel/os/firehose_buffer_private.h" "$LD/os/firehose_buffer_private.h" || [ $? = 1 ]
"$W/toolchain/bin/darwin-clang" -c -O2 -o "$W/build/firehose/firehose_buffer.o" "$LD/src/firehose/firehose_buffer.c" \
	-mkernel -nostdinc -Wno-packed -DKERNEL=1 -DDISPATCH_USE_DTRACE=0 -DOS_FIREHOSE_SPI=1 \
	-DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DOS_ATOMIC_CONFIG_STARVATION_FREE_ONLY=0 \
	-I "$LD" -I "$KF/PrivateHeaders" -I "$KF/Headers" -I "$SDK/usr/local/include/os" -I "$SDK/usr/local/include/firehose" \
	-I "$W/src/xnu/EXTERNAL_HEADERS" -I "$SDK/usr/include"
rm -f "$SDK/usr/local/lib/kernel/libfirehose_kernel.a"
llvm-ar rcs "$SDK/usr/local/lib/kernel/libfirehose_kernel.a" "$W/build/firehose/firehose_buffer.o"
echo "built $SDK/usr/local/lib/kernel/libfirehose_kernel.a"
