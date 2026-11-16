/* Tests for opemu_util.c (effective addresses, GPR access) and the full decode -> emulate -> write-back pipeline. */
#include "shim_types.h"
#include "opemu.h"
#include "ssse3_priv.h"
#include <immintrin.h>
#include <nmmintrin.h>
#include <sys/mman.h>
#include <unistd.h>
#include "ht.h"

static x86_saved_state_t st;
static op_t op;
static ud_t ud;

/* decode raw bytes as one 64-bit mode instruction and set up an op_t around it */
static int decode(const uint8_t *code, int len)
{
	static uint8_t win[16];	/* the kernel hands the decoder a 15 byte window, so do the same */
	unsigned n;
	memset(win, 0x90, sizeof win); memcpy(win, code, len);
	ud_init(&ud); ud_set_input_buffer(&ud, win, 15); ud_set_mode(&ud, 64); ud_set_syntax(&ud, UD_SYN_INTEL); ud_set_vendor(&ud, UD_VENDOR_ANY);
	n = ud_disassemble(&ud);
	memset(&op, 0, sizeof op);
	op.state = &st; op.state64 = &st.ss_64; op.state_flavor = SAVEDSTATE_64; op.ud_obj = &ud; op.ring0 = 1; op.insn_len = len;
	if (n != (unsigned)len) { fprintf(stderr, "decode: libudis86 length %u != %d for %02x %02x %02x %02x...\n", n, len, code[0], code[1], code[2], code[3]); return -1; }
	return 0;
}
static uint8_t gin0[16], gin1[16];
static void set_xmm(int n, const uint8_t *v) { memcpy(n ? gin1 : gin0, v, 16); }
/* load xmm0/xmm1, emulate, read xmm0/xmm1 back: nothing but asm and the call in between */
static int __attribute__((noinline)) run_and_grab(int xn, uint8_t *out)
{
	uint8_t t0[16], t1[16];
	int r;
	__asm__ __volatile__("movdqu %0,%%xmm0\n movdqu %1,%%xmm1" :: "m"(*(uint8_t(*)[16])gin0), "m"(*(uint8_t(*)[16])gin1) : "xmm0", "xmm1");
	r = op_sse41_run(&op);
	__asm__ __volatile__("movdqu %%xmm0,%0\n movdqu %%xmm1,%1" : "=m"(*(uint8_t(*)[16])t0), "=m"(*(uint8_t(*)[16])t1));
	memcpy(out, xn ? t1 : t0, 16);
	return r;
}

