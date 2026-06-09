#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"
#include "cpu.h"

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
    return (s->mcsr_ms != EXT_STATUS_DISABLED) && !s->msd;
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

static bool do_mxcfg(DisasContext *s, TCGv tval, bool mxa, bool mxb,
                     bool blk, bool col)
{
    TCGv a_blk, b_blk, a_col, b_col;
    a_blk = tcg_temp_new();
    b_blk = tcg_temp_new();
    a_col = tcg_temp_new();
    b_col = tcg_temp_new();

    tcg_gen_ld_tl(a_blk, tcg_env, offsetof(CPURISCVState, ma_blksize));
    tcg_gen_ld_tl(b_blk, tcg_env, offsetof(CPURISCVState, mb_blksize));
    tcg_gen_ld_tl(a_col, tcg_env, offsetof(CPURISCVState, ma_colidx));
    tcg_gen_ld_tl(b_col, tcg_env, offsetof(CPURISCVState, mb_colidx));

    tcg_debug_assert(blk || col);
    tcg_debug_assert(mxa || mxb);
    if (!require_matrix(s) || (!s->mxf8 && !s->mxf4 && !s->mxf8f4)) {
        return false;
    }

    if (blk && mxa) {
        tcg_gen_extract_tl(a_blk, tval, 0, 2);
    }
    if (blk && mxb) {
        tcg_gen_extract_tl(b_blk, tval, 0, 2);
    }
    if (col && mxa) {
        if (blk) {
            tcg_gen_extract_tl(a_col, tval, 2, 16);
        } else {
            tcg_gen_extract_tl(a_col, tval, 0, 16);
        }
    }
    if (col && mxb) {
        if (blk) {
            tcg_gen_extract_tl(b_col, tval, 2, 16);
        } else {
            tcg_gen_extract_tl(b_col, tval, 0, 16);
        }
    }
    tcg_gen_st_tl(a_blk, tcg_env, offsetof(CPURISCVState, ma_blksize));
    tcg_gen_st_tl(b_blk, tcg_env, offsetof(CPURISCVState, mb_blksize));
    tcg_gen_st_tl(a_col, tcg_env, offsetof(CPURISCVState, ma_colidx));
    tcg_gen_st_tl(b_col, tcg_env, offsetof(CPURISCVState, mb_colidx));

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

static bool trans_mxcfg(DisasContext *s, arg_mx *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_mxcfg(s, src1, a->mxa, a->mxb, true, true);
}

static bool trans_mxcfg_blk(DisasContext *s, arg_mx *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_mxcfg(s, src1, a->mxa, a->mxb, true, false);
}

static bool trans_mxcfg_col(DisasContext *s, arg_mx *a)
{
    TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);
    return do_mxcfg(s, src1, a->mxa, a->mxb, false, true);
}

static bool trans_mxcfgi_blk(DisasContext *s, arg_mx_i *a)
{
    return do_mxcfg(s, tcg_constant_tl(a->imm), a->mxa, a->mxb, true, false);
}

static bool trans_mxcfgi_col(DisasContext *s, arg_mx_i *a)
{
    return do_mxcfg(s, tcg_constant_tl(a->imm), a->mxa, a->mxb, false, true);
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
    return !s->mill && !s->nx2 && !s->kill;
}

static inline bool mmext_check_pwf16_mv(DisasContext *s)
{
    return s->pwfp && s->mew16b && require_matrix(s) && mmext_check_mk(s) &&
           !s->km2;
}

static inline bool mmext_check_pwf32_mv(DisasContext *s)
{
    return s->pwfp && s->mew32b && require_matrix(s) &&
           mmext_check_mk(s) && !s->km4;
}

static inline bool mmext_check_pwf64_mv(DisasContext *s)
{
    return s->pwfp && s->mew64b && require_matrix(s) &&
           mmext_check_mk(s) && !s->km8;
}

static inline bool mmext_check_cvt_f64f32(DisasContext *s)
{
    return s->pwfp && s->mew32b && s->mew64b && require_matrix(s);
}

static inline bool mmext_check_cvt_f32f16(DisasContext *s)
{
    return s->pwfp && s->mew16b && s->mew32b && require_matrix(s);
}

static inline bool mmext_check_cvt_f8f16(DisasContext *s)
{
    return s->pwfp && s->mew8b && s->mew16b && require_matrix(s);
}

static inline bool mmext_check_cvt_f8f32(DisasContext *s)
{
    return s->pwfp && s->mew8b && s->mew32b && require_matrix(s);
}

static inline bool mmext_check_cvt_f32i32(DisasContext *s)
{
    return s->fp_int_cvt && s->mew32b && require_matrix(s);
}

static inline bool mmext_check_cvt_f16i8(DisasContext *s)
{
    return s->fp_int_cvt && s->mew8b && s->mew16b && require_matrix(s);
}

static inline bool mmext_check_pwi32_mv(DisasContext *s)
{
    return s->pwint && s->mew32b && require_matrix(s) && !s->km4 &&
           mmext_check_mk(s);
}

static inline bool mmext_check_pwi64_mv(DisasContext *s)
{
    return s->pwint && s->mew64b && require_matrix(s) && !s->km8 &&
           mmext_check_mk(s);
}

