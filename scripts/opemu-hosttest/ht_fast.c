/* Fast path (opemu_fast.c) tests and benchmark: PSHUFB / PALIGNR, every register pair, every immediate, memory forms. */
#include "shim_types.h"
#include "opemu.h"
#include "ssse3_priv.h"
#include <immintrin.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include "ht.h"

static x86_saved_state_t fst;
static uint8_t in256[256] __attribute__((aligned(16))), out256[256] __attribute__((aligned(16)));

/* load all 16 XMM registers, run the fast path, read them all back: nothing but asm and the call in between */
static int __attribute__((noinline)) run_fast(const uint8_t *code, int ring0)
{
	int r;
	opemu_xmm_restore(in256);
	r = opemu_fast(code, &fst.ss_64, ring0);
	opemu_xmm_save(out256);
	return r;
}

static __m128i nat_pshufb(__m128i d, __m128i s) { return _mm_shuffle_epi8(d, s); }
#include "imm.inc"

static int encode(uint8_t *b, int palignr, int reg, int rm, int imm, int force_rex)
{
	int n = 0, rex = 0x40 | ((reg >> 3) << 2) | (rm >> 3);
	b[n++] = 0x66;
	if (rex != 0x40 || force_rex) b[n++] = (uint8_t)rex;
	b[n++] = 0x0f;
	if (palignr) { b[n++] = 0x3a; b[n++] = 0x0f; } else { b[n++] = 0x38; b[n++] = 0x00; }
	b[n++] = (uint8_t)(0xc0 | ((reg & 7) << 3) | (rm & 7));
	if (palignr) b[n++] = (uint8_t)imm;
	return n;
}

static void fillregs(void) { for (int i = 0; i < 16; i++) ht_fill16(in256 + 16 * i); }

