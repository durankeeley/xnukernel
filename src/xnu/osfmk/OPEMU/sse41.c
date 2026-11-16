/*
 * SSE4.1 (+ POPCNT, CRC32) emulation for CPUs that lack them (AMD K8/K10).
 *
 * xcross: new. Written from Intel's SDM and verified bit-for-bit against the hardware instructions by
 * scripts/opemu-hosttest.sh. Each instruction is a small routine on an ssse3_t (dst = first operand, src = second operand);
 * op_sse41_run() does the operand decoding (registers, memory with the correct size, GPRs) and the write-back.
 *
 * Not implemented: AVX and later (userland only uses them when the capability bits say so, which they do not on K8/K10),
 * segment-override addressing (fs:/gs:), and the MMX-register forms (SSE4.1 has none).
 */
#include <stdint.h>
#include <string.h>
#include "ssse3_priv.h"

#define RF_CF	0x0001
#define RF_PF	0x0004
#define RF_AF	0x0010
#define RF_ZF	0x0040
#define RF_SF	0x0080
#define RF_OF	0x0800
#define RF_ARITH (RF_CF | RF_PF | RF_AF | RF_ZF | RF_SF | RF_OF)

/* ------------------------------------------------------------------ blends */
void s41_pblendw(ssse3_t *t) { uint8_t m = t->udo_imm->lval.ubyte; for (int i = 0; i < 8; i++) t->res.uint16[i] = (m >> i & 1) ? t->src.uint16[i] : t->dst.uint16[i]; }
void s41_blendps(ssse3_t *t) { uint8_t m = t->udo_imm->lval.ubyte; for (int i = 0; i < 4; i++) t->res.uint32[i] = (m >> i & 1) ? t->src.uint32[i] : t->dst.uint32[i]; }
void s41_blendpd(ssse3_t *t) { uint8_t m = t->udo_imm->lval.ubyte; for (int i = 0; i < 2; i++) t->res.uint64[i] = (m >> i & 1) ? t->src.uint64[i] : t->dst.uint64[i]; }
void s41_pblendvb(ssse3_t *t) { for (int i = 0; i < 16; i++) t->res.uint8[i] = (t->xmm0.uint8[i] & 0x80) ? t->src.uint8[i] : t->dst.uint8[i]; }
void s41_blendvps(ssse3_t *t) { for (int i = 0; i < 4; i++) t->res.uint32[i] = (t->xmm0.uint32[i] >> 31) ? t->src.uint32[i] : t->dst.uint32[i]; }
void s41_blendvpd(ssse3_t *t) { for (int i = 0; i < 2; i++) t->res.uint64[i] = (t->xmm0.uint64[i] >> 63) ? t->src.uint64[i] : t->dst.uint64[i]; }

/* ------------------------------------------------------------ dot products */
void s41_dpps(ssse3_t *t)
{
	uint8_t m = t->udo_imm->lval.ubyte;
	float tmp[4], sum, d[4], s[4], r[4];
	memcpy(d, &t->dst, 16); memcpy(s, &t->src, 16);
	for (int i = 0; i < 4; i++) tmp[i] = (m & (0x10 << i)) ? d[i] * s[i] : 0.0f;
	sum = (tmp[0] + tmp[1]) + (tmp[2] + tmp[3]);
	for (int i = 0; i < 4; i++) r[i] = (m & (1 << i)) ? sum : 0.0f;
	memcpy(&t->res, r, 16);
}
void s41_dppd(ssse3_t *t)
{
	uint8_t m = t->udo_imm->lval.ubyte;
	double tmp[2], sum, d[2], s[2], r[2];
	memcpy(d, &t->dst, 16); memcpy(s, &t->src, 16);
	for (int i = 0; i < 2; i++) tmp[i] = (m & (0x10 << i)) ? d[i] * s[i] : 0.0;
	sum = tmp[0] + tmp[1];
	for (int i = 0; i < 2; i++) r[i] = (m & (1 << i)) ? sum : 0.0;
	memcpy(&t->res, r, 16);
}

