/* SSE3 tests: emulated routine (real OPEMU source) vs native instruction. Floating-point lanes: bit-exact, except
 * that any NaN equals any NaN (payload/sign of generated QNaNs is not compared). */
#include "shim_types.h"
#include "opemu.h"
#include "sse3_priv.h"
#include <immintrin.h>
#include <math.h>
#include "ht.h"

extern void addsubpd(sse3_t*), addsubps(sse3_t*), haddpd(sse3_t*), haddps(sse3_t*), hsubpd(sse3_t*), hsubps(sse3_t*),
	lddqu(sse3_t*), movddup(sse3_t*), movshdup(sse3_t*), movsldup(sse3_t*);
typedef void (*emu_fn)(sse3_t *);

static void emu(emu_fn f, const uint8_t *d, const uint8_t *s, uint8_t *out)
{
	sse3_t t; memset(&t, 0, sizeof t);
	memcpy(&t.dst, d, 16); memcpy(&t.src, s, 16);
	f(&t); memcpy(out, &t.res, 16);
}
#define N2(n, e) static void n_##n(const uint8_t*d,const uint8_t*s,uint8_t*o){ \
	__m128 Df=_mm_loadu_ps((const float*)d),Sf=_mm_loadu_ps((const float*)s); (void)Df;(void)Sf; \
	__m128d Dd=_mm_loadu_pd((const double*)d),Sd=_mm_loadu_pd((const double*)s); (void)Dd;(void)Sd; e; }
N2(addsubps, _mm_storeu_ps((float*)o,_mm_addsub_ps(Df,Sf)))
N2(addsubpd, _mm_storeu_pd((double*)o,_mm_addsub_pd(Dd,Sd)))
N2(haddps,   _mm_storeu_ps((float*)o,_mm_hadd_ps(Df,Sf)))
N2(haddpd,   _mm_storeu_pd((double*)o,_mm_hadd_pd(Dd,Sd)))
N2(hsubps,   _mm_storeu_ps((float*)o,_mm_hsub_ps(Df,Sf)))
N2(hsubpd,   _mm_storeu_pd((double*)o,_mm_hsub_pd(Dd,Sd)))
N2(lddqu,    _mm_storeu_si128((void*)o,_mm_lddqu_si128((const void*)s)))
N2(movddup,  _mm_storeu_pd((double*)o,_mm_movedup_pd(Sd)))
N2(movshdup, _mm_storeu_ps((float*)o,_mm_movehdup_ps(Sf)))
N2(movsldup, _mm_storeu_ps((float*)o,_mm_moveldup_ps(Sf)))

static const struct { const char *name; emu_fn e; void (*n)(const uint8_t*, const uint8_t*, uint8_t*); int dbl; } tests[] = {
#define T(x, dbl) { #x, x, n_##x, dbl }
	T(addsubps,0), T(addsubpd,1), T(haddps,0), T(haddpd,1), T(hsubps,0), T(hsubpd,1),
	T(lddqu,1), T(movddup,1), T(movshdup,0), T(movsldup,0),
};

static int same(const uint8_t *a, const uint8_t *b, int dbl)
{
	if (dbl) { for (int i = 0; i < 2; i++) { double x, y; memcpy(&x, a + 8*i, 8); memcpy(&y, b + 8*i, 8);
		if (!(isnan(x) && isnan(y)) && memcmp(a + 8*i, b + 8*i, 8)) return 0; } }
	else { for (int i = 0; i < 4; i++) { float x, y; memcpy(&x, a + 4*i, 4); memcpy(&y, b + 4*i, 4);
		if (!(isnan(x) && isnan(y)) && memcmp(a + 4*i, b + 4*i, 4)) return 0; } }
	return 1;
}

int test_sse3(long iters)
{
	uint8_t d[16], s[16], ex[16], go[16];
	for (unsigned k = 0; k < sizeof tests / sizeof *tests; k++) {
		ht_stat_t *st = ht_stat("sse3", tests[k].name);
		for (long i = 0; i < iters; i++) {
			ht_fill16(d); ht_fill16(s);
			if (i % 3 == 0) { /* friendly finite values so ordering bugs are visible */
				float *fd = (float*)d, *fs = (float*)s; for (int j = 0; j < 4; j++) { fd[j] = (float)(int)(ht_rand()%2000) - 1000; fs[j] = (float)(int)(ht_rand()%2000) - 1000; } }
			tests[k].n(d, s, ex); emu(tests[k].e, d, s, go);
			if (!same(ex, go, tests[k].dbl)) { char a[40], b[40], c[40], e[40]; ht_hex(a,d,16); ht_hex(b,s,16); ht_hex(c,ex,16); ht_hex(e,go,16);
				ht_note(st, 0, "dst=%s src=%s expect=%s got=%s", a, b, c, e); } else ht_note(st, 1, "");
		}
	}
	return 0;
}
