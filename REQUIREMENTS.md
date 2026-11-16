# Requirements

What you need to cross-compile the kernel on Linux. All of it was checked by building from a fresh clone: `scripts/bootstrap.sh`, then a kernel build, with nothing carried over from the original working directory.

Tested on CachyOS (Arch Linux), x86_64, Linux 7.3, 8 cores, 16 GB RAM. I did not try other distributions, so the package names for those are for you to map.

## Hardware

| | Needed |
|---|---|
| CPU | x86-64. The build itself was only done on x86-64. The host tests (`opemu-hosttest`, `opemu-avxtest`) and the VM tests need an x86-64 CPU. The AVX test needs AVX2, BMI2, FMA, F16C, AES and PCLMULQDQ. |
| RAM | 8 GB builds the kernel. The VM tests give the guest 8 GB on top of that, so 16 GB is comfortable. |
| Disk | About 1 GB for the sources and toolchain, 0.5 GB per kernel build tree. The VM tests need much more: a 50 to 60 GB guest disk image, and for Tahoe about 6 GB for the unpacked Kernel Debug Kit. |
| Time | The kernel builds in roughly 10 to 20 minutes on 8 cores. |
| Network | `scripts/fetch-deps.sh` clones three repositories from GitHub. |

## Packages

Arch package names. These are what the build used.

| Package | What it is for |
|---|---|
| `clang`, `llvm`, `lld` (23.1.4 tested) | The compiler (clang as a Darwin cross compiler), `llvm-config`, `libLTO.so` (the kernel is linked with LTO), `llvm-strip`, `llvm-nm`, `llvm-lipo`, `llvm-libtool-darwin`, `dsymutil`. Other versions may work. The Tahoe branch needs a recent clang. |
| `make`, `bison`, `flex`, `m4`, `perl`, `python` (3) | XNU's makefiles and the host tools it builds (`SETUP/*`) |
| `unifdef` | Header export (`make installhdrs`) |
| `git` | `scripts/fetch-deps.sh` |
| `libbsd` | Linked by `toolchain/bin/host-cc` for XNU's host tools |
| `libdispatch` (provides `libBlocksRuntime`) | Apple's ld64 links against it |
| `util-linux-libs` (`libuuid`) | Apple's ld64 |
| `ncurses` | tcsh |
| `clang++` with libstdc++ (comes with `gcc-libs`) | Building ld64 and tcsh |

For the VM tests and helper scripts:

