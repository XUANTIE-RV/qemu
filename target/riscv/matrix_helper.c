#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "exec/memop.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"
#include "vector_internals.h"
#include "exec/tracestub.h"


target_ulong riscv_cpu_get_mfflags(CPURISCVState *env)
{
    int soft = get_float_exception_flags(&env->mfp_status);
    target_ulong hard = 0;

    hard |= (soft & float_flag_inexact) ? FPEXC_NX : 0;
    hard |= (soft & float_flag_underflow) ? FPEXC_UF : 0;
    hard |= (soft & float_flag_overflow) ? FPEXC_OF : 0;
    hard |= (soft & float_flag_divbyzero) ? FPEXC_DZ : 0;
    hard |= (soft & float_flag_invalid) ? FPEXC_NV : 0;

    return hard;
}

void riscv_cpu_set_mfflags(CPURISCVState *env, target_ulong hard)
{
    int soft = 0;

    soft |= (hard & FPEXC_NX) ? float_flag_inexact : 0;
    soft |= (hard & FPEXC_UF) ? float_flag_underflow : 0;
    soft |= (hard & FPEXC_OF) ? float_flag_overflow : 0;
    soft |= (hard & FPEXC_DZ) ? float_flag_divbyzero : 0;
    soft |= (hard & FPEXC_NV) ? float_flag_invalid : 0;

    set_float_exception_flags(soft, &env->mfp_status);
}

