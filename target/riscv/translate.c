/*
 * RISC-V emulation for qemu: main translation routines.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
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
#include "qemu/log.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "disas/disas.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"
#include "exec/tracestub.h"

#include "instmap.h"
#include "internals.h"

/* global register indices */
static TCGv cpu_gpr[32], cpu_pc, cpu_vl, cpu_vstart;
static TCGv sizem, sizen, sizek;
static TCGv_i64 cpu_fpr[32]; /* assume F and D extensions */
static TCGv load_res;
static TCGv load_val;

#include "exec/gen-icount.h"

/**
 * riscv_tbflags_from_tb:
 * @tb: the TranslationBlock
 *
 * Extract the flag values from @tb.
 */
static inline CPURISCVTBFlags riscv_tbflags_from_tb(const TranslationBlock *tb)
{
    return (CPURISCVTBFlags){ tb->flags, tb->cs_base };
}

/*
 * If an operation is being performed on less than TARGET_LONG_BITS,
 * it may require the inputs to be sign- or zero-extended; which will
 * depend on the exact operation being performed.
 */
typedef enum {
    EXT_NONE,
    EXT_SIGN,
    EXT_ZERO,
} DisasExtend;

typedef struct DisasContext {
    DisasContextBase base;
    /* pc_succ_insn points to the instruction following base.pc_next */
    target_ulong pc_succ_insn;
    target_ulong priv_ver;
    target_ulong vext_ver;
    RISCVMXL xl;
    uint32_t misa_ext;
    uint32_t opcode;
    uint32_t mstatus_fs;
    uint32_t mstatus_vs;
    uint32_t mcsr_ms;
    uint32_t mem_idx;
    /* Remember the rounding mode encoded in the previous fp instruction,
       which we have already installed into env->fp_status.  Or -1 for
       no previous fp instruction.  Note that we exit the TB when writing
       to any system register, which includes CSR_FRM, so we do not have
       to reset this known value.  */
    int frm;
    RISCVMXL ol;
    bool virt_enabled;
    bool ext_ifencei;
    bool ext_psfoperand;
    bool ext_svinval;
    bool hlsx;
    /* vector extension */
    bool vill;
    bool bf16;
    /*
     * Encode LMUL to lmul as follows:
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
    int8_t lmul;
    uint8_t sew;
    bool pwi32;
    bool pwi64;
    bool i4i32;
    bool i8i32;
    bool i16i64;
    bool f16f16;
    bool f32f32;
    bool f64f64;
    bool mill;
    bool nill;
    bool kill;
    bool npill;
    uint16_t vlen;
    uint16_t mlen;
    uint16_t mrowlen;
    target_ulong vstart;
    bool vl_eq_vlmax;
    uint8_t ntemp;
    CPUState *cs;
    TCGv zero;
    /* Space for 3 operands plus 1 extra for address computation. */
    TCGv temp[4];
} DisasContext;

static void csky_dump_tb_map(DisasContextBase *dcbase)
{
    target_ulong tb_pc = dcbase->pc_first;
    target_ulong tb_end = dcbase->pc_next;
    uint64_t icount = dcbase->num_insns;

    qemu_log_mask(CPU_TB_TRACE, "tb_map: 0x" TARGET_FMT_lx " 0x" TARGET_FMT_lx " %" PRIu64 "\n", tb_pc, tb_end, icount);
}

static void csky_trace_tb_start(CPURISCVState *env, const TranslationBlock *tb)
{
    TCGv t0;

    t0 = tcg_const_tl(tb->pc);
    gen_helper_trace_tb_start(cpu_env, t0);
    tcg_temp_free(t0);
}

