#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "fpu/softfloat.h"
#include "vector_internals.h"
#include "cpu.h"
#include "xt_reduction.h"

#define FLOAT16_CNAN 0x7e00
#define FP16_EXP_SHIFT 10
#define FP16_EXP_MASK 0x1f
#define FP16_EXP_BIAS 0xf
#define FP16_FRAC_MASK 0x3ff
#define FP16_EXP_SIZE  5
#define FP16_FRAC_SIZE 10
#define FP16_EXP_MAX   0xf
#define FP16_EXP_MIN   -0xf
#define FP16_MAX       0x7bff
#define FP16_CNAN      0x7e00
#define BF16_EXP_SHIFT 7
#define BF16_EXP_MASK  0xff
#define BF16_EXP_BIAS  0x7f
#define BF16_FRAC_MASK 0x7f
#define E4M3_FRAC_BF16_SHIFT 0x4

typedef struct unpacked_float {
    uint64_t frac;
    int64_t  frac_signed;
    uint16_t exp;
    int16_t  exp_signed;
    bool     sign;
    bool     iszero;
    bool     isdenormal;
} unpacked_float;

/* Extend frac to 38 bit */
static inline uint64_t xt_extend_frac_38_f16(uint64_t frac, bool denormal,
                                             uint8_t *denormal_shift)
{
    if (denormal) {
        uint8_t shift = clz64(frac);
        /* 64 - shift is no zero frac value */
        *denormal_shift = 11 - (64 - shift);
        return frac << (38 - (64 - shift));
    } else {
        return frac << (37 - 10) | (1ULL << 37);
    }
}

static inline uint64_t xt_extend_frac_38_f32(uint64_t frac, bool denormal,
                                             uint8_t *denormal_shift)
{
    if (denormal) {
        uint8_t shift = clz64(frac);
        /* 64 - shift is no zero frac value */
        *denormal_shift = 24 - (64 - shift);
        return frac << (38 - (64 - shift));
    } else {
        return frac << (37 - 23) | (1ULL << 37);
    }
}

static inline uint64_t xt_extend_frac_38_bf16(uint64_t frac, bool denormal,
                                             uint8_t *denormal_shift)
{
    if (denormal) {
        uint8_t shift = clz64(frac);
        /* 64 - shift is no zero frac value */
        *denormal_shift = 8 - (64 - shift);
        return frac << (38 - (64 - shift));
    } else {
        return frac << (37 - 7) | (1ULL << 37);
    }
}

/* Get signed fraction */
static inline int64_t xt_get_frac_signed(unpacked_float *f)
{
    return f->sign ? -f->frac : f->frac;
}

/* Align frac to exp max */
static void
xt_align_frac_expmax(unpacked_float *f, uint16_t exp_max,
                     bool stick, uint8_t denormal_shift, float_status *s)
{
    unsigned short shift = exp_max - f->exp;
    uint64_t tmp;
    if (shift == 0) {
        return;
    } else { /* process denormal */
        if ((shift > 0) && !f->exp) {
            /* Denormal exponent is 0 - EXPBIAS + 1 */
            shift = exp_max + denormal_shift - 1;
        }
    }
    if (shift >= 64) {
        if (f->frac) {
            s->float_exception_flags |= float_flag_inexact;
            f->frac = stick;
        }
        return;
    }
    tmp = f->frac >> shift;
    if (f->frac != (tmp << shift)) {
        s->float_exception_flags |= float_flag_inexact;
        f->frac = tmp | stick;
    } else {
        f->frac = tmp;
    }
}

/* Shift to canonical */
static void xt_canon_fp16(unpacked_float *f, float_status *s)
{
    /* MSB from 1 */
    uint8_t msb = 64 - clz64(f->frac);

    /* Keep 1 + 10 + 3 bits for fp16 fraction before round */
    uint8_t shift;

    /* Move the fraction msb to 38 bit from 1*/
    if (msb > 38) {
        f->exp_signed = f->exp_signed + msb - 38;
    } else {
        f->exp_signed = f->exp_signed - (38 - msb);
    }
    /* We don't really shift fraction to 39 bit, so just keep 14 bits here */
    if (msb > 14) {
        uint64_t jam;
        shift = msb - 14;
        jam = f->frac >> shift;
        if (jam << shift != f->frac) {
            f->frac = jam | 0x1;
            s->float_exception_flags |= float_flag_inexact;
        } else {
            f->frac = jam;
        }
    } else if (msb < 14) {
        shift = 14 - msb;
        f->frac = f->frac << shift;
    }
}

