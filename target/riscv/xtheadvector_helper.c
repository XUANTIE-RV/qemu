/*
 * RISC-V XTheadVector Extension Helpers for QEMU.
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
#include <math.h>

/* Different desc encoding need different parse functions */
static inline uint32_t th_nf(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA_TH, NF);
}

static inline uint32_t th_mlen(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA_TH, MLEN);
}

static inline uint32_t th_vm(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA_TH, VM);
}

static inline uint32_t th_lmul(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA_TH, LMUL);
}

static uint32_t th_wd(uint32_t desc)
{
    return (simd_data(desc) >> 11) & 0x1;
}

/*
 * Get vector group length in bytes. Its range is [64, 2048].
 *
 * As simd_desc support at most 256, the max vlen is 512 bits.
 * So vlen in bytes is encoded as maxsz.
 *
 * XtheadVector diff from RVV1.0 is that TH not have fractional lmul and emul.
 */
static inline uint32_t th_maxsz(uint32_t desc)
{
    return simd_maxsz(desc) << th_lmul(desc);
}

/* XTheadVector need to clear the tail elements */
#if HOST_BIG_ENDIAN
static void th_clear(void *tail, uint32_t cnt, uint32_t tot)
{
    /*
     * Split the remaining range to two parts.
     * The first part is in the last uint64_t unit.
     * The second part start from the next uint64_t unit.
     */
    int part1 = 0, part2 = tot - cnt;
    if (cnt % 8) {
        part1 = 8 - (cnt % 8);
        part2 = tot - cnt - part1;
        memset(QEMU_ALIGN_PTR_DOWN(tail, 8), 0, part1);
        memset(QEMU_ALIGN_PTR_UP(tail, 8), 0, part2);
    } else {
        memset(tail, 0, part2);
    }
}
#else
static void th_clear(void *tail, uint32_t cnt, uint32_t tot)
{
    memset(tail, 0, tot - cnt);
}
#endif

static void clearb_th(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot)
{
    int8_t *cur = ((int8_t *)vd + H1(idx));
    th_clear(cur, cnt, tot);
}

static void clearh_th(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot)
{
    int16_t *cur = ((int16_t *)vd + H2(idx));
    th_clear(cur, cnt, tot);
}

static void clearl_th(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot)
{
    int32_t *cur = ((int32_t *)vd + H4(idx));
    th_clear(cur, cnt, tot);
}

static void clearq_th(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot)
{
    int64_t *cur = (int64_t *)vd + idx;
    th_clear(cur, cnt, tot);
}

/*
 * XTheadVector has different mask layout.
 *
 * The mask bits for element i are located in
 * bits [MLEN*i+(MLEN-1) : MLEN*i] of the mask register.
 *
 * MLEN = SEW/LMUL
 */
static inline int th_elem_mask(void *v0, int mlen, int index)
{
    int idx = (index * mlen) / 64;
    int pos = (index * mlen) % 64;
    return (((uint64_t *)v0)[idx] >> pos) & 1;
}

static inline void th_set_elem_mask(void *v0, int mlen, int index,
                                    uint8_t value)
{
    int idx = (index * mlen) / 64;
    int pos = (index * mlen) % 64;
    uint64_t old = ((uint64_t *)v0)[idx];
    ((uint64_t *)v0)[idx] = deposit64(old, pos, mlen, value);
}

/* elements operations for load and store */
typedef void th_ldst_elem_fn(CPURISCVState *env, abi_ptr addr,
                             uint32_t idx, void *vd, uintptr_t retaddr);

/*
 * GEN_TH_LD_ELEM is almost the copy of to GEN_VEXT_LD_ELEM, except that
 * we add "MTYPE data" to deal with zero/sign-extended.
 *
 * For XTheadVector, mem access width is determined by the instruction,
 * while the reg element size equals SEW, therefore mem access width may
 * not equal reg element size. For example, ldb_d means load 8-bit data
 * and sign-extended to 64-bit vector element.
 * As for RVV1.0, mem access width always equals reg element size.
 *
 * So we need to deal with zero/sign-extended in addition.
 */

#define GEN_TH_LD_ELEM(NAME, MTYPE, ETYPE, H, LDSUF)       \
static void NAME(CPURISCVState *env, abi_ptr addr,         \
                 uint32_t idx, void *vd, uintptr_t retaddr)\
{                                                          \
    MTYPE data;                                            \
    ETYPE *cur = ((ETYPE *)vd + H(idx));                   \
    data = cpu_##LDSUF##_data_ra(env, addr, retaddr);      \
    *cur = data;                                           \
}                                                          \

GEN_TH_LD_ELEM(ldb_b, int8_t,  int8_t,  H1, ldsb)
GEN_TH_LD_ELEM(ldb_h, int8_t,  int16_t, H2, ldsb)
GEN_TH_LD_ELEM(ldb_w, int8_t,  int32_t, H4, ldsb)
GEN_TH_LD_ELEM(ldb_d, int8_t,  int64_t, H8, ldsb)
GEN_TH_LD_ELEM(ldh_h, int16_t, int16_t, H2, ldsw)
GEN_TH_LD_ELEM(ldh_w, int16_t, int32_t, H4, ldsw)
GEN_TH_LD_ELEM(ldh_d, int16_t, int64_t, H8, ldsw)
GEN_TH_LD_ELEM(ldw_w, int32_t, int32_t, H4, ldl)
GEN_TH_LD_ELEM(ldw_d, int32_t, int64_t, H8, ldl)
GEN_TH_LD_ELEM(lde_b, int8_t,  int8_t,  H1, ldsb)
GEN_TH_LD_ELEM(lde_h, int16_t, int16_t, H2, ldsw)
GEN_TH_LD_ELEM(lde_w, int32_t, int32_t, H4, ldl)
GEN_TH_LD_ELEM(lde_d, int64_t, int64_t, H8, ldq)
GEN_TH_LD_ELEM(ldbu_b, uint8_t,  uint8_t,  H1, ldub)
GEN_TH_LD_ELEM(ldbu_h, uint8_t,  uint16_t, H2, ldub)
GEN_TH_LD_ELEM(ldbu_w, uint8_t,  uint32_t, H4, ldub)
GEN_TH_LD_ELEM(ldbu_d, uint8_t,  uint64_t, H8, ldub)
GEN_TH_LD_ELEM(ldhu_h, uint16_t, uint16_t, H2, lduw)
GEN_TH_LD_ELEM(ldhu_w, uint16_t, uint32_t, H4, lduw)
GEN_TH_LD_ELEM(ldhu_d, uint16_t, uint64_t, H8, lduw)
GEN_TH_LD_ELEM(ldwu_w, uint32_t, uint32_t, H4, ldl)
GEN_TH_LD_ELEM(ldwu_d, uint32_t, uint64_t, H8, ldl)

#define GEN_TH_ST_ELEM(NAME, ETYPE, H, STSUF)            \
static void NAME(CPURISCVState *env, abi_ptr addr,         \
                 uint32_t idx, void *vd, uintptr_t retaddr)\
{                                                          \
    ETYPE data = *((ETYPE *)vd + H(idx));                  \
    cpu_##STSUF##_data_ra(env, addr, data, retaddr);       \
}

GEN_TH_ST_ELEM(stb_b, int8_t,  H1, stb)
GEN_TH_ST_ELEM(stb_h, int16_t, H2, stb)
GEN_TH_ST_ELEM(stb_w, int32_t, H4, stb)
GEN_TH_ST_ELEM(stb_d, int64_t, H8, stb)
GEN_TH_ST_ELEM(sth_h, int16_t, H2, stw)
GEN_TH_ST_ELEM(sth_w, int32_t, H4, stw)
GEN_TH_ST_ELEM(sth_d, int64_t, H8, stw)
GEN_TH_ST_ELEM(stw_w, int32_t, H4, stl)
GEN_TH_ST_ELEM(stw_d, int64_t, H8, stl)
GEN_TH_ST_ELEM(ste_b, int8_t,  H1, stb)
GEN_TH_ST_ELEM(ste_h, int16_t, H2, stw)
GEN_TH_ST_ELEM(ste_w, int32_t, H4, stl)
GEN_TH_ST_ELEM(ste_d, int64_t, H8, stq)

/*
 * stride: access vector element from strided memory
 */
typedef void clear_fn(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot);

/*
 * This function is almost the copy of vext_ldst_stride, except:
 * 1) XTheadVector has different mask layout, using th_elem_mask
 *    to get [MLEN*i] bit
 * 2) XTheadVector using different data encoding, using th_ functions
 *    to parse.
 * 3) XTheadVector keep the masked elements value, while RVV1.0 policy is
 *    determined by vma.
 * 4) XTheadVector clear the tail elements, while RVV1.0 policy is to rather
 *    set all bits 1s or keep it, determined by vta.
 */
static void
th_ldst_stride(void *vd, void *v0, target_ulong base,
               target_ulong stride, CPURISCVState *env,
               uint32_t desc, uint32_t vm,
               th_ldst_elem_fn *ldst_elem, clear_fn *clear_elem,
               uint32_t esz, uint32_t msz, uintptr_t ra)
{
    uint32_t i, k;
    uint32_t nf = th_nf(desc);
    uint32_t mlen = th_mlen(desc);
    uint32_t vlmax = th_maxsz(desc) / esz;

    VSTART_CHECK_EARLY_EXIT(env);

    /* do real access */
    for (i = env->vstart; i < env->vl; env->vstart = ++i) {
        k = 0;
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        while (k < nf) {
            target_ulong addr = base + stride * i + k * msz;
            ldst_elem(env, adjust_addr(env, addr), i + k * vlmax, vd, ra);
            k++;
        }
    }
    env->vstart = 0;
    /*
     * clear tail elements
     * clear_elem is NULL when store
     */
    if (clear_elem) {
        for (k = 0; k < nf; k++) {
            clear_elem(vd, env->vl + k * vlmax, env->vl * esz, vlmax * esz);
        }
    }
}

/*
 * GEN_TH_LD_STRIDE is similar to GEN_VEXT_LD_STRIDE
 * just change the function args
 */
#define GEN_TH_LD_STRIDE(NAME, MTYPE, ETYPE, LOAD_FN, CLEAR_FN)         \
void HELPER(NAME)(void *vd, void * v0, target_ulong base,               \
                  target_ulong stride, CPURISCVState *env,              \
                  uint32_t desc)                                        \
{                                                                       \
    uint32_t vm = th_vm(desc);                                          \
    th_ldst_stride(vd, v0, base, stride, env, desc, vm, LOAD_FN,        \
                   CLEAR_FN, sizeof(ETYPE), sizeof(MTYPE), GETPC());    \
}

GEN_TH_LD_STRIDE(th_vlsb_v_b,  int8_t,   int8_t,   ldb_b,  clearb_th)
GEN_TH_LD_STRIDE(th_vlsb_v_h,  int8_t,   int16_t,  ldb_h,  clearh_th)
GEN_TH_LD_STRIDE(th_vlsb_v_w,  int8_t,   int32_t,  ldb_w,  clearl_th)
GEN_TH_LD_STRIDE(th_vlsb_v_d,  int8_t,   int64_t,  ldb_d,  clearq_th)
GEN_TH_LD_STRIDE(th_vlsh_v_h,  int16_t,  int16_t,  ldh_h,  clearh_th)
GEN_TH_LD_STRIDE(th_vlsh_v_w,  int16_t,  int32_t,  ldh_w,  clearl_th)
GEN_TH_LD_STRIDE(th_vlsh_v_d,  int16_t,  int64_t,  ldh_d,  clearq_th)
GEN_TH_LD_STRIDE(th_vlsw_v_w,  int32_t,  int32_t,  ldw_w,  clearl_th)
GEN_TH_LD_STRIDE(th_vlsw_v_d,  int32_t,  int64_t,  ldw_d,  clearq_th)
GEN_TH_LD_STRIDE(th_vlse_v_b,  int8_t,   int8_t,   lde_b,  clearb_th)
GEN_TH_LD_STRIDE(th_vlse_v_h,  int16_t,  int16_t,  lde_h,  clearh_th)
GEN_TH_LD_STRIDE(th_vlse_v_w,  int32_t,  int32_t,  lde_w,  clearl_th)
GEN_TH_LD_STRIDE(th_vlse_v_d,  int64_t,  int64_t,  lde_d,  clearq_th)
GEN_TH_LD_STRIDE(th_vlsbu_v_b, uint8_t,  uint8_t,  ldbu_b, clearb_th)
GEN_TH_LD_STRIDE(th_vlsbu_v_h, uint8_t,  uint16_t, ldbu_h, clearh_th)
GEN_TH_LD_STRIDE(th_vlsbu_v_w, uint8_t,  uint32_t, ldbu_w, clearl_th)
GEN_TH_LD_STRIDE(th_vlsbu_v_d, uint8_t,  uint64_t, ldbu_d, clearq_th)
GEN_TH_LD_STRIDE(th_vlshu_v_h, uint16_t, uint16_t, ldhu_h, clearh_th)
GEN_TH_LD_STRIDE(th_vlshu_v_w, uint16_t, uint32_t, ldhu_w, clearl_th)
GEN_TH_LD_STRIDE(th_vlshu_v_d, uint16_t, uint64_t, ldhu_d, clearq_th)
GEN_TH_LD_STRIDE(th_vlswu_v_w, uint32_t, uint32_t, ldwu_w, clearl_th)
GEN_TH_LD_STRIDE(th_vlswu_v_d, uint32_t, uint64_t, ldwu_d, clearq_th)

/*
 * GEN_TH_ST_STRIDE is similar to GEN_VEXT_ST_STRIDE
 * just change the function name and args
 */
#define GEN_TH_ST_STRIDE(NAME, MTYPE, ETYPE, STORE_FN)                  \
void HELPER(NAME)(void *vd, void *v0, target_ulong base,                \
                  target_ulong stride, CPURISCVState *env,              \
                  uint32_t desc)                                        \
{                                                                       \
    uint32_t vm = th_vm(desc);                                          \
    th_ldst_stride(vd, v0, base, stride, env, desc, vm, STORE_FN,       \
                   NULL, sizeof(ETYPE), sizeof(MTYPE), GETPC());        \
}

GEN_TH_ST_STRIDE(th_vssb_v_b, int8_t,  int8_t,  stb_b)
GEN_TH_ST_STRIDE(th_vssb_v_h, int8_t,  int16_t, stb_h)
GEN_TH_ST_STRIDE(th_vssb_v_w, int8_t,  int32_t, stb_w)
GEN_TH_ST_STRIDE(th_vssb_v_d, int8_t,  int64_t, stb_d)
GEN_TH_ST_STRIDE(th_vssh_v_h, int16_t, int16_t, sth_h)
GEN_TH_ST_STRIDE(th_vssh_v_w, int16_t, int32_t, sth_w)
GEN_TH_ST_STRIDE(th_vssh_v_d, int16_t, int64_t, sth_d)
GEN_TH_ST_STRIDE(th_vssw_v_w, int32_t, int32_t, stw_w)
GEN_TH_ST_STRIDE(th_vssw_v_d, int32_t, int64_t, stw_d)
GEN_TH_ST_STRIDE(th_vsse_v_b, int8_t,  int8_t,  ste_b)
GEN_TH_ST_STRIDE(th_vsse_v_h, int16_t, int16_t, ste_h)
GEN_TH_ST_STRIDE(th_vsse_v_w, int32_t, int32_t, ste_w)
GEN_TH_ST_STRIDE(th_vsse_v_d, int64_t, int64_t, ste_d)

/*
 * unit-stride: access elements stored contiguously in memory
 */

/*
 * unmasked unit-stride load and store operation
 *
 * This function is almost the copy of vext_ldst_us, except:
 * 1) different mask layout
 * 2) different data encoding
 * 3) different the tail elements process policy
 */
static void
th_ldst_us(void *vd, target_ulong base, CPURISCVState *env, uint32_t desc,
           th_ldst_elem_fn *ldst_elem, clear_fn *clear_elem,
           uint32_t esz, uint32_t msz, uintptr_t ra)
{
    uint32_t i, k;
    uint32_t nf = th_nf(desc);
    uint32_t vlmax = th_maxsz(desc) / esz;

    VSTART_CHECK_EARLY_EXIT(env);

    /* load bytes from guest memory */
    for (i = env->vstart; i < env->vl; env->vstart = ++i) {
        k = 0;
        while (k < nf) {
            target_ulong addr = base + (i * nf + k) * msz;
            ldst_elem(env, adjust_addr(env, addr), i + k * vlmax, vd, ra);
            k++;
        }
    }
    env->vstart = 0;
    /* clear tail elements */
    if (clear_elem) {
        for (k = 0; k < nf; k++) {
            clear_elem(vd, env->vl + k * vlmax, env->vl * esz, vlmax * esz);
        }
    }
}

/*
 * masked unit-stride load and store operation will be a special case of stride,
 * stride = NF * sizeof (MTYPE)
 *
 * similar to GEN_GEN_VEXT_LD_US, change the function
 */
#define GEN_TH_LD_US(NAME, MTYPE, ETYPE, LOAD_FN, CLEAR_FN)             \
void HELPER(NAME##_mask)(void *vd, void *v0, target_ulong base,         \
                         CPURISCVState *env, uint32_t desc)             \
{                                                                       \
    uint32_t stride = th_nf(desc) * sizeof(MTYPE);                      \
    th_ldst_stride(vd, v0, base, stride, env, desc, false, LOAD_FN,     \
                   CLEAR_FN, sizeof(ETYPE), sizeof(MTYPE), GETPC());    \
}                                                                       \
                                                                        \
void HELPER(NAME)(void *vd, void *v0, target_ulong base,                \
                  CPURISCVState *env, uint32_t desc)                    \
{                                                                       \
    th_ldst_us(vd, base, env, desc, LOAD_FN, CLEAR_FN,                  \
               sizeof(ETYPE), sizeof(MTYPE), GETPC());                  \
}

GEN_TH_LD_US(th_vlb_v_b,  int8_t,   int8_t,   ldb_b,  clearb_th)
GEN_TH_LD_US(th_vlb_v_h,  int8_t,   int16_t,  ldb_h,  clearh_th)
GEN_TH_LD_US(th_vlb_v_w,  int8_t,   int32_t,  ldb_w,  clearl_th)
GEN_TH_LD_US(th_vlb_v_d,  int8_t,   int64_t,  ldb_d,  clearq_th)
GEN_TH_LD_US(th_vlh_v_h,  int16_t,  int16_t,  ldh_h,  clearh_th)
GEN_TH_LD_US(th_vlh_v_w,  int16_t,  int32_t,  ldh_w,  clearl_th)
GEN_TH_LD_US(th_vlh_v_d,  int16_t,  int64_t,  ldh_d,  clearq_th)
GEN_TH_LD_US(th_vlw_v_w,  int32_t,  int32_t,  ldw_w,  clearl_th)
GEN_TH_LD_US(th_vlw_v_d,  int32_t,  int64_t,  ldw_d,  clearq_th)
GEN_TH_LD_US(th_vle_v_b,  int8_t,   int8_t,   lde_b,  clearb_th)
GEN_TH_LD_US(th_vle_v_h,  int16_t,  int16_t,  lde_h,  clearh_th)
GEN_TH_LD_US(th_vle_v_w,  int32_t,  int32_t,  lde_w,  clearl_th)
GEN_TH_LD_US(th_vle_v_d,  int64_t,  int64_t,  lde_d,  clearq_th)
GEN_TH_LD_US(th_vlbu_v_b, uint8_t,  uint8_t,  ldbu_b, clearb_th)
GEN_TH_LD_US(th_vlbu_v_h, uint8_t,  uint16_t, ldbu_h, clearh_th)
GEN_TH_LD_US(th_vlbu_v_w, uint8_t,  uint32_t, ldbu_w, clearl_th)
GEN_TH_LD_US(th_vlbu_v_d, uint8_t,  uint64_t, ldbu_d, clearq_th)
GEN_TH_LD_US(th_vlhu_v_h, uint16_t, uint16_t, ldhu_h, clearh_th)
GEN_TH_LD_US(th_vlhu_v_w, uint16_t, uint32_t, ldhu_w, clearl_th)
GEN_TH_LD_US(th_vlhu_v_d, uint16_t, uint64_t, ldhu_d, clearq_th)
GEN_TH_LD_US(th_vlwu_v_w, uint32_t, uint32_t, ldwu_w, clearl_th)
GEN_TH_LD_US(th_vlwu_v_d, uint32_t, uint64_t, ldwu_d, clearq_th)

/* similar to GEN_GEN_VEXT_ST_US, change the function */
#define GEN_TH_ST_US(NAME, MTYPE, ETYPE, STORE_FN)                      \
void HELPER(NAME##_mask)(void *vd, void *v0, target_ulong base,         \
                         CPURISCVState *env, uint32_t desc)             \
{                                                                       \
    uint32_t stride = th_nf(desc) * sizeof(MTYPE);                      \
    th_ldst_stride(vd, v0, base, stride, env, desc, false, STORE_FN,    \
                   NULL, sizeof(ETYPE), sizeof(MTYPE), GETPC());        \
}                                                                       \
                                                                        \
void HELPER(NAME)(void *vd, void *v0, target_ulong base,                \
                  CPURISCVState *env, uint32_t desc)                    \
{                                                                       \
    th_ldst_us(vd, base, env, desc, STORE_FN, NULL,                     \
               sizeof(ETYPE), sizeof(MTYPE), GETPC());                  \
}

GEN_TH_ST_US(th_vsb_v_b, int8_t,  int8_t , stb_b)
GEN_TH_ST_US(th_vsb_v_h, int8_t,  int16_t, stb_h)
GEN_TH_ST_US(th_vsb_v_w, int8_t,  int32_t, stb_w)
GEN_TH_ST_US(th_vsb_v_d, int8_t,  int64_t, stb_d)
GEN_TH_ST_US(th_vsh_v_h, int16_t, int16_t, sth_h)
GEN_TH_ST_US(th_vsh_v_w, int16_t, int32_t, sth_w)
GEN_TH_ST_US(th_vsh_v_d, int16_t, int64_t, sth_d)
GEN_TH_ST_US(th_vsw_v_w, int32_t, int32_t, stw_w)
GEN_TH_ST_US(th_vsw_v_d, int32_t, int64_t, stw_d)
GEN_TH_ST_US(th_vse_v_b, int8_t,  int8_t , ste_b)
GEN_TH_ST_US(th_vse_v_h, int16_t, int16_t, ste_h)
GEN_TH_ST_US(th_vse_v_w, int32_t, int32_t, ste_w)
GEN_TH_ST_US(th_vse_v_d, int64_t, int64_t, ste_d)

/*
 * index: access vector element from indexed memory
 */
typedef target_ulong th_get_index_addr(target_ulong base,
        uint32_t idx, void *vs2);

/*
 * This function is almost the copy of vext_ldst_index, except:
 * 1) different mask layout
 * 2) different data encoding
 * 3) different mask/tail elements process policy
 */
static inline void
th_ldst_index(void *vd, void *v0, target_ulong base,
              void *vs2, CPURISCVState *env, uint32_t desc,
              th_get_index_addr get_index_addr,
              th_ldst_elem_fn *ldst_elem,
              clear_fn *clear_elem,
              uint32_t esz, uint32_t msz, uintptr_t ra)
{
    uint32_t i, k;
    uint32_t nf = th_nf(desc);
    uint32_t vm = th_vm(desc);
    uint32_t mlen = th_mlen(desc);
    uint32_t vlmax = th_maxsz(desc) / esz;

    VSTART_CHECK_EARLY_EXIT(env);

    /* load bytes from guest memory */
    for (i = env->vstart; i < env->vl; env->vstart = ++i) {
        k = 0;
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        while (k < nf) {
            abi_ptr addr = get_index_addr(base, i, vs2) + k * msz;
            ldst_elem(env, adjust_addr(env, addr), i + k * vlmax, vd, ra);
            k++;
        }
    }
    env->vstart = 0;
    /* clear tail elements */
    if (clear_elem) {
        for (k = 0; k < nf; k++) {
            clear_elem(vd, env->vl + k * vlmax, env->vl * esz, vlmax * esz);
        }
    }
}

/* Similar to GEN_VEXT_LD_INDEX */
#define GEN_TH_LD_INDEX(NAME, MTYPE, ETYPE, INDEX_FN, LOAD_FN, CLEAR_FN)   \
void HELPER(NAME)(void *vd, void *v0, target_ulong base,                   \
                  void *vs2, CPURISCVState *env, uint32_t desc)            \
{                                                                          \
    th_ldst_index(vd, v0, base, vs2, env, desc, INDEX_FN,                  \
                  LOAD_FN, CLEAR_FN, sizeof(ETYPE), sizeof(MTYPE),         \
                  GETPC());                                                \
}

GEN_TH_LD_INDEX(th_vlxb_v_b,  int8_t,   int8_t,   idx_b, ldb_b,  clearb_th)
GEN_TH_LD_INDEX(th_vlxb_v_h,  int8_t,   int16_t,  idx_h, ldb_h,  clearh_th)
GEN_TH_LD_INDEX(th_vlxb_v_w,  int8_t,   int32_t,  idx_w, ldb_w,  clearl_th)
GEN_TH_LD_INDEX(th_vlxb_v_d,  int8_t,   int64_t,  idx_d, ldb_d,  clearq_th)
GEN_TH_LD_INDEX(th_vlxh_v_h,  int16_t,  int16_t,  idx_h, ldh_h,  clearh_th)
GEN_TH_LD_INDEX(th_vlxh_v_w,  int16_t,  int32_t,  idx_w, ldh_w,  clearl_th)
GEN_TH_LD_INDEX(th_vlxh_v_d,  int16_t,  int64_t,  idx_d, ldh_d,  clearq_th)
GEN_TH_LD_INDEX(th_vlxw_v_w,  int32_t,  int32_t,  idx_w, ldw_w,  clearl_th)
GEN_TH_LD_INDEX(th_vlxw_v_d,  int32_t,  int64_t,  idx_d, ldw_d,  clearq_th)
GEN_TH_LD_INDEX(th_vlxe_v_b,  int8_t,   int8_t,   idx_b, lde_b,  clearb_th)
GEN_TH_LD_INDEX(th_vlxe_v_h,  int16_t,  int16_t,  idx_h, lde_h,  clearh_th)
GEN_TH_LD_INDEX(th_vlxe_v_w,  int32_t,  int32_t,  idx_w, lde_w,  clearl_th)
GEN_TH_LD_INDEX(th_vlxe_v_d,  int64_t,  int64_t,  idx_d, lde_d,  clearq_th)
GEN_TH_LD_INDEX(th_vlxbu_v_b, uint8_t,  uint8_t,  idx_b, ldbu_b, clearb_th)
GEN_TH_LD_INDEX(th_vlxbu_v_h, uint8_t,  uint16_t, idx_h, ldbu_h, clearh_th)
GEN_TH_LD_INDEX(th_vlxbu_v_w, uint8_t,  uint32_t, idx_w, ldbu_w, clearl_th)
GEN_TH_LD_INDEX(th_vlxbu_v_d, uint8_t,  uint64_t, idx_d, ldbu_d, clearq_th)
GEN_TH_LD_INDEX(th_vlxhu_v_h, uint16_t, uint16_t, idx_h, ldhu_h, clearh_th)
GEN_TH_LD_INDEX(th_vlxhu_v_w, uint16_t, uint32_t, idx_w, ldhu_w, clearl_th)
GEN_TH_LD_INDEX(th_vlxhu_v_d, uint16_t, uint64_t, idx_d, ldhu_d, clearq_th)
GEN_TH_LD_INDEX(th_vlxwu_v_w, uint32_t, uint32_t, idx_w, ldwu_w, clearl_th)
GEN_TH_LD_INDEX(th_vlxwu_v_d, uint32_t, uint64_t, idx_d, ldwu_d, clearq_th)

/* Similar to GEN_VEXT_ST_INDEX */
#define GEN_TH_ST_INDEX(NAME, MTYPE, ETYPE, INDEX_FN, STORE_FN)  \
void HELPER(NAME)(void *vd, void *v0, target_ulong base,         \
                  void *vs2, CPURISCVState *env, uint32_t desc)  \
{                                                                \
    th_ldst_index(vd, v0, base, vs2, env, desc, INDEX_FN,        \
                  STORE_FN, NULL, sizeof(ETYPE), sizeof(MTYPE),  \
                  GETPC());                                      \
}

GEN_TH_ST_INDEX(th_vsxb_v_b, int8_t,  int8_t,  idx_b, stb_b)
GEN_TH_ST_INDEX(th_vsxb_v_h, int8_t,  int16_t, idx_h, stb_h)
GEN_TH_ST_INDEX(th_vsxb_v_w, int8_t,  int32_t, idx_w, stb_w)
GEN_TH_ST_INDEX(th_vsxb_v_d, int8_t,  int64_t, idx_d, stb_d)
GEN_TH_ST_INDEX(th_vsxh_v_h, int16_t, int16_t, idx_h, sth_h)
GEN_TH_ST_INDEX(th_vsxh_v_w, int16_t, int32_t, idx_w, sth_w)
GEN_TH_ST_INDEX(th_vsxh_v_d, int16_t, int64_t, idx_d, sth_d)
GEN_TH_ST_INDEX(th_vsxw_v_w, int32_t, int32_t, idx_w, stw_w)
GEN_TH_ST_INDEX(th_vsxw_v_d, int32_t, int64_t, idx_d, stw_d)
GEN_TH_ST_INDEX(th_vsxe_v_b, int8_t,  int8_t,  idx_b, ste_b)
GEN_TH_ST_INDEX(th_vsxe_v_h, int16_t, int16_t, idx_h, ste_h)
GEN_TH_ST_INDEX(th_vsxe_v_w, int32_t, int32_t, idx_w, ste_w)
GEN_TH_ST_INDEX(th_vsxe_v_d, int64_t, int64_t, idx_d, ste_d)

/*
 * unit-stride fault-only-first load instructions
 */

/*
 * This function is almost the copy of vext_ldff, except:
 * 1) different mask layout
 * 2) different data encoding
 * 3) different mask/tail elements process policy
 */
