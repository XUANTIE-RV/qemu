#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"

static inline uint32_t get_rows(DisasContext *s)
{
    return s->mrowlen / RV_MACC_LEN;
}

static inline target_ulong get_row_mask(DisasContext *s)
{
    return (target_ulong)pow2ceil(get_rows(s)) - 1;
}

static inline bool require_matrix(DisasContext *s)
{
    return s->mcsr_ms != 0;
}

static bool do_mcfg(DisasContext *s, TCGv tval, bool m, bool n, bool k)
{
    TCGv t = tcg_temp_new();

    tcg_debug_assert(m || n || k);
    if (!require_matrix(s)) {
        return false;
    }

    if (m && n && k) {
        tcg_gen_extract_tl(sizem, tval, 0, 8);
        tcg_gen_extract_tl(sizen, tval, 8, 8);
        tcg_gen_extract_tl(sizek, tval, 16, 16);
    } else {
        if (m || n) {
            tcg_gen_extract_tl(t, tval, 0, 8);
        } else {
            tcg_gen_extract_tl(t, tval, 0, 16);
        }
        tcg_gen_mov_tl(m ? sizem : (n ? sizen : sizek), t);
    }

    mark_ms_dirty(s);
    gen_update_pc(s, s->cur_insn_len);
    if (gen_tb_trace()) {
        csky_trace_tb_exit(0x2, s->base.pc_next + s->cur_insn_len -
                                s->base.pc_first);
    }
    lookup_and_goto_ptr(s);
    s->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_mcfgm(DisasContext *s, arg_mm_cfg *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_mcfg(s, src1, true, false, false);
}

static bool trans_mcfgn(DisasContext *s, arg_mm_cfg *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_mcfg(s, src1, false, true, false);
}

static bool trans_mcfgk(DisasContext *s, arg_mm_cfg *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_mcfg(s, src1, false, false, true);
}

static bool trans_mcfg(DisasContext *s, arg_mm_cfg *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_mcfg(s, src1, true, true, true);
}

static bool do_mcfgi(DisasContext *s, target_ulong val, bool m, bool n, bool k)
{
    tcg_debug_assert(m || n || k);
    if (!require_matrix(s)) {
        return false;
    }

    tcg_gen_movi_tl(m ? sizem : (n ? sizen : sizek), val);
    mark_ms_dirty(s);
    gen_update_pc(s, s->cur_insn_len);
    if (gen_tb_trace()) {
        csky_trace_tb_exit(0x2, s->base.pc_next + s->cur_insn_len -
                                s->base.pc_first);
    }
    lookup_and_goto_ptr(s);
    s->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_mcfgmi(DisasContext *s, arg_mm_cfg_i *a)
{
    return do_mcfgi(s, a->uimm7, true, false, false);
}

static bool trans_mcfgni(DisasContext *s, arg_mm_cfg_i *a)
{
    return do_mcfgi(s, a->uimm7, false, true, false);
}

static bool trans_mcfgki(DisasContext *s, arg_mm_cfg_i *a)
{
    return do_mcfgi(s, a->uimm7, false, false, true);
}

/* matrix register offset from env */
static uint32_t mregs_ofs(DisasContext *s, int mreg_idx)
{
    return offsetof(CPURISCVState, mreg) + (mreg_idx * s->mrowlen * s->mrowlen >> 8);
}

/* Check */

/* Check sizeM and sizeK */
static inline bool mmext_check_mk(DisasContext *s)
{
    return !s->mill && !s->kill;
}

/* Check sizeM, sizeN and sizeK */
static inline bool mmext_check_mnk(DisasContext *s)
{
    return !s->nill && mmext_check_mk(s);
}

/* Check sizeM, sizeN(pair) and sizeK */
static inline bool mmext_check_mnpk(DisasContext *s)
{
    return !s->mill && !s->npill && !s->kill;
}

static inline bool mmext_check_pwf16(DisasContext *s)
{
    return s->pwfp && s->f16f16 && require_matrix(s) && mmext_check_mk(s);
}

static inline bool mmext_check_pwf32(DisasContext *s)
{
    return s->pwfp && (s->f16f32 | s->f32f32) && require_matrix(s) &&
           mmext_check_mk(s);
}

static inline bool mmext_check_pwf64(DisasContext *s)
{
    return s->pwfp && (s->f32f64 | s->f64f64) && require_matrix(s) &&
           mmext_check_mk(s);
}

static inline bool mmext_check_cvt_f64f32(DisasContext *s)
{
    return s->pwfp && s->f32f64 && require_matrix(s);
}

static inline bool mmext_check_cvt_f32f16(DisasContext *s)
{
    return s->pwfp && s->f16f32 && require_matrix(s);
}

static inline bool mmext_check_cvt_f32bf16(DisasContext *s)
{
    return s->pwfp && s->f16f32 && require_matrix(s);
}

static inline bool mmext_check_cvt_f8f16(DisasContext *s)
{
    return s->pwfp && s->f8f16 && require_matrix(s);
}

static inline bool mmext_check_cvt_f8f32(DisasContext *s)
{
    return s->pwfp && s->f8f32 && require_matrix(s);
}

static inline bool mmext_check_cvt_f32i32(DisasContext *s)
{
    return s->fp_int_cvt && (s->f32f64 | s->f32f32) && require_matrix(s);
}

static inline bool mmext_check_cvt_f16i8(DisasContext *s)
{
    return s->fp_int_cvt && (s->f16f16 | s->f16f32) && require_matrix(s);
}

static inline bool mmext_check_pwi32(DisasContext *s)
{
    return s->pwint && (s->i4i32 | s->i8i32) && require_matrix(s) &&
           mmext_check_mk(s);
}

static inline bool mmext_check_pwi64(DisasContext *s)
{
    return s->pwint && s->i16i64 && require_matrix(s) && mmext_check_mk(s);
}

static inline bool mmext_check_cvt_i4i8(DisasContext *s)
{
    return s->pwint && s->i4i32 && require_matrix(s) && mmext_check_mk(s);
}

typedef void gen_helper_opimmi(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv, TCGv_env);

static bool mm_opi_trans(uint32_t md, uint32_t ms1, uint32_t ms2,
                         target_ulong s1, gen_helper_opimmi *fn,
                         DisasContext *s)
{
    TCGv_ptr dest, src1, src2;
    TCGv s1_v;

    dest = tcg_temp_new_ptr();
    src1 = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();

    s1_v = tcg_constant_tl(s1 & get_row_mask(s));

    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));
    tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, ms1));
    tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, ms2));

    fn(dest, src1, src2, s1_v, tcg_env);

    mark_ms_dirty(s);
    return true;
}