/* Shift to canonical */
static void xt_canon_bf16(unpacked_float *f, float_status *s)
{
    /* MSB from 1 */
    uint8_t msb = 64 - clz64(f->frac);

    /* Keep 1 + 7 + 3 bits for bf16 fraction before round */
    uint8_t shift;

    /* Move the fraction msb to 38 bit from 1*/
    if (msb > 38) {
        f->exp_signed = f->exp_signed + msb - 38;
    } else {
        f->exp_signed = f->exp_signed - (38 - msb);
    }
    /* We don't really shift fraction to 39 bit, so just keep 11 bits here */
    if (msb > 11) {
        uint64_t jam;
        shift = msb - 11;
        jam = f->frac >> shift;
        if (jam << shift != f->frac) {
            f->frac = jam | 0x1;
            s->float_exception_flags |= float_flag_inexact;
        } else {
            f->frac = jam;
        }
    } else if (msb < 11) {
        shift = 11 - msb;
        f->frac = f->frac << shift;
    }
}

/* Pack a float from parts, but do not canonicalize.  */
static uint64_t xt_pack_raw64(const unpacked_float *p, int f_size, int e_size)
{
    uint64_t ret;

    ret = (uint64_t)p->sign << (f_size + e_size);
    ret = deposit64(ret, f_size, e_size, p->exp);
    ret = deposit64(ret, 0, f_size, p->frac);
    return ret;
}

static float16 xt_round_fp16(unpacked_float *f, int frm, bool sat,
                             float_status *s)
{
    uint64_t round;
    float16 result;
    round = get_round(frm, f->frac, 3);
    f->frac = (f->frac >> 3) + round;
    /* Round move the msb bit */
    if (f->frac & (1 << (FP16_FRAC_SIZE + 1))) {
        f->frac = f->frac >> 1;
        f->exp_signed++;
    }
    f->frac = f->frac & FP16_FRAC_MASK;
    if (round != 0) {
        s->float_exception_flags |= float_flag_inexact;
    }
    if (f->exp_signed > FP16_EXP_MAX) {
        if (sat) {
            result = float16_set_sign(FP16_MAX, f->sign);
        } else {
            result = float16_set_sign(float16_infinity, f->sign);
            s->float_exception_flags |= (float_flag_inexact |
                                         float_flag_overflow);
        }
        return result;
    } else if (f->exp_signed < FP16_EXP_MIN) {
        f->exp = 0;
        if (s->float_exception_flags & float_flag_inexact) {
            s->float_exception_flags |= float_flag_underflow;
        }
    } else {
        f->exp = f->exp_signed + FP16_EXP_BIAS;
    }
    result = xt_pack_raw64(f, FP16_FRAC_SIZE, FP16_EXP_SIZE);
    return result;
}

#define FP32_EXP_SIZE  8
#define FP32_FRAC_SIZE 23
#define FP32_FRAC_MASK 0x7fffff
#define FP32_EXP_SHIFT 23
#define FP32_EXP_MASK  0xff
#define FP32_EXP_BIAS  0x7f
#define FP32_EXP_MAX   0x7f
#define FP32_EXP_MIN   -0x7f
#define FP32_MAX       0x7f7fffff
#define FP32_CNAN      0x7fc00000

/* Shift to canonical */
static void xt_canon_fp32(unpacked_float *f, float_status *s)
{
    /* MSB from 1 */
    uint8_t msb = 64 - clz64(f->frac);

    /* Keep 1 + 23 + 3 bits for fp32 fraction before round */
    uint8_t shift;

    /* Move the fraction msb to 38 bit from 1 */
    if (msb > 38) {
        f->exp_signed = f->exp_signed + msb - 38;
    } else {
        f->exp_signed = f->exp_signed - (38 - msb);
    }
    /* We don't really shift fraction to 39 bit, so just keep 27 bits here */
    if (msb > 27) {
        uint64_t jam;
        shift = msb  - 27;
        jam = f->frac >> shift;
        if (jam << shift != f->frac) {
            f->frac = jam | 0x1;
            s->float_exception_flags |= float_flag_inexact;
        } else {
            f->frac = jam;
        }
    } else if (msb < 27) {
        shift = 27 - msb;
        f->frac = f->frac << shift;
    }
}