static inline void
th_ldff(void *vd, void *v0, target_ulong base,
        CPURISCVState *env, uint32_t desc,
        th_ldst_elem_fn *ldst_elem,
        clear_fn *clear_elem,
        uint32_t esz, uint32_t msz, uintptr_t ra)
{
    void *host;
    uint32_t i, k, vl = 0;
    uint32_t mlen = th_mlen(desc);
    uint32_t nf = th_nf(desc);
    uint32_t vm = th_vm(desc);
    uint32_t vlmax = th_maxsz(desc) / esz;
    target_ulong addr, offset, remain;
    int mmu_index = riscv_env_mmu_index(env, false);

    VSTART_CHECK_EARLY_EXIT(env);
    /* probe every access*/
    for (i = env->vstart; i < env->vl; i++) {
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        addr = adjust_addr(env, base + nf * i * msz);
        if (i == 0) {
            probe_pages(env, addr, nf * msz, ra, MMU_DATA_LOAD);
        } else {
            /* if it triggers an exception, no need to check watchpoint */
            remain = nf * msz;
            while (remain > 0) {
                offset = -(addr | TARGET_PAGE_MASK);
                host = tlb_vaddr_to_host(env, addr, MMU_DATA_LOAD, mmu_index);
                if (host) {
#ifdef CONFIG_USER_ONLY
                    if (!page_check_range(addr, offset, PAGE_READ)) {
                        vl = i;
                        goto ProbeSuccess;
                    }
#else
                    probe_pages(env, addr, offset, ra, MMU_DATA_LOAD);
#endif
                } else {
                    vl = i;
                    goto ProbeSuccess;
                }
                if (remain <=  offset) {
                    break;
                }
                remain -= offset;
                addr = adjust_addr(env, addr + offset);
            }
        }
    }
ProbeSuccess:
    /* load bytes from guest memory */
    if (vl != 0) {
        env->vl = vl;
    }
    for (i = env->vstart; i < env->vl; i++) {
        k = 0;
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        while (k < nf) {
            addr = base + (i * nf + k) * msz;
            ldst_elem(env, adjust_addr(env, addr), i + k * vlmax, vd, ra);
            k++;
        }
    }
    env->vstart = 0;
    /* clear tail elements */
    if (vl != 0) {
        return;
    }
    for (k = 0; k < nf; k++) {
        clear_elem(vd, env->vl + k * vlmax, env->vl * esz, vlmax * esz);
    }
}

#define GEN_TH_LDFF(NAME, MTYPE, ETYPE, LOAD_FN, CLEAR_FN)       \
void HELPER(NAME)(void *vd, void *v0, target_ulong base,         \
                  CPURISCVState *env, uint32_t desc)             \
{                                                                \
    th_ldff(vd, v0, base, env, desc, LOAD_FN, CLEAR_FN,          \
            sizeof(ETYPE), sizeof(MTYPE), GETPC());              \
}

GEN_TH_LDFF(th_vlbff_v_b,  int8_t,   int8_t,   ldb_b,  clearb_th)
GEN_TH_LDFF(th_vlbff_v_h,  int8_t,   int16_t,  ldb_h,  clearh_th)
GEN_TH_LDFF(th_vlbff_v_w,  int8_t,   int32_t,  ldb_w,  clearl_th)
GEN_TH_LDFF(th_vlbff_v_d,  int8_t,   int64_t,  ldb_d,  clearq_th)
GEN_TH_LDFF(th_vlhff_v_h,  int16_t,  int16_t,  ldh_h,  clearh_th)
GEN_TH_LDFF(th_vlhff_v_w,  int16_t,  int32_t,  ldh_w,  clearl_th)
GEN_TH_LDFF(th_vlhff_v_d,  int16_t,  int64_t,  ldh_d,  clearq_th)
GEN_TH_LDFF(th_vlwff_v_w,  int32_t,  int32_t,  ldw_w,  clearl_th)
GEN_TH_LDFF(th_vlwff_v_d,  int32_t,  int64_t,  ldw_d,  clearq_th)
GEN_TH_LDFF(th_vleff_v_b,  int8_t,   int8_t,   lde_b,  clearb_th)
GEN_TH_LDFF(th_vleff_v_h,  int16_t,  int16_t,  lde_h,  clearh_th)
GEN_TH_LDFF(th_vleff_v_w,  int32_t,  int32_t,  lde_w,  clearl_th)
GEN_TH_LDFF(th_vleff_v_d,  int64_t,  int64_t,  lde_d,  clearq_th)
GEN_TH_LDFF(th_vlbuff_v_b, uint8_t,  uint8_t,  ldbu_b, clearb_th)
GEN_TH_LDFF(th_vlbuff_v_h, uint8_t,  uint16_t, ldbu_h, clearh_th)
GEN_TH_LDFF(th_vlbuff_v_w, uint8_t,  uint32_t, ldbu_w, clearl_th)
GEN_TH_LDFF(th_vlbuff_v_d, uint8_t,  uint64_t, ldbu_d, clearq_th)
GEN_TH_LDFF(th_vlhuff_v_h, uint16_t, uint16_t, ldhu_h, clearh_th)
GEN_TH_LDFF(th_vlhuff_v_w, uint16_t, uint32_t, ldhu_w, clearl_th)
GEN_TH_LDFF(th_vlhuff_v_d, uint16_t, uint64_t, ldhu_d, clearq_th)
GEN_TH_LDFF(th_vlwuff_v_w, uint32_t, uint32_t, ldwu_w, clearl_th)
GEN_TH_LDFF(th_vlwuff_v_d, uint32_t, uint64_t, ldwu_d, clearq_th)

/*
 * Vector AMO Operations (Zvamo)
 */
typedef void th_amo_noatomic_fn(void *vs3, target_ulong addr,
                                uint32_t wd, uint32_t idx, CPURISCVState *env,
                                uintptr_t retaddr);

#define TH_SWAP(N, M) (M)
#define TH_XOR(N, M)  (N ^ M)
#define TH_OR(N, M)   (N | M)
#define TH_AND(N, M)  (N & M)
#define TH_ADD(N, M)  (N + M)

#define GEN_TH_AMO_NOATOMIC_OP(NAME, ESZ, MSZ, H, DO_OP, SUF)   \
static void                                                     \
NAME##_noatomic_op(void *vs3, target_ulong addr,                \
                   uint32_t wd, uint32_t idx,                   \
                   CPURISCVState *env, uintptr_t retaddr)       \
{                                                               \
    typedef int##ESZ##_t ETYPE;                                 \
    typedef int##MSZ##_t MTYPE;                                 \
    typedef uint##MSZ##_t UMTYPE __attribute__((unused));       \
    ETYPE *pe3 = (ETYPE *)vs3 + H(idx);                         \
    MTYPE  a = cpu_ld##SUF##_data(env, addr), b = *pe3;         \
                                                                \
    cpu_st##SUF##_data(env, addr, DO_OP(a, b));                 \
    if (wd) {                                                   \
        *pe3 = a;                                               \
    }                                                           \
}

#define TH_MAX(N, M)  ((N) >= (M) ? (N) : (M))
#define TH_MIN(N, M)  ((N) >= (M) ? (M) : (N))
#define TH_MAXU(N, M) TH_MAX((UMTYPE)N, (UMTYPE)M)
#define TH_MINU(N, M) TH_MIN((UMTYPE)N, (UMTYPE)M)

GEN_TH_AMO_NOATOMIC_OP(th_vamoswapw_v_w, 32, 32, H4, TH_SWAP, l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoaddw_v_w,  32, 32, H4, TH_ADD,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoxorw_v_w,  32, 32, H4, TH_XOR,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoandw_v_w,  32, 32, H4, TH_AND,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoorw_v_w,   32, 32, H4, TH_OR,   l)
GEN_TH_AMO_NOATOMIC_OP(th_vamominw_v_w,  32, 32, H4, TH_MIN,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamomaxw_v_w,  32, 32, H4, TH_MAX,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamominuw_v_w, 32, 32, H4, TH_MINU, l)
GEN_TH_AMO_NOATOMIC_OP(th_vamomaxuw_v_w, 32, 32, H4, TH_MAXU, l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoswapw_v_d, 64, 32, H8, TH_SWAP, l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoswapd_v_d, 64, 64, H8, TH_SWAP, q)
GEN_TH_AMO_NOATOMIC_OP(th_vamoaddw_v_d,  64, 32, H8, TH_ADD,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoaddd_v_d,  64, 64, H8, TH_ADD,  q)
GEN_TH_AMO_NOATOMIC_OP(th_vamoxorw_v_d,  64, 32, H8, TH_XOR,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoxord_v_d,  64, 64, H8, TH_XOR,  q)
GEN_TH_AMO_NOATOMIC_OP(th_vamoandw_v_d,  64, 32, H8, TH_AND,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoandd_v_d,  64, 64, H8, TH_AND,  q)
GEN_TH_AMO_NOATOMIC_OP(th_vamoorw_v_d,   64, 32, H8, TH_OR,   l)
GEN_TH_AMO_NOATOMIC_OP(th_vamoord_v_d,   64, 64, H8, TH_OR,   q)
GEN_TH_AMO_NOATOMIC_OP(th_vamominw_v_d,  64, 32, H8, TH_MIN,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamomind_v_d,  64, 64, H8, TH_MIN,  q)
GEN_TH_AMO_NOATOMIC_OP(th_vamomaxw_v_d,  64, 32, H8, TH_MAX,  l)
GEN_TH_AMO_NOATOMIC_OP(th_vamomaxd_v_d,  64, 64, H8, TH_MAX,  q)
GEN_TH_AMO_NOATOMIC_OP(th_vamominuw_v_d, 64, 32, H8, TH_MINU, l)
GEN_TH_AMO_NOATOMIC_OP(th_vamominud_v_d, 64, 64, H8, TH_MINU, q)
GEN_TH_AMO_NOATOMIC_OP(th_vamomaxuw_v_d, 64, 32, H8, TH_MAXU, l)
GEN_TH_AMO_NOATOMIC_OP(th_vamomaxud_v_d, 64, 64, H8, TH_MAXU, q)

static inline void
th_amo_noatomic(void *vs3, void *v0, target_ulong base,
                void *vs2, CPURISCVState *env, uint32_t desc,
                th_get_index_addr get_index_addr,
                th_amo_noatomic_fn * noatomic_op,
                clear_fn * clear_elem,
                uint32_t esz, uint32_t msz, uintptr_t ra)
{
    uint32_t i;
    target_long addr;
    uint32_t wd = th_wd(desc);
    uint32_t vm = th_vm(desc);
    uint32_t mlen = th_mlen(desc);
    uint32_t vlmax = th_maxsz(desc) / esz;
    uint32_t vl = env->vl;

    VSTART_CHECK_EARLY_EXIT(env);

    for (i = env->vstart; i < vl; env->vstart = ++i) {
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        addr = get_index_addr(base, i, vs2);
        noatomic_op(vs3, adjust_addr(env, addr), wd, i, env, ra);
    }
    env->vstart = 0;
    clear_elem(vs3, env->vl, env->vl * esz, vlmax * esz);
}

#define GEN_TH_AMO(NAME, MTYPE, ETYPE, INDEX_FN, CLEAR_FN)      \
void HELPER(NAME)(void *vs3, void *v0, target_ulong base,       \
                  void *vs2, CPURISCVState *env, uint32_t desc) \
{                                                               \
    th_amo_noatomic(vs3, v0, base, vs2, env, desc,              \
                    INDEX_FN, NAME##_noatomic_op,               \
                    CLEAR_FN, sizeof(ETYPE), sizeof(MTYPE),     \
                    GETPC());                                   \
}

GEN_TH_AMO(th_vamoswapw_v_d, int32_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamoswapd_v_d, int64_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamoaddw_v_d,  int32_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamoaddd_v_d,  int64_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamoxorw_v_d,  int32_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamoxord_v_d,  int64_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamoandw_v_d,  int32_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamoandd_v_d,  int64_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamoorw_v_d,   int32_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamoord_v_d,   int64_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamominw_v_d,  int32_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamomind_v_d,  int64_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamomaxw_v_d,  int32_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamomaxd_v_d,  int64_t,  int64_t,  idx_d, clearq_th)
GEN_TH_AMO(th_vamominuw_v_d, uint32_t, uint64_t, idx_d, clearq_th)
GEN_TH_AMO(th_vamominud_v_d, uint64_t, uint64_t, idx_d, clearq_th)
GEN_TH_AMO(th_vamomaxuw_v_d, uint32_t, uint64_t, idx_d, clearq_th)
GEN_TH_AMO(th_vamomaxud_v_d, uint64_t, uint64_t, idx_d, clearq_th)
GEN_TH_AMO(th_vamoswapw_v_w, int32_t,  int32_t,  idx_w, clearl_th)
GEN_TH_AMO(th_vamoaddw_v_w,  int32_t,  int32_t,  idx_w, clearl_th)
GEN_TH_AMO(th_vamoxorw_v_w,  int32_t,  int32_t,  idx_w, clearl_th)
GEN_TH_AMO(th_vamoandw_v_w,  int32_t,  int32_t,  idx_w, clearl_th)
GEN_TH_AMO(th_vamoorw_v_w,   int32_t,  int32_t,  idx_w, clearl_th)
GEN_TH_AMO(th_vamominw_v_w,  int32_t,  int32_t,  idx_w, clearl_th)
GEN_TH_AMO(th_vamomaxw_v_w,  int32_t,  int32_t,  idx_w, clearl_th)
GEN_TH_AMO(th_vamominuw_v_w, uint32_t, uint32_t, idx_w, clearl_th)
GEN_TH_AMO(th_vamomaxuw_v_w, uint32_t, uint32_t, idx_w, clearl_th)

/*
 * Vector Integer Arithmetic Instructions
 */

/* redefine macros to decouple */

#define THCALL(macro, ...)  macro(__VA_ARGS__)

/* operation of two vector elements */
#define opivv2_fn_th opivv2_fn

#define TH_OPIVV2(NAME, TD, T1, T2, TX1, TX2, HD, HS1, HS2, OP) \
        OPIVV2(NAME, TD, T1, T2, TX1, TX2, HD, HS1, HS2, OP)

#define TH_SUB(N, M) (N - M)
#define TH_RSUB(N, M) (M - N)

THCALL(TH_OPIVV2, th_vadd_vv_b, OP_SSS_B, H1, H1, H1, TH_ADD)
THCALL(TH_OPIVV2, th_vadd_vv_h, OP_SSS_H, H2, H2, H2, TH_ADD)
THCALL(TH_OPIVV2, th_vadd_vv_w, OP_SSS_W, H4, H4, H4, TH_ADD)
THCALL(TH_OPIVV2, th_vadd_vv_d, OP_SSS_D, H8, H8, H8, TH_ADD)
THCALL(TH_OPIVV2, th_vsub_vv_b, OP_SSS_B, H1, H1, H1, TH_SUB)
THCALL(TH_OPIVV2, th_vsub_vv_h, OP_SSS_H, H2, H2, H2, TH_SUB)
THCALL(TH_OPIVV2, th_vsub_vv_w, OP_SSS_W, H4, H4, H4, TH_SUB)
THCALL(TH_OPIVV2, th_vsub_vv_d, OP_SSS_D, H8, H8, H8, TH_SUB)

/*
 * This function is almost the copy of do_vext_vv, except:
 * 1) XTheadVector has different mask layout, using th_elem_mask
 *    to get [MLEN*i] bit
 * 2) XTheadVector using different data encoding, using th_ functions
 *    to parse.
 * 3) XTheadVector keep the masked elements value, while RVV1.0 policy is
 *    determined by vma.
 * 4) XTheadVector clear the tail elements, while RVV1.0 policy is to rather
 *    set all bits 1s or keep it, determined by vta.
 */
static void do_th_vv(void *vd, void *v0, void *vs1, void *vs2,
                     CPURISCVState *env, uint32_t desc,
                     uint32_t esz, uint32_t dsz,
                     opivv2_fn_th *fn, clear_fn *clearfn)
{
    uint32_t vlmax = th_maxsz(desc) / esz;
    uint32_t mlen = th_mlen(desc);
    uint32_t vm = th_vm(desc);
    uint32_t vl = env->vl;
    uint32_t i;

    VSTART_CHECK_EARLY_EXIT(env);

    for (i = env->vstart; i < vl; i++) {
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        fn(vd, vs1, vs2, i);
    }
    env->vstart = 0;
    clearfn(vd, vl, vl * dsz,  vlmax * dsz);
}

/* generate the helpers for OPIVV */
#define GEN_TH_VV(NAME, ESZ, DSZ, CLEAR_FN)               \
void HELPER(NAME)(void *vd, void *v0, void *vs1,          \
                  void *vs2, CPURISCVState *env,          \
                  uint32_t desc)                          \
{                                                         \
    do_th_vv(vd, v0, vs1, vs2, env, desc, ESZ, DSZ,       \
             do_##NAME, CLEAR_FN);                        \
}

GEN_TH_VV(th_vadd_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vadd_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vadd_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vadd_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vsub_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vsub_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vsub_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vsub_vv_d, 8, 8, clearq_th)

#define opivx2_fn_th opivx2_fn

/*
 * (T1)s1 gives the real operator type.
 * (TX1)(T1)s1 expands the operator type of widen or narrow operations.
 */
#define TH_OPIVX2(NAME, TD, T1, T2, TX1, TX2, HD, HS2, OP)    \
        OPIVX2(NAME, TD, T1, T2, TX1, TX2, HD, HS2, OP)

THCALL(TH_OPIVX2, th_vadd_vx_b, OP_SSS_B, H1, H1, TH_ADD)
THCALL(TH_OPIVX2, th_vadd_vx_h, OP_SSS_H, H2, H2, TH_ADD)
THCALL(TH_OPIVX2, th_vadd_vx_w, OP_SSS_W, H4, H4, TH_ADD)
THCALL(TH_OPIVX2, th_vadd_vx_d, OP_SSS_D, H8, H8, TH_ADD)
THCALL(TH_OPIVX2, th_vsub_vx_b, OP_SSS_B, H1, H1, TH_SUB)
THCALL(TH_OPIVX2, th_vsub_vx_h, OP_SSS_H, H2, H2, TH_SUB)
THCALL(TH_OPIVX2, th_vsub_vx_w, OP_SSS_W, H4, H4, TH_SUB)
THCALL(TH_OPIVX2, th_vsub_vx_d, OP_SSS_D, H8, H8, TH_SUB)
THCALL(TH_OPIVX2, th_vrsub_vx_b, OP_SSS_B, H1, H1, TH_RSUB)
THCALL(TH_OPIVX2, th_vrsub_vx_h, OP_SSS_H, H2, H2, TH_RSUB)
THCALL(TH_OPIVX2, th_vrsub_vx_w, OP_SSS_W, H4, H4, TH_RSUB)
THCALL(TH_OPIVX2, th_vrsub_vx_d, OP_SSS_D, H8, H8, TH_RSUB)

/*
 * This function is almost the copy of do_vext_vx, except:
 * 1) different mask layout
 * 2) different data encoding
 * 3) different mask/tail elements process policy
 */
static void do_th_vx(void *vd, void *v0, target_long s1, void *vs2,
                     CPURISCVState *env, uint32_t desc,
                     uint32_t esz, uint32_t dsz,
                     opivx2_fn_th fn, clear_fn *clearfn)
{
    uint32_t vlmax = th_maxsz(desc) / esz;
    uint32_t mlen = th_mlen(desc);
    uint32_t vm = th_vm(desc);
    uint32_t vl = env->vl;
    uint32_t i;

    VSTART_CHECK_EARLY_EXIT(env);

    for (i = env->vstart; i < vl; i++) {
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        fn(vd, s1, vs2, i);
    }
    env->vstart = 0;
    clearfn(vd, vl, vl * dsz,  vlmax * dsz);
}

/* generate the helpers for OPIVX */
#define GEN_TH_VX(NAME, ESZ, DSZ, CLEAR_FN)               \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1,    \
                  void *vs2, CPURISCVState *env,          \
                  uint32_t desc)                          \
{                                                         \
    do_th_vx(vd, v0, s1, vs2, env, desc, ESZ, DSZ,        \
             do_##NAME, CLEAR_FN);                        \
}

GEN_TH_VX(th_vadd_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vadd_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vadd_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vadd_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vsub_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vsub_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vsub_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vsub_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vrsub_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vrsub_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vrsub_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vrsub_vx_d, 8, 8, clearq_th)

/* Vector Widening Integer Add/Subtract */

THCALL(TH_OPIVV2, th_vwaddu_vv_b, WOP_UUU_B, H2, H1, H1, TH_ADD)
THCALL(TH_OPIVV2, th_vwaddu_vv_h, WOP_UUU_H, H4, H2, H2, TH_ADD)
THCALL(TH_OPIVV2, th_vwaddu_vv_w, WOP_UUU_W, H8, H4, H4, TH_ADD)
THCALL(TH_OPIVV2, th_vwsubu_vv_b, WOP_UUU_B, H2, H1, H1, TH_SUB)
THCALL(TH_OPIVV2, th_vwsubu_vv_h, WOP_UUU_H, H4, H2, H2, TH_SUB)
THCALL(TH_OPIVV2, th_vwsubu_vv_w, WOP_UUU_W, H8, H4, H4, TH_SUB)
THCALL(TH_OPIVV2, th_vwadd_vv_b, WOP_SSS_B, H2, H1, H1, TH_ADD)
THCALL(TH_OPIVV2, th_vwadd_vv_h, WOP_SSS_H, H4, H2, H2, TH_ADD)
THCALL(TH_OPIVV2, th_vwadd_vv_w, WOP_SSS_W, H8, H4, H4, TH_ADD)
THCALL(TH_OPIVV2, th_vwsub_vv_b, WOP_SSS_B, H2, H1, H1, TH_SUB)
THCALL(TH_OPIVV2, th_vwsub_vv_h, WOP_SSS_H, H4, H2, H2, TH_SUB)
THCALL(TH_OPIVV2, th_vwsub_vv_w, WOP_SSS_W, H8, H4, H4, TH_SUB)
THCALL(TH_OPIVV2, th_vwaddu_wv_b, WOP_WUUU_B, H2, H1, H1, TH_ADD)
THCALL(TH_OPIVV2, th_vwaddu_wv_h, WOP_WUUU_H, H4, H2, H2, TH_ADD)
THCALL(TH_OPIVV2, th_vwaddu_wv_w, WOP_WUUU_W, H8, H4, H4, TH_ADD)
THCALL(TH_OPIVV2, th_vwsubu_wv_b, WOP_WUUU_B, H2, H1, H1, TH_SUB)
THCALL(TH_OPIVV2, th_vwsubu_wv_h, WOP_WUUU_H, H4, H2, H2, TH_SUB)
THCALL(TH_OPIVV2, th_vwsubu_wv_w, WOP_WUUU_W, H8, H4, H4, TH_SUB)
THCALL(TH_OPIVV2, th_vwadd_wv_b, WOP_WSSS_B, H2, H1, H1, TH_ADD)
THCALL(TH_OPIVV2, th_vwadd_wv_h, WOP_WSSS_H, H4, H2, H2, TH_ADD)
THCALL(TH_OPIVV2, th_vwadd_wv_w, WOP_WSSS_W, H8, H4, H4, TH_ADD)
THCALL(TH_OPIVV2, th_vwsub_wv_b, WOP_WSSS_B, H2, H1, H1, TH_SUB)
THCALL(TH_OPIVV2, th_vwsub_wv_h, WOP_WSSS_H, H4, H2, H2, TH_SUB)
THCALL(TH_OPIVV2, th_vwsub_wv_w, WOP_WSSS_W, H8, H4, H4, TH_SUB)
GEN_TH_VV(th_vwaddu_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwaddu_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwaddu_vv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwsubu_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwsubu_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwsubu_vv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwadd_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwadd_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwadd_vv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwsub_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwsub_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwsub_vv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwaddu_wv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwaddu_wv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwaddu_wv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwsubu_wv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwsubu_wv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwsubu_wv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwadd_wv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwadd_wv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwadd_wv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwsub_wv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwsub_wv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwsub_wv_w, 4, 8, clearq_th)

THCALL(TH_OPIVX2, th_vwaddu_vx_b, WOP_UUU_B, H2, H1, TH_ADD)
THCALL(TH_OPIVX2, th_vwaddu_vx_h, WOP_UUU_H, H4, H2, TH_ADD)
THCALL(TH_OPIVX2, th_vwaddu_vx_w, WOP_UUU_W, H8, H4, TH_ADD)
THCALL(TH_OPIVX2, th_vwsubu_vx_b, WOP_UUU_B, H2, H1, TH_SUB)
THCALL(TH_OPIVX2, th_vwsubu_vx_h, WOP_UUU_H, H4, H2, TH_SUB)
THCALL(TH_OPIVX2, th_vwsubu_vx_w, WOP_UUU_W, H8, H4, TH_SUB)
THCALL(TH_OPIVX2, th_vwadd_vx_b, WOP_SSS_B, H2, H1, TH_ADD)
THCALL(TH_OPIVX2, th_vwadd_vx_h, WOP_SSS_H, H4, H2, TH_ADD)
THCALL(TH_OPIVX2, th_vwadd_vx_w, WOP_SSS_W, H8, H4, TH_ADD)
THCALL(TH_OPIVX2, th_vwsub_vx_b, WOP_SSS_B, H2, H1, TH_SUB)
THCALL(TH_OPIVX2, th_vwsub_vx_h, WOP_SSS_H, H4, H2, TH_SUB)
THCALL(TH_OPIVX2, th_vwsub_vx_w, WOP_SSS_W, H8, H4, TH_SUB)
THCALL(TH_OPIVX2, th_vwaddu_wx_b, WOP_WUUU_B, H2, H1, TH_ADD)
THCALL(TH_OPIVX2, th_vwaddu_wx_h, WOP_WUUU_H, H4, H2, TH_ADD)
THCALL(TH_OPIVX2, th_vwaddu_wx_w, WOP_WUUU_W, H8, H4, TH_ADD)
THCALL(TH_OPIVX2, th_vwsubu_wx_b, WOP_WUUU_B, H2, H1, TH_SUB)
THCALL(TH_OPIVX2, th_vwsubu_wx_h, WOP_WUUU_H, H4, H2, TH_SUB)
THCALL(TH_OPIVX2, th_vwsubu_wx_w, WOP_WUUU_W, H8, H4, TH_SUB)
THCALL(TH_OPIVX2, th_vwadd_wx_b, WOP_WSSS_B, H2, H1, TH_ADD)
THCALL(TH_OPIVX2, th_vwadd_wx_h, WOP_WSSS_H, H4, H2, TH_ADD)
THCALL(TH_OPIVX2, th_vwadd_wx_w, WOP_WSSS_W, H8, H4, TH_ADD)
THCALL(TH_OPIVX2, th_vwsub_wx_b, WOP_WSSS_B, H2, H1, TH_SUB)
THCALL(TH_OPIVX2, th_vwsub_wx_h, WOP_WSSS_H, H4, H2, TH_SUB)
THCALL(TH_OPIVX2, th_vwsub_wx_w, WOP_WSSS_W, H8, H4, TH_SUB)
GEN_TH_VX(th_vwaddu_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwaddu_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwaddu_vx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwsubu_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwsubu_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwsubu_vx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwadd_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwadd_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwadd_vx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwsub_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwsub_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwsub_vx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwaddu_wx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwaddu_wx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwaddu_wx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwsubu_wx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwsubu_wx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwsubu_wx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwadd_wx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwadd_wx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwadd_wx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwsub_wx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwsub_wx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwsub_wx_w, 4, 8, clearq_th)

/* Vector Integer Add-with-Carry / Subtract-with-Borrow Instructions */

#define TH_VADC(N, M, C) (N + M + C)
#define TH_VSBC(N, M, C) (N - M - C)
/*
 * This function is almost the copy of GEN_VEXT_VADC_VVM, except:
 * 1) different mask layout
 * 2) different data encoding
 * 3) different tail elements process policy
 */
#define GEN_TH_VADC_VVM(NAME, ETYPE, H, DO_OP, CLEAR_FN)      \
void HELPER(NAME)(void *vd, void *v0, void *vs1, void *vs2,   \
                  CPURISCVState *env, uint32_t desc)          \
{                                                             \
    uint32_t mlen = th_mlen(desc);                            \
    uint32_t vl = env->vl;                                    \
    uint32_t esz = sizeof(ETYPE);                             \
    uint32_t vlmax = th_maxsz(desc) / esz;                    \
    uint32_t i;                                               \
                                                              \
    VSTART_CHECK_EARLY_EXIT(env);                             \
    for (i = env->vstart; i < vl; i++) {                      \
        ETYPE s1 = *((ETYPE *)vs1 + H(i));                    \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                    \
        uint8_t carry = th_elem_mask(v0, mlen, i);            \
                                                              \
        *((ETYPE *)vd + H(i)) = DO_OP(s2, s1, carry);         \
    }                                                         \
    env->vstart = 0;                                          \
    CLEAR_FN(vd, vl, vl * esz, vlmax * esz);                  \
}

GEN_TH_VADC_VVM(th_vadc_vvm_b, uint8_t,  H1, TH_VADC, clearb_th)
GEN_TH_VADC_VVM(th_vadc_vvm_h, uint16_t, H2, TH_VADC, clearh_th)
GEN_TH_VADC_VVM(th_vadc_vvm_w, uint32_t, H4, TH_VADC, clearl_th)
GEN_TH_VADC_VVM(th_vadc_vvm_d, uint64_t, H8, TH_VADC, clearq_th)

GEN_TH_VADC_VVM(th_vsbc_vvm_b, uint8_t,  H1, TH_VSBC, clearb_th)
GEN_TH_VADC_VVM(th_vsbc_vvm_h, uint16_t, H2, TH_VSBC, clearh_th)
GEN_TH_VADC_VVM(th_vsbc_vvm_w, uint32_t, H4, TH_VSBC, clearl_th)
GEN_TH_VADC_VVM(th_vsbc_vvm_d, uint64_t, H8, TH_VSBC, clearq_th)
/*
 * This function is almost the copy of GEN_VEXT_VADC_VXM, except:
 * 1) different mask layout
 * 2) different data encoding
 * 3) different tail elements process policy
 */
#define GEN_TH_VADC_VXM(NAME, ETYPE, H, DO_OP, CLEAR_FN)                 \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1, void *vs2,        \
                  CPURISCVState *env, uint32_t desc)                     \
{                                                                        \
    uint32_t mlen = th_mlen(desc);                                       \
    uint32_t vl = env->vl;                                               \
    uint32_t esz = sizeof(ETYPE);                                        \
    uint32_t vlmax = th_maxsz(desc) / esz;                               \
    uint32_t i;                                                          \
                                                                         \
    VSTART_CHECK_EARLY_EXIT(env);                                        \
    for (i = env->vstart; i < vl; i++) {                                 \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                               \
        uint8_t carry = th_elem_mask(v0, mlen, i);                       \
                                                                         \
        *((ETYPE *)vd + H(i)) = DO_OP(s2, (ETYPE)(target_long)s1, carry);\
    }                                                                    \
    env->vstart = 0;                                                     \
    CLEAR_FN(vd, vl, vl * esz, vlmax * esz);                             \
}

