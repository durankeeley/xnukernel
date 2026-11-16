/**
 * AVX / AVX2 / BMI / FMA instruction engine of the Opcode Emulator.
 * Pure C, no kernel dependencies (so it is compiled and tested on the host too): it decodes ONE VEX instruction (and legacy MOVBE),
 * executes it on a snapshot of the register file and returns its length. See opemu_avx.h for the interface; the kernel glue is opemu_avx_glue.c.
 * VEX.128 forms zero bits 255:128 of the destination, VEX.256 forms work per 128-bit lane unless the instruction crosses lanes.
 *
 * STATUS
 *  . Moves, logic, integer add/sub/compare/min/max/avg/mul/madd/sad, saturating ops, shifts (imm, xmm count, per-element), pack/unpack, shuffles
 *  .   (pshufb, palignr, pshufd/hw/lw, shufps/pd, permq/permd/perm2i128, permilps/pd), blends, broadcasts, insert/extract, pmovsx/zx, ptest/vtest are implemented.
 *  . Floating point (add/sub/mul/div/min/max/sqrt/rcp/rsqrt, hadd/hsub/addsub, cmp 0-31, comis, conversions, cvtsi2sd/cvt(t)sd2si) goes through opemu_avx_fp.c.
 *  . FMA (all 132/213/231 forms, ps/pd/ss/sd, fmaddsub/fmsubadd) is exact: round-to-nearest-even only, MXCSR rounding mode is ignored.
 *  . BMI1/BMI2 (andn bextr blsi blsmsk blsr bzhi mulx pdep pext rorx sarx shlx shrx) and MOVBE are implemented.
 *  . Host-tested against the real CPU by scripts/opemu-avxtest.sh: ~60,000 instruction forms, no mismatches.
 *  . NOT implemented: vroundps/pd/ss/sd, dpps/dppd, mpsadbw, pcmpistri/m family, AES-NI, PCLMULQDQ, F16C, phminposuw, gathers, EVEX/AVX-512.
 *  . TZCNT cannot be emulated (it runs as BSF on K8/K10 and differs only for a zero input); XGETBV/XSAVE users are not handled.
 *
 * HISTORY
 *  . XCROSS   New: VEX decoder and engine written for macOS Tahoe, whose userland is built for Haswell (x86_64h) only
 */
#include <stdint.h>
#include <string.h>
#include "opemu_avx.h"
#include "opemu_avx_fp.h"

typedef union {
	uint8_t  b[32];  int8_t  sb[32];
	uint16_t w[16];  int16_t sw[16];
	uint32_t d[8];   int32_t sd[8];
	uint64_t q[4];   int64_t sq[4];
} v256;

#define RF_CF 0x0001u
#define RF_PF 0x0004u
#define RF_AF 0x0010u
#define RF_ZF 0x0040u
#define RF_SF 0x0080u
#define RF_OF 0x0800u
#define RF_ARITH (RF_CF | RF_PF | RF_AF | RF_ZF | RF_SF | RF_OF)

struct vx {
	int      map, pp, W, L, R, X, B;
	unsigned vvvv;       /* already inverted */
	uint8_t  opc;
	uint8_t  modrm;
	int      mod, reg, rm;   /* reg and rm include the REX-like extension bits */
	int      ismem;
	uint64_t ea;
	int      hasimm;
	uint8_t  imm;
	unsigned len;
};

static inline int8_t  sat8(int v)   { return v > 127 ? 127 : v < -128 ? -128 : (int8_t)v; }
static inline uint8_t usat8(int v)  { return v > 255 ? 255 : v < 0 ? 0 : (uint8_t)v; }
static inline int16_t sat16(int v)  { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }
static inline uint16_t usat16(int v){ return v > 65535 ? 65535 : v < 0 ? 0 : (uint16_t)v; }

static void
set_flags(avx_ctx_t *c, uint32_t f)
{
	c->rflags = (c->rflags & ~(uint64_t)RF_ARITH) | f;
}

static uint32_t
parity(uint8_t v)
{
	v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
	return (v & 1) ? 0 : RF_PF;
}

static int decode_modrm(avx_ctx_t *c, const uint8_t *p, unsigned n, struct vx *v);
/* bits of the immediate / displacement needed to compute the length, and the effective address */
static int
decode(avx_ctx_t *c, const uint8_t *p, struct vx *v)
{
	unsigned n = 0;
	memset(v, 0, sizeof *v);
	if (p[0] == 0xC5) {
		uint8_t b1 = p[1];
		v->R = !(b1 & 0x80); v->X = 0; v->B = 0;
		v->vvvv = (~b1 >> 3) & 0xF; v->L = (b1 >> 2) & 1; v->pp = b1 & 3; v->map = 1; v->W = 0;
		n = 2;
	} else if (p[0] == 0xC4) {
		uint8_t b1 = p[1], b2 = p[2];
		v->R = !(b1 & 0x80); v->X = !(b1 & 0x40); v->B = !(b1 & 0x20); v->map = b1 & 0x1F;
		v->W = (b2 >> 7) & 1; v->vvvv = (~b2 >> 3) & 0xF; v->L = (b2 >> 2) & 1; v->pp = b2 & 3;
		n = 3;
	} else {
		return AVX_NOTVEX;
	}
	if (v->map < 1 || v->map > 3)
		return AVX_UNSUPPORTED;
	v->opc = p[n++];
	/* VZEROUPPER/ALL (0F 77) has no ModRM */
	if (v->map == 1 && v->opc == 0x77) {
		v->len = n;
		return AVX_OK;
	}
	return decode_modrm(c, p, n, v);
}

/* ModRM / SIB / displacement / immediate and the effective address; n = offset of the ModRM byte */
static int
decode_modrm(avx_ctx_t *c, const uint8_t *p, unsigned n, struct vx *v)
{
	v->modrm = p[n++];
	v->mod = v->modrm >> 6;
	v->reg = ((v->modrm >> 3) & 7) | (v->R << 3);
	v->rm = v->modrm & 7;
	int64_t disp = 0;
	int has_base = 1, rip_rel = 0, base = 0, index = -1, scale = 0;
	if (v->mod == 3) {
		v->rm |= v->B << 3;
		v->ismem = 0;
	} else {
		v->ismem = 1;
		if (v->rm == 4) {
			uint8_t sib = p[n++];
			scale = sib >> 6;
			int idx = ((sib >> 3) & 7) | (v->X << 3);
			index = (idx == 4) ? -1 : idx;
			base = (sib & 7) | (v->B << 3);
			if ((sib & 7) == 5 && v->mod == 0) { has_base = 0; disp = (int32_t)(p[n] | p[n+1] << 8 | p[n+2] << 16 | (uint32_t)p[n+3] << 24); n += 4; }
		} else if (v->rm == 5 && v->mod == 0) {
			rip_rel = 1; has_base = 0;
			disp = (int32_t)(p[n] | p[n+1] << 8 | p[n+2] << 16 | (uint32_t)p[n+3] << 24); n += 4;
		} else {
			base = v->rm | (v->B << 3);
		}
		if (v->mod == 1) { disp = (int8_t)p[n++]; }
		else if (v->mod == 2) { disp = (int32_t)(p[n] | p[n+1] << 8 | p[n+2] << 16 | (uint32_t)p[n+3] << 24); n += 4; }
		v->rm = 0;
	}
	/* does this opcode carry an imm8? */
	int imm = 0;
	if (v->map == 3) imm = 1;
	else if (v->map == 1 && (v->opc == 0x70 || v->opc == 0x71 || v->opc == 0x72 || v->opc == 0x73 ||
	    v->opc == 0xC2 || v->opc == 0xC4 || v->opc == 0xC5 || v->opc == 0xC6)) imm = 1;
	if (imm) { v->hasimm = 1; v->imm = p[n++]; }
	v->len = n;
	if (v->ismem) {
		uint64_t a = (uint64_t)disp;
		if (rip_rel) a += c->rip + n;
		else if (has_base) a += c->g[base];
		if (index >= 0) a += c->g[index] << scale;
		if (!c->is64) a &= 0xffffffffULL;
		v->ea = a;
	}
	return AVX_OK;
}


/* ---- rounding of a float/double held as raw bits to an integral value; mode 0 nearest-even, 1 down, 2 up, 3 truncate ---- */
static uint64_t
round_bits(uint64_t x, int is32, int mode)
{
	int mb = is32 ? 23 : 52, eb = is32 ? 8 : 11, bias = is32 ? 127 : 1023;
	uint64_t signbit = 1ULL << (mb + eb), sign = x & signbit;
	int neg = sign != 0;
	int e = (int)((x >> mb) & ((1u << eb) - 1)) - bias;
	uint64_t frac = x & ((1ULL << mb) - 1);
	if (e == (1 << eb) - 1 - bias) return frac ? (x | (1ULL << (mb - 1))) : x;      /* inf stays, NaN is quieted */
	if (e >= mb) return x;                                                              /* already integral */
	if ((x & ~signbit) == 0) return x;                                                  /* +-0 */
	if (e < 0) {                                                                        /* |x| < 1 */
		uint64_t one = (uint64_t)bias << mb;                                           /* 1.0 */
		int up;
		switch (mode) {
		case 0: up = (e == -1) && frac != 0; break;            /* > 0.5 -> 1, == 0.5 -> 0 (even), < 0.5 -> 0 */
		case 1: up = neg; break;
		case 2: up = !neg; break;
		default: up = 0; break;
		}
		return up ? (sign | one) : sign;
	}
	uint64_t fmask = (1ULL << (mb - e)) - 1;
	uint64_t fr = x & fmask, base = x & ~fmask, ulp = 1ULL << (mb - e), half = ulp >> 1;
	int up;
	switch (mode) {
	case 0: up = fr > half || (fr == half && (base & ulp)); break;
	case 1: up = neg && fr; break;
	case 2: up = !neg && fr; break;
	default: up = 0; break;
	}
	if (up) base += ulp;           /* a carry into the exponent field is exactly what we want */
	return base;
}

/* ---- half <-> single ---- */
static uint32_t
h2f(uint16_t h)
{
	uint32_t sign = (uint32_t)(h & 0x8000) << 16; int e = (h >> 10) & 31; uint32_t f = h & 0x3FF;
	if (e == 0) {
		if (f == 0) return sign;
		e = 1; while (!(f & 0x400)) { f <<= 1; e--; } f &= 0x3FF;            /* normalise the subnormal */
		return sign | ((uint32_t)(e + 112) << 23) | (f << 13);
	}
	if (e == 31) return sign | 0x7F800000u | (f ? (0x400000u | (f << 13)) : 0);
	return sign | ((uint32_t)(e + 112) << 23) | (f << 13);
}

