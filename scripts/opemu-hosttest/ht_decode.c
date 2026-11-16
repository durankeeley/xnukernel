/* Decoder coverage: every SSE4.1/SSE4.2/POPCNT/CRC32 encoding must decode to the right mnemonic in libudis86. */
#include "shim_types.h"
#include "opemu.h"
#include "ssse3_priv.h"
#include "ht.h"

static const struct { const char *name; uint8_t b[10]; int len; } enc[] = {
	/* 66 0F 38 xx /r  (modrm c1 = xmm0, xmm1) */
#define M38(n, op) { n, {0x66,0x0f,0x38,op,0xc1}, 5 }
	M38("pblendvb",0x10), M38("blendvps",0x14), M38("blendvpd",0x15), M38("ptest",0x17),
	M38("pmovsxbw",0x20), M38("pmovsxbd",0x21), M38("pmovsxbq",0x22), M38("pmovsxwd",0x23), M38("pmovsxwq",0x24), M38("pmovsxdq",0x25),
	M38("pmuldq",0x28), M38("pcmpeqq",0x29), M38("packusdw",0x2b),
	M38("pmovzxbw",0x30), M38("pmovzxbd",0x31), M38("pmovzxbq",0x32), M38("pmovzxwd",0x33), M38("pmovzxwq",0x34), M38("pmovzxdq",0x35),
	M38("pcmpgtq",0x37), M38("pminsb",0x38), M38("pminsd",0x39), M38("pminuw",0x3a), M38("pminud",0x3b),
	M38("pmaxsb",0x3c), M38("pmaxsd",0x3d), M38("pmaxuw",0x3e), M38("pmaxud",0x3f), M38("pmulld",0x40), M38("phminposuw",0x41),
	{ "movntdqa", {0x66,0x0f,0x38,0x2a,0x00}, 5 },	/* movntdqa xmm0,[rax] */
	/* 66 0F 3A xx /r ib */
#define M3A(n, op) { n, {0x66,0x0f,0x3a,op,0xc1,0x05}, 6 }
	M3A("roundps",0x08), M3A("roundpd",0x09), M3A("roundss",0x0a), M3A("roundsd",0x0b), M3A("blendps",0x0c), M3A("blendpd",0x0d), M3A("pblendw",0x0e),
	M3A("pextrb",0x14), M3A("pextrd",0x16), M3A("extractps",0x17), M3A("pinsrb",0x20), M3A("insertps",0x21), M3A("pinsrd",0x22),
	M3A("dpps",0x40), M3A("dppd",0x41), M3A("mpsadbw",0x42),
	M3A("pcmpestrm",0x60), M3A("pcmpestri",0x61), M3A("pcmpistrm",0x62), M3A("pcmpistri",0x63),
	{ "pextrq", {0x66,0x48,0x0f,0x3a,0x16,0xc1,0x01}, 7 }, { "pinsrq", {0x66,0x48,0x0f,0x3a,0x22,0xc1,0x01}, 7 },
	{ "popcnt", {0xf3,0x0f,0xb8,0xc1}, 4 }, { "popcnt", {0xf3,0x48,0x0f,0xb8,0xc1}, 5 },
	{ "crc32", {0xf2,0x0f,0x38,0xf0,0xc1}, 5 }, { "crc32", {0xf2,0x0f,0x38,0xf1,0xc1}, 5 }, { "crc32", {0xf2,0x48,0x0f,0x38,0xf1,0xc1}, 6 },
	/* the two the fast path owns, for completeness */
	{ "pshufb", {0x66,0x0f,0x38,0x00,0xc1}, 5 }, { "palignr", {0x66,0x0f,0x3a,0x0f,0xc1,0x05}, 6 },
};

int test_decode(long iters)
{
	(void)iters;
	ht_stat_t *st = ht_stat("decode", "libudis86 coverage");
	for (unsigned k = 0; k < sizeof enc / sizeof *enc; k++) {
		static uint8_t win[16]; ud_t ud; unsigned n;
		memset(win, 0x90, sizeof win); memcpy(win, enc[k].b, enc[k].len);
		ud_init(&ud); ud_set_input_buffer(&ud, win, 15); ud_set_mode(&ud, 64); ud_set_syntax(&ud, NULL); ud_set_vendor(&ud, UD_VENDOR_ANY);
		n = ud_disassemble(&ud);
		const char *got = ud_lookup_mnemonic(ud_insn_mnemonic(&ud));
		int ok = n == (unsigned)enc[k].len && got && !strcmp(got, enc[k].name);
		if (!ok) fprintf(stderr, "decode MISS: %-10s %02x %02x %02x %02x -> len %u mnemonic '%s'\n", enc[k].name, enc[k].b[0], enc[k].b[1], enc[k].b[2], enc[k].b[3], n, got ? got : "?");
		ht_note(st, ok, "%s: decoded as '%s' len %u (expected len %d)", enc[k].name, got ? got : "?", n, enc[k].len);
	}
	return 0;
}
