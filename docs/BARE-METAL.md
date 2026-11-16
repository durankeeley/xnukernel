# Installing And Booting On Real Hardware (High Sierra)

**Nobody has done this yet.** Every boot of the AMD kernel so far was a QEMU guest on an Intel host. This page is the plan, built from what the VM tests showed. Each step that is a guess and not something I ran is marked **unverified**. If you try it, a photo or serial log of the boot is the most useful thing you can send back.

The numbered steps below are Route A (install in a VM, copy the disk). Route B (bootable USB, installer on our kernel) is further down.

## Two Routes

macOS 10.13 does not install on a K8 or K10 on its own. The installer boots Apple's stock kernel, which assumes SSSE3 and SSE4. So the installer itself has to run on our kernel, or the install has to happen somewhere else.

| Route | How | What has been tried |
|---|---|---|
| A: install in a VM, copy the disk | Install macOS and our kernel into a QEMU guest on an Intel CPU model, write the disk to the real disk, boot it with OpenCore | The VM half is what every test in this repo did. Writing the disk to a real machine has not been done. |
| B: bootable USB with our kernel | Make an installer USB that boots our kernel, run the installer on the AMD machine, then install our kernel into the new system | Nothing here has been run. The steps are my plan, and the weak spots are marked. |

Route A is the safer one. Route B is the one you want if you do not have an Intel machine or a free disk to image. Both start with the same kernel build and the same OpenCore setup.

## Hardware

| Needs | Notes |
|---|---|
| 64-bit AMD CPU | K8 (Athlon 64, Opteron) or K10 (Phenom, Phenom II, Athlon II) |
| UEFI firmware | OpenCore needs UEFI. Many older boards are BIOS only. OpenDuet (legacy boot) is the way around that, see [OPENCORE.md](OPENCORE.md). **Unverified** on these boards. |
| 8 GB RAM | What the VM tests used |
| SATA disk in AHCI mode | 60 GB, the size of the test image |
| USB keyboard | Login needs one. High Sierra has OHCI and EHCI drivers, **unverified** on your board |
| A GPU that works on the UEFI framebuffer | Some Radeon HD cards have a native High Sierra driver. **Unverified** for yours. |

## Step 1: Build The Kernel

See [README.md](../README.md) for the one-time `scripts/bootstrap.sh`.

```sh
XCROSS_MARCH=k8-sse3 XNU_OBJ=xnu-k8 scripts/xnu-make.sh
```

Use `XCROSS_MARCH=k8-sse3` for both K8 and K10. A kernel built for the default target contains SSE4.1 in early boot code and faults on a K8 or K10 before any trap handler exists. The result is `build/xnu-k8/obj/RELEASE_X86_64/kernel.unstripped`.

## Step 2: Install macOS And The Kernel In A VM

Install macOS 10.13.6 into `../OSX-KVM/mac_hdd_ng.img` as described in the README. Then install the kernel as a second kernel and prelinked kernel:

```sh
BOOTARGS='-v serial=3 keepsyms=1 kcsuffix=xcross slide=100' scripts/iterate.sh build/xnu-k8/obj/RELEASE_X86_64/kernel.unstripped
```

`iterate.sh` boots the guest on Apple's kernel, runs `kextcache` to build `prelinkedkernel.xcross` for our kernel, copies `kernel.xcross` and `prelinkedkernel.xcross` into `/System/Library/Kernels` and `/System/Library/PrelinkedKernels`, then reboots with `kcsuffix=xcross`.

Boot the AMD CPU models in the VM before moving the disk:

```sh
scripts/amd-test.sh am2      # K8
scripts/amd-test.sh am3      # K10
```

If the guest does not reach the login window here, it will not on real hardware.

## Step 3: Write The Disk

Shut the VM down. Then write the image to the real disk. **This destroys everything on the target.** Check the device name with `lsblk` twice.

```sh
sudo qemu-img convert -p -f qcow2 -O raw ../OSX-KVM/mac_hdd_ng.img /dev/sdX
```

## Step 4: Put OpenCore On A USB Stick

In the VM, OpenCore lives on its own disk image (`OSX-KVM/OpenCore/OpenCore.qcow2`). On real hardware copy its EFI folder to a FAT32 USB stick with a GPT partition table:

```sh
sudo modprobe nbd max_part=8
sudo qemu-nbd -c /dev/nbd0 ../OSX-KVM/OpenCore/OpenCore.qcow2
sudo mount /dev/nbd0p1 /mnt/oc
sudo cp -r /mnt/oc/EFI /mnt/usb/          # /mnt/usb is your FAT32 stick
sudo umount /mnt/oc; sudo qemu-nbd -d /dev/nbd0
```

[OPENCORE.md](OPENCORE.md) covers OpenCore itself, what to try when it hangs, and how to boot a machine that has no UEFI. Then edit `EFI/OC/config.plist` for the real machine. This is the part I cannot test. What the VM tests do tell us:

| Setting | Value | Why |
|---|---|---|
| `NVRAM` `boot-args` | `-v keepsyms=1 kcsuffix=xcross` | `kcsuffix=xcross` selects our kernel and prelinked kernel. Add `debug=0x8 serial=3` if you have a serial port. |
| `NVRAM` `csr-active-config` | `7F000000` (`0x7f`) | SIP off. Our kernel is not signed by Apple |
| `Kernel > Emulate` | leave empty | Our kernel handles AMD CPUs itself. Do not add the AMD Vanilla patches or CPUID spoofing, they would fight the kernel changes. |
| `PlatformInfo` | **Unverified.** Generate your own serial and UUID (GenSMBIOS) and pick a model that 10.13.6 supports, for example iMac18,3 | The VM uses iMac19,1 and needs the CPUID hypervisor bit set, because boot.efi rejects that board-id on a machine that is not a VM. On real hardware there is no hypervisor bit. |
| `Booter > Quirks` | **Unverified.** Depends on the board's firmware, start from the OpenCore Install Guide quirks for your chipset | |

Do not use `slide=100`. That was a workaround for the OVMF memory layout in the VM.

## Step 5: Firmware Settings And First Boot

- UEFI mode, CSM off, SATA in AHCI mode
- Boot from the USB stick, pick the macOS disk in OpenCanopy
- Boot with `-v` and watch the screen

A good start: the banner reads `Darwin Kernel Version 17.7.0 ... xnu-4570.71.2.AMD`, the OPEMU lines show up (`OPEMU: ssse3 mode=auto cpu=lacks -> EMULATE`), and after a while the login window appears. The default policy is `auto`, so the emulator only turns on for instruction sets the CPU's own CPUID says it lacks. That is the case this whole project is for, and it is the part the VM could not test. Expect a slow boot. See "What A Missing SSSE3 And SSE4 Means" in the README.

## Route B: A Bootable USB That Runs The Installer On Our Kernel

**Unverified from start to finish.** It needs a Mac or a macOS VM for one step, because `kextcache` only runs on macOS.

### What You Need