#define GEN_MMEXT_OPI_TRANS(NAME, CHECK)                                    \
static bool trans_##NAME(DisasContext *s, arg_mm_i *a)                      \
{                                                                           \
    if (CHECK(s)){                                                          \
        return mm_opi_trans(a->md, a->ms1, a->ms2, (target_ulong) a->uimm3, \
                            gen_helper_##NAME, s);                          \
    }                                                                       \
    return false;                                                           \
}

GEN_MMEXT_OPI_TRANS(madd_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(msub_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(msra_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mmul_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mmulh_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mmax_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mmin_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mumax_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mumin_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(msll_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(msrl_s_mv_i, mmext_check_pwi32)

GEN_MMEXT_OPI_TRANS(madd_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(msub_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(msra_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(mmul_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(mmulh_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(mmax_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(mmin_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(msll_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(msrl_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(mumax_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(mumin_d_mv_i, mmext_check_pwi64)

GEN_MMEXT_OPI_TRANS(mfadd_h_mv_i, mmext_check_pwf16)
GEN_MMEXT_OPI_TRANS(mfadd_s_mv_i, mmext_check_pwf32)
GEN_MMEXT_OPI_TRANS(mfadd_d_mv_i, mmext_check_pwf64)
GEN_MMEXT_OPI_TRANS(mfmax_h_mv_i, mmext_check_pwf16)
GEN_MMEXT_OPI_TRANS(mfmax_s_mv_i, mmext_check_pwf32)
GEN_MMEXT_OPI_TRANS(mfmax_d_mv_i, mmext_check_pwf64)
GEN_MMEXT_OPI_TRANS(mfmin_h_mv_i, mmext_check_pwf16)
GEN_MMEXT_OPI_TRANS(mfmin_s_mv_i, mmext_check_pwf32)
GEN_MMEXT_OPI_TRANS(mfmin_d_mv_i, mmext_check_pwf64)
GEN_MMEXT_OPI_TRANS(mfmul_h_mv_i, mmext_check_pwf16)
GEN_MMEXT_OPI_TRANS(mfmul_s_mv_i, mmext_check_pwf32)
GEN_MMEXT_OPI_TRANS(mfmul_d_mv_i, mmext_check_pwf64)
GEN_MMEXT_OPI_TRANS(mfsub_h_mv_i, mmext_check_pwf16)
GEN_MMEXT_OPI_TRANS(mfsub_s_mv_i, mmext_check_pwf32)
GEN_MMEXT_OPI_TRANS(mfsub_d_mv_i, mmext_check_pwf64)

static bool trans_mmov_mm(DisasContext *s, arg_mmov_mm *a)
{
    uint32_t len = get_rows(s) * s->mrowlen / 8;

    if (!require_matrix(s)) {
        return false;
    }

    tcg_gen_gvec_mov(MO_64, mregs_ofs(s, a->md), mregs_ofs(s, a->ms1),
                     len, len);
    mark_ms_dirty(s);
    return true;
}

static bool trans_mmov_mv_x(DisasContext *s, arg_mmov_mv_x *a)
{
    TCGv_ptr dest, src1;
    TCGv row_index;

    if (!require_matrix(s)) {
        return false;
    }

    row_index = get_gpr(s, a->rs1, EXT_ZERO);
    tcg_gen_andi_tl(row_index, row_index, get_row_mask(s));

    dest = tcg_temp_new_ptr();
    src1 = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, a->md));
    tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, a->ms1));

    gen_helper_mmov_mv_x(dest, src1, row_index, tcg_env);

    mark_ms_dirty(s);
    return true;
}

static bool trans_mmov_mv_i(DisasContext *s, arg_mm_mv_i *a)
{
    uint32_t rows = get_rows(s);

    if (!require_matrix(s)) {
        return false;
    }

    for (int i = 0; i < rows; i++) {
        tcg_gen_gvec_mov(MO_64, mregs_ofs(s, a->md) + i * s->mrowlen / 8,
                         mregs_ofs(s, a->ms1) + a->uimm3 * s->mrowlen / 8,
                         s->mrowlen / 8, s->mrowlen / 8);
    }
    mark_ms_dirty(s);
    return true;
}

/* translation function for slide and cast-move instructions */
typedef void gen_helper_slide_cast(TCGv_ptr, TCGv_ptr, TCGv, TCGv_env);
static bool mm_slide_cast_trans(uint32_t md, uint32_t ms1, target_ulong s1,
                                gen_helper_slide_cast *fn, DisasContext *s)
{
    TCGv_ptr dest, src1;
    TCGv s1_v;

    dest = tcg_temp_new_ptr();
    src1 = tcg_temp_new_ptr();
    s1_v = tcg_constant_tl(s1);

    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));
    tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, ms1));

    fn(dest, src1, s1_v, tcg_env);
    mark_ms_dirty(s);
    return true;
}

#define GEN_MMEXT_SLIDE_CAST_TRANS(NAME, CHECK)              \
static bool trans_##NAME(DisasContext *s, arg_mm_mv_i *a)    \
{                                                            \
    if (CHECK(s)) {                                          \
        return mm_slide_cast_trans(a->md, a->ms1, a->uimm3,  \
                                   gen_helper_##NAME, s);    \
    }                                                        \
    return false;                                            \
}

GEN_MMEXT_SLIDE_CAST_TRANS(mcslidedown_b, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcslidedown_d, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcslidedown_h, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcslidedown_s, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcslideup_b, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcslideup_d, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcslideup_h, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcslideup_s, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mrslidedown, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mrslideup, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcmovb_mv_i, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcmovh_mv_i, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcmovs_mv_i, require_matrix)
GEN_MMEXT_SLIDE_CAST_TRANS(mcmovd_mv_i, require_matrix)

static bool trans_mdup_m_x(DisasContext *s, arg_mm_dup *a)
{
    uint32_t len = get_rows(s) * s->mrowlen / 8;
    TCGv t = get_gpr(s, a->rs2, EXT_SIGN);
    TCGv_i64 t_i64 = tcg_temp_new_i64();
    tcg_gen_ext_tl_i64(t_i64, t);

    if (!require_matrix(s)) {
        return false;
    }
    tcg_gen_gvec_dup_i64(a->mo, mregs_ofs(s, a->md), len, len, t_i64);
    mark_ms_dirty(s);
    return true;
}

static void mm_store_element(TCGv_i64 val, TCGv_ptr base,
                             int ofs, int sew)
{
    switch (sew) {
    case MO_8:
        tcg_gen_st8_i64(val, base, ofs);
        break;
    case MO_16:
        tcg_gen_st16_i64(val, base, ofs);
        break;
    case MO_32:
        tcg_gen_st32_i64(val, base, ofs);
        break;
    case MO_64:
        tcg_gen_st_i64(val, base, ofs);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

static bool trans_mmov_m_x(DisasContext *s, arg_mm_mov *a)
{
    if (!require_matrix(s)) {
        return false;
    }
    uint32_t len = get_rows(s) * s->mrowlen / 8;

    TCGv src2 = get_gpr(s, a->rs2, EXT_SIGN);
    TCGv_i64 val = tcg_temp_new_i64();
    tcg_gen_ext_tl_i64(val, src2);

    /* Get byte_index */
    TCGv src1 = get_gpr(s, a->rs1, EXT_ZERO);
    TCGv byte_index = tcg_temp_new();
    tcg_gen_shli_tl(byte_index, src1, a->mo);
    tcg_gen_rem_tl(byte_index, byte_index, tcg_constant_tl(len));
    TCGv_i32 t_i32 = tcg_temp_new_i32();
    TCGv_ptr t_ptr = tcg_temp_new_ptr();

    /* Convert byte_index to pointer, thus it can be added to tcg_env */
    tcg_gen_trunc_tl_i32(t_i32, byte_index);
    tcg_gen_ext_i32_ptr(t_ptr, t_i32);
    tcg_gen_add_ptr(t_ptr, tcg_env, t_ptr);

    mm_store_element(val, t_ptr, mregs_ofs(s, a->md), a->mo);

    mark_ms_dirty(s);
    return true;
}

static void mm_load_element(TCGv_i64 dest, TCGv_ptr base,
                            int ofs, int mo, bool sign)
{
    switch (mo) {
    case MO_8:
        if (!sign) {
            tcg_gen_ld8u_i64(dest, base, ofs);
        } else {
            tcg_gen_ld8s_i64(dest, base, ofs);
        }
        break;
    case MO_16:
        if (!sign) {
            tcg_gen_ld16u_i64(dest, base, ofs);
        } else {
            tcg_gen_ld16s_i64(dest, base, ofs);
        }
        break;
    case MO_32:
        if (!sign) {
            tcg_gen_ld32u_i64(dest, base, ofs);
        } else {
            tcg_gen_ld32s_i64(dest, base, ofs);
        }
        break;
    case MO_64:
        tcg_gen_ld_i64(dest, base, ofs);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

static bool trans_mmov_x_m(DisasContext *s, arg_mm_mxm *a)
{
    if (!require_matrix(s)) {
        return false;
    }
    uint32_t len = get_rows(s) * s->mrowlen / 8;

    TCGv_i64 t_i64 = tcg_temp_new_i64();
    TCGv dest = dest_gpr(s, a->rd);

    /* Get byte_index */
    TCGv src1 = get_gpr(s, a->rs1, EXT_ZERO);
    TCGv byte_index = tcg_temp_new();
    tcg_gen_shli_tl(byte_index, src1, a->mo);
    tcg_gen_rem_tl(byte_index, byte_index, tcg_constant_tl(len));
    TCGv_i32 t_i32 = tcg_temp_new_i32();
    TCGv_ptr t_ptr = tcg_temp_new_ptr();

    /* Convert byte_index to pointer, thus it can be added to tcg_env */
    tcg_gen_trunc_tl_i32(t_i32, byte_index);
    tcg_gen_ext_i32_ptr(t_ptr, t_i32);
    tcg_gen_add_ptr(t_ptr, tcg_env, t_ptr);

    mm_load_element(t_i64, t_ptr, mregs_ofs(s, a->ms2), a->mo, true);

    /* Covert signed i64 result to tl */
    tcg_gen_trunc_i64_tl(dest, t_i64);
    gen_set_gpr(s, a->rd, dest);

    return true;
}

/* check */
static bool mmext_check_mpack(DisasContext *s, arg_mm_rmm *a)
{
    return require_matrix(s);
}

static bool mmext_check_mm_rmm_s(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwi32(s);
}

static bool mmext_check_mm_rmm_d(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwi64(s);
}

static bool mmext_check_mm_rmm_f16(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwf16(s);
}

static bool mmext_check_mm_rmm_f32(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwf32(s);
}

static bool mmext_check_mm_rmm_f64(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwf64(s);
}

static bool mmext_check_mmaqa_b(DisasContext *s, arg_mm_rmm *a)
{
    return s->i8i32 && require_matrix(s) && mmext_check_mnk(s);
}

static inline bool mmext_check_pmmaqa_b_subext(DisasContext *s, arg_mm_rmm *a)
{
    return s->i4i32 && require_matrix(s) && mmext_check_mnk(s);
}

typedef void gen_helper_opimmrmm(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_env);

/* translation function for matrix-vector (register indexed) instructions */
static bool mm_oprmm_trans(uint32_t md, uint32_t ms1, uint32_t ms2,
                           gen_helper_opimmrmm *fn, DisasContext *s)
{
    TCGv_ptr dest, src1, src2;

    dest = tcg_temp_new_ptr();
    src1 = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));
    tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, ms1));
    tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, ms2));

    fn(dest, src1, src2, tcg_env);

    mark_ms_dirty(s);
    return true;
}

#define GEN_MMEXT_OPRMM_TRANS(NAME, CHECK)                  \
static bool trans_##NAME(DisasContext *s, arg_mm_rmm *a)    \
{                                                           \
    if (CHECK(s, a)){                                       \
        return mm_oprmm_trans(a->md, a->ms1, a->ms2,        \
                              gen_helper_##NAME, s);        \
    }                                                       \
    return false;                                           \
}

GEN_MMEXT_OPRMM_TRANS(madd_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(msub_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(msra_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mmul_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mmulh_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mmax_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mmin_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mumax_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mumin_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(msll_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(msrl_s_mm, mmext_check_mm_rmm_s)

GEN_MMEXT_OPRMM_TRANS(madd_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(msub_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(msra_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(mmul_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(mmulh_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(mmax_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(mmin_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(msll_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(msrl_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(mumax_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(mumin_d_mm, mmext_check_mm_rmm_d)

GEN_MMEXT_OPRMM_TRANS(mfadd_h_mm, mmext_check_mm_rmm_f16)
GEN_MMEXT_OPRMM_TRANS(mfmax_h_mm, mmext_check_mm_rmm_f16)
GEN_MMEXT_OPRMM_TRANS(mfmin_h_mm, mmext_check_mm_rmm_f16)
GEN_MMEXT_OPRMM_TRANS(mfmul_h_mm, mmext_check_mm_rmm_f16)
GEN_MMEXT_OPRMM_TRANS(mfsub_h_mm, mmext_check_mm_rmm_f16)
GEN_MMEXT_OPRMM_TRANS(mfadd_s_mm, mmext_check_mm_rmm_f32)
GEN_MMEXT_OPRMM_TRANS(mfmax_s_mm, mmext_check_mm_rmm_f32)
GEN_MMEXT_OPRMM_TRANS(mfmin_s_mm, mmext_check_mm_rmm_f32)
GEN_MMEXT_OPRMM_TRANS(mfmul_s_mm, mmext_check_mm_rmm_f32)
GEN_MMEXT_OPRMM_TRANS(mfsub_s_mm, mmext_check_mm_rmm_f32)
GEN_MMEXT_OPRMM_TRANS(mfadd_d_mm, mmext_check_mm_rmm_f64)
GEN_MMEXT_OPRMM_TRANS(mfmax_d_mm, mmext_check_mm_rmm_f64)
GEN_MMEXT_OPRMM_TRANS(mfmin_d_mm, mmext_check_mm_rmm_f64)
GEN_MMEXT_OPRMM_TRANS(mfmul_d_mm, mmext_check_mm_rmm_f64)
GEN_MMEXT_OPRMM_TRANS(mfsub_d_mm, mmext_check_mm_rmm_f64)

GEN_MMEXT_OPRMM_TRANS(mn4cliph_s_mm,  mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mn4cliphu_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mn4clipl_s_mm,  mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mn4cliplu_s_mm, mmext_check_mm_rmm_s)
GEN_MMEXT_OPRMM_TRANS(mn4cliph_d_mm,  mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(mn4cliphu_d_mm, mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(mn4clipl_d_mm,  mmext_check_mm_rmm_d)
GEN_MMEXT_OPRMM_TRANS(mn4cliplu_d_mm, mmext_check_mm_rmm_d)

GEN_MMEXT_OPRMM_TRANS(mpack,   mmext_check_mpack)
GEN_MMEXT_OPRMM_TRANS(mpackhh, mmext_check_mpack)
GEN_MMEXT_OPRMM_TRANS(mpackhl, mmext_check_mpack)

GEN_MMEXT_OPI_TRANS(mn4cliph_s_mv_i,  mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mn4cliphu_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mn4clipl_s_mv_i,  mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mn4cliplu_s_mv_i, mmext_check_pwi32)
GEN_MMEXT_OPI_TRANS(mn4cliph_d_mv_i,  mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(mn4cliphu_d_mv_i, mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(mn4clipl_d_mv_i,  mmext_check_pwi64)
GEN_MMEXT_OPI_TRANS(mn4cliplu_d_mv_i, mmext_check_pwi64)

/* mmaqa instructions */
GEN_MMEXT_OPRMM_TRANS(mmaqa_b,   mmext_check_mmaqa_b)
GEN_MMEXT_OPRMM_TRANS(mmaqau_b,  mmext_check_mmaqa_b)
GEN_MMEXT_OPRMM_TRANS(mmaqaus_b, mmext_check_mmaqa_b)
GEN_MMEXT_OPRMM_TRANS(mmaqasu_b, mmext_check_mmaqa_b)

GEN_MMEXT_OPRMM_TRANS(pmmaqa_b,   mmext_check_pmmaqa_b_subext)
GEN_MMEXT_OPRMM_TRANS(pmmaqau_b,  mmext_check_pmmaqa_b_subext)
GEN_MMEXT_OPRMM_TRANS(pmmaqaus_b, mmext_check_pmmaqa_b_subext)
GEN_MMEXT_OPRMM_TRANS(pmmaqasu_b, mmext_check_pmmaqa_b_subext)

/* mixed-precision byte x half-byte matrix multiplication check */
static bool mmext_check_mmacc_bp(DisasContext *s, arg_mm_rmm_i * a)
{
    return s->i8i32 && require_matrix(s) && mmext_check_mnk(s);
}

#define GEN_MMEXT_OPRMMI_TRANS(NAME, CHECK)                                 \
static bool trans_##NAME(DisasContext *s, arg_mm_rmm_i *a)                  \
{                                                                           \
    if (CHECK(s, a)) {                                                      \
        return mm_opi_trans(a->md, a->ms1, a->ms2, (target_ulong) a->uimm1, \
                            gen_helper_##NAME, s);                          \
    }                                                                       \
    return false;                                                           \
}

GEN_MMEXT_OPRMMI_TRANS(mmaccsu_s_bp, mmext_check_mmacc_bp)
GEN_MMEXT_OPRMMI_TRANS(mmaccu_s_bp,  mmext_check_mmacc_bp)
GEN_MMEXT_OPRMMI_TRANS(mmaccus_s_bp, mmext_check_mmacc_bp)
GEN_MMEXT_OPRMMI_TRANS(mmacc_s_bp,   mmext_check_mmacc_bp)

/* mmaqa{u, s}.h instructions require the md to be aligned to 2 */
static bool mmext_check_mmaqa_h(DisasContext *s, arg_mm_rmm * a)
{
    return (a->md % 2 == 0) && s->i16i64 && require_matrix(s)
           && mmext_check_mnk(s);
}

GEN_MMEXT_OPRMM_TRANS(mmaqa_h,   mmext_check_mmaqa_h)
GEN_MMEXT_OPRMM_TRANS(mmaqau_h,  mmext_check_mmaqa_h)
GEN_MMEXT_OPRMM_TRANS(mmaqaus_h, mmext_check_mmaqa_h)
GEN_MMEXT_OPRMM_TRANS(mmaqasu_h, mmext_check_mmaqa_h)


/* fmmacc/fwmacc instructions */
static bool mmext_check_fmmacc_h(DisasContext *s, arg_mm_rmm * a)
{
    return (a->ms2 % 2 == 0) && s->f16f16 && require_matrix(s)
           && mmext_check_mnpk(s);
}

static bool mmext_check_fmmacc_s(DisasContext *s, arg_mm_rmm * a)
{
    return s->f32f32 && require_matrix(s) && mmext_check_mnk(s);
}

static bool mmext_check_fmmacc_d(DisasContext *s, arg_mm_rmm * a)
{
    return (a->md % 2 == 0) && s->f64f64 && require_matrix(s)
           && mmext_check_mnk(s);
}

static bool mmext_check_fmmacc_h_to_s(DisasContext *s, arg_mm_rmm * a)
{
    return s->f16f32 && require_matrix(s) && mmext_check_mnk(s);
}

static bool mmext_check_fmmacc_d_s(DisasContext *s, arg_mm_rmm * a)
{
    return s->f32f64 && (a->md % 2 == 0) && require_matrix(s)
           && mmext_check_mnk(s);
}

static bool trans_fmmacc_h(DisasContext *s, arg_mm_rmm * a)
{
    if (mmext_check_fmmacc_h(s, a)) {
        TCGv_ptr dest, src1, src2;

        dest = tcg_temp_new_ptr();
        src1 = tcg_temp_new_ptr();
        src2 = tcg_temp_new_ptr();

        TCGv_i32 bf16_flag = tcg_constant_i32(s->bf16);

        tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, a->md));
        tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, a->ms1));
        tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, a->ms2));

        gen_helper_fmmacc_h(dest, src1, src2, tcg_env, bf16_flag);

        mark_ms_dirty(s);
        return true;
    }
    return false;
}

GEN_MMEXT_OPRMM_TRANS(fmmacc_s,  mmext_check_fmmacc_s)
GEN_MMEXT_OPRMM_TRANS(fmmacc_d,  mmext_check_fmmacc_d)
GEN_MMEXT_OPRMM_TRANS(fmmacc_d_s, mmext_check_fmmacc_d_s)

static bool mmext_check_fmmacc_b_to_h(DisasContext *s, arg_mm_rmm * a)
{
    return s->f8f16 && (a->ms2 % 2 == 0) && require_matrix(s)
           && mmext_check_mnk(s);
}

GEN_MMEXT_OPRMM_TRANS(fmmacc_bf16_e4, mmext_check_fmmacc_b_to_h)
GEN_MMEXT_OPRMM_TRANS(fmmacc_bf16_e5, mmext_check_fmmacc_b_to_h)
GEN_MMEXT_OPRMM_TRANS(fmmacc_h_e4,    mmext_check_fmmacc_b_to_h)
GEN_MMEXT_OPRMM_TRANS(fmmacc_h_e5,    mmext_check_fmmacc_b_to_h)

static bool mmext_check_fmmacc_b_to_s(DisasContext *s, arg_mm_rmm * a)
{
    return s->f8f32 && require_matrix(s) && mmext_check_mnk(s);
}

GEN_MMEXT_OPRMM_TRANS(fmmacc_s_bf16, mmext_check_fmmacc_h_to_s)
GEN_MMEXT_OPRMM_TRANS(fmmacc_s_h,    mmext_check_fmmacc_h_to_s)
GEN_MMEXT_OPRMM_TRANS(fmmacc_s_e4,   mmext_check_fmmacc_b_to_s)
GEN_MMEXT_OPRMM_TRANS(fmmacc_s_e5,   mmext_check_fmmacc_b_to_s)

GEN_MMEXT_OPRMM_TRANS(fmmacc_h_hb,  mmext_check_fmmacc_b_to_h)
GEN_MMEXT_OPRMM_TRANS(fmmaccu_h_hb, mmext_check_fmmacc_b_to_h)

/* mixed-precision float16 x {byte, half-byte} with immediate index */
static bool mmext_check_fmmacc_hh_mpi(DisasContext *s, arg_mm_i * a,
                                      uint32_t max_index)
{
    return s->f16f16 && a->uimm3 <= max_index &&
           require_matrix(s) && mmext_check_mnk(s);
}

static bool mmext_check_fmmacc_sh_mpi(DisasContext *s, arg_mm_i * a,
                                      uint32_t max_index)
{
    return s->f16f32 && a->uimm3 <= max_index &&
           require_matrix(s) && mmext_check_mnk(s);
}

#define GEN_MMEXT_FMMACC_MPI_TRANS(NAME, CHECK, IDX_RANGE)                  \
static bool trans_##NAME(DisasContext *s, arg_mm_i *a)                      \
{                                                                           \
    if (CHECK(s, a, IDX_RANGE)) {                                           \
        return mm_opi_trans(a->md, a->ms1, a->ms2, (target_ulong) a->uimm3, \
                            gen_helper_##NAME, s);                          \
    }                                                                       \
    return false;                                                           \
}

GEN_MMEXT_FMMACC_MPI_TRANS(fmmacc_h_hp,  mmext_check_fmmacc_hh_mpi, 1)
GEN_MMEXT_FMMACC_MPI_TRANS(fmmacc_s_hb,  mmext_check_fmmacc_sh_mpi, 1)
GEN_MMEXT_FMMACC_MPI_TRANS(fmmacc_s_hp,  mmext_check_fmmacc_sh_mpi, 3)
GEN_MMEXT_FMMACC_MPI_TRANS(fmmaccu_h_hp, mmext_check_fmmacc_hh_mpi, 1)
GEN_MMEXT_FMMACC_MPI_TRANS(fmmaccu_s_hb, mmext_check_fmmacc_sh_mpi, 1)
GEN_MMEXT_FMMACC_MPI_TRANS(fmmaccu_s_hp, mmext_check_fmmacc_sh_mpi, 3)

/* floating-point conversion translation */

typedef void gen_helper_fcvt(TCGv_ptr, TCGv_ptr, TCGv_env);

static bool mm_opmm_mv_trans(uint32_t md, uint32_t ms1, gen_helper_fcvt *fn,
                             DisasContext *s) {
    TCGv_ptr dest, src1;

    dest = tcg_temp_new_ptr();
    src1 = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));
    tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, ms1));

    fn(dest, src1, tcg_env);
    mark_ms_dirty(s);
    return true;
}

#define GEN_MMEXT_OPMM_MV_TRANS(NAME, CHECK)             \
static bool trans_##NAME(DisasContext *s, arg_mm_mv *a)  \
{                                                        \
    if (CHECK(s)) {                                      \
        return mm_opmm_mv_trans(a->md, a->ms1,           \
                             gen_helper_##NAME, s);      \
    }                                                    \
    return false;                                        \
}

GEN_MMEXT_OPMM_MV_TRANS(mfcvth_bf16_s, mmext_check_cvt_f32bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e4_h,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e4_s,   mmext_check_cvt_f8f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e5_h,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e5_s,   mmext_check_cvt_f8f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_h_e4,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_h_e5,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_h_s,    mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_s_bf16, mmext_check_cvt_f32bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_s_h,    mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_bf16_s, mmext_check_cvt_f32bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e4_h,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e4_s,   mmext_check_cvt_f8f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e5_h,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e5_s,   mmext_check_cvt_f8f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_h_e4,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_h_e5,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_h_s,    mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_s_bf16, mmext_check_cvt_f32bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_s_h,    mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_d_s,    mmext_check_cvt_f64f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_s_d,    mmext_check_cvt_f64f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_d_s,    mmext_check_cvt_f64f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_s_d,    mmext_check_cvt_f64f32)

GEN_MMEXT_OPMM_MV_TRANS(mfscvt_s_s,  mmext_check_cvt_f32i32)
GEN_MMEXT_OPMM_MV_TRANS(mfscvth_b_h, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mfscvtl_b_h, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mfucvt_s_s,  mmext_check_cvt_f32i32)
GEN_MMEXT_OPMM_MV_TRANS(mfucvth_b_h, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mfucvtl_b_h, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(msfcvt_s_s,  mmext_check_cvt_f32i32)
GEN_MMEXT_OPMM_MV_TRANS(msfcvth_h_b, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(msfcvtl_h_b, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mufcvt_s_s,  mmext_check_cvt_f32i32)
GEN_MMEXT_OPMM_MV_TRANS(mufcvth_h_b, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mufcvtl_h_b, mmext_check_cvt_f16i8)

GEN_MMEXT_OPMM_MV_TRANS(mucvth_b_p, mmext_check_cvt_i4i8)
GEN_MMEXT_OPMM_MV_TRANS(mucvtl_b_p, mmext_check_cvt_i4i8)
GEN_MMEXT_OPMM_MV_TRANS(mscvth_b_p, mmext_check_cvt_i4i8)
GEN_MMEXT_OPMM_MV_TRANS(mscvtl_b_p, mmext_check_cvt_i4i8)

/* load/store translation */
static bool mmext_check_ldst(DisasContext *s)
{
    return require_matrix(s) && mmext_check_mk(s) ;
}

typedef void gen_helper_ldst(TCGv_ptr, TCGv, TCGv, TCGv_env);

static bool mm_ldst_trans(uint32_t md, TCGv rs1, TCGv s2,
                          gen_helper_ldst *fn, DisasContext *s){
    TCGv_ptr dest;

    dest = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));

    fn(dest, rs1, s2, tcg_env);

    return true;
}

#define GEN_MMEXT_LD_TRANS(NAME, CHECK)                     \
static bool trans_##NAME(DisasContext *s, arg_mm_ld *a)     \
{                                                           \
    if (CHECK(s)) {                                         \
        mark_ms_dirty(s);                                   \
        return mm_ldst_trans(a->md,                         \
                           get_gpr(s, a->rs1, EXT_ZERO),    \
                           get_gpr(s, a->rs2, EXT_ZERO),    \
                           gen_helper_##NAME, s);           \
    }                                                       \
    return false;                                           \
}

#define GEN_MMEXT_ST_TRANS(NAME, CHECK)                     \
static bool trans_##NAME(DisasContext *s, arg_mm_st *a)     \
{                                                           \
    if (CHECK(s)) {                                         \
        return mm_ldst_trans(a->ms3,                        \
                           get_gpr(s, a->rs1, EXT_ZERO),    \
                           get_gpr(s, a->rs2, EXT_ZERO),    \
                           gen_helper_##NAME, s);           \
    }                                                       \
    return false;                                           \
}

GEN_MMEXT_LD_TRANS(mld_b, mmext_check_ldst)
GEN_MMEXT_LD_TRANS(mld_h, mmext_check_ldst)
GEN_MMEXT_LD_TRANS(mld_w, mmext_check_ldst)
GEN_MMEXT_LD_TRANS(mld_d, mmext_check_ldst)

GEN_MMEXT_LD_TRANS(msld_b, mmext_check_ldst)
GEN_MMEXT_LD_TRANS(msld_h, mmext_check_ldst)
GEN_MMEXT_LD_TRANS(msld_w, mmext_check_ldst)
GEN_MMEXT_LD_TRANS(msld_d, mmext_check_ldst)

GEN_MMEXT_ST_TRANS(mst_b, mmext_check_ldst)
GEN_MMEXT_ST_TRANS(mst_h, mmext_check_ldst)
GEN_MMEXT_ST_TRANS(mst_w, mmext_check_ldst)
GEN_MMEXT_ST_TRANS(mst_d, mmext_check_ldst)

GEN_MMEXT_ST_TRANS(msst_b, mmext_check_ldst)
GEN_MMEXT_ST_TRANS(msst_h, mmext_check_ldst)
GEN_MMEXT_ST_TRANS(msst_w, mmext_check_ldst)
GEN_MMEXT_ST_TRANS(msst_d, mmext_check_ldst)

/* whole matrix load/store translation */
static bool mmext_check_ldm(DisasContext *s, arg_mm_ldm *a, uint8_t nf)
{
    return require_matrix(s) && (a->md % nf == 0);
}

static bool mmext_check_stm(DisasContext *s, arg_mm_stm *a, uint8_t nf)
{
    return require_matrix(s) && (a->ms3 % nf == 0);
}

typedef void gen_helper_ldstm(TCGv_ptr, TCGv, TCGv_env);

static bool mm_ldstm_trans(uint32_t md, TCGv rs1,
                           gen_helper_ldstm *fn, DisasContext *s){
    TCGv_ptr dest;

    dest = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));

    fn(dest, rs1, tcg_env);

    return true;
}

#define GEN_MMEXT_LDM_TRANS(NAME, CHECK, nf)                \
static bool trans_##NAME(DisasContext *s, arg_mm_ldm *a)    \
{                                                           \
    if (CHECK(s, a, nf)) {                                  \
        mark_ms_dirty(s);                                   \
        return mm_ldstm_trans(a->md,                        \
                           get_gpr(s, a->rs1, EXT_ZERO),    \
                           gen_helper_##NAME, s);           \
    }                                                       \
    return false;                                           \
}

#define GEN_MMEXT_STM_TRANS(NAME, CHECK, nf)                \
static bool trans_##NAME(DisasContext *s, arg_mm_stm *a)    \
{                                                           \
    if (CHECK(s, a, nf)) {                                  \
        return mm_ldstm_trans(a->ms3,                       \
                           get_gpr(s, a->rs1, EXT_ZERO),    \
                           gen_helper_##NAME, s);           \
    }                                                       \
    return false;                                           \
}

GEN_MMEXT_LDM_TRANS(mld1m_b, mmext_check_ldm, 1)
GEN_MMEXT_LDM_TRANS(mld2m_b, mmext_check_ldm, 2)
GEN_MMEXT_LDM_TRANS(mld4m_b, mmext_check_ldm, 4)
GEN_MMEXT_LDM_TRANS(mld8m_b, mmext_check_ldm, 8)

GEN_MMEXT_LDM_TRANS(mld1m_h, mmext_check_ldm, 1)
GEN_MMEXT_LDM_TRANS(mld2m_h, mmext_check_ldm, 2)
GEN_MMEXT_LDM_TRANS(mld4m_h, mmext_check_ldm, 4)
GEN_MMEXT_LDM_TRANS(mld8m_h, mmext_check_ldm, 8)

GEN_MMEXT_LDM_TRANS(mld1m_w, mmext_check_ldm, 1)
GEN_MMEXT_LDM_TRANS(mld2m_w, mmext_check_ldm, 2)
GEN_MMEXT_LDM_TRANS(mld4m_w, mmext_check_ldm, 4)
GEN_MMEXT_LDM_TRANS(mld8m_w, mmext_check_ldm, 8)

GEN_MMEXT_LDM_TRANS(mld1m_d, mmext_check_ldm, 1)
GEN_MMEXT_LDM_TRANS(mld2m_d, mmext_check_ldm, 2)
GEN_MMEXT_LDM_TRANS(mld4m_d, mmext_check_ldm, 4)
GEN_MMEXT_LDM_TRANS(mld8m_d, mmext_check_ldm, 8)

GEN_MMEXT_STM_TRANS(mst1m_b, mmext_check_stm, 1)
GEN_MMEXT_STM_TRANS(mst2m_b, mmext_check_stm, 2)
GEN_MMEXT_STM_TRANS(mst4m_b, mmext_check_stm, 4)
GEN_MMEXT_STM_TRANS(mst8m_b, mmext_check_stm, 8)

GEN_MMEXT_STM_TRANS(mst1m_h, mmext_check_stm, 1)
GEN_MMEXT_STM_TRANS(mst2m_h, mmext_check_stm, 2)
GEN_MMEXT_STM_TRANS(mst4m_h, mmext_check_stm, 4)
GEN_MMEXT_STM_TRANS(mst8m_h, mmext_check_stm, 8)

GEN_MMEXT_STM_TRANS(mst1m_w, mmext_check_stm, 1)
GEN_MMEXT_STM_TRANS(mst2m_w, mmext_check_stm, 2)
GEN_MMEXT_STM_TRANS(mst4m_w, mmext_check_stm, 4)
GEN_MMEXT_STM_TRANS(mst8m_w, mmext_check_stm, 8)

GEN_MMEXT_STM_TRANS(mst1m_d, mmext_check_stm, 1)
GEN_MMEXT_STM_TRANS(mst2m_d, mmext_check_stm, 2)
GEN_MMEXT_STM_TRANS(mst4m_d, mmext_check_stm, 4)
GEN_MMEXT_STM_TRANS(mst8m_d, mmext_check_stm, 8)

static bool trans_mzero(DisasContext *s, arg_mm_rmi *a)
{
    
    uint32_t len = get_rows(s) * s->mrowlen / 8 * (a->uimm3 + 1);

    if (!require_matrix(s)) {
        return false;
    }
    if (a->uimm3 & (a->uimm3 + 1)) {
        return false;
    } 

    tcg_gen_gvec_dup_imm(MO_64, mregs_ofs(s, a->md), len, len, 0);
    mark_ms_dirty(s);
    return true;
}

static bool trans_mrelease(DisasContext *s, arg_mrelease *a)
{
    if (!require_matrix(s)) {
        return false;
    }

#ifndef CONFIG_USER_ONLY
    if (s->mcsr_ms == EXT_STATUS_INITIAL) {
        return true;
    }
    s->mcsr_ms = EXT_STATUS_INITIAL;
    if (!s->msd) {
        TCGv tmp = tcg_temp_new();
        tcg_gen_ld_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus));
        tcg_gen_deposit_tl(tmp, tmp, tcg_constant_tl(EXT_STATUS_INITIAL),
                           25, 2);
        tcg_gen_st_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus));
    }
#endif
    return true;
}
