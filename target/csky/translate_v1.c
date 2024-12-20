/*
 * csky_v1 translation
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "translate.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"
#include "exec/translator.h"
#include "tcg/tcg-op.h"
#include "qemu/log.h"
#include "exec/gdbstub.h"
#include "exec/tracestub.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

static const char *regnames[] = {
    "sp", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };

/* is_jmp field values */
#define DISAS_JUMP    DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_UPDATE  DISAS_TARGET_1 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP DISAS_TARGET_2 /* only pc was modified statically */

static TCGv_i32 cpu_R[16];
static TCGv_i32 cpu_c;
static TCGv_i32 cpu_v;
static TCGv_i32 cpu_s;
static TCGv_i32 cpu_hi ;
static TCGv_i32 cpu_lo;
static TCGv_i32 cpu_hi_guard;
static TCGv_i32 cpu_lo_guard;

#if defined(CONFIG_USER_ONLY)
#define IS_SUPER(dc)  0
#else
#define IS_SUPER(dc)  (dc->super)
#endif

/* initialize TCG globals. */
void csky_translate_init(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        cpu_R[i] = tcg_global_mem_new_i32(tcg_env,
                            offsetof(CPUCSKYState, regs[i]),
                            regnames[i]);
    }
    cpu_c = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, psr_c), "cpu_c");
    cpu_v = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, dcsr_v), "cpu_v");
    cpu_s = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, psr_s), "cpu_s");
    cpu_hi = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, hi), "cpu_hi");
    cpu_lo = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, lo), "cpu_lo");
    cpu_hi_guard = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, hi_guard), "cpu_hi_guard");
    cpu_lo_guard = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, lo_guard), "cpu_lo_guard");

}


static inline TCGv load_cpu_offset(int offset)
{
    TCGv tmp = tcg_temp_new();
    tcg_gen_ld_i32(tmp, tcg_env, offset);
    return tmp;
}

#define load_cpu_field(name) load_cpu_offset(offsetof(CPUCSKYState, name))

static inline void store_cpu_offset(TCGv var, int offset)
{
    tcg_gen_st_i32(var, tcg_env, offset);
}

#define store_cpu_field(var, name) \
store_cpu_offset(var, offsetof(CPUCSKYState, name))

static inline void gen_save_pc(target_ulong pc)
{
    TCGv t0;

    t0 = tcg_constant_tl(pc);
    store_cpu_field(t0, pc);

}

static inline void generate_exception(DisasContext *ctx, int excp)
{
    TCGv t0;

    print_exception(ctx, excp);

    t0 = tcg_constant_tl(excp);
    gen_save_pc(ctx->pc);
    gen_helper_exception(tcg_env, t0);
    ctx->base.is_jmp = DISAS_NORETURN;

}

static inline void gen_goto_tb(DisasContext *ctx, int n, uint32_t dest)
{
    const TranslationBlock *tb;
    TCGv t0;

    tb = ctx->base.tb;

    if (unlikely(ctx->base.singlestep_enabled)) {
        gen_save_pc(dest);
        t0 = tcg_constant_tl(EXCP_DEBUG);
        gen_helper_exception(tcg_env, t0);
    }
#if !defined(CONFIG_USER_ONLY)
    else if (unlikely((ctx->trace_mode == INST_TRACE_MODE)
                || (ctx->trace_mode == BRAN_TRACE_MODE))) {
        gen_save_pc(dest);
        t0 = tcg_constant_tl(EXCP_CSKY_TRACE);
        gen_helper_exception(tcg_env, t0);
        ctx->maybe_change_flow = 1;
    }
#endif
    else if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(n);
        gen_save_pc(dest);
        tcg_gen_exit_tb(tb, n);
    } else {
        gen_save_pc(dest);
        tcg_gen_exit_tb(NULL, 0);
    }

}

static inline void check_insn(DisasContext *ctx, uint32_t flags)
{
    if (unlikely(!has_insn(ctx, flags))) {
        generate_exception(ctx, EXCP_CSKY_UDEF);
    }
}

