# Scripts

| Script | Purpose |
|---|---|
| `xnu-make.sh` | run XNU's make with every cross override. Env: `XNU_SRC`, `XNU_OBJ`, `XCROSS_LD` (Apple ld64), `XCROSS_MARCH` (target CPU, e.g. `k8-sse3`) |
| `build-mig.sh`, `build-ld64.sh` | build MIG and Apple's ld64 for Linux |
| `gen-stubs.sh` + `stub-symbols.txt` | generate `libkern/c++/xcross_missing_stubs.c` (105 do-nothing kernel symbols that Apple's kexts import) |
| `kext-undef.sh` | list kernel symbols the stock kexts import that a kernel lacks |
| `boot-highsierra.sh` | boot the VM (env `CPU` overrides the CPU model) |
| `boot-debug.sh`, `trace.sh`, `trace-filter.py`, `addr2sym.py` | TCG boot with exception logging; symbolizes the first faults |
| `iterate.sh` | one full test cycle for a kernel (install, kextcache, boot, collect screenshot/status) |
| `watch-boot.sh`, `amd-test.sh`, `sample-rip.sh` | boot with frame capture; AM2/AM3 CPU profiles; sample the guest RIP to find hangs |
| `vm.sh` | QEMU monitor helper: `key`, `type`, `shot`, `waitpicker`, `pickmac` |
| `oc-set.sh` | edit NVRAM boot-args / `csr-active-config` in the OpenCore image |
| `agent-server.py`, `guest-agent.sh`, `install-agent.sh`, `guest-run.sh` | host/guest command channel |
| `gdb-kernel.sh`, `isa-scan.sh` | attach gdb to the VM; count post-K8 instructions in a kernel binary |
| `opemu-hosttest.sh` (+ `opemu-hosttest/`) | compiles the real OPEMU sources on the host and compares every emulated instruction with the CPU's own, plus the effective-address code and the decode->emulate->write-back pipeline |
| `boot-tcg.sh`, `tcg-amd-boot.sh` | boot under TCG (software CPU, raises real #UD) with an AMD CPU model and report the OPEMU log lines |
| `profile-boot.sh`, `profile-tcg.py`, `qemu-plugin/hotblocks.c` | profile where a TCG guest spends its instructions (per kernel function, kernel vs user), including a steady-state window between two snapshots |
| `tcg-time-to-login.sh` | the benchmark: seconds from selecting the volume to the login window on a mimicked AMD CPU under TCG (quiet boot) |
| `kvm-trap-trace.sh` | boot under KVM paused, attach gdb with hardware breakpoints on the kernel trap handlers and log every trap until the guest dies (finds triple-fault causes) |
| `xarpbzx.py` | extract a `.pkg` (xar) payload (pbzx) to a cpio stream |
| `gdb-attach.sh`, `gdb/xcross.py` | attach gdb to a guest booted with `GDBPORT=`, find the slide, and use `xcpus`, `xthreads`, `xps`, `xbt`, `xpanic` (docs/DEBUGGING.md) |
| `make-install-disk.sh`, `install-disk/<profile>.conf`, `guest/install-disk-build.sh` | build one bootable install disk (OpenCore, installer, our kernel) for a macOS version (docs/INSTALL.md) |
| `recovery-terminal.sh`, `extract-extensions.sh` | boot an installer and open its Terminal from the keyboard; unpack the full kext set from InstallESD |
| `guest/cpu-info.sh` | print kernel, processor and capabilities as the guest sees them |
| `guest/hs-target.sh`, `guest/hs-single.sh`, `guest/hs-account.sh` | make stage 2 use our kernel; install our kernel from single-user mode; create the test account from Recovery |
| `hs-setup-assistant.sh` | click through Setup Assistant and create the account `xnu` / `xnu` |
| `install-unattended.sh`, `guest/hs-target-watch.sh` | UNFINISHED: chain the whole install without a person |
| `hs-stage2-test.sh` | boot stage 2 of the install on a given QEMU CPU model and report whether it reaches the GUI |
| `install-gui.sh`, `vm-shot.sh`, `agent-run.sh` | scripted stage 1 (flaky, finished by hand); screenshot helper; run a command in the guest through the agent |
| `vm.sh hotkbd`, `vm.sh wake` | fresh USB keyboard; pointer event so a guest GUI session accepts keys |
