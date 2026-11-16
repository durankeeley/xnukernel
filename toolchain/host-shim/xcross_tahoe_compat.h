/* xcross: fallbacks for Apple-clang-only builtins that Tahoe's XNU uses (kalloc_type type metadata).
 * Upstream clang lacks them. With an empty signature / mixed summary the typed allocators still work; they just stop
 * segregating allocations by pointer-content (a security hardening, not needed for functionality). */
#pragma once
#define __builtin_xnu_type_signature(t)       ""
/* NOTE: with summary 0 every type is "data-only" (kalloc_type then has no signature zones; kalloc.c is patched to cope). A non-zero
 * summary breaks static asserts in OSData.h etc., which need true per-type information. */
#define __builtin_xnu_type_summary(t)         0
#ifdef __cplusplus
#define __builtin_xnu_types_compatible(a, b)  __is_same(a, b)
#else
#define __builtin_xnu_types_compatible(a, b)  __builtin_types_compatible_p(a, b)
#endif