| Package | What it is for |
|---|---|
| `qemu-full` (11.1.3 tested), `edk2-ovmf` | The test VM |
| `socat` | Talking to the QEMU monitor (`scripts/vm.sh`) |
| `python-pillow` | Screenshots (`scripts/vm-shot.sh`, `scripts/tahoe-cpu-test.sh`) |
| `gdb` | Core tool. Debugging every kernel goes through it (`scripts/gdb-attach.sh`, docs/DEBUGGING.md). Any recent gdb with Python. |
| `p7zip`, `cpio` | Tahoe only: unpacking the Kernel Debug Kit |
| [OSX-KVM](https://github.com/kholia/OSX-KVM) | OpenCore, OVMF vars and the VM launch layout. Check it out next to this repo (`../OSX-KVM`) or set `OSXKVM=`. |

`echo 1 | sudo tee /sys/module/kvm/parameters/ignore_msrs` before starting a guest. It resets on reboot.

## Setting Up

The builder is the same in every branch: `scripts/bootstrap.sh`, `scripts/xnu-make.sh`, `scripts/fetch-deps.sh`, `scripts/build-*.sh` and everything in `toolchain/bin`. Only two small files differ per macOS version: `scripts/deps.txt` (the pinned version of each dependency: MIG, libdispatch, libplatform, AvailabilityVersions, dtrace and so on) and `scripts/version.conf` (SDK version, kernel version string).

```sh
git clone -b <branch> <this repo> xnu-cross && cd xnu-cross
scripts/bootstrap.sh
```

`bootstrap.sh` does five things, each also usable on its own:

1. `scripts/fetch-deps.sh` clones the sources pinned in `scripts/deps.txt` into `src/`: Apple's open source `bootstrap_cmds` (MIG), `libdispatch` (the firehose logging code the kernel links), `libplatform` (os/internal headers) and `AvailabilityVersions` (`availability.pl`), plus `cctools-port` for ld64 and `tcsh`
2. `scripts/build-ld64.sh` builds Apple's ld64 into `toolchain/bin/ld64-apple`
3. `scripts/build-tcsh.sh` builds tcsh into `toolchain/tcsh`. XNU's `SETUP/config/doconf` is a csh script and Linux has no csh.
4. `scripts/build-mig.sh` builds MIG for the host
5. It builds the SDK: `TargetConditionals.h` from `toolchain/sdk-seed`, libplatform's `os/internal` headers, then `make installhdrs` for the headers XNU installs itself, then `scripts/build-firehose.sh` compiles `libfirehose_kernel.a` from libdispatch's `firehose_buffer.c`. 

Then build the kernel with the script named in the branch README.

## What Is Open Source And What Is Not

Apple's released sources are used wherever they exist: xnu itself (including the CoreEntitlements v1, CoreTrust, corecrypto and Availability headers in `EXTERNAL_HEADERS`), MIG, libdispatch, libplatform and Apple's ld64 through `cctools-port`. What is still a stand-in, because Apple has not released it:

| Stand-in | Where | Why |
|---|---|---|
| `TargetConditionals.h` | `toolchain/sdk-seed` | The open CarbonHeaders copy predates `TARGET_OS_OSX` and friends |
| `TrustCache/API.h`, `CoreEntitlements/V2/*.h`, `CodeSignature/Entitlements.h` | `toolchain/tahoe-stubs` (Tahoe branch) | Included by xnu-12377, no public release of libTrustCache or CoreEntitlements V2 |
| `iig` | `toolchain/bin/iig` | DriverKit's interface generator is closed source. This one builds the kernel, DriverKit dispatch does not work. |
| 105 kernel symbols the stock kexts import | `libkern/c++/xcross_missing_stubs.c` (High Sierra), `libkern/xcross_tahoe_stubs.c` and `xcross_iig_abi.c` (Tahoe) | Skywalk, xcpm, Thunderbolt and others are not in the open xnu release |

## Why Not Just Use lld

lld ignores `-segaddr`, `-image_base` and `-seg_page_size`, so a kernel linked with it ends up at address 0 and does not boot. Apple's ld64 honours them. That is why the toolchain builds ld64 from `cctools-port`. The pinned `cctools-port` predates LLVM 23, so `scripts/build-ld64.sh` applies `toolchain/patches/cctools-port-llvm23.patch` (new copies of the `llvm-c` headers) first. If your `libLTO.so` is not at `/usr/lib/libLTO.so` (Debian puts it under `/usr/lib/llvm-N/lib`), set `XCROSS_LTO=/path/to/libLTO.so`.

## What Used To Fail

`make` used to end with errors after the kernel was linked. They are fixed and the build exits 0:

- `llvm-strip` refuses the kernel Mach-O ("shared library is not yet supported"). `scripts/build-ld64.sh` now also builds Apple's `strip` from cctools-port and `toolchain/bin/xcrun` uses it.
- `tools/lldbmacros` runs a Python 2 syntax checker. `scripts/xnu-make.sh` passes `PYTHON=true` so the check is skipped (it only validates the optional lldb macros).
- `check_all_exports` runs `kextsymboltool -output /dev/null`, which fails with "Illegal seek" on Linux. `config/Makefile` now writes to a scratch file.
- On Tahoe the same check also failed with "duplicate export: _VNOP_GETXATTR" because our `Dsep` symbol set repeats symbols from `MACFramework` on purpose. `config/Makefile` leaves `Dsep` out of that check.
