#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "exec/memop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"
#include "exec/tracestub.h"


typedef enum{
    ADD,
    SUB,
    SRA,
    MUL,
} op_t;


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
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit;
    for (i = 0; i < get_mrows(env); i++){
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
                case SRA:
                    if (esz == 2) {
                        n_bit = (uint64_t) (get_elem(ms1, s1, k, env) & 0x1F);
                    } else {
                        n_bit = (uint64_t) (get_elem(ms1, s1, k, env) & 0x3F);
                    }
                    result = get_elem(ms2, i, k, env);
                    uint8_t round = get_round(env->mxrm, result, n_bit);
                    result = (result >> n_bit) + round;
                    set_elem(md, i, k, env, result);
                    break;
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
GEN_OP_MV_MX_HELPER(madd_d_mv_i, ADD, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(msub_d_mv_i, SUB, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(msra_d_mv_i, SRA, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(mmul_d_mv_i, MUL, get_elem_d, set_elem_d, 3, false)

GEN_OP_MV_MX_HELPER(madd_s_mv_x, ADD, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(msub_s_mv_x, SUB, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(msra_s_mv_x, SRA, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(mmul_s_mv_x, MUL, get_elem_s, set_elem_s, 2, false)
GEN_OP_MV_MX_HELPER(madd_d_mv_x, ADD, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(msub_d_mv_x, SUB, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(msra_d_mv_x, SRA, get_elem_d, set_elem_d, 3, false)
GEN_OP_MV_MX_HELPER(mmul_d_mv_x, MUL, get_elem_d, set_elem_d, 3, false)

GEN_OP_MV_MX_HELPER(mmulh_s_mv_i, MUL, get_elem_s, set_elem_s, 2, true)
GEN_OP_MV_MX_HELPER(mmulh_s_mv_x, MUL, get_elem_s, set_elem_s, 2, true)


static inline void mmext_mx(void* md, void* ms2, target_ulong s1,
                            CPURISCVState* env, mmext_get_elem* get_elem,
                            mmext_set_elem* set_elem, op_t op, uint8_t esz,
                            bool keep_hi){
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit;
    for (i = 0; i < get_mrows(env); i++){
        for (k = 0; k < cols; k++){
            if(i < env->sizem && k < (env->sizek >> esz)){
                switch(op){
                case ADD:
                    result = get_elem(ms2, i, k, env) + s1;
                    set_elem(md, i, k, env, result);
                    break;
                case SUB:
                    result = get_elem(ms2, i, k, env) - s1;
                    set_elem(md, i, k, env, result);
                    break;
                case MUL:
                    if (esz == 2) {
                        result = mul32(get_elem(ms2, i, k, env),
                                       s1, keep_hi);
                    } else {
                        result = mul64(get_elem(ms2, i, k, env),
                                       s1, keep_hi);
                    }
                    set_elem(md, i, k, env, result);
                    break;
                case SRA:
                    if (esz == 2) {
                        n_bit = (uint64_t) (s1 & 0x1F);
                    } else {
                        n_bit = (uint64_t) (s1 & 0x3F);
                    }
                    result = get_elem(ms2, i, k, env);
                    uint8_t round = get_round(env->mxrm, result, n_bit);
                    result = (result >> n_bit) + round;
                    set_elem(md, i, k, env, result);
                    break;
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

#define GEN_OP_MX_HELPER(insn, op, get_elem, set_elem, ESZ, keep_hi) \
void HELPER(insn)(void* md, void* ms2, target_ulong s1,              \
                  CPURISCVState* env){                               \
    mmext_mx(md, ms2, s1, env,                                       \
             get_elem, set_elem, op, ESZ, keep_hi);                  \
}

GEN_OP_MX_HELPER(madd_s_mx, ADD, get_elem_s, set_elem_s, 2, false)
GEN_OP_MX_HELPER(msub_s_mx, SUB, get_elem_s, set_elem_s, 2, false)
GEN_OP_MX_HELPER(msra_s_mx, SRA, get_elem_s, set_elem_s, 2, false)
GEN_OP_MX_HELPER(mmul_s_mx, MUL, get_elem_s, set_elem_s, 2, false)
GEN_OP_MX_HELPER(madd_d_mx, ADD, get_elem_d, set_elem_d, 3, false)
GEN_OP_MX_HELPER(msub_d_mx, SUB, get_elem_d, set_elem_d, 3, false)
GEN_OP_MX_HELPER(msra_d_mx, SRA, get_elem_d, set_elem_d, 3, false)
GEN_OP_MX_HELPER(mmul_d_mx, MUL, get_elem_d, set_elem_d, 3, false)

GEN_OP_MX_HELPER(mmulh_s_mx, MUL, get_elem_s, set_elem_s, 2, true)

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
    uint64_t n_bit;
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
                case SRA:
                    if (esz == 2) {
                        n_bit = (uint64_t) (get_elem(ms1, i, k, env) & 0x1F);
                    } else {
                        n_bit = (uint64_t) (get_elem(ms1, i, k, env) & 0x3F);
                    }
                    result = get_elem(ms2, i, k, env);
                    uint8_t round = get_round(env->mxrm, result, n_bit);
                    result = (result >> n_bit) + round;
                    set_elem(md, i, k, env, result);
                    break;
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
GEN_OP_MM_HELPER(madd_d_mm, ADD, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(msub_d_mm, SUB, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(msra_d_mm, SRA, get_elem_d, set_elem_d, 3, false)
GEN_OP_MM_HELPER(mmul_d_mm, MUL, get_elem_d, set_elem_d, 3, false)

GEN_OP_MM_HELPER(mmulh_s_mm, MUL, get_elem_s, set_elem_s, 2, true)

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
        if (result > UINT8_MAX) {
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
        if (result > UINT16_MAX) {
            result = UINT16_MAX;
            env->mxsat = 0x01;
        }
    }
    return result;
}

static inline void mmext_n4clip_mm(void *md, void *ms1, void *ms2,
                                   CPURISCVState *env, mmext_get_elem * get_elem,
                                   mmext_set_elem * set_elem, bool use_signed,
                                   uint8_t esz){
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit;
    uint8_t round;

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
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_OP_N4CLIP_MM_HELPER(insn, use_signed, get_elem, set_elem, ESZ)  \
void HELPER(insn)(void *md, void *ms1, void *ms2,                           \
                  CPURISCVState *env){                                      \
    mmext_n4clip_mm(md, ms1, ms2, env,                                      \
                    get_elem, set_elem, use_signed, ESZ);                   \
}

GEN_OP_N4CLIP_MM_HELPER(mn4clip_s_mm,  true,  get_elem_s, set_elem_b, 2)
GEN_OP_N4CLIP_MM_HELPER(mn4clipu_s_mm, false, get_elem_s, set_elem_b, 2)
GEN_OP_N4CLIP_MM_HELPER(mn4clip_d_mm,  true,  get_elem_d, set_elem_h, 3)
GEN_OP_N4CLIP_MM_HELPER(mn4clipu_d_mm, false, get_elem_d, set_elem_h, 3)


static inline void mmext_n4clip_mv(void *md, void *ms1, void *ms2, target_ulong s1,
                                   CPURISCVState *env, mmext_get_elem * get_elem,
                                   mmext_set_elem * set_elem, bool use_signed,
                                   uint8_t esz){
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit;
    uint8_t round;

    for (i = 0; i < get_mrows(env); i++) {
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
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_OP_N4CLIP_MV_HELPER(insn, use_signed, get_elem, set_elem, ESZ)  \
void HELPER(insn)(void *md, void *ms1, void *ms2, target_ulong s1,          \
                  CPURISCVState *env){                                      \
    mmext_n4clip_mv(md, ms1, ms2, s1, env,                                  \
                    get_elem, set_elem, use_signed, ESZ);                   \
}

GEN_OP_N4CLIP_MV_HELPER(mn4clip_s_mv_x,  true,  get_elem_s, set_elem_b, 2)
GEN_OP_N4CLIP_MV_HELPER(mn4clipu_s_mv_x, false, get_elem_s, set_elem_b, 2)
GEN_OP_N4CLIP_MV_HELPER(mn4clip_d_mv_x,  true,  get_elem_d, set_elem_h, 3)
GEN_OP_N4CLIP_MV_HELPER(mn4clipu_d_mv_x, false, get_elem_d, set_elem_h, 3)

GEN_OP_N4CLIP_MV_HELPER(mn4clip_s_mv_i,  true,  get_elem_s, set_elem_b, 2)
GEN_OP_N4CLIP_MV_HELPER(mn4clipu_s_mv_i, false, get_elem_s, set_elem_b, 2)
GEN_OP_N4CLIP_MV_HELPER(mn4clip_d_mv_i,  true,  get_elem_d, set_elem_h, 3)
GEN_OP_N4CLIP_MV_HELPER(mn4clipu_d_mv_i, false, get_elem_d, set_elem_h, 3)

static inline void mmext_n4clip_mx(void *md, void *ms2, target_ulong s1,
                                   CPURISCVState *env, mmext_get_elem * get_elem,
                                   mmext_set_elem * set_elem, bool use_signed,
                                   uint8_t esz){
    uint32_t i, k;
    uint32_t cols = get_rlenb(env) >> esz;
    int64_t result;
    uint64_t n_bit;
    uint8_t round;

    for (i = 0; i < get_mrows(env); i++) {
        for (k = 0; k < cols; k++) {
            if (i < env->sizem && k < (env->sizek >> esz)) {
                if (esz == 2) {
                    n_bit = (uint64_t) (s1 & 0x1F);
                } else {
                    n_bit = (uint64_t) (s1 & 0x3F);
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
                set_elem(md, i, k, env, result);
            } else {
                set_elem(md, i, k, env, 0);
            }
        }
    }
}

#define GEN_OP_N4CLIP_MX_HELPER(insn, use_signed, get_elem, set_elem, ESZ)  \
void HELPER(insn)(void *md, void *ms2, target_ulong s1,                     \
                  CPURISCVState *env){                                      \
    mmext_n4clip_mx(md, ms2, s1, env,                                       \
                    get_elem, set_elem, use_signed, ESZ);                   \
}

GEN_OP_N4CLIP_MX_HELPER(mn4clip_s_mx,  true,  get_elem_s, set_elem_b, 2)
GEN_OP_N4CLIP_MX_HELPER(mn4clipu_s_mx, false, get_elem_s, set_elem_b, 2)
GEN_OP_N4CLIP_MX_HELPER(mn4clip_d_mx,  true,  get_elem_d, set_elem_h, 3)
GEN_OP_N4CLIP_MX_HELPER(mn4clipu_d_mx, false, get_elem_d, set_elem_h, 3)

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
    return sum + (uint16_t) a * (uint16_t) b;
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

/* fmmacc instructions */
static uint16_t fmacc16(uint16_t a, uint16_t b, uint16_t d, float_status * s)
{
    return float16_muladd(a, b, d, 0, s);
}

static uint16_t fmaccbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s)
{
    return bfloat16_muladd(a, b, d, 0, s);
}

static uint32_t fmacc32(uint32_t a, uint32_t b, uint32_t d, float_status *s)
{
    return float32_muladd(a, b, d, 0, s);
}

static uint64_t fmacc64(uint64_t a, uint64_t b, uint64_t d, float_status *s)
{
    return float64_muladd(a, b, d, 0, s);
}


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
                    temp = fmaccbf16(oprd_a, oprd_b, temp, &env->fp_status);
                } else {
                    temp = fmacc16(oprd_a, oprd_b, temp, &env->fp_status);
                }
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_h(md, i, j, env);
                if (use_bf16) {
                    psum = bfloat16_add(psum, temp, &env->fp_status);
                } else {
                    psum = float16_add(psum, temp, &env->fp_status);
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
                temp = fmacc32(oprd_a, oprd_b, temp, &env->fp_status);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_s(md, i, j, env);
                psum = float32_add(psum, temp, &env->fp_status);
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
                temp = fmacc64(oprd_a, oprd_b, temp, &env->fp_status);
            }
            if (j >= (get_mrows(env) >> 1)) {
                if (i <= env->sizem && j <= env->sizen) {
                    psum = get_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1), env);
                    psum = float64_add(psum, temp, &env->fp_status);
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, psum);
                } else {
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, 0);
                }
            } else {
                if (i < env->sizem && j < env->sizen) {
                    psum = get_elem_d(md_pair_1, i, j, env);
                    psum = float64_add(psum, temp, &env->fp_status);
                    set_elem_d(md, i, j, env, psum);
                } else {
                    set_elem_d(md, i, j, env, 0);
                }
            }
        }
    }
}

/* fwmacc instructions */
static uint32_t fwmacc16(uint16_t a, uint16_t b, uint32_t d, float_status *s)
{
    return float32_muladd(float16_to_float32(a, true, s),
                          float16_to_float32(b, true, s), d, 0, s);
}

static uint64_t fwmacc32(uint32_t a, uint32_t b, uint64_t d, float_status *s)
{
    return float64_muladd(float32_to_float64(a, s),
                          float32_to_float64(b, s), d, 0, s);
}

void helper_fwmmacc_h(void *md, void *ms1, void *ms2,
                      CPURISCVState *env){
    uint32_t i, j, k;
    uint32_t temp, psum;
    uint16_t oprd_a, oprd_b;
    for (i = 0; i < get_mrows(env); i++) {
        for (j = 0; j < get_mrows(env); j++) {
            temp = 0;
            for (k = 0; k < (env->sizek >> 1); k++) {
                oprd_a = get_elem_h(ms1, i, k, env);
                oprd_b = get_elem_h(ms2, j, k, env);
                temp = fwmacc16(oprd_a, oprd_b, temp, &env->fp_status);
            }
            if (i < env->sizem && j < env->sizen) {
                psum = get_elem_s(md, i, j, env);
                psum = float32_add(psum, temp, &env->fp_status);
                set_elem_s(md, i, j, env, psum);
            } else {
                set_elem_s(md, i, j, env, 0);
            }
        }
    }
}

void helper_fwmmacc_s(void *md, void *ms1, void *ms2,
                      CPURISCVState *env){
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
                temp = fwmacc32(oprd_a, oprd_b, temp, &env->fp_status);
            }
            if (j >= (get_mrows(env) >> 1)) {
                if (i < env->sizem && j < env->sizen) {
                    psum = get_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1), env);
                    psum = float64_add(psum, temp, &env->fp_status);
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, psum);
                } else {
                    set_elem_d(md_pair_2, i, j % (get_mrows(env) >> 1),
                               env, 0);
                }
            } else {
                if (i < env->sizem && j < env->sizen) {
                    psum = get_elem_d(md_pair_1, i, j, env);
                    psum = float64_add(psum, temp, &env->fp_status);
                    set_elem_d(md_pair_1, i, j, env, psum);
                } else {
                    set_elem_d(md_pair_1, i, j, env, 0);
                }
            }
        }
    }
}

/* load/store instructions */
static void probe_pages(CPURISCVState *env, target_ulong addr,
                        target_ulong len, uintptr_t ra,
                        MMUAccessType access_type)
{
    target_ulong pagelen = -(addr | TARGET_PAGE_MASK);
    target_ulong curlen = MIN(pagelen, len);

    probe_access(env, addr, curlen, access_type,
                 cpu_mmu_index(env, false), ra);
    if (len > curlen) {
        addr += curlen;
        curlen = len - curlen;
        probe_access(env, addr, curlen, access_type,
                     cpu_mmu_index(env, false), ra);
    }
}

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

    for (n = 0; n < nf; n++) {
        for (i = 0; i < get_mrows(env); i++) {
            addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i;
            probe_pages(env, addr, get_rlenb(env), ra,
                        MMU_DATA_LOAD);
        }
    }

    for (n = 0; n < nf; n++) {
        md = (void *)((char *) md + n * get_mlenb(env));
        for (i = 0; i < get_mrows(env); i++) {
            for (k = 0; k < (get_rlenb(env) >> esz); k++) {
                addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i + k * (1 << esz);
                set_elem(md, i, k, env, ld_elem(env, addr, ra));
            }
        }
    }

    if (gen_mem_trace()) {
        uint32_t packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        for (n = 0; n < nf; n++) {
            for (i = 0; i < get_mrows(env); i++) {
                target_ulong row_start_addr = rs1 + n * get_mlenb(env) +
                                              get_rlenb(env) * i;
                write_trace_8_8(DATA_RADDR, packlen, get_rlenb(env), row_start_addr);
                for (k = 0; k < get_rlenb(env) / 4; k++) {
                    uint32_t data_value = get_elem_s(md, i, k, env);
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

    for (n = 0; n < nf; n++) {
        for (i = 0; i < get_mrows(env); i++) {
            addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i;
            probe_pages(env, addr, get_rlenb(env), ra,
                        MMU_DATA_STORE);
        }
    }

    for (n = 0; n < nf; n++) {
        ms3 = (void *)((char *) ms3 + n * get_mlenb(env));
        for (i = 0; i < get_mrows(env); i++) {
            for (k = 0; k < (get_rlenb(env) >> esz); k++) {
                addr = rs1 + n * get_mlenb(env) + get_rlenb(env) * i + k * (1 << esz);
                st_elem(env, addr, get_elem(ms3, i, k, env), ra);
            }
        }
    }

    if (gen_mem_trace()) {
        uint32_t packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        for (n = 0; n < nf; n++) {
            for (i = 0; i < get_mrows(env); i++) {
                target_ulong row_start_addr = rs1 + n * get_mlenb(env) +
                                              get_rlenb(env) * i;
                write_trace_8_8(DATA_WADDR, packlen, get_rlenb(env), row_start_addr);
                for (k = 0; k < get_rlenb(env) / 4; k++) {
                    uint32_t data_value = get_elem_s(ms3, i, k, env);
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