int test_fast(long iters)
{
	ht_stat_t *sp = ht_stat("fast", "pshufb reg,reg"), *sa = ht_stat("fast", "palignr reg,reg"), *sm = ht_stat("fast", "mem forms"), *sf = ht_stat("fast", "fallback (must return 0)");
	uint8_t code[16], ex[16];
	opemu_ssse3_enabled();	/* warm up the lazy policy init (its printf would clobber the XMM registers we load below) */
	memset(&fst, 0, sizeof fst);

	for (long it = 0; it < iters / 20; it++) {
		int reg = ht_rand() % 16, rm = ht_rand() % 16, n;
		fillregs();
		/* pshufb */
		n = encode(code, 0, reg, rm, 0, ht_rand() & 1);
		int r = run_fast(code, 1);
		_mm_storeu_si128((void*)ex, nat_pshufb(_mm_loadu_si128((void*)(in256 + 16 * reg)), _mm_loadu_si128((void*)(in256 + 16 * rm))));
		int ok = r == n && !memcmp(out256 + 16 * reg, ex, 16);
		for (int i = 0; i < 16 && ok; i++) if (i != reg && memcmp(out256 + 16 * i, in256 + 16 * i, 16)) ok = 0;
		ht_note(sp, ok, "reg=%d rm=%d len=%d/%d (other registers disturbed or wrong result)", reg, rm, r, n);
		/* palignr, a few immediates each time, plus the boundaries */
		for (int k = 0; k < 8; k++) {
			int imm = k < 4 ? (int)(ht_rand() & 255) : (int[]){0, 15, 16, 31}[k - 4];
			n = encode(code, 1, reg, rm, imm, ht_rand() & 1);
			r = run_fast(code, 1);
			_mm_storeu_si128((void*)ex, nat_alignr_x(_mm_loadu_si128((void*)(in256 + 16 * reg)), _mm_loadu_si128((void*)(in256 + 16 * rm)), imm));
			ok = r == n && !memcmp(out256 + 16 * reg, ex, 16);
			for (int i = 0; i < 16 && ok; i++) if (i != reg && memcmp(out256 + 16 * i, in256 + 16 * i, 16)) ok = 0;
			ht_note(sa, ok, "reg=%d rm=%d imm=%d len=%d/%d", reg, rm, imm, r, n);
		}
	}
	/* every palignr immediate once on a fixed pair */
	for (int imm = 0; imm < 256; imm++) { fillregs(); int n = encode(code, 1, 3, 9, imm, 0); int r = run_fast(code, 1);
		_mm_storeu_si128((void*)ex, nat_alignr_x(_mm_loadu_si128((void*)(in256 + 48)), _mm_loadu_si128((void*)(in256 + 144)), imm));
		ht_note(sa, r == n && !memcmp(out256 + 48, ex, 16), "imm=%d", imm); }

	/* memory forms: [rax] [rax+disp8] [rax+disp32] [rax+rcx*4] [rax+rcx*8+disp8] [r12] [r13+disp8] [rsp+disp8] [abs index only] [rip+disp32] ... */
	{
		long pg = sysconf(_SC_PAGESIZE);
		uint8_t *page = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		mprotect(page + pg, pg, PROT_NONE);
		uint8_t *buf = page + pg - 16;	/* operand at the very end of the page: must read exactly 16 bytes */
		static const struct { const char *name; uint8_t b[14]; int len; int base, idx, scale; int64_t disp; int rip; int palignr; } f[] = {
			{ "[rax]",             {0x66,0x0f,0x38,0x00,0x00}, 5, 0, -1, 1, 0, 0, 0 },
			{ "[rax+8]",           {0x66,0x0f,0x38,0x00,0x40,0x08}, 6, 0, -1, 1, 8, 0, 0 },
			{ "[rax-0x10]",        {0x66,0x0f,0x38,0x00,0x40,0xf0}, 6, 0, -1, 1, -16, 0, 0 },
			{ "[rax+0x100]",       {0x66,0x0f,0x38,0x00,0x80,0x00,0x01,0,0}, 9, 0, -1, 1, 0x100, 0, 0 },
			{ "[rax+rcx*4]",       {0x66,0x0f,0x38,0x00,0x04,0x88}, 6, 0, 1, 4, 0, 0, 0 },
			{ "[rax+rcx*8+8]",     {0x66,0x0f,0x38,0x00,0x44,0xc8,0x08}, 7, 0, 1, 8, 8, 0, 0 },
			{ "[r12]",             {0x66,0x41,0x0f,0x38,0x00,0x04,0x24}, 7, 12, -1, 1, 0, 0, 0 },
			{ "[r13+8]",           {0x66,0x41,0x0f,0x38,0x00,0x45,0x08}, 7, 13, -1, 1, 8, 0, 0 },
			{ "[rsp+8]",           {0x66,0x0f,0x38,0x00,0x44,0x24,0x08}, 7, 4, -1, 1, 8, 0, 0 },
			{ "[r10+r11*2]",       {0x66,0x43,0x0f,0x38,0x00,0x04,0x5a}, 7, 10, 11, 2, 0, 0, 0 },
			{ "[rip+0x20]",        {0x66,0x0f,0x38,0x00,0x05,0x20,0,0,0}, 9, -1, -1, 1, 0x20, 1, 0 },
			{ "palignr [rax+rdx]", {0x66,0x0f,0x3a,0x0f,0x04,0x10,0x07}, 7, 0, 2, 1, 0, 0, 1 },
			{ "palignr [rip-0x8],5",{0x66,0x0f,0x3a,0x0f,0x05,0xf8,0xff,0xff,0xff,0x05}, 10, -1, -1, 1, -8, 1, 1 },
		};
		for (unsigned k = 0; k < sizeof f / sizeof *f; k++) for (int rep = 0; rep < 40; rep++) {
			fillregs(); for (int i = 0; i < 16; i++) buf[i] = (uint8_t)ht_rand();
			memset(&fst, 0, sizeof fst);
			uint64_t *gp[16] = { &fst.ss_64.rax, &fst.ss_64.rcx, &fst.ss_64.rdx, &fst.ss_64.rbx, &fst.ss_64.isf.rsp, &fst.ss_64.rbp, &fst.ss_64.rsi, &fst.ss_64.rdi,
				&fst.ss_64.r8, &fst.ss_64.r9, &fst.ss_64.r10, &fst.ss_64.r11, &fst.ss_64.r12, &fst.ss_64.r13, &fst.ss_64.r14, &fst.ss_64.r15 };
			uint64_t target = (uint64_t)(uintptr_t)buf;
			uint64_t idxv = 3, off = 0;
			if (f[k].idx >= 0) { *gp[f[k].idx] = idxv; off = idxv * f[k].scale; }
			if (f[k].rip) fst.ss_64.isf.rip = target - f[k].disp - f[k].len;
			else if (f[k].base >= 0) *gp[f[k].base] = target - f[k].disp - off;
			int reg = 5;	/* xmm5 as destination */
			memcpy(code, f[k].b, f[k].len); code[f[k].palignr ? 3 : 4 - 0] = code[f[k].palignr ? 3 : 4];
			/* patch the reg field (bits 5:3) of the modrm byte to xmm5, keeping mod and rm */
			int mo = (f[k].b[1] & 0xf0) == 0x40 ? 5 : 4;	/* index of the modrm byte: 66 [REX] 0F xx xx modrm */
			code[mo] = (uint8_t)((code[mo] & 0xc7) | (reg << 3));
			int imm = f[k].palignr ? code[f[k].len - 1] : 0;
			int r = run_fast(code, 1);
			__m128i D = _mm_loadu_si128((void*)(in256 + 16 * reg)), S = _mm_loadu_si128((void*)buf);
			_mm_storeu_si128((void*)ex, f[k].palignr ? nat_alignr_x(D, S, imm) : nat_pshufb(D, S));
			ht_note(sm, r == f[k].len && !memcmp(out256 + 16 * reg, ex, 16), "%s: len=%d/%d", f[k].name, r, f[k].len);
		}
		munmap(page, 2 * pg);
	}

	/* things it must NOT handle: the generic path takes over */
	{
		static const uint8_t no[][8] = {
			{0x0f,0x38,0x00,0xc1},			/* MMX pshufb (no 66) */
			{0x66,0x0f,0x38,0x01,0xc1},		/* phaddw */
			{0x66,0x0f,0x38,0x17,0xc1},		/* ptest */
			{0xf2,0x66,0x0f,0x38,0x00,0xc1},	/* extra prefix */
			{0x67,0x66,0x0f,0x38,0x00,0x00},	/* 32-bit addressing */
			{0x66,0x0f,0x3a,0x22,0xc1,0x02},	/* pinsrd */
		};
		for (unsigned k = 0; k < sizeof no / sizeof *no; k++) { fillregs(); memcpy(code, no[k], 8); int r = run_fast(code, 1); ht_note(sf, r == 0, "case %u returned %d", k, r); }
	}
	return 0;
}

