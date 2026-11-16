/*
 * SSE4.2 emulation: PCMPGTQ and the string-compare family (PCMPISTRI/M, PCMPESTRI/M).
 *
 * xcross: rewritten from Intel's documented algorithm (SDM vol. 2, "PCMPISTRI"/"PCMPESTRI") and verified bit-for-bit against
 * the hardware instructions by scripts/opemu-hosttest.sh (all 128 immediates, random data, random EAX/EDX lengths).
 * The previous version ignored EAX/EDX, mapped the flags to the wrong RFLAGS bits and returned wrong results for many inputs.
 *
 * Operand convention (matches ssse3_grab_operands): this->dst = xmm1 (the "A" string), this->src = xmm2/m128 (the "B" string).
 
 *
 * STATUS
 *  . PCMPGTQ and PCMPISTRI/M, PCMPESTRI/M with all 128 immediates; host-tested (random data, random EAX/EDX lengths).
 *
 * HISTORY
 *  . XCROSS   Rewrote the whole file; the previous version gave wrong results for 25-40% of inputs and used the wrong RFLAGS bits
 */
#include "ssse3_priv.h"

/* real RFLAGS bit positions */
#define RF_CF	0x0001
#define RF_PF	0x0004
#define RF_AF	0x0010
#define RF_ZF	0x0040
#define RF_SF	0x0080
#define RF_OF	0x0800
#define RF_ARITH (RF_CF | RF_PF | RF_AF | RF_ZF | RF_SF | RF_OF)

static int
pcs_elem(const sse_reg_t *r, int idx, int word, int sgn)
{
	if (word)
		return sgn ? (int)r->int16[idx] : (int)r->uint16[idx];
	return sgn ? (int)r->int8[idx] : (int)r->uint8[idx];
}

/* number of elements before the first zero element (implicit-length forms) */
static int
pcs_implicit_len(const sse_reg_t *r, int word)
{
	int n = word ? 8 : 16, i;
	for (i = 0; i < n; i++)
		if ((word ? r->uint16[i] : r->uint8[i]) == 0)
			break;
	return i;
}

/* explicit length from EAX/EDX: |value|, saturated to the element count */
static int
pcs_explicit_len(int32_t v, int word)
{
	int n = word ? 8 : 16;
	int64_t a = v < 0 ? -(int64_t)v : (int64_t)v;
	return a > n ? n : (int)a;
}

/*
 * Core of all four string instructions.
 * @return IntRes2 (n bits); *flags receives the RFLAGS bits to set (CF, ZF, SF, OF).
 */
static uint32_t
pcs_core(const sse_reg_t *a, int la, const sse_reg_t *b, int lb, int imm, uint64_t *flags)
{
	const int word = imm & 1, sgn = (imm >> 1) & 1, agg = (imm >> 2) & 3, pol = (imm >> 4) & 3;
	const int n = word ? 8 : 16;
	uint8_t bres[16][16];	/* bres[i][j]: A element i against B element j */
	uint32_t ires1 = 0, ires2;
	int i, j, k;

	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++) {
			const int va = i < la, vb = j < lb;
			uint8_t r;
			if (va && vb) {
				const int x = pcs_elem(a, i, word, sgn), y = pcs_elem(b, j, word, sgn);
				if (agg == 1)		/* ranges: even = lower bound (>=), odd = upper bound (<=) */
					r = (i & 1) ? (y <= x) : (y >= x);
				else
					r = (x == y);
			} else if (agg == 2) {		/* equal each */
				r = !va && !vb;
			} else if (agg == 3) {		/* equal ordered */
				r = !va;
			} else {			/* equal any, ranges */
				r = 0;
			}
			bres[i][j] = r;
		}
	}

	for (j = 0; j < n; j++) {
		int r = 0;
		switch (agg) {
		case 0:		/* equal any */
			for (i = 0; i < n; i++) r |= bres[i][j];
			break;
		case 1:		/* ranges */
			for (i = 0; i < n; i += 2) r |= (bres[i][j] & bres[i + 1][j]);
			break;
		case 2:		/* equal each */
			r = bres[j][j];
			break;
		default:	/* equal ordered */
			r = 1;
			for (k = 0; j + k < n; k++) r &= bres[k][j + k];
			break;
		}
		ires1 |= (uint32_t)(r & 1) << j;
	}

	switch (pol) {
	case 1:  ires2 = ~ires1 & ((1u << n) - 1); break;			/* negative */
	case 3:  /* masked negative: invert only where B is valid */
		ires2 = ires1;
		for (j = 0; j < lb && j < n; j++) ires2 ^= 1u << j;
		break;
	default: ires2 = ires1; break;						/* positive, masked positive */
	}

	*flags = (ires2 ? RF_CF : 0) | (lb < n ? RF_ZF : 0) | (la < n ? RF_SF : 0) | ((ires2 & 1) ? RF_OF : 0);
	return ires2;
}

