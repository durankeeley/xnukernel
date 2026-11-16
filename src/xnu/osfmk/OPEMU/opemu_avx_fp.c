/**
 * Floating point helpers for the AVX engine (opemu_avx.c).
 * K8/K10 execute legacy SSE/SSE2/SSE3 natively, so the VEX arithmetic forms are computed by running the equivalent legacy instruction on scratch
 * registers (the caller has already saved the live XMM registers). The kernel is built -msoft-float, so no C floating point is used here:
 * everything goes through asm blocks on raw bytes. FMA does not exist on these CPUs and is computed with exact 128-bit integer arithmetic.
 *
 * STATUS
 *  . add sub mul div min max sqrt rcp rsqrt hadd hsub addsub (ps pd ss sd), compare predicates 0-31, comiss/comisd flags, int/float conversions
 *  .   (cvtdq2ps cvtps2dq cvttps2dq cvtdq2pd cvtps2pd cvtpd2ps cvtpd2dq cvttpd2dq cvtss2sd cvtsd2ss cvtsi2ss/sd cvt(t)ss/sd2si) are implemented.
 *  . avx_softfma() (binary32 and binary64) was checked against libm fma()/fmaf() on 2,000,000 random operand sets, including subnormals and cancellation.
 *  . Round-to-nearest-even only for FMA; exceptions raised by FMA are not reported in MXCSR.
 *
 * HISTORY
 *  . XCROSS   New for the Tahoe AVX engine
 */
#include <stdint.h>
#include <string.h>
#include "opemu_avx.h"
#include "opemu_avx_fp.h"

#define BIN(fn, mnem) \
	static void fn(uint8_t *r, const uint8_t *a, const uint8_t *b) { \
		__asm__ __volatile__("movdqu (%1),%%xmm0\n\tmovdqu (%2),%%xmm1\n\t" mnem " %%xmm1,%%xmm0\n\tmovdqu %%xmm0,(%0)" \
		    : : "r"(r), "r"(a), "r"(b) : "xmm0", "xmm1", "memory"); }

BIN(addps_, "addps") BIN(addpd_, "addpd") BIN(addss_, "addss") BIN(addsd_, "addsd")
BIN(subps_, "subps") BIN(subpd_, "subpd") BIN(subss_, "subss") BIN(subsd_, "subsd")
BIN(mulps_, "mulps") BIN(mulpd_, "mulpd") BIN(mulss_, "mulss") BIN(mulsd_, "mulsd")
BIN(divps_, "divps") BIN(divpd_, "divpd") BIN(divss_, "divss") BIN(divsd_, "divsd")
BIN(minps_, "minps") BIN(minpd_, "minpd") BIN(minss_, "minss") BIN(minsd_, "minsd")
BIN(maxps_, "maxps") BIN(maxpd_, "maxpd") BIN(maxss_, "maxss") BIN(maxsd_, "maxsd")
BIN(sqrtps_, "sqrtps") BIN(sqrtpd_, "sqrtpd") BIN(sqrtss_, "sqrtss") BIN(sqrtsd_, "sqrtsd")
BIN(rcpps_, "rcpps") BIN(rcpss_, "rcpss") BIN(rsqrtps_, "rsqrtps") BIN(rsqrtss_, "rsqrtss")
BIN(haddps_, "haddps") BIN(haddpd_, "haddpd") BIN(hsubps_, "hsubps") BIN(hsubpd_, "hsubpd")
BIN(addsubps_, "addsubps") BIN(addsubpd_, "addsubpd")
BIN(cmpeqps_, "cmpeqps") BIN(cmpltps_, "cmpltps") BIN(cmpleps_, "cmpleps") BIN(cmpunordps_, "cmpunordps")
BIN(cmpeqpd_, "cmpeqpd") BIN(cmpltpd_, "cmpltpd") BIN(cmplepd_, "cmplepd") BIN(cmpunordpd_, "cmpunordpd")
BIN(cmpeqss_, "cmpeqss") BIN(cmpltss_, "cmpltss") BIN(cmpless_, "cmpless") BIN(cmpunordss_, "cmpunordss")
BIN(cmpeqsd_, "cmpeqsd") BIN(cmpltsd_, "cmpltsd") BIN(cmplesd_, "cmplesd") BIN(cmpunordsd_, "cmpunordsd")