GEN_TH_VADC_VXM(th_vadc_vxm_b, uint8_t,  H1, TH_VADC, clearb_th)
GEN_TH_VADC_VXM(th_vadc_vxm_h, uint16_t, H2, TH_VADC, clearh_th)
GEN_TH_VADC_VXM(th_vadc_vxm_w, uint32_t, H4, TH_VADC, clearl_th)
GEN_TH_VADC_VXM(th_vadc_vxm_d, uint64_t, H8, TH_VADC, clearq_th)

GEN_TH_VADC_VXM(th_vsbc_vxm_b, uint8_t,  H1, TH_VSBC, clearb_th)
GEN_TH_VADC_VXM(th_vsbc_vxm_h, uint16_t, H2, TH_VSBC, clearh_th)
GEN_TH_VADC_VXM(th_vsbc_vxm_w, uint32_t, H4, TH_VSBC, clearl_th)
GEN_TH_VADC_VXM(th_vsbc_vxm_d, uint64_t, H8, TH_VSBC, clearq_th)

#define TH_MADC(N, M, C) (C ? (__typeof(N))(N + M + 1) <= N :           \
                          (__typeof(N))(N + M) < N)
#define TH_MSBC(N, M, C) (C ? N <= M : N < M)
/*
 * This function is almost the copy of GEN_VEXT_VMADC_VVM, except:
 * 1) different mask layout
 * 2) different data encoding
 * 3) different tail elements process policy
 * 4) When vm = 1, RVV1.0 vmadc and vmsbc perform the computation
 *    without carry-in/borrow-in. While XTheadVector does not have
 *    this kind of situation.
 */
#define GEN_TH_VMADC_VVM(NAME, ETYPE, H, DO_OP)               \
void HELPER(NAME)(void *vd, void *v0, void *vs1, void *vs2,   \
                  CPURISCVState *env, uint32_t desc)          \
{                                                             \
    uint32_t mlen = th_mlen(desc);                            \
    uint32_t vl = env->vl;                                    \
    uint32_t vlmax = th_maxsz(desc) / sizeof(ETYPE);          \
    uint32_t i;                                               \
                                                              \
    VSTART_CHECK_EARLY_EXIT(env);                             \
    for (i = env->vstart; i < vl; i++) {                      \
        ETYPE s1 = *((ETYPE *)vs1 + H(i));                    \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                    \
        uint8_t carry = th_elem_mask(v0, mlen, i);            \
                                                              \
        th_set_elem_mask(vd, mlen, i, DO_OP(s2, s1, carry));  \
    }                                                         \
    env->vstart = 0;                                          \
    for (; i < vlmax; i++) {                                  \
        th_set_elem_mask(vd, mlen, i, 0);                     \
    }                                                         \
}

GEN_TH_VMADC_VVM(th_vmadc_vvm_b, uint8_t,  H1, TH_MADC)
GEN_TH_VMADC_VVM(th_vmadc_vvm_h, uint16_t, H2, TH_MADC)
GEN_TH_VMADC_VVM(th_vmadc_vvm_w, uint32_t, H4, TH_MADC)
GEN_TH_VMADC_VVM(th_vmadc_vvm_d, uint64_t, H8, TH_MADC)

GEN_TH_VMADC_VVM(th_vmsbc_vvm_b, uint8_t,  H1, TH_MSBC)
GEN_TH_VMADC_VVM(th_vmsbc_vvm_h, uint16_t, H2, TH_MSBC)
GEN_TH_VMADC_VVM(th_vmsbc_vvm_w, uint32_t, H4, TH_MSBC)
GEN_TH_VMADC_VVM(th_vmsbc_vvm_d, uint64_t, H8, TH_MSBC)
/*
 * This function is almost the copy of GEN_VEXT_VMADC_VXM, except:
 * 1) different mask layout
 * 2) different data encoding
 * 3) different tail elements process policy
 * 4) When vm = 1, RVV1.0 vmadc and vmsbc perform the computation
 *    without carry-in/borrow-in. While XTheadVector does not have
 *    this kind of situation.
 */
#define GEN_TH_VMADC_VXM(NAME, ETYPE, H, DO_OP)                 \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1,          \
                  void *vs2, CPURISCVState *env, uint32_t desc) \
{                                                               \
    uint32_t mlen = th_mlen(desc);                              \
    uint32_t vl = env->vl;                                      \
    uint32_t vlmax = th_maxsz(desc) / sizeof(ETYPE);            \
    uint32_t i;                                                 \
                                                                \
    VSTART_CHECK_EARLY_EXIT(env);                               \
    for (i = env->vstart; i < vl; i++) {                        \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                      \
        uint8_t carry = th_elem_mask(v0, mlen, i);              \
                                                                \
        th_set_elem_mask(vd, mlen, i,                           \
                DO_OP(s2, (ETYPE)(target_long)s1, carry));      \
    }                                                           \
    env->vstart = 0;                                            \
    for (; i < vlmax; i++) {                                    \
        th_set_elem_mask(vd, mlen, i, 0);                       \
    }                                                           \
}

GEN_TH_VMADC_VXM(th_vmadc_vxm_b, uint8_t,  H1, TH_MADC)
GEN_TH_VMADC_VXM(th_vmadc_vxm_h, uint16_t, H2, TH_MADC)
GEN_TH_VMADC_VXM(th_vmadc_vxm_w, uint32_t, H4, TH_MADC)
GEN_TH_VMADC_VXM(th_vmadc_vxm_d, uint64_t, H8, TH_MADC)

GEN_TH_VMADC_VXM(th_vmsbc_vxm_b, uint8_t,  H1, TH_MSBC)
GEN_TH_VMADC_VXM(th_vmsbc_vxm_h, uint16_t, H2, TH_MSBC)
GEN_TH_VMADC_VXM(th_vmsbc_vxm_w, uint32_t, H4, TH_MSBC)
GEN_TH_VMADC_VXM(th_vmsbc_vxm_d, uint64_t, H8, TH_MSBC)

/* Vector Bitwise Logical Instructions */
THCALL(TH_OPIVV2, th_vand_vv_b, OP_SSS_B, H1, H1, H1, TH_AND)
THCALL(TH_OPIVV2, th_vand_vv_h, OP_SSS_H, H2, H2, H2, TH_AND)
THCALL(TH_OPIVV2, th_vand_vv_w, OP_SSS_W, H4, H4, H4, TH_AND)
THCALL(TH_OPIVV2, th_vand_vv_d, OP_SSS_D, H8, H8, H8, TH_AND)
THCALL(TH_OPIVV2, th_vor_vv_b, OP_SSS_B, H1, H1, H1, TH_OR)
THCALL(TH_OPIVV2, th_vor_vv_h, OP_SSS_H, H2, H2, H2, TH_OR)
THCALL(TH_OPIVV2, th_vor_vv_w, OP_SSS_W, H4, H4, H4, TH_OR)
THCALL(TH_OPIVV2, th_vor_vv_d, OP_SSS_D, H8, H8, H8, TH_OR)
THCALL(TH_OPIVV2, th_vxor_vv_b, OP_SSS_B, H1, H1, H1, TH_XOR)
THCALL(TH_OPIVV2, th_vxor_vv_h, OP_SSS_H, H2, H2, H2, TH_XOR)
THCALL(TH_OPIVV2, th_vxor_vv_w, OP_SSS_W, H4, H4, H4, TH_XOR)
THCALL(TH_OPIVV2, th_vxor_vv_d, OP_SSS_D, H8, H8, H8, TH_XOR)
GEN_TH_VV(th_vand_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vand_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vand_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vand_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vor_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vor_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vor_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vor_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vxor_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vxor_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vxor_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vxor_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2, th_vand_vx_b, OP_SSS_B, H1, H1, TH_AND)
THCALL(TH_OPIVX2, th_vand_vx_h, OP_SSS_H, H2, H2, TH_AND)
THCALL(TH_OPIVX2, th_vand_vx_w, OP_SSS_W, H4, H4, TH_AND)
THCALL(TH_OPIVX2, th_vand_vx_d, OP_SSS_D, H8, H8, TH_AND)
THCALL(TH_OPIVX2, th_vor_vx_b, OP_SSS_B, H1, H1, TH_OR)
THCALL(TH_OPIVX2, th_vor_vx_h, OP_SSS_H, H2, H2, TH_OR)
THCALL(TH_OPIVX2, th_vor_vx_w, OP_SSS_W, H4, H4, TH_OR)
THCALL(TH_OPIVX2, th_vor_vx_d, OP_SSS_D, H8, H8, TH_OR)
THCALL(TH_OPIVX2, th_vxor_vx_b, OP_SSS_B, H1, H1, TH_XOR)
THCALL(TH_OPIVX2, th_vxor_vx_h, OP_SSS_H, H2, H2, TH_XOR)
THCALL(TH_OPIVX2, th_vxor_vx_w, OP_SSS_W, H4, H4, TH_XOR)
THCALL(TH_OPIVX2, th_vxor_vx_d, OP_SSS_D, H8, H8, TH_XOR)
GEN_TH_VX(th_vand_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vand_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vand_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vand_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vor_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vor_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vor_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vor_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vxor_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vxor_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vxor_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vxor_vx_d, 8, 8, clearq_th)

/* Vector Single-Width Bit Shift Instructions */
#define TH_SLL(N, M)  (N << (M))
#define TH_SRL(N, M)  (N >> (M))

/*
 * generate the helpers for shift instructions with two vector operators
 *
 * GEN_TH_SHIFT_VV and GEN_TH_SHIFT_VX are almost the copy of
 * GEN_VEXT_SHIFT_VV and GEN_VEXT_SHIFT_VX, except:
 * 1) different mask layout
 * 2) different data encoding
 * 3) different masked/tail elements process policy
 */
#define GEN_TH_SHIFT_VV(NAME, TS1, TS2, HS1, HS2, OP, MASK, CLEAR_FN)     \
void HELPER(NAME)(void *vd, void *v0, void *vs1,                          \
                  void *vs2, CPURISCVState *env, uint32_t desc)           \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vm = th_vm(desc);                                            \
    uint32_t vl = env->vl;                                                \
    uint32_t esz = sizeof(TS1);                                           \
    uint32_t vlmax = th_maxsz(desc) / esz;                                \
    uint32_t i;                                                           \
                                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                                         \
    for (i = env->vstart; i < vl; i++) {                                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                          \
            continue;                                                     \
        }                                                                 \
        TS1 s1 = *((TS1 *)vs1 + HS1(i));                                  \
        TS2 s2 = *((TS2 *)vs2 + HS2(i));                                  \
        *((TS1 *)vd + HS1(i)) = OP(s2, s1 & MASK);                        \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, vl, vl * esz, vlmax * esz);                              \
}

GEN_TH_SHIFT_VV(th_vsll_vv_b, uint8_t,  uint8_t, H1, H1, TH_SLL,
                0x7, clearb_th)
GEN_TH_SHIFT_VV(th_vsll_vv_h, uint16_t, uint16_t, H2, H2, TH_SLL,
                0xf, clearh_th)
GEN_TH_SHIFT_VV(th_vsll_vv_w, uint32_t, uint32_t, H4, H4, TH_SLL,
                0x1f, clearl_th)
GEN_TH_SHIFT_VV(th_vsll_vv_d, uint64_t, uint64_t, H8, H8, TH_SLL,
                0x3f, clearq_th)

GEN_TH_SHIFT_VV(th_vsrl_vv_b, uint8_t, uint8_t, H1, H1, TH_SRL,
                0x7, clearb_th)
GEN_TH_SHIFT_VV(th_vsrl_vv_h, uint16_t, uint16_t, H2, H2, TH_SRL,
                0xf, clearh_th)
GEN_TH_SHIFT_VV(th_vsrl_vv_w, uint32_t, uint32_t, H4, H4, TH_SRL,
                0x1f, clearl_th)
GEN_TH_SHIFT_VV(th_vsrl_vv_d, uint64_t, uint64_t, H8, H8, TH_SRL,
                0x3f, clearq_th)

GEN_TH_SHIFT_VV(th_vsra_vv_b, uint8_t,  int8_t, H1, H1, TH_SRL,
                0x7, clearb_th)
GEN_TH_SHIFT_VV(th_vsra_vv_h, uint16_t, int16_t, H2, H2, TH_SRL,
                0xf, clearh_th)
GEN_TH_SHIFT_VV(th_vsra_vv_w, uint32_t, int32_t, H4, H4, TH_SRL,
                0x1f, clearl_th)
GEN_TH_SHIFT_VV(th_vsra_vv_d, uint64_t, int64_t, H8, H8, TH_SRL,
                0x3f, clearq_th)

/* generate the helpers for shift instructions with one vector and one scalar */
#define GEN_TH_SHIFT_VX(NAME, TD, TS2, HD, HS2, OP, MASK, CLEAR_FN)   \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1,                \
        void *vs2, CPURISCVState *env, uint32_t desc)                 \
{                                                                     \
    uint32_t mlen = th_mlen(desc);                                    \
    uint32_t vm = th_vm(desc);                                        \
    uint32_t vl = env->vl;                                            \
    uint32_t esz = sizeof(TD);                                        \
    uint32_t vlmax = th_maxsz(desc) / esz;                            \
    uint32_t i;                                                       \
                                                                      \
    VSTART_CHECK_EARLY_EXIT(env);                                     \
    for (i = env->vstart; i < vl; i++) {                              \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                      \
            continue;                                                 \
        }                                                             \
        TS2 s2 = *((TS2 *)vs2 + HS2(i));                              \
        *((TD *)vd + HD(i)) = OP(s2, s1 & MASK);                      \
    }                                                                 \
    env->vstart = 0;                                                  \
    CLEAR_FN(vd, vl, vl * esz, vlmax * esz);                          \
}

GEN_TH_SHIFT_VX(th_vsll_vx_b, uint8_t, int8_t, H1, H1, TH_SLL,
                0x7, clearb_th)
GEN_TH_SHIFT_VX(th_vsll_vx_h, uint16_t, int16_t, H2, H2, TH_SLL,
                0xf, clearh_th)
GEN_TH_SHIFT_VX(th_vsll_vx_w, uint32_t, int32_t, H4, H4, TH_SLL,
                0x1f, clearl_th)
GEN_TH_SHIFT_VX(th_vsll_vx_d, uint64_t, int64_t, H8, H8, TH_SLL,
                0x3f, clearq_th)

GEN_TH_SHIFT_VX(th_vsrl_vx_b, uint8_t, uint8_t, H1, H1, TH_SRL,
                0x7, clearb_th)
GEN_TH_SHIFT_VX(th_vsrl_vx_h, uint16_t, uint16_t, H2, H2, TH_SRL,
                0xf, clearh_th)
GEN_TH_SHIFT_VX(th_vsrl_vx_w, uint32_t, uint32_t, H4, H4, TH_SRL,
                0x1f, clearl_th)
GEN_TH_SHIFT_VX(th_vsrl_vx_d, uint64_t, uint64_t, H8, H8, TH_SRL,
                0x3f, clearq_th)

GEN_TH_SHIFT_VX(th_vsra_vx_b, int8_t, int8_t, H1, H1, TH_SRL,
                0x7, clearb_th)
GEN_TH_SHIFT_VX(th_vsra_vx_h, int16_t, int16_t, H2, H2, TH_SRL,
                0xf, clearh_th)
GEN_TH_SHIFT_VX(th_vsra_vx_w, int32_t, int32_t, H4, H4, TH_SRL,
                0x1f, clearl_th)
GEN_TH_SHIFT_VX(th_vsra_vx_d, int64_t, int64_t, H8, H8, TH_SRL,
                0x3f, clearq_th)

/* Vector Narrowing Integer Right Shift Instructions */
GEN_TH_SHIFT_VV(th_vnsrl_vv_b, uint8_t,  uint16_t, H1, H2, TH_SRL,
                0xf, clearb_th)
GEN_TH_SHIFT_VV(th_vnsrl_vv_h, uint16_t, uint32_t, H2, H4, TH_SRL,
                0x1f, clearh_th)
GEN_TH_SHIFT_VV(th_vnsrl_vv_w, uint32_t, uint64_t, H4, H8, TH_SRL,
                0x3f, clearl_th)
GEN_TH_SHIFT_VV(th_vnsra_vv_b, uint8_t,  int16_t, H1, H2, TH_SRL,
                0xf, clearb_th)
GEN_TH_SHIFT_VV(th_vnsra_vv_h, uint16_t, int32_t, H2, H4, TH_SRL,
                0x1f, clearh_th)
GEN_TH_SHIFT_VV(th_vnsra_vv_w, uint32_t, int64_t, H4, H8, TH_SRL,
                0x3f, clearl_th)
GEN_TH_SHIFT_VX(th_vnsrl_vx_b, uint8_t, uint16_t, H1, H2, TH_SRL,
                0xf, clearb_th)
GEN_TH_SHIFT_VX(th_vnsrl_vx_h, uint16_t, uint32_t, H2, H4, TH_SRL,
                0x1f, clearh_th)
GEN_TH_SHIFT_VX(th_vnsrl_vx_w, uint32_t, uint64_t, H4, H8, TH_SRL,
                0x3f, clearl_th)
GEN_TH_SHIFT_VX(th_vnsra_vx_b, int8_t, int16_t, H1, H2, TH_SRL,
                0xf, clearb_th)
GEN_TH_SHIFT_VX(th_vnsra_vx_h, int16_t, int32_t, H2, H4, TH_SRL,
                0x1f, clearh_th)
GEN_TH_SHIFT_VX(th_vnsra_vx_w, int32_t, int64_t, H4, H8, TH_SRL,
                0x3f, clearl_th)

/* Vector Integer Comparison Instructions */
#define TH_MSEQ(N, M) (N == M)
#define TH_MSNE(N, M) (N != M)
#define TH_MSLT(N, M) (N < M)
#define TH_MSLE(N, M) (N <= M)
#define TH_MSGT(N, M) (N > M)

#define GEN_TH_CMP_VV(NAME, ETYPE, H, DO_OP)                  \
void HELPER(NAME)(void *vd, void *v0, void *vs1, void *vs2,   \
                  CPURISCVState *env, uint32_t desc)          \
{                                                             \
    uint32_t mlen = th_mlen(desc);                            \
    uint32_t vm = th_vm(desc);                                \
    uint32_t vl = env->vl;                                    \
    uint32_t vlmax = th_maxsz(desc) / sizeof(ETYPE);          \
    uint32_t i;                                               \
                                                              \
    VSTART_CHECK_EARLY_EXIT(env);                             \
    for (i = env->vstart; i < vl; i++) {                      \
        ETYPE s1 = *((ETYPE *)vs1 + H(i));                    \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                    \
        if (!vm && !th_elem_mask(v0, mlen, i)) {              \
            continue;                                         \
        }                                                     \
        th_set_elem_mask(vd, mlen, i, DO_OP(s2, s1));         \
    }                                                         \
    env->vstart = 0;                                          \
    for (; i < vlmax; i++) {                                  \
        th_set_elem_mask(vd, mlen, i, 0);                     \
    }                                                         \
}

GEN_TH_CMP_VV(th_vmseq_vv_b, uint8_t,  H1, TH_MSEQ)
GEN_TH_CMP_VV(th_vmseq_vv_h, uint16_t, H2, TH_MSEQ)
GEN_TH_CMP_VV(th_vmseq_vv_w, uint32_t, H4, TH_MSEQ)
GEN_TH_CMP_VV(th_vmseq_vv_d, uint64_t, H8, TH_MSEQ)

GEN_TH_CMP_VV(th_vmsne_vv_b, uint8_t,  H1, TH_MSNE)
GEN_TH_CMP_VV(th_vmsne_vv_h, uint16_t, H2, TH_MSNE)
GEN_TH_CMP_VV(th_vmsne_vv_w, uint32_t, H4, TH_MSNE)
GEN_TH_CMP_VV(th_vmsne_vv_d, uint64_t, H8, TH_MSNE)

GEN_TH_CMP_VV(th_vmsltu_vv_b, uint8_t,  H1, TH_MSLT)
GEN_TH_CMP_VV(th_vmsltu_vv_h, uint16_t, H2, TH_MSLT)
GEN_TH_CMP_VV(th_vmsltu_vv_w, uint32_t, H4, TH_MSLT)
GEN_TH_CMP_VV(th_vmsltu_vv_d, uint64_t, H8, TH_MSLT)

GEN_TH_CMP_VV(th_vmslt_vv_b, int8_t,  H1, TH_MSLT)
GEN_TH_CMP_VV(th_vmslt_vv_h, int16_t, H2, TH_MSLT)
GEN_TH_CMP_VV(th_vmslt_vv_w, int32_t, H4, TH_MSLT)
GEN_TH_CMP_VV(th_vmslt_vv_d, int64_t, H8, TH_MSLT)

GEN_TH_CMP_VV(th_vmsleu_vv_b, uint8_t,  H1, TH_MSLE)
GEN_TH_CMP_VV(th_vmsleu_vv_h, uint16_t, H2, TH_MSLE)
GEN_TH_CMP_VV(th_vmsleu_vv_w, uint32_t, H4, TH_MSLE)
GEN_TH_CMP_VV(th_vmsleu_vv_d, uint64_t, H8, TH_MSLE)

GEN_TH_CMP_VV(th_vmsle_vv_b, int8_t,  H1, TH_MSLE)
GEN_TH_CMP_VV(th_vmsle_vv_h, int16_t, H2, TH_MSLE)
GEN_TH_CMP_VV(th_vmsle_vv_w, int32_t, H4, TH_MSLE)
GEN_TH_CMP_VV(th_vmsle_vv_d, int64_t, H8, TH_MSLE)

#define GEN_TH_CMP_VX(NAME, ETYPE, H, DO_OP)                        \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1, void *vs2,   \
                  CPURISCVState *env, uint32_t desc)                \
{                                                                   \
    uint32_t mlen = th_mlen(desc);                                  \
    uint32_t vm = th_vm(desc);                                      \
    uint32_t vl = env->vl;                                          \
    uint32_t vlmax = th_maxsz(desc) / sizeof(ETYPE);                \
    uint32_t i;                                                     \
                                                                    \
    VSTART_CHECK_EARLY_EXIT(env);                                   \
    for (i = env->vstart; i < vl; i++) {                            \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                          \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                    \
            continue;                                               \
        }                                                           \
        th_set_elem_mask(vd, mlen, i,                               \
                DO_OP(s2, (ETYPE)(target_long)s1));                 \
    }                                                               \
    env->vstart = 0;                                                \
    for (; i < vlmax; i++) {                                        \
        th_set_elem_mask(vd, mlen, i, 0);                           \
    }                                                               \
}

GEN_TH_CMP_VX(th_vmseq_vx_b, uint8_t,  H1, TH_MSEQ)
GEN_TH_CMP_VX(th_vmseq_vx_h, uint16_t, H2, TH_MSEQ)
GEN_TH_CMP_VX(th_vmseq_vx_w, uint32_t, H4, TH_MSEQ)
GEN_TH_CMP_VX(th_vmseq_vx_d, uint64_t, H8, TH_MSEQ)

GEN_TH_CMP_VX(th_vmsne_vx_b, uint8_t,  H1, TH_MSNE)
GEN_TH_CMP_VX(th_vmsne_vx_h, uint16_t, H2, TH_MSNE)
GEN_TH_CMP_VX(th_vmsne_vx_w, uint32_t, H4, TH_MSNE)
GEN_TH_CMP_VX(th_vmsne_vx_d, uint64_t, H8, TH_MSNE)

GEN_TH_CMP_VX(th_vmsltu_vx_b, uint8_t,  H1, TH_MSLT)
GEN_TH_CMP_VX(th_vmsltu_vx_h, uint16_t, H2, TH_MSLT)
GEN_TH_CMP_VX(th_vmsltu_vx_w, uint32_t, H4, TH_MSLT)
GEN_TH_CMP_VX(th_vmsltu_vx_d, uint64_t, H8, TH_MSLT)

GEN_TH_CMP_VX(th_vmslt_vx_b, int8_t,  H1, TH_MSLT)
GEN_TH_CMP_VX(th_vmslt_vx_h, int16_t, H2, TH_MSLT)
GEN_TH_CMP_VX(th_vmslt_vx_w, int32_t, H4, TH_MSLT)
GEN_TH_CMP_VX(th_vmslt_vx_d, int64_t, H8, TH_MSLT)

GEN_TH_CMP_VX(th_vmsleu_vx_b, uint8_t,  H1, TH_MSLE)
GEN_TH_CMP_VX(th_vmsleu_vx_h, uint16_t, H2, TH_MSLE)
GEN_TH_CMP_VX(th_vmsleu_vx_w, uint32_t, H4, TH_MSLE)
GEN_TH_CMP_VX(th_vmsleu_vx_d, uint64_t, H8, TH_MSLE)

GEN_TH_CMP_VX(th_vmsle_vx_b, int8_t,  H1, TH_MSLE)
GEN_TH_CMP_VX(th_vmsle_vx_h, int16_t, H2, TH_MSLE)
GEN_TH_CMP_VX(th_vmsle_vx_w, int32_t, H4, TH_MSLE)
GEN_TH_CMP_VX(th_vmsle_vx_d, int64_t, H8, TH_MSLE)

GEN_TH_CMP_VX(th_vmsgtu_vx_b, uint8_t,  H1, TH_MSGT)
GEN_TH_CMP_VX(th_vmsgtu_vx_h, uint16_t, H2, TH_MSGT)
GEN_TH_CMP_VX(th_vmsgtu_vx_w, uint32_t, H4, TH_MSGT)
GEN_TH_CMP_VX(th_vmsgtu_vx_d, uint64_t, H8, TH_MSGT)

GEN_TH_CMP_VX(th_vmsgt_vx_b, int8_t,  H1, TH_MSGT)
GEN_TH_CMP_VX(th_vmsgt_vx_h, int16_t, H2, TH_MSGT)
GEN_TH_CMP_VX(th_vmsgt_vx_w, int32_t, H4, TH_MSGT)
GEN_TH_CMP_VX(th_vmsgt_vx_d, int64_t, H8, TH_MSGT)

/* Vector Integer Min/Max Instructions */
THCALL(TH_OPIVV2, th_vminu_vv_b, OP_UUU_B, H1, H1, H1, TH_MIN)
THCALL(TH_OPIVV2, th_vminu_vv_h, OP_UUU_H, H2, H2, H2, TH_MIN)
THCALL(TH_OPIVV2, th_vminu_vv_w, OP_UUU_W, H4, H4, H4, TH_MIN)
THCALL(TH_OPIVV2, th_vminu_vv_d, OP_UUU_D, H8, H8, H8, TH_MIN)
THCALL(TH_OPIVV2, th_vmin_vv_b, OP_SSS_B, H1, H1, H1, TH_MIN)
THCALL(TH_OPIVV2, th_vmin_vv_h, OP_SSS_H, H2, H2, H2, TH_MIN)
THCALL(TH_OPIVV2, th_vmin_vv_w, OP_SSS_W, H4, H4, H4, TH_MIN)
THCALL(TH_OPIVV2, th_vmin_vv_d, OP_SSS_D, H8, H8, H8, TH_MIN)
THCALL(TH_OPIVV2, th_vmaxu_vv_b, OP_UUU_B, H1, H1, H1, TH_MAX)
THCALL(TH_OPIVV2, th_vmaxu_vv_h, OP_UUU_H, H2, H2, H2, TH_MAX)
THCALL(TH_OPIVV2, th_vmaxu_vv_w, OP_UUU_W, H4, H4, H4, TH_MAX)
THCALL(TH_OPIVV2, th_vmaxu_vv_d, OP_UUU_D, H8, H8, H8, TH_MAX)
THCALL(TH_OPIVV2, th_vmax_vv_b, OP_SSS_B, H1, H1, H1, TH_MAX)
THCALL(TH_OPIVV2, th_vmax_vv_h, OP_SSS_H, H2, H2, H2, TH_MAX)
THCALL(TH_OPIVV2, th_vmax_vv_w, OP_SSS_W, H4, H4, H4, TH_MAX)
THCALL(TH_OPIVV2, th_vmax_vv_d, OP_SSS_D, H8, H8, H8, TH_MAX)
GEN_TH_VV(th_vminu_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vminu_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vminu_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vminu_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vmin_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vmin_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vmin_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vmin_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vmaxu_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vmaxu_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vmaxu_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vmaxu_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vmax_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vmax_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vmax_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vmax_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2, th_vminu_vx_b, OP_UUU_B, H1, H1, TH_MIN)
THCALL(TH_OPIVX2, th_vminu_vx_h, OP_UUU_H, H2, H2, TH_MIN)
THCALL(TH_OPIVX2, th_vminu_vx_w, OP_UUU_W, H4, H4, TH_MIN)
THCALL(TH_OPIVX2, th_vminu_vx_d, OP_UUU_D, H8, H8, TH_MIN)
THCALL(TH_OPIVX2, th_vmin_vx_b, OP_SSS_B, H1, H1, TH_MIN)
THCALL(TH_OPIVX2, th_vmin_vx_h, OP_SSS_H, H2, H2, TH_MIN)
THCALL(TH_OPIVX2, th_vmin_vx_w, OP_SSS_W, H4, H4, TH_MIN)
THCALL(TH_OPIVX2, th_vmin_vx_d, OP_SSS_D, H8, H8, TH_MIN)
THCALL(TH_OPIVX2, th_vmaxu_vx_b, OP_UUU_B, H1, H1, TH_MAX)
THCALL(TH_OPIVX2, th_vmaxu_vx_h, OP_UUU_H, H2, H2, TH_MAX)
THCALL(TH_OPIVX2, th_vmaxu_vx_w, OP_UUU_W, H4, H4, TH_MAX)
THCALL(TH_OPIVX2, th_vmaxu_vx_d, OP_UUU_D, H8, H8, TH_MAX)
THCALL(TH_OPIVX2, th_vmax_vx_b, OP_SSS_B, H1, H1, TH_MAX)
THCALL(TH_OPIVX2, th_vmax_vx_h, OP_SSS_H, H2, H2, TH_MAX)
THCALL(TH_OPIVX2, th_vmax_vx_w, OP_SSS_W, H4, H4, TH_MAX)
THCALL(TH_OPIVX2, th_vmax_vx_d, OP_SSS_D, H8, H8, TH_MAX)
GEN_TH_VX(th_vminu_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vminu_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vminu_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vminu_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vmin_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vmin_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vmin_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vmin_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vmaxu_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vmaxu_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vmaxu_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vmaxu_vx_d, 8, 8,  clearq_th)
GEN_TH_VX(th_vmax_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vmax_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vmax_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vmax_vx_d, 8, 8, clearq_th)