void riscv_cpu_set_mfrm(CPURISCVState *env, uint32_t rm)
{
    int softrm;
    switch (rm) {
    case RISCV_FRM_RNE:
        softrm = float_round_nearest_even;
        break;
    case RISCV_FRM_RTZ:
        softrm = float_round_to_zero;
        break;
    case RISCV_FRM_RDN:
        softrm = float_round_down;
        break;
    case RISCV_FRM_RUP:
        softrm = float_round_up;
        break;
    case RISCV_FRM_RMM:
        softrm = float_round_ties_away;
        break;
    default:
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
    set_float_rounding_mode(softrm, &env->mfp_status);
}


typedef enum{
    ADD,
    SUB,
    SRA,
    MUL,
    MAX,
    MIN,
    UMAX,
    UMIN,
    SLL,
    SRL,
} op_t;


static inline int64_t get_elem_p(void *md, uint32_t i, uint32_t j,
                                 CPURISCVState *env) {
    uint32_t idx = i * get_rlenb(env) + (j >> 1);
    uint8_t ele = ((int8_t *) md)[idx];
    return (j & 0x1) ? (ele >> 4) : ele & (0x0f);
}

static inline int64_t set_elem_p(void *md, uint32_t i, uint32_t j,
                                 CPURISCVState *env, int64_t val) {
    uint32_t idx = i * get_rlenb(env) + (j >> 1);
    uint8_t ele = ((int8_t *) md)[idx];
    ele = (j & 0x1) ? ((ele & 0x0f) | (((uint8_t) val) << 4)) :
                      ((ele & 0xf0) | (((uint8_t) val) & 0xf0));
    return ((int8_t *) md)[idx] = (int8_t) ele;
}

static inline int64_t get_elem_b(void *md, uint32_t i, uint32_t j,
                                 CPURISCVState *env){
    uint32_t idx = i * get_rlenb(env) + j;
    return ((int8_t *)md)[idx];
}

static inline void set_elem_b(void *md, uint32_t i, uint32_t j,
                              CPURISCVState *env, int64_t val){
    uint32_t idx = i * get_rlenb(env) + j;
    ((int8_t *) md)[idx] = (int8_t) val;
}

static inline int64_t get_elem_h(void *md, uint32_t i, uint32_t j,
                                 CPURISCVState *env){
    uint32_t idx = i * (get_rlenb(env) >> 1) + j;
    return ((int16_t *)md)[idx];
}

static inline void set_elem_h(void *md, uint32_t i, uint32_t j,
                              CPURISCVState *env, int64_t val){
    uint32_t idx = i * (get_rlenb(env) >> 1) + j;
    ((int16_t *) md)[idx] = (int16_t) val;
}

static inline int64_t get_elem_s(void* md, uint32_t i, uint32_t j,
                                 CPURISCVState* env){
    uint32_t idx = i * (get_rlenb(env) >> 2) + j;
    return ((int32_t *)md)[idx];
}

static inline void set_elem_s(void* md, uint32_t i, uint32_t j,
                              CPURISCVState* env, int64_t val){
    uint32_t idx = i * (get_rlenb(env) >> 2) + j;
    ((int32_t *) md)[idx] = (int32_t) val;
}

static inline int64_t get_elem_d(void* md, uint32_t i, uint32_t j,
                                 CPURISCVState* env){
    uint32_t idx = i * (get_rlenb(env) >> 3) + j;
    return ((int64_t *)md)[idx];
}

static inline void set_elem_d(void* md, uint32_t i, uint32_t j,
                              CPURISCVState* env, int64_t val){
    uint32_t idx = i * (get_rlenb(env) >> 3) + j;
    ((int64_t *) md)[idx] = val;
}

static inline uint64_t get_unsigned_mask(uint8_t lg2_sz_in_bytes)
{
    return ~((~((uint64_t) 0x0)) << (((uint64_t) 8) << lg2_sz_in_bytes));
}

typedef int64_t mmext_get_elem(void*, uint32_t, uint32_t, CPURISCVState*);
typedef void mmext_set_elem(void*, uint32_t, uint32_t, CPURISCVState*, int64_t);


static inline int64_t mul32(int64_t oprd_a, int64_t oprd_b, bool keep_hi)
{
    int64_t tmp = oprd_a * oprd_b;
    if (keep_hi) {
        return tmp >> 32;
    }
    return tmp;
}

static inline int64_t mul64(int64_t oprd_a, int64_t oprd_b, bool keep_hi)
{
    uint64_t hi_64, lo_64;
    muls64(&lo_64, &hi_64, oprd_a, oprd_b);
    if (keep_hi) {
        return hi_64;
    }
    return lo_64;
}

static inline void mmext_mv_mx(void* md, void* ms1, void* ms2, target_ulong s1,
                               CPURISCVState* env, mmext_get_elem* get_elem,
                               mmext_set_elem* set_elem, op_t op, uint8_t esz,
                               bool keep_hi){
    uint32_t i, k, idx;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit, mask;
    uint32_t rows = get_mrows(env);

    for (idx = 0; idx < rows; idx++) {
        i = idx;
        if (ms1 == md) {
            if (idx == s1) {
                i = rows - 1;
            } else if (idx == rows - 1) {
                i = s1;
            }
        }
        for (k = 0; k < cols; k++){
            if(i < env->sizem && k < (env->sizek >> esz)){
                switch(op){
                case ADD:
                    result = get_elem(ms2, i, k, env) + get_elem(ms1, s1, k, env);
                    set_elem(md, i, k, env, result);
                    break;
                case SUB:
                    result = get_elem(ms2, i, k, env) - get_elem(ms1, s1, k, env);
                    set_elem(md, i, k, env, result);
                    break;
                case MUL:
                    if (esz == 2) {
                        result = mul32(get_elem(ms2, i, k, env),
                                       get_elem(ms1, s1, k, env), keep_hi);
                    } else {
                        result = mul64(get_elem(ms2, i, k, env),
                                       get_elem(ms1, s1, k, env), keep_hi);
                    }
                    set_elem(md, i, k, env, result);
                    break;
                case SLL:
                case SRA:
                case SRL: {
                    if (esz == 2) {
                        n_bit = (uint64_t) (get_elem(ms1, s1, k, env) & 0x1F);
                    } else {
                        n_bit = (uint64_t) (get_elem(ms1, s1, k, env) & 0x3F);
                    }
                    result = get_elem(ms2, i, k, env);
                    uint8_t round = get_round(env->mxrm, result, n_bit);
                    if (op == SRA)
                        result = (result >> n_bit) + round;
                    else if (op == SRL) {
                        mask = get_unsigned_mask(esz);
                        result = ((((uint64_t) result) & mask) >> n_bit) +
                            round;
                    } else
                        result = result << n_bit;
                    set_elem(md, i, k, env, result);
                    break;
                }
                case MAX:
                case MIN: {
                    int64_t oprd_a = get_elem(ms1, s1, k, env);
                    int64_t oprd_b = get_elem(ms2, i, k, env);
                    result = (op == MAX ? oprd_a > oprd_b : oprd_a < oprd_b) ?
                            oprd_a : oprd_b;
                    set_elem(md, i, k, env, result);
                    break;
                }
                case UMAX:
                case UMIN: {
                    mask = get_unsigned_mask(esz);
                    uint64_t oprd_a = get_elem(ms1, s1, k, env) & mask;
                    uint64_t oprd_b = get_elem(ms2, i, k, env) & mask;
                    result = (op == UMAX ? oprd_a > oprd_b : oprd_a < oprd_b) ?
                            oprd_a : oprd_b;
                    set_elem(md, i, k, env, result);
                    break;
                }
                default:
                    break;
                }
            }
            else{
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_OP_MV_MX_HELPER(insn, op, get_elem, set_elem, ESZ, keep_hi) \
void HELPER(insn)(void* md, void* ms1, void* ms2, target_ulong s1,      \
                  CPURISCVState* env){                                  \
    mmext_mv_mx(md, ms1, ms2, s1, env,                                  \
                get_elem, set_elem, op, ESZ, keep_hi);                  \
}

GEN_OP_MV_MX_HELPER(madd_s_mv_i, ADD, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(msub_s_mv_i, SUB, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(msra_s_mv_i, SRA, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(mmul_s_mv_i, MUL, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(mmax_s_mv_i, MAX, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(mmin_s_mv_i, MIN, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(mumax_s_mv_i, UMAX, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(mumin_s_mv_i, UMIN, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(msll_s_mv_i, SLL, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(msrl_s_mv_i, SRL, get_elem_s, set_elem_s, 2, false)

GEN_OP_MV_MX_HELPER(madd_d_mv_i, ADD, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(msub_d_mv_i, SUB, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(msra_d_mv_i, SRA, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(mmul_d_mv_i, MUL, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(mmax_d_mv_i, MAX, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(mmin_d_mv_i, MIN, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(msll_d_mv_i, SLL, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(msrl_d_mv_i, SRL, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(mumax_d_mv_i, UMAX, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(mumin_d_mv_i, UMIN, get_elem_d, set_elem_d, 3, false)

GEN_OP_MV_MX_HELPER(mmulh_s_mv_i, MUL, get_elem_s, set_elem_s, 2, true)
GEN_OP_MV_MX_HELPER(mmulh_d_mv_i, MUL, get_elem_d, set_elem_d, 3, true)

void helper_mmov_mv_x(void *md, void *ms1, target_ulong s1, CPURISCVState *env)
{
    uint32_t mlenb = get_rlenb(env);
    uint32_t mrows = get_mrows(env);

    for (int i = 0; i < mrows; i++) {
        memcpy(md + i * mlenb, ms1 + s1 * mlenb, mlenb);
    }
}
static inline void mmext_mm(void* md, void* ms1, void* ms2,
                            CPURISCVState* env, mmext_get_elem* get_elem,
                            mmext_set_elem* set_elem, op_t op, uint8_t esz,
                            bool keep_hi){
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit, mask;
    for (i = 0; i < get_mrows(env); i++){
        for (k = 0; k < cols; k++){
            if(i < env->sizem && k < (env->sizek >> esz)){
                switch(op){
                case ADD:
                    result = get_elem(ms2, i, k, env) + get_elem(ms1, i, k, env);
                    set_elem(md, i, k, env, result);
                    break;
                case SUB:
                    result = get_elem(ms2, i, k, env) - get_elem(ms1, i, k, env);
                    set_elem(md, i, k, env, result);
                    break;
                case MUL:
                    if (esz == 2) {
                        result = mul32(get_elem(ms2, i, k, env),
                                       get_elem(ms1, i, k, env), keep_hi);
                    } else {
                        result = mul64(get_elem(ms2, i, k, env),
                                       get_elem(ms1, i, k, env), keep_hi);
                    }
                    set_elem(md, i, k, env, result);
                    break;
                case SLL:
                case SRA:
                case SRL: {
                    if (esz == 2) {
                        n_bit = (uint64_t) (get_elem(ms1, i, k, env) & 0x1F);
                    } else {
                        n_bit = (uint64_t) (get_elem(ms1, i, k, env) & 0x3F);
                    }
                    result = get_elem(ms2, i, k, env);
                    uint8_t round = get_round(env->mxrm, result, n_bit);
                    if (op == SRA)
                        result = (result >> n_bit) + round;
                    else if (op == SRL) {
                        mask = get_unsigned_mask(esz);
                        result = ((((uint64_t) result) & mask) >> n_bit) +
                                round;
                    } else
                        result = result << n_bit;
                    set_elem(md, i, k, env, result);
                    break;
                }
                case MAX:
                case MIN: {
                    int64_t oprd_a = get_elem(ms1, i, k, env);
                    int64_t oprd_b = get_elem(ms2, i, k, env);
                    result = (op == MAX ? oprd_a > oprd_b : oprd_a < oprd_b) ?
                            oprd_a : oprd_b;
                    set_elem(md, i, k, env, result);
                    break;
                }
                case UMAX:
                case UMIN: {
                    mask = get_unsigned_mask(esz);
                    uint64_t oprd_a = get_elem(ms1, i, k, env) & mask;
                    uint64_t oprd_b = get_elem(ms2, i, k, env) & mask;
                    result = (op == UMAX ? oprd_a > oprd_b : oprd_a < oprd_b) ?
                            oprd_a : oprd_b;
                    set_elem(md, i, k, env, result);
                    break;
                }
                default:
                    break;
                }
            }
            else{
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_OP_MM_HELPER(insn, op, get_elem, set_elem, ESZ, keep_hi) \
void HELPER(insn)(void *md, void *ms1, void *ms2,                    \
                  CPURISCVState *env){                               \
    mmext_mm(md, ms1, ms2, env,                                      \
             get_elem, set_elem, op, ESZ, keep_hi);                  \
}

GEN_OP_MM_HELPER(madd_s_mm, ADD, get_elem_s, set_elem_s, 2, false)
GEN_OP_MM_HELPER(msub_s_mm, SUB, get_elem_s, set_elem_s, 2, false)
GEN_OP_MM_HELPER(msra_s_mm, SRA, get_elem_s, set_elem_s, 2, false)
GEN_OP_MM_HELPER(mmul_s_mm, MUL, get_elem_s, set_elem_s, 2, false)
GEN_OP_MM_HELPER(mmax_s_mm, MAX, get_elem_s, set_elem_s, 2, false)
GEN_OP_MM_HELPER(mmin_s_mm, MIN, get_elem_s, set_elem_s, 2, false)
GEN_OP_MM_HELPER(mumax_s_mm, UMAX, get_elem_s, set_elem_s, 2, false)
GEN_OP_MM_HELPER(mumin_s_mm, UMIN, get_elem_s, set_elem_s, 2, false)
GEN_OP_MM_HELPER(msll_s_mm, SLL, get_elem_s, set_elem_s, 2, false)
GEN_OP_MM_HELPER(msrl_s_mm, SRL, get_elem_s, set_elem_s, 2, false)

GEN_OP_MM_HELPER(madd_d_mm, ADD, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(msub_d_mm, SUB, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(msra_d_mm, SRA, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(mmul_d_mm, MUL, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(mmax_d_mm, MAX, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(mmin_d_mm, MIN, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(msll_d_mm, SLL, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(msrl_d_mm, SRL, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(mumax_d_mm, UMAX, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(mumin_d_mm, UMIN, get_elem_d, set_elem_d, 3, false)

GEN_OP_MM_HELPER(mmulh_s_mm, MUL, get_elem_s, set_elem_s, 2, true)
GEN_OP_MM_HELPER(mmulh_d_mm, MUL, get_elem_d, set_elem_d, 3, true)

static inline int64_t clip8(int64_t result, bool use_signed,
                            CPURISCVState *env){
    if (use_signed) {
        if (result > INT8_MAX) {
            result = INT8_MAX;
            env->mxsat = 0x01;
        }
        if (result < INT8_MIN) {
            result = INT8_MIN;
            env->mxsat = 0x01;
        }
    } else {
        if ((uint64_t)result > UINT8_MAX) {
            result = UINT8_MAX;
            env->mxsat = 0x01;
        }
    }
    return result;
}

static inline int64_t clip16(int64_t result, bool use_signed,
                             CPURISCVState *env){
    if (use_signed) {
        if (result > INT16_MAX) {
            result = INT16_MAX;
            env->mxsat = 0x01;
        }
        if (result < INT16_MIN) {
            result = INT16_MIN;
            env->mxsat = 0x01;
        }
    } else {
        if ((uint64_t)result > UINT16_MAX) {
            result = UINT16_MAX;
            env->mxsat = 0x01;
        }
    }
    return result;
}

static inline void mmext_n4clip_mm(void *md, void *ms1, void *ms2,
                                   CPURISCVState *env, mmext_get_elem * get_elem,
                                   mmext_set_elem * set_elem, bool use_signed,
                                   uint8_t esz, bool high){
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit;
    uint8_t round;

    uint32_t col_offset = high ? cols : 0;

    for (i = 0; i < get_mrows(env); i++) {
        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek >> esz)) {
                if (esz == 2) {
                    n_bit = (uint64_t) (get_elem(ms1, i, k, env) & 0x1F);
                } else {
                    n_bit = (uint64_t) (get_elem(ms1, i, k, env) & 0x3F);
                }

                /* deal with signed/unsigned right-shift */
                result = get_elem(ms2, i, k, env);
                if (use_signed) {
                    round = get_round(env->mxrm, result, n_bit);
                    result = (result >> n_bit) + round;
                } else {
                    if (esz == 2) {
                        round = get_round(env->mxrm, (uint32_t) result, n_bit);
                        result = ((uint32_t) result >> n_bit) + round;
                    } else {
                        round = get_round(env->mxrm, (uint64_t) result, n_bit);
                        result = ((uint64_t) result >> n_bit) + round;
                    }
                }

                /* deal with signed/unsigned 8/16-bit clip */
                if (esz == 2) {
                    result = clip8(result, use_signed, env);
                } else {
                    result = clip16(result, use_signed, env);
                }
                set_elem(md, i, k + col_offset, env, result);
            }
        }
    }
}

#define GEN_N4CLIP_MM_HELPER(insn, use_signed, get_elem, set_elem, ESZ, hi) \
void HELPER(insn)(void *md, void *ms1, void *ms2,                           \
                  CPURISCVState *env){                                      \
    mmext_n4clip_mm(md, ms1, ms2, env,                                      \
                    get_elem, set_elem, use_signed, ESZ, hi);               \
}

GEN_N4CLIP_MM_HELPER(mn4cliph_s_mm,  true,  get_elem_s, set_elem_b, 2, true)
GEN_N4CLIP_MM_HELPER(mn4cliphu_s_mm, false, get_elem_s, set_elem_b, 2, true)
GEN_N4CLIP_MM_HELPER(mn4clipl_s_mm,  true,  get_elem_s, set_elem_b, 2, false)
GEN_N4CLIP_MM_HELPER(mn4cliplu_s_mm, false, get_elem_s, set_elem_b, 2, false)

GEN_N4CLIP_MM_HELPER(mn4cliph_d_mm,  true,  get_elem_d, set_elem_h, 3, true)
GEN_N4CLIP_MM_HELPER(mn4cliphu_d_mm, false, get_elem_d, set_elem_h, 3, true)
GEN_N4CLIP_MM_HELPER(mn4clipl_d_mm,  true,  get_elem_d, set_elem_h, 3, false)
GEN_N4CLIP_MM_HELPER(mn4cliplu_d_mm, false, get_elem_d, set_elem_h, 3, false)


static inline void mmext_n4clip_mv(void *md, void *ms1, void *ms2, target_ulong s1,
                                   CPURISCVState *env, mmext_get_elem * get_elem,
                                   mmext_set_elem * set_elem, bool use_signed,
                                   uint8_t esz, bool high){
    uint32_t i, k, idx;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit;
    uint8_t round;
    uint32_t rows = get_mrows(env);

    uint32_t col_offset = high ? cols : 0;

    for (idx = 0; idx < rows; idx++) {
        i = idx;
        if (ms1 == md) {
            if (idx == s1) {
                i = rows - 1;
            } else if (idx == rows - 1) {
                i = s1;
            }
        }
        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek >> esz)) {
                if (esz == 2) {
                    n_bit = (uint64_t) (get_elem(ms1, s1, k, env) & 0x1F);
                } else {
                    n_bit = (uint64_t) (get_elem(ms1, s1, k, env) & 0x3F);
                }

                /* deal with signed/unsigned right-shift */
                result = get_elem(ms2, i, k, env);
                if (use_signed) {
                    round = get_round(env->mxrm, result, n_bit);
                    result = (result >> n_bit) + round;
                } else {
                    if (esz == 2) {
                        round = get_round(env->mxrm, (uint32_t) result, n_bit);
                        result = ((uint32_t) result >> n_bit) + round;
                    } else {
                        round = get_round(env->mxrm, (uint64_t) result, n_bit);
                        result = ((uint64_t) result >> n_bit) + round;
                    }
                }

                /* deal with signed/unsigned 8/16-bit clip */
                if (esz == 2) {
                    result = clip8(result, use_signed, env);
                } else {
                    result = clip16(result, use_signed, env);
                }
                set_elem(md, i, k + col_offset, env, result);
            } else {
                set_elem(md, i, k + col_offset, env, 0);
            }
        }
    }
}

#define GEN_N4CLIP_MV_HELPER(insn, use_signed, get_elem, set_elem, ESZ, hi)  \
void HELPER(insn)(void *md, void *ms1, void *ms2, target_ulong s1,           \
                  CPURISCVState *env){                                       \
    mmext_n4clip_mv(md, ms1, ms2, s1, env,                                   \
                    get_elem, set_elem, use_signed, ESZ, hi);                \
}

GEN_N4CLIP_MV_HELPER(mn4cliph_s_mv_i,  true,  get_elem_s, set_elem_b, 2, true)
GEN_N4CLIP_MV_HELPER(mn4cliphu_s_mv_i, false, get_elem_s, set_elem_b, 2, true)
GEN_N4CLIP_MV_HELPER(mn4clipl_s_mv_i,  true,  get_elem_s, set_elem_b, 2, false)
GEN_N4CLIP_MV_HELPER(mn4cliplu_s_mv_i, false, get_elem_s, set_elem_b, 2, false)

GEN_N4CLIP_MV_HELPER(mn4cliph_d_mv_i,  true,  get_elem_d, set_elem_h, 3, true)
GEN_N4CLIP_MV_HELPER(mn4cliphu_d_mv_i, false, get_elem_d, set_elem_h, 3, true)
GEN_N4CLIP_MV_HELPER(mn4clipl_d_mv_i,  true,  get_elem_d, set_elem_h, 3, false)
GEN_N4CLIP_MV_HELPER(mn4cliplu_d_mv_i, false, get_elem_d, set_elem_h, 3, false)

/* integer conversion of half-byte instructions */
static inline void mmext_p_int_cvt(void* md, void* ms1, CPURISCVState* env,
                                   bool hi, bool use_signed) {
    uint32_t i, k;
    uint32_t cols = get_rlenb(env);
    int64_t result;
    uint32_t rows = get_mrows(env);
    uint32_t col_offset = hi ? cols : 0;

    for (i = 0; i < rows; i++) {
        for (k = 0; k < cols; k++) {
            result = get_elem_p(ms1, i, k + col_offset, env);
            if (use_signed) {
                result = (((int8_t) result) << 4) >> 4;
            }
            set_elem_b(md, i, k, env, result);
        }
    }
}

#define GEN_PINT_CVT_HELPER(insn, hi, use_signed)          \
void HELPER(insn)(void* md, void* ms1, CPURISCVState* env) \
{                                                          \
    mmext_p_int_cvt(md, ms1, env, hi, use_signed);         \
}

GEN_PINT_CVT_HELPER(mucvth_b_p, true,  false)
GEN_PINT_CVT_HELPER(mucvtl_b_p, false, false)
GEN_PINT_CVT_HELPER(mscvth_b_p, true,  true)
GEN_PINT_CVT_HELPER(mscvtl_b_p, false, true)

/* mmaqa instructions */

/* byte oprands accumulate to single word */
static inline int32_t macc_b_ss_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + a * b;
}

static inline int32_t macc_b_su_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + a * (uint8_t) b;
}

static inline int32_t macc_b_us_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + (uint8_t) a * b;
}

static inline int32_t macc_b_uu_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + (uint8_t) a * (uint8_t) b;
}

typedef int32_t macc_fn_b(int8_t, int8_t, int32_t);

static void mmext_mmaqa_b(void *md, void *ms1, void *ms2, CPURISCVState *env,
                          macc_fn_b *macc){
    uint32_t i, j, k;
    int32_t temp, psum;
    int8_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < env->sizek; k++) {
                oprd_a = get_elem_b(ms1, i, k, env);
                oprd_b = get_elem_b(ms2, j, k, env);
                temp = macc(oprd_a, oprd_b, temp);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_s(md, i, j, env);
                psum += temp;
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

#define GEN_MMAQA_B_HELPER(insn, macc_fn_b)                   \
void HELPER(insn)(void *md, void *ms1, void *ms2,             \
                  CPURISCVState *env){                        \
    mmext_mmaqa_b(md, ms1, ms2, env, macc_fn_b);              \
}

GEN_MMAQA_B_HELPER(mmaqa_b,   macc_b_ss_s)
GEN_MMAQA_B_HELPER(mmaqau_b,  macc_b_uu_s)
GEN_MMAQA_B_HELPER(mmaqaus_b, macc_b_us_s)
GEN_MMAQA_B_HELPER(mmaqasu_b, macc_b_su_s)

/* half byte oprands accumulate to single word */
static inline int32_t macc_p_ss_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (sextract32(a, start, length) * sextract32(b, start, length));
}

static inline int32_t macc_p_su_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (sextract32(a, start, length) * extract32(b, start, length));
}

static inline int32_t macc_p_us_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (extract32(a, start, length) * sextract32(b, start, length));
}

static inline int32_t macc_p_uu_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (extract32(a, start, length) * extract32(b, start, length));
}

typedef int32_t macc_fn_p(int8_t, int8_t, int32_t, uint32_t, uint32_t);

static void mmext_mmaqa_p(void *md, void *ms1, void *ms2, CPURISCVState *env,
                          macc_fn_p *macc){
    uint32_t i, j, k;
    int32_t temp, psum;
    int8_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < env->sizek; k++) {
                oprd_a = get_elem_b(ms1, i, k, env);
                oprd_b = get_elem_b(ms2, j, k, env);
                temp = macc(oprd_a, oprd_b, temp, 0, 4);
                temp = macc(oprd_a, oprd_b, temp, 4, 4);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_s(md, i, j, env);
                psum += temp;
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

#define GEN_MMAQA_P_HELPER(insn, macc_fn_p)                   \
void HELPER(insn)(void *md, void *ms1, void *ms2,             \
                  CPURISCVState *env){                        \
    mmext_mmaqa_p(md, ms1, ms2, env, macc_fn_p);              \
}

GEN_MMAQA_P_HELPER(pmmaqa_b,   macc_p_ss_s)
GEN_MMAQA_P_HELPER(pmmaqau_b,  macc_p_uu_s)
GEN_MMAQA_P_HELPER(pmmaqaus_b, macc_p_us_s)
GEN_MMAQA_P_HELPER(pmmaqasu_b, macc_p_su_s)

/* half word oprands accumulate to double words */
static inline int64_t macc_h_ss_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + a * b;
}

static inline int64_t macc_h_su_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + a * (uint16_t) b;
}

static inline int64_t macc_h_us_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + (uint16_t) a * b;
}

static inline int64_t macc_h_uu_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + (uint64_t)(uint16_t) a * (uint64_t)(uint16_t) b;
}

typedef int64_t macc_fn_h(int16_t, int16_t, int64_t);

static void mmext_mmaqa_h(void *md, void *ms1, void *ms2, CPURISCVState *env,
                          macc_fn_h *macc){
    uint32_t i, j, k;
    int64_t temp, psum;
    int16_t oprd_a, oprd_b;
    void *md_pair_1 = md;
    void *md_pair_2 = (void *) (((int8_t *) md) + get_mlenb(env));

    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->sizek >> 1); k++) {
                oprd_a = get_elem_h(ms1, i, k, env);
                oprd_b = get_elem_h(ms2, j, k, env);
                temp = macc(oprd_a, oprd_b, temp);
            }
            if (j >= (get_mrows(env) >> 1)) {
                if (i < env->sizem && j < env->sizen) {
                    psum = get_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                                      env);
                    psum += temp;
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, psum);
                } else {
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, 0);
                }
            } else {
                if (i < env->sizem && j < env->sizen) {
                    psum = get_elem_d(md_pair_1, i, j, env);
                    psum += temp;
                    set_elem_d(md_pair_1, i, j, env, psum);
                } else {
                    set_elem_d(md_pair_1, i, j, env, 0);
                }
            }
        }
    }
}

#define GEN_MMAQA_H_HELPER(insn, macc_fn_h)                   \
void HELPER(insn)(void *md, void *ms1, void *ms2,             \
                  CPURISCVState *env){                        \
    mmext_mmaqa_h(md, ms1, ms2, env, macc_fn_h);              \
}

GEN_MMAQA_H_HELPER(mmaqa_h,   macc_h_ss_d)
GEN_MMAQA_H_HELPER(mmaqau_h,  macc_h_uu_d)
GEN_MMAQA_H_HELPER(mmaqaus_h, macc_h_us_d)
GEN_MMAQA_H_HELPER(mmaqasu_h, macc_h_su_d)

/* half byte x byte accumulate to single word */
static inline int32_t macc_i8xi4_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + ((int32_t) a) * (sextract32(b, 0, 4));
}

static inline int32_t macc_i8xu4_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + ((int32_t) a) * (extract32(b, 0, 4));
}

static inline int32_t macc_u8xi4_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + ((int32_t) ((uint8_t) a)) * (sextract32(b, 0, 4));
}

static inline int32_t macc_u8xu4_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + ((uint32_t) ((uint8_t) a)) * (extract32(b, 0, 4));
}

typedef int32_t macc_bp_s(int8_t a, int8_t b, int32_t sum);

/* mixed-precision byte x half-byte to int32 matrix multiplication */
static void mmext_mmaqa_bp(void *md, void *ms1, void *ms2, target_ulong s1,
                           CPURISCVState *env, macc_bp_s *macc) {
    uint32_t i, j, k;
    int32_t temp, psum;
    int8_t oprd_a, oprd_b;
    uint32_t cols = get_rlenb(env);
    uint32_t k_start = cols * s1;

    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < env->sizek; k++) {
                oprd_a = get_elem_b(ms1, i, k, env);
                oprd_b = get_elem_p(ms2, j, k + k_start, env);
                temp = macc(oprd_a, oprd_b, temp);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_s(md, i, j, env);
                psum += temp;
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

#define GEN_MMAQA_HP_HELPER(insn, macc_fn_bp)            \
void HELPER(insn)(void *md, void *ms1, void *ms2,        \
                  target_ulong s1, CPURISCVState *env) { \
    mmext_mmaqa_bp(md, ms1, ms2, s1, env, macc_fn_bp);   \
}

GEN_MMAQA_HP_HELPER(mmaccsu_s_bp, macc_i8xu4_s)
GEN_MMAQA_HP_HELPER(mmaccu_s_bp,  macc_u8xu4_s)
GEN_MMAQA_HP_HELPER(mmaccus_s_bp, macc_u8xi4_s)
GEN_MMAQA_HP_HELPER(mmacc_s_bp,   macc_i8xi4_s)

/* floating point arithmetic instructions */

/* wrapped soft-float functions to have same function prototypes */
#define FP_BINOP_FN(width, op) float##width##_##op##_wrapped
#define FP_BINOP_WRAPPER_DEF(width, op)                                \
static inline uint64_t FP_BINOP_FN(width, op)(uint64_t a, uint64_t b,  \
                                              float_status *status)    \
{                                                                      \
    return float##width##_##op(a, b, status);                          \
}

FP_BINOP_WRAPPER_DEF(16, add)
FP_BINOP_WRAPPER_DEF(16, sub)
FP_BINOP_WRAPPER_DEF(16, mul)
FP_BINOP_WRAPPER_DEF(16, max)
FP_BINOP_WRAPPER_DEF(16, min)
FP_BINOP_WRAPPER_DEF(32, add)
FP_BINOP_WRAPPER_DEF(32, sub)
FP_BINOP_WRAPPER_DEF(32, mul)
FP_BINOP_WRAPPER_DEF(32, max)
FP_BINOP_WRAPPER_DEF(32, min)
FP_BINOP_WRAPPER_DEF(64, add)
FP_BINOP_WRAPPER_DEF(64, sub)
FP_BINOP_WRAPPER_DEF(64, mul)
FP_BINOP_WRAPPER_DEF(64, max)
FP_BINOP_WRAPPER_DEF(64, min)

#define FUNOP(unop) unop##_wrapped
#define FP_UNOP_WRAPPER_DEF(unop)                                     \
static inline uint64_t FUNOP(unop)(uint64_t a, float_status *status)  \
{                                                                     \
    return unop(a, status);                                           \
}

static inline uint32_t f16_to_f32_ieee(uint64_t a, float_status *status)
{
    return float16_to_float32(a, true, status);
}

static inline uint32_t f32_to_f16_ieee(uint64_t a, float_status *status)
{
    return float32_to_float16(a, true, status);
}

FP_UNOP_WRAPPER_DEF(bfloat16_to_float32)
FP_UNOP_WRAPPER_DEF(float8e4_to_float16)
FP_UNOP_WRAPPER_DEF(float8e4_to_float32)
FP_UNOP_WRAPPER_DEF(float8e5_to_float16)
FP_UNOP_WRAPPER_DEF(float8e5_to_float32)
FP_UNOP_WRAPPER_DEF(float16_to_float8e4)
FP_UNOP_WRAPPER_DEF(float16_to_float8e5)
FP_UNOP_WRAPPER_DEF(f16_to_f32_ieee)
FP_UNOP_WRAPPER_DEF(float32_to_bfloat16)
FP_UNOP_WRAPPER_DEF(float32_to_float8e4)
FP_UNOP_WRAPPER_DEF(float32_to_float8e5)
FP_UNOP_WRAPPER_DEF(f32_to_f16_ieee)
FP_UNOP_WRAPPER_DEF(float32_to_float64)
FP_UNOP_WRAPPER_DEF(float64_to_float32)

FP_UNOP_WRAPPER_DEF(float16_to_int8)
FP_UNOP_WRAPPER_DEF(float16_to_uint8)
FP_UNOP_WRAPPER_DEF(int8_to_float16)
FP_UNOP_WRAPPER_DEF(uint8_to_float16)
FP_UNOP_WRAPPER_DEF(float32_to_int32)
FP_UNOP_WRAPPER_DEF(float32_to_uint32)
FP_UNOP_WRAPPER_DEF(uint32_to_float32)
FP_UNOP_WRAPPER_DEF(int32_to_float32)

typedef uint64_t fp_binop(uint64_t, uint64_t, float_status *);

/* floating point matrix-matrix binary operations */
static inline void mmext_fp_mm(void* md, void* ms1, void* ms2,
                               CPURISCVState* env, mmext_get_elem* get_elem,
                               mmext_set_elem* set_elem, fp_binop *fp_fn,
                               uint8_t esz) {
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint32_t rows = get_mrows(env);

    for (i = 0; i < rows; i++) {
        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek >> esz)) {
                int64_t oprd_a = get_elem(ms2, i, k, env);
                int64_t oprd_b = get_elem(ms1, i, k, env);
                result = fp_fn(oprd_a, oprd_b, &env->mfp_status);
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_FP_MM_HELPER(insn, get_elem, set_elem, ESZ, fp_fn)          \
void HELPER(insn)(void* md, void* ms1, void* ms2, CPURISCVState* env)   \
{                                                                       \
    mmext_fp_mm(md, ms1, ms2, env, get_elem, set_elem, fp_fn, ESZ);     \
}

GEN_FP_MM_HELPER(mfadd_h_mm, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, add))
GEN_FP_MM_HELPER(mfadd_s_mm, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, add))
GEN_FP_MM_HELPER(mfadd_d_mm, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, add))
GEN_FP_MM_HELPER(mfmax_h_mm, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, max))
GEN_FP_MM_HELPER(mfmax_s_mm, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, max))
GEN_FP_MM_HELPER(mfmax_d_mm, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, max))
GEN_FP_MM_HELPER(mfmin_h_mm, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, min))
GEN_FP_MM_HELPER(mfmin_s_mm, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, min))
GEN_FP_MM_HELPER(mfmin_d_mm, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, min))
GEN_FP_MM_HELPER(mfmul_h_mm, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, mul))
GEN_FP_MM_HELPER(mfmul_s_mm, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, mul))
GEN_FP_MM_HELPER(mfmul_d_mm, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, mul))
GEN_FP_MM_HELPER(mfsub_h_mm, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, sub))
GEN_FP_MM_HELPER(mfsub_s_mm, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, sub))
GEN_FP_MM_HELPER(mfsub_d_mm, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, sub))

