# Status: macOS Tahoe (xnu-12377.121.6, guest 26.7.3 / 25G342)

Last updated 2026-11-16. How to build and run is in `README.md`, test results with screenshots are in `docs/TESTING.md`.

## Summary

| Kernel | Result |
|---|---|
| Intel | Boots in QEMU/KVM with OpenCore to Setup Assistant and on to the desktop. Apple's real Intel kexts, launchd, APFS, WindowServer and loginwindow run. |
| K10 (`-march=amdfam10`) | Boots to the login window on a Phenom model. Logging in as `xnu` loads a session (widgets draw, the Dock is slow because the guest renders in software). |
| K8 (`-march=k8-sse3`) | Boots to the login window on an Opteron_G2 model (family 0Fh, SSE3, CX16). |

**Not tested on real AMD hardware.** KVM on an Intel host runs AVX and the other Haswell instructions natively, so the opcode emulator is never triggered in these boots. Its instruction semantics are verified on the host against the real CPU (`scripts/opemu-avxtest.sh`), the trap path is not.

Not done: DriverKit, graphics acceleration (Metal), K8 without CMPXCHG16B, SMP behaviour on real K8/K10, a real hardware boot. Reims (a virtual GPU for QEMU) was looked at and postponed.

## TSC calibration (osfmk/i386/tsc.c), added 2026-11-17

The High Sierra branch found that the AMD path computes a wrong TSC frequency in a VM (800 MHz on a host running about 2.1 GHz, because QEMU gives no `CPUFrequency` and the P-state MSRs read zero). The same fix is here: when no frequency is available and the CPU is virtual, `tsc_calibrate_pit()` measures the TSC against PIT channel 2 for 20 ms. Real hardware still uses the P-state MSRs first.

On this branch it compiles (a fresh `build/xnu-tahoe-fresh` K8 build links) and has **not been booted**. The Tahoe K8 and K10 results above are from before the change.

## Intel kernel: boots to Setup Assistant, then the desktop

The cross-built RELEASE kernel boots under QEMU/KVM + OpenCore with Apple's real Intel kexts. `launchd` and the daemons run, APFS mounts the volume and the cryptexes,
WindowServer and loginwindow run, Setup Assistant completes and the desktop loads. No kernel panic. The black screen that blocked this earlier had three independent
causes (table below). Not done: DriverKit (dexts) and graphics acceleration.

