# Status: macOS High Sierra (xnu-4570.71.2, guest 10.13.6 / 17G66)

Last updated 2026-11-16. How to build and run is in `README.md`, test results with screenshots are in `docs/TESTING.md`.

## Summary

**The AMD kernel boots macOS 10.13.6 to the login window on mimicked AM2, AM3 and AM3+ CPUs under QEMU/KVM** (`scripts/amd-test.sh am2|am3|am3plus`; screenshots
`docs/screenshots/highsierra-*-login.png`). The guest reports `AuthenticAMD`, family 15 / 16 / 21, kernel `xnu-4570.71.2.AMD`. AM3 takes about 3 minutes from selecting the volume.
KVM executes SSSE3/SSE4 natively, so this validates the AMD-specific kernel paths (family TSC setup, MSR handling, CPUID) at full speed. The opcode emulator itself is
exercised under TCG (below), which is far slower.

## Done

| Item | State |
|---|---|
| Toolchain (clang wrappers, fake xcrun, MIG, tcsh, home-made SDK) | done |
| Apple ld64 built for Linux (`toolchain/bin/ld64-apple`) | done; required (lld ignores the kernel layout flags) |
| Stock Apple xnu-4570.71.2 built | **boots macOS 10.13.6 to the login window** (`out/kernel.BOOTS.stock-source.x86_64`) |
| AMD-patched tree (`src/xnu`) built | **boots to the login window on an Intel CPU model** (`out/kernel.BOOTS.amd-patched.x86_64`) |
| Stock macOS 10.13.6 installed in the VM (offline, from gibMacOS files) | done; login `xnu` / `xnu` |
| Test loop (`iterate.sh`, `trace.sh`, `watch-boot.sh`, `amd-test.sh`, guest agent) | done |
| Remote debugging | gdb attaches through the QEMU stub and reads our kernel's symbols and source lines |
| Kernel serial log | works with boot-arg `debug=0x8` (DB_KPRT); `serial=` alone does not enable kprintf |

## Install on our kernel (single bootable disk)

Added 2026-11-17. `docs/INSTALL.md` is the guide, this is what was found.

- **The whole install runs on our kernel.** One disk (OpenCore on the EFI partition, the installer and our kernel on an HFS+ volume) boots Recovery and runs installer stage 1 and stage 2 (on the `Penryn` model with an AMD vendor string). The installed system then boots on `xnu-4570.71.2.AMD` to the login window and desktop on the `Opteron_G2` model. Screenshots are in `docs/TESTING.md`.
- The installer's `BaseSystem` has a stripped kext folder (no `AppleUSBCommon`), so the prelinked kernel is built from the 321 kexts in `Core.pkg` (`scripts/extract-extensions.sh`). All 321 give a 95 MB cache that boot.efi cannot read (`Read error 0x3`). `kextcache -all-loaded` gives 11 MB and loads.
- Stage 2 boots files Apple put on the target, not ours. It panics with `root image authentication failed (err = 22)` because open source xnu lacks Apple's production chunklist keys. `scripts/guest/hs-target.sh` copies our prelinked kernel there and switches `auth-root-dmg=` to `root-dmg=`.
- Apple's 10.13 installer certificates expired in 2019. Stage 1 and stage 2 both need a fake clock before that, stage 2 later than stage 1 (`HS_DATE`).
- Single-user mode (`-s`) is the way to put our kernel on the installed volume without the Recovery Terminal (`scripts/guest/hs-single.sh`). It has no network and `dscl` fails there, so the account is made in Setup Assistant.
- **Input in a VM guest GUI:** a session ignores the keyboard until it sees a pointer event. A Recovery that looked hung for half an hour only needed `scripts/vm.sh wake` (hot-plug and move a relative mouse).

### TSC calibration (osfmk/i386/tsc.c)

The AMD path of `tsc_init` only works if firmware supplies `CPUFrequency` (OpenCore does on a real machine, QEMU does not). Without it the code fell back to a 200 MHz bus times the multiplier read from the P-state MSRs, which read as zero in a VM. gdb showed `tscFreq = 800000000` on a host that runs about 2.1 GHz.

The fix measures the TSC against PIT channel 2 for 20 ms when no frequency is available (`tsc_calibrate_pit`). With it the K8 guest reports `hw.cpufrequency: 2110000000`. The same fix is in the Tahoe branch. It compiles there, and has not been booted.

On real hardware the P-state MSR path still runs first, so this changes nothing there. The LAPIC timer in QEMU also counts at 1 GHz while the code assumes the bus frequency, so VM timers fire early. That is not fixed.

### Stage 2 on the Opteron_G2 model: fixed

Stage 2 used to hang at `Initialized /dev/rdisk3 as a 5 MB case-insensitive HFS Plus volume` on `Opteron_G2` and pass on `Penryn,vendor=AuthenticAMD`. gdb (`docs/DEBUGGING.md`) showed the guest idle with `newfs_hfs` blocked in `IOService::waitQuiet`, IOKit never going quiet, and `kextd` not running. Reading the timer state then showed `tscFreq` at 800 MHz on a host that runs at about 2.1 GHz.

