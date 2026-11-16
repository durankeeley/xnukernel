/* SSE4.1 / POPCNT / CRC32 tests: emulated routine (real OPEMU source) vs native instruction. */
#include "shim_types.h"
#include "opemu.h"
#include "ssse3_priv.h"
#include <immintrin.h>
#include <math.h>
#include <nmmintrin.h>
#include "ht.h"

#define DECL(n) extern void s41_##n(ssse3_t*);
DECL(pblendw) DECL(blendps) DECL(blendpd) DECL(pblendvb) DECL(blendvps) DECL(blendvpd) DECL(dpps) DECL(dppd) DECL(insertps)
DECL(pinsrb) DECL(pinsrd) DECL(pinsrq) DECL(pextrb) DECL(pextrd) DECL(pextrq) DECL(mpsadbw) DECL(packusdw) DECL(pcmpeqq)
DECL(phminposuw) DECL(pmaxsb) DECL(pmaxsd) DECL(pmaxuw) DECL(pmaxud) DECL(pminsb) DECL(pminsd) DECL(pminuw) DECL(pminud)
DECL(pmuldq) DECL(pmulld) DECL(movntdqa) DECL(pmovsxbw) DECL(pmovsxbd) DECL(pmovsxbq) DECL(pmovsxwd) DECL(pmovsxwq)
DECL(pmovsxdq) DECL(pmovzxbw) DECL(pmovzxbd) DECL(pmovzxbq) DECL(pmovzxwd) DECL(pmovzxwq) DECL(pmovzxdq) DECL(ptest)
DECL(popcnt) DECL(crc32) DECL(roundps) DECL(roundpd) DECL(roundss) DECL(roundsd)
#include "imm.inc"

typedef void (*emu_fn)(ssse3_t *);
static uint8_t g_x0[16];

/* run a routine; FP routines are bracketed so the harness's own XMM state survives */
static int run(emu_fn f, ssse3_t *t)
{
	int crashed; uint8_t xs[256] __attribute__((aligned(16)));
	opemu_xmm_save(xs);
	HT_GUARD(f(t), crashed);
	opemu_xmm_restore(xs);
	return crashed;
}
static void prep(ssse3_t *t, const uint8_t *d, const uint8_t *s, int imm, ud_operand_t *io)
{
	memset(t, 0, sizeof *t); memset(io, 0, sizeof *io);
	io->type = UD_OP_IMM; io->lval.ubyte = (uint8_t)imm;
	memcpy(&t->dst, d, 16); memcpy(&t->src, s, 16); t->udo_imm = io;
}
static int feq(const uint8_t *a, const uint8_t *b, int dbl)
{
	if (dbl) { for (int i = 0; i < 2; i++) { double x, y; memcpy(&x, a + 8*i, 8); memcpy(&y, b + 8*i, 8); if (!(isnan(x) && isnan(y)) && memcmp(a + 8*i, b + 8*i, 8)) return 0; } }
	else     { for (int i = 0; i < 4; i++) { float x, y;  memcpy(&x, a + 4*i, 4); memcpy(&y, b + 4*i, 4); if (!(isnan(x) && isnan(y)) && memcmp(a + 4*i, b + 4*i, 4)) return 0; } }
	return 1;
}
static void fill_fp(uint8_t *p, int dbl)	/* mix of random bits, friendly numbers, halves (rounding ties), infinities, NaNs */
{
	ht_fill16(p);
	int mode = ht_rand() % 6;
	if (mode >= 3) {
		if (dbl) { double *q = (double*)p; for (int i = 0; i < 2; i++) { double v = (double)(int)(ht_rand() % 4001) / 2.0 - 1000.0; if (mode == 5) v *= 1e-3; q[i] = v; } }
		else { float *q = (float*)p; for (int i = 0; i < 4; i++) { float v = (float)(int)(ht_rand() % 4001) / 2.0f - 1000.0f; if (mode == 5) v *= 1e-3f; q[i] = v; } }
	}
}
static void bad(ht_stat_t *st, const char *what, const uint8_t *d, const uint8_t *s, int imm, const uint8_t *e, const uint8_t *g)
{
	char a[40], b[40], c[40], x[40]; ht_hex(a, d, 16); ht_hex(b, s, 16); ht_hex(c, e, 16); ht_hex(x, g, 16);
	ht_note(st, 0, "%s dst=%s src=%s imm=%d expect=%s got=%s", what, a, b, imm, c, x);
}

