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
#include "xt_reduction.h"
#include "sfu.h"

#if !defined(CONFIG_USER_ONLY)
#include "hw/riscv/riscv_tpe.h"
#endif

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

void riscv_cpu_set_xmsaten(CPURISCVState *env, bool sat)
{
    env->mfp_status.sat = sat;
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

static inline void set_elem_p(void *md, uint32_t i, uint32_t j,
                                 CPURISCVState *env, int64_t val) {
    uint32_t idx = i * get_rlenb(env) + (j >> 1);
    uint8_t ele = ((int8_t *) md)[idx];
    ele = (j & 0x1) ? ((ele & 0x0f) | (((uint8_t) val) << 4)) :
                      ((ele & 0xf0) | (((uint8_t) val) & 0xf));
    ((int8_t *) md)[idx] = (int8_t) ele;
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
                               bool keep_hi, bool col){
    uint32_t i, k, idx;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit, mask;
    uint32_t rows = get_mrows(env);
    if (col) {
        s1 &= cols - 1;
    } else {
        s1 &= rows - 1;
    }

    for (idx = 0; idx < rows; idx++) {
        int64_t oprd_b;
        i = idx;
        if (ms1 == md) {
            if (idx == s1) {
                i = rows - 1;
            } else if (idx == rows - 1) {
                i = s1;
            }
        }
        oprd_b = get_elem(ms1, i, s1, env);
        for (k = 0; k < cols; k++){
            if(i < env->sizem && k < (env->sizek >> esz)){
                if (!col) {
                    oprd_b = get_elem(ms1, s1, k, env);
                }
                switch(op){
                case ADD:
                    result = get_elem(ms2, i, k, env) + oprd_b;
                    break;
                case SUB:
                    result = get_elem(ms2, i, k, env) - oprd_b;
                    break;
                case MUL:
                    if (esz == 2) {
                        result = mul32(get_elem(ms2, i, k, env),
                                       oprd_b, keep_hi);
                    } else {
                        result = mul64(get_elem(ms2, i, k, env),
                                       oprd_b, keep_hi);
                    }
                    break;
                case SLL:
                case SRA:
                case SRL: {
                    if (esz == 2) {
                        n_bit = (uint64_t) (oprd_b & 0x1F);
                    } else {
                        n_bit = (uint64_t) (oprd_b & 0x3F);
                    }
                    result = get_elem(ms2, i, k, env);
                    if (op == SRA)
                        result = result >> n_bit;
                    else if (op == SRL) {
                        mask = get_unsigned_mask(esz);
                        result = (((uint64_t) result) & mask) >> n_bit;
                    } else
                        result = result << n_bit;
                    break;
                }
                case MAX:
                case MIN: {
                    int64_t oprd_a = get_elem(ms2, i, k, env);
                    result = (op == MAX ? oprd_a > oprd_b : oprd_a < oprd_b) ?
                            oprd_a : oprd_b;
                    break;
                }
                case UMAX:
                case UMIN: {
                    mask = get_unsigned_mask(esz);
                    uint64_t oprd_a = get_elem(ms2, i, k, env) & mask;
                    oprd_b = oprd_b & mask;
                    result = (op == UMAX ? oprd_a > oprd_b : oprd_a < oprd_b) ?
                            oprd_a : oprd_b;
                    break;
                }
                default:
                    break;
                }
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_OP_MV_MX_HELPER(insn, op, get_elem, set_elem, ESZ, keep_hi, col) \
void HELPER(insn)(void* md, void* ms1, void* ms2, target_ulong s1,      \
                  CPURISCVState* env){                                  \
    mmext_mv_mx(md, ms1, ms2, s1, env,                                  \
                get_elem, set_elem, op, ESZ, keep_hi, col);             \
}

GEN_OP_MV_MX_HELPER(madd_w_mv_i, ADD, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MV_MX_HELPER(msub_w_mv_i, SUB, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MV_MX_HELPER(msra_w_mv_i, SRA, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MV_MX_HELPER(mmul_w_mv_i, MUL, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MV_MX_HELPER(mmax_w_mv_i, MAX, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MV_MX_HELPER(mmin_w_mv_i, MIN, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MV_MX_HELPER(mumax_w_mv_i, UMAX, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MV_MX_HELPER(mumin_w_mv_i, UMIN, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MV_MX_HELPER(msll_w_mv_i, SLL, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MV_MX_HELPER(msrl_w_mv_i, SRL, get_elem_s, set_elem_s, 2, false, false)

GEN_OP_MV_MX_HELPER(madd_w_mc_i, ADD, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(msub_w_mc_i, SUB, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(msra_w_mc_i, SRA, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(mmul_w_mc_i, MUL, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(mmax_w_mc_i, MAX, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(mmin_w_mc_i, MIN, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(mumax_w_mc_i, UMAX, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(mumin_w_mc_i, UMIN, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(msll_w_mc_i, SLL, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(msrl_w_mc_i, SRL, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MV_MX_HELPER(mmulh_w_mc_i, MUL, get_elem_s, set_elem_s, 2, true, true)

GEN_OP_MV_MX_HELPER(madd_d_mv_i, ADD, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MV_MX_HELPER(msub_d_mv_i, SUB, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MV_MX_HELPER(msra_d_mv_i, SRA, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MV_MX_HELPER(mmul_d_mv_i, MUL, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MV_MX_HELPER(mmax_d_mv_i, MAX, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MV_MX_HELPER(mmin_d_mv_i, MIN, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MV_MX_HELPER(msll_d_mv_i, SLL, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MV_MX_HELPER(msrl_d_mv_i, SRL, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MV_MX_HELPER(mumax_d_mv_i, UMAX, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MV_MX_HELPER(mumin_d_mv_i, UMIN, get_elem_d, set_elem_d, 3, false, false)

GEN_OP_MV_MX_HELPER(mmulh_w_mv_i, MUL, get_elem_s, set_elem_s, 2, true, false)
GEN_OP_MV_MX_HELPER(mmulh_d_mv_i, MUL, get_elem_d, set_elem_d, 3, true, false)

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
                            bool keep_hi, bool scalar){
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t src1, result;
    uint64_t n_bit, mask;
    src1 = get_elem(ms1, 0, 0, env);
    for (i = 0; i < get_mrows(env); i++){
        for (k = 0; k < cols; k++){
            if(i < env->sizem && k < (env->sizek >> esz)){
                if (!scalar) {
                    src1 = get_elem(ms1, i, k, env);
                }
                switch(op){
                case ADD:
                    result = get_elem(ms2, i, k, env) + src1;
                    set_elem(md, i, k, env, result);
                    break;
                case SUB:
                    result = get_elem(ms2, i, k, env) - src1;
                    set_elem(md, i, k, env, result);
                    break;
                case MUL:
                    if (esz == 2) {
                        result = mul32(get_elem(ms2, i, k, env),
                                       src1, keep_hi);
                    } else {
                        result = mul64(get_elem(ms2, i, k, env),
                                       src1, keep_hi);
                    }
                    set_elem(md, i, k, env, result);
                    break;
                case SLL:
                case SRA:
                case SRL: {
                    if (esz == 2) {
                        n_bit = (uint64_t) (src1 & 0x1F);
                    } else {
                        n_bit = (uint64_t) (src1 & 0x3F);
                    }
                    result = get_elem(ms2, i, k, env);
                    if (op == SRA)
                        result = result >> n_bit;
                    else if (op == SRL) {
                        mask = get_unsigned_mask(esz);
                        result = (((uint64_t) result) & mask) >> n_bit;
                    } else
                        result = result << n_bit;
                    set_elem(md, i, k, env, result);
                    break;
                }
                case MAX:
                case MIN: {
                    int64_t oprd_a = src1;
                    int64_t oprd_b = get_elem(ms2, i, k, env);
                    result = (op == MAX ? oprd_a > oprd_b : oprd_a < oprd_b) ?
                            oprd_a : oprd_b;
                    set_elem(md, i, k, env, result);
                    break;
                }
                case UMAX:
                case UMIN: {
                    mask = get_unsigned_mask(esz);
                    uint64_t oprd_a = src1 & mask;
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

#define GEN_OP_MM_HELPER(insn, op, get_elem, set_elem, ESZ, keep_hi, scalar) \
void HELPER(insn)(void *md, void *ms1, void *ms2,                    \
                  CPURISCVState *env){                               \
    mmext_mm(md, ms1, ms2, env,                                      \
             get_elem, set_elem, op, ESZ, keep_hi, scalar);                  \
}

GEN_OP_MM_HELPER(madd_w_mm, ADD, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MM_HELPER(msub_w_mm, SUB, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MM_HELPER(msra_w_mm, SRA, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MM_HELPER(mmul_w_mm, MUL, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MM_HELPER(mmax_w_mm, MAX, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MM_HELPER(mmin_w_mm, MIN, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MM_HELPER(mumax_w_mm, UMAX, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MM_HELPER(mumin_w_mm, UMIN, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MM_HELPER(msll_w_mm, SLL, get_elem_s, set_elem_s, 2, false, false)
GEN_OP_MM_HELPER(msrl_w_mm, SRL, get_elem_s, set_elem_s, 2, false, false)

GEN_OP_MM_HELPER(madd_d_mm, ADD, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MM_HELPER(msub_d_mm, SUB, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MM_HELPER(msra_d_mm, SRA, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MM_HELPER(mmul_d_mm, MUL, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MM_HELPER(mmax_d_mm, MAX, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MM_HELPER(mmin_d_mm, MIN, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MM_HELPER(msll_d_mm, SLL, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MM_HELPER(msrl_d_mm, SRL, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MM_HELPER(mumax_d_mm, UMAX, get_elem_d, set_elem_d, 3, false, false)
GEN_OP_MM_HELPER(mumin_d_mm, UMIN, get_elem_d, set_elem_d, 3, false, false)

GEN_OP_MM_HELPER(mmulh_w_mm, MUL, get_elem_s, set_elem_s, 2, true, false)
GEN_OP_MM_HELPER(mmulh_d_mm, MUL, get_elem_d, set_elem_d, 3, true, false)

GEN_OP_MM_HELPER(madd_w_mx, ADD, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(msub_w_mx, SUB, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(msra_w_mx, SRA, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(mmul_w_mx, MUL, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(mmax_w_mx, MAX, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(mmin_w_mx, MIN, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(mumax_w_mx, UMAX, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(mumin_w_mx, UMIN, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(msll_w_mx, SLL, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(msrl_w_mx, SRL, get_elem_s, set_elem_s, 2, false, true)
GEN_OP_MM_HELPER(mmulh_w_mx, MUL, get_elem_s, set_elem_s, 2, true, true)

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
static inline int32_t macc_w_b_ss_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + a * b;
}

static inline int32_t macc_w_b_su_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + a * (uint8_t) b;
}

static inline int32_t macc_w_b_us_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + (uint8_t) a * b;
}

static inline int32_t macc_w_b_uu_s(int8_t a, int8_t b, int32_t sum)
{
    return sum + (uint8_t) a * (uint8_t) b;
}

typedef int32_t macc_fn_b(int8_t, int8_t, int32_t);

static int32_t sadd32_mx(CPURISCVState *env, int32_t a, int32_t b)
{
    int32_t res = a + b;
    if ((res ^ a) & (res ^ b) & INT32_MIN) {
        res = a > 0 ? INT32_MAX : INT32_MIN;
        env->mxsat = 0x1;
    }
    return res;
}

static int64_t sadd64_mx(CPURISCVState *env, int64_t a, int64_t b)
{
    int64_t res = a + b;
    if ((res ^ a) & (res ^ b) & INT64_MIN) {
        res = a > 0 ? INT64_MAX : INT64_MIN;
        env->mxsat = 0x1;
    }
    return res;
}

static void mmext_mmacc_w_b(void *md, void *ms1, void *ms2, CPURISCVState *env,
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
                psum = sadd32_mx(env, psum, temp);
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

#define GEN_MMACC_W_B_HELPER(insn, macc_fn_b)                   \
void HELPER(insn)(void *md, void *ms1, void *ms2,             \
                  CPURISCVState *env){                        \
    mmext_mmacc_w_b(md, ms1, ms2, env, macc_fn_b);              \
}

GEN_MMACC_W_B_HELPER(mmacc_w_b,   macc_w_b_ss_s)
GEN_MMACC_W_B_HELPER(mmaccu_w_b,  macc_w_b_uu_s)
GEN_MMACC_W_B_HELPER(mmaccus_w_b, macc_w_b_us_s)
GEN_MMACC_W_B_HELPER(mmaccsu_w_b, macc_w_b_su_s)

/* half byte oprands accumulate to single word */
static inline int32_t macc_w_p_ss_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (sextract32(a, start, length) * sextract32(b, start, length));
}

static inline int32_t macc_w_p_su_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (sextract32(a, start, length) * extract32(b, start, length));
}

static inline int32_t macc_w_p_us_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (extract32(a, start, length) * sextract32(b, start, length));
}

static inline int32_t macc_w_p_uu_s(int8_t a, int8_t b, int32_t sum,
                                  uint32_t start, uint32_t length){
    return sum + (int32_t) (extract32(a, start, length) * extract32(b, start, length));
}

typedef int32_t macc_fn_p(int8_t, int8_t, int32_t, uint32_t, uint32_t);

static void mmext_mmacc_w_p(void *md, void *ms1, void *ms2, CPURISCVState *env,
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
                psum = sadd32_mx(env, psum, temp);
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

#define GEN_MMACC_W_P_HELPER(insn, macc_fn_p)                   \
void HELPER(insn)(void *md, void *ms1, void *ms2,             \
                  CPURISCVState *env){                        \
    mmext_mmacc_w_p(md, ms1, ms2, env, macc_fn_p);              \
}

GEN_MMACC_W_P_HELPER(mmacc_w_p,   macc_w_p_ss_s)
GEN_MMACC_W_P_HELPER(mmaccu_w_p,  macc_w_p_uu_s)
GEN_MMACC_W_P_HELPER(mmaccus_w_p, macc_w_p_us_s)
GEN_MMACC_W_P_HELPER(mmaccsu_w_p, macc_w_p_su_s)

/* half word oprands accumulate to double words */
static inline int64_t macc_d_h_ss_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + a * b;
}

static inline int64_t macc_d_h_su_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + a * (uint16_t) b;
}

static inline int64_t macc_d_h_us_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + (uint16_t) a * b;
}

static inline int64_t macc_d_h_uu_d(int16_t a, int16_t b, int64_t sum)
{
    return sum + (uint64_t)(uint16_t) a * (uint64_t)(uint16_t) b;
}

typedef int64_t macc_fn_h(int16_t, int16_t, int64_t);

static void mmext_mmacc_d_h(void *md, void *ms1, void *ms2, CPURISCVState *env,
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
                    psum = sadd64_mx(env, psum, temp);
                    set_elem_d(md_pair_1, i, j, env, psum);
                } else {
                    set_elem_d(md_pair_1, i, j, env, 0);
                }
            }
        }
    }
}

#define GEN_MMACC_D_H_HELPER(insn, macc_fn_d_h)                   \
void HELPER(insn)(void *md, void *ms1, void *ms2,             \
                  CPURISCVState *env){                        \
    mmext_mmacc_d_h(md, ms1, ms2, env, macc_fn_d_h);              \
}

GEN_MMACC_D_H_HELPER(mmacc_d_h,   macc_d_h_ss_d)
GEN_MMACC_D_H_HELPER(mmaccu_d_h,  macc_d_h_uu_d)
GEN_MMACC_D_H_HELPER(mmaccus_d_h, macc_d_h_us_d)
GEN_MMACC_D_H_HELPER(mmaccsu_d_h, macc_d_h_su_d)

/* half byte x byte accumulate to single word */
static inline int32_t macc_i8xi4_w(int8_t a, int8_t b, int32_t sum)
{
    return sum + ((int32_t) a) * (sextract32(b, 0, 4));
}

static inline int32_t macc_i8xu4_w(int8_t a, int8_t b, int32_t sum)
{
    return sum + ((int32_t) a) * (extract32(b, 0, 4));
}

static inline int32_t macc_u8xi4_w(int8_t a, int8_t b, int32_t sum)
{
    return sum + ((int32_t) ((uint8_t) a)) * (sextract32(b, 0, 4));
}

static inline int32_t macc_u8xu4_w(int8_t a, int8_t b, int32_t sum)
{
    return sum + ((uint32_t) ((uint8_t) a)) * (extract32(b, 0, 4));
}

typedef int32_t macc_bp_w(int8_t a, int8_t b, int32_t sum);

/* mixed-precision byte x half-byte to int32 matrix multiplication */
static void mmext_mmacc_w_bp(void *md, void *ms1, void *ms2, target_ulong s1,
                           CPURISCVState *env, macc_bp_w *macc) {
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

#define GEN_MMACC_W_BP_HELPER(insn, macc_fn_bp)            \
void HELPER(insn)(void *md, void *ms1, void *ms2,        \
                  target_ulong s1, CPURISCVState *env) { \
    mmext_mmacc_w_bp(md, ms1, ms2, s1, env, macc_fn_bp);   \
}

GEN_MMACC_W_BP_HELPER(mmaccsu_w_bp, macc_i8xu4_w)
GEN_MMACC_W_BP_HELPER(mmaccu_w_bp,  macc_u8xu4_w)
GEN_MMACC_W_BP_HELPER(mmaccus_w_bp, macc_u8xi4_w)
GEN_MMACC_W_BP_HELPER(mmacc_w_bp,   macc_i8xi4_w)

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
FP_BINOP_WRAPPER_DEF(16, maximum_number)
FP_BINOP_WRAPPER_DEF(16, minimum_number)
FP_BINOP_WRAPPER_DEF(32, add)
FP_BINOP_WRAPPER_DEF(32, sub)
FP_BINOP_WRAPPER_DEF(32, mul)
FP_BINOP_WRAPPER_DEF(32, maximum_number)
FP_BINOP_WRAPPER_DEF(32, minimum_number)
FP_BINOP_WRAPPER_DEF(64, add)
FP_BINOP_WRAPPER_DEF(64, sub)
FP_BINOP_WRAPPER_DEF(64, mul)
FP_BINOP_WRAPPER_DEF(64, maximum_number)
FP_BINOP_WRAPPER_DEF(64, minimum_number)

#define BF16_BINOP_FN(op) bfloat16_##op##_wrapped
#define BF16_BINOP_WRAPPER_DEF(op)                                \
static inline uint64_t BF16_BINOP_FN(op)(uint64_t a, uint64_t b,  \
                                              float_status *status)    \
{                                                                      \
    return bfloat16_##op(a, b, status);                          \
}

BF16_BINOP_WRAPPER_DEF(add)
BF16_BINOP_WRAPPER_DEF(sub)
BF16_BINOP_WRAPPER_DEF(mul)
BF16_BINOP_WRAPPER_DEF(maximum_number)
BF16_BINOP_WRAPPER_DEF(minimum_number)

#define FP_TRIOP_FN(width, op) float##width##_##op##_wrapped
#define FP_TRIOP_WRAPPER_DEF(width, op)                                \
static inline uint64_t FP_TRIOP_FN(width, op)(uint64_t a, uint64_t b,  \
                                              uint64_t c,              \
                                              float_status *status)    \
{                                                                      \
    return float##width##_##op(a, b, c, 0, status);                    \
}
FP_TRIOP_WRAPPER_DEF(16, muladd)
FP_TRIOP_WRAPPER_DEF(32, muladd)
FP_TRIOP_WRAPPER_DEF(64, muladd)

#define BF16_TRIOP_FN(op) bfloat_##op##_wrapped
#define BF16_TRIOP_WRAPPER_DEF(op)                                \
static inline uint64_t BF16_TRIOP_FN(op)(uint64_t a, uint64_t b,  \
                                              uint64_t c,              \
                                              float_status *status)    \
{                                                                      \
    return bfloat16_##op(a, b, c, 0, status);                    \
}
BF16_TRIOP_WRAPPER_DEF(muladd)

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

static inline uint64_t f16_abs(uint64_t a, float_status *status)
{
    return float16_abs(a);
}

static inline uint64_t bf16_abs(uint64_t a, float_status *status)
{
    return bfloat16_abs(a);
}

static inline uint64_t f32_abs(uint64_t a, float_status *status)
{
    return float32_abs(a);
}

static inline uint64_t f64_abs(uint64_t a, float_status *status)
{
    return float64_abs(a);
}

FP_UNOP_WRAPPER_DEF(bfloat16_to_float32)
FP_UNOP_WRAPPER_DEF(float8e4_to_float16)
FP_UNOP_WRAPPER_DEF(float8e4_to_float32)
FP_UNOP_WRAPPER_DEF(float4e2_to_float32)
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
FP_UNOP_WRAPPER_DEF(float8e5_to_float4e2)
FP_UNOP_WRAPPER_DEF(float8e4_to_float4e2)
FP_UNOP_WRAPPER_DEF(float4e2_to_float8e5)
FP_UNOP_WRAPPER_DEF(float4e2_to_float8e4)
FP_UNOP_WRAPPER_DEF(int8_to_float8e5)
FP_UNOP_WRAPPER_DEF(int8_to_float8e4)
FP_UNOP_WRAPPER_DEF(uint8_to_float8e5)
FP_UNOP_WRAPPER_DEF(uint8_to_float8e4)
FP_UNOP_WRAPPER_DEF(f16_abs)
FP_UNOP_WRAPPER_DEF(bf16_abs)
FP_UNOP_WRAPPER_DEF(f32_abs)
FP_UNOP_WRAPPER_DEF(f64_abs)
FP_UNOP_WRAPPER_DEF(float16_floor)
FP_UNOP_WRAPPER_DEF(bfloat16_floor)
FP_UNOP_WRAPPER_DEF(float32_floor)
FP_UNOP_WRAPPER_DEF(float64_floor)
FP_UNOP_WRAPPER_DEF(float16_ceil)
FP_UNOP_WRAPPER_DEF(bfloat16_ceil)
FP_UNOP_WRAPPER_DEF(float32_ceil)
FP_UNOP_WRAPPER_DEF(float64_ceil)
FP_UNOP_WRAPPER_DEF(bfloat16_to_float4e2)
FP_UNOP_WRAPPER_DEF(bfloat16_to_float8e0)
FP_UNOP_WRAPPER_DEF(float32_to_float8e0)
FP_UNOP_WRAPPER_DEF(float8e0_to_bfloat16)
FP_UNOP_WRAPPER_DEF(bfloat16_to_float8e4)
FP_UNOP_WRAPPER_DEF(bfloat16_to_float8e5)
FP_UNOP_WRAPPER_DEF(float8e4_to_bfloat16)
FP_UNOP_WRAPPER_DEF(float8e5_to_bfloat16)
FP_UNOP_WRAPPER_DEF(bfloat16_to_int8)
FP_UNOP_WRAPPER_DEF(bfloat16_to_uint8)
FP_UNOP_WRAPPER_DEF(int8_to_bfloat16)
FP_UNOP_WRAPPER_DEF(uint8_to_bfloat16)

typedef uint64_t fp_unop(uint64_t, float_status *);
/* floating point matrix-matrix unary operations */
static inline void mmext_fp_m(void* md, void* ms1,
                              CPURISCVState* env, mmext_get_elem* get_elem,
                              mmext_set_elem* set_elem, fp_unop *fp_fn,
                              uint8_t esz) {
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint32_t rows = get_mrows(env);

    for (i = 0; i < rows; i++) {
        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek >> esz)) {
                int64_t oprd_a = get_elem(ms1, i, k, env);
                result = fp_fn(oprd_a, &env->mfp_status);
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_FP_M_HELPER(insn, get_elem, set_elem, ESZ, fp_fn)          \
void HELPER(insn)(void* md, void* ms1, CPURISCVState* env)             \
{                                                                      \
    mmext_fp_m(md, ms1, env, get_elem, set_elem, fp_fn, ESZ);          \
}

GEN_FP_M_HELPER(mfabs_h_mm, get_elem_h, set_elem_h, 1, FUNOP(f16_abs))
GEN_FP_M_HELPER(mfabs_bf16_mm, get_elem_h, set_elem_h, 1, FUNOP(bf16_abs))
GEN_FP_M_HELPER(mfabs_s_mm, get_elem_s, set_elem_s, 2, FUNOP(f32_abs))
GEN_FP_M_HELPER(mfabs_d_mm, get_elem_d, set_elem_d, 3, FUNOP(f64_abs))
GEN_FP_M_HELPER(mffloor_h_mm, get_elem_h, set_elem_h, 1, FUNOP(float16_floor))
GEN_FP_M_HELPER(mffloor_bf16_mm, get_elem_h, set_elem_h, 1, FUNOP(bfloat16_floor))
GEN_FP_M_HELPER(mffloor_s_mm, get_elem_s, set_elem_s, 2, FUNOP(float32_floor))
GEN_FP_M_HELPER(mffloor_d_mm, get_elem_d, set_elem_d, 3, FUNOP(float64_floor))
GEN_FP_M_HELPER(mfceil_h_mm, get_elem_h, set_elem_h, 1, FUNOP(float16_ceil))
GEN_FP_M_HELPER(mfceil_bf16_mm, get_elem_h, set_elem_h, 1, FUNOP(bfloat16_ceil))
GEN_FP_M_HELPER(mfceil_s_mm, get_elem_s, set_elem_s, 2, FUNOP(float32_ceil))
GEN_FP_M_HELPER(mfceil_d_mm, get_elem_d, set_elem_d, 3, FUNOP(float64_ceil))

static uint64_t do_tanh_s(uint64_t src1, float_status *s)
{
    float32 f = (float32)src1, tmp = 0;
    bool sign = float32_is_neg(f);
    if (float32_is_infinity(f)) {
        tmp = float32_set_sign(float32_one, sign);
    } else if (float32_is_zero(f)) {
        tmp = float32_set_sign(float32_zero, sign);
    } else if (float32_is_quiet_nan(f, s)) {
        tmp = float32_default_nan(s);
    } else if (float32_is_signaling_nan(f, s)) {
        s->float_exception_flags |= float_flag_invalid;
        tmp = float32_default_nan(s);
    } else {
        sfu_output a = sfu_tanh(f);
        sfu_set_flags(s, &a);
        tmp = sfu_to_f32(&a);
    }
    return tmp;
}

static uint64_t do_tanh_bf16(uint64_t src1, float_status *s)
{
    return do_tanh_s(src1 << 16, s) >> 16;
}

static uint64_t do_exp2_s(uint64_t src1, float_status *s)
{
    float32 f = (float32)src1, tmp = 0;
    bool sign = float32_is_neg(f);
    if (float32_is_infinity(f)) {
        if (sign) {
            tmp = float32_zero;
        } else {
            tmp = float32_infinity;
        }
    } else if (float32_is_zero(f)) {
        tmp = float32_one;
    } else if (float32_is_quiet_nan(f, s)) {
        tmp = float32_default_nan(s);
    } else if (float32_is_signaling_nan(f, s)) {
        s->float_exception_flags |= float_flag_invalid;
        tmp = float32_default_nan(s);
    } else {
        sfu_output a = sfu_exp2(f);
        sfu_set_flags(s, &a);
        tmp = sfu_to_f32(&a);
    }
    return tmp;
}

static uint64_t do_exp2_bf16(uint64_t src1, float_status *s)
{
    return do_exp2_s(src1 << 16, s) >> 16;
}

static uint64_t do_rec_s(uint64_t src1, float_status *s)
{
    float32 f = (float32)src1, tmp = 0;
    bool sign = float32_is_neg(f);
    if (float32_is_infinity(f)) {
        tmp = float32_set_sign(float32_zero, sign);
    } else if (float32_is_zero(f)) {
        tmp = float32_set_sign(float32_infinity, sign);
        s->float_exception_flags |= float_flag_divbyzero;
    } else if (float32_is_quiet_nan(f, s)) {
        tmp = float32_default_nan(s);
    } else if (float32_is_signaling_nan(f, s)) {
        s->float_exception_flags |= float_flag_invalid;
        tmp = float32_default_nan(s);
    } else {
        sfu_output a = sfu_rcp(f);
        sfu_set_flags(s, &a);
        tmp = sfu_to_f32(&a);
    }
    return tmp;
}

static uint64_t do_rec_bf16(uint64_t src1, float_status *s)
{
    return do_rec_s(src1 << 16, s) >> 16;
}

static uint64_t do_sig_s(uint64_t src1, float_status *s)
{
    float32 f = (float32)src1, tmp = 0;
    bool sign = float32_is_neg(f);
    if (float32_is_infinity(f)) {
        tmp = sign ? float32_zero : float32_one;
    } else if (float32_is_zero(f)) {
        tmp = float32_half;
    } else if (float32_is_quiet_nan(f, s)) {
        tmp = float32_default_nan(s);
    } else if (float32_is_signaling_nan(f, s)) {
        s->float_exception_flags |= float_flag_invalid;
        tmp = float32_default_nan(s);
    } else {
        sfu_output a = sfu_sigmoid(f);
        sfu_set_flags(s, &a);
        tmp = sfu_to_f32(&a);
    }
    return tmp;
}

static uint64_t do_sig_bf16(uint64_t src1, float_status *s)
{
    return do_sig_s(src1 << 16, s) >> 16;
}

static uint64_t do_sin_s(uint64_t src1, float_status *s)
{
    float32 f = (float32)src1, tmp = 0;
    bool sign = float32_is_neg(f);
    if (float32_is_infinity(f)) {
        tmp = float32_default_nan(s);
    } else if (float32_is_zero(f)) {
        tmp = float32_set_sign(float32_zero, sign);
    } else if (float32_is_any_nan(f)) {
        tmp = float32_default_nan(s);
        if (float32_is_signaling_nan(f, s)) {
            s->float_exception_flags |= float_flag_invalid;
        }
    } else {
        sfu_output a = sfu_sin(f);
        sfu_set_flags(s, &a);
        tmp = sfu_to_f32(&a);
    }
    return tmp;
}

static uint64_t do_sin_bf16(uint64_t src1, float_status *s)
{
    return do_sin_s(src1 << 16, s) >> 16;
}

static uint64_t do_cos_s(uint64_t src1, float_status *s)
{
    float32 f = (float32)src1, tmp = 0;
    if (float32_is_infinity(f)) {
        tmp = float32_default_nan(s);
    } else if (float32_is_zero(f)) {
        tmp = float32_one;
    } else if (float32_is_any_nan(f)) {
        tmp = float32_default_nan(s);
        if (float32_is_signaling_nan(f, s)) {
            s->float_exception_flags |= float_flag_invalid;
        }
    } else {
        sfu_output a = sfu_cos(f);
        sfu_set_flags(s, &a);
        tmp = sfu_to_f32(&a);
    }
    return tmp;
}

static uint64_t do_cos_bf16(uint64_t src1, float_status *s)
{
    return do_cos_s(src1 << 16, s) >> 16;
}

static uint64_t do_log2_s(uint64_t src1, float_status *s)
{
    float32 f = (float32)src1, tmp = 0;
    bool sign = float32_is_neg(f);
    if (sign || float32_is_zero(f)) { /* -inf, -inf < x < 0, -0, +0 */
        tmp = float32_default_nan(s);
        s->float_exception_flags |= float_flag_invalid;
    } else if (float32_is_infinity(f)) {
        tmp = float32_infinity;
    } else if (float32_is_any_nan(f)) {
        tmp = float32_default_nan(s);
        if (float32_is_signaling_nan(f, s)) {
            s->float_exception_flags |= float_flag_invalid;
        }
    } else {
        sfu_output a = sfu_log2(f);
        sfu_set_flags(s, &a);
        tmp = sfu_to_f32(&a);
    }
    return tmp;
}

static uint64_t do_log2_bf16(uint64_t src1, float_status *s)
{
    return do_log2_s(src1 << 16, s) >> 16;
}

static uint64_t do_sqrt_s(uint64_t src1, float_status *s)
{
    float32 f = (float32)src1, tmp = 0;
    bool sign = float32_is_neg(f);
    if (float32_is_zero(f)) {
        tmp = f;
    } else if (sign) {
        tmp = float32_default_nan(s);
        s->float_exception_flags |= float_flag_invalid;
    } else if (float32_is_infinity(f)) {
        tmp = float32_infinity;
    } else if (float32_is_any_nan(f)) {
        tmp = float32_default_nan(s);
        if (float32_is_signaling_nan(f, s)) {
            s->float_exception_flags |= float_flag_invalid;
        }
    } else {
        sfu_output a = sfu_sqrt(f);
        sfu_set_flags(s, &a);
        tmp = sfu_to_f32(&a);
    }
    return tmp;
}

static uint64_t do_sqrt_bf16(uint64_t src1, float_status *s)
{
    return do_sqrt_s(src1 << 16, s) >> 16;
}

GEN_FP_M_HELPER(mfexp2_s     , get_elem_s, set_elem_s, 2, do_exp2_s)
GEN_FP_M_HELPER(mfrec_s      , get_elem_s, set_elem_s, 2, do_rec_s)
GEN_FP_M_HELPER(mfsig_s      , get_elem_s, set_elem_s, 2, do_sig_s)
GEN_FP_M_HELPER(mftanh_s     , get_elem_s, set_elem_s, 2, do_tanh_s)
GEN_FP_M_HELPER(mfsin_s      , get_elem_s, set_elem_s, 2, do_sin_s)
GEN_FP_M_HELPER(mfcos_s      , get_elem_s, set_elem_s, 2, do_cos_s)
GEN_FP_M_HELPER(mfsqrt_s     , get_elem_s, set_elem_s, 2, do_sqrt_s)
GEN_FP_M_HELPER(mflog2_s     , get_elem_s, set_elem_s, 2, do_log2_s)
GEN_FP_M_HELPER(mfexp2_bf16  , get_elem_h, set_elem_h, 1, do_exp2_bf16)
GEN_FP_M_HELPER(mfrec_bf16   , get_elem_h, set_elem_h, 1, do_rec_bf16)
GEN_FP_M_HELPER(mfsig_bf16   , get_elem_h, set_elem_h, 1, do_sig_bf16)
GEN_FP_M_HELPER(mftanh_bf16  , get_elem_h, set_elem_h, 1, do_tanh_bf16)
GEN_FP_M_HELPER(mfsin_bf16   , get_elem_h, set_elem_h, 1, do_sin_bf16)
GEN_FP_M_HELPER(mfcos_bf16   , get_elem_h, set_elem_h, 1, do_cos_bf16)
GEN_FP_M_HELPER(mfsqrt_bf16  , get_elem_h, set_elem_h, 1, do_sqrt_bf16)
GEN_FP_M_HELPER(mflog2_bf16  , get_elem_h, set_elem_h, 1, do_log2_bf16)

typedef uint64_t fp_binop(uint64_t, uint64_t, float_status *);

/* floating point matrix-matrix binary operations */
static inline void mmext_fp_mm(void* md, void* ms1, void* ms2,
                               CPURISCVState* env, mmext_get_elem* get_elem,
                               mmext_set_elem* set_elem, fp_binop *fp_fn,
                               uint8_t esz, bool scalar) {
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t src1, result;
    uint32_t rows = get_mrows(env);

    if (scalar) {
        src1 = get_elem(ms1, 0, 0, env);
    }
    for (i = 0; i < rows; i++) {
        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek >> esz)) {
                int64_t oprd_a = get_elem(ms2, i, k, env);
                int64_t oprd_b = scalar ? src1 : get_elem(ms1, i, k, env);
                result = fp_fn(oprd_a, oprd_b, &env->mfp_status);
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_FP_MM_HELPER(insn, get_elem, set_elem, ESZ, fp_fn, scalar)      \
void HELPER(insn)(void* md, void* ms1, void* ms2, CPURISCVState* env)   \
{                                                                       \
    mmext_fp_mm(md, ms1, ms2, env, get_elem, set_elem, fp_fn, ESZ, scalar);     \
}

GEN_FP_MM_HELPER(mfadd_h_mm, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, add), false)
GEN_FP_MM_HELPER(mfadd_s_mm, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, add), false)
GEN_FP_MM_HELPER(mfadd_d_mm, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, add), false)
GEN_FP_MM_HELPER(mfmax_h_mm, get_elem_h, set_elem_h, 1,
                    FP_BINOP_FN(16, maximum_number), false)
GEN_FP_MM_HELPER(mfmax_s_mm, get_elem_s, set_elem_s, 2,
                    FP_BINOP_FN(32, maximum_number), false)
GEN_FP_MM_HELPER(mfmax_d_mm, get_elem_d, set_elem_d, 3,
                    FP_BINOP_FN(64, maximum_number), false)
GEN_FP_MM_HELPER(mfmin_h_mm, get_elem_h, set_elem_h, 1,
                    FP_BINOP_FN(16, minimum_number), false)
GEN_FP_MM_HELPER(mfmin_s_mm, get_elem_s, set_elem_s, 2,
                    FP_BINOP_FN(32, minimum_number), false)
GEN_FP_MM_HELPER(mfmin_d_mm, get_elem_d, set_elem_d, 3,
                    FP_BINOP_FN(64, minimum_number), false)
GEN_FP_MM_HELPER(mfmul_h_mm, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, mul), false)
GEN_FP_MM_HELPER(mfmul_s_mm, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, mul), false)
GEN_FP_MM_HELPER(mfmul_d_mm, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, mul), false)
GEN_FP_MM_HELPER(mfsub_h_mm, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, sub), false)
GEN_FP_MM_HELPER(mfsub_s_mm, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, sub), false)
GEN_FP_MM_HELPER(mfsub_d_mm, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, sub), false)

GEN_FP_MM_HELPER(mfadd_bf16_mm, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(add), false)
GEN_FP_MM_HELPER(mfsub_bf16_mm, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(sub), false)
GEN_FP_MM_HELPER(mfmul_bf16_mm, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(mul), false)
GEN_FP_MM_HELPER(mfmax_bf16_mm, get_elem_h, set_elem_h, 1,
                    BF16_BINOP_FN(maximum_number), false)
GEN_FP_MM_HELPER(mfmin_bf16_mm, get_elem_h, set_elem_h, 1,
                    BF16_BINOP_FN(minimum_number), false)

GEN_FP_MM_HELPER(mfadd_h_mf, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, add), true)
GEN_FP_MM_HELPER(mfadd_s_mf, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, add), true)
GEN_FP_MM_HELPER(mfadd_d_mf, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, add), true)
GEN_FP_MM_HELPER(mfmax_h_mf, get_elem_h, set_elem_h, 1, 
                    FP_BINOP_FN(16, maximum_number), true)
GEN_FP_MM_HELPER(mfmax_s_mf, get_elem_s, set_elem_s, 2, 
                    FP_BINOP_FN(32, maximum_number), true)
GEN_FP_MM_HELPER(mfmax_d_mf, get_elem_d, set_elem_d, 3, 
                    FP_BINOP_FN(64, maximum_number), true)
GEN_FP_MM_HELPER(mfmin_h_mf, get_elem_h, set_elem_h, 1, 
                    FP_BINOP_FN(16, minimum_number), true)
GEN_FP_MM_HELPER(mfmin_s_mf, get_elem_s, set_elem_s, 2, 
                    FP_BINOP_FN(32, minimum_number), true)
GEN_FP_MM_HELPER(mfmin_d_mf, get_elem_d, set_elem_d, 3, 
                    FP_BINOP_FN(64, minimum_number), true)
GEN_FP_MM_HELPER(mfmul_h_mf, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, mul), true)
GEN_FP_MM_HELPER(mfmul_s_mf, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, mul), true)
GEN_FP_MM_HELPER(mfmul_d_mf, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, mul), true)
GEN_FP_MM_HELPER(mfsub_h_mf, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, sub), true)
GEN_FP_MM_HELPER(mfsub_s_mf, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, sub), true)
GEN_FP_MM_HELPER(mfsub_d_mf, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, sub), true)

GEN_FP_MM_HELPER(mfadd_bf16_mf, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(add), true)
GEN_FP_MM_HELPER(mfsub_bf16_mf, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(sub), true)
GEN_FP_MM_HELPER(mfmul_bf16_mf, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(mul), true)
GEN_FP_MM_HELPER(mfmax_bf16_mf, get_elem_h, set_elem_h, 1,
                    BF16_BINOP_FN(maximum_number), true)
GEN_FP_MM_HELPER(mfmin_bf16_mf, get_elem_h, set_elem_h, 1,
                    BF16_BINOP_FN(minimum_number), true)

/* floating point matrix-matrix fused operations */
typedef uint64_t fp_triop(uint64_t, uint64_t, uint64_t, float_status *);

static inline void mmext_fp_mm_fused(void* md, void* ms1, void* ms2,
                                     CPURISCVState* env,
                                     mmext_get_elem* get_elem,
                                     mmext_set_elem* set_elem, fp_triop *fp_fn,
                                     uint8_t esz, bool scalar) {
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t src1, result;
    uint32_t rows = get_mrows(env);
    if (scalar) {
        src1 = get_elem(ms1, 0, 0, env);
    }

    for (i = 0; i < rows; i++) {
        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek >> esz)) {
                int64_t oprd_a = get_elem(ms2, i, k, env);
                int64_t oprd_b = scalar ? src1 : get_elem(ms1, i, k, env);
                int64_t oprd_c = get_elem(md, i, k, env);
                result = fp_fn(oprd_a, oprd_b, oprd_c, &env->mfp_status);
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_FP_MM_FUSED_HELPER(insn, get_elem, set_elem, ESZ, fp_fn, scalar)    \
void HELPER(insn)(void* md, void* ms1, void* ms2, CPURISCVState* env)   \
{                                                                       \
    mmext_fp_mm_fused(md, ms1, ms2, env, get_elem, set_elem, fp_fn, ESZ, scalar);  \
}

GEN_FP_MM_FUSED_HELPER(mfma_h_mm, get_elem_h, set_elem_h, 1, FP_TRIOP_FN(16, muladd), false)
GEN_FP_MM_FUSED_HELPER(mfma_s_mm, get_elem_s, set_elem_s, 2, FP_TRIOP_FN(32, muladd), false)
GEN_FP_MM_FUSED_HELPER(mfma_d_mm, get_elem_d, set_elem_d, 3, FP_TRIOP_FN(64, muladd), false)
GEN_FP_MM_FUSED_HELPER(mfma_bf16_mm, get_elem_h, set_elem_h, 1, BF16_TRIOP_FN(muladd), false)
GEN_FP_MM_FUSED_HELPER(mfma_h_mf, get_elem_h, set_elem_h, 1, FP_TRIOP_FN(16, muladd), true)
GEN_FP_MM_FUSED_HELPER(mfma_s_mf, get_elem_s, set_elem_s, 2, FP_TRIOP_FN(32, muladd), true)
GEN_FP_MM_FUSED_HELPER(mfma_d_mf, get_elem_d, set_elem_d, 3, FP_TRIOP_FN(64, muladd), true)
GEN_FP_MM_FUSED_HELPER(mfma_bf16_mf, get_elem_h, set_elem_h, 1, BF16_TRIOP_FN(muladd), true)

/* floating point matrix-vector(immediate-indexed) binary operations */
static inline void mmext_fp_mv(void* md, void* ms1, void* ms2, target_ulong s1,
                               CPURISCVState* env, mmext_get_elem* get_elem,
                               mmext_set_elem* set_elem, fp_binop *fp_fn,
                               uint8_t esz, bool col) {
    uint32_t i, k, idx;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint32_t rows = get_mrows(env);
    for (idx = 0; idx < rows; idx++) {
        int64_t oprd_b;
        i = idx;
        if (ms1 == md) {
            if (idx == s1) {
                i = rows - 1;
            } else if (idx == rows - 1) {
                i = s1;
            }
        }
        oprd_b = get_elem(ms1, i, s1, env);
        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek >> esz)) {
                int64_t oprd_a = get_elem(ms2, i, k, env);
                if (!col) {
                    oprd_b = get_elem(ms1, s1, k, env);
                }
                result = fp_fn(oprd_a, oprd_b, &env->mfp_status);
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_FP_MV_HELPER(insn, get_elem, set_elem, ESZ, fp_fn, col)     \
void HELPER(insn)(void* md, void* ms1, void* ms2, target_ulong s1,      \
                  CPURISCVState* env)                                   \
{                                                                       \
    mmext_fp_mv(md, ms1, ms2, s1, env, get_elem, set_elem, fp_fn, ESZ, col); \
}

GEN_FP_MV_HELPER(mfadd_h_mv_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, add), false)
GEN_FP_MV_HELPER(mfadd_s_mv_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, add), false)
GEN_FP_MV_HELPER(mfadd_d_mv_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, add), false)
GEN_FP_MV_HELPER(mfmax_h_mv_i, get_elem_h, set_elem_h, 1,
                    FP_BINOP_FN(16, maximum_number), false)
GEN_FP_MV_HELPER(mfmax_s_mv_i, get_elem_s, set_elem_s, 2,
                    FP_BINOP_FN(32, maximum_number), false)
GEN_FP_MV_HELPER(mfmax_d_mv_i, get_elem_d, set_elem_d, 3,
                    FP_BINOP_FN(64, maximum_number), false)
GEN_FP_MV_HELPER(mfmin_h_mv_i, get_elem_h, set_elem_h, 1,
                    FP_BINOP_FN(16, minimum_number), false)
GEN_FP_MV_HELPER(mfmin_s_mv_i, get_elem_s, set_elem_s, 2,
                    FP_BINOP_FN(32, minimum_number), false)
GEN_FP_MV_HELPER(mfmin_d_mv_i, get_elem_d, set_elem_d, 3,
                    FP_BINOP_FN(64, minimum_number), false)
GEN_FP_MV_HELPER(mfmul_h_mv_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, mul), false)
GEN_FP_MV_HELPER(mfmul_s_mv_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, mul), false)
GEN_FP_MV_HELPER(mfmul_d_mv_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, mul), false)
GEN_FP_MV_HELPER(mfsub_h_mv_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, sub), false)
GEN_FP_MV_HELPER(mfsub_s_mv_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, sub), false)
GEN_FP_MV_HELPER(mfsub_d_mv_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, sub), false)

GEN_FP_MV_HELPER(mfadd_bf16_mv_i, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(add), false)
GEN_FP_MV_HELPER(mfmax_bf16_mv_i, get_elem_h, set_elem_h, 1,
                    BF16_BINOP_FN(maximum_number), false)
GEN_FP_MV_HELPER(mfmin_bf16_mv_i, get_elem_h, set_elem_h, 1,
                    BF16_BINOP_FN(minimum_number), false)
GEN_FP_MV_HELPER(mfmul_bf16_mv_i, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(mul), false)
GEN_FP_MV_HELPER(mfsub_bf16_mv_i, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(sub), false)

GEN_FP_MV_HELPER(mfadd_h_mc_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, add), true)
GEN_FP_MV_HELPER(mfadd_s_mc_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, add), true)
GEN_FP_MV_HELPER(mfadd_d_mc_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, add), true)
GEN_FP_MV_HELPER(mfmax_h_mc_i, get_elem_h, set_elem_h, 1,
                    FP_BINOP_FN(16, maximum_number), true)
GEN_FP_MV_HELPER(mfmax_s_mc_i, get_elem_s, set_elem_s, 2,
                    FP_BINOP_FN(32, maximum_number), true)
GEN_FP_MV_HELPER(mfmax_d_mc_i, get_elem_d, set_elem_d, 3,
                    FP_BINOP_FN(64, maximum_number), true)
GEN_FP_MV_HELPER(mfmin_h_mc_i, get_elem_h, set_elem_h, 1,
                    FP_BINOP_FN(16, minimum_number), true)
GEN_FP_MV_HELPER(mfmin_s_mc_i, get_elem_s, set_elem_s, 2,
                    FP_BINOP_FN(32, minimum_number), true)
GEN_FP_MV_HELPER(mfmin_d_mc_i, get_elem_d, set_elem_d, 3,
                    FP_BINOP_FN(64, minimum_number), true)
GEN_FP_MV_HELPER(mfmul_h_mc_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, mul), true)
GEN_FP_MV_HELPER(mfmul_s_mc_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, mul), true)
GEN_FP_MV_HELPER(mfmul_d_mc_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, mul), true)
GEN_FP_MV_HELPER(mfsub_h_mc_i, get_elem_h, set_elem_h, 1, FP_BINOP_FN(16, sub), true)
GEN_FP_MV_HELPER(mfsub_s_mc_i, get_elem_s, set_elem_s, 2, FP_BINOP_FN(32, sub), true)
GEN_FP_MV_HELPER(mfsub_d_mc_i, get_elem_d, set_elem_d, 3, FP_BINOP_FN(64, sub), true)

GEN_FP_MV_HELPER(mfadd_bf16_mc_i, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(add), true)
GEN_FP_MV_HELPER(mfmax_bf16_mc_i, get_elem_h, set_elem_h, 1,
                    BF16_BINOP_FN(maximum_number), true)
GEN_FP_MV_HELPER(mfmin_bf16_mc_i, get_elem_h, set_elem_h, 1,
                    BF16_BINOP_FN(minimum_number), true)
GEN_FP_MV_HELPER(mfmul_bf16_mc_i, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(mul), true)
GEN_FP_MV_HELPER(mfsub_bf16_mc_i, get_elem_h, set_elem_h, 1, BF16_BINOP_FN(sub), true)
/* floating point matrix-vector(immediate-indexed) fused operations */
static inline void mmext_fp_mv_fused(void* md, void* ms1, void* ms2,
                                     target_ulong s1, CPURISCVState* env,
                                     mmext_get_elem* get_elem,
                                     mmext_set_elem* set_elem, fp_triop *fp_fn,
                                     uint8_t esz, bool col) {
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
            int64_t oprd_b = get_elem(ms1, i, s1, env);
            if (i < env->sizem && k < (env->sizek >> esz)) {
                int64_t oprd_a = get_elem(ms2, i, k, env);
                int64_t oprd_c = get_elem(md, i, k, env);
                if (!col) {
                    oprd_b = get_elem(ms1, s1, k, env);
                }
                result = fp_fn(oprd_a, oprd_b, oprd_c, &env->mfp_status);
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_FP_MV_FUSED_HELPER(insn, get_elem, set_elem, ESZ, fp_fn, col)    \
void HELPER(insn)(void* md, void* ms1, void* ms2, target_ulong s1,      \
                  CPURISCVState* env)                                   \
{                                                                       \
    mmext_fp_mv_fused(md, ms1, ms2, s1, env, get_elem, set_elem, fp_fn, ESZ, col); \
}

GEN_FP_MV_FUSED_HELPER(mfma_h_mv_i, get_elem_h, set_elem_h, 1, FP_TRIOP_FN(16, muladd), false)
GEN_FP_MV_FUSED_HELPER(mfma_s_mv_i, get_elem_s, set_elem_s, 2, FP_TRIOP_FN(32, muladd), false)
GEN_FP_MV_FUSED_HELPER(mfma_d_mv_i, get_elem_d, set_elem_d, 3, FP_TRIOP_FN(64, muladd), false)
GEN_FP_MV_FUSED_HELPER(mfma_h_mc_i, get_elem_h, set_elem_h, 1, FP_TRIOP_FN(16, muladd), true)
GEN_FP_MV_FUSED_HELPER(mfma_s_mc_i, get_elem_s, set_elem_s, 2, FP_TRIOP_FN(32, muladd), true)
GEN_FP_MV_FUSED_HELPER(mfma_d_mc_i, get_elem_d, set_elem_d, 3, FP_TRIOP_FN(64, muladd), true)

GEN_FP_MV_FUSED_HELPER(mfma_bf16_mv_i, get_elem_h, set_elem_h, 1, BF16_TRIOP_FN(muladd), false)
GEN_FP_MV_FUSED_HELPER(mfma_bf16_mc_i, get_elem_h, set_elem_h, 1, BF16_TRIOP_FN(muladd), true)

/* floating point type conversion operations */

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
    const uint32_t mlenb = get_mlenb(env);
    void *tmp = g_malloc0(mlenb);
    memcpy(tmp, md, mlenb);
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
            set_elem(tmp, i, k + dst_col_offset, env, result);
        }
    }
    memcpy(md, tmp, mlenb);
    g_free(tmp);
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
GEN_FP_CVT_HELPER(mfcvth_e5_s,   s, b, 1, FUNOP(float32_to_float8e5), 1, 0)
GEN_FP_CVT_HELPER(mfcvth_h_e4,   b, h, 1, FUNOP(float8e4_to_float16), 1, 1)
GEN_FP_CVT_HELPER(mfcvth_h_e5,   b, h, 1, FUNOP(float8e5_to_float16), 1, 1)
GEN_FP_CVT_HELPER(mfcvth_h_s,    s, h, 2, FUNOP(f32_to_f16_ieee),     1, 0)
GEN_FP_CVT_HELPER(mfcvth_s_bf16, h, s, 2, FUNOP(bfloat16_to_float32), 1, 1)
GEN_FP_CVT_HELPER(mfcvth_s_h,    h, s, 2, FUNOP(f16_to_f32_ieee),     1, 1)
GEN_FP_CVT_HELPER(mfcvtl_bf16_s, s, h, 2, FUNOP(float32_to_bfloat16), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e4_h,   h, b, 1, FUNOP(float16_to_float8e4), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e4_s,   s, b, 2, FUNOP(float32_to_float8e4), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e5_h,   h, b, 1, FUNOP(float16_to_float8e5), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e5_s,   s, b, 1, FUNOP(float32_to_float8e5), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_h_e4,   b, h, 1, FUNOP(float8e4_to_float16), 0, 1)
GEN_FP_CVT_HELPER(mfcvtl_h_e5,   b, h, 1, FUNOP(float8e5_to_float16), 0, 1)
GEN_FP_CVT_HELPER(mfcvtl_h_s,    s, h, 2, FUNOP(f32_to_f16_ieee),     0, 0)
GEN_FP_CVT_HELPER(mfcvtl_s_bf16, h, s, 2, FUNOP(bfloat16_to_float32), 0, 1)
GEN_FP_CVT_HELPER(mfcvtl_s_h,    h, s, 2, FUNOP(f16_to_f32_ieee),     0, 1)
GEN_FP_CVT_HELPER(mfcvth_d_s,    s, d, 3, FUNOP(float32_to_float64),  1, 1)
GEN_FP_CVT_HELPER(mfcvth_s_d,    d, s, 3, FUNOP(float64_to_float32),  1, 0)
GEN_FP_CVT_HELPER(mfcvtl_d_s,    s, d, 3, FUNOP(float32_to_float64),  0, 1)
GEN_FP_CVT_HELPER(mfcvtl_s_d,    d, s, 3, FUNOP(float64_to_float32),  0, 0)

/* v0.5 fp8 <-> bf16 conversion */
GEN_FP_CVT_HELPER(mfcvth_e4_bf16,   h, b, 1, FUNOP(bfloat16_to_float8e4), 1, 0)
GEN_FP_CVT_HELPER(mfcvth_e5_bf16,   h, b, 1, FUNOP(bfloat16_to_float8e5), 1, 0)
GEN_FP_CVT_HELPER(mfcvth_bf16_e4,   b, h, 1, FUNOP(float8e4_to_bfloat16), 1, 1)
GEN_FP_CVT_HELPER(mfcvth_bf16_e5,   b, h, 1, FUNOP(float8e5_to_bfloat16), 1, 1)
GEN_FP_CVT_HELPER(mfcvtl_e4_bf16,   h, b, 1, FUNOP(bfloat16_to_float8e4), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e5_bf16,   h, b, 1, FUNOP(bfloat16_to_float8e5), 0, 0)
GEN_FP_CVT_HELPER(mfcvtl_bf16_e4,   b, h, 1, FUNOP(float8e4_to_bfloat16), 0, 1)
GEN_FP_CVT_HELPER(mfcvtl_bf16_e5,   b, h, 1, FUNOP(float8e5_to_bfloat16), 0, 1)

/* v0.5 fp4 conversion */
GEN_FP_CVT_HELPER(mfcvth_e2m1_e5m2, b, p, 0, FUNOP(float8e5_to_float4e2),  1, 0)
GEN_FP_CVT_HELPER(mfcvth_e2m1_e4m3, b, p, 0, FUNOP(float8e4_to_float4e2),  1, 0)
GEN_FP_CVT_HELPER(mfcvtl_e2m1_e5m2, b, p, 0, FUNOP(float8e5_to_float4e2),  0, 0)
GEN_FP_CVT_HELPER(mfcvtl_e2m1_e4m3, b, p, 0, FUNOP(float8e4_to_float4e2),  0, 0)
GEN_FP_CVT_HELPER(mfcvth_e5m2_e2m1, p, b, 0, FUNOP(float4e2_to_float8e5),  1, 1)
GEN_FP_CVT_HELPER(mfcvth_e4m3_e2m1, p, b, 0, FUNOP(float4e2_to_float8e4),  1, 1)
GEN_FP_CVT_HELPER(mfcvtl_e5m2_e2m1, p, b, 0, FUNOP(float4e2_to_float8e5),  0, 1)
GEN_FP_CVT_HELPER(mfcvtl_e4m3_e2m1, p, b, 0, FUNOP(float4e2_to_float8e4),  0, 1)

GEN_FP_CVT_HELPER(mfcvth_e2m1_bf16, h, p, 1, FUNOP(bfloat16_to_float4e2),  1, 0)
GEN_FP_CVT_HELPER(mfcvtl_e2m1_bf16, h, p, 1, FUNOP(bfloat16_to_float4e2),  0, 0)

/* v0.5 e8m0 conversion */
GEN_FP_CVT_HELPER(mfcvth_e8m0_bf16, h, b, 1, FUNOP(bfloat16_to_float8e0),  1, 0)
GEN_FP_CVT_HELPER(mfcvtl_e8m0_bf16, h, b, 1, FUNOP(bfloat16_to_float8e0),  0, 0)
GEN_FP_CVT_HELPER(mfcvth_bf16_e8m0, b, h, 1, FUNOP(float8e0_to_bfloat16),  1, 1)
GEN_FP_CVT_HELPER(mfcvtl_bf16_e8m0, b, h, 1, FUNOP(float8e0_to_bfloat16),  0, 1)
GEN_FP_CVT_HELPER(mfcvth_e8m0_s,    s, b, 2, FUNOP(float32_to_float8e0),  1, 0)
GEN_FP_CVT_HELPER(mfcvtl_e8m0_s,    s, b, 2, FUNOP(float32_to_float8e0),  0, 0)

/* floating-point v.s. integer conversion */
GEN_FP_CVT_HELPER(mfscvt_w_s,  s, s, 2, FUNOP(float32_to_int32),  0, 0)
GEN_FP_CVT_HELPER(mfscvth_b_h, h, b, 1, FUNOP(float16_to_int8),   1, 0)
GEN_FP_CVT_HELPER(mfscvtl_b_h, h, b, 1, FUNOP(float16_to_int8),   0, 0)
GEN_FP_CVT_HELPER(mfucvt_w_s,  s, s, 2, FUNOP(float32_to_uint32), 0, 0)
GEN_FP_CVT_HELPER(mfucvth_b_h, h, b, 1, FUNOP(float16_to_int8),   1, 0)
GEN_FP_CVT_HELPER(mfucvtl_b_h, h, b, 1, FUNOP(float16_to_uint8),  0, 0)
GEN_FP_CVT_HELPER(msfcvt_s_w,  s, s, 2, FUNOP(int32_to_float32),  0, 0)
GEN_FP_CVT_HELPER(msfcvth_h_b, b, h, 1, FUNOP(int8_to_float16),   1, 1)
GEN_FP_CVT_HELPER(msfcvtl_h_b, b, h, 1, FUNOP(int8_to_float16),   0, 1)
GEN_FP_CVT_HELPER(mufcvt_s_w,  s, s, 2, FUNOP(uint32_to_float32), 0, 0)
GEN_FP_CVT_HELPER(mufcvth_h_b, b, h, 1, FUNOP(uint8_to_float16),  1, 1)
GEN_FP_CVT_HELPER(mufcvtl_h_b, b, h, 1, FUNOP(uint8_to_float16),  0, 1)

GEN_FP_CVT_HELPER(mfscvth_b_bf16, h, b, 1, FUNOP(bfloat16_to_int8),   1, 0)
GEN_FP_CVT_HELPER(mfscvtl_b_bf16, h, b, 1, FUNOP(bfloat16_to_int8),   0, 0)
GEN_FP_CVT_HELPER(mfucvth_b_bf16, h, b, 1, FUNOP(bfloat16_to_int8),   1, 0)
GEN_FP_CVT_HELPER(mfucvtl_b_bf16, h, b, 1, FUNOP(bfloat16_to_uint8),  0, 0)
GEN_FP_CVT_HELPER(msfcvth_bf16_b, b, h, 1, FUNOP(int8_to_bfloat16),   1, 1)
GEN_FP_CVT_HELPER(msfcvtl_bf16_b, b, h, 1, FUNOP(int8_to_bfloat16),   0, 1)
GEN_FP_CVT_HELPER(mufcvth_bf16_b, b, h, 1, FUNOP(uint8_to_bfloat16),  1, 1)
GEN_FP_CVT_HELPER(mufcvtl_bf16_b, b, h, 1, FUNOP(uint8_to_bfloat16),  0, 1)

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

void helper_fmmacc_s_bf20(void *md, void *ms1, void *ms2,
                          CPURISCVState *env){
    uint32_t i, j, k;
    uint32_t temp, psum;
    uint32_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->sizek >> 2); k++) {
                oprd_a = get_elem_s(ms1, i, k, env) & ~MAKE_64BIT_MASK(0, 12);
                oprd_b = get_elem_s(ms2, j, k, env) & ~MAKE_64BIT_MASK(0, 12);
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

GEN_FMMACCH_B_HELPER(fmmacc_bf16_e4, fmacc_f8e4_to_bf16, BF16_BINOP_FN(add))
GEN_FMMACCH_B_HELPER(fmmacc_bf16_e5, fmacc_f8e5_to_bf16, BF16_BINOP_FN(add))
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
                      bool streaming, bool transposed){
    uint32_t i, k;
    target_ulong addr;
    bool tcm = (rs1 & MAKE_64BIT_MASK(62, 2)) >> 62 == 0b10;

    if (!tcm) {
        if (transposed) {
            for (i = 0; i < env->sizek >> esz; i++) {
                probe_pages(env, rs1 + i * s2, env->sizem << esz, ra,
                            MMU_DATA_LOAD);
            }
        } else {
            for (i = 0; i < env->sizem; i++) {
                probe_pages(env, rs1 + i * s2, env->sizek, ra,
                            MMU_DATA_LOAD);
            }
        }
    }

    for (i = 0; i < get_mrows(env); i++) {
        for (k = 0; k < (get_rlenb(env) >> esz); k++) {
            if (transposed) {
                addr = rs1 + k * s2 + i * (1 << esz);
            } else {
                addr = rs1 + i * s2 + k * (1 << esz);
            }
            if (i < env->sizem && k < (env->sizek >> esz)) {
                uint64_t val = 0;
                if (tcm) {
#if !defined(CONFIG_USER_ONLY)
                    tpe_tcm_memory_read(env->tpe, addr, &val, 1 << esz);
#else
                    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST,
                                          GETPC());
#endif
                } else {
                    val = ld_elem(env, addr, ra);
                }
                set_elem(md, i, k, env, val);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
    if (gen_mem_trace()) {
        uint32_t packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        uint8_t type = streaming ? DATA_SRADDR : DATA_RADDR;
        uint32_t rows;
        uint32_t rlenb;
        if (transposed) {
            rows = env->sizek >> esz;
            rlenb = env->sizem << esz;
        } else {
            rows = env->sizem;
            rlenb = env->sizek;
        }
        for (i = 0; i < rows; i++) {
            target_ulong row_start_addr = rs1 + i * s2;
            write_trace_8_8(type, packlen, rlenb, row_start_addr);
            for (k = 0; k < rlenb / 4; k++) {
                uint32_t data_value = get_elem_s(md, i, k, env);
                write_trace_8_8(DATA_VALUE, packlen, 0, data_value);
                if (rlenb % 4) {
                    uint32_t mask =  (1 << (rlenb % 4) * 8) - 1;
                    write_trace_8_8(DATA_VALUE, packlen, 0, data_value & mask);
                }
            }
        }
    }
}

#define GEN_MMEXT_LD_HELPER(insn, ld_elem, set_elem, ESZ, streaming, \
                            transposed) \
void HELPER(insn)(void *md, target_ulong rs1, target_ulong s2,       \
                  CPURISCVState *env){                               \
    mmext_mld(md, rs1, s2, ld_elem, set_elem, env, ESZ, GETPC(),     \
              streaming, transposed);                                \
}

GEN_MMEXT_LD_HELPER(mld_b, ld_b, set_elem_b, 0, false, false)
GEN_MMEXT_LD_HELPER(mld_h, ld_h, set_elem_h, 1, false, false)
GEN_MMEXT_LD_HELPER(mld_w, ld_w, set_elem_s, 2, false, false)
GEN_MMEXT_LD_HELPER(mld_d, ld_d, set_elem_d, 3, false, false)

GEN_MMEXT_LD_HELPER(msld_b, ld_b, set_elem_b, 0, true, false)
GEN_MMEXT_LD_HELPER(msld_h, ld_h, set_elem_h, 1, true, false)
GEN_MMEXT_LD_HELPER(msld_w, ld_w, set_elem_s, 2, true, false)
GEN_MMEXT_LD_HELPER(msld_d, ld_d, set_elem_d, 3, true, false)

GEN_MMEXT_LD_HELPER(mldt_e8, ld_b, set_elem_b, 0, false, true)
GEN_MMEXT_LD_HELPER(mldt_e16, ld_h, set_elem_h, 1, false, true)
GEN_MMEXT_LD_HELPER(mldt_e32, ld_w, set_elem_s, 2, false, true)
GEN_MMEXT_LD_HELPER(mldt_e64, ld_d, set_elem_d, 3, false, true)

GEN_MMEXT_LD_HELPER(msldt_e8, ld_b, set_elem_b, 0, true, true)
GEN_MMEXT_LD_HELPER(msldt_e16, ld_h, set_elem_h, 1, true, true)
GEN_MMEXT_LD_HELPER(msldt_e32, ld_w, set_elem_s, 2, true, true)
GEN_MMEXT_LD_HELPER(msldt_e64, ld_d, set_elem_d, 3, true, true)

static void mmext_mldm(void *md, target_ulong rs1, uint8_t nf,
                       mmext_ld_fn *ld_elem, mmext_set_elem *set_elem,
                       CPURISCVState *env, uint8_t esz, uintptr_t ra){
    uint32_t n, i, k;
    target_ulong addr;
    void *temp;
    bool tcm = (rs1 & MAKE_64BIT_MASK(62, 2)) >> 62 == 0b10;

    if (!tcm) {
        for (n = 0; n < nf; n++) {
            for (i = 0; i < get_mrows(env); i++) {
                addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i;
                probe_pages(env, addr, get_rlenb(env), ra,
                            MMU_DATA_LOAD);
            }
        }
    }
    for (n = 0; n < nf; n++) {
        temp = (void *)((char *) md + n * get_mlenb(env));
        for (i = 0; i < get_mrows(env); i++) {
            for (k = 0; k < (get_rlenb(env) >> esz); k++) {
                uint64_t val = 0;
                addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i + k * (1 << esz);
                if (tcm) {
#if !defined(CONFIG_USER_ONLY)
                    tpe_tcm_memory_read(env->tpe, addr, &val, 1 << esz);
#else
                    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST,
                                          GETPC());
#endif
                } else {
                    val = ld_elem(env, addr, ra);
                }
                set_elem(temp, i, k, env, val);
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
                      bool streaming, bool transposed) {
    uint32_t i, k;
    target_ulong addr;
    bool tcm = (rs1 & MAKE_64BIT_MASK(62, 2)) >> 62 == 0b10;
    if (!tcm) {
        if (transposed) {
            for (i = 0; i < env->sizek >> esz; i++) {
                probe_pages(env, rs1 + i * s2, env->sizem << esz, ra,
                            MMU_DATA_LOAD);
            }
        } else {
            for (i = 0; i < env->sizem; i++) {
                probe_pages(env, rs1 + i * s2, env->sizek, ra,
                            MMU_DATA_STORE);
            }
        }
    }
    for (i = 0; i < env->sizem; i++) {
        for (k = 0; k < (env->sizek >> esz); k++) {
            uint64_t val = 0;
            if (transposed) {
                addr = rs1 + k * s2 + i * (1 << esz);
            } else {
                addr = rs1 + i * s2 + k * (1 << esz);
            }
            val = get_elem(ms3, i, k, env);
            if (tcm) {
#if !defined(CONFIG_USER_ONLY)
                tpe_tcm_memory_write(env->tpe, addr, &val, 1 << esz);
#else
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST,
                                      GETPC());
#endif
            } else {
                st_elem(env, addr, val, ra);
            }
        }
    }
    if (gen_mem_trace()) {
        uint32_t packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        uint8_t type = streaming ? DATA_SWADDR : DATA_WADDR;
        uint32_t rows;
        uint32_t rlenb;
        if (transposed) {
            rows = env->sizek >> esz;
            rlenb = env->sizem << esz;
        } else {
            rows = env->sizem;
            rlenb = env->sizek;
        }

        for (i = 0; i < rows; i++) {
            target_ulong row_start_addr = rs1 + i * s2;
            write_trace_8_8(type, packlen, rlenb, row_start_addr);
            for (k = 0; k < rlenb / 4; k++) {
                 uint32_t data_value = get_elem_s(ms3, i, k, env);
                 write_trace_8_8(DATA_VALUE, packlen, 0, data_value);
                 if (rlenb % 4) {
                     uint32_t mask =  (1 << (rlenb % 4) * 8) - 1;
                     write_trace_8_8(DATA_VALUE, packlen, 0, data_value & mask);
                 }
            }
        }
    }
}

#define GEN_MMEXT_ST_HELPER(insn, st_elem, get_elem, ESZ, streaming,    \
                            transposed)                                 \
void HELPER(insn)(void *ms3, target_ulong rs1, target_ulong s2,         \
                  CPURISCVState *env){                                  \
    mmext_mst(ms3, rs1, s2, st_elem, get_elem, env, ESZ, GETPC(),       \
              streaming, transposed);                                   \
}

GEN_MMEXT_ST_HELPER(mst_b, st_b, get_elem_b, 0, false, false)
GEN_MMEXT_ST_HELPER(mst_h, st_h, get_elem_h, 1, false, false)
GEN_MMEXT_ST_HELPER(mst_w, st_w, get_elem_s, 2, false, false)
GEN_MMEXT_ST_HELPER(mst_d, st_d, get_elem_d, 3, false, false)

GEN_MMEXT_ST_HELPER(msst_b, st_b, get_elem_b, 0, true, false)
GEN_MMEXT_ST_HELPER(msst_h, st_h, get_elem_h, 1, true, false)
GEN_MMEXT_ST_HELPER(msst_w, st_w, get_elem_s, 2, true, false)
GEN_MMEXT_ST_HELPER(msst_d, st_d, get_elem_d, 3, true, false)

GEN_MMEXT_ST_HELPER(mstt_e8, st_b, get_elem_b, 0, false, true)
GEN_MMEXT_ST_HELPER(mstt_e16, st_h, get_elem_h, 1, false, true)
GEN_MMEXT_ST_HELPER(mstt_e32, st_w, get_elem_s, 2, false, true)
GEN_MMEXT_ST_HELPER(mstt_e64, st_d, get_elem_d, 3, false, true)

GEN_MMEXT_ST_HELPER(msstt_e8, st_b, get_elem_b, 0, true, true)
GEN_MMEXT_ST_HELPER(msstt_e16, st_h, get_elem_h, 1, true, true)
GEN_MMEXT_ST_HELPER(msstt_e32, st_w, get_elem_s, 2, true, true)
GEN_MMEXT_ST_HELPER(msstt_e64, st_d, get_elem_d, 3, true, true)

static void mmext_mstm(void *ms3, target_ulong rs1, uint8_t nf,
                       mmext_st_fn *st_elem, mmext_get_elem *get_elem,
                       CPURISCVState *env, uint8_t esz, uintptr_t ra){
    uint32_t n, i, k;
    target_ulong addr;
    void *temp;
    bool tcm = (rs1 & MAKE_64BIT_MASK(62, 2)) >> 62 == 0b10;

    if (!tcm) {
        for (n = 0; n < nf; n++) {
            for (i = 0; i < get_mrows(env); i++) {
                addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i;
                probe_pages(env, addr, get_rlenb(env), ra,
                            MMU_DATA_STORE);
            }
        }
    }
    for (n = 0; n < nf; n++) {
        temp = (void *)((char *) ms3 + n * get_mlenb(env));
        for (i = 0; i < get_mrows(env); i++) {
            for (k = 0; k < (get_rlenb(env) >> esz); k++) {
                uint64_t val = 0;
                addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i + k * (1 << esz);
                val =  get_elem(temp, i, k, env);
                if (tcm) {
#if !defined(CONFIG_USER_ONLY)
                    tpe_tcm_memory_write(env->tpe, addr, &val, 1 << esz);
#else
                    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST,
                                          GETPC());
#endif
                } else {
                    st_elem(env, addr, val, ra);
                }
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
GEN_MPACK_HELPER(mpackhl, false, true)

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
        /* reverse direction iteration to avoid data overlap when ms1=md */
        if (up) {
            /* slide up: iterate from right to left */
            for (k = cols - 1; k >= 0; k--) {
                if (k >= valid_uimm) {
                    result = get_elem(ms1, i, k - valid_uimm, env);
                    dst_col = k;
                } else {
                    result = 0;
                    dst_col = k;
                }
                set_elem(md, i, dst_col, env, result);
            }
        } else {
            /* slide down: iterate from left to right */
            for (k = 0; k < cols; k++) {
                if (k < cols - valid_uimm) {
                    result = get_elem(ms1, i, k + valid_uimm, env);
                } else {
                    result = 0;
                }
                set_elem(md, i, k, env, result);
            }
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

/* v0.5 */
void helper_mfmacc_h_e2m1(void *md, void *ms1, void *ms2,
                          CPURISCVState *env)
{
    uint32_t i, j, k;
    uint16_t temp, psum;
    float16 oprd_a, oprd_b;
    void *ms2_pair_1 = ms2;
    void *ms2_pair_2 = (void *) (((int8_t *) ms2) + get_mlenb(env));
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env) * 2; j++) {
            temp = 0;
            for (k = 0; k < (env->sizek << 1); k++) {
                float4e2 a, b;
                a = get_elem_p(ms1, i, k, env);
                oprd_a = float4e2_to_float16(a, &env->mfp_status);
                if (j >= get_mrows(env)) {
                    b = get_elem_p(ms2_pair_2, j % (get_mrows(env)),
                                   k, env);
                } else {
                    b = get_elem_p(ms2_pair_1, j, k, env);
                }
                oprd_b = float4e2_to_float16(b, &env->mfp_status);
                temp = fmacc16(oprd_a, oprd_b, temp, &env->mfp_status);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_h(md, i, j, env);
                psum = float16_add(psum, temp, &env->mfp_status);
                set_elem_h(md, i, j, env, psum);
            } else {
                set_elem_h(md, i, j, env, 0);
            }
        }
    }
}

void helper_mfmacc_bf16_e2m1(void *md, void *ms1, void *ms2,
                             CPURISCVState *env)
{
    uint32_t i, j, k;
    uint16_t temp, psum;
    bfloat16 oprd_a, oprd_b;
    void *ms2_pair_1 = ms2;
    void *ms2_pair_2 = (void *) (((int8_t *) ms2) + get_mlenb(env));
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env) * 2; j++) {
            temp = 0;
            for (k = 0; k < (env->sizek << 1); k++) {
                float4e2 a, b;
                a = get_elem_p(ms1, i, k, env);
                oprd_a = float4e2_to_bfloat16(a, &env->mfp_status);
                if (j >= get_mrows(env)) {
                    b = get_elem_p(ms2_pair_2, j % (get_mrows(env)),
                                   k, env);
                } else {
                    b = get_elem_p(ms2_pair_1, j, k, env);
                }
                oprd_b = float4e2_to_bfloat16(b, &env->mfp_status);
                temp = fmaccbf16(oprd_a, oprd_b, temp, &env->mfp_status);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_h(md, i, j, env);
                psum = bfloat16_add(psum, temp, &env->mfp_status);
                set_elem_h(md, i, j, env, psum);
            } else {
                set_elem_h(md, i, j, env, 0);
            }
        }
    }
}

void helper_mfmacc_s_e2m1(void *md, void *ms1, void *ms2,
                          CPURISCVState *env)
{
    uint32_t i, j, k;
    uint32_t temp, psum;
    float32 oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->sizek << 1); k++) {
                float4e2 a, b;
                a = get_elem_p(ms1, i, k, env);
                oprd_a = float4e2_to_float32(a, &env->mfp_status);
                b = get_elem_p(ms2, j, k, env);
                oprd_b = float4e2_to_float32(b, &env->mfp_status);
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

static float32
do_k1_once(uint32_t i, uint32_t j, uint32_t k2, uint32_t k2_blocksize,
           uint32_t k1, uint32_t k1_blocksize, uint32_t k1_blocks,
           uint32_t k0_blocks, uint32_t a_blocksize, uint32_t b_blocksize,
           uint32_t k_start, uint32_t psum,
           mmext_get_elem* a_get_elem,
           mmext_get_elem* b_get_elem,
           fp_unop *a_fcvt_fn,
           fp_unop *b_fcvt_fn,
           void *ms1, void *ms2, void *ms1_s, void *ms2_s,
           bool ue4m3,
           CPURISCVState *env)
{
    float8e0 sa, sb;
    uint8_t a, b;
    uint32_t bsum = 0;
    float32 oprd_a, oprd_b;
    uint32_t k0;
    for (k0 = 0; k0 < k0_blocks; k0++) {
        a = a_get_elem(ms1, i, k2 * k2_blocksize +
                               k1 * k1_blocksize + k0, env);
        b = b_get_elem(ms2, j, k_start + k2 * k2_blocksize +
                               k1 * k1_blocksize + k0, env);
        oprd_a = a_fcvt_fn(a, &env->mfp_status);
        oprd_b = b_fcvt_fn(b, &env->mfp_status);
        bsum = fmacc32(oprd_a, oprd_b, bsum, &env->mfp_status);
    }
    if (a_blocksize > b_blocksize) {
        sa = get_elem_b(ms1_s, i, env->ma_colidx + k2, env);
        sb = get_elem_b(ms2_s, j, env->mb_colidx +
                                  k2 * k1_blocks + k1, env);
    } else {
        sb = get_elem_b(ms2_s, j, env->mb_colidx + k2, env);
        sa = get_elem_b(ms1_s, i, env->ma_colidx +
                                  k2 * k1_blocks + k1, env);
    }
    if (ue4m3) {
        sa = sa & 0x7f;
        sb = sb & 0x7f;
        oprd_a = float8e4_to_float32(sa, &env->mfp_status);
        oprd_b = float8e4_to_float32(sb, &env->mfp_status);
    } else {
        oprd_a = float8e0_to_float32(sa, &env->mfp_status);
        oprd_b = float8e0_to_float32(sb, &env->mfp_status);
    }
    bsum = float32_mul(bsum, oprd_a, &env->mfp_status);
    bsum = float32_mul(bsum, oprd_b, &env->mfp_status);
    return float32_add(psum, bsum, &env->mfp_status);
}

static void do_mfmacc_s_mx(void *md, void *ms1, void *ms2,
                           void *ms1_s, void *ms2_s,
                           mmext_get_elem* a_get_elem,
                           mmext_get_elem* b_get_elem,
                           fp_unop *a_fcvt_fn,
                           fp_unop *b_fcvt_fn,
                           uint32_t a_esz,
                           uint32_t b_esz,
                           uint32_t a_blocksize,
                           uint32_t b_blocksize,
                           bool ms2_hi, bool ue4m3,
                           CPURISCVState *env)
{
    uint32_t i, j, k2, k1;
    uint32_t psum;
    uint32_t k2_blocks, k1_blocks, k0_blocks;
    uint32_t k2_blocksize, k1_blocksize;
    uint32_t l_k2_blocksize, l_k1_blocks, l_k0_blocks;
    uint32_t ll_k1_blocksize, ll_k0_blocks;

    if (a_blocksize > b_blocksize) {
        /* process body elements */
        assert((a_blocksize & (a_blocksize - 1)) == 0);
        k2_blocks = (env->sizek * 8 / a_esz) / a_blocksize;
        k1_blocks = a_blocksize / b_blocksize;
        k0_blocks = b_blocksize;
        k2_blocksize = a_blocksize;
        k1_blocksize = b_blocksize;
        l_k2_blocksize = (env->sizek * 8 / a_esz) % k2_blocksize;
    } else {
        assert((b_blocksize & (b_blocksize - 1)) == 0);
        /* for process body elements */
        k2_blocks = (env->sizek * 8 / a_esz) / b_blocksize;
        k1_blocks = b_blocksize / a_blocksize;
        k0_blocks = a_blocksize;
        k2_blocksize = b_blocksize;
        k1_blocksize = a_blocksize;
        l_k2_blocksize = (env->sizek * 8 / a_esz) % k2_blocksize;
    }
    /* process tail elements */
    l_k1_blocks = l_k2_blocksize / k1_blocksize;
    l_k0_blocks = k1_blocksize;

    /* process tail elements */
    ll_k1_blocksize = l_k2_blocksize % k1_blocksize;
    ll_k0_blocks = ll_k1_blocksize;

    uint32_t k_start = ms2_hi ? get_rlenb(env) * 4 / b_esz : 0;

    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            psum = get_elem_s(md, i, j, env);
            for (k2 = 0; k2 < k2_blocks; k2++) {
                for (k1 = 0; k1 < k1_blocks; k1++) {
                    psum = do_k1_once(i, j, k2, k2_blocksize, k1, k1_blocksize,
                               k1_blocks, k0_blocks,
                               a_blocksize, b_blocksize, k_start, psum,
                               a_get_elem, b_get_elem, a_fcvt_fn, b_fcvt_fn,
                               ms1, ms2, ms1_s, ms2_s, ue4m3, env);
                }
            }
            if (l_k2_blocksize) {
               for (k1 = 0; k1 < l_k1_blocks; k1++) {
                    psum = do_k1_once(i, j, k2, k2_blocksize, k1, k1_blocksize,
                               k1_blocks, l_k0_blocks,
                               a_blocksize, b_blocksize, k_start, psum,
                               a_get_elem, b_get_elem, a_fcvt_fn, b_fcvt_fn,
                               ms1, ms2, ms1_s, ms2_s, ue4m3, env);
               }
               if (ll_k1_blocksize) {
                    psum = do_k1_once(i, j, k2, k2_blocksize, k1, k1_blocksize,
                               k1_blocks, ll_k0_blocks,
                               a_blocksize, b_blocksize, k_start, psum,
                               a_get_elem, b_get_elem, a_fcvt_fn, b_fcvt_fn,
                               ms1, ms2, ms1_s, ms2_s, ue4m3, env);
               }
           }

           if (i < env->sizem && j < env->sizen) {
               set_elem_s(md, i, j, env, psum);
           } else {
               set_elem_s(md, i, j, env, 0);
           }
        }
    }
}

static uint32_t mfmacc_s_mx_check(CPURISCVState *env, uint64_t ra,
                                  uint32_t bits, bool mxa, bool half)
{
    uint32_t pnum = riscv_cpu_cfg(env)->mrowlen / bits;
    uint32_t bnum = (1 << (mxa ? env->ma_blksize : env->mb_blksize)) * 16;

    if (half) {
        pnum = pnum / 2;
    }
    if (bnum > pnum) {
        /* The blocksize of private elements should not bigger than PNUM */
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
    assert((pnum % bnum) == 0);
    if ((mxa ? env->ma_colidx : env->mb_colidx) % (pnum / bnum)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
    return bnum;
}

void helper_mfmacc_s_mxe5m2(void *md, void *ms1, void *ms2,
                            void *ms1_s, void *ms2_s,
                            CPURISCVState *env)
{
    uint32_t a_bnum = mfmacc_s_mx_check(env, GETPC(), 8, true, false);
    uint32_t b_bnum = mfmacc_s_mx_check(env, GETPC(), 8, false, false);
    do_mfmacc_s_mx(md, ms1, ms2, ms1_s, ms2_s, get_elem_b, get_elem_b,
                   FUNOP(float8e5_to_float32),
                   FUNOP(float8e5_to_float32),
                   8, 8, a_bnum, b_bnum, false, false, env);
}

void helper_mfmacc_s_mxe4m3(void *md, void *ms1, void *ms2,
                            void *ms1_s, void *ms2_s,
                            CPURISCVState *env)
{
    uint32_t a_bnum = mfmacc_s_mx_check(env, GETPC(), 8, true, false);
    uint32_t b_bnum = mfmacc_s_mx_check(env, GETPC(), 8, false, false);
    do_mfmacc_s_mx(md, ms1, ms2, ms1_s, ms2_s, get_elem_b, get_elem_b,
                   FUNOP(float8e4_to_float32),
                   FUNOP(float8e4_to_float32),
                   8, 8,  a_bnum, b_bnum, false, false, env);
}

void helper_mfmacc_s_mxe2m1(void *md, void *ms1, void *ms2,
                            void *ms1_s, void *ms2_s,
                            CPURISCVState *env)
{
    uint32_t a_bnum = mfmacc_s_mx_check(env, GETPC(), 4, true, false);
    uint32_t b_bnum = mfmacc_s_mx_check(env, GETPC(), 4, false, false);
    do_mfmacc_s_mx(md, ms1, ms2, ms1_s, ms2_s, get_elem_p, get_elem_p,
                   FUNOP(float4e2_to_float32),
                   FUNOP(float4e2_to_float32),
                   4, 4, a_bnum, b_bnum, false, false, env);
}

void helper_mfmacc_s_mxe2m1_ue4m3(void *md, void *ms1, void *ms2,
                                  void *ms1_s, void *ms2_s,
                                  CPURISCVState *env)
{
    uint32_t a_bnum = mfmacc_s_mx_check(env, GETPC(), 4, true, false);
    uint32_t b_bnum = mfmacc_s_mx_check(env, GETPC(), 4, false, false);
    do_mfmacc_s_mx(md, ms1, ms2, ms1_s, ms2_s, get_elem_p, get_elem_p,
                   FUNOP(float4e2_to_float32),
                   FUNOP(float4e2_to_float32),
                   4, 4, a_bnum, b_bnum, false, true, env);
}

void helper_mfmacc_s_mxe5m2e2m1_l(void *md, void *ms1, void *ms2,
                                  void *ms1_s, void *ms2_s,
                                  CPURISCVState *env)
{
    uint32_t a_bnum = mfmacc_s_mx_check(env, GETPC(), 8, true, false);
    uint32_t b_bnum = mfmacc_s_mx_check(env, GETPC(), 4, false, true);
    do_mfmacc_s_mx(md, ms1, ms2, ms1_s, ms2_s, get_elem_b, get_elem_p,
                   FUNOP(float8e5_to_float32),
                   FUNOP(float4e2_to_float32),
                   8, 4, a_bnum, b_bnum, false, false, env);
}

void helper_mfmacc_s_mxe5m2e2m1_h(void *md, void *ms1, void *ms2,
                                  void *ms1_s, void *ms2_s,
                                  CPURISCVState *env)
{
    uint32_t a_bnum = mfmacc_s_mx_check(env, GETPC(), 8, true, false);
    uint32_t b_bnum = mfmacc_s_mx_check(env, GETPC(), 4, false, true);
    do_mfmacc_s_mx(md, ms1, ms2, ms1_s, ms2_s, get_elem_b, get_elem_p,
                   FUNOP(float8e5_to_float32),
                   FUNOP(float4e2_to_float32),
                   8, 4, a_bnum, b_bnum, true, false, env);
}

void helper_mfmacc_s_mxe4m3e2m1_l(void *md, void *ms1, void *ms2,
                                  void *ms1_s, void *ms2_s,
                                  CPURISCVState *env)
{
    uint32_t a_bnum = mfmacc_s_mx_check(env, GETPC(), 8, true, false);
    uint32_t b_bnum = mfmacc_s_mx_check(env, GETPC(), 4, false, true);
    do_mfmacc_s_mx(md, ms1, ms2, ms1_s, ms2_s, get_elem_b, get_elem_p,
                   FUNOP(float8e4_to_float32),
                   FUNOP(float4e2_to_float32),
                   8, 4, a_bnum, b_bnum, false, false, env);
}

void helper_mfmacc_s_mxe4m3e2m1_h(void *md, void *ms1, void *ms2,
                                  void *ms1_s, void *ms2_s,
                                  CPURISCVState *env)
{
    uint32_t a_bnum = mfmacc_s_mx_check(env, GETPC(), 8, true, false);
    uint32_t b_bnum = mfmacc_s_mx_check(env, GETPC(), 4, false, true);
    do_mfmacc_s_mx(md, ms1, ms2, ms1_s, ms2_s, get_elem_b, get_elem_p,
                   FUNOP(float8e4_to_float32),
                   FUNOP(float4e2_to_float32),
                   8, 4, a_bnum, b_bnum, true, false, env);
}

static inline uint64_t
fmacc_e4m3xe2m1_to_f16(uint64_t a, uint64_t b, uint64_t c, float_status *s)
{
    float16 b_f16 = float4e2_to_float16(b, s);
    float16 a_f16 = float8e4_to_float16(a, s);
    return fmacc16(a_f16, b_f16, c, s);
}

static inline uint64_t
fmacc_e5m2xe2m1_to_f16(uint64_t a, uint64_t b, uint64_t c, float_status *s)
{
    float16 b_f16 = float4e2_to_float16(b, s);
    float16 a_f16 = float8e5_to_float16(a, s);
    return fmacc16(a_f16, b_f16, c, s);
}

GEN_MPFMMACC_HELPER(mfmacc_h_e4m3e2m1,  b, p, h,
                    fmacc_e4m3xe2m1_to_f16, FADD16, 2)
GEN_MPFMMACC_HELPER(mfmacc_h_e5m2e2m1,  b, p, h,
                    fmacc_e5m2xe2m1_to_f16, FADD16, 2)
static inline uint64_t
fmacc_e4m3xe2m1_to_f32(uint64_t a, uint64_t b, uint64_t c, float_status *s)
{
    float32 b_f32 = float4e2_to_float32(b, s);
    float32 a_f32 = float8e4_to_float32(a, s);
    return fmacc32(a_f32, b_f32, c, s);
}

static inline uint64_t
fmacc_e5m2xe2m1_to_f32(uint64_t a, uint64_t b, uint64_t c, float_status *s)
{
    float32 b_f32 = float4e2_to_float32(b, s);
    float32 a_f32 = float8e5_to_float32(a, s);
    return fmacc32(a_f32, b_f32, c, s);
}

GEN_MPFMMACC_I_HELPER(mfmacc_s_e4m3e2m1, b, p, s,
                      fmacc_e4m3xe2m1_to_f32, FADD32, 1)
GEN_MPFMMACC_I_HELPER(mfmacc_s_e5m2e2m1, b, p, s,
                      fmacc_e5m2xe2m1_to_f32, FADD32, 1)
static inline void
mmext_p_float_cvt(void* md, void* ms1, CPURISCVState* env,
                  bool hi, bool use_signed, fp_unop *fp_fn)
{
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
            set_elem_b(md, i, k, env, fp_fn(result, &env->mfp_status));
        }
    }
}

#define GEN_PFLOAT_CVT_HELPER(insn, hi, use_signed, fp_fn) \
void HELPER(insn)(void* md, void* ms1, CPURISCVState* env) \
{                                                          \
    mmext_p_float_cvt(md, ms1, env, hi, use_signed, fp_fn);\
}

GEN_PFLOAT_CVT_HELPER(msfcvth_e4m3_p, true,  true, FUNOP(int8_to_float8e4))
GEN_PFLOAT_CVT_HELPER(msfcvtl_e4m3_p, false, true, FUNOP(int8_to_float8e4))
GEN_PFLOAT_CVT_HELPER(msfcvth_e5m2_p, true,  true, FUNOP(int8_to_float8e5))
GEN_PFLOAT_CVT_HELPER(msfcvtl_e5m2_p, false, true, FUNOP(int8_to_float8e5))
GEN_PFLOAT_CVT_HELPER(mufcvth_e4m3_p, true,  false, FUNOP(uint8_to_float8e4))
GEN_PFLOAT_CVT_HELPER(mufcvtl_e4m3_p, false, false, FUNOP(uint8_to_float8e4))
GEN_PFLOAT_CVT_HELPER(mufcvth_e5m2_p, true,  false, FUNOP(uint8_to_float8e5))
GEN_PFLOAT_CVT_HELPER(mufcvtl_e5m2_p, false, false, FUNOP(uint8_to_float8e5))

typedef uint16_t (*ConvertFunc)(uint64_t src, int offset, int bits, bool is_signed, float_status *status);
typedef uint16_t (*ArithFunc)(uint16_t a, uint16_t b, float_status *status);

typedef struct {
    // 数据提取配置
    int weight_bits;        // 元素位数（4或8）
    bool weight_is_signed;       // 是否符号扩展
    bool has_zeropoint;   // 是否需要减去零点
    bool dual_scale;      // 是否使用双缩放因子

    // 函数指针
    ConvertFunc convert;  // 数据类型转换函数
    ArithFunc mul;        // 乘法运算函数
    ArithFunc sub;        // 减法运算函数
} DequantConfig;

static void dequant_common(
    void *md, void *ms1, void *ms2,
    target_ulong rs1, target_ulong imm,
    CPURISCVState *env,
    const DequantConfig *cfg, mmext_get_elem *get_elem)
{
    uint32_t i, k, skip;
    const uint32_t cols = get_rlenb(env) / 2;
    const uint32_t rows = get_mrows(env);

    skip = cols * imm;
    const uint32_t mlenb = get_mlenb(env);
    void *dest = g_malloc0(mlenb);
    for (i = 0; i < rows; i++) {
        // 加载缩放因子和零点
        uint64_t scale_data = (cfg->dual_scale || cfg->has_zeropoint) ?
            get_elem_s(ms1, i, rs1, env) :
            get_elem_h(ms1, i, rs1, env);

        const uint16_t scale0 = extract64(scale_data, 0, 16);
        const uint16_t scale1 = cfg->dual_scale ? extract64(scale_data, 16, 16) : 0;
        const uint16_t zp = cfg->has_zeropoint ? extract64(scale_data, 16, 16) : 0;

        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek / 2)) {
                // 提取权重
                const uint64_t packed = get_elem(ms2, i, k + skip, env);
                const uint16_t weight = cfg->convert(packed, 0,
                                                     cfg->weight_bits,
                                                     cfg->weight_is_signed,
                                                     &env->mfp_status);

                // 计算缩放
                const uint16_t scale = cfg->dual_scale ?
                    (k < cols/2 ? scale0 : scale1) : scale0;
                uint16_t result = cfg->mul(weight, scale, &env->mfp_status);

                // 应用零点
                if (cfg->has_zeropoint) {
                    result = cfg->sub(result, zp, &env->mfp_status);
                }

                set_elem_h(dest, i, k, env, result);
            }
        }
    }
    memcpy(md, dest, mlenb);
    g_free(dest);
}

// float16转换
static uint16_t convert_to_f16(uint64_t src, int offset, int bits, bool weight_is_signed, float_status *s) {
    int64_t val = weight_is_signed ?
        sextract64(src, offset, bits) :
        extract64(src, offset, bits);
    return int8_to_float16(val, s);
}

// bfloat16转换
static uint16_t convert_to_bf16(uint64_t src, int offset, int bits, bool weight_is_signed, float_status *s) {
    int64_t val = weight_is_signed ?
        sextract64(src, offset, bits) :
        extract64(src, offset, bits);
    return int8_to_bfloat16(val, s);
}

void HELPER(mfdequantu_h_hp_zp)(void* md, void* ms1, void *ms2,
                                target_ulong rs1, target_ulong imm,
                                CPURISCVState *env)
{

    DequantConfig cfg = {
        .weight_bits = 4,
        .weight_is_signed = false,
        .has_zeropoint = true,
        .dual_scale = false,
        .convert = convert_to_f16,
        .mul = float16_mul,
        .sub = float16_sub
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_p);
}

void HELPER(mfdequant_h_hp)(void* md, void* ms1, void *ms2,
                             target_ulong rs1, target_ulong imm,
                             CPURISCVState *env)
{
    DequantConfig cfg = {
        .weight_bits = 4,
        .weight_is_signed = true,
        .has_zeropoint = false,
        .dual_scale = false,
        .convert = convert_to_f16,
        .mul = float16_mul,
        .sub = float16_sub
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_p);
}

void HELPER(mfdequant_h_hb)(void* md, void* ms1, void *ms2,
                             target_ulong rs1, target_ulong imm,
                             CPURISCVState *env)
{
    DequantConfig cfg = {
        .weight_bits = 8,
        .weight_is_signed = true,
        .has_zeropoint = false,
        .dual_scale = false,
        .convert = convert_to_f16,
        .mul = float16_mul,
        .sub = float16_sub
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_b);
}

void HELPER(mfdequant_bf16_bf16b)(void* md, void* ms1, void *ms2,
                             target_ulong rs1, target_ulong imm,
                             CPURISCVState *env)
{
    DequantConfig cfg = {
        .weight_bits = 8,
        .weight_is_signed = true,
        .has_zeropoint = false,
        .dual_scale = false,
        .convert = convert_to_bf16,
        .mul = bfloat16_mul,
        .sub = bfloat16_sub
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_b);
}

void HELPER(mfdequantu_bf16_bf16p_zp)(void* md, void* ms1, void *ms2,
                                target_ulong rs1, target_ulong imm,
                                CPURISCVState *env)
{
    const DequantConfig cfg = {
        .weight_bits = 4,
        .weight_is_signed = false,
        .has_zeropoint = true,
        .dual_scale = false,
        .convert = convert_to_bf16,
        .mul = bfloat16_mul,
        .sub = bfloat16_sub
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_p);
}

void HELPER(mfdequant_bf16_bf16p)(void* md, void* ms1, void *ms2,
                                target_ulong rs1, target_ulong imm,
                                CPURISCVState *env)
{
    DequantConfig cfg = {
        .weight_bits = 4,
        .weight_is_signed = true,
        .has_zeropoint = false,
        .dual_scale = false,
        .convert = convert_to_bf16,
        .mul = bfloat16_mul,
        .sub = bfloat16_sub // 即使未使用也需占位
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_p);
}

void HELPER(mfdequantu_h_hb_zp)(void* md, void* ms1, void *ms2,
                                target_ulong rs1, target_ulong imm,
                                CPURISCVState *env)
{
    DequantConfig cfg = {
        .weight_bits = 8,
        .weight_is_signed = false,
        .has_zeropoint = true,
        .dual_scale = false,
        .convert = convert_to_f16,
        .mul = float16_mul,
        .sub = float16_sub
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_b);
}

void HELPER(mfdequantu_bf16_bf16b_zp)(void* md, void* ms1, void *ms2,
                                target_ulong rs1, target_ulong imm,
                                CPURISCVState *env)
{
    DequantConfig cfg = {
        .weight_bits = 8,
        .weight_is_signed = false,
        .has_zeropoint = true,
        .dual_scale = false,
        .convert = convert_to_bf16,
        .mul = bfloat16_mul,
        .sub = bfloat16_sub
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_b);
}

void HELPER(mfdequanth_h_hb)(void* md, void* ms1, void *ms2,
                             target_ulong rs1, target_ulong imm,
                             CPURISCVState *env)
{
    DequantConfig cfg = {
        .weight_bits = 8,
        .weight_is_signed = true,
        .has_zeropoint = false,
        .dual_scale = true, // 启用双缩放因子
        .convert = convert_to_f16,
        .mul = float16_mul,
        .sub = float16_sub
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_b);
}

void HELPER(mfdequanth_bf16_bf16b)(void* md, void* ms1, void *ms2,
                             target_ulong rs1, target_ulong imm,
                             CPURISCVState *env)
{
    DequantConfig cfg = {
        .weight_bits = 8,
        .weight_is_signed = true,
        .has_zeropoint = false,
        .dual_scale = true, // 启用双缩放因子
        .convert = convert_to_bf16,
        .mul = bfloat16_mul,
        .sub = bfloat16_sub
    };
    dequant_common(md, ms1, ms2, rs1, imm, env, &cfg, get_elem_b);
}

// 通用归约操作函数类型
// abs: 是否对输入取绝对值（如 mfredmax.abs）
typedef int64_t (mfred_op_fn)(void *start, CPURISCVState *env,
                            uint32_t count, uint32_t initial, bool abs);

static void mfred_mf_common(void *md, void *ms1, void *ms2,
                            CPURISCVState *env,
                            uint32_t esz_log2,           // 元素大小的 log2（h:1, w:2）
                            bool abs, bool dup,
                            mfred_op_fn *reduce_fn,       // 归约函数
                            mmext_get_elem *get_elem,        // get_elem_h / get_elem_s
                            mmext_set_elem *set_elem)        // set_elem_h / set_elem_s
{
    const uint32_t elem_bytes = 1U << esz_log2;          // 每个元素字节数
    const uint32_t total_bytes = env->sizek;             // sizek 是字节数
    const uint32_t total_elems = total_bytes >> esz_log2; // 元素个数

    const uint32_t rlenb = get_rlenb(env);
    const uint32_t max_elems_per_block = rlenb >> esz_log2; // RLEN 限制下的最大元素数
    const uint32_t mlenb = get_mlenb(env);

    // 基础块大小（元素个数）
    uint32_t base_blocksize_in_elems = 16 << env->ma_blksize;
    void *dest = g_malloc0(mlenb);

    // Clamping：不能超过 RLEN 支持的最大块
    if (base_blocksize_in_elems > max_elems_per_block) {
        base_blocksize_in_elems = max_elems_per_block;
    }

    // 块数量：向上取整
    uint32_t blocks = (total_elems + base_blocksize_in_elems - 1) / base_blocksize_in_elems;

    // 最后一块的元素数
    uint32_t last_block_elems = total_elems % base_blocksize_in_elems;
    if (last_block_elems == 0) {
        last_block_elems = base_blocksize_in_elems;
    }

    // 主循环：遍历每行和每块
    for (uint32_t i = 0; i < env->sizem; i++) {
        for (uint32_t b = 0; b < blocks; b++) {
            // 当前块起始地址（按元素偏移）
            void *block_start = (char *)ms2 + i * rlenb +
                                b * base_blocksize_in_elems * elem_bytes;

            // 当前块实际元素数
            uint32_t elem_count = (b == blocks - 1) ? last_block_elems : base_blocksize_in_elems;

            // 从 ms1 获取初始值
            int64_t init_val = get_elem(ms1, i, b, env);
            int64_t result;

            // 执行归约操作
            result = reduce_fn(block_start, env, elem_count + 1, init_val, abs);

            // 存储到 md
            if (dup) {
                for (uint32_t j = 0; j < elem_count; j++) {
                    set_elem(dest, i, b * base_blocksize_in_elems + j, env, result);
                }
            } else {
                set_elem(dest, i, b, env, result);
            }
        }
    }
    memcpy(md, dest, mlenb);
    g_free(dest);
}

static void mfred_whole_mf_common(void *md, void *ms1, void *ms2,
                            CPURISCVState *env,
                            uint32_t esz_log2,           // 元素大小的 log2（h:1, w:2）
                            bool abs,
                            mfred_op_fn *reduce_fn,       // 归约函数
                            mmext_get_elem *get_elem,        // get_elem_h / get_elem_s
                            mmext_set_elem *set_elem)        // set_elem_h / set_elem_s
{
    const uint32_t mlenb = get_mlenb(env);
    const uint32_t total_elems = mlenb >> esz_log2; // 元素个数
    void *dest = g_malloc0(mlenb);

    // 从 ms1 获取初始值
    int64_t init_val = get_elem(ms1, 0, 0, env);
    int64_t result;

    // 执行归约操作
    result = reduce_fn(ms2, env, total_elems + 1, init_val, abs);

    // 存储到 md
    set_elem(dest, 0, 0, env, result);
    memcpy(md, dest, mlenb);
    g_free(dest);
}

// 通用生成宏
#define GEN_MFRED(OP, ELEM, ESZ, GET, SET, ABS, DUP, SUFFIX)                  \
void HELPER(mfred##OP##_##ELEM##_##SUFFIX)(void *md, void *ms1, void *ms2,    \
                                           CPURISCVState *env)                \
{                                                                             \
    mfred_mf_common(md, ms1, ms2, env, ESZ, ABS, DUP,                         \
                    do_mfred##OP##_##ELEM##_internal,                         \
                    GET, SET);                                                \
}

// 批量生成某元素类型的所有变体
#define GEN_FOR_ELEM(ELEM, ESZ, GET, SET)                                     \
    /* dup_mf variants (abs=0, dup=1) */                                      \
    GEN_MFRED(max,    ELEM, ESZ, GET, SET, false, true, dup_mf)               \
    GEN_MFRED(min,    ELEM, ESZ, GET, SET, false, true, dup_mf)               \
    GEN_MFRED(sum,    ELEM, ESZ, GET, SET, false, true, dup_mf)               \
                                                                              \
    /* dup_abs_mf (only max) */                                               \
    GEN_MFRED(max,    ELEM, ESZ, GET, SET, true, true, dup_abs_mf)            \
                                                                              \
    /* c_mf variants (abs=false, dup=false) */                                \
    GEN_MFRED(max,    ELEM, ESZ, GET, SET, false, false, c_mf)                \
    GEN_MFRED(min,    ELEM, ESZ, GET, SET, false, false, c_mf)                \
    GEN_MFRED(sum,    ELEM, ESZ, GET, SET, false, false, c_mf)                \
                                                                              \
    /* c_abs_mf (only max) */                                                 \
    GEN_MFRED(max,    ELEM, ESZ, GET, SET, true, false, c_abs_mf)

// 生成 h 类型
GEN_FOR_ELEM(h, 1, get_elem_h, set_elem_h)

// 生成 bf16 类型（使用 h 的访问器）
GEN_FOR_ELEM(bf16, 1, get_elem_h, set_elem_h)

// 生成 w 类型
GEN_FOR_ELEM(s, 2, get_elem_s, set_elem_s)

// 清理宏
#undef GEN_MFRED
#undef GEN_FOR_ELEM

// 通用生成宏
#define GEN_MFRED_WHOLE(OP, ELEM, ESZ, GET, SET, ABS,SUFFIX)                  \
void HELPER(mfred##OP##_##ELEM##_m_##SUFFIX)(void *md, void *ms1, void *ms2,  \
                                             CPURISCVState *env)              \
{                                                                             \
    mfred_whole_mf_common(md, ms1, ms2, env, ESZ, ABS,                        \
                    do_mfred##OP##_##ELEM##_internal,                         \
                    GET, SET);                                                \
}

// 批量生成某元素类型的所有变体
#define GEN_WHOLE_FOR_ELEM(ELEM, ESZ, GET, SET)                    \
    /* mf variants (abs=0) */                                      \
    GEN_MFRED_WHOLE(max,    ELEM, ESZ, GET, SET, false, mf)        \
    GEN_MFRED_WHOLE(min,    ELEM, ESZ, GET, SET, false, mf)        \
    GEN_MFRED_WHOLE(sum,    ELEM, ESZ, GET, SET, false, mf)        \
                                                                   \
    /* abs_mf (only max) */                                        \
    GEN_MFRED_WHOLE(max,    ELEM, ESZ, GET, SET, true, abs_mf)

// 生成 h 类型
GEN_WHOLE_FOR_ELEM(h, 1, get_elem_h, set_elem_h)

// 生成 bf16 类型（使用 h 的访问器）
GEN_WHOLE_FOR_ELEM(bf16, 1, get_elem_h, set_elem_h)

// 生成 w 类型
GEN_WHOLE_FOR_ELEM(s, 2, get_elem_s, set_elem_s)

// 清理宏
#undef GEN_MFRED_WHOLE
#undef GEN_WHOLE_FOR_ELEM

static void mfred_dup_common(void *md, void *ms1,
                             CPURISCVState *env,
                             uint32_t esz_log2,
                             mmext_get_elem *get_elem,
                             mmext_set_elem *set_elem)
{
    const uint32_t rlenb = get_rlenb(env);
    const uint32_t total_elems = rlenb >> esz_log2; // 元素个数
    const uint32_t max_elems_per_block = rlenb >> esz_log2; // RLEN 限制下的最大元素数
    const uint32_t mlenb = get_mlenb(env);
    // 基础块大小（元素个数）
    uint32_t base_blocksize_in_elems = 16 << env->ma_blksize;
    void *dest = g_malloc0(mlenb);

    // Clamping：不能超过 RLEN 支持的最大块
    if (base_blocksize_in_elems > max_elems_per_block) {
        base_blocksize_in_elems = max_elems_per_block;
    }

    // 块数量
    uint32_t blocks = total_elems / base_blocksize_in_elems;

    // 主循环：遍历每行和每块
    for (uint32_t i = 0; i < env->sizem; i++) {
        for (uint32_t b = 0; b < blocks; b++) {
            // 从 ms1 获取值
            int64_t result = get_elem(ms1, i, b, env);

            // 存储到 md
            for (uint32_t j = 0; j < base_blocksize_in_elems; j++) {
                set_elem(dest, i, b * base_blocksize_in_elems + j, env, result);
            }
        }
    }
    memcpy(md, dest, mlenb);
    g_free(dest);
}

void HELPER(mfdup_e16)(void *md, void *ms1, CPURISCVState *env)
{
    mfred_dup_common(md, ms1, env, 1, get_elem_h, set_elem_h);
}

void HELPER(mfdup_e32)(void *md, void *ms1, CPURISCVState *env)
{
    mfred_dup_common(md, ms1, env, 2, get_elem_s, set_elem_s);
}

void HELPER(mfdup_e64)(void *md, void *ms1, CPURISCVState *env)
{
    mfred_dup_common(md, ms1, env, 3, get_elem_d, set_elem_d);
}

#if !defined(CONFIG_USER_ONLY)
void HELPER(th_dma_push_mem_r)(target_ulong rs1, CPURISCVState *env)
{
    tpe_dma_push_mem_r(env->tpe, rs1, env->mhartid);
}

void HELPER(th_dma_push_mem_w)(target_ulong rs1, CPURISCVState *env)
{
    tpe_dma_push_mem_w(env->tpe, rs1, env->mhartid);
}

void HELPER(th_dma_push_sram_pa)(target_ulong rs1, CPURISCVState *env)
{
    tpe_dma_push_tcm_pa(env->tpe, rs1, env->mhartid);
}

void HELPER(th_dma_push_desc_pa)(target_ulong rs1, CPURISCVState *env)
{
    tpe_dma_push_desc_pa(env->tpe, rs1, env->mhartid);
}

void HELPER(th_dma_push_coord)(target_ulong rs1, CPURISCVState *env)
{
    tpe_dma_push_coord(env->tpe, rs1, env->mhartid);
}

void HELPER(th_dma_copy_desc)(target_ulong rs1, CPURISCVState *env)
{
    tpe_dma_copy_desc(env->tpe, rs1, riscv_env_mmu_index(env, false),
                      env->mhartid);
}

void HELPER(th_dma_copy_tcm_mem)(target_ulong rs1, CPURISCVState *env)
{
    if (rs1 < 8 * sizeof(target_ulong)) {
        env->xmdmaidle &= ~((target_ulong)1 << rs1);
    }
    tpe_dma_copy_tcm_mem(env->tpe, rs1, riscv_env_mmu_index(env, false),
                         env->mhartid);
}

void HELPER(th_dma_copy_mem_tcm)(target_ulong rs1, CPURISCVState *env)
{
    if (rs1 < 8 * sizeof(target_ulong)) {
        env->xmdmaidle &= ~((target_ulong)1 << rs1);
    }
    tpe_dma_copy_mem_tcm(env->tpe, rs1, riscv_env_mmu_index(env, false),
                         env->mhartid);
}

/**
 * @brief Internal implementation for loading a matrix of packed 6-bit integers.
 *
 * This function handles the core logic of reading packed 6-bit data from
 * physical memory. It correctly calculates bit offsets, reads data across
 * byte boundaries, and performs either zero or sign extension based on the
 * `is_signed` flag before writing to the destination matrix `md`.
 *
 * @param md        Destination matrix/TCM structure.
 * @param rs1       Base physical address of the packed 6-bit source data.
 * @param rs2       row stride, must be 512 bit aligned.
 * @param env       CPU state, contains matrix dimensions and config.
 * @param is_signed If true, perform sign-extension; otherwise, zero-extend.
 */
static inline
void __load_6bit_matrix_impl(void *md, target_ulong rs1, target_ulong rs2,
                             CPURISCVState *env, bool is_signed,
                             uintptr_t ra)
{
    uint32_t i, k; /* Loop iterators for destination matrix rows (i) and columns (k) */

    bool tcm = (rs1 & MAKE_64BIT_MASK(62, 2)) >> 62 == 0b10;

    if (!tcm) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
    for (i = 0; i < get_mrows(env); i++) {
        for (k = 0; k < get_rlenb(env); k++) {
            /* If the destination element is outside the valid source region, pad with zero. */
            if (i < env->sizem && k < env->sizek) {
                /* 1. Calculate the element's starting bit offset from the base address. */
                uint64_t start_bit_offset = i * rs2 * 8 + k * 6 ;

                /* 2. Calculate the byte address and the bit offset within that byte. */
                uint64_t start_byte_offset = start_bit_offset / 8;
                uint32_t bit_in_byte = start_bit_offset % 8;

                target_ulong read_addr = rs1 + start_byte_offset;

                /* 3. Read a 16-bit window to ensure all bits are captured, even */
                /*    if the element straddles a byte boundary.                   */
                uint16_t buffer;
                tpe_tcm_memory_read(env->tpe, read_addr, &buffer, 2);

                /* 4. Extract the raw 6 bits from the buffer. */
                uint8_t raw_val6 = (buffer >> bit_in_byte) & 0x3F; /* 0b00111111 */
                uint8_t final_val;

                if (is_signed) {
                    /* --- Sign Extension --- */
                    final_val = (int8_t)(raw_val6 << 2) >> 2;
                } else {
                    /* --- Zero Extension --- */
                    /* The value is already zero-extended by the '& 0x3F' mask. */
                    final_val = raw_val6;
                }

                /* 5. Set the final extended element in the destination. */
                set_elem_b(md, i, k, env, final_val);
            } else {
                /* Pad with zero for elements outside the source matrix bounds. */
                set_elem_b(md, i, k, env, 0);
            }
        }
    }
}
#endif

void HELPER(th_mldtu6_tcm)(void *md, target_ulong rs1, target_ulong rs2,
                           CPURISCVState *env)
{
#if !defined(CONFIG_USER_ONLY)
    /* Call the internal implementation with sign-extension disabled. */
    __load_6bit_matrix_impl(md, rs1, rs2, env, false, GETPC());
#else
    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
#endif
}

void HELPER(th_mldts6_tcm)(void *md, target_ulong rs1, target_ulong rs2,
                           CPURISCVState *env)
{
#if !defined(CONFIG_USER_ONLY)
    /* Call the internal implementation with sign-extension enabled. */
    __load_6bit_matrix_impl(md, rs1, rs2, env, true, GETPC());
#else
    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
#endif
}