int test_util(long iters)
{
	(void)iters;
	ht_stat_t *ea = ht_stat("util", "effective-address"), *gp = ht_stat("util", "gpr-decode/write"), *pl = ht_stat("pipeline", "end-to-end");
	memset(&st, 0, sizeof st);
	st.ss_64.rax = 0x1000; st.ss_64.rcx = 0x20; st.ss_64.isf.rip = 0x400000; st.ss_64.r10 = 0x5000;

	/* ---- effective addresses -------------------------------------------------------------------------- */
	static const struct { const char *name; uint8_t code[12]; int len; uint64_t rax, expect; } eas[] = {
		{ "[rax]",             {0x66,0x0f,0x38,0x00,0x00}, 5, 0x1000, 0x1000 },
		{ "[rax+rcx+0x10]",    {0x66,0x0f,0x38,0x00,0x44,0x08,0x10}, 7, 0x1000, 0x1000+0x20+0x10 },
		{ "[rax+rcx*8]",       {0x66,0x0f,0x38,0x00,0x04,0xc8}, 6, 0x1000, 0x1000+0x20*8 },
		{ "[rax-0x10]",        {0x66,0x0f,0x38,0x00,0x40,0xf0}, 6, 0x1000, 0x1000-0x10 },
		{ "[rip+0x10]",        {0x66,0x0f,0x38,0x00,0x05,0x10,0,0,0}, 9, 0x1000, 0x400000+9+0x10 },
		{ "[abs 0x12345678]",  {0x66,0x0f,0x38,0x00,0x04,0x25,0x78,0x56,0x34,0x12}, 10, 0x1000, 0x12345678 },
		{ "[eax] addr32 wrap", {0x67,0x66,0x0f,0x38,0x00,0x00}, 6, 0x100001000ULL, 0x1000 },
		{ "[r10+rcx*8]",       {0x66,0x41,0x0f,0x38,0x00,0x04,0xca}, 7, 0x1000, 0x5000+0x20*8 },
	};
	for (unsigned i = 0; i < sizeof eas / sizeof *eas; i++) {
		uint64_t a = 0; st.ss_64.rax = eas[i].rax;
		if (decode(eas[i].code, eas[i].len) != 0) { ht_note(ea, 0, "%s: decode failed", eas[i].name); continue; }
		const ud_operand_t *m = ud_insn_opr(&ud, 1);
		int rc = m ? opemu_ea(&op, m, &a) : -1;
		if (rc != 0 || a != eas[i].expect) ht_note(ea, 0, "%s: expect %#llx got %#llx (rc=%d)", eas[i].name, (unsigned long long)eas[i].expect, (unsigned long long)a, rc); else ht_note(ea, 1, "");
	}

	/* ---- GPR access semantics ------------------------------------------------------------------------- */
	{
		st.ss_64.rax = 0x1122334455667788ULL;
		uint64_t v; op.state_flavor = SAVEDSTATE_64; op.state64 = &st.ss_64; op.state = &st;
		opemu_gpr_read(&op, UD_R_EAX, &v);  ht_note(gp, v == 0x55667788ULL, "read eax=%llx", (unsigned long long)v);
		opemu_gpr_read(&op, UD_R_AX, &v);   ht_note(gp, v == 0x7788, "read ax=%llx", (unsigned long long)v);
		opemu_gpr_read(&op, UD_R_AH, &v);   ht_note(gp, v == 0x77, "read ah=%llx", (unsigned long long)v);
		opemu_gpr_write(&op, UD_R_EAX, 0xdeadbeefULL); ht_note(gp, st.ss_64.rax == 0xdeadbeefULL, "write eax zero-extends: rax=%llx", (unsigned long long)st.ss_64.rax);
		st.ss_64.rax = 0x1122334455667788ULL; opemu_gpr_write(&op, UD_R_AX, 0xabcd); ht_note(gp, st.ss_64.rax == 0x112233445566abcdULL, "write ax merges: rax=%llx", (unsigned long long)st.ss_64.rax);
		st.ss_64.rax = 0x1122334455667788ULL; opemu_gpr_write(&op, UD_R_AH, 0xee); ht_note(gp, st.ss_64.rax == 0x112233445566ee88ULL, "write ah merges: rax=%llx", (unsigned long long)st.ss_64.rax);
		st.ss_64.r10 = 0x1122334455667788ULL; opemu_gpr_write(&op, UD_R_R10D, 5); ht_note(gp, st.ss_64.r10 == 5, "write r10d zero-extends: r10=%llx", (unsigned long long)st.ss_64.r10);
		st.ss_64.rsi = 0x1122334455667788ULL; opemu_gpr_write(&op, UD_R_SIL, 0x99); ht_note(gp, st.ss_64.rsi == 0x1122334455667799ULL, "write sil: rsi=%llx", (unsigned long long)st.ss_64.rsi);
	}

	/* ---- end to end through op_sse41_run: real bytes, real XMM registers -------------------------------- */
	long pg = sysconf(_SC_PAGESIZE);
	uint8_t *page = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	mprotect(page + pg, pg, PROT_NONE);			/* guard page: an over-wide read faults */
	uint8_t *edge8 = page + pg - 8, *edge4 = page + pg - 4, *edge2 = page + pg - 2, *edge1 = page + pg - 1;
	uint8_t x0[16], x1[16], got[16], exp[16];
	for (int round = 0; round < 200; round++) {
		ht_fill16(x0); ht_fill16(x1); uint64_t rv = ht_rand(); int crashed;
		memset(&st, 0, sizeof st);

		/* pinsrd xmm0, ecx, 2 */
		{ static const uint8_t c[] = {0x66,0x0f,0x3a,0x22,0xc1,0x02}; st.ss_64.rcx = rv; decode(c, 6); set_xmm(0, x0);
		  int rc; HT_GUARD(rc = run_and_grab(0, got), crashed); _mm_storeu_si128((void*)exp, _mm_insert_epi32(_mm_loadu_si128((void*)x0), (int)rv, 2));
		  { char hg[40], he[40], hx[40]; ht_hex(hg, got, 16); ht_hex(he, exp, 16); ht_hex(hx, x0, 16); ht_note(pl, !crashed && rc == 0 && !memcmp(got, exp, 16), "pinsrd xmm0,ecx,2 rc=%d crashed=%d in=%s rcx=%llx got=%s exp=%s", rc, crashed, hx, (unsigned long long)rv, hg, he); } }
		/* pextrd eax, xmm1, 3  (result must zero-extend into rax) */
		{ static const uint8_t c[] = {0x66,0x0f,0x3a,0x16,0xc8,0x03}; st.ss_64.rax = ~0ULL; decode(c, 6); set_xmm(1, x1);
		  int rc; HT_GUARD(rc = run_and_grab(1, got), crashed); uint64_t e = (uint32_t)_mm_extract_epi32(_mm_loadu_si128((void*)x1), 3);
		  ht_note(pl, !crashed && rc == 0 && st.ss_64.rax == e, "pextrd eax,xmm1,3 rax=%llx expect=%llx", (unsigned long long)st.ss_64.rax, (unsigned long long)e); }
		/* popcnt rax, rcx */
		{ static const uint8_t c[] = {0xf3,0x48,0x0f,0xb8,0xc1}; st.ss_64.rcx = rv; st.ss_64.rax = 7; st.ss_64.isf.rflags = 0xfff; decode(c, 5);
		  int rc = op_sse41_run(&op); unsigned long long e = _mm_popcnt_u64(rv);
		  ht_note(pl, rc == 0 && st.ss_64.rax == e && (!!(st.ss_64.isf.rflags & RFZ)) == (rv == 0), "popcnt rax,rcx rax=%llu expect=%llu rflags=%#llx", (unsigned long long)st.ss_64.rax, e, (unsigned long long)st.ss_64.isf.rflags); }
		/* crc32 eax, cl */
		{ static const uint8_t c[] = {0xf2,0x0f,0x38,0xf0,0xc1}; uint32_t acc = (uint32_t)ht_rand(); st.ss_64.rax = acc | (1ULL << 40); st.ss_64.rcx = rv; decode(c, 5);
		  int rc = op_sse41_run(&op); uint32_t e = _mm_crc32_u8(acc, (uint8_t)rv);
		  ht_note(pl, rc == 0 && st.ss_64.rax == e, "crc32 eax,cl rax=%llx expect=%x", (unsigned long long)st.ss_64.rax, e); }
		/* ptest xmm0, xmm1 */
		{ static const uint8_t c[] = {0x66,0x0f,0x38,0x17,0xc1}; st.ss_64.isf.rflags = 0xfff; decode(c, 5); set_xmm(0, x0); set_xmm(1, x1);
		  int rc = run_and_grab(0, got); unsigned e = (_mm_testz_si128(_mm_loadu_si128((void*)x0), _mm_loadu_si128((void*)x1)) ? RFZ : 0) | (_mm_testc_si128(_mm_loadu_si128((void*)x0), _mm_loadu_si128((void*)x1)) ? RFC : 0);
		  unsigned g = st.ss_64.isf.rflags & 0x8d5;
		  ht_note(pl, rc == 0 && g == e, "ptest flags expect=%#x got=%#x", e, g); }
		/* pmovzxbw xmm0, [rax] with the 8-byte operand at the very end of a page (must not read 16 bytes) */
		{ static const uint8_t c[] = {0x66,0x0f,0x38,0x30,0x00}; for (int i = 0; i < 8; i++) edge8[i] = (uint8_t)ht_rand(); st.ss_64.rax = (uint64_t)(uintptr_t)edge8; decode(c, 5); set_xmm(0, x0);
		  int rc; HT_GUARD(rc = run_and_grab(0, got), crashed); uint8_t s16[16] = {0}; memcpy(s16, edge8, 8); _mm_storeu_si128((void*)exp, _mm_cvtepu8_epi16(_mm_loadu_si128((void*)s16)));
		  ht_note(pl, !crashed && rc == 0 && !memcmp(got, exp, 16), "pmovzxbw xmm0,[edge8] rc=%d crashed=%d", rc, crashed); }
		/* pmovzxbq xmm0, [rax]: 2-byte operand at the end of a page */
		{ static const uint8_t c[] = {0x66,0x0f,0x38,0x32,0x00}; edge2[0] = (uint8_t)ht_rand(); edge2[1] = (uint8_t)ht_rand(); st.ss_64.rax = (uint64_t)(uintptr_t)edge2; decode(c, 5); set_xmm(0, x0);
		  int rc; HT_GUARD(rc = run_and_grab(0, got), crashed); uint8_t s16[16] = {0}; memcpy(s16, edge2, 2); _mm_storeu_si128((void*)exp, _mm_cvtepu8_epi64(_mm_loadu_si128((void*)s16)));
		  ht_note(pl, !crashed && rc == 0 && !memcmp(got, exp, 16), "pmovzxbq xmm0,[edge2] rc=%d crashed=%d", rc, crashed); }
		/* roundss xmm0, [rax], 1 : 4-byte operand at the end of a page */
		{ static const uint8_t c[] = {0x66,0x0f,0x3a,0x0a,0x00,0x01}; float f = (float)(int)(ht_rand() % 2001) / 4.0f - 250.0f; memcpy(edge4, &f, 4); st.ss_64.rax = (uint64_t)(uintptr_t)edge4; decode(c, 6); set_xmm(0, x0);
		  int rc; HT_GUARD(rc = run_and_grab(0, got), crashed); uint8_t s16[16] = {0}; memcpy(s16, edge4, 4); _mm_storeu_ps((float*)exp, _mm_round_ss(_mm_loadu_ps((float*)x0), _mm_loadu_ps((float*)s16), 1));
		  ht_note(pl, !crashed && rc == 0 && !memcmp(got, exp, 16), "roundss xmm0,[edge4],1 rc=%d crashed=%d", rc, crashed); }
		/* insertps xmm0, xmm1, 0x10 (register form: what WindowServer executes) and insertps xmm0, [rax], 0x10 (memory form) */
		{ static const uint8_t c[] = {0x66,0x0f,0x3a,0x21,0xc1,0x10}; decode(c, 6); set_xmm(0, x0); set_xmm(1, x1);
		  int rc; HT_GUARD(rc = run_and_grab(0, got), crashed); _mm_storeu_ps((float*)exp, _mm_insert_ps(_mm_loadu_ps((float*)x0), _mm_loadu_ps((float*)x1), 0x10));
		  ht_note(pl, !crashed && rc == 0 && !memcmp(got, exp, 16), "insertps xmm0,xmm1,0x10 rc=%d crashed=%d", rc, crashed); }
		{ static const uint8_t c[] = {0x66,0x0f,0x3a,0x21,0x00,0x10}; uint8_t s16[16] = {0}; for (int i = 0; i < 4; i++) edge4[i] = (uint8_t)ht_rand();
		  st.ss_64.rax = (uint64_t)(uintptr_t)edge4; decode(c, 6); set_xmm(0, x0);
		  int rc; HT_GUARD(rc = run_and_grab(0, got), crashed); memcpy(s16, edge4, 4); _mm_storeu_ps((float*)exp, _mm_insert_ps(_mm_loadu_ps((float*)x0), _mm_loadu_ps((float*)s16), 0x10));
		  ht_note(pl, !crashed && rc == 0 && !memcmp(got, exp, 16), "insertps xmm0,[edge4],0x10 rc=%d crashed=%d", rc, crashed); }
		/* pextrb [rax], xmm1, 5 : 1-byte store at the end of a page */
		{ static const uint8_t c[] = {0x66,0x0f,0x3a,0x14,0x08,0x05}; edge1[0] = 0; st.ss_64.rax = (uint64_t)(uintptr_t)edge1; decode(c, 6); set_xmm(1, x1);
		  int rc; HT_GUARD(rc = run_and_grab(1, got), crashed);
		  ht_note(pl, !crashed && rc == 0 && edge1[0] == x1[5], "pextrb [edge1],xmm1,5 rc=%d crashed=%d byte=%02x expect=%02x", rc, crashed, edge1[0], x1[5]); }
		/* pshufb-style SIB + RIP-relative load through the SSSE3 path (shared grab code): pmovsxwd xmm0, [rax+rcx*2+8] */
		{ static const uint8_t c[] = {0x66,0x0f,0x38,0x23,0x44,0x48,0x08}; uint8_t *buf = page + 256; for (int i = 0; i < 64; i++) buf[i] = (uint8_t)ht_rand();
		  st.ss_64.rax = (uint64_t)(uintptr_t)buf; st.ss_64.rcx = 5; decode(c, 7); set_xmm(0, x0);
		  int rc; HT_GUARD(rc = run_and_grab(0, got), crashed); uint8_t s16[16] = {0}; memcpy(s16, buf + 5*2 + 8, 8); _mm_storeu_si128((void*)exp, _mm_cvtepi16_epi32(_mm_loadu_si128((void*)s16)));
		  ht_note(pl, !crashed && rc == 0 && !memcmp(got, exp, 16), "pmovsxwd xmm0,[rax+rcx*2+8] rc=%d crashed=%d", rc, crashed); }
	}
	munmap(page, 2 * pg);
	return 0;
}