static void csky_trace_tb_exit(uint32_t subtype, uint32_t offset)
{
    TCGv_i32 t0 = tcg_const_i32(subtype);
    TCGv_i32 t1 = tcg_const_i32(offset);

    gen_helper_trace_tb_exit(t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static TCGOp *jcount_start_insn;

static void gen_csky_jcount_start(DisasContext *dc, CPUState *cpu)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv_i32 t1 = tcg_temp_local_new_i32();
    CPUArchState *env = cpu->env_ptr;

    /* We emit a movi with a dummy immediate argument. Keep the insn index
     * of the movi so that we later (when we know the actual insn count)
     * can update the immediate argument with the actual insn count.  */
    tcg_gen_movi_i32(t1, 0xdeadbeef);
    jcount_start_insn = tcg_last_op();

    tcg_gen_movi_tl(t0, dc->base.pc_first);

    if (env->jcount_start != 0) {
        gen_helper_jcount(cpu_env, t0, t1);
    }
    if (cpu->csky_trace_features & CSKY_TRACE) {
        gen_helper_csky_trace_icount(cpu_env, t0, t1);
    }
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
}

static void gen_csky_jcount_end(int num_insns)
{
    tcg_set_insn_param(jcount_start_insn, 1,
                       tcgv_i32_arg(tcg_constant_i32(num_insns)));
}

static inline bool has_ext(DisasContext *ctx, uint32_t ext)
{
    return ctx->misa_ext & ext;
}

#ifdef TARGET_RISCV32
#define get_xl(ctx)    MXL_RV32
#elif defined(CONFIG_USER_ONLY)
#define get_xl(ctx)    MXL_RV64
#else
#define get_xl(ctx)    ((ctx)->xl)
#endif

/* The word size for this machine mode. */
static inline int __attribute__((unused)) get_xlen(DisasContext *ctx)
{
    return 16 << get_xl(ctx);
}

/* The operation length, as opposed to the xlen. */
#ifdef TARGET_RISCV32
#define get_ol(ctx)    MXL_RV32
#else
#define get_ol(ctx)    ((ctx)->ol)
#endif

static inline int get_olen(DisasContext *ctx)
{
    return 16 << get_ol(ctx);
}

/*
 * RISC-V requires NaN-boxing of narrower width floating point values.
 * This applies when a 32-bit value is assigned to a 64-bit FP register.
 * For consistency and simplicity, we nanbox results even when the RVD
 * extension is not present.
 */
static void gen_nanbox_s(TCGv_i64 out, TCGv_i64 in)
{
    tcg_gen_ori_i64(out, in, MAKE_64BIT_MASK(32, 32));
}

static void gen_nanbox_h(TCGv_i64 out, TCGv_i64 in)
{
    tcg_gen_ori_i64(out, in, MAKE_64BIT_MASK(16, 48));
}

/*
 * A narrow n-bit operation, where n < FLEN, checks that input operands
 * are correctly Nan-boxed, i.e., all upper FLEN - n bits are 1.
 * If so, the least-significant bits of the input are used, otherwise the
 * input value is treated as an n-bit canonical NaN (v2.2 section 9.2).
 *
 * Here, the result is always nan-boxed, even the canonical nan.
 */
static void gen_check_nanbox_h(TCGv_i64 out, TCGv_i64 in)
{
    TCGv_i64 t_max = tcg_const_i64(0xffffffffffff0000ull);
    TCGv_i64 t_nan = tcg_const_i64(0xffffffffffff7e00ull);

    tcg_gen_movcond_i64(TCG_COND_GEU, out, in, t_max, in, t_nan);
    tcg_temp_free_i64(t_max);
    tcg_temp_free_i64(t_nan);
}

static void gen_check_nanbox_bh(TCGv_i64 out, TCGv_i64 in)
{
    TCGv_i64 t_max = tcg_const_i64(0xffffffffffff0000ull);
    TCGv_i64 t_nan = tcg_const_i64(0xffffffffffff7fc0ull);

    tcg_gen_movcond_i64(TCG_COND_GEU, out, in, t_max, in, t_nan);
    tcg_temp_free_i64(t_max);
    tcg_temp_free_i64(t_nan);
}

static void gen_check_nanbox_s(TCGv_i64 out, TCGv_i64 in)
{
    TCGv_i64 t_max = tcg_constant_i64(0xffffffff00000000ull);
    TCGv_i64 t_nan = tcg_constant_i64(0xffffffff7fc00000ull);

    tcg_gen_movcond_i64(TCG_COND_GEU, out, in, t_max, in, t_nan);
}

static void gen_set_pc(DisasContext *ctx, target_ulong dest)
{
    if (get_xl(ctx) == MXL_RV32) {
        dest = (int32_t)dest;
    }
    tcg_gen_movi_tl(cpu_pc, dest);
}

static void generate_exception(DisasContext *ctx, int excp)
{
    gen_set_pc(ctx, ctx->base.pc_next);
    gen_helper_raise_exception(cpu_env, tcg_constant_i32(excp));
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void generate_exception_mtval(DisasContext *ctx, int excp)
{
    gen_set_pc(ctx, ctx->base.pc_next);
    tcg_gen_st_tl(cpu_pc, cpu_env, offsetof(CPURISCVState, badaddr));
    gen_helper_raise_exception(cpu_env, tcg_constant_i32(excp));
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_debug(void)
{
    gen_helper_raise_exception(cpu_env, tcg_constant_i32(EXCP_DEBUG));
}

/* Wrapper around tcg_gen_exit_tb that handles single stepping */
static void exit_tb(DisasContext *ctx)
{
    if (ctx->base.singlestep_enabled) {
        gen_exception_debug();
    } else {
        tcg_gen_exit_tb(NULL, 0);
    }
}

/* Wrapper around tcg_gen_lookup_and_goto_ptr that handles single stepping */
static void lookup_and_goto_ptr(DisasContext *ctx)
{
    if (ctx->base.singlestep_enabled) {
        gen_exception_debug();
    } else {
        tcg_gen_lookup_and_goto_ptr();
    }
}

static void gen_exception_illegal(DisasContext *ctx)
{
    generate_exception(ctx, RISCV_EXCP_ILLEGAL_INST);
}

static void gen_exception_inst_addr_mis(DisasContext *ctx)
{
    generate_exception_mtval(ctx, RISCV_EXCP_INST_ADDR_MIS);
}

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(n);
        gen_set_pc(ctx, dest);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        gen_set_pc(ctx, dest);
        lookup_and_goto_ptr(ctx);
    }
}

/*
 * Wrappers for getting reg values.
 *
 * The $zero register does not have cpu_gpr[0] allocated -- we supply the
 * constant zero as a source, and an uninitialized sink as destination.
 *
 * Further, we may provide an extension for word operations.
 */
static TCGv temp_new(DisasContext *ctx)
{
    assert(ctx->ntemp < ARRAY_SIZE(ctx->temp));
    return ctx->temp[ctx->ntemp++] = tcg_temp_new();
}

static TCGv get_gpr(DisasContext *ctx, int reg_num, DisasExtend ext)
{
    TCGv t;

    if (reg_num == 0) {
        return ctx->zero;
    }

    switch (get_ol(ctx)) {
    case MXL_RV32:
        switch (ext) {
        case EXT_NONE:
            break;
        case EXT_SIGN:
            t = temp_new(ctx);
            tcg_gen_ext32s_tl(t, cpu_gpr[reg_num]);
            return t;
        case EXT_ZERO:
            t = temp_new(ctx);
            tcg_gen_ext32u_tl(t, cpu_gpr[reg_num]);
            return t;
        default:
            g_assert_not_reached();
        }
        break;
    case MXL_RV64:
        break;
    default:
        g_assert_not_reached();
    }
    return cpu_gpr[reg_num];
}

static TCGv dest_gpr(DisasContext *ctx, int reg_num)
{
    if (reg_num == 0 || get_olen(ctx) < TARGET_LONG_BITS) {
        return temp_new(ctx);
    }
    return cpu_gpr[reg_num];
}

static void gen_set_gpr(DisasContext *ctx, int reg_num, TCGv t)
{
    if (reg_num != 0) {
        switch (get_ol(ctx)) {
        case MXL_RV32:
            tcg_gen_ext32s_tl(cpu_gpr[reg_num], t);
            break;
        case MXL_RV64:
            tcg_gen_mov_tl(cpu_gpr[reg_num], t);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

/* Compute a canonical address from a register plus offset. */
static TCGv get_address(DisasContext *ctx, int rs1, int imm)
{
    TCGv addr = temp_new(ctx);
    TCGv src1 = get_gpr(ctx, rs1, EXT_NONE);

    tcg_gen_addi_tl(addr, src1, imm);
    if (get_xl(ctx) == MXL_RV32) {
        tcg_gen_ext32u_tl(addr, addr);
    }
    return addr;
}

static void gen_jal(DisasContext *ctx, int rd, target_ulong imm)
{
    target_ulong next_pc;

    /* check misaligned: */
    next_pc = ctx->base.pc_next + imm;
    if (!has_ext(ctx, RVC)) {
        if ((next_pc & 0x3) != 0) {
            gen_exception_inst_addr_mis(ctx);
            return;
        }
    }
    if (rd != 0) {
        tcg_gen_movi_tl(cpu_gpr[rd], ctx->pc_succ_insn);
    }

    gen_goto_tb(ctx, 0, ctx->base.pc_next + imm); /* must use this for safety */
    ctx->base.is_jmp = DISAS_NORETURN;
}

#ifndef CONFIG_USER_ONLY
/* The states of mstatus_fs are:
 * 0 = disabled, 1 = initial, 2 = clean, 3 = dirty
 * We will have already diagnosed disabled state,
 * and need to turn initial/clean into dirty.
 */
static void mark_fs_dirty(DisasContext *ctx)
{
    TCGv tmp;

    if (ctx->mstatus_fs == MSTATUS_FS) {
        return;
    }
    /* Remember the state change for the rest of the TB.  */
    ctx->mstatus_fs = MSTATUS_FS;

    tmp = tcg_temp_new();

    tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus));
    tcg_gen_ori_tl(tmp, tmp, MSTATUS_FS);
    tcg_gen_st_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus));

    if (ctx->virt_enabled) {
        tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus_hs));
        tcg_gen_ori_tl(tmp, tmp, MSTATUS_FS);
        tcg_gen_st_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus_hs));
    }
    tcg_temp_free(tmp);
}
#else
static inline void mark_fs_dirty(DisasContext *ctx) { }
#endif