static uint16_t
f2h(uint32_t x, int mode)
{
	uint16_t sign = (uint16_t)((x >> 16) & 0x8000); int neg = sign != 0;
	int e = (int)((x >> 23) & 255); uint32_t f = x & 0x7FFFFF;
	if (e == 255) return sign | 0x7C00 | (f ? (0x200 | (f >> 13)) : 0);
	int ue = e - 127;                                       /* unbiased */
	uint64_t m = f | (e ? 0x800000 : 0);                    /* 24-bit significand */
	if (e == 0) ue = -126;
	int shift;                                              /* bits to drop from m (24 -> 11) */
	if (ue >= -14) shift = 13; else shift = 13 + (-14 - ue);
	uint64_t q, rem, half;
	if (shift >= 40) { q = 0; rem = m; half = 1ULL << 39; }  /* far below the smallest subnormal */
	else { q = m >> shift; rem = m & ((1ULL << shift) - 1); half = 1ULL << (shift - 1); }
	int up;
	switch (mode) {
	case 0: up = rem > half || (rem == half && (q & 1)); break;
	case 1: up = neg && rem; break;
	case 2: up = !neg && rem; break;
	default: up = 0; break;
	}
	if (up) q++;
	if (ue >= -14) {
		int he = ue + 15;                                    /* biased half exponent */
		if (q >> 11) { q >>= 1; he++; }                      /* carry out of the significand */
		if (he >= 31) return sign | ((mode == 3 || (mode == 1 && !neg) || (mode == 2 && neg)) ? 0x7BFF : 0x7C00);
		return sign | (uint16_t)(he << 10) | (uint16_t)(q & 0x3FF);
	}
	/* subnormal half (q may have rounded up to the smallest normal) */
	return sign | (uint16_t)q;
}

/* ---- AES ---- */
static uint8_t aes_sb[256], aes_isb[256]; static int aes_ready;
static uint8_t rotl8(uint8_t x, int n) { return (uint8_t)((x << n) | (x >> (8 - n))); }
static void
aes_init(void)
{
	uint8_t p = 1, q = 1;
	do {
		p = p ^ (uint8_t)(p << 1) ^ ((p & 0x80) ? 0x1B : 0);
		q ^= q << 1; q ^= q << 2; q ^= q << 4; if (q & 0x80) q ^= 0x09;
		aes_sb[p] = q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4) ^ 0x63;
	} while (p != 1);
	aes_sb[0] = 0x63;
	for (int i = 0; i < 256; i++) aes_isb[aes_sb[i]] = (uint8_t)i;
	aes_ready = 1;
}
static uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
static uint8_t gm(uint8_t a, uint8_t b) { uint8_t r = 0; while (b) { if (b & 1) r ^= a; a = xt(a); b >>= 1; } return r; }
static void
aes_round(uint8_t *o, const uint8_t *in, int dec, int last)
{
	uint8_t t[16]; int r, cc;
	for (cc = 0; cc < 4; cc++) for (r = 0; r < 4; r++) {                 /* (Inv)ShiftRows then (Inv)SubBytes */
		int src = dec ? (cc - r) & 3 : (cc + r) & 3;
		t[r + 4 * cc] = dec ? aes_isb[in[r + 4 * src]] : aes_sb[in[r + 4 * src]];
	}
	if (last) { memcpy(o, t, 16); return; }
	for (cc = 0; cc < 4; cc++) {
		uint8_t *c4 = t + 4 * cc, a0 = c4[0], a1 = c4[1], a2 = c4[2], a3 = c4[3];
		if (!dec) {
			o[4 * cc + 0] = gm(a0, 2) ^ gm(a1, 3) ^ a2 ^ a3;
			o[4 * cc + 1] = a0 ^ gm(a1, 2) ^ gm(a2, 3) ^ a3;
			o[4 * cc + 2] = a0 ^ a1 ^ gm(a2, 2) ^ gm(a3, 3);
			o[4 * cc + 3] = gm(a0, 3) ^ a1 ^ a2 ^ gm(a3, 2);
		} else {
			o[4 * cc + 0] = gm(a0, 14) ^ gm(a1, 11) ^ gm(a2, 13) ^ gm(a3, 9);
			o[4 * cc + 1] = gm(a0, 9) ^ gm(a1, 14) ^ gm(a2, 11) ^ gm(a3, 13);
			o[4 * cc + 2] = gm(a0, 13) ^ gm(a1, 9) ^ gm(a2, 14) ^ gm(a3, 11);
			o[4 * cc + 3] = gm(a0, 11) ^ gm(a1, 13) ^ gm(a2, 9) ^ gm(a3, 14);
		}
	}
}
static void
aes_imc(uint8_t *o, const uint8_t *in)
{
	for (int cc = 0; cc < 4; cc++) {
		const uint8_t *c4 = in + 4 * cc;
		o[4 * cc + 0] = gm(c4[0], 14) ^ gm(c4[1], 11) ^ gm(c4[2], 13) ^ gm(c4[3], 9);
		o[4 * cc + 1] = gm(c4[0], 9) ^ gm(c4[1], 14) ^ gm(c4[2], 11) ^ gm(c4[3], 13);
		o[4 * cc + 2] = gm(c4[0], 13) ^ gm(c4[1], 9) ^ gm(c4[2], 14) ^ gm(c4[3], 11);
		o[4 * cc + 3] = gm(c4[0], 11) ^ gm(c4[1], 13) ^ gm(c4[2], 9) ^ gm(c4[3], 14);
	}
}

/* carry-less 64x64 -> 128 */
static void
clmul64(uint64_t a, uint64_t b, uint64_t *lo, uint64_t *hi)
{
	unsigned __int128 r = 0, aa = a;
	for (int i = 0; i < 64; i++) if ((b >> i) & 1) r ^= aa << i;
	*lo = (uint64_t)r; *hi = (uint64_t)(r >> 64);
}

/* ---- operand access ---- */
static int
ld(avx_ctx_t *c, const struct vx *v, void *dst, size_t n)
{
	return c->rd(c->user, v->ea, dst, n) ? AVX_FAULT : AVX_OK;
}
static int
st(avx_ctx_t *c, const struct vx *v, const void *src, size_t n)
{
	return c->wr(c->user, v->ea, src, n) ? AVX_FAULT : AVX_OK;
}

static void getv(avx_ctx_t *c, int r, v256 *o) { memcpy(o->b, c->y[r], 16); memcpy(o->b + 16, c->y[r] + 16, 16); }
/* write dst: L=0 zeroes the upper lane */
static void putv(avx_ctx_t *c, int r, const v256 *x, int L)
{
	memcpy(c->y[r], x->b, 16);
	if (L) memcpy(c->y[r] + 16, x->b + 16, 16); else memset(c->y[r] + 16, 0, 16);
}

/* load the r/m source as a 128/256-bit vector; n = bytes to read from memory (rest zero) */
static int
rmsrc(avx_ctx_t *c, const struct vx *v, v256 *o, size_t n)
{
	memset(o, 0, sizeof *o);
	if (!v->ismem) { getv(c, v->rm, o); return AVX_OK; }
	return ld(c, v, o->b, n);
}

#define NB (v->L ? 32 : 16)
#define NW (v->L ? 16 : 8)
#define ND (v->L ? 8 : 4)
#define NQ (v->L ? 4 : 2)

/* variable shifts by a count: count >= width gives 0 (or sign fill) */
static uint64_t shl_n(uint64_t x, unsigned bits, uint64_t cnt) { return cnt >= bits ? 0 : (bits == 64 ? x << cnt : (x << cnt)); }
static uint64_t shr_n(uint64_t x, unsigned bits, uint64_t cnt) { return cnt >= bits ? 0 : x >> cnt; }


/* legacy-encoded instructions that K8/K10 lack: MOVBE (0F 38 F0/F1) */
static int
legacy_exec(avx_ctx_t *c, const uint8_t *code, unsigned *len)
{
	unsigned n = 0; int opsz16 = 0, rex = 0;
	while (code[n] == 0x66 && n < 4) { opsz16 = 1; n++; }
	if ((code[n] & 0xF0) == 0x40) { rex = code[n++]; }
	if (!(code[n] == 0x0F && code[n + 1] == 0x38 && (code[n + 2] == 0xF0 || code[n + 2] == 0xF1))) return AVX_UNSUPPORTED;
	struct vx V, *v = &V;
	memset(v, 0, sizeof *v);
	v->R = (rex >> 2) & 1; v->X = (rex >> 1) & 1; v->B = rex & 1; v->W = (rex >> 3) & 1;
	v->map = 2; v->opc = code[n + 2];
	int rc = decode_modrm(c, code, n + 3, v);
	if (rc != AVX_OK) return rc;
	*len = v->len;
	unsigned sz = v->W ? 8 : (opsz16 ? 2 : 4);
	if (!v->ismem) return AVX_UNSUPPORTED;
	uint64_t val = 0, out = 0; int i;
	if (v->opc == 0xF0) {             /* movbe r, m */
		if (c->rd(c->user, v->ea, &val, sz)) return AVX_FAULT;
		for (i = 0; i < (int)sz; i++) out |= ((val >> (8 * i)) & 0xFF) << (8 * (sz - 1 - i));
		if (sz == 8) c->g[v->reg] = out;
		else if (sz == 4) c->g[v->reg] = (uint32_t)out;
		else c->g[v->reg] = (c->g[v->reg] & ~0xFFFFULL) | (uint16_t)out;
	} else {                          /* movbe m, r */
		val = c->g[v->reg];
		for (i = 0; i < (int)sz; i++) out |= ((val >> (8 * i)) & 0xFF) << (8 * (sz - 1 - i));
		if (c->wr(c->user, v->ea, &out, sz)) return AVX_FAULT;
	}
	return AVX_OK;
}