/* floating point matrix-vector(immediate-indexed) binary operations */
static inline void mmext_fp_mv(void* md, void* ms1, void* ms2, target_ulong s1,
                               CPURISCVState* env, mmext_get_elem* get_elem,
                               mmext_set_elem* set_elem, fp_binop *fp_fn,
                               uint8_t esz) {
    uint32_t i, k, idx;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint32_t rows = get_mrows(env);

    for (idx = 0; idx < rows; idx++) {
        i = idx;
        if (ms1 == md) {
            if (idx == s1) {
                i = rows - 1;
            } else if (idx == rows - 1) {
                i = s1;
            }
        }
        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek >> esz)) {
                int64_t oprd_a = get_elem(ms2, i, k, env);
                int64_t oprd_b = get_elem(ms1, s1, k, env);
                result = fp_fn(oprd_a, oprd_b, &env->mfp_status);
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_FP_MV_HELPER(insn, get_elem, set_elem, ESZ, fp_fn)          \
void HELPER(insn)(void* md, void* ms1, void* ms2, target_ulong s1,      \
                  CPURISCVState* env)                                   \
{                                                                       \
    mmext_fp_mv(md, ms1, ms2, s1, env, get_elem, set_elem, fp_fn, ESZ); \
}

GEN_FP_MV_HELPER(mfadd_h_mv_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, add))
GEN_FP_MV_HELPER(mfadd_s_mv_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, add))
GEN_FP_MV_HELPER(mfadd_d_mv_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, add))
GEN_FP_MV_HELPER(mfmax_h_mv_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, max))
GEN_FP_MV_HELPER(mfmax_s_mv_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, max))
GEN_FP_MV_HELPER(mfmax_d_mv_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, max))
GEN_FP_MV_HELPER(mfmin_h_mv_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, min))
GEN_FP_MV_HELPER(mfmin_s_mv_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, min))
GEN_FP_MV_HELPER(mfmin_d_mv_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, min))
GEN_FP_MV_HELPER(mfmul_h_mv_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, mul))
GEN_FP_MV_HELPER(mfmul_s_mv_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, mul))
GEN_FP_MV_HELPER(mfmul_d_mv_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, mul))
GEN_FP_MV_HELPER(mfsub_h_mv_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, sub))
GEN_FP_MV_HELPER(mfsub_s_mv_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, sub))
GEN_FP_MV_HELPER(mfsub_d_mv_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, sub))