#ifndef CONFIG_USER_ONLY
/* The states of mstatus_vs are:
 * 0 = disabled, 1 = initial, 2 = clean, 3 = dirty
 * We will have already diagnosed disabled state,
 * and need to turn initial/clean into dirty.
 */
static void mark_vs_dirty(DisasContext *ctx)
{
    TCGv tmp;

    if (ctx->mstatus_vs == MSTATUS_VS) {
        return;
    }
    /* Remember the state change for the rest of the TB.  */
    ctx->mstatus_vs = MSTATUS_VS;

    tmp = tcg_temp_new();

    tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus));
    tcg_gen_ori_tl(tmp, tmp, MSTATUS_VS);
    tcg_gen_st_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus));

    if (ctx->virt_enabled) {
        tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus_hs));
        tcg_gen_ori_tl(tmp, tmp, MSTATUS_VS);
        tcg_gen_st_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus_hs));
    }
    tcg_temp_free(tmp);
}
#else
static inline void mark_vs_dirty(DisasContext *ctx) { }
#endif

static void gen_set_rm(DisasContext *ctx, int rm)
{
    if (ctx->frm == rm) {
        return;
    }
    ctx->frm = rm;
    if (rm == RISCV_FRM_ROD) {
        gen_helper_set_rod_rounding_mode(cpu_env);
        return;
    }
    gen_helper_set_rounding_mode(cpu_env, tcg_constant_i32(rm));
}