### How it is built and installed
- Source `src/xnu` (xnu-12377.121.6) with `patches/xnu-tahoe-xcross.patch` (it is also the second commit of this branch); wrapper `scripts/xnu-make.sh -Otarget -k -j$(nproc)` (clang 23 + Apple ld64
  from cctools-port, `toolchain/sdk`, `DO_CTFCONVERT=0`, `RC_DARWIN_KERNEL_VERSION=25.6.0`). `make` now exits 0 (since 2026-11-17, see REQUIREMENTS.md); the
  outputs are `kernel` (stripped with Apple's strip) and `kernel.unstripped` in `build/xnu-tahoe/obj/RELEASE_X86_64/`.
- Guest: stock Tahoe 26.7.3 (25G342) installed in `OSX-KVM/mac_tahoe.qcow2`, OpenCore, `csr 0x87f`, boot-args `-v keepsyms=1 debug=0x100 serial=3 auxKCAtBoot=false` (no `kcsuffix`: the custom collections are installed unsuffixed, the stock pair is kept in `<Data>/kcbackup`).
- Intel Tahoe boots from a boot kernel collection (BKC) + system collection (SKC). Apple ships the kext executables only inside prelinked collections, so the collections
  are rebuilt around our kernel with `kmutil` using the Kernel Debug Kit for 25G342 (dortania/KdkSupportPkg release `25G342`, unpacked with `scripts/pbzx.py`).
- One command does the whole install: `scripts/tahoe-kc-install.sh` (kernel -> Recovery Terminal -> `scripts/guest/kc-build.sh` -> diag daemons -> `kc-resnap.sh`), then `scripts/tahoe-boot-test.sh`. It takes 25-35 min; the pipeline's own timeout can expire first, so wait for `build/agent/files/kc-resnap.out`. Spec and observed constraints:
  `docs/cleanroom/kc-rebuild-spec.md` (behavioral spec; implementation written from it, not copied from other projects).

### What had to be fixed to get here (all in the patch / toolchain)
| problem | cause | fix |
|---|---|---|
| triple fault right after `PMAP:` lines | clang 23 emits SSE in early boot (CR4.OSFXSR not yet set) | `start.s` sets CR4.OSFXSR/OSXMMEXCPT with PAE |
| `kalloc_init` divide error / NULL deref | `__builtin_xnu_type_summary` fallback = 0 makes every type data-only, so no signature views | guards in `kalloc.c` (`kalloc_type_apply_policy`, `kalloc_type_view_init_var`) |
| `Malformed vtable` (IOUserClient, OSAction) | iig stand-in declared DriverKit methods `virtual` | `toolchain/bin/iig`: wrappers non-virtual; no vtable slot for LOCALHOST/LOCALONLY |
| kexts fail to bind (290 symbols) | real iig ABI (`Method(..., OSDispatchMethod)`, `_Invoke`, `Dispatch`) and libtb (`_tb_*`) missing | `scripts/missing-exports.py` -> `scripts/iig-abi-symbols.txt` -> `scripts/gen-iig-abi.py` -> `libkern/xcross_iig_abi.c` (alias to existing impl, else `kIOReturnUnsupported`); tb names added to `config/Private.exports`; real class `OSAction_IOUserClient_KernelCompletion` in `iokit/Kernel/xcross_osaction.cpp` |
| write fault in `processor_init` | CPUs register after `machine_lockdown()` makes `__DATA_CONST` read-only (timing differs from stock) | `x86_64_protect_data_const()` leaves it writable |
| panic `rwlock_count ... still holds a rwlock` | count leak not yet explained | **stopgap**: warning/reset in `thread_terminate_self` and `assert_thread_return_to_user` |
| `kmutil` cannot link/resolve | volume kexts have no executables; flat bundles (IOGraphicsFamily) and pseudo-kexts (`com.apple.kpi.*`) | `kc-build.sh` merges every missing Mach-O from the KDK into the volume's kext bundles |
| `System KC UUID ... linked against X, but Y is loaded` | system collection name follows the kernel variant string (RELEASE -> unsuffixed) and the system boots a *snapshot* | install SKC unsuffixed + `bless --create-snapshot` |
| black screen, WindowServer stuck in `IOKitWaitQuiet` | `kmutil -R` bakes the Recovery mount path into every bundle path; kernelmanagerd cannot resolve kext resources, so `X86PlatformPlugin::start` (resource request) never finishes and IOKit never quiesces | `kc-build.sh`: mount at the canonical `/private/tmp/sysmnt`, then same-length perl rewrite of the prefix to `/./././.../`; check with `grep -ac`, not `strings` |
| loginwindow blocked on `usermanagerd` | `usermanagerd` hangs in `renamex_np`: Sandbox (26.7.3) takes an rwlock in `begin_rename_swap` and releases it in `end_rename_swap`; our older xnu has the end hook only as `mpo_reserved16` (slot 55) and never calls it | `mac_policy.h` names the slot, `mac_vfs.c` wrapper, `vfs_syscalls.c` calls it after `vn_rename`, `policy_check.c` table |
| boot panic `System KC UUID ... linked against X, but Y is loaded` | after a build the bless "Will root to" snapshot can be an older one with an older SKC | `kc-resnap.sh` makes a fresh snapshot at the end of every install; verify with `diskutil apfs listSnapshots` |

### Known deviations from stock (not hidden)
- `rwlock_count` checks are non-fatal (stopgap above); `__DATA_CONST` is never write-protected; kalloc_type treats every type as data-only (no signature zones).
- Stubs: `libfirehose_kernel.a` (now built from libdispatch's open source by `scripts/build-firehose.sh`; it used to be a hand-built archive), TrustCache and CoreEntitlements stand-in headers
  (real CoreEntitlements headers exist in `EXTERNAL_HEADERS`), `_tb_*` Thunderbolt functions (proprietary, unsupported stubs), iig is a reimplementation (no Apple source).
- DriverKit RPC does not work: every dispatch returns `kIOReturnUnsupported` (`AppleUserHIDDrivers.dext` is currently moved out of the volume by `scripts/guest/dext-off.sh`).
- MAC policy ops are positional and our xnu-12377 headers are older than the 26.7.3 kexts: `will_rename_swap` is really `begin_rename_swap`; other formerly-reserved slots may be in use by other kexts (not audited).

### Open problems
1. DriverKit: real iig-style RPC dispatch. Audit the other MAC policy slots used by the installed kexts (compare each kext's ops table with `mac_policy.h`, the Sandbox slot is the known one).
2. Find the source of the rwlock count leak and remove the stopgap. Replace the stub headers and firehose with the real open-source pieces.
3. Metal acceleration for the guest (Reims was looked at and postponed).
4. Each kernel change costs about 50 minutes (rebuild, Recovery kmutil, boot). Debug aid: `sysctl -w debug.xcross_dump=<pid>` prints blocked-thread kernel stacks on serial (offsets from `vm_kernel_stext`; symbolise with `llvm-nm` and `0xffffff8000200000`).

### Gotchas (also in the memory notes)
- Guest input stalls: `scripts/vm.sh hotkbd` hot-plugs a fresh USB keyboard; first keystroke after idle is lost. Hot-plugged keyboards can lose key-up events and repeat forever: `device_del` all `kb*` devices and add one fresh; avoid ctrl-u and `|` in typed commands.
- Start QEMU with `setsid` (a harness monitor that expires kills its process group); `tahoe-boot-test.sh` does.
- `pkill -f <pattern>` from a command line containing the pattern kills your own shell: use `scripts/stop-all.sh`.
- Do not edit a running bash script (bash reads it incrementally).
- With `kmutil -R <vol>`, `-B/-S` paths resolve under the volume; keep scratch inside it.
- Debug helpers: `scripts/df-analyze.sh` (stack of a double fault), `scripts/tcg-fault.sh` (TCG exception chain), `scripts/vtable-diff.py`, `scripts/splitinfo-check.py`.

## AMD K10 (Phenom II): kernel port and AVX/AVX2/BMI/FMA emulator

Goal: run macOS Tahoe (xnu-12377) on K10. Build: `scripts/tahoe-build-k10.sh` (`-march=amdfam10`, objects in `build/xnu-tahoe-k10`), install with
`KSRC=build/xnu-tahoe-k10/obj/RELEASE_X86_64/kernel.unstripped scripts/tahoe-kc-install.sh`, boot with
`CPU='phenom,family=16,model=4,stepping=2,vendor=AuthenticAMD,kvm=off' scripts/tahoe-boot-test.sh`. The known-good Intel kernel is `out/kernel.tahoe-intel-good.x86_64`.

**Key finding:** Tahoe ships only the Haswell (`x86_64h`) dyld shared cache, so the whole userland (and many kexts) is compiled for AVX2/BMI/FMA/MOVBE, not just
runtime-dispatched paths. A K10 kernel must report `x86_64h` and emulate those instructions.

Ported from the High Sierra AMD tree (small, hand-merged instead of the 2900-line cpuid.c rewrite):
- `cpuid.c/h`: `cpuid_vendor_is_amd()`; AMD is presented as Penryn-class; no Intel-only MSRs (BIOS_SIGN_ID, PLATFORM_ID); cores from CPUID 0x80000008; Intel CWAs off; subtype forced to x86_64h.
- `tsc.c`: AMD path (EFI frequencies, else K8 FIDVID / K10 P-state MSRs). `cpu_threads.c`: topology mismatch tolerated on AMD.
- `trap.c`: kernel/user `#UD` -> OPEMU; kernel `#GP` on RDMSR/WRMSR of unimplemented MSRs returns 0 / is ignored (AMD only).
- `osfmk/OPEMU` (SSE3/SSSE3/SSE4.1/4.2/POPCNT/CRC32 from the High Sierra tree) plus NEW `opemu_avx.c` (VEX decoder + pure-C engine: AVX/AVX2 integer + FP,
  BMI1/2, MOVBE, permutes/broadcasts, FMA with exact software fusing, masked moves), `opemu_avx_fp.c` (SSE-based FP lane helpers), `opemu_avx_glue.c` (kernel glue;
  upper YMM halves live in `thread->machine.opemu_ymmh[user|kernel]`).
- Host test: `scripts/opemu-avxtest.sh [iters] [v]` runs every VEX encoding natively and through the engine: 60,663 forms, 0 real mismatches
  (3 reported are artifacts of rsi being the test buffer pointer). `v` lists unimplemented forms (round, dpps, mpsadbw, pcmpXstr, AES-NI, PCLMUL, F16C, phminposuw, gathers).
- Not emulatable: TZCNT runs as BSF on K10 (differs only for a zero input); XSAVE/XGETBV users.

### K10 result
Boots on a Phenom model (`CPU='phenom,family=16,model=4,stepping=2,vendor=AuthenticAMD,kvm=off'`) to the login window. The guest reports `AMD Phenom(tm) 9550 Quad-Core Processor`,
`AuthenticAMD`, cpusubtype 8 (x86_64h). Logging in as `xnu` loads the session. Along the way:
- the first VEX instruction hit `launchd` and the AppleFSCompression kext (`lzvn_decode_buffer`), which needed the kernel-mode emulator
- `vstmxcsr` in `fsck` panicked the guest until MXCSR support went in (plus VROUND, DPPS, F16C, AES-NI, PCLMULQDQ, PHMINPOSUW, MPSADBW)
- a panic in `opemu_fast` came from reading user instruction bytes by direct dereference of a non-resident page. User-mode OPEMU now fetches them with `copyin`
- one unsupported VEX instruction is logged per boot (`vpxor` with a bad address, a real fault)

### K8 result
Built with `XCROSS_MARCH=k8-sse3` (`scripts/tahoe-build-k8.sh`). Boots to the login window on `Opteron_G2,vendor=AuthenticAMD,kvm=off` (family 0Fh, SSE3, CX16, no SSSE3, SSE4 or POPCNT).
The vendor has to be forced. Without it QEMU reports the host's `GenuineIntel`, and the kernel panics with "Unsupported CPU" in `cpuid_set_info` (the panic message spins on the serial port before the console is up, so the guest looks hung at the "HANDOFF TO XNU" line).
Not covered: K8 without CMPXCHG16B (before revision E) and SMP behaviour on real K8.