static float32 xt_round_fp32(unpacked_float *f, int frm, bool sat,
                             float_status *s)
{
    uint64_t round;
    float32 result;
    round = get_round(frm, f->frac, 3);
    f->frac = (f->frac >> 3) + round;

    /* Round move the msb bit */
    if (f->frac & (1 << (FP32_FRAC_SIZE + 1))) {
        f->frac = f->frac >> 1;
        f->exp_signed++;
    }
    f->frac = f->frac & FP32_FRAC_MASK;
    if (round != 0) {
        s->float_exception_flags |= float_flag_inexact;
    }
    if (f->exp_signed > FP32_EXP_MAX) {
        if (sat) {
            result = float32_set_sign(FP32_MAX, f->sign);
        } else {
            result = float32_set_sign(float32_infinity, f->sign);
            s->float_exception_flags |= (float_flag_inexact |
                                         float_flag_overflow);
        }
        return result;
    } else if (f->exp_signed < FP32_EXP_MIN) {
        f->exp = 0;
        if (s->float_exception_flags & float_flag_inexact) {
            s->float_exception_flags |= float_flag_underflow;
        }
    } else {
        f->exp = f->exp_signed + FP32_EXP_BIAS;
    }
    result = xt_pack_raw64(f, FP32_FRAC_SIZE, FP32_EXP_SIZE);
    return result;
}

#define BF16_EXP_SIZE  8
#define BF16_FRAC_SIZE 7
#define BF16_EXP_MAX   0x7f
#define BF16_EXP_MIN  -0x7f
#define BF16_MAX       0x7f7f
#define BF16_CNAN       0x7fc0

static bfloat16 xt_round_bf16(unpacked_float *f, int frm, bool sat,
                              float_status *s)
{
    uint64_t round;
    bfloat16 result;
    round = get_round(frm, f->frac, 3);
    f->frac = (f->frac >> 3) + round;

    /* Round move the msb bit */
    if (f->frac & (1 << (BF16_FRAC_SIZE + 1))) {
        f->frac = f->frac >> 1;
        f->exp_signed++;
    }
    f->frac = f->frac & BF16_FRAC_MASK;
    if (round != 0) {
        s->float_exception_flags |= float_flag_inexact;
    }
    if (f->exp_signed > BF16_EXP_MAX) {
        if (sat) {
            result = bfloat16_set_sign(BF16_MAX, f->sign);
        } else {
            result = bfloat16_set_sign(bfloat16_infinity, f->sign);
            s->float_exception_flags |= (float_flag_inexact |
                                         float_flag_overflow);
        }
        return result;
    } else if (f->exp_signed < BF16_EXP_MIN) {
        f->exp = 0;
        if (s->float_exception_flags & float_flag_inexact) {
            s->float_exception_flags |= float_flag_underflow;
        }
    } else {
        f->exp = f->exp_signed + BF16_EXP_BIAS;
    }
    result = xt_pack_raw64(f, BF16_FRAC_SIZE, BF16_EXP_SIZE);
    return result;
}

/*
 * First process special cases for NaN and Inf, then the "normal" cases:
 * 1) Unpack all sources from float16 format.
 * 2) Extend the fraction to 39 bits.
 * 3) Find the max exp and align to it. Notice this may cause inexact.
 * 4) Add all sources to get the signed fraction and the fraction.
 * 5) Canonicalize the fraction (only keep the 1 + 10 + 3 bits) and get the
 *    signed exp. Notice this may cause inexact.
 * 6) Round. Notice this may cause MSB bit change and inexact.
 * 7) Set overflow or underflow exception or nothing.
 * 8) Pack to float16 format.
 */