#ifndef CONFIG_USER_ONLY
/* generate mfcr instruction */
static inline void gen_mfcr(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
{
    TCGv t0;

    switch (cr_num) {
    case 0x0:
        /* cr0 psr */
        gen_helper_mfcr_cr0(cpu_R[rz], tcg_env);
        break;
    case 0x1:
        /* vbr */
        t0 = load_cpu_field(cp0.vbr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x2:
        /* epsr */
        t0 = load_cpu_field(cp0.epsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x3:
        /* fpsr */
        t0 = load_cpu_field(cp0.fpsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x4:
        /* epc */
        t0 = load_cpu_field(cp0.epc);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x5:
        /* fpc */
        t0 = load_cpu_field(cp0.fpc);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x6:
        /* ss0 */
        t0 = load_cpu_field(cp0.ss0);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x7:
        /* ss1 */
        t0 = load_cpu_field(cp0.ss1);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x8:
        /* ss2 */
        t0 = load_cpu_field(cp0.ss2);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x9:
        /* ss3 */
        t0 = load_cpu_field(cp0.ss3);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0xa:
        /* ss4 */
        t0 = load_cpu_field(cp0.ss4);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0xb:
        /* gcr */
        t0 = load_cpu_field(cp0.gcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0xc:
        /* gsr */
        t0 = load_cpu_field(cp0.gsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0xd:
        /* cpidr */
        t0 = load_cpu_field(cp0.cpidr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0xe:
        /* cr14 dcsr */
        t0 = load_cpu_field(cp0.dcsr);
        tcg_gen_andi_tl(cpu_R[rz], t0, ~0x1);
        t0 = load_cpu_field(dcsr_v);
        tcg_gen_or_tl(cpu_R[rz], cpu_R[rz], t0);

        break;
    case 0xf:
        /* cpwr */
        t0 = load_cpu_field(cp0.cpwr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x10:
        /* no CR16 */
        break;
    case 0x11:
        /* cfr */
        t0 = load_cpu_field(cp0.cfr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x12:
        /* ccr */
        t0 = load_cpu_field(cp0.ccr);
        tcg_gen_mov_tl(cpu_R[rz], t0);


        break;
    case 0x13:
        /* capr */
        t0 = load_cpu_field(cp0.capr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x14:
        /* cr20 pacr */
        gen_helper_mfcr_cr20(cpu_R[rz], tcg_env);
        break;
    case 0x15:
        /* prsr */
        t0 = load_cpu_field(cp0.prsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    default:
        break;
    }
}

/* generate mtcr instruction */
static inline void gen_mtcr(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
{

    switch (cr_num) {
    case 0x0:
        /* psr */
        gen_helper_mtcr_cr0(tcg_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x1:
        /* vbr */
        {
            TCGv t0 = tcg_temp_new();

            tcg_gen_andi_tl(t0, cpu_R[rx], ~0x3ff);
            store_cpu_field(t0, cp0.vbr);

        }
        break;
    case 0x2:
        /* epsr */
        store_cpu_field(cpu_R[rx], cp0.epsr);
        break;
    case 0x3:
        /* fpsr */
        store_cpu_field(cpu_R[rx], cp0.fpsr);
        break;
    case 0x4:
        /* epc */
        store_cpu_field(cpu_R[rx], cp0.epc);
        break;
    case 0x5:
        /* fpc */
        store_cpu_field(cpu_R[rx], cp0.fpc);
        break;
    case 0x6:
        /* ss0 */
        store_cpu_field(cpu_R[rx], cp0.ss0);
        break;
    case 0x7:
        /* ss1 */
        store_cpu_field(cpu_R[rx], cp0.ss1);
        break;
    case 0x8:
        /* ss2 */
        store_cpu_field(cpu_R[rx], cp0.ss2);
        break;
    case 0x9:
        /* ss3 */
        store_cpu_field(cpu_R[rx], cp0.ss3);
        break;
    case 0xa:
        /* ss4 */
        store_cpu_field(cpu_R[rx], cp0.ss4);
        break;
    case 0xb:
        /* gcr */
        store_cpu_field(cpu_R[rx], cp0.gcr);
        break;
    case 0xc:
        /* gsr */
        /* Read only */
        break;
    case 0xd:
        /* cpidr */
        /* Read only */
        break;
    case 0xe:
        /* dcsr */
        {
            TCGv t0, t1;

            t0 = load_cpu_field(cp0.dcsr);
            t1 = load_cpu_field(dcsr_v);
            tcg_gen_andi_tl(t0, t0, ~0x1);
            tcg_gen_or_tl(t1, t1, t0);
            store_cpu_field(t1, cp0.dcsr);

        }
        break;
    case 0xf:
        /* cpwr */
        /* FIXME */
        store_cpu_field(cpu_R[rx], cp0.cpwr);
        break;
    case 0x10:
        /* no CR16 */
        break;
    case 0x11:
        /* cfr */
        store_cpu_field(cpu_R[rx], cp0.cfr);
        break;
    case 0x12:
        /* ccr */
        gen_helper_mtcr_cr18(tcg_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x13:
        /* capr */
        store_cpu_field(cpu_R[rx], cp0.capr);
        break;
    case 0x14:
        /* pacr */
        gen_helper_mtcr_cr20(tcg_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x15:
        /* psrsr */
        store_cpu_field(cpu_R[rx], cp0.prsr);
        break;
    default:
        break;
    }
}

/* Read MMU Coprocessor Contronl Registers */
static inline void
gen_cprcr_cp15(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
{
    TCGv t0;

    switch (cr_num) {
    case 0x0:
        /* MIR */
        t0 = load_cpu_field(mmu.mir);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x1:
        /* MRR */
        t0 = load_cpu_field(mmu.mrr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x2:
        /* MEL0 */
        t0 = load_cpu_field(mmu.mel0);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x3:
        /* MEL1 */
        t0 = load_cpu_field(mmu.mel1);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x4:
        /* MEH */
        t0 = load_cpu_field(mmu.meh);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x5:
        /* MCR */
        t0 = load_cpu_field(mmu.mcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x6:
        /* MPR */
        t0 = load_cpu_field(mmu.mpr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x7:
        /* MWR */
        t0 = load_cpu_field(mmu.mwr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x8:
        /* MCIR */
        t0 = load_cpu_field(mmu.mcir);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0x9:
        /* CP15_CR9 */
        t0 = load_cpu_field(mmu.cr9);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0xa:
        /* CP15_CR10 */
        t0 = load_cpu_field(mmu.cr10);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0xb:
        /* CP15_CR11 */
        t0 = load_cpu_field(mmu.cr11);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        break;
    case 0xc:
        /* CP15_CR12 */
        t0 = load_cpu_field(mmu.cr12);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0xd:
        /* CP15_CR13 */
        t0 = load_cpu_field(mmu.cr13);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0xe:
        /* CP15_CR14 */
        t0 = load_cpu_field(mmu.cr14);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0xf:
        /* CP15_CR15 */
        t0 = load_cpu_field(mmu.cr15);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x10:
        /* CP15_CR16 */
        t0 = load_cpu_field(mmu.cr16);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1d:
        /* CP15_mpgd */
        t0 = load_cpu_field(mmu.mpgd1);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1e:
        t0 = load_cpu_field(mmu.msa0);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1f:
        t0 = load_cpu_field(mmu.msa1);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    default:
        break;
    }
}

/* Write MMU Coprocessor Contronl Registers */
static inline void
gen_cpwcr_cp15(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
{
    switch (cr_num) {
    case 0x0:
        /* MIR */
        store_cpu_field(cpu_R[rx], mmu.mir);
        break;
    case 0x1:
        /* MRR */
        store_cpu_field(cpu_R[rx], mmu.mrr);
        break;
    case 0x2:
        /* MEL0 */
        store_cpu_field(cpu_R[rx], mmu.mel0);
        break;
    case 0x3:
        /* MEL1 */
        store_cpu_field(cpu_R[rx], mmu.mel1);
        break;
    case 0x4:
        /* MEH */
        gen_helper_meh_write(tcg_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x5:
        /* MCR */
        store_cpu_field(cpu_R[rx], mmu.mcr);
        break;
    case 0x6:
        /* MPR */
        store_cpu_field(cpu_R[rx], mmu.mpr);
        break;
    case 0x7:
        /* MWR */
        store_cpu_field(cpu_R[rx], mmu.mwr);
        break;
    case 0x8:
        /* MCIR */
        gen_helper_mcir_write(tcg_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x9:
        /* FIXME SPM is not implement yet */
        /* CP15_CR9 */
        store_cpu_field(cpu_R[rx], mmu.cr9);
        break;
    case 0xa:
        /* CP15_CR10 */
        store_cpu_field(cpu_R[rx], mmu.cr10);
        break;
    case 0xb:
        /* CP15_CR11 */
        store_cpu_field(cpu_R[rx], mmu.cr11);
        break;
    case 0xc:
        /* CP15_CR12 */
        store_cpu_field(cpu_R[rx], mmu.cr12);
        break;
    case 0xd:
        /* CP15_CR13 */
        store_cpu_field(cpu_R[rx], mmu.cr13);
        break;
    case 0xe:
        /* CP15_CR14 */
        store_cpu_field(cpu_R[rx], mmu.cr14);
        break;
    case 0xf:
        /* CP15_CR15 */
        store_cpu_field(cpu_R[rx], mmu.cr15);
        break;
    case 0x10:
        /* CP15_CR16 */
        store_cpu_field(cpu_R[rx], mmu.cr16);
        break;
    case 0x1d:
        /* CP15_mpar */
        store_cpu_field(cpu_R[rx], mmu.mpgd1);
        gen_save_pc(ctx->pc + 2);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x1e:
        store_cpu_field(cpu_R[rx], mmu.msa0);
        gen_save_pc(ctx->pc + 2);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x1f:
        store_cpu_field(cpu_R[rx], mmu.msa1);
        gen_save_pc(ctx->pc + 2);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    default:
        break;
    }
}
#endif /* !CONFIG_USER_ONLY */

static inline void tstnbz(int rx)
{
    TCGv t0 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    tcg_gen_movi_tl(cpu_c, 0);
    tcg_gen_andi_tl(t0, cpu_R[rx], 0xff000000);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_andi_tl(t0, cpu_R[rx], 0x00ff0000);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_andi_tl(t0, cpu_R[rx], 0x0000ff00);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_andi_tl(t0, cpu_R[rx], 0x000000ff);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_movi_tl(cpu_c, 1);
    gen_set_label(l1);

}

static inline void mac(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t0, cpu_R[rx]);
    tcg_gen_extu_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_tl_i64(t1, cpu_lo, cpu_hi);
    tcg_gen_add_i64(t0, t0, t1);

    tcg_gen_trunc_i64_tl(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t0);

}

static inline void addc(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_mov_tl(t0, cpu_R[rx]);
    tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
    tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_c);
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    tcg_gen_setcond_tl(TCG_COND_LTU, cpu_c, cpu_R[rx], t0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_setcond_tl(TCG_COND_LEU, cpu_c, cpu_R[rx], t0);
    gen_set_label(l2);

}

static inline void subc(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_subfi_tl(t0, 1, cpu_c);
    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
    tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], t0);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_setcond_tl(TCG_COND_GTU, cpu_c, t1, cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, t1, cpu_R[ry]);
    gen_set_label(l2);
}

static inline void lsr(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_movi_tl(t1, 0);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_shr_tl(t1, cpu_R[rx], t0);
    gen_set_label(l1);
    tcg_gen_mov_tl(cpu_R[rx], t1);

}

static inline void lsl(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_movi_tl(t1, 0);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_shl_tl(t1, cpu_R[rx], t0);
    gen_set_label(l1);
    tcg_gen_mov_tl(cpu_R[rx], t1);

}

static inline void bgenr(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    tcg_gen_mov_tl(t1, cpu_R[ry]);  /* maybe rx==ry, so ry's saved in temp */
    tcg_gen_movi_tl(cpu_R[rx], 0);
    tcg_gen_andi_tl(t0, t1, 0x20);
    tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);
    tcg_gen_andi_tl(t1, t1, 0x1f);
    tcg_gen_movi_tl(t0, 1);
    tcg_gen_shl_tl(cpu_R[rx], t0, t1);
    gen_set_label(l1);

}

static inline void asr(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_brcondi_tl(TCG_COND_LEU, t0, 31, l1);
    tcg_gen_movi_tl(t0, 31);
    gen_set_label(l1);
    tcg_gen_sar_tl(cpu_R[rx], cpu_R[rx], t0);

}


static inline void divu(DisasContext *ctx, int rx)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    TCGv t0;

    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[1], 0, l1);
    tcg_gen_divu_tl(cpu_R[rx], cpu_R[rx], cpu_R[1]);
    tcg_gen_br(l2);
    gen_set_label(l1);

    t0 = tcg_constant_tl(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(tcg_env, t0);
    ctx->base.is_jmp = DISAS_NEXT;


    gen_set_label(l2);
}

static inline void divs(DisasContext *ctx, int rx)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    TCGv t0;

    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[1], 0, l1);
    tcg_gen_div_tl(cpu_R[rx], cpu_R[rx], cpu_R[1]);
    tcg_gen_br(l2);
    gen_set_label(l1);

    t0 = tcg_constant_tl(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(tcg_env, t0);
    ctx->base.is_jmp = DISAS_NEXT;


    gen_set_label(l2);
}

static inline void xsr(int rx)
{
    TCGv t0 = tcg_temp_new();

    tcg_gen_andi_tl(t0, cpu_R[rx], 0x1);
    tcg_gen_shri_tl(cpu_R[rx], cpu_R[rx], 1);
    tcg_gen_shli_tl(cpu_c, cpu_c, 31);
    tcg_gen_or_tl(cpu_R[rx], cpu_R[rx], cpu_c);
    tcg_gen_mov_tl(cpu_c, t0);

}

static inline void muls(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_movi_tl(cpu_v, 0);
    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);
    tcg_gen_ext_tl_i64(t0, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_trunc_i64_tl(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);

}

static inline void mulsa(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_temp_new_i64();
    TCGv_i32 t5 = tcg_temp_new_i32();

    tcg_gen_ext_tl_i64(t0, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_hi);
    tcg_gen_add_i64(t2, t0, t1);

    tcg_gen_xor_i64(t3, t0, t1);
    tcg_gen_xor_i64(t4, t0, t2);
    tcg_gen_andc_i64(t4, t4, t3);

    TCGv_i64 tx = tcg_temp_new_i64();
    tcg_gen_shri_i64(tx, t4, 63);
    tcg_gen_extrl_i64_i32(t5, tx);
    tcg_gen_mov_i32(cpu_v, t5);

    tcg_gen_trunc_i64_tl(cpu_lo, t2);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t2);

    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);

}

static inline void mulss(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_temp_new_i64();
    TCGv_i32 t5 = tcg_temp_new_i32();

    tcg_gen_ext_tl_i64(t0, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_hi);
    tcg_gen_sub_i64(t2, t1, t0);

    tcg_gen_xor_i64(t3, t0, t1);
    tcg_gen_xor_i64(t4, t0, t2);
    tcg_gen_andc_i64(t4, t3, t4);

    TCGv_i64 tx = tcg_temp_new_i64();
    tcg_gen_shri_i64(tx, t4, 63);
    tcg_gen_extrl_i64_i32(t5, tx);
    tcg_gen_mov_i32(cpu_v, t5);

    tcg_gen_shri_i64(t3, t2, 63);
    tcg_gen_trunc_i64_tl(cpu_lo, t2);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t2);

    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);


}

static inline void mulu(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_movi_tl(cpu_v, 0);
    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);

    tcg_gen_extu_tl_i64(t0, cpu_R[rx]);
    tcg_gen_extu_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_trunc_i64_tl(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);

}

static inline void mulua(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t0, cpu_R[rx]);
    tcg_gen_extu_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t1, t0);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_hi);
    tcg_gen_add_i64(t2, t1, t0);

    tcg_gen_trunc_i64_tl(cpu_lo, t2);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t2);
    TCGv_i64 tx = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_LT, tx, t2, t1);
    tcg_gen_extrl_i64_i32(cpu_v, tx);

    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);

}

static inline void mulus(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t0, cpu_R[rx]);
    tcg_gen_extu_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t1, t0);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_hi);

    TCGv_i64 tx = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_LT, tx, t1, t0);
    tcg_gen_extrl_i64_i32(cpu_v, tx);
    tcg_gen_sub_i64(t2, t1, t0);

    tcg_gen_trunc_i64_tl(cpu_lo, t2);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t2);

    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);


}

