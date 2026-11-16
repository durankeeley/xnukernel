/* Host test for opemu_avx.c: runs each candidate VEX instruction natively and in the emulator and compares the results. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include "opemu_avx.h"

struct st { uint8_t y[16][32]; uint64_t g[16]; uint64_t rflags; } __attribute__((aligned(64)));
extern void run_native(void *code, struct st *s);

static sigjmp_buf jb;
static void onsig(int s) { siglongjmp(jb, 1); }
static uint64_t rs = 88172645463325252ULL;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

static uint8_t membuf[2][128] __attribute__((aligned(64)));
static uint8_t *memcur;
static int mrd(void *u, uint64_t a, void *d, size_t n) { memcpy(d, (void *)(uintptr_t)a, n); return 0; }
static int mwr(void *u, uint64_t a, const void *s, size_t n) { memcpy((void *)(uintptr_t)a, s, n); return 0; }

static void randomize(struct st *s, int edge)
{
	for (int i = 0; i < 16; i++) for (int j = 0; j < 4; j++) {
		uint64_t v = rnd();
		if (edge == 1 && (rnd() & 3) == 0) v = (rnd() & 1) ? 0 : ~0ULL;
		if (edge == 2) { v &= 0x8000800080008000ULL | (rnd() & 0x0001000100010001ULL); if (rnd() & 1) v |= 0x7FFF7FFF7FFF7FFFULL & rnd(); }
		memcpy(&s->y[i][j * 8], &v, 8);
	}
	for (int i = 0; i < 16; i++) { s->g[i] = rnd(); if (edge == 1 && (rnd() & 3) == 0) s->g[i] = (rnd() & 1) ? 0 : ~0ULL; if (edge == 3) s->g[i] &= 0x3F | ((uint64_t)0x3F << 8); }
	s->rflags = 0x202 | (rnd() & 0x8D5);
}

int main(int argc, char **argv)
{
	int iters = argc > 1 ? atoi(argv[1]) : 20;
	int verbose = argc > 2;
	uint8_t *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	setvbuf(stdout, NULL, _IOLBF, 0); signal(SIGBUS, onsig); signal(SIGILL, onsig); signal(SIGSEGV, onsig); signal(SIGFPE, onsig);
	long tested = 0, bad = 0, unsup = 0, nonsense = 0;
	static int unsup_seen[4][4][256];
	for (int map = 1; map <= 3; map++) for (int pp = 0; pp < 4; pp++) for (int opc = 0; opc < 256; opc++)
	for (int W = 0; W < 2; W++) for (int L = 0; L < 2; L++) for (int form = 0; form < 3; form++) {
		int reg = 1 + rnd() % 14, vvvv = rnd() % 16, rm = 1 + rnd() % 14;
		if (reg == 4 || rm == 4) continue;
		if (opc >= 0xF0 && map >= 2 && vvvv == 4) continue;
		if (map == 1 && opc == 0x77 && form) continue;
		uint8_t ins[16]; int n = 0;
		int mem = form == 2;
		uint8_t modrm;
		if (mem) modrm = (uint8_t)(((reg & 7) << 3) | 6);                 /* [rsi] */
		else modrm = (uint8_t)(0xC0 | ((reg & 7) << 3) | (rm & 7));
		/* groups use the reg field as an opcode extension: sweep it via 'reg' low bits */
		int Bx = mem ? 0 : (rm >> 3), Rx = reg >> 3;
		ins[n++] = 0xC4;
		ins[n++] = (uint8_t)((!Rx << 7) | (1 << 6) | (!Bx << 5) | map);
		ins[n++] = (uint8_t)((W << 7) | ((~vvvv & 15) << 3) | (L << 2) | pp);
		ins[n++] = (uint8_t)opc;
		if (!(map == 1 && opc == 0x77)) ins[n++] = modrm;
		if (map == 3) ins[n++] = (uint8_t)(rnd() & 0xFF);
		else if (map == 1 && (opc >= 0x70 && opc <= 0x73 || opc == 0xC6 || opc == 0xC4 || opc == 0xC5 || opc == 0xC2)) ins[n++] = (uint8_t)(rnd() & 0x1F);
		/* is4 register operand (vpblendvb etc.) lives in imm[7:4] and is random: fine */
		int imm_given = 1; (void)imm_given;
		for (int it = 0; it < iters; it++) {
			struct st a, b;
			randomize(&a, it % 4);
			a.g[6] = (uint64_t)(uintptr_t)membuf[0];
			if (mem) { for (int k = 0; k < 128; k++) membuf[0][k] = (uint8_t)rnd(); }
			memcpy(membuf[1], membuf[0], 128);
			b = a; b.g[6] = (uint64_t)(uintptr_t)membuf[1];
			memcpy(code, ins, n); code[n] = 0xC3;
			/* native */
			int ok = 1;
			if (sigsetjmp(jb, 1) == 0) { run_native(code, &a); } else ok = 0;
			if (!ok) { if (it == 0) nonsense++; break; }
			/* emulator */
			avx_ctx_t c; memset(&c, 0, sizeof c);
			memcpy(c.y, b.y, sizeof c.y); memcpy(c.g, b.g, sizeof c.g); c.rflags = b.rflags; c.is64 = 1;
			c.rip = (uint64_t)(uintptr_t)code; c.rd = mrd; c.wr = mwr;
			uint8_t padded[32]; memset(padded, 0, 32); memcpy(padded, ins, n);
			unsigned len = 0; int rc = avx_exec(&c, padded, &len);
			if (rc == AVX_UNSUPPORTED) { if (!unsup_seen[map][pp][opc]) { unsup_seen[map][pp][opc] = 1; unsup++; if (verbose) printf("UNSUP map=%d pp=%d op=%02x W=%d L=%d\n", map, pp, opc, W, L); } break; }
			tested++;
			if (rc) { printf("ERR rc=%d map=%d pp=%d op=%02x\n", rc, map, pp, opc); bad++; break; }
			if (len != (unsigned)n) { printf("LEN map=%d pp=%d op=%02x W=%d L=%d emu=%u real=%d\n", map, pp, opc, W, L, len, n); bad++; break; }
			/* compare (RFLAGS: only arithmetic bits; PF/AF undefined for BMI: compare CF,ZF,SF,OF only) */
			c.g[6] = a.g[6];
			int same = !memcmp(c.y, a.y, sizeof c.y) && !memcmp(c.g, a.g, sizeof c.g) && !memcmp(membuf[0], membuf[1], 128);
			uint64_t fm = 0x8C1;
			int fsame = (c.rflags & fm) == (a.rflags & fm);
			if (!same || !fsame) {
				printf("MISMATCH map=%d pp=%d op=%02x W=%d L=%d form=%s reg=%d vvvv=%d rm=%d imm=%02x iter=%d%s\n", map, pp, opc, W, L, mem ? "mem" : "reg", reg, vvvv, rm, ins[n - 1], it, !fsame ? " (flags)" : "");
				if (verbose) {
					for (int i = 0; i < 16; i++) if (memcmp(c.y[i], a.y[i], 32)) { printf("  ymm%d emu:", i); for (int k = 0; k < 32; k++) printf("%02x", c.y[i][k]); printf("\n       real:"); for (int k = 0; k < 32; k++) printf("%02x", a.y[i][k]); printf("\n"); }
				}
				bad++; break;
			}
		}
	}
	printf("tested=%ld mismatches=%ld unsupported-forms=%ld (invalid on host: %ld)\n", tested, bad, unsup, nonsense);
	return bad != 0;
}