/* floating point type conversion operations */

typedef uint64_t fp_unop(uint64_t, float_status *);

/* floating point and integer type conversion */

static inline void mmext_fp_cvt(void* md, void* ms1, CPURISCVState* env,
                                mmext_get_elem* get_elem,
                                mmext_set_elem* set_elem,
                                fp_unop *fp_fn, uint8_t esz, bool hi,
                                bool widen) {
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint32_t rows = get_mrows(env);
    uint32_t src_col_offset = 0, dst_col_offset = 0;
    if (hi) {
        if (widen) {
            src_col_offset = cols;
        } else {
            dst_col_offset = cols;
        }
    }

    for (i = 0; i < rows; i++) {
        for (k = 0; k < cols; k++) {
            int64_t oprd_a = get_elem(ms1, i, k + src_col_offset, env);
            result = fp_fn(oprd_a, &env->mfp_status);
            set_elem(md, i, k + dst_col_offset, env, result);
        }
    }
}

#define GEN_FP_CVT_HELPER(insn, get_ty, set_ty, ESZ, fp_fn, hi, lg2_r)  \
void HELPER(insn)(void* md, void* ms1, CPURISCVState* env)              \
{                                                                       \
    mmext_fp_cvt(md, ms1, env, get_elem_##get_ty, set_elem_##set_ty,    \
                 fp_fn, ESZ, hi, lg2_r);                                \
}