/* ----------------------------------------------------------- insert/extract */
void s41_insertps(ssse3_t *t)
{
	uint8_t m = t->udo_imm->lval.ubyte;
	int cs = t->extended ? 0 : (m >> 6) & 3;	/* extended: source is a 32-bit memory operand */
	int cd = (m >> 4) & 3;
	t->res = t->dst;
	t->res.uint32[cd] = t->src.uint32[cs];
	for (int i = 0; i < 4; i++) if (m >> i & 1) t->res.uint32[i] = 0;
}
void s41_pinsrb(ssse3_t *t) { t->res = t->dst; t->res.uint8[t->udo_imm->lval.ubyte & 15] = (uint8_t)t->gpr_in; }
void s41_pinsrd(ssse3_t *t) { t->res = t->dst; t->res.uint32[t->udo_imm->lval.ubyte & 3] = (uint32_t)t->gpr_in; }
void s41_pinsrq(ssse3_t *t) { t->res = t->dst; t->res.uint64[t->udo_imm->lval.ubyte & 1] = t->gpr_in; }
/* the xmm operand of the extract forms is passed in src */
void s41_pextrb(ssse3_t *t) { t->gpr_out = t->src.uint8[t->udo_imm->lval.ubyte & 15]; }
void s41_pextrd(ssse3_t *t) { t->gpr_out = t->src.uint32[t->udo_imm->lval.ubyte & 3]; }
void s41_pextrq(ssse3_t *t) { t->gpr_out = t->src.uint64[t->udo_imm->lval.ubyte & 1]; }

/* ----------------------------------------------------------- integer misc */
void s41_mpsadbw(ssse3_t *t)
{
	uint8_t m = t->udo_imm->lval.ubyte;
	int so = (m & 3) * 4, dof = ((m >> 2) & 1) * 4;
	for (int i = 0; i < 8; i++) {
		int sum = 0;
		for (int j = 0; j < 4; j++) {
			int a = t->dst.uint8[dof + i + j], b = t->src.uint8[so + j];
			sum += a > b ? a - b : b - a;
		}
		t->res.uint16[i] = (uint16_t)sum;
	}
}
void s41_packusdw(ssse3_t *t)
{
	for (int i = 0; i < 4; i++) {
		int32_t a = t->dst.int32[i], b = t->src.int32[i];
		t->res.uint16[i]     = a < 0 ? 0 : a > 65535 ? 65535 : (uint16_t)a;
		t->res.uint16[i + 4] = b < 0 ? 0 : b > 65535 ? 65535 : (uint16_t)b;
	}
}
void s41_pcmpeqq(ssse3_t *t) { for (int i = 0; i < 2; i++) t->res.uint64[i] = t->dst.uint64[i] == t->src.uint64[i] ? ~0ULL : 0; }
void s41_phminposuw(ssse3_t *t)
{
	uint16_t min = t->src.uint16[0]; int idx = 0;
	for (int i = 1; i < 8; i++) if (t->src.uint16[i] < min) { min = t->src.uint16[i]; idx = i; }
	t->res.uint128 = 0; t->res.uint16[0] = min; t->res.uint16[1] = (uint16_t)idx;
}
#define MINMAX(name, T, f, op) void name(ssse3_t *t) { for (int i = 0; i < (int)(16 / sizeof(T)); i++) \
	t->res.f[i] = (t->dst.f[i] op t->src.f[i]) ? t->dst.f[i] : t->src.f[i]; }
MINMAX(s41_pmaxsb, int8_t,   int8,   >)
MINMAX(s41_pmaxsd, int32_t,  int32,  >)
MINMAX(s41_pmaxuw, uint16_t, uint16, >)
MINMAX(s41_pmaxud, uint32_t, uint32, >)
MINMAX(s41_pminsb, int8_t,   int8,   <)
MINMAX(s41_pminsd, int32_t,  int32,  <)
MINMAX(s41_pminuw, uint16_t, uint16, <)
MINMAX(s41_pminud, uint32_t, uint32, <)

