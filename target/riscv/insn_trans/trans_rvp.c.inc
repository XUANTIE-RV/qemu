/*
 * RISC-V translation routines for the RVP Standard Extension.
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

#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "tcg/tcg.h"

/*
 *** SIMD Data Processing Instructions
 */

/* 16-bit Addition & Subtraction Instructions */

/*
 * For some instructions, such as add16, an oberservation can be utilized:
 * 1) If any reg is zero, it can be reduced to an inline op on the whole reg.
 * 2) Otherwise, it can be acclebrated by an vec op.
 */
static inline bool
r_inline(DisasContext *ctx, arg_r *a, DisasExtend ext,
         void (* vecop)(TCGv, TCGv, TCGv),
         void (* op)(TCGv, TCGv, TCGv))
{
    if (!has_ext(ctx, RVP)) {
        return false;
    }
    if (a->rd && a->rs1 && a->rs2) {
        vecop(cpu_gpr[a->rd], cpu_gpr[a->rs1], cpu_gpr[a->rs2]);
    } else {
        gen_arith(ctx, a, ext, op, NULL);
    }
    return true;
}

/* Complete inline implementation */
#define GEN_RVP_R_INLINE(NAME, EXT, VECOP, OP)           \
static bool trans_##NAME(DisasContext *s, arg_r *a)      \
{                                                        \
    return r_inline(s, a, EXT, VECOP, OP);               \
}

GEN_RVP_R_INLINE(add16, EXT_NONE, tcg_gen_vec_add16_tl, tcg_gen_add_tl);
GEN_RVP_R_INLINE(sub16, EXT_NONE, tcg_gen_vec_sub16_tl, tcg_gen_sub_tl);

/* Out of line helpers for R format packed instructions */
static inline bool
r_ool(DisasContext *ctx, arg_r *a, DisasExtend ext,
      void (* fn)(TCGv, TCGv_ptr, TCGv, TCGv))
{
    TCGv src1, src2, dst;
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = get_gpr(ctx, a->rs1, ext);
    src2 = get_gpr(ctx, a->rs2, ext);
    dst = dest_gpr(ctx, a->rd);

    fn(dst, tcg_env, src1, src2);

    gen_set_gpr(ctx, a->rd, dst);
    return true;
}

#define GEN_RVP_R_OOL(NAME, EXT)                       \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_ool(s, a, EXT, gen_helper_##NAME);        \
}

GEN_RVP_R_OOL(radd16, EXT_NONE);
GEN_RVP_R_OOL(uradd16, EXT_NONE);
GEN_RVP_R_OOL(kadd16, EXT_NONE);
GEN_RVP_R_OOL(ukadd16, EXT_NONE);
GEN_RVP_R_OOL(rsub16, EXT_NONE);
GEN_RVP_R_OOL(ursub16, EXT_NONE);
GEN_RVP_R_OOL(ksub16, EXT_NONE);
GEN_RVP_R_OOL(uksub16, EXT_NONE);
GEN_RVP_R_OOL(cras16, EXT_NONE);
GEN_RVP_R_OOL(rcras16, EXT_NONE);
GEN_RVP_R_OOL(urcras16, EXT_NONE);
GEN_RVP_R_OOL(kcras16, EXT_NONE);
GEN_RVP_R_OOL(ukcras16, EXT_NONE);
GEN_RVP_R_OOL(crsa16, EXT_NONE);
GEN_RVP_R_OOL(rcrsa16, EXT_NONE);
GEN_RVP_R_OOL(urcrsa16, EXT_NONE);
GEN_RVP_R_OOL(kcrsa16, EXT_NONE);
GEN_RVP_R_OOL(ukcrsa16, EXT_NONE);
GEN_RVP_R_OOL(stas16, EXT_NONE);
GEN_RVP_R_OOL(rstas16, EXT_NONE);
GEN_RVP_R_OOL(urstas16, EXT_NONE);
GEN_RVP_R_OOL(kstas16, EXT_NONE);
GEN_RVP_R_OOL(ukstas16, EXT_NONE);
GEN_RVP_R_OOL(stsa16, EXT_NONE);
GEN_RVP_R_OOL(rstsa16, EXT_NONE);
GEN_RVP_R_OOL(urstsa16, EXT_NONE);
GEN_RVP_R_OOL(kstsa16, EXT_NONE);
GEN_RVP_R_OOL(ukstsa16, EXT_NONE);

/* 8-bit Addition & Subtraction Instructions */
GEN_RVP_R_INLINE(add8, EXT_NONE, tcg_gen_vec_add8_tl, tcg_gen_add_tl);
GEN_RVP_R_INLINE(sub8, EXT_NONE, tcg_gen_vec_sub8_tl, tcg_gen_sub_tl);

GEN_RVP_R_OOL(radd8, EXT_NONE);
GEN_RVP_R_OOL(uradd8, EXT_NONE);
GEN_RVP_R_OOL(kadd8, EXT_NONE);
GEN_RVP_R_OOL(ukadd8, EXT_NONE);
GEN_RVP_R_OOL(rsub8, EXT_NONE);
GEN_RVP_R_OOL(ursub8, EXT_NONE);
GEN_RVP_R_OOL(ksub8, EXT_NONE);
GEN_RVP_R_OOL(uksub8, EXT_NONE);

