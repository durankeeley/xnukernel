# xnu-cross

Cross-compile Apple's XNU kernel on Linux.

To run modern macOS on old AMD CPUs (K8 Athlon 64, K10 Phenom II). A patched kernel that reports an AMD CPU properly and emulates the instructions the CPU does not have. The emulator is a port of the OPEMU opcode emulator from the previous AMD kernels, extended to AVX, AVX2, BMI and FMA for macOS Tahoe.

**None of the AMD work has been tested on real AMD hardware.** Everything was tested in QEMU/KVM on an Intel host with a mimicked AMD CPU model.

## Branches

Each branch has two commits. The first is Apple's stock xnu, untouched. The second is our patches, the toolchain, the scripts and the docs. `git diff <branch>~1 <branch> -- src/xnu` shows every change made to the kernel.

| Branch | macOS | xnu | State |
|---|---|---|---|
| [`highsierra`](../../tree/highsierra) | 10.13.6 (17G66) | xnu-4570.71.2 | Stock and AMD-patched kernels boot to the login window. AMD K8, K10 and Bulldozer CPU models boot to the login window under KVM. |
| [`tahoe`](../../tree/tahoe) | 26.7.3 (25G342) | xnu-12377.121.6 | Intel kernel boots to the desktop. K10 and K8 kernels boot to the login window, and the K10 kernel to a logged in session. |

Check one out with:

```sh
git checkout highsierra     # or tahoe
```

then read `README.md` and `REQUIREMENTS.md` on that branch. To run one of the AMD kernels on a real machine, read `docs/BARE-METAL.md` on the same branch first.

## What is in a version branch

```
src/xnu/        stock xnu (commit 1), patched xnu (commit 2)
scripts/        build, install, boot and test scripts
toolchain/      clang wrappers, fake xcrun, iig stand-in, header stand-ins
patches/        the xnu changes as a patch file
docs/           test results with screenshots, notes
README.md  REQUIREMENTS.md  STATUS.md
```

Third party sources (Apple's open source MIG, libdispatch and libplatform, plus ld64 and tcsh) are not committed. `scripts/fetch-deps.sh` clones them at the pinned commits listed in `scripts/deps.txt`.

## Credits

The AMD patches build on work from the community:
- The First version of OPEMU by Sinetek December 31 2012 <philmb3487@proton.me>
- Major revisions and extended features by [ANV - Andy Vandijck](https://github.com/andyvand) and [BRONYA](https://www.insanelymac.com/forum/profile/31928-bronya/)
- The AMD High Sierra XNU tree by AnV (OPEMU opcode emulator, AMD CPUID, TSC and LAPIC code), the base of the `highsierra` AMD kernel
- AMD Moderators of InsanelyMac: [theconnactic](https://www.insanelymac.com/forum/profile/998446-theconnactic/), [Shaneee](https://github.com/Shaneee), [spakk](https://www.insanelymac.com/forum/profile/159782-spakk/)
- [tpoechtrager/cctools-port](https://github.com/tpoechtrager/cctools-port) for Apple's ld64 on Linux
- [kholia/OSX-KVM](https://github.com/kholia/OSX-KVM) and OpenCore for the test VM
- dortania's [KdkSupportPkg](https://github.com/dortania/KdkSupportPkg) for the Tahoe Kernel Debug Kit


The Apple open source releases used: xnu, bootstrap_cmds (MIG), libdispatch, libplatform, AvailabilityVersions, CarbonHeaders and dtrace from [apple-oss-distributions](https://github.com/apple-oss-distributions). xnu is Apple's code under the APSL. The patches here are for research and testing.

## License

The kernel in `src/xnu` on both version branches is Apple's xnu, under the Apple Public Source License 2.0. The license text is `src/xnu/APPLE_LICENSE` on each branch. The patches in the second commit of each branch modify that source, so they are covered by the same license. The scripts, toolchain wrappers and docs in the second commit have no license file of their own yet. Third-party sources that `scripts/fetch-deps.sh` downloads (MIG, libdispatch, cctools-port, tcsh and so on) keep their own licenses.