static inline bool mmext_check_cvt_i4i8(DisasContext *s)
{
    return s->pwint && s->mew4b && s->mew8b && require_matrix(s);
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

GEN_MMEXT_OPI_TRANS(madd_w_mv_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(msub_w_mv_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(msra_w_mv_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mmul_w_mv_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mmulh_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mmax_w_mv_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mmin_w_mv_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mumax_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mumin_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(msll_w_mv_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(msrl_w_mv_i,  mmext_check_pwi32_mv)

GEN_MMEXT_OPI_TRANS(madd_w_mc_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(msub_w_mc_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(msra_w_mc_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mmul_w_mc_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mmulh_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mmax_w_mc_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mmin_w_mc_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mumax_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(mumin_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(msll_w_mc_i,  mmext_check_pwi32_mv)
GEN_MMEXT_OPI_TRANS(msrl_w_mc_i,  mmext_check_pwi32_mv)

GEN_MMEXT_OPI_TRANS(madd_d_mv_i,  mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(msub_d_mv_i,  mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(msra_d_mv_i,  mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(mmul_d_mv_i,  mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(mmulh_d_mv_i, mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(mmax_d_mv_i,  mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(mmin_d_mv_i,  mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(msll_d_mv_i,  mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(msrl_d_mv_i,  mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(mumax_d_mv_i, mmext_check_pwi64_mv)
GEN_MMEXT_OPI_TRANS(mumin_d_mv_i, mmext_check_pwi64_mv)

GEN_MMEXT_OPI_TRANS(mfadd_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfadd_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfadd_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfmax_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmax_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfmax_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfmin_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmin_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfmin_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfmul_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmul_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfmul_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfsub_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfsub_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfsub_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfma_h_mv_i,  mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfma_s_mv_i,  mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfma_d_mv_i,  mmext_check_pwf64_mv)

GEN_MMEXT_OPI_TRANS(mfadd_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfadd_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfadd_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfmax_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmax_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfmax_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfmin_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmin_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfmin_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfmul_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmul_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfmul_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfsub_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfsub_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfsub_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_TRANS(mfma_h_mc_i,  mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfma_s_mc_i,  mmext_check_pwf32_mv)
GEN_MMEXT_OPI_TRANS(mfma_d_mc_i,  mmext_check_pwf64_mv)

GEN_MMEXT_OPI_TRANS(mfadd_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmax_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmin_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmul_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfsub_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfma_bf16_mv_i,  mmext_check_pwf16_mv)

GEN_MMEXT_OPI_TRANS(mfadd_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmax_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmin_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfmul_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfsub_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_TRANS(mfma_bf16_mc_i,  mmext_check_pwf16_mv)

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

static bool mmext_check_pwf16_mm(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwf16_mv(s);
}

static bool mmext_check_pwf32_mm(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwf32_mv(s);
}

static bool mmext_check_pwf64_mm(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwf64_mv(s);
}

static bool mmext_check_mmacc_w_b(DisasContext *s, arg_mm_rmm *a)
{
    return s->i8i32 && require_matrix(s) && mmext_check_mnk(s) &&
           (a->md != a->ms1) && (a->md != a->ms2);
}

static inline bool mmext_check_mmacc_w_p(DisasContext *s, arg_mm_rmm *a)
{
    return s->i4i32 && require_matrix(s) && mmext_check_mnk(s) &&
           (a->md != a->ms1) && (a->md != a->ms2);
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

static bool mmext_check_pwi32_mm(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwi32_mv(s);
}

static bool mmext_check_pwi64_mm(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_pwi64_mv(s);
}

GEN_MMEXT_OPRMM_TRANS(madd_w_mm,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(msub_w_mm,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(msra_w_mm,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mmul_w_mm,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mmulh_w_mm, mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mmax_w_mm,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mmin_w_mm,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mumax_w_mm, mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mumin_w_mm, mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(msll_w_mm,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(msrl_w_mm,  mmext_check_pwi32_mm)

GEN_MMEXT_OPRMM_TRANS(madd_d_mm,  mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(msub_d_mm,  mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(msra_d_mm,  mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(mmul_d_mm,  mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(mmulh_d_mm, mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(mmax_d_mm,  mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(mmin_d_mm,  mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(msll_d_mm,  mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(msrl_d_mm,  mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(mumax_d_mm, mmext_check_pwi64_mm)
GEN_MMEXT_OPRMM_TRANS(mumin_d_mm, mmext_check_pwi64_mm)

GEN_MMEXT_OPRMM_TRANS(mfadd_h_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmax_h_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmin_h_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmul_h_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfsub_h_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfadd_s_mm, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfmax_s_mm, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfmin_s_mm, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfmul_s_mm, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfsub_s_mm, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfadd_d_mm, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfmax_d_mm, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfmin_d_mm, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfmul_d_mm, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfsub_d_mm, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfma_h_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfma_s_mm, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfma_d_mm, mmext_check_pwf64_mm)

GEN_MMEXT_OPRMM_TRANS(mfadd_bf16_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmax_bf16_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmin_bf16_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmul_bf16_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfsub_bf16_mm, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfma_bf16_mm, mmext_check_pwf16_mm)

static inline bool mmext_check_mn4clip_mv_w(DisasContext *s)
{
    return s->pwint && s->mew8b && s->mew32b && require_matrix(s);
}

static inline bool mmext_check_mn4clip_mv_d(DisasContext *s)
{
    return s->pwint && s->mew16b && s->mew64b && require_matrix(s); 
}

static bool mmext_check_mn4clip_mm_w(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_mn4clip_mv_w(s);
}

static bool mmext_check_mn4clip_mm_d(DisasContext *s, arg_mm_rmm *a)
{
    return mmext_check_mn4clip_mv_d(s);
}

GEN_MMEXT_OPRMM_TRANS(mn4cliph_s_mm,  mmext_check_mn4clip_mm_w)
GEN_MMEXT_OPRMM_TRANS(mn4cliphu_s_mm, mmext_check_mn4clip_mm_w)
GEN_MMEXT_OPRMM_TRANS(mn4clipl_s_mm,  mmext_check_mn4clip_mm_w)
GEN_MMEXT_OPRMM_TRANS(mn4cliplu_s_mm, mmext_check_mn4clip_mm_w)
GEN_MMEXT_OPRMM_TRANS(mn4cliph_d_mm,  mmext_check_mn4clip_mm_d)
GEN_MMEXT_OPRMM_TRANS(mn4cliphu_d_mm, mmext_check_mn4clip_mm_d)
GEN_MMEXT_OPRMM_TRANS(mn4clipl_d_mm,  mmext_check_mn4clip_mm_d)
GEN_MMEXT_OPRMM_TRANS(mn4cliplu_d_mm, mmext_check_mn4clip_mm_d)

GEN_MMEXT_OPRMM_TRANS(mpack,   mmext_check_mpack)
GEN_MMEXT_OPRMM_TRANS(mpackhh, mmext_check_mpack)
GEN_MMEXT_OPRMM_TRANS(mpackhl, mmext_check_mpack)

GEN_MMEXT_OPI_TRANS(mn4cliph_s_mv_i,  mmext_check_mn4clip_mv_w)
GEN_MMEXT_OPI_TRANS(mn4cliphu_s_mv_i, mmext_check_mn4clip_mv_w)
GEN_MMEXT_OPI_TRANS(mn4clipl_s_mv_i,  mmext_check_mn4clip_mv_w)
GEN_MMEXT_OPI_TRANS(mn4cliplu_s_mv_i, mmext_check_mn4clip_mv_w)
GEN_MMEXT_OPI_TRANS(mn4cliph_d_mv_i,  mmext_check_mn4clip_mv_d)
GEN_MMEXT_OPI_TRANS(mn4cliphu_d_mv_i, mmext_check_mn4clip_mv_d)
GEN_MMEXT_OPI_TRANS(mn4clipl_d_mv_i,  mmext_check_mn4clip_mv_d)
GEN_MMEXT_OPI_TRANS(mn4cliplu_d_mv_i, mmext_check_mn4clip_mv_d)

/* mmacc instructions */
GEN_MMEXT_OPRMM_TRANS(mmacc_w_b,   mmext_check_mmacc_w_b)
GEN_MMEXT_OPRMM_TRANS(mmaccu_w_b,  mmext_check_mmacc_w_b)
GEN_MMEXT_OPRMM_TRANS(mmaccus_w_b, mmext_check_mmacc_w_b)
GEN_MMEXT_OPRMM_TRANS(mmaccsu_w_b, mmext_check_mmacc_w_b)

GEN_MMEXT_OPRMM_TRANS(mmacc_w_p,   mmext_check_mmacc_w_p)
GEN_MMEXT_OPRMM_TRANS(mmaccu_w_p,  mmext_check_mmacc_w_p)
GEN_MMEXT_OPRMM_TRANS(mmaccus_w_p, mmext_check_mmacc_w_p)
GEN_MMEXT_OPRMM_TRANS(mmaccsu_w_p, mmext_check_mmacc_w_p)

/* mixed-precision byte x half-byte matrix multiplication check */
static bool mmext_check_mmacc_w_bp(DisasContext *s, arg_mm_rmm_i * a)
{
    return s->mhp && require_matrix(s) && mmext_check_mnk(s) &&
           (a->md != a->ms1) && (a->md != a->ms2);
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

GEN_MMEXT_OPRMMI_TRANS(mmaccsu_w_bp, mmext_check_mmacc_w_bp)
GEN_MMEXT_OPRMMI_TRANS(mmaccu_w_bp,  mmext_check_mmacc_w_bp)
GEN_MMEXT_OPRMMI_TRANS(mmaccus_w_bp, mmext_check_mmacc_w_bp)
GEN_MMEXT_OPRMMI_TRANS(mmacc_w_bp,   mmext_check_mmacc_w_bp)

/* mmacc{u, s}.d.h instructions require the md to be aligned to 2 */
static bool mmext_check_mmacc_d_h(DisasContext *s, arg_mm_rmm * a)
{
    return (a->md % 2 == 0) && s->i16i64 && require_matrix(s)
           && mmext_check_mnk(s);
}

GEN_MMEXT_OPRMM_TRANS(mmacc_d_h,   mmext_check_mmacc_d_h)
GEN_MMEXT_OPRMM_TRANS(mmaccu_d_h,  mmext_check_mmacc_d_h)
GEN_MMEXT_OPRMM_TRANS(mmaccus_d_h, mmext_check_mmacc_d_h)
GEN_MMEXT_OPRMM_TRANS(mmaccsu_d_h, mmext_check_mmacc_d_h)


/* fmmacc/fwmacc instructions */
static bool mmext_check_fmmacc_h(DisasContext *s, arg_mm_rmm * a)
{
    return mmext_check_mnpk(s) && (s->km2 == 0) && (a->ms2 % 2 == 0) &&
           (a->md != a->ms1) && (a->md != a->ms2) && (a->md != a->ms2 + 1) &&
           s->f16f16 && require_matrix(s);
}

static bool mmext_check_fmmacc_s(DisasContext *s, arg_mm_rmm * a)
{
    return s->f32f32 && require_matrix(s) && mmext_check_mnk(s) &&
           (a->md != a->ms1) && (a->md != a->ms2);
}

static bool mmext_check_fmmacc_d(DisasContext *s, arg_mm_rmm * a)
{
    return (a->md % 2 == 0) && s->f64f64 && require_matrix(s) &&
           mmext_check_mnk(s) && (a->md != a->ms1) && (a->md != a->ms2) &&
           (a->md + 1 != a->ms1) && (a->md + 1 != a->ms2);
}

static bool mmext_check_fmmacc_h_to_s(DisasContext *s, arg_mm_rmm * a)
{
    return mmext_check_mnk(s) && (s->km2 == 0) &&
           (a->md != a->ms1) && (a->md != a->ms2) &&
           s->f16f32 && require_matrix(s);
}

static bool mmext_check_fmmacc_bh_to_s(DisasContext *s, arg_mm_rmm * a)
{
    return mmext_check_mnk(s) && (s->km2 == 0) &&
           (a->md != a->ms1) && (a->md != a->ms2) &&
           s->bf16f32 && require_matrix(s);
}

static bool mmext_check_fmmacc_d_s(DisasContext *s, arg_mm_rmm * a)
{
    return s->f32f64 && (a->md % 2 == 0) && require_matrix(s) &&
           mmext_check_mnk(s) && (a->md != a->ms1) && (a->md != a->ms2) &&
           (a->md + 1 != a->ms1) && (a->md + 1 != a->ms2);
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

static bool mmext_check_mfmacc_bf16(DisasContext *s, arg_mm_rmm * a)
{
    return mmext_check_mnpk(s) && (s->km2 == 0) && (a->ms2 % 2 == 0) &&
           (a->md != a->ms1) && (a->md != a->ms2) && (a->md != a->ms2 + 1) &&
           s->bf16bf16 && require_matrix(s);
}

static bool trans_mfmacc_bf16(DisasContext *s, arg_mm_rmm * a)
{
    if (mmext_check_mfmacc_bf16(s, a)) {
        TCGv_ptr dest, src1, src2;

        dest = tcg_temp_new_ptr();
        src1 = tcg_temp_new_ptr();
        src2 = tcg_temp_new_ptr();

        tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, a->md));
        tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, a->ms1));
        tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, a->ms2));

        gen_helper_fmmacc_h(dest, src1, src2, tcg_env,
                            tcg_constant_i32(true));

        mark_ms_dirty(s);
        return true;
    }
    return false;
}

GEN_MMEXT_OPRMM_TRANS(fmmacc_s,  mmext_check_fmmacc_s)
GEN_MMEXT_OPRMM_TRANS(fmmacc_d,  mmext_check_fmmacc_d)
GEN_MMEXT_OPRMM_TRANS(fmmacc_d_s, mmext_check_fmmacc_d_s)

static bool mmext_check_fmmacc_s_bf20(DisasContext *s, arg_mm_rmm * a)
{
    return mmext_check_mnk(s) && (s->km4 == 0) &&
           (a->md != a->ms1) && (a->md != a->ms2) &&
           s->bf20f32 && require_matrix(s);
}

GEN_MMEXT_OPRMM_TRANS(fmmacc_s_bf20,  mmext_check_fmmacc_s_bf20)

static bool mmext_check_fmmacc_b_to_h(DisasContext *s, arg_mm_rmm * a)
{
    return s->f8f16 && (a->ms2 % 2 == 0) && require_matrix(s)
           && mmext_check_mnpk(s) && (a->md != a->ms1) && (a->md != a->ms2) &&
           (a->md != a->ms2 + 1);
}

static bool mmext_check_fmmacc_b_to_bh(DisasContext *s, arg_mm_rmm * a)
{
    return s->f8bf16 && (a->ms2 % 2 == 0) && require_matrix(s)
           && mmext_check_mnpk(s) && (a->md != a->ms1) && (a->md != a->ms2) &&
           (a->md != a->ms2 + 1);
}

GEN_MMEXT_OPRMM_TRANS(fmmacc_bf16_e4, mmext_check_fmmacc_b_to_bh)
GEN_MMEXT_OPRMM_TRANS(fmmacc_bf16_e5, mmext_check_fmmacc_b_to_bh)
GEN_MMEXT_OPRMM_TRANS(fmmacc_h_e4,    mmext_check_fmmacc_b_to_h)
GEN_MMEXT_OPRMM_TRANS(fmmacc_h_e5,    mmext_check_fmmacc_b_to_h)

static bool mmext_check_fmmacc_b_to_s(DisasContext *s, arg_mm_rmm * a)
{
    return s->f8f32 && require_matrix(s) && mmext_check_mnk(s) &&
           (a->md != a->ms1) && (a->md != a->ms2);
}

GEN_MMEXT_OPRMM_TRANS(fmmacc_s_bf16, mmext_check_fmmacc_bh_to_s)
GEN_MMEXT_OPRMM_TRANS(fmmacc_s_h,    mmext_check_fmmacc_h_to_s)
GEN_MMEXT_OPRMM_TRANS(fmmacc_s_e4,   mmext_check_fmmacc_b_to_s)
GEN_MMEXT_OPRMM_TRANS(fmmacc_s_e5,   mmext_check_fmmacc_b_to_s)

static bool mmext_check_fmmacc_hhb(DisasContext *s, arg_mm_rmm * a)
{
    return s->mhp && require_matrix(s) && mmext_check_mnpk(s) &&
           (a->md != a->ms1) && (a->md != a->ms2);
}

static bool mmext_check_fmmacc_sh_mpi(DisasContext *s, arg_mm_i * a,
                                      uint32_t max_index)
{
    /* FIXME */
    return s->mhp && a->uimm3 <= max_index &&
           require_matrix(s) && mmext_check_mnk(s) &&
           (a->md != a->ms1) && (a->md != a->ms2);
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

GEN_MMEXT_OPMM_MV_TRANS(mfcvth_bf16_s, mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e4_h,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e4_s,   mmext_check_cvt_f8f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e5_h,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e5_s,   mmext_check_cvt_f8f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_h_e4,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_h_e5,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_h_s,    mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_s_bf16, mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_s_h,    mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_bf16_s, mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e4_h,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e4_s,   mmext_check_cvt_f8f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e5_h,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e5_s,   mmext_check_cvt_f8f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_h_e4,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_h_e5,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_h_s,    mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_s_bf16, mmext_check_cvt_f32f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_s_h,    mmext_check_cvt_f32f16)

GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e4_bf16,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e4_bf16,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e5_bf16,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e5_bf16,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_bf16_e4,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_bf16_e4,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_bf16_e5,   mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_bf16_e5,   mmext_check_cvt_f8f16)

/* Not implemented on zhijiang */
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_d_s,    mmext_check_cvt_f64f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_s_d,    mmext_check_cvt_f64f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_d_s,    mmext_check_cvt_f64f32)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_s_d,    mmext_check_cvt_f64f32)

GEN_MMEXT_OPMM_MV_TRANS(mfscvt_w_s,  mmext_check_cvt_f32i32)
GEN_MMEXT_OPMM_MV_TRANS(mfscvth_b_h, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mfscvtl_b_h, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mfucvt_w_s,  mmext_check_cvt_f32i32)
GEN_MMEXT_OPMM_MV_TRANS(mfucvth_b_h, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mfucvtl_b_h, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(msfcvt_s_w,  mmext_check_cvt_f32i32)
GEN_MMEXT_OPMM_MV_TRANS(msfcvth_h_b, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(msfcvtl_h_b, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mufcvt_s_w,  mmext_check_cvt_f32i32)
GEN_MMEXT_OPMM_MV_TRANS(mufcvth_h_b, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mufcvtl_h_b, mmext_check_cvt_f16i8)

GEN_MMEXT_OPMM_MV_TRANS(mucvth_b_p, mmext_check_cvt_i4i8)
GEN_MMEXT_OPMM_MV_TRANS(mucvtl_b_p, mmext_check_cvt_i4i8)
GEN_MMEXT_OPMM_MV_TRANS(mscvth_b_p, mmext_check_cvt_i4i8)
GEN_MMEXT_OPMM_MV_TRANS(mscvtl_b_p, mmext_check_cvt_i4i8)

GEN_MMEXT_OPMM_MV_TRANS(mfscvth_b_bf16, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mfscvtl_b_bf16, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mfucvth_b_bf16, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mfucvtl_b_bf16, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(msfcvth_bf16_b, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(msfcvtl_bf16_b, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mufcvth_bf16_b, mmext_check_cvt_f16i8)
GEN_MMEXT_OPMM_MV_TRANS(mufcvtl_bf16_b, mmext_check_cvt_f16i8)

/* load/store translation */
/* Check for load/store with e8/b element size */
static bool mmext_check_ldst_e8(DisasContext *s)
{
    return require_matrix(s) && (s->ke0 || !s->mill) && (s->me0 || !s->kill);
}

/* Check for load/store with e16/h element size */
static bool mmext_check_ldst_e16(DisasContext *s)
{
    return require_matrix(s) && (s->ke0 || !s->mill) &&
           (s->me0 || (!s->kill && !s->km2));
}

/* Check for load/store with e32/w element size */
static bool mmext_check_ldst_e32(DisasContext *s)
{
    return require_matrix(s) && (s->ke0 || !s->mill) &&
           (s->me0 || (!s->kill && !s->km4));
}

/* Check for load/store with e64/d element size */
static bool mmext_check_ldst_e64(DisasContext *s)
{
    return require_matrix(s) && (s->ke0 || !s->mill) &&
           (s->me0 || (!s->kill && !s->km8));
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

GEN_MMEXT_LD_TRANS(mld_b, mmext_check_ldst_e8)
GEN_MMEXT_LD_TRANS(mld_h, mmext_check_ldst_e16)
GEN_MMEXT_LD_TRANS(mld_w, mmext_check_ldst_e32)
GEN_MMEXT_LD_TRANS(mld_d, mmext_check_ldst_e64)

GEN_MMEXT_LD_TRANS(mldt_e8, mmext_check_ldst_e8)
GEN_MMEXT_LD_TRANS(mldt_e16, mmext_check_ldst_e16)
GEN_MMEXT_LD_TRANS(mldt_e32, mmext_check_ldst_e32)
GEN_MMEXT_LD_TRANS(mldt_e64, mmext_check_ldst_e64)

GEN_MMEXT_LD_TRANS(msld_b, mmext_check_ldst_e8)
GEN_MMEXT_LD_TRANS(msld_h, mmext_check_ldst_e16)
GEN_MMEXT_LD_TRANS(msld_w, mmext_check_ldst_e32)
GEN_MMEXT_LD_TRANS(msld_d, mmext_check_ldst_e64)

GEN_MMEXT_LD_TRANS(msldt_e8, mmext_check_ldst_e8)
GEN_MMEXT_LD_TRANS(msldt_e16, mmext_check_ldst_e16)
GEN_MMEXT_LD_TRANS(msldt_e32, mmext_check_ldst_e32)
GEN_MMEXT_LD_TRANS(msldt_e64, mmext_check_ldst_e64)

#define GEN_MMEXT_PREFETCH_LD_TRANS(NAME, CHECK)            \
static bool trans_##NAME(DisasContext *s, arg_r2_s *a)     \
{                                                           \
    return CHECK(s);                                        \
}

GEN_MMEXT_PREFETCH_LD_TRANS(mpld_e8, mmext_check_ldst_e8)
GEN_MMEXT_PREFETCH_LD_TRANS(mpld_e16, mmext_check_ldst_e16)
GEN_MMEXT_PREFETCH_LD_TRANS(mpld_e32, mmext_check_ldst_e32)
GEN_MMEXT_PREFETCH_LD_TRANS(mpld_e64, mmext_check_ldst_e64)
GEN_MMEXT_PREFETCH_LD_TRANS(mpldt_e8, mmext_check_ldst_e8)
GEN_MMEXT_PREFETCH_LD_TRANS(mpldt_e16, mmext_check_ldst_e16)
GEN_MMEXT_PREFETCH_LD_TRANS(mpldt_e32, mmext_check_ldst_e32)
GEN_MMEXT_PREFETCH_LD_TRANS(mpldt_e64, mmext_check_ldst_e64)

GEN_MMEXT_ST_TRANS(mst_b, mmext_check_ldst_e8)
GEN_MMEXT_ST_TRANS(mst_h, mmext_check_ldst_e16)
GEN_MMEXT_ST_TRANS(mst_w, mmext_check_ldst_e32)
GEN_MMEXT_ST_TRANS(mst_d, mmext_check_ldst_e64)

GEN_MMEXT_ST_TRANS(msst_b, mmext_check_ldst_e8)
GEN_MMEXT_ST_TRANS(msst_h, mmext_check_ldst_e16)
GEN_MMEXT_ST_TRANS(msst_w, mmext_check_ldst_e32)
GEN_MMEXT_ST_TRANS(msst_d, mmext_check_ldst_e64)

GEN_MMEXT_ST_TRANS(mstt_e8, mmext_check_ldst_e8)
GEN_MMEXT_ST_TRANS(mstt_e16, mmext_check_ldst_e16)
GEN_MMEXT_ST_TRANS(mstt_e32, mmext_check_ldst_e32)
GEN_MMEXT_ST_TRANS(mstt_e64, mmext_check_ldst_e64)

GEN_MMEXT_ST_TRANS(msstt_e8, mmext_check_ldst_e8)
GEN_MMEXT_ST_TRANS(msstt_e16, mmext_check_ldst_e16)
GEN_MMEXT_ST_TRANS(msstt_e32, mmext_check_ldst_e32)
GEN_MMEXT_ST_TRANS(msstt_e64, mmext_check_ldst_e64)
/* whole matrix load/store translation */
static bool mmext_check_ldm(DisasContext *s, arg_mm_ldm *a, uint8_t nf)
{
    /* FIXME */
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
    if (a->md & a->uimm3) {
        return false;
    }

    if (len <= (8 << SIMD_MAXSZ_BITS)) {
        tcg_gen_gvec_dup_imm(MO_64, mregs_ofs(s, a->md), len, len, 0);
    } else {
        uint32_t maxsz = 0;
        int i = 0;
        for (; i < len / (8 << SIMD_MAXSZ_BITS); i++) {
            maxsz = 8 << SIMD_MAXSZ_BITS;
            tcg_gen_gvec_dup_imm(MO_64, mregs_ofs(s, a->md) +
                                 (i << (3 + SIMD_MAXSZ_BITS)),
                                 maxsz, maxsz, 0);
        }
        maxsz = len % (8 << SIMD_MAXSZ_BITS);
        if (maxsz) {
            tcg_gen_gvec_dup_imm(MO_64, mregs_ofs(s, a->md) +
                                 (i << (3 + SIMD_MAXSZ_BITS)),
                                 maxsz, maxsz, 0);
        }
    }
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

/* v0.5 new instructions */
static bool mmext_check_mfmacc_h_e2m1(DisasContext *s, arg_mm_rmm * a)
{
    return (a->ms2 % 2 == 0) && s->f4f16 &&
           require_matrix(s) && mmext_check_mnpk(s) && (a->md != a->ms1) &&
           (a->md != a->ms2) && (a->md != a->ms2 + 1);
}

static bool trans_mfmacc_h_e2m1(DisasContext *s, arg_mm_rmm * a)
{
    if (mmext_check_mfmacc_h_e2m1(s, a)) {
        TCGv_ptr dest, src1, src2;

        dest = tcg_temp_new_ptr();
        src1 = tcg_temp_new_ptr();
        src2 = tcg_temp_new_ptr();

        tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, a->md));
        tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, a->ms1));
        tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, a->ms2));

        gen_helper_mfmacc_h_e2m1(dest, src1, src2, tcg_env);

        mark_ms_dirty(s);
        return true;
    }
    return false;
}

static bool mmext_check_mfmacc_bf16_e2m1(DisasContext *s, arg_mm_rmm * a)
{
    return (a->ms2 % 2 == 0) && s->f4bf16 &&
           require_matrix(s) && mmext_check_mnpk(s) && (a->md != a->ms1) &&
           (a->md != a->ms2) && (a->md != a->ms2 + 1);
}

static bool trans_mfmacc_bf16_e2m1(DisasContext *s, arg_mm_rmm * a)
{
    if (mmext_check_mfmacc_bf16_e2m1(s, a)) {
        TCGv_ptr dest, src1, src2;

        dest = tcg_temp_new_ptr();
        src1 = tcg_temp_new_ptr();
        src2 = tcg_temp_new_ptr();

        tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, a->md));
        tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, a->ms1));
        tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, a->ms2));

        gen_helper_mfmacc_bf16_e2m1(dest, src1, src2, tcg_env);

        mark_ms_dirty(s);
        return true;
    }
    return false;
}

static bool mmext_check_mfmacc_s_e2m1(DisasContext *s, arg_mm_rmm * a)
{
    return s->f4f32 && require_matrix(s) &&
           mmext_check_mnk(s) && (a->md != a->ms1) && (a->md != a->ms2);
}

static bool trans_mfmacc_s_e2m1(DisasContext *s, arg_mm_rmm * a)
{
    if (mmext_check_mfmacc_s_e2m1(s, a)) {
        TCGv_ptr dest, src1, src2;

        dest = tcg_temp_new_ptr();
        src1 = tcg_temp_new_ptr();
        src2 = tcg_temp_new_ptr();

        tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, a->md));
        tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, a->ms1));
        tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, a->ms2));

        gen_helper_mfmacc_s_e2m1(dest, src1, src2, tcg_env);

        mark_ms_dirty(s);
        return true;
    }
    return false;
}

static inline bool mmext_check_cvt_f8f4(DisasContext *s)
{
    return s->pwfp && s->mew4b && s->mew8b && require_matrix(s);
}

static inline bool mmext_check_cvt_bf16f4(DisasContext *s)
{
    return s->pwfp && s->mew4b && s->mew16b && require_matrix(s);
}

static inline bool mmext_check_cvt_f32f8(DisasContext *s)
{
    return s->pwfp && s->mew8b && s->mew32b && require_matrix(s);
}

GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e2m1_e5m2,    mmext_check_cvt_f8f4)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e2m1_e4m3,    mmext_check_cvt_f8f4)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e2m1_e5m2,    mmext_check_cvt_f8f4)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e2m1_e4m3,    mmext_check_cvt_f8f4)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e5m2_e2m1,    mmext_check_cvt_f8f4)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e4m3_e2m1,    mmext_check_cvt_f8f4)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e5m2_e2m1,    mmext_check_cvt_f8f4)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e4m3_e2m1,    mmext_check_cvt_f8f4)

GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e2m1_bf16,    mmext_check_cvt_bf16f4)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e2m1_bf16,    mmext_check_cvt_bf16f4)

GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e8m0_bf16,    mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e8m0_bf16,    mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvth_bf16_e8m0,    mmext_check_cvt_f8f16)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_bf16_e8m0,    mmext_check_cvt_f8f16)

GEN_MMEXT_OPMM_MV_TRANS(mfcvth_e8m0_s,    mmext_check_cvt_f32f8)
GEN_MMEXT_OPMM_MV_TRANS(mfcvtl_e8m0_s,    mmext_check_cvt_f32f8)

static inline bool mmext_check_mfmacc_s_mxe5m2(DisasContext *s, arg_mx_rmm *a)
{
    return s->mxf8 && require_matrix(s) &&
           mmext_check_mnk(s) && (a->md != a->ms1) &&
           (a->md != a->ms2) && (a->md != a->ms1_s) && (a->md != a->ms2_s);
}

static inline bool mmext_check_mfmacc_s_mxe2m1(DisasContext *s, arg_mx_rmm *a)
{
    return s->mxf4 && require_matrix(s) &&
           mmext_check_mnk(s) && (a->md != a->ms1) &&
           (a->md != a->ms2) && (a->md != a->ms1_s) && (a->md != a->ms2_s);
}

#define GEN_MMEXT_MX_TRANS(NAME, CHECK)                           \
static bool trans_##NAME(DisasContext *s, arg_mx_rmm *a)          \
{                                                                 \
    if (CHECK(s, a)) {                                            \
        TCGv_ptr dest, src1, src2, s1, s2;                        \
                                                                  \
        dest = tcg_temp_new_ptr();                                \
        src1 = tcg_temp_new_ptr();                                \
        src2 = tcg_temp_new_ptr();                                \
        s1 = tcg_temp_new_ptr();                                  \
        s2 = tcg_temp_new_ptr();                                  \
                                                                  \
        tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, a->md));     \
        tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, a->ms1));    \
        tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, a->ms2));    \
        tcg_gen_addi_ptr(s1, tcg_env, mregs_ofs(s, a->ms1_s));    \
        tcg_gen_addi_ptr(s2, tcg_env, mregs_ofs(s, a->ms2_s));    \
                                                                  \
        gen_helper_##NAME(dest, src1, src2, s1, s2, tcg_env);     \
                                                                  \
        mark_ms_dirty(s);                                         \
        return true;                                              \
    }                                                             \
    return false;                                                 \
}