static float16
xt_fredsum_h_internal(void *vs2, float_status *s,
                      target_ulong frm, bool sat, target_ulong rlen,
                      float16 init_val, bool use_init_val, bool abs)
{
    float16 f;
    int j;

    uint32_t exp_max = 0, exp;
    bool snan = false;
    bool any_nan = false;
    bool inf_p = false;
    bool inf_n = false;
    unpacked_float *unpack = g_new0(unpacked_float, rlen);
    unpacked_float result = {0};

    for (j = 0; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float16 *)vs2 + j);
        }
        if (abs) {
            f = float16_abs(f);
        }
        exp = (f >> FP16_EXP_SHIFT) & FP16_EXP_MASK;
        exp_max = MAX(exp, exp_max);
        if (float16_is_any_nan(f)) {
            any_nan = true;
            if (float16_is_signaling_nan(f, s)) {
                snan = true;
                break;
            }
        }
        if (float16_is_infinity(f)) {
            if (float16_is_neg(f)) {
                inf_n = true;
            } else {
                inf_p = true;
            }
        }
        unpack[j].sign = float16_is_neg(f);
        unpack[j].exp = exp;
        unpack[j].frac = f & FP16_FRAC_MASK;
        unpack[j].iszero = float16_is_zero(f);
    }

    if (any_nan || (inf_n && inf_p)) {
        if (snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return FLOAT16_CNAN;
    }
    if (inf_n || inf_p) {
        return float16_set_sign(float16_infinity, inf_n);
    }

    /* Align to exp_max */
    for (j = 0; j < rlen; j++) {
        uint8_t denormal_shift = 0;
        if (unpack[j].iszero) {
            continue;
        }
        unpack[j].frac = xt_extend_frac_38_f16(unpack[j].frac,
                                               unpack[j].isdenormal,
                                               &denormal_shift);
        xt_align_frac_expmax(&unpack[j], exp_max, true, denormal_shift, s);
        unpack[j].exp = exp_max;
        /* Add signed frac */
        unpack[j].frac_signed = xt_get_frac_signed(&unpack[j]);
        result.frac_signed += unpack[j].frac_signed;
    }
    g_free(unpack);

    if (result.frac_signed == 0) {
        return float16_zero;
    }
    /* Init the result */
    result.frac = llabs(result.frac_signed);
    result.sign = result.frac_signed < 0;
    result.exp = exp_max;
    result.exp_signed = exp_max - FP16_EXP_BIAS;

    /* Get the canonical format */
    xt_canon_fp16(&result, s);

    /* Round */
    return xt_round_fp16(&result, frm, sat, s);
}

float16
do_fredsum_32_h_internal(void *vs2, int i, float_status *s,
                         target_ulong frm, bool sat)
{
    return xt_fredsum_h_internal((float16 *)vs2 + 32 * i, s, frm, sat,
                                 32, 0, false, false);
}

/*
 * First process special cases for NaN and Inf, then the "normal" cases:
 * 1) Unpack all sources from float32 format.
 * 2) Extend the fraction to 39 bits.
 * 3) Find the max exp and align to it. Notice this may cause inexact.
 * 4) Add all sources to get the signed fraction and the fraction.
 * 5) Canonicalize the fraction (only keep the 1 + 10 + 3 bits) and get the
 *    signed exp. Notice this may cause inexact.
 * 6) Round. Notice this may cause MSB bit change and inexact.
 * 7) Set overflow or underflow exception or nothing.
 * 8) Pack to float32 format.
 */
static float32
xt_fredsum_w_internal(void *vs2, float_status *s,
                      target_ulong frm, bool sat, target_ulong rlen,
                      float32 init_val, bool use_init_val, bool abs)
{
    float32 f;
    int j;

    uint32_t exp_max = 0, exp;
    bool snan = false;
    bool any_nan = false;
    bool inf_p = false;
    bool inf_n = false;
    unpacked_float *unpack = g_new0(unpacked_float, rlen);
    unpacked_float result = {0};

    for (j = 0; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float32 *)vs2 + j);
        }
        if (abs) {
            f = float32_abs(f);
        }
        exp = (f >> FP32_EXP_SHIFT) & FP32_EXP_MASK;
        exp_max = MAX(exp, exp_max);
        if (float32_is_any_nan(f)) {
            any_nan = true;
            if (float32_is_signaling_nan(f, s)) {
                snan = true;
                break;
            }
        }
        if (float32_is_infinity(f)) {
            if (float32_is_neg(f)) {
                inf_n = true;
            } else {
                inf_p = true;
            }
        }
        unpack[j].sign = float32_is_neg(f);
        unpack[j].exp = exp;
        unpack[j].frac = f & FP32_FRAC_MASK;
        unpack[j].iszero = float32_is_zero(f);
        if (float32_is_zero_or_denormal(f) && !unpack[j].iszero) {
            unpack[j].isdenormal = true;
        }
    }

    if (any_nan || (inf_n && inf_p)) {
        if (snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return FP32_CNAN;
    }
    if (inf_n || inf_p) {
        return float32_set_sign(float32_infinity, inf_n);
    }

    /* Align to exp_max */
    for (j = 0; j < rlen; j++) {
        uint8_t denormal_shift = 0;
        if (unpack[j].iszero) {
            continue;
        }
        unpack[j].frac = xt_extend_frac_38_f32(unpack[j].frac,
                                               unpack[j].isdenormal,
                                               &denormal_shift);
        xt_align_frac_expmax(&unpack[j], exp_max, true, denormal_shift, s);
        unpack[j].exp = exp_max;
        /* Add signed frac */
        unpack[j].frac_signed = xt_get_frac_signed(&unpack[j]);
        result.frac_signed += unpack[j].frac_signed;
    }

    g_free(unpack);
    if (result.frac_signed == 0) {
        return float32_zero;
    }
    /* Init the result */
    result.frac = llabs(result.frac_signed);
    result.sign = result.frac_signed < 0;
    result.exp = exp_max;
    result.exp_signed = exp_max - FP32_EXP_BIAS;

    /* Get the canonical format */
    xt_canon_fp32(&result, s);

    /* Round */
    return xt_round_fp32(&result, frm, sat, s);
}