static inline void mulsha(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t0, cpu_R[rx]);
    tcg_gen_ext16s_tl(t1, cpu_R[ry]);
    tcg_gen_mul_tl(t0, t0, t1);
    tcg_gen_ext_tl_i64(t2, t0);
    tcg_gen_concat_i32_i64(t3, cpu_lo, cpu_lo_guard);
    tcg_gen_add_i64(t2, t3, t2);
    tcg_gen_extrl_i64_i32(cpu_lo, t2);
    tcg_gen_shri_i64(t3, t2, 31);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t2);
    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t3, 0x0, l1);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t3, 0x1ffffffffLL, l1);
    tcg_gen_movi_i32(cpu_v, 1);
    gen_set_label(l1);

}

static inline void mulshs(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t0, cpu_R[rx]);
    tcg_gen_ext16s_tl(t1, cpu_R[ry]);
    tcg_gen_mul_tl(t0, t0, t1);
    tcg_gen_ext_tl_i64(t2, t0);
    tcg_gen_concat_i32_i64(t3, cpu_lo, cpu_lo_guard);
    tcg_gen_sub_i64(t2, t3, t2);
    tcg_gen_extrl_i64_i32(cpu_lo, t2);
    tcg_gen_shri_i64(t3, t2, 31);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t2);
    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t3, 0x0, l1);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t3, 0x1ffffffffLL, l1);
    tcg_gen_movi_i32(cpu_v, 1);
    gen_set_label(l1);


}

static inline void mulsw(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t2 = tcg_temp_new();

    tcg_gen_ext16s_tl(t2, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t2);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rx], t0);

}

static inline void mulswa(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t0, cpu_R[rx]) ;
    tcg_gen_ext_tl_i64(t1, t0);
    tcg_gen_ext_tl_i64(t2, cpu_R[ry]);
    tcg_gen_mul_i64(t1, t1, t2) ;
    tcg_gen_shri_i64(t1, t1, 16);
    tcg_gen_concat_i32_i64(t2, cpu_lo, cpu_lo_guard);
    tcg_gen_add_i64(t2, t2, t1);
    tcg_gen_extrl_i64_i32(cpu_lo, t2);
    tcg_gen_shri_i64(t1, t2, 31);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t2);
    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t1, 0x0, l1);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t1, 0x1ffffffffLL, l1);
    tcg_gen_movi_i32(cpu_v, 1);
    gen_set_label(l1);


}


static inline void mulsws(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, t0);
    tcg_gen_ext_i32_i64(t2, cpu_R[ry]);
    tcg_gen_mul_i64(t1, t1, t2);
    tcg_gen_shri_i64(t1, t1, 16);
    tcg_gen_concat_i32_i64(t2, cpu_lo, cpu_lo_guard);
    tcg_gen_sub_i64(t2, t2, t1);
    tcg_gen_extrl_i64_i32(cpu_lo, t2);
    tcg_gen_shri_i64(t1, t2, 31);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t2);
    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t1, 0x0, l1);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t1, 0x1ffffffffLL, l1);
    tcg_gen_movi_i32(cpu_v, 1);
    gen_set_label(l1);


}



static inline void vmulsh(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    tcg_gen_movi_tl(cpu_v, 0);
    tcg_gen_ext16s_tl(t0, cpu_R[rx]);
    tcg_gen_ext16s_tl(t1, cpu_R[ry]);
    tcg_gen_mul_tl(cpu_lo, t0, t1);
    tcg_gen_sari_tl(cpu_lo_guard, cpu_lo, 31);
    tcg_gen_sari_tl(t0, cpu_R[rx], 16);
    tcg_gen_sari_tl(t1, cpu_R[ry], 16);
    tcg_gen_mul_tl(cpu_hi, t0, t1);
    tcg_gen_sari_tl(cpu_hi_guard, cpu_hi, 31);


}


static inline void vmulsha(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t3, cpu_R[rx]);
    tcg_gen_ext16s_tl(t4,  cpu_R[ry]);
    tcg_gen_mul_tl(t3, t3, t4);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_lo_guard);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0);

    tcg_gen_sari_tl(t3, cpu_R[rx], 16);
    tcg_gen_sari_tl(t4, cpu_R[ry], 16);
    tcg_gen_mul_tl(t3, t3, t4);
    tcg_gen_concat_i32_i64(t1, cpu_hi, cpu_hi_guard);
    tcg_gen_ext_tl_i64(t0, t3) ;
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0);

    tcg_gen_sari_tl(t3, cpu_lo, 31);
    tcg_gen_sari_tl(t4, cpu_hi, 31);
    tcg_gen_movi_tl(cpu_v, 1);
    tcg_gen_brcond_tl(TCG_COND_NE, t3, cpu_lo_guard, l1);
    tcg_gen_brcond_tl(TCG_COND_NE, t4, cpu_hi_guard, l1);
    tcg_gen_movi_tl(cpu_v, 0);
    gen_set_label(l1);


}


static inline void vmulshs(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t3, cpu_R[rx]);
    tcg_gen_ext16s_tl(t4,  cpu_R[ry]);
    tcg_gen_mul_tl(t3, t3, t4);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_lo_guard);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_sub_i64(t0, t1, t0);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0);

    tcg_gen_sari_tl(t3, cpu_R[rx], 16);
    tcg_gen_sari_tl(t4, cpu_R[ry], 16);
    tcg_gen_mul_tl(t3, t3, t4);
    tcg_gen_concat_i32_i64(t1, cpu_hi, cpu_hi_guard);
    tcg_gen_ext_tl_i64(t0, t3) ;
    tcg_gen_sub_i64(t0, t1, t0);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0);

    tcg_gen_sari_tl(t3, cpu_lo, 31);
    tcg_gen_sari_tl(t4, cpu_hi, 31);
    tcg_gen_movi_tl(cpu_v, 1);
    tcg_gen_brcond_tl(TCG_COND_NE, t3, cpu_lo_guard, l1);
    tcg_gen_brcond_tl(TCG_COND_NE, t4, cpu_hi_guard, l1);
    tcg_gen_movi_tl(cpu_v, 0);
    gen_set_label(l1);



}

static inline void vmulsw(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t2 = tcg_temp_new();

    tcg_gen_movi_tl(cpu_v, 0);
    tcg_gen_ext16s_tl(t2, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t2);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_sari_tl(cpu_lo_guard, cpu_lo, 31);
    tcg_gen_sari_tl(t2, cpu_R[rx], 16);
    tcg_gen_ext_tl_i64(t0, t2);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_sari_tl(cpu_hi_guard, cpu_hi, 31);


}

static inline void vmulswa(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t3, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_concat_i32_i64(t2, cpu_lo, cpu_lo_guard);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_sari_i64(t0, t0, 16);
    tcg_gen_add_i64(t0, t0, t2);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0);

    tcg_gen_sari_tl(t3, cpu_R[rx], 16);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_concat_i32_i64(t2, cpu_hi, cpu_hi_guard);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_sari_i64(t0, t0, 16);
    tcg_gen_add_i64(t0, t0, t2);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0);

    tcg_gen_sari_tl(t3, cpu_lo, 31);
    tcg_gen_sari_tl(t4, cpu_hi, 31);
    tcg_gen_movi_tl(cpu_v, 1);
    tcg_gen_brcond_tl(TCG_COND_NE, t3, cpu_lo_guard, l1);
    tcg_gen_brcond_tl(TCG_COND_NE, t4, cpu_hi_guard, l1);
    tcg_gen_movi_tl(cpu_v, 0);
    gen_set_label(l1);


}

static inline void vmulsws(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t3, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_concat_i32_i64(t2, cpu_lo, cpu_lo_guard);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_sari_i64(t0, t0, 16);
    tcg_gen_sub_i64(t0, t2, t0);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0);

    tcg_gen_sari_tl(t3, cpu_R[rx], 16);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_concat_i32_i64(t2, cpu_hi, cpu_hi_guard);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_sari_i64(t0, t0, 16);
    tcg_gen_sub_i64(t0, t2, t0);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0) ;

    tcg_gen_sari_tl(t3, cpu_lo, 31);
    tcg_gen_sari_tl(t4, cpu_hi, 31);
    tcg_gen_movi_tl(cpu_v, 1);
    tcg_gen_brcond_tl(TCG_COND_NE, t3, cpu_lo_guard, l1);
    tcg_gen_brcond_tl(TCG_COND_NE, t4, cpu_hi_guard, l1);
    tcg_gen_movi_tl(cpu_v, 0);
    gen_set_label(l1);


}

static inline void bt(DisasContext *ctx, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();

    val = offset << 1;
    if (val & 0x800) {
        val |= 0xfffff800;
    }
    val += ctx->pc + 2;

    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    gen_goto_tb(ctx, 1, ctx->pc + 2);
    gen_set_label(l1);

    gen_goto_tb(ctx, 0, val);
}

static inline void bf(DisasContext *ctx, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();

    val = offset << 1;
    if (val & 0x800) {
        val |= 0xfffff800;
    }
    val += ctx->pc + 2;
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 1, l1);
    gen_goto_tb(ctx, 1, ctx->pc + 2);
    gen_set_label(l1);

    gen_goto_tb(ctx, 0, val);
}

static inline void br(DisasContext *ctx, int offset)
{
    int val;
    val = offset << 1;
    if (val & 0x800) {
        val |= 0xfffff800;
    }
    val += ctx->pc + 2;

    gen_goto_tb(ctx, 0, val);

}

static inline void bsr(DisasContext *ctx, int offset)
{
    int val;
    val = offset << 1;
    if (val & 0x800) {
        val |= 0xfffff800;
    }
    val += ctx->pc + 2;
    tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);

    gen_goto_tb(ctx, 0, val);
}