/* Vector Single-Width Integer Multiply Instructions */
#define TH_MUL(N, M) (N * M)
THCALL(TH_OPIVV2, th_vmul_vv_b, OP_SSS_B, H1, H1, H1, TH_MUL)
THCALL(TH_OPIVV2, th_vmul_vv_h, OP_SSS_H, H2, H2, H2, TH_MUL)
THCALL(TH_OPIVV2, th_vmul_vv_w, OP_SSS_W, H4, H4, H4, TH_MUL)
THCALL(TH_OPIVV2, th_vmul_vv_d, OP_SSS_D, H8, H8, H8, TH_MUL)
GEN_TH_VV(th_vmul_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vmul_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vmul_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vmul_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVV2, th_vmulh_vv_b, OP_SSS_B, H1, H1, H1, do_mulh_b)
THCALL(TH_OPIVV2, th_vmulh_vv_h, OP_SSS_H, H2, H2, H2, do_mulh_h)
THCALL(TH_OPIVV2, th_vmulh_vv_w, OP_SSS_W, H4, H4, H4, do_mulh_w)
THCALL(TH_OPIVV2, th_vmulh_vv_d, OP_SSS_D, H8, H8, H8, do_mulh_d)
THCALL(TH_OPIVV2, th_vmulhu_vv_b, OP_UUU_B, H1, H1, H1, do_mulhu_b)
THCALL(TH_OPIVV2, th_vmulhu_vv_h, OP_UUU_H, H2, H2, H2, do_mulhu_h)
THCALL(TH_OPIVV2, th_vmulhu_vv_w, OP_UUU_W, H4, H4, H4, do_mulhu_w)
THCALL(TH_OPIVV2, th_vmulhu_vv_d, OP_UUU_D, H8, H8, H8, do_mulhu_d)
THCALL(TH_OPIVV2, th_vmulhsu_vv_b, OP_SUS_B, H1, H1, H1, do_mulhsu_b)
THCALL(TH_OPIVV2, th_vmulhsu_vv_h, OP_SUS_H, H2, H2, H2, do_mulhsu_h)
THCALL(TH_OPIVV2, th_vmulhsu_vv_w, OP_SUS_W, H4, H4, H4, do_mulhsu_w)
THCALL(TH_OPIVV2, th_vmulhsu_vv_d, OP_SUS_D, H8, H8, H8, do_mulhsu_d)
GEN_TH_VV(th_vmulh_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vmulh_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vmulh_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vmulh_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vmulhu_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vmulhu_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vmulhu_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vmulhu_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vmulhsu_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vmulhsu_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vmulhsu_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vmulhsu_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2, th_vmul_vx_b, OP_SSS_B, H1, H1, TH_MUL)
THCALL(TH_OPIVX2, th_vmul_vx_h, OP_SSS_H, H2, H2, TH_MUL)
THCALL(TH_OPIVX2, th_vmul_vx_w, OP_SSS_W, H4, H4, TH_MUL)
THCALL(TH_OPIVX2, th_vmul_vx_d, OP_SSS_D, H8, H8, TH_MUL)
THCALL(TH_OPIVX2, th_vmulh_vx_b, OP_SSS_B, H1, H1, do_mulh_b)
THCALL(TH_OPIVX2, th_vmulh_vx_h, OP_SSS_H, H2, H2, do_mulh_h)
THCALL(TH_OPIVX2, th_vmulh_vx_w, OP_SSS_W, H4, H4, do_mulh_w)
THCALL(TH_OPIVX2, th_vmulh_vx_d, OP_SSS_D, H8, H8, do_mulh_d)
THCALL(TH_OPIVX2, th_vmulhu_vx_b, OP_UUU_B, H1, H1, do_mulhu_b)
THCALL(TH_OPIVX2, th_vmulhu_vx_h, OP_UUU_H, H2, H2, do_mulhu_h)
THCALL(TH_OPIVX2, th_vmulhu_vx_w, OP_UUU_W, H4, H4, do_mulhu_w)
THCALL(TH_OPIVX2, th_vmulhu_vx_d, OP_UUU_D, H8, H8, do_mulhu_d)
THCALL(TH_OPIVX2, th_vmulhsu_vx_b, OP_SUS_B, H1, H1, do_mulhsu_b)
THCALL(TH_OPIVX2, th_vmulhsu_vx_h, OP_SUS_H, H2, H2, do_mulhsu_h)
THCALL(TH_OPIVX2, th_vmulhsu_vx_w, OP_SUS_W, H4, H4, do_mulhsu_w)
THCALL(TH_OPIVX2, th_vmulhsu_vx_d, OP_SUS_D, H8, H8, do_mulhsu_d)
GEN_TH_VX(th_vmul_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vmul_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vmul_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vmul_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vmulh_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vmulh_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vmulh_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vmulh_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vmulhu_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vmulhu_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vmulhu_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vmulhu_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vmulhsu_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vmulhsu_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vmulhsu_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vmulhsu_vx_d, 8, 8, clearq_th)

/* Vector Integer Divide Instructions */
#define TH_DIVU(N, M) (unlikely(M == 0) ? (__typeof(N))(-1) : N / M)
#define TH_REMU(N, M) (unlikely(M == 0) ? N : N % M)
#define TH_DIV(N, M)  (unlikely(M == 0) ? (__typeof(N))(-1) :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? N : N / M)
#define TH_REM(N, M)  (unlikely(M == 0) ? N :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? 0 : N % M)

THCALL(TH_OPIVV2, th_vdivu_vv_b, OP_UUU_B, H1, H1, H1, TH_DIVU)
THCALL(TH_OPIVV2, th_vdivu_vv_h, OP_UUU_H, H2, H2, H2, TH_DIVU)
THCALL(TH_OPIVV2, th_vdivu_vv_w, OP_UUU_W, H4, H4, H4, TH_DIVU)
THCALL(TH_OPIVV2, th_vdivu_vv_d, OP_UUU_D, H8, H8, H8, TH_DIVU)
THCALL(TH_OPIVV2, th_vdiv_vv_b, OP_SSS_B, H1, H1, H1, TH_DIV)
THCALL(TH_OPIVV2, th_vdiv_vv_h, OP_SSS_H, H2, H2, H2, TH_DIV)
THCALL(TH_OPIVV2, th_vdiv_vv_w, OP_SSS_W, H4, H4, H4, TH_DIV)
THCALL(TH_OPIVV2, th_vdiv_vv_d, OP_SSS_D, H8, H8, H8, TH_DIV)
THCALL(TH_OPIVV2, th_vremu_vv_b, OP_UUU_B, H1, H1, H1, TH_REMU)
THCALL(TH_OPIVV2, th_vremu_vv_h, OP_UUU_H, H2, H2, H2, TH_REMU)
THCALL(TH_OPIVV2, th_vremu_vv_w, OP_UUU_W, H4, H4, H4, TH_REMU)
THCALL(TH_OPIVV2, th_vremu_vv_d, OP_UUU_D, H8, H8, H8, TH_REMU)
THCALL(TH_OPIVV2, th_vrem_vv_b, OP_SSS_B, H1, H1, H1, TH_REM)
THCALL(TH_OPIVV2, th_vrem_vv_h, OP_SSS_H, H2, H2, H2, TH_REM)
THCALL(TH_OPIVV2, th_vrem_vv_w, OP_SSS_W, H4, H4, H4, TH_REM)
THCALL(TH_OPIVV2, th_vrem_vv_d, OP_SSS_D, H8, H8, H8, TH_REM)
GEN_TH_VV(th_vdivu_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vdivu_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vdivu_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vdivu_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vdiv_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vdiv_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vdiv_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vdiv_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vremu_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vremu_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vremu_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vremu_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vrem_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vrem_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vrem_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vrem_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2, th_vdivu_vx_b, OP_UUU_B, H1, H1, TH_DIVU)
THCALL(TH_OPIVX2, th_vdivu_vx_h, OP_UUU_H, H2, H2, TH_DIVU)
THCALL(TH_OPIVX2, th_vdivu_vx_w, OP_UUU_W, H4, H4, TH_DIVU)
THCALL(TH_OPIVX2, th_vdivu_vx_d, OP_UUU_D, H8, H8, TH_DIVU)
THCALL(TH_OPIVX2, th_vdiv_vx_b, OP_SSS_B, H1, H1, TH_DIV)
THCALL(TH_OPIVX2, th_vdiv_vx_h, OP_SSS_H, H2, H2, TH_DIV)
THCALL(TH_OPIVX2, th_vdiv_vx_w, OP_SSS_W, H4, H4, TH_DIV)
THCALL(TH_OPIVX2, th_vdiv_vx_d, OP_SSS_D, H8, H8, TH_DIV)
THCALL(TH_OPIVX2, th_vremu_vx_b, OP_UUU_B, H1, H1, TH_REMU)
THCALL(TH_OPIVX2, th_vremu_vx_h, OP_UUU_H, H2, H2, TH_REMU)
THCALL(TH_OPIVX2, th_vremu_vx_w, OP_UUU_W, H4, H4, TH_REMU)
THCALL(TH_OPIVX2, th_vremu_vx_d, OP_UUU_D, H8, H8, TH_REMU)
THCALL(TH_OPIVX2, th_vrem_vx_b, OP_SSS_B, H1, H1, TH_REM)
THCALL(TH_OPIVX2, th_vrem_vx_h, OP_SSS_H, H2, H2, TH_REM)
THCALL(TH_OPIVX2, th_vrem_vx_w, OP_SSS_W, H4, H4, TH_REM)
THCALL(TH_OPIVX2, th_vrem_vx_d, OP_SSS_D, H8, H8, TH_REM)
GEN_TH_VX(th_vdivu_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vdivu_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vdivu_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vdivu_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vdiv_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vdiv_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vdiv_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vdiv_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vremu_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vremu_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vremu_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vremu_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vrem_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vrem_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vrem_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vrem_vx_d, 8, 8, clearq_th)

/* Vector Widening Integer Multiply Instructions */
THCALL(TH_OPIVV2, th_vwmul_vv_b, WOP_SSS_B, H2, H1, H1, TH_MUL)
THCALL(TH_OPIVV2, th_vwmul_vv_h, WOP_SSS_H, H4, H2, H2, TH_MUL)
THCALL(TH_OPIVV2, th_vwmul_vv_w, WOP_SSS_W, H8, H4, H4, TH_MUL)
THCALL(TH_OPIVV2, th_vwmulu_vv_b, WOP_UUU_B, H2, H1, H1, TH_MUL)
THCALL(TH_OPIVV2, th_vwmulu_vv_h, WOP_UUU_H, H4, H2, H2, TH_MUL)
THCALL(TH_OPIVV2, th_vwmulu_vv_w, WOP_UUU_W, H8, H4, H4, TH_MUL)
THCALL(TH_OPIVV2, th_vwmulsu_vv_b, WOP_SUS_B, H2, H1, H1, TH_MUL)
THCALL(TH_OPIVV2, th_vwmulsu_vv_h, WOP_SUS_H, H4, H2, H2, TH_MUL)
THCALL(TH_OPIVV2, th_vwmulsu_vv_w, WOP_SUS_W, H8, H4, H4, TH_MUL)
GEN_TH_VV(th_vwmul_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwmul_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwmul_vv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwmulu_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwmulu_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwmulu_vv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwmulsu_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwmulsu_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwmulsu_vv_w, 4, 8, clearq_th)

THCALL(TH_OPIVX2, th_vwmul_vx_b, WOP_SSS_B, H2, H1, TH_MUL)
THCALL(TH_OPIVX2, th_vwmul_vx_h, WOP_SSS_H, H4, H2, TH_MUL)
THCALL(TH_OPIVX2, th_vwmul_vx_w, WOP_SSS_W, H8, H4, TH_MUL)
THCALL(TH_OPIVX2, th_vwmulu_vx_b, WOP_UUU_B, H2, H1, TH_MUL)
THCALL(TH_OPIVX2, th_vwmulu_vx_h, WOP_UUU_H, H4, H2, TH_MUL)
THCALL(TH_OPIVX2, th_vwmulu_vx_w, WOP_UUU_W, H8, H4, TH_MUL)
THCALL(TH_OPIVX2, th_vwmulsu_vx_b, WOP_SUS_B, H2, H1, TH_MUL)
THCALL(TH_OPIVX2, th_vwmulsu_vx_h, WOP_SUS_H, H4, H2, TH_MUL)
THCALL(TH_OPIVX2, th_vwmulsu_vx_w, WOP_SUS_W, H8, H4, TH_MUL)
GEN_TH_VX(th_vwmul_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwmul_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwmul_vx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwmulu_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwmulu_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwmulu_vx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwmulsu_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwmulsu_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwmulsu_vx_w, 4, 8, clearq_th)

/* Vector Single-Width Integer Multiply-Add Instructions */
#define TH_OPIVV3(NAME, TD, T1, T2, TX1, TX2, HD, HS1, HS2, OP)   \
static void do_##NAME(void *vd, void *vs1, void *vs2, int i)       \
{                                                                  \
    TX1 s1 = *((T1 *)vs1 + HS1(i));                                \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                                \
    TD d = *((TD *)vd + HD(i));                                    \
    *((TD *)vd + HD(i)) = OP(s2, s1, d);                           \
}
#define TH_MACC(N, M, D) (M * N + D)
#define TH_NMSAC(N, M, D) (-(M * N) + D)
#define TH_MADD(N, M, D) (M * D + N)
#define TH_NMSUB(N, M, D) (-(M * D) + N)
THCALL(TH_OPIVV3, th_vmacc_vv_b, OP_SSS_B, H1, H1, H1, TH_MACC)
THCALL(TH_OPIVV3, th_vmacc_vv_h, OP_SSS_H, H2, H2, H2, TH_MACC)
THCALL(TH_OPIVV3, th_vmacc_vv_w, OP_SSS_W, H4, H4, H4, TH_MACC)
THCALL(TH_OPIVV3, th_vmacc_vv_d, OP_SSS_D, H8, H8, H8, TH_MACC)
THCALL(TH_OPIVV3, th_vnmsac_vv_b, OP_SSS_B, H1, H1, H1, TH_NMSAC)
THCALL(TH_OPIVV3, th_vnmsac_vv_h, OP_SSS_H, H2, H2, H2, TH_NMSAC)
THCALL(TH_OPIVV3, th_vnmsac_vv_w, OP_SSS_W, H4, H4, H4, TH_NMSAC)
THCALL(TH_OPIVV3, th_vnmsac_vv_d, OP_SSS_D, H8, H8, H8, TH_NMSAC)
THCALL(TH_OPIVV3, th_vmadd_vv_b, OP_SSS_B, H1, H1, H1, TH_MADD)
THCALL(TH_OPIVV3, th_vmadd_vv_h, OP_SSS_H, H2, H2, H2, TH_MADD)
THCALL(TH_OPIVV3, th_vmadd_vv_w, OP_SSS_W, H4, H4, H4, TH_MADD)
THCALL(TH_OPIVV3, th_vmadd_vv_d, OP_SSS_D, H8, H8, H8, TH_MADD)
THCALL(TH_OPIVV3, th_vnmsub_vv_b, OP_SSS_B, H1, H1, H1, TH_NMSUB)
THCALL(TH_OPIVV3, th_vnmsub_vv_h, OP_SSS_H, H2, H2, H2, TH_NMSUB)
THCALL(TH_OPIVV3, th_vnmsub_vv_w, OP_SSS_W, H4, H4, H4, TH_NMSUB)
THCALL(TH_OPIVV3, th_vnmsub_vv_d, OP_SSS_D, H8, H8, H8, TH_NMSUB)
GEN_TH_VV(th_vmacc_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vmacc_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vmacc_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vmacc_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vnmsac_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vnmsac_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vnmsac_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vnmsac_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vmadd_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vmadd_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vmadd_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vmadd_vv_d, 8, 8, clearq_th)
GEN_TH_VV(th_vnmsub_vv_b, 1, 1, clearb_th)
GEN_TH_VV(th_vnmsub_vv_h, 2, 2, clearh_th)
GEN_TH_VV(th_vnmsub_vv_w, 4, 4, clearl_th)
GEN_TH_VV(th_vnmsub_vv_d, 8, 8, clearq_th)

#define TH_OPIVX3(NAME, TD, T1, T2, TX1, TX2, HD, HS2, OP)             \
static void do_##NAME(void *vd, target_long s1, void *vs2, int i)   \
{                                                                   \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                                 \
    TD d = *((TD *)vd + HD(i));                                     \
    *((TD *)vd + HD(i)) = OP(s2, (TX1)(T1)s1, d);                   \
}

THCALL(TH_OPIVX3, th_vmacc_vx_b, OP_SSS_B, H1, H1, TH_MACC)
THCALL(TH_OPIVX3, th_vmacc_vx_h, OP_SSS_H, H2, H2, TH_MACC)
THCALL(TH_OPIVX3, th_vmacc_vx_w, OP_SSS_W, H4, H4, TH_MACC)
THCALL(TH_OPIVX3, th_vmacc_vx_d, OP_SSS_D, H8, H8, TH_MACC)
THCALL(TH_OPIVX3, th_vnmsac_vx_b, OP_SSS_B, H1, H1, TH_NMSAC)
THCALL(TH_OPIVX3, th_vnmsac_vx_h, OP_SSS_H, H2, H2, TH_NMSAC)
THCALL(TH_OPIVX3, th_vnmsac_vx_w, OP_SSS_W, H4, H4, TH_NMSAC)
THCALL(TH_OPIVX3, th_vnmsac_vx_d, OP_SSS_D, H8, H8, TH_NMSAC)
THCALL(TH_OPIVX3, th_vmadd_vx_b, OP_SSS_B, H1, H1, TH_MADD)
THCALL(TH_OPIVX3, th_vmadd_vx_h, OP_SSS_H, H2, H2, TH_MADD)
THCALL(TH_OPIVX3, th_vmadd_vx_w, OP_SSS_W, H4, H4, TH_MADD)
THCALL(TH_OPIVX3, th_vmadd_vx_d, OP_SSS_D, H8, H8, TH_MADD)
THCALL(TH_OPIVX3, th_vnmsub_vx_b, OP_SSS_B, H1, H1, TH_NMSUB)
THCALL(TH_OPIVX3, th_vnmsub_vx_h, OP_SSS_H, H2, H2, TH_NMSUB)
THCALL(TH_OPIVX3, th_vnmsub_vx_w, OP_SSS_W, H4, H4, TH_NMSUB)
THCALL(TH_OPIVX3, th_vnmsub_vx_d, OP_SSS_D, H8, H8, TH_NMSUB)
GEN_TH_VX(th_vmacc_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vmacc_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vmacc_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vmacc_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vnmsac_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vnmsac_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vnmsac_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vnmsac_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vmadd_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vmadd_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vmadd_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vmadd_vx_d, 8, 8, clearq_th)
GEN_TH_VX(th_vnmsub_vx_b, 1, 1, clearb_th)
GEN_TH_VX(th_vnmsub_vx_h, 2, 2, clearh_th)
GEN_TH_VX(th_vnmsub_vx_w, 4, 4, clearl_th)
GEN_TH_VX(th_vnmsub_vx_d, 8, 8, clearq_th)

/* Vector Widening Integer Multiply-Add Instructions */
THCALL(TH_OPIVV3, th_vwmaccu_vv_b, WOP_UUU_B, H2, H1, H1, TH_MACC)
THCALL(TH_OPIVV3, th_vwmaccu_vv_h, WOP_UUU_H, H4, H2, H2, TH_MACC)
THCALL(TH_OPIVV3, th_vwmaccu_vv_w, WOP_UUU_W, H8, H4, H4, TH_MACC)
THCALL(TH_OPIVV3, th_vwmacc_vv_b, WOP_SSS_B, H2, H1, H1, TH_MACC)
THCALL(TH_OPIVV3, th_vwmacc_vv_h, WOP_SSS_H, H4, H2, H2, TH_MACC)
THCALL(TH_OPIVV3, th_vwmacc_vv_w, WOP_SSS_W, H8, H4, H4, TH_MACC)
THCALL(TH_OPIVV3, th_vwmaccsu_vv_b, WOP_SSU_B, H2, H1, H1, TH_MACC)
THCALL(TH_OPIVV3, th_vwmaccsu_vv_h, WOP_SSU_H, H4, H2, H2, TH_MACC)
THCALL(TH_OPIVV3, th_vwmaccsu_vv_w, WOP_SSU_W, H8, H4, H4, TH_MACC)
GEN_TH_VV(th_vwmaccu_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwmaccu_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwmaccu_vv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwmacc_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwmacc_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwmacc_vv_w, 4, 8, clearq_th)
GEN_TH_VV(th_vwmaccsu_vv_b, 1, 2, clearh_th)
GEN_TH_VV(th_vwmaccsu_vv_h, 2, 4, clearl_th)
GEN_TH_VV(th_vwmaccsu_vv_w, 4, 8, clearq_th)

THCALL(TH_OPIVX3, th_vwmaccu_vx_b, WOP_UUU_B, H2, H1, TH_MACC)
THCALL(TH_OPIVX3, th_vwmaccu_vx_h, WOP_UUU_H, H4, H2, TH_MACC)
THCALL(TH_OPIVX3, th_vwmaccu_vx_w, WOP_UUU_W, H8, H4, TH_MACC)
THCALL(TH_OPIVX3, th_vwmacc_vx_b, WOP_SSS_B, H2, H1, TH_MACC)
THCALL(TH_OPIVX3, th_vwmacc_vx_h, WOP_SSS_H, H4, H2, TH_MACC)
THCALL(TH_OPIVX3, th_vwmacc_vx_w, WOP_SSS_W, H8, H4, TH_MACC)
THCALL(TH_OPIVX3, th_vwmaccsu_vx_b, WOP_SSU_B, H2, H1, TH_MACC)
THCALL(TH_OPIVX3, th_vwmaccsu_vx_h, WOP_SSU_H, H4, H2, TH_MACC)
THCALL(TH_OPIVX3, th_vwmaccsu_vx_w, WOP_SSU_W, H8, H4, TH_MACC)
THCALL(TH_OPIVX3, th_vwmaccus_vx_b, WOP_SUS_B, H2, H1, TH_MACC)
THCALL(TH_OPIVX3, th_vwmaccus_vx_h, WOP_SUS_H, H4, H2, TH_MACC)
THCALL(TH_OPIVX3, th_vwmaccus_vx_w, WOP_SUS_W, H8, H4, TH_MACC)
GEN_TH_VX(th_vwmaccu_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwmaccu_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwmaccu_vx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwmacc_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwmacc_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwmacc_vx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwmaccsu_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwmaccsu_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwmaccsu_vx_w, 4, 8, clearq_th)
GEN_TH_VX(th_vwmaccus_vx_b, 1, 2, clearh_th)
GEN_TH_VX(th_vwmaccus_vx_h, 2, 4, clearl_th)
GEN_TH_VX(th_vwmaccus_vx_w, 4, 8, clearq_th)

/* Vector Integer Merge and Move Instructions */

/*
 * The funtions below of VMV and vmerge are all the copy of RVV1.0 functions,
 * except:
 * 1) different desc encoding
 * 2) different tail/masked element process policy
 * 3) different mask layout
 */
#define GEN_TH_VMV_VV(NAME, ETYPE, H, CLEAR_FN)                      \
void HELPER(NAME)(void *vd, void *vs1, CPURISCVState *env,           \
                  uint32_t desc)                                     \
{                                                                    \
    uint32_t vl = env->vl;                                           \
    uint32_t esz = sizeof(ETYPE);                                    \
    uint32_t vlmax = th_maxsz(desc) / esz;                           \
    uint32_t i;                                                      \
                                                                     \
    VSTART_CHECK_EARLY_EXIT(env);                                    \
    for (i = env->vstart; i < vl; i++) {                             \
        ETYPE s1 = *((ETYPE *)vs1 + H(i));                           \
        *((ETYPE *)vd + H(i)) = s1;                                  \
    }                                                                \
    env->vstart = 0;                                                 \
    CLEAR_FN(vd, vl, vl * esz, vlmax * esz);                         \
}

GEN_TH_VMV_VV(th_vmv_v_v_b, int8_t,  H1, clearb_th)
GEN_TH_VMV_VV(th_vmv_v_v_h, int16_t, H2, clearh_th)
GEN_TH_VMV_VV(th_vmv_v_v_w, int32_t, H4, clearl_th)
GEN_TH_VMV_VV(th_vmv_v_v_d, int64_t, H8, clearq_th)

#define GEN_TH_VMV_VX(NAME, ETYPE, H, CLEAR_FN)                      \
void HELPER(NAME)(void *vd, uint64_t s1, CPURISCVState *env,         \
                  uint32_t desc)                                     \
{                                                                    \
    uint32_t vl = env->vl;                                           \
    uint32_t esz = sizeof(ETYPE);                                    \
    uint32_t vlmax = th_maxsz(desc) / esz;                           \
    uint32_t i;                                                      \
                                                                     \
    VSTART_CHECK_EARLY_EXIT(env);                                    \
    for (i = env->vstart; i < vl; i++) {                             \
        *((ETYPE *)vd + H(i)) = (ETYPE)s1;                           \
    }                                                                \
    env->vstart = 0;                                                 \
    CLEAR_FN(vd, vl, vl * esz, vlmax * esz);                         \
}

GEN_TH_VMV_VX(th_vmv_v_x_b, int8_t,  H1, clearb_th)
GEN_TH_VMV_VX(th_vmv_v_x_h, int16_t, H2, clearh_th)
GEN_TH_VMV_VX(th_vmv_v_x_w, int32_t, H4, clearl_th)
GEN_TH_VMV_VX(th_vmv_v_x_d, int64_t, H8, clearq_th)

#define GEN_TH_VMERGE_VV(NAME, ETYPE, H, CLEAR_FN)                   \
void HELPER(NAME)(void *vd, void *v0, void *vs1, void *vs2,          \
                  CPURISCVState *env, uint32_t desc)                 \
{                                                                    \
    uint32_t mlen = th_mlen(desc);                                   \
    uint32_t vl = env->vl;                                           \
    uint32_t esz = sizeof(ETYPE);                                    \
    uint32_t vlmax = th_maxsz(desc) / esz;                           \
    uint32_t i;                                                      \
                                                                     \
    VSTART_CHECK_EARLY_EXIT(env);                                    \
    for (i = env->vstart; i < vl; i++) {                             \
        ETYPE *vt = (!th_elem_mask(v0, mlen, i) ? vs2 : vs1);        \
        *((ETYPE *)vd + H(i)) = *(vt + H(i));                        \
    }                                                                \
    env->vstart = 0;                                                 \
    CLEAR_FN(vd, vl, vl * esz, vlmax * esz);                         \
}

GEN_TH_VMERGE_VV(th_vmerge_vvm_b, int8_t,  H1, clearb_th)
GEN_TH_VMERGE_VV(th_vmerge_vvm_h, int16_t, H2, clearh_th)
GEN_TH_VMERGE_VV(th_vmerge_vvm_w, int32_t, H4, clearl_th)
GEN_TH_VMERGE_VV(th_vmerge_vvm_d, int64_t, H8, clearq_th)

#define GEN_TH_VMERGE_VX(NAME, ETYPE, H, CLEAR_FN)                   \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1,               \
                  void *vs2, CPURISCVState *env, uint32_t desc)      \
{                                                                    \
    uint32_t mlen = th_mlen(desc);                                   \
    uint32_t vl = env->vl;                                           \
    uint32_t esz = sizeof(ETYPE);                                    \
    uint32_t vlmax = th_maxsz(desc) / esz;                           \
    uint32_t i;                                                      \
                                                                     \
    VSTART_CHECK_EARLY_EXIT(env);                                    \
    for (i = env->vstart; i < vl; i++) {                             \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                           \
        ETYPE d = (!th_elem_mask(v0, mlen, i) ? s2 :                 \
                   (ETYPE)(target_long)s1);                          \
        *((ETYPE *)vd + H(i)) = d;                                   \
    }                                                                \
    env->vstart = 0;                                                 \
    CLEAR_FN(vd, vl, vl * esz, vlmax * esz);                         \
}

GEN_TH_VMERGE_VX(th_vmerge_vxm_b, int8_t,  H1, clearb_th)
GEN_TH_VMERGE_VX(th_vmerge_vxm_h, int16_t, H2, clearh_th)
GEN_TH_VMERGE_VX(th_vmerge_vxm_w, int32_t, H4, clearl_th)
GEN_TH_VMERGE_VX(th_vmerge_vxm_d, int64_t, H8, clearq_th)

/*
 * Vector Fixed-Point Arithmetic Instructions
 */

/* Vector Single-Width Saturating Add and Subtract */

/*
 * As fixed point instructions probably have round mode and saturation,
 * define common macros for fixed point here.
 */
typedef void opivv2_rm_fn_th(void *vd, void *vs1, void *vs2, int i,
                             CPURISCVState *env, int vxrm);

/*
 * The functions of fix-point operations below are just the copies of
 * functions in RVV1.0.
 * The changes in these functions are:
 * 1) different desc encoding
 * 2) different tail/masked element process policy
 * 3) different mask layout
 */
