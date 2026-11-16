#!/bin/bash
# tahoe-build-k8.sh -- build the Tahoe kernel with an AMD K8 (k8-sse3) code baseline into build/xnu-tahoe-k8.
# The baseline matters: a kernel built for the default Penryn target contains SSE4.1 in early boot code and faults on K8/K10 before
# any trap handler exists. Then install: KSRC=build/xnu-tahoe-k8/obj/RELEASE_X86_64/kernel.unstripped scripts/tahoe-kc-install.sh
# (kernel.unstripped has the symbols and is what tahoe-kc-install.sh takes; kernel is the stripped one)
cd "$(dirname "$0")/.."
XCROSS_MARCH=${XCROSS_MARCH:-k8-sse3} XNU_OBJ=xnu-tahoe-k8 KERNEL_BUILD_OBJROOT=xnu_development-xcross-k8/DEVELOPMENT_X86_64 \
  exec scripts/xnu-make.sh -Otarget -k -j"$(nproc)" "$@"
