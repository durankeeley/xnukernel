/* Minimal <mach-o/arch.h>: the host is always little-endian x86_64 here. */
#pragma once
#include <mach/machine.h>
#include <architecture/byte_order.h>	/* enum NXByteOrder */
typedef struct {
	const char *name;
	cpu_type_t cputype;
	cpu_subtype_t cpusubtype;
	enum NXByteOrder byteorder;
	const char *description;
} NXArchInfo;
static inline const NXArchInfo *NXGetLocalArchInfo(void)
{
	static const NXArchInfo x86_64 = { "x86_64", CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL,
					   NX_LittleEndian, "Intel x86-64" };
	return &x86_64;
}