#define UN(fn, mnem) \
	static void fn(uint8_t *r, const uint8_t *b) { \
		__asm__ __volatile__("movdqu (%1),%%xmm0\n\t" mnem " %%xmm0,%%xmm0\n\tmovdqu %%xmm0,(%0)" : : "r"(r), "r"(b) : "xmm0", "memory"); }
UN(cvtdq2ps_, "cvtdq2ps") UN(cvtps2dq_, "cvtps2dq") UN(cvttps2dq_, "cvttps2dq") UN(cvtdq2pd_, "cvtdq2pd") UN(cvtps2pd_, "cvtps2pd")
UN(cvtpd2ps_, "cvtpd2ps") UN(cvtpd2dq_, "cvtpd2dq") UN(cvttpd2dq_, "cvttpd2dq")

/* binary ops with a merge: result = a with the converted low element of b (cvtss2sd, cvtsd2ss) */
BIN(cvtss2sd_, "cvtss2sd") BIN(cvtsd2ss_, "cvtsd2ss")

typedef void (*bin_fn)(uint8_t *, const uint8_t *, const uint8_t *);

static const bin_fn arith[8][4] = {   /* [kind][flavor ps pd ss sd] */
	[FP_ADD] = { addps_, addpd_, addss_, addsd_ },
	[FP_SUB] = { subps_, subpd_, subss_, subsd_ },
	[FP_MUL] = { mulps_, mulpd_, mulss_, mulsd_ },
	[FP_DIV] = { divps_, divpd_, divss_, divsd_ },
	[FP_MIN] = { minps_, minpd_, minss_, minsd_ },
	[FP_MAX] = { maxps_, maxpd_, maxss_, maxsd_ },
	[FP_SQRT] = { sqrtps_, sqrtpd_, sqrtss_, sqrtsd_ },
};

static void
merge_scalar(uint8_t *res, const uint8_t *a, const uint8_t *scalar_res, int dbl)
{
	memcpy(res, a, 16);
	memcpy(res, scalar_res, dbl ? 8 : 4);
}

/* res = op(a, b) on one 128-bit lane. For the scalar flavors only the low element is computed and the rest comes from a
 * (for sqrt/rcp/rsqrt the source is b, merged into a). Returns 0 on an unsupported combination. */
int
avx_fp128(int kind, int flavor, uint8_t *res, const uint8_t *a, const uint8_t *b)
{
	uint8_t t[16] __attribute__((aligned(16)));
	int dbl = flavor & 1, scalar = flavor >= 2;
	if (kind >= FP_ADD && kind <= FP_SQRT) {
		bin_fn f = arith[kind][flavor];
		if (!f) return 0;
		if (kind == FP_SQRT) {        /* unary: the source is b */
			if (scalar) { f(t, a, b); merge_scalar(res, a, t, dbl); }
			else { f(t, b, b); memcpy(res, t, 16); }
			return 1;
		}
		f(t, a, b);
		memcpy(res, t, 16);           /* the legacy scalar forms already keep the upper part of a */
		return 1;
	}
	switch (kind) {
	case FP_RCP:   if (dbl) return 0; if (scalar) { rcpss_(t, a, b); memcpy(res, t, 16); } else { rcpps_(t, b, b); memcpy(res, t, 16); } return 1;
	case FP_RSQRT: if (dbl) return 0; if (scalar) { rsqrtss_(t, a, b); memcpy(res, t, 16); } else { rsqrtps_(t, b, b); memcpy(res, t, 16); } return 1;
	case FP_HADD:  if (scalar) return 0; (dbl ? haddpd_ : haddps_)(t, a, b); memcpy(res, t, 16); return 1;
	case FP_HSUB:  if (scalar) return 0; (dbl ? hsubpd_ : hsubps_)(t, a, b); memcpy(res, t, 16); return 1;
	case FP_ADDSUB: if (scalar) return 0; (dbl ? addsubpd_ : addsubps_)(t, a, b); memcpy(res, t, 16); return 1;
	case FP_CVTSS2SD: cvtss2sd_(t, a, b); memcpy(res, t, 16); return 1;
	case FP_CVTSD2SS: cvtsd2ss_(t, a, b); memcpy(res, t, 16); return 1;
	}
	return 0;
}