#define TH_OPIVV2_RM(NAME, TD, T1, T2, TX1, TX2, HD, HS1, HS2, OP)  \
static inline void                                                  \
do_##NAME(void *vd, void *vs1, void *vs2, int i,                    \
          CPURISCVState *env, int vxrm)                             \
{                                                                   \
    TX1 s1 = *((T1 *)vs1 + HS1(i));                                 \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                                 \
    *((TD *)vd + HD(i)) = OP(env, vxrm, s2, s1);                    \
}

static inline void
th_vv_rm_1(void *vd, void *v0, void *vs1, void *vs2,
           CPURISCVState *env,
           uint32_t vl, uint32_t vm, uint32_t mlen, int vxrm,
           opivv2_rm_fn_th *fn)
{
    VSTART_CHECK_EARLY_EXIT(env);
    for (uint32_t i = env->vstart; i < vl; i++) {
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        fn(vd, vs1, vs2, i, env, vxrm);
    }
    env->vstart = 0;
}

static inline void
th_vv_rm_2(void *vd, void *v0, void *vs1, void *vs2,
           CPURISCVState *env,
           uint32_t desc, uint32_t esz, uint32_t dsz,
           opivv2_rm_fn_th *fn, clear_fn *clearfn)
{
    uint32_t vlmax = th_maxsz(desc) / esz;
    uint32_t mlen = th_mlen(desc);
    uint32_t vm = th_vm(desc);
    uint32_t vl = env->vl;

    switch (env->vxrm) {
    case 0: /* rnu */
        th_vv_rm_1(vd, v0, vs1, vs2,
                   env, vl, vm, mlen, 0, fn);
        break;
    case 1: /* rne */
        th_vv_rm_1(vd, v0, vs1, vs2,
                   env, vl, vm, mlen, 1, fn);
        break;
    case 2: /* rdn */
        th_vv_rm_1(vd, v0, vs1, vs2,
                   env, vl, vm, mlen, 2, fn);
        break;
    default: /* rod */
        th_vv_rm_1(vd, v0, vs1, vs2,
                   env, vl, vm, mlen, 3, fn);
        break;
    }

    clearfn(vd, vl, vl * dsz,  vlmax * dsz);
}

/* generate helpers for fixed point instructions with OPIVV format */
#define GEN_TH_VV_RM(NAME, ESZ, DSZ, CLEAR_FN)                  \
void HELPER(NAME)(void *vd, void *v0, void *vs1, void *vs2,     \
                  CPURISCVState *env, uint32_t desc)            \
{                                                               \
    th_vv_rm_2(vd, v0, vs1, vs2, env, desc, ESZ, DSZ,           \
               do_##NAME, CLEAR_FN);                            \
}

THCALL(TH_OPIVV2_RM, th_vsaddu_vv_b, OP_UUU_B, H1, H1, H1, saddu8)
THCALL(TH_OPIVV2_RM, th_vsaddu_vv_h, OP_UUU_H, H2, H2, H2, saddu16)
THCALL(TH_OPIVV2_RM, th_vsaddu_vv_w, OP_UUU_W, H4, H4, H4, saddu32)
THCALL(TH_OPIVV2_RM, th_vsaddu_vv_d, OP_UUU_D, H8, H8, H8, saddu64)
GEN_TH_VV_RM(th_vsaddu_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vsaddu_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vsaddu_vv_w, 4, 4, clearl_th)
GEN_TH_VV_RM(th_vsaddu_vv_d, 8, 8, clearq_th)

typedef void opivx2_rm_fn_th(void *vd, target_long s1, void *vs2, int i,
                             CPURISCVState *env, int vxrm);

#define TH_OPIVX2_RM(NAME, TD, T1, T2, TX1, TX2, HD, HS2, OP)       \
static inline void                                                  \
do_##NAME(void *vd, target_long s1, void *vs2, int i,               \
          CPURISCVState *env, int vxrm)                             \
{                                                                   \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                                 \
    *((TD *)vd + HD(i)) = OP(env, vxrm, s2, (TX1)(T1)s1);           \
}

static inline void
th_vx_rm_1(void *vd, void *v0, target_long s1, void *vs2,
           CPURISCVState *env,
           uint32_t vl, uint32_t vm, uint32_t mlen, int vxrm,
           opivx2_rm_fn_th *fn)
{
    VSTART_CHECK_EARLY_EXIT(env);
    for (uint32_t i = env->vstart; i < vl; i++) {
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        fn(vd, s1, vs2, i, env, vxrm);
    }
    env->vstart = 0;
}

static inline void
th_vx_rm_2(void *vd, void *v0, target_long s1, void *vs2,
           CPURISCVState *env,
           uint32_t desc, uint32_t esz, uint32_t dsz,
           opivx2_rm_fn_th *fn, clear_fn *clearfn)
{
    uint32_t vlmax = th_maxsz(desc) / esz;
    uint32_t mlen = th_mlen(desc);
    uint32_t vm = th_vm(desc);
    uint32_t vl = env->vl;

    switch (env->vxrm) {
    case 0: /* rnu */
        th_vx_rm_1(vd, v0, s1, vs2,
                   env, vl, vm, mlen, 0, fn);
        break;
    case 1: /* rne */
        th_vx_rm_1(vd, v0, s1, vs2,
                   env, vl, vm, mlen, 1, fn);
        break;
    case 2: /* rdn */
        th_vx_rm_1(vd, v0, s1, vs2,
                   env, vl, vm, mlen, 2, fn);
        break;
    default: /* rod */
        th_vx_rm_1(vd, v0, s1, vs2,
                   env, vl, vm, mlen, 3, fn);
        break;
    }

    clearfn(vd, vl, vl * dsz,  vlmax * dsz);
}

/* generate helpers for fixed point instructions with OPIVX format */
#define GEN_TH_VX_RM(NAME, ESZ, DSZ, CLEAR_FN)            \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1,    \
        void *vs2, CPURISCVState *env, uint32_t desc)     \
{                                                         \
    th_vx_rm_2(vd, v0, s1, vs2, env, desc, ESZ, DSZ,      \
               do_##NAME, CLEAR_FN);                      \
}

THCALL(TH_OPIVX2_RM, th_vsaddu_vx_b, OP_UUU_B, H1, H1, saddu8)
THCALL(TH_OPIVX2_RM, th_vsaddu_vx_h, OP_UUU_H, H2, H2, saddu16)
THCALL(TH_OPIVX2_RM, th_vsaddu_vx_w, OP_UUU_W, H4, H4, saddu32)
THCALL(TH_OPIVX2_RM, th_vsaddu_vx_d, OP_UUU_D, H8, H8, saddu64)
GEN_TH_VX_RM(th_vsaddu_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vsaddu_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vsaddu_vx_w, 4, 4, clearl_th)
GEN_TH_VX_RM(th_vsaddu_vx_d, 8, 8, clearq_th)

THCALL(TH_OPIVV2_RM, th_vsadd_vv_b, OP_SSS_B, H1, H1, H1, sadd8)
THCALL(TH_OPIVV2_RM, th_vsadd_vv_h, OP_SSS_H, H2, H2, H2, sadd16)
THCALL(TH_OPIVV2_RM, th_vsadd_vv_w, OP_SSS_W, H4, H4, H4, sadd32)
THCALL(TH_OPIVV2_RM, th_vsadd_vv_d, OP_SSS_D, H8, H8, H8, sadd64)
GEN_TH_VV_RM(th_vsadd_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vsadd_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vsadd_vv_w, 4, 4, clearl_th)
GEN_TH_VV_RM(th_vsadd_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2_RM, th_vsadd_vx_b, OP_SSS_B, H1, H1, sadd8)
THCALL(TH_OPIVX2_RM, th_vsadd_vx_h, OP_SSS_H, H2, H2, sadd16)
THCALL(TH_OPIVX2_RM, th_vsadd_vx_w, OP_SSS_W, H4, H4, sadd32)
THCALL(TH_OPIVX2_RM, th_vsadd_vx_d, OP_SSS_D, H8, H8, sadd64)
GEN_TH_VX_RM(th_vsadd_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vsadd_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vsadd_vx_w, 4, 4, clearl_th)
GEN_TH_VX_RM(th_vsadd_vx_d, 8, 8, clearq_th)

THCALL(TH_OPIVV2_RM, th_vssubu_vv_b, OP_UUU_B, H1, H1, H1, ssubu8)
THCALL(TH_OPIVV2_RM, th_vssubu_vv_h, OP_UUU_H, H2, H2, H2, ssubu16)
THCALL(TH_OPIVV2_RM, th_vssubu_vv_w, OP_UUU_W, H4, H4, H4, ssubu32)
THCALL(TH_OPIVV2_RM, th_vssubu_vv_d, OP_UUU_D, H8, H8, H8, ssubu64)
GEN_TH_VV_RM(th_vssubu_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vssubu_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vssubu_vv_w, 4, 4, clearl_th)
GEN_TH_VV_RM(th_vssubu_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2_RM, th_vssubu_vx_b, OP_UUU_B, H1, H1, ssubu8)
THCALL(TH_OPIVX2_RM, th_vssubu_vx_h, OP_UUU_H, H2, H2, ssubu16)
THCALL(TH_OPIVX2_RM, th_vssubu_vx_w, OP_UUU_W, H4, H4, ssubu32)
THCALL(TH_OPIVX2_RM, th_vssubu_vx_d, OP_UUU_D, H8, H8, ssubu64)
GEN_TH_VX_RM(th_vssubu_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vssubu_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vssubu_vx_w, 4, 4, clearl_th)
GEN_TH_VX_RM(th_vssubu_vx_d, 8, 8, clearq_th)

THCALL(TH_OPIVV2_RM, th_vssub_vv_b, OP_SSS_B, H1, H1, H1, ssub8)
THCALL(TH_OPIVV2_RM, th_vssub_vv_h, OP_SSS_H, H2, H2, H2, ssub16)
THCALL(TH_OPIVV2_RM, th_vssub_vv_w, OP_SSS_W, H4, H4, H4, ssub32)
THCALL(TH_OPIVV2_RM, th_vssub_vv_d, OP_SSS_D, H8, H8, H8, ssub64)
GEN_TH_VV_RM(th_vssub_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vssub_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vssub_vv_w, 4, 4, clearl_th)
GEN_TH_VV_RM(th_vssub_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2_RM, th_vssub_vx_b, OP_SSS_B, H1, H1, ssub8)
THCALL(TH_OPIVX2_RM, th_vssub_vx_h, OP_SSS_H, H2, H2, ssub16)
THCALL(TH_OPIVX2_RM, th_vssub_vx_w, OP_SSS_W, H4, H4, ssub32)
THCALL(TH_OPIVX2_RM, th_vssub_vx_d, OP_SSS_D, H8, H8, ssub64)
GEN_TH_VX_RM(th_vssub_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vssub_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vssub_vx_w, 4, 4, clearl_th)
GEN_TH_VX_RM(th_vssub_vx_d, 8, 8, clearq_th)

THCALL(TH_OPIVV2_RM, th_vaadd_vv_b, OP_SSS_B, H1, H1, H1, aadd32)
THCALL(TH_OPIVV2_RM, th_vaadd_vv_h, OP_SSS_H, H2, H2, H2, aadd32)
THCALL(TH_OPIVV2_RM, th_vaadd_vv_w, OP_SSS_W, H4, H4, H4, aadd32)
THCALL(TH_OPIVV2_RM, th_vaadd_vv_d, OP_SSS_D, H8, H8, H8, aadd64)
GEN_TH_VV_RM(th_vaadd_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vaadd_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vaadd_vv_w, 4, 4, clearl_th)
GEN_TH_VV_RM(th_vaadd_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2_RM, th_vaadd_vx_b, OP_SSS_B, H1, H1, aadd32)
THCALL(TH_OPIVX2_RM, th_vaadd_vx_h, OP_SSS_H, H2, H2, aadd32)
THCALL(TH_OPIVX2_RM, th_vaadd_vx_w, OP_SSS_W, H4, H4, aadd32)
THCALL(TH_OPIVX2_RM, th_vaadd_vx_d, OP_SSS_D, H8, H8, aadd64)
GEN_TH_VX_RM(th_vaadd_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vaadd_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vaadd_vx_w, 4, 4, clearl_th)
GEN_TH_VX_RM(th_vaadd_vx_d, 8, 8, clearq_th)

THCALL(TH_OPIVV2_RM, th_vasub_vv_b, OP_SSS_B, H1, H1, H1, asub32)
THCALL(TH_OPIVV2_RM, th_vasub_vv_h, OP_SSS_H, H2, H2, H2, asub32)
THCALL(TH_OPIVV2_RM, th_vasub_vv_w, OP_SSS_W, H4, H4, H4, asub32)
THCALL(TH_OPIVV2_RM, th_vasub_vv_d, OP_SSS_D, H8, H8, H8, asub64)
GEN_TH_VV_RM(th_vasub_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vasub_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vasub_vv_w, 4, 4, clearl_th)
GEN_TH_VV_RM(th_vasub_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2_RM, th_vasub_vx_b, OP_SSS_B, H1, H1, asub32)
THCALL(TH_OPIVX2_RM, th_vasub_vx_h, OP_SSS_H, H2, H2, asub32)
THCALL(TH_OPIVX2_RM, th_vasub_vx_w, OP_SSS_W, H4, H4, asub32)
THCALL(TH_OPIVX2_RM, th_vasub_vx_d, OP_SSS_D, H8, H8, asub64)
GEN_TH_VX_RM(th_vasub_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vasub_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vasub_vx_w, 4, 4, clearl_th)
GEN_TH_VX_RM(th_vasub_vx_d, 8, 8, clearq_th)

/* Vector Single-Width Fractional Multiply with Rounding and Saturation */
THCALL(TH_OPIVV2_RM, th_vsmul_vv_b, OP_SSS_B, H1, H1, H1, vsmul8)
THCALL(TH_OPIVV2_RM, th_vsmul_vv_h, OP_SSS_H, H2, H2, H2, vsmul16)
THCALL(TH_OPIVV2_RM, th_vsmul_vv_w, OP_SSS_W, H4, H4, H4, vsmul32)
THCALL(TH_OPIVV2_RM, th_vsmul_vv_d, OP_SSS_D, H8, H8, H8, vsmul64)
GEN_TH_VV_RM(th_vsmul_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vsmul_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vsmul_vv_w, 4, 4, clearl_th)
GEN_TH_VV_RM(th_vsmul_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2_RM, th_vsmul_vx_b, OP_SSS_B, H1, H1, vsmul8)
THCALL(TH_OPIVX2_RM, th_vsmul_vx_h, OP_SSS_H, H2, H2, vsmul16)
THCALL(TH_OPIVX2_RM, th_vsmul_vx_w, OP_SSS_W, H4, H4, vsmul32)
THCALL(TH_OPIVX2_RM, th_vsmul_vx_d, OP_SSS_D, H8, H8, vsmul64)
GEN_TH_VX_RM(th_vsmul_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vsmul_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vsmul_vx_w, 4, 4, clearl_th)
GEN_TH_VX_RM(th_vsmul_vx_d, 8, 8, clearq_th)

/*
 * Vector Widening Saturating Scaled Multiply-Add
 *
 * RVV1.0 does not have similar instructions
 */

static inline uint16_t
vwsmaccu8(CPURISCVState *env, int vxrm, uint8_t a, uint8_t b,
          uint16_t c)
{
    uint8_t round;
    uint16_t res = (uint16_t)a * b;

    round = get_round(vxrm, res, 4);
    res   = (res >> 4) + round;
    return saddu16(env, vxrm, c, res);
}

static inline uint32_t
vwsmaccu16(CPURISCVState *env, int vxrm, uint16_t a, uint16_t b,
           uint32_t c)
{
    uint8_t round;
    uint32_t res = (uint32_t)a * b;

    round = get_round(vxrm, res, 8);
    res   = (res >> 8) + round;
    return saddu32(env, vxrm, c, res);
}

static inline uint64_t
vwsmaccu32(CPURISCVState *env, int vxrm, uint32_t a, uint32_t b,
           uint64_t c)
{
    uint8_t round;
    uint64_t res = (uint64_t)a * b;

    round = get_round(vxrm, res, 16);
    res   = (res >> 16) + round;
    return saddu64(env, vxrm, c, res);
}

#define TH_OPIVV3_RM(NAME, TD, T1, T2, TX1, TX2, HD, HS1, HS2, OP) \
static inline void                                                 \
do_##NAME(void *vd, void *vs1, void *vs2, int i,                   \
          CPURISCVState *env, int vxrm)                            \
{                                                                  \
    TX1 s1 = *((T1 *)vs1 + HS1(i));                                \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                                \
    TD d = *((TD *)vd + HD(i));                                    \
    *((TD *)vd + HD(i)) = OP(env, vxrm, s2, s1, d);                \
}

THCALL(TH_OPIVV3_RM, th_vwsmaccu_vv_b, WOP_UUU_B, H2, H1, H1, vwsmaccu8)
THCALL(TH_OPIVV3_RM, th_vwsmaccu_vv_h, WOP_UUU_H, H4, H2, H2, vwsmaccu16)
THCALL(TH_OPIVV3_RM, th_vwsmaccu_vv_w, WOP_UUU_W, H8, H4, H4, vwsmaccu32)
GEN_TH_VV_RM(th_vwsmaccu_vv_b, 1, 2, clearh_th)
GEN_TH_VV_RM(th_vwsmaccu_vv_h, 2, 4, clearl_th)
GEN_TH_VV_RM(th_vwsmaccu_vv_w, 4, 8, clearq_th)

#define TH_OPIVX3_RM(NAME, TD, T1, T2, TX1, TX2, HD, HS2, OP)      \
static inline void                                                 \
do_##NAME(void *vd, target_long s1, void *vs2, int i,              \
          CPURISCVState *env, int vxrm)                            \
{                                                                  \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                                \
    TD d = *((TD *)vd + HD(i));                                    \
    *((TD *)vd + HD(i)) = OP(env, vxrm, s2, (TX1)(T1)s1, d);       \
}

THCALL(TH_OPIVX3_RM, th_vwsmaccu_vx_b, WOP_UUU_B, H2, H1, vwsmaccu8)
THCALL(TH_OPIVX3_RM, th_vwsmaccu_vx_h, WOP_UUU_H, H4, H2, vwsmaccu16)
THCALL(TH_OPIVX3_RM, th_vwsmaccu_vx_w, WOP_UUU_W, H8, H4, vwsmaccu32)
GEN_TH_VX_RM(th_vwsmaccu_vx_b, 1, 2, clearh_th)
GEN_TH_VX_RM(th_vwsmaccu_vx_h, 2, 4, clearl_th)
GEN_TH_VX_RM(th_vwsmaccu_vx_w, 4, 8, clearq_th)

static inline int16_t
vwsmacc8(CPURISCVState *env, int vxrm, int8_t a, int8_t b, int16_t c)
{
    uint8_t round;
    int16_t res = (int16_t)a * b;

    round = get_round(vxrm, res, 4);
    res   = (res >> 4) + round;
    return sadd16(env, vxrm, c, res);
}

static inline int32_t
vwsmacc16(CPURISCVState *env, int vxrm, int16_t a, int16_t b, int32_t c)
{
    uint8_t round;
    int32_t res = (int32_t)a * b;

    round = get_round(vxrm, res, 8);
    res   = (res >> 8) + round;
    return sadd32(env, vxrm, c, res);

}

static inline int64_t
vwsmacc32(CPURISCVState *env, int vxrm, int32_t a, int32_t b, int64_t c)
{
    uint8_t round;
    int64_t res = (int64_t)a * b;

    round = get_round(vxrm, res, 16);
    res   = (res >> 16) + round;
    return sadd64(env, vxrm, c, res);
}

THCALL(TH_OPIVV3_RM, th_vwsmacc_vv_b, WOP_SSS_B, H2, H1, H1, vwsmacc8)
THCALL(TH_OPIVV3_RM, th_vwsmacc_vv_h, WOP_SSS_H, H4, H2, H2, vwsmacc16)
THCALL(TH_OPIVV3_RM, th_vwsmacc_vv_w, WOP_SSS_W, H8, H4, H4, vwsmacc32)
GEN_TH_VV_RM(th_vwsmacc_vv_b, 1, 2, clearh_th)
GEN_TH_VV_RM(th_vwsmacc_vv_h, 2, 4, clearl_th)
GEN_TH_VV_RM(th_vwsmacc_vv_w, 4, 8, clearq_th)
THCALL(TH_OPIVX3_RM, th_vwsmacc_vx_b, WOP_SSS_B, H2, H1, vwsmacc8)
THCALL(TH_OPIVX3_RM, th_vwsmacc_vx_h, WOP_SSS_H, H4, H2, vwsmacc16)
THCALL(TH_OPIVX3_RM, th_vwsmacc_vx_w, WOP_SSS_W, H8, H4, vwsmacc32)
GEN_TH_VX_RM(th_vwsmacc_vx_b, 1, 2, clearh_th)
GEN_TH_VX_RM(th_vwsmacc_vx_h, 2, 4, clearl_th)
GEN_TH_VX_RM(th_vwsmacc_vx_w, 4, 8, clearq_th)

static inline int16_t
vwsmaccsu8(CPURISCVState *env, int vxrm, uint8_t a, int8_t b, int16_t c)
{
    uint8_t round;
    int16_t res = a * (int16_t)b;

    round = get_round(vxrm, res, 4);
    res   = (res >> 4) + round;
    return ssub16(env, vxrm, c, res);
}

static inline int32_t
vwsmaccsu16(CPURISCVState *env, int vxrm, uint16_t a, int16_t b, uint32_t c)
{
    uint8_t round;
    int32_t res = a * (int32_t)b;

    round = get_round(vxrm, res, 8);
    res   = (res >> 8) + round;
    return ssub32(env, vxrm, c, res);
}

static inline int64_t
vwsmaccsu32(CPURISCVState *env, int vxrm, uint32_t a, int32_t b, int64_t c)
{
    uint8_t round;
    int64_t res = a * (int64_t)b;

    round = get_round(vxrm, res, 16);
    res   = (res >> 16) + round;
    return ssub64(env, vxrm, c, res);
}

THCALL(TH_OPIVV3_RM, th_vwsmaccsu_vv_b, WOP_SSU_B, H2, H1, H1, vwsmaccsu8)
THCALL(TH_OPIVV3_RM, th_vwsmaccsu_vv_h, WOP_SSU_H, H4, H2, H2, vwsmaccsu16)
THCALL(TH_OPIVV3_RM, th_vwsmaccsu_vv_w, WOP_SSU_W, H8, H4, H4, vwsmaccsu32)
GEN_TH_VV_RM(th_vwsmaccsu_vv_b, 1, 2, clearh_th)
GEN_TH_VV_RM(th_vwsmaccsu_vv_h, 2, 4, clearl_th)
GEN_TH_VV_RM(th_vwsmaccsu_vv_w, 4, 8, clearq_th)
THCALL(TH_OPIVX3_RM, th_vwsmaccsu_vx_b, WOP_SSU_B, H2, H1, vwsmaccsu8)
THCALL(TH_OPIVX3_RM, th_vwsmaccsu_vx_h, WOP_SSU_H, H4, H2, vwsmaccsu16)
THCALL(TH_OPIVX3_RM, th_vwsmaccsu_vx_w, WOP_SSU_W, H8, H4, vwsmaccsu32)
GEN_TH_VX_RM(th_vwsmaccsu_vx_b, 1, 2, clearh_th)
GEN_TH_VX_RM(th_vwsmaccsu_vx_h, 2, 4, clearl_th)
GEN_TH_VX_RM(th_vwsmaccsu_vx_w, 4, 8, clearq_th)

static inline int16_t
vwsmaccus8(CPURISCVState *env, int vxrm, int8_t a, uint8_t b, int16_t c)
{
    uint8_t round;
    int16_t res = (int16_t)a * b;

    round = get_round(vxrm, res, 4);
    res   = (res >> 4) + round;
    return ssub16(env, vxrm, c, res);
}

static inline int32_t
vwsmaccus16(CPURISCVState *env, int vxrm, int16_t a, uint16_t b, int32_t c)
{
    uint8_t round;
    int32_t res = (int32_t)a * b;

    round = get_round(vxrm, res, 8);
    res   = (res >> 8) + round;
    return ssub32(env, vxrm, c, res);
}

static inline int64_t
vwsmaccus32(CPURISCVState *env, int vxrm, int32_t a, uint32_t b, int64_t c)
{
    uint8_t round;
    int64_t res = (int64_t)a * b;

    round = get_round(vxrm, res, 16);
    res   = (res >> 16) + round;
    return ssub64(env, vxrm, c, res);
}

THCALL(TH_OPIVX3_RM, th_vwsmaccus_vx_b, WOP_SUS_B, H2, H1, vwsmaccus8)
THCALL(TH_OPIVX3_RM, th_vwsmaccus_vx_h, WOP_SUS_H, H4, H2, vwsmaccus16)
THCALL(TH_OPIVX3_RM, th_vwsmaccus_vx_w, WOP_SUS_W, H8, H4, vwsmaccus32)
GEN_TH_VX_RM(th_vwsmaccus_vx_b, 1, 2, clearh_th)
GEN_TH_VX_RM(th_vwsmaccus_vx_h, 2, 4, clearl_th)
GEN_TH_VX_RM(th_vwsmaccus_vx_w, 4, 8, clearq_th)

/* Vector Single-Width Scaling Shift Instructions */

THCALL(TH_OPIVV2_RM, th_vssrl_vv_b, OP_UUU_B, H1, H1, H1, vssrl8)
THCALL(TH_OPIVV2_RM, th_vssrl_vv_h, OP_UUU_H, H2, H2, H2, vssrl16)
THCALL(TH_OPIVV2_RM, th_vssrl_vv_w, OP_UUU_W, H4, H4, H4, vssrl32)
THCALL(TH_OPIVV2_RM, th_vssrl_vv_d, OP_UUU_D, H8, H8, H8, vssrl64)
GEN_TH_VV_RM(th_vssrl_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vssrl_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vssrl_vv_w, 4, 4, clearl_th)
GEN_TH_VV_RM(th_vssrl_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2_RM, th_vssrl_vx_b, OP_UUU_B, H1, H1, vssrl8)
THCALL(TH_OPIVX2_RM, th_vssrl_vx_h, OP_UUU_H, H2, H2, vssrl16)
THCALL(TH_OPIVX2_RM, th_vssrl_vx_w, OP_UUU_W, H4, H4, vssrl32)
THCALL(TH_OPIVX2_RM, th_vssrl_vx_d, OP_UUU_D, H8, H8, vssrl64)
GEN_TH_VX_RM(th_vssrl_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vssrl_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vssrl_vx_w, 4, 4, clearl_th)
GEN_TH_VX_RM(th_vssrl_vx_d, 8, 8, clearq_th)

THCALL(TH_OPIVV2_RM, th_vssra_vv_b, OP_SSS_B, H1, H1, H1, vssra8)
THCALL(TH_OPIVV2_RM, th_vssra_vv_h, OP_SSS_H, H2, H2, H2, vssra16)
THCALL(TH_OPIVV2_RM, th_vssra_vv_w, OP_SSS_W, H4, H4, H4, vssra32)
THCALL(TH_OPIVV2_RM, th_vssra_vv_d, OP_SSS_D, H8, H8, H8, vssra64)
GEN_TH_VV_RM(th_vssra_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vssra_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vssra_vv_w, 4, 4, clearl_th)
GEN_TH_VV_RM(th_vssra_vv_d, 8, 8, clearq_th)

THCALL(TH_OPIVX2_RM, th_vssra_vx_b, OP_SSS_B, H1, H1, vssra8)
THCALL(TH_OPIVX2_RM, th_vssra_vx_h, OP_SSS_H, H2, H2, vssra16)
THCALL(TH_OPIVX2_RM, th_vssra_vx_w, OP_SSS_W, H4, H4, vssra32)
THCALL(TH_OPIVX2_RM, th_vssra_vx_d, OP_SSS_D, H8, H8, vssra64)
GEN_TH_VX_RM(th_vssra_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vssra_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vssra_vx_w, 4, 4, clearl_th)
GEN_TH_VX_RM(th_vssra_vx_d, 8, 8, clearq_th)

/* Vector Narrowing Fixed-Point Clip Instructions */
THCALL(TH_OPIVV2_RM, th_vnclip_vv_b, NOP_SSS_B, H1, H2, H1, vnclip8)
THCALL(TH_OPIVV2_RM, th_vnclip_vv_h, NOP_SSS_H, H2, H4, H2, vnclip16)
THCALL(TH_OPIVV2_RM, th_vnclip_vv_w, NOP_SSS_W, H4, H8, H4, vnclip32)
GEN_TH_VV_RM(th_vnclip_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vnclip_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vnclip_vv_w, 4, 4, clearl_th)

THCALL(TH_OPIVX2_RM, th_vnclip_vx_b, NOP_SSS_B, H1, H2, vnclip8)
THCALL(TH_OPIVX2_RM, th_vnclip_vx_h, NOP_SSS_H, H2, H4, vnclip16)
THCALL(TH_OPIVX2_RM, th_vnclip_vx_w, NOP_SSS_W, H4, H8, vnclip32)
GEN_TH_VX_RM(th_vnclip_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vnclip_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vnclip_vx_w, 4, 4, clearl_th)

THCALL(TH_OPIVV2_RM, th_vnclipu_vv_b, NOP_UUU_B, H1, H2, H1, vnclipu8)
THCALL(TH_OPIVV2_RM, th_vnclipu_vv_h, NOP_UUU_H, H2, H4, H2, vnclipu16)
THCALL(TH_OPIVV2_RM, th_vnclipu_vv_w, NOP_UUU_W, H4, H8, H4, vnclipu32)
GEN_TH_VV_RM(th_vnclipu_vv_b, 1, 1, clearb_th)
GEN_TH_VV_RM(th_vnclipu_vv_h, 2, 2, clearh_th)
GEN_TH_VV_RM(th_vnclipu_vv_w, 4, 4, clearl_th)