float32
do_fredsum_32_w_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredsum_w_internal((float32 *)vs2 + 32 * i, s, frm, sat,
                                 32, 0, false, false);
}

/*
 * First process special cases for NaN and Inf, then the "normal" cases:
 * 1) Unpack all sources from bfloat16 format.
 * 2) Extend the fraction to 39 bits.
 * 3) Find the max exp and align to it. Notice this may cause inexact.
 * 4) Add all sources to get the signed fraction and the fraction.
 * 5) Canonicalize the fraction (only keep the 1 + 10 + 3 bits) and get the
 *    signed exp. Notice this may cause inexact.
 * 6) Round. Notice this may cause MSB bit change and inexact.
 * 7) Set overflow or underflow exception or nothing.
 * 8) Pack to bfloat16 format.
 */
static bfloat16
xt_bfredsum_h_internal(void *vs2, float_status *s,
                       target_ulong frm, bool sat, target_ulong rlen,
                       bfloat16 init_val, bool use_init_val, bool abs)
{
    bfloat16 f;
    int j;

    uint32_t exp_max = 0, exp;
    bool snan = false;
    bool any_nan = false;
    bool inf_p = false;
    bool inf_n = false;
    unpacked_float *unpack = g_new0(unpacked_float, rlen);
    unpacked_float result = {0};

    for (j = 0; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((bfloat16 *)vs2 + j);
        }
        if (abs) {
            f = bfloat16_abs(f);
        }
        exp = (f >> BF16_EXP_SHIFT) & BF16_EXP_MASK;
        exp_max = MAX(exp, exp_max);
        if (bfloat16_is_any_nan(f)) {
            any_nan = true;
            if (bfloat16_is_signaling_nan(f, s)) {
                snan = true;
                break;
            }
        }
        if (bfloat16_is_infinity(f)) {
            if (bfloat16_is_neg(f)) {
                inf_n = true;
            } else {
                inf_p = true;
            }
        }
        unpack[j].sign = bfloat16_is_neg(f);
        unpack[j].exp = exp;
        unpack[j].frac = f & BF16_FRAC_MASK;
        unpack[j].iszero = bfloat16_is_zero(f);
    }

    if (any_nan || (inf_n && inf_p)) {
        if (snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return BF16_CNAN;
    }
    if (inf_n || inf_p) {
        return bfloat16_set_sign(bfloat16_infinity, inf_n);
    }

    /* Align to exp_max */
    for (j = 0; j < rlen; j++) {
        uint8_t denormal_shift = 0;
        if (unpack[j].iszero) {
            continue;
        }
        unpack[j].frac = xt_extend_frac_38_bf16(unpack[j].frac,
                                                unpack[j].isdenormal,
                                                &denormal_shift);
        xt_align_frac_expmax(&unpack[j], exp_max, true, denormal_shift, s);
        unpack[j].exp = exp_max;
        /* Add signed frac */
        unpack[j].frac_signed = xt_get_frac_signed(&unpack[j]);
        result.frac_signed += unpack[j].frac_signed;
    }

    g_free(unpack);
    if (result.frac_signed == 0) {
        return bfloat16_zero;
    }
    /* Init the result */
    result.frac = llabs(result.frac_signed);
    result.sign = result.frac_signed < 0;
    result.exp = exp_max;
    result.exp_signed = exp_max - BF16_EXP_BIAS;

    /* Get the canonical format */
    xt_canon_bf16(&result, s);

    /* Round */
    return xt_round_bf16(&result, frm, sat, s);
}
bfloat16
do_bfredsum_32_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_bfredsum_h_internal((bfloat16 *)vs2 + 32 * i, s, frm, sat,
                                  32, 0, false, false);
}

float16
do_fredsum_64_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredsum_h_internal((float16 *)vs2 + i * 64, s, frm, sat,
                                 64, 0, false, false);
}

