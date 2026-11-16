# Debugging the kernel with gdb

gdb is a core tool here, the same as the compiler. Every kernel we build and boot can be stopped and inspected through QEMU's gdb stub, with our symbols and source lines. It works under KVM and under TCG, and it is how the stage 2 hang described below was narrowed down.

Nothing here needs the guest to be healthy. A guest that is hung, spinning with interrupts off or sitting in a panic can still be read.

## Quick start

```sh
GDBPORT=1234 scripts/boot-tahoe.sh              # any boot script with GDBPORT set opens the stub
scripts/gdb-attach.sh -s auto                  # attach, find the kernel slide, load symbols
```

`gdb-attach.sh` picks the newest `build/*/obj/RELEASE_X86_64/kernel.unstripped`. Pass `-k <file>` for another one, `-p <port>` for another port and `-x <file>` to run a command file in batch mode and detach (this is what the scripts use, since leaving gdb attached stops the guest).

```sh
printf 'xcpus\nxthreads newfs\n' > /tmp/cmds
scripts/gdb-attach.sh -s auto -x /tmp/cmds
```

## Commands

`scripts/gdb/xcross.py` adds these. All of them only read memory.

| Command | What it prints |
|---|---|
| `xslide` | Finds where the kernel was loaded (the boot slide) by scanning for the Mach-O header, then reloads the symbols at that address. `-s auto` runs it for you. |
| `xcpus` | The instruction pointer of each CPU, symbolized, and whether it is in the idle loop. |
| `xthreads [name]` | Every thread (filtered by process name if given): process, pid, state, wait event, and the symbolized words on its kernel stack. |
| `xps` | The task list with pids and thread counts. |
| `xbt <name or pid>` | A backtrace for one process by walking saved frame pointers. |
| `xpanic` | The panic string, if the kernel has one. |

## Things that bite

- Under KVM use `hbreak`, not `break`. Software breakpoints patch guest memory and KVM does not see the patch land.
- The slide changes every boot. Run `xslide` (or pass `-s auto`) after every boot, or the symbols are wrong and `xcpus` prints nonsense like every CPU at the same address.
- If the guest is running the stock Apple kernel (no `kcsuffix`, or the file is missing), the symbols will not match. A tell is a thread count of 0 and the same RIP on all CPUs.
- Kernel queue links hold pointers to the objects, not to the link fields. `xthreads` knows this. If you walk a queue by hand, `p *(thread_t)ptr` works and `p *(queue_entry_t)ptr` does not.
- Stack words are guesses. A symbol next to a stack address is a return address only if the frame is real. Read the list top down and trust the repeated, plausible chains.

## Worked example: where a High Sierra install hung

This one is from the High Sierra branch, where the same scripts are used. On the `Opteron_G2` model the installer's stage 2 stopped at `Initialized /dev/rdisk3 as a 5 MB case-insensitive HFS Plus volume` and never moved. The screen shows nothing more. gdb showed this:

```
xcpus       all four CPUs in machine_idle     (the guest is idle, not spinning)
xthreads    newfs_hfs blocked in
              thread_block_reason
              IOService::waitForState
              IOService::waitQuiet
              spec_close -> VNOP_CLOSE -> close_nocancel
```

`newfs_hfs` finished formatting and was waiting in `close()` for IOKit to go quiet, which never happened. `kextd` was not running and no kext load request was pending. That ruled out a pending kext request. Reading the timer state next showed `tscFreq` at 800 MHz on a host that runs at about 2.1 GHz, which led to the TSC calibration fix (STATUS.md). Whether that wrong frequency is what keeps IOKit busy has not been shown. With the same installer on a Penryn model the wait ended.

## On this branch (Tahoe)

This gdb cannot read Tahoe's debug info (`DW_FORM_strx used without .debug_str`), so the commands that need struct layouts (`xthreads`, `xps`, `xbt`) fail with `No struct type named thread`. They were written and checked against High Sierra's DWARF. `xslide`, `xcpus` and plain `info symbol` only need the symbol table and should work, but none of the commands has been run against a Tahoe guest yet. For thread lists on Tahoe use the older `scripts/gdb-threads.py` or `scripts/tahoe-debug.sh`, lldb (below), or a gdb built from current sources.

The Tahoe kernel has its own dump trigger: `sysctl debug.xcross_dump` makes it print its threads to the console.

## lldb

Apple's kernel debugging macros (`tools/lldbmacros` in xnu) are written for lldb, and lldb reads DWARF 5. It speaks the same gdb remote protocol, so it can attach to the same stub:

```
lldb build/xnu-k8/obj/RELEASE_X86_64/kernel.unstripped
(lldb) gdb-remote 1234
```

This is the second option and it is not wired up yet. No lldb is installed on the machine these docs were written on, so none of the lldb steps have been run. If you try it, the slide still has to be applied (`target modules load --file kernel.unstripped --slide <slide>`), and Apple's macros expect Apple's lldb with its Darwin kernel plugin, which Linux builds of lldb may not have.