/* floating-point v.s. floating-point conversion */
GEN_FP_CVT_HELPER(mfcvth_bf16_s, s, h, 2, FUNOP(float32_to_bfloat16), 1, 0)
GEN_FP_CVT_HELPER(mfcvth_e4_h,   h, b, 1, FUNOP(float16_to_float8e4), 1, 0)
GEN_FP_CVT_HELPER(mfcvth_e4_s,   s, b, 2, FUNOP(float32_to_float8e4), 1, 0)
GEN_FP_CVT_HELPER(mfcvth_e5_h,   h, b, 1, FUNOP(float16_to_float8e5), 1, 0)
GEN_FP_CVT_HELPER(mfcvth_e5_s,   h, s, 2, FUNOP(float32_to_float8e5), 1, 0)
GEN_FP_CVT_HELPER(mfcvth_h_e4,   b, h, 1, FUNOP(float8e4_to_float16), 1, 1)
GEN_FP_CVT_HELPER(mfcvth_h_e5,   b, h, 1, FUNOP(float8e5_to_float16), 1, 1)
GEN_FP_CVT_HELPER(mfcvth_h_s,    s, h, 2, FUNOP(f32_to_f16_ieee),     1, 0)
GEN_FP_CVT_HELPER(mfcvth_s_bf16, h, s, 2, FUNOP(bfloat16_to_float32), 1, 1)
GEN_FP_CVT_HELPER(mfcvth_s_h,    h, s, 2, FUNOP(f16_to_f32_ieee),     1, 1)
GEN_FP_CVT_HELPER(mfcvtl_bf16_s, s, h, 2, FUNOP(float32_to_bfloat16), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e4_h,   h, b, 1, FUNOP(float16_to_float8e4), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e4_s,   s, b, 2, FUNOP(float32_to_float8e4), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e5_h,   h, b, 1, FUNOP(float16_to_float8e5), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e5_s,   h, s, 2, FUNOP(float32_to_float8e5), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_h_e4,   b, h, 1, FUNOP(float8e4_to_float16), 0, 1)
GEN_FP_CVT_HELPER(mfcvtl_h_e5,   b, h, 1, FUNOP(float8e5_to_float16), 0, 1)
GEN_FP_CVT_HELPER(mfcvtl_h_s,    s, h, 2, FUNOP(f32_to_f16_ieee),     0, 0)
GEN_FP_CVT_HELPER(mfcvtl_s_bf16, h, s, 2, FUNOP(bfloat16_to_float32), 0, 1)
GEN_FP_CVT_HELPER(mfcvtl_s_h,    h, s, 2, FUNOP(f16_to_f32_ieee),     0, 1)
GEN_FP_CVT_HELPER(mfcvth_d_s,    s, d, 3, FUNOP(float32_to_float64),  1, 1)
GEN_FP_CVT_HELPER(mfcvth_s_d,    d, s, 3, FUNOP(float64_to_float32),  1, 0)
GEN_FP_CVT_HELPER(mfcvtl_d_s,    s, d, 3, FUNOP(float32_to_float64),  0, 1)
GEN_FP_CVT_HELPER(mfcvtl_s_d,    d, s, 3, FUNOP(float64_to_float32),  0, 0)

/* floating-point v.s. integer conversion */
GEN_FP_CVT_HELPER(mfscvt_s_s,  s, s, 2, FUNOP(float32_to_int32),  0, 0)
GEN_FP_CVT_HELPER(mfscvth_b_h, h, b, 1, FUNOP(float16_to_int8),   1, 0)
GEN_FP_CVT_HELPER(mfscvtl_b_h, h, b, 1, FUNOP(float16_to_int8),   0, 0)
GEN_FP_CVT_HELPER(mfucvt_s_s,  s, s, 2, FUNOP(float32_to_uint32), 0, 0)
GEN_FP_CVT_HELPER(mfucvth_b_h, h, b, 1, FUNOP(float16_to_int8),   1, 0)
GEN_FP_CVT_HELPER(mfucvtl_b_h, h, b, 1, FUNOP(float16_to_uint8),  0, 0)
GEN_FP_CVT_HELPER(msfcvt_s_s,  s, s, 2, FUNOP(int32_to_float32),  0, 0)
GEN_FP_CVT_HELPER(msfcvth_h_b, b, h, 1, FUNOP(int8_to_float16),   1, 1)
GEN_FP_CVT_HELPER(msfcvtl_h_b, b, h, 1, FUNOP(int8_to_float16),   0, 1)
GEN_FP_CVT_HELPER(mufcvt_s_s,  s, s, 2, FUNOP(uint32_to_float32), 0, 0)
GEN_FP_CVT_HELPER(mufcvth_h_b, b, h, 1, FUNOP(uint8_to_float16),  1, 1)
GEN_FP_CVT_HELPER(mufcvtl_h_b, b, h, 1, FUNOP(uint8_to_float16),  0, 1)

/* fmmacc instructions */
void helper_fmmacc_h(void *md, void *ms1, void *ms2,
                     CPURISCVState *env, uint32_t use_bf16){
    uint32_t i, j, k;
    uint16_t temp, psum;
    uint16_t oprd_a, oprd_b;
    void *ms2_pair_1 = ms2;
    void *ms2_pair_2 = (void *) (((int8_t *) ms2) + get_mlenb(env));
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env) * 2; j++) {
            temp = 0;
            for (k = 0; k < (env->sizek >> 1); k++) {
                oprd_a = get_elem_h(ms1, i, k, env);
                if (j >= get_mrows(env)) {
                    oprd_b = get_elem_h(ms2_pair_2, j % (get_mrows(env)),
                                        k, env);
                } else {
                    oprd_b = get_elem_h(ms2_pair_1, j, k, env);
                }
                if (use_bf16) {
                    temp = fmaccbf16(oprd_a, oprd_b, temp, &env->mfp_status);
                } else {
                    temp = fmacc16(oprd_a, oprd_b, temp, &env->mfp_status);
                }
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_h(md, i, j, env);
                if (use_bf16) {
                    psum = bfloat16_add(psum, temp, &env->mfp_status);
                } else {
                    psum = float16_add(psum, temp, &env->mfp_status);
                }
                set_elem_h(md, i, j, env, psum);
            } else {
                set_elem_h(md, i, j, env, 0);
            }
        }
    }
}

void helper_fmmacc_s(void *md, void *ms1, void *ms2,
                     CPURISCVState *env){
    uint32_t i, j, k;
    uint32_t temp, psum;
    uint32_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->sizek >> 2); k++) {
                oprd_a = get_elem_s(ms1, i, k, env);
                oprd_b = get_elem_s(ms2, j, k, env);
                temp = fmacc32(oprd_a, oprd_b, temp, &env->mfp_status);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_s(md, i, j, env);
                psum = float32_add(psum, temp, &env->mfp_status);
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

void helper_fmmacc_d(void *md, void *ms1, void *ms2,
                     CPURISCVState *env){
    uint32_t i, j, k;
    uint64_t temp, psum;
    uint64_t oprd_a, oprd_b;
    void *md_pair_1 = md;
    void *md_pair_2 = (void *) (((int8_t *) md) + get_mlenb(env));
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->sizek >> 3); k++) {
                oprd_a = get_elem_d(ms1, i, k, env);
                oprd_b = get_elem_d(ms2, j, k, env);
                temp = fmacc64(oprd_a, oprd_b, temp, &env->mfp_status);
            }
            if (j >= (get_mrows(env) >> 1)) {
                if (i <= env->sizem && j <= env->sizen) {
                    psum = get_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1), env);
                    psum = float64_add(psum, temp, &env->mfp_status);
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, psum);
                } else {
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, 0);
                }
            } else {
                if (i < env->sizem && j < env->sizen) {
                    psum = get_elem_d(md_pair_1, i, j, env);
                    psum = float64_add(psum, temp, &env->mfp_status);
                    set_elem_d(md, i, j, env, psum);
                } else {
                    set_elem_d(md, i, j, env, 0);
                }
            }
        }
    }
}

/* fmmacc.s.e4: float8e4 x float8e4 + float32 -> float32 */
static inline uint64_t fmacc_f8e4_to_f32(uint64_t a, uint64_t b, uint64_t c,
                                         float_status *s) {
    float32 a_f32 = float8e4_to_float32((uint8_t) a, s);
    float32 b_f32 = float8e4_to_float32((uint8_t) b, s);
    return fmacc32(a_f32, b_f32, c, s);
}

/* fmmacc.s.e5: float8e5 x float8e5 + float32 -> float32 */
static inline uint64_t fmacc_f8e5_to_f32(uint64_t a, uint64_t b, uint64_t c,
                                         float_status *s) {
    float32 a_f32 = float8e5_to_float32((uint8_t) a, s);
    float32 b_f32 = float8e5_to_float32((uint8_t) b, s);
    return fmacc32(a_f32, b_f32, c, s);
}

/* fmmacc.s.bf16: bfloat16 x bfloat16 + float32 -> float32 */
static inline uint64_t fmacc_bf16_to_f32(uint64_t a, uint64_t b, uint64_t c,
                                         float_status *s) {
    float32 a_f32 = bfloat16_to_float32((uint16_t) a, s);
    float32 b_f32 = bfloat16_to_float32((uint16_t) b, s);
    return fmacc32(a_f32, b_f32, c, s);
}

/* fmmacc.s.h: float16 x float16 + float32 -> float32 */
static inline uint64_t fmacc_f16_to_f32(uint64_t a, uint64_t b, uint64_t c,
                                        float_status *s) {
    float32 a_f32 = float16_to_float32((uint16_t) a, true, s);
    float32 b_f32 = float16_to_float32((uint16_t) b, true, s);
    return fmacc32(a_f32, b_f32, c, s);
}

/* fmmacc.h.e4: float8e4 x float8e4 + float16 -> float16 */
static inline uint64_t fmacc_f8e4_to_f16(uint64_t a, uint64_t b, uint64_t c,
                                         float_status *s) {
    float16 a_f16 = float8e4_to_float16((uint8_t) a, s);
    float16 b_f16 = float8e4_to_float16((uint8_t) b, s);
    return fmacc16(a_f16, b_f16, c, s);
}

/* fmmacc.h.e4: float8e5 x float8e5 + float16 -> float16 */
static inline uint64_t fmacc_f8e5_to_f16(uint64_t a, uint64_t b, uint64_t c,
                                         float_status *s) {
    float16 a_f16 = float8e5_to_float16((uint8_t) a, s);
    float16 b_f16 = float8e5_to_float16((uint8_t) b, s);
    return fmacc16(a_f16, b_f16, c, s);
}

/* fmmacc.bf16.e4 */
static inline uint64_t fmacc_f8e4_to_bf16(uint64_t a, uint64_t b, uint64_t c,
                                          float_status *s) {
    bfloat16 a_bf16 = float8e4_to_bfloat16((uint8_t) a, s);
    bfloat16 b_bf16 = float8e4_to_bfloat16((uint8_t) b, s);
    return fmaccbf16(a_bf16, b_bf16, c, s);
}

/* fmmacc.bf16.e5 */
static inline uint64_t fmacc_f8e5_to_bf16(uint64_t a, uint64_t b, uint64_t c,
                                          float_status *s) {
    bfloat16 a_bf16 = float8e5_to_bfloat16((uint8_t) a, s);
    bfloat16 b_bf16 = float8e5_to_bfloat16((uint8_t) b, s);
    return fmaccbf16(a_bf16, b_bf16, c, s);
}

typedef uint64_t fmacc_fn(uint64_t, uint64_t, uint64_t, float_status*);

