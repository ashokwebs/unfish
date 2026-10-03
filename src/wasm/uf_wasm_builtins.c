/* 128-bit multiply builtins that wasi-libc needs (strtol/strtod's intscan and
 * clock_nanosleep) but which normally come from compiler-rt's
 * libclang_rt.builtins-wasm32.a, which a plain clang install lacks. Only
 * linked into the browser build (`make wasm-web`). Implemented with 64-bit
 * arithmetic only, since a 128-bit `*` or `/` here would call back into these
 * very functions. */

#include <stdint.h>

__extension__ typedef __int128 ti_int;
__extension__ typedef unsigned __int128 tu_int;

/* Full 64x64 -> 128-bit unsigned product, from 32-bit partial products. */
static void mul_u64(uint64_t a, uint64_t b, uint64_t* hi, uint64_t* lo) {
    uint64_t a_lo = (uint32_t)a, a_hi = a >> 32;
    uint64_t b_lo = (uint32_t)b, b_hi = b >> 32;
    uint64_t ll = a_lo * b_lo;
    uint64_t lh = a_lo * b_hi;
    uint64_t hl = a_hi * b_lo;
    uint64_t hh = a_hi * b_hi;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    *lo = (mid << 32) | (uint32_t)ll;
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

static tu_int make_tu(uint64_t hi, uint64_t lo) {
    return ((tu_int)hi << 64) | lo;
}

/* Low 128 bits of a * b (wrapping), identical for signed and unsigned. */
ti_int __multi3(ti_int a, ti_int b) {
    tu_int ua = (tu_int)a, ub = (tu_int)b;
    uint64_t a_lo = (uint64_t)ua, a_hi = (uint64_t)(ua >> 64);
    uint64_t b_lo = (uint64_t)ub, b_hi = (uint64_t)(ub >> 64);
    uint64_t hi, lo;
    mul_u64(a_lo, b_lo, &hi, &lo);
    hi += a_hi * b_lo + a_lo * b_hi;
    return (ti_int)make_tu(hi, lo);
}

/* Signed 128-bit multiply; sets *overflow when the true product does not
 * fit in 128 bits. */
ti_int __muloti4(ti_int a, ti_int b, int* overflow) {
    *overflow = 0;
    int negative = (a < 0) != (b < 0);
    tu_int ua = a < 0 ? (tu_int)0 - (tu_int)a : (tu_int)a;
    tu_int ub = b < 0 ? (tu_int)0 - (tu_int)b : (tu_int)b;
    uint64_t a_lo = (uint64_t)ua, a_hi = (uint64_t)(ua >> 64);
    uint64_t b_lo = (uint64_t)ub, b_hi = (uint64_t)(ub >> 64);

    /* |a| * |b| as a 256-bit value; any bit at or above bit 128 overflows. */
    int wide = (a_hi != 0 && b_hi != 0);
    uint64_t hi, lo, c1_hi, c1_lo, c2_hi, c2_lo;
    mul_u64(a_lo, b_lo, &hi, &lo);
    mul_u64(a_hi, b_lo, &c1_hi, &c1_lo);
    mul_u64(a_lo, b_hi, &c2_hi, &c2_lo);
    if (c1_hi != 0 || c2_hi != 0) wide = 1;
    uint64_t sum = hi + c1_lo;
    if (sum < hi) wide = 1;
    uint64_t sum2 = sum + c2_lo;
    if (sum2 < sum) wide = 1;
    tu_int mag = make_tu(sum2, lo);

    /* The magnitude must fit the signed range: up to 2^127 - 1, or exactly
     * 2^127 when the result is negative. */
    tu_int limit = ((tu_int)1 << 127) - (negative ? 0 : 1);
    if (wide || mag > limit) *overflow = 1;

    return negative ? (ti_int)((tu_int)0 - mag) : (ti_int)mag;
}
