#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
extern sigjmp_buf ht_jb; extern volatile int ht_armed;
/* run stmt; set crashed=1 if it faults (SIGSEGV/SIGBUS) */
#define HT_GUARD(stmt, crashed) do { (crashed) = 0; ht_armed = 1; if (!sigsetjmp(ht_jb, 1)) { stmt; } else (crashed) = 1; ht_armed = 0; } while (0)
typedef struct { const char *name; const char *set; long n, fail; char first[640]; } ht_stat_t;
uint64_t ht_rand(void);
void ht_fill16(uint8_t *p);
ht_stat_t *ht_stat(const char *set, const char *name);
static inline void ht_hex(char *o, const uint8_t *p, int n) { for (int i = n - 1; i >= 0; i--) o += sprintf(o, "%02x", p[i]); }
void ht_note(ht_stat_t *st, int ok, const char *fmt, ...);
int test_ssse3(long iters);
int test_sse3(long iters);
int test_sse41(long iters);
int test_util(long iters);
int test_fast(long iters);
int test_decode(long iters);
void bench_fast(void);
#define RFC 0x0001
#define RFZ 0x0040
