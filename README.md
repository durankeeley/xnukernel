# xnu-cross: macOS Tahoe

Cross-compile Apple's xnu-12377.121.6 (macOS 26 Tahoe, Intel x86_64) on Linux and boot it in QEMU/KVM, with patches to run it on AMD K8 and K10 CPUs.

This branch has two commits. The first is Apple's stock xnu from [apple-oss-distributions/xnu](https://github.com/apple-oss-distributions/xnu). The second is our changes to it plus the scripts, toolchain and docs. To see what we changed in the kernel:

```sh
git diff HEAD~1 HEAD -- src/xnu
```

The same diff is in `patches/xnu-tahoe-xcross.patch`.

## Status

| Kernel | CPU model in QEMU | Result |
|---|---|---|
| Intel (default target) | Skylake-Client | Boots to the desktop |
| K10 (`-march=amdfam10`) | Phenom, family 10h, `AuthenticAMD` | Boots to the login window, logged in as `xnu`, desktop session loads |
| K8 (`-march=k8-sse3`) | Opteron_G2, family 0Fh, `AuthenticAMD` | Boots to the login window |

Screenshots and the guest's own `sysctl` output are in [docs/TESTING.md](docs/TESTING.md). The full log of what was fixed and what is still open is in [STATUS.md](STATUS.md).

## Not Tested On Real AMD Hardware

Everything here ran in QEMU/KVM on an Intel host. QEMU can mimic an AMD CPU model (vendor string, family, feature flags), so the AMD code paths in the kernel are exercised: CPUID, TSC calibration, topology, MSR handling, the x86_64h subtype switch.

What it cannot do is take instructions away. KVM runs AVX, AVX2, BMI and FMA on the host CPU even when the guest's CPUID says they are missing. So the opcode emulator never fires in these boots. We tested the emulator another way: `scripts/opemu-avxtest.sh` runs every VEX encoding it supports natively and through the emulator on the host and compares the results. About 63,000 instruction forms match, zero real mismatches. That proves the instruction semantics. It does not prove the trap path (`#UD` on a CPU that really lacks the instruction) works on a real Phenom II.

Booting on a real K10 or K8 board is the next test and nobody has done it yet.

## What A Missing AVX Means

macOS Tahoe only ships the Haswell (`x86_64h`) dyld shared cache. The whole userland is built for AVX2, BMI and FMA, not just a few dispatched fast paths. K8 and K10 have none of that, so on real hardware:

- Every AVX, AVX2, BMI, FMA, F16C or MOVBE instruction raises `#UD` and the kernel emulates it in C. This is slow. A trap, a software decode and a software execute cost far more than one hardware instruction (nobody has measured it on real hardware). Expect a sluggish desktop, and anything heavy on vector code (image processing, compression, crypto) to crawl.
- K8 and K10 have no XSAVE, so the hardware never saves the upper halves of the YMM registers. The kernel keeps them in a per-thread shadow area (`opemu_ymmh` in `struct machine_thread`). Code that uses `XGETBV` or `XSAVE` directly will fail.
- `TZCNT` runs as `BSF` on these CPUs and cannot be trapped. The two differ only when the input is zero.
- AVX-512 is not emulated. Tahoe does not need it.
- The kernel reports cpu subtype `x86_64h` on purpose, otherwise dyld finds no shared cache and `launchd` dies. That means userland trusts the AVX2 code paths.
- No Metal acceleration. The guest renders in software, so the desktop takes 10 to 30 minutes to draw after login in the VM. Real hardware with a GPU macOS supports would not have that problem.

K8 also lacks SSSE3, SSE4.1, SSE4.2 and POPCNT. Those are emulated too (SSE3 to SSE4.2, POPCNT and CRC32 come from the High Sierra OPEMU, fixed against the host CPU). K8 parts older than revision E have no CMPXCHG16B and that is not handled at all.

## Prerequisites

See [REQUIREMENTS.md](REQUIREMENTS.md) for the packages and disk space. For the VM tests you also need:

- [OSX-KVM](https://github.com/kholia/OSX-KVM) checked out next to this repo (`../OSX-KVM`), with OpenCore and the OVMF files
- A macOS Tahoe 26.7.3 (25G342) guest at `../OSX-KVM/mac_tahoe.qcow2`, installed from the recovery installer (`INSTALL=1 scripts/boot-tahoe.sh`)
- The Kernel Debug Kit for 25G342 from [dortania/KdkSupportPkg](https://github.com/dortania/KdkSupportPkg/releases)
- `echo 1 | sudo tee /sys/module/kvm/parameters/ignore_msrs` (resets on host reboot)
- gdb, for [debugging](docs/DEBUGGING.md)

## Build

```sh
scripts/bootstrap.sh                                  # once: ld64, tcsh, MIG, SDK headers
scripts/xnu-make.sh -Otarget -k -j$(nproc)      # Intel kernel
scripts/tahoe-build-k10.sh                            # K10 kernel
scripts/tahoe-build-k8.sh                             # K8 kernel
```

`make` exits 0. It leaves two files in `build/<obj>/obj/RELEASE_X86_64/`: `kernel`, stripped with Apple's `strip` (built from cctools-port by `scripts/build-ld64.sh` into `toolchain/bin/strip-apple`), and `kernel.unstripped`, which has the symbols and is what gdb needs. `llvm-strip` cannot be used: it stops with "shared library is not yet supported" on the kernel Mach-O. Everything booted in this project so far was `kernel.unstripped`. The stripped `kernel` has been checked to be a valid Mach-O executable with its global symbols, and has not been booted.

## Install And Boot

Tahoe boots from kernel collections, not a plain kernel file. The collections have to be rebuilt around our kernel with `kmutil` inside the Recovery environment. One script does it, and it takes 25 to 35 minutes:

```sh
scripts/tahoe-extract-kdk.sh ~/Downloads/KDK_26.7.3_25G342.kdk.dmg      # once
KSRC=build/xnu-tahoe-k10/obj/RELEASE_X86_64/kernel.unstripped scripts/tahoe-kc-install.sh
CPU='phenom,family=16,model=4,stepping=2,vendor=AuthenticAMD,kvm=off' scripts/tahoe-boot-test.sh
```

The serial log is `/tmp/hs-serial.log` and the QEMU monitor is `/tmp/hs-monitor.sock`. How the collections are rebuilt is in [docs/cleanroom/kc-rebuild-spec.md](docs/cleanroom/kc-rebuild-spec.md).

`vendor=AuthenticAMD` is required. Without it QEMU reports the host's `GenuineIntel` and the kernel panics with "Unsupported CPU".

## Debugging

gdb attaches to any guest started with `GDBPORT=`; [docs/DEBUGGING.md](docs/DEBUGGING.md) has the commands, the pitfalls and what does not work yet on this branch.

## Releases

`.github/workflows/build.yml` builds the kernel in an Arch Linux container on GitHub Actions for each variant (see the file for the list) and keeps the result as a workflow artifact on every push to this branch. Pushing a tag named `tahoe-v*` (for example `tahoe-v0.1`) also creates a GitHub release with each variant as a `.tar.xz`: the stripped kernel, `kernel.unstripped`, `BUILD-INFO.txt` and `SHA256SUMS`. The workflow has not run on GitHub yet; its steps were run in an Arch container locally.

## Real Hardware

The plan for installing and booting on a real K8 or K10 machine is in [docs/BARE-METAL.md](docs/BARE-METAL.md), with OpenCore setup and booting machines without UEFI in [docs/OPENCORE.md](docs/OPENCORE.md). It is untested, and says which steps are guesses.

## Tests

```sh
scripts/tahoe-cpu-test.sh k10        # boots, logs in as xnu, saves screenshots and sysctl output into docs/
scripts/tahoe-cpu-test.sh k8
scripts/opemu-hosttest.sh 20000      # SSE3 to SSE4.2, POPCNT, CRC32 against the host CPU
scripts/opemu-avxtest.sh             # AVX, AVX2, BMI, FMA, F16C, AES, PCLMUL against the host CPU
```

The host tests need an x86-64 CPU with AVX2, BMI2, FMA, F16C, AES and PCLMULQDQ.

## Layout

```
src/xnu/          Apple xnu-12377.121.6, patched
scripts/          build, install, boot and test scripts (scripts/guest runs inside the guest)
toolchain/        clang wrappers, xcrun, iig stand-in, header stand-ins, build patches
patches/          xnu-tahoe-xcross.patch
docs/             DEBUGGING.md, TESTING.md, screenshots, kernel collection spec
```

The kernel patches are in `osfmk/OPEMU/` (the emulator), `osfmk/i386/` (cpuid, tsc, trap, cpu_threads) and a handful of files under `bsd/`, `security/` and `iokit/`. Every OPEMU source file starts with a header comment that lists its status and history.

## Known Issues

- Stand-ins remain where Apple has not released the source, listed in REQUIREMENTS.md. libfirehose, MIG, libplatform and ld64 are the real open source builds.
- DriverKit does not work. The `iig` tool is closed source, so `toolchain/bin/iig` is a stand-in that builds the kernel but every DriverKit dispatch returns `kIOReturnUnsupported`.
- Two checks are loosened. The rwlock count check in `thread_terminate_self` is a warning, and `__DATA_CONST` is never made read-only.
- The MAC policy ops table is positional and our xnu is older than the 26.7.3 kexts. We fixed the Sandbox `end_rename_swap` slot. Other kexts may use other slots and nobody has audited them.
- Each kernel change costs about 50 minutes: rebuild, the Recovery `kmutil` run, boot.

## Credits

See the `main` branch README.