static int ex_plus_1(DisasContext *ctx, int nf)
{
    return nf + 1;
}

static int ex_plus_8(DisasContext *ctx, int rs)
{
    return rs + 8;
}

#define EX_SH(amount) \
    static int ex_shift_##amount(DisasContext *ctx, int imm) \
    {                                         \
        return imm << amount;                 \
    }
EX_SH(1)
EX_SH(2)
EX_SH(3)
EX_SH(4)
EX_SH(12)

#define REQUIRE_EXT(ctx, ext) do { \
    if (!has_ext(ctx, ext)) {      \
        return false;              \
    }                              \
} while (0)

#define REQUIRE_32BIT(ctx) do {    \
    if (get_xl(ctx) != MXL_RV32) { \
        return false;              \
    }                              \
} while (0)

#define REQUIRE_64BIT(ctx) do {    \
    if (get_xl(ctx) < MXL_RV64) {  \
        return false;              \
    }                              \
} while (0)

static int ex_rvc_register(DisasContext *ctx, int reg)
{
    return 8 + reg;
}

static int ex_rvc_shifti(DisasContext *ctx, int imm)
{
    /* For RV128 a shamt of 0 means a shift by 64. */
    return imm ? imm : 64;
}

/* Include the auto-generated decoder for 32 bit insn */
#include "decode-insn32.c.inc"