THCALL(TH_OPIVX2_RM, th_vnclipu_vx_b, NOP_UUU_B, H1, H2, vnclipu8)
THCALL(TH_OPIVX2_RM, th_vnclipu_vx_h, NOP_UUU_H, H2, H4, vnclipu16)
THCALL(TH_OPIVX2_RM, th_vnclipu_vx_w, NOP_UUU_W, H4, H8, vnclipu32)
GEN_TH_VX_RM(th_vnclipu_vx_b, 1, 1, clearb_th)
GEN_TH_VX_RM(th_vnclipu_vx_h, 2, 2, clearh_th)
GEN_TH_VX_RM(th_vnclipu_vx_w, 4, 4, clearl_th)

/*
 * Vector Float Point Arithmetic Instructions
 */

/* Vector Single-Width Floating-Point Add/Subtract Instructions */

/*
 * Some functions or macros are just the same as RVV1.0.
 * But it is not worthy to extract them from RVV1.0, so we just copy
 * them.
 */
#define TH_OPFVV2(NAME, TD, T1, T2, TX1, TX2, HD, HS1, HS2, OP)\
static void do_##NAME(void *vd, void *vs1, void *vs2, int i,   \
                      CPURISCVState *env)                      \
{                                                              \
    TX1 s1 = *((T1 *)vs1 + HS1(i));                            \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                            \
    *((TD *)vd + HD(i)) = OP(s2, s1, &env->fp_status);         \
}

#define GEN_TH_VV_ENV(NAME, ESZ, DSZ, CLEAR_FN)           \
void HELPER(NAME)(void *vd, void *v0, void *vs1,          \
                  void *vs2, CPURISCVState *env,          \
                  uint32_t desc)                          \
{                                                         \
    uint32_t vlmax = th_maxsz(desc) / ESZ;                \
    uint32_t mlen = th_mlen(desc);                        \
    uint32_t vm = th_vm(desc);                            \
    uint32_t vl = env->vl;                                \
    uint32_t i;                                           \
                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                         \
    for (i = env->vstart; i < vl; i++) {                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {          \
            continue;                                     \
        }                                                 \
        do_##NAME(vd, vs1, vs2, i, env);                  \
    }                                                     \
    env->vstart = 0;                                      \
    CLEAR_FN(vd, vl, vl * DSZ,  vlmax * DSZ);             \
}

THCALL(TH_OPFVV2, th_vfadd_vv_bh, OP_UUU_H, H2, H2, H2, bfloat16_add)
THCALL(TH_OPFVV2, th_vfadd_vv_h, OP_UUU_H, H2, H2, H2, float16_add)
THCALL(TH_OPFVV2, th_vfadd_vv_w, OP_UUU_W, H4, H4, H4, float32_add)
THCALL(TH_OPFVV2, th_vfadd_vv_d, OP_UUU_D, H8, H8, H8, float64_add)
GEN_TH_VV_ENV(th_vfadd_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfadd_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfadd_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfadd_vv_d, 8, 8, clearq_th)

#define TH_OPFVF2(NAME, TD, T1, T2, TX1, TX2, HD, HS2, OP)     \
static void do_##NAME(void *vd, uint64_t s1, void *vs2, int i, \
                      CPURISCVState *env)                      \
{                                                              \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                            \
    *((TD *)vd + HD(i)) = OP(s2, (TX1)(T1)s1, &env->fp_status);\
}

#define GEN_TH_VF(NAME, ESZ, DSZ, CLEAR_FN)               \
void HELPER(NAME)(void *vd, void *v0, uint64_t s1,        \
                  void *vs2, CPURISCVState *env,          \
                  uint32_t desc)                          \
{                                                         \
    uint32_t vlmax = th_maxsz(desc) / ESZ;                \
    uint32_t mlen = th_mlen(desc);                        \
    uint32_t vm = th_vm(desc);                            \
    uint32_t vl = env->vl;                                \
    uint32_t i;                                           \
                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                         \
    for (i = env->vstart; i < vl; i++) {                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {          \
            continue;                                     \
        }                                                 \
        do_##NAME(vd, s1, vs2, i, env);                   \
    }                                                     \
    env->vstart = 0;                                      \
    CLEAR_FN(vd, vl, vl * DSZ,  vlmax * DSZ);             \
}

THCALL(TH_OPFVF2, th_vfadd_vf_bh, OP_UUU_H, H2, H2, bfloat16_add)
THCALL(TH_OPFVF2, th_vfadd_vf_h, OP_UUU_H, H2, H2, float16_add)
THCALL(TH_OPFVF2, th_vfadd_vf_w, OP_UUU_W, H4, H4, float32_add)
THCALL(TH_OPFVF2, th_vfadd_vf_d, OP_UUU_D, H8, H8, float64_add)
GEN_TH_VF(th_vfadd_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfadd_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfadd_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfadd_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV2, th_vfsub_vv_bh, OP_UUU_H, H2, H2, H2, bfloat16_sub)
THCALL(TH_OPFVV2, th_vfsub_vv_h, OP_UUU_H, H2, H2, H2, float16_sub)
THCALL(TH_OPFVV2, th_vfsub_vv_w, OP_UUU_W, H4, H4, H4, float32_sub)
THCALL(TH_OPFVV2, th_vfsub_vv_d, OP_UUU_D, H8, H8, H8, float64_sub)
GEN_TH_VV_ENV(th_vfsub_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfsub_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfsub_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfsub_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfsub_vf_bh, OP_UUU_H, H2, H2, bfloat16_sub)
THCALL(TH_OPFVF2, th_vfsub_vf_h, OP_UUU_H, H2, H2, float16_sub)
THCALL(TH_OPFVF2, th_vfsub_vf_w, OP_UUU_W, H4, H4, float32_sub)
THCALL(TH_OPFVF2, th_vfsub_vf_d, OP_UUU_D, H8, H8, float64_sub)
GEN_TH_VF(th_vfsub_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfsub_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfsub_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfsub_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVF2, th_vfrsub_vf_bh, OP_UUU_H, H2, H2, bfloat16_rsub)
THCALL(TH_OPFVF2, th_vfrsub_vf_h, OP_UUU_H, H2, H2, float16_rsub)
THCALL(TH_OPFVF2, th_vfrsub_vf_w, OP_UUU_W, H4, H4, float32_rsub)
THCALL(TH_OPFVF2, th_vfrsub_vf_d, OP_UUU_D, H8, H8, float64_rsub)
GEN_TH_VF(th_vfrsub_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfrsub_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfrsub_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfrsub_vf_d, 8, 8, clearq_th)

/* Vector Widening Floating-Point Add/Subtract Instructions */

THCALL(TH_OPFVV2, th_vfwadd_vv_bh, WOP_UUU_H, H4, H2, H2, vfwaddbf16)
THCALL(TH_OPFVV2, th_vfwadd_vv_h, WOP_UUU_H, H4, H2, H2, vfwadd16)
THCALL(TH_OPFVV2, th_vfwadd_vv_w, WOP_UUU_W, H8, H4, H4, vfwadd32)
GEN_TH_VV_ENV(th_vfwadd_vv_bh, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwadd_vv_h, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwadd_vv_w, 4, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfwadd_vf_bh, WOP_UUU_H, H4, H2, vfwaddbf16)
THCALL(TH_OPFVF2, th_vfwadd_vf_h, WOP_UUU_H, H4, H2, vfwadd16)
THCALL(TH_OPFVF2, th_vfwadd_vf_w, WOP_UUU_W, H8, H4, vfwadd32)
GEN_TH_VF(th_vfwadd_vf_bh, 2, 4, clearl_th)
GEN_TH_VF(th_vfwadd_vf_h, 2, 4, clearl_th)
GEN_TH_VF(th_vfwadd_vf_w, 4, 8, clearq_th)

THCALL(TH_OPFVV2, th_vfwsub_vv_bh, WOP_UUU_H, H4, H2, H2, vfwsubbf16)
THCALL(TH_OPFVV2, th_vfwsub_vv_h, WOP_UUU_H, H4, H2, H2, vfwsub16)
THCALL(TH_OPFVV2, th_vfwsub_vv_w, WOP_UUU_W, H8, H4, H4, vfwsub32)
GEN_TH_VV_ENV(th_vfwsub_vv_bh, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwsub_vv_h, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwsub_vv_w, 4, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfwsub_vf_bh, WOP_UUU_H, H4, H2, vfwsubbf16)
THCALL(TH_OPFVF2, th_vfwsub_vf_h, WOP_UUU_H, H4, H2, vfwsub16)
THCALL(TH_OPFVF2, th_vfwsub_vf_w, WOP_UUU_W, H8, H4, vfwsub32)
GEN_TH_VF(th_vfwsub_vf_bh, 2, 4, clearl_th)
GEN_TH_VF(th_vfwsub_vf_h, 2, 4, clearl_th)
GEN_TH_VF(th_vfwsub_vf_w, 4, 8, clearq_th)

THCALL(TH_OPFVV2, th_vfwadd_wv_bh, WOP_WUUU_H, H4, H2, H2, vfwaddwbf16)
THCALL(TH_OPFVV2, th_vfwadd_wv_h, WOP_WUUU_H, H4, H2, H2, vfwaddw16)
THCALL(TH_OPFVV2, th_vfwadd_wv_w, WOP_WUUU_W, H8, H4, H4, vfwaddw32)
GEN_TH_VV_ENV(th_vfwadd_wv_bh, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwadd_wv_h, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwadd_wv_w, 4, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfwadd_wf_bh, WOP_WUUU_H, H4, H2, vfwaddwbf16)
THCALL(TH_OPFVF2, th_vfwadd_wf_h, WOP_WUUU_H, H4, H2, vfwaddw16)
THCALL(TH_OPFVF2, th_vfwadd_wf_w, WOP_WUUU_W, H8, H4, vfwaddw32)
GEN_TH_VF(th_vfwadd_wf_bh, 2, 4, clearl_th)
GEN_TH_VF(th_vfwadd_wf_h, 2, 4, clearl_th)
GEN_TH_VF(th_vfwadd_wf_w, 4, 8, clearq_th)

THCALL(TH_OPFVV2, th_vfwsub_wv_bh, WOP_WUUU_H, H4, H2, H2, vfwsubwbf16)
THCALL(TH_OPFVV2, th_vfwsub_wv_h, WOP_WUUU_H, H4, H2, H2, vfwsubw16)
THCALL(TH_OPFVV2, th_vfwsub_wv_w, WOP_WUUU_W, H8, H4, H4, vfwsubw32)
GEN_TH_VV_ENV(th_vfwsub_wv_bh, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwsub_wv_h, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwsub_wv_w, 4, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfwsub_wf_bh, WOP_WUUU_H, H4, H2, vfwsubwbf16)
THCALL(TH_OPFVF2, th_vfwsub_wf_h, WOP_WUUU_H, H4, H2, vfwsubw16)
THCALL(TH_OPFVF2, th_vfwsub_wf_w, WOP_WUUU_W, H8, H4, vfwsubw32)
GEN_TH_VF(th_vfwsub_wf_bh, 2, 4, clearl_th)
GEN_TH_VF(th_vfwsub_wf_h, 2, 4, clearl_th)
GEN_TH_VF(th_vfwsub_wf_w, 4, 8, clearq_th)

/* Vector Single-Width Floating-Point Multiply/Divide Instructions */
THCALL(TH_OPFVV2, th_vfmul_vv_bh, OP_UUU_H, H2, H2, H2, bfloat16_mul)
THCALL(TH_OPFVV2, th_vfmul_vv_h, OP_UUU_H, H2, H2, H2, float16_mul)
THCALL(TH_OPFVV2, th_vfmul_vv_w, OP_UUU_W, H4, H4, H4, float32_mul)
THCALL(TH_OPFVV2, th_vfmul_vv_d, OP_UUU_D, H8, H8, H8, float64_mul)
GEN_TH_VV_ENV(th_vfmul_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmul_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmul_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfmul_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfmul_vf_bh, OP_UUU_H, H2, H2, bfloat16_mul)
THCALL(TH_OPFVF2, th_vfmul_vf_h, OP_UUU_H, H2, H2, float16_mul)
THCALL(TH_OPFVF2, th_vfmul_vf_w, OP_UUU_W, H4, H4, float32_mul)
THCALL(TH_OPFVF2, th_vfmul_vf_d, OP_UUU_D, H8, H8, float64_mul)
GEN_TH_VF(th_vfmul_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfmul_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfmul_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfmul_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV2, th_vfdiv_vv_bh, OP_UUU_H, H2, H2, H2, bfloat16_div)
THCALL(TH_OPFVV2, th_vfdiv_vv_h, OP_UUU_H, H2, H2, H2, float16_div)
THCALL(TH_OPFVV2, th_vfdiv_vv_w, OP_UUU_W, H4, H4, H4, float32_div)
THCALL(TH_OPFVV2, th_vfdiv_vv_d, OP_UUU_D, H8, H8, H8, float64_div)
GEN_TH_VV_ENV(th_vfdiv_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfdiv_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfdiv_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfdiv_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfdiv_vf_bh, OP_UUU_H, H2, H2, bfloat16_div)
THCALL(TH_OPFVF2, th_vfdiv_vf_h, OP_UUU_H, H2, H2, float16_div)
THCALL(TH_OPFVF2, th_vfdiv_vf_w, OP_UUU_W, H4, H4, float32_div)
THCALL(TH_OPFVF2, th_vfdiv_vf_d, OP_UUU_D, H8, H8, float64_div)
GEN_TH_VF(th_vfdiv_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfdiv_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfdiv_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfdiv_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVF2, th_vfrdiv_vf_bh, OP_UUU_H, H2, H2, bfloat16_rdiv)
THCALL(TH_OPFVF2, th_vfrdiv_vf_h, OP_UUU_H, H2, H2, float16_rdiv)
THCALL(TH_OPFVF2, th_vfrdiv_vf_w, OP_UUU_W, H4, H4, float32_rdiv)
THCALL(TH_OPFVF2, th_vfrdiv_vf_d, OP_UUU_D, H8, H8, float64_rdiv)
GEN_TH_VF(th_vfrdiv_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfrdiv_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfrdiv_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfrdiv_vf_d, 8, 8, clearq_th)

/* Vector Widening Floating-Point Multiply */

THCALL(TH_OPFVV2, th_vfwmul_vv_bh, WOP_UUU_H, H4, H2, H2, vfwmulbf16)
THCALL(TH_OPFVV2, th_vfwmul_vv_h, WOP_UUU_H, H4, H2, H2, vfwmul16)
THCALL(TH_OPFVV2, th_vfwmul_vv_w, WOP_UUU_W, H8, H4, H4, vfwmul32)
GEN_TH_VV_ENV(th_vfwmul_vv_bh, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwmul_vv_h, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwmul_vv_w, 4, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfwmul_vf_bh, WOP_UUU_H, H4, H2, vfwmulbf16)
THCALL(TH_OPFVF2, th_vfwmul_vf_h, WOP_UUU_H, H4, H2, vfwmul16)
THCALL(TH_OPFVF2, th_vfwmul_vf_w, WOP_UUU_W, H8, H4, vfwmul32)
GEN_TH_VF(th_vfwmul_vf_bh, 2, 4, clearl_th)
GEN_TH_VF(th_vfwmul_vf_h, 2, 4, clearl_th)
GEN_TH_VF(th_vfwmul_vf_w, 4, 8, clearq_th)

/* Vector Single-Width Floating-Point Fused Multiply-Add Instructions */
#define TH_OPFVV3(NAME, TD, T1, T2, TX1, TX2, HD, HS1, HS2, OP)    \
static void do_##NAME(void *vd, void *vs1, void *vs2, int i,       \
        CPURISCVState *env)                                        \
{                                                                  \
    TX1 s1 = *((T1 *)vs1 + HS1(i));                                \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                                \
    TD d = *((TD *)vd + HD(i));                                    \
    *((TD *)vd + HD(i)) = OP(s2, s1, d, &env->fp_status);          \
}

THCALL(TH_OPFVV3, th_vfmacc_vv_bh, OP_UUU_H, H2, H2, H2, fmaccbf16)
THCALL(TH_OPFVV3, th_vfmacc_vv_h, OP_UUU_H, H2, H2, H2, fmacc16)
THCALL(TH_OPFVV3, th_vfmacc_vv_w, OP_UUU_W, H4, H4, H4, fmacc32)
THCALL(TH_OPFVV3, th_vfmacc_vv_d, OP_UUU_D, H8, H8, H8, fmacc64)
GEN_TH_VV_ENV(th_vfmacc_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmacc_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmacc_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfmacc_vv_d, 8, 8, clearq_th)

#define TH_OPFVF3(NAME, TD, T1, T2, TX1, TX2, HD, HS2, OP)        \
static void do_##NAME(void *vd, uint64_t s1, void *vs2, int i,    \
        CPURISCVState *env)                                       \
{                                                                 \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                               \
    TD d = *((TD *)vd + HD(i));                                   \
    *((TD *)vd + HD(i)) = OP(s2, (TX1)(T1)s1, d, &env->fp_status);\
}

THCALL(TH_OPFVF3, th_vfmacc_vf_bh, OP_UUU_H, H2, H2, fmaccbf16)
THCALL(TH_OPFVF3, th_vfmacc_vf_h, OP_UUU_H, H2, H2, fmacc16)
THCALL(TH_OPFVF3, th_vfmacc_vf_w, OP_UUU_W, H4, H4, fmacc32)
THCALL(TH_OPFVF3, th_vfmacc_vf_d, OP_UUU_D, H8, H8, fmacc64)
GEN_TH_VF(th_vfmacc_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfmacc_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfmacc_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfmacc_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfnmacc_vv_bh, OP_UUU_H, H2, H2, H2, fnmaccbf16)
THCALL(TH_OPFVV3, th_vfnmacc_vv_h, OP_UUU_H, H2, H2, H2, fnmacc16)
THCALL(TH_OPFVV3, th_vfnmacc_vv_w, OP_UUU_W, H4, H4, H4, fnmacc32)
THCALL(TH_OPFVV3, th_vfnmacc_vv_d, OP_UUU_D, H8, H8, H8, fnmacc64)
GEN_TH_VV_ENV(th_vfnmacc_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfnmacc_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfnmacc_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfnmacc_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfnmacc_vf_bh, OP_UUU_H, H2, H2, fnmaccbf16)
THCALL(TH_OPFVF3, th_vfnmacc_vf_h, OP_UUU_H, H2, H2, fnmacc16)
THCALL(TH_OPFVF3, th_vfnmacc_vf_w, OP_UUU_W, H4, H4, fnmacc32)
THCALL(TH_OPFVF3, th_vfnmacc_vf_d, OP_UUU_D, H8, H8, fnmacc64)
GEN_TH_VF(th_vfnmacc_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfnmacc_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfnmacc_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfnmacc_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfmsac_vv_bh, OP_UUU_H, H2, H2, H2, fmsacbf16)
THCALL(TH_OPFVV3, th_vfmsac_vv_h, OP_UUU_H, H2, H2, H2, fmsac16)
THCALL(TH_OPFVV3, th_vfmsac_vv_w, OP_UUU_W, H4, H4, H4, fmsac32)
THCALL(TH_OPFVV3, th_vfmsac_vv_d, OP_UUU_D, H8, H8, H8, fmsac64)
GEN_TH_VV_ENV(th_vfmsac_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmsac_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmsac_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfmsac_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfmsac_vf_bh, OP_UUU_H, H2, H2, fmsacbf16)
THCALL(TH_OPFVF3, th_vfmsac_vf_h, OP_UUU_H, H2, H2, fmsac16)
THCALL(TH_OPFVF3, th_vfmsac_vf_w, OP_UUU_W, H4, H4, fmsac32)
THCALL(TH_OPFVF3, th_vfmsac_vf_d, OP_UUU_D, H8, H8, fmsac64)
GEN_TH_VF(th_vfmsac_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfmsac_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfmsac_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfmsac_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfnmsac_vv_bh, OP_UUU_H, H2, H2, H2, fnmsacbf16)
THCALL(TH_OPFVV3, th_vfnmsac_vv_h, OP_UUU_H, H2, H2, H2, fnmsac16)
THCALL(TH_OPFVV3, th_vfnmsac_vv_w, OP_UUU_W, H4, H4, H4, fnmsac32)
THCALL(TH_OPFVV3, th_vfnmsac_vv_d, OP_UUU_D, H8, H8, H8, fnmsac64)
GEN_TH_VV_ENV(th_vfnmsac_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfnmsac_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfnmsac_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfnmsac_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfnmsac_vf_bh, OP_UUU_H, H2, H2, fnmsacbf16)
THCALL(TH_OPFVF3, th_vfnmsac_vf_h, OP_UUU_H, H2, H2, fnmsac16)
THCALL(TH_OPFVF3, th_vfnmsac_vf_w, OP_UUU_W, H4, H4, fnmsac32)
THCALL(TH_OPFVF3, th_vfnmsac_vf_d, OP_UUU_D, H8, H8, fnmsac64)
GEN_TH_VF(th_vfnmsac_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfnmsac_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfnmsac_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfnmsac_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfmadd_vv_bh, OP_UUU_H, H2, H2, H2, fmaddbf16)
THCALL(TH_OPFVV3, th_vfmadd_vv_h, OP_UUU_H, H2, H2, H2, fmadd16)
THCALL(TH_OPFVV3, th_vfmadd_vv_w, OP_UUU_W, H4, H4, H4, fmadd32)
THCALL(TH_OPFVV3, th_vfmadd_vv_d, OP_UUU_D, H8, H8, H8, fmadd64)
GEN_TH_VV_ENV(th_vfmadd_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmadd_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmadd_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfmadd_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfmadd_vf_bh, OP_UUU_H, H2, H2, fmaddbf16)
THCALL(TH_OPFVF3, th_vfmadd_vf_h, OP_UUU_H, H2, H2, fmadd16)
THCALL(TH_OPFVF3, th_vfmadd_vf_w, OP_UUU_W, H4, H4, fmadd32)
THCALL(TH_OPFVF3, th_vfmadd_vf_d, OP_UUU_D, H8, H8, fmadd64)
GEN_TH_VF(th_vfmadd_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfmadd_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfmadd_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfmadd_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfnmadd_vv_bh, OP_UUU_H, H2, H2, H2, fnmaddbf16)
THCALL(TH_OPFVV3, th_vfnmadd_vv_h, OP_UUU_H, H2, H2, H2, fnmadd16)
THCALL(TH_OPFVV3, th_vfnmadd_vv_w, OP_UUU_W, H4, H4, H4, fnmadd32)
THCALL(TH_OPFVV3, th_vfnmadd_vv_d, OP_UUU_D, H8, H8, H8, fnmadd64)
GEN_TH_VV_ENV(th_vfnmadd_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfnmadd_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfnmadd_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfnmadd_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfnmadd_vf_bh, OP_UUU_H, H2, H2, fnmaddbf16)
THCALL(TH_OPFVF3, th_vfnmadd_vf_h, OP_UUU_H, H2, H2, fnmadd16)
THCALL(TH_OPFVF3, th_vfnmadd_vf_w, OP_UUU_W, H4, H4, fnmadd32)
THCALL(TH_OPFVF3, th_vfnmadd_vf_d, OP_UUU_D, H8, H8, fnmadd64)
GEN_TH_VF(th_vfnmadd_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfnmadd_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfnmadd_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfnmadd_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfmsub_vv_bh, OP_UUU_H, H2, H2, H2, fmsubbf16)
THCALL(TH_OPFVV3, th_vfmsub_vv_h, OP_UUU_H, H2, H2, H2, fmsub16)
THCALL(TH_OPFVV3, th_vfmsub_vv_w, OP_UUU_W, H4, H4, H4, fmsub32)
THCALL(TH_OPFVV3, th_vfmsub_vv_d, OP_UUU_D, H8, H8, H8, fmsub64)
GEN_TH_VV_ENV(th_vfmsub_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmsub_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmsub_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfmsub_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfmsub_vf_bh, OP_UUU_H, H2, H2, fmsubbf16)
THCALL(TH_OPFVF3, th_vfmsub_vf_h, OP_UUU_H, H2, H2, fmsub16)
THCALL(TH_OPFVF3, th_vfmsub_vf_w, OP_UUU_W, H4, H4, fmsub32)
THCALL(TH_OPFVF3, th_vfmsub_vf_d, OP_UUU_D, H8, H8, fmsub64)
GEN_TH_VF(th_vfmsub_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfmsub_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfmsub_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfmsub_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfnmsub_vv_bh, OP_UUU_H, H2, H2, H2, fnmsubbf16)
THCALL(TH_OPFVV3, th_vfnmsub_vv_h, OP_UUU_H, H2, H2, H2, fnmsub16)
THCALL(TH_OPFVV3, th_vfnmsub_vv_w, OP_UUU_W, H4, H4, H4, fnmsub32)
THCALL(TH_OPFVV3, th_vfnmsub_vv_d, OP_UUU_D, H8, H8, H8, fnmsub64)
GEN_TH_VV_ENV(th_vfnmsub_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfnmsub_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfnmsub_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfnmsub_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfnmsub_vf_bh, OP_UUU_H, H2, H2, fnmsubbf16)
THCALL(TH_OPFVF3, th_vfnmsub_vf_h, OP_UUU_H, H2, H2, fnmsub16)
THCALL(TH_OPFVF3, th_vfnmsub_vf_w, OP_UUU_W, H4, H4, fnmsub32)
THCALL(TH_OPFVF3, th_vfnmsub_vf_d, OP_UUU_D, H8, H8, fnmsub64)
GEN_TH_VF(th_vfnmsub_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfnmsub_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfnmsub_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfnmsub_vf_d, 8, 8, clearq_th)

/* Vector Widening Floating-Point Fused Multiply-Add Instructions */

THCALL(TH_OPFVV3, th_vfwmacc_vv_bh, WOP_UUU_H, H4, H2, H2, fwmaccbf16)
THCALL(TH_OPFVV3, th_vfwmacc_vv_h, WOP_UUU_H, H4, H2, H2, fwmacc16)
THCALL(TH_OPFVV3, th_vfwmacc_vv_w, WOP_UUU_W, H8, H4, H4, fwmacc32)
GEN_TH_VV_ENV(th_vfwmacc_vv_bh, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwmacc_vv_h, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwmacc_vv_w, 4, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfwmacc_vf_bh, WOP_UUU_H, H4, H2, fwmaccbf16)
THCALL(TH_OPFVF3, th_vfwmacc_vf_h, WOP_UUU_H, H4, H2, fwmacc16)
THCALL(TH_OPFVF3, th_vfwmacc_vf_w, WOP_UUU_W, H8, H4, fwmacc32)
GEN_TH_VF(th_vfwmacc_vf_bh, 2, 4, clearl_th)
GEN_TH_VF(th_vfwmacc_vf_h, 2, 4, clearl_th)
GEN_TH_VF(th_vfwmacc_vf_w, 4, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfwnmacc_vv_bh, WOP_UUU_H, H4, H2, H2, fwnmaccbf16)
THCALL(TH_OPFVV3, th_vfwnmacc_vv_h, WOP_UUU_H, H4, H2, H2, fwnmacc16)
THCALL(TH_OPFVV3, th_vfwnmacc_vv_w, WOP_UUU_W, H8, H4, H4, fwnmacc32)
GEN_TH_VV_ENV(th_vfwnmacc_vv_bh, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwnmacc_vv_h, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwnmacc_vv_w, 4, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfwnmacc_vf_bh, WOP_UUU_H, H4, H2, fwnmaccbf16)
THCALL(TH_OPFVF3, th_vfwnmacc_vf_h, WOP_UUU_H, H4, H2, fwnmacc16)
THCALL(TH_OPFVF3, th_vfwnmacc_vf_w, WOP_UUU_W, H8, H4, fwnmacc32)
GEN_TH_VF(th_vfwnmacc_vf_bh, 2, 4, clearl_th)
GEN_TH_VF(th_vfwnmacc_vf_h, 2, 4, clearl_th)
GEN_TH_VF(th_vfwnmacc_vf_w, 4, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfwmsac_vv_bh, WOP_UUU_H, H4, H2, H2, fwmsacbf16)
THCALL(TH_OPFVV3, th_vfwmsac_vv_h, WOP_UUU_H, H4, H2, H2, fwmsac16)
THCALL(TH_OPFVV3, th_vfwmsac_vv_w, WOP_UUU_W, H8, H4, H4, fwmsac32)
GEN_TH_VV_ENV(th_vfwmsac_vv_bh, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwmsac_vv_h, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwmsac_vv_w, 4, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfwmsac_vf_bh, WOP_UUU_H, H4, H2, fwmsacbf16)
THCALL(TH_OPFVF3, th_vfwmsac_vf_h, WOP_UUU_H, H4, H2, fwmsac16)
THCALL(TH_OPFVF3, th_vfwmsac_vf_w, WOP_UUU_W, H8, H4, fwmsac32)
GEN_TH_VF(th_vfwmsac_vf_bh, 2, 4, clearl_th)
GEN_TH_VF(th_vfwmsac_vf_h, 2, 4, clearl_th)
GEN_TH_VF(th_vfwmsac_vf_w, 4, 8, clearq_th)

THCALL(TH_OPFVV3, th_vfwnmsac_vv_bh, WOP_UUU_H, H4, H2, H2, fwnmsacbf16)
THCALL(TH_OPFVV3, th_vfwnmsac_vv_h, WOP_UUU_H, H4, H2, H2, fwnmsac16)
THCALL(TH_OPFVV3, th_vfwnmsac_vv_w, WOP_UUU_W, H8, H4, H4, fwnmsac32)
GEN_TH_VV_ENV(th_vfwnmsac_vv_bh, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwnmsac_vv_h, 2, 4, clearl_th)
GEN_TH_VV_ENV(th_vfwnmsac_vv_w, 4, 8, clearq_th)
THCALL(TH_OPFVF3, th_vfwnmsac_vf_bh, WOP_UUU_H, H4, H2, fwnmsacbf16)
THCALL(TH_OPFVF3, th_vfwnmsac_vf_h, WOP_UUU_H, H4, H2, fwnmsac16)
THCALL(TH_OPFVF3, th_vfwnmsac_vf_w, WOP_UUU_W, H8, H4, fwnmsac32)
GEN_TH_VF(th_vfwnmsac_vf_bh, 2, 4, clearl_th)
GEN_TH_VF(th_vfwnmsac_vf_h, 2, 4, clearl_th)
GEN_TH_VF(th_vfwnmsac_vf_w, 4, 8, clearq_th)

/* Vector Floating-Point Square-Root Instruction */