/* 16-bit Shift Instructions */
GEN_RVP_R_OOL(sra16, EXT_NONE);
GEN_RVP_R_OOL(srl16, EXT_NONE);
GEN_RVP_R_OOL(sll16, EXT_NONE);
GEN_RVP_R_OOL(sra16_u, EXT_NONE);
GEN_RVP_R_OOL(srl16_u, EXT_NONE);
GEN_RVP_R_OOL(ksll16, EXT_NONE);
GEN_RVP_R_OOL(kslra16, EXT_NONE);
GEN_RVP_R_OOL(kslra16_u, EXT_NONE);

static bool
rvp_shifti_ool(DisasContext *ctx, arg_shift *a, DisasExtend ext,
               void (* fn)(TCGv, TCGv_ptr, TCGv, TCGv))
{
    TCGv src1, dst, shift;

    src1 = get_gpr(ctx, a->rs1, ext);
    dst = dest_gpr(ctx, a->rd);
    shift = tcg_constant_tl(a->shamt);

    fn(dst, tcg_env, src1, shift);

    gen_set_gpr(ctx, a->rd, dst);
    return true;
}

static inline bool
rvp_shifti(DisasContext *ctx, arg_shift *a, DisasExtend ext,
           void (* vecop)(TCGv, TCGv, target_long),
           void (* op)(TCGv, TCGv_ptr, TCGv, TCGv))
{
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    if (a->rd && a->rs1 && vecop) {
        vecop(cpu_gpr[a->rd], cpu_gpr[a->rs1], a->shamt);
        return true;
    }
    return rvp_shifti_ool(ctx, a, ext, op);
}

#define GEN_RVP_SHIFTI(NAME, VECOP, OP, EXT)             \
static bool trans_##NAME(DisasContext *s, arg_shift *a)  \
{                                                        \
    return rvp_shifti(s, a, EXT, VECOP, OP);             \
}

GEN_RVP_SHIFTI(srai16, tcg_gen_vec_sar16i_tl, gen_helper_sra16, EXT_NONE);
GEN_RVP_SHIFTI(srli16, tcg_gen_vec_shr16i_tl, gen_helper_srl16, EXT_NONE);
GEN_RVP_SHIFTI(slli16, tcg_gen_vec_shl16i_tl, gen_helper_sll16, EXT_NONE);
GEN_RVP_SHIFTI(srai16_u, NULL, gen_helper_sra16_u, EXT_NONE);
GEN_RVP_SHIFTI(srli16_u, NULL, gen_helper_srl16_u, EXT_NONE);
GEN_RVP_SHIFTI(kslli16, NULL, gen_helper_ksll16, EXT_NONE);

/* SIMD 8-bit Shift Instructions */
GEN_RVP_R_OOL(sra8, EXT_NONE);
GEN_RVP_R_OOL(srl8, EXT_NONE);
GEN_RVP_R_OOL(sll8, EXT_NONE);
GEN_RVP_R_OOL(sra8_u, EXT_NONE);
GEN_RVP_R_OOL(srl8_u, EXT_NONE);
GEN_RVP_R_OOL(ksll8, EXT_NONE);
GEN_RVP_R_OOL(kslra8, EXT_NONE);
GEN_RVP_R_OOL(kslra8_u, EXT_NONE);
GEN_RVP_SHIFTI(srai8, tcg_gen_vec_sar8i_tl, gen_helper_sra8, EXT_NONE);
GEN_RVP_SHIFTI(srli8, tcg_gen_vec_shr8i_tl, gen_helper_srl8, EXT_NONE);
GEN_RVP_SHIFTI(slli8, tcg_gen_vec_shl8i_tl, gen_helper_sll8, EXT_NONE);
GEN_RVP_SHIFTI(srai8_u, NULL, gen_helper_sra8_u, EXT_NONE);
GEN_RVP_SHIFTI(srli8_u, NULL, gen_helper_srl8_u, EXT_NONE);
GEN_RVP_SHIFTI(kslli8, NULL, gen_helper_ksll8, EXT_NONE);

/* SIMD 16-bit Compare Instructions */
GEN_RVP_R_OOL(cmpeq16, EXT_NONE);
GEN_RVP_R_OOL(scmplt16, EXT_NONE);
GEN_RVP_R_OOL(scmple16, EXT_NONE);
GEN_RVP_R_OOL(ucmplt16, EXT_NONE);
GEN_RVP_R_OOL(ucmple16, EXT_NONE);

/* SIMD 8-bit Compare Instructions */
GEN_RVP_R_OOL(cmpeq8, EXT_NONE);
GEN_RVP_R_OOL(scmplt8, EXT_NONE);
GEN_RVP_R_OOL(scmple8, EXT_NONE);
GEN_RVP_R_OOL(ucmplt8, EXT_NONE);
GEN_RVP_R_OOL(ucmple8, EXT_NONE);

/* SIMD 16-bit Multiply Instructions */
static void set_pair_regs(DisasContext *ctx, TCGv_i64 dst, int rd)
{
    TCGv t1, t2;

    t1 = tcg_temp_new();
    t2 = tcg_temp_new();

    if (ctx->xl == MXL_RV32) {
        TCGv_i32 lo, hi;

        lo = tcg_temp_new_i32();
        hi = tcg_temp_new_i32();
        tcg_gen_extr_i64_i32(lo, hi, dst);

        tcg_gen_ext_i32_tl(t1, lo);
        tcg_gen_ext_i32_tl(t2, hi);

        gen_set_gpr(ctx, rd, t1);
        gen_set_gpr(ctx, rd + 1, t2);
    } else {
        tcg_gen_trunc_i64_tl(t1, dst);
        gen_set_gpr(ctx, rd, t1);
    }
}