static bool gen_arith_imm_fn(DisasContext *ctx, arg_i *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, target_long))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);

    func(dest, src1, a->imm);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_arith_imm_tl(DisasContext *ctx, arg_i *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv src2 = tcg_constant_tl(a->imm);

    func(dest, src1, src2);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_arith(DisasContext *ctx, arg_r *a, DisasExtend ext,
                      void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv src2 = get_gpr(ctx, a->rs2, ext);

    func(dest, src1, src2);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static void gen_load_internal(DisasContext *ctx, int memop, TCGv t1, TCGv t0)
{
    if (gen_mem_trace()) {
        TCGv t2 = tcg_const_tl(ctx->base.pc_next);
        switch (memop) {
        case MO_SB:
            gen_helper_trace_ld8s(cpu_env, t2, t1, t0);
            break;
        case MO_TESW:
            gen_helper_trace_ld16s(cpu_env, t2, t1, t0);
            break;
        case MO_TESL:
            gen_helper_trace_ld32s(cpu_env, t2, t1, t0);
            break;
        case MO_UB:
            gen_helper_trace_ld8u(cpu_env, t2, t1, t0);
            break;
        case MO_TEUW:
            gen_helper_trace_ld16u(cpu_env, t2, t1, t0);
            break;
        case MO_TEUL:
            gen_helper_trace_ld32u(cpu_env, t2, t1, t0);
            break;
        case MO_TEQ:
            gen_helper_trace_ld64u(cpu_env, t2, t1, t0);
            break;
        default:
            break;
        }
        tcg_temp_free(t2);
    }
}

static void gen_store_internal(DisasContext *ctx, int memop, TCGv dat, TCGv t0)
{
    if (gen_mem_trace()) {
        TCGv t1 = tcg_const_tl(ctx->base.pc_next);
        switch (memop) {
        case MO_SB:
        case MO_UB:
            gen_helper_trace_st8(cpu_env, t1, dat, t0);
            break;
        case MO_TESW:
        case MO_TEUW:
            gen_helper_trace_st16(cpu_env, t1, dat, t0);
            break;
        case MO_TESL:
        case MO_TEUL:
            gen_helper_trace_st32(cpu_env, t1, dat, t0);
            break;
        case MO_TEQ:
            gen_helper_trace_st64(cpu_env, t1, dat, t0);
            break;
        default:
            break;
        }
        tcg_temp_free(t1);
    }
}

/* Include the auto-generated decoder for vector v0.7.1 insn */
#include "decode-vector-071.c.inc"

static bool gen_arith_per_ol(DisasContext *ctx, arg_r *a, DisasExtend ext,
                             void (*f_tl)(TCGv, TCGv, TCGv),
                             void (*f_32)(TCGv, TCGv, TCGv))
{
    int olen = get_olen(ctx);

    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            f_tl = f_32;
        } else {
            g_assert_not_reached();
        }
    }
    return gen_arith(ctx, a, ext, f_tl);
}

static bool gen_shift_imm_fn(DisasContext *ctx, arg_shift *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, target_long))
{
    TCGv dest, src1;
    int max_len = get_olen(ctx);

    if (a->shamt >= max_len) {
        return false;
    }

    dest = dest_gpr(ctx, a->rd);
    src1 = get_gpr(ctx, a->rs1, ext);

    func(dest, src1, a->shamt);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_shift_imm_fn_per_ol(DisasContext *ctx, arg_shift *a,
                                    DisasExtend ext,
                                    void (*f_tl)(TCGv, TCGv, target_long),
                                    void (*f_32)(TCGv, TCGv, target_long))
{
    int olen = get_olen(ctx);
    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            f_tl = f_32;
        } else {
            g_assert_not_reached();
        }
    }
    return gen_shift_imm_fn(ctx, a, ext, f_tl);
}