#define  insn_1  ((ctx->insn & 0xf000) >> 12)
#define  insn_2  ((ctx->insn & 0x0f00) >> 8)
#define  insn_3  ((ctx->insn & 0x00f0) >> 4)
#define  insn_4   (ctx->insn & 0x000f)

static void disas_csky_v1_insn(CPUCSKYState *env, DisasContext *ctx)
{   unsigned int insn, rz, rx, ry;
    int imm, disp, i;
    TCGv t0 = 0, t1 = 0 , t2 = 0, t3 = 0;
    TCGLabel *l1;

    rx = 0;
    ry = 0;
    insn = ctx->insn;

    switch (insn_1) {
    case 0x0:
        switch (insn_2) {
        case 0x0:
            switch (insn_3) {
            case 0x0:
                switch (insn_4) {
                case 0x0:
                    if (is_gdbserver_start == TRUE) {
                        generate_exception(ctx, EXCP_DEBUG);
                    } else {
                        TCGv_i32 t4 = tcg_constant_i32(ctx->pc);
                        gen_helper_bkpt_exit(tcg_env, t4);
                    }
#if !defined(CONFIG_USER_ONLY)
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*bkpt*/
                case 0x1:
                    break;/*sync*/
                case 0x2:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_constant_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_helper_rte(tcg_env);
                        ctx->base.is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*rte*/
                case 0x3:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_constant_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_helper_rfi(tcg_env);
                        ctx->base.is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*rfi*/
                case 0x4:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_constant_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_save_pc(ctx->pc + 2);
                        gen_helper_stop(tcg_env);
                        ctx->base.is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*stop*/
                case 0x5:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_constant_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_save_pc(ctx->pc + 2);
                        gen_helper_wait(tcg_env);
                        ctx->base.is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*wait*/
                case 0x6:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_constant_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_save_pc(ctx->pc + 2);
                        gen_helper_doze(tcg_env);
                        ctx->base.is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*doze*/
                case 0x7:
#if !defined(CONFIG_USER_ONLY)
                    if (ctx->trace_mode == NORMAL_MODE) {
                        l1 = gen_new_label();
                        t0 =  load_cpu_field(idly4_counter);
                        tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);

                        t0 = tcg_constant_tl(4);
                        store_cpu_field(t0, idly4_counter);
                        tcg_gen_movi_tl(cpu_c, 0);

                        gen_save_pc(ctx->pc + 2);
                        ctx->base.is_jmp = DISAS_UPDATE;
                        gen_set_label(l1);
                    }
#endif
                    break;/*idly4*/
                case 0x8:
                    generate_exception(ctx, EXCP_CSKY_TRAP0);
#if !defined(CONFIG_USER_ONLY)
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*trap0*/
                case 0x9:
#if !defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_TRAP1);
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*trap1*/
                case 0xa:
                    generate_exception(ctx, EXCP_CSKY_TRAP2);
#if !defined(CONFIG_USER_ONLY)
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*trap2*/
                case 0xb:
                    generate_exception(ctx, EXCP_CSKY_TRAP3);
#if !defined(CONFIG_USER_ONLY)
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*trap3*/
                case 0xc:
                    check_insn(ctx, ABIV1_DSP);
                    tcg_gen_mov_tl(cpu_c, cpu_v);
                    break;/*mvtc*/
                case 0xd:
                    gen_helper_cprc(tcg_env);
                    gen_save_pc(ctx->pc + 2);
                    ctx->base.is_jmp = DISAS_UPDATE;
                    break;/*cprc-todo*/

                default:
                    goto illegal_op;
                }
                break;
            case 0x1:
#if defined(CONFIG_USER_ONLY)
                /*
                 * A temporary solution to support
                 * float instructions in cskyv1
                 */
                rx = (insn & 0xf) << 24;
                t0 = load_cpu_field(cp0.psr);
                tcg_gen_andi_tl(t0, t0, ~0x0f000000);
                tcg_gen_ori_tl(t0, t0, rx);
                store_cpu_field(t0, cp0.psr);
                if (rx == (0x1 << 24)) {
                    tcg_gen_movi_tl(t0, 0x0);
                    store_cpu_field(t0, cp1.fcr);
                    store_cpu_field(t0, cp1.fsr);
                }
                gen_save_pc(ctx->pc + 2);
                ctx->base.is_jmp = DISAS_UPDATE;

#else
                if (IS_SUPER(ctx)) {
                    rx = (insn & 0xf) << 24;
                    t0 = load_cpu_field(cp0.psr);
                    tcg_gen_andi_tl(t0, t0, ~0x0f000000);
                    tcg_gen_ori_tl(t0, t0, rx);
                    store_cpu_field(t0, cp0.psr);
                    if (rx == (0x1 << 24)) {
                        tcg_gen_movi_tl(t0, 0x0);
                        store_cpu_field(t0, cp1.fcr);
                        store_cpu_field(t0, cp1.fsr);
                    }
                    gen_save_pc(ctx->pc + 2);
                    ctx->base.is_jmp = DISAS_UPDATE;

                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
#endif
                break;/*cpseti-todo*/
            case 0x2:
                rx = insn & 0x000f;
                tcg_gen_mov_tl(cpu_R[rx], cpu_c);
                break;/*mvc*/
            case 0x3:
                rx = insn & 0x000f;
                tcg_gen_subfi_tl(cpu_R[rx], 1, cpu_c);
                break;/*mvcv*/
            case 0x4:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();
                tcg_gen_mov_tl(t0, cpu_R[rx]);
                t1 = tcg_temp_new();
                t2 = tcg_temp_new();
                t3 = tcg_temp_new();
                if (gen_mem_trace()) {
                    tcg_gen_movi_tl(t1, 4);
                    tcg_gen_movi_tl(t2, DATA_RADDR);
                    tcg_gen_movi_tl(t3, ctx->pc);
                    gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
                }
                for (i = 4; i <= 7; i++) {
                    tcg_gen_qemu_ld_i32(cpu_R[i], t0, ctx->mem_idx, MO_TEUL);
                    if (gen_mem_trace()) {
                        gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[i]);
                    }
                    tcg_gen_addi_tl(t0, t0, 4);
                }
                break;/*ldq*/
            case 0x5:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();
                tcg_gen_mov_tl(t0, cpu_R[rx]);
                t1 = tcg_temp_new();
                t2 = tcg_temp_new();
                t3 = tcg_temp_new();
                if (gen_mem_trace()) {
                    tcg_gen_movi_tl(t1, 4);
                    tcg_gen_movi_tl(t2, DATA_WADDR);
                    tcg_gen_movi_tl(t3, ctx->pc);
                    gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
                }
                for (i = 4; i <= 7; i++) {
                    tcg_gen_qemu_st_i32(cpu_R[i], t0, ctx->mem_idx, MO_TEUL);
                    if (gen_mem_trace()) {
                        gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[i]);
                    }
                    tcg_gen_addi_tl(t0, t0, 4);
                }
                break;/*stq*/
            case 0x6:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();

                tcg_gen_mov_tl(t0, cpu_R[0]);
                t1 = tcg_temp_new();
                t2 = tcg_temp_new();
                t3 = tcg_temp_new();
                if (gen_mem_trace()) {
                    tcg_gen_movi_tl(t1, 15 - rx + 1);
                    tcg_gen_movi_tl(t2, DATA_RADDR);
                    tcg_gen_movi_tl(t3, ctx->pc);
                    gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
                }
                for (i = rx; i <= 15; i++) {
                    tcg_gen_qemu_ld_i32(cpu_R[i], t0, ctx->mem_idx, MO_TEUL);
                    if (gen_mem_trace()) {
                        gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[i]);
                    }
                    tcg_gen_addi_tl(t0, t0, 4);
                }
                break;/*ldm*/
            case 0x7:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();

                tcg_gen_mov_tl(t0, cpu_R[0]);
                t1 = tcg_temp_new();
                t2 = tcg_temp_new();
                t3 = tcg_temp_new();
                if (gen_mem_trace()) {
                    tcg_gen_movi_tl(t1, 15 - rx + 1);
                    tcg_gen_movi_tl(t2, DATA_WADDR);
                    tcg_gen_movi_tl(t3, ctx->pc);
                    gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
                }
                for (i = rx; i <= 15; i++) {
                    tcg_gen_qemu_st_i32(cpu_R[i], t0, ctx->mem_idx, MO_TEUL);
                    if (gen_mem_trace()) {
                        gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[i]);
                    }
                    tcg_gen_addi_tl(t0, t0, 4);
                }
                break;/*stm*/
            case 0x8:
                rx = insn & 0x000f;
                tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], cpu_c);
                break;/*dect*/
            case 0x9:
                rx = insn & 0x000f;
                tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_c);
                break;/*decf*/
            case 0xa:
                rx = insn & 0x000f;
                tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_c);
                break;/*inct*/
            case 0xb:
                rx = insn & 0x000f;
                tcg_gen_addi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], cpu_c);
                break;/*incf*/
            case 0xc:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();
                tcg_gen_andi_tl(t0, cpu_R[rx], 0xfffffffe);
                store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
                if ((ctx->trace_mode == BRAN_TRACE_MODE)
                        || (ctx->trace_mode == INST_TRACE_MODE)) {
                    tcg_gen_movi_tl(t0, EXCP_CSKY_TRACE);
                    gen_helper_exception(tcg_env, t0);
                }
                ctx->maybe_change_flow = 1;
#endif
                ctx->base.is_jmp = DISAS_JUMP;
                break;/*jmp*/
            case 0xd:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();
                tcg_gen_andi_tl(t0, cpu_R[rx], 0xfffffffe);
                tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
                store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
                if ((ctx->trace_mode == BRAN_TRACE_MODE)
                        || (ctx->trace_mode == INST_TRACE_MODE)) {
                    tcg_gen_movi_tl(t0, EXCP_CSKY_TRACE);
                    gen_helper_exception(tcg_env, t0);
                }
                ctx->maybe_change_flow = 1;