void s41_pmuldq(ssse3_t *t) { t->res.int64[0] = (int64_t)t->dst.int32[0] * t->src.int32[0]; t->res.int64[1] = (int64_t)t->dst.int32[2] * t->src.int32[2]; }
void s41_pmulld(ssse3_t *t) { for (int i = 0; i < 4; i++) t->res.uint32[i] = t->dst.uint32[i] * t->src.uint32[i]; }
void s41_movntdqa(ssse3_t *t) { t->res = t->src; }

/* sign/zero extension: element counts and sizes from the mnemonic */
#define PMOV(name, STYPE, SF, DF, N, EXT) void name(ssse3_t *t) { for (int i = 0; i < N; i++) t->res.DF[i] = (EXT)(STYPE)t->src.SF[i]; }
PMOV(s41_pmovsxbw, int8_t,   int8,   int16, 8, int16_t)
PMOV(s41_pmovsxbd, int8_t,   int8,   int32, 4, int32_t)
PMOV(s41_pmovsxbq, int8_t,   int8,   int64, 2, int64_t)
PMOV(s41_pmovsxwd, int16_t,  int16,  int32, 4, int32_t)
PMOV(s41_pmovsxwq, int16_t,  int16,  int64, 2, int64_t)
PMOV(s41_pmovsxdq, int32_t,  int32,  int64, 2, int64_t)
PMOV(s41_pmovzxbw, uint8_t,  uint8,  uint16, 8, uint16_t)
PMOV(s41_pmovzxbd, uint8_t,  uint8,  uint32, 4, uint32_t)
PMOV(s41_pmovzxbq, uint8_t,  uint8,  uint64, 2, uint64_t)
PMOV(s41_pmovzxwd, uint16_t, uint16, uint32, 4, uint32_t)
PMOV(s41_pmovzxwq, uint16_t, uint16, uint64, 2, uint64_t)
PMOV(s41_pmovzxdq, uint32_t, uint32, uint64, 2, uint64_t)

void s41_ptest(ssse3_t *t)
{
	int zf = !((t->dst.uint64[0] & t->src.uint64[0]) | (t->dst.uint64[1] & t->src.uint64[1]));
	int cf = !((~t->dst.uint64[0] & t->src.uint64[0]) | (~t->dst.uint64[1] & t->src.uint64[1]));
	t->flags_out = (zf ? RF_ZF : 0) | (cf ? RF_CF : 0);
}

/* -------------------------------------------------------------- scalar ops */
void s41_popcnt(ssse3_t *t)
{
	uint64_t v = t->gpr_in; int n = 0;
	if (t->opsize < 64) v &= (1ULL << t->opsize) - 1;
	t->flags_out = v ? 0 : RF_ZF;
	while (v) { v &= v - 1; n++; }
	t->gpr_out = (uint64_t)n;
}
/* CRC32C (Castagnoli, reflected 0x82F63B78). gpr_out holds the accumulator on entry and the result on exit. */
void s41_crc32(ssse3_t *t)
{
	uint32_t crc = (uint32_t)t->gpr_out;
	int bytes = t->opsize / 8;
	for (int b = 0; b < bytes; b++) {
		crc ^= (uint8_t)(t->gpr_in >> (8 * b));
		for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0x82F63B78u & (uint32_t)-(int32_t)(crc & 1));
	}
	t->gpr_out = crc;
}

/* ---------------------------------------------------------------- rounding */
static uint64_t d2u(double x) { uint64_t u; memcpy(&u, &x, 8); return u; }
static double   u2d(uint64_t u) { double x; memcpy(&x, &u, 8); return x; }

