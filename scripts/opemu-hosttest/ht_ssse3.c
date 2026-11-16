/* SSSE3 + SSE4.2 tests: emulated routine (real OPEMU source) vs native instruction. */
#include "shim_types.h"
#include "opemu.h"
#include "ssse3_priv.h"
#include <immintrin.h>
#include "ht.h"

extern void psignb(ssse3_t*), psignw(ssse3_t*), psignd(ssse3_t*), pabsb(ssse3_t*), pabsw(ssse3_t*), pabsd(ssse3_t*),
	palignr(ssse3_t*), pshufb(ssse3_t*), pmulhrsw(ssse3_t*), pmaddubsw(ssse3_t*), phsubw(ssse3_t*), phsubd(ssse3_t*),
	phsubsw(ssse3_t*), phaddw(ssse3_t*), phaddd(ssse3_t*), phaddsw(ssse3_t*),
	pcmpistri(ssse3_t*), pcmpestri(ssse3_t*), pcmpistrm(ssse3_t*), pcmpestrm(ssse3_t*), pcmpgtq(ssse3_t*);

#include "imm.inc"

typedef void (*emu_fn)(ssse3_t *);
static int g_dstreg = UD_R_XMM0;
static uint8_t g_x0[16];
/* call the routine and capture XMM0 immediately afterwards (nothing may touch XMM0 in between) */
static void __attribute__((noinline)) call_grab(void (*f)(ssse3_t*), ssse3_t *t)
{
	f(t);
	__asm__ __volatile__("movdqu %%xmm0, %0" : "=m"(*(uint8_t(*)[16])g_x0));
}
static x86_saved_state_t fake_state;
static op_t fake_op;

static int emu(emu_fn f, const uint8_t *d, const uint8_t *s, int legacy, int imm, uint8_t *out)
{
	int crashed;
	ssse3_t t; memset(&t, 0, sizeof t);
	ud_operand_t io; memset(&io, 0, sizeof io); io.type = UD_OP_IMM; io.lval.ubyte = (uint8_t)imm;
	ud_operand_t dop; memset(&dop, 0, sizeof dop); dop.type = UD_OP_REG; dop.base = g_dstreg; t.udo_dst = &dop;
	memcpy(&t.dst, d, 16); memcpy(&t.src, s, 16);
	t.islegacy = legacy; t.udo_imm = &io; t.op_obj = &fake_op;
	HT_GUARD(call_grab(f, &t), crashed);
	if (crashed) memset(out, 0xCC, 16);   /* marker: routine faulted (shown as got=cccc...) */
	else memcpy(out, &t.res, 16);
	return crashed;
}

/* native references --------------------------------------------------------------- */
#define X2(n, e) static void nx_##n(const uint8_t*d,const uint8_t*s,uint8_t*o){__m128i D=_mm_loadu_si128((const void*)d),S=_mm_loadu_si128((const void*)s);_mm_storeu_si128((void*)o,e);}
#define M2(n, e) static void nm_##n(const uint8_t*d,const uint8_t*s,uint8_t*o){__m64 D=*(const __m64*)d,S=*(const __m64*)s;*(__m64*)o=e;_mm_empty();}
X2(psignb,_mm_sign_epi8(D,S)) X2(psignw,_mm_sign_epi16(D,S)) X2(psignd,_mm_sign_epi32(D,S))
X2(pabsb,_mm_abs_epi8(S)) X2(pabsw,_mm_abs_epi16(S)) X2(pabsd,_mm_abs_epi32(S))
X2(pshufb,_mm_shuffle_epi8(D,S)) X2(pmulhrsw,_mm_mulhrs_epi16(D,S)) X2(pmaddubsw,_mm_maddubs_epi16(D,S))
X2(phsubw,_mm_hsub_epi16(D,S)) X2(phsubd,_mm_hsub_epi32(D,S)) X2(phsubsw,_mm_hsubs_epi16(D,S))
X2(phaddw,_mm_hadd_epi16(D,S)) X2(phaddd,_mm_hadd_epi32(D,S)) X2(phaddsw,_mm_hadds_epi16(D,S))
X2(pcmpgtq,_mm_cmpgt_epi64(D,S))
M2(psignb,_mm_sign_pi8(D,S)) M2(psignw,_mm_sign_pi16(D,S)) M2(psignd,_mm_sign_pi32(D,S))
M2(pabsb,_mm_abs_pi8(S)) M2(pabsw,_mm_abs_pi16(S)) M2(pabsd,_mm_abs_pi32(S))
M2(pshufb,_mm_shuffle_pi8(D,S)) M2(pmulhrsw,_mm_mulhrs_pi16(D,S)) M2(pmaddubsw,_mm_maddubs_pi16(D,S))
M2(phsubw,_mm_hsub_pi16(D,S)) M2(phsubd,_mm_hsub_pi32(D,S)) M2(phsubsw,_mm_hsubs_pi16(D,S))
M2(phaddw,_mm_hadd_pi16(D,S)) M2(phaddd,_mm_hadd_pi32(D,S)) M2(phaddsw,_mm_hadds_pi16(D,S))