#endif
                ctx->base.is_jmp = DISAS_JUMP;
                break;/*jsr*/
            case 0xe:
                rx = insn & 0x000f;
                gen_helper_ff1(cpu_R[rx], cpu_R[rx]);
                break;/*ff1*/
            case 0xf:
                rx = insn & 0x000f;
                gen_helper_brev(cpu_R[rx], cpu_R[rx]);
                break;/*brev*/
            default:
                goto illegal_op;
            }
            break;
        case 0x1:
            switch (insn_3) {
            case 0x0:
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_R[1], cpu_R[rx], 0x000000ff);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[1], 0);
                break;/*xtrb3*/
            case 0x1:
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_R[1], cpu_R[rx], 0x0000ff00);
                tcg_gen_shri_tl(cpu_R[1], cpu_R[1], 8);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[1], 0);
                break;/*xtrb2*/
            case 0x2:
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_R[1], cpu_R[rx], 0x00ff0000);
                tcg_gen_shri_tl(cpu_R[1], cpu_R[1], 16);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[1], 0);
                break;/*xtrb1*/
            case 0x3:
                rx = insn & 0x000f;
                tcg_gen_shri_tl(cpu_R[1], cpu_R[rx], 24);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[1], 0);
                break;/*xtrb0*/
            case 0x4:
                rx = insn & 0x000f;
                tcg_gen_ext8u_tl(cpu_R[rx], cpu_R[rx]);
                break;/*zextb*/
            case 0x5:
                rx = insn & 0x000f;
                tcg_gen_ext8s_tl(cpu_R[rx], cpu_R[rx]);
                break;/*sextb*/
            case 0x6:
                rx = insn & 0x000f;
                tcg_gen_ext16u_tl(cpu_R[rx], cpu_R[rx]);
                break;/*zexth*/
            case 0x7:
                rx = insn & 0x000f;
                tcg_gen_ext16s_tl(cpu_R[rx], cpu_R[rx]);
                break;/*sexth*/
            case 0x8:
                rx = insn & 0x000f;
                tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rx], 0);
                break;/*declt*/
            case 0x9:
                rx = insn & 0x000f;
                tstnbz(rx);
                break;/*tstnbz*/
            case 0xa:
                rx = insn & 0x000f;
                tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_setcondi_tl(TCG_COND_GT, cpu_c, cpu_R[rx], 0);
                break;/*decgt*/
            case 0xb:
                rx = insn & 0x000f;
                tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rx], 0);
                break;/*decne*/
            case 0xc:
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
                tcg_gen_movi_tl(cpu_R[rx], 0);
                gen_set_label(l1);
                break;/*clrt*/
            case 0xd:
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
                tcg_gen_movi_tl(cpu_R[rx], 0);
                gen_set_label(l1);
                break;/*clrf*/
            case 0xe:
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[rx], 0x80000000, l1);
                tcg_gen_brcondi_tl(TCG_COND_GE, cpu_R[rx], 0, l1);
                tcg_gen_neg_tl(cpu_R[rx], cpu_R[rx]);
                gen_set_label(l1);
                break;/*abs*/
            case 0xf:
                rx = insn & 0x000f;
                tcg_gen_not_tl(cpu_R[rx], cpu_R[rx]);
                break;/*not*/
            default:
                goto illegal_op;
            }
            break;
        case 0x2:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
            tcg_gen_mov_tl(cpu_R[rx], cpu_R[ry]);
            gen_set_label(l1);
            break;/*movt*/
        case 0x3:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_mul_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*mult*/
        case 0x4:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mac(rx, ry);
            break;/*mac*/
        case 0x5:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*subu*/
        case 0x6:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            addc(rx, ry);
            break;/*addc*/
        case 0x7:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            subc(rx, ry);
            break;/*subc*/
        case 0x8:
        case 0x9:
             rx = (insn & 0x1f0) >> 4;
             rz = insn & 0xf;
             t0 = load_cpu_field(cp1.fr[rx]);
             tcg_gen_mov_tl(cpu_R[rz], t0);
            break;/*cprgr-todo*/
        case 0xa:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
            tcg_gen_mov_tl(cpu_R[rx], cpu_R[ry]);
            gen_set_label(l1);
            break;/*movf*/
        case 0xb:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            lsr(rx, ry);
            break;/*lsr*/
        case 0xc:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], cpu_R[ry]);
            break;/*cmphs*/
        case 0xd:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_setcond_tl(TCG_COND_LT, cpu_c, cpu_R[rx], cpu_R[ry]);
            break;/*cmplt*/
        case 0xe:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            t0 = tcg_temp_new();
            tcg_gen_and_tl(t0, cpu_R[rx], cpu_R[ry]);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, t0, 0);
            break;/*tst*/
        case 0xf:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_setcond_tl(TCG_COND_NE, cpu_c, cpu_R[rx], cpu_R[ry]);
            break;/*cmpne*/
        default:
            goto illegal_op;
        }
        break;

    case 0x1:
        switch (insn_2) {
        case 0x0:
#if defined(CONFIG_USER_ONLY)
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
            if (IS_SUPER(ctx)) {
                rz = insn & 0xf;
                rx = (insn & 0x1f0) >> 4;
                gen_mfcr(ctx, rz, rx);
            } else {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }
#endif
            break;/*mfcr*/
        case 0x1:
            if (insn_3 != 0xf) {
#if defined(CONFIG_USER_ONLY)
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                if (IS_SUPER(ctx)) {
                    rz = insn & 0xf;
                    rx = (insn & 0x1f0) >> 4;
                    gen_mfcr(ctx, rz, rx);
                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
#endif
                break;/*mfcr*/
            } else if (insn_3 == 0xf) {
                if (insn_4 >> 3 == 0) {
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_constant_tl(insn & 0x7);
                        gen_helper_psrclr(tcg_env, t0);

                        gen_save_pc(ctx->pc + 2);
                        ctx->base.is_jmp = DISAS_UPDATE;
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*psrclr*/
                } else if (insn_4 >> 3 == 1) {
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_constant_tl(insn & 0x7);
                        gen_helper_psrset(tcg_env, t0);

                        gen_save_pc(ctx->pc + 2);
                        ctx->base.is_jmp = DISAS_UPDATE;
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*psrset*/
                } else {
                    goto illegal_op;
                }
            } else {
                goto illegal_op;
            }
        case 0x2:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_mov_tl(cpu_R[rx], cpu_R[ry]);
            break;/*mov*/
        case 0x3:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            bgenr(rx, ry);
            break;/*bgenr*/
        case 0x4:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_sub_tl(cpu_R[rx], cpu_R[ry], cpu_R[rx]);
            break;/*rsub*/
        case 0x5:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            t0 = tcg_temp_new();
            tcg_gen_shli_tl(t0, cpu_R[ry], 2);
            tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], t0);
            break;/*ixw*/
        case 0x6:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_and_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*and*/
        case 0x7:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_xor_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*xor*/
        case 0x8:
        case 0x9:
#if defined(CONFIG_USER_ONLY)
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
            if (IS_SUPER(ctx)) {
                rx = insn & 0xf;
                rz = (insn & 0x1f0) >> 4;
                gen_mtcr(ctx, rz, rx);
            } else {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }
#endif
            break;/*mtcr*/
        case 0xa:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            asr(rx, ry);
            break;/*asr*/
        case 0xb:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            lsl(rx, ry);
            break;/*lsl*/
        case 0xc:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*addu*/
        case 0xd:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            {
            t0 = tcg_temp_new();
            tcg_gen_shli_tl(t0, cpu_R[ry], 1);
            tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], t0);
            }
            break;/*ixh*/
        case 0xe:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_or_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*or*/
        case 0xf:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_andc_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*andn*/
        default:
            goto illegal_op;
        }
        break;
    case 0x2:
        switch (insn_2) {
        case 0x0:
        case 0x1:
            rx = insn & 0x000f;
            imm = ((insn & 0x01f0) >> 4) + 1;
            tcg_gen_addi_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*addi*/
        case 0x2:
        case 0x3:
            rx = insn & 0x000f;
            imm = ((insn & 0x01f0) >> 4) + 1;
            tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rx], imm);
            break;/*cmplti*/
        case 0x4:
        case 0x5:
            rx = insn & 0x000f;
            imm = ((insn & 0x01f0) >> 4) + 1;
            tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*subi*/
        case 0x6:
        case 0x7:
            rx = (insn & 0x1f0) >> 4;
            rz = insn & 0xf;
            store_cpu_field(cpu_R[rz], cp1.fr[rx]);
            break;/*cpwgr-todo*/
        case 0x8:
        case 0x9:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_subfi_tl(cpu_R[rx], imm, cpu_R[rx]);
            break;/*rsubi*/
        case 0xa:
        case 0xb:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rx], imm);
            break;/*cmpnei*/
        case 0xc:
            switch (insn_3) {
            case 0x0:
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;

                if (imm == 0) {
                    tcg_gen_movi_tl(cpu_R[rx], 0xffffffff);
                } else {
                    tcg_gen_movi_tl(cpu_R[rx], (1 << imm) - 1);
                }

                break;/*bmaski#32*/
            case 0x1:
                rx = insn & 0x000f;
                divu(ctx, rx);
                break;/*divu*/
            case 0x2:
                check_insn(ctx, CPU_620 | ABIV1_DSP);
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_mov_tl(cpu_R[rx], cpu_lo_guard);
                tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_v, 1, l1);
                tcg_gen_mov_tl(cpu_R[rx], cpu_lo);
                gen_set_label(l1);
                break;/*mflos*/
            case 0x3:
                check_insn(ctx, CPU_620 | ABIV1_DSP);
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_mov_tl(cpu_R[rx], cpu_hi_guard);
                tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_v, 1, l1);
                tcg_gen_mov_tl(cpu_R[rx], cpu_hi);
                gen_set_label(l1);
                break;/*mfhis*/
            case 0x4:
                {
                    check_insn(ctx, CPU_620 | ABIV1_DSP);
                    rx = insn & 0x000f;
                    TCGv_i64 t0_64 = tcg_temp_new_i64();
                    tcg_gen_movi_tl(cpu_v, 0);
                    tcg_gen_mov_tl(cpu_lo, cpu_R[rx]);
                    tcg_gen_ext_tl_i64(t0_64, cpu_lo);
                    tcg_gen_shri_i64(t0_64, t0_64, 32);
                    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0_64);
                }
                break;/*mtlo*/
            case 0x5:
                {
                    check_insn(ctx, CPU_620 | ABIV1_DSP);
                    rx = insn & 0x000f;
                    TCGv_i64 t0_64 = tcg_temp_new_i64();
                    tcg_gen_movi_tl(cpu_v, 0);
                    tcg_gen_mov_tl(cpu_hi, cpu_R[rx]);
                    tcg_gen_ext_tl_i64(t0_64, cpu_hi);
                    tcg_gen_shri_i64(t0_64, t0_64, 32);
                    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0_64);
                }
                break;/*mthi*/
            case 0x6:
                check_insn(ctx, CPU_620 | ABIV1_DSP);
                rx = insn & 0x000f;
                tcg_gen_mov_tl(cpu_R[rx], cpu_lo);
                break;/*mflo*/
            case 0x7:
                check_insn(ctx, CPU_620 | ABIV1_DSP);
                rx = insn & 0x000f;
                tcg_gen_mov_tl(cpu_R[rx], cpu_hi);
                break;/*mfhi*/
            case 0x8:
            case 0x9:
            case 0xa:
            case 0xb:
            case 0xc:
            case 0xd:
            case 0xe:
            case 0xf:
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;

                if (imm == 0) {
                    tcg_gen_movi_tl(cpu_R[rx], 0xffffffff);
                } else {
                    tcg_gen_movi_tl(cpu_R[rx], (1 << imm) - 1);
                }

                break;/*bmaski*/
            default:
                goto illegal_op;
            }
            break;
        case 0xd:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;

            if (imm == 0) {
                tcg_gen_movi_tl(cpu_R[rx], 0xffffffff);
            } else {
                tcg_gen_movi_tl(cpu_R[rx], (1 << imm) - 1);
            }
            break;/*bmaski*/
        case 0xe:
        case 0xf:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_andi_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*andi*/
        default:
            goto illegal_op;
        }
        break;
    case 0x3:
        switch (insn_2) {
        case 0x0:
        case 0x1:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_andi_tl(cpu_R[rx], cpu_R[rx], ~(1 << imm));
            break;/*bclri*/
        case 0x2:
            switch (insn_3) {
            case 0x0:
                rx = insn & 0xf;
                store_cpu_field(cpu_R[rx], cp1.fir);
                gen_helper_cpwir(tcg_env);
                gen_save_pc(ctx->pc + 2);
                ctx->base.is_jmp = DISAS_UPDATE;
                break;/*cpwir-todo*/
            case 0x1:
                rx = insn & 0x000f;
                divs(ctx, rx);
                break;/*divs*/
            case 0x2:
#if defined(CONFIG_USER_ONLY)
                rz = insn & 0xf;
                t0 = load_cpu_field(cp1.fsr);
                tcg_gen_mov_tl(cpu_R[rz], t0);
#else
                if (IS_SUPER(ctx)) {
                    rz = insn & 0xf;
                    t0 = load_cpu_field(cp1.fsr);
                    tcg_gen_mov_tl(cpu_R[rz], t0);
                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
#endif
                break;/*cprsr-todo*/
            case 0x3:
#if defined(CONFIG_USER_ONLY)
                rz = insn & 0xf;
                store_cpu_field(cpu_R[rz], cp1.fsr);
#else
                if (IS_SUPER(ctx)) {
                    rz = insn & 0xf;
                    store_cpu_field(cpu_R[rz], cp1.fsr);
                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
#endif
                break;/*cpwsr-todo*/

            case 0x7:
            case 0x8:
            case 0x9:
            case 0xa:
            case 0xb:
            case 0xc:
            case 0xd:
            case 0xe:
            case 0xf:
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_movi_tl(cpu_R[rx], 1 << imm);
                break;/* bgeni */
            default:
                goto illegal_op;
            }
            break;

        case 0x3:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_movi_tl(cpu_R[rx], 1 << imm);
            break;/*bgeni*/
        case 0x4:
        case 0x5:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_ori_tl(cpu_R[rx], cpu_R[rx], 1 << imm);
            break;/*bseti*/
        case 0x6:
        case 0x7:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            t0 = tcg_temp_new();
            tcg_gen_andi_tl(t0, cpu_R[rx], 1 << imm);
            tcg_gen_shri_tl(cpu_c, t0, imm);
            break;/*btsti*/
        case 0x8:
            if (insn_3 == 0) {
                rx = insn & 0x000f;
                xsr(rx);
                break;/*xsr*/
            } else {
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_rotli_tl(cpu_R[rx], cpu_R[rx], imm);
                break;/*rotli*/
            }
        case 0x9:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_rotli_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*rotli*/
        case 0xa:
            if (insn_3 == 0) {
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_c, cpu_R[rx], 0x1);
                tcg_gen_sari_tl(cpu_R[rx], cpu_R[rx], 1);
                break;/*asrc*/
            } else {
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_sari_tl(cpu_R[rx], cpu_R[rx], imm);
                break;/*asri*/
            }
        case 0xb:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_sari_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*asri*/
        case 0xc:
            if (insn_3 == 0) {
                rx = insn & 0x000f;
                tcg_gen_shri_tl(cpu_c, cpu_R[rx], 31);
                tcg_gen_shli_tl(cpu_R[rx], cpu_R[rx], 1);
                break;/*lslc*/
            } else {
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_shli_tl(cpu_R[rx], cpu_R[rx], imm);
                break;/*lsli*/
            }
        case 0xd:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_shli_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*lsli*/
        case 0xe:
            if (insn_3 == 0) {
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_c, cpu_R[rx], 0x1);
                tcg_gen_shri_tl(cpu_R[rx], cpu_R[rx], 1);
                break;/*lsrc*/
            } else {
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_shri_tl(cpu_R[rx], cpu_R[rx], imm);
                break;/*lsri*/
            }
        case 0xf:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_shri_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*lsri*/
        default:
            goto illegal_op;
        }
        break;
    case 0x4:
        switch (insn_2) {
        case 0x0:
            goto illegal_op;
            break;/*omflip0*/
        case 0x1:
            goto illegal_op;
            break;/* omflip1*/
        case 0x2:
            goto illegal_op;
            break;/* omflip2*/
        case 0x3:
            goto illegal_op;
            break;/* omflip3*/

        default:
            goto illegal_op;
        }
        break;
    case 0x5:
        switch (insn_2) {
        case 0x0:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            muls(rx, ry);
            break;/*muls*/
        case 0x1:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulsa(rx, ry);
            break;/*mulsa*/
        case 0x2:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulss(rx, ry);
            break;/*mulss*/
        case 0x4:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulu(rx, ry);
            break;/*mulu*/
        case 0x5:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulua(rx, ry);
            break;/*mulua*/
        case 0x6:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulus(rx, ry);
            break;/*mulus*/
        case 0x8:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulsh(rx, ry);

            break;/*vmulsh*/
        case 0x9:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulsha(rx, ry);

            break;/* vmulsha */
        case 0xa:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulshs(rx, ry);

            break;/* vmulshs */
        case 0xc:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulsw(rx, ry);

            break;/* vmulsw*/
        case 0xd:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulswa(rx, ry);

            break;/* vmulswa */
        case 0xe:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulsws(rx, ry);

            break;/* vmulsws */
        default:
            goto illegal_op;
        }
        break;
    case 0x6:
        switch (insn_2) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            rx = insn & 0x000f;
            imm = (insn & 0x07f0) >> 4;
            tcg_gen_movi_tl(cpu_R[rx], imm);
            break; /*movi*/
        case 0x8:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            t0 = tcg_temp_new();
            tcg_gen_ext16s_tl(cpu_R[rx], cpu_R[rx]);
            tcg_gen_ext16s_tl(t0, cpu_R[ry]);
            tcg_gen_mul_tl(cpu_R[rx], cpu_R[rx], t0);

            break;/*mulsh*/
        case 0x9:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulsha(rx, ry);

            break;/*mulsha*/
        case 0xa:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulshs(rx, ry);

            break;/*mulshs*/
        case 0xb:
#if defined(CONFIG_USER_ONLY)
            /* ck610 has only one coprocessor: fpu, needn't be tested */
            rz = insn & 0x7;
            rx = (insn & 0xf8) >> 3;
            check_insn(ctx, ABIV1_FPU);
            switch (rx) {
            case 0:
                t0 = load_cpu_field(cp1.fpcid);
                break;
            case 1:
                t0 = load_cpu_field(cp1.fcr);
                break;
            case 2:
                t0 = load_cpu_field(cp1.fsr);
                break;
            case 3:
                t0 = load_cpu_field(cp1.fir);
                break;
            case 4:
                t0 = load_cpu_field(cp1.fesr);
                break;
            case 5:
                t0 = load_cpu_field(cp1.feinst1);
                break;
            case 6:
                t0 = load_cpu_field(cp1.feinst2);
                break;
            default:
                tcg_gen_movi_tl(t0, 0);
                break;
            }
            tcg_gen_mov_tl(cpu_R[rz], t0);
#else
            rz = insn & 0x7;
            rx = (insn & 0xf8) >> 3;

            if (ctx->current_cp == 1) {
                check_insn(ctx, ABIV1_FPU);
                switch (rx) {
                case 0:
                    t0 = load_cpu_field(cp1.fpcid);
                    break;
                case 1:
                    t0 = load_cpu_field(cp1.fcr);
                    break;
                case 2:
                    t0 = load_cpu_field(cp1.fsr);
                    break;
                case 3:
                    t0 = load_cpu_field(cp1.fir);
                    break;
                case 4:
                    t0 = load_cpu_field(cp1.fesr);
                    break;
                case 5:
                    t0 = load_cpu_field(cp1.feinst1);
                    break;
                case 6:
                    t0 = load_cpu_field(cp1.feinst2);
                    break;
                default:
                    tcg_gen_movi_tl(t0, 0);
                    break;
                }
                tcg_gen_mov_tl(cpu_R[rz], t0);
                break;
            }

            if (!IS_SUPER(ctx)) {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }

            if (ctx->current_cp == 15) {
                check_insn(ctx, CSKY_MMU);
                gen_cprcr_cp15(ctx, rz, rx);
            }