static bool gen_shift_imm_tl(DisasContext *ctx, arg_shift *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest, src1, src2;
    int max_len = get_olen(ctx);

    if (a->shamt >= max_len) {
        return false;
    }

    dest = dest_gpr(ctx, a->rd);
    src1 = get_gpr(ctx, a->rs1, ext);
    src2 = tcg_constant_tl(a->shamt);

    func(dest, src1, src2);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_shift(DisasContext *ctx, arg_r *a, DisasExtend ext,
                      void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv ext2 = tcg_temp_new();

    tcg_gen_andi_tl(ext2, src2, get_olen(ctx) - 1);
    func(dest, src1, ext2);

    gen_set_gpr(ctx, a->rd, dest);
    tcg_temp_free(ext2);
    return true;
}

static bool gen_shift_per_ol(DisasContext *ctx, arg_r *a, DisasExtend ext,
                             void (*f_tl)(TCGv, TCGv, TCGv),
                             void (*f_32)(TCGv, TCGv, TCGv))
{
    int olen = get_olen(ctx);
    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            f_tl = f_32;
        } else {
            g_assert_not_reached();
        }
    }
    return gen_shift(ctx, a, ext, f_tl);
}

static bool gen_unary(DisasContext *ctx, arg_r2 *a, DisasExtend ext,
                      void (*func)(TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);

    func(dest, src1);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_unary_per_ol(DisasContext *ctx, arg_r2 *a, DisasExtend ext,
                             void (*f_tl)(TCGv, TCGv),
                             void (*f_32)(TCGv, TCGv))
{
    int olen = get_olen(ctx);

    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            f_tl = f_32;
        } else {
            g_assert_not_reached();
        }
    }
    return gen_unary(ctx, a, ext, f_tl);
}

static uint32_t opcode_at(DisasContextBase *dcbase, target_ulong pc)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUState *cpu = ctx->cs;
    CPURISCVState *env = cpu->env_ptr;

    return cpu_ldl_code(env, pc);
}

/* Include insn module translation function */
#include "insn_trans/trans_rvi.c.inc"
#include "insn_trans/trans_rvm.c.inc"
#include "insn_trans/trans_rva.c.inc"
#include "insn_trans/trans_rvf.c.inc"
#include "insn_trans/trans_rvd.c.inc"
#include "insn_trans/trans_rvh.c.inc"
#include "insn_trans/trans_rvv.c.inc"
#include "insn_trans/trans_rvb.c.inc"
#include "insn_trans/trans_rvv_7.c.inc"
#include "insn_trans/trans_rvp.c.inc"
#include "insn_trans/trans_rvzicbo.c.inc"
#include "insn_trans/trans_privileged.c.inc"
#include "insn_trans/trans_svinval.c.inc"
#include "insn_trans/trans_xthead.inc.c"
#include "insn_trans/trans_rvmm.c.inc"

/* Include the auto-generated decoder for 16 bit insn */
#include "decode-insn16.c.inc"

static void decode_opc(CPURISCVState *env, DisasContext *ctx, uint16_t opcode)
{
    /* check for compressed insn */
    if (extract16(opcode, 0, 2) != 3) {
        if (!has_ext(ctx, RVC)) {
            gen_exception_illegal(ctx);
        } else {
            ctx->pc_succ_insn = ctx->base.pc_next + 2;
            if (!decode_insn16(ctx, opcode)) {
                gen_exception_illegal(ctx);
            }
        }
    } else {
        uint32_t opcode32 = opcode;
        opcode32 = deposit32(opcode32, 16, 16,
                             translator_lduw(env, ctx->base.pc_next + 2));
        ctx->pc_succ_insn = ctx->base.pc_next + 4;
        if (ctx->vext_ver == VEXT_VERSION_0_07_1) {
            if (decode_vector_7(ctx, opcode32)) {
                return;
            }
        }
        if (!decode_insn32(ctx, opcode32)) {
            gen_exception_illegal(ctx);
        }
    }
}

static void riscv_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cs->env_ptr;
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVTBFlags tb_flags = riscv_tbflags_from_tb(dcbase->tb);

    ctx->pc_succ_insn = ctx->base.pc_first;
    ctx->mem_idx = EX_TBFLAGS_ANY(tb_flags, MEM_IDX);
    ctx->mstatus_fs = tb_flags.flags & TB_FLAGS_ANY_MSTATUS_FS;
    ctx->mstatus_vs = tb_flags.flags & TB_FLAGS_ANY_MSTATUS_VS;
    ctx->mcsr_ms = EX_TBFLAGS_THEAD(tb_flags, MS);
    ctx->priv_ver = env->priv_ver;
    ctx->vext_ver = env->vext_ver;