/* mode: 0 nearest-even, 1 down, 2 up, 3 truncate */
static double
rnd_double(double x, int mode)
{
	uint64_t u = d2u(x), sign = u & 0x8000000000000000ULL;
	int e = (int)((u >> 52) & 0x7ff);
	double ax, t, frac, r;

	if (e == 0x7ff)	/* inf stays, NaN is quieted */
		return (u & 0xfffffffffffffULL) ? u2d(u | 0x8000000000000ULL) : x;
	if (e >= 1075)	/* |x| >= 2^52: already integral */
		return x;
	ax = u2d(u & 0x7fffffffffffffffULL);
	t = (double)(int64_t)ax;
	frac = ax - t;
	switch (mode) {
	case 3:  r = t; break;
	case 1:  r = (sign && frac > 0) ? t + 1 : t; break;
	case 2:  r = (!sign && frac > 0) ? t + 1 : t; break;
	default: r = (frac > 0.5 || (frac == 0.5 && ((int64_t)t & 1))) ? t + 1 : t; break;
	}
	return u2d(d2u(r) | sign);
}

static float
rnd_float(float x, int mode)
{
	uint32_t u; memcpy(&u, &x, 4);
	int e = (int)((u >> 23) & 0xff);
	if (e == 0xff) {
		if (u & 0x7fffff) { u |= 0x400000; memcpy(&x, &u, 4); }	/* quiet NaN */
		return x;
	}
	if (e >= 150) return x;					/* |x| >= 2^23: already integral */
	return (float)rnd_double((double)x, mode);
}

static int
round_mode(uint8_t imm)
{
	if (imm & 4) {	/* use MXCSR.RC */
		uint32_t m;
		__asm__ __volatile__("stmxcsr %0" : "=m"(m));
		return (m >> 13) & 3;
	}
	return imm & 3;
}

void s41_roundps(ssse3_t *t) { int m = round_mode(t->udo_imm->lval.ubyte); float s[4], r[4]; memcpy(s, &t->src, 16); for (int i = 0; i < 4; i++) r[i] = rnd_float(s[i], m); memcpy(&t->res, r, 16); }
void s41_roundpd(ssse3_t *t) { int m = round_mode(t->udo_imm->lval.ubyte); double s[2], r[2]; memcpy(s, &t->src, 16); for (int i = 0; i < 2; i++) r[i] = rnd_double(s[i], m); memcpy(&t->res, r, 16); }
void s41_roundss(ssse3_t *t) { int m = round_mode(t->udo_imm->lval.ubyte); float s[4], r; memcpy(s, &t->src, 16); r = rnd_float(s[0], m); t->res = t->dst; memcpy(&t->res, &r, 4); }
void s41_roundsd(ssse3_t *t) { int m = round_mode(t->udo_imm->lval.ubyte); double s[2], r; memcpy(s, &t->src, 16); r = rnd_double(s[0], m); t->res = t->dst; memcpy(&t->res, &r, 8); }

/* ---------------------------------------------------------------- dispatcher */
enum kind { K_XMM, K_BLENDV, K_PINSR, K_PEXTR, K_PTEST, K_POPCNT, K_CRC32 };