/* register-register vector ops without an immediate ---------------------------------------------------- */
#define X2(n, e) static void nx_##n(const uint8_t*d,const uint8_t*s,uint8_t*o){__m128i D=_mm_loadu_si128((const void*)d),S=_mm_loadu_si128((const void*)s);(void)D;_mm_storeu_si128((void*)o,e);}
X2(pcmpeqq,_mm_cmpeq_epi64(D,S)) X2(packusdw,_mm_packus_epi32(D,S))
X2(pmaxsb,_mm_max_epi8(D,S)) X2(pmaxsd,_mm_max_epi32(D,S)) X2(pmaxuw,_mm_max_epu16(D,S)) X2(pmaxud,_mm_max_epu32(D,S))
X2(pminsb,_mm_min_epi8(D,S)) X2(pminsd,_mm_min_epi32(D,S)) X2(pminuw,_mm_min_epu16(D,S)) X2(pminud,_mm_min_epu32(D,S))
X2(pmuldq,_mm_mul_epi32(D,S)) X2(pmulld,_mm_mullo_epi32(D,S)) X2(phminposuw,_mm_minpos_epu16(S)) X2(movntdqa,S)
X2(pmovsxbw,_mm_cvtepi8_epi16(S)) X2(pmovsxbd,_mm_cvtepi8_epi32(S)) X2(pmovsxbq,_mm_cvtepi8_epi64(S))
X2(pmovsxwd,_mm_cvtepi16_epi32(S)) X2(pmovsxwq,_mm_cvtepi16_epi64(S)) X2(pmovsxdq,_mm_cvtepi32_epi64(S))
X2(pmovzxbw,_mm_cvtepu8_epi16(S)) X2(pmovzxbd,_mm_cvtepu8_epi32(S)) X2(pmovzxbq,_mm_cvtepu8_epi64(S))
X2(pmovzxwd,_mm_cvtepu16_epi32(S)) X2(pmovzxwq,_mm_cvtepu16_epi64(S)) X2(pmovzxdq,_mm_cvtepu32_epi64(S))
typedef void (*nat_fn)(const uint8_t*, const uint8_t*, uint8_t*);
static const struct { const char *name; emu_fn e; nat_fn n; } simple[] = {
#define T(x) { #x, s41_##x, nx_##x }
	T(pcmpeqq), T(packusdw), T(pmaxsb), T(pmaxsd), T(pmaxuw), T(pmaxud), T(pminsb), T(pminsd), T(pminuw), T(pminud),
	T(pmuldq), T(pmulld), T(phminposuw), T(movntdqa), T(pmovsxbw), T(pmovsxbd), T(pmovsxbq), T(pmovsxwd), T(pmovsxwq),
	T(pmovsxdq), T(pmovzxbw), T(pmovzxbd), T(pmovzxbq), T(pmovzxwd), T(pmovzxwq), T(pmovzxdq),
};