float32
do_fredsum_64_w_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredsum_w_internal((float32 *)vs2 + 64 * i, s, frm, sat,
                                 64, 0, false, false);
}

bfloat16
do_bfredsum_64_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_bfredsum_h_internal((bfloat16 *)vs2 + 64 * i, s, frm, sat,
                                  64, 0, false, false);
}

static float16
xt_fredmax_h_internal(void *vs2, float_status *s,
                      target_ulong frm, bool sat, target_ulong rlen,
                      float16 init_val, bool use_init_val, bool abs)
{
    float16 f, f_max;
    int j;

    uint32_t exp_max = 0, exp;
    bool snan = false;
    bool any_nan = false;
    bool inf_p = false;

    for (j = 0; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float16 *)vs2 + j);
        }
        if (abs) {
            f = float16_abs(f);
        }
        exp = (f >> FP16_EXP_SHIFT) & FP16_EXP_MASK;
        exp_max = MAX(exp, exp_max);
        if (float16_is_any_nan(f)) {
            any_nan = true;
            if (float16_is_signaling_nan(f, s)) {
                snan = true;
                break;
            }
        }
        if (float16_is_infinity(f)) {
            if (!float16_is_neg(f)) {
                inf_p = true;
            }
        }
    }

    if (any_nan) {
        if (snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return FP16_CNAN;
    }

    if (inf_p) {
        if (sat) {
            return FP16_MAX;
        } else {
            return float16_infinity;
        }
    }

    f_max = *(float16 *)vs2;
    if (abs) {
        f_max = float16_abs(f_max);
    }
    for (j = 1; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float16 *)vs2 + j);
        }
        if (abs) {
            f = float16_abs(f);
        }
        f_max = float16_max(f_max, f, s);
    }

    return f_max;
}

float16
do_fredmax_32_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredmax_h_internal((float16 *)vs2 + 32 * i, s, frm, sat,
                                 32, 0, false, false);
}
static float32
xt_fredmax_w_internal(void *vs2, float_status *s,
                      target_ulong frm, bool sat, target_ulong rlen,
                      float32 init_val, bool use_init_val, bool abs)
{
    float32 f, f_max;
    int j;

    uint32_t exp_max = 0, exp;
    bool snan = false;
    bool any_nan = false;
    bool inf_p = false;

    for (j = 0; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float32 *)vs2 + j);
        }
        if (abs) {
            f = float32_abs(f);
        }
        exp = (f >> FP32_EXP_SHIFT) & FP32_EXP_MASK;
        exp_max = MAX(exp, exp_max);
        if (float32_is_any_nan(f)) {
            any_nan = true;
            if (float32_is_signaling_nan(f, s)) {
                snan = true;
                break;
            }
        } else if (float32_is_infinity(f) && !float32_is_neg(f)) {
            inf_p = true;
        }
    }

    if (any_nan) {
        if (snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return FP32_CNAN;
    }

    if (inf_p) {
        if (sat) {
            return FP32_MAX;
        } else {
            return float32_infinity;
        }
    }

    f_max = *(float32 *)vs2;
    if (abs) {
        f_max = float32_abs(f_max);
    }
    for (j = 1; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float32 *)vs2 + j);
        }
        if (abs) {
            f = float32_abs(f);
        }
        f_max = float32_max(f_max, f, s);
    }

    return f_max;
}

float32
do_fredmax_32_w_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredmax_w_internal((float32 *)vs2 + 32 * i, s, frm, sat,
                                 32, 0, false, false);
}

static bfloat16
xt_bfredmax_h_internal(void *vs2, float_status *s,
                      target_ulong frm, bool sat, target_ulong rlen,
                      bfloat16 init_val, bool use_init_val, bool abs)
{
    bfloat16 f, f_max;
    int j;

    uint32_t exp_max = 0, exp;
    bool snan = false;
    bool any_nan = false;
    bool inf_p = false;

    for (j = 0; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((bfloat16 *)vs2 + j);
        }
        if (abs) {
            f = bfloat16_abs(f);
        }
        exp = (f >> BF16_EXP_SHIFT) & BF16_EXP_MASK;
        exp_max = MAX(exp, exp_max);
        if (bfloat16_is_any_nan(f)) {
            any_nan = true;
            if (bfloat16_is_signaling_nan(f, s)) {
                snan = true;
                break;
            }
        }
        if (bfloat16_is_infinity(f)) {
            if (!bfloat16_is_neg(f)) {
                inf_p = true;
            }
        }
    }

    if (any_nan) {
        if (snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return BF16_CNAN;
    }

    if (inf_p) {
        if (sat) {
            return BF16_MAX;
        } else {
            return bfloat16_infinity;
        }
    }

    f_max = *(bfloat16 *)vs2;
    if (abs) {
        f_max = bfloat16_abs(f_max);
    }
    for (j = 1; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((bfloat16 *)vs2 + j);
        }
        if (abs) {
            f = bfloat16_abs(f);
        }
        f_max = bfloat16_max(f_max, f, s);
    }

    return f_max;
}