static const struct ent {
	uint32_t mnemonic; uint8_t kind; uint8_t memsz; uint8_t fp; ssse3_func f;
} tab[] = {
#define E(m, kind, memsz, fp, f) { UD_I##m, kind, memsz, fp, f }
	E(pblendw, K_XMM, 16, 0, s41_pblendw), E(blendps, K_XMM, 16, 0, s41_blendps), E(blendpd, K_XMM, 16, 0, s41_blendpd),
	E(pblendvb, K_BLENDV, 16, 0, s41_pblendvb), E(blendvps, K_BLENDV, 16, 0, s41_blendvps), E(blendvpd, K_BLENDV, 16, 0, s41_blendvpd),
	E(dpps, K_XMM, 16, 1, s41_dpps), E(dppd, K_XMM, 16, 1, s41_dppd), E(insertps, K_XMM, 4, 0, s41_insertps),
	E(movntdqa, K_XMM, 16, 0, s41_movntdqa), E(mpsadbw, K_XMM, 16, 0, s41_mpsadbw), E(packusdw, K_XMM, 16, 0, s41_packusdw),
	E(pcmpeqq, K_XMM, 16, 0, s41_pcmpeqq), E(phminposuw, K_XMM, 16, 0, s41_phminposuw),
	E(pmaxsb, K_XMM, 16, 0, s41_pmaxsb), E(pmaxsd, K_XMM, 16, 0, s41_pmaxsd), E(pmaxuw, K_XMM, 16, 0, s41_pmaxuw), E(pmaxud, K_XMM, 16, 0, s41_pmaxud),
	E(pminsb, K_XMM, 16, 0, s41_pminsb), E(pminsd, K_XMM, 16, 0, s41_pminsd), E(pminuw, K_XMM, 16, 0, s41_pminuw), E(pminud, K_XMM, 16, 0, s41_pminud),
	E(pmuldq, K_XMM, 16, 0, s41_pmuldq), E(pmulld, K_XMM, 16, 0, s41_pmulld),
	E(pmovsxbw, K_XMM, 8, 0, s41_pmovsxbw), E(pmovsxbd, K_XMM, 4, 0, s41_pmovsxbd), E(pmovsxbq, K_XMM, 2, 0, s41_pmovsxbq),
	E(pmovsxwd, K_XMM, 8, 0, s41_pmovsxwd), E(pmovsxwq, K_XMM, 4, 0, s41_pmovsxwq), E(pmovsxdq, K_XMM, 8, 0, s41_pmovsxdq),
	E(pmovzxbw, K_XMM, 8, 0, s41_pmovzxbw), E(pmovzxbd, K_XMM, 4, 0, s41_pmovzxbd), E(pmovzxbq, K_XMM, 2, 0, s41_pmovzxbq),
	E(pmovzxwd, K_XMM, 8, 0, s41_pmovzxwd), E(pmovzxwq, K_XMM, 4, 0, s41_pmovzxwq), E(pmovzxdq, K_XMM, 8, 0, s41_pmovzxdq),
	E(roundps, K_XMM, 16, 1, s41_roundps), E(roundpd, K_XMM, 16, 1, s41_roundpd), E(roundss, K_XMM, 4, 1, s41_roundss), E(roundsd, K_XMM, 8, 1, s41_roundsd),
	E(ptest, K_PTEST, 16, 0, s41_ptest),
	E(pinsrb, K_PINSR, 1, 0, s41_pinsrb), E(pinsrd, K_PINSR, 4, 0, s41_pinsrd), E(pinsrq, K_PINSR, 8, 0, s41_pinsrq),
	E(pextrb, K_PEXTR, 1, 0, s41_pextrb), E(pextrd, K_PEXTR, 4, 0, s41_pextrd), E(pextrq, K_PEXTR, 8, 0, s41_pextrq),
	E(extractps, K_PEXTR, 4, 0, s41_pextrd),
	E(popcnt, K_POPCNT, 0, 0, s41_popcnt), E(crc32, K_CRC32, 0, 0, s41_crc32),
};

static int
is_xmm(const ud_operand_t *o) { return o && o->type == UD_OP_REG && o->base >= UD_R_XMM0 && o->base <= UD_R_XMM15; }

static void
apply_flags(const op_t *op, uint32_t f)
{
	if (op->state_flavor == SAVEDSTATE_64)
		op->state64->isf.rflags = (op->state64->isf.rflags & ~(uint64_t)RF_ARITH) | f;
	else
		op->state32->efl = (op->state32->efl & ~(uint32_t)RF_ARITH) | f;
}

/*
 * xcross: the user's XMM registers are live in the CPU while we run, and any code we execute (memset, float arithmetic,
 * ...) may clobber them. So op_sse41_run() snapshots all sixteen first, the instruction is emulated against the snapshot,
 * and the snapshot (with the destination updated) is restored at the end.
 */
static int sse41_run(const op_t *op, uint8_t *xs);

int
op_sse41_run(const op_t *op)
{
	uint8_t xs[256] __attribute__((aligned(16)));
	int r;
	opemu_xmm_save(xs);
	r = sse41_run(op, xs);
	opemu_xmm_restore(xs);
	return r;
}

#define XREG(n)	((void *)(xs + 16 * (n)))

