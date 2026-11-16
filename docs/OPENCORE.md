# OpenCore And Boot Loaders

How to get from "the kernel boots in a VM" to "the machine starts macOS". This page covers OpenCore on UEFI machines and the options for machines without UEFI. It applies to both macOS versions in this repo. The kernel-specific parts are in `BARE-METAL.md`.

**Nothing on this page has been run on a K8 or K10 machine.** The VM tests used OpenCore from [OSX-KVM](https://github.com/kholia/OSX-KVM), so the settings marked "VM" below are known to work in QEMU and nowhere else. Everything marked "Dortania" is advice from the [OpenCore Install Guide](https://dortania.github.io/OpenCore-Install-Guide/), which does not cover K8 or K10 CPUs. Read their guide next to this page.

## What OpenCore Does Here

OpenCore is the boot loader. It starts from UEFI (or from a legacy shim, see below), finds the macOS volume, sets NVRAM values such as `boot-args`, and hands over to Apple's `boot.efi`, which loads the kernel. It does not replace the kernel. That is why our patched kernel boots through the same OpenCore setup as a stock one.

One thing OpenCore setups for AMD usually do, and ours must not: the community AMD kernel patches (AMD_Vanilla, applied with `Kernel > Patch`) rewrite Apple's kernel in memory so it accepts an AMD CPU. Our kernel already handles AMD in its own source, so leave those patches out. Also leave `Kernel > Emulate` empty (no CPUID spoofing).

## The EFI Folder

Take the OpenCore release from [acidanthera/OpenCorePkg](https://github.com/acidanthera/OpenCorePkg/releases) and copy its `X64/EFI` folder to the root of a FAT32 partition (Dortania: IA32 only for 32-bit firmware). The layout:

```
EFI/
  BOOT/BOOTx64.efi
  OC/
    OpenCore.efi
    config.plist
    Drivers/    OpenRuntime.efi, OpenHfsPlus.efi (High Sierra volumes are HFS+), OpenCanopy.efi (the picker), ResetNvramEntry.efi
    Kexts/      Lilu.kext, VirtualSMC.kext, plus what your hardware needs
    ACPI/       SSDTs for your board
    Tools/      OpenShell.efi (optional)
```

What the test VM uses (copied from OSX-KVM, VM): drivers `OpenRuntime`, `OpenHfsPlus`, `OpenCanopy`, `ResetNvramEntry`, `ToggleSipEntry`; kexts `Lilu`, `VirtualSMC`, `WhateverGreen`, `VoodooPS2Controller`, `AppleMCEReporterDisabler`, `USBToolBox` with `UTBMap`. For a real board you keep `Lilu` and `VirtualSMC` (macOS will not boot without an SMC) and replace the rest with what your hardware needs. `OpenRuntime.efi` is required. Do not use Clover's firmware drivers (AptioMemoryFix, EmuVariableUEFI), Dortania says they do not mix with OpenCore.

## config.plist: What The VM Uses

| Setting | VM value | Notes |
|---|---|---|
| `NVRAM > 7C436110-AB2A-4BBB-A880-FE41995C9F82 > boot-args` | Tahoe: `-v keepsyms=1 debug=0x100 serial=3 auxKCAtBoot=false`. High Sierra: `-v keepsyms=1 kcsuffix=xcross` (the test script adds `debug=0x8 serial=3 slide=100`) | Set with `scripts/oc-set.sh` in the VM |
| `csr-active-config` | Tahoe `7F080000`, High Sierra `7F000000` | SIP mostly off, our collections and kernel are unsigned |
| `Misc > Security > SecureBootModel` | `Disabled` | |
| `Misc > Boot > PickerMode` | `External` (OpenCanopy) | |
| `PlatformInfo` | iMac19,1 in the VM | Needs your own serial and UUID on real hardware, and a model boot.efi accepts without the VM flag, see below |
| `Kernel > Quirks` | `ProvideCurrentCpuInfo` on, `DisableLinkeditJettison` on, `ForceSecureBootScheme` on, the rest off | |
| `Kernel > Emulate` | `DummyPowerManagement` on, CPUID fields empty | |
| `Booter > Quirks` | `AvoidRuntimeDefrag` and `EnableWriteUnprotector` on, the rest off | Firmware specific, see the next section |

The VM's boot.efi only accepts the iMac19,1 board-id because the CPUID hypervisor bit is set. A real machine has no such bit, so a different `SystemProductName` may be needed. Dortania's [SMBIOS page](https://dortania.github.io/OpenCore-Install-Guide/extras/smbios-support.html) lists which models run which macOS version.

## Boot Hangs: What Dortania Says To Try

These are from Dortania's [kernel issues page](https://dortania.github.io/OpenCore-Install-Guide/troubleshooting/extended/kernel-issues.html). They are written for Intel and newer AMD boards. Treat them as the first things to try on a K8 or K10 board, not as known fixes.

| Where it stops | Try |
|---|---|
| `[EB\|#LOG:EXITBS:START]` | Booter quirks, in this order of suspicion: `EnableWriteUnprotector`, `RebuildAppleMemoryMap`, `SyncRuntimePermissions` (firmware with MAT support, 2018 and later: write unprotector off, rebuild and sync on. Older firmware: the reverse), `DevirtualiseMmio`, `SetupVirtualMap`. Also `ProvideConsoleGop` under `UEFI`. Check the OpenCore log for `OCABC: MAT support is 1` |
| `OCABC: Memory pool allocation failure`, `Buffer Too Small` | `Above4GDecoding` on in the BIOS, CSM off |
| `Couldn't allocate runtime area` | The KASLR slide problem, see Dortania's [KASLR page](https://dortania.github.io/OpenCore-Install-Guide/extras/kaslr-fix.html) |
| Stuck at `RTC`, `PCI Configuration Begin`, `Previous Shutdown` | Update the BIOS. On AMD, boards with broken PCI allocation sometimes need `npci=0x2000` or `npci=0x3000` (one, not both). CSM off |
| Stuck at `AppleIntelCPUPowerManagement` | `DummyPowerManagement` on (the VM already has it). On Penryn-class boards, force HPET with an ACPI patch |
| `Waiting for Root Device` or a prohibited sign | A USB or SATA problem. `ReleaseUsbOwnership` under `UEFI > Quirks`, SATA in AHCI mode |
| `X64 Exception` on AMD | CSM on in the BIOS, or the `ProvideCurrentCpuInfo` quirk. Dortania's fix was an older AMD_Vanilla patch set, which does not apply here, so try turning `ProvideCurrentCpuInfo` off |

`AppleCpuPmCfgLock` and `AppleXcpmCfgLock` are Intel only and stay off on AMD.

For a K8 or K10 board the likeliest trouble spots are firmware and boot.efi (old BIOS memory maps, no MAT), then our kernel. A hang before the kernel prints anything is firmware or the booter. A hang after the kernel banner is ours, and `BARE-METAL.md` says what to collect.

## Machines Without UEFI

Many older boards (socket 939, AM2 and some AM2+ and AM3 boards) have only a legacy BIOS. OpenCore itself is a UEFI application, so it needs something to provide UEFI.

### Option 1: OpenDuet (DuetPkg), from OpenCorePkg

Dortania documents this as the legacy install path (see "Legacy Install Setup" on their [Windows install page](https://dortania.github.io/OpenCore-Install-Guide/installer-guide/windows-install.html)). OpenCorePkg ships a legacy boot sector set under `Utilities/LegacyBoot/`:

| File | What it is |
|---|---|
| `boot0` | MBR boot code, written to the first sector of the disk |
| `boot1f32` | Volume boot record (PBR) code for a FAT32 partition |
| `bootx64` (or `bootia32`) | The DUET firmware, a UEFI environment that runs on a BIOS machine. Copy it to the root of the FAT32 partition and rename it to `boot` |

With it in place the BIOS loads `boot0`, which loads `boot1f32`, which loads `boot`, which gives OpenCore a UEFI environment. The `EFI/OC` folder sits next to it as usual. Two things from the Dortania notes:
- `OpenPartitionDxe` is built into OpenDuet, do not add it as a driver. `OpenUsbKbDxe` and `UsbMouseDxe` are for DuetPkg systems, so add `OpenUsbKbDxe.efi` if the keyboard does not work in the picker.
- Dortania prepares the disk as GPT with one FAT32 partition and warns: "MBR disks will fail to boot" with this method. That sits oddly next to `boot0` being an MBR loader, and I have not resolved it. Follow their order of steps on their page.

Dortania writes `boot0` and `boot1f32` with BOOTICE on Windows ("Process MBR", "Process PBR"). **On Linux I have not done it.** What the tool does is replace the boot code and keep the disk's own partition table and filesystem parameters. The equivalent by hand, **unverified and able to destroy the disk if the device is wrong**:

```sh
# MBR: only the 440 bytes of boot code, the partition table after it stays
sudo dd if=boot0 of=/dev/sdX bs=440 count=1 conv=notrunc
# PBR of the FAT32 partition: new boot code, but keep the BPB (bytes 3 to 89) that mkfs wrote
sudo dd if=/dev/sdX1 of=/tmp/pbr.old bs=512 count=1
cp boot1f32 /tmp/pbr.new
dd if=/tmp/pbr.old of=/tmp/pbr.new bs=1 skip=3 seek=3 count=87 conv=notrunc
sudo dd if=/tmp/pbr.new of=/dev/sdX1 bs=512 count=1
```

Then mount the partition, copy `boot` and the `EFI` folder to its root. Test it first on a USB stick in the target machine, not on the system disk.

### Option 2: Clover in legacy mode

[Clover](https://github.com/CloverHackyColor/CloverBootloader) can boot from a BIOS machine using its own `boot0`/`boot1f32`/`boot` chain (CloverEFI). It is the loader older AMD Hackintoshes used for High Sierra. Its AMD kernel patching is the AMD_Vanilla style that must stay off for our kernel, and Dortania recommends OpenCore over Clover. Use it only if OpenDuet will not start on your board. I would only consider it for High Sierra, and I have not checked how well it handles Tahoe.

### Option 3: Chainload from another boot loader

If the machine can already boot Linux with GRUB, GRUB can chainload the partition that carries `boot1f32`:

```
menuentry "OpenCore (OpenDuet)" {
    insmod part_gpt
    insmod chain
    set root=(hd0,gpt1)       # the FAT32 partition with boot1f32 and boot
    chainloader +1
}
```

**Unverified.** It saves you from touching the disk's MBR if Linux already owns it, at the cost of depending on that Linux install.

### What Does Not Help

- Booting OpenCore from a USB stick on a BIOS machine needs the same legacy files on the stick. The BIOS has to support booting from USB.
- Putting only the `EFI` folder on the stick does nothing on a BIOS machine. Without UEFI or DUET nothing reads it.
- **Unverified:** whether OpenCore's own binaries run on a K8 or K10. They target x86-64 and I am not aware of anything beyond SSE2 in them, but nobody has tried.

## Making The Installer USB (Linux)

For Route B in `BARE-METAL.md` you need an installer on a stick. Dortania's [Linux page](https://dortania.github.io/OpenCore-Install-Guide/installer-guide/linux-install.html) covers it, and it needs UEFI for the stick to boot, which on a BIOS machine means the DUET setup above. In short:

1. Download a recovery image with `macrecovery.py` from OpenCorePkg (`Utilities/macrecovery`), using a board-id and serial for a model that supports the macOS version.
2. Partition the stick with `gdisk` as GPT. One method puts everything on one FAT32 partition (type `0700`) with the files `BaseSystem.dmg`, `BaseSystem.chunklist` and an empty `com.apple.recovery.boot` folder. The other uses a 200 MB FAT32 `OPENCORE` partition plus a second partition of type `af00` that gets the recovery image written with `dmg2img`.
3. Copy the `EFI` folder to the FAT32 partition.

The recovery image installs by downloading macOS from Apple, so the machine needs a working network driver in that environment. The offline alternative is `createinstallmedia` from a Mac or macOS VM.
