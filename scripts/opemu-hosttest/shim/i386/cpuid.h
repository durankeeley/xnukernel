/* host shim: the bits of xnu's i386/cpuid.h that opemu_policy.c uses */
#pragma once
#include <stdint.h>
typedef enum { eax = 0, ebx = 1, ecx = 2, edx = 3 } cpuid_register_t;
static inline void do_cpuid(uint32_t sel, uint32_t *d)
{
	__asm__ __volatile__("cpuid" : "=a"(d[0]), "=b"(d[1]), "=c"(d[2]), "=d"(d[3]) : "a"(sel), "c"(0));
}
