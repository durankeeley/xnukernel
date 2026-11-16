# Installing And Booting On Real Hardware (Tahoe)

**Nobody has done this yet.** Every boot of the K8 and K10 kernels so far was a QEMU/KVM guest on an Intel host. This page is the plan, built from what the VM tests showed. Each step that is a guess and not something I ran is marked **unverified**. If you try it, the serial or photographed boot log is the most useful thing you can send back.

## Two Routes

The Apple installer and Recovery boot Apple's stock kernel, and that kernel does not run on a K8 or K10. So either the installer has to run on our kernel, or the install has to happen somewhere else.

| Route | How | State |
|---|---|---|
| A: install in a VM, copy the disk | Install Tahoe and our kernel into a QEMU guest on an Intel CPU model, write the disk to the real disk, boot it with OpenCore | The VM half is what every test in this repo did. Writing the disk to a real machine has not been done. |
| B: bootable USB with our kernel | Make an installer USB whose Recovery boots our kernel, run the installer on the AMD machine, install our kernel into the new system | Not implemented for Tahoe, see below |

Use Route A. The steps on this page are Route A.

### Why Route B Is Not Done For Tahoe

Tahoe does not boot a kernel file. It boots a boot kernel collection and a system kernel collection, which hold the kernel and every kext. For the installer USB to boot on a K8 or K10, its Recovery volume would need collections rebuilt around our kernel, the same thing `scripts/tahoe-kc-install.sh` does for an installed volume. That script runs `kmutil` inside Recovery against the installed system and was only written for that case. Doing it for the installer media is the missing piece, and the Recovery image is a read-only disk image that has to be unpacked, changed and repacked on macOS.

Even with that done, the installer restarts into the target disk and boots the collections stored there, so every stage would need our collections copied across before it restarts. The `docs/BARE-METAL.md` on the `highsierra` branch outlines the same flow for the simpler kernel-file case.

## Hardware

| Needs | Notes |
|---|---|
| 64-bit AMD CPU with CMPXCHG16B | K10 (Phenom, Phenom II, Athlon II, AM2+ and AM3) has it. K8 only has it from revision E on (check for `cx16` in `/proc/cpuinfo`). Earlier K8 parts have no CMPXCHG16B and that is not handled. |
| UEFI firmware | OpenCore needs UEFI. Many older boards are BIOS only. OpenDuet (legacy boot) is the way around that, see [OPENCORE.md](OPENCORE.md). **Unverified** on these boards. |
| 8 GB RAM or more | 8 GB is what the VM tests used |
| SATA disk in AHCI mode | 64 GB or more, it holds the whole install |
| USB keyboard | Login needs one. **Unverified:** whether Tahoe has a driver for your USB controller (old OHCI and EHCI ports may not work) |
| A GPU you can run on the UEFI framebuffer | There is no acceleration (see below) |

## Step 1: Build The Kernel

Use the CPU class you have. See [README.md](../README.md) for the one-time `scripts/bootstrap.sh`.

```sh
scripts/tahoe-build-k10.sh     # Phenom, Phenom II, Athlon II (-march=amdfam10)
scripts/tahoe-build-k8.sh      # Athlon 64, Opteron (-march=k8-sse3)
```

The CPU class matters. A kernel built for the default target contains SSE4.1 in early boot code and faults on a K8 or K10 before any trap handler exists. Use the K8 build if you are unsure, it runs on both.

## Step 2: Install macOS And The Kernel In A VM

Create the guest as described in the README (OSX-KVM, recovery installer, Tahoe 26.7.3). Install the kernel into it:

```sh
scripts/tahoe-extract-kdk.sh ~/Downloads/KDK_26.7.3_25G342.kdk.dmg      # once
KSRC=build/xnu-tahoe-k10/obj/RELEASE_X86_64/kernel.unstripped scripts/tahoe-kc-install.sh
```

This boots the guest into Recovery and rebuilds the boot and system kernel collections around our kernel with `kmutil`. It takes 25 to 35 minutes. At the end it makes a fresh bless snapshot. That matters: without it the guest can root to an older snapshot and panic with "System KC UUID ... linked against X but Y is loaded".

Boot it once in the VM with the matching CPU model before you move it:

```sh
scripts/tahoe-cpu-test.sh k10      # or k8: boots, logs in as xnu, saves screenshots
```

If the guest does not reach the login window here it will not on real hardware either.