static inline bool
r_d64_ool(DisasContext *ctx, arg_r *a, DisasExtend ext,
          void (* fn)(TCGv_i64, TCGv_ptr, TCGv, TCGv))
{
    TCGv_i64 t3;
    TCGv t1, t2;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    t1 = get_gpr(ctx, a->rs1, ext);
    t2 = get_gpr(ctx, a->rs2, ext);
    t3 = tcg_temp_new_i64();

    fn(t3, tcg_env, t1, t2);
    set_pair_regs(ctx, t3, a->rd);

    return true;
}

#define GEN_RVP_R_D64_OOL(NAME, EXT)                   \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_d64_ool(s, a, EXT, gen_helper_##NAME);    \
}

GEN_RVP_R_D64_OOL(smul16, EXT_NONE);
GEN_RVP_R_D64_OOL(smulx16, EXT_NONE);
GEN_RVP_R_D64_OOL(umul16, EXT_NONE);
GEN_RVP_R_D64_OOL(umulx16, EXT_NONE);
GEN_RVP_R_OOL(khm16, EXT_NONE);
GEN_RVP_R_OOL(khmx16, EXT_NONE);

/* SIMD 8-bit Multiply Instructions */
GEN_RVP_R_D64_OOL(smul8, EXT_NONE);
GEN_RVP_R_D64_OOL(smulx8, EXT_NONE);
GEN_RVP_R_D64_OOL(umul8, EXT_NONE);
GEN_RVP_R_D64_OOL(umulx8, EXT_NONE);
GEN_RVP_R_OOL(khm8, EXT_NONE);
GEN_RVP_R_OOL(khmx8, EXT_NONE);

/* SIMD 16-bit Miscellaneous Instructions */
GEN_RVP_R_OOL(smin16, EXT_NONE);
GEN_RVP_R_OOL(umin16, EXT_NONE);
GEN_RVP_R_OOL(smax16, EXT_NONE);
GEN_RVP_R_OOL(umax16, EXT_NONE);
GEN_RVP_SHIFTI(sclip16, NULL, gen_helper_sclip16, EXT_NONE);
GEN_RVP_SHIFTI(uclip16, NULL, gen_helper_uclip16, EXT_NONE);

/* Out of line helpers for R2 format */
static bool
r2_ool(DisasContext *ctx, arg_r2 *a, DisasExtend ext,
       void (* fn)(TCGv, TCGv_ptr, TCGv))
{
    TCGv src1, dst;

    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = get_gpr(ctx, a->rs1, ext);
    dst = dest_gpr(ctx, a->rd);

    fn(dst, tcg_env, src1);
    gen_set_gpr(ctx, a->rd, dst);

    return true;
}

#define GEN_RVP_R2_OOL(NAME, EXT)                      \
static bool trans_##NAME(DisasContext *s, arg_r2 *a)   \
{                                                      \
    return r2_ool(s, a, EXT, gen_helper_##NAME);       \
}

GEN_RVP_R2_OOL(kabs16, EXT_NONE);
GEN_RVP_R2_OOL(clrs16, EXT_NONE);
GEN_RVP_R2_OOL(clz16, EXT_NONE);
GEN_RVP_R2_OOL(clo16, EXT_NONE);

/* SIMD 8-bit Miscellaneous Instructions */
GEN_RVP_R_OOL(smin8, EXT_NONE);
GEN_RVP_R_OOL(umin8, EXT_NONE);
GEN_RVP_R_OOL(smax8, EXT_NONE);
GEN_RVP_R_OOL(umax8, EXT_NONE);
GEN_RVP_SHIFTI(sclip8, NULL, gen_helper_sclip8, EXT_NONE);
GEN_RVP_SHIFTI(uclip8, NULL, gen_helper_uclip8, EXT_NONE);
GEN_RVP_R2_OOL(kabs8, EXT_NONE);
GEN_RVP_R2_OOL(clrs8, EXT_NONE);
GEN_RVP_R2_OOL(clz8, EXT_NONE);
GEN_RVP_R2_OOL(clo8, EXT_NONE);
GEN_RVP_R2_OOL(swap8, EXT_NONE);

/* 8-bit Unpacking Instructions */
GEN_RVP_R2_OOL(sunpkd810, EXT_NONE);
GEN_RVP_R2_OOL(sunpkd820, EXT_NONE);
GEN_RVP_R2_OOL(sunpkd830, EXT_NONE);
GEN_RVP_R2_OOL(sunpkd831, EXT_NONE);
GEN_RVP_R2_OOL(sunpkd832, EXT_NONE);
GEN_RVP_R2_OOL(zunpkd810, EXT_NONE);
GEN_RVP_R2_OOL(zunpkd820, EXT_NONE);
GEN_RVP_R2_OOL(zunpkd830, EXT_NONE);
GEN_RVP_R2_OOL(zunpkd831, EXT_NONE);
GEN_RVP_R2_OOL(zunpkd832, EXT_NONE);

/*
 *** Partial-SIMD Data Processing Instruction
 */
/* 16-bit Packing Instructions */
GEN_RVP_R_OOL(pkbb16, EXT_NONE);
GEN_RVP_R_OOL(pkbt16, EXT_NONE);
GEN_RVP_R_OOL(pktt16, EXT_NONE);
GEN_RVP_R_OOL(pktb16, EXT_NONE);

/* Most Significant Word “32x32” Multiply & Add Instructions */
GEN_RVP_R_OOL(smmul, EXT_NONE);
GEN_RVP_R_OOL(smmul_u, EXT_NONE);

/* Function to accumulate destination register */
static inline bool r_acc_ool(DisasContext *ctx, arg_r *a, DisasExtend ext,
                             void (* fn)(TCGv, TCGv_ptr, TCGv, TCGv, TCGv))
{
    TCGv src1, src2, src3, dst;

    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = get_gpr(ctx, a->rs1, ext);
    src2 = get_gpr(ctx, a->rs2, ext);
    src3 = get_gpr(ctx, a->rd, ext);
    dst = dest_gpr(ctx, a->rd);

    fn(dst, tcg_env, src1, src2, src3);
    gen_set_gpr(ctx, a->rd, dst);
    return true;
}

#define GEN_RVP_R_ACC_OOL(NAME, EXT)                   \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_acc_ool(s, a, EXT, gen_helper_##NAME);    \
}