- The kernel from Step 1, and a prelinked kernel built for it (`kextcache`, see below)
- A macOS 10.13.6 installer: `createinstallmedia` output on a USB stick, or `BaseSystem.dmg` from [gibMacOS](https://github.com/corpnewt/gibMacOS) or OSX-KVM's `fetch-macOS-v2.py`
- A second USB stick (or a second partition) with the OpenCore EFI folder from Step 4
- A VM or a Mac to prepare the installer stick

### 1. Build The Prelinked Kernel

The installer boots from a prelinked kernel, a single file that holds the kernel and every kext it needs. Ours has to be built from the installer's own kexts. `scripts/iterate.sh` does the same `kextcache` call against an installed system. For the installer, run it in the High Sierra VM with the installer stick attached (QEMU `-device usb-host,vendorid=...,productid=...`), pointing at the stick's kexts:

```sh
kextcache -z -a x86_64 -K /tmp/kernel.xcross -all-loaded -c /tmp/prelinkedkernel.xcross "/Volumes/Install macOS High Sierra/System/Library/Extensions"
```

(The path depends on how the installer volume is named, check with `ls /Volumes`.)

### 2. Put Our Kernel On The Installer Stick

Copy both files next to Apple's, with the suffix the kernel will look for:

```sh
cp /tmp/kernel.xcross "/Volumes/Install macOS High Sierra/System/Library/Kernels/kernel.xcross"
cp /tmp/prelinkedkernel.xcross "/Volumes/Install macOS High Sierra/System/Library/PrelinkedKernels/prelinkedkernel.xcross"
```

Do the same on a second copy of the files somewhere easy to reach from the installer's Terminal (for example the root of the stick), you will need them again in step 5.

### 3. Boot The Installer On The AMD Machine

Boot the OpenCore stick, pick the installer stick, with boot-args `-v keepsyms=1 kcsuffix=xcross` and `csr-active-config` `7F000000` as in Step 4. If you get as far as the language picker, the installer is running on our kernel and the emulator is covering SSSE3 and SSE4. This is the step most likely to need debugging, see the section at the end.

### 4. Partition And Install

Open Disk Utility from the installer's Utilities menu and erase the target disk as `Mac OS Extended (Journaled)` with a GUID partition map. Use HFS+, not APFS. High Sierra uses APFS only on SSDs by default and OpenCore's HFS+ driver is the safer match for a first try. Then run the installer.

### 5. The Part That Can Go Wrong: Our Kernel Is Not On The Target Yet

The installer restarts at least once. After the restart it boots from the target disk, using the `kernel` and `prelinkedkernel` that the installer put there. Those are Apple's, so with `kcsuffix=xcross` set the boot looks for `kernel.xcross` on the target, does not find it, and falls back to Apple's kernel. On a K8 or K10 that fails.

The way I would try to get past it:
1. When the installer has copied its files to the target (the volume now has a `System` folder or `macOS Install Data`) but before the restart, open Terminal from the Utilities menu and copy the two files in:
   ```sh
   cp /Volumes/<usb>/kernel.xcross "/Volumes/<target>/System/Library/Kernels/"
   cp /Volumes/<usb>/prelinkedkernel.xcross "/Volumes/<target>/System/Library/PrelinkedKernels/"
   ```
2. If the restart already happened and failed, boot the installer stick again (it works, it is on our kernel), open Terminal, and run the same two commands. Then reboot from the target disk and let the install continue.

I do not know whether the installer wipes extra files in `System/Library/Kernels` between stages. If it does, repeat step 2 each time it restarts, and keep the copy commands in a script on the stick.

### 6. First Boot Of The Installed System

Boot OpenCore with the same boot-args and pick the new volume. After setup finishes, the system has Apple's kernel as `kernel` and ours as `kernel.xcross`. Our kernel only loads while `kcsuffix=xcross` is in the boot-args.

For later kernel updates, boot the installed system and use the same `kextcache` command against `/System/Library/Extensions` and copy the results to `/System/Library/Kernels` and `/System/Library/PrelinkedKernels`. `scripts/iterate.sh` does exactly that in the VM.

## If It Does Not Boot

- A hang before the kernel prints anything (the log stops at an `[EB|...]` or `OCABC` line) is the firmware and OpenCore stage. The table in [OPENCORE.md](OPENCORE.md) lists what Dortania suggests for each stop.
- A black screen or reset before any kernel output is usually the firmware and boot.efi stage (SMBIOS model, booter quirks), not our kernel.
- A reset right after the kernel starts under KVM turned out to be the wrong CPU vendor string reaching the kernel. On real hardware the vendor is right, so look at the serial log instead.
- Boot with `opemu_log=1` to log every emulated instruction group, and with `opemu_ssse3=0` (or `opemu_sse41=0`, `opemu_sse42=0`) to see what happens when emulation is off. More boot-args are in [OPEMU.md](OPEMU.md).
- To go back to Apple's kernel, remove `kcsuffix=xcross` from the boot-args. Apple's `kernel` and `prelinkedkernel` are untouched. Note that Apple's kernel does not run on the AMD CPU, so this only helps for checking the disk from another machine.

## Known Limits On Real Hardware

- The emulator is checked against the host CPU (12 million cases, 0 mismatches) but never ran on a CPU that really lacks SSSE3 or SSE4.
- The guest has no dyld shared cache in the test install, so every process loads its libraries one by one. That is slower than a normal install.
- Sleep, power management, audio, Wi-Fi, Bluetooth and Ethernet were not tested.