#define TH_OPFVV1(NAME, TD, T2, TX2, HD, HS2, OP)        \
static void do_##NAME(void *vd, void *vs2, int i,      \
        CPURISCVState *env)                            \
{                                                      \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                    \
    *((TD *)vd + HD(i)) = OP(s2, &env->fp_status);     \
}

#define GEN_TH_V_ENV(NAME, ESZ, DSZ, CLEAR_FN)       \
void HELPER(NAME)(void *vd, void *v0, void *vs2,       \
        CPURISCVState *env, uint32_t desc)             \
{                                                      \
    uint32_t vlmax = th_maxsz(desc) / ESZ;             \
    uint32_t mlen = th_mlen(desc);                     \
    uint32_t vm = th_vm(desc);                         \
    uint32_t vl = env->vl;                             \
    uint32_t i;                                        \
                                                       \
    VSTART_CHECK_EARLY_EXIT(env);                      \
    if (vl == 0) {                                     \
        return;                                        \
    }                                                  \
    for (i = env->vstart; i < vl; i++) {               \
        if (!vm && !th_elem_mask(v0, mlen, i)) {       \
            continue;                                  \
        }                                              \
        do_##NAME(vd, vs2, i, env);                    \
    }                                                  \
    env->vstart = 0;                                   \
    CLEAR_FN(vd, vl, vl * DSZ,  vlmax * DSZ);          \
}

THCALL(TH_OPFVV1, th_vfsqrt_v_bh, OP_UU_H, H2, H2, bfloat16_sqrt)
THCALL(TH_OPFVV1, th_vfsqrt_v_h, OP_UU_H, H2, H2, float16_sqrt)
THCALL(TH_OPFVV1, th_vfsqrt_v_w, OP_UU_W, H4, H4, float32_sqrt)
THCALL(TH_OPFVV1, th_vfsqrt_v_d, OP_UU_D, H8, H8, float64_sqrt)
GEN_TH_V_ENV(th_vfsqrt_v_bh, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfsqrt_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfsqrt_v_w, 4, 4, clearl_th)
GEN_TH_V_ENV(th_vfsqrt_v_d, 8, 8, clearq_th)

/* Vector Floating-Point MIN/MAX Instructions */
THCALL(TH_OPFVV2, th_vfmin_vv_bh, OP_UUU_H, H2, H2, H2, bfloat16_minnum)
THCALL(TH_OPFVV2, th_vfmin_vv_h, OP_UUU_H, H2, H2, H2, float16_minnum)
THCALL(TH_OPFVV2, th_vfmin_vv_w, OP_UUU_W, H4, H4, H4, float32_minnum)
THCALL(TH_OPFVV2, th_vfmin_vv_d, OP_UUU_D, H8, H8, H8, float64_minnum)
GEN_TH_VV_ENV(th_vfmin_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmin_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmin_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfmin_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfmin_vf_bh, OP_UUU_H, H2, H2, bfloat16_minnum)
THCALL(TH_OPFVF2, th_vfmin_vf_h, OP_UUU_H, H2, H2, float16_minnum)
THCALL(TH_OPFVF2, th_vfmin_vf_w, OP_UUU_W, H4, H4, float32_minnum)
THCALL(TH_OPFVF2, th_vfmin_vf_d, OP_UUU_D, H8, H8, float64_minnum)
GEN_TH_VF(th_vfmin_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfmin_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfmin_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfmin_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV2, th_vfmax_vv_bh, OP_UUU_H, H2, H2, H2, bfloat16_maxnum)
THCALL(TH_OPFVV2, th_vfmax_vv_h, OP_UUU_H, H2, H2, H2, float16_maxnum)
THCALL(TH_OPFVV2, th_vfmax_vv_w, OP_UUU_W, H4, H4, H4, float32_maxnum)
THCALL(TH_OPFVV2, th_vfmax_vv_d, OP_UUU_D, H8, H8, H8, float64_maxnum)
GEN_TH_VV_ENV(th_vfmax_vv_bh, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmax_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfmax_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfmax_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfmax_vf_bh, OP_UUU_H, H2, H2, bfloat16_maxnum)
THCALL(TH_OPFVF2, th_vfmax_vf_h, OP_UUU_H, H2, H2, float16_maxnum)
THCALL(TH_OPFVF2, th_vfmax_vf_w, OP_UUU_W, H4, H4, float32_maxnum)
THCALL(TH_OPFVF2, th_vfmax_vf_d, OP_UUU_D, H8, H8, float64_maxnum)
GEN_TH_VF(th_vfmax_vf_bh, 2, 2, clearh_th)
GEN_TH_VF(th_vfmax_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfmax_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfmax_vf_d, 8, 8, clearq_th)

/* Vector Floating-Point Sign-Injection Instructions */

THCALL(TH_OPFVV2, th_vfsgnj_vv_h, OP_UUU_H, H2, H2, H2, fsgnj16)
THCALL(TH_OPFVV2, th_vfsgnj_vv_w, OP_UUU_W, H4, H4, H4, fsgnj32)
THCALL(TH_OPFVV2, th_vfsgnj_vv_d, OP_UUU_D, H8, H8, H8, fsgnj64)
GEN_TH_VV_ENV(th_vfsgnj_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfsgnj_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfsgnj_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfsgnj_vf_h, OP_UUU_H, H2, H2, fsgnj16)
THCALL(TH_OPFVF2, th_vfsgnj_vf_w, OP_UUU_W, H4, H4, fsgnj32)
THCALL(TH_OPFVF2, th_vfsgnj_vf_d, OP_UUU_D, H8, H8, fsgnj64)
GEN_TH_VF(th_vfsgnj_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfsgnj_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfsgnj_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV2, th_vfsgnjn_vv_h, OP_UUU_H, H2, H2, H2, fsgnjn16)
THCALL(TH_OPFVV2, th_vfsgnjn_vv_w, OP_UUU_W, H4, H4, H4, fsgnjn32)
THCALL(TH_OPFVV2, th_vfsgnjn_vv_d, OP_UUU_D, H8, H8, H8, fsgnjn64)
GEN_TH_VV_ENV(th_vfsgnjn_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfsgnjn_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfsgnjn_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfsgnjn_vf_h, OP_UUU_H, H2, H2, fsgnjn16)
THCALL(TH_OPFVF2, th_vfsgnjn_vf_w, OP_UUU_W, H4, H4, fsgnjn32)
THCALL(TH_OPFVF2, th_vfsgnjn_vf_d, OP_UUU_D, H8, H8, fsgnjn64)
GEN_TH_VF(th_vfsgnjn_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfsgnjn_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfsgnjn_vf_d, 8, 8, clearq_th)

THCALL(TH_OPFVV2, th_vfsgnjx_vv_h, OP_UUU_H, H2, H2, H2, fsgnjx16)
THCALL(TH_OPFVV2, th_vfsgnjx_vv_w, OP_UUU_W, H4, H4, H4, fsgnjx32)
THCALL(TH_OPFVV2, th_vfsgnjx_vv_d, OP_UUU_D, H8, H8, H8, fsgnjx64)
GEN_TH_VV_ENV(th_vfsgnjx_vv_h, 2, 2, clearh_th)
GEN_TH_VV_ENV(th_vfsgnjx_vv_w, 4, 4, clearl_th)
GEN_TH_VV_ENV(th_vfsgnjx_vv_d, 8, 8, clearq_th)
THCALL(TH_OPFVF2, th_vfsgnjx_vf_h, OP_UUU_H, H2, H2, fsgnjx16)
THCALL(TH_OPFVF2, th_vfsgnjx_vf_w, OP_UUU_W, H4, H4, fsgnjx32)
THCALL(TH_OPFVF2, th_vfsgnjx_vf_d, OP_UUU_D, H8, H8, fsgnjx64)
GEN_TH_VF(th_vfsgnjx_vf_h, 2, 2, clearh_th)
GEN_TH_VF(th_vfsgnjx_vf_w, 4, 4, clearl_th)
GEN_TH_VF(th_vfsgnjx_vf_d, 8, 8, clearq_th)

/* Vector Floating-Point Compare Instructions */
#define GEN_TH_CMP_VV_ENV(NAME, ETYPE, H, DO_OP)              \
void HELPER(NAME)(void *vd, void *v0, void *vs1, void *vs2,   \
                  CPURISCVState *env, uint32_t desc)          \
{                                                             \
    uint32_t mlen = th_mlen(desc);                            \
    uint32_t vm = th_vm(desc);                                \
    uint32_t vl = env->vl;                                    \
    uint32_t vlmax = th_maxsz(desc) / sizeof(ETYPE);          \
    uint32_t i;                                               \
                                                              \
    VSTART_CHECK_EARLY_EXIT(env);                             \
    for (i = env->vstart; i < vl; i++) {                      \
        ETYPE s1 = *((ETYPE *)vs1 + H(i));                    \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                    \
        if (!vm && !th_elem_mask(v0, mlen, i)) {              \
            continue;                                         \
        }                                                     \
        th_set_elem_mask(vd, mlen, i,                         \
                         DO_OP(s2, s1, &env->fp_status));     \
    }                                                         \
    env->vstart = 0;                                          \
    for (; i < vlmax; i++) {                                  \
        th_set_elem_mask(vd, mlen, i, 0);                     \
    }                                                         \
}

GEN_TH_CMP_VV_ENV(th_vmfeq_vv_bh, uint16_t, H2, bfloat16_eq_quiet)
GEN_TH_CMP_VV_ENV(th_vmfeq_vv_h, uint16_t, H2, float16_eq_quiet)
GEN_TH_CMP_VV_ENV(th_vmfeq_vv_w, uint32_t, H4, float32_eq_quiet)
GEN_TH_CMP_VV_ENV(th_vmfeq_vv_d, uint64_t, H8, float64_eq_quiet)

#define GEN_TH_CMP_VF(NAME, ETYPE, H, DO_OP)                        \
void HELPER(NAME)(void *vd, void *v0, uint64_t s1, void *vs2,       \
                  CPURISCVState *env, uint32_t desc)                \
{                                                                   \
    uint32_t mlen = th_mlen(desc);                                  \
    uint32_t vm = th_vm(desc);                                      \
    uint32_t vl = env->vl;                                          \
    uint32_t vlmax = th_maxsz(desc) / sizeof(ETYPE);                \
    uint32_t i;                                                     \
                                                                    \
    VSTART_CHECK_EARLY_EXIT(env);                                   \
    for (i = env->vstart; i < vl; i++) {                            \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                          \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                    \
            continue;                                               \
        }                                                           \
        th_set_elem_mask(vd, mlen, i,                               \
                         DO_OP(s2, (ETYPE)s1, &env->fp_status));    \
    }                                                               \
    env->vstart = 0;                                                \
    for (; i < vlmax; i++) {                                        \
        th_set_elem_mask(vd, mlen, i, 0);                           \
    }                                                               \
}

GEN_TH_CMP_VF(th_vmfeq_vf_bh, uint16_t, H2, bfloat16_eq_quiet)
GEN_TH_CMP_VF(th_vmfeq_vf_h, uint16_t, H2, float16_eq_quiet)
GEN_TH_CMP_VF(th_vmfeq_vf_w, uint32_t, H4, float32_eq_quiet)
GEN_TH_CMP_VF(th_vmfeq_vf_d, uint64_t, H8, float64_eq_quiet)

GEN_TH_CMP_VV_ENV(th_vmfne_vv_bh, uint16_t, H2, vmfnebf16)
GEN_TH_CMP_VV_ENV(th_vmfne_vv_h, uint16_t, H2, vmfne16)
GEN_TH_CMP_VV_ENV(th_vmfne_vv_w, uint32_t, H4, vmfne32)
GEN_TH_CMP_VV_ENV(th_vmfne_vv_d, uint64_t, H8, vmfne64)
GEN_TH_CMP_VF(th_vmfne_vf_bh, uint16_t, H2, vmfnebf16)
GEN_TH_CMP_VF(th_vmfne_vf_h, uint16_t, H2, vmfne16)
GEN_TH_CMP_VF(th_vmfne_vf_w, uint32_t, H4, vmfne32)
GEN_TH_CMP_VF(th_vmfne_vf_d, uint64_t, H8, vmfne64)

GEN_TH_CMP_VV_ENV(th_vmflt_vv_bh, uint16_t, H2, bfloat16_lt)
GEN_TH_CMP_VV_ENV(th_vmflt_vv_h, uint16_t, H2, float16_lt)
GEN_TH_CMP_VV_ENV(th_vmflt_vv_w, uint32_t, H4, float32_lt)
GEN_TH_CMP_VV_ENV(th_vmflt_vv_d, uint64_t, H8, float64_lt)
GEN_TH_CMP_VF(th_vmflt_vf_bh, uint16_t, H2, bfloat16_lt)
GEN_TH_CMP_VF(th_vmflt_vf_h, uint16_t, H2, float16_lt)
GEN_TH_CMP_VF(th_vmflt_vf_w, uint32_t, H4, float32_lt)
GEN_TH_CMP_VF(th_vmflt_vf_d, uint64_t, H8, float64_lt)

GEN_TH_CMP_VV_ENV(th_vmfle_vv_bh, uint16_t, H2, bfloat16_le)
GEN_TH_CMP_VV_ENV(th_vmfle_vv_h, uint16_t, H2, float16_le)
GEN_TH_CMP_VV_ENV(th_vmfle_vv_w, uint32_t, H4, float32_le)
GEN_TH_CMP_VV_ENV(th_vmfle_vv_d, uint64_t, H8, float64_le)
GEN_TH_CMP_VF(th_vmfle_vf_bh, uint16_t, H2, bfloat16_le)
GEN_TH_CMP_VF(th_vmfle_vf_h, uint16_t, H2, float16_le)
GEN_TH_CMP_VF(th_vmfle_vf_w, uint32_t, H4, float32_le)
GEN_TH_CMP_VF(th_vmfle_vf_d, uint64_t, H8, float64_le)

GEN_TH_CMP_VF(th_vmfgt_vf_bh, uint16_t, H2, vmfgtbf16)
GEN_TH_CMP_VF(th_vmfgt_vf_h, uint16_t, H2, vmfgt16)
GEN_TH_CMP_VF(th_vmfgt_vf_w, uint32_t, H4, vmfgt32)
GEN_TH_CMP_VF(th_vmfgt_vf_d, uint64_t, H8, vmfgt64)

GEN_TH_CMP_VF(th_vmfge_vf_bh, uint16_t, H2, vmfgebf16)
GEN_TH_CMP_VF(th_vmfge_vf_h, uint16_t, H2, vmfge16)
GEN_TH_CMP_VF(th_vmfge_vf_w, uint32_t, H4, vmfge32)
GEN_TH_CMP_VF(th_vmfge_vf_d, uint64_t, H8, vmfge64)

GEN_TH_CMP_VV_ENV(th_vmford_vv_bh, uint16_t, H2, !bfloat16_unordered_quiet)
GEN_TH_CMP_VV_ENV(th_vmford_vv_h, uint16_t, H2, !float16_unordered_quiet)
GEN_TH_CMP_VV_ENV(th_vmford_vv_w, uint32_t, H4, !float32_unordered_quiet)
GEN_TH_CMP_VV_ENV(th_vmford_vv_d, uint64_t, H8, !float64_unordered_quiet)
GEN_TH_CMP_VF(th_vmford_vf_bh, uint16_t, H2, !bfloat16_unordered_quiet)
GEN_TH_CMP_VF(th_vmford_vf_h, uint16_t, H2, !float16_unordered_quiet)
GEN_TH_CMP_VF(th_vmford_vf_w, uint32_t, H4, !float32_unordered_quiet)
GEN_TH_CMP_VF(th_vmford_vf_d, uint64_t, H8, !float64_unordered_quiet)

/* Vector Floating-Point Classify Instruction */
#define TH_OPIVV1(NAME, TD, T2, TX2, HD, HS2, OP) \
        OPIVV1(NAME, TD, T2, TX2, HD, HS2, OP)

#define GEN_TH_V(NAME, ESZ, DSZ, CLEAR_FN)             \
void HELPER(NAME)(void *vd, void *v0, void *vs2,       \
                  CPURISCVState *env, uint32_t desc)   \
{                                                      \
    uint32_t vlmax = th_maxsz(desc) / ESZ;             \
    uint32_t mlen = th_mlen(desc);                     \
    uint32_t vm = th_vm(desc);                         \
    uint32_t vl = env->vl;                             \
    uint32_t i;                                        \
                                                       \
    VSTART_CHECK_EARLY_EXIT(env);                      \
    for (i = env->vstart; i < vl; i++) {               \
        if (!vm && !th_elem_mask(v0, mlen, i)) {       \
            continue;                                  \
        }                                              \
        do_##NAME(vd, vs2, i);                         \
    }                                                  \
    env->vstart = 0;                                   \
    CLEAR_FN(vd, vl, vl * DSZ,  vlmax * DSZ);          \
}

THCALL(TH_OPIVV1, th_vfclass_v_bh, OP_UU_H, H2, H2, fclass_bh)
THCALL(TH_OPIVV1, th_vfclass_v_h, OP_UU_H, H2, H2, fclass_h)
THCALL(TH_OPIVV1, th_vfclass_v_w, OP_UU_W, H4, H4, fclass_s)
THCALL(TH_OPIVV1, th_vfclass_v_d, OP_UU_D, H8, H8, fclass_d)
GEN_TH_V(th_vfclass_v_bh, 2, 2, clearh_th)
GEN_TH_V(th_vfclass_v_h, 2, 2, clearh_th)
GEN_TH_V(th_vfclass_v_w, 4, 4, clearl_th)
GEN_TH_V(th_vfclass_v_d, 8, 8, clearq_th)

/* Vector Floating-Point Merge Instruction */
#define GEN_VFMERGE_VF_TH(NAME, ETYPE, H, CLEAR_FN)           \
void HELPER(NAME)(void *vd, void *v0, uint64_t s1, void *vs2, \
                  CPURISCVState *env, uint32_t desc)          \
{                                                             \
    uint32_t mlen = th_mlen(desc);                            \
    uint32_t vm = th_vm(desc);                                \
    uint32_t vl = env->vl;                                    \
    uint32_t esz = sizeof(ETYPE);                             \
    uint32_t vlmax = th_maxsz(desc) / esz;                    \
    uint32_t i;                                               \
                                                              \
    VSTART_CHECK_EARLY_EXIT(env);                             \
    for (i = env->vstart; i < vl; i++) {                      \
        ETYPE s2 = *((ETYPE *)vs2 + H(i));                    \
        *((ETYPE *)vd + H(i))                                 \
          = (!vm && !th_elem_mask(v0, mlen, i) ? s2 : s1);    \
    }                                                         \
    env->vstart = 0;                                          \
    CLEAR_FN(vd, vl, vl * esz, vlmax * esz);                  \
}

GEN_VFMERGE_VF_TH(th_vfmerge_vfm_h, int16_t, H2, clearh_th)
GEN_VFMERGE_VF_TH(th_vfmerge_vfm_w, int32_t, H4, clearl_th)
GEN_VFMERGE_VF_TH(th_vfmerge_vfm_d, int64_t, H8, clearq_th)

/* Single-Width Floating-Point/Integer Type-Convert Instructions */
/* vfcvt.xu.f.v vd, vs2, vm # Convert float to unsigned integer. */
THCALL(TH_OPFVV1, th_vfcvt_xu_f_v_bh, OP_UU_H, H2, H2, bfloat16_to_uint16)
THCALL(TH_OPFVV1, th_vfcvt_xu_f_v_h, OP_UU_H, H2, H2, float16_to_uint16)
THCALL(TH_OPFVV1, th_vfcvt_xu_f_v_w, OP_UU_W, H4, H4, float32_to_uint32)
THCALL(TH_OPFVV1, th_vfcvt_xu_f_v_d, OP_UU_D, H8, H8, float64_to_uint64)
GEN_TH_V_ENV(th_vfcvt_xu_f_v_bh, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfcvt_xu_f_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfcvt_xu_f_v_w, 4, 4, clearl_th)
GEN_TH_V_ENV(th_vfcvt_xu_f_v_d, 8, 8, clearq_th)

/* vfcvt.x.f.v vd, vs2, vm # Convert float to signed integer. */
THCALL(TH_OPFVV1, th_vfcvt_x_f_v_bh, OP_UU_H, H2, H2, bfloat16_to_int16)
THCALL(TH_OPFVV1, th_vfcvt_x_f_v_h, OP_UU_H, H2, H2, float16_to_int16)
THCALL(TH_OPFVV1, th_vfcvt_x_f_v_w, OP_UU_W, H4, H4, float32_to_int32)
THCALL(TH_OPFVV1, th_vfcvt_x_f_v_d, OP_UU_D, H8, H8, float64_to_int64)
GEN_TH_V_ENV(th_vfcvt_x_f_v_bh, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfcvt_x_f_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfcvt_x_f_v_w, 4, 4, clearl_th)
GEN_TH_V_ENV(th_vfcvt_x_f_v_d, 8, 8, clearq_th)

/* vfcvt.f.xu.v vd, vs2, vm # Convert unsigned integer to float. */
THCALL(TH_OPFVV1, th_vfcvt_f_xu_v_bh, OP_UU_H, H2, H2, uint16_to_bfloat16)
THCALL(TH_OPFVV1, th_vfcvt_f_xu_v_h, OP_UU_H, H2, H2, uint16_to_float16)
THCALL(TH_OPFVV1, th_vfcvt_f_xu_v_w, OP_UU_W, H4, H4, uint32_to_float32)
THCALL(TH_OPFVV1, th_vfcvt_f_xu_v_d, OP_UU_D, H8, H8, uint64_to_float64)
GEN_TH_V_ENV(th_vfcvt_f_xu_v_bh, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfcvt_f_xu_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfcvt_f_xu_v_w, 4, 4, clearl_th)
GEN_TH_V_ENV(th_vfcvt_f_xu_v_d, 8, 8, clearq_th)

/* vfcvt.f.x.v vd, vs2, vm # Convert integer to float. */
THCALL(TH_OPFVV1, th_vfcvt_f_x_v_bh, OP_UU_H, H2, H2, int16_to_bfloat16)
THCALL(TH_OPFVV1, th_vfcvt_f_x_v_h, OP_UU_H, H2, H2, int16_to_float16)
THCALL(TH_OPFVV1, th_vfcvt_f_x_v_w, OP_UU_W, H4, H4, int32_to_float32)
THCALL(TH_OPFVV1, th_vfcvt_f_x_v_d, OP_UU_D, H8, H8, int64_to_float64)
GEN_TH_V_ENV(th_vfcvt_f_x_v_bh, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfcvt_f_x_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfcvt_f_x_v_w, 4, 4, clearl_th)
GEN_TH_V_ENV(th_vfcvt_f_x_v_d, 8, 8, clearq_th)

/* Widening Floating-Point/Integer Type-Convert Instructions */

/* vfwcvt.xu.f.v vd, vs2, vm # Convert float to double-width unsigned integer.*/
THCALL(TH_OPFVV1, th_vfwcvt_xu_f_v_bh, WOP_UU_H, H4, H2, bfloat16_to_uint32)
THCALL(TH_OPFVV1, th_vfwcvt_xu_f_v_h, WOP_UU_H, H4, H2, float16_to_uint32)
THCALL(TH_OPFVV1, th_vfwcvt_xu_f_v_w, WOP_UU_W, H8, H4, float32_to_uint64)
GEN_TH_V_ENV(th_vfwcvt_xu_f_v_bh, 2, 4, clearl_th)
GEN_TH_V_ENV(th_vfwcvt_xu_f_v_h, 2, 4, clearl_th)
GEN_TH_V_ENV(th_vfwcvt_xu_f_v_w, 4, 8, clearq_th)

/* vfwcvt.x.f.v vd, vs2, vm # Convert float to double-width signed integer. */
THCALL(TH_OPFVV1, th_vfwcvt_x_f_v_bh, WOP_UU_H, H4, H2, bfloat16_to_int32)
THCALL(TH_OPFVV1, th_vfwcvt_x_f_v_h, WOP_UU_H, H4, H2, float16_to_int32)
THCALL(TH_OPFVV1, th_vfwcvt_x_f_v_w, WOP_UU_W, H8, H4, float32_to_int64)
GEN_TH_V_ENV(th_vfwcvt_x_f_v_bh, 2, 4, clearl_th)
GEN_TH_V_ENV(th_vfwcvt_x_f_v_h, 2, 4, clearl_th)
GEN_TH_V_ENV(th_vfwcvt_x_f_v_w, 4, 8, clearq_th)

/* vfwcvt.f.xu.v vd, vs2, vm # Convert unsigned integer to double-width float */
THCALL(TH_OPFVV1, th_vfwcvt_f_xu_v_bb, WOP_UU_B, H2, H1, uint8_to_bfloat16)
THCALL(TH_OPFVV1, th_vfwcvt_f_xu_v_b, WOP_UU_B, H2, H1, uint8_to_float16)
THCALL(TH_OPFVV1, th_vfwcvt_f_xu_v_h, WOP_UU_H, H4, H2, uint16_to_float32)
THCALL(TH_OPFVV1, th_vfwcvt_f_xu_v_w, WOP_UU_W, H8, H4, uint32_to_float64)
GEN_TH_V_ENV(th_vfwcvt_f_xu_v_bb, 1, 2, clearh_th)
GEN_TH_V_ENV(th_vfwcvt_f_xu_v_b, 1, 2, clearh_th)
GEN_TH_V_ENV(th_vfwcvt_f_xu_v_h, 2, 4, clearl_th)
GEN_TH_V_ENV(th_vfwcvt_f_xu_v_w, 4, 8, clearq_th)

/* vfwcvt.f.x.v vd, vs2, vm # Convert integer to double-width float. */
THCALL(TH_OPFVV1, th_vfwcvt_f_x_v_bb, WOP_UU_B, H2, H1, int8_to_bfloat16)
THCALL(TH_OPFVV1, th_vfwcvt_f_x_v_b, WOP_UU_B, H2, H1, int8_to_float16)
THCALL(TH_OPFVV1, th_vfwcvt_f_x_v_h, WOP_UU_H, H4, H2, int16_to_float32)
THCALL(TH_OPFVV1, th_vfwcvt_f_x_v_w, WOP_UU_W, H8, H4, int32_to_float64)
GEN_TH_V_ENV(th_vfwcvt_f_x_v_bb, 1, 2, clearh_th)
GEN_TH_V_ENV(th_vfwcvt_f_x_v_b, 1, 2, clearh_th)
GEN_TH_V_ENV(th_vfwcvt_f_x_v_h, 2, 4, clearl_th)
GEN_TH_V_ENV(th_vfwcvt_f_x_v_w, 4, 8, clearq_th)

/*
 * vfwcvt.f.f.v vd, vs2, vm #
 * Convert single-width float to double-width float.
 */
static uint32_t vfwcvtffv16(uint16_t a, float_status *s)
{
    return float16_to_float32(a, true, s);
}

static uint32_t vfwcvtffvbf16(uint16_t a, float_status *s)
{
    return bfloat16_to_float32(a, s);
}

THCALL(TH_OPFVV1, th_vfwcvt_f_f_v_bh, WOP_UU_H, H4, H2, vfwcvtffvbf16)
THCALL(TH_OPFVV1, th_vfwcvt_f_f_v_h, WOP_UU_H, H4, H2, vfwcvtffv16)
THCALL(TH_OPFVV1, th_vfwcvt_f_f_v_w, WOP_UU_W, H8, H4, float32_to_float64)
GEN_TH_V_ENV(th_vfwcvt_f_f_v_bh, 2, 4, clearl_th)
GEN_TH_V_ENV(th_vfwcvt_f_f_v_h, 2, 4, clearl_th)
GEN_TH_V_ENV(th_vfwcvt_f_f_v_w, 4, 8, clearq_th)

/* Narrowing Floating-Point/Integer Type-Convert Instructions */

/* vfncvt.xu.f.v vd, vs2, vm # Convert float to unsigned integer. */
THCALL(TH_OPFVV1, th_vfncvt_xu_f_v_bb, NOP_UU_B, H1, H2, bfloat16_to_uint8)
THCALL(TH_OPFVV1, th_vfncvt_xu_f_v_b, NOP_UU_B, H1, H2, float16_to_uint8)
THCALL(TH_OPFVV1, th_vfncvt_xu_f_v_h, NOP_UU_H, H2, H4, float32_to_uint16)
THCALL(TH_OPFVV1, th_vfncvt_xu_f_v_w, NOP_UU_W, H4, H8, float64_to_uint32)
GEN_TH_V_ENV(th_vfncvt_xu_f_v_bb, 1, 1, clearb_th)
GEN_TH_V_ENV(th_vfncvt_xu_f_v_b, 1, 1, clearb_th)
GEN_TH_V_ENV(th_vfncvt_xu_f_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfncvt_xu_f_v_w, 4, 4, clearl_th)

/* vfncvt.x.f.v vd, vs2, vm # Convert double-width float to signed integer. */
THCALL(TH_OPFVV1, th_vfncvt_x_f_v_bb, NOP_UU_B, H1, H2, bfloat16_to_int8)
THCALL(TH_OPFVV1, th_vfncvt_x_f_v_b, NOP_UU_B, H1, H2, float16_to_int8)
THCALL(TH_OPFVV1, th_vfncvt_x_f_v_h, NOP_UU_H, H2, H4, float32_to_int16)
THCALL(TH_OPFVV1, th_vfncvt_x_f_v_w, NOP_UU_W, H4, H8, float64_to_int32)
GEN_TH_V_ENV(th_vfncvt_x_f_v_bb, 1, 1, clearb_th)
GEN_TH_V_ENV(th_vfncvt_x_f_v_b, 1, 1, clearb_th)
GEN_TH_V_ENV(th_vfncvt_x_f_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfncvt_x_f_v_w, 4, 4, clearl_th)

/* vfncvt.f.xu.v vd, vs2, vm # Convert double-width unsigned integer to float */
THCALL(TH_OPFVV1, th_vfncvt_f_xu_v_bh, NOP_UU_H, H2, H4, uint32_to_bfloat16)
THCALL(TH_OPFVV1, th_vfncvt_f_xu_v_h, NOP_UU_H, H2, H4, uint32_to_float16)
THCALL(TH_OPFVV1, th_vfncvt_f_xu_v_w, NOP_UU_W, H4, H8, uint64_to_float32)
GEN_TH_V_ENV(th_vfncvt_f_xu_v_bh, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfncvt_f_xu_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfncvt_f_xu_v_w, 4, 4, clearl_th)

