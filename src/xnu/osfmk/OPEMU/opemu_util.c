/*
 * OPEMU shared helpers: effective addresses, memory access and general purpose registers.
 *
 * xcross: the original plugins could only address [base + disp] (no SIB index/scale, RIP-relative used the address of the
 * faulting instruction instead of the next one, no 32-bit register names). These helpers handle all of that.
 * Segment overrides (fs:/gs:) are not applied.
 
 *
 * STATUS
 *  . Effective addresses with base, SIB index/scale, RIP-relative (relative to the NEXT instruction) and 32-bit addressing; sized user/kernel
 *  .   memory access; GPR read/write with x86 write semantics for 8/16/32/64-bit names. Segment overrides (fs:/gs:) are not applied.
 *  . Used by the libudis86 based sets (sse3, ssse3, sse41, sse42); the AVX engine has its own decoder.
 *
 * HISTORY
 *  . XCROSS   New: the original plugins only supported [base + disp] and used the faulting instruction's address for RIP-relative operands
 */
#include <stdint.h>
#include <string.h>
#include <kern/misc_protos.h>
#include "opemu.h"

/* register name ranges in libudis86's enum ud_type */
static int
reg_in(ud_type_t r, ud_type_t first, ud_type_t last) { return r >= first && r <= last; }

/*
 * Decode a general purpose register name.
 * index: x86 encoding number 0..15 (rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8..r15); bits: 8/16/32/64; high8: AH/CH/DH/BH
 * @return 0 on success, -1 if r is not a general purpose register
 */
int
opemu_gpr_decode(ud_type_t r, int *index, int *bits, int *high8)
{
	*high8 = 0;
	if (reg_in(r, UD_R_AL, UD_R_BL)) { *index = r - UD_R_AL; *bits = 8; return 0; }
	if (reg_in(r, UD_R_AH, UD_R_BH)) { *index = r - UD_R_AH; *bits = 8; *high8 = 1; return 0; }
	if (reg_in(r, UD_R_SPL, UD_R_DIL)) { *index = 4 + (r - UD_R_SPL); *bits = 8; return 0; }
	if (reg_in(r, UD_R_R8B, UD_R_R15B)) { *index = 8 + (r - UD_R_R8B); *bits = 8; return 0; }
	if (reg_in(r, UD_R_AX, UD_R_DI)) { *index = r - UD_R_AX; *bits = 16; return 0; }
	if (reg_in(r, UD_R_R8W, UD_R_R15W)) { *index = 8 + (r - UD_R_R8W); *bits = 16; return 0; }
	if (reg_in(r, UD_R_EAX, UD_R_EDI)) { *index = r - UD_R_EAX; *bits = 32; return 0; }
	if (reg_in(r, UD_R_R8D, UD_R_R15D)) { *index = 8 + (r - UD_R_R8D); *bits = 32; return 0; }
	if (reg_in(r, UD_R_RAX, UD_R_RDI)) { *index = r - UD_R_RAX; *bits = 64; return 0; }
	if (reg_in(r, UD_R_R8, UD_R_R15)) { *index = 8 + (r - UD_R_R8); *bits = 64; return 0; }
	return -1;
}

static uint64_t *
gpr64_slot(x86_saved_state64_t *s, int idx)
{
	switch (idx) {
	case 0: return &s->rax;  case 1: return &s->rcx;  case 2: return &s->rdx;  case 3: return &s->rbx;
	case 4: return &s->isf.rsp; case 5: return &s->rbp; case 6: return &s->rsi; case 7: return &s->rdi;
	case 8: return &s->r8;   case 9: return &s->r9;   case 10: return &s->r10; case 11: return &s->r11;
	case 12: return &s->r12; case 13: return &s->r13; case 14: return &s->r14; case 15: return &s->r15;
	}
	return NULL;
}

static uint32_t *
gpr32_slot(x86_saved_state32_t *s, int idx)
{
	switch (idx) {
	case 0: return &s->eax; case 1: return &s->ecx; case 2: return &s->edx; case 3: return &s->ebx;
	case 4: return &s->uesp; case 5: return &s->ebp; case 6: return &s->esi; case 7: return &s->edi;
	}
	return NULL;
}