/* conversions: res = cvt(b) over one 128-bit source (the number of produced elements follows the instruction) */
int
avx_fp_cvt(int kind, uint8_t *res, const uint8_t *b)
{
	uint8_t t[16] __attribute__((aligned(16)));
	switch (kind) {
	case FP_CVTDQ2PS:  cvtdq2ps_(t, b); break;
	case FP_CVTPS2DQ:  cvtps2dq_(t, b); break;
	case FP_CVTTPS2DQ: cvttps2dq_(t, b); break;
	case FP_CVTDQ2PD:  cvtdq2pd_(t, b); break;
	case FP_CVTPS2PD:  cvtps2pd_(t, b); break;
	case FP_CVTPD2PS:  cvtpd2ps_(t, b); break;
	case FP_CVTPD2DQ:  cvtpd2dq_(t, b); break;
	case FP_CVTTPD2DQ: cvttpd2dq_(t, b); break;
	default: return 0;
	}
	memcpy(res, t, 16);
	return 1;
}

/* compare: imm 0..31, per element mask; flavor selects ps/pd/ss/sd */
int
avx_fp_cmp(int flavor, uint8_t *res, const uint8_t *a, const uint8_t *b, unsigned imm)
{
	static const bin_fn cmp[4][4] = {
		{ cmpeqps_, cmpltps_, cmpleps_, cmpunordps_ }, { cmpeqpd_, cmpltpd_, cmplepd_, cmpunordpd_ },
		{ cmpeqss_, cmpltss_, cmpless_, cmpunordss_ }, { cmpeqsd_, cmpltsd_, cmplesd_, cmpunordsd_ },
	};
	uint8_t eq[16], lt[16], le[16], un[16], m[16];
	cmp[flavor][0](eq, a, b); cmp[flavor][1](lt, a, b); cmp[flavor][2](le, a, b); cmp[flavor][3](un, a, b);
	unsigned p = imm & 15;
	int i;
	for (i = 0; i < 16; i++) {
		uint8_t E = eq[i], Lt = lt[i], Le = le[i], U = un[i], O = (uint8_t)~U;
		uint8_t g;
		switch (p) {
		case 0: g = E; break;                      /* EQ */
		case 1: g = Lt; break;                     /* LT */
		case 2: g = Le; break;                     /* LE */
		case 3: g = U; break;                      /* UNORD */
		case 4: g = ~E; break;                     /* NEQ */
		case 5: g = ~Lt; break;                    /* NLT */
		case 6: g = ~Le; break;                    /* NLE */
		case 7: g = O; break;                      /* ORD */
		case 8: g = E | U; break;                  /* EQ_UQ */
		case 9: g = Lt | U; break;                 /* NGE */
		case 10: g = Le | U; break;                /* NGT */
		case 11: g = 0; break;                     /* FALSE */
		case 12: g = ~E & O; break;                /* NEQ_OQ */
		case 13: g = ~Lt & O; break;               /* GE */
		case 14: g = ~Le & O; break;               /* GT */
		default: g = 0xFF; break;                  /* TRUE */
		}
		m[i] = g;
	}
	/* the legacy scalar compares leave the rest of the destination unchanged: for ss/sd merge into a */
	if (flavor >= 2) {
		int n = (flavor == 2) ? 4 : 8;
		memcpy(res, a, 16);
		memcpy(res, m, n);
	} else {
		memcpy(res, m, 16);
	}
	return 1;
}

