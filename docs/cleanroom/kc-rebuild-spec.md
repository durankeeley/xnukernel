# Spec: rebuilding Tahoe's kernel collections around a custom kernel

Status: behavioral specification. Written first; the implementation (`scripts/guest/kc-build.sh`,
`scripts/tahoe-kc-install.sh`) is written from this document alone.

## Provenance and licensing

This spec records *observable behavior and ordering constraints* of a Tahoe collection rebuild. Sources:
- our own experiments in this repository (kmutil error messages, `kmutil inspect` output, serial logs of failed boots);
- the publicly documented *behavior* of the lythka/hao-patcher build (README/docs: which `kmutil` verbs run, in which order,
  what is placed where, which boot-arg avoids a boot stall). No source code from that project was copied or adapted;
  its original code is MIT-licensed and its XNU-derived parts are APSL-2.0. Nothing here depends on either.
- Apple's public `kmutil` help text.

Where the two sources agree, the behavior below is a requirement. Where only we observed it, it is marked (observed).

## Inputs
| name | meaning |
|---|---|
| `VOL` | the target System volume, mounted **read-write** (Recovery: unmount the auto-mounted read-only snapshot, mount `diskNs4` with `-o nobrowse`) |
| `PREBOOT` | the Preboot volume's `<volume-group-UUID>/boot/System/Library/KernelCollections` directory |
| `KERNEL` | the custom kernel Mach-O (x86_64, thin) |
| `SUFFIX` | the variant suffix selected at boot by the `kcsuffix=` boot-arg (here `development`) |
| donor kext binaries | executables for the Intel kexts. The System volume ships kext bundles **without** executables (observed: 705 bundles, 70 binaries) |

## Preconditions
P1. Every kext bundle that will be linked must contain its executable *in the volume's own `System/Library/Extensions`*.
    Supplying the donor tree as a second repository does not work: identifiers then exist twice and dependency
    resolution reports `kpi.*` / family kexts as "not found" (observed, nondeterministically which one).
P2. The merge must handle every bundle layout: `Contents/MacOS/<exe>`, flat bundles (executable at the bundle root,
    e.g. IOGraphicsFamily), nested `PlugIns`, and the pseudo-kexts `System.kext/PlugIns/*.kext/<Name>` (observed).
    Only Mach-O files that are missing on the volume are copied; nothing existing is overwritten.
P3. The kernel's exported-symbol set must cover everything the donor kexts import (observed; see
    `scripts/missing-exports.py`), and class vtables the kernel exports must not be larger than the ones the kexts were
    compiled against ("Malformed vtable", observed).
P4. Back up the stock `BootKernelExtensions.kc` / `SystemKernelExtensions.kc` first; `kmutil install` and `kmutil create`
    can overwrite the unsuffixed files.

## Behavior (ordered)
B0. **Scratch location (observed).** With `-R VOL`, the collection paths given to `-B`/`-S` are resolved *under* `VOL`
    (a path outside the volume fails with "No such file or directory"). Use a scratch directory inside `VOL`
    (e.g. `VOL/.kcscratch`, passed as `/.kcscratch/...`) and remove it afterwards. `-k` takes an ordinary absolute path.
B1. **Boot collection.** `kmutil create -n boot` with: `-a x86_64`, `-R VOL`, `-k KERNEL`, `--allow-missing-kdk`,
    a variant flag, `-B <scratch>/boot.kc`, `--no-variant-extension`. The kext set is the volume's OSBundleRequired set.
B2. **Member list.** `kmutil inspect -B boot.kc` lists the fileset members. The identifiers of all members except
    `com.apple.kernel` are the *elide list*.
B3. **System collection.** `kmutil create -n sys` with the same arguments as B1 plus `-S <scratch>/sys.kc`, **and one
    `--elide-identifier <id>` per elide-list entry**. Reason: a driver present in both collections makes the kernel discard
    one copy while other drivers still call into it (hao-patcher docs). B1 and B3 must use the same kernel and variant.
B4. **Install (observed: suffix-based selection is unreliable; use the unsuffixed names).** Install `boot.kc` as
    `VOL/System/Library/KernelCollections/BootKernelExtensions.kc` and `sys.kc` as `SystemKernelExtensions.kc` (each with the elide
    list as `<name>.elides`); copy the boot collection and its `.elides` to `PREBOOT`, and nothing else to Preboot. Reasons, all observed
    with the inventory in `scripts/guest/kc-inventory.sh`:
    - `boot.efi` loaded `BootKernelExtensions.kc.development` whenever that file existed, even with no `kcsuffix` boot-arg;
    - userspace loaded the *unsuffixed* `SystemKernelExtensions.kc` regardless of `kcsuffix` and of the kernel's version string
      (a `.../DEVELOPMENT_X86_64` kernel still got the unsuffixed one);
    so a `.development` pair next to a stock unsuffixed pair always ends in the UUID panic. Keep the stock pair only in a backup
    (P4) and move any `*.development*` collection files out of `VOL` and `PREBOOT`.
B5. **UUID consistency.** The system collection records the UUID of the boot collection it was linked against; the kernel
    panics at load time if it differs from the loaded boot collection (observed: `System KC UUID ... linked against X,
    but Y is loaded`). B1 and B3 must therefore come from the same run, and both installed together. Re-running B1
    requires re-running B3.
B5b. **Snapshot (after B6, so the snapshot contains the stock unsuffixed collections) (observed, then confirmed by the symptom).** With authenticated root disabled the system boots from a
    *snapshot* of the System volume, not the live volume. Anything written to the live volume (the system collection, merged
    kext binaries) is invisible until a new snapshot is created and blessed. After B4 run
    `bless --folder VOL/System/Library/CoreServices --bootefi --create-snapshot`. Symptom when skipped: the stock system
    collection keeps loading (same stock UUID in every `System KC UUID` panic regardless of what was installed).
B6. **Stock fallback.** Swapping is done by file replacement, not by boot-args: `scripts/guest/kc-restore-stock.sh` copies the stock pair
    back from the backup, moves `*.development*` files away and snapshots; `kc-build.sh` does the reverse. Each swap needs a Recovery trip.
B7. **Boot-args.** No `kcsuffix`. Add `auxKCAtBoot=false` so kernelmanagerd does not stall trying to build an
    auxiliary collection (hao-patcher docs). Remote `serial=3` for diagnostics.

## Postconditions (checks)
C1. `kmutil inspect -B` of the installed boot collection lists > 200 members including `com.apple.kec.corecrypto`.
C2. The installed system collection's member list is disjoint from the boot collection's.
C3. At boot, serial shows the custom kernel's `Darwin Kernel Version` string, and no `System KC UUID` panic.

## Non-goals
Disk-image assembly, OpenCore patching, userspace compatibility shims, and anything about macOS 27.
