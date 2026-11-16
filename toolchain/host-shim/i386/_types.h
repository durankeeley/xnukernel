/* Host (Linux) stand-in for XNU's <i386/_types.h>: just the __darwin_*
 * names, sized for x86_64, without clashing with glibc's own types. */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int8_t   __int8_t_darwin_unused;
typedef long              __darwin_intptr_t;
typedef unsigned int      __darwin_natural_t;
typedef int               __darwin_ct_rune_t;
typedef long              __darwin_ptrdiff_t;
typedef unsigned long     __darwin_size_t;
typedef long              __darwin_ssize_t;
typedef int               __darwin_wchar_t;
typedef int               __darwin_rune_t;
typedef int               __darwin_wint_t;
typedef unsigned long     __darwin_clock_t;
typedef unsigned int      __darwin_socklen_t;
typedef long              __darwin_time_t;
typedef __builtin_va_list __darwin_va_list;