GEN_RVP_R_ACC_OOL(kmmac, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmac_u, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmsb, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmsb_u, EXT_NONE);
GEN_RVP_R_OOL(kwmmul, EXT_NONE);
GEN_RVP_R_OOL(kwmmul_u, EXT_NONE);

/* Most Significant Word “32x16” Multiply & Add Instructions */
GEN_RVP_R_OOL(smmwb, EXT_NONE);
GEN_RVP_R_OOL(smmwb_u, EXT_NONE);
GEN_RVP_R_OOL(smmwt, EXT_NONE);
GEN_RVP_R_OOL(smmwt_u, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmawb, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmawb_u, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmawt, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmawt_u, EXT_NONE);
GEN_RVP_R_OOL(kmmwb2, EXT_NONE);
GEN_RVP_R_OOL(kmmwb2_u, EXT_NONE);
GEN_RVP_R_OOL(kmmwt2, EXT_NONE);
GEN_RVP_R_OOL(kmmwt2_u, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmawb2, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmawb2_u, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmawt2, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmmawt2_u, EXT_NONE);

/* Signed 16-bit Multiply with 32-bit Add/Subtract Instructions */
GEN_RVP_R_OOL(smbb16, EXT_NONE);
GEN_RVP_R_OOL(smbt16, EXT_NONE);
GEN_RVP_R_OOL(smtt16, EXT_NONE);
GEN_RVP_R_OOL(kmda, EXT_NONE);
GEN_RVP_R_OOL(kmxda, EXT_NONE);
GEN_RVP_R_OOL(smds, EXT_NONE);
GEN_RVP_R_OOL(smdrs, EXT_NONE);
GEN_RVP_R_OOL(smxds, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmabb, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmabt, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmatt, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmada, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmaxda, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmads, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmadrs, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmaxds, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmsda, EXT_NONE);
GEN_RVP_R_ACC_OOL(kmsxda, EXT_NONE);

/* Signed 16-bit Multiply with 64-bit Add/Subtract Instructions */
static bool
r_d64_s64_ool(DisasContext *ctx, arg_r *a, DisasExtend ext,
              void (* fn)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv_i64 src1, dst;
    TCGv src2;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = get_gpr(ctx, a->rs2, ext);
    dst = tcg_temp_new_i64();

    if (ctx->xl == MXL_RV32) {
        TCGv t0 = get_gpr(ctx, a->rs1, ext);
        TCGv t1 = get_gpr(ctx, a->rs1 + 1, ext);
        tcg_gen_concat_tl_i64(src1, t0, t1);
    } else {
        TCGv t0 = get_gpr(ctx, a->rs1, ext);
        tcg_gen_ext_tl_i64(src1, t0);
    }

    fn(dst, tcg_env, src1, src2);
    set_pair_regs(ctx, dst, a->rd);

    return true;
}

