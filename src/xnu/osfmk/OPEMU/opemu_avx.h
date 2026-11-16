/*
 * OPEMU AVX/AVX2/BMI engine (xcross). Pure C, no kernel dependencies, so it can be tested on the host against the real CPU
 * (scripts/opemu-hosttest.sh avx).
 *
 * The caller provides a snapshot of the register file (16 x 256-bit vector registers, 16 GPRs, RFLAGS, RIP) and callbacks to access memory.
 * avx_exec() decodes ONE instruction, executes it against the snapshot and returns its length. The caller writes the snapshot back and
 * advances RIP only when it returns 0.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct avx_ctx {
	uint8_t   y[16][32];          /* vector registers: [0..15] = XMM / low lane, [16..31] = upper lane (shadow) */
	uint64_t  g[16];              /* rax rcx rdx rbx rsp rbp rsi rdi r8..r15 */
	uint64_t  rflags;
	uint64_t  rip;                /* address of the instruction (RIP-relative operands are relative to its end) */
	uint32_t  mxcsr;              /* user MXCSR (for the floating point forms) */
	int       mxcsr_dirty;        /* set by VLDMXCSR: the caller must load c->mxcsr into the CPU */
	int       is64;
	void     *user;
	int     (*rd)(void *user, uint64_t addr, void *dst, size_t n);        /* 0 = ok */
	int     (*wr)(void *user, uint64_t addr, const void *src, size_t n);
} avx_ctx_t;

#define AVX_OK          0
#define AVX_UNSUPPORTED (-1)   /* valid/unknown instruction the engine does not implement */
#define AVX_FAULT       (-2)   /* memory access failed */
#define AVX_NOTVEX      (-3)   /* not a VEX instruction */

/* returns AVX_OK and the instruction length, or an error. 'code' must have at least 15 readable bytes. */
int avx_exec(avx_ctx_t *c, const uint8_t *code, unsigned *len);

/* floating point helper, implemented by the host (inline asm on SSE registers): res = op(a, b, imm) on a 128-bit lane.
 * Implemented in opemu_avx_fp.c; 'kind' is one of the AVXFP_* values. Returns 0 if the combination is not supported. */
int avx_fp_lane(int kind, uint8_t *res, const uint8_t *a, const uint8_t *b, unsigned imm, uint32_t *mxcsr);
