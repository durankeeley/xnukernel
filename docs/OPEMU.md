# OPEMU

Emulates SSE3, SSSE3, SSE4.1, SSE4.2 (string compares, PCMPGTQ), POPCNT and CRC32 for CPUs that lack them (AMD K8/K10).
Every instruction is verified against the hardware by `scripts/opemu-hosttest.sh` (see below). Policy boot-args, each `0` = never, `1` = always, `2` = auto
(default: emulate only if the CPU's own CPUID says it lacks the set):

```
opemu=<n>   opemu_sse3=<n>   opemu_ssse3=<n>   opemu_sse41=<n>   opemu_sse42=<n>   opemu_log=1
```

For example, if the hypervisor passes SSSE3 through natively: `opemu_ssse3=0`.

## Speed

Everything below is in the AMD tree and is covered by the host harness or measured under TCG.

| Change | Effect |
|---|---|
| `opemu_fast.c`: hand-decoded fast path for `PSHUFB`/`PALIGNR` (>99% of all traps: macOS `memmove` uses them unconditionally), register and memory forms, also in kernel mode | host: 11-26x faster per emulated instruction; TCG boot: ~8,000 -> ~64,000 traps/s |
| libudis86 no longer builds Intel-syntax text for every decode (`ud_set_syntax(NULL)`; text only on the error path) | host: 2.7-3.8x on the generic path; it was ~30% of all kernel instructions in a profile |
| Opcode classification cached per mnemonic in `opemu_allow` | removes a string comparison per trap |
| `xcross_cr3.c`: user and kernel share one CR3 on AMD (boot-arg `kpti=0/1`, default: share on AMD, split elsewhere; ignored if the CPU has PCID) | removes two `CR3` reloads (and TLB flushes) per trap on CPUs without PCID; no measurable change under TCG, helps real K8/K10 |

Not an optimisation but important for test speed: console and serial output are very expensive under TCG, so `scripts/tcg-time-to-login.sh` boots quiet
(no `-v`, no `serial=`). Spotlight indexing of a fresh volume saturates the guest for tens of minutes; it is disabled in the test VM
(`mdutil -a -i off`).

## Testing

```sh
scripts/opemu-hosttest.sh 20000     # ~10 s; compiles ssse3.c/sse3.c/sse42.c/sse41.c/opemu_util.c on the host, runs 100+ instruction forms
```

It prints one row per instruction form with the number of cases and mismatches, and the first mismatching input. It needs an x86-64 host with SSE4.2.
The harness found and fixed: wrong operand order in `addsub*`/`hadd*`/`hsub*`/`pcmpgtq`, `palignr` crashes, broken `pcmp[ie]str*` flags/results/lengths,
no SIB/RIP-relative addressing, and XMM state corruption by the FP routines.