#if !defined(CONFIG_USER_ONLY)
    if (riscv_has_ext(env, RVH)) {
        ctx->virt_enabled = riscv_cpu_virt_enabled(env);
    } else {
        ctx->virt_enabled = false;
    }
#else
    ctx->virt_enabled = false;
#endif
    ctx->misa_ext = env->misa_ext;
    ctx->frm = -1;  /* unknown rounding mode */
    ctx->ext_ifencei = cpu->cfg.ext_ifencei;
    ctx->vlen = cpu->cfg.vlen;
    ctx->mrowlen = cpu->cfg.mrowlen;
    ctx->hlsx = EX_TBFLAGS_ANY(tb_flags, HLSX);
    ctx->vill = EX_TBFLAGS_ANY(tb_flags, VILL);
    ctx->sew = EX_TBFLAGS_ANY(tb_flags, SEW);
    if (ctx->vext_ver == VEXT_VERSION_0_07_1) {
        ctx->lmul = EX_TBFLAGS_ANY(tb_flags, LMUL);
        ctx->mlen = 1 << (ctx->sew  + 3 - ctx->lmul);
    } else {
        ctx->lmul = sextract32(EX_TBFLAGS_ANY(tb_flags, LMUL), 0, 3);
        ctx->mlen = 1;
    }
    ctx->vstart = env->vstart;
    ctx->vl_eq_vlmax = EX_TBFLAGS_ANY(tb_flags, VL_EQ_VLMAX);
    ctx->ext_psfoperand = cpu->cfg.ext_psfoperand;
    ctx->xl = EX_TBFLAGS_ANY(tb_flags, XL);
    ctx->bf16 = EX_TBFLAGS_THEAD(tb_flags, BF16);
    ctx->pwi32 = EX_TBFLAGS_THEAD(tb_flags, PWI32);
    ctx->pwi64 = EX_TBFLAGS_THEAD(tb_flags, PWI64);
    ctx->i4i32 = EX_TBFLAGS_THEAD(tb_flags, I4I32);
    ctx->i8i32 = EX_TBFLAGS_THEAD(tb_flags, I8I32);
    ctx->i16i64 = EX_TBFLAGS_THEAD(tb_flags, I16I64);
    ctx->f16f16 = EX_TBFLAGS_THEAD(tb_flags, F16F16);
    ctx->f32f32 = EX_TBFLAGS_THEAD(tb_flags, F32F32);
    ctx->f64f64 = EX_TBFLAGS_THEAD(tb_flags, F64F64);
    ctx->mill = EX_TBFLAGS_THEAD(tb_flags, MILL);
    ctx->nill = EX_TBFLAGS_THEAD(tb_flags, NILL);
    ctx->kill = EX_TBFLAGS_THEAD(tb_flags, KILL);
    ctx->npill = EX_TBFLAGS_THEAD(tb_flags, NPILL);
    ctx->cs = cs;
    ctx->ntemp = 0;
    memset(ctx->temp, 0, sizeof(ctx->temp));

    ctx->zero = tcg_constant_tl(0);
}

static void csky_tb_start_tb(CPURISCVState *env, const TranslationBlock *tb)
{
    TCGv t0;

    t0 = tcg_const_tl(tb->pc);
    gen_helper_tb_trace(cpu_env, t0);
    tcg_temp_free(t0);
}

static void riscv_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
    DisasContext *ctx = container_of(db, DisasContext, base);
    CPURISCVState *env = cpu->env_ptr;
    const TranslationBlock *tb = db->tb;

    if ((cpu->csky_trace_features & CSKY_TRACE) || env->jcount_start != 0) {
        gen_csky_jcount_start(ctx, cpu);
    }

    if (gen_tb_trace()) {
        csky_trace_tb_start(env, tb);
    }

    if (env->tb_trace == 1 || env->pctrace == 1) {
        csky_tb_start_tb(env, tb);
    }
}

