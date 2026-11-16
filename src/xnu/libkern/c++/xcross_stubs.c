/*
 * Cross-build stubs. On macOS these come from Apple's compiler-rt profile
 * runtime and the C++ ABI runtime; they are unavailable in this toolchain.
 * PGO data collection is reported as unsupported; static dtors never run.
 */
#include <stdint.h>

int __cxa_atexit(void (*f)(void *), void *p, void *d)
{
	(void)f; (void)p; (void)d;
	return 0;
}

uint64_t __llvm_profile_get_size_for_buffer_internal(const char *a, const char *b,
    const char *c, const char *d, const char *e, const char *f)
{
	(void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
	return 0;
}

int __llvm_profile_write_buffer_internal(char *buf, const char *a, const char *b,
    const char *c, const char *d, const char *e, const char *f)
{
	(void)buf; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
	return -1;
}