With the TSC calibration fix (above) a clean install on `Opteron_G2` ran: stage 1 on our kernel, stage 2 to the end (`docs/screenshots/highsierra-stage2-opteron-installing.png`), then the installed system to Setup Assistant and the desktop. What shows the fix is the cause and not something else: with the same CPU model and the same stage 2 clock (2018-12-21) the new kernel got as far as the installer GUI, where the old one stopped on the console. What does not: the install disk and the target were rebuilt in between, and I did not run the old kernel against the new disk. The fix removes the hang, but I have not shown how a wrong TSC keeps IOKit busy.

The TSC fix is the only change to the kernel for this. The other things found while getting there are about the install, not the kernel:

- Stage 2 needs the current date. At 2018-12-21 it stops with "The installer resources have expired".
- Recovery on the install disk takes no input once an install exists on the target, so the stage 2 patch has to run right after stage 1.
- One rebuild of the install disk produced a Recovery whose USB controller never enumerated (no keyboard or mouse). Another rebuild from the same inputs was fine. The `-all-loaded` set is not stable, and there is no check for it in `make-install-disk.sh`.
- Our kernel gets onto the new volume through single-user mode on a Penryn model (`scripts/guest/hs-single.sh`), then boots there on `Opteron_G2` (`docs/screenshots/highsierra-single-user-amd-k8.png`).
- `scripts/install-unattended.sh` is not finished.

## AMD tree: what changed (beyond the shared toolchain fixes)

- `osfmk/i386/tsc.c`: Apple's original `tsc_init` is used for non-AMD CPU vendors (`tsc_init_intel`); the AMD fallback path now computes
  its bus conversion factors (it divided by zero when firmware gives no CPU frequency).
- OPEMU (opcode emulator for CPUs lacking SSSE3/SSE4, i.e. AMD K8/K10) was audited with a host-side harness and substantially fixed:
  - `opemu_policy.c` (new): policy. Default `auto` = emulate a set only if the CPU's CPUID says it lacks it. Boot-args `opemu`, `opemu_sse3`,
    `opemu_ssse3`, `opemu_sse41`, `opemu_sse42` (0 never, 1 always, 2 auto) and `opemu_log=1`.
  - `sse3.c`: `addsub*` subtracted the wrong way round and `hadd*`/`hsub*` had the dst/src halves swapped; the FP routines also clobbered the
    user's live XMM registers (now saved/restored).
  - `ssse3.c`: `palignr` crashed on misaligned 128-bit loads and read out of bounds for immediates >= 16 (rewritten byte-wise).
  - `sse42.c`: rewritten from Intel's algorithm. The old `pcmp[ie]str[im]` gave wrong results for ~25-40% of inputs, set flags in the wrong
    RFLAGS bits, ignored EAX/EDX for the explicit-length forms and (for `*strm`) wrote the wrong register; `pcmpgtq` had its operands swapped.
  - `opemu_util.c` (new): effective addresses with SIB index/scale, RIP-relative (relative to the NEXT instruction), 32-bit addressing;
    GPR access by name with x86 write semantics; sized memory access (copyin/copyout). The old code supported only `[base+disp]` and used the
    wrong RIP base.
  - `sse41.c` (new): the whole SSE4.1 set (blends, DPPS/DPPD, INSERTPS, PINSR*/PEXTR*/EXTRACTPS, MPSADBW, PACKUSDW, PCMPEQQ, PHMINPOSUW,
    PMIN/PMAX, PMOVSX/ZX, PMULDQ/PMULLD, PTEST, ROUND*, MOVNTDQA) plus POPCNT and CRC32 (CRC32C). Works on a snapshot of all XMM registers.
  - Result: `scripts/opemu-hosttest.sh` compares 103 instruction forms against the host CPU's real instructions (random + edge cases, all
    immediates, MMX forms, page-edge memory operands, full decode -> emulate -> write-back pipeline): **12 million cases, 0 mismatches**.
    The pre-existing SSSE3 core (`pshufb`, `pabs*`, `psign*`, `phadd*`, `phsub*`, `pmaddubsw`, `pmulhrsw`) was already correct.

## AM2 / AM3 (AMD K8 / K10) emulation

Profiles in `scripts/amd-test.sh` (KVM) and `scripts/tcg-amd-boot.sh` (TCG): `am2` = QEMU Opteron_G2 (K8, family 0Fh), `am3` = phenom
model 4 (K10, family 10h), `am3plus` = Opteron_G4.

- boot.efi rejects the iMac19,1 board-id unless the CPUID hypervisor bit is set, so the profiles keep it on.
- The Penryn-targeted kernel contains SSE4.1 (`extractps`) in early boot code, so it faults on K8/K10 before any trap handler exists.
  Build for an AMD baseline: `XCROSS_MARCH=k8-sse3 XNU_OBJ=xnu-k8 scripts/xnu-make.sh` -> `out/kernel.amd-k8.x86_64`.
