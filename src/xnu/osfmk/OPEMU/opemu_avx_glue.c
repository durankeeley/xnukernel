/**
 * Kernel glue for the AVX engine: called from the #UD handlers (opemu_utrap / opemu_ktrap) before the generic decoder.
 * It snapshots the XMM registers (live in the CPU), joins them with the per-thread shadow of the upper YMM halves, runs avx_exec() and
 * writes the result back (XMM, shadow, GPRs, arithmetic RFLAGS, RIP). K8/K10 have no XSAVE, so the kernel never saves YMM state itself: the
 * shadow lives in struct machine_thread (opemu_ymmh[0] for user code, [1] for kernel code, so a kext using AVX cannot corrupt user state).
 *
 * STATUS
 *  . 64-bit user and kernel code; 32-bit processes are not handled (C4/C5 are LES/LDS there).
 *  . A failed user memory access returns 0 and ends up as SIGILL instead of a page fault.
 *  . Unsupported instructions are logged (first 200) with their bytes so the missing form can be added.
 *  . The shadow is not copied on fork/thread_dup: a child starts with zero upper halves.
 *
 * HISTORY
 *  . XCROSS   New for the Tahoe AVX engine
 */
#include <stdint.h>
#include <string.h>
#include <i386/trap.h>
#include <i386/thread.h>
#include <kern/thread.h>
#include <kern/misc_protos.h>
#include "opemu.h"
#include "opemu_avx.h"


struct avx_ref { int ring0; };

static int
k_rd(void *u, uint64_t a, void *d, size_t n)
{
	if (((struct avx_ref *)u)->ring0) { memcpy(d, (const void *)(uintptr_t)a, n); return 0; }
	return copyin(a, (char *)d, n) ? -1 : 0;
}

static int
k_wr(void *u, uint64_t a, const void *s, size_t n)
{
	if (((struct avx_ref *)u)->ring0) { memcpy((void *)(uintptr_t)a, s, n); return 0; }
	return copyout(s, a, n) ? -1 : 0;
}

static uint64_t avx_hits, avx_fail;

/* returns 1 if the instruction was emulated (RIP advanced), 0 if it is not a VEX instruction / not supported */
int
opemu_avx_try(x86_saved_state_t *state, int ring0)
{
	if (!is_saved_state64(state)) {
		return 0;
	}
	x86_saved_state64_t *ss = saved_state64(state);
	uint8_t code[32];
	memset(code, 0, sizeof code);
	if (ring0) {
		memcpy(code, (const void *)(uintptr_t)ss->isf.rip, 16);
	} else if (copyin(ss->isf.rip, (char *)code, 16) != 0) {
		/* near the end of a mapping: try a shorter read */
		if (copyin(ss->isf.rip, (char *)code, 8) != 0) {
			return 0;
		}
	}
	{
		/* VEX (C4/C5), or legacy MOVBE: [66] [REX] 0F 38 F0/F1 */
		unsigned k = 0;
		while (code[k] == 0x66 && k < 4) k++;
		if ((code[k] & 0xF0) == 0x40) k++;
		int movbe = code[k] == 0x0F && code[k + 1] == 0x38 && (code[k + 2] == 0xF0 || code[k + 2] == 0xF1);
		if (code[0] != 0xC4 && code[0] != 0xC5 && !movbe) {
			return 0;
		}
	}

	thread_t th = current_thread();
	uint8_t *hi = th->machine.opemu_ymmh[ring0 ? 1 : 0];
	uint8_t xs[256] __attribute__((aligned(16)));
	avx_ctx_t c;
	struct avx_ref ref = { ring0 };

	opemu_xmm_save(xs);
	for (int i = 0; i < 16; i++) {
		memcpy(c.y[i], xs + 16 * i, 16);
		memcpy(c.y[i] + 16, hi + 16 * i, 16);
	}
	c.g[0] = ss->rax; c.g[1] = ss->rcx; c.g[2] = ss->rdx; c.g[3] = ss->rbx; c.g[4] = ss->isf.rsp; c.g[5] = ss->rbp; c.g[6] = ss->rsi; c.g[7] = ss->rdi;
	c.g[8] = ss->r8; c.g[9] = ss->r9; c.g[10] = ss->r10; c.g[11] = ss->r11; c.g[12] = ss->r12; c.g[13] = ss->r13; c.g[14] = ss->r14; c.g[15] = ss->r15;
	c.rflags = ss->isf.rflags;
	c.rip = ss->isf.rip;
	__asm__ __volatile__("stmxcsr %0" : "=m"(c.mxcsr));
	c.mxcsr_dirty = 0;
	c.is64 = 1;
	c.user = &ref; c.rd = k_rd; c.wr = k_wr;

	unsigned len = 0;
	int rc = avx_exec(&c, code, &len);
	if (rc != AVX_OK) {
		opemu_xmm_restore(xs);
		avx_fail++;
		if (avx_fail <= 200) {
			printf("OPEMU-AVX: unsupported (rc=%d) rip=%llx bytes=%02x %02x %02x %02x %02x %02x %02x %02x %02x\n", rc, ss->isf.rip,
			    code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7], code[8]);
		}
		return 0;
	}
	for (int i = 0; i < 16; i++) {
		memcpy(xs + 16 * i, c.y[i], 16);
		memcpy(hi + 16 * i, c.y[i] + 16, 16);
	}
	opemu_xmm_restore(xs);
	if (c.mxcsr_dirty) {
		uint32_t m = c.mxcsr & 0xFFBF;      /* DAZ (bit 6) is not supported by every K8 */
		__asm__ __volatile__("ldmxcsr %0" : : "m"(m));
	}
	ss->rax = c.g[0]; ss->rcx = c.g[1]; ss->rdx = c.g[2]; ss->rbx = c.g[3]; ss->isf.rsp = c.g[4]; ss->rbp = c.g[5]; ss->rsi = c.g[6]; ss->rdi = c.g[7];
	ss->r8 = c.g[8]; ss->r9 = c.g[9]; ss->r10 = c.g[10]; ss->r11 = c.g[11]; ss->r12 = c.g[12]; ss->r13 = c.g[13]; ss->r14 = c.g[14]; ss->r15 = c.g[15];
	ss->isf.rflags = (ss->isf.rflags & ~(uint64_t)0x8D5) | (c.rflags & 0x8D5);
	ss->isf.rip += len;
	avx_hits++;
	return 1;
}
