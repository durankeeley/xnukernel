/*
 * OPEMU fast path for PSHUFB and PALIGNR (SSE form, 64-bit mode).
 *
 * xcross: macOS 10.13's memmove/memcpy uses these two instructions unconditionally, so a CPU without SSSE3 (AMD K8/K10)
 * traps on them constantly (>99% of all emulated instructions). The generic path (libudis86 decode, generic dispatcher,
 * operand structs) is far too heavy for that, so this file decodes just these encodings by hand and computes on 64-bit
 * words. Anything it does not recognise returns 0 and falls back to the generic path, so behaviour is unchanged.
 *
 *   66 [REX] 0F 38 00 /r         PSHUFB  xmm1, xmm2/m128
 *   66 [REX] 0F 3A 0F /r ib      PALIGNR xmm1, xmm2/m128, imm8
 *
 * The user's XMM registers are live while we run; we only read the two operands and write the destination, and this code
 * is compiled without implicit vector use (-mno-implicit-float), so nothing else is disturbed.
 */
#include <stdint.h>
#include <string.h>
#include <kern/misc_protos.h>
#include "opemu.h"
#include "ssse3_priv.h"

static inline uint64_t *
gpr(x86_saved_state64_t *s, int i)
{
	switch (i) {
	case 0: return &s->rax;  case 1: return &s->rcx;  case 2: return &s->rdx;  case 3: return &s->rbx;
	case 4: return &s->isf.rsp; case 5: return &s->rbp; case 6: return &s->rsi; case 7: return &s->rdi;
	case 8: return &s->r8;   case 9: return &s->r9;   case 10: return &s->r10; case 11: return &s->r11;
	case 12: return &s->r12; case 13: return &s->r13; case 14: return &s->r14; default: return &s->r15;
	}
}

static inline void
pshufb16(const uint64_t *d, const uint64_t *c, uint64_t *out)
{
	const uint8_t *data = (const uint8_t *)d, *ctl = (const uint8_t *)c;
	uint8_t r[16];
	for (int i = 0; i < 16; i++)
		r[i] = (ctl[i] & 0x80) ? 0 : data[ctl[i] & 15];
	memcpy(out, r, 16);
}

/* (dst:src) >> (imm * 8), 128 bits of result; imm >= 32 gives zero */
static inline void
palignr16(const uint64_t *d, const uint64_t *s, unsigned imm, uint64_t *out)
{
	uint64_t w[6];
	unsigned q, rem;
	if (imm >= 32) { out[0] = out[1] = 0; return; }
	w[0] = s[0]; w[1] = s[1]; w[2] = d[0]; w[3] = d[1]; w[4] = w[5] = 0;
	q = imm >> 3; rem = (imm & 7) * 8;
	if (rem) {
		out[0] = (w[q] >> rem) | (w[q + 1] << (64 - rem));
		out[1] = (w[q + 1] >> rem) | (w[q + 2] << (64 - rem));
	} else {
		out[0] = w[q]; out[1] = w[q + 1];
	}
}

int
opemu_fast(const uint8_t *code, x86_saved_state64_t *ss, int ring0)
{
	unsigned i = 0, rex = 0, modrm, mod, rm, reg, imm = 0;
	int is_palignr;
	uint64_t d[2], s[2], r[2];

	if (code[i] != 0x66) return 0;			/* the MMX forms and everything unusual: generic path */
	i++;
	if ((code[i] & 0xf0) == 0x40) rex = code[i++];
	if (code[i++] != 0x0f) return 0;
	if (code[i] == 0x38) { if (code[i + 1] != 0x00) return 0; is_palignr = 0; }
	else if (code[i] == 0x3a) { if (code[i + 1] != 0x0f) return 0; is_palignr = 1; }
	else return 0;
	i += 2;

	modrm = code[i++];
	mod = modrm >> 6; rm = modrm & 7;
	reg = ((modrm >> 3) & 7) | ((rex & 4) << 1);

	if (!opemu_ssse3_enabled())
		return 0;

	if (mod == 3) {
		if (is_palignr) imm = code[i++];
		_store_xmm(rm | ((rex & 1) << 3), (__uint128_t *)s);
	} else {
		uint64_t ea = 0;
		int64_t disp = 0;
		unsigned base = rm | ((rex & 1) << 3);
		int has_base = 1, rip_rel = 0;

		if (rm == 4) {					/* SIB */
			unsigned sib = code[i++], idx = ((sib >> 3) & 7) | ((rex & 2) << 2);
			base = (sib & 7) | ((rex & 1) << 3);
			if (idx != 4) ea = *gpr(ss, idx) << (sib >> 6);	/* index 4 (rsp) without REX.X means none */
			if ((sib & 7) == 5 && mod == 0) { has_base = 0; disp = (int32_t)(code[i] | code[i + 1] << 8 | code[i + 2] << 16 | (uint32_t)code[i + 3] << 24); i += 4; }
		} else if (rm == 5 && mod == 0) {		/* RIP-relative */
			has_base = 0; rip_rel = 1;
			disp = (int32_t)(code[i] | code[i + 1] << 8 | code[i + 2] << 16 | (uint32_t)code[i + 3] << 24); i += 4;
		}
		if (mod == 1) disp = (int8_t)code[i++];
		else if (mod == 2) { disp = (int32_t)(code[i] | code[i + 1] << 8 | code[i + 2] << 16 | (uint32_t)code[i + 3] << 24); i += 4; }
		if (is_palignr) imm = code[i++];

		if (has_base) ea += *gpr(ss, base);
		if (rip_rel) ea += ss->isf.rip + i;		/* relative to the end of the instruction */
		ea += (uint64_t)disp;
		if (ring0) memcpy(s, (const void *)(uintptr_t)ea, 16);
		else if (copyin(ea, (char *)s, 16) != 0) return 0;
	}

	_store_xmm(reg, (__uint128_t *)d);
	if (is_palignr) palignr16(d, s, imm, r);
	else pshufb16(d, s, r);
	_load_xmm(reg, (const __uint128_t *)r);
	opemu_ssse3_hit(is_palignr);
	return (int)i;
}