#endif
            break;/*cprcr-todo*/

        case 0xc:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulsw(rx, ry);

            break;/*mulsw*/
        case 0xd:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulswa(rx, ry);

            break;/*mulswa*/
        case 0xe:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulsws(rx, ry);

            break;/*mulsws*/
        case 0xf:
#if defined(CONFIG_USER_ONLY)
            /* ck610 has only one coprocessor: fpu, so needn't be test */
            rx = insn & 0x7;
            rz = (insn & 0xf8) >> 3;
            store_cpu_field(cpu_R[rz], cp1.fcr);
#else
            rx = insn & 0x7;
            rz = (insn & 0xf8) >> 3;

            if (!IS_SUPER(ctx)) {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }

            if (ctx->current_cp == 15) {
                check_insn(ctx, CSKY_MMU);
                gen_cpwcr_cp15(ctx, rz, rx);
            } else if (ctx->current_cp == 1) {
                store_cpu_field(cpu_R[rz], cp1.fcr);
            }
#endif
            break;/*cpwcr-todo*/
        default:
            goto illegal_op;
        }
        break;

    case 0x7:
        if ((insn_2 > 0) && (insn_2 < 15)) {
            target_ulong addr;
            t0 = tcg_temp_new();
            t3 = tcg_temp_new();

            disp = insn & 0x00ff;
            rz = (insn & 0x0f00) >> 8;
            addr = (ctx->pc + 2 + (disp << 2)) & 0xfffffffc ;
            tcg_gen_movi_tl(t0, addr);
            tcg_gen_qemu_ld_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUL);
            if (gen_mem_trace()) {
                tcg_gen_movi_tl(t3, ctx->pc);
                gen_helper_trace_ld32u(tcg_env, t3, cpu_R[rz], t0);
            }

            break;/*lrw*/
        } else if (insn_2 == 0) {
            target_ulong addr;
            t0 = tcg_temp_new();
            t1 = tcg_temp_new();
            t3 = tcg_temp_new();

            disp = insn & 0x00ff;
            addr = (ctx->pc + 2 + (disp << 2)) & 0xfffffffc ;
            tcg_gen_movi_tl(t1, addr);
#if defined(CONFIG_USER_ONLY)
            addr = cpu_ldl_code(env, addr);
            if (gen_mem_trace()) {
                tcg_gen_movi_tl(t0, addr);
                tcg_gen_movi_tl(t3, ctx->pc);
                gen_helper_trace_ld32u(tcg_env, t3, t0, t1);
            }
            gen_goto_tb(ctx, 0, addr);
            ctx->base.is_jmp = DISAS_TB_JUMP;
#else
            tcg_gen_movi_tl(t0, addr);
            tcg_gen_qemu_ld_i32(t0, t0, ctx->mem_idx, MO_TEUL);
            if (gen_mem_trace()) {
                tcg_gen_movi_tl(t3, ctx->pc);
                gen_helper_trace_ld32u(tcg_env, t3, t0, t1);
            }
            tcg_gen_andi_tl(t0, t0, 0xfffffffe);
            store_cpu_field(t0, pc);

            if ((ctx->trace_mode == BRAN_TRACE_MODE)
                    || (ctx->trace_mode == INST_TRACE_MODE)) {
                tcg_gen_movi_tl(t0, EXCP_CSKY_TRACE);
                gen_helper_exception(tcg_env, t0);
            }
            ctx->maybe_change_flow = 1;
            ctx->base.is_jmp = DISAS_JUMP;
#endif
            break;/*jmpi*/
        } else if (insn_2 == 15) {
            target_ulong addr;
            t0 = tcg_temp_new();
            t1 = tcg_temp_new();
            t3 = tcg_temp_new();

            disp = insn & 0x00ff;
            addr = (ctx->pc + 2 + (disp << 2)) & 0xfffffffc;
            tcg_gen_movi_tl(t1, addr);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
#if defined(CONFIG_USER_ONLY)
            addr = cpu_ldl_code(env, addr);
            if (gen_mem_trace()) {
                tcg_gen_movi_tl(t0, addr);
                tcg_gen_movi_tl(t3, ctx->pc);
                gen_helper_trace_ld32u(tcg_env, t3, t0, t1);
            }
            gen_goto_tb(ctx, 0, addr);
            ctx->base.is_jmp = DISAS_TB_JUMP;
#else
            tcg_gen_movi_tl(t0, addr);
            tcg_gen_qemu_ld_i32(t0, t0, ctx->mem_idx, MO_TEUL);
            if (gen_mem_trace()) {
                tcg_gen_movi_tl(t3, ctx->pc);
                gen_helper_trace_ld32u(tcg_env, t3, t0, t1);
            }
            tcg_gen_andi_tl(t0, t0, 0xfffffffe);
            store_cpu_field(t0, pc);

            if ((ctx->trace_mode == BRAN_TRACE_MODE)
                    || (ctx->trace_mode == INST_TRACE_MODE)) {
                tcg_gen_movi_tl(t0, EXCP_CSKY_TRACE);
                gen_helper_exception(tcg_env, t0);
            }
            ctx->maybe_change_flow = 1;
            ctx->base.is_jmp = DISAS_JUMP;
#endif
            break;/*jsri*/
        } else {
            goto illegal_op;
        }
    case 0x8:
        rx = insn & 0x000f;
        imm = ((insn & 0x00f0) >> 4) << 2;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        t3 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_ld_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            tcg_gen_movi_tl(t3, ctx->pc);
            gen_helper_trace_ld32u(tcg_env, t3, cpu_R[rz], t0);
        }
        break;/*ld.w*/
    case 0x9:
        rx = insn & 0x000f;
        imm = ((insn & 0x00f0) >> 4) << 2;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        t3 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_st_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            tcg_gen_movi_tl(t3, ctx->pc);
            gen_helper_trace_st32(tcg_env, t3, cpu_R[rz], t0);
        }
        break;/*st.w*/
    case 0xa:
        rx = insn & 0x000f;
        imm = (insn & 0x00f0) >> 4;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        t3 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_ld_i32(cpu_R[rz], t0, ctx->mem_idx, MO_UB);
        if (gen_mem_trace()) {
            tcg_gen_movi_tl(t3, ctx->pc);
            gen_helper_trace_ld8u(tcg_env, t3, cpu_R[rz], t0);
        }
        break;/*ld.b*/
    case 0xb:
        rx = insn & 0x000f;
        imm = (insn & 0x00f0) >> 4;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        t3 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_st_i32(cpu_R[rz], t0, ctx->mem_idx, MO_UB);
        if (gen_mem_trace()) {
            tcg_gen_movi_tl(t3, ctx->pc);
            gen_helper_trace_st32(tcg_env, t3, cpu_R[rz], t0);
        }
        break;/*st.b*/
    case 0xc:
        rx = insn & 0x000f;
        imm = ((insn & 0x00f0) >> 4) << 1;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        t3 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_ld_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUW);
        if (gen_mem_trace()) {
            tcg_gen_movi_tl(t3, ctx->pc);
            gen_helper_trace_ld16u(tcg_env, t3, cpu_R[rz], t0);
        }
        break;/*ld.h*/
    case 0xd:
        rx = insn & 0x000f;
        imm = ((insn & 0x00f0) >> 4) << 1;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        t3 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_st_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUW);
        if (gen_mem_trace()) {
            tcg_gen_movi_tl(t3, ctx->pc);
            gen_helper_trace_st16(tcg_env, t3, cpu_R[rz], t0);
        }

        break;/*st.h*/
    case 0xe:
        if (insn_2 >> 3 == 0) {
            disp = insn & 0x07ff;

            bt(ctx, disp);
            ctx->base.is_jmp = DISAS_TB_JUMP;
            break;/*bt*/
        } else if (insn_2 >> 3 == 1) {
            disp = insn & 0x07ff;

            bf(ctx, disp);
            ctx->base.is_jmp = DISAS_TB_JUMP;
            break;/*bf*/
        } else {
            goto illegal_op;
        }
    case 0xf:
        if (insn_2 >> 3 == 0) {
            disp = insn & 0x07ff;

            br(ctx, disp);
            ctx->base.is_jmp = DISAS_TB_JUMP;
            break;/*br*/
        } else if (insn_2 >> 3 == 1) {
            disp = insn & 0x07ff;

            bsr(ctx, disp);
            ctx->base.is_jmp = DISAS_TB_JUMP;
            break;/*bsr*/
        } else {
            goto illegal_op;
        }
    default:

illegal_op:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void csky_trace_tb_start(CPUCSKYState *env, const TranslationBlock *tb)
{
    uint32_t tb_pc = (uint32_t)tb->pc;
    TCGv t0 = tcg_constant_tl(tb_pc);
    gen_helper_trace_tb_start(tcg_env, t0);
}

static void csky_trace_tb_exit(uint32_t subtype, uint32_t offset)
{
    TCGv t0 = tcg_constant_tl(subtype);
    TCGv t1 = tcg_constant_tl(offset);

    gen_helper_trace_tb_exit(t0, t1);
}

static void csky_tb_start_tb(CPUCSKYState *env, const TranslationBlock *tb)
{
    uint32_t tb_pc = (uint32_t)tb->pc;
    TCGv t0 = tcg_constant_tl(tb_pc);
    gen_helper_tb_trace(tcg_env, t0);
}

static void csky_dump_tb_map(DisasContextBase *dcbase)
{
    uint32_t tb_pc = (uint32_t)dcbase->pc_first;
    uint32_t tb_end = (uint32_t)dcbase->pc_next;
    uint32_t icount = dcbase->num_insns;

    qemu_log_mask(CPU_TB_TRACE, "tb_map: 0x%.8x 0x%.8x %d\n",
                  tb_pc, tb_end, icount);
}