static int
sse41_run(const op_t *op, uint8_t *xs)
{
	const ud_t *ud = op->ud_obj;
	const uint32_t mn = ud_insn_mnemonic((ud_t *)ud);
	const struct ent *e = NULL;
	ssse3_t t;
	ud_operand_t noimm;
	const ud_operand_t *o0, *o1, *o2;

	for (unsigned i = 0; i < sizeof tab / sizeof *tab; i++)
		if (tab[i].mnemonic == mn) { e = &tab[i]; break; }
	if (!e)
		return -1;

	memset(&t, 0, sizeof t);
	memset(&noimm, 0, sizeof noimm);
	noimm.type = UD_OP_IMM;
	o0 = ud_insn_opr((ud_t *)ud, 0); o1 = ud_insn_opr((ud_t *)ud, 1); o2 = ud_insn_opr((ud_t *)ud, 2);
	if (!o0 || !o1) return -1;
	t.op_obj = op; t.udo_dst = o0; t.udo_src = o1;
	t.udo_imm = (o2 && o2->type == UD_OP_IMM) ? o2 : &noimm;

	switch (e->kind) {
	case K_XMM: case K_BLENDV: case K_PTEST: {
		if (!is_xmm(o0)) return -1;
		memcpy(&t.dst, XREG(o0->base - UD_R_XMM0), 16);
		if (is_xmm(o1)) memcpy(&t.src, XREG(o1->base - UD_R_XMM0), 16);
		else if (o1->type == UD_OP_MEM) { t.extended = 1; if (opemu_load_mem(op, o1, &t.src.uint128, e->memsz) != 0) return -1; }
		else return -1;
		if (e->kind == K_BLENDV) memcpy(&t.xmm0, XREG(0), 16);
		break;
	}
	case K_PINSR: {
		if (!is_xmm(o0)) return -1;
		memcpy(&t.dst, XREG(o0->base - UD_R_XMM0), 16);
		if (o1->type == UD_OP_REG) { if (opemu_gpr_read(op, o1->base, &t.gpr_in) != 0) return -1; }
		else if (o1->type == UD_OP_MEM) { if (opemu_load_mem(op, o1, &t.gpr_in, e->memsz) != 0) return -1; }
		else return -1;
		break;
	}
	case K_PEXTR: {
		if (!is_xmm(o1)) return -1;
		memcpy(&t.src, XREG(o1->base - UD_R_XMM0), 16);
		break;
	}
	case K_POPCNT: case K_CRC32: {
		uint64_t v = 0;
		if (o0->type != UD_OP_REG) return -1;
		if (o1->type == UD_OP_REG) { if (opemu_gpr_read(op, o1->base, &v) != 0) return -1; t.opsize = o1->size; }
		else if (o1->type == UD_OP_MEM) { t.opsize = o1->size; if (!t.opsize) return -1; if (opemu_load_mem(op, o1, &v, t.opsize / 8) != 0) return -1; }
		else return -1;
		t.gpr_in = v;
		if (e->kind == K_POPCNT) t.opsize = o0->size;
		else { uint64_t acc; if (opemu_gpr_read(op, o0->base, &acc) != 0) return -1; t.gpr_out = acc; }
		break;
	}
	}

	e->f(&t);

	switch (e->kind) {
	case K_XMM: case K_BLENDV: case K_PINSR:
		memcpy(XREG(o0->base - UD_R_XMM0), &t.res, 16);
		break;
	case K_PTEST:
		apply_flags(op, t.flags_out);
		break;
	case K_PEXTR:
		if (o0->type == UD_OP_REG) { if (opemu_gpr_write(op, o0->base, t.gpr_out) != 0) return -1; }
		else if (o0->type == UD_OP_MEM) { if (opemu_store_mem(op, o0, &t.gpr_out, e->memsz) != 0) return -1; }
		else return -1;
		break;
	case K_POPCNT:
		if (opemu_gpr_write(op, o0->base, t.gpr_out) != 0) return -1;
		apply_flags(op, t.flags_out);
		break;
	case K_CRC32:
		if (opemu_gpr_write(op, o0->base, (uint32_t)t.gpr_out) != 0) return -1;	/* zero-extends */
		break;
	}
	return 0;
}
