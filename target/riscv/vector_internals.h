/*
 * RISC-V Vector Extension Internals
 *
 * Copyright (c) 2020 T-Head Semiconductor Co., Ltd. All rights reserved.
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

#ifndef TARGET_RISCV_VECTOR_INTERNALS_H
#define TARGET_RISCV_VECTOR_INTERNALS_H

#include "qemu/bitops.h"
#include "cpu.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"

#define VSTART_CHECK_EARLY_EXIT(env) do { \
    if (env->vstart >= env->vl) {         \
        env->vstart = 0;                  \
        return;                           \
    }                                     \
} while (0)

static inline uint32_t vext_nf(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, NF);
}

/*
 * Note that vector data is stored in host-endian 64-bit chunks,
 * so addressing units smaller than that needs a host-endian fixup.
 */
#if HOST_BIG_ENDIAN
#define H1(x)   ((x) ^ 7)
#define H1_2(x) ((x) ^ 6)
#define H1_4(x) ((x) ^ 4)
#define H2(x)   ((x) ^ 3)
#define H4(x)   ((x) ^ 1)
#define H8(x)   ((x))
#else
#define H1(x)   (x)
#define H1_2(x) (x)
#define H1_4(x) (x)
#define H2(x)   (x)
#define H4(x)   (x)
#define H8(x)   (x)
#endif

/*
 * Encode LMUL to lmul as following:
 *     LMUL    vlmul    lmul
 *      1       000       0
 *      2       001       1
 *      4       010       2
 *      8       011       3
 *      -       100       -
 *     1/8      101      -3
 *     1/4      110      -2
 *     1/2      111      -1
 */
static inline int32_t vext_lmul(uint32_t desc)
{
    return sextract32(FIELD_EX32(simd_data(desc), VDATA, LMUL), 0, 3);
}

static inline uint32_t vext_vm(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, VM);
}

static inline uint32_t vext_vma(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, VMA);
}

static inline uint32_t vext_vta(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, VTA);
}

static inline uint32_t vext_vta_all_1s(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, VTA_ALL_1S);
}

/*
 * Earlier designs (pre-0.9) had a varying number of bits
 * per mask value (MLEN). In the 0.9 design, MLEN=1.
 * (Section 4.5)
 */
static inline int vext_elem_mask(void *v0, int index)
{
    int idx = index / 64;
    int pos = index  % 64;
    return (((uint64_t *)v0)[idx] >> pos) & 1;
}

/*
 * Get number of total elements, including prestart, body and tail elements.
 * Note that when LMUL < 1, the tail includes the elements past VLMAX that
 * are held in the same vector register.
 */
static inline uint32_t vext_get_total_elems(CPURISCVState *env, uint32_t desc,
                                            uint32_t esz)
{
    uint32_t vlenb = simd_maxsz(desc);
    uint32_t sew = 1 << FIELD_EX64(env->vtype, VTYPE, VSEW);
    int8_t emul = ctzl(esz) - ctzl(sew) + vext_lmul(desc) < 0 ? 0 :
                  ctzl(esz) - ctzl(sew) + vext_lmul(desc);
    return (vlenb << emul) / esz;
}

/* set agnostic elements to 1s */
void vext_set_elems_1s(void *base, uint32_t is_agnostic, uint32_t cnt,
                       uint32_t tot);

/* expand macro args before macro */
#define RVVCALL(macro, ...)  macro(__VA_ARGS__)

/* (TD, T2, TX2) */
#define OP_UU_B uint8_t, uint8_t, uint8_t
#define OP_UU_H uint16_t, uint16_t, uint16_t
#define OP_UU_W uint32_t, uint32_t, uint32_t
#define OP_UU_D uint64_t, uint64_t, uint64_t
#define WOP_UU_B uint16_t, uint8_t,  uint8_t
#define WOP_UU_H uint32_t, uint16_t, uint16_t
#define WOP_UU_W uint64_t, uint32_t, uint32_t
#define NOP_UU_B uint8_t,  uint16_t, uint32_t
#define NOP_UU_H uint16_t, uint32_t, uint32_t
#define NOP_UU_W uint32_t, uint64_t, uint64_t

