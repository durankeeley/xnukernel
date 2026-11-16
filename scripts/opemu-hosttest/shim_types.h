/* Host-test shim: just enough of XNU's saved-state types for the OPEMU routines to compile on Linux. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef struct {
	uint64_t rdi, rsi, rdx, r10, r8, r9, rax, rbx, rcx, rbp, r11, r12, r13, r14, r15, gs, fs;
	uint32_t _pad;
	struct { uint64_t trapno, trapfn, err, rip, cs, rflags, rsp, ss; } isf;
} x86_saved_state64_t;
typedef struct {
	uint32_t gs, fs, es, ds, edi, esi, ebp, cr2, ebx, edx, ecx, eax, trapno, err, eip, cs, efl, uesp, ss;
} x86_saved_state32_t;
typedef struct { x86_saved_state64_t ss_64; x86_saved_state32_t ss_32; } x86_saved_state_t;
static inline int is_saved_state64(const x86_saved_state_t *s) { (void)s; return 1; }
static inline x86_saved_state64_t *saved_state64(x86_saved_state_t *s) { return &s->ss_64; }
static inline x86_saved_state32_t *saved_state32(x86_saved_state_t *s) { return &s->ss_32; }
typedef uint64_t user_addr_t;
int copyin(const user_addr_t from, char *to, size_t n);
int copyout(const char *from, user_addr_t to, size_t n);