#define GEN_RVP_R_D64_S64_OOL(NAME, EXT)                \
static bool trans_##NAME(DisasContext *s, arg_r *a)     \
{                                                       \
    return r_d64_s64_ool(s, a, EXT, gen_helper_##NAME); \
}

GEN_RVP_R_D64_S64_OOL(smal, EXT_NONE);

/* Partial-SIMD Miscellaneous Instructions */
GEN_RVP_SHIFTI(sclip32, NULL, gen_helper_sclip32, EXT_NONE);
GEN_RVP_SHIFTI(uclip32, NULL, gen_helper_uclip32, EXT_NONE);
GEN_RVP_R2_OOL(clrs32, EXT_NONE);
GEN_RVP_R2_OOL(clz32, EXT_NONE);
GEN_RVP_R2_OOL(clo32, EXT_NONE);
GEN_RVP_R_OOL(pbsad, EXT_NONE);
GEN_RVP_R_ACC_OOL(pbsada, EXT_NONE);

/* 8-bit Multiply with 32-bit Add Instructions */
GEN_RVP_R_ACC_OOL(smaqa, EXT_NONE);
GEN_RVP_R_ACC_OOL(umaqa, EXT_NONE);
GEN_RVP_R_ACC_OOL(smaqa_su, EXT_NONE);

/*
 *** 64-bit Profile Instructions
 */
/* 64-bit Addition & Subtraction Instructions */
static bool
r_d64_s64_s64_ool(DisasContext *ctx, arg_r *a, DisasExtend ext,
                  void (* fn)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv_i64 src1, src2, dst;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new_i64();
    dst = tcg_temp_new_i64();

    if (ctx->xl == MXL_RV32) {
        TCGv a0 = get_gpr(ctx, a->rs1, ext);
        TCGv a1 = get_gpr(ctx, a->rs1 + 1, ext);
        TCGv b0 = get_gpr(ctx, a->rs2, ext);
        TCGv b1 = get_gpr(ctx, a->rs2 + 1, ext);

        tcg_gen_concat_tl_i64(src1, a0, a1);
        tcg_gen_concat_tl_i64(src2, b0, b1);
    } else {
        TCGv t1 = get_gpr(ctx, a->rs1, ext);
        TCGv t2 = get_gpr(ctx, a->rs2, ext);
        tcg_gen_ext_tl_i64(src1, t1);
        tcg_gen_ext_tl_i64(src2, t2);
    }

    fn(dst, tcg_env, src1, src2);
    set_pair_regs(ctx, dst, a->rd);

    return true;
}

#define GEN_RVP_R_D64_S64_S64_OOL(NAME, EXT)                \
static bool trans_##NAME(DisasContext *s, arg_r *a)         \
{                                                           \
    return r_d64_s64_s64_ool(s, a, EXT, gen_helper_##NAME); \
}

GEN_RVP_R_D64_S64_S64_OOL(add64, EXT_NONE);
GEN_RVP_R_D64_S64_S64_OOL(radd64, EXT_NONE);
GEN_RVP_R_D64_S64_S64_OOL(uradd64, EXT_NONE);
GEN_RVP_R_D64_S64_S64_OOL(kadd64, EXT_NONE);
GEN_RVP_R_D64_S64_S64_OOL(ukadd64, EXT_NONE);
GEN_RVP_R_D64_S64_S64_OOL(sub64, EXT_NONE);
GEN_RVP_R_D64_S64_S64_OOL(rsub64, EXT_NONE);
GEN_RVP_R_D64_S64_S64_OOL(ursub64, EXT_NONE);
GEN_RVP_R_D64_S64_S64_OOL(ksub64, EXT_NONE);
GEN_RVP_R_D64_S64_S64_OOL(uksub64, EXT_NONE);

/* 32-bit Multiply with 64-bit Add/Subtract Instructions */

/* Function to accumulate 64bit destination register */
static bool
r_d64_acc_ool(DisasContext *ctx, arg_r *a, DisasExtend ext,
              void (* fn)(TCGv_i64, TCGv_ptr, TCGv, TCGv, TCGv_i64))
{
    TCGv src1, src2;
    TCGv_i64 dst, src3;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = get_gpr(ctx, a->rs1, ext);
    src2 = get_gpr(ctx, a->rs2, ext);
    src3 = tcg_temp_new_i64();
    dst = tcg_temp_new_i64();

    if (ctx->xl == MXL_RV32) {
        TCGv t0 = get_gpr(ctx, a->rd, ext);
        TCGv t1 = get_gpr(ctx, a->rd + 1, ext);
        tcg_gen_concat_tl_i64(src3, t0, t1);
    } else {
        TCGv t0 = get_gpr(ctx, a->rd, ext);
        tcg_gen_ext_tl_i64(src3, t0);
    }

    fn(dst, tcg_env, src1, src2, src3);

    set_pair_regs(ctx, dst, a->rd);

    return true;
}

#define GEN_RVP_R_D64_ACC_OOL(NAME, EXT)                \
static bool trans_##NAME(DisasContext *s, arg_r *a)     \
{                                                       \
    return r_d64_acc_ool(s, a, EXT, gen_helper_##NAME); \
}

GEN_RVP_R_D64_ACC_OOL(smar64, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smsr64, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(umar64, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(umsr64, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(kmar64, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(kmsr64, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(ukmar64, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(ukmsr64, EXT_NONE);

/* Signed 16-bit Multiply with 64-bit Add/Subtract Instructions */
GEN_RVP_R_D64_ACC_OOL(smalbb, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smalbt, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smaltt, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smalda, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smalxda, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smalds, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smaldrs, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smalxds, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smslda, EXT_NONE);
GEN_RVP_R_D64_ACC_OOL(smslxda, EXT_NONE);

/*
 *** Non-SIMD Instructions
 */
/* Non-SIMD Q15 saturation ALU Instructions */
GEN_RVP_R_OOL(kaddh, EXT_NONE);
GEN_RVP_R_OOL(ksubh, EXT_NONE);
GEN_RVP_R_OOL(khmbb, EXT_NONE);
GEN_RVP_R_OOL(khmbt, EXT_NONE);
GEN_RVP_R_OOL(khmtt, EXT_NONE);
GEN_RVP_R_OOL(ukaddh, EXT_NONE);
GEN_RVP_R_OOL(uksubh, EXT_NONE);

/* Non-SIMD Q31 saturation ALU Instructions */
GEN_RVP_R_OOL(kaddw, EXT_NONE);
GEN_RVP_R_OOL(ukaddw, EXT_NONE);
GEN_RVP_R_OOL(ksubw, EXT_NONE);
GEN_RVP_R_OOL(uksubw, EXT_NONE);
GEN_RVP_R_OOL(kdmbb, EXT_NONE);
GEN_RVP_R_OOL(kdmbt, EXT_NONE);
GEN_RVP_R_OOL(kdmtt, EXT_NONE);
GEN_RVP_R_OOL(kslraw, EXT_NONE);
GEN_RVP_R_OOL(kslraw_u, EXT_NONE);
GEN_RVP_R_OOL(ksllw, EXT_NONE);
GEN_RVP_SHIFTI(kslliw, NULL, gen_helper_ksllw, EXT_NONE);
GEN_RVP_R_ACC_OOL(kdmabb, EXT_NONE);
GEN_RVP_R_ACC_OOL(kdmabt, EXT_NONE);
GEN_RVP_R_ACC_OOL(kdmatt, EXT_NONE);
GEN_RVP_R2_OOL(kabsw, EXT_NONE);

/* 32-bit Computation Instructions */
GEN_RVP_R_OOL(raddw, EXT_NONE);
GEN_RVP_R_OOL(uraddw, EXT_NONE);
GEN_RVP_R_OOL(rsubw, EXT_NONE);
GEN_RVP_R_OOL(ursubw, EXT_NONE);
GEN_RVP_R_OOL(minw, EXT_NONE);
GEN_RVP_R_OOL(maxw, EXT_NONE);
GEN_RVP_R_D64_OOL(mulr64, EXT_NONE);
GEN_RVP_R_D64_OOL(mulsr64, EXT_NONE);

/* Non-SIMD Miscellaneous Instructions */
GEN_RVP_R_OOL(ave, EXT_NONE);
GEN_RVP_R_OOL(sra_u, EXT_NONE);
GEN_RVP_SHIFTI(srai_u, NULL, gen_helper_sra_u, EXT_NONE);
GEN_RVP_R_OOL(bitrev, EXT_NONE);
GEN_RVP_SHIFTI(bitrevi, NULL, gen_helper_bitrev, EXT_NONE);

static bool
r_s64_ool(DisasContext *ctx, arg_r *a, DisasExtend ext,
          void (* fn)(TCGv, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv_i64 src1;
    TCGv src2, dst;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = get_gpr(ctx, a->rs2, ext);
    dst = dest_gpr(ctx, a->rd);

    if (ctx->xl == MXL_RV32) {
        TCGv t0 = get_gpr(ctx, a->rs1, ext);
        TCGv t1 = get_gpr(ctx, a->rs1 + 1, ext);
        tcg_gen_concat_tl_i64(src1, t0, t1);
    } else {
        TCGv t0 = get_gpr(ctx, a->rs1, ext);
        tcg_gen_ext_tl_i64(src1, t0);
    }
    fn(dst, tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dst);

    return true;
}

#define GEN_RVP_R_S64_OOL(NAME, EXT)                   \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_s64_ool(s, a, EXT, gen_helper_##NAME);    \
}

GEN_RVP_R_S64_OOL(wext, EXT_NONE);

static bool rvp_shifti_s64_ool(DisasContext *ctx, arg_shift *a, DisasExtend ext,
                               void (* fn)(TCGv, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv_i64 src1;
    TCGv dst, shift;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    dst = dest_gpr(ctx, a->rd);

    if (ctx->xl == MXL_RV32) {
        TCGv t0 = get_gpr(ctx, a->rs1, ext);
        TCGv t1 = get_gpr(ctx, a->rs1 + 1, ext);
        tcg_gen_concat_tl_i64(src1, t0, t1);
    } else {
        TCGv t0 = get_gpr(ctx, a->rs1, ext);
        tcg_gen_ext_tl_i64(src1, t0);
    }
    shift = tcg_constant_tl(a->shamt);
    fn(dst, tcg_env, src1, shift);
    gen_set_gpr(ctx, a->rd, dst);

    return true;
}

#define GEN_RVP_SHIFTI_S64_OOL(NAME, OP, EXT)               \
static bool trans_##NAME(DisasContext *s, arg_shift *a)     \
{                                                           \
    return rvp_shifti_s64_ool(s, a, EXT, gen_helper_##OP);  \
}

GEN_RVP_SHIFTI_S64_OOL(wexti, wext, EXT_NONE);

typedef void gen_helper_rvp_r4(TCGv, TCGv_ptr, TCGv, TCGv, TCGv);

static bool r4_ool(DisasContext *ctx, arg_r4 *a, DisasExtend ext,
                   gen_helper_rvp_r4 *fn)
{
    TCGv src1, src2, src3, dst;

    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = get_gpr(ctx, a->rs1, ext);
    src2 = get_gpr(ctx, a->rs2, ext);
    src3 = get_gpr(ctx, a->rs3, ext);
    dst = dest_gpr(ctx, a->rd);

    fn(dst, tcg_env, src1, src2, src3);
    gen_set_gpr(ctx, a->rd, dst);

    return true;
}

#define GEN_RVP_R4_OOL(NAME, EXT)                      \
static bool trans_##NAME(DisasContext *s, arg_r4 *a)   \
{                                                      \
    return r4_ool(s, a, EXT, gen_helper_##NAME);       \
}

GEN_RVP_R4_OOL(bpick, EXT_NONE);

static bool trans_insb(DisasContext *ctx, arg_shift *a)
{
    TCGv src1, d1, dst, b0;
    uint8_t shift;

    if (!has_ext(ctx, RVP)) {
        return false;
    }
    if (ctx->xl == MXL_RV32) {
        shift = a->shamt & 0x3;
    } else {
        shift = a->shamt;
    }
    src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    d1 = get_gpr(ctx, a->rd, EXT_NONE);
    dst = dest_gpr(ctx, a->rd);
    b0 = tcg_temp_new();

    tcg_gen_andi_tl(b0, src1, 0xff);
    tcg_gen_deposit_tl(dst, d1, b0, shift * 8, 8);
    gen_set_gpr(ctx, a->rd, dst);

    return true;
}

static bool trans_maddr32(DisasContext *ctx, arg_r *a)
{
    TCGv_i32 w1, w2, w3;
    TCGv src1, src2, d1, dst;

    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    d1 = get_gpr(ctx, a->rd, EXT_NONE);
    dst = dest_gpr(ctx, a->rd);
    w1 = tcg_temp_new_i32();
    w2 = tcg_temp_new_i32();
    w3 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(w1, src1);
    tcg_gen_trunc_tl_i32(w2, src2);
    tcg_gen_trunc_tl_i32(w3, d1);

    tcg_gen_mul_i32(w1, w1, w2);
    tcg_gen_add_i32(w3, w3, w1);
    tcg_gen_ext_i32_tl(dst, w3);
    gen_set_gpr(ctx, a->rd, dst);

    return true;
}

static bool trans_msubr32(DisasContext *ctx, arg_r *a)
{
    TCGv_i32 w1, w2, w3;
    TCGv src1, src2, d1, dst;

    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    d1 = get_gpr(ctx, a->rd, EXT_NONE);
    dst = dest_gpr(ctx, a->rd);
    w1 = tcg_temp_new_i32();
    w2 = tcg_temp_new_i32();
    w3 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(w1, src1);
    tcg_gen_trunc_tl_i32(w2, src2);
    tcg_gen_trunc_tl_i32(w3, d1);

    tcg_gen_mul_i32(w1, w1, w2);
    tcg_gen_sub_i32(w3, w3, w1);
    tcg_gen_ext_i32_tl(dst, w3);
    gen_set_gpr(ctx, a->rd, dst);

    return true;
}

/*
 *** RV64 Only Instructions
 */
/* RV64 Only) SIMD 32-bit Add/Subtract Instructions */
#define GEN_RVP64_R_INLINE(NAME, EXT, VECOP, OP)         \
static bool trans_##NAME(DisasContext *s, arg_r *a)      \
{                                                        \
    REQUIRE_64BIT(s);                                    \
    return r_inline(s, a, EXT, VECOP, OP);               \
}

GEN_RVP64_R_INLINE(add32, EXT_NONE, tcg_gen_vec_add32_tl, tcg_gen_add_tl);
GEN_RVP64_R_INLINE(sub32, EXT_NONE, tcg_gen_vec_sub32_tl, tcg_gen_sub_tl);

static bool
r_64_ool(DisasContext *ctx, arg_r *a,
         void (* fn)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv t1, t2, d1;
    TCGv_i64 src1, src2, dst;

    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new_i64();
    dst = tcg_temp_new_i64();
    d1 = dest_gpr(ctx, a->rd);

    t1 = get_gpr(ctx, a->rs1, EXT_NONE);
    t2 = get_gpr(ctx, a->rs2, EXT_NONE);
    tcg_gen_ext_tl_i64(src1, t1);
    tcg_gen_ext_tl_i64(src2, t2);

    fn(dst, tcg_env, src1, src2);
    tcg_gen_trunc_i64_tl(d1, dst);
    gen_set_gpr(ctx, a->rd, d1);

    return true;
}

#define GEN_RVP64_R_OOL(NAME)                          \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    REQUIRE_64BIT(s);                                  \
    return r_64_ool(s, a, gen_helper_##NAME);          \
}

GEN_RVP64_R_OOL(radd32);
GEN_RVP64_R_OOL(uradd32);
GEN_RVP64_R_OOL(kadd32);
GEN_RVP64_R_OOL(ukadd32);
GEN_RVP64_R_OOL(rsub32);
GEN_RVP64_R_OOL(ursub32);
GEN_RVP64_R_OOL(ksub32);
GEN_RVP64_R_OOL(uksub32);
GEN_RVP64_R_OOL(cras32);
GEN_RVP64_R_OOL(rcras32);
GEN_RVP64_R_OOL(urcras32);
GEN_RVP64_R_OOL(kcras32);
GEN_RVP64_R_OOL(ukcras32);
GEN_RVP64_R_OOL(crsa32);
GEN_RVP64_R_OOL(rcrsa32);
GEN_RVP64_R_OOL(urcrsa32);
GEN_RVP64_R_OOL(kcrsa32);
GEN_RVP64_R_OOL(ukcrsa32);
GEN_RVP64_R_OOL(stas32);
GEN_RVP64_R_OOL(rstas32);
GEN_RVP64_R_OOL(urstas32);
GEN_RVP64_R_OOL(kstas32);
GEN_RVP64_R_OOL(ukstas32);
GEN_RVP64_R_OOL(stsa32);
GEN_RVP64_R_OOL(rstsa32);
GEN_RVP64_R_OOL(urstsa32);
GEN_RVP64_R_OOL(kstsa32);
GEN_RVP64_R_OOL(ukstsa32);

/* (RV64 Only) SIMD 32-bit Shift Instructions */
static inline bool
rvp64_shifti(DisasContext *ctx, arg_shift *a,
             void (* fn)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv src1, dst;
    TCGv_i64 t1, d1, shift;
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    dst = dest_gpr(ctx, a->rd);
    t1 = tcg_temp_new_i64();
    d1 = tcg_temp_new_i64();

    tcg_gen_ext_tl_i64(t1, src1);
    shift = tcg_constant_i64(a->shamt);

    fn(d1, tcg_env, t1, shift);
    tcg_gen_trunc_i64_tl(dst, d1);
    gen_set_gpr(ctx, a->rd, dst);

    return true;
}

#define GEN_RVP64_SHIFTI(NAME, OP)                       \
static bool trans_##NAME(DisasContext *s, arg_shift *a)  \
{                                                        \
    REQUIRE_64BIT(s);                                    \
    return rvp64_shifti(s, a, OP);                       \
}

GEN_RVP64_SHIFTI(srai32, gen_helper_sra32);
GEN_RVP64_SHIFTI(srli32, gen_helper_srl32);
GEN_RVP64_SHIFTI(slli32, gen_helper_sll32);

GEN_RVP64_SHIFTI(srai32_u, gen_helper_sra32_u);
GEN_RVP64_SHIFTI(srli32_u, gen_helper_srl32_u);
GEN_RVP64_SHIFTI(kslli32, gen_helper_ksll32);

GEN_RVP64_R_OOL(sra32);
GEN_RVP64_R_OOL(srl32);
GEN_RVP64_R_OOL(sll32);
GEN_RVP64_R_OOL(ksll32);
GEN_RVP64_R_OOL(kslra32);

GEN_RVP64_R_OOL(sra32_u);
GEN_RVP64_R_OOL(srl32_u);
GEN_RVP64_R_OOL(kslra32_u);

/* (RV64 Only) SIMD 32-bit Miscellaneous Instructions */
GEN_RVP64_R_OOL(smin32);
GEN_RVP64_R_OOL(umin32);
GEN_RVP64_R_OOL(smax32);
GEN_RVP64_R_OOL(umax32);

#define GEN_RVP64_R2_OOL(NAME, EXT)                    \
static bool trans_##NAME(DisasContext *s, arg_r2 *a)   \
{                                                      \
    REQUIRE_64BIT(s);                                  \
    return r2_ool(s, a, EXT, gen_helper_##NAME);       \
}

GEN_RVP64_R2_OOL(kabs32, EXT_NONE);

/* (RV64 Only) SIMD Q15 saturating Multiply Instructions */
GEN_RVP64_R_OOL(khmbb16);
GEN_RVP64_R_OOL(khmbt16);
GEN_RVP64_R_OOL(khmtt16);
GEN_RVP64_R_OOL(kdmbb16);
GEN_RVP64_R_OOL(kdmbt16);
GEN_RVP64_R_OOL(kdmtt16);

#define GEN_RVP64_R_ACC_OOL(NAME, EXT)                 \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    REQUIRE_64BIT(s);                                  \
    return r_acc_ool(s, a, EXT, gen_helper_##NAME);    \
}

GEN_RVP64_R_ACC_OOL(kdmabb16, EXT_NONE);
GEN_RVP64_R_ACC_OOL(kdmabt16, EXT_NONE);
GEN_RVP64_R_ACC_OOL(kdmatt16, EXT_NONE);

/* (RV64 Only) 32-bit Multiply Instructions */
GEN_RVP64_R_OOL(smbt32);
GEN_RVP64_R_OOL(smtt32);

/* (RV64 Only) 32-bit Multiply & Add Instructions */
GEN_RVP64_R_ACC_OOL(kmabb32, EXT_NONE);
GEN_RVP64_R_ACC_OOL(kmabt32, EXT_NONE);
GEN_RVP64_R_ACC_OOL(kmatt32, EXT_NONE);

/* (RV64 Only) 32-bit Parallel Multiply & Add Instructions */
GEN_RVP64_R_OOL(kmda32);
GEN_RVP64_R_OOL(kmxda32);
GEN_RVP64_R_ACC_OOL(kmaxda32, EXT_NONE);
GEN_RVP64_R_ACC_OOL(kmads32, EXT_NONE);
GEN_RVP64_R_ACC_OOL(kmadrs32, EXT_NONE);
GEN_RVP64_R_ACC_OOL(kmaxds32, EXT_NONE);
GEN_RVP64_R_ACC_OOL(kmsda32, EXT_NONE);
GEN_RVP64_R_ACC_OOL(kmsxda32, EXT_NONE);
GEN_RVP64_R_OOL(smds32);
GEN_RVP64_R_OOL(smdrs32);
GEN_RVP64_R_OOL(smxds32);

/* (RV64 Only) Non-SIMD 32-bit Shift Instructions */
GEN_RVP64_SHIFTI(sraiw_u, gen_helper_sraiw_u);

/* (RV64 Only) 32-bit Packing Instructions */
GEN_RVP64_R_OOL(pkbb32);
GEN_RVP64_R_OOL(pkbt32);
GEN_RVP64_R_OOL(pktt32);
GEN_RVP64_R_OOL(pktb32);