/* (TD, T1, T2, TX1, TX2) */
#define OP_UUU_B uint8_t, uint8_t, uint8_t, uint8_t, uint8_t
#define OP_UUU_H uint16_t, uint16_t, uint16_t, uint16_t, uint16_t
#define OP_UUU_W uint32_t, uint32_t, uint32_t, uint32_t, uint32_t
#define OP_UUU_D uint64_t, uint64_t, uint64_t, uint64_t, uint64_t
#define OP_SSS_B int8_t, int8_t, int8_t, int8_t, int8_t
#define OP_SSS_H int16_t, int16_t, int16_t, int16_t, int16_t
#define OP_SSS_W int32_t, int32_t, int32_t, int32_t, int32_t
#define OP_SSS_D int64_t, int64_t, int64_t, int64_t, int64_t
#define OP_SUS_B int8_t, uint8_t, int8_t, uint8_t, int8_t
#define OP_SUS_H int16_t, uint16_t, int16_t, uint16_t, int16_t
#define OP_SUS_W int32_t, uint32_t, int32_t, uint32_t, int32_t
#define OP_SUS_D int64_t, uint64_t, int64_t, uint64_t, int64_t

#define OPIVV1(NAME, TD, T2, TX2, HD, HS2, OP)         \
static void do_##NAME(void *vd, void *vs2, int i)      \
{                                                      \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                    \
    *((TD *)vd + HD(i)) = OP(s2);                      \
}

#define GEN_VEXT_V(NAME, ESZ)                          \
void HELPER(NAME)(void *vd, void *v0, void *vs2,       \
                  CPURISCVState *env, uint32_t desc)   \
{                                                      \
    uint32_t vm = vext_vm(desc);                       \
    uint32_t vl = env->vl;                             \
    uint32_t total_elems =                             \
        vext_get_total_elems(env, desc, ESZ);          \
    uint32_t vta = vext_vta(desc);                     \
    uint32_t vma = vext_vma(desc);                     \
    uint32_t i;                                        \
                                                       \
    VSTART_CHECK_EARLY_EXIT(env);                      \
                                                       \
    for (i = env->vstart; i < vl; i++) {               \
        if (!vm && !vext_elem_mask(v0, i)) {           \
            /* set masked-off elements to 1s */        \
            vext_set_elems_1s(vd, vma, i * ESZ,        \
                              (i + 1) * ESZ);          \
            continue;                                  \
        }                                              \
        do_##NAME(vd, vs2, i);                         \
    }                                                  \
    env->vstart = 0;                                   \
    /* set tail elements to 1s */                      \
    vext_set_elems_1s(vd, vta, vl * ESZ,               \
                      total_elems * ESZ);              \
}

/* operation of two vector elements */
typedef void opivv2_fn(void *vd, void *vs1, void *vs2, int i);

#define OPIVV2(NAME, TD, T1, T2, TX1, TX2, HD, HS1, HS2, OP)    \
static void do_##NAME(void *vd, void *vs1, void *vs2, int i)    \
{                                                               \
    TX1 s1 = *((T1 *)vs1 + HS1(i));                             \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                             \
    *((TD *)vd + HD(i)) = OP(s2, s1);                           \
}

void do_vext_vv(void *vd, void *v0, void *vs1, void *vs2,
                CPURISCVState *env, uint32_t desc,
                opivv2_fn *fn, uint32_t esz);