/* ------------------------------------------------------------------------------------------------ benchmark */
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
extern int opemu_allow(uint32_t);

/* what opemu_utrap does on the generic path for one instruction */
static int __attribute__((noinline)) slow_gen(const uint8_t *window, x86_saved_state_t *st, int with_text)
{
	ud_t ud; op_t op; unsigned n;
	ud_init(&ud); ud_set_input_buffer(&ud, window, 15); ud_set_mode(&ud, 64); ud_set_syntax(&ud, with_text ? UD_SYN_INTEL : NULL); ud_set_vendor(&ud, UD_VENDOR_ANY);
	n = ud_disassemble(&ud);
	if (!n || !opemu_allow(ud_insn_mnemonic(&ud))) return -1;
	op.state = st; op.state64 = &st->ss_64; op.state_flavor = SAVEDSTATE_64; op.ud_obj = &ud; op.ring0 = 1; op.insn_len = n;
	return op_sse3x_run(&op) ? -1 : (int)n;
}

static int slow(const uint8_t *w, x86_saved_state_t *st) { return slow_gen(w, st, 1); }
static int mid(const uint8_t *w, x86_saved_state_t *st) { return slow_gen(w, st, 0); }

void bench_fast(void)
{
	static const struct { const char *n; uint8_t b[16]; } cases[] = {
		{ "pshufb xmm1,xmm2",       {0x66,0x0f,0x38,0x00,0xca} },
		{ "palignr xmm1,xmm2,5",    {0x66,0x0f,0x3a,0x0f,0xca,0x05} },
		{ "pshufb xmm1,[rax]",      {0x66,0x0f,0x38,0x00,0x08} },
		{ "palignr xmm1,[rax+8],5", {0x66,0x0f,0x3a,0x0f,0x48,0x08,0x05} },
	};
	uint8_t data[64] __attribute__((aligned(16))); for (int i = 0; i < 64; i++) data[i] = (uint8_t)(i * 7);
	const long N = 1500000;
	printf("\nspeed (host, per emulated instruction; the kernel adds the same trap entry/exit to both):\n");
	for (unsigned k = 0; k < sizeof cases / sizeof *cases; k++) {
		uint8_t win[16]; memset(win, 0x90, 16); memcpy(win, cases[k].b, 15);
		memset(&fst, 0, sizeof fst); fst.ss_64.rax = (uint64_t)(uintptr_t)data; fillregs();
		opemu_xmm_restore(in256);
		double t0 = now(); for (long i = 0; i < N; i++) slow(win, &fst); double t1 = now();
		for (long i = 0; i < N; i++) mid(win, &fst); double t2 = now();
		for (long i = 0; i < N; i++) opemu_fast(win, &fst.ss_64, 1); double t3 = now();
		printf("  %-24s original %7.1f ns   no-text %7.1f ns   fast %6.1f ns   (%.1fx / %.1fx vs original)\n", cases[k].n, (t1 - t0) / N, (t2 - t1) / N, (t3 - t2) / N, (t1 - t0) / (t2 - t1), (t1 - t0) / (t3 - t2));
	}
}
