/*
 * xcross: definitions for third-party / Apple-internal symbols that the open-source XNU tree references but does not contain
 * (libfirehose_kernel, libTrustCache, compiler-rt profiling, C++ runtime). Behaviour follows the real libraries where simple.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <TrustCache/API.h>

/* kernel C++ has no static destructors; objects built without -fno-c++-static-destructors may still reference this */
int
__cxa_atexit(void (*f)(void *), void *p, void *d)
{
	(void)f; (void)p; (void)d;
	return 0;
}

/* compiler-rt PGO hooks (coverage builds only) */
uint64_t
__llvm_profile_get_size_for_buffer_internal(const char *a, const char *b, const char *c, const char *d, const char *e, const char *f)
{
	(void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
	return 0;
}

int
__llvm_profile_write_buffer_internal(char *buf, const char *a, const char *b, const char *c, const char *d, const char *e, const char *f)
{
	(void)buf; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
	return 0;
}

/* libTrustCache: runtime setup (the AMFI kext supplies the rest of the interface later) */
void
trustCacheInitializeRuntime(TrustCacheRuntime_t *runtime, TrustCacheMutableRuntime_t *mutableRuntime,
    bool allowSecondStaticTC, bool allowEngineeringTC, bool allowLegacyTC, const void *img4Runtime)
{
	(void)allowLegacyTC; (void)img4Runtime;
	memset(runtime, 0, sizeof(*runtime));
	if (mutableRuntime) {
		memset(mutableRuntime, 0, sizeof(*mutableRuntime));
	}
	runtime->allowSecondStaticTC = allowSecondStaticTC;
	runtime->allowEngineeringTC = allowEngineeringTC;
}