static TCGOp *jcount_start_insn;
static void gen_csky_jcount_start(DisasContext *dc, CPUCSKYState *env)
{
    CPUState *cs = env_cpu(env);
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();
    /* We emit a movi with a dummy immediate argument. Keep the insn index
     * of the movi so that we later (when we know the actual insn count)
     * can update the immediate argument with the actual insn count.  */
    tcg_gen_movi_i32(t1, 0xdeadbeef);
    jcount_start_insn = tcg_last_op();

    tcg_gen_movi_tl(t0, dc->pc);
    if (env->jcount_start != 0) {
        gen_helper_jcount(tcg_env, t0, t1);
    }
    if (cs->csky_trace_features & CSKY_TRACE) {
        gen_helper_csky_trace_icount(tcg_env, t0, t1);
    }
}

static void gen_csky_jcount_end(int num_insns)
{
    tcg_set_insn_param(jcount_start_insn, 1,
                       tcgv_i32_arg(tcg_constant_i32(num_insns)));
}

static bool trace_match_range(CPUCSKYState *env, uint32_t pc)
{
    bool match = false;
    uint32_t insn = cpu_lduw_code(env, pc);
    uint32_t mask = (insn & 0xc000) != 0xc000
        ? 0xfffffffe : 0xfffffffc;
    match  = trace_range_test(env, pc, mask);
    return match;
}

static void csky_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUCSKYState *env = cpu_env(cs);
#ifndef CONFIG_USER_ONLY
    const TranslationBlock *tb = dcbase->tb;
#endif

    dc->pc = dc->base.pc_first;
    dc->features = env->features;

#ifndef CONFIG_USER_ONLY
    dc->super = CSKY_TBFLAG_PSR_S(tb->flags);
    dc->current_cp = CSKY_TBFLAG_CPID(tb->flags);
    dc->trace_mode = (TraceMode)CSKY_TBFLAG_PSR_TM(tb->flags);
#endif

#ifdef CONFIG_USER_ONLY
    dc->mem_idx = CSKY_USERMODE;
#else
    dc->mem_idx = dc->super;
#endif

    dc->next_page_start =
        (dc->base.pc_first & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
}

static void csky_tr_tb_start(DisasContextBase *dcbase, CPUState *cpu)
{
    CPUCSKYState *env = cpu_env(cpu);
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    const TranslationBlock *tb = dcbase->tb;

    dc->idly4_counter = env->idly4_counter;
    dc->condexec_cond = env->sce_condexec_bits;

    if (tfilter.enable) {
        dc->trace_match = trace_match_range(env, dc->pc);
    } else {
        dc->trace_match = false;
    }

    if (env->jcount_start != 0 || cpu->csky_trace_features & CSKY_TRACE) {
        gen_csky_jcount_start(dc, env);
    }

    if (gen_tb_trace()) {
        csky_trace_tb_start(env, tb);
    }

    if (env->tb_trace == 1 || env->pctrace == 1) {
        csky_tb_start_tb(env, tb);
    }
}

static void csky_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(dc->pc);
}

//static bool csky_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cpu,
//                                     const CPUBreakpoint *bp)
//{
//    DisasContext *dc = container_of(dcbase, DisasContext, base);
//    if (bp->flags & BP_ANY) {
//        if (gen_tb_trace()) {
//            csky_trace_tb_exit(0x1, dc->pc - dc->base.pc_first);
//        }
//        generate_exception(dc, EXCP_DEBUG);
//        /* The address covered by the breakpoint must be included in
//           [tb->pc, tb->pc + tb->size) in order to for it to be
//           properly cleared -- thus we increment the PC here so that
//           the generic logic setting tb->size later does the right thing.  */
//        dc->pc += 2;
//        return true;
//    } else {
//        return false;
//    }
//}

static bool csky_pre_translate_insn(DisasContext *dc, CPUState *cpu)
{
    CPUCSKYState *env = cpu_env(cpu);
#ifndef CONFIG_USER_ONLY
    dc->cannot_be_traced = 0;
    dc->maybe_change_flow = 0;
#endif

    if (env->exit_addr != 0) {
        if (env->exit_addr == dc->pc) {
            generate_exception(dc, EXCP_CSKY_EXIT);
            return true;
        }
    }

    if ((!dc->trace_match) && tfilter.enable) {
        if (unlikely(trace_match_range(env, dc->pc))) {
            gen_save_pc(dc->pc);
            dc->base.is_jmp = DISAS_UPDATE;
            dc->base.num_insns--;
            return true;
        }
    }
    return false;
}

static void csky_post_translate_insn(DisasContext *dc)
{
    TCGv_i32 t0;
    dc->base.pc_next = dc->pc;

    /* update idly4 in env. */
    if (dc->idly4_counter != 0) {
        dc->idly4_counter--;
        t0 = tcg_constant_tl(dc->idly4_counter);
        store_cpu_field(t0, idly4_counter);
        gen_save_pc(dc->pc);
        dc->base.is_jmp = DISAS_UPDATE;
    }

#ifndef CONFIG_USER_ONLY
    if (dc->cannot_be_traced) {
        return;
    }
    if (dc->trace_mode == INST_TRACE_MODE) {
        if (!dc->maybe_change_flow) {
            generate_exception(dc, EXCP_CSKY_TRACE);
        }
    }
#endif
    return;
}

static void csky_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUCSKYState *env = cpu_env(cpu);

    if (csky_pre_translate_insn(dc, cpu)) {
        return;
    }

    dc->insn = cpu_lduw_code(env, dc->pc);

    /* 16 bit instruction*/
    disas_csky_v1_insn(env, dc);
    dc->pc += 2;

    csky_post_translate_insn(dc);
}

static void csky_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUCSKYState *env = cpu_env(cpu);

    dc->base.pc_next = dc->pc;

    /* send TB_EXIT trace when tb exit in special situation. */
    if (gen_tb_trace()) {
        if (cpu->singlestep_enabled) {
            /* exit on singlestep. */
            csky_trace_tb_exit(0x1, dc->pc - dc->base.pc_first);
        } else if (dc->base.is_jmp == DISAS_NEXT
            || dc->base.is_jmp == DISAS_UPDATE) {
            /* exit on special insns. */
            csky_trace_tb_exit(0x2, dc->pc - dc->base.pc_first);
        } else if (dc->base.is_jmp == DISAS_TOO_MANY) {
            /* exit on too many insns. */
            csky_trace_tb_exit(0x3, dc->pc - dc->base.pc_first);
        }
    }
    if (dc->base.is_jmp != DISAS_TB_JUMP && dc->base.is_jmp != DISAS_TB_JUMP) {
        if (dcbase->num_insns == 1) {
            gen_helper_tag_pctrace(tcg_env, tcg_constant_tl(dc->base.pc_first));
        }
    }
    if (unlikely(cpu->singlestep_enabled)) {
       /* in singstep max_insns=1, often cause DISAS_TOO_MANY. */
        if (dc->base.is_jmp == DISAS_NEXT
            || dc->base.is_jmp == DISAS_TOO_MANY) {
            generate_exception(dc, EXCP_DEBUG);
        } else if (dc->base.is_jmp != DISAS_TB_JUMP) {
            TCGv t0 = tcg_constant_tl(EXCP_DEBUG);
            gen_helper_exception(tcg_env, t0);
        }
    } else {
        switch (dc->base.is_jmp) {
        case DISAS_NEXT:
        case DISAS_TOO_MANY:
            gen_goto_tb(dc, 1, dc->pc);
            break;
        case DISAS_JUMP:
            /* indicate that the hash table must be used to find the next TB */
            tcg_gen_lookup_and_goto_ptr();
            break;
        case DISAS_UPDATE:
            /* special insns like mtcr/wait/rfi
            * indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(NULL, 0);
            break;
        case DISAS_NORETURN:
            /* exception */
        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        default:
            /* indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(NULL, 0);
            break;
        }
    }

    if (env->jcount_start != 0 || cpu->csky_trace_features & CSKY_TRACE) {
        gen_csky_jcount_end(dcbase->num_insns);
    }

    if (env->tb_trace == 1) {
        csky_dump_tb_map(dcbase);
    }
}

static void csky_tr_disas_log(const DisasContextBase *dcbase,
                              CPUState *cpu, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps csky_translator_ops = {
    .init_disas_context = csky_tr_init_disas_context,
    .tb_start           = csky_tr_tb_start,
    .insn_start         = csky_tr_insn_start,
    //.breakpoint_check   = csky_tr_breakpoint_check,
    .translate_insn     = csky_tr_translate_insn,
    .tb_stop            = csky_tr_tb_stop,
    .disas_log          = csky_tr_disas_log,
};

/*
 * generate intermediate code in tcg_ctx.gen_opc_buf and gen_opparam_buf for
 * basic block 'tb'. If search_pc is TRUE, also generate PC
 * information for each intermediate instruction.
 */
void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int *max_insns,
                           vaddr pc, void *host_pc)
{
    DisasContext ctx;
    translator_loop(cs, tb, max_insns, pc, host_pc, &csky_translator_ops, &ctx.base);
}

void csky_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    int i;
    uint32_t psr;
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;

    psr = (env->psr_s << 31) | (env->psr_tm << 14) | (env->cp0.psr) |
           (env->psr_c);

    for (i = 0; i < 16; i++) {
        qemu_fprintf(f, "R%02d=%08x", i, env->regs[i]);
        if ((i % 4) == 3) {
            qemu_fprintf(f, "\n");
        } else {
            qemu_fprintf(f, " ");
        }
    }

    qemu_fprintf(f, "psr=%08x ", psr);
    qemu_fprintf(f, "pc=%08x\n", env->pc);
    qemu_fprintf(f, "epsr=%08x ", env->cp0.epsr);
    qemu_fprintf(f, "epc=%08x\n", env->cp0.epc);
    qemu_fprintf(f, "hi=%08x ", env->hi);
    qemu_fprintf(f, "lo=%08x ", env->lo);
    qemu_fprintf(f, "hi_guard=%08x ", env->hi_guard);
    qemu_fprintf(f, "lo_guard=%08x\n", env->lo_guard);
}