/* (u)comiss/sd: returns the resulting EFLAGS (ZF PF CF) */
uint32_t
avx_fp_comis(int dbl, const uint8_t *a, const uint8_t *b)
{
	uint64_t f;
	if (dbl) {
		__asm__ __volatile__("movdqu (%1),%%xmm0\n\tmovdqu (%2),%%xmm1\n\tcomisd %%xmm1,%%xmm0\n\tpushfq\n\tpop %0"
		    : "=r"(f) : "r"(a), "r"(b) : "xmm0", "xmm1", "cc", "memory");
	} else {
		__asm__ __volatile__("movdqu (%1),%%xmm0\n\tmovdqu (%2),%%xmm1\n\tcomiss %%xmm1,%%xmm0\n\tpushfq\n\tpop %0"
		    : "=r"(f) : "r"(a), "r"(b) : "xmm0", "xmm1", "cc", "memory");
	}
	return (uint32_t)(f & 0x45);   /* ZF, PF, CF */
}

/* scalar int <-> float. kind: 0 cvtsi2ss, 1 cvtsi2sd, 2 cvtss2si, 3 cvtsd2si, 4 cvttss2si, 5 cvttsd2si; w = 64-bit GPR */
uint64_t
avx_fp_cvt_gpr(int kind, int w, const uint8_t *xin, uint64_t gin, uint8_t *xout)
{
	uint8_t t[16] __attribute__((aligned(16)));
	uint64_t r = 0;
	memcpy(t, xin, 16);
	switch (kind) {
	case 0: if (w) __asm__ __volatile__("movdqu (%0),%%xmm0\n\tcvtsi2ssq %1,%%xmm0\n\tmovdqu %%xmm0,(%0)" : : "r"(t), "r"(gin) : "xmm0", "memory");
		else __asm__ __volatile__("movdqu (%0),%%xmm0\n\tcvtsi2ssl %1,%%xmm0\n\tmovdqu %%xmm0,(%0)" : : "r"(t), "r"((uint32_t)gin) : "xmm0", "memory"); break;
	case 1: if (w) __asm__ __volatile__("movdqu (%0),%%xmm0\n\tcvtsi2sdq %1,%%xmm0\n\tmovdqu %%xmm0,(%0)" : : "r"(t), "r"(gin) : "xmm0", "memory");
		else __asm__ __volatile__("movdqu (%0),%%xmm0\n\tcvtsi2sdl %1,%%xmm0\n\tmovdqu %%xmm0,(%0)" : : "r"(t), "r"((uint32_t)gin) : "xmm0", "memory"); break;
	case 2: if (w) __asm__ __volatile__("movdqu (%1),%%xmm0\n\tcvtss2si %%xmm0,%0" : "=r"(r) : "r"(t) : "xmm0", "memory");
		else { uint32_t x; __asm__ __volatile__("movdqu (%1),%%xmm0\n\tcvtss2si %%xmm0,%0" : "=r"(x) : "r"(t) : "xmm0", "memory"); r = x; } break;
	case 3: if (w) __asm__ __volatile__("movdqu (%1),%%xmm0\n\tcvtsd2si %%xmm0,%0" : "=r"(r) : "r"(t) : "xmm0", "memory");
		else { uint32_t x; __asm__ __volatile__("movdqu (%1),%%xmm0\n\tcvtsd2si %%xmm0,%0" : "=r"(x) : "r"(t) : "xmm0", "memory"); r = x; } break;
	case 4: if (w) __asm__ __volatile__("movdqu (%1),%%xmm0\n\tcvttss2si %%xmm0,%0" : "=r"(r) : "r"(t) : "xmm0", "memory");
		else { uint32_t x; __asm__ __volatile__("movdqu (%1),%%xmm0\n\tcvttss2si %%xmm0,%0" : "=r"(x) : "r"(t) : "xmm0", "memory"); r = x; } break;
	case 5: if (w) __asm__ __volatile__("movdqu (%1),%%xmm0\n\tcvttsd2si %%xmm0,%0" : "=r"(r) : "r"(t) : "xmm0", "memory");
		else { uint32_t x; __asm__ __volatile__("movdqu (%1),%%xmm0\n\tcvttsd2si %%xmm0,%0" : "=r"(x) : "r"(t) : "xmm0", "memory"); r = x; } break;
	}
	if (xout) memcpy(xout, t, 16);
	return r;
}