static int
pcs_index(uint32_t ires2, int imm)
{
	const int n = (imm & 1) ? 8 : 16;
	int i;
	if (!ires2) return n;
	if (imm & 0x40) { for (i = n - 1; i >= 0; i--) if (ires2 & (1u << i)) return i; }
	else            { for (i = 0; i < n; i++)      if (ires2 & (1u << i)) return i; }
	return n;
}

static void
pcs_mask(sse_reg_t *res, uint32_t ires2, int imm)
{
	const int word = imm & 1, n = word ? 8 : 16;
	int i;
	res->uint128 = 0;
	if (!(imm & 0x40)) {			/* bit mask, zero extended */
		res->uint32[0] = ires2;
	} else {				/* byte/word mask */
		for (i = 0; i < n; i++) {
			if (!(ires2 & (1u << i))) continue;
			if (word) res->uint16[i] = 0xffff; else res->uint8[i] = 0xff;
		}
	}
}

/* write ECX/RCX and the arithmetic flags into the saved state */
static void
pcs_set_state(const op_t *op, uint64_t flags, int set_index, int index)
{
	if (is_saved_state64(op->state)) {
		if (set_index)
			op->state64->rcx = (uint32_t)index;	/* 32-bit write zero-extends */
		op->state64->isf.rflags = (op->state64->isf.rflags & ~(uint64_t)RF_ARITH) | flags;
	} else {
		if (set_index)
			op->state32->ecx = (uint32_t)index;
		op->state32->efl = (op->state32->efl & ~(uint32_t)RF_ARITH) | (uint32_t)flags;
	}
}

static void
pcs_get_lengths(const op_t *op, int word, int *la, int *lb)
{
	int32_t eax, edx;
	if (is_saved_state64(op->state)) {
		eax = (int32_t)op->state64->rax;
		edx = (int32_t)op->state64->rdx;
	} else {
		eax = (int32_t)op->state32->eax;
		edx = (int32_t)op->state32->edx;
	}
	*la = pcs_explicit_len(eax, word);
	*lb = pcs_explicit_len(edx, word);
}

/*
 * The *STRM forms always write XMM0. The generic code path commits this->res into the destination register, so:
 *  - destination is XMM0: res = mask (commit writes it);
 *  - otherwise: write the mask to XMM0 here and make res the unchanged destination value.
 */
static void
pcs_store_mask(ssse3_t *this, const sse_reg_t *mask)
{
	if (this->udo_dst && this->udo_dst->base == UD_R_XMM0) {
		this->res = *mask;
	} else {
		__uint128_t m = mask->uint128;
		this->res = this->dst;
		_load_xmm(0, &m);	/* keep this last: _load_xmm does not tell the compiler that XMM0 changed */
	}
}

void pcmpistri (ssse3_t *this)
{
	const int imm = this->udo_imm->lval.ubyte, word = imm & 1;
	uint64_t flags;
	uint32_t r = pcs_core(&this->dst, pcs_implicit_len(&this->dst, word), &this->src, pcs_implicit_len(&this->src, word), imm, &flags);
	pcs_set_state(this->op_obj, flags, 1, pcs_index(r, imm));
	this->res = this->dst;
}

void pcmpestri (ssse3_t *this)
{
	const int imm = this->udo_imm->lval.ubyte, word = imm & 1;
	int la, lb;
	uint64_t flags;
	pcs_get_lengths(this->op_obj, word, &la, &lb);
	uint32_t r = pcs_core(&this->dst, la, &this->src, lb, imm, &flags);
	pcs_set_state(this->op_obj, flags, 1, pcs_index(r, imm));
	this->res = this->dst;
}

void pcmpistrm (ssse3_t *this)
{
	const int imm = this->udo_imm->lval.ubyte, word = imm & 1;
	uint64_t flags;
	sse_reg_t mask;
	uint32_t r = pcs_core(&this->dst, pcs_implicit_len(&this->dst, word), &this->src, pcs_implicit_len(&this->src, word), imm, &flags);
	pcs_mask(&mask, r, imm);
	pcs_set_state(this->op_obj, flags, 0, 0);
	pcs_store_mask(this, &mask);
}

void pcmpestrm (ssse3_t *this)
{
	const int imm = this->udo_imm->lval.ubyte, word = imm & 1;
	int la, lb;
	uint64_t flags;
	sse_reg_t mask;
	pcs_get_lengths(this->op_obj, word, &la, &lb);
	uint32_t r = pcs_core(&this->dst, la, &this->src, lb, imm, &flags);
	pcs_mask(&mask, r, imm);
	pcs_set_state(this->op_obj, flags, 0, 0);
	pcs_store_mask(this, &mask);
}

/* PCMPGTQ xmm1, xmm2/m128: each qword of xmm1 = (xmm1 > xmm2) signed */
void pcmpgtq (ssse3_t *this)
{
	this->res.int64[0] = this->dst.int64[0] > this->src.int64[0] ? -1LL : 0;
	this->res.int64[1] = this->dst.int64[1] > this->src.int64[1] ? -1LL : 0;
}