/* vfncvt.f.x.v vd, vs2, vm # Convert double-width integer to float. */
THCALL(TH_OPFVV1, th_vfncvt_f_x_v_bh, NOP_UU_H, H2, H4, int32_to_bfloat16)
THCALL(TH_OPFVV1, th_vfncvt_f_x_v_h, NOP_UU_H, H2, H4, int32_to_float16)
THCALL(TH_OPFVV1, th_vfncvt_f_x_v_w, NOP_UU_W, H4, H8, int64_to_float32)
GEN_TH_V_ENV(th_vfncvt_f_x_v_bh, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfncvt_f_x_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfncvt_f_x_v_w, 4, 4, clearl_th)

/* vfncvt.f.f.v vd, vs2, vm # Convert double float to single-width float. */
static uint16_t vfncvtffv16(uint32_t a, float_status *s)
{
    return float32_to_float16(a, true, s);
}

static uint16_t vfncvtffvbf16(uint32_t a, float_status *s)
{
    return float32_to_bfloat16(a, s);
}

THCALL(TH_OPFVV1, th_vfncvt_f_f_v_bh, NOP_UU_H, H2, H4, vfncvtffvbf16)
THCALL(TH_OPFVV1, th_vfncvt_f_f_v_h, NOP_UU_H, H2, H4, vfncvtffv16)
THCALL(TH_OPFVV1, th_vfncvt_f_f_v_w, NOP_UU_W, H4, H8, float64_to_float32)
GEN_TH_V_ENV(th_vfncvt_f_f_v_bh, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfncvt_f_f_v_h, 2, 2, clearh_th)
GEN_TH_V_ENV(th_vfncvt_f_f_v_w, 4, 4, clearl_th)

/*
 * Vector Reduction Operations
 */

/* Vector Single-Width Integer Reduction Instructions */
#define GEN_TH_RED(NAME, TD, TS2, HD, HS2, OP, CLEAR_FN)  \
void HELPER(NAME)(void *vd, void *v0, void *vs1,          \
        void *vs2, CPURISCVState *env, uint32_t desc)     \
{                                                         \
    uint32_t mlen = th_mlen(desc);                        \
    uint32_t vm = th_vm(desc);                            \
    uint32_t vl = env->vl;                                \
    uint32_t i;                                           \
    uint32_t tot = env_archcpu(env)->cfg.vlenb;           \
    TD s1 =  *((TD *)vs1 + HD(0));                        \
                                                          \
    for (i = env->vstart; i < vl; i++) {                  \
        TS2 s2 = *((TS2 *)vs2 + HS2(i));                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {          \
            continue;                                     \
        }                                                 \
        s1 = OP(s1, (TD)s2);                              \
    }                                                     \
    *((TD *)vd + HD(0)) = s1;                             \
    env->vstart = 0;                                      \
    CLEAR_FN(vd, 1, sizeof(TD), tot);                     \
}

/* vd[0] = sum(vs1[0], vs2[*]) */
GEN_TH_RED(th_vredsum_vs_b, int8_t, int8_t, H1, H1, TH_ADD, clearb_th)
GEN_TH_RED(th_vredsum_vs_h, int16_t, int16_t, H2, H2, TH_ADD, clearh_th)
GEN_TH_RED(th_vredsum_vs_w, int32_t, int32_t, H4, H4, TH_ADD, clearl_th)
GEN_TH_RED(th_vredsum_vs_d, int64_t, int64_t, H8, H8, TH_ADD, clearq_th)

/* vd[0] = maxu(vs1[0], vs2[*]) */
GEN_TH_RED(th_vredmaxu_vs_b, uint8_t, uint8_t, H1, H1, TH_MAX, clearb_th)
GEN_TH_RED(th_vredmaxu_vs_h, uint16_t, uint16_t, H2, H2, TH_MAX, clearh_th)
GEN_TH_RED(th_vredmaxu_vs_w, uint32_t, uint32_t, H4, H4, TH_MAX, clearl_th)
GEN_TH_RED(th_vredmaxu_vs_d, uint64_t, uint64_t, H8, H8, TH_MAX, clearq_th)

/* vd[0] = max(vs1[0], vs2[*]) */
GEN_TH_RED(th_vredmax_vs_b, int8_t, int8_t, H1, H1, TH_MAX, clearb_th)
GEN_TH_RED(th_vredmax_vs_h, int16_t, int16_t, H2, H2, TH_MAX, clearh_th)
GEN_TH_RED(th_vredmax_vs_w, int32_t, int32_t, H4, H4, TH_MAX, clearl_th)
GEN_TH_RED(th_vredmax_vs_d, int64_t, int64_t, H8, H8, TH_MAX, clearq_th)

/* vd[0] = minu(vs1[0], vs2[*]) */
GEN_TH_RED(th_vredminu_vs_b, uint8_t, uint8_t, H1, H1, TH_MIN, clearb_th)
GEN_TH_RED(th_vredminu_vs_h, uint16_t, uint16_t, H2, H2, TH_MIN, clearh_th)
GEN_TH_RED(th_vredminu_vs_w, uint32_t, uint32_t, H4, H4, TH_MIN, clearl_th)
GEN_TH_RED(th_vredminu_vs_d, uint64_t, uint64_t, H8, H8, TH_MIN, clearq_th)

/* vd[0] = min(vs1[0], vs2[*]) */
GEN_TH_RED(th_vredmin_vs_b, int8_t, int8_t, H1, H1, TH_MIN, clearb_th)
GEN_TH_RED(th_vredmin_vs_h, int16_t, int16_t, H2, H2, TH_MIN, clearh_th)
GEN_TH_RED(th_vredmin_vs_w, int32_t, int32_t, H4, H4, TH_MIN, clearl_th)
GEN_TH_RED(th_vredmin_vs_d, int64_t, int64_t, H8, H8, TH_MIN, clearq_th)

/* vd[0] = and(vs1[0], vs2[*]) */
GEN_TH_RED(th_vredand_vs_b, int8_t, int8_t, H1, H1, TH_AND, clearb_th)
GEN_TH_RED(th_vredand_vs_h, int16_t, int16_t, H2, H2, TH_AND, clearh_th)
GEN_TH_RED(th_vredand_vs_w, int32_t, int32_t, H4, H4, TH_AND, clearl_th)
GEN_TH_RED(th_vredand_vs_d, int64_t, int64_t, H8, H8, TH_AND, clearq_th)

/* vd[0] = or(vs1[0], vs2[*]) */
GEN_TH_RED(th_vredor_vs_b, int8_t, int8_t, H1, H1, TH_OR, clearb_th)
GEN_TH_RED(th_vredor_vs_h, int16_t, int16_t, H2, H2, TH_OR, clearh_th)
GEN_TH_RED(th_vredor_vs_w, int32_t, int32_t, H4, H4, TH_OR, clearl_th)
GEN_TH_RED(th_vredor_vs_d, int64_t, int64_t, H8, H8, TH_OR, clearq_th)

/* vd[0] = xor(vs1[0], vs2[*]) */
GEN_TH_RED(th_vredxor_vs_b, int8_t, int8_t, H1, H1, TH_XOR, clearb_th)
GEN_TH_RED(th_vredxor_vs_h, int16_t, int16_t, H2, H2, TH_XOR, clearh_th)
GEN_TH_RED(th_vredxor_vs_w, int32_t, int32_t, H4, H4, TH_XOR, clearl_th)
GEN_TH_RED(th_vredxor_vs_d, int64_t, int64_t, H8, H8, TH_XOR, clearq_th)

/* Vector Widening Integer Reduction Instructions */
/* signed sum reduction into double-width accumulator */
GEN_TH_RED(th_vwredsum_vs_b, int16_t, int8_t, H2, H1, TH_ADD, clearh_th)
GEN_TH_RED(th_vwredsum_vs_h, int32_t, int16_t, H4, H2, TH_ADD, clearl_th)
GEN_TH_RED(th_vwredsum_vs_w, int64_t, int32_t, H8, H4, TH_ADD, clearq_th)

/* Unsigned sum reduction into double-width accumulator */
GEN_TH_RED(th_vwredsumu_vs_b, uint16_t, uint8_t, H2, H1, TH_ADD, clearh_th)
GEN_TH_RED(th_vwredsumu_vs_h, uint32_t, uint16_t, H4, H2, TH_ADD, clearl_th)
GEN_TH_RED(th_vwredsumu_vs_w, uint64_t, uint32_t, H8, H4, TH_ADD, clearq_th)

/* Vector Single-Width Floating-Point Reduction Instructions */
#define GEN_TH_FRED(NAME, TD, TS2, HD, HS2, OP, CLEAR_FN)  \
void HELPER(NAME)(void *vd, void *v0, void *vs1,           \
                  void *vs2, CPURISCVState *env,           \
                  uint32_t desc)                           \
{                                                          \
    uint32_t mlen = th_mlen(desc);                         \
    uint32_t vm = th_vm(desc);                             \
    uint32_t vl = env->vl;                                 \
    uint32_t i;                                            \
    uint32_t tot = env_archcpu(env)->cfg.vlenb;            \
    TD s1 =  *((TD *)vs1 + HD(0));                         \
                                                           \
    for (i = env->vstart; i < vl; i++) {                   \
        TS2 s2 = *((TS2 *)vs2 + HS2(i));                   \
        if (!vm && !th_elem_mask(v0, mlen, i)) {           \
            continue;                                      \
        }                                                  \
        s1 = OP(s1, (TD)s2, &env->fp_status);              \
    }                                                      \
    *((TD *)vd + HD(0)) = s1;                              \
    env->vstart = 0;                                       \
    CLEAR_FN(vd, 1, sizeof(TD), tot);                      \
}

/* Unordered sum */
GEN_TH_FRED(th_vfredsum_vs_bh, uint16_t, uint16_t, H2, H2,
            bfloat16_add, clearh_th)
GEN_TH_FRED(th_vfredsum_vs_h, uint16_t, uint16_t, H2, H2,
            float16_add, clearh_th)
GEN_TH_FRED(th_vfredsum_vs_w, uint32_t, uint32_t, H4, H4,
            float32_add, clearl_th)
GEN_TH_FRED(th_vfredsum_vs_d, uint64_t, uint64_t, H8, H8,
            float64_add, clearq_th)

/* Maximum value */
GEN_TH_FRED(th_vfredmax_vs_bh, uint16_t, uint16_t, H2, H2,
            bfloat16_maxnum, clearh_th)
GEN_TH_FRED(th_vfredmax_vs_h, uint16_t, uint16_t, H2, H2,
            float16_maxnum, clearh_th)
GEN_TH_FRED(th_vfredmax_vs_w, uint32_t, uint32_t, H4, H4,
            float32_maxnum, clearl_th)
GEN_TH_FRED(th_vfredmax_vs_d, uint64_t, uint64_t, H8, H8,
            float64_maxnum, clearq_th)

/* Minimum value */
GEN_TH_FRED(th_vfredmin_vs_bh, uint16_t, uint16_t, H2, H2,
            bfloat16_minnum, clearh_th)
GEN_TH_FRED(th_vfredmin_vs_h, uint16_t, uint16_t, H2, H2,
            float16_minnum, clearh_th)
GEN_TH_FRED(th_vfredmin_vs_w, uint32_t, uint32_t, H4, H4,
            float32_minnum, clearl_th)
GEN_TH_FRED(th_vfredmin_vs_d, uint64_t, uint64_t, H8, H8,
            float64_minnum, clearq_th)

/* Vector Widening Floating-Point Add functions */
static uint32_t bfwadd16(uint32_t a, uint16_t b, float_status *s)
{
    return float32_add(a, bfloat16_to_float32(b, s), s);
}

static uint32_t fwadd16(uint32_t a, uint16_t b, float_status *s)
{
    return float32_add(a, float16_to_float32(b, true, s), s);
}

static uint64_t fwadd32(uint64_t a, uint32_t b, float_status *s)
{
    return float64_add(a, float32_to_float64(b, s), s);
}

/* Vector Widening Floating-Point Reduction Instructions */
/* Unordered reduce 2*SEW = 2*SEW + sum(promote(SEW)) */
GEN_TH_FRED(th_vfwredsum_vs_bh, uint32_t, uint16_t, H4, H2, bfwadd16, clearl_th)
GEN_TH_FRED(th_vfwredsum_vs_h, uint32_t, uint16_t, H4, H2, fwadd16, clearl_th)
GEN_TH_FRED(th_vfwredsum_vs_w, uint64_t, uint32_t, H8, H4, fwadd32, clearq_th)

/*
 * Vector Mask Operations
 */
/* Vector Mask-Register Logical Instructions */
#define GEN_TH_MASK_VV(NAME, OP)                                 \
void HELPER(NAME)(void *vd, void *v0, void *vs1,                 \
                  void *vs2, CPURISCVState *env,                 \
                  uint32_t desc)                                 \
{                                                                \
    uint32_t mlen = th_mlen(desc);                               \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;  \
    uint32_t vl = env->vl;                                       \
    uint32_t i;                                                  \
    int a, b;                                                    \
                                                                 \
    VSTART_CHECK_EARLY_EXIT(env);                                \
    for (i = env->vstart; i < vl; i++) {                         \
        a = th_elem_mask(vs1, mlen, i);                          \
        b = th_elem_mask(vs2, mlen, i);                          \
        th_set_elem_mask(vd, mlen, i, OP(b, a));                 \
    }                                                            \
    env->vstart = 0;                                             \
    for (; i < vlmax; i++) {                                     \
        th_set_elem_mask(vd, mlen, i, 0);                        \
    }                                                            \
}

#define TH_NAND(N, M)  (!(N & M))
#define TH_ANDNOT(N, M)  (N & !M)
#define TH_NOR(N, M)  (!(N | M))
#define TH_ORNOT(N, M)  (N | !M)
#define TH_XNOR(N, M)  (!(N ^ M))

GEN_TH_MASK_VV(th_vmand_mm, TH_AND)
GEN_TH_MASK_VV(th_vmnand_mm, TH_NAND)
GEN_TH_MASK_VV(th_vmandnot_mm, TH_ANDNOT)
GEN_TH_MASK_VV(th_vmxor_mm, TH_XOR)
GEN_TH_MASK_VV(th_vmor_mm, TH_OR)
GEN_TH_MASK_VV(th_vmnor_mm, TH_NOR)
GEN_TH_MASK_VV(th_vmornot_mm, TH_ORNOT)
GEN_TH_MASK_VV(th_vmxnor_mm, TH_XNOR)

/* Vector mask population count vmpopc */
target_ulong HELPER(th_vmpopc_m)(void *v0, void *vs2, CPURISCVState *env,
                              uint32_t desc)
{
    target_ulong cnt = 0;
    uint32_t mlen = th_mlen(desc);
    uint32_t vm = th_vm(desc);
    uint32_t vl = env->vl;
    int i;

    for (i = env->vstart; i < vl; i++) {
        if (vm || th_elem_mask(v0, mlen, i)) {
            if (th_elem_mask(vs2, mlen, i)) {
                cnt++;
            }
        }
    }
    env->vstart = 0;
    return cnt;
}

/* vmfirst find-first-set mask bit*/
target_ulong HELPER(th_vmfirst_m)(void *v0, void *vs2, CPURISCVState *env,
                               uint32_t desc)
{
    uint32_t mlen = th_mlen(desc);
    uint32_t vm = th_vm(desc);
    uint32_t vl = env->vl;
    int i;

    for (i = env->vstart; i < vl; i++) {
        if (vm || th_elem_mask(v0, mlen, i)) {
            if (th_elem_mask(vs2, mlen, i)) {
                return i;
            }
        }
    }
    env->vstart = 0;
    return -1LL;
}

enum set_mask_type_th {
    ONLY_FIRST = 1,
    INCLUDE_FIRST,
    BEFORE_FIRST,
};

static void vmsetm(void *vd, void *v0, void *vs2, CPURISCVState *env,
                   uint32_t desc, enum set_mask_type_th type)
{
    uint32_t mlen = th_mlen(desc);
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;
    uint32_t vm = th_vm(desc);
    uint32_t vl = env->vl;
    int i;
    bool first_mask_bit = false;

    for (i = env->vstart; i < vl; i++) {
        if (!vm && !th_elem_mask(v0, mlen, i)) {
            continue;
        }
        /* write a zero to all following active elements */
        if (first_mask_bit) {
            th_set_elem_mask(vd, mlen, i, 0);
            continue;
        }
        if (th_elem_mask(vs2, mlen, i)) {
            first_mask_bit = true;
            if (type == BEFORE_FIRST) {
                th_set_elem_mask(vd, mlen, i, 0);
            } else {
                th_set_elem_mask(vd, mlen, i, 1);
            }
        } else {
            if (type == ONLY_FIRST) {
                th_set_elem_mask(vd, mlen, i, 0);
            } else {
                th_set_elem_mask(vd, mlen, i, 1);
            }
        }
    }
    env->vstart = 0;
    for (; i < vlmax; i++) {
        th_set_elem_mask(vd, mlen, i, 0);
    }
}

void HELPER(th_vmsbf_m)(void *vd, void *v0, void *vs2, CPURISCVState *env,
                        uint32_t desc)
{
    vmsetm(vd, v0, vs2, env, desc, BEFORE_FIRST);
}

void HELPER(th_vmsif_m)(void *vd, void *v0, void *vs2, CPURISCVState *env,
                        uint32_t desc)
{
    vmsetm(vd, v0, vs2, env, desc, INCLUDE_FIRST);
}

void HELPER(th_vmsof_m)(void *vd, void *v0, void *vs2, CPURISCVState *env,
                        uint32_t desc)
{
    vmsetm(vd, v0, vs2, env, desc, ONLY_FIRST);
}

/* Vector Iota Instruction */
#define GEN_TH_VIOTA_M(NAME, ETYPE, H, CLEAR_FN)                          \
void HELPER(NAME)(void *vd, void *v0, void *vs2, CPURISCVState *env,      \
                  uint32_t desc)                                          \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;           \
    uint32_t vm = th_vm(desc);                                            \
    uint32_t vl = env->vl;                                                \
    uint32_t sum = 0;                                                     \
    int i;                                                                \
                                                                          \
    for (i = env->vstart; i < vl; i++) {                                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                          \
            continue;                                                     \
        }                                                                 \
        *((ETYPE *)vd + H(i)) = sum;                                      \
        if (th_elem_mask(vs2, mlen, i)) {                                 \
            sum++;                                                        \
        }                                                                 \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, vl, vl * sizeof(ETYPE), vlmax * sizeof(ETYPE));          \
}

GEN_TH_VIOTA_M(th_viota_m_b, uint8_t, H1, clearb_th)
GEN_TH_VIOTA_M(th_viota_m_h, uint16_t, H2, clearh_th)
GEN_TH_VIOTA_M(th_viota_m_w, uint32_t, H4, clearl_th)
GEN_TH_VIOTA_M(th_viota_m_d, uint64_t, H8, clearq_th)

/* Vector Element Index Instruction */
#define GEN_TH_VID_V(NAME, ETYPE, H, CLEAR_FN)                            \
void HELPER(NAME)(void *vd, void *v0, CPURISCVState *env, uint32_t desc)  \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;           \
    uint32_t vm = th_vm(desc);                                            \
    uint32_t vl = env->vl;                                                \
    int i;                                                                \
                                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                                         \
    for (i = env->vstart; i < vl; i++) {                                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                          \
            continue;                                                     \
        }                                                                 \
        *((ETYPE *)vd + H(i)) = i;                                        \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, vl, vl * sizeof(ETYPE), vlmax * sizeof(ETYPE));          \
}

GEN_TH_VID_V(th_vid_v_b, uint8_t, H1, clearb_th)
GEN_TH_VID_V(th_vid_v_h, uint16_t, H2, clearh_th)
GEN_TH_VID_V(th_vid_v_w, uint32_t, H4, clearl_th)
GEN_TH_VID_V(th_vid_v_d, uint64_t, H8, clearq_th)

/*
 * Vector Permutation Instructions
 */

/* Vector Slide Instructions */
#define GEN_TH_VSLIDEUP_VX(NAME, ETYPE, H, CLEAR_FN)                      \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1, void *vs2,         \
                  CPURISCVState *env, uint32_t desc)                      \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;           \
    uint32_t vm = th_vm(desc);                                            \
    uint32_t vl = env->vl;                                                \
    target_ulong offset = s1, i_min, i;                                   \
                                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                                         \
    i_min = MAX(env->vstart, offset);                                     \
    for (i = i_min; i < vl; i++) {                                        \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                          \
            continue;                                                     \
        }                                                                 \
        *((ETYPE *)vd + H(i)) = *((ETYPE *)vs2 + H(i - offset));          \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, vl, vl * sizeof(ETYPE), vlmax * sizeof(ETYPE));          \
}

/* vslideup.vx vd, vs2, rs1, vm # vd[i+rs1] = vs2[i] */
GEN_TH_VSLIDEUP_VX(th_vslideup_vx_b, uint8_t, H1, clearb_th)
GEN_TH_VSLIDEUP_VX(th_vslideup_vx_h, uint16_t, H2, clearh_th)
GEN_TH_VSLIDEUP_VX(th_vslideup_vx_w, uint32_t, H4, clearl_th)
GEN_TH_VSLIDEUP_VX(th_vslideup_vx_d, uint64_t, H8, clearq_th)

#define GEN_TH_VSLIDEDOWN_VX(NAME, ETYPE, H, CLEAR_FN)                    \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1, void *vs2,         \
                  CPURISCVState *env, uint32_t desc)                      \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;           \
    uint32_t vm = th_vm(desc);                                            \
    uint32_t vl = env->vl;                                                \
    target_ulong offset = s1, i;                                          \
                                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                                         \
    for (i = env->vstart; i < vl; ++i) {                                  \
        target_ulong j = i + offset;                                      \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                          \
            continue;                                                     \
        }                                                                 \
        *((ETYPE *)vd + H(i)) = j >= vlmax ? 0 : *((ETYPE *)vs2 + H(j));  \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, vl, vl * sizeof(ETYPE), vlmax * sizeof(ETYPE));          \
}

/* vslidedown.vx vd, vs2, rs1, vm # vd[i] = vs2[i+rs1] */
GEN_TH_VSLIDEDOWN_VX(th_vslidedown_vx_b, uint8_t, H1, clearb_th)
GEN_TH_VSLIDEDOWN_VX(th_vslidedown_vx_h, uint16_t, H2, clearh_th)
GEN_TH_VSLIDEDOWN_VX(th_vslidedown_vx_w, uint32_t, H4, clearl_th)
GEN_TH_VSLIDEDOWN_VX(th_vslidedown_vx_d, uint64_t, H8, clearq_th)

#define GEN_TH_VSLIDE1UP_VX(NAME, ETYPE, H, CLEAR_FN)                     \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1, void *vs2,         \
                  CPURISCVState *env, uint32_t desc)                      \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;           \
    uint32_t vm = th_vm(desc);                                            \
    uint32_t vl = env->vl;                                                \
    uint32_t i;                                                           \
                                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                                         \
    for (i = env->vstart; i < vl; i++) {                                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                          \
            continue;                                                     \
        }                                                                 \
        if (i == 0) {                                                     \
            *((ETYPE *)vd + H(i)) = s1;                                   \
        } else {                                                          \
            *((ETYPE *)vd + H(i)) = *((ETYPE *)vs2 + H(i - 1));           \
        }                                                                 \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, vl, vl * sizeof(ETYPE), vlmax * sizeof(ETYPE));          \
}

/* vslide1up.vx vd, vs2, rs1, vm # vd[0]=x[rs1], vd[i+1] = vs2[i] */
GEN_TH_VSLIDE1UP_VX(th_vslide1up_vx_b, uint8_t, H1, clearb_th)
GEN_TH_VSLIDE1UP_VX(th_vslide1up_vx_h, uint16_t, H2, clearh_th)
GEN_TH_VSLIDE1UP_VX(th_vslide1up_vx_w, uint32_t, H4, clearl_th)
GEN_TH_VSLIDE1UP_VX(th_vslide1up_vx_d, uint64_t, H8, clearq_th)

#define GEN_TH_VSLIDE1DOWN_VX(NAME, ETYPE, H, CLEAR_FN)                   \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1, void *vs2,         \
                  CPURISCVState *env, uint32_t desc)                      \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;           \
    uint32_t vm = th_vm(desc);                                            \
    uint32_t vl = env->vl;                                                \
    uint32_t i;                                                           \
                                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                                         \
    for (i = env->vstart; i < vl; i++) {                                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                          \
            continue;                                                     \
        }                                                                 \
        if (i == vl - 1) {                                                \
            *((ETYPE *)vd + H(i)) = s1;                                   \
        } else {                                                          \
            *((ETYPE *)vd + H(i)) = *((ETYPE *)vs2 + H(i + 1));           \
        }                                                                 \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, vl, vl * sizeof(ETYPE), vlmax * sizeof(ETYPE));          \
}

/* vslide1down.vx vd, vs2, rs1, vm # vd[i] = vs2[i+1], vd[vl-1]=x[rs1] */
GEN_TH_VSLIDE1DOWN_VX(th_vslide1down_vx_b, uint8_t, H1, clearb_th)
GEN_TH_VSLIDE1DOWN_VX(th_vslide1down_vx_h, uint16_t, H2, clearh_th)
GEN_TH_VSLIDE1DOWN_VX(th_vslide1down_vx_w, uint32_t, H4, clearl_th)
GEN_TH_VSLIDE1DOWN_VX(th_vslide1down_vx_d, uint64_t, H8, clearq_th)

/* Vector Register Gather Instruction */
#define GEN_TH_VRGATHER_VV(NAME, ETYPE, H, CLEAR_FN)                      \
void HELPER(NAME)(void *vd, void *v0, void *vs1, void *vs2,               \
                  CPURISCVState *env, uint32_t desc)                      \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;           \
    uint32_t vm = th_vm(desc);                                            \
    uint32_t vl = env->vl;                                                \
    uint32_t index, i;                                                    \
                                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                                         \
    for (i = env->vstart; i < vl; i++) {                                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                          \
            continue;                                                     \
        }                                                                 \
        index = *((ETYPE *)vs1 + H(i));                                   \
        if (index >= vlmax) {                                             \
            *((ETYPE *)vd + H(i)) = 0;                                    \
        } else {                                                          \
            *((ETYPE *)vd + H(i)) = *((ETYPE *)vs2 + H(index));           \
        }                                                                 \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, vl, vl * sizeof(ETYPE), vlmax * sizeof(ETYPE));          \
}

/* vd[i] = (vs1[i] >= VLMAX) ? 0 : vs2[vs1[i]]; */
GEN_TH_VRGATHER_VV(th_vrgather_vv_b, uint8_t, H1, clearb_th)
GEN_TH_VRGATHER_VV(th_vrgather_vv_h, uint16_t, H2, clearh_th)
GEN_TH_VRGATHER_VV(th_vrgather_vv_w, uint32_t, H4, clearl_th)
GEN_TH_VRGATHER_VV(th_vrgather_vv_d, uint64_t, H8, clearq_th)

#define GEN_TH_VRGATHER_VX(NAME, ETYPE, H, CLEAR_FN)                      \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1, void *vs2,         \
                  CPURISCVState *env, uint32_t desc)                      \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;           \
    uint32_t vm = th_vm(desc);                                            \
    uint32_t vl = env->vl;                                                \
    uint32_t index = s1, i;                                               \
                                                                          \
    VSTART_CHECK_EARLY_EXIT(env);                                         \
    for (i = env->vstart; i < vl; i++) {                                  \
        if (!vm && !th_elem_mask(v0, mlen, i)) {                          \
            continue;                                                     \
        }                                                                 \
        if (index >= vlmax) {                                             \
            *((ETYPE *)vd + H(i)) = 0;                                    \
        } else {                                                          \
            *((ETYPE *)vd + H(i)) = *((ETYPE *)vs2 + H(index));           \
        }                                                                 \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, vl, vl * sizeof(ETYPE), vlmax * sizeof(ETYPE));          \
}

/* vd[i] = (x[rs1] >= VLMAX) ? 0 : vs2[rs1] */
GEN_TH_VRGATHER_VX(th_vrgather_vx_b, uint8_t, H1, clearb_th)
GEN_TH_VRGATHER_VX(th_vrgather_vx_h, uint16_t, H2, clearh_th)
GEN_TH_VRGATHER_VX(th_vrgather_vx_w, uint32_t, H4, clearl_th)
GEN_TH_VRGATHER_VX(th_vrgather_vx_d, uint64_t, H8, clearq_th)

/* Vector Compress Instruction */
#define GEN_TH_VCOMPRESS_VM(NAME, ETYPE, H, CLEAR_FN)                     \
void HELPER(NAME)(void *vd, void *v0, void *vs1, void *vs2,               \
                  CPURISCVState *env, uint32_t desc)                      \
{                                                                         \
    uint32_t mlen = th_mlen(desc);                                        \
    uint32_t vlmax = (env_archcpu(env)->cfg.vlenb << 3) / mlen;           \
    uint32_t vl = env->vl;                                                \
    uint32_t num = 0, i;                                                  \
                                                                          \
    for (i = env->vstart; i < vl; i++) {                                  \
        if (!th_elem_mask(vs1, mlen, i)) {                                \
            continue;                                                     \
        }                                                                 \
        *((ETYPE *)vd + H(num)) = *((ETYPE *)vs2 + H(i));                 \
        num++;                                                            \
    }                                                                     \
    env->vstart = 0;                                                      \
    CLEAR_FN(vd, num, num * sizeof(ETYPE), vlmax * sizeof(ETYPE));        \
}

/* Compress into vd elements of vs2 where vs1 is enabled */
GEN_TH_VCOMPRESS_VM(th_vcompress_vm_b, uint8_t, H1, clearb_th)
GEN_TH_VCOMPRESS_VM(th_vcompress_vm_h, uint16_t, H2, clearh_th)
GEN_TH_VCOMPRESS_VM(th_vcompress_vm_w, uint32_t, H4, clearl_th)
GEN_TH_VCOMPRESS_VM(th_vcompress_vm_d, uint64_t, H8, clearq_th)