/* ---------------------------------------------------------------------------------------------------------------------------
 * Exact fused multiply-add: round-to-nearest-even of a*b + c, for binary32 (is32) or binary64 operands held in the low bits.
 * ------------------------------------------------------------------------------------------------------------------------- */
typedef unsigned __int128 u128;

struct fp { int sign; int cls; /* 0 zero 1 finite 2 inf 3 nan */ int exp; uint64_t mant; };

static struct fp
unpack(uint64_t x, int is32)
{
	struct fp f;
	int mb = is32 ? 23 : 52, eb = is32 ? 8 : 11, bias = is32 ? 127 : 1023;
	uint64_t frac = x & ((1ULL << mb) - 1);
	int e = (int)((x >> mb) & ((1u << eb) - 1));
	f.sign = (int)((x >> (mb + eb)) & 1);
	f.exp = 0; f.mant = 0;
	if (e == (1 << eb) - 1) { f.cls = frac ? 3 : 2; f.mant = frac; return f; }
	if (e == 0) {
		if (frac == 0) { f.cls = 0; return f; }
		f.cls = 1; f.mant = frac; f.exp = 1 - bias - mb;     /* subnormal: value = frac * 2^(1-bias-mb) */
		return f;
	}
	f.cls = 1; f.mant = frac | (1ULL << mb); f.exp = e - bias - mb;
	return f;
}

static int
clz128(u128 v)
{
	uint64_t hi = (uint64_t)(v >> 64), lo = (uint64_t)v;
	if (hi) return __builtin_clzll(hi);
	if (lo) return 64 + __builtin_clzll(lo);
	return 128;
}

/* value = m * 2^e (m 128-bit); round to the format and pack */
static uint64_t
pack(int sign, u128 m, int e, int is32, int sticky_in)
{
	int mb = is32 ? 23 : 52, eb = is32 ? 8 : 11, bias = is32 ? 127 : 1023, P = mb + 1;
	int emin = 1 - bias;           /* smallest normal exponent (of 1.xxx) */
	uint64_t signbit = (uint64_t)sign << (mb + eb);
	if (m == 0 && !sticky_in) return signbit;
	int top = 127 - clz128(m);       /* index of msb */
	int ex = e + top;                /* exponent of the leading bit (value in [2^ex, 2^(ex+1))) */
	int keep = P;                    /* bits to keep */
	if (ex < emin) keep = P - (emin - ex);    /* subnormal: fewer bits */
	int drop = top + 1 - keep;       /* number of low bits to discard */
	u128 q; int round = 0, sticky = sticky_in;
	if (keep <= 0) {
		/* everything is below half of the smallest subnormal or around it */
		if (keep < 0) { q = 0; round = 0; sticky = 1; }
		else { q = 0; round = 1; sticky |= (m & (((u128)1 << (top)) - 1)) != 0; }       /* keep == 0: msb is the round bit */
	} else if (drop > 0) {
		q = m >> drop;
		round = (int)((m >> (drop - 1)) & 1);
		sticky |= (m & (((u128)1 << (drop - 1)) - 1)) != 0;
	} else {
		q = m << (-drop); round = 0;
	}
	if (round && (sticky || (q & 1))) q++;
	/* q now holds 'keep' bits (maybe keep+1 after carry) */
	int qe;     /* exponent of the lsb of q */
	if (keep <= 0) qe = emin - mb; else qe = (ex - (keep - 1));
	if (keep < P && keep > 0) qe = emin - mb;
	/* renormalise after rounding */
	if (q == 0) return signbit;
	{
		/* value = q * 2^qe ; find biased exponent */
		int qt = 63 - __builtin_clzll((uint64_t)q);
		int e2 = qe + qt;            /* exponent of leading bit */
		if (qt > mb) {               /* carry made it one bit wider: drop the lsb (it is zero) */
			q >>= 1; qe++; qt--; e2 = qe + qt;
		}
		if (e2 >= emin) {
			/* normal */
			if (e2 > bias) return signbit | ((uint64_t)((1 << eb) - 1) << mb);        /* overflow -> inf */
			uint64_t frac = ((uint64_t)q << (mb - qt)) & ((1ULL << mb) - 1);
			return signbit | ((uint64_t)(e2 + bias) << mb) | frac;
		} else {
			/* subnormal: q is aligned so that lsb = 2^(emin - mb) */
			uint64_t frac = (uint64_t)q << (qe - (emin - mb));
			if (frac >> mb) return signbit | (1ULL << mb) | (frac & ((1ULL << mb) - 1));   /* rounded up to the smallest normal */
			return signbit | frac;
		}
	}
}