GEN_MMEXT_MX_TRANS(mfmacc_s_mxe5m2, mmext_check_mfmacc_s_mxe5m2)
GEN_MMEXT_MX_TRANS(mfmacc_s_mxe4m3, mmext_check_mfmacc_s_mxe5m2)
GEN_MMEXT_MX_TRANS(mfmacc_s_mxe2m1, mmext_check_mfmacc_s_mxe2m1)
GEN_MMEXT_MX_TRANS(mfmacc_s_mxe2m1_ue4m3, mmext_check_mfmacc_s_mxe2m1)

static inline bool
mmext_check_mfmacc_s_mxe5m2e2m1(DisasContext *s, arg_mx_rmm *a)
{
    return s->mxf8f4 && mmext_check_mnk(s) && require_matrix(s) &&
           (a->md != a->ms1) && (a->md != a->ms2) && (a->md != a->ms1_s) &&
           (a->md != a->ms2_s);
}

GEN_MMEXT_MX_TRANS(mfmacc_s_mxe5m2e2m1_l, mmext_check_mfmacc_s_mxe5m2e2m1)
GEN_MMEXT_MX_TRANS(mfmacc_s_mxe5m2e2m1_h, mmext_check_mfmacc_s_mxe5m2e2m1)
GEN_MMEXT_MX_TRANS(mfmacc_s_mxe4m3e2m1_l, mmext_check_mfmacc_s_mxe5m2e2m1)
GEN_MMEXT_MX_TRANS(mfmacc_s_mxe4m3e2m1_h, mmext_check_mfmacc_s_mxe5m2e2m1)