/* generate the helpers for OPIVV */
#define GEN_VEXT_VV(NAME, ESZ)                            \
void HELPER(NAME)(void *vd, void *v0, void *vs1,          \
                  void *vs2, CPURISCVState *env,          \
                  uint32_t desc)                          \
{                                                         \
    do_vext_vv(vd, v0, vs1, vs2, env, desc,               \
               do_##NAME, ESZ);                           \
}

typedef void opivx2_fn(void *vd, target_long s1, void *vs2, int i);

/*
 * (T1)s1 gives the real operator type.
 * (TX1)(T1)s1 expands the operator type of widen or narrow operations.
 */
#define OPIVX2(NAME, TD, T1, T2, TX1, TX2, HD, HS2, OP)             \
static void do_##NAME(void *vd, target_long s1, void *vs2, int i)   \
{                                                                   \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                                 \
    *((TD *)vd + HD(i)) = OP(s2, (TX1)(T1)s1);                      \
}

void do_vext_vx(void *vd, void *v0, target_long s1, void *vs2,
                CPURISCVState *env, uint32_t desc,
                opivx2_fn fn, uint32_t esz);

/* generate the helpers for OPIVX */
#define GEN_VEXT_VX(NAME, ESZ)                            \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1,    \
                  void *vs2, CPURISCVState *env,          \
                  uint32_t desc)                          \
{                                                         \
    do_vext_vx(vd, v0, s1, vs2, env, desc,                \
               do_##NAME, ESZ);                           \
}

/* Three of the widening shortening macros: */
/* (TD, T1, T2, TX1, TX2) */
#define WOP_UUU_B uint16_t, uint8_t, uint8_t, uint16_t, uint16_t
#define WOP_UUU_H uint32_t, uint16_t, uint16_t, uint32_t, uint32_t
#define WOP_UUU_W uint64_t, uint32_t, uint32_t, uint64_t, uint64_t
#define WOP_SSS_B int16_t, int8_t, int8_t, int16_t, int16_t
#define WOP_SSS_H int32_t, int16_t, int16_t, int32_t, int32_t
#define WOP_SSS_W int64_t, int32_t, int32_t, int64_t, int64_t
#define WOP_WUUU_B  uint16_t, uint8_t, uint16_t, uint16_t, uint16_t
#define WOP_WUUU_H  uint32_t, uint16_t, uint32_t, uint32_t, uint32_t
#define WOP_WUUU_W  uint64_t, uint32_t, uint64_t, uint64_t, uint64_t
#define WOP_WSSS_B  int16_t, int8_t, int16_t, int16_t, int16_t
#define WOP_WSSS_H  int32_t, int16_t, int32_t, int32_t, int32_t
#define WOP_WSSS_W  int64_t, int32_t, int64_t, int64_t, int64_t
#define WOP_SUS_B int16_t, uint8_t, int8_t, uint16_t, int16_t
#define WOP_SUS_H int32_t, uint16_t, int16_t, uint32_t, int32_t
#define WOP_SUS_W int64_t, uint32_t, int32_t, uint64_t, int64_t
#define WOP_SSU_B int16_t, int8_t, uint8_t, int16_t, uint16_t
#define WOP_SSU_H int32_t, int16_t, uint16_t, int32_t, uint32_t
#define WOP_SSU_W int64_t, int32_t, uint32_t, int64_t, uint64_t
#define NOP_SSS_B int8_t, int8_t, int16_t, int8_t, int16_t
#define NOP_SSS_H int16_t, int16_t, int32_t, int16_t, int32_t
#define NOP_SSS_W int32_t, int32_t, int64_t, int32_t, int64_t
#define NOP_UUU_B uint8_t, uint8_t, uint16_t, uint8_t, uint16_t
#define NOP_UUU_H uint16_t, uint16_t, uint32_t, uint16_t, uint32_t
#define NOP_UUU_W uint32_t, uint32_t, uint64_t, uint32_t, uint64_t

/* share functions */
static inline target_ulong adjust_addr(CPURISCVState *env, target_ulong addr)
{
    RISCVPmPmm pmm = riscv_pm_get_pmm(env);
    if (pmm == PMM_FIELD_DISABLED) {
        return addr;
    }
    int pmlen = riscv_pm_get_pmlen(pmm);
    bool signext = riscv_cpu_virt_mem_enabled(env);
    addr = addr << pmlen;
    /* sign/zero extend masked address by N-1 bit */
    if (signext) {
        addr = (target_long)addr >> pmlen;
    } else {
        addr = addr >> pmlen;
    }
    return addr;
}

target_ulong idx_b(target_ulong base, uint32_t idx, void *vs2);
target_ulong idx_h(target_ulong base, uint32_t idx, void *vs2);
target_ulong idx_w(target_ulong base, uint32_t idx, void *vs2);
target_ulong idx_d(target_ulong base, uint32_t idx, void *vs2);

void probe_pages(CPURISCVState *env, target_ulong addr,
                 target_ulong len, uintptr_t ra,
                 MMUAccessType access_type);

int8_t do_mulh_b(int8_t s2, int8_t s1);
int16_t do_mulh_h(int16_t s2, int16_t s1);
int32_t do_mulh_w(int32_t s2, int32_t s1);
int64_t do_mulh_d(int64_t s2, int64_t s1);
uint8_t do_mulhu_b(uint8_t s2, uint8_t s1);
uint16_t do_mulhu_h(uint16_t s2, uint16_t s1);
uint32_t do_mulhu_w(uint32_t s2, uint32_t s1);
uint64_t do_mulhu_d(uint64_t s2, uint64_t s1);
int8_t do_mulhsu_b(int8_t s2, uint8_t s1);
int16_t do_mulhsu_h(int16_t s2, uint16_t s1);
int32_t do_mulhsu_w(int32_t s2, uint32_t s1);
int64_t do_mulhsu_d(int64_t s2, uint64_t s1);

uint8_t saddu8(CPURISCVState *env, int vxrm, uint8_t a, uint8_t b);
uint16_t saddu16(CPURISCVState *env, int vxrm, uint16_t a, uint16_t b);
uint32_t saddu32(CPURISCVState *env, int vxrm, uint32_t a, uint32_t b);
uint64_t saddu64(CPURISCVState *env, int vxrm, uint64_t a, uint64_t b);

int8_t sadd8(CPURISCVState *env, int vxrm, int8_t a, int8_t b);
int16_t sadd16(CPURISCVState *env, int vxrm, int16_t a, int16_t b);
int32_t sadd32(CPURISCVState *env, int vxrm, int32_t a, int32_t b);
int64_t sadd64(CPURISCVState *env, int vxrm, int64_t a, int64_t b);

int8_t ssub8(CPURISCVState *env, int vxrm, int8_t a, int8_t b);
int16_t ssub16(CPURISCVState *env, int vxrm, int16_t a, int16_t b);
int32_t ssub32(CPURISCVState *env, int vxrm, int32_t a, int32_t b);
int64_t ssub64(CPURISCVState *env, int vxrm, int64_t a, int64_t b);

uint8_t ssubu8(CPURISCVState *env, int vxrm, uint8_t a, uint8_t b);
uint16_t ssubu16(CPURISCVState *env, int vxrm, uint16_t a, uint16_t b);
uint32_t ssubu32(CPURISCVState *env, int vxrm, uint32_t a, uint32_t b);
uint64_t ssubu64(CPURISCVState *env, int vxrm, uint64_t a, uint64_t b);

int32_t aadd32(CPURISCVState *env, int vxrm, int32_t a, int32_t b);
int64_t aadd64(CPURISCVState *env, int vxrm, int64_t a, int64_t b);
int32_t asub32(CPURISCVState *env, int vxrm, int32_t a, int32_t b);
int64_t asub64(CPURISCVState *env, int vxrm, int64_t a, int64_t b);

int8_t vsmul8(CPURISCVState *env, int vxrm, int8_t a, int8_t b);
int16_t vsmul16(CPURISCVState *env, int vxrm, int16_t a, int16_t b);
int32_t vsmul32(CPURISCVState *env, int vxrm, int32_t a, int32_t b);
int64_t vsmul64(CPURISCVState *env, int vxrm, int64_t a, int64_t b);

uint8_t get_round(int vxrm, uint64_t v, uint8_t shift);

uint8_t vssrl8(CPURISCVState *env, int vxrm, uint8_t a, uint8_t b);
uint16_t vssrl16(CPURISCVState *env, int vxrm, uint16_t a, uint16_t b);
uint32_t vssrl32(CPURISCVState *env, int vxrm, uint32_t a, uint32_t b);
uint64_t vssrl64(CPURISCVState *env, int vxrm, uint64_t a, uint64_t b);

int8_t vssra8(CPURISCVState *env, int vxrm, int8_t a, int8_t b);
int16_t vssra16(CPURISCVState *env, int vxrm, int16_t a, int16_t b);
int32_t vssra32(CPURISCVState *env, int vxrm, int32_t a, int32_t b);
int64_t vssra64(CPURISCVState *env, int vxrm, int64_t a, int64_t b);

int8_t vnclip8(CPURISCVState *env, int vxrm, int16_t a, int8_t b);
int16_t vnclip16(CPURISCVState *env, int vxrm, int32_t a, int16_t b);
int32_t vnclip32(CPURISCVState *env, int vxrm, int64_t a, int32_t b);

uint8_t vnclipu8(CPURISCVState *env, int vxrm, uint16_t a, uint8_t b);
uint16_t vnclipu16(CPURISCVState *env, int vxrm, uint32_t a, uint16_t b);
uint32_t vnclipu32(CPURISCVState *env, int vxrm, uint64_t a, uint32_t b);

uint16_t bfloat16_rsub(uint16_t a, uint16_t b, float_status *s);
uint16_t float16_rsub(uint16_t a, uint16_t b, float_status *s);
uint32_t float32_rsub(uint32_t a, uint32_t b, float_status *s);
uint64_t float64_rsub(uint64_t a, uint64_t b, float_status *s);

uint32_t vfwaddbf16(uint16_t a, uint16_t b, float_status *s);
uint32_t vfwadd16(uint16_t a, uint16_t b, float_status *s);
uint64_t vfwadd32(uint32_t a, uint32_t b, float_status *s);
uint32_t vfwsubbf16(uint16_t a, uint16_t b, float_status *s);
uint32_t vfwsub16(uint16_t a, uint16_t b, float_status *s);
uint64_t vfwsub32(uint32_t a, uint32_t b, float_status *s);
uint32_t vfwaddwbf16(uint32_t a, uint16_t b, float_status *s);
uint32_t vfwaddw16(uint32_t a, uint16_t b, float_status *s);
uint64_t vfwaddw32(uint64_t a, uint32_t b, float_status *s);
uint32_t vfwsubwbf16(uint32_t a, uint16_t b, float_status *s);
uint32_t vfwsubw16(uint32_t a, uint16_t b, float_status *s);
uint64_t vfwsubw32(uint64_t a, uint32_t b, float_status *s);

uint16_t bfloat16_rdiv(uint16_t a, uint16_t b, float_status *s);
uint16_t float16_rdiv(uint16_t a, uint16_t b, float_status *s);
uint32_t float32_rdiv(uint32_t a, uint32_t b, float_status *s);
uint64_t float64_rdiv(uint64_t a, uint64_t b, float_status *s);

uint32_t vfwmulbf16(uint16_t a, uint16_t b, float_status *s);
uint32_t vfwmul16(uint16_t a, uint16_t b, float_status *s);
uint64_t vfwmul32(uint32_t a, uint32_t b, float_status *s);

uint16_t fmaccbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint16_t fmacc16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint32_t fmacc32(uint32_t a, uint32_t b, uint32_t d, float_status *s);
uint64_t fmacc64(uint64_t a, uint64_t b, uint64_t d, float_status *s);
uint16_t fnmaccbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint16_t fnmacc16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint32_t fnmacc32(uint32_t a, uint32_t b, uint32_t d, float_status *s);
uint64_t fnmacc64(uint64_t a, uint64_t b, uint64_t d, float_status *s);
uint16_t fmsacbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint16_t fmsac16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint32_t fmsac32(uint32_t a, uint32_t b, uint32_t d, float_status *s);
uint64_t fmsac64(uint64_t a, uint64_t b, uint64_t d, float_status *s);
uint16_t fnmsacbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint16_t fnmsac16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint32_t fnmsac32(uint32_t a, uint32_t b, uint32_t d, float_status *s);
uint64_t fnmsac64(uint64_t a, uint64_t b, uint64_t d, float_status *s);
uint16_t fmaddbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint16_t fmadd16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint32_t fmadd32(uint32_t a, uint32_t b, uint32_t d, float_status *s);
uint64_t fmadd64(uint64_t a, uint64_t b, uint64_t d, float_status *s);
uint16_t fnmaddbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint16_t fnmadd16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint32_t fnmadd32(uint32_t a, uint32_t b, uint32_t d, float_status *s);
uint64_t fnmadd64(uint64_t a, uint64_t b, uint64_t d, float_status *s);
uint16_t fmsubbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint16_t fmsub16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint32_t fmsub32(uint32_t a, uint32_t b, uint32_t d, float_status *s);
uint64_t fmsub64(uint64_t a, uint64_t b, uint64_t d, float_status *s);
uint16_t fnmsubbf16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint16_t fnmsub16(uint16_t a, uint16_t b, uint16_t d, float_status *s);
uint32_t fnmsub32(uint32_t a, uint32_t b, uint32_t d, float_status *s);
uint64_t fnmsub64(uint64_t a, uint64_t b, uint64_t d, float_status *s);

uint32_t fwmaccbf16(uint16_t a, uint16_t b, uint32_t d, float_status *s);
uint32_t fwmacc16(uint16_t a, uint16_t b, uint32_t d, float_status *s);
uint64_t fwmacc32(uint32_t a, uint32_t b, uint64_t d, float_status *s);
uint32_t fwnmaccbf16(uint16_t a, uint16_t b, uint32_t d, float_status *s);
uint32_t fwnmacc16(uint16_t a, uint16_t b, uint32_t d, float_status *s);
uint64_t fwnmacc32(uint32_t a, uint32_t b, uint64_t d, float_status *s);
uint32_t fwmsacbf16(uint16_t a, uint16_t b, uint32_t d, float_status *s);
uint32_t fwmsac16(uint16_t a, uint16_t b, uint32_t d, float_status *s);
uint64_t fwmsac32(uint32_t a, uint32_t b, uint64_t d, float_status *s);
uint32_t fwnmsacbf16(uint16_t a, uint16_t b, uint32_t d, float_status *s);
uint32_t fwnmsac16(uint16_t a, uint16_t b, uint32_t d, float_status *s);
uint64_t fwnmsac32(uint32_t a, uint32_t b, uint64_t d, float_status *s);

uint16_t fsgnj16(uint16_t a, uint16_t b, float_status *s);
uint32_t fsgnj32(uint32_t a, uint32_t b, float_status *s);
uint64_t fsgnj64(uint64_t a, uint64_t b, float_status *s);
uint16_t fsgnjn16(uint16_t a, uint16_t b, float_status *s);
uint32_t fsgnjn32(uint32_t a, uint32_t b, float_status *s);
uint64_t fsgnjn64(uint64_t a, uint64_t b, float_status *s);
uint16_t fsgnjx16(uint16_t a, uint16_t b, float_status *s);
uint32_t fsgnjx32(uint32_t a, uint32_t b, float_status *s);
uint64_t fsgnjx64(uint64_t a, uint64_t b, float_status *s);

bool vmfnebf16(uint16_t a, uint16_t b, float_status *s);
bool vmfne16(uint16_t a, uint16_t b, float_status *s);
bool vmfne32(uint32_t a, uint32_t b, float_status *s);
bool vmfne64(uint64_t a, uint64_t b, float_status *s);
bool vmfgtbf16(uint16_t a, uint16_t b, float_status *s);
bool vmfgt16(uint16_t a, uint16_t b, float_status *s);
bool vmfgt32(uint32_t a, uint32_t b, float_status *s);
bool vmfgt64(uint64_t a, uint64_t b, float_status *s);
bool vmfgebf16(uint16_t a, uint16_t b, float_status *s);
bool vmfge16(uint16_t a, uint16_t b, float_status *s);
bool vmfge32(uint32_t a, uint32_t b, float_status *s);
bool vmfge64(uint64_t a, uint64_t b, float_status *s);

#endif /* TARGET_RISCV_VECTOR_INTERNALS_H */