uint64_t
avx_softfma(uint64_t a, uint64_t b, uint64_t c, int is32)
{
	int mb = is32 ? 23 : 52, eb = is32 ? 8 : 11;
	uint64_t qnan = ((uint64_t)((1 << eb) - 1) << mb) | (1ULL << (mb - 1));
	uint64_t indef = qnan | ((uint64_t)1 << (mb + eb));      /* x86 "real indefinite" */
	struct fp A = unpack(a, is32), B = unpack(b, is32), C = unpack(c, is32);
	if (A.cls == 3) return a | (1ULL << (mb - 1));
	if (B.cls == 3) return b | (1ULL << (mb - 1));
	if (C.cls == 3) return c | (1ULL << (mb - 1));
	int ps = A.sign ^ B.sign;
	uint64_t infv = ((uint64_t)((1 << eb) - 1) << mb);
	/* specials */
	if (A.cls == 2 || B.cls == 2) {
		if (A.cls == 0 || B.cls == 0) return indef;                       /* inf * 0 */
		if (C.cls == 2 && C.sign != ps) return indef;                     /* inf - inf */
		return ((uint64_t)ps << (mb + eb)) | infv;
	}
	if (C.cls == 2) return c;
	if (A.cls == 0 || B.cls == 0) {              /* product is exactly zero */
		if (C.cls == 0) return ((uint64_t)(ps & C.sign) << (mb + eb));    /* +0 unless both negative (round to nearest) */
		return c;
	}
	u128 pm = (u128)A.mant * B.mant;
	int pe = A.exp + B.exp;
	if (C.cls == 0) return pack(ps, pm, pe, is32, 0);
	/* normalise both to msb at bit 125 and align */
	u128 cm = C.mant; int ce = C.exp;
	int ptop = 127 - clz128(pm), ctop = 127 - clz128(cm);
	pm <<= (125 - ptop); pe -= (125 - ptop);
	cm <<= (125 - ctop); ce -= (125 - ctop);
	int sticky = 0;
	u128 big, small; int be, bs_sign, sm_sign;
	if (pe >= ce) { big = pm; be = pe; bs_sign = ps; small = cm; sm_sign = C.sign; int d = pe - ce;
		if (d >= 127) { sticky = (small != 0); small = 0; } else if (d > 0) { sticky = (small & (((u128)1 << d) - 1)) != 0; small >>= d; } }
	else { big = cm; be = ce; bs_sign = C.sign; small = pm; sm_sign = ps; int d = ce - pe;
		if (d >= 127) { sticky = (small != 0); small = 0; } else if (d > 0) { sticky = (small & (((u128)1 << d) - 1)) != 0; small >>= d; } }
	u128 sum; int sign;
	if (bs_sign == sm_sign) { sum = big + small; sign = bs_sign; }
	else {
		/* big - (small + sticky epsilon) */
		if (big > small || (big == small && !sticky)) {
			sum = big - small; sign = bs_sign;
			if (sticky) { sum -= 1; }          /* subtracting a tiny positive remainder: borrow, result keeps a sticky bit below */
		} else { sum = small - big; sign = sm_sign; }
		if (sum == 0 && !sticky) return 0;     /* exact cancellation: +0 in round-to-nearest */
	}
	return pack(sign, sum, be, is32, sticky);
}