- **Under TCG with the K8 model the kernel boots and userland runs.** The kernel log shows `SSE3 has -> do not emulate; SSSE3/SSE4.1/SSE4.2
  lacks -> EMULATE`, and launchd and the rest of userland survive tens of millions of emulated `palignr`/`pshufb` plus `pinsrd`, `pmovsxbw`,
  `phaddd` (before the SSE4.1 module existed, launchd died on the first `pinsrd`).
- macOS 10.13's own `memmove` uses SSSE3 unconditionally, so a K8/K10 traps constantly. Under TCG that is ~8,000 traps/s, so the boot to the
  login window is very slow; on real hardware a trap costs microseconds. Not yet seen reaching the login window under TCG.
- **KVM resets were a test-profile bug, now fixed:** under KVM, QEMU reports the *host's* vendor string (`GenuineIntel`) for an AMD model unless `vendor=AuthenticAMD`
  is given, so the kernel saw "Intel, family 16" and ran its Intel `tsc_init` path, which divides by zero for family 16. Found with hardware breakpoints through the
  gdb stub (`scripts/kvm-trap-trace.sh`). KVM still executes CPUID-hidden SSSE3/SSE4 natively, so OPEMU can only be exercised under TCG.

## Speed work and the WindowServer finding (latest)

- Fast path, no-text decode, classification cache and CR3 sharing are in (see README "Speed"). AM3 under TCG: ~64,000 emulated instructions/s (was ~8,000).
- A TCG hot-block profile showed the guest was *not* trap-bound during the kext-loading phase: ~70% of kernel instructions were console/serial output,
  so benchmarks boot quiet.
- The login window did not appear on AM3 because `WindowServer` was crash-looping with SIGILL every ~6 s. Caught with gdb through QEMU's runtime
  `gdbserver` command: the instruction was `66 0F 3A 21 C1 10` (`INSERTPS xmm0,xmm1,0x10`). Cause: libudis86's table allowed only a memory source for
  `0F 3A 21`, so the register form decoded as invalid. Fixed in `libudis86/itab.c`; a decoder-coverage test (`ht_decode.c`) now checks every
  SSE4.1/4.2/POPCNT/CRC32 encoding, and the failure message prints the address and raw bytes so the next one needs no debugger.
- The kernel version banner is now `xnu-4570.71.2.AMD` (was a hardcoded name).

## TCG findings (the opcode emulator in anger)

- Speed: AM3 under TCG runs ~64,000 emulated instructions/s (was ~8,000 before the fast path). A hot-block profile showed that during the kext-loading phase ~70% of the
  kernel's instructions were console/serial output, so benchmarks boot quiet (`scripts/tcg-time-to-login.sh`).
- `WindowServer` crash-looped with SIGILL on `INSERTPS xmm0,xmm1,0x10` (libudis86's table only allowed a memory source). Fixed; a decode-coverage test now checks every
  SSE4.1/4.2/POPCNT/CRC32 encoding, and the failure message prints the address and raw bytes.
- A boot-time race (found with gdb on a hung guest): `commpage_populate_one` published `commPagePtr32/64` before the page was faulted in, so a timer interrupt
  (`thread_quantum_expire`) could fault on it in interrupt context and panic ("preemption_level 1 ... within interrupt context"). Fixed by touching the area first.
- With those fixed, `WindowServer`, `loginwindow` and `SecurityAgent` all run on the TCG AM3 model, but the login screen did not appear within ~40 minutes: `SecurityAgent` was
  CPU-bound, the load average was ~40 and about a third of CPU 0's samples were Local-APIC accesses (expensive device exits under TCG). Contributing factors: no dyld shared
  cache in this manual install (every process binds its libraries individually) and Spotlight/first-boot daemons (disabled in the test VM). This is emulation cost, not a hang.

## Known issues

- `make` used to exit non-zero after the link (`llvm-strip`, `lldbmacros`, `check_all_exports`). Fixed 2026-11-17, it exits 0 and produces a stripped `kernel`: see REQUIREMENTS.md.
  `kernel.unstripped` is the usable output.
- Kernel `slide=0` cannot be used in this VM (OVMF owns that memory); tests use `slide=100`.
- **Nothing here has run on real AMD hardware.** Every AMD result is a QEMU CPU model on an Intel host.

## Suggested next steps

1. Speed up the emulation hot path (fast decode for register-register `pshufb`/`palignr`) and let the TCG boot reach the login window.
2. Find why AMD CPU models reset under KVM (gdb stub with hardware breakpoints).
3. Test on real AM2/AM3 hardware (the only place the AMD-specific kernel paths are truly exercised).
4. x87 `fisttp` and MONITOR/MWAIT emulation are still untested by the harness.