/* read a register by name (zero extended to 64 bits) */
int
opemu_gpr_read(const op_t *op, ud_type_t r, uint64_t *value)
{
	int idx, bits, high8;
	uint64_t v;

	if (opemu_gpr_decode(r, &idx, &bits, &high8) != 0)
		return -1;
	if (op->state_flavor == SAVEDSTATE_64) {
		uint64_t *slot = gpr64_slot(op->state64, idx & ~(high8 ? 4 : 0));
		if (!slot) return -1;
		v = *slot;
	} else {
		uint32_t *slot = gpr32_slot(op->state32, idx & 7);
		if (!slot) return -1;
		v = *slot;
	}
	if (high8) v >>= 8;
	if (bits < 64) v &= (1ULL << bits) - 1;
	*value = v;
	return 0;
}

/* write a register by name with x86 semantics: 32-bit writes zero the upper half, 8/16-bit writes merge */
int
opemu_gpr_write(const op_t *op, ud_type_t r, uint64_t value)
{
	int idx, bits, high8;

	if (opemu_gpr_decode(r, &idx, &bits, &high8) != 0)
		return -1;
	if (op->state_flavor == SAVEDSTATE_64) {
		uint64_t *slot = gpr64_slot(op->state64, idx);
		if (!slot) return -1;
		switch (bits) {
		case 64: *slot = value; break;
		case 32: *slot = (uint32_t)value; break;
		case 16: *slot = (*slot & ~0xffffULL) | (value & 0xffff); break;
		default:
			if (high8) *slot = (*slot & ~0xff00ULL) | ((value & 0xff) << 8);
			else       *slot = (*slot & ~0xffULL) | (value & 0xff);
		}
	} else {
		uint32_t *slot = gpr32_slot(op->state32, idx);
		if (!slot) return -1;
		switch (bits) {
		case 32: *slot = (uint32_t)value; break;
		case 16: *slot = (*slot & ~0xffffu) | (value & 0xffff); break;
		default:
			if (high8) *slot = (*slot & ~0xff00u) | ((value & 0xff) << 8);
			else       *slot = (*slot & ~0xffu) | (value & 0xff);
		}
	}
	return 0;
}

/* effective address of a memory operand */
int
opemu_ea(const op_t *op, const ud_operand_t *m, uint64_t *addr)
{
	uint64_t a = 0, v;
	int64_t disp = 0;

	if (m->type != UD_OP_MEM)
		return -1;

	switch (m->offset) {		/* displacement size in bits */
	case 8:  disp = m->lval.sbyte;  break;
	case 16: disp = m->lval.sword;  break;
	case 32: disp = m->lval.sdword; break;
	case 64: disp = m->lval.sqword; break;
	}

	if (m->base == UD_R_RIP) {
		/* RIP-relative: relative to the end of the instruction */
		if (op->state_flavor == SAVEDSTATE_64)
			a = op->state64->isf.rip + op->insn_len;
		else
			a = op->state32->eip + op->insn_len;
	} else if (m->base != UD_NONE) {
		if (opemu_gpr_read(op, m->base, &v) != 0)
			return -1;
		a = v;
	}
	if (m->index != UD_NONE) {
		if (opemu_gpr_read(op, m->index, &v) != 0)
			return -1;
		a += v * (m->scale ? m->scale : 1);
	}
	a += (uint64_t)disp;

	/* 32-bit addressing (0x67 prefix, or a 32-bit process) wraps at 4 GiB */
	if (op->ud_obj->adr_mode == 32 || op->state_flavor != SAVEDSTATE_64)
		a &= 0xffffffffULL;
	*addr = a;
	return 0;
}

int
opemu_load_mem(const op_t *op, const ud_operand_t *m, void *dst, size_t n)
{
	uint64_t a;
	if (opemu_ea(op, m, &a) != 0)
		return -1;
	if (op->ring0) {
		memcpy(dst, (const void *)(uintptr_t)a, n);
		return 0;
	}
	return copyin(a, (char *)dst, n) == 0 ? 0 : -1;
}

int
opemu_store_mem(const op_t *op, const ud_operand_t *m, const void *src, size_t n)
{
	uint64_t a;
	if (opemu_ea(op, m, &a) != 0)
		return -1;
	if (op->ring0) {
		memcpy((void *)(uintptr_t)a, src, n);
		return 0;
	}
	return copyout((const char *)src, a, n) == 0 ? 0 : -1;
}
