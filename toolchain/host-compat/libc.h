/* Darwin's <libc.h> umbrella, for XNU's build-time tools on Linux. */
#pragma once
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdint.h>
typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_size_t;
typedef uintptr_t vm_address_t;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
/* x86 pages are 4 KiB */
#define mach_vm_round_page(x) (((uint64_t)(x) + 4095) & ~(uint64_t)4095)