static inline void mmext_fmmacc_b_to_h(void *md, void *ms1, void *ms2,
                                       CPURISCVState *env, fmacc_fn *macc_fn,
                                       fp_binop *add_fn) {
    uint32_t i, j, k;
    uint16_t temp, psum;
    uint16_t oprd_a, oprd_b;
    void *ms2_pair_1 = ms2;
    void *ms2_pair_2 = (void *) (((int8_t *) ms2) + get_mlenb(env));
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env) * 2; j++) {
            temp = 0;
            for (k = 0; k < env->sizek; k++) {
                oprd_a = get_elem_b(ms1, i, k, env);
                if (j >= get_mrows(env)) {
                    oprd_b = get_elem_b(ms2_pair_2, j % (get_mrows(env)),
                                        k, env);
                } else {
                    oprd_b = get_elem_b(ms2_pair_1, j, k, env);
                }
                temp = macc_fn(oprd_a, oprd_b, temp, &env->mfp_status);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_h(md, i, j, env);
                psum = add_fn(psum, temp, &env->mfp_status);
                set_elem_h(md, i, j, env, psum);
            } else {
                set_elem_h(md, i, j, env, 0);
            }
        }
    }
}

static inline void mmext_fmmacc_to_s(void *md, void *ms1, void *ms2,
                                     CPURISCVState *env, uint8_t esz,
                                     fmacc_fn *macc_fn,
                                     mmext_get_elem *get_elem) {
    uint32_t i, j, k;
    uint32_t temp, psum;
    uint32_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->sizek >> esz); k++) {
                oprd_a = get_elem(ms1, i, k, env);
                oprd_b = get_elem(ms2, j, k, env);
                temp = macc_fn(oprd_a, oprd_b, temp, &env->mfp_status);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_s(md, i, j, env);
                psum = float32_add(psum, temp, &env->mfp_status);
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

#define GEN_FMMACCH_B_HELPER(insn, macc, add)                         \
void HELPER(insn)(void* md, void* ms1, void* ms2, CPURISCVState* env) \
{                                                                     \
    mmext_fmmacc_b_to_h(md, ms1, ms2, env, macc, add);                \
}

#define GEN_FMMACC_S_HELPER(insn, macc, get_ty, esz)                    \
void HELPER(insn)(void* md, void* ms1, void* ms2, CPURISCVState* env)   \
{                                                                       \
    mmext_fmmacc_to_s(md, ms1, ms2, env, esz, macc, get_elem_##get_ty); \
}

static inline uint64_t bfloat16_add_wrapped(uint64_t a, uint64_t b,
                                            float_status *s) {
    return bfloat16_add(a, b, s);
}

GEN_FMMACCH_B_HELPER(fmmacc_bf16_e4, fmacc_f8e4_to_bf16, bfloat16_add_wrapped)
GEN_FMMACCH_B_HELPER(fmmacc_bf16_e5, fmacc_f8e5_to_bf16, bfloat16_add_wrapped)
GEN_FMMACCH_B_HELPER(fmmacc_h_e4,    fmacc_f8e4_to_f16,  FP_BINOP_FN(16, add))
GEN_FMMACCH_B_HELPER(fmmacc_h_e5,    fmacc_f8e5_to_f16,  FP_BINOP_FN(16, add))

GEN_FMMACC_S_HELPER(fmmacc_s_bf16, fmacc_bf16_to_f32, h, 1)
GEN_FMMACC_S_HELPER(fmmacc_s_h,    fmacc_f16_to_f32,  h, 1)
GEN_FMMACC_S_HELPER(fmmacc_s_e4,   fmacc_f8e4_to_f32, b, 0)
GEN_FMMACC_S_HELPER(fmmacc_s_e5,   fmacc_f8e5_to_f32, b, 0)

/* floating point mixed precision matrix-multiplication-accumulation */

/* fmmacc.h.hp: float16 x signed-half-byte + float16 -> float16 */
static inline uint64_t fmacc_f16xi4_to_f16(uint64_t a, uint64_t b, uint64_t c,
                                          float_status *s) {
    float16 b_f16 = int8_to_float16((((int8_t) b) << 4) >> 4, s);
    return fmacc16(a, b_f16, c, s);
}

/* fmmacc.s.hp: float16 x signed-half-byte + float32 -> float32 */
static inline uint64_t fmacc_f16xi4_to_f32(uint64_t a, uint64_t b, uint64_t c,
                                          float_status *s) {
    float32 a_f32 = f16_to_f32_ieee(a, s);
    float32 b_f32 = int16_to_float32((((int16_t) b) << 12) >> 12, s);
    return fmacc32(a_f32, b_f32, c, s);
}

/* fmmaccu.h.hp: float16 x unsigned-half-byte + float16 -> float16 */
static inline uint64_t fmacc_f16xu4_to_f16(uint64_t a, uint64_t b, uint64_t c,
                                           float_status *s) {
    float16 b_f16 = uint8_to_float16((uint8_t) (b & 0x0f), s);
    return fmacc16(a, b_f16, c, s);
}

/* fmmaccu.s.hp: float16 x unsigned-half-byte + float32 -> float32 */
static inline uint64_t fmacc_f16xu4_to_f32(uint64_t a, uint64_t b, uint64_t c,
                                           float_status *s) {
    float32 a_f32 = f16_to_f32_ieee(a, s);
    float32 b_f32 = uint16_to_float32((uint16_t) (b & 0x0f), s);
    return fmacc32(a_f32, b_f32, c, s);
}

/* fmmacc.h.hb: float16 x signed int8 + float16 -> float16 */
static inline uint64_t fmacc_f16xi8_to_f16(uint64_t a, uint64_t b, uint64_t c,
                                           float_status *s) {
    float16 b_f16 = int8_to_float16((int8_t) b, s);
    return fmacc16(a, b_f16, c, s);
}

/* fmmacc.s.hb: float16 x signed int8 + float32 -> float32 */
static inline uint64_t fmacc_f16xi8_to_f32(uint64_t a, uint64_t b, uint64_t c,
                                           float_status *s) {
    float32 a_f32 = f16_to_f32_ieee(a, s);
    float32 b_f32 = int16_to_float32((((int16_t) b) << 8) >> 8, s);
    return fmacc32(a_f32, b_f32, c, s);
}

/* fmmaccu.h.hb: float16 x unsigned int8 + float16 -> float16 */
static inline uint64_t fmacc_f16xu8_to_f16(uint64_t a, uint64_t b, uint64_t c,
                                           float_status *s) {
    float16 b_f16 = uint8_to_float16((uint8_t) b, s);
    return fmacc16(a, b_f16, c, s);
}

/* fmmaccu.s.hb: float16 x unsigned int8 + float32 -> float32 */
static inline uint64_t fmacc_f16xu8_to_f32(uint64_t a, uint64_t b, uint64_t c,
                                           float_status *s) {
    float32 a_f32 = f16_to_f32_ieee(a, s);
    float32 b_f32 = uint16_to_float32((uint16_t) (b & 0xff), s);
    return fmacc32(a_f32, b_f32, c, s);
}

static inline void mmext_mixed_prec_fmmacc(void *md, void *ms1, void *ms2,
                                           target_ulong s2, CPURISCVState *env,
                                           mmext_get_elem *get_a,
                                           mmext_get_elem *get_b,
                                           mmext_get_elem *get_c,
                                           mmext_set_elem *set_elem,
                                           fmacc_fn *macc_fn, fp_binop *add_fn,
                                           int8_t esz, int8_t total_parts) {
    uint32_t i, j, k;
    uint32_t cols = get_rlenb(env) >> esz;
    uint64_t oprd_a, oprd_b, temp;

    uint32_t k_start = s2 * cols * total_parts;
    for (uint32_t part = 0; part < total_parts; part++) {
        uint32_t dst_offset = part * get_mrows(env);
        for (i = 0; i < get_mrows(env); i++) {
            for (j = 0; j < get_mrows(env); j++) {
                temp = 0;
                for (k = 0; k < (env->sizek >> esz); k++) {
                    oprd_a = get_a(ms1, i, k, env);
                    oprd_b = get_b(ms2, j, k_start + k, env);
                    temp = macc_fn(oprd_a, oprd_b, temp, &env->mfp_status);
                }
                if (i < env->sizem && j < env->sizen) {
                    uint64_t psum = get_c(md, i, j + dst_offset, env);
                    psum = add_fn(psum, temp, &env->mfp_status);
                    set_elem(md, i, j + dst_offset, env, psum);
                } else {
                    set_elem(md, i, j + dst_offset, env, 0);
                }
            }
        }
        k_start += cols;
    }
}

#define GEN_MPFMMACC_I_HELPER(insn, ty_a, ty_b, ty_c, macc, add, tot)  \
void HELPER(insn)(void* md, void* ms1, void* ms2, target_ulong s2,     \
                  CPURISCVState* env)                                  \
{                                                                      \
    mmext_mixed_prec_fmmacc(md, ms1, ms2, s2, env, get_elem_##ty_a,    \
                            get_elem_##ty_b, get_elem_##ty_c,          \
                            set_elem_##ty_c, macc, add, 1, tot);       \
}

#define GEN_MPFMMACC_HELPER(insn, ty_a, ty_b, ty_c, macc, add, tot)    \
void HELPER(insn)(void* md, void* ms1, void* ms2, CPURISCVState* env)  \
{                                                                      \
    mmext_mixed_prec_fmmacc(md, ms1, ms2, 0, env, get_elem_##ty_a,     \
                            get_elem_##ty_b, get_elem_##ty_c,          \
                            set_elem_##ty_c, macc, add, 1, tot);       \
}

#define FADD32 FP_BINOP_FN(32, add)
#define FADD16 FP_BINOP_FN(16, add)

GEN_MPFMMACC_I_HELPER(fmmacc_h_hp,  h, p, h, fmacc_f16xi4_to_f16, FADD16, 2)
GEN_MPFMMACC_I_HELPER(fmmacc_s_hb,  h, b, s, fmacc_f16xi8_to_f32, FADD32, 1)
GEN_MPFMMACC_I_HELPER(fmmacc_s_hp,  h, p, s, fmacc_f16xi4_to_f32, FADD32, 1)
GEN_MPFMMACC_I_HELPER(fmmaccu_h_hp, h, p, h, fmacc_f16xu4_to_f16, FADD16, 2)
GEN_MPFMMACC_I_HELPER(fmmaccu_s_hb, h, b, s, fmacc_f16xi8_to_f32, FADD32, 1)
GEN_MPFMMACC_I_HELPER(fmmaccu_s_hp, h, p, s, fmacc_f16xu4_to_f32, FADD32, 1)

GEN_MPFMMACC_HELPER(fmmacc_h_hb,  h, b, h, fmacc_f16xi8_to_f16, FADD16, 2)
GEN_MPFMMACC_HELPER(fmmaccu_h_hb, h, b, h, fmacc_f16xu8_to_f16, FADD16, 2)

void helper_fmmacc_d_s(void *md, void *ms1, void *ms2,
                       CPURISCVState *env) {
    uint32_t i, j, k;
    uint64_t temp, psum;
    uint32_t oprd_a, oprd_b;
    void *md_pair_1 = md;
    void *md_pair_2 = (void *) (((int8_t *) md) + get_mlenb(env));
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->sizek >> 2); k++) {
                oprd_a = get_elem_s(ms1, i, k, env);
                oprd_b = get_elem_s(ms2, j, k, env);
                temp = fwmacc32(oprd_a, oprd_b, temp, &env->mfp_status);
            }
            if (j >= (get_mrows(env) >> 1)) {
                if (i < env->sizem && j < env->sizen) {
                    psum = get_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1), env);
                    psum = float64_add(psum, temp, &env->mfp_status);
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, psum);
                } else {
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, 0);
                }
            } else {
                if (i < env->sizem && j < env->sizen) {
                    psum = get_elem_d(md_pair_1, i, j, env);
                    psum = float64_add(psum, temp, &env->mfp_status);
                    set_elem_d(md_pair_1, i, j, env, psum);
                } else {
                    set_elem_d(md_pair_1, i, j, env, 0);
                }
            }
        }
    }
}