The guest must be installed and updated before this step. Do not let it update afterwards, a macOS update replaces the kernel collections and overwrites our kernel. Automatic updates are already off in the test guest.

## Step 3: Write The Disk

Shut the VM down. Then write the disk image to the real disk. **This destroys everything on the target.** Check the device name with `lsblk` twice.

```sh
sudo qemu-img convert -p -f qcow2 -O raw ../OSX-KVM/mac_tahoe.qcow2 /dev/sdX
```

Or attach the real disk to QEMU as the guest disk (`DISK=/dev/sdX scripts/boot-tahoe.sh`, and change `format=qcow2` to `format=raw` on the `MacHDD` drive line in that script) and install straight onto it. The guest then boots from the real disk in the VM first, which is a good check.

## Step 4: Put OpenCore On A USB Stick

In the VM, OpenCore lives on its own disk image (`OSX-KVM/OpenCore/OpenCore.qcow2`) and the guest disk has no OpenCore on it. On real hardware, copy that EFI folder to a FAT32 USB stick with a GPT partition table:

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
| `NVRAM` `boot-args` | `-v keepsyms=1 debug=0x100 serial=3 auxKCAtBoot=false` | The values used in every test. `-v` shows the boot log on screen. `auxKCAtBoot=false` because our collections are installed without a suffix. Drop `serial=3` if you have no serial port. |
| `NVRAM` `csr-active-config` | `7F080000` (little endian for `0x87f`) | SIP has to be mostly off: the collections we build are not signed by Apple |
| `Misc > Security > SecureBootModel` | `Disabled` | Same reason |
| `Kernel > Emulate` | leave empty | Our kernel handles AMD CPUs itself. Do not add the AMD Vanilla patches or CPUID spoofing, they would fight the kernel changes. |
| `PlatformInfo` | **Unverified.** Generate your own serial and UUID (GenSMBIOS), pick a model Tahoe supports (for example iMac20,1 or iMacPro1,1) | The VM uses iMac19,1 and needs the CPUID hypervisor bit set for boot.efi to accept it. On real hardware there is no hypervisor bit, so a different model may be needed. |
| `Booter > Quirks` | **Unverified.** Depends on the board's firmware, start from the OpenCore Install Guide quirks for your chipset | |

Do not use `slide=100`. That was a workaround for the OVMF memory layout in the VM.

## Step 5: Firmware Settings And First Boot

- UEFI mode, CSM off, SATA in AHCI mode
- Boot from the USB stick and pick the Tahoe disk in OpenCanopy
- Boot with `-v` and watch the screen. The first lines come from boot.efi, then the kernel prints its own log

What a good start looks like: the kernel banner reads `Darwin Kernel Version 25.5.0 ... xnu_development-xcross-k10/DEVELOPMENT_X86_64`, the OPEMU lines show up (`OPEMU: ssse3 mode=auto cpu=lacks -> EMULATE`), launchd starts, and the login window appears. It will be slow. See "What A Missing AVX Means" in the README.

## If It Does Not Boot

- A hang before the kernel prints anything (the log stops at an `[EB|...]` or `OCABC` line) is the firmware and OpenCore stage. The table in [OPENCORE.md](OPENCORE.md) lists what Dortania suggests for each stop.
- A hang right after `HANDOFF TO XNU` with nothing printed is a kernel panic before the console is up. In the VM this was `Unsupported CPU` when the CPU vendor string was not AuthenticAMD. On real hardware that can only be a CPUID problem, so try the other kernel (K8 build) first.
- Lines starting `OPEMU-AVX: unsupported` mean the emulator met a VEX instruction it does not implement. Note the `bytes=` value and open an issue with it. Unsupported forms still missing: the string-compare family `vpcmpXstrX`, gathers, legacy-encoded AES-NI and PCLMUL, AVX-512, TZCNT.
- A panic during early userland is almost always a missing emulated instruction. A photo of the panic screen with the backtrace is enough to start from.
- To get back to Apple's kernel, use `scripts/guest/kc-restore-stock.sh` in a VM Recovery session. The stock kernel does not boot on the AMD machine, so recovery always happens in the VM.

## Known Limits On Real Hardware

- No graphics acceleration. Metal does not exist for these GPUs on Tahoe, so the guest renders in software. In the VM the desktop needs 10 to 30 minutes after login to finish drawing.
- Sleep, power management and thermal control are not looked at. The kernel runs with dummy power management in the VM config.
- Audio, Wi-Fi, Bluetooth and Ethernet depend on kexts for your hardware and were not tested.
