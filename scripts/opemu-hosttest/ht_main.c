/* OPEMU host test driver: runs every emulated instruction against the host CPU's native implementation. */
#include "ht.h"
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "shim_types.h"
#include "opemu.h"
#include <signal.h>
sigjmp_buf ht_jb; volatile int ht_armed;
static void on_fault(int sig){ if (ht_armed) siglongjmp(ht_jb, 1); signal(sig, SIG_DFL); raise(sig); }
#define MAXST 128
static ht_stat_t stats[MAXST]; static int nst;
static uint64_t s0 = 0x9E3779B97F4A7C15ull, s1 = 0xD1B54A32D192ED03ull;
uint64_t ht_rand(void){ uint64_t a=s0,b=s1; s0=b; a^=a<<23; s1=a^b^(a>>17)^(b>>26); return s1+b; }
void ht_fill16(uint8_t *p){
	int mode = ht_rand() % 10;
	for (int i = 0; i < 16; i++) {
		switch (mode) {
		case 0: p[i] = 0; break;
		case 1: p[i] = 0xff; break;
		case 2: p[i] = (ht_rand() & 1) ? 0x80 : 0x7f; break;
		case 3: p[i] = (uint8_t)(ht_rand() % 3) - 1; break;
		case 4: p[i] = (ht_rand() & 7) == 0 ? 0 : (uint8_t)ht_rand(); break;   /* sprinkled NULs: string ops */
		default: p[i] = (uint8_t)ht_rand();
		}
	}
}
ht_stat_t *ht_stat(const char *set, const char *name){
	for (int i = 0; i < nst; i++) if (!strcmp(stats[i].name, name)) return &stats[i];
	ht_stat_t *s = &stats[nst++]; memset(s, 0, sizeof *s); s->name = name; s->set = set; return s;
}
void ht_note(ht_stat_t *st, int ok, const char *fmt, ...){
	st->n++;
	if (ok) return;
	if (st->fail++ == 0) { va_list ap; va_start(ap, fmt); vsnprintf(st->first, sizeof st->first, fmt, ap); va_end(ap); }
}
/* libudis86 pieces referenced by the OPEMU sources but never called by the arithmetic routines */
/* policy shim: boot-arg opemu=1 forces emulation on regardless of what this (modern) host CPU supports */
int PE_parse_boot_argn(const char *n, void *p, int sz){ (void)sz; if (!strcmp(n, "opemu")) { *(uint32_t*)p = 1; return 1; } return 0; }
int copyin(const user_addr_t f, char *t, size_t n){ memcpy(t, (void*)f, n); return 0; }
int copyout(const char *f, user_addr_t t, size_t n){ memcpy((void*)t, f, n); return 0; }
int retrieve_reg(x86_saved_state_t *s, const ud_type_t b, uint64_t *w){ (void)s;(void)b;(void)w; return -1; }
int main(int argc, char **argv){
	long iters = argc > 1 ? atol(argv[1]) : 20000;
	setvbuf(stdout, NULL, _IOLBF, 0); signal(SIGSEGV, on_fault); signal(SIGBUS, on_fault);
	test_ssse3(iters); test_sse3(iters); test_sse41(iters); test_util(iters); test_fast(iters); test_decode(iters);
	int bad = 0; const char *last = "";
	printf("%-8s %-12s %10s %10s  %s\n", "set", "instruction", "cases", "mismatch", "first mismatch");
	for (int i = 0; i < nst; i++) {
		if (strcmp(last, stats[i].set)) { last = stats[i].set; }
		printf("%-8s %-12s %10ld %10ld  %s\n", stats[i].set, stats[i].name, stats[i].n, stats[i].fail, stats[i].fail ? stats[i].first : "ok");
		bad += stats[i].fail != 0;
	}
	printf("\n%d of %d instruction forms differ from the hardware\n", bad, nst);
	bench_fast();
	return bad != 0;
}
