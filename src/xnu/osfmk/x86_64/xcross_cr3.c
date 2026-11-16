/*
 * xcross: skip the user<->kernel CR3 switch on CPUs that are not vulnerable to Meltdown.
 *
 * XNU 10.13.2+ runs user mode on a mirrored, user-only PML4 (pm_ucr3) and the kernel on the task's full PML4, and every
 * kernel entry and exit reloads CR3 (idt64.s: L_dispatch on entry, ks_64bit_return on exit). With PCID that is cheap; on
 * CPUs without PCID (AMD K8/K10, QEMU TCG) each reload flushes the whole TLB, which dominates the cost of every trap.
 * AMD CPUs are not affected by Meltdown, so there the split buys nothing.
 *
 * When enabled, the two `mov %rax,%cr3` instructions in the trampolines are overwritten with a 3 byte NOP once, at boot,
 * before any other CPU is running; user mode then runs on the task's full page tables (kernel mappings stay supervisor-only).
 * The trampolines run on the user page table where no kernel data is mapped, so a runtime flag test there is impossible;
 * patching the text is the only way. The doublemap maps the same physical pages, so the patch covers both views.
 *
 * Boot-arg kpti: 0 = share one CR3 (fast), 1 = keep the split. Default: share on AMD, split on everything else.
 */
#include <mach/mach_types.h>
#include <i386/proc_reg.h>
#include <i386/cpuid.h>
#include <pexpert/pexpert.h>
#include <kern/misc_protos.h>
#include <string.h>

extern uint8_t xcross_cr3_entry[], xcross_cr3_exit[];

void xcross_share_user_cr3(void);

void
xcross_share_user_cr3(void)
{
	static int done;
	static const uint8_t mov_rax_cr3[3] = { 0x0f, 0x22, 0xd8 };
	static const uint8_t nop3[3]        = { 0x0f, 0x1f, 0x00 };
	uint32_t kpti = 1;
	int amd, want_shared;
	uintptr_t cr0;

	if (done)
		return;
	done = 1;

	amd = (strncmp(cpuid_info()->cpuid_vendor, CPUID_VID_AMD, 12) == 0);
	want_shared = PE_parse_boot_argn("kpti", &kpti, sizeof(kpti)) ? (kpti == 0) : amd;
	if (!want_shared)
		return;
	if (cpuid_features() & CPUID_FEATURE_PCID) {
		printf("KPTI: CPU has PCID, keeping the user/kernel CR3 switch\n");
		return;
	}
	if (memcmp(xcross_cr3_entry, mov_rax_cr3, 3) || memcmp(xcross_cr3_exit, mov_rax_cr3, 3)) {
		printf("KPTI: trampoline code not as expected, leaving the CR3 switch alone\n");
		return;
	}

	cr0 = get_cr0();
	set_cr0(cr0 & ~CR0_WP);
	memcpy(xcross_cr3_entry, nop3, 3);
	memcpy(xcross_cr3_exit, nop3, 3);
	set_cr0(cr0);
	__asm__ __volatile__("" ::: "memory");
	printf("KPTI: user and kernel share one address space (CR3 switch on kernel entry/exit disabled)\n");
}