bfloat16
do_bfredmax_32_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_bfredmax_h_internal((bfloat16 *)vs2 + 32 * i, s, frm, sat,
                                  32, 0, false, false);
}

float16
do_fredmax_64_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredmax_h_internal((float16 *)vs2 + 64 * i, s, frm, sat,
                                 64, 0, false, false);
}

float32
do_fredmax_64_w_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredmax_w_internal((float32 *)vs2 + 64 * i, s, frm, sat,
                                 64, 0, false, false);
}

bfloat16
do_bfredmax_64_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_bfredmax_h_internal((bfloat16 *)vs2 + 64 * i, s, frm, sat,
                                  64, 0, false, false);
}

static float16
xt_fredmin_h_internal(void *vs2, float_status *s,
                      target_ulong frm, bool sat, target_ulong rlen,
                      float16 init_val, bool use_init_val, bool abs)
{
    float16 f, f_min;
    int j;

    uint32_t exp_min = 0, exp;
    bool snan = false;
    bool any_nan = false;
    bool inf_n = false;

    for (j = 0; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float16 *)vs2 + j);
        }
        if (abs) {
            f = float16_abs(f);
        }
        exp = (f >> FP16_EXP_SHIFT) & FP16_EXP_MASK;
        exp_min = MAX(exp, exp_min);
        if (float16_is_any_nan(f)) {
            any_nan = true;
            if (float16_is_signaling_nan(f, s)) {
                snan = true;
                break;
            }
        } else if (float16_is_infinity(f) && float16_is_neg(f)) {
            inf_n = true;
        }
    }

    if (any_nan) {
        if (snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return FP16_CNAN;
    }

    if (inf_n) {
        if (sat) {
            return float16_set_sign(FP16_MAX, 1);
        } else {
            return float16_set_sign(float16_infinity, 1);
        }
    }

    f_min = *(float16 *)vs2;
    if (abs) {
        f_min = float16_abs(f_min);
    }
    for (j = 1; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float16 *)vs2 + j);
        }
        if (abs) {
            f = float16_abs(f);
        }
        f_min = float16_min(f_min, f, s);
    }

    return f_min;
}

float16
do_fredmin_32_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredmin_h_internal((float16 *)vs2 + 32 * i, s, frm, sat,
                                 32, 0, false, false);
}

static float32
xt_fredmin_w_internal(void *vs2, float_status *s,
                      target_ulong frm, bool sat, target_ulong rlen,
                      float32 init_val, bool use_init_val, bool abs)
{
    float32 f, f_min;
    int j;

    uint32_t exp_min = 0, exp;
    bool snan = false;
    bool any_nan = false;
    bool inf_n = false;

    for (j = 0; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float32 *)vs2 + j);
        }
        if (abs) {
            f = float32_abs(f);
        }
        exp = (f >> FP32_EXP_SHIFT) & FP32_EXP_MASK;
        exp_min = MAX(exp, exp_min);
        if (float32_is_any_nan(f)) {
            any_nan = true;
            if (float32_is_signaling_nan(f, s)) {
                snan = true;
                break;
            }
        } else if (float32_is_infinity(f) && float32_is_neg(f)) {
            inf_n = true;
        }
    }

    if (any_nan) {
        if (snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return FP32_CNAN;
    }

    if (inf_n) {
        if (sat) {
            return float32_set_sign(FP32_MAX, 1);
        } else {
            return float32_set_sign(float32_infinity, 1);
        }
    }

    f_min = *(float32 *)vs2;
    if (abs) {
        f_min = float32_abs(f_min);
    }
    for (j = 1; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((float32 *)vs2 + j);
        }
        if (abs) {
            f = float32_abs(f);
        }
        f_min = float32_min(f_min, f, s);
    }

    return f_min;
}
float32
do_fredmin_32_w_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredmin_w_internal((float32 *)vs2 + 32 * i, s, frm, sat,
                                 32, 0, false, false);
}