typedef void (*nat_fn)(const uint8_t*, const uint8_t*, uint8_t*);
static const struct { const char *name; emu_fn e; nat_fn x, m; } simple[] = {
#define T(n) { #n, n, nx_##n, nm_##n }
	T(psignb), T(psignw), T(psignd), T(pabsb), T(pabsw), T(pabsd), T(pshufb), T(pmulhrsw), T(pmaddubsw),
	T(phsubw), T(phsubd), T(phsubsw), T(phaddw), T(phaddd), T(phaddsw),
	{ "pcmpgtq", pcmpgtq, nx_pcmpgtq, 0 },
};

static void fail_note(ht_stat_t *st, const char *form, const uint8_t *d, const uint8_t *s, int imm, const uint8_t *exp, const uint8_t *got, int len)
{
	char a[40], b[40], c[40], e[40];
	ht_hex(a, d, len); ht_hex(b, s, len); ht_hex(c, exp, len); ht_hex(e, got, len);
	ht_note(st, 0, "%s dst=%s src=%s imm=%d expect=%s got=%s", form, a, b, imm, c, e);
}

int test_ssse3(long iters)
{
	uint8_t d[16], s[16], ex[16], go[16];
	memset(&fake_state, 0, sizeof fake_state);
	fake_op.state = &fake_state; fake_op.state64 = &fake_state.ss_64;

	for (unsigned k = 0; k < sizeof simple / sizeof *simple; k++) {
		const char *set = !strcmp(simple[k].name, "pcmpgtq") ? "sse4.2" : "ssse3";
		ht_stat_t *sx = ht_stat(set, simple[k].name);
		char mn[32]; snprintf(mn, sizeof mn, "%s(mmx)", simple[k].name);
		ht_stat_t *sm = simple[k].m ? ht_stat(set, strdup(mn)) : 0;
		for (long i = 0; i < iters; i++) {
			ht_fill16(d); ht_fill16(s);
			simple[k].x(d, s, ex); emu(simple[k].e, d, s, 0, 0, go);
			if (memcmp(ex, go, 16)) fail_note(sx, "xmm", d, s, 0, ex, go, 16); else ht_note(sx, 1, "");
			if (sm) {
				simple[k].m(d, s, ex); emu(simple[k].e, d, s, 1, 0, go);
				if (memcmp(ex, go, 8)) fail_note(sm, "mmx", d, s, 0, ex, go, 8); else ht_note(sm, 1, "");
			}
		}
	}
	/* palignr: all 256 immediates */
	ht_stat_t *pa = ht_stat("ssse3", "palignr"), *pam = ht_stat("ssse3", "palignr(mmx)");
	for (int imm = 0; imm < 256; imm++) for (int i = 0; i < 40; i++) {
		ht_fill16(d); ht_fill16(s);
		__m128i r = nat_alignr_x(_mm_loadu_si128((void*)d), _mm_loadu_si128((void*)s), imm); _mm_storeu_si128((void*)ex, r);
		emu(palignr, d, s, 0, imm, go);
		if (memcmp(ex, go, 16)) fail_note(pa, "xmm", d, s, imm, ex, go, 16); else ht_note(pa, 1, "");
		__m64 rm = nat_alignr_m(*(__m64*)d, *(__m64*)s, imm); *(__m64*)ex = rm; _mm_empty();
		emu(palignr, d, s, 1, imm, go);
		if (memcmp(ex, go, 8)) fail_note(pam, "mmx", d, s, imm, ex, go, 8); else ht_note(pam, 1, "");
	}
	/* SSE4.2 string compares: all 128 immediates; explicit-length forms also vary EAX/EDX */
	ht_stat_t *si = ht_stat("sse4.2", "pcmpistri"), *se = ht_stat("sse4.2", "pcmpestri");
	ht_stat_t *sim = ht_stat("sse4.2", "pcmpistrm"), *sem = ht_stat("sse4.2", "pcmpestrm");
	ht_stat_t *sif = ht_stat("sse4.2", "pcmpistr*:flags"), *sef = ht_stat("sse4.2", "pcmpestr*:flags");
	for (int imm = 0; imm < 128; imm++) for (int i = 0; i < 60; i++) {
		ht_fill16(d); ht_fill16(s);
		__m128i D = _mm_loadu_si128((void*)d), S = _mm_loadu_si128((void*)s);
		int la = (int)(ht_rand() % 41) - 20, lb = (int)(ht_rand() % 41) - 20;
		/* implicit length */
		int ni = nat_cmpistri(D, S, imm);
		int fl = (nat_cmpistrc(D,S,imm)?1:0) | (nat_cmpistrz(D,S,imm)?2:0) | (nat_cmpistrs(D,S,imm)?4:0) | (nat_cmpistro(D,S,imm)?8:0);
		fake_state.ss_64.rcx = 0xdead; fake_state.ss_64.isf.rflags = 0;
		emu(pcmpistri, d, s, 0, imm, go);
		int ei = (int)fake_state.ss_64.rcx;
		uint64_t rf = fake_state.ss_64.isf.rflags;
		int ef = ((rf>>0)&1) | (((rf>>6)&1)<<1) | (((rf>>7)&1)<<2) | (((rf>>11)&1)<<3);
		if (ni != ei) ht_note(si, 0, "dst=%016llx%016llx src=%016llx%016llx imm=%#x expect idx=%d got=%d", (unsigned long long)*(uint64_t*)(d+8),(unsigned long long)*(uint64_t*)d,(unsigned long long)*(uint64_t*)(s+8),(unsigned long long)*(uint64_t*)s, imm, ni, ei); else ht_note(si, 1, "");
		if (fl != ef) ht_note(sif, 0, "imm=%#x expect CF/ZF/SF/OF=%x got=%x (rflags=%#llx)", imm, fl, ef, (unsigned long long)rf); else ht_note(sif, 1, "");
		__m128i nm = nat_cmpistrm(D, S, imm); _mm_storeu_si128((void*)ex, nm);
		emu(pcmpistrm, d, s, 0, imm, go);
		if (memcmp(ex, go, 16)) fail_note(sim, "xmm", d, s, imm, ex, go, 16); else ht_note(sim, 1, "");
		/* explicit length */
		fake_state.ss_64.rax = (uint64_t)(int64_t)la; fake_state.ss_64.rdx = (uint64_t)(int64_t)lb;
		int ne = nat_cmpestri(D, la, S, lb, imm);
		int fl2 = (nat_cmpestrc(D,la,S,lb,imm)?1:0) | (nat_cmpestrz(D,la,S,lb,imm)?2:0) | (nat_cmpestrs(D,la,S,lb,imm)?4:0) | (nat_cmpestro(D,la,S,lb,imm)?8:0);
		fake_state.ss_64.rcx = 0xdead; fake_state.ss_64.isf.rflags = 0;
		emu(pcmpestri, d, s, 0, imm, go);
		int ee = (int)fake_state.ss_64.rcx; rf = fake_state.ss_64.isf.rflags;
		int ef2 = ((rf>>0)&1) | (((rf>>6)&1)<<1) | (((rf>>7)&1)<<2) | (((rf>>11)&1)<<3);
		if (ne != ee) ht_note(se, 0, "imm=%#x EAX=%d EDX=%d dst=%016llx%016llx src=%016llx%016llx expect idx=%d got=%d", imm, la, lb, (unsigned long long)*(uint64_t*)(d+8),(unsigned long long)*(uint64_t*)d,(unsigned long long)*(uint64_t*)(s+8),(unsigned long long)*(uint64_t*)s, ne, ee); else ht_note(se, 1, "");
		if (fl2 != ef2) ht_note(sef, 0, "imm=%#x EAX=%d EDX=%d expect CF/ZF/SF/OF=%x got=%x", imm, la, lb, fl2, ef2); else ht_note(sef, 1, "");
		nm = nat_cmpestrm(D, la, S, lb, imm); _mm_storeu_si128((void*)ex, nm);
		emu(pcmpestrm, d, s, 0, imm, go);
		if (memcmp(ex, go, 16)) fail_note(sem, "xmm", d, s, imm, ex, go, 16); else ht_note(sem, 1, "");
	}
	/* *STRM must write XMM0 even when the destination operand is another register */
	{
		ht_stat_t *sx0 = ht_stat("sse4.2", "pcmpistrm->xmm0");
		for (int imm = 0; imm < 128; imm += 3) for (int i = 0; i < 10; i++) {
			uint8_t x0[16], pre[16] = {0}; ht_fill16(d); ht_fill16(s);
			__m128i nm = nat_cmpistrm(_mm_loadu_si128((void*)d), _mm_loadu_si128((void*)s), imm); _mm_storeu_si128((void*)ex, nm);
			g_dstreg = UD_R_XMM3;
			emu(pcmpistrm, d, s, 0, imm, go);
			memcpy(x0, g_x0, 16);
			g_dstreg = UD_R_XMM0; (void)pre;
			if (memcmp(x0, ex, 16) || memcmp(go, d, 16)) fail_note(sx0, "xmm3-dst", d, s, imm, ex, x0, 16); else ht_note(sx0, 1, "");
		}
	}
	return 0;
}
