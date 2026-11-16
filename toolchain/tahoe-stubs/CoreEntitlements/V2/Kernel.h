/* xcross stand-in for <CoreEntitlements/V2/Kernel.h> */
#ifndef XCROSS_CE_V2_KERNEL_H
#define XCROSS_CE_V2_KERNEL_H
#include <CoreEntitlements/V2/API.h>
/* V2 kernel-side types (private to Apple): sizes over-allocated, see the notes in V2/API.h */
typedef int32_t CEReturn_t;
#define kCEReturnSuccess 0
#define kCEReturnError   1
typedef struct { const uint8_t *data; size_t length; } CEBuffer_t;
typedef struct { uint64_t opaque[6]; } CEElement_t;
typedef struct CEContext { uint64_t opaque[24]; } CEContext_t;
enum { kCEContextTypeProvisioningProfile = 1, kCEContextTypeProvisioningProfileEntitlements };
typedef struct CEKernelAPI {
	uint64_t version;
	CEReturn_t (*contextGetLegacyContext)(const CEContext_t *ctx, CEQueryContext_t *legacy);
	CEReturn_t (*contextValueForKey)(const CEContext_t *ctx, const char *key, CEElement_t *value);
	CEReturn_t (*elementMatchBool)(const CEElement_t *element, bool value);
	CEReturn_t (*elementGetCEBuffer)(const CEElement_t *element, CEBuffer_t *buffer);
	CEReturn_t (*contextInitWithTypeLegacy)(int type, const uint8_t *data, size_t length, CEContext_t *ctx, ...);
	CEReturn_t (*contextCheckAcceleration)(const CEContext_t *ctx);
	CEReturn_t (*contextEvaluateAcceleration)(const CEContext_t *ctx, size_t *length);
	CEReturn_t (*contextAccelerate)(CEContext_t *ctx);
	void *reserved[64];
} CEKernelAPI_t;
#endif
