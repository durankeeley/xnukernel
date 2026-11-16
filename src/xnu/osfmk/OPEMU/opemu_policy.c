/*
 * OPEMU policy: decide which instruction sets the opcode emulator may emulate.
 *
 * Default ("auto"): emulate an instruction set only if the CPU itself reports (CPUID) that it lacks it.
 * If the CPU does have it, a #UD for such an instruction is a genuine bad opcode and is not emulated.
 *
 * Boot-args (value 0 = never emulate, 1 = always emulate, 2 = auto, the default):
 *   opemu=<n>          default for every set
 *   opemu_sse3=<n>  opemu_ssse3=<n>  opemu_sse41=<n>  opemu_sse42=<n>     per-set override
 *   opemu_log=1        print each set the first time it is used, and a running count
 * Example: a hypervisor that passes SSSE3 through natively  ->  opemu_ssse3=0
 */
#include <stdint.h>
#include <string.h>
#include <i386/cpuid.h>
#include <pexpert/pexpert.h>
#include <kern/misc_protos.h>

#include "opemu.h"

enum { OPEMU_OFF = 0, OPEMU_ON = 1, OPEMU_AUTO = 2 };
enum { OPC_SSE3, OPC_SSSE3, OPC_SSE41, OPC_SSE42, OPC_COUNT };

static const char *const opc_name[OPC_COUNT] = { "sse3", "ssse3", "sse41", "sse42" };
static const char *const opc_arg[OPC_COUNT]  = { "opemu_sse3", "opemu_ssse3", "opemu_sse41", "opemu_sse42" };
static const uint32_t     opc_ecx_bit[OPC_COUNT] = { 1u << 0, 1u << 9, 1u << 19, 1u << 20 };

static int      opc_mode[OPC_COUNT];
static int      opc_hw[OPC_COUNT];
static int      opc_emulate[OPC_COUNT];
static uint32_t opc_hits[OPC_COUNT];
static uint32_t opemu_log;
static volatile int policy_ready;

static const char *const list_sse3[] = {
	"addsubpd", "addsubps", "haddpd", "haddps", "hsubpd", "hsubps", "lddqu", "movddup",
	"movshdup", "movsldup", "fisttp", "fistp", "mwait", "monitor", NULL };
static const char *const list_ssse3[] = {
	"pshufb", "phaddw", "phaddd", "phaddsw", "phsubw", "phsubd", "phsubsw", "pmaddubsw",
	"pmulhrsw", "psignb", "psignw", "psignd", "pabsb", "pabsw", "pabsd", "palignr", NULL };
static const char *const list_sse41[] = {
	"pblendw", "blendps", "blendpd", "blendvps", "blendvpd", "pblendvb", "pextrb", "pextrd",
	"pextrq", "pinsrb", "pinsrd", "pinsrq", "extractps", "insertps", "ptest", "pminsb",
	"pminsd", "pminuw", "pminud", "pmaxsb", "pmaxsd", "pmaxuw", "pmaxud", "pmulld", "pmuldq",
	"pcmpeqq", "packusdw", "pmovsxbw", "pmovsxbd", "pmovsxbq", "pmovsxwd", "pmovsxwq",
	"pmovsxdq", "pmovzxbw", "pmovzxbd", "pmovzxbq", "pmovzxwd", "pmovzxwq", "pmovzxdq",
	"roundps", "roundpd", "roundss", "roundsd", "dpps", "dppd", "mpsadbw", "phminposuw",
	"movntdqa", NULL };
static const char *const list_sse42[] = {
	"pcmpestri", "pcmpestrm", "pcmpistri", "pcmpistrm", "pcmpgtq", "crc32", "popcnt", NULL };
static const char *const *const opc_list[OPC_COUNT] = { list_sse3, list_ssse3, list_sse41, list_sse42 };

static int
in_list(const char *const *l, const char *name)
{
	for (; *l; l++)
		if (strcmp(*l, name) == 0)
			return 1;
	return 0;
}

/* classification is by mnemonic name, which is slow: cache it per mnemonic (0 = not yet classified, else class + 2) */
static int8_t cls_cache[1024];

static int
opemu_classify(uint32_t mnemonic)
{
	const char *name;
	int c, found = -1;

	if (mnemonic < sizeof cls_cache && cls_cache[mnemonic])
		return cls_cache[mnemonic] - 2;

	name = ud_lookup_mnemonic((enum ud_mnemonic_code)mnemonic);
	if (name) {
		for (c = 0; c < OPC_COUNT; c++)
			if (in_list(opc_list[c], name)) { found = c; break; }
	}
	if (mnemonic < sizeof cls_cache)
		cls_cache[mnemonic] = (int8_t)(found + 2);
	return found;	/* -1: not an extension instruction, never gated */
}

static void
opemu_policy_init(void)
{
	uint32_t reg[4];
	uint32_t def = OPEMU_AUTO, v;

	if (PE_parse_boot_argn("opemu", &v, sizeof(v)) && v <= OPEMU_AUTO)
		def = v;
	if (!PE_parse_boot_argn("opemu_log", &opemu_log, sizeof(opemu_log)))
		opemu_log = 0;

	do_cpuid(1, reg);	/* raw CPUID: what the silicon (or hypervisor) really advertises */

	for (int c = 0; c < OPC_COUNT; c++) {
		opc_mode[c] = def;
		if (PE_parse_boot_argn(opc_arg[c], &v, sizeof(v)) && v <= OPEMU_AUTO)
			opc_mode[c] = v;
		opc_hw[c] = (reg[ecx] & opc_ecx_bit[c]) ? 1 : 0;
		opc_emulate[c] = (opc_mode[c] == OPEMU_ON) || (opc_mode[c] == OPEMU_AUTO && !opc_hw[c]);
		printf("OPEMU: %-5s mode=%s cpu=%s -> %s\n", opc_name[c],
		    opc_mode[c] == OPEMU_ON ? "on" : opc_mode[c] == OPEMU_OFF ? "off" : "auto",
		    opc_hw[c] ? "has" : "lacks",
		    opc_emulate[c] ? "EMULATE" : "do not emulate");
	}
	policy_ready = 1;
}

/* Returns 1 if the emulator may handle this instruction. */
int
opemu_allow(uint32_t mnemonic)
{
	if (!policy_ready)
		opemu_policy_init();

	int c = opemu_classify(mnemonic);
	if (c < 0)
		return 1;
	if (!opc_emulate[c])
		return 0;

	uint32_t n = ++opc_hits[c];
	if (opemu_log && (n == 1 || (n & 0xFFF) == 0))
		printf("OPEMU: %s %s emulated (%u so far)\n", opc_name[c],
		    ud_lookup_mnemonic((enum ud_mnemonic_code)mnemonic), n);
	return 1;
}

/*
 * Fast-path hooks (opemu_fast.c): the two hottest instructions, PSHUFB and PALIGNR, skip the generic decoder.
 * They still obey the policy and are counted/logged like everything else.
 */
int
opemu_ssse3_enabled(void)
{
	if (!policy_ready)
		opemu_policy_init();
	return opc_emulate[OPC_SSSE3];
}

void
opemu_ssse3_hit(int is_palignr)
{
	uint32_t n = ++opc_hits[OPC_SSSE3];
	if (opemu_log && (n == 1 || (n & 0xFFF) == 0))
		printf("OPEMU: ssse3 %s emulated (%u so far)\n", is_palignr ? "palignr" : "pshufb", n);
}
