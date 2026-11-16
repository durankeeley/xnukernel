/* xcross: Darwin <sys/cdefs.h> type-declaration macros, for building migcom against XNU's Mach headers on a glibc host. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define __kernel_ptr_semantics
#define __enum_decl(name, type, ...)          typedef type name; enum __VA_ARGS__
#define __enum_closed_decl(name, type, ...)   typedef type name; enum __VA_ARGS__
#define __options_decl(name, type, ...)       typedef type name; enum __VA_ARGS__
#define __options_closed_decl(name, type, ...) typedef type name; enum __VA_ARGS__
#define __enum_open
#define __enum_closed