GEN_MMEXT_OPRMM_TRANS(mfmacc_h_e4m3e2m1,  mmext_check_fmmacc_hhb)
GEN_MMEXT_OPRMM_TRANS(mfmacc_h_e5m2e2m1,  mmext_check_fmmacc_hhb)

GEN_MMEXT_FMMACC_MPI_TRANS(mfmacc_s_e4m3e2m1, mmext_check_fmmacc_sh_mpi, 1)
GEN_MMEXT_FMMACC_MPI_TRANS(mfmacc_s_e5m2e2m1, mmext_check_fmmacc_sh_mpi, 1)

static inline bool mmext_check_cvt_f8i4(DisasContext *s)
{
    return s->fp_int_cvt && s->mew4b && s->mew8b && require_matrix(s);
}

GEN_MMEXT_OPMM_MV_TRANS(msfcvtl_e4m3_p, mmext_check_cvt_f8i4)
GEN_MMEXT_OPMM_MV_TRANS(msfcvtl_e5m2_p, mmext_check_cvt_f8i4)
GEN_MMEXT_OPMM_MV_TRANS(msfcvth_e4m3_p, mmext_check_cvt_f8i4)
GEN_MMEXT_OPMM_MV_TRANS(msfcvth_e5m2_p, mmext_check_cvt_f8i4)
GEN_MMEXT_OPMM_MV_TRANS(mufcvtl_e4m3_p, mmext_check_cvt_f8i4)
GEN_MMEXT_OPMM_MV_TRANS(mufcvtl_e5m2_p, mmext_check_cvt_f8i4)
GEN_MMEXT_OPMM_MV_TRANS(mufcvth_e4m3_p, mmext_check_cvt_f8i4)
GEN_MMEXT_OPMM_MV_TRANS(mufcvth_e5m2_p, mmext_check_cvt_f8i4)