static bfloat16
xt_bfredmin_h_internal(void *vs2, float_status *s,
                      target_ulong frm, bool sat, target_ulong rlen,
                      bfloat16 init_val, bool use_init_val, bool abs)
{
    bfloat16 f, f_min;
    int j;

    uint32_t exp_min = 0, exp;
    bool snan = false;
    bool any_nan = false;
    bool inf_n = false;

    for (j = 0; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((bfloat16 *)vs2 + j);
        }
        if (abs) {
            f = bfloat16_abs(f);
        }
        exp = (f >> BF16_EXP_SHIFT) & BF16_EXP_MASK;
        exp_min = MAX(exp, exp_min);
        if (bfloat16_is_any_nan(f)) {
            any_nan = true;
            if (bfloat16_is_signaling_nan(f, s)) {
                snan = true;
                break;
            }
        } else if (bfloat16_is_infinity(f) && bfloat16_is_neg(f)) {
            inf_n = true;
        }
    }

    if (any_nan) {
        if (snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return BF16_CNAN;
    }

    if (inf_n) {
        if (sat) {
            return bfloat16_set_sign(BF16_MAX, 1);
        } else {
            return bfloat16_set_sign(bfloat16_infinity, 1);
        }
    }

    f_min = *(bfloat16 *)vs2;
    if (abs) {
        f_min = bfloat16_abs(f_min);
    }
    for (j = 1; j < rlen; j++) {
        if ((j == rlen - 1) && use_init_val) {
            f = init_val;
        } else {
            f = *((bfloat16 *)vs2 + j);
        }
        if (abs) {
            f = bfloat16_abs(f);
        }
        f_min = bfloat16_min(f_min, f, s);
    }

    return f_min;
}

bfloat16
do_bfredmin_32_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_bfredmin_h_internal((bfloat16 *)vs2 + 32 * i, s, frm, sat,
                                  32, 0, false, false);
}

float16
do_fredmin_64_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredmin_h_internal((float16 *)vs2 + 64 * i, s, frm, sat,
                                 64, 0, false, false);
}

float32
do_fredmin_64_w_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_fredmin_w_internal((float32 *)vs2 + 64 * i, s, frm, sat,
                                 64, 0, false, false);
}

bfloat16
do_bfredmin_64_h_internal(void *vs2, int i, float_status *s,
    target_ulong frm, bool sat)
{
    return xt_bfredmin_h_internal((bfloat16 *)vs2 + 64 * i, s, frm, sat,
                                  64, 0, false, false);
}

int64_t
do_mfredmax_h_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs)
{
    return xt_fredmax_h_internal(start, &env->mfp_status, env->mfrm,
                                 env->xmsaten, elem_count, init_val, true, abs);
}

int64_t
do_mfredmin_h_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs)
{
    return xt_fredmin_h_internal(start, &env->mfp_status, env->mfrm,
                                 env->xmsaten, elem_count, init_val, true, abs);
}

int64_t
do_mfredsum_h_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs)
{
    return xt_fredsum_h_internal(start, &env->mfp_status, env->mfrm,
                                 env->xmsaten, elem_count, init_val, true, abs);
}

int64_t
do_mfredmax_s_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs)
{
    return xt_fredmax_w_internal(start, &env->mfp_status, env->mfrm,
                                 env->xmsaten, elem_count, init_val, true, abs);
}

int64_t
do_mfredmin_s_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs)
{
    return xt_fredmin_w_internal(start, &env->mfp_status, env->mfrm,
                                 env->xmsaten, elem_count, init_val, true, abs);
}

int64_t
do_mfredsum_s_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs)
{
    return xt_fredsum_w_internal(start, &env->mfp_status, env->mfrm,
                                 env->xmsaten, elem_count, init_val, true, abs);
}

int64_t
do_mfredmax_bf16_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs)
{
    return xt_bfredmax_h_internal(start, &env->mfp_status, env->mfrm,
                                 env->xmsaten, elem_count, init_val, true, abs);
}

int64_t
do_mfredmin_bf16_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs)
{
    return xt_bfredmin_h_internal(start, &env->mfp_status, env->mfrm,
                                 env->xmsaten, elem_count, init_val, true, abs);
}

int64_t
do_mfredsum_bf16_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs)
{
    return xt_bfredsum_h_internal(start, &env->mfp_status, env->mfrm,
                                 env->xmsaten, elem_count, init_val, true, abs);
}
