#!/bin/bash
# tahoe-kc-install.sh [--boot] -- install the freshly built kernel into the Tahoe guest (spec: docs/cleanroom/kc-rebuild-spec.md).
# Stages the kernel and guest scripts on the agent server, runs scripts/guest/kc-build.sh in a Recovery Terminal, then (with --boot)
# boots Tahoe with kcsuffix=development and auxKCAtBoot=false. Guest log: build/agent/files/kc-build.out
cd "$(dirname "$0")/.."
F=build/agent/files
# KSRC=<kernel.unstripped> selects another build (e.g. the K10 kernel in build/xnu-tahoe-k10); the result is also kept as out/kernel.tahoe-unstripped.x86_64
KSRC=${KSRC:-build/xnu-tahoe/obj/RELEASE_X86_64/kernel.unstripped}
cp "$KSRC" out/kernel.tahoe-unstripped.x86_64
cp out/kernel.tahoe-unstripped.x86_64 $F/kernel.xcross2
cp scripts/guest/kc-build.sh $F/kc-build.sh
# the real kpi pseudo-kext executables (small) are fetched by the guest script from /tmp/skr; stage them
tar -C build/kdk-real/kdk/System/Library/Extensions -czf $F/syskreal.tgz System.kext
sed -i 's|^if \[ -d /tmp/skr/System.kext/PlugIns \]; then|mkdir -p /tmp/skr; [ -d /tmp/skr/System.kext ] \|\| curl -s http://10.0.2.2:8000/files/syskreal.tgz \| tar -xzf - -C /tmp/skr\nif [ -d /tmp/skr/System.kext/PlugIns ]; then|' $F/kc-build.sh
cp scripts/guest/install-diag-daemon.sh $F/install-diag-daemon.sh
echo 'curl -s http://10.0.2.2:8000/files/install-diag-daemon.sh | sh' >> $F/kc-build.sh   # also (re)install the diagnostics daemon in the same Recovery session
cp scripts/guest/kc-resnap.sh $F/kc-resnap.sh
echo 'curl -s http://10.0.2.2:8000/files/kc-resnap.sh | sh   # final fresh bootable snapshot' >> $F/kc-build.sh
rm -f $F/kc-build.out $F/kc-resnap.out $F/install-diag-daemon.out
scripts/tahoe-recovery-run.sh kc-build.sh
if [ "$1" = "--boot" ]; then scripts/tahoe-boot-test.sh auxKCAtBoot=false; fi