/* load/store instructions */

#define MMEXT_LD_ELEM(NAME, LDSUF)                                         \
static int64_t NAME(CPURISCVState *env, target_ulong addr,                 \
                    uintptr_t retaddr){                                    \
    return cpu_##LDSUF##_data_ra(env, addr, retaddr);                      \
}

MMEXT_LD_ELEM(ld_b, ldsb)
MMEXT_LD_ELEM(ld_h, ldsw)
MMEXT_LD_ELEM(ld_w, ldl)
MMEXT_LD_ELEM(ld_d, ldq)

typedef int64_t mmext_ld_fn(CPURISCVState *env, target_ulong addr,
                            uintptr_t retaddr);

static void mmext_mld(void *md, target_ulong rs1, target_ulong s2,
                      mmext_ld_fn *ld_elem, mmext_set_elem *set_elem,
                      CPURISCVState *env, uint8_t esz, uintptr_t ra,
                      bool streaming){
    uint32_t i, k;
    target_ulong addr;

    for (i = 0; i < env->sizem; i++) {
        probe_pages(env, rs1 + i * s2, env->sizek, ra,
                    MMU_DATA_LOAD);
    }

    for (i = 0; i < get_mrows(env); i++) {
        for (k = 0; k < (get_rlenb(env) >> esz); k++) {
            addr = rs1 + i * s2 + k * (1 << esz);
            if (i < env->sizem && k < (env->sizek >> esz)) {
                set_elem(md, i, k, env, ld_elem(env, addr, ra));
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
    if (gen_mem_trace()) {
        uint32_t packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        uint8_t type = streaming ? DATA_SRADDR : DATA_RADDR;
        for (i = 0; i < env->sizem; i++) {
            target_ulong row_start_addr = rs1 + i * s2;
            write_trace_8_8(type, packlen, env->sizek, row_start_addr);
            for (k = 0; k < env->sizek / 4; k++) {
                uint32_t data_value = get_elem_s(md, i, k, env);
                write_trace_8_8(DATA_VALUE, packlen, 0, data_value);
                if (env->sizek % 4) {
                    uint32_t mask =  (1 << (env->sizek % 4) * 8) - 1;
                    write_trace_8_8(DATA_VALUE, packlen, 0, data_value & mask);
                }
            }
        }
    }
}

#define GEN_MMEXT_LD_HELPER(insn, ld_elem, set_elem, ESZ, streaming) \
void HELPER(insn)(void *md, target_ulong rs1, target_ulong s2,       \
                  CPURISCVState *env){                               \
    mmext_mld(md, rs1, s2, ld_elem, set_elem, env, ESZ, GETPC(),     \
              streaming);                                            \
}

GEN_MMEXT_LD_HELPER(mld_b, ld_b, set_elem_b, 0, false)
GEN_MMEXT_LD_HELPER(mld_h, ld_h, set_elem_h, 1, false)
GEN_MMEXT_LD_HELPER(mld_w, ld_w, set_elem_s, 2, false)
GEN_MMEXT_LD_HELPER(mld_d, ld_d, set_elem_d, 3, false)

GEN_MMEXT_LD_HELPER(msld_b, ld_b, set_elem_b, 0, true)
GEN_MMEXT_LD_HELPER(msld_h, ld_h, set_elem_h, 1, true)
GEN_MMEXT_LD_HELPER(msld_w, ld_w, set_elem_s, 2, true)
GEN_MMEXT_LD_HELPER(msld_d, ld_d, set_elem_d, 3, true)

static void mmext_mldm(void *md, target_ulong rs1, uint8_t nf,
                       mmext_ld_fn *ld_elem, mmext_set_elem *set_elem,
                       CPURISCVState *env, uint8_t esz, uintptr_t ra){
    uint32_t n, i, k;
    target_ulong addr;
    void *temp;

    for (n = 0; n < nf; n++) {
        for (i = 0; i < get_mrows(env); i++) {
            addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i;
            probe_pages(env, addr, get_rlenb(env), ra,
                        MMU_DATA_LOAD);
        }
    }

    for (n = 0; n < nf; n++) {
        temp = (void *)((char *) md + n * get_mlenb(env));
        for (i = 0; i < get_mrows(env); i++) {
            for (k = 0; k < (get_rlenb(env) >> esz); k++) {
                addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i + k * (1 << esz);
                set_elem(temp, i, k, env, ld_elem(env, addr, ra));
            }
        }
    }
    if (gen_mem_trace()) {
        uint32_t packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        for (n = 0; n < nf; n++) {
            temp = (void *)((char *) md + n * get_mlenb(env));
            for (i = 0; i < get_mrows(env); i++) {
                target_ulong row_start_addr = rs1 + n * get_mlenb(env) +
                                              get_rlenb(env) * i;
                write_trace_8_8(DATA_RADDR, packlen, get_rlenb(env),
                                row_start_addr);
                for (k = 0; k < get_rlenb(env) / 4; k++) {
                    uint32_t data_value = get_elem_s(temp, i, k, env);
                    write_trace_8_8(DATA_VALUE, packlen, 0, data_value);
                }
            }
        }
    }
}

#define GEN_MMEXT_LDM_HELPER(insn, ld_elem, set_elem, ESZ, nf)          \
void HELPER(insn)(void *md, target_ulong rs1, CPURISCVState *env){      \
    mmext_mldm(md, rs1, nf, ld_elem, set_elem, env, ESZ, GETPC());      \
}

GEN_MMEXT_LDM_HELPER(mld1m_b, ld_b, set_elem_b, 0, 1)
GEN_MMEXT_LDM_HELPER(mld2m_b, ld_b, set_elem_b, 0, 2)
GEN_MMEXT_LDM_HELPER(mld4m_b, ld_b, set_elem_b, 0, 4)
GEN_MMEXT_LDM_HELPER(mld8m_b, ld_b, set_elem_b, 0, 8)

GEN_MMEXT_LDM_HELPER(mld1m_h, ld_h, set_elem_h, 1, 1)
GEN_MMEXT_LDM_HELPER(mld2m_h, ld_h, set_elem_h, 1, 2)
GEN_MMEXT_LDM_HELPER(mld4m_h, ld_h, set_elem_h, 1, 4)
GEN_MMEXT_LDM_HELPER(mld8m_h, ld_h, set_elem_h, 1, 8)

GEN_MMEXT_LDM_HELPER(mld1m_w, ld_w, set_elem_s, 2, 1)
GEN_MMEXT_LDM_HELPER(mld2m_w, ld_w, set_elem_s, 2, 2)
GEN_MMEXT_LDM_HELPER(mld4m_w, ld_w, set_elem_s, 2, 4)
GEN_MMEXT_LDM_HELPER(mld8m_w, ld_w, set_elem_s, 2, 8)

GEN_MMEXT_LDM_HELPER(mld1m_d, ld_d, set_elem_d, 3, 1)
GEN_MMEXT_LDM_HELPER(mld2m_d, ld_d, set_elem_d, 3, 2)
GEN_MMEXT_LDM_HELPER(mld4m_d, ld_d, set_elem_d, 3, 4)
GEN_MMEXT_LDM_HELPER(mld8m_d, ld_d, set_elem_d, 3, 8)

#define MMEXT_ST_ELEM(NAME, STSUF)                                      \
static void NAME(CPURISCVState *env, target_ulong addr, uint64_t val,   \
                 uintptr_t retaddr){                                    \
    cpu_##STSUF##_data_ra(env, addr, val, retaddr);                     \
}

MMEXT_ST_ELEM(st_b, stb)
MMEXT_ST_ELEM(st_h, stw)
MMEXT_ST_ELEM(st_w, stl)
MMEXT_ST_ELEM(st_d, stq)

typedef void mmext_st_fn(CPURISCVState *env, target_ulong addr, uint64_t val,
                         uintptr_t retaddr);

static void mmext_mst(void *ms3, target_ulong rs1, target_ulong s2,
                      mmext_st_fn *st_elem, mmext_get_elem *get_elem,
                      CPURISCVState *env, uint8_t esz, uintptr_t ra,
                      bool streaming){
    uint32_t i, k;
    target_ulong addr;

    for (i = 0; i < env->sizem; i++) {
        probe_pages(env, rs1 + i * s2, env->sizek, ra,
                    MMU_DATA_STORE);
    }

    for (i = 0; i < env->sizem; i++) {
        for (k = 0; k < (env->sizek >> esz); k++) {
            addr = rs1 + i * s2 + k * (1 << esz);
            st_elem(env, addr, get_elem(ms3, i, k, env), ra);
        }
    }
    if (gen_mem_trace()) {
        uint32_t packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        uint8_t type = streaming ? DATA_SWADDR : DATA_WADDR;
        for (i = 0; i < env->sizem; i++) {
            target_ulong row_start_addr = rs1 + i * s2;
            write_trace_8_8(type, packlen, env->sizek, row_start_addr);
            for (k = 0; k < env->sizek / 4; k++) {
                uint32_t data_value = get_elem_s(ms3, i, k, env);
                write_trace_8_8(DATA_VALUE, packlen, 0, data_value);
                if (env->sizek % 4) {
                    uint32_t mask =  (1 << (env->sizek % 4) * 8) - 1;
                    write_trace_8_8(DATA_VALUE, packlen, 0, data_value & mask);
                }
            }
        }
    }
}

#define GEN_MMEXT_ST_HELPER(insn, st_elem, get_elem, ESZ, streaming)    \
void HELPER(insn)(void *ms3, target_ulong rs1, target_ulong s2,         \
                  CPURISCVState *env){                                  \
    mmext_mst(ms3, rs1, s2, st_elem, get_elem, env, ESZ, GETPC(),       \
              streaming);                                               \
}

GEN_MMEXT_ST_HELPER(mst_b, st_b, get_elem_b, 0, false)
GEN_MMEXT_ST_HELPER(mst_h, st_h, get_elem_h, 1, false)
GEN_MMEXT_ST_HELPER(mst_w, st_w, get_elem_s, 2, false)
GEN_MMEXT_ST_HELPER(mst_d, st_d, get_elem_d, 3, false)

GEN_MMEXT_ST_HELPER(msst_b, st_b, get_elem_b, 0, true)
GEN_MMEXT_ST_HELPER(msst_h, st_h, get_elem_h, 1, true)
GEN_MMEXT_ST_HELPER(msst_w, st_w, get_elem_s, 2, true)
GEN_MMEXT_ST_HELPER(msst_d, st_d, get_elem_d, 3, true)

static void mmext_mstm(void *ms3, target_ulong rs1, uint8_t nf,
                       mmext_st_fn *st_elem, mmext_get_elem *get_elem,
                       CPURISCVState *env, uint8_t esz, uintptr_t ra){
    uint32_t n, i, k;
    target_ulong addr;
    void *temp;

    for (n = 0; n < nf; n++) {
        for (i = 0; i < get_mrows(env); i++) {
            addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i;
            probe_pages(env, addr, get_rlenb(env), ra,
                        MMU_DATA_STORE);
        }
    }

    for (n = 0; n < nf; n++) {
        temp = (void *)((char *) ms3 + n * get_mlenb(env));
        for (i = 0; i < get_mrows(env); i++) {
            for (k = 0; k < (get_rlenb(env) >> esz); k++) {
                addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i + k * (1 << esz);
                st_elem(env, addr, get_elem(temp, i, k, env), ra);
            }
        }
    }
    if (gen_mem_trace()) {
        uint32_t packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        for (n = 0; n < nf; n++) {
            temp = (void *)((char *) ms3 + n * get_mlenb(env));
            for (i = 0; i < get_mrows(env); i++) {
                target_ulong row_start_addr = rs1 + n * get_mlenb(env) +
                                              get_rlenb(env) * i;
                write_trace_8_8(DATA_WADDR, packlen, get_rlenb(env),
                                row_start_addr);
                for (k = 0; k < get_rlenb(env) / 4; k++) {
                    uint32_t data_value = get_elem_s(temp, i, k, env);
                    write_trace_8_8(DATA_VALUE, packlen, 0, data_value);
                }
            }
        }
    }
}

#define GEN_MMEXT_STM_HELPER(insn, st_elem, get_elem, ESZ, nf)         \
void HELPER(insn)(void *ms3, target_ulong rs1, CPURISCVState *env){    \
    mmext_mstm(ms3, rs1, nf, st_elem, get_elem, env, ESZ, GETPC());    \
}

GEN_MMEXT_STM_HELPER(mst1m_b, st_b, get_elem_b, 0, 1)
GEN_MMEXT_STM_HELPER(mst2m_b, st_b, get_elem_b, 0, 2)
GEN_MMEXT_STM_HELPER(mst4m_b, st_b, get_elem_b, 0, 4)
GEN_MMEXT_STM_HELPER(mst8m_b, st_b, get_elem_b, 0, 8)

GEN_MMEXT_STM_HELPER(mst1m_h, st_h, get_elem_h, 1, 1)
GEN_MMEXT_STM_HELPER(mst2m_h, st_h, get_elem_h, 1, 2)
GEN_MMEXT_STM_HELPER(mst4m_h, st_h, get_elem_h, 1, 4)
GEN_MMEXT_STM_HELPER(mst8m_h, st_h, get_elem_h, 1, 8)

GEN_MMEXT_STM_HELPER(mst1m_w, st_w, get_elem_s, 2, 1)
GEN_MMEXT_STM_HELPER(mst2m_w, st_w, get_elem_s, 2, 2)
GEN_MMEXT_STM_HELPER(mst4m_w, st_w, get_elem_s, 2, 4)
GEN_MMEXT_STM_HELPER(mst8m_w, st_w, get_elem_s, 2, 8)

GEN_MMEXT_STM_HELPER(mst1m_d, st_d, get_elem_d, 3, 1)
GEN_MMEXT_STM_HELPER(mst2m_d, st_d, get_elem_d, 3, 2)
GEN_MMEXT_STM_HELPER(mst4m_d, st_d, get_elem_d, 3, 4)
GEN_MMEXT_STM_HELPER(mst8m_d, st_d, get_elem_d, 3, 8)

/* matrix pack instructions */

static inline void mmext_pack(void *md, void *ms1, void *ms2,
                              CPURISCVState *env, bool a_hi, bool b_hi) {
    int32_t i;
    uint32_t rows = get_mrows(env);
    uint32_t mlenb = get_rlenb(env);
    void *md_addr, *ms1_addr, *ms2_addr;

    for (i = 0; i < rows; i++) {
        ms1_addr = ms1 + i * mlenb + (a_hi ? (mlenb >> 1) : 0);
        ms2_addr = ms2 + i * mlenb + (b_hi ? (mlenb >> 1) : 0);
        md_addr = md + i * mlenb;
        memcpy(md_addr, ms1_addr, mlenb >> 1);
        memcpy(md_addr + (mlenb >> 1), ms2_addr, mlenb >> 1);
    }
}

#define GEN_MPACK_HELPER(insn, a_hi, b_hi)                            \
void HELPER(insn)(void* md, void* ms1, void* ms2, CPURISCVState* env) \
{                                                                     \
    mmext_pack(md, ms1, ms2, env, a_hi, b_hi);                        \
}

GEN_MPACK_HELPER(mpack, false, false)
GEN_MPACK_HELPER(mpackhh, true, true)
GEN_MPACK_HELPER(mpackhl, true, false)

/* matrix column slide instructions */

static inline void mmext_mcslide(void *md, void *ms1, target_ulong s1,
                                 CPURISCVState *env, mmext_get_elem *get_elem,
                                 mmext_set_elem *set_elem, uint8_t esz,
                                 bool up){
    int32_t k, i;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t valid_uimm = s1 >= cols - 1 ? cols - 1 : s1;
    uint32_t rows = get_mrows(env);
    uint32_t dst_col;

    for (i = 0; i < rows; i++) {
        /* reverse direction iteration to avoid data overlap when slide up */
        for (k = up ? cols - 1 : 0; up ? k >= 0 : k < cols; up ? k-- : k++) {
            if (k >= valid_uimm) {
                if (up) {
                    result = get_elem(ms1, i, k - valid_uimm, env);
                    dst_col = k;
                } else {
                    result = get_elem(ms1, i, k, env);
                    dst_col = k - valid_uimm;
                }
            } else {
                result = 0;
                dst_col = up ? k : cols - 1 - k;
            }
            set_elem(md, i, dst_col, env, result);
        }
    }
}

#define GEN_MMEXT_OP_MCSLIDE(insn, get_elem, set_elem, ESZ, up)              \
void HELPER(insn)(void *md, void *ms1, target_ulong s1, CPURISCVState *env)  \
{                                                                            \
    mmext_mcslide(md, ms1, s1, env, get_elem, set_elem, ESZ, up);            \
}

GEN_MMEXT_OP_MCSLIDE(mcslidedown_b, get_elem_b, set_elem_b, 0, false)
GEN_MMEXT_OP_MCSLIDE(mcslidedown_h, get_elem_h, set_elem_h, 1, false)
GEN_MMEXT_OP_MCSLIDE(mcslidedown_s, get_elem_s, set_elem_s, 2, false)
GEN_MMEXT_OP_MCSLIDE(mcslidedown_d, get_elem_d, set_elem_d, 3, false)
GEN_MMEXT_OP_MCSLIDE(mcslideup_b, get_elem_b, set_elem_b, 0, true)
GEN_MMEXT_OP_MCSLIDE(mcslideup_h, get_elem_h, set_elem_h, 1, true)
GEN_MMEXT_OP_MCSLIDE(mcslideup_s, get_elem_s, set_elem_s, 2, true)
GEN_MMEXT_OP_MCSLIDE(mcslideup_d, get_elem_d, set_elem_d, 3, true)

/* matrix row slide instructions */

static inline void mmext_mrslide(void *md, void *ms1, target_ulong s1,
                                 CPURISCVState *env, bool up) {
    int32_t i, src_row, dst_row;
    uint32_t rows = get_mrows(env);
    uint32_t mlenb = get_rlenb(env);
    uint64_t valid_uimm = s1 >= rows - 1 ? rows - 1 : s1;

    /* reverse direction iteration to avoid data overlap when slide up */
    for (i = up ? rows - 1 : 0; up ? i >= 0 : i < rows; up ? i-- : i++) {
        if (i < valid_uimm) {
            dst_row = up ? i : rows - 1 - i;
            memset(md + dst_row * mlenb, 0, mlenb);
        } else {
            src_row = up ? i - valid_uimm : i;
            dst_row = up ? i : i - valid_uimm;
            memcpy(md + dst_row * mlenb, ms1 + src_row * mlenb, mlenb);
        }
    }
}

#define GEN_MMEXT_OP_MRSLIDE(insn, up)                                       \
void HELPER(insn)(void *md, void *ms1, target_ulong s1, CPURISCVState *env)  \
{                                                                            \
    mmext_mrslide(md, ms1, s1, env, up);                                     \
}

GEN_MMEXT_OP_MRSLIDE(mrslidedown, false)
GEN_MMEXT_OP_MRSLIDE(mrslideup, true)

/* matrix column move(broadcast) instructions */

static inline void mmext_mcmov(void *md, void *ms1, target_ulong s1,
                               CPURISCVState *env, mmext_get_elem *get_elem,
                               mmext_set_elem *set_elem, uint8_t esz) {
    uint32_t k, i;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t valid_uimm = s1 >= cols - 1 ? cols - 1 : s1;
    uint32_t rows = get_mrows(env);

    for (i = 0; i < rows; i++) {
        result = get_elem(ms1, i, valid_uimm, env);
        for (k = 0; k < cols; k++) {
            set_elem(md, i, k, env, result);
        }
    }
}

#define GEN_MMEXT_OP_MCMOV(insn, get_elem, set_elem, ESZ)                     \
void HELPER(insn)(void *md, void *ms1, target_ulong s1, CPURISCVState *env)   \
{                                                                             \
    mmext_mcmov(md, ms1, s1, env, get_elem, set_elem, ESZ);                   \
}

GEN_MMEXT_OP_MCMOV(mcmovb_mv_i, get_elem_b, set_elem_b, 0)
GEN_MMEXT_OP_MCMOV(mcmovh_mv_i, get_elem_h, set_elem_h, 1)
GEN_MMEXT_OP_MCMOV(mcmovs_mv_i, get_elem_s, set_elem_s, 2)
GEN_MMEXT_OP_MCMOV(mcmovd_mv_i, get_elem_d, set_elem_d, 3)
