# Notes

## Why These Changes Were Needed

1. **Apple ld64 instead of ld64.lld.** lld silently ignores `-segaddr`, `-image_base` and `-seg_page_size`, so the kernel was
   linked at address 0 and could not boot. `ld64` (cctools-port) honours them. It also needs `-export_dynamic` so LTO keeps every
   global symbol external (kexts resolve kernel symbols by name).
2. **105 stub symbols.** The stock kexts import 3,112 kernel symbols; the open-source tree lacks 105 of them (Skywalk, xcpm,
   bpf_tap, mach_bridge, IOSK). They are stubbed so `kextcache` can link every kext.
3. **Early SSE.** Upstream clang emits SSE for struct copies in early boot code that Apple's clang does not (even with
   `-mno-implicit-float`). `osfmk/x86_64/start.s` sets `CR4.OSFXSR|OSXMMEXCPT` in the 64-bit mode switch so this is harmless.
4. **Compat patches:** `doconf` shebang, `makekdebugevents.py` ported to Python 3, `idt64.s` `movw`, `vm_object.c` struct copy,
   `if_ipsec.c` guard (a bug in Apple's own open-source release), stub files registered in `libkern/conf/files`,
   `-Wno-error` appended by the compiler wrappers.
5. **KASLR slide.** `boot.efi` cannot place the kernel at slide 0 in this VM, so tests use `slide=100` (deterministic, 0xc800000).
6. **boot.efi platform check.** It rejects the iMac19,1 board-id unless the CPUID hypervisor bit is set, so the VM keeps it set.

AMD tree only:

7. `tsc_init` handled AMD families only (no Intel case, so it divided by zero on an Intel CPU). Apple's original `tsc_init` is now
   used for non-AMD vendors (`tsc_init_intel`), and the AMD fallback path computes its bus conversion factors.

## AM2, AM3 And AM3+ Test Profiles

`scripts/amd-test.sh am2|am3|am3plus` boots the installed kernel on QEMU/KVM with an AMD CPU model (K8 family 0Fh `Opteron_G2`, K10 family 10h `phenom` model 4,
Bulldozer family 15h `Opteron_G4`). Notes:
- `vendor=AuthenticAMD` is mandatory under KVM (otherwise QEMU reports the host's `GenuineIntel`).
- The CPUID hypervisor bit stays on: boot.efi only accepts the iMac19,1 board-id when it believes it runs in a VM.
- KVM runs SSSE3/SSE4 natively, so OPEMU is not triggered there; use `scripts/tcg-amd-boot.sh` / `scripts/tcg-time-to-login.sh` (TCG raises real `#UD`) to exercise it,
  expecting it to be 20-50x slower.
- Build for an AMD baseline with `XCROSS_MARCH=k8-sse3` (see Quick start), otherwise the kernel itself contains SSE4.1.

## Kernel Banner

`kern.version` reads `Darwin Kernel Version 17.7.0: <date>; xnu-4570.71.2.AMD:obj/RELEASE_X86_64`. The `xnu-4570.71.2.AMD` part comes from
`src/xnu/config/SourceVersion` (read by `config/newvers.pl`, token `###KERNEL_SOURCE_VERSION###`), so it is one line to change.

## Debugging

| Tool | How |
|---|---|
| Kernel serial log | boot-args `-v debug=0x8 serial=3` (`debug=0x8` = DB_KPRT is what routes `kprintf` to COM1); log at `/tmp/hs-serial.log` and `build/iterate/serial.log` |
| Remote debugger | start the VM with `GDBPORT=1234 scripts/boot-highsierra.sh`, then `scripts/gdb-kernel.sh out/<kernel> 0xc800000` (slide 100). Symbols and source lines work; `b tsc_init`, `si`, `bt`, `x/10i $rip` |
| Exception trace | `scripts/trace.sh` (TCG, `-d int`), symbolized with `scripts/addr2sym.py` |
| Where is it stuck? | `scripts/sample-rip.sh` samples the guest RIP through the QEMU monitor |
| Other `debug=` bits | `0x1` halt at boot for a debugger, `0x4` NMI, `0x400` core dump on panic |
| KDP / lldb | Apple's network KDP needs Apple's lldb, which is not available on Linux |

## Known Issues

- The VM presents an Intel CPU, so the AMD-specific paths (MSR reads, OPEMU) are exercised only through the AM2/AM3 profiles in
  `scripts/amd-test.sh` and on real AMD hardware (untested).
- The guest has no dyld shared cache (`update_dyld_shared_cache` finds 0 OS dylibs because the manual install has no installer receipts), so every process
  loads its libraries individually: slower boots, most noticeable under emulation.
- `make` used to exit non-zero after the link; fixed 2026-11-17 (REQUIREMENTS.md, "What Used To Fail").
- QEMU/KVM cannot truly hide an instruction: with KVM, CPUID-hidden SSSE3/SSE4 still execute natively, so OPEMU is only
  triggered under TCG.