static void riscv_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static void riscv_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cpu->env_ptr;
    uint16_t opcode16 = translator_lduw(env, ctx->base.pc_next);

    ctx->ol = ctx->xl;
    decode_opc(env, ctx, opcode16);
    ctx->base.pc_next = ctx->pc_succ_insn;

    for (int i = ctx->ntemp - 1; i >= 0; --i) {
        tcg_temp_free(ctx->temp[i]);
        ctx->temp[i] = NULL;
    }
    ctx->ntemp = 0;

    if (ctx->base.is_jmp == DISAS_NEXT) {
        target_ulong page_start;

        page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
        if (ctx->base.pc_next - page_start >= TARGET_PAGE_SIZE) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void riscv_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cpu->env_ptr;

    if (dcbase->is_jmp == DISAS_TOO_MANY || dcbase->is_jmp == DISAS_NEXT) {
        if (dcbase->num_insns == 1) {
            gen_helper_tag_pctrace(cpu_env, tcg_constant_tl(dcbase->pc_first));
        }
    }
    switch (dcbase->is_jmp) {
    case DISAS_TOO_MANY:
        if (gen_tb_trace()) {
            if (cpu->singlestep_enabled) {
                csky_trace_tb_exit(0x1, dcbase->pc_next - dcbase->pc_first);
            } else {
                /* exit on too many insns. */
                csky_trace_tb_exit(0x3, dcbase->pc_next - dcbase->pc_first);
            }
        }
        gen_goto_tb(ctx, 0, dcbase->pc_next);
        break;
    case DISAS_NORETURN:
        if (gen_tb_trace()) {
            csky_trace_tb_exit(0x2, dcbase->pc_next - dcbase->pc_first);
        }
        break;
    default:
        g_assert_not_reached();
    }
    if (cpu->csky_trace_features & CSKY_TRACE || env->jcount_start != 0) {
        gen_csky_jcount_end(dcbase->num_insns);
    }

    if (env->tb_trace == 1) {
        /* jcount to filter tb_trace */
        if (env->jcount_enable == 0) {
            csky_dump_tb_map(dcbase);
        } else if ((dcbase->pc_first > env->jcount_start) &&
                    (dcbase->pc_next < env->jcount_end)) {
            csky_dump_tb_map(dcbase);
        }
    }
}

static void riscv_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
#ifndef CONFIG_USER_ONLY
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
#endif

    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
#ifndef CONFIG_USER_ONLY
    qemu_log("Priv: "TARGET_FMT_ld"; Virt: "TARGET_FMT_ld"\n", env->priv, env->virt);
#endif
    log_target_disas(cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps riscv_tr_ops = {
    .init_disas_context = riscv_tr_init_disas_context,
    .tb_start           = riscv_tr_tb_start,
    .insn_start         = riscv_tr_insn_start,
    .translate_insn     = riscv_tr_translate_insn,
    .tb_stop            = riscv_tr_tb_stop,
    .disas_log          = riscv_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    DisasContext ctx;

    translator_loop(&riscv_tr_ops, &ctx.base, cs, tb, max_insns);
}

void riscv_translate_init(void)
{
    int i;

    /*
     * cpu_gpr[0] is a placeholder for the zero register. Do not use it.
     * Use the gen_set_gpr and get_gpr helper functions when accessing regs,
     * unless you specifically block reads/writes to reg 0.
     */
    cpu_gpr[0] = NULL;

    for (i = 1; i < 32; i++) {
        cpu_gpr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPURISCVState, gpr[i]), riscv_int_regnames[i]);
    }

    for (i = 0; i < 32; i++) {
        cpu_fpr[i] = tcg_global_mem_new_i64(cpu_env,
            offsetof(CPURISCVState, fpr[i]), riscv_fpr_regnames[i]);
    }

    cpu_pc = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, pc), "pc");
    cpu_vl = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, vl), "vl");
    sizem = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, sizem), "sizem");
    sizen = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, sizen), "sizen");
    sizek = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, sizek), "sizek");
    cpu_vstart = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, vstart),
                            "vstart");
    load_res = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, load_res),
                             "load_res");
    load_val = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, load_val),
                             "load_val");
}
