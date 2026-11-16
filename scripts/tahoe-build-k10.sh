#!/bin/bash
# tahoe-build-k10.sh -- build the Tahoe kernel with an AMD K10 (amdfam10) code baseline into build/xnu-tahoe-k10.
# Then install: KSRC=build/xnu-tahoe-k10/obj/RELEASE_X86_64/kernel.unstripped scripts/tahoe-kc-install.sh
# (kernel.unstripped has the symbols and is what tahoe-kc-install.sh takes; kernel is the stripped one)
cd "$(dirname "$0")/.."
XCROSS_MARCH=${XCROSS_MARCH:-amdfam10} XNU_OBJ=xnu-tahoe-k10 KERNEL_BUILD_OBJROOT=xnu_development-xcross-k10/DEVELOPMENT_X86_64 \
  exec scripts/xnu-make.sh -Otarget -k -j"$(nproc)" "$@"
