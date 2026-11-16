#pragma once
#include <stdint.h>
/* kinds for avx_fp128 (flavor: 0 ps, 1 pd, 2 ss, 3 sd) */
enum { FP_ADD, FP_SUB, FP_MUL, FP_DIV, FP_MIN, FP_MAX, FP_SQRT, FP_RCP, FP_RSQRT, FP_HADD, FP_HSUB, FP_ADDSUB, FP_CVTSS2SD, FP_CVTSD2SS };
enum { FP_CVTDQ2PS = 100, FP_CVTPS2DQ, FP_CVTTPS2DQ, FP_CVTDQ2PD, FP_CVTPS2PD, FP_CVTPD2PS, FP_CVTPD2DQ, FP_CVTTPD2DQ };
int avx_fp128(int kind, int flavor, uint8_t *res, const uint8_t *a, const uint8_t *b);
int avx_fp_cvt(int kind, uint8_t *res, const uint8_t *b);
int avx_fp_cmp(int flavor, uint8_t *res, const uint8_t *a, const uint8_t *b, unsigned imm);
uint32_t avx_fp_comis(int dbl, const uint8_t *a, const uint8_t *b);
uint64_t avx_fp_cvt_gpr(int kind, int w, const uint8_t *xin, uint64_t gin, uint8_t *xout);
uint64_t avx_softfma(uint64_t a, uint64_t b, uint64_t c, int is32);