/* ---- the engine ---- */
int
avx_exec(avx_ctx_t *c, const uint8_t *code, unsigned *len)
{
	struct vx V, *v = &V;
	int rc = decode(c, code, v);
	if (rc == AVX_NOTVEX) return legacy_exec(c, code, len);
	if (rc != AVX_OK) return rc;
	*len = v->len;

	v256 s1, s2, r;
	memset(&r, 0, sizeof r);
	const int key = (v->map << 16) | (v->pp << 8) | v->opc;
	int i, l;

	/* ---- zero ---- */
	if (key == ((1 << 16) | 0x77)) {
		for (i = 0; i < 16; i++) { if (v->L) memset(c->y[i], 0, 32); else memset(c->y[i] + 16, 0, 16); }
		return AVX_OK;
	}

	getv(c, v->vvvv, &s1);

	/* helper macros that fall through to 'done' */
#define SRC2(n)  do { rc = rmsrc(c, v, &s2, (n)); if (rc) return rc; } while (0)
#define SRC2F    SRC2(NB)
#define DONE     do { putv(c, v->reg, &r, v->L); return AVX_OK; } while (0)
#define B8(expr)  do { SRC2F; for (i = 0; i < NB; i++) { uint8_t a = s1.b[i], b = s2.b[i]; int8_t sa = (int8_t)a, sb = (int8_t)b; (void)a;(void)b;(void)sa;(void)sb; r.b[i] = (expr); } DONE; } while (0)
#define W16(expr) do { SRC2F; for (i = 0; i < NW; i++) { uint16_t a = s1.w[i], b = s2.w[i]; int16_t sa = (int16_t)a, sb = (int16_t)b; (void)a;(void)b;(void)sa;(void)sb; r.w[i] = (expr); } DONE; } while (0)
#define D32(expr) do { SRC2F; for (i = 0; i < ND; i++) { uint32_t a = s1.d[i], b = s2.d[i]; int32_t sa = (int32_t)a, sb = (int32_t)b; (void)a;(void)b;(void)sa;(void)sb; r.d[i] = (expr); } DONE; } while (0)
#define Q64(expr) do { SRC2F; for (i = 0; i < NQ; i++) { uint64_t a = s1.q[i], b = s2.q[i]; int64_t sa = (int64_t)a, sb = (int64_t)b; (void)a;(void)b;(void)sa;(void)sb; r.q[i] = (expr); } DONE; } while (0)

	switch (key) {
	/* ================= moves ================= */
	case (1 << 16) | (0 << 8) | 0x10: case (1 << 16) | (1 << 8) | 0x10:    /* vmovups/vmovupd load */
	case (1 << 16) | (0 << 8) | 0x28: case (1 << 16) | (1 << 8) | 0x28:    /* vmovaps/vmovapd load */
	case (1 << 16) | (1 << 8) | 0x6F: case (1 << 16) | (2 << 8) | 0x6F:    /* vmovdqa / vmovdqu load */
	case (1 << 16) | (3 << 8) | 0xF0:                                       /* vlddqu */
		SRC2F; r = s2; DONE;
	case (1 << 16) | (0 << 8) | 0x11: case (1 << 16) | (1 << 8) | 0x11:    /* store forms */
	case (1 << 16) | (0 << 8) | 0x29: case (1 << 16) | (1 << 8) | 0x29:
	case (1 << 16) | (1 << 8) | 0x7F: case (1 << 16) | (2 << 8) | 0x7F:
	case (1 << 16) | (0 << 8) | 0x2B: case (1 << 16) | (1 << 8) | 0x2B:
	case (1 << 16) | (1 << 8) | 0xE7: {
		getv(c, v->reg, &r);
		if (v->ismem) return st(c, v, r.b, NB);
		/* register destination (rm): copy, zero upper for 128-bit */
		v256 d = r; putv(c, v->rm, &d, v->L); return AVX_OK;
	}
	case (1 << 16) | (2 << 8) | 0x10: case (1 << 16) | (3 << 8) | 0x10: {   /* vmovss / vmovsd */
		size_t n = (v->pp == 2) ? 4 : 8;
		if (v->ismem) { rc = ld(c, v, r.b, n); if (rc) return rc; /* upper bits zero */ }
		else { getv(c, v->rm, &s2); r = s1; memcpy(r.b, s2.b, n); r.q[2] = r.q[3] = 0; }
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (2 << 8) | 0x11: case (1 << 16) | (3 << 8) | 0x11: {
		size_t n = (v->pp == 2) ? 4 : 8;
		getv(c, v->reg, &s2);
		if (v->ismem) return st(c, v, s2.b, n);
		getv(c, v->rm, &r); r = (v256){{0}}; getv(c, v->vvvv, &r); memcpy(r.b, s2.b, n); r.q[2] = r.q[3] = 0;
		putv(c, v->rm, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (1 << 8) | 0x6E: {          /* vmovd/vmovq xmm, r/m */
		uint64_t val = 0;
		if (v->ismem) { rc = ld(c, v, &val, v->W ? 8 : 4); if (rc) return rc; }
		else val = v->W ? c->g[v->rm] : (uint32_t)c->g[v->rm];
		r.q[0] = val; putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (1 << 8) | 0x7E: {          /* vmovd/vmovq r/m, xmm */
		getv(c, v->reg, &s2);
		uint64_t val = v->W ? s2.q[0] : (uint32_t)s2.q[0];
		if (v->ismem) return st(c, v, &val, v->W ? 8 : 4);
		c->g[v->rm] = val; return AVX_OK;
	}
	case (1 << 16) | (2 << 8) | 0x7E: {          /* vmovq xmm, xmm/m64 */
		if (v->ismem) { rc = ld(c, v, &r.q[0], 8); if (rc) return rc; } else { getv(c, v->rm, &s2); r.q[0] = s2.q[0]; }
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (1 << 8) | 0xD6: {          /* vmovq xmm/m64, xmm */
		getv(c, v->reg, &s2);
		if (v->ismem) return st(c, v, &s2.q[0], 8);
		r.q[0] = s2.q[0]; putv(c, v->rm, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (1 << 8) | 0xD7: {          /* vpmovmskb r32, xmm/ymm */
		if (v->ismem) return AVX_UNSUPPORTED;
		getv(c, v->rm, &s2); uint32_t m = 0;
		for (i = 0; i < NB; i++) m |= (uint32_t)(s2.b[i] >> 7) << i;
		c->g[v->reg] = m; return AVX_OK;
	}
	case (1 << 16) | (0 << 8) | 0x50: case (1 << 16) | (1 << 8) | 0x50: {   /* vmovmskps/pd */
		if (v->ismem) return AVX_UNSUPPORTED;
		getv(c, v->rm, &s2); uint32_t m = 0;
		if (v->pp == 0) for (i = 0; i < ND; i++) m |= (s2.d[i] >> 31) << i;
		else for (i = 0; i < NQ; i++) m |= (uint32_t)(s2.q[i] >> 63) << i;
		c->g[v->reg] = m; return AVX_OK;
	}

	/* ================= bitwise ================= */
	case (1 << 16) | (0 << 8) | 0x54: case (1 << 16) | (1 << 8) | 0x54: case (1 << 16) | (1 << 8) | 0xDB: Q64(a & b);
	case (1 << 16) | (0 << 8) | 0x55: case (1 << 16) | (1 << 8) | 0x55: case (1 << 16) | (1 << 8) | 0xDF: Q64(~a & b);
	case (1 << 16) | (0 << 8) | 0x56: case (1 << 16) | (1 << 8) | 0x56: case (1 << 16) | (1 << 8) | 0xEB: Q64(a | b);
	case (1 << 16) | (0 << 8) | 0x57: case (1 << 16) | (1 << 8) | 0x57: case (1 << 16) | (1 << 8) | 0xEF: Q64(a ^ b);

	/* ================= integer add/sub/compare ================= */
	case (1 << 16) | (1 << 8) | 0xFC: B8(a + b);
	case (1 << 16) | (1 << 8) | 0xFD: W16(a + b);
	case (1 << 16) | (1 << 8) | 0xFE: D32(a + b);
	case (1 << 16) | (1 << 8) | 0xD4: Q64(a + b);
	case (1 << 16) | (1 << 8) | 0xF8: B8(a - b);
	case (1 << 16) | (1 << 8) | 0xF9: W16(a - b);
	case (1 << 16) | (1 << 8) | 0xFA: D32(a - b);
	case (1 << 16) | (1 << 8) | 0xFB: Q64(a - b);
	case (1 << 16) | (1 << 8) | 0xEC: B8(sat8(sa + sb));
	case (1 << 16) | (1 << 8) | 0xED: W16(sat16(sa + sb));
	case (1 << 16) | (1 << 8) | 0xDC: B8(usat8(a + b));
	case (1 << 16) | (1 << 8) | 0xDD: W16(usat16(a + b));
	case (1 << 16) | (1 << 8) | 0xE8: B8(sat8(sa - sb));
	case (1 << 16) | (1 << 8) | 0xE9: W16(sat16(sa - sb));
	case (1 << 16) | (1 << 8) | 0xD8: B8(usat8((int)a - (int)b));
	case (1 << 16) | (1 << 8) | 0xD9: W16(usat16((int)a - (int)b));
	case (1 << 16) | (1 << 8) | 0x74: B8(a == b ? 0xFF : 0);
	case (1 << 16) | (1 << 8) | 0x75: W16(a == b ? 0xFFFF : 0);
	case (1 << 16) | (1 << 8) | 0x76: D32(a == b ? 0xFFFFFFFFu : 0);
	case (1 << 16) | (1 << 8) | 0x64: B8(sa > sb ? 0xFF : 0);
	case (1 << 16) | (1 << 8) | 0x65: W16(sa > sb ? 0xFFFF : 0);
	case (1 << 16) | (1 << 8) | 0x66: D32(sa > sb ? 0xFFFFFFFFu : 0);
	case (1 << 16) | (1 << 8) | 0xDA: B8(a < b ? a : b);
	case (1 << 16) | (1 << 8) | 0xDE: B8(a > b ? a : b);
	case (1 << 16) | (1 << 8) | 0xEA: W16(sa < sb ? sa : sb);
	case (1 << 16) | (1 << 8) | 0xEE: W16(sa > sb ? sa : sb);
	case (1 << 16) | (1 << 8) | 0xE0: B8(((unsigned)a + b + 1) >> 1);
	case (1 << 16) | (1 << 8) | 0xE3: W16(((unsigned)a + b + 1) >> 1);
	case (1 << 16) | (1 << 8) | 0xD5: W16((uint16_t)((int)sa * sb));
	case (1 << 16) | (1 << 8) | 0xE5: W16((uint16_t)(((int)sa * sb) >> 16));
	case (1 << 16) | (1 << 8) | 0xE4: W16((uint16_t)(((unsigned)a * b) >> 16));
	case (2 << 16) | (1 << 8) | 0x40: D32(a * b);
	case (2 << 16) | (1 << 8) | 0x28: { SRC2F; for (i = 0; i < NQ; i++) r.sq[i] = (int64_t)s1.sd[2 * i] * s2.sd[2 * i]; DONE; }   /* vpmuldq */
	case (1 << 16) | (1 << 8) | 0xF4: { SRC2F; for (i = 0; i < NQ; i++) r.q[i] = (uint64_t)s1.d[2 * i] * s2.d[2 * i]; DONE; }        /* vpmuludq */
	case (1 << 16) | (1 << 8) | 0xF5: { SRC2F; for (i = 0; i < ND; i++) r.sd[i] = (int32_t)s1.sw[2 * i] * s2.sw[2 * i] + (int32_t)s1.sw[2 * i + 1] * s2.sw[2 * i + 1]; DONE; } /* vpmaddwd */
	case (1 << 16) | (1 << 8) | 0xF6: {                                                                                                  /* vpsadbw */
		SRC2F;
		for (i = 0; i < NQ; i++) { unsigned s = 0; for (int k = 0; k < 8; k++) { int d = (int)s1.b[8 * i + k] - (int)s2.b[8 * i + k]; s += d < 0 ? -d : d; } r.q[i] = s; }
		DONE;
	}
	case (2 << 16) | (1 << 8) | 0x38: B8(sa < sb ? sa : sb);
	case (2 << 16) | (1 << 8) | 0x39: D32(sa < sb ? sa : sb);
	case (2 << 16) | (1 << 8) | 0x3A: W16(a < b ? a : b);
	case (2 << 16) | (1 << 8) | 0x3B: D32(a < b ? a : b);
	case (2 << 16) | (1 << 8) | 0x3C: B8(sa > sb ? sa : sb);
	case (2 << 16) | (1 << 8) | 0x3D: D32(sa > sb ? sa : sb);
	case (2 << 16) | (1 << 8) | 0x3E: W16(a > b ? a : b);
	case (2 << 16) | (1 << 8) | 0x3F: D32(a > b ? a : b);
	case (2 << 16) | (1 << 8) | 0x29: Q64(a == b ? ~0ULL : 0);
	case (2 << 16) | (1 << 8) | 0x37: Q64(sa > sb ? ~0ULL : 0);
	case (2 << 16) | (1 << 8) | 0x1C: { SRC2F; for (i = 0; i < NB; i++) r.b[i] = s2.sb[i] < 0 ? -s2.sb[i] : s2.sb[i]; DONE; }
	case (2 << 16) | (1 << 8) | 0x1D: { SRC2F; for (i = 0; i < NW; i++) r.w[i] = s2.sw[i] < 0 ? -s2.sw[i] : s2.sw[i]; DONE; }
	case (2 << 16) | (1 << 8) | 0x1E: { SRC2F; for (i = 0; i < ND; i++) r.d[i] = s2.sd[i] < 0 ? -s2.sd[i] : s2.sd[i]; DONE; }
	case (2 << 16) | (1 << 8) | 0x08: B8(sb < 0 ? -sa : (sb == 0 ? 0 : sa));
	case (2 << 16) | (1 << 8) | 0x09: W16(sb < 0 ? -sa : (sb == 0 ? 0 : sa));
	case (2 << 16) | (1 << 8) | 0x0A: D32(sb < 0 ? -sa : (sb == 0 ? 0 : sa));
	case (2 << 16) | (1 << 8) | 0x0B: W16((uint16_t)(((((int)sa * sb) >> 14) + 1) >> 1));
	case (2 << 16) | (1 << 8) | 0x04: {                                                         /* vpmaddubsw */
		SRC2F;
		for (i = 0; i < NW; i++) r.sw[i] = sat16((int)s1.b[2 * i] * s2.sb[2 * i] + (int)s1.b[2 * i + 1] * s2.sb[2 * i + 1]);
		DONE;
	}

	/* ================= shuffles / permutes ================= */
	case (2 << 16) | (1 << 8) | 0x00: {                                                         /* vpshufb */
		SRC2F;
		for (l = 0; l < (v->L ? 2 : 1); l++)
			for (i = 0; i < 16; i++) { uint8_t m = s2.b[16 * l + i]; r.b[16 * l + i] = (m & 0x80) ? 0 : s1.b[16 * l + (m & 15)]; }
		DONE;
	}
	case (3 << 16) | (1 << 8) | 0x0F: {                                                         /* vpalignr */
		SRC2F;
		for (l = 0; l < (v->L ? 2 : 1); l++)
			for (i = 0; i < 16; i++) {
				unsigned k = i + v->imm;
				r.b[16 * l + i] = k < 16 ? s2.b[16 * l + k] : (k < 32 ? s1.b[16 * l + k - 16] : 0);
			}
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0x70: case (1 << 16) | (2 << 8) | 0x70: case (1 << 16) | (3 << 8) | 0x70: {   /* vpshufd/hw/lw */
		SRC2F;
		for (l = 0; l < (v->L ? 2 : 1); l++) {
			const uint8_t *s = s2.b + 16 * l; uint8_t *d = r.b + 16 * l;
			if (v->pp == 1) for (i = 0; i < 4; i++) memcpy(d + 4 * i, s + 4 * ((v->imm >> (2 * i)) & 3), 4);
			else if (v->pp == 3) { for (i = 0; i < 4; i++) memcpy(d + 2 * i, s + 2 * ((v->imm >> (2 * i)) & 3), 2); memcpy(d + 8, s + 8, 8); }
			else { memcpy(d, s, 8); for (i = 0; i < 4; i++) memcpy(d + 8 + 2 * i, s + 8 + 2 * ((v->imm >> (2 * i)) & 3), 2); }
		}
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0x60: case (1 << 16) | (1 << 8) | 0x68: {                      /* unpck l/h bw */
		SRC2F; int hi = v->opc == 0x68;
		for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 8; i++) { r.b[16 * l + 2 * i] = s1.b[16 * l + 8 * hi + i]; r.b[16 * l + 2 * i + 1] = s2.b[16 * l + 8 * hi + i]; }
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0x61: case (1 << 16) | (1 << 8) | 0x69: {                      /* wd */
		SRC2F; int hi = v->opc == 0x69;
		for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 4; i++) { r.w[8 * l + 2 * i] = s1.w[8 * l + 4 * hi + i]; r.w[8 * l + 2 * i + 1] = s2.w[8 * l + 4 * hi + i]; }
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0x62: case (1 << 16) | (1 << 8) | 0x6A: {                      /* dq */
		SRC2F; int hi = v->opc == 0x6A;
		for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 2; i++) { r.d[4 * l + 2 * i] = s1.d[4 * l + 2 * hi + i]; r.d[4 * l + 2 * i + 1] = s2.d[4 * l + 2 * hi + i]; }
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0x6C: case (1 << 16) | (1 << 8) | 0x6D: {                      /* qdq */
		SRC2F; int hi = v->opc == 0x6D;
		for (l = 0; l < (v->L ? 2 : 1); l++) { r.q[2 * l] = s1.q[2 * l + hi]; r.q[2 * l + 1] = s2.q[2 * l + hi]; }
		DONE;
	}
	case (1 << 16) | (0 << 8) | 0x14: case (1 << 16) | (0 << 8) | 0x15: {                      /* vunpcklps/hps */
		SRC2F; int hi = v->opc == 0x15;
		for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 2; i++) { r.d[4 * l + 2 * i] = s1.d[4 * l + 2 * hi + i]; r.d[4 * l + 2 * i + 1] = s2.d[4 * l + 2 * hi + i]; }
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0x14: case (1 << 16) | (1 << 8) | 0x15: {                      /* vunpcklpd/hpd */
		SRC2F; int hi = v->opc == 0x15;
		for (l = 0; l < (v->L ? 2 : 1); l++) { r.q[2 * l] = s1.q[2 * l + hi]; r.q[2 * l + 1] = s2.q[2 * l + hi]; }
		DONE;
	}
	case (1 << 16) | (0 << 8) | 0xC6: {                                                         /* vshufps */
		SRC2F;
		for (l = 0; l < (v->L ? 2 : 1); l++) {
			r.d[4 * l + 0] = s1.d[4 * l + (v->imm & 3)];      r.d[4 * l + 1] = s1.d[4 * l + ((v->imm >> 2) & 3)];
			r.d[4 * l + 2] = s2.d[4 * l + ((v->imm >> 4) & 3)]; r.d[4 * l + 3] = s2.d[4 * l + ((v->imm >> 6) & 3)];
		}
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0xC6: {                                                         /* vshufpd */
		SRC2F;
		for (l = 0; l < (v->L ? 2 : 1); l++) { r.q[2 * l] = s1.q[2 * l + ((v->imm >> (2 * l)) & 1)]; r.q[2 * l + 1] = s2.q[2 * l + ((v->imm >> (2 * l + 1)) & 1)]; }
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0x63: case (1 << 16) | (1 << 8) | 0x67: {                      /* vpacksswb / vpackuswb */
		SRC2F; int u = v->opc == 0x67;
		for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 8; i++) {
			r.b[16 * l + i]     = u ? usat8(s1.sw[8 * l + i]) : (uint8_t)sat8(s1.sw[8 * l + i]);
			r.b[16 * l + 8 + i] = u ? usat8(s2.sw[8 * l + i]) : (uint8_t)sat8(s2.sw[8 * l + i]);
		}
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0x6B: case (2 << 16) | (1 << 8) | 0x2B: {                      /* vpackssdw / vpackusdw */
		SRC2F; int u = v->map == 2;
		for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 4; i++) {
			r.w[8 * l + i]     = u ? usat16(s1.sd[4 * l + i]) : (uint16_t)sat16(s1.sd[4 * l + i]);
			r.w[8 * l + 4 + i] = u ? usat16(s2.sd[4 * l + i]) : (uint16_t)sat16(s2.sd[4 * l + i]);
		}
		DONE;
	}
	case (2 << 16) | (1 << 8) | 0x01: case (2 << 16) | (1 << 8) | 0x02: case (2 << 16) | (1 << 8) | 0x05: case (2 << 16) | (1 << 8) | 0x06:
	case (2 << 16) | (1 << 8) | 0x03: case (2 << 16) | (1 << 8) | 0x07: {                      /* vphadd / vphsub w,d,sw */
		SRC2F;
		int sub = (v->opc == 0x05 || v->opc == 0x06 || v->opc == 0x07);
		int dw = (v->opc == 0x02 || v->opc == 0x06), sw = (v->opc == 0x03 || v->opc == 0x07);
		for (l = 0; l < (v->L ? 2 : 1); l++) {
			if (dw) {
				for (i = 0; i < 2; i++) { r.d[4 * l + i] = sub ? s1.d[4 * l + 2 * i] - s1.d[4 * l + 2 * i + 1] : s1.d[4 * l + 2 * i] + s1.d[4 * l + 2 * i + 1]; }
				for (i = 0; i < 2; i++) { r.d[4 * l + 2 + i] = sub ? s2.d[4 * l + 2 * i] - s2.d[4 * l + 2 * i + 1] : s2.d[4 * l + 2 * i] + s2.d[4 * l + 2 * i + 1]; }
			} else {
				for (i = 0; i < 4; i++) {
					int a0 = s1.sw[8 * l + 2 * i], a1 = s1.sw[8 * l + 2 * i + 1], b0 = s2.sw[8 * l + 2 * i], b1 = s2.sw[8 * l + 2 * i + 1];
					int x = sub ? a0 - a1 : a0 + a1, y = sub ? b0 - b1 : b0 + b1;
					r.w[8 * l + i] = sw ? (uint16_t)sat16(x) : (uint16_t)x;
					r.w[8 * l + 4 + i] = sw ? (uint16_t)sat16(y) : (uint16_t)y;
				}
			}
		}
		DONE;
	}

	/* shifts by immediate (group opcodes): dst = vvvv, src = rm */
	case (1 << 16) | (1 << 8) | 0x71: case (1 << 16) | (1 << 8) | 0x72: case (1 << 16) | (1 << 8) | 0x73: {
		int g = (v->modrm >> 3) & 7; if (v->ismem) return AVX_UNSUPPORTED;
		getv(c, v->rm, &s2); unsigned n = v->imm; int d = v->vvvv;
		if (v->opc == 0x71) {
			for (i = 0; i < NW; i++) r.w[i] = g == 2 ? (n > 15 ? 0 : s2.w[i] >> n) : g == 4 ? (uint16_t)(s2.sw[i] >> (n > 15 ? 15 : n)) : g == 6 ? (n > 15 ? 0 : (uint16_t)(s2.w[i] << n)) : 0;
			if (g != 2 && g != 4 && g != 6) return AVX_UNSUPPORTED;
		} else if (v->opc == 0x72) {
			for (i = 0; i < ND; i++) r.d[i] = g == 2 ? (n > 31 ? 0 : s2.d[i] >> n) : g == 4 ? (uint32_t)(s2.sd[i] >> (n > 31 ? 31 : n)) : g == 6 ? (n > 31 ? 0 : s2.d[i] << n) : 0;
			if (g != 2 && g != 4 && g != 6) return AVX_UNSUPPORTED;
		} else {
			if (g == 2 || g == 6) { for (i = 0; i < NQ; i++) r.q[i] = g == 2 ? (n > 63 ? 0 : s2.q[i] >> n) : (n > 63 ? 0 : s2.q[i] << n); }
			else if (g == 3 || g == 7) {      /* byte shifts per 128-bit lane */
				for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 16; i++) {
					int k = g == 3 ? i + (int)n : i - (int)n;
					r.b[16 * l + i] = (k < 0 || k > 15 || n > 15) ? 0 : s2.b[16 * l + k];
				}
			} else return AVX_UNSUPPORTED;
		}
		putv(c, d, &r, v->L); return AVX_OK;
	}
	/* shifts by xmm count */
	case (1 << 16) | (1 << 8) | 0xD1: case (1 << 16) | (1 << 8) | 0xD2: case (1 << 16) | (1 << 8) | 0xD3:
	case (1 << 16) | (1 << 8) | 0xE1: case (1 << 16) | (1 << 8) | 0xE2:
	case (1 << 16) | (1 << 8) | 0xF1: case (1 << 16) | (1 << 8) | 0xF2: case (1 << 16) | (1 << 8) | 0xF3: {
		v256 cnt; rc = rmsrc(c, v, &cnt, 16); if (rc) return rc;
		uint64_t n = cnt.q[0];
		switch (v->opc) {
		case 0xD1: for (i = 0; i < NW; i++) r.w[i] = n > 15 ? 0 : s1.w[i] >> n; break;
		case 0xD2: for (i = 0; i < ND; i++) r.d[i] = n > 31 ? 0 : s1.d[i] >> n; break;
		case 0xD3: for (i = 0; i < NQ; i++) r.q[i] = n > 63 ? 0 : s1.q[i] >> n; break;
		case 0xE1: for (i = 0; i < NW; i++) r.w[i] = (uint16_t)(s1.sw[i] >> (n > 15 ? 15 : n)); break;
		case 0xE2: for (i = 0; i < ND; i++) r.d[i] = (uint32_t)(s1.sd[i] >> (n > 31 ? 31 : n)); break;
		case 0xF1: for (i = 0; i < NW; i++) r.w[i] = n > 15 ? 0 : (uint16_t)(s1.w[i] << n); break;
		case 0xF2: for (i = 0; i < ND; i++) r.d[i] = n > 31 ? 0 : s1.d[i] << n; break;
		case 0xF3: for (i = 0; i < NQ; i++) r.q[i] = n > 63 ? 0 : s1.q[i] << n; break;
		}
		DONE;
	}
	/* AVX2 per-element variable shifts */
	case (2 << 16) | (1 << 8) | 0x45: case (2 << 16) | (1 << 8) | 0x46: case (2 << 16) | (1 << 8) | 0x47: {
		SRC2F;
		if (v->W) { for (i = 0; i < NQ; i++) { uint64_t n = s2.q[i]; r.q[i] = v->opc == 0x45 ? shr_n(s1.q[i], 64, n) : v->opc == 0x47 ? shl_n(s1.q[i], 64, n) : 0; if (v->opc == 0x46) return AVX_UNSUPPORTED; } }
		else for (i = 0; i < ND; i++) {
			uint32_t n = s2.d[i];
			r.d[i] = v->opc == 0x45 ? (uint32_t)shr_n(s1.d[i], 32, n) : v->opc == 0x47 ? (uint32_t)shl_n(s1.d[i], 32, n) : (uint32_t)(s1.sd[i] >> (n > 31 ? 31 : n));
		}
		DONE;
	}

	/* ================= cross-lane / insert / extract / broadcast ================= */
	case (3 << 16) | (1 << 8) | 0x46: case (3 << 16) | (1 << 8) | 0x06: {                      /* vperm2i128 / vperm2f128 */
		SRC2F;
		for (l = 0; l < 2; l++) {
			unsigned s = (v->imm >> (4 * l)) & 0xF;
			if (s & 8) { memset(r.b + 16 * l, 0, 16); continue; }
			const uint8_t *src = (s & 2) ? s2.b : s1.b;
			memcpy(r.b + 16 * l, src + 16 * (s & 1), 16);
		}
		DONE;
	}
	case (3 << 16) | (1 << 8) | 0x00: case (3 << 16) | (1 << 8) | 0x01: {                      /* vpermq / vpermpd */
		SRC2F; for (i = 0; i < 4; i++) r.q[i] = s2.q[(v->imm >> (2 * i)) & 3]; DONE;
	}
	case (2 << 16) | (1 << 8) | 0x36: case (2 << 16) | (1 << 8) | 0x16: {                      /* vpermd / vpermps */
		SRC2F; for (i = 0; i < 8; i++) r.d[i] = s2.d[s1.d[i] & 7]; DONE;
	}
	case (3 << 16) | (1 << 8) | 0x02: {                                                         /* vpblendd */
		SRC2F; for (i = 0; i < ND; i++) r.d[i] = ((v->imm >> i) & 1) ? s2.d[i] : s1.d[i]; DONE;
	}
	case (3 << 16) | (1 << 8) | 0x0E: {                                                         /* vpblendw */
		SRC2F; for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 8; i++) r.w[8 * l + i] = ((v->imm >> i) & 1) ? s2.w[8 * l + i] : s1.w[8 * l + i]; DONE;
	}
	case (3 << 16) | (1 << 8) | 0x4C: {                                                         /* vpblendvb */
		SRC2F; v256 m; getv(c, v->imm >> 4, &m); for (i = 0; i < NB; i++) r.b[i] = (m.b[i] & 0x80) ? s2.b[i] : s1.b[i]; DONE;
	}
	case (3 << 16) | (1 << 8) | 0x38: case (3 << 16) | (1 << 8) | 0x18: {                      /* vinserti128 / vinsertf128 */
		if (!v->L) return AVX_UNSUPPORTED;
		v256 t; rc = rmsrc(c, v, &t, 16); if (rc) return rc;
		r = s1; memcpy(r.b + 16 * (v->imm & 1), t.b, 16); DONE;
	}
	case (3 << 16) | (1 << 8) | 0x39: case (3 << 16) | (1 << 8) | 0x19: {                      /* vextracti128 / vextractf128 */
		if (!v->L) return AVX_UNSUPPORTED;
		getv(c, v->reg, &s2);
		if (v->ismem) return st(c, v, s2.b + 16 * (v->imm & 1), 16);
		memcpy(r.b, s2.b + 16 * (v->imm & 1), 16); putv(c, v->rm, &r, 0); return AVX_OK;
	}
	case (2 << 16) | (1 << 8) | 0x5A: case (2 << 16) | (1 << 8) | 0x1A: {                      /* vbroadcasti128 / vbroadcastf128 (mem only) */
		if (!v->ismem) return AVX_UNSUPPORTED;
		rc = ld(c, v, r.b, 16); if (rc) return rc; memcpy(r.b + 16, r.b, 16); putv(c, v->reg, &r, 1); return AVX_OK;
	}
	case (2 << 16) | (1 << 8) | 0x78: case (2 << 16) | (1 << 8) | 0x79: case (2 << 16) | (1 << 8) | 0x58:
	case (2 << 16) | (1 << 8) | 0x59: case (2 << 16) | (1 << 8) | 0x18: case (2 << 16) | (1 << 8) | 0x19: {   /* vpbroadcastb/w/d/q, vbroadcastss/sd */
		unsigned esz = (v->opc == 0x78) ? 1 : (v->opc == 0x79) ? 2 : (v->opc == 0x58 || v->opc == 0x18) ? 4 : 8;
		uint8_t e[8] = { 0 };
		if (v->ismem) { rc = ld(c, v, e, esz); if (rc) return rc; } else { getv(c, v->rm, &s2); memcpy(e, s2.b, esz); }
		for (i = 0; i < (int)(NB / esz); i++) memcpy(r.b + esz * i, e, esz);
		DONE;
	}
	case (2 << 16) | (1 << 8) | 0x20: case (2 << 16) | (1 << 8) | 0x21: case (2 << 16) | (1 << 8) | 0x22:
	case (2 << 16) | (1 << 8) | 0x23: case (2 << 16) | (1 << 8) | 0x24: case (2 << 16) | (1 << 8) | 0x25:
	case (2 << 16) | (1 << 8) | 0x30: case (2 << 16) | (1 << 8) | 0x31: case (2 << 16) | (1 << 8) | 0x32:
	case (2 << 16) | (1 << 8) | 0x33: case (2 << 16) | (1 << 8) | 0x34: case (2 << 16) | (1 << 8) | 0x35: {   /* vpmovsx / vpmovzx */
		int sx = v->opc < 0x30; unsigned k = v->opc & 0xF;       /* 0 bw 1 bd 2 bq 3 wd 4 wq 5 dq */
		static const unsigned from[6] = { 1, 1, 1, 2, 2, 4 }, to[6] = { 2, 4, 8, 4, 8, 8 };
		unsigned fs = from[k], ts = to[k], cnt = NB / ts;
		v256 t; rc = rmsrc(c, v, &t, fs * cnt); if (rc) return rc;
		for (i = 0; i < (int)cnt; i++) {
			int64_t val = 0; uint64_t uv = 0;
			switch (fs) {
			case 1: val = t.sb[i]; uv = t.b[i]; break;
			case 2: val = t.sw[i]; uv = t.w[i]; break;
			case 4: val = t.sd[i]; uv = t.d[i]; break;
			}
			uint64_t x = sx ? (uint64_t)val : uv;
			memcpy(r.b + ts * i, &x, ts);
		}
		DONE;
	}
	case (2 << 16) | (1 << 8) | 0x17: {                                                         /* vptest */
		SRC2F; getv(c, v->reg, &s1);
		int zf = 1, cf = 1;
		for (i = 0; i < NB; i++) { if (s1.b[i] & s2.b[i]) zf = 0; if (~s1.b[i] & s2.b[i]) cf = 0; }
		set_flags(c, (zf ? RF_ZF : 0) | (cf ? RF_CF : 0)); return AVX_OK;
	}
	case (3 << 16) | (1 << 8) | 0x14: case (3 << 16) | (1 << 8) | 0x15: case (3 << 16) | (1 << 8) | 0x16: {   /* vpextrb/w/d/q */
		getv(c, v->reg, &s2); uint64_t val; unsigned sz;
		if (v->opc == 0x14) { val = s2.b[v->imm & 15]; sz = 1; }
		else if (v->opc == 0x15) { val = s2.w[v->imm & 7]; sz = 2; }
		else if (v->W) { val = s2.q[v->imm & 1]; sz = 8; }
		else { val = s2.d[v->imm & 3]; sz = 4; }
		if (v->ismem) return st(c, v, &val, sz);
		c->g[v->rm] = val; return AVX_OK;
	}
	case (3 << 16) | (1 << 8) | 0x20: case (3 << 16) | (1 << 8) | 0x22: {                      /* vpinsrb/d/q */
		r = s1; uint64_t val = 0; unsigned sz = v->opc == 0x20 ? 1 : (v->W ? 8 : 4);
		if (v->ismem) { rc = ld(c, v, &val, sz); if (rc) return rc; } else val = c->g[v->rm];
		memcpy(r.b + sz * (v->imm & (16 / sz - 1)), &val, sz); putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (1 << 8) | 0xC4: {                                                         /* vpinsrw */
		r = s1; uint16_t val = 0;
		if (v->ismem) { rc = ld(c, v, &val, 2); if (rc) return rc; } else val = (uint16_t)c->g[v->rm];
		r.w[v->imm & 7] = val; putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (1 << 8) | 0xC5: {                                                         /* vpextrw r32, xmm, imm */
		if (v->ismem) return AVX_UNSUPPORTED;
		getv(c, v->rm, &s2); c->g[v->reg] = s2.w[v->imm & 7]; return AVX_OK;
	}


	/* ================= floating point ================= */
	case (1 << 16) | (0 << 8) | 0x51: case (1 << 16) | (1 << 8) | 0x51: case (1 << 16) | (2 << 8) | 0x51: case (1 << 16) | (3 << 8) | 0x51:
	case (1 << 16) | (0 << 8) | 0x52: case (1 << 16) | (2 << 8) | 0x52:
	case (1 << 16) | (0 << 8) | 0x53: case (1 << 16) | (2 << 8) | 0x53:
	case (1 << 16) | (0 << 8) | 0x58: case (1 << 16) | (1 << 8) | 0x58: case (1 << 16) | (2 << 8) | 0x58: case (1 << 16) | (3 << 8) | 0x58:
	case (1 << 16) | (0 << 8) | 0x59: case (1 << 16) | (1 << 8) | 0x59: case (1 << 16) | (2 << 8) | 0x59: case (1 << 16) | (3 << 8) | 0x59:
	case (1 << 16) | (0 << 8) | 0x5C: case (1 << 16) | (1 << 8) | 0x5C: case (1 << 16) | (2 << 8) | 0x5C: case (1 << 16) | (3 << 8) | 0x5C:
	case (1 << 16) | (0 << 8) | 0x5D: case (1 << 16) | (1 << 8) | 0x5D: case (1 << 16) | (2 << 8) | 0x5D: case (1 << 16) | (3 << 8) | 0x5D:
	case (1 << 16) | (0 << 8) | 0x5E: case (1 << 16) | (1 << 8) | 0x5E: case (1 << 16) | (2 << 8) | 0x5E: case (1 << 16) | (3 << 8) | 0x5E:
	case (1 << 16) | (0 << 8) | 0x5F: case (1 << 16) | (1 << 8) | 0x5F: case (1 << 16) | (2 << 8) | 0x5F: case (1 << 16) | (3 << 8) | 0x5F:
	case (1 << 16) | (1 << 8) | 0x7C: case (1 << 16) | (3 << 8) | 0x7C: case (1 << 16) | (1 << 8) | 0x7D: case (1 << 16) | (3 << 8) | 0x7D:
	case (1 << 16) | (1 << 8) | 0xD0: case (1 << 16) | (3 << 8) | 0xD0: {
		int kind, flavor = v->pp;
		switch (v->opc) {
		case 0x51: kind = FP_SQRT; break; case 0x52: kind = FP_RSQRT; break; case 0x53: kind = FP_RCP; break;
		case 0x58: kind = FP_ADD; break; case 0x59: kind = FP_MUL; break; case 0x5C: kind = FP_SUB; break;
		case 0x5D: kind = FP_MIN; break; case 0x5E: kind = FP_DIV; break; case 0x5F: kind = FP_MAX; break;
		case 0x7C: kind = FP_HADD; flavor = (v->pp == 1); break; case 0x7D: kind = FP_HSUB; flavor = (v->pp == 1); break;
		default:   kind = FP_ADDSUB; flavor = (v->pp == 1); break;
		}
		int scalar = flavor >= 2 && v->opc < 0x7C;
		SRC2(scalar ? (flavor == 2 ? 4 : 8) : NB);
		if (scalar) {
			if (!avx_fp128(kind, flavor, r.b, s1.b, s2.b)) return AVX_UNSUPPORTED;
			putv(c, v->reg, &r, 0); return AVX_OK;
		}
		for (l = 0; l < (v->L ? 2 : 1); l++)
			if (!avx_fp128(kind, flavor, r.b + 16 * l, s1.b + 16 * l, s2.b + 16 * l)) return AVX_UNSUPPORTED;
		DONE;
	}
	case (1 << 16) | (0 << 8) | 0xC2: case (1 << 16) | (1 << 8) | 0xC2: case (1 << 16) | (2 << 8) | 0xC2: case (1 << 16) | (3 << 8) | 0xC2: {
		int scalar = v->pp >= 2;
		SRC2(scalar ? (v->pp == 2 ? 4 : 8) : NB);
		if (scalar) { avx_fp_cmp(v->pp, r.b, s1.b, s2.b, v->imm & 31); putv(c, v->reg, &r, 0); return AVX_OK; }
		for (l = 0; l < (v->L ? 2 : 1); l++) avx_fp_cmp(v->pp, r.b + 16 * l, s1.b + 16 * l, s2.b + 16 * l, v->imm & 31);
		DONE;
	}
	case (1 << 16) | (0 << 8) | 0x2E: case (1 << 16) | (1 << 8) | 0x2E: case (1 << 16) | (0 << 8) | 0x2F: case (1 << 16) | (1 << 8) | 0x2F: {
		SRC2(v->pp == 1 ? 8 : 4); getv(c, v->reg, &s1);
		set_flags(c, avx_fp_comis(v->pp == 1, s1.b, s2.b)); return AVX_OK;
	}
	case (1 << 16) | (0 << 8) | 0x5B: case (1 << 16) | (1 << 8) | 0x5B: case (1 << 16) | (2 << 8) | 0x5B: {
		SRC2F; int k = v->pp == 0 ? FP_CVTDQ2PS : v->pp == 1 ? FP_CVTPS2DQ : FP_CVTTPS2DQ;
		for (l = 0; l < (v->L ? 2 : 1); l++) if (!avx_fp_cvt(k, r.b + 16 * l, s2.b + 16 * l)) return AVX_UNSUPPORTED;
		DONE;
	}
	case (1 << 16) | (0 << 8) | 0x5A: case (1 << 16) | (1 << 8) | 0x5A: {      /* vcvtps2pd / vcvtpd2ps */
		if (v->pp == 0) {          /* ps -> pd : source 64/128 bits, result 128/256 bits */
			SRC2(v->L ? 16 : 8);
			if (!avx_fp_cvt(FP_CVTPS2PD, r.b, s2.b)) return AVX_UNSUPPORTED;
			if (v->L) { uint8_t t[16]; memcpy(t, s2.b + 8, 8); memset(t + 8, 0, 8); if (!avx_fp_cvt(FP_CVTPS2PD, r.b + 16, t)) return AVX_UNSUPPORTED; }
			DONE;
		} else {                   /* pd -> ps : source 128/256 bits, result 64/128 bits in xmm */
			SRC2F;
			uint8_t t[16];
			if (!avx_fp_cvt(FP_CVTPD2PS, t, s2.b)) return AVX_UNSUPPORTED; memcpy(r.b, t, 8);
			if (v->L) { if (!avx_fp_cvt(FP_CVTPD2PS, t, s2.b + 16)) return AVX_UNSUPPORTED; memcpy(r.b + 8, t, 8); }
			putv(c, v->reg, &r, 0); return AVX_OK;
		}
	}
	case (1 << 16) | (2 << 8) | 0x5A: case (1 << 16) | (3 << 8) | 0x5A: {                         /* vcvtss2sd / vcvtsd2ss */
		SRC2(v->pp == 2 ? 4 : 8);
		if (!avx_fp128(v->pp == 2 ? FP_CVTSS2SD : FP_CVTSD2SS, v->pp, r.b, s1.b, s2.b)) return AVX_UNSUPPORTED;
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (2 << 8) | 0xE6: {                                                         /* vcvtdq2pd */
		SRC2(v->L ? 16 : 8);
		if (!avx_fp_cvt(FP_CVTDQ2PD, r.b, s2.b)) return AVX_UNSUPPORTED;
		if (v->L) { uint8_t t[16]; memcpy(t, s2.b + 8, 8); memset(t + 8, 0, 8); if (!avx_fp_cvt(FP_CVTDQ2PD, r.b + 16, t)) return AVX_UNSUPPORTED; }
		DONE;
	}
	case (1 << 16) | (1 << 8) | 0xE6: case (1 << 16) | (3 << 8) | 0xE6: {                         /* vcvttpd2dq / vcvtpd2dq */
		SRC2F; uint8_t t[16]; int k = v->pp == 1 ? FP_CVTTPD2DQ : FP_CVTPD2DQ;
		if (!avx_fp_cvt(k, t, s2.b)) return AVX_UNSUPPORTED; memcpy(r.b, t, 8);
		if (v->L) { if (!avx_fp_cvt(k, t, s2.b + 16)) return AVX_UNSUPPORTED; memcpy(r.b + 8, t, 8); }
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (2 << 8) | 0x2A: case (1 << 16) | (3 << 8) | 0x2A: {                         /* vcvtsi2ss / vcvtsi2sd */
		uint64_t g = 0;
		if (v->ismem) { rc = ld(c, v, &g, v->W ? 8 : 4); if (rc) return rc; } else g = v->W ? c->g[v->rm] : (uint32_t)c->g[v->rm];
		avx_fp_cvt_gpr(v->pp == 2 ? 0 : 1, v->W, s1.b, g, r.b);
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (2 << 8) | 0x2C: case (1 << 16) | (3 << 8) | 0x2C: case (1 << 16) | (2 << 8) | 0x2D: case (1 << 16) | (3 << 8) | 0x2D: {
		SRC2(v->pp == 2 ? 4 : 8);
		int k = (v->opc == 0x2C ? 4 : 2) + (v->pp == 3);
		uint64_t x = avx_fp_cvt_gpr(k, v->W, s2.b, 0, NULL);
		c->g[v->reg] = v->W ? x : (uint32_t)x; return AVX_OK;
	}
	/* ---- vmovddup / vmovsldup / vmovshdup ---- */
	case (1 << 16) | (3 << 8) | 0x12: {
		SRC2(v->L ? 32 : 8);
		for (l = 0; l < (v->L ? 2 : 1); l++) { r.q[2 * l] = s2.q[2 * l]; r.q[2 * l + 1] = s2.q[2 * l]; }
		DONE;
	}
	case (1 << 16) | (2 << 8) | 0x12: case (1 << 16) | (2 << 8) | 0x16: {
		SRC2F; int hi = v->opc == 0x16;
		for (i = 0; i < ND / 2; i++) { r.d[2 * i] = s2.d[2 * i + hi]; r.d[2 * i + 1] = s2.d[2 * i + hi]; }
		DONE;
	}
	/* ---- vmovlps/hps/lpd/hpd ---- */
	case (1 << 16) | (0 << 8) | 0x12: case (1 << 16) | (1 << 8) | 0x12: {                         /* vmovlps/vmovlpd xmm, xmm, m64 (reg form is vmovhlps) */
		r = s1;
		if (v->ismem) { rc = ld(c, v, &r.q[0], 8); if (rc) return rc; }
		else { if (v->pp) return AVX_UNSUPPORTED; getv(c, v->rm, &s2); r.q[0] = s2.q[1]; }
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (0 << 8) | 0x16: case (1 << 16) | (1 << 8) | 0x16: {                         /* vmovhps/vmovhpd (reg form is vmovlhps) */
		r = s1;
		if (v->ismem) { rc = ld(c, v, &r.q[1], 8); if (rc) return rc; }
		else { if (v->pp) return AVX_UNSUPPORTED; getv(c, v->rm, &s2); r.q[1] = s2.q[0]; }
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (1 << 16) | (0 << 8) | 0x13: case (1 << 16) | (1 << 8) | 0x13: case (1 << 16) | (0 << 8) | 0x17: case (1 << 16) | (1 << 8) | 0x17: {   /* stores */
		if (!v->ismem) return AVX_UNSUPPORTED;
		getv(c, v->reg, &s2); return st(c, v, &s2.q[v->opc == 0x17], 8);
	}
	/* ---- permute-in-lane, test, masked moves ---- */
	case (2 << 16) | (1 << 8) | 0x0C: {                                                         /* vpermilps (variable) */
		SRC2F; for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 4; i++) r.d[4 * l + i] = s1.d[4 * l + (s2.d[4 * l + i] & 3)]; DONE;
	}
	case (2 << 16) | (1 << 8) | 0x0D: {                                                         /* vpermilpd (variable) */
		SRC2F; for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 2; i++) r.q[2 * l + i] = s1.q[2 * l + ((s2.q[2 * l + i] >> 1) & 1)]; DONE;
	}
	case (3 << 16) | (1 << 8) | 0x04: {                                                         /* vpermilps imm */
		SRC2F; for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 4; i++) r.d[4 * l + i] = s2.d[4 * l + ((v->imm >> (2 * i)) & 3)]; DONE;
	}
	case (3 << 16) | (1 << 8) | 0x05: {                                                         /* vpermilpd imm */
		SRC2F; for (l = 0; l < (v->L ? 2 : 1); l++) for (i = 0; i < 2; i++) r.q[2 * l + i] = s2.q[2 * l + ((v->imm >> (2 * l + i)) & 1)]; DONE;
	}
	case (2 << 16) | (1 << 8) | 0x0E: case (2 << 16) | (1 << 8) | 0x0F: {                      /* vtestps / vtestpd */
		SRC2F; getv(c, v->reg, &s1); int zf = 1, cf = 1;
		if (v->opc == 0x0E) { for (i = 0; i < ND; i++) { if ((s1.d[i] & s2.d[i]) >> 31) zf = 0; if ((~s1.d[i] & s2.d[i]) >> 31) cf = 0; } }
		else { for (i = 0; i < NQ; i++) { if ((s1.q[i] & s2.q[i]) >> 63) zf = 0; if ((~s1.q[i] & s2.q[i]) >> 63) cf = 0; } }
		set_flags(c, (zf ? RF_ZF : 0) | (cf ? RF_CF : 0)); return AVX_OK;
	}
	case (2 << 16) | (1 << 8) | 0x2C: case (2 << 16) | (1 << 8) | 0x2D: case (2 << 16) | (1 << 8) | 0x8C: {       /* masked loads: vmaskmovps/pd, vpmaskmovd/q */
		if (!v->ismem) return AVX_UNSUPPORTED;
		int e = (v->opc == 0x2C || (v->opc == 0x8C && !v->W)) ? 4 : 8, n = NB / e;
		for (i = 0; i < n; i++) {
			uint64_t m = (e == 4) ? s1.d[i] >> 31 : s1.q[i] >> 63;
			if (m) { uint8_t t[8]; rc = c->rd(c->user, v->ea + (uint64_t)e * i, t, e) ? AVX_FAULT : AVX_OK; if (rc) return rc; memcpy(r.b + e * i, t, e); }
		}
		DONE;
	}
	case (2 << 16) | (1 << 8) | 0x2E: case (2 << 16) | (1 << 8) | 0x2F: case (2 << 16) | (1 << 8) | 0x8E: {       /* masked stores */
		if (!v->ismem) return AVX_UNSUPPORTED;
		getv(c, v->reg, &s2);
		int e = (v->opc == 0x2E || (v->opc == 0x8E && !v->W)) ? 4 : 8, n = NB / e;
		for (i = 0; i < n; i++) {
			uint64_t m = (e == 4) ? s1.d[i] >> 31 : s1.q[i] >> 63;
			if (m) { if (c->wr(c->user, v->ea + (uint64_t)e * i, s2.b + e * i, e)) return AVX_FAULT; }
		}
		return AVX_OK;
	}
	/* ---- vinsertps / vextractps ---- */
	case (3 << 16) | (1 << 8) | 0x21: {
		uint32_t val;
		if (v->ismem) { rc = ld(c, v, &val, 4); if (rc) return rc; r = s1; }
		else { getv(c, v->rm, &s2); val = s2.d[(v->imm >> 6) & 3]; r = s1; }
		unsigned dst = (v->imm >> 4) & 3; r.d[dst] = val;
		for (i = 0; i < 4; i++) if ((v->imm >> i) & 1) r.d[i] = 0;
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (3 << 16) | (1 << 8) | 0x17: {
		getv(c, v->reg, &s2); uint32_t val = s2.d[v->imm & 3];
		if (v->ismem) return st(c, v, &val, 4);
		c->g[v->rm] = val; return AVX_OK;
	}
	/* ---- FMA ---- */
	case (2 << 16) | (1 << 8) | 0x96: case (2 << 16) | (1 << 8) | 0x97: case (2 << 16) | (1 << 8) | 0x98: case (2 << 16) | (1 << 8) | 0x99:
	case (2 << 16) | (1 << 8) | 0x9A: case (2 << 16) | (1 << 8) | 0x9B: case (2 << 16) | (1 << 8) | 0x9C: case (2 << 16) | (1 << 8) | 0x9D:
	case (2 << 16) | (1 << 8) | 0x9E: case (2 << 16) | (1 << 8) | 0x9F:
	case (2 << 16) | (1 << 8) | 0xA6: case (2 << 16) | (1 << 8) | 0xA7: case (2 << 16) | (1 << 8) | 0xA8: case (2 << 16) | (1 << 8) | 0xA9:
	case (2 << 16) | (1 << 8) | 0xAA: case (2 << 16) | (1 << 8) | 0xAB: case (2 << 16) | (1 << 8) | 0xAC: case (2 << 16) | (1 << 8) | 0xAD:
	case (2 << 16) | (1 << 8) | 0xAE: case (2 << 16) | (1 << 8) | 0xAF:
	case (2 << 16) | (1 << 8) | 0xB6: case (2 << 16) | (1 << 8) | 0xB7: case (2 << 16) | (1 << 8) | 0xB8: case (2 << 16) | (1 << 8) | 0xB9:
	case (2 << 16) | (1 << 8) | 0xBA: case (2 << 16) | (1 << 8) | 0xBB: case (2 << 16) | (1 << 8) | 0xBC: case (2 << 16) | (1 << 8) | 0xBD:
	case (2 << 16) | (1 << 8) | 0xBE: case (2 << 16) | (1 << 8) | 0xBF: {
		unsigned form = v->opc >> 4, op = v->opc & 15;     /* form 9: 132, A: 213, B: 231 */
		int scalar = (op & 1) && op >= 9;
		int is32 = !v->W, esz = is32 ? 4 : 8;
		v256 o1, o2, o3; getv(c, v->reg, &o1); o2 = s1;
		SRC2(scalar ? esz : NB); o3 = s2;
		int nel = scalar ? 1 : NB / esz;
		int negmul = (op == 0xC || op == 0xD || op == 0xE || op == 0xF), negadd = (op == 0xA || op == 0xB || op == 0xE || op == 0xF);
		int addsub = (op == 6), subadd = (op == 7);
		uint64_t sbit = is32 ? 0x80000000ULL : 0x8000000000000000ULL;
		r = o1;
		for (i = 0; i < nel; i++) {
			uint64_t x, y, z, e1 = 0, e2 = 0, e3 = 0;
			memcpy(&e1, o1.b + esz * i, esz); memcpy(&e2, o2.b + esz * i, esz); memcpy(&e3, o3.b + esz * i, esz);
			if (form == 9) { x = e1; y = e3; z = e2; } else if (form == 0xA) { x = e2; y = e1; z = e3; } else { x = e2; y = e3; z = e1; }
			int na = negadd, nm = negmul;
			if (addsub) na = !(i & 1) ? 1 : 0;        /* even: subtract, odd: add */
			if (subadd) na = (i & 1) ? 1 : 0;
			{   /* a NaN operand is returned as is: negation only applies to numbers */
				uint64_t em = is32 ? 0x7F800000ULL : 0x7FF0000000000000ULL, fm = is32 ? 0x7FFFFFULL : 0xFFFFFFFFFFFFFULL;
				if (nm && !((x & em) == em && (x & fm))) x ^= sbit;
				if (na && !((z & em) == em && (z & fm))) z ^= sbit;
			}
			uint64_t res = avx_softfma(x, y, z, is32);
			memcpy(r.b + esz * i, &res, esz);
		}
		putv(c, v->reg, &r, scalar ? 0 : v->L); return AVX_OK;
	}

	case (3 << 16) | (1 << 8) | 0x0C: { SRC2F; for (i = 0; i < ND; i++) r.d[i] = ((v->imm >> i) & 1) ? s2.d[i] : s1.d[i]; DONE; }        /* vblendps */
	case (3 << 16) | (1 << 8) | 0x0D: { SRC2F; for (i = 0; i < NQ; i++) r.q[i] = ((v->imm >> i) & 1) ? s2.q[i] : s1.q[i]; DONE; }        /* vblendpd */
	case (3 << 16) | (1 << 8) | 0x4A: { SRC2F; v256 m; getv(c, v->imm >> 4, &m); for (i = 0; i < ND; i++) r.d[i] = (m.d[i] >> 31) ? s2.d[i] : s1.d[i]; DONE; }
	case (3 << 16) | (1 << 8) | 0x4B: { SRC2F; v256 m; getv(c, v->imm >> 4, &m); for (i = 0; i < NQ; i++) r.q[i] = (m.q[i] >> 63) ? s2.q[i] : s1.q[i]; DONE; }

	/* ================= MXCSR ================= */
	case (1 << 16) | (0 << 8) | 0xAE: {
		int g = (v->modrm >> 3) & 7;
		if (!v->ismem || (g != 2 && g != 3)) return AVX_UNSUPPORTED;
		if (g == 3) return st(c, v, &c->mxcsr, 4);
		uint32_t m; rc = ld(c, v, &m, 4); if (rc) return rc;
		c->mxcsr = m; c->mxcsr_dirty = 1; return AVX_OK;
	}
	/* ================= VROUND ================= */
	case (3 << 16) | (1 << 8) | 0x08: case (3 << 16) | (1 << 8) | 0x09: case (3 << 16) | (1 << 8) | 0x0A: case (3 << 16) | (1 << 8) | 0x0B: {
		int scalar = v->opc >= 0x0A, is32 = (v->opc & 1) == 0 || v->opc == 0x08;
		is32 = (v->opc == 0x08 || v->opc == 0x0A);
		int mode = (v->imm & 4) ? (int)((c->mxcsr >> 13) & 3) : (v->imm & 3);
		unsigned esz = is32 ? 4 : 8;
		SRC2(scalar ? esz : NB);
		if (scalar) {
			r = s1; uint64_t x = 0; memcpy(&x, s2.b, esz); x = round_bits(x, is32, mode); memcpy(r.b, &x, esz);
			putv(c, v->reg, &r, 0); return AVX_OK;
		}
		for (i = 0; i < (int)(NB / esz); i++) { uint64_t x = 0; memcpy(&x, s2.b + esz * i, esz); x = round_bits(x, is32, mode); memcpy(r.b + esz * i, &x, esz); }
		DONE;
	}
	/* ================= DPPS / DPPD ================= */
	case (3 << 16) | (1 << 8) | 0x40: {
		SRC2F;
		for (l = 0; l < (v->L ? 2 : 1); l++) {
			uint8_t m[16], h1[16], h2[16];
			if (!avx_fp128(FP_MUL, 0, m, s1.b + 16 * l, s2.b + 16 * l)) return AVX_UNSUPPORTED;
			for (i = 0; i < 4; i++) if (!((v->imm >> (4 + i)) & 1)) memset(m + 4 * i, 0, 4);
			avx_fp128(FP_HADD, 0, h1, m, m); avx_fp128(FP_HADD, 0, h2, h1, h1);
			for (i = 0; i < 4; i++) { if ((v->imm >> i) & 1) memcpy(r.b + 16 * l + 4 * i, h2, 4); else memset(r.b + 16 * l + 4 * i, 0, 4); }
		}
		DONE;
	}
	case (3 << 16) | (1 << 8) | 0x41: {
		SRC2(16);
		uint8_t m[16], h[16];
		if (!avx_fp128(FP_MUL, 1, m, s1.b, s2.b)) return AVX_UNSUPPORTED;
		for (i = 0; i < 2; i++) if (!((v->imm >> (4 + i)) & 1)) memset(m + 8 * i, 0, 8);
		avx_fp128(FP_HADD, 1, h, m, m);
		for (i = 0; i < 2; i++) { if ((v->imm >> i) & 1) memcpy(r.b + 8 * i, h, 8); else memset(r.b + 8 * i, 0, 8); }
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	/* ================= F16C ================= */
	case (2 << 16) | (1 << 8) | 0x13: {                                               /* vcvtph2ps */
		SRC2(v->L ? 16 : 8);
		for (i = 0; i < (v->L ? 8 : 4); i++) r.d[i] = h2f(s2.w[i]);
		DONE;
	}
	case (3 << 16) | (1 << 8) | 0x1D: {                                               /* vcvtps2ph */
		getv(c, v->reg, &s2);
		int mode = (v->imm & 4) ? (int)((c->mxcsr >> 13) & 3) : (v->imm & 3);
		for (i = 0; i < (v->L ? 8 : 4); i++) r.w[i] = f2h(s2.d[i], mode);
		if (v->ismem) return st(c, v, r.b, v->L ? 16 : 8);
		putv(c, v->rm, &r, 0); return AVX_OK;
	}
	/* ================= AES-NI (VEX) ================= */
	case (2 << 16) | (1 << 8) | 0xDC: case (2 << 16) | (1 << 8) | 0xDD: case (2 << 16) | (1 << 8) | 0xDE: case (2 << 16) | (1 << 8) | 0xDF: {
		SRC2F; if (!aes_ready) aes_init();
		int dec = v->opc >= 0xDE, last = v->opc & 1;
		for (l = 0; l < (v->L ? 2 : 1); l++) {
			aes_round(r.b + 16 * l, s1.b + 16 * l, dec, last);
			for (i = 0; i < 16; i++) r.b[16 * l + i] ^= s2.b[16 * l + i];
		}
		DONE;
	}
	case (2 << 16) | (1 << 8) | 0xDB: {                                               /* vaesimc */
		SRC2(16); if (!aes_ready) aes_init(); aes_imc(r.b, s2.b); putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (3 << 16) | (1 << 8) | 0xDF: {                                               /* vaeskeygenassist */
		SRC2(16); if (!aes_ready) aes_init();
		uint32_t x1 = s2.d[1], x3 = s2.d[3], w1 = 0, w3 = 0;
		for (i = 0; i < 4; i++) { w1 |= (uint32_t)aes_sb[(x1 >> (8 * i)) & 255] << (8 * i); w3 |= (uint32_t)aes_sb[(x3 >> (8 * i)) & 255] << (8 * i); }
		r.d[0] = w1; r.d[1] = ((w1 >> 8) | (w1 << 24)) ^ v->imm; r.d[2] = w3; r.d[3] = ((w3 >> 8) | (w3 << 24)) ^ v->imm;
		putv(c, v->reg, &r, 0); return AVX_OK;
	}
	/* ================= PCLMULQDQ / PHMINPOSUW / MPSADBW ================= */
	case (3 << 16) | (1 << 8) | 0x44: {
		SRC2F;
		for (l = 0; l < (v->L ? 2 : 1); l++) clmul64(s1.q[2 * l + (v->imm & 1)], s2.q[2 * l + ((v->imm >> 4) & 1)], &r.q[2 * l], &r.q[2 * l + 1]);
		DONE;
	}
	case (2 << 16) | (1 << 8) | 0x41: {
		SRC2(16); uint16_t mn = s2.w[0]; int pos = 0;
		for (i = 1; i < 8; i++) if (s2.w[i] < mn) { mn = s2.w[i]; pos = i; }
		r.w[0] = mn; r.w[1] = (uint16_t)pos; putv(c, v->reg, &r, 0); return AVX_OK;
	}
	case (3 << 16) | (1 << 8) | 0x42: {
		SRC2F;
		for (l = 0; l < (v->L ? 2 : 1); l++) {
			unsigned b2 = 4 * ((v->imm >> (3 * l)) & 3), b1 = 4 * ((v->imm >> (3 * l + 2)) & 1);
			for (i = 0; i < 8; i++) {
				unsigned sum = 0;
				for (int j = 0; j < 4; j++) { int d = (int)s1.b[16 * l + b1 + i + j] - (int)s2.b[16 * l + b2 + j]; sum += d < 0 ? -d : d; }
				r.w[8 * l + i] = (uint16_t)sum;
			}
		}
		DONE;
	}

	/* ================= BMI1 / BMI2 (GPR) ================= */
	case (2 << 16) | (0 << 8) | 0xF2: case (2 << 16) | (0 << 8) | 0xF3: case (2 << 16) | (0 << 8) | 0xF5: case (2 << 16) | (0 << 8) | 0xF7:
	case (2 << 16) | (1 << 8) | 0xF7: case (2 << 16) | (2 << 8) | 0xF5: case (2 << 16) | (2 << 8) | 0xF7:
	case (2 << 16) | (3 << 8) | 0xF5: case (2 << 16) | (3 << 8) | 0xF6: case (2 << 16) | (3 << 8) | 0xF7:
	case (3 << 16) | (3 << 8) | 0xF0: {
		unsigned bits = v->W ? 64 : 32;
		uint64_t mask = v->W ? ~0ULL : 0xFFFFFFFFULL;
		uint64_t src = 0, vv = c->g[v->vvvv] & mask, res = 0;
		if (v->ismem) { rc = ld(c, v, &src, bits / 8); if (rc) return rc; } else src = c->g[v->rm] & mask;
		uint32_t fl = 0; int setf = 0;
		switch (key) {
		case (2 << 16) | (0 << 8) | 0xF2:      /* andn */
			res = ~vv & src & mask; setf = 1; fl = 0; break;
		case (2 << 16) | (0 << 8) | 0xF3: {    /* blsr/blsmsk/blsi (dst = vvvv) */
			int g = (v->modrm >> 3) & 7; setf = 1;
			if (g == 1) { res = src & (src - 1) & mask; fl = (src == 0) ? RF_CF : 0; }
			else if (g == 2) { res = (src ^ (src - 1)) & mask; fl = (src == 0) ? RF_CF : 0; }
			else if (g == 3) { res = src & (0 - src) & mask; fl = (src != 0) ? RF_CF : 0; }
			else return AVX_UNSUPPORTED;
			uint64_t top = v->W ? (res >> 63) : (res >> 31);
			fl |= (res == 0 ? RF_ZF : 0) | (top & 1 ? RF_SF : 0);
			set_flags(c, fl);
			c->g[v->vvvv] = res;
			return AVX_OK;
		}
		case (2 << 16) | (0 << 8) | 0xF5: {    /* bzhi: dst = reg, src = rm, idx = vvvv[7:0] */
			unsigned idx = vv & 0xFF; setf = 1;
			res = idx >= bits ? src : (src & ((1ULL << idx) - 1));
			fl = idx >= bits ? RF_CF : 0; break;
		}
		case (2 << 16) | (0 << 8) | 0xF7: {    /* bextr: dst = reg, src = rm, ctrl = vvvv */
			unsigned start = vv & 0xFF, ln = (vv >> 8) & 0xFF; setf = 1;
			uint64_t t = start >= bits ? 0 : src >> start;
			res = ln >= bits ? t : (t & ((1ULL << ln) - 1)); res &= mask; break;
		}
		case (2 << 16) | (1 << 8) | 0xF7: res = (uint64_t)(src << ((vv) & (bits - 1))) & mask; break;      /* shlx */
		case (2 << 16) | (3 << 8) | 0xF7: res = (src >> ((vv) & (bits - 1))) & mask; break;               /* shrx */
		case (2 << 16) | (2 << 8) | 0xF7: {                                                               /* sarx */
			unsigned n = vv & (bits - 1);
			res = v->W ? (uint64_t)((int64_t)src >> n) : (uint32_t)((int32_t)src >> n); break;
		}
		case (2 << 16) | (3 << 8) | 0xF5: {    /* pdep: dst = reg, src = vvvv, mask = rm */
			uint64_t m = src, s = vv, out = 0; unsigned k = 0;
			for (unsigned b = 0; b < bits; b++) if ((m >> b) & 1) { if ((s >> k) & 1) out |= 1ULL << b; k++; }
			res = out; break;
		}
		case (2 << 16) | (2 << 8) | 0xF5: {    /* pext */
			uint64_t m = src, s = vv, out = 0; unsigned k = 0;
			for (unsigned b = 0; b < bits; b++) if ((m >> b) & 1) { if ((s >> b) & 1) out |= 1ULL << k; k++; }
			res = out; break;
		}
		case (2 << 16) | (3 << 8) | 0xF6: {    /* mulx: hi -> reg, lo -> vvvv, multiplicand rdx */
			uint64_t d = c->g[2] & mask, hi, lo;
			if (v->W) { __uint128_t p = (__uint128_t)d * src; lo = (uint64_t)p; hi = (uint64_t)(p >> 64); }
			else { uint64_t p = (uint64_t)(uint32_t)d * (uint32_t)src; lo = (uint32_t)p; hi = p >> 32; }
			c->g[v->vvvv] = lo; c->g[v->reg] = hi; return AVX_OK;
		}
		case (3 << 16) | (3 << 8) | 0xF0: {    /* rorx: dst = reg, src = rm, imm */
			unsigned n = v->imm & (bits - 1);
			res = n ? (((src >> n) | (src << (bits - n))) & mask) : src; break;
		}
		}
		if (setf) {
			uint64_t top = v->W ? (res >> 63) : (res >> 31);
			if (key == ((2 << 16) | (0 << 8) | 0xF2) || key == ((2 << 16) | (0 << 8) | 0xF5)) fl |= (res == 0 ? RF_ZF : 0) | ((top & 1) ? RF_SF : 0);
			else fl = (res == 0 ? RF_ZF : 0);      /* bextr: ZF only, CF=OF=0 */
			set_flags(c, fl);
		}
		c->g[v->reg] = res;   /* 32-bit results zero-extend */
		(void)parity;
		return AVX_OK;
	}
	default:
		break;
	}
	return AVX_UNSUPPORTED;
}