int test_sse41(long iters)
{
	uint8_t d[16], s[16], m[16], ex[16], go[16]; ssse3_t t; ud_operand_t io;

	for (unsigned k = 0; k < sizeof simple / sizeof *simple; k++) {
		ht_stat_t *st = ht_stat("sse4.1", simple[k].name);
		for (long i = 0; i < iters; i++) {
			ht_fill16(d); ht_fill16(s);
			simple[k].n(d, s, ex); prep(&t, d, s, 0, &io);
			if (run(simple[k].e, &t)) memset(go, 0xCC, 16); else memcpy(go, &t.res, 16);
			if (memcmp(ex, go, 16)) bad(st, "xmm", d, s, 0, ex, go); else ht_note(st, 1, "");
		}
	}

	/* blends with immediate (integer view) */
	struct { const char *n; emu_fn e; int nimm; int kind; } bl[] = {
		{ "pblendw", s41_pblendw, 256, 0 }, { "blendps", s41_blendps, 16, 1 }, { "blendpd", s41_blendpd, 4, 2 } };
	for (unsigned k = 0; k < 3; k++) {
		ht_stat_t *st = ht_stat("sse4.1", bl[k].n);
		for (int imm = 0; imm < bl[k].nimm; imm++) for (int i = 0; i < 30; i++) {
			ht_fill16(d); ht_fill16(s); prep(&t, d, s, imm, &io); run(bl[k].e, &t); memcpy(go, &t.res, 16);
			__m128i D = _mm_loadu_si128((void*)d), S = _mm_loadu_si128((void*)s), R;
			if (bl[k].kind == 0) R = nat_pblendw(D, S, imm);
			else if (bl[k].kind == 1) R = _mm_castps_si128(nat_blendps(_mm_castsi128_ps(D), _mm_castsi128_ps(S), imm));
			else R = _mm_castpd_si128(nat_blendpd(_mm_castsi128_pd(D), _mm_castsi128_pd(S), imm));
			_mm_storeu_si128((void*)ex, R);
			if (memcmp(ex, go, 16)) bad(st, "xmm", d, s, imm, ex, go); else ht_note(st, 1, "");
		}
	}
	/* variable blends (mask in xmm0) */
	{
		ht_stat_t *a = ht_stat("sse4.1", "pblendvb"), *b = ht_stat("sse4.1", "blendvps"), *c = ht_stat("sse4.1", "blendvpd");
		for (long i = 0; i < iters; i++) {
			ht_fill16(d); ht_fill16(s); ht_fill16(m);
			__m128i D = _mm_loadu_si128((void*)d), S = _mm_loadu_si128((void*)s), M = _mm_loadu_si128((void*)m);
			prep(&t, d, s, 0, &io); memcpy(&t.xmm0, m, 16); run(s41_pblendvb, &t); memcpy(go, &t.res, 16);
			_mm_storeu_si128((void*)ex, _mm_blendv_epi8(D, S, M)); if (memcmp(ex, go, 16)) bad(a, "xmm", d, s, 0, ex, go); else ht_note(a, 1, "");
			prep(&t, d, s, 0, &io); memcpy(&t.xmm0, m, 16); run(s41_blendvps, &t); memcpy(go, &t.res, 16);
			_mm_storeu_si128((void*)ex, _mm_castps_si128(_mm_blendv_ps(_mm_castsi128_ps(D), _mm_castsi128_ps(S), _mm_castsi128_ps(M)))); if (memcmp(ex, go, 16)) bad(b, "xmm", d, s, 0, ex, go); else ht_note(b, 1, "");
			prep(&t, d, s, 0, &io); memcpy(&t.xmm0, m, 16); run(s41_blendvpd, &t); memcpy(go, &t.res, 16);
			_mm_storeu_si128((void*)ex, _mm_castpd_si128(_mm_blendv_pd(_mm_castsi128_pd(D), _mm_castsi128_pd(S), _mm_castsi128_pd(M)))); if (memcmp(ex, go, 16)) bad(c, "xmm", d, s, 0, ex, go); else ht_note(c, 1, "");
		}
	}
	/* mpsadbw */
	{ ht_stat_t *st = ht_stat("sse4.1", "mpsadbw");
	  for (int imm = 0; imm < 8; imm++) for (int i = 0; i < 2000; i++) { ht_fill16(d); ht_fill16(s); prep(&t, d, s, imm, &io); run(s41_mpsadbw, &t); memcpy(go, &t.res, 16);
		_mm_storeu_si128((void*)ex, nat_mpsadbw(_mm_loadu_si128((void*)d), _mm_loadu_si128((void*)s), imm)); if (memcmp(ex, go, 16)) bad(st, "xmm", d, s, imm, ex, go); else ht_note(st, 1, ""); } }
	/* floating point: dot products, insertps, rounding */
	{
		ht_stat_t *sp = ht_stat("sse4.1", "dpps"), *sd = ht_stat("sse4.1", "dppd"), *si = ht_stat("sse4.1", "insertps"), *sim = ht_stat("sse4.1", "insertps(mem)");
		for (int imm = 0; imm < 256; imm++) for (int i = 0; i < 30; i++) {
			fill_fp(d, 0); fill_fp(s, 0); prep(&t, d, s, imm, &io); run(s41_dpps, &t); memcpy(go, &t.res, 16);
			_mm_storeu_ps((float*)ex, nat_dpps(_mm_loadu_ps((float*)d), _mm_loadu_ps((float*)s), imm)); if (!feq(ex, go, 0)) bad(sp, "xmm", d, s, imm, ex, go); else ht_note(sp, 1, "");
			fill_fp(d, 1); fill_fp(s, 1); prep(&t, d, s, imm, &io); run(s41_dppd, &t); memcpy(go, &t.res, 16);
			_mm_storeu_pd((double*)ex, nat_dppd(_mm_loadu_pd((double*)d), _mm_loadu_pd((double*)s), imm)); if (!feq(ex, go, 1)) bad(sd, "xmm", d, s, imm, ex, go); else ht_note(sd, 1, "");
			ht_fill16(d); ht_fill16(s); prep(&t, d, s, imm, &io); run(s41_insertps, &t); memcpy(go, &t.res, 16);
			_mm_storeu_ps((float*)ex, nat_insertps(_mm_loadu_ps((float*)d), _mm_loadu_ps((float*)s), imm)); if (memcmp(ex, go, 16)) bad(si, "xmm", d, s, imm, ex, go); else ht_note(si, 1, "");
			/* memory source: count_s forced to 0 -> equals the register form with the value in element 0 */
			prep(&t, d, s, imm, &io); t.extended = 1; run(s41_insertps, &t); memcpy(go, &t.res, 16);
			{ uint8_t s2[16] = {0}; memcpy(s2, s, 4); int imm2 = imm & 0x3f;  /* count_s = 0 */
			  _mm_storeu_ps((float*)ex, nat_insertps(_mm_loadu_ps((float*)d), _mm_loadu_ps((float*)s2), imm2)); }
			if (memcmp(ex, go, 16)) bad(sim, "mem", d, s, imm, ex, go); else ht_note(sim, 1, "");
		}
		struct { const char *n; emu_fn e; int dbl, scalar, kind; } rd[] = {
			{ "roundps", s41_roundps, 0, 0, 0 }, { "roundpd", s41_roundpd, 1, 0, 1 }, { "roundss", s41_roundss, 0, 1, 2 }, { "roundsd", s41_roundsd, 1, 1, 3 } };
		for (unsigned k = 0; k < 4; k++) {
			ht_stat_t *st = ht_stat("sse4.1", rd[k].n);
			for (int imm = 0; imm < 16; imm++) for (int i = 0; i < 4000; i++) {
				fill_fp(d, rd[k].dbl); fill_fp(s, rd[k].dbl); prep(&t, d, s, imm, &io); run(rd[k].e, &t); memcpy(go, &t.res, 16);
				if (rd[k].kind == 0) _mm_storeu_ps((float*)ex, nat_roundps(_mm_loadu_ps((float*)d), _mm_loadu_ps((float*)s), imm));
				else if (rd[k].kind == 1) _mm_storeu_pd((double*)ex, nat_roundpd(_mm_loadu_pd((double*)d), _mm_loadu_pd((double*)s), imm));
				else if (rd[k].kind == 2) _mm_storeu_ps((float*)ex, nat_roundss(_mm_loadu_ps((float*)d), _mm_loadu_ps((float*)s), imm));
				else _mm_storeu_pd((double*)ex, nat_roundsd(_mm_loadu_pd((double*)d), _mm_loadu_pd((double*)s), imm));
				if (!feq(ex, go, rd[k].dbl)) bad(st, "xmm", d, s, imm, ex, go); else ht_note(st, 1, "");
			}
		}
	}
	/* insert / extract */
	{
		ht_stat_t *a = ht_stat("sse4.1", "pinsrb"), *b = ht_stat("sse4.1", "pinsrd"), *c = ht_stat("sse4.1", "pinsrq");
		ht_stat_t *x = ht_stat("sse4.1", "pextrb"), *y = ht_stat("sse4.1", "pextrd"), *z = ht_stat("sse4.1", "pextrq");
		for (int imm = 0; imm < 16; imm++) for (int i = 0; i < 400; i++) {
			ht_fill16(d); ht_fill16(s); uint64_t v = ht_rand();
			__m128i D = _mm_loadu_si128((void*)d);
			prep(&t, d, s, imm, &io); t.gpr_in = v; run(s41_pinsrb, &t); memcpy(go, &t.res, 16);
			_mm_storeu_si128((void*)ex, nat_pinsrb(D, (int)v, imm)); if (memcmp(ex, go, 16)) bad(a, "xmm", d, s, imm, ex, go); else ht_note(a, 1, "");
			prep(&t, d, s, imm, &io); t.gpr_in = v; run(s41_pinsrd, &t); memcpy(go, &t.res, 16);
			_mm_storeu_si128((void*)ex, nat_pinsrd(D, (int)v, imm & 3)); if (memcmp(ex, go, 16)) bad(b, "xmm", d, s, imm, ex, go); else ht_note(b, 1, "");
			prep(&t, d, s, imm, &io); t.gpr_in = v; run(s41_pinsrq, &t); memcpy(go, &t.res, 16);
			_mm_storeu_si128((void*)ex, nat_pinsrq(D, (long long)v, imm & 1)); if (memcmp(ex, go, 16)) bad(c, "xmm", d, s, imm, ex, go); else ht_note(c, 1, "");
			prep(&t, d, s, imm, &io); run(s41_pextrb, &t); long long e1 = (unsigned char)nat_pextrb(_mm_loadu_si128((void*)s), imm);
			if ((long long)t.gpr_out != e1) ht_note(x, 0, "src=%016llx%016llx imm=%d expect=%llx got=%llx", (unsigned long long)*(uint64_t*)(s+8), (unsigned long long)*(uint64_t*)s, imm, (unsigned long long)e1, (unsigned long long)t.gpr_out); else ht_note(x, 1, "");
			if (imm < 4) { prep(&t, d, s, imm, &io); run(s41_pextrd, &t); long long e2 = (unsigned int)nat_pextrd(_mm_loadu_si128((void*)s), imm);
				if ((long long)t.gpr_out != e2) ht_note(y, 0, "imm=%d expect=%llx got=%llx", imm, (unsigned long long)e2, (unsigned long long)t.gpr_out); else ht_note(y, 1, ""); }
			if (imm < 2) { prep(&t, d, s, imm, &io); run(s41_pextrq, &t); long long e3 = nat_pextrq(_mm_loadu_si128((void*)s), imm);
				if ((long long)t.gpr_out != e3) ht_note(z, 0, "imm=%d expect=%llx got=%llx", imm, (unsigned long long)e3, (unsigned long long)t.gpr_out); else ht_note(z, 1, ""); }
		}
	}
	/* ptest, popcnt, crc32 */
	{
		ht_stat_t *p = ht_stat("sse4.1", "ptest"), *pc = ht_stat("sse4.2", "popcnt"), *cr = ht_stat("sse4.2", "crc32");
		for (long i = 0; i < iters; i++) {
			ht_fill16(d); ht_fill16(s);
			if (i % 4 == 0) { memset(d, 0, 16); }
			if (i % 4 == 1) { memcpy(d, s, 16); }
			__m128i D = _mm_loadu_si128((void*)d), S = _mm_loadu_si128((void*)s);
			prep(&t, d, s, 0, &io); run(s41_ptest, &t);
			unsigned ef = (_mm_testz_si128(D, S) ? RFZ : 0) | (_mm_testc_si128(D, S) ? RFC : 0);
			if (ef != t.flags_out) ht_note(p, 0, "dst/src differ flags expect=%#x got=%#x", ef, t.flags_out); else ht_note(p, 1, "");
			uint64_t v = ht_rand(); if (i % 5 == 0) v = 0;
			static const int sizes[] = { 16, 32, 64 };
			for (int k = 0; k < 3; k++) {
				int sz = sizes[k]; uint64_t mv = sz == 64 ? v : v & ((1ULL << sz) - 1);
				prep(&t, d, s, 0, &io); t.gpr_in = v; t.opsize = sz; run(s41_popcnt, &t);
				unsigned long long en = sz == 64 ? _mm_popcnt_u64(mv) : _mm_popcnt_u32((unsigned)mv);
				unsigned eflg = mv ? 0 : RFZ;
				if (en != t.gpr_out || eflg != t.flags_out) ht_note(pc, 0, "size=%d val=%llx expect=%llu/%#x got=%llu/%#x", sz, (unsigned long long)mv, en, eflg, (unsigned long long)t.gpr_out, t.flags_out); else ht_note(pc, 1, "");
			}
			uint32_t acc = (uint32_t)ht_rand();
			{ prep(&t, d, s, 0, &io); t.gpr_in = v; t.gpr_out = acc; t.opsize = 8;  run(s41_crc32, &t); uint32_t e = _mm_crc32_u8(acc, (uint8_t)v);  if (e != (uint32_t)t.gpr_out) ht_note(cr, 0, "u8 acc=%08x val=%02x expect=%08x got=%08x", acc, (uint8_t)v, e, (uint32_t)t.gpr_out); else ht_note(cr, 1, ""); }
			{ prep(&t, d, s, 0, &io); t.gpr_in = v; t.gpr_out = acc; t.opsize = 16; run(s41_crc32, &t); uint32_t e = _mm_crc32_u16(acc, (uint16_t)v); if (e != (uint32_t)t.gpr_out) ht_note(cr, 0, "u16 acc=%08x val=%04x expect=%08x got=%08x", acc, (uint16_t)v, e, (uint32_t)t.gpr_out); else ht_note(cr, 1, ""); }
			{ prep(&t, d, s, 0, &io); t.gpr_in = v; t.gpr_out = acc; t.opsize = 32; run(s41_crc32, &t); uint32_t e = _mm_crc32_u32(acc, (uint32_t)v); if (e != (uint32_t)t.gpr_out) ht_note(cr, 0, "u32 acc=%08x val=%08x expect=%08x got=%08x", acc, (uint32_t)v, e, (uint32_t)t.gpr_out); else ht_note(cr, 1, ""); }
			{ prep(&t, d, s, 0, &io); t.gpr_in = v; t.gpr_out = acc; t.opsize = 64; run(s41_crc32, &t); uint64_t e = _mm_crc32_u64(acc, v); if ((uint32_t)e != (uint32_t)t.gpr_out) ht_note(cr, 0, "u64 acc=%08x val=%016llx expect=%08x got=%08x", acc, (unsigned long long)v, (uint32_t)e, (uint32_t)t.gpr_out); else ht_note(cr, 1, ""); }
		}
	}
	return 0;
}
