#pragma once

#include <stdint.h>
#include <kern/sched_prim.h>
#include "libudis86/extern.h"

struct op {
	// one of either. order here matters.
	union {
		x86_saved_state64_t *state64;
		x86_saved_state32_t *state32;
	};

	enum {
		SAVEDSTATE_64,
		SAVEDSTATE_32,
	} state_flavor;

	// just another version of the above
	x86_saved_state_t *state;

	// disassembly object
	ud_t		*ud_obj;

	// boolean flag
	uint8_t		ring0;

	// xcross: length in bytes of the faulting instruction (RIP-relative operands are relative to the NEXT instruction)
	uint8_t		insn_len;
};
typedef struct op op_t;

/**
 * Trap handlers, analogous to a program's entry point
 * @param state: xnu's trap.c saved thread state
 */
int opemu_ktrap(x86_saved_state_t *state);
extern int opemu_avx_try(x86_saved_state_t *state, int ring0);   /* opemu_avx_glue.c */
void opemu_utrap(x86_saved_state_t *state) __attribute__((noreturn));


/**
 * Forward declarations of private xnu functions
 */
extern void mach_call_munger(x86_saved_state_t *state);
extern void unix_syscall(x86_saved_state_t *);
extern void mach_call_munger64(x86_saved_state_t *state);
extern void unix_syscall64(x86_saved_state_t *);

/**
 * Policy: may the emulator handle this mnemonic? (see opemu_policy.c for the opemu* boot-args)
 */
extern int opemu_allow(uint32_t mnemonic);

int retrieve_reg(/*const*/ x86_saved_state_t *, const ud_type_t, uint64_t *);

/**
 * Entry points for the "plugins"
 */
extern int op_sse3x_run(const op_t*);
extern int op_sse3_run(const op_t*);
extern int op_sse41_run(const op_t*);	/* SSE4.1, POPCNT, CRC32 (sse41.c) */

/**
 * xcross: shared helpers (opemu_util.c)
 *  - effective address of a ud memory operand: base + index*scale + disp, RIP-relative, 32-bit addressing
 *  - sized memory read/write (direct in ring 0, copyin/copyout for user space)
 *  - general purpose register access by ud register name (8/16/32/64 bit, both saved-state flavours)
 */
/**
 * xcross: fast path for PSHUFB/PALIGNR (opemu_fast.c). Returns the instruction length if it emulated the instruction
 * (the caller then advances RIP), or 0 if the generic path must handle it. Policy hooks live in opemu_policy.c.
 */
extern int  opemu_fast(const uint8_t *code, x86_saved_state64_t *ss, int ring0);
extern int  opemu_ssse3_enabled(void);
extern void opemu_ssse3_hit(int is_palignr);

extern int  opemu_ea(const op_t *op, const ud_operand_t *m, uint64_t *addr);
extern int  opemu_load_mem(const op_t *op, const ud_operand_t *m, void *dst, size_t n);
extern int  opemu_store_mem(const op_t *op, const ud_operand_t *m, const void *src, size_t n);
extern int  opemu_gpr_decode(ud_type_t r, int *index, int *bits, int *high8);
extern int  opemu_gpr_read(const op_t *op, ud_type_t r, uint64_t *value);
extern int  opemu_gpr_write(const op_t *op, ud_type_t r, uint64_t value);

/**
 * xcross: the emulation routines run in the kernel while the user's XMM registers are still live in the CPU.
 * Any routine that may use XMM registers itself (floating point) must be bracketed by these.
 */
static inline void opemu_xmm_save(uint8_t *buf)	/* 256 bytes */
{
	__asm__ __volatile__(
	    "movdqu %%xmm0,   0(%0)\n movdqu %%xmm1,  16(%0)\n movdqu %%xmm2,  32(%0)\n movdqu %%xmm3,  48(%0)\n"
	    "movdqu %%xmm4,  64(%0)\n movdqu %%xmm5,  80(%0)\n movdqu %%xmm6,  96(%0)\n movdqu %%xmm7, 112(%0)\n"
	    "movdqu %%xmm8, 128(%0)\n movdqu %%xmm9, 144(%0)\n movdqu %%xmm10,160(%0)\n movdqu %%xmm11,176(%0)\n"
	    "movdqu %%xmm12,192(%0)\n movdqu %%xmm13,208(%0)\n movdqu %%xmm14,224(%0)\n movdqu %%xmm15,240(%0)\n"
	    : : "r"(buf) : "memory");
}
static inline void opemu_xmm_restore(const uint8_t *buf)
{
	__asm__ __volatile__(
	    "movdqu   0(%0),%%xmm0\n movdqu  16(%0),%%xmm1\n movdqu  32(%0),%%xmm2\n movdqu  48(%0),%%xmm3\n"
	    "movdqu  64(%0),%%xmm4\n movdqu  80(%0),%%xmm5\n movdqu  96(%0),%%xmm6\n movdqu 112(%0),%%xmm7\n"
	    "movdqu 128(%0),%%xmm8\n movdqu 144(%0),%%xmm9\n movdqu 160(%0),%%xmm10\n movdqu 176(%0),%%xmm11\n"
	    "movdqu 192(%0),%%xmm12\n movdqu 208(%0),%%xmm13\n movdqu 224(%0),%%xmm14\n movdqu 240(%0),%%xmm15\n"
	    : : "r"(buf) : "memory", "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7",
	      "xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15");
}

