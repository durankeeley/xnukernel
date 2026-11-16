/* xcross stand-in for Apple's (non-public) libTrustCache <TrustCache/API.h>.
 * The kernel only uses these as types and as a function-pointer table that the AMFI kext fills in at runtime, so only layouts
 * matter. Opaque structs are over-allocated so a kext writing into them cannot overrun our allocation. */
#ifndef XCROSS_TRUSTCACHE_API_H
#define XCROSS_TRUSTCACHE_API_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define kUUIDSize        16
#define kTCEntryHashSize 20

typedef uint8_t TCError_t;
typedef uint8_t TCComponent_t;
typedef struct _TCReturn {
	TCError_t     error;
	TCComponent_t component;
	uint32_t      uniqueError;
} TCReturn_t;

enum {
	kTCReturnSuccess = 0,
	kTCReturnError,
	kTCReturnNotFound,
	kTCReturnDuplicate,
	kTCReturnInvalid,
};

typedef uint8_t TCType_t;
enum {
	kTCTypeInvalid = 0,
	kTCTypeStatic,
	kTCTypeEngineering,
	kTCTypeLegacy,
	kTCTypeDTRS,
	kTCTypeLTRS,
	kTCTypeCryptex1BootOS,
	kTCTypeCryptex1BootApp,
	kTCTypeCryptex1,
	kTCTypeCryptex1Generic,
	kTCTypeCryptex1GenericSupplemental,
	kTCTypeOtherCryptex,
	kTCTypeTotal,
};

typedef uint8_t TCQueryType_t;
enum {
	kTCQueryTypeStatic    = 0x01,
	kTCQueryTypeLoadable  = 0x02,
	kTCQueryTypeAll       = 0xFF,
	kTCQueryTypeTotal     = 3,
};

typedef uint64_t TCCapabilities_t;
#define kTCCapabilityNone 0ULL

typedef struct _TrustCache {
	uint64_t opaque[32];
} TrustCache_t;

typedef struct _TrustCacheRuntime {
	bool     allowEngineeringTC;      /* kernel-visible policy flags */
	bool     allowSecondStaticTC;
	uint64_t opaque[128];
} TrustCacheRuntime_t;

typedef struct _TrustCacheMutableRuntime {
	uint64_t opaque[128];
} TrustCacheMutableRuntime_t;

typedef struct _TrustCacheQueryToken {
	const TrustCache_t *trustCache;
	const void         *trustCacheEntry;
	uint64_t            opaque[4];
} TrustCacheQueryToken_t;

typedef struct _TCTypeConfig {
	const char *entitlementValue;     /* needed to load this trust cache type; NULL = none */
	uint32_t    flags;
} TCTypeConfig_t;
static const TCTypeConfig_t TCTypeConfig[kTCTypeTotal] = { { 0, 0 } };

void trustCacheInitializeRuntime(TrustCacheRuntime_t *runtime, TrustCacheMutableRuntime_t *mutableRuntime,
    bool allowSecondStaticTC, bool allowEngineeringTC, bool allowLegacyTC, const void *img4Runtime);

#endif