typedef void gen_helper_dequant(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv, TCGv, TCGv_env);
static bool mm_rmm_i_rs1_trans(gen_helper_dequant *fn, DisasContext *s,
                               arg_mm_rmm_i_rs1 *a)
{
    TCGv_ptr dest, src1, src2;
    TCGv rs1, uimm;
    dest = tcg_temp_new_ptr();
    src1 = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();
    rs1 = get_gpr(s, a->rs1, EXT_NONE);
    uimm = tcg_constant_tl(a->uimm);

    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, a->md));
    tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, a->ms1));
    tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, a->ms2));

    fn(dest, src1, src2, rs1, uimm, tcg_env);

    mark_ms_dirty(s);
    return true;
}

#define GEN_MMEXT_RMM_I_RS1_TRANS(NAME, CHECK)                 \
static bool trans_##NAME(DisasContext *s, arg_mm_rmm_i_rs1 *a) \
{                                                              \
    if (CHECK(s)) {                                            \
        return mm_rmm_i_rs1_trans(gen_helper_##NAME, s, a);    \
    }                                                          \
    return false;                                              \
}

GEN_MMEXT_RMM_I_RS1_TRANS(mfdequantu_h_hp_zp, mmext_check_pwf16_mv)
GEN_MMEXT_RMM_I_RS1_TRANS(mfdequant_h_hp, mmext_check_pwf16_mv)
GEN_MMEXT_RMM_I_RS1_TRANS(mfdequantu_bf16_bf16p_zp, mmext_check_pwf16_mv)
GEN_MMEXT_RMM_I_RS1_TRANS(mfdequant_bf16_bf16p, mmext_check_pwf16_mv)
GEN_MMEXT_RMM_I_RS1_TRANS(mfdequantu_h_hb_zp, mmext_check_pwf16_mv)
GEN_MMEXT_RMM_I_RS1_TRANS(mfdequant_h_hb, mmext_check_pwf16_mv)
GEN_MMEXT_RMM_I_RS1_TRANS(mfdequanth_h_hb, mmext_check_pwf16_mv)
GEN_MMEXT_RMM_I_RS1_TRANS(mfdequantu_bf16_bf16b_zp, mmext_check_pwf16_mv)
GEN_MMEXT_RMM_I_RS1_TRANS(mfdequant_bf16_bf16b, mmext_check_pwf16_mv)
GEN_MMEXT_RMM_I_RS1_TRANS(mfdequanth_bf16_bf16b, mmext_check_pwf16_mv)


typedef void gen_helper_opimmr(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv, TCGv_env);

static bool mm_opi_mmr_trans(uint32_t md, uint32_t ms1, uint32_t ms2,
                         target_ulong s1, gen_helper_opimmr *fn,
                         DisasContext *s)
{
    TCGv_ptr dest, src1, src2;
    TCGv s1_v;

    dest = tcg_temp_new_ptr();
    src1 = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();

    s1_v = get_gpr(s, s1, EXT_NONE);

    tcg_gen_addi_ptr(dest, tcg_env, mregs_ofs(s, md));
    tcg_gen_addi_ptr(src1, tcg_env, mregs_ofs(s, ms1));
    tcg_gen_addi_ptr(src2, tcg_env, mregs_ofs(s, ms2));

    fn(dest, src1, src2, s1_v, tcg_env);

    mark_ms_dirty(s);
    return true;
}

#define GEN_MMEXT_OPI_MMR_TRANS(NAME, FNAME, CHECK)                         \
static bool trans_##NAME(DisasContext *s, arg_mm_r *a)                      \
{                                                                           \
    if (CHECK(s)){                                                          \
        return mm_opi_mmr_trans(a->md, a->ms1, a->ms2, a->rs1,              \
                                gen_helper_##FNAME, s);                     \
    }                                                                       \
    return false;                                                           \
}

GEN_MMEXT_OPI_MMR_TRANS(madd_w_mv,  madd_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(msrl_w_mv,  msrl_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(msll_w_mv,  msll_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mmax_w_mv,  mmax_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mumax_w_mv,  mumax_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mmin_w_mv,  mmin_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mumin_w_mv,  mumin_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(msub_w_mv,  msub_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(msra_w_mv,  msra_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mmul_w_mv,    mmul_w_mv_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mmulh_w_mv,   mmulh_w_mv_i, mmext_check_pwi32_mv)

GEN_MMEXT_OPI_MMR_TRANS(madd_w_mc,  madd_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(msrl_w_mc,  msrl_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(msll_w_mc,  msll_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mmax_w_mc,  mmax_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mumax_w_mc,  mumax_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mmin_w_mc,  mmin_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mumin_w_mc,  mumin_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(msub_w_mc,  msub_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(msra_w_mc,  msra_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mmul_w_mc,    mmul_w_mc_i, mmext_check_pwi32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mmulh_w_mc,   mmulh_w_mc_i, mmext_check_pwi32_mv)

GEN_MMEXT_OPI_MMR_TRANS(mfadd_h_mv, mfadd_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfadd_s_mv, mfadd_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfadd_d_mv, mfadd_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmax_h_mv, mfmax_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmax_s_mv, mfmax_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmax_d_mv, mfmax_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmin_h_mv, mfmin_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmin_s_mv, mfmin_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmin_d_mv, mfmin_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmul_h_mv, mfmul_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmul_s_mv, mfmul_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmul_d_mv, mfmul_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfsub_h_mv, mfsub_h_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfsub_s_mv, mfsub_s_mv_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfsub_d_mv, mfsub_d_mv_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfma_h_mv, mfma_h_mv_i,  mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfma_s_mv, mfma_s_mv_i,  mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfma_d_mv, mfma_d_mv_i,  mmext_check_pwf64_mv)

GEN_MMEXT_OPI_MMR_TRANS(mfadd_h_mc, mfadd_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfadd_s_mc, mfadd_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfadd_d_mc, mfadd_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmax_h_mc, mfmax_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmax_s_mc, mfmax_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmax_d_mc, mfmax_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmin_h_mc, mfmin_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmin_s_mc, mfmin_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmin_d_mc, mfmin_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmul_h_mc, mfmul_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmul_s_mc, mfmul_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmul_d_mc, mfmul_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfsub_h_mc, mfsub_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfsub_s_mc, mfsub_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfsub_d_mc, mfsub_d_mc_i, mmext_check_pwf64_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfma_h_mc, mfma_h_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfma_s_mc, mfma_s_mc_i, mmext_check_pwf32_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfma_d_mc, mfma_d_mc_i, mmext_check_pwf64_mv)


GEN_MMEXT_OPI_MMR_TRANS(mfadd_bf16_mv, mfadd_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmax_bf16_mv, mfmax_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmin_bf16_mv, mfmin_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmul_bf16_mv, mfmul_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfsub_bf16_mv, mfsub_bf16_mv_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfma_bf16_mv, mfma_bf16_mv_i,  mmext_check_pwf16_mv)

GEN_MMEXT_OPI_MMR_TRANS(mfadd_bf16_mc, mfadd_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmax_bf16_mc, mfmax_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmin_bf16_mc, mfmin_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfmul_bf16_mc, mfmul_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfsub_bf16_mc, mfsub_bf16_mc_i, mmext_check_pwf16_mv)
GEN_MMEXT_OPI_MMR_TRANS(mfma_bf16_mc, mfma_bf16_mc_i,  mmext_check_pwf16_mv)

GEN_MMEXT_OPMM_MV_TRANS(mfabs_h_mm,      mmext_check_pwf16_mv)
GEN_MMEXT_OPMM_MV_TRANS(mfabs_bf16_mm,   mmext_check_pwf16_mv)
GEN_MMEXT_OPMM_MV_TRANS(mfabs_s_mm,      mmext_check_pwf32_mv)
GEN_MMEXT_OPMM_MV_TRANS(mfabs_d_mm,      mmext_check_pwf64_mv)
GEN_MMEXT_OPMM_MV_TRANS(mffloor_h_mm,      mmext_check_pwf16_mv)
GEN_MMEXT_OPMM_MV_TRANS(mffloor_bf16_mm,   mmext_check_pwf16_mv)
GEN_MMEXT_OPMM_MV_TRANS(mffloor_s_mm,      mmext_check_pwf32_mv)
GEN_MMEXT_OPMM_MV_TRANS(mffloor_d_mm,      mmext_check_pwf64_mv)
GEN_MMEXT_OPMM_MV_TRANS(mfceil_h_mm,      mmext_check_pwf16_mv)
GEN_MMEXT_OPMM_MV_TRANS(mfceil_bf16_mm,   mmext_check_pwf16_mv)
GEN_MMEXT_OPMM_MV_TRANS(mfceil_s_mm,      mmext_check_pwf32_mv)
GEN_MMEXT_OPMM_MV_TRANS(mfceil_d_mm,      mmext_check_pwf64_mv)

GEN_MMEXT_OPRMM_TRANS(madd_w_mx,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(msub_w_mx,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(msra_w_mx,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mmul_w_mx,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mmulh_w_mx, mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mmax_w_mx,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mmin_w_mx,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mumax_w_mx, mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(mumin_w_mx, mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(msll_w_mx,  mmext_check_pwi32_mm)
GEN_MMEXT_OPRMM_TRANS(msrl_w_mx,  mmext_check_pwi32_mm)

GEN_MMEXT_OPRMM_TRANS(mfadd_h_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmax_h_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmin_h_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmul_h_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfsub_h_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfadd_s_mf, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfmax_s_mf, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfmin_s_mf, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfmul_s_mf, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfsub_s_mf, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfadd_d_mf, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfmax_d_mf, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfmin_d_mf, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfmul_d_mf, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfsub_d_mf, mmext_check_pwf64_mm)
GEN_MMEXT_OPRMM_TRANS(mfma_h_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfma_s_mf, mmext_check_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfma_d_mf, mmext_check_pwf64_mm)

GEN_MMEXT_OPRMM_TRANS(mfadd_bf16_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmax_bf16_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmin_bf16_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfmul_bf16_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfsub_bf16_mf, mmext_check_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfma_bf16_mf, mmext_check_pwf16_mm)

/* Reduction Extension */

static bool mmext_check_red_pwf16_mm(DisasContext *s, arg_mm_rmm *a)
{
    return s->mred && s->mew16b && require_matrix(s) && mmext_check_mk(s) &&
           !s->km2;
}

static bool mmext_check_red_pwf32_mm(DisasContext *s, arg_mm_rmm *a)
{
    return s->mred && s->mew32b && require_matrix(s) &&
           mmext_check_mk(s) && !s->km4;
}

static inline bool mmext_check_mfdup16(DisasContext *s)
{
    return s->mred && s->mew16b;
}

static inline bool mmext_check_mfdup32(DisasContext *s)
{
    return s->mred && s->mew32b;
}

static inline bool mmext_check_mfdup64(DisasContext *s)
{
    return s->mred && s->mew64b;
}

GEN_MMEXT_OPRMM_TRANS(mfredsum_h_dup_mf        , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredsum_h_c_mf          , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredsum_h_m_mf          , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredsum_bf16_dup_mf     , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredsum_bf16_c_mf       , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredsum_bf16_m_mf       , mmext_check_red_pwf16_mm)

GEN_MMEXT_OPRMM_TRANS(mfredsum_s_dup_mf        , mmext_check_red_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfredsum_s_c_mf          , mmext_check_red_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfredsum_s_m_mf          , mmext_check_red_pwf32_mm)

GEN_MMEXT_OPRMM_TRANS(mfredmax_h_dup_mf        , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_h_c_mf          , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_h_m_mf          , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_h_dup_abs_mf    , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_h_c_abs_mf      , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_h_m_abs_mf      , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_bf16_dup_mf     , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_bf16_c_mf       , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_bf16_m_mf       , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_bf16_dup_abs_mf , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_bf16_c_abs_mf   , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_bf16_m_abs_mf   , mmext_check_red_pwf16_mm)

GEN_MMEXT_OPRMM_TRANS(mfredmax_s_dup_mf        , mmext_check_red_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_s_c_mf          , mmext_check_red_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_s_m_mf          , mmext_check_red_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_s_dup_abs_mf    , mmext_check_red_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_s_c_abs_mf      , mmext_check_red_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmax_s_m_abs_mf      , mmext_check_red_pwf32_mm)

GEN_MMEXT_OPRMM_TRANS(mfredmin_h_dup_mf        , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmin_h_c_mf          , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmin_h_m_mf          , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmin_bf16_dup_mf     , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmin_bf16_c_mf       , mmext_check_red_pwf16_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmin_bf16_m_mf       , mmext_check_red_pwf16_mm)

GEN_MMEXT_OPRMM_TRANS(mfredmin_s_dup_mf        , mmext_check_red_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmin_s_c_mf          , mmext_check_red_pwf32_mm)
GEN_MMEXT_OPRMM_TRANS(mfredmin_s_m_mf          , mmext_check_red_pwf32_mm)

GEN_MMEXT_OPMM_MV_TRANS(mfdup_e16              , mmext_check_mfdup16)
GEN_MMEXT_OPMM_MV_TRANS(mfdup_e32              , mmext_check_mfdup32)
GEN_MMEXT_OPMM_MV_TRANS(mfdup_e64              , mmext_check_mfdup64)

/* Special Function Instructions */
static inline bool mmext_check_sf_mv_bf16(DisasContext *s)
{
    return s->msf && mmext_check_mk(s) && require_matrix(s) && !s->km2;
}

static inline bool mmext_check_sf_mv_s(DisasContext *s)
{
    return s->msf && mmext_check_mk(s) && require_matrix(s) && !s->km4;
}

GEN_MMEXT_OPMM_MV_TRANS(mflog2_bf16 , mmext_check_sf_mv_bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfsqrt_bf16 , mmext_check_sf_mv_bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfcos_bf16  , mmext_check_sf_mv_bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfsin_bf16  , mmext_check_sf_mv_bf16)
GEN_MMEXT_OPMM_MV_TRANS(mftanh_bf16 , mmext_check_sf_mv_bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfsig_bf16  , mmext_check_sf_mv_bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfrec_bf16  , mmext_check_sf_mv_bf16)
GEN_MMEXT_OPMM_MV_TRANS(mfexp2_bf16 , mmext_check_sf_mv_bf16)
GEN_MMEXT_OPMM_MV_TRANS(mflog2_s    , mmext_check_sf_mv_s)
GEN_MMEXT_OPMM_MV_TRANS(mfsqrt_s    , mmext_check_sf_mv_s)
GEN_MMEXT_OPMM_MV_TRANS(mfcos_s     , mmext_check_sf_mv_s)
GEN_MMEXT_OPMM_MV_TRANS(mfsin_s     , mmext_check_sf_mv_s)
GEN_MMEXT_OPMM_MV_TRANS(mftanh_s    , mmext_check_sf_mv_s)
GEN_MMEXT_OPMM_MV_TRANS(mfsig_s     , mmext_check_sf_mv_s)
GEN_MMEXT_OPMM_MV_TRANS(mfrec_s     , mmext_check_sf_mv_s)
GEN_MMEXT_OPMM_MV_TRANS(mfexp2_s    , mmext_check_sf_mv_s)

/*
 * We define two different versions of the macro based on whether
 * CONFIG_USER_ONLY is defined. This moves the preprocessor logic
 * outside the macro body, which is the correct approach.
 */
#ifndef CONFIG_USER_ONLY
#define GEN_MMEXT_DMA_TRANS(NAME)                                  \
static bool trans_##NAME(DisasContext *s, arg_mm_cfg *a)           \
{                                                                  \
    if (s->tpe_tcm && require_matrix(s) && s->mdma) {              \
        TCGv src1 = get_gpr(s, a->rs1, EXT_NONE);                  \
        gen_helper_##NAME(src1, tcg_env);                          \
        mark_ms_dirty(s);                                          \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}

#else
#define GEN_MMEXT_DMA_TRANS(NAME)                                  \
static bool trans_##NAME(DisasContext *s, arg_mm_cfg *a)           \
{                                                                  \
    return false;                                                  \
}
#endif

GEN_MMEXT_DMA_TRANS(th_dma_push_mem_r)
GEN_MMEXT_DMA_TRANS(th_dma_push_mem_w)
GEN_MMEXT_DMA_TRANS(th_dma_push_sram_pa)
GEN_MMEXT_DMA_TRANS(th_dma_push_desc_pa)
GEN_MMEXT_DMA_TRANS(th_dma_push_coord)
GEN_MMEXT_DMA_TRANS(th_dma_copy_desc)
GEN_MMEXT_DMA_TRANS(th_dma_copy_tcm_mem)
GEN_MMEXT_DMA_TRANS(th_dma_copy_mem_tcm)

static bool trans_th_fence_tcm_r(DisasContext *s, arg_th_fence_tcm_r *a)
{
    if (s->tpe_tcm && require_matrix(s) && s->mdma) {
        tcg_gen_mb(TCG_MO_LD_LD | TCG_MO_LD_ST | TCG_BAR_STRL);
        mark_ms_dirty(s);
        return true;
    } else {
        return false;
    }
}

static bool trans_th_fence_tcm_w(DisasContext *s, arg_th_fence_tcm_w *a)
{
    if (s->tpe_tcm && require_matrix(s) && s->mdma) {
        tcg_gen_mb(TCG_MO_ST_LD | TCG_MO_ST_ST | TCG_BAR_STRL);
        mark_ms_dirty(s);
        return true;
    } else {
        return false;
    }
}

static bool trans_th_fence_tcm_rw(DisasContext *s, arg_th_fence_tcm_rw *a)
{
    if (s->tpe_tcm && require_matrix(s) && s->mdma) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
        mark_ms_dirty(s);
        return true;
    } else {
        return false;
    }
}

static bool mmext_check_dma_ld(DisasContext *s)
{
    return require_matrix(s) && (s->ke0 || !s->mill);
}

GEN_MMEXT_LD_TRANS(th_mldtu6_tcm, mmext_check_dma_ld)
GEN_MMEXT_LD_TRANS(th_mldts6_tcm, mmext_check_dma_ld)
