/*
 * C-SKY translation
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
#include <math.h>
#include "exec/gdbstub.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H


/*******************************/
#define sp 14
#define SVBR 30
#define FP 23
#define TOP 24
#define BSP 25
#define WORD_MASK 0x3
#define REG_MASK   0x1c
/*******************************************/

/* is_jmp field values */
#define DISAS_JUMP    DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_UPDATE  DISAS_TARGET_1 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP DISAS_TARGET_2 /* only pc was modified statically */

static TCGv_i32 cpu_R[32];
static TCGv_i32 cpu_c;
static TCGv_i32 cpu_v;
static TCGv_i32 cpu_hi ;
static TCGv_i32 cpu_lo;
static TCGv_i32 cpu_hi_guard;
static TCGv_i32 cpu_lo_guard;

static TCGv_i32 cpu_F0s, cpu_F1s;
static TCGv_i64 cpu_F0d, cpu_F1d;
TCGv_i32 excl_val;
TCGv_i32 excl_addr;

static const char *regnames[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "sp", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31" };

#if defined(CONFIG_USER_ONLY)
#define IS_SUPER(ctx)   0
#define IS_TRUST(ctx)   0
#else
#define IS_SUPER(ctx)   (ctx->super)
#define IS_TRUST(ctx)   (ctx->trust)
#endif

static inline void csky_concat_i32_i64(TCGv_i64 dest, TCGv_i32 first,
                                        TCGv_i32 second)
{
#if TARGET_BIG_ENDIAN
    tcg_gen_concat_i32_i64(dest, second, first);
#else
    tcg_gen_concat_i32_i64(dest, first, second);
#endif
}

static inline void csky_extr_i64_i32(TCGv_i32 first, TCGv_i32 second,
                                        TCGv_i64 arg)
{
#if TARGET_BIG_ENDIAN
    tcg_gen_extr_i64_i32(second, first, arg);
#else
    tcg_gen_extr_i64_i32(first, second, arg);
#endif

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

static inline void mulsw(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t2 = tcg_temp_new();

    tcg_gen_ext16s_tl(t2, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t2);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);

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

/* initialize TCG globals. */
void csky_translate_init(void)
{
    int i;

    for (i = 0; i < 32; i++) {
        cpu_R[i] = tcg_global_mem_new_i32(tcg_env,
            offsetof(CPUCSKYState, regs[i]),
            regnames[i]);
    }

    cpu_c = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, psr_c), "cpu_c");
    cpu_v = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, dcsr_v), "cpu_v");
    cpu_hi = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, hi), "cpu_hi");
    cpu_lo = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, lo), "cpu_lo");
    cpu_hi_guard = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, hi_guard), "cpu_hi_guard");
    cpu_lo_guard = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, lo_guard), "cpu_lo_guard");
    excl_addr = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, excl_addr), "excl_addr");
    excl_val = tcg_global_mem_new_i32(tcg_env,
        offsetof(CPUCSKYState, excl_val), "excl_val");
}

static TCGv_i32 new_tmp(void)
{
    return tcg_temp_new_i32();
}

static void dead_tmp(TCGv tmp)
{
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
    TCGv t0 = tcg_constant_tl(pc);
    store_cpu_field(t0, pc);

}

static void csky_trace_tb_start(CPUCSKYState *env, const TranslationBlock *tb)
{
    uint32_t tb_pc = (uint32_t)tb->pc;
    TCGv t0;

    t0 = tcg_constant_tl(tb_pc);
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
    TCGv t0;

    t0 = tcg_constant_tl(tb_pc);
    gen_helper_tb_trace(tcg_env, t0);
}

static inline void generate_exception(DisasContext *ctx, int excp)
{
    TCGv t0;

    print_exception(ctx, excp);

    if (excp == EXCP_CSKY_INT_EXIT) {
        if (current_cpu->singlestep_enabled) {
            excp = EXCP_DEBUG;
            if (gen_tb_trace()) {
                csky_trace_tb_exit(0x1, ctx->pc - ctx->base.pc_first);
            }
        } else {
            if (gen_tb_trace()) {
                csky_trace_tb_exit(0x2, ctx->pc - ctx->base.pc_first);
            }
        }
    }
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
        if ((ctx->insn & 0xc0000000) != 0xc0000000) {
            csky_trace_tb_exit(0x1, ctx->pc + 2 - ctx->base.pc_first);
        } else {
            csky_trace_tb_exit(0x1, ctx->pc + 4 - ctx->base.pc_first);
        }
        gen_save_pc(dest);
        t0 = tcg_constant_tl(EXCP_DEBUG);
        gen_helper_exception(tcg_env, t0);
        /* do exit on singlestep in csky_tr_tb_stop. */
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

#define ldst(name, suf, rx, rz, imm, isize, mop)                   \
do {                                                               \
    if (ctx->bctm) {                                               \
        TCGLabel *l1 = gen_new_label();                            \
        tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rx], 0, l1);         \
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + isize);               \
        tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);                       \
        store_cpu_field(t0, pc);                                   \
        tcg_gen_exit_tb(NULL, 0);                                  \
        gen_set_label(l1);                                         \
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);                       \
        tcg_gen_qemu_##name(cpu_R[rz], t0, ctx->mem_idx, mop);     \
        if (gen_mem_trace()) {                                     \
            TCGv_i32 t2 = tcg_constant_i32(ctx->pc);               \
            gen_helper_trace_##suf(tcg_env, t2, cpu_R[rz], t0);    \
        }                                                          \
        gen_goto_tb(ctx, 1, ctx->pc + isize);                      \
        ctx->base.is_jmp = DISAS_TB_JUMP;                          \
    } else {                                                       \
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);                       \
        tcg_gen_qemu_##name(cpu_R[rz], t0, ctx->mem_idx, mop);     \
        if (gen_mem_trace()) {                                     \
            TCGv_i32 t2 = tcg_constant_i32(ctx->pc);               \
            gen_helper_trace_##suf(tcg_env, t2, cpu_R[rz], t0);    \
        }                                                          \
    }                                                              \
} while (0)

#define ldrstr(name, suf, rx, ry, rz, imm, mop)                    \
do {                                                               \
    if (ctx->bctm) {                                               \
        TCGLabel *l1 = gen_new_label();                            \
        tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rx], 0, l1);         \
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);                   \
        tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);                       \
        store_cpu_field(t0, pc);                                   \
        tcg_gen_exit_tb(NULL, 0);                                  \
        gen_set_label(l1);                                         \
        tcg_gen_shli_tl(t0, cpu_R[ry], imm);                       \
        tcg_gen_add_tl(t0, cpu_R[rx], t0);                         \
        tcg_gen_qemu_##name(cpu_R[rz], t0, ctx->mem_idx, mop);     \
        if (gen_mem_trace()) {                                     \
            TCGv_i32 t2 = tcg_constant_i32(ctx->pc);               \
            gen_helper_trace_##suf(tcg_env, t2, cpu_R[rz], t0);    \
        }                                                          \
        gen_goto_tb(ctx, 1, ctx->pc + 4);                          \
        ctx->base.is_jmp = DISAS_TB_JUMP;                          \
        break;                                                     \
    } else {                                                       \
        tcg_gen_shli_tl(t0, cpu_R[ry], imm);                       \
        tcg_gen_add_tl(t0, cpu_R[rx], t0);                         \
        tcg_gen_qemu_##name(cpu_R[rz], t0, ctx->mem_idx, mop);     \
        if (gen_mem_trace()) {                                     \
            TCGv_i32 t2 = tcg_constant_i32(ctx->pc);               \
            gen_helper_trace_##suf(tcg_env, t2, cpu_R[rz], t0);    \
        }                                                          \
    }                                                              \
} while (0)

/* The insn support the cpu contained in the argument 'flags'.  */
static inline void check_insn(DisasContext *ctx, uint64_t flags)
{
    if (unlikely(!has_insn(ctx, flags))) {
        generate_exception(ctx, EXCP_CSKY_UDEF);
    }
}

/* The insn not support the cpu contained in the argument 'flags'.  */
static inline void check_insn_except(DisasContext *ctx, uint64_t flags)
{
    if (unlikely(has_insn(ctx, flags))) {
        generate_exception(ctx, EXCP_CSKY_UDEF);
    }
}

/* Set a variable to the value of a CPU register.  */
static void load_reg_var(DisasContext *s, TCGv var, int reg)
{
    tcg_gen_mov_i32(var, cpu_R[reg]);
}

/* Create a new temporary and set it to the value of a CPU register.  */
static inline TCGv load_reg(DisasContext *s, int reg)
{
    TCGv tmp = new_tmp();
    load_reg_var(s, tmp, reg);
    return tmp;
}

static inline void addc(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_add_tl(t0, t1, cpu_R[ry]);
    tcg_gen_add_tl(cpu_R[rz], t0, cpu_c);
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    tcg_gen_setcond_tl(TCG_COND_LTU, cpu_c, cpu_R[rz], t1);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_setcond_tl(TCG_COND_LEU, cpu_c, cpu_R[rz], t1);
    gen_set_label(l2);

}

static inline void subc(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    tcg_gen_subfi_tl(t0, 1, cpu_c);
    tcg_gen_sub_tl(t1, cpu_R[rx], cpu_R[ry]);
    tcg_gen_sub_tl(t2, t1, t0);
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    tcg_gen_setcond_tl(TCG_COND_GTU, cpu_c, cpu_R[rx], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], cpu_R[ry]);
    gen_set_label(l2);
    tcg_gen_mov_tl(cpu_R[rz], t2);


}


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

static inline void lsl(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    TCGLabel *l1 = gen_new_label();

    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_shl_tl(cpu_R[rz], t1, t0);
    gen_set_label(l1);

}

static inline void lsr(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    TCGLabel *l1 = gen_new_label();

    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_shr_tl(cpu_R[rz], t1, t0);
    gen_set_label(l1);

}

static inline void asr(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();

    TCGLabel *l1 = gen_new_label();
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_brcondi_tl(TCG_COND_LEU, t0, 31, l1);
    tcg_gen_movi_tl(t0, 31);
    gen_set_label(l1);
    tcg_gen_sar_tl(cpu_R[rz], cpu_R[rx], t0);

}

static inline void rotl(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    TCGLabel *l1 = gen_new_label();

    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_rotl_tl(cpu_R[rz], t1, t0);
    gen_set_label(l1);

}

static inline void branch16(DisasContext *ctx, TCGCond cond, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();

    val = offset << 1;
    if (val & 0x400) {
        val |= 0xfffffc00;
    }
    val += ctx->pc;

    tcg_gen_brcondi_tl(cond, cpu_c, 0, l1);
    gen_goto_tb(ctx, 1, ctx->pc + 2);
    gen_set_label(l1);

    gen_goto_tb(ctx, 0, val);

    ctx->base.is_jmp = DISAS_TB_JUMP;
}

static inline void bsr16(DisasContext *ctx, int offset)
{
    int val;

    val = offset << 1;
    if (val & 0x400) {
        val |= 0xfffffc00;
    }
    val += ctx->pc;

    gen_goto_tb(ctx, 0, val);

    ctx->base.is_jmp = DISAS_TB_JUMP;
}

static inline void pop16(DisasContext *ctx, int imm)
{
    int i;
    TCGv t0 = tcg_temp_new();
    tcg_gen_mov_tl(t0, cpu_R[sp]);

    TCGv t1 = tcg_temp_new();
    tcg_gen_movi_tl(t1, 0);

    TCGv t2 = tcg_constant_i32(DATA_RADDR);
    TCGv t3 = tcg_constant_i32(ctx->pc);
    if (imm & 0x10) {
        tcg_gen_addi_tl(t1, t1, 1);
    }
    if (imm & 0xf) {
        tcg_gen_addi_tl(t1, t1, imm & 0xf);
    }
    if (gen_mem_trace()) {
        gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
    }
    if (imm & 0xf) {
        for (i = 0; i < (imm & 0xf); i++) {
            tcg_gen_qemu_ld_i32(cpu_R[i + 4], t0, ctx->mem_idx, MO_TEUL);
            if (gen_mem_trace()) {
                gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[i + 4]);
            }
            tcg_gen_addi_i32(t0, t0, 4);
        }
    }
    if (imm & 0x10) {
        tcg_gen_qemu_ld_i32(cpu_R[15], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[15]);
        }
        tcg_gen_addi_i32(t0, t0, 4);
    }
    tcg_gen_mov_tl(cpu_R[sp], t0);

    tcg_gen_andi_tl(t0, cpu_R[15], 0xfffffffe);
    store_cpu_field(t0, pc);
    ctx->base.is_jmp = DISAS_JUMP;
}

static inline void push16(DisasContext *ctx, int imm)
{
    int i;
    TCGv t0 = tcg_temp_new();
    tcg_gen_mov_tl(t0, cpu_R[sp]);

    TCGv t1 = tcg_temp_new();
    tcg_gen_movi_tl(t1, 0);

    TCGv t2 = tcg_constant_i32(DATA_WADDR);

    TCGv t3 = tcg_constant_i32(ctx->pc);
    if (imm & 0x10) {
        tcg_gen_addi_tl(t1, t1, 1);
    }
    if (imm & 0xf) {
        tcg_gen_addi_tl(t1, t1, imm & 0xf);
    }
    if (gen_mem_trace()) {
        gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
    }
    if (imm & 0x10) {
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st_i32(cpu_R[15], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[15]);
        }
    }

    if (imm & 0xf) {
        for (i = (imm & 0xf); i > 0; i--) {
            tcg_gen_subi_i32(t0, t0, 4);
            tcg_gen_qemu_st_i32(cpu_R[i + 3], t0, ctx->mem_idx, MO_TEUL);
            if (gen_mem_trace()) {
                gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[i + 3]);
            }
        }
    }
    tcg_gen_mov_tl(cpu_R[sp], t0);
}

static inline void gen_cmp16(DisasContext *ctx, uint32_t sop, int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* cmphs */
        tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], cpu_R[rz]);
        break;
    case 0x1:
        /* cmplt */
        tcg_gen_setcond_tl(TCG_COND_LT, cpu_c, cpu_R[rx], cpu_R[rz]);
        break;
    case 0x2:
        /* cmpne */
        tcg_gen_setcond_tl(TCG_COND_NE, cpu_c, cpu_R[rx], cpu_R[rz]);
        break;
    case 0x3:
        /* mvcv */
        tcg_gen_subfi_tl(cpu_R[rz], 1, cpu_c);
        break;
    default:
        break;
    }
}

static inline void gen_logic_and16(DisasContext *ctx, uint32_t sop,
                                   int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* and */
        tcg_gen_and_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x1:
        /* andn */
        tcg_gen_andc_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x2:
        /* tst */
        {
            TCGv t0 = tcg_temp_new();
            tcg_gen_and_tl(t0, cpu_R[rx], cpu_R[rz]);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, t0, 0);

        }
        break;
    case 0x3:
        /* tstnbz */
        tstnbz(rx);
        break;
    default:
        break;
    }
}

static inline void gen_logic_or16(DisasContext *ctx, uint32_t sop,
                                  int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* or */
        tcg_gen_or_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x1:
        /* xor */
        tcg_gen_xor_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x2:
        /* nor */
        tcg_gen_nor_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x3:
        /* mov */
        tcg_gen_mov_tl(cpu_R[rz], cpu_R[rx]);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void gen_shift_reg16(DisasContext *ctx, uint32_t sop,
                                   int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* lsl */
        lsl(rz, rz, rx);
        break;
    case 0x1:
        /* lsr */
        lsr(rz, rz, rx);
        break;
    case 0x2:
        /* asr */
        asr(rz, rz, rx);
        break;
    case 0x3:
        /* rotl */
        rotl(rz, rz, rx);
        break;
    default:
        break;
    }
}

static inline void gen_ext16(DisasContext *ctx, uint32_t sop, int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* zextb */
        tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0xff);
        break;
    case 0x1:
        /* zexth */
        tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0xffff);
        break;
    case 0x2:
        /* sextb */
        tcg_gen_shli_tl(cpu_R[rz], cpu_R[rx], 24);
        tcg_gen_sari_tl(cpu_R[rz], cpu_R[rz], 24);
        break;
    case 0x3:
        /* sexth */
        tcg_gen_shli_tl(cpu_R[rz], cpu_R[rx], 16);
        tcg_gen_sari_tl(cpu_R[rz], cpu_R[rz], 16);
        break;
    default:
        break;
    }
}

static inline void gen_arith_misc16(DisasContext *ctx, uint32_t sop,
                                    int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* jmp */
        {
            TCGv t0 = tcg_temp_new();
            tcg_gen_andi_tl(t0, cpu_R[rx], 0xfffffffe);
            store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
            if ((ctx->trace_mode == BRAN_TRACE_MODE)
                || (ctx->trace_mode == INST_TRACE_MODE)) {
                tcg_gen_movi_i32(t0, EXCP_CSKY_TRACE);
                gen_helper_exception(tcg_env, t0);
            }
            ctx->maybe_change_flow = 1;
#endif
            ctx->base.is_jmp = DISAS_JUMP;
        }
        break;
    case 0x1:
        /* jsr */
        {
            TCGv t0 = tcg_temp_new();
            tcg_gen_andi_tl(t0, cpu_R[rx], 0xfffffffe);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
            store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
            if ((ctx->trace_mode == BRAN_TRACE_MODE)
                || (ctx->trace_mode == INST_TRACE_MODE)) {
                tcg_gen_movi_i32(t0, EXCP_CSKY_TRACE);
                gen_helper_exception(tcg_env, t0);
            }
            ctx->maybe_change_flow = 1;
#endif
            ctx->base.is_jmp = DISAS_JUMP;
            break;
        }
        break;
    case 0x2:
        /* revb */
        check_insn_except(ctx, CPU_E801);
        tcg_gen_bswap32_tl(cpu_R[rz], cpu_R[rx], TCG_BSWAP_OZ);
        break;
    case 0x3:
        /* revh */
        {
            check_insn_except(ctx, CPU_E801);
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();

            tcg_gen_bswap32_tl(t0, cpu_R[rx], TCG_BSWAP_OZ);
            tcg_gen_shri_tl(t1, t0, 16);
            tcg_gen_shli_tl(t0, t0, 16);
            tcg_gen_or_tl(cpu_R[rz], t0, t1);

        }
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void gen_mul16(DisasContext *ctx, uint32_t sop, int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* mult */
        tcg_gen_mul_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x1:
        /* mulsh */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        {
            TCGv t0 = tcg_temp_new();

            tcg_gen_ext16s_tl(t0, cpu_R[rx]);
            tcg_gen_ext16s_tl(cpu_R[rz], cpu_R[rz]);
            tcg_gen_mul_tl(cpu_R[rz], cpu_R[rz], t0);

        }
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_branch16(DisasContext *ctx, uint32_t op, int offset)
{
    switch (op) {
    case 0x0:
        if (offset == 0) {
            /*bkpt16*/
            if (is_gdbserver_start) {
                generate_exception(ctx, EXCP_DEBUG);
            } else {
                TCGv_i32 t0 = tcg_constant_i32(ctx->pc);
                gen_helper_bkpt_exit(tcg_env, t0);
            }
#if !defined(CONFIG_USER_ONLY)
            ctx->cannot_be_traced = 1;
#endif
        } else if (!has_insn(ctx, ABIV2_ELRW)) {
            /*bsr16*/
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
            bsr16(ctx, offset);
        } else {
            /* lrw16 extend */
            unsigned int imm, rz, addr;
            TCGv t0 = tcg_temp_new();

            imm = ((ctx->insn & 0x300) >> 3) | (ctx->insn & 0x1f);
            imm = (~imm & 0x7f) | 0x80;
            rz = (ctx->insn >> 5) & 0x7;
            addr = (ctx->pc + (imm << 2)) & 0xfffffffc ;
            tcg_gen_movi_tl(t0, addr);
            tcg_gen_qemu_ld_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUL);
        }
        break;
    case 0x1:
        /*br16*/
        {
            int val;

            val = offset << 1;
            if (val & 0x400) {
                val |= 0xfffffc00;
            }
            val += ctx->pc;

            gen_goto_tb(ctx, 0, val);

            ctx->base.is_jmp = DISAS_TB_JUMP;
        }
        break;
    case 0x2:
        /*bt16*/
        branch16(ctx, TCG_COND_NE, offset);
        break;
    case 0x3:
        /*bf16*/
        branch16(ctx, TCG_COND_EQ, offset);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void gen_nvic_insn(DisasContext *ctx, uint32_t op, int imm)
{
    int i;
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1;
    tcg_gen_mov_i32(t0, cpu_R[sp]);

    switch (imm) {
    case 0x0:
        /* nie */
        t1 = load_cpu_field(cp0.epc);
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st_i32(t1, t0, ctx->mem_idx, MO_TEUL);
        t1 = load_cpu_field(cp0.epsr);
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st_i32(t1, t0, ctx->mem_idx, MO_TEUL);
        tcg_gen_mov_i32(cpu_R[sp], t0);
        t1 = load_cpu_field(cp0.psr);
        tcg_gen_ori_i32(t1, t1, PSR_EE_MASK);
        tcg_gen_ori_i32(t1, t1, PSR_IE_MASK);
        store_cpu_field(t1, cp0.psr);
        break;
    case 0x1:
        /* nir */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t1 = tcg_temp_new_i32();
            tcg_gen_qemu_ld_i32(t1, t0, ctx->mem_idx, MO_TEUL);
            store_cpu_field(t1, cp0.epsr);
            tcg_gen_addi_i32(t0, t0, 4);
            tcg_gen_qemu_ld_i32(t1, t0, ctx->mem_idx, MO_TEUL);
            store_cpu_field(t1, cp0.epc);
            tcg_gen_addi_i32(t0, t0, 4);
            tcg_gen_mov_i32(cpu_R[sp], t0);
            tcg_gen_movi_i32(t0, 0);
            store_cpu_field(t0, idly4_counter);
            gen_helper_rte(tcg_env);
            ctx->base.is_jmp = DISAS_UPDATE;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x2:
        /* ipush */
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st_i32(cpu_R[13], t0, ctx->mem_idx, MO_TEUL);
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st_i32(cpu_R[12], t0, ctx->mem_idx, MO_TEUL);
        for (i = 3; i >= 0; i--) {
            tcg_gen_subi_i32(t0, t0, 4);
            tcg_gen_qemu_st_i32(cpu_R[i], t0, ctx->mem_idx, MO_TEUL);
        }
        tcg_gen_mov_i32(cpu_R[sp], t0);
        break;
    case 0x3:
        /* ipop */
        for (i = 0; i <= 3; i++) {
            tcg_gen_qemu_ld_i32(cpu_R[i], t0, ctx->mem_idx, MO_TEUL);
            tcg_gen_addi_i32(t0, t0, 4);
        }
        tcg_gen_qemu_ld_i32(cpu_R[12], t0, ctx->mem_idx, MO_TEUL);
        tcg_gen_addi_i32(t0, t0, 4);
        tcg_gen_qemu_ld_i32(cpu_R[13], t0, ctx->mem_idx, MO_TEUL);
        tcg_gen_addi_i32(t0, t0, 4);
        tcg_gen_mov_i32(cpu_R[sp], t0);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_imm7_arith16(DisasContext *ctx, uint32_t op, uint16_t sop,
                             int imm)
{
    switch (sop) {
    case 0x0:
        /*addisp(2)*/
        tcg_gen_addi_tl(cpu_R[sp], cpu_R[sp], imm << 2);
        break;
    case 0x1:
        /*subisp*/
        tcg_gen_subi_tl(cpu_R[sp], cpu_R[sp], imm << 2);
        break;
    case 0x3:
        check_insn(ctx, CPU_E801 | CPU_E802 | CPU_E803);
        gen_nvic_insn(ctx, op, imm);
        break;
    case 0x4:
        /*pop16*/
        if (ctx->bctm) {
            TCGv t0 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[sp], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(NULL, 0);
            gen_set_label(l1);
            pop16(ctx, imm & 0x1f);
            break;
        } else {
            pop16(ctx, imm & 0x1f);
            break;
        }
    case 0x6:
        /*push16*/
        if (ctx->bctm) {
            TCGv t0 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[sp], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(NULL, 0);
            gen_set_label(l1);
            push16(ctx, imm & 0x1f);
            gen_goto_tb(ctx, 1, ctx->pc + 2);
            ctx->base.is_jmp = DISAS_TB_JUMP;
            break;
        } else {
            push16(ctx, imm & 0x1f);
            break;
        }
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_imm8_arith16(DisasContext *ctx, uint32_t op, int rz, int imm)
{
    switch (op) {
    case 0x6:
    case 0x7:
        /*addisp(1)*/
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[sp], imm << 2);
        break;
    case 0x8:
    case 0x9:
        /*addi16(1)*/
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[rz], imm + 1);
        break;
    case 0xa:
    case 0xb:
        /*subi16(1)*/
        tcg_gen_subi_tl(cpu_R[rz], cpu_R[rz], imm + 1);
        break;
    case 0xc:
    case 0xd:
        /*movi16*/
        tcg_gen_movi_tl(cpu_R[rz], imm);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_imm5_arith16(DisasContext *ctx, uint32_t op, uint16_t sop,
                             int rx, int imm)
{
    switch (sop) {
    case 0x0:
        /*cmphsi16*/
        tcg_gen_setcondi_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], imm + 1);
        break;
    case 0x1:
        /*cmplti16*/
        tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rx], imm + 1);
        break;
    case 0x2:
        /*cmpnei16*/
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rx], imm);
        break;
    case 0x4:
        /*bclri16*/
        tcg_gen_andi_tl(cpu_R[rx], cpu_R[rx], ~(1 << imm));
        break;
    case 0x5:
        /*bseti16*/
        tcg_gen_ori_tl(cpu_R[rx], cpu_R[rx], 1 << imm);
        break;
    case 0x7:
        /* jmpix16 */
        check_insn(ctx, ABIV2_JAVA);
        if (ctx->bctm) {
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();
            /*
            tcg_gen_addi_tl(t0, cpu_R[rx], 0xff);
            tcg_gen_and_tl(t0, t0, (16 + ((imm & 0x3) << 3)));
            tcg_gen_add_tl(t0, cpu_R[SVBR], t0);
            */
            tcg_gen_andi_tl(t0, cpu_R[rx], 0xff);
            switch (imm & 0x3) {
            case 0x0:
                tcg_gen_shli_tl(t0, t0, 4);
                break;
            case 0x1:
                tcg_gen_shli_tl(t1, t0, 4);
                tcg_gen_shli_tl(t0, t0, 3);
                tcg_gen_add_tl(t0, t0, t1);
                break;
            case 0x2:
                tcg_gen_shli_tl(t0, t0, 5);
                break;
            case 0x3:
                tcg_gen_shli_tl(t1, t0, 5);
                tcg_gen_shli_tl(t0, t0, 3);
                tcg_gen_add_tl(t0, t0, t1);
                break;
            default:
                break;
            }
            tcg_gen_add_tl(t0, cpu_R[SVBR], t0);
            store_cpu_field(t0, pc);

            ctx->base.is_jmp = DISAS_JUMP;
        }
        break;
    case 0x6:
        if (has_insn(ctx, ABIV2_ELRW)) {
            /* btsti16 */
            tcg_gen_andi_tl(cpu_c, cpu_R[rx], 1 << imm);
            tcg_gen_shri_tl(cpu_c, cpu_c, imm);
        }
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_reg1_arith16(DisasContext *ctx, uint32_t op, int rz,
                             int rx, int imm)
{
    switch (op) {
    case 0x10:
    case 0x11:
        /*lsli16*/
        tcg_gen_shli_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    case 0x12:
    case 0x13:
        /*lsri16*/
        tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    case 0x14:
    case 0x15:
        /*asri16*/
        tcg_gen_sari_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_reg3_arith16(DisasContext *ctx, uint32_t op, uint16_t sop,
                             int rz, int rx, int ry, int imm)
{
    switch (sop) {
    case 0x0:
        /*addu16(2)*/
        tcg_gen_add_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case 0x1:
        /*subu16(2)*/
        tcg_gen_sub_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case 0x2:
        /*addi16(2)*/
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[rx], imm + 1);
        break;
    case 0x3:
        /*subi16(2)*/
        tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], imm + 1);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_reg2_arith16(DisasContext *ctx, uint32_t op, uint16_t sop,
                             int rx, int rz)
{
    switch (op) {
    case 0x18:
        switch (sop) {
        case 0x0:
            /*addu16(1)*/
            tcg_gen_add_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
            break;
        case 0x1:
            /*addc16*/
            addc(rz, rz, rx);
            break;
        case 0x2:
            /*subu16(1)*/
            tcg_gen_sub_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
            break;
        case 0x3:
            /*subc16*/
            subc(rz, rz, rx);
            break;
        default:
            generate_exception(ctx, EXCP_CSKY_UDEF);
            break;
        }
        break;
    case 0x19:
        gen_cmp16(ctx, sop, rz, rx);
        break;
    case 0x1a:
        gen_logic_and16(ctx, sop, rz, rx);
        break;
    case 0x1b:
        gen_logic_or16(ctx, sop, rz, rx);
        break;
    case 0x1c:
        gen_shift_reg16(ctx, sop, rz, rx);
        break;
    case 0x1d:
        gen_ext16(ctx, sop, rz, rx);
        break;
    case 0x1e:
        gen_arith_misc16(ctx, sop, rz, rx);
        break;
    case 0x1f:
        gen_mul16(ctx, sop, rz, rx);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_ldst16(DisasContext *ctx, uint32_t op, int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_new();

    switch (op) {
    case 0x20:
    case 0x21:
        /*ld.b16*/
        ldst(ld_i32, ld8u, rx, rz, imm, 2, MO_UB);
        break;
    case 0x22:
    case 0x23:
        /*ld.h16*/
        ldst(ld_i32, ld16u, rx, rz, imm << 1, 2, MO_TEUW);
        break;
    case 0x24:
    case 0x25:
        /*ld.w16*/
        ldst(ld_i32, ld32u, rx, rz, imm << 2, 2, MO_TEUL);
        break;
    case 0x28:
    case 0x29:
        /*st.b16*/
        ldst(st_i32, st8, rx, rz, imm, 2, MO_UB);
        break;
    case 0x2a:
    case 0x2b:
        /*st.h16*/
        ldst(st_i32, st16, rx, rz, imm << 1, 2, MO_ALIGN | MO_TEUW);
        break;
    case 0x2c:
    case 0x2d:
        /*st.w16*/
        ldst(st_i32, st32, rx, rz, imm << 2, 2, MO_TEUL);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }


}

static void disas_csky_16_insn(CPUCSKYState *env, DisasContext *ctx)
{
    unsigned int insn, rz, rx, ry;
    uint32_t op, sop;
    int imm;
    target_ulong addr;
    TCGv t0;

    insn = ctx->insn;

    op = (insn >> 10) & 0x3f;
    switch (op) {
    case 0x0 ... 0x3:
        imm = insn & 0x3ff;
        gen_branch16(ctx, op, imm);
        break;
    case 0x4:
        /*lrw16*/
        t0 = tcg_temp_new();
        TCGv_i32 t3 = tcg_constant_i32(ctx->pc);
        imm = ((insn & 0x300) >> 3) | (insn & 0x1f);
        rz = (insn >> 5) & 0x7;
        addr = (ctx->pc + (imm << 2)) & 0xfffffffc ;
        tcg_gen_movi_tl(t0, addr);
        tcg_gen_qemu_ld_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_ld32u(tcg_env, t3, cpu_R[rz], t0);
        }
        break;
    case 0x5:
        imm = ((insn >> 3) & 0x60) | (insn & 0x1f);
        sop = (insn >> 5) & 0x7;
        gen_imm7_arith16(ctx, op, sop, imm);
        break;
    case 0x6 ... 0xd:
        imm = insn & 0xff;
        rz = (insn >> 8) & 0x7;
        gen_imm8_arith16(ctx, op, rz, imm);
        break;
    case 0xe:
    case 0xf:
        imm = insn & 0x1f;
        sop = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_imm5_arith16(ctx, op, sop, rx, imm);
        break;
    case 0x10 ... 0x15:
        imm = insn & 0x1f;
        rz = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_reg1_arith16(ctx, op, rz, rx, imm);
        break;
    case 0x16:
    case 0x17:
        sop = insn & 0x3;
        imm = (insn >> 2) & 0x7;
        ry = (insn >> 2) & 0x7;
        rz = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_reg3_arith16(ctx, op, sop, rz, rx, ry, imm);
        break;
    case 0x18 ... 0x1f:
        sop = insn & 0x3;
        rx = (insn >> 2) & 0xf;
        rz = (insn >> 6) & 0xf;
        gen_reg2_arith16(ctx, op, sop, rx, rz);
        break;
    case 0x20 ... 0x25:
        imm = insn & 0x1f;
        rz = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_ldst16(ctx, op, rz, rx, imm);
        break;
    case 0x26:
    case 0x27:
        /*ld16.w(sp)*/
        t0 = tcg_temp_new();
        imm = ((insn & 0x700) >> 3) | (insn & 0x1f);
        rz = (insn >> 5) & 0x7;
        ldst(ld_i32, ld32u, sp, rz, imm << 2, 2, MO_TEUL);
        break;
    case 0x28 ... 0x2d:
        imm = insn & 0x1f;
        rz = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_ldst16(ctx, op, rz, rx, imm);
        break;
    case 0x2e:
    case 0x2f:
        /*st16.w(sp)*/
        t0 = tcg_temp_new();
        imm = ((insn & 0x700) >> 3) | (insn & 0x1f);
        rz = (insn >> 5) & 0x7;
        ldst(st_i32, st32, sp, rz, imm << 2, 2, MO_TEUL);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void bnezad(DisasContext *ctx, int rx, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();

    val = offset << 1;
    if (val & 0x10000) {
        val |= 0xffff0000;
    }
    val += ctx->pc;

    tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);

    tcg_gen_brcondi_tl(TCG_COND_GTU, cpu_R[rx], 0, l1);
    gen_goto_tb(ctx, 1, ctx->pc + 4);
    gen_set_label(l1);
    gen_goto_tb(ctx, 0, val);

    ctx->base.is_jmp = DISAS_TB_JUMP;
}

static inline void bnerai(DisasContext *ctx, int rx, int ry, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();

    val = offset << 1;
    if (val & 0x10000) {
        val |= 0xffff0000;
    }
    val += ctx->pc;

    tcg_gen_addi_tl(cpu_R[rx], cpu_R[rx], 1);
    tcg_gen_brcond_tl(TCG_COND_NE, cpu_R[rx], cpu_R[ry], l1);
    gen_goto_tb(ctx, 1, ctx->pc + 4);
    gen_set_label(l1);
    gen_goto_tb(ctx, 0, val);

    ctx->base.is_jmp = DISAS_TB_JUMP;
}

static inline void branch32(DisasContext *ctx, TCGCond cond, int rx, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();

    val = offset << 1;
    if (val & 0x10000) {
        val |= 0xffff0000;
    }
    val += ctx->pc;

    if (rx != -1) {
        tcg_gen_brcondi_tl(cond, cpu_R[rx], 0, l1);
    } else {
        tcg_gen_brcondi_tl(cond, cpu_c, 0, l1);
    }
    gen_goto_tb(ctx, 1, ctx->pc + 4);
    gen_set_label(l1);

    gen_goto_tb(ctx, 0, val);

    ctx->base.is_jmp = DISAS_TB_JUMP;
}

static inline void bsr32(DisasContext *ctx, int offset)
{
    int val;

    val = offset << 1;
    if (val & 0x4000000) {
        val |= 0xfc000000;
    }
    val += ctx->pc;

    gen_goto_tb(ctx, 0, val);

    ctx->base.is_jmp = DISAS_TB_JUMP;
}

static inline void sce(DisasContext *ctx, int cond)
{
    TCGv t0 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_movi_tl(t0, cond);
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    tcg_gen_not_tl(t0, t0);
    tcg_gen_andi_tl(t0, t0, 0xf);
    gen_set_label(l1);
    tcg_gen_ori_tl(t0, t0, 0x10);
    store_cpu_field(t0, sce_condexec_bits);

    gen_save_pc(ctx->pc + 4);
    ctx->base.is_jmp = DISAS_UPDATE;
}

#ifndef CONFIG_USER_ONLY
static inline void gen_mfcr_cpu(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
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
        gen_helper_mfcr_cpidr(cpu_R[rz], tcg_env);
        break;
    case 0xe:
        /* dcsr */
        t0 = load_cpu_field(cp0.dcsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

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
        if (ctx->features & ABIV2_TEE) {
            gen_helper_tee_mfcr_cr19(cpu_R[rz], tcg_env);
        } else {
            t0 = load_cpu_field(cp0.capr);
            tcg_gen_mov_tl(cpu_R[rz], t0);
        }
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
    case 0x1c:
        /* rvbr */
        gen_helper_mfcr_cr28(cpu_R[rz], tcg_env);
        break;
    case 0x1d:
        /* rmr */
        gen_helper_mfcr_cr29(cpu_R[rz], tcg_env);
        break;
    case 0x1e:
        gen_helper_mfcr_cr30(cpu_R[rz], tcg_env);
        break;
    case 0x1f:
        gen_helper_mfcr_cr31(cpu_R[rz], tcg_env);
        break;
    default:
        break;
    }
}

static inline void gen_mfcr_tee(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
{
    TCGv t0;
    switch (cr_num) {
    case 0x0:
        /* NT_PSR */
        t0 = load_cpu_field(tee.nt_psr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1:
        /* NT_VBR */
        t0 = load_cpu_field(tee.nt_vbr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x2:
        /* NT_EPSR */
        t0 = load_cpu_field(tee.nt_epsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x4:
        /* NT_EPC */
        t0 = load_cpu_field(tee.nt_epc);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x6:
        /* NT_SSP */
        t0 = load_cpu_field(stackpoint.nt_ssp);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x7:
        /* T_USP */
        t0 = load_cpu_field(stackpoint.t_usp);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x8:
        /* T_DCR */
        t0 = load_cpu_field(tee.t_dcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x9:
        /* T_PCR */
        t0 = load_cpu_field(tee.t_pcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0xa:
        /* NT_EBR */
        t0 = load_cpu_field(tee.nt_ebr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;

    default:
        break;
    }
}

/* Read MMU Coprocessor Contronl Registers */
static inline void
gen_mfcr_mmu(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
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
    case 0x1c:
        /* CP15_CR28 */
        t0 = load_cpu_field(mmu.mpgd0);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1d:
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
        t0 = tcg_temp_new();
        break;
    }
}

static inline void gen_mtcr_cpu(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
{

    switch (cr_num) {
    case 0x0:
        /* psr */
        gen_helper_mtcr_cr0(tcg_env, cpu_R[rx]);
        ctx->pc += 4;
        generate_exception(ctx, EXCP_CSKY_INT_EXIT);
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
        store_cpu_field(cpu_R[rx], cp0.dcsr);
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
        break;
    case 0x13:
        /* capr */
        if (ctx->features & ABIV2_TEE) {
            gen_helper_tee_mtcr_cr19(tcg_env, cpu_R[rx]);
        } else {
            store_cpu_field(cpu_R[rx], cp0.capr);
        }
        break;
    case 0x14:
        /* pacr */
        gen_helper_mtcr_cr20(tcg_env, cpu_R[rx]);
        break;
    case 0x15:
        /* prsr */
        store_cpu_field(cpu_R[rx], cp0.prsr);
        break;
    case 0x1c:
        /* rvbr */
        gen_helper_mtcr_cr28(tcg_env, cpu_R[rx]);
        break;
    case 0x1d:
        /* rmr */
        gen_helper_mtcr_cr29(tcg_env, cpu_R[rx]);
        break;
    case 0x1f:
        /* chr */
        gen_helper_mtcr_cr31(tcg_env, cpu_R[rx]);
    default:
        break;
    }
}

static inline void gen_mtcr_tee(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
{
    TCGv t0 = tcg_temp_new();
    switch (cr_num) {
    case 0x0:
        /* NT_PSR */
        if (!(ctx->features & ABIV2_JAVA)) {
            tcg_gen_andi_tl(t0, cpu_R[rx], ~0x400);
            store_cpu_field(t0, tee.nt_psr);
        } else {
            store_cpu_field(cpu_R[rx], tee.nt_psr);
        }
        break;
    case 0x1:
        /* NT_VBR */
        tcg_gen_andi_tl(t0, cpu_R[rx], ~0x3ff);
        store_cpu_field(t0, tee.nt_vbr);
        break;
    case 0x2:
        /* NT_EPSR */
        store_cpu_field(cpu_R[rx], tee.nt_epsr);
        break;
    case 0x4:
        /* NT_EPC */
        store_cpu_field(cpu_R[rx], tee.nt_epc);
        break;
    case 0x6:
        /* NT_SSP */
        store_cpu_field(cpu_R[rx], stackpoint.nt_ssp);
        break;
    case 0x7:
        /* T_USP */
        store_cpu_field(cpu_R[rx], stackpoint.t_usp);
        break;
    case 0x8:
        /* T_DCR */
        tcg_gen_andi_tl(t0, cpu_R[rx], 0x3);
        store_cpu_field(t0, tee.t_dcr);
        break;
    case 0x9:
        /* T_PCR */
        tcg_gen_andi_tl(t0, cpu_R[rx], 0x1);
        store_cpu_field(t0, tee.t_pcr);
        break;
    case 0xa:
        /* NT_EBR */
        store_cpu_field(cpu_R[rx], tee.nt_ebr);
        break;

    default:
        break;
    }
}

static inline void gen_mtcr_mmu(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
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
        gen_save_pc(ctx->pc + 4);
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
        gen_save_pc(ctx->pc + 4);
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
    case 0x1c:
        /* CP15_CR28 */
        store_cpu_field(cpu_R[rx], mmu.mpgd0);
        gen_save_pc(ctx->pc + 4);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x1d:
        store_cpu_field(cpu_R[rx], mmu.mpgd1);
        gen_save_pc(ctx->pc + 4);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x1e:
        store_cpu_field(cpu_R[rx], mmu.msa0);
        gen_save_pc(ctx->pc + 4);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x1f:
        store_cpu_field(cpu_R[rx], mmu.msa1);
        gen_save_pc(ctx->pc + 4);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    default:
        break;
    }
}

static inline void gen_mtcr_mptimer(DisasContext *ctx,
    uint32_t rz, uint32_t rx)
{
    int pos = rz;
    TCGv t0;
    translator_io_start(&ctx->base);
    switch (pos) {
    case 0x0:
        /* PTIM_CTLR */
        store_cpu_field(cpu_R[rx], mptimer.ctlr);
        break;
    case 0x1:
        /* PTIM_TSR */
        store_cpu_field(cpu_R[rx], mptimer.isr);
        break;
    case 0x4:
        /* PTIM_CMVR_HI */
        store_cpu_field(cpu_R[rx], mptimer.cmvr_hi);
        break;
    case 0x5:
        /* PTIM_CMVR_LO */
        store_cpu_field(cpu_R[rx], mptimer.cmvr_lo);
        break;
    case 0x6:
        /* PTIM_CMVR_LVR */
        store_cpu_field(cpu_R[rx], mptimer.lvr);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "wrong mtcr control register: pc=%x rx=%d\n",
                      ctx->pc, rx);
        break;
    }

    t0 = tcg_constant_i32(pos);
    gen_helper_mtcr_mptimer(tcg_env, t0, cpu_R[rx]);
}

static inline void gen_mfcr_mptimer(DisasContext *ctx, uint32_t rz, uint32_t rx)
{
    int pos = rx;
    TCGv t0, t1;
    translator_io_start(&ctx->base);
    switch (pos) {
        case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5:
        /* PTIM_CTLR/PTIM_ISR/PTIM_CCVR/PTIM_CMVR */
        t1 = tcg_temp_new();
        t0 = tcg_constant_i32(pos);
        gen_helper_mfcr_mptimer(t1, tcg_env, t0);
        tcg_gen_mov_i32(cpu_R[rz], t1);
        break;
    case 0x1f: /* INTC_BAR */
        tcg_gen_movi_i32(cpu_R[rz], 0xfffe0000);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "wrong mfcr control register: pc=%x rx=%d\n",
                      ctx->pc, rx);
        break;
    }
}
#endif /* !CONFIG_USER_ONLY */

static inline void gen_mfcr_vfp(DisasContext *ctx,  int rz, int rx)
{
    TCGv t0;

    switch (rx) {
    case 0x0:
        /* fid*/
        t0 = load_cpu_field(vfp.fid);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1:
        /* fcr*/
        t0 = load_cpu_field(vfp.fcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x2:
        /* fesr*/
        t0 = load_cpu_field(vfp.fesr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "wrong mfcr vfp control register: pc=%x rx=%d\n",
                      ctx->pc, rx);
        break;
    }
}

static inline void gen_mfcr_tp(DisasContext *ctx, int rz, int rx)
{
    switch (rz) {
    case 0x0:
        /*load_cpu_field(cpu_R[rx], cp13.tcr);*/
        break;
    default:
        break;
    }
}
static inline void gen_mtcr_tp(DisasContext *ctx, int rz, int rx)
{
    switch (rz) {
    case 0x0:
        store_cpu_field(cpu_R[rx], cp13.tcr);
        gen_helper_trace_update_tcr(tcg_env);
        gen_save_pc(ctx->pc + 4);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x1:
        store_cpu_field(cpu_R[rx], cp13.ter);
        break;
    case 0x2:
        store_cpu_field(cpu_R[rx], cp13.addr_cmpr_config[0]);
        break;
    case 0x3:
        store_cpu_field(cpu_R[rx], cp13.addr_cmpr[0]);
        break;
    case 0x4:
        store_cpu_field(cpu_R[rx], cp13.addr_cmpr[1]);
        break;
    case 0x5:
        store_cpu_field(cpu_R[rx], cp13.data_cmpr_config[0]);
        break;
    case 0x6:
        store_cpu_field(cpu_R[rx], cp13.data_cmpr[0]);
        break;
    case 0x7:
        store_cpu_field(cpu_R[rx], cp13.data_cmpr[1]);
        break;
    case 0x8:
        store_cpu_field(cpu_R[rx], cp13.asid);
        break;
    default:
        break;
    }
}

static inline void gen_mtcr_vfp(DisasContext *ctx,  int rz, int rx)
{
    switch (rz) {
    case 0x1:
        /* store_cpu_field(cpu_R[rx], vfp.fcr); */
        store_cpu_field(cpu_R[rx], vfp.fcr);
        gen_helper_vfp_update_fcr(tcg_env);
        gen_save_pc(ctx->pc + 4);
        ctx->base.is_jmp = DISAS_UPDATE;
        break;
    case 0x2:
        store_cpu_field(cpu_R[rx], vfp.fesr);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "wrong mfcr vfp control register: pc=%x rz=%d\n",
                      ctx->pc, rz);
        break;
    }
}

static inline void add_ix(int rz, int rx, int ry, int imm)
{
    TCGv t0 = tcg_temp_new();

    tcg_gen_shli_tl(t0, cpu_R[ry], imm);
    tcg_gen_add_tl(cpu_R[rz], cpu_R[rx], t0);

}

static inline void lslc(int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_movi_tl(t1, imm);
    tcg_gen_mov_tl(t0, cpu_R[rx]);
    tcg_gen_andi_tl(cpu_c, t0, 0x1);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 32, l1);
    tcg_gen_shl_tl(cpu_R[rz], t0, t1);
    tcg_gen_rotl_tl(t2, t0, t1);
    tcg_gen_andi_tl(cpu_c, t2, 0x1);
    gen_set_label(l1);

}

static inline void lsrc(int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_movi_tl(t1, imm);
    tcg_gen_mov_tl(t0, cpu_R[rx]);
    tcg_gen_andi_tl(cpu_c, t0, 0x80000000);
    tcg_gen_shri_tl(cpu_c, cpu_c, 31);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 32, l1);
    tcg_gen_shr_tl(cpu_R[rz], t0, t1);
    tcg_gen_shri_tl(t2, t0, imm - 1);
    tcg_gen_andi_tl(cpu_c, t2, 0x1);
    gen_set_label(l1);

}

static inline void asrc(int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_movi_tl(t1, imm);
    tcg_gen_mov_tl(t0, cpu_R[rx]);
    tcg_gen_sari_tl(cpu_R[rz], t0, 31);
    tcg_gen_andi_tl(cpu_c, cpu_R[rz], 0x1);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 32, l1);
    tcg_gen_sar_tl(cpu_R[rz], t0, t1);
    tcg_gen_shri_tl(t2, t0, imm - 1);
    tcg_gen_andi_tl(cpu_c, t2, 0x1);
    gen_set_label(l1);

}

static inline void divu(DisasContext *ctx, int rz, int rx, int ry)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[ry], 0, l1);
    tcg_gen_divu_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv t0 = tcg_temp_new();

    tcg_gen_movi_tl(t0, EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(tcg_env, t0);
    ctx->base.is_jmp = DISAS_NEXT;


    gen_set_label(l2);
}

static inline void divs(DisasContext *ctx, int rz, int rx, int ry)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[ry], 0, l1);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_div_i64(t0, t0, t1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv t2 = tcg_temp_new();

    tcg_gen_movi_tl(t2, EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(tcg_env, t2);
    ctx->base.is_jmp = DISAS_NEXT;


    gen_set_label(l2);
}

static inline int csky_log2(uint32_t s)
{
    int i = 0;
    if (s == 0) {
        return -1;
    }

    while (s != 1) {
        s >>= 1;
        i++;
    }
    return i;
}
#define ldbistbi(name, suf, rx, rz, imm, mop)                   \
do {                                                            \
    tcg_gen_qemu_##name(cpu_R[rz], cpu_R[rx], ctx->mem_idx, mop);    \
    if (gen_mem_trace()) {                                      \
        TCGv_i32 t2_32;                       \
        t2_32 = tcg_constant_i32(ctx->pc);                            \
        gen_helper_trace_##suf(tcg_env, t2_32, cpu_R[rz], cpu_R[rx]);    \
    }                                                           \
    tcg_gen_addi_tl(cpu_R[rx], cpu_R[rx], imm);                 \
} while (0)

#define ldbirstbir(name, suf, rx, rz, ry, mop)                  \
do {                                                            \
    tcg_gen_mov_tl(t0, cpu_R[ry]);                              \
    tcg_gen_qemu_##name(cpu_R[rz], cpu_R[rx], ctx->mem_idx, mop);    \
    if (gen_mem_trace()) {                                      \
        TCGv_i32 t2_32;                       \
        t2_32 = tcg_constant_i32(ctx->pc);                            \
        gen_helper_trace_##suf(tcg_env, t2_32, cpu_R[rz], cpu_R[rx]);    \
    }                                                           \
    tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], t0);                   \
} while (0)

static inline void dspv2_insn_pldbi_d(DisasContext *s, int rz, int rx)
{
    /* Rz[31:0] <- mem(Rx)
     * Rz+1[31:0] <- mem(Rx + 4)
     * Rx[31:0] <- Rx[31:0] + 8 */
    TCGv_i32 t2 = tcg_constant_i32(s->pc);
    tcg_gen_qemu_ld_i32(cpu_R[rz], cpu_R[rx], s->mem_idx, MO_TEUL);
    if (gen_mem_trace()) {
        gen_helper_trace_ld32u(tcg_env, t2, cpu_R[rz], cpu_R[rx]);
    }
    tcg_gen_addi_i32(cpu_R[rx], cpu_R[rx], 4);
    tcg_gen_qemu_ld_i32(cpu_R[(rz + 1) % 32], cpu_R[rx], s->mem_idx, MO_TEUL);
    if (gen_mem_trace()) {
        gen_helper_trace_ld32u(tcg_env, t2, cpu_R[(rz + 1) % 32], cpu_R[rx]);
    }
    tcg_gen_addi_i32(cpu_R[rx], cpu_R[rx], 4);
}

static inline void dspv2_insn_pldbir_d(DisasContext *s, int rz, int rx, int ry)
{
    /* Rz[31:0] <- mem(Rx)
     * Rz+1[31:0] <- mem(Rx + Ry)
     * Rx[31:0] <- Rx[31:0] + 2*Ry */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_constant_i32(s->pc);
    tcg_gen_mov_i32(t0, cpu_R[ry]);
    tcg_gen_qemu_ld_i32(cpu_R[rz], cpu_R[rx], s->mem_idx, MO_TEUL);
    if (gen_mem_trace()) {
        gen_helper_trace_ld32u(tcg_env, t2, cpu_R[rz], cpu_R[rx]);
    }
    tcg_gen_add_i32(cpu_R[rx], cpu_R[rx], t0);
    tcg_gen_qemu_ld_i32(cpu_R[(rz + 1) % 32], cpu_R[rx], s->mem_idx, MO_TEUL);
    if (gen_mem_trace()) {
        gen_helper_trace_ld32u(tcg_env, t2, cpu_R[(rz + 1) % 32], cpu_R[rx]);
    }
    tcg_gen_add_i32(cpu_R[rx], cpu_R[rx], t0);
}

static void ldr(DisasContext *ctx, uint32_t sop,
                 uint32_t pcode, int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    int imm = 0;
    if (sop < 32) {
        /* basic ld instructions. */
        imm = csky_log2(pcode);
        if (imm == -1) {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        check_insn(ctx, ABIV2_DSP2);
        if (pcode != 0) {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            return;
        }
    }

    switch (sop) {
    case 0x0:
        /* ldr.b */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldrstr(ld_i32, ld8u, rx, ry, rz, imm, MO_UB);
        break;
    case 0x1:
        /* ldr.h */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldrstr(ld_i32, ld16u, rx, ry, rz, imm, MO_TEUW);
        break;
    case 0x2:
        /* ldr.w */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldrstr(ld_i32, ld32u, rx, ry, rz, imm, MO_TEUL);
        break;
    case 0x3:
        /* ldr.d */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        tcg_gen_shli_tl(t0, cpu_R[ry], imm);
        tcg_gen_add_tl(t0, cpu_R[rx], t0);
        tcg_gen_qemu_ld_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUL);
        tcg_gen_addi_tl(t0, t0, 4);
        tcg_gen_qemu_ld_i32(cpu_R[rz + 1], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            TCGv t1, t2, t3;
            t1 = tcg_constant_i32(2);
            t2 = tcg_constant_i32(DATA_RADDR);
            t3 = tcg_constant_i32(ctx->pc);
            tcg_gen_subi_tl(t0, t0, 4);
            gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[rz]);
            tcg_gen_addi_tl(t0, t0, 4);
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[rz + 1]);
        }
        break;
    case 0x4:
        /* ldr.bs */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldrstr(ld_i32, ld8s, rx, ry, rz, imm, MO_SB);
        break;
    case 0x5:
        /* ldr.hs */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldrstr(ld_i32, ld16s, rx, ry, rz, imm, MO_TESW);
        break;
    case 0x7:
        /* ldm or ldq */
        check_insn_except(ctx, CPU_E801);

        TCGv t1 = tcg_constant_i32(rz + 1);
        TCGv t2 = tcg_constant_i32(DATA_RADDR);
        TCGv t3 = tcg_constant_i32(ctx->pc);
        if (ctx->bctm) {
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rx], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(NULL, 0);
            gen_set_label(l1);
            tcg_gen_mov_tl(t0, cpu_R[rx]);
            if (gen_mem_trace()) {
                gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
            }
            for (imm = 0; imm <= rz; imm++) {
                tcg_gen_qemu_ld_i32(cpu_R[ry + imm], t0, ctx->mem_idx, MO_TEUL);
                if (gen_mem_trace()) {
                    gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[ry + imm]);
                }
                tcg_gen_addi_tl(t0, t0, 4);
            }
            gen_goto_tb(ctx, 1, ctx->pc + 4);
            ctx->base.is_jmp = DISAS_TB_JUMP;
            break;
        } else {
            tcg_gen_mov_tl(t0, cpu_R[rx]);
            if (gen_mem_trace()) {
                gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
            }
            for (imm = 0; imm <= rz; imm++) {
                tcg_gen_qemu_ld_i32(cpu_R[ry + imm], t0, ctx->mem_idx, MO_TEUL);
                if (gen_mem_trace()) {
                    gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[ry + imm]);
                }
                tcg_gen_addi_tl(t0, t0, 4);
            }
            break;
        }
    case OP_LDBI_B:
        ldbistbi(ld_i32, ld8u, rx, rz, 1, MO_UB);
        break;
    case OP_LDBI_H:
        ldbistbi(ld_i32, ld16u, rx, rz, 2, MO_TEUW);
        break;
    case OP_LDBI_W:
        ldbistbi(ld_i32, ld32u, rx, rz, 4, MO_TEUL);
        break;
    case OP_PLDBI_D:
        dspv2_insn_pldbi_d(ctx, rz, rx);
        break;
    case OP_LDBI_BS:
        ldbistbi(ld_i32, ld8s, rx, rz, 1, MO_SB);
        break;
    case OP_LDBI_HS:
        ldbistbi(ld_i32, ld16s, rx, rz, 2, MO_TESW);
        break;
    case OP_LDBIR_B:
        ldbirstbir(ld_i32, ld8u, rx, rz, ry, MO_UB);
        break;
    case OP_LDBIR_H:
        ldbirstbir(ld_i32, ld16u, rx, rz, ry, MO_TEUW);
        break;
    case OP_LDBIR_W:
        ldbirstbir(ld_i32, ld32u, rx, rz, ry, MO_TEUL);
        break;
    case OP_PLDBIR_D:
        dspv2_insn_pldbir_d(ctx, rz, rx, ry);
        break;
    case OP_LDBIR_BS:
        ldbirstbir(ld_i32, ld8s, rx, rz, ry, MO_SB);
        break;
    case OP_LDBIR_HS:
        ldbirstbir(ld_i32, ld16s, rx, rz, ry, MO_TESW);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void str(DisasContext *ctx, uint32_t sop,
                 uint32_t pcode, int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    int imm = 0;

    if (sop < 32) {
        imm = csky_log2(pcode);

        if (imm == -1) {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        check_insn(ctx, ABIV2_DSP2);
        if (pcode != 0) {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            return;
        }
    }

    switch (sop) {
    case 0x0:
        /* str.b */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldrstr(st_i32, st8, rx, ry, rz, imm, MO_UB);
        break;
    case 0x1:
        /* str.h */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldrstr(st_i32, st16, rx, ry, rz, imm, MO_TEUW);
        break;
    case 0x2:
        /* str.w */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldrstr(st_i32, st32, rx, ry, rz, imm, MO_TEUL);
        break;
    case 0x3:
        /* str.d */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        tcg_gen_shli_tl(t0, cpu_R[ry], imm);
        tcg_gen_add_tl(t0, cpu_R[rx], t0);
        tcg_gen_qemu_st_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUL);
        tcg_gen_addi_tl(t0, t0, 4);
        tcg_gen_qemu_st_i32(cpu_R[rz + 1], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            TCGv t1, t2, t3;
            t1 = tcg_constant_i32(2);
            t2 = tcg_constant_i32(DATA_WADDR);
            t3 = tcg_constant_i32(ctx->pc);
            tcg_gen_subi_tl(t0, t0, 4);
            gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[rz]);
            tcg_gen_addi_tl(t0, t0, 4);
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[rz + 1]);
        }
        break;
    case 0x7:
        /* stm or stq */
        check_insn_except(ctx, CPU_E801);
        TCGv t1;
        TCGv t2 = tcg_constant_i32(DATA_WADDR);
        TCGv t3 = tcg_constant_i32(ctx->pc);
        if (ctx->bctm) {
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rx], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(NULL, 0);
            gen_set_label(l1);
            tcg_gen_mov_tl(t0, cpu_R[rx]);
            if (gen_mem_trace()) {
                t1 = tcg_constant_i32(rz + 1);
                gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
            }
            for (imm = 0; imm <= rz; imm++) {
                tcg_gen_qemu_st_i32(cpu_R[ry + imm], t0, ctx->mem_idx, MO_TEUL);
                if (gen_mem_trace()) {
                    gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[ry + imm]);
                }
                tcg_gen_addi_tl(t0, t0, 4);
            }
            gen_goto_tb(ctx, 1, ctx->pc + 4);
            ctx->base.is_jmp = DISAS_TB_JUMP;
        } else {
            tcg_gen_mov_tl(t0, cpu_R[rx]);
            if (gen_mem_trace()) {
                t1 = tcg_constant_tl(rz + 1);
                gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
            }
            for (imm = 0; imm <= rz; imm++) {
                tcg_gen_qemu_st_i32(cpu_R[ry + imm], t0, ctx->mem_idx, MO_TEUL);
                if (gen_mem_trace()) {
                    gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[ry + imm]);
                }
                tcg_gen_addi_tl(t0, t0, 4);
            }
        }
        break;
    case OP_STBI_B:
        ldbistbi(st_i32, st8, rx, rz, 1, MO_UB);
        break;
    case OP_STBI_H:
        ldbistbi(st_i32, st16, rx, rz, 2, MO_TEUW);
        break;
    case OP_STBI_W:
        ldbistbi(st_i32, st32, rx, rz, 4, MO_TEUL);
        break;
    case OP_STBIR_B:
        ldbirstbir(st_i32, st8, rx, rz, ry, MO_UB);
        break;
    case OP_STBIR_H:
        ldbirstbir(st_i32, st16, rx, rz, ry, MO_TEUW);
        break;
    case OP_STBIR_W:
        ldbirstbir(st_i32, st32, rx, rz, ry, MO_TEUL);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void pop(DisasContext *ctx, int imm)
{
    int i;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_constant_i32(DATA_RADDR);
    TCGv t3 = tcg_constant_i32(ctx->pc);

    tcg_gen_mov_tl(t0, cpu_R[sp]);
    tcg_gen_movi_tl(t1, 0);
    if (imm & 0x100) {
        tcg_gen_addi_tl(t1, t1, 1);
    }
    if ((imm >> 5) & 0x7) {
        tcg_gen_addi_tl(t1, t1, (imm >> 5) & 0x7);
    }
    if (imm & 0x10) {
        tcg_gen_addi_tl(t1, t1, 1);
    }
    if (imm & 0xf) {
        tcg_gen_addi_tl(t1, t1, imm & 0xf);
    }
    if (gen_mem_trace()) {
        gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
    }
    if (imm & 0xf) {
        for (i = 0; i < (imm & 0xf); i++) {
            tcg_gen_qemu_ld_i32(cpu_R[4 + i], t0, ctx->mem_idx, MO_TEUL);
            if (gen_mem_trace()) {
                gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[4 + i]);
            }
            tcg_gen_addi_i32(t0, t0, 4);
        }
    }

    if (imm & 0x10) {
        tcg_gen_qemu_ld_i32(cpu_R[15], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[15]);
        }
        tcg_gen_addi_i32(t0, t0, 4);
    }

    if ((imm >> 5) & 0x7) {
        for (i = 0; i < ((imm >> 5) & 0x7); i++) {
            tcg_gen_qemu_ld_i32(cpu_R[16 + i], t0, ctx->mem_idx, MO_TEUL);
            if (gen_mem_trace()) {
                gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[16 + i]);
            }
            tcg_gen_addi_i32(t0, t0, 4);
        }
    }

    if (imm & 0x100) {
        tcg_gen_qemu_ld_i32(cpu_R[28], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[28]);
        }
        tcg_gen_addi_i32(t0, t0, 4);
    }
    tcg_gen_mov_tl(cpu_R[sp], t0);

    tcg_gen_andi_tl(t0, cpu_R[15], 0xfffffffe);
    store_cpu_field(t0, pc);
    ctx->base.is_jmp = DISAS_JUMP;
}

static inline void ldex_w(DisasContext *ctx,
                          uint32_t rz, uint32_t rx, uint32_t imm)
{
    tcg_gen_addi_tl(excl_addr, cpu_R[rx], imm << 2);
    tcg_gen_qemu_ld_i32(excl_val, excl_addr, ctx->mem_idx, MO_TEUL);
    tcg_gen_mov_i32(cpu_R[rz], excl_val);
}

static void ldi(DisasContext *ctx, uint32_t sop,
                int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_new();

    switch (sop) {
    case 0x0:
        /* ld.b */
        ldst(ld_i32, ld8u, rx, rz, imm, 4, MO_UB);
        break;
    case 0x1:
        /* ld.h */
        ldst(ld_i32, ld16u, rx, rz, imm << 1, 4, MO_TEUW);
        break;
    case 0x2:
        /* ld.w */
        ldst(ld_i32, ld32u, rx, rz, imm << 2, 4, MO_TEUL);
        break;
    case 0x3:
        /* ld.d */
        check_insn(ctx, CPU_C810 | CPU_C807 | CPU_C860);
        tcg_gen_addi_tl(t0, cpu_R[rx], imm << 2);
        tcg_gen_qemu_ld_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUL);
        tcg_gen_addi_tl(t0, t0, 4);
        tcg_gen_qemu_ld_i32(cpu_R[(rz + 1) % 32], t0, ctx->mem_idx, MO_TEUL);
        break;
    case 0x4:
        /* ld.bs */
        check_insn_except(ctx, CPU_E801);
        ldst(ld_i32, ld8s, rx, rz, imm, 4, MO_SB);
        break;
    case 0x5:
        /* ld.hs */
        check_insn_except(ctx, CPU_E801);
        ldst(ld_i32, ld16s, rx, rz, imm << 1, 4, MO_TESW);
        break;
    case 0x6:
        /* pldr */    /*ignore this instruction*/
        check_insn(ctx, CPU_C807 | CPU_C810 | CPU_C860);
        break;
    case 0x7:
        /* ldex.w */
        check_insn(ctx, CPU_C860);
        ldex_w(ctx, rz, rx, imm);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }

}

static inline void push(DisasContext *ctx, int imm)
{
    TCGv t0 =  tcg_temp_new();
    int i;

    tcg_gen_mov_tl(t0, cpu_R[sp]);

    TCGv t1 = tcg_temp_new();
    tcg_gen_movi_tl(t1, 0);

    TCGv t2 = tcg_constant_i32(DATA_WADDR);
    TCGv t3 = tcg_constant_i32(ctx->pc);
    if (imm & 0x100) {
        tcg_gen_addi_tl(t1, t1, 1);
    }
    if ((imm >> 5) & 0x7) {
        tcg_gen_addi_tl(t1, t1, (imm >> 5) & 0x7);
    }
    if (imm & 0x10) {
        tcg_gen_addi_tl(t1, t1, 1);
    }
    if (imm & 0xf) {
        tcg_gen_addi_tl(t1, t1, imm & 0xf);
    }
    if (gen_mem_trace()) {
        gen_helper_trace_m_addr(tcg_env, t3, t0, t1, t2);
    }
    if (imm & 0x100) {
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st_i32(cpu_R[28], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[28]);
        }
    }

    if ((imm >> 5) & 0x7) {
        for (i = (imm >> 5) & 0x7; i > 0; i--) {
            tcg_gen_subi_i32(t0, t0, 4);
            tcg_gen_qemu_st_i32(cpu_R[15 + i], t0, ctx->mem_idx, MO_TEUL);
            if (gen_mem_trace()) {
                gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[15 + i]);
            }
        }
    }

    if (imm & 0x10) {
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st_i32(cpu_R[15], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[15]);
        }
    }

    if (imm & 0xf) {
        for (i = (imm & 0xf); i > 0; i--) {
            tcg_gen_subi_i32(t0, t0, 4);
            tcg_gen_qemu_st_i32(cpu_R[3 + i], t0, ctx->mem_idx, MO_TEUL);
            if (gen_mem_trace()) {
                gen_helper_trace_m_value(tcg_env, t3, t0, cpu_R[3 + i]);
            }
        }
    }
    tcg_gen_mov_tl(cpu_R[sp], t0);

}

static inline void stex_w(DisasContext *ctx,
                          uint32_t rz, uint32_t rx, uint32_t imm)
{
    TCGv_i32 t0, t1;
    TCGLabel *l3 = gen_new_label();
    TCGLabel *l4 = gen_new_label();

    MemOp opc = 2 | MO_ALIGN | MO_LE;

    t0 = tcg_temp_new_i32();
    t1 = tcg_temp_new_i32();
    tcg_gen_addi_i32(t0, cpu_R[rx], imm << 2);
    tcg_gen_brcond_i32(TCG_COND_NE, t0, excl_addr, l3);

    tcg_gen_atomic_cmpxchg_i32(t1, excl_addr, excl_val, cpu_R[rz],
        ctx->mem_idx, opc);
    tcg_gen_setcond_i32(TCG_COND_EQ, t1, t1, excl_val);
    tcg_gen_mov_i32(cpu_R[rz], t1);
    tcg_gen_br(l4);

    gen_set_label(l3);
    tcg_gen_movi_i32(cpu_R[rz], 0);

    gen_set_label(l4);
    tcg_gen_movi_i32(excl_addr, -1);

}

static void sti(DisasContext *ctx, uint32_t sop, int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_new();

    switch (sop) {
    case 0x0:
        /* st.b */
        ldst(st_i32, st8, rx, rz, imm, 4, MO_UB);
        break;
    case 0x1:
        /* st.h */
        ldst(st_i32, st16, rx, rz, imm << 1, 4, MO_TEUW);
        break;
    case 0x2:
        /* st.w */
        ldst(st_i32, st32, rx, rz, imm << 2, 4, MO_TEUL);
        break;
    case 0x3:
        /* st.d */
        check_insn(ctx, CPU_C810 | CPU_C807 | CPU_C860);
        tcg_gen_addi_tl(t0, cpu_R[rx], imm << 2);
        tcg_gen_qemu_st_i32(cpu_R[rz], t0, ctx->mem_idx, MO_TEUL);
        tcg_gen_addi_tl(t0, t0, 4);
        tcg_gen_qemu_st_i32(cpu_R[(rz + 1) % 32], t0, ctx->mem_idx, MO_TEUL);
        break;
    case 0x6:
        /* pldw */    /*ignore this instruction*/
        check_insn(ctx, CPU_C807 | CPU_C810 | CPU_C860);
        break;
    case 0x7:
        /* stex.w */
        check_insn(ctx, CPU_C860);
        stex_w(ctx, rz, rx, imm);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void special(DisasContext *ctx, int rx, uint32_t sop,
                            int rz, int ry)
{
    /* ry:25-21, rx:20-16, sop:15-10, rz:4-0 */
    TCGv t0;

    switch (sop) {
    case 0x1:
        /* sync */
        break;
    case 0x4:
        /* bmset */
        check_insn(ctx, ABIV2_JAVA);

        t0 = tcg_constant_tl(1);
        store_cpu_field(t0, psr_bm);
        ctx->base.is_jmp = DISAS_UPDATE;

        break;
    case 0x5:
        /* bmclr */
        check_insn(ctx, ABIV2_JAVA);

        t0 = tcg_constant_tl(0);
        store_cpu_field(t0, psr_bm);
        ctx->base.is_jmp = DISAS_UPDATE;

        break;
    case 0x6:
        /* sce */
        check_insn(ctx, CPU_C810 | CPU_E803 | CPU_C807);
        sce(ctx, ry & 0xf);
        break;
    case 0x7:
        /* idly */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
#if !defined(CONFIG_USER_ONLY)
        if (ctx->trace_mode == NORMAL_MODE) {
            TCGLabel *l1 = gen_new_label();

            t0 = load_cpu_field(idly4_counter);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);

            tcg_gen_movi_tl(t0, 4);
            store_cpu_field(t0, idly4_counter);
            tcg_gen_movi_tl(cpu_c, 0);

            gen_set_label(l1);
            ctx->base.is_jmp = DISAS_UPDATE;
            gen_save_pc(ctx->pc + 4);
        }
#endif
        break;
    case 0x8:
        /* trap 0 */
        generate_exception(ctx, EXCP_CSKY_TRAP0);
#if !defined(CONFIG_USER_ONLY)
        ctx->cannot_be_traced = 1;
#endif
        break;
    case 0x9:
        /* trap 1 */
#if !defined(CONFIG_USER_ONLY)
        generate_exception(ctx, EXCP_CSKY_TRAP1);
        ctx->cannot_be_traced = 1;
#endif
        break;
    case 0xa:
        /* trap 2 */
        generate_exception(ctx, EXCP_CSKY_TRAP2);
#if !defined(CONFIG_USER_ONLY)
        ctx->cannot_be_traced = 1;
#endif
        break;
    case 0xb:
        /* trap 3 */
        generate_exception(ctx, EXCP_CSKY_TRAP3);
#if !defined(CONFIG_USER_ONLY)
        ctx->cannot_be_traced = 1;
#endif
        break;
    case 0xf:
        /* wsc */
        check_insn(ctx, ABIV2_TEE);
#ifndef CONFIG_USER_ONLY
        t0 = tcg_temp_new();
        tcg_gen_movi_tl(t0, ctx->pc);
        store_cpu_field(t0, pc);
        tcg_gen_movi_tl(t0, 0);
        store_cpu_field(t0, idly4_counter);
        gen_helper_wsc(tcg_env);
        ctx->base.is_jmp = DISAS_UPDATE;
        ctx->cannot_be_traced = 1;
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x15:
        /* we */
        break;
    case 0x16:
        /* se */
        break;
    case 0x10:
        /* rte */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_constant_tl(0);
            store_cpu_field(t0, idly4_counter);

            gen_helper_rte(tcg_env);
            ctx->base.is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x11:
        /* rfi */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_constant_tl(0);
            store_cpu_field(t0, idly4_counter);

            gen_helper_rfi(tcg_env);
            ctx->base.is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x12:
        /* stop */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_constant_tl(0);
            store_cpu_field(t0, idly4_counter);

            gen_save_pc(ctx->pc + 4);
            gen_helper_stop(tcg_env);
            ctx->base.is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x13:
        /* wait */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_constant_tl(0);
            store_cpu_field(t0, idly4_counter);

            gen_save_pc(ctx->pc + 4);
            gen_helper_wait(tcg_env);
            ctx->base.is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x14:
        /* doze */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_constant_tl(0);
            store_cpu_field(t0, idly4_counter);

            gen_save_pc(ctx->pc + 4);
            gen_helper_doze(tcg_env);
            ctx->base.is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x18:
        /* mfcr */
#ifndef CONFIG_USER_ONLY

        if (ry == 2) {
            check_insn(ctx, ABIV2_FLOAT_S);
            gen_mfcr_vfp(ctx, rz, rx);
            break;
        }
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        }

        if (ry == 0) { /* sel */
            gen_mfcr_cpu(ctx, rz, rx);
        } else if (ry == 3) {  /* tee */
            check_insn(ctx, ABIV2_TEE);
            if (IS_TRUST(ctx)) {
                gen_mfcr_tee(ctx, rz, rx);
            } else {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }
        } else if (ry == 14) {
            gen_mfcr_mptimer(ctx, rz, rx);
        } else if (ry == 15) {
            check_insn(ctx, CSKY_MMU);
            gen_mfcr_mmu(ctx, rz, rx);
        } else if ((ry == 1) && (rx == 14)) {
            gen_helper_mfcr_cr14(cpu_R[rz], tcg_env);
        } else if ((ry == 1) && (rx == 15)) {
            gen_helper_mfcr_cr15(cpu_R[rz], tcg_env);
        } else if ((ry == 1) && (rx == 1)) {  /* mfcr cr<1, 1> */
            check_insn(ctx, ABIV2_TEE);
            if (IS_TRUST(ctx)) {
                t0 = load_cpu_field(tee.t_ebr);
            } else {
                t0 = load_cpu_field(tee.nt_ebr);
            }
            tcg_gen_mov_tl(cpu_R[rz], t0);
        }
        break;
#else
        if (ry == 2) {
            check_insn(ctx, ABIV2_FLOAT_S);
            gen_mfcr_vfp(ctx, rz, rx);
        } else {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        }
        break;
#endif
    case 0x19:
        /* mtcr */
#ifndef CONFIG_USER_ONLY
        if (ry == 2) {
            check_insn(ctx, ABIV2_FLOAT_S);
            gen_mtcr_vfp(ctx, rz, rx);
        } else if (ry == 13) {
            gen_mtcr_tp(ctx, rz, rx);
        } else if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            if (ry == 0) {  /* sel */
                gen_mtcr_cpu(ctx, rz, rx);
            } else if (ry == 3) {  /* tee */
                check_insn(ctx, ABIV2_TEE);
                if (IS_TRUST(ctx)) {
                    gen_mtcr_tee(ctx, rz, rx);
                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
            } else if (ry == 14) { /* mptimer */
                gen_mtcr_mptimer(ctx, rz, rx);
            } else if (ry == 15) {
                check_insn(ctx, CSKY_MMU);
                gen_mtcr_mmu(ctx, rz, rx);
            } else if ((ry == 1) && (rz == 14)) { /* mtcr cr<14, 1> */
                gen_helper_mtcr_cr14(tcg_env, cpu_R[rx]);
            } else if ((ry == 1) && (rz == 15)) { /* mtcr cr<14, 1> */
                gen_helper_mtcr_cr15(tcg_env, cpu_R[rx]);
            } else if ((ry == 1) && (rz == 1)) {  /* mtcr cr<1, 1> */
                check_insn(ctx, ABIV2_TEE);
                t0 = tcg_temp_new();
                tcg_gen_andi_tl(t0, cpu_R[rx], ~0x3);
                if (IS_TRUST(ctx)) {
                    store_cpu_field(t0, tee.t_ebr);
                } else {
                    store_cpu_field(t0, tee.nt_ebr);
                }
            }
        }
#else
        if (ry == 2) {
            check_insn(ctx, ABIV2_FLOAT_S);
            gen_mtcr_vfp(ctx, rz, rx);
        } else if (ry == 13) {
            gen_mtcr_tp(ctx, rz, rx);
        } else {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        }
#endif
        if (ctx->base.is_jmp == DISAS_NEXT) {
            gen_save_pc(ctx->pc + 4);
            ctx->base.is_jmp = DISAS_UPDATE;
        }
        break;
    case 0x1c:
        /* psrclr */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_constant_tl(ry);
            gen_helper_psrclr(tcg_env, t0);

            gen_save_pc(ctx->pc + 4);
            ctx->base.is_jmp = DISAS_UPDATE;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x1d:
        /* psrset */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_constant_tl(ry);
            gen_helper_psrset(tcg_env, t0);

            gen_save_pc(ctx->pc + 4);
            ctx->base.is_jmp = DISAS_UPDATE;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x21:
        /* bar */
        check_insn(ctx, CPU_C860);
        /* ignore this instruction. */
        break;
    case 0x22:
        /* ck860 tlbi instructions */
#ifndef CONFIG_USER_ONLY
        check_insn(ctx, CPU_C860);
        t0 = tcg_constant_tl(ry);

        gen_helper_mp_tlb_inv(tcg_env, cpu_R[rx], t0);

        gen_save_pc(ctx->pc + 4);
        ctx->base.is_jmp = DISAS_UPDATE;
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x24:
        /* ck860 icache instructions: icache.iall(s)/icache iva */
        check_insn(ctx, CPU_C860);
        break;
    case 0x25:
        /* ck860 dcache instructions */
        check_insn(ctx, CPU_C860);
        /* ignore those instructions. */
        break;
    case 0x26:
        /* ck860 l2cache instructions */
        check_insn(ctx, CPU_C860);
        /* ignore those instructions. */
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void arth_reg32(DisasContext *ctx,  int ry, int rx, uint32_t sop,
                              uint32_t pcode, int rz)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int lsb;
    int msb;

    switch (sop) {
    case 0x0:
        if (pcode == 0x1) {
            /* addu */
            check_insn_except(ctx, CPU_E801);
            tcg_gen_add_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x2) {
            /* addc */
            check_insn_except(ctx, CPU_E801);
            addc(rz, rx, ry);
        } else if (pcode == 0x4) {
            /* subu or rsub */
            check_insn_except(ctx, CPU_E801);
            tcg_gen_sub_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x8) {
            /* subc */
            check_insn_except(ctx, CPU_E801);
            subc(rz, rx, ry);
        } else if (pcode == 0x10) {
            /* abs */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            TCGLabel *l1 = gen_new_label();
            tcg_gen_mov_tl(cpu_R[rz], cpu_R[rx]);
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[rx], 0x80000000, l1);
            tcg_gen_brcondi_tl(TCG_COND_GE, cpu_R[rx], 0, l1);
            tcg_gen_neg_tl(cpu_R[rz], cpu_R[rx]);
            gen_set_label(l1);
        } else {
            goto illegal_op;
        }
        break;
    case 0x1:
        if (pcode == 0x1) {
            /* cmphs */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x2) {
            /* cmplt */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_setcond_tl(TCG_COND_LT, cpu_c, cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x4) {
            /* cmpne */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_setcond_tl(TCG_COND_NE, cpu_c, cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x8) {
            /* mvc */
            check_insn_except(ctx, CPU_E801);
            tcg_gen_mov_tl(cpu_R[rz], cpu_c);
        } else if (pcode == 0x10) {
            /* mvcv */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_subfi_tl(cpu_R[rz], 1, cpu_c);
        } else {
            goto illegal_op;
        }
        break;
    case 0x2:
        if (pcode == 0x1) {
            /* ixh */
            check_insn_except(ctx, CPU_E801);
            add_ix(rz, rx, ry, 1);
        } else if (pcode == 0x2) {
            /* ixw */
            check_insn_except(ctx, CPU_E801);
            add_ix(rz, rx, ry, 2);
        } else if (pcode == 0x4) {
            /* ixd */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            add_ix(rz, rx, ry, 3);
        } else {
            goto illegal_op;
        }
        break;
    case 0x3:
        check_insn_except(ctx, CPU_E801);
        if (pcode == 0x1) {
            /* incf */
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
            tcg_gen_addi_tl(cpu_R[ry], cpu_R[rx], rz);
            gen_set_label(l1);
        } else if (pcode == 0x2) {
            /* inct */
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
            tcg_gen_addi_tl(cpu_R[ry], cpu_R[rx], rz);
            gen_set_label(l1);
        } else if (pcode == 0x4) {
            /* decf */
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
            tcg_gen_subi_tl(cpu_R[ry], cpu_R[rx], rz);
            gen_set_label(l1);
        } else if (pcode == 0x8) {
            /* dect */
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
            tcg_gen_subi_tl(cpu_R[ry], cpu_R[rx], rz);
            gen_set_label(l1);
        } else {
            goto illegal_op;
        }
        break;
    case 0x4:
        if (pcode == 0x1) {
            /* decgt */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], ry);
            tcg_gen_setcondi_tl(TCG_COND_GT, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x2) {
            /* declt */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], ry);
            tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x4) {
            /* decne */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], ry);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else {
            goto illegal_op;
        }
        break;
    case 0x7:
        if (pcode == 1) {
            /* cmpix */
            check_insn(ctx, ABIV2_JAVA);
            if (ctx->bctm) {
                TCGLabel *l1 = gen_new_label();
                tcg_gen_brcond_tl(TCG_COND_LT, cpu_R[rx], cpu_R[ry], l1);
                tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
                tcg_gen_subi_tl(t0, cpu_R[SVBR], 8);
                store_cpu_field(t0, pc);
                ctx->base.is_jmp = DISAS_JUMP;
                gen_set_label(l1);
            } else {
                break;
            }
        } else {
            goto illegal_op;
        }
        break;
    case 0x8:
        if (pcode == 0x1) {
            /* and */
            check_insn_except(ctx, CPU_E801);
            tcg_gen_and_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x2) {
            /* andn */
            check_insn_except(ctx, CPU_E801);
            tcg_gen_andc_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x4) {
            /* tst */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_and_tl(t0, cpu_R[rx], cpu_R[ry]);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, t0, 0);
        } else if (pcode == 0x8) {
            /* tstnbz */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tstnbz(rx);
        } else {
            goto illegal_op;
        }
        break;
    case 0x9:
        check_insn_except(ctx, CPU_E801);
        if (pcode == 0x1) {
            /* or */
            tcg_gen_or_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x2) {
            /* xor */
            tcg_gen_xor_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x4) {
            /* nor */
            tcg_gen_nor_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else {
            goto illegal_op;
        }
        break;
    case 0xa:
        check_insn_except(ctx, CPU_E801);
        if (pcode == 0x1) {
            /* bclri */
            tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], ~(1 << ry));
        } else if (pcode == 0x2) {
            /* bseti */
            tcg_gen_ori_tl(cpu_R[rz], cpu_R[rx], 1 << ry);
        } else if (pcode == 0x4) {
            /* btsti */
            tcg_gen_andi_tl(cpu_c, cpu_R[rx], 1 << ry);
            tcg_gen_shri_tl(cpu_c, cpu_c, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0xb:
        if (pcode == 0x1) {
            /* clrf */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
            tcg_gen_movi_tl(cpu_R[ry], 0);
            gen_set_label(l1);
        } else if (pcode == 0x2) {
            /* clrt */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
            tcg_gen_movi_tl(cpu_R[ry], 0);
            gen_set_label(l1);
        } else {
            goto illegal_op;
        }
        break;
    case 0x10:
        check_insn_except(ctx, CPU_E801);
        if (pcode == 0x1) {
            /* lsl */
            lsl(rz, rx, ry);
        } else if (pcode == 0x2) {
            /* lsr */
            lsr(rz, rx, ry);
        } else if (pcode == 0x4) {
            /* asr */
            asr(rz, rx, ry);
        } else if (pcode == 0x8) {
            /* rotl */
            rotl(rz, rx, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x12:
        check_insn_except(ctx, CPU_E801);
        if (pcode == 0x1) {
            /* lsli */
            tcg_gen_shli_tl(cpu_R[rz], cpu_R[rx], ry);
        } else if (pcode == 0x2) {
            /* lsri */
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], ry);
        } else if (pcode == 0x4) {
            /* asri */
            tcg_gen_sari_tl(cpu_R[rz], cpu_R[rx], ry);
        } else if (pcode == 0x8) {
            /* rotli */
            tcg_gen_rotli_tl(cpu_R[rz], cpu_R[rx], ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x13:
        check_insn_except(ctx, CPU_E801);
        if (pcode == 0x1) {
            /* lslc */
            lslc(rz, rx, ry + 1);
        } else if (pcode == 0x2) {
            /* lsrc */
            lsrc(rz, rx, ry + 1);
        } else if (pcode == 0x4) {
            /* asrc */
            asrc(rz, rx, ry + 1);
        } else if (pcode == 0x8) {
            /* xsr */
            t0 = tcg_constant_tl(ry + 1);
            gen_helper_xsr(cpu_R[rz], tcg_env, cpu_R[rx], t0);
        } else {
            goto illegal_op;
        }
        break;
    case 0x14:
        if (pcode == 0x1) {
            /* bmaski */
            check_insn_except(ctx, CPU_E801);
            ry += 1;
            if (ry == 32) {
                tcg_gen_movi_tl(cpu_R[rz], 0xffffffff);
            } else {
                tcg_gen_movi_tl(cpu_R[rz], (1 << ry) - 1);
            }
        } else if (pcode == 0x2) {
            /* bgenr */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            TCGv t2 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();

            tcg_gen_mov_tl(t2, cpu_R[rx]);
            tcg_gen_movi_tl(cpu_R[rz], 0);
            tcg_gen_andi_tl(t1, t2, 0x20);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(t1, 1);
            tcg_gen_andi_tl(t2, t2, 0x1f);
            tcg_gen_shl_tl(cpu_R[rz], t1, t2);
            gen_set_label(l1);

        } else {
            goto illegal_op;
        }
        break;
    case 0x15:
        /* zext or zextb or zexth */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        lsb = ry;
        msb = pcode;
        if (lsb == 0 && msb == 31) {
            tcg_gen_mov_tl(cpu_R[rz], cpu_R[rx]);
        } else {
            tcg_gen_movi_tl(t0, 0);
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], lsb);
            tcg_gen_deposit_tl(cpu_R[rz], t0, cpu_R[rz], 0, msb - lsb + 1);
        }
        break;
    case 0x16:
        /* sext or sextb or sexth */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        lsb = ry;
        msb = pcode;
        if (lsb == 0 && msb == 31) {
            tcg_gen_mov_tl(cpu_R[rz], cpu_R[rx]);
        } else {
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], lsb);
            tcg_gen_movi_tl(t0, 0);
            tcg_gen_deposit_tl(t0, t0, cpu_R[rz], 0, msb - lsb + 1);
            tcg_gen_shli_tl(t0, t0, 32 - (msb - lsb + 1));
            tcg_gen_sari_tl(cpu_R[rz], t0, 32 - (msb - lsb + 1));
        }
        break;
    case 0x17:
        /* ins */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        lsb = rz;
        if (pcode == 31) {
            tcg_gen_mov_tl(cpu_R[ry], cpu_R[rx]);
        } else {
            tcg_gen_deposit_tl(cpu_R[ry], cpu_R[ry], cpu_R[rx], lsb, pcode + 1);
        }
        break;
    case 0x18:
        if (pcode == 0x4) {
            /* revb */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_bswap32_tl(cpu_R[rz], cpu_R[rx], TCG_BSWAP_OZ);
        } else if (pcode == 0x8) {
            /* revh */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_bswap32_tl(t0, cpu_R[rx], TCG_BSWAP_OZ);
            tcg_gen_shri_tl(t1, t0, 16);
            tcg_gen_shli_tl(t0, t0, 16);
            tcg_gen_or_tl(cpu_R[rz], t0, t1);
        } else if (pcode == 0x10) {
            /* brev */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            gen_helper_brev(cpu_R[rz], cpu_R[rx]);
        } else {
            goto illegal_op;
        }
        break;
    case 0x1c:
        check_insn_except(ctx, CPU_E801);
        if (pcode == 0x1) {
            /* xtrb0 */
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], 24);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x2) {
            /* xtrb1 */
            tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0x00ff0000);
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rz], 16);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x4) {
            /* xtrb2 */
            tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0x0000ff00);
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rz], 8);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x8) {
            /* xtrb3 */
            tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0xff);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else {
            goto illegal_op;
        }
        break;
    case 0x1f:
        check_insn_except(ctx, CPU_E801);
        if (pcode == 0x1) {
            /* ff0 */
            gen_helper_ff0(cpu_R[rz], cpu_R[rx]);
        } else if (pcode == 0x2) {
            /* ff1 */
            gen_helper_ff1(cpu_R[rz], cpu_R[rx]);
        } else {
            goto illegal_op;
        }
        break;
    case 0x20:
        if (pcode == 0x1) {
            /* divu */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            divu(ctx, rz, rx, ry);
        } else if (pcode == 0x2) {
            /* divs */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            divs(ctx, rz, rx, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x21:
        check_insn_except(ctx, CPU_E801);
        if (pcode == 0x1) {
            /* mult */
            tcg_gen_mul_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else {
            goto illegal_op;
        }
        break;
    case 0x22:
        if (pcode == 0x1) {
            /* mulu */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_extu_tl_i64(t2, cpu_R[rx]);
            tcg_gen_extu_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t2, t2, t3);
            tcg_gen_trunc_i64_tl(cpu_lo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t2);
        } else if (pcode == 0x2) {
            /* mulua */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_extu_tl_i64(t2, cpu_R[rx]);
            tcg_gen_extu_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t3, t3, t2);
            tcg_gen_concat_tl_i64(t2, cpu_lo, cpu_hi);
            tcg_gen_add_i64(t3, t3, t2);
            tcg_gen_trunc_i64_tl(cpu_lo, t3);
            tcg_gen_shri_i64(t3, t3, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t3);
        } else if (pcode == 0x4) {
            /* mulus */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_extu_tl_i64(t2, cpu_R[rx]);
            tcg_gen_extu_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t3, t3, t2);
            tcg_gen_concat_tl_i64(t2, cpu_lo, cpu_hi);
            tcg_gen_sub_i64(t3, t2, t3);
            tcg_gen_trunc_i64_tl(cpu_lo, t3);
            tcg_gen_shri_i64(t3, t3, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t3);
        } else {
            goto illegal_op;
        }
        break;
    case 0x23:
        if (pcode == 0x1) {
            /* muls */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(t2, cpu_R[rx]);
            tcg_gen_ext_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t2, t2, t3);
            tcg_gen_trunc_i64_tl(cpu_lo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t2);
        } else if (pcode == 0x2) {
            /* mulsa */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(t2, cpu_R[rx]);
            tcg_gen_ext_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t3, t3, t2);
            tcg_gen_concat_tl_i64(t2, cpu_lo, cpu_hi);
            tcg_gen_add_i64(t3, t3, t2);
            tcg_gen_trunc_i64_tl(cpu_lo, t3);
            tcg_gen_shri_i64(t3, t3, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t3);
        } else if (pcode == 0x4) {
            /* mulss */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(t2, cpu_R[rx]);
            tcg_gen_ext_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t3, t3, t2);
            tcg_gen_concat_tl_i64(t2, cpu_lo, cpu_hi);
            tcg_gen_sub_i64(t3, t2, t3);
            tcg_gen_trunc_i64_tl(cpu_lo, t3);
            tcg_gen_shri_i64(t3, t3, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t3);
        } else {
            goto illegal_op;
        }
        break;
    case 0x24:
        if (pcode == 0x1) {
            /* mulsh */
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            tcg_gen_ext16s_tl(t0, cpu_R[rx]);
            tcg_gen_ext16s_tl(t1, cpu_R[ry]);
            tcg_gen_mul_tl(cpu_R[rz], t0, t1);
        } else if (pcode == 0x2) {
            /* mulsha */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            mulsha(rx, ry);
        } else if (pcode == 0x4) {
            /* mulshs */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            mulshs(rx, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x25:
        if (pcode == 0x1) {
            /* mulsw */
            check_insn(ctx, CPU_C810 | CPU_C807 | CPU_C860 | ABIV2_DSP);
            mulsw(rz, rx, ry);
        } else if (pcode == 0x2) {
            /* mulswa */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            mulswa(rx, ry);
        } else if (pcode == 0x4) {
            /* mulsws */
            check_insn(ctx, CPU_C810 | CPU_C807 | ABIV2_DSP);
            mulsws(rx, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x26:
        if (pcode == 0x10) {
            /*mvtc*/
            check_insn(ctx, CPU_C807 | CPU_C810 | CPU_C860 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_c, cpu_v);
        } else {
            goto illegal_op;
        }
        break;
    case 0x27:
        if (pcode == 0x1) {
            /* mfhi */
            check_insn(ctx, CPU_C807 | CPU_C810 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_R[rz], cpu_hi);
        } else if (pcode == 0x2) {
            /* mthi */
            check_insn(ctx, CPU_C807 | CPU_C810 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_hi, cpu_R[rx]);
        } else if (pcode == 0x4) {
            /* mflo */
            check_insn(ctx, CPU_C807 | CPU_C810 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_R[rz], cpu_lo);
        } else if (pcode == 0x8) {
            /* mtlo */
            check_insn(ctx, CPU_C807 | CPU_C810 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_lo, cpu_R[rx]);
        } else {
            goto illegal_op;
        }
        break;
    default:
illegal_op:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }


}

static inline void lrs(DisasContext *ctx, int rz, uint32_t sop, int imm)
{
    TCGv t0 = tcg_temp_new();
    switch (sop) {
    case 0x0: /*lrs.b*/
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldst(ld_i32, ld8u, 28, rz, imm, 4, MO_UB);
        break;
    case 0x1: /*lrs.h*/
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldst(ld_i32, ld16u, 28, rz, imm << 1, 4, MO_TEUW);
        break;
    case 0x2: /*lrs.w*/
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldst(ld_i32, ld32u, 28, rz, imm << 2, 4, MO_TEUL);
        break;
    case 0x3: /*grs*/
        {
            check_insn_except(ctx, CPU_E801 | CPU_E802);
            int t1;
            t1 = imm << 1;
            if (t1 & 0x40000) {
                t1 |= 0xfffc0000;
            }
            t1 += ctx->pc;
            tcg_gen_movi_tl(cpu_R[rz], t1);
        }
        break;
    case 0x4: /*srs.b*/
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldst(st_i32, st8, 28, rz, imm, 4, MO_UB);
        break;
    case 0x5: /*srs.h*/
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldst(st_i32, st16, 28, rz, imm << 1, 4, MO_TEUW);
        break;
    case 0x6: /*srs.w*/
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        ldst(st_i32, st32, 28, rz, imm << 2, 4, MO_TEUL);
        break;
    case 0x7:  /* addi */
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[28], imm + 1);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void imm_2op(DisasContext *ctx, int rz, int rx, uint32_t sop,
                            int imm)
{
    TCGv t0;

    check_insn_except(ctx, CPU_E801);
    switch (sop) {
    case 0x0:  /* addi */
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[rx], imm + 1);
        break;
    case 0x1: /* subi */
        tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], imm + 1);
        break;
    case 0x2: /* andi */
        tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    case 0x3: /* andni */
        t0 = tcg_constant_tl(imm);
        tcg_gen_andc_tl(cpu_R[rz], cpu_R[rx], t0);
        break;
    case 0x4: /* xori */
        tcg_gen_xori_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void imm_1op(DisasContext *ctx, uint32_t sop, int rx, int imm)
{
    target_ulong addr;
    int val = 0;
    TCGv_i32 t0, t1, t3;

    switch (sop) {
    case 0x0: /* br */
        val = imm << 1;
        if (val & 0x10000) {
            val |= 0xffff0000;
        }
        val += ctx->pc;

        gen_goto_tb(ctx, 0, val);
        ctx->base.is_jmp = DISAS_TB_JUMP;
        break;
    case 0x1: /* bnezad */
        check_insn(ctx, CPU_C860 | CPU_E803);
        bnezad(ctx, rx, imm);
        break;
    case 0x2: /* bf */
        check_insn_except(ctx, CPU_E801);
        branch32(ctx, TCG_COND_EQ, -1, imm);
        break;
    case 0x3: /* bt */
        check_insn_except(ctx, CPU_E801);
        branch32(ctx, TCG_COND_NE, -1, imm);
        break;
    case 0x6: /* jmp */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        t0 = tcg_temp_new();
        tcg_gen_andi_tl(t0, cpu_R[rx], 0xfffffffe);
        store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
        if ((ctx->trace_mode == BRAN_TRACE_MODE)
            || (ctx->trace_mode == INST_TRACE_MODE)) {
            t0 = tcg_constant_i32(EXCP_CSKY_TRACE);
            gen_helper_exception(tcg_env, t0);
        }
        ctx->maybe_change_flow = 1;
#endif
        ctx->base.is_jmp = DISAS_JUMP;
        break;
    case 0x7:/* jsr */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        t0 = tcg_temp_new();
        tcg_gen_andi_tl(t0, cpu_R[rx], 0xfffffffe);
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
        store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
        if ((ctx->trace_mode == BRAN_TRACE_MODE)
            || (ctx->trace_mode == INST_TRACE_MODE)) {
            t0 = tcg_constant_i32(EXCP_CSKY_TRACE);
            gen_helper_exception(tcg_env, t0);
        }
        ctx->maybe_change_flow = 1;
#endif
        ctx->base.is_jmp = DISAS_JUMP;
        break;
    case 0x8: /* bez */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        branch32(ctx, TCG_COND_EQ, rx, imm);
        break;
    case 0x9: /* bnez */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        branch32(ctx, TCG_COND_NE, rx, imm);
        break;
    case 0xa:  /* bhz */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        branch32(ctx, TCG_COND_GT, rx, imm);
        break;
    case 0xb: /* blsz */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        branch32(ctx, TCG_COND_LE, rx, imm);
        break;
    case 0xc: /* blz */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        branch32(ctx, TCG_COND_LT, rx, imm);
        break;
    case 0xd: /* bhsz */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        branch32(ctx, TCG_COND_GE, rx, imm);
        break;
    case 0xe: /* bloop */
        check_insn(ctx, ABIV2_DSP2 | ABIV2_VDSP2);
        if (imm & 0x800) {
            val = imm | 0xf000;
        }
        tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
        branch32(ctx, TCG_COND_NE, rx, val);
        break;
    case 0xf: /* jmpix */
        check_insn(ctx, ABIV2_JAVA);
        if (ctx->bctm) {
            t0 = tcg_temp_new();
            t1 = tcg_temp_new();

            tcg_gen_andi_tl(t0, cpu_R[rx], 0xff);
            switch (imm & 0x3) {
            case 0x0:
                tcg_gen_shli_tl(t0, t0, 4);
                break;
            case 0x1:
                tcg_gen_shli_tl(t1, t0, 4);
                tcg_gen_shli_tl(t0, t0, 3);
                tcg_gen_add_tl(t0, t0, t1);
                break;
            case 0x2:
                tcg_gen_shli_tl(t0, t0, 5);
                break;
            case 0x3:
                tcg_gen_shli_tl(t1, t0, 5);
                tcg_gen_shli_tl(t0, t0, 3);
                tcg_gen_add_tl(t0, t0, t1);
                break;
            default:
                break;
            }
            tcg_gen_add_tl(t0, cpu_R[SVBR], t0);
            store_cpu_field(t0, pc);

            ctx->base.is_jmp = DISAS_JUMP;
            break;
        } else {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            break;
        }
    case 0x10: /* movi */
        check_insn_except(ctx, CPU_E801);
        tcg_gen_movi_tl(cpu_R[rx], imm);
        break;
    case 0x11: /* movih */
        check_insn_except(ctx, CPU_E801);
        tcg_gen_movi_tl(cpu_R[rx], imm << 16);
        break;
    case 0x14:/* lrw */
        t0 = tcg_temp_new();
        t3 = tcg_constant_i32(ctx->pc);
        addr = (ctx->pc + (imm << 2)) & 0xfffffffc ;
        tcg_gen_movi_tl(t0, addr);
        tcg_gen_qemu_ld_i32(cpu_R[rx], t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_ld32u(tcg_env, t3, cpu_R[rx], t0);
        }
        break;
    case 0x16: /* jmpi */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        t0 = tcg_temp_new();
        t3 = tcg_constant_i32(ctx->pc);

        addr = (ctx->pc + (imm << 2)) & 0xfffffffc ;
        t1 = tcg_temp_new();
        tcg_gen_movi_tl(t1, addr);
        tcg_gen_movi_tl(t0, addr);
        tcg_gen_qemu_ld_i32(t0, t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_ld32u(tcg_env, t3, t0, t1);
        }
        store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
        if ((ctx->trace_mode == BRAN_TRACE_MODE)
            || (ctx->trace_mode == INST_TRACE_MODE)) {
            t0 = tcg_constant_i32(EXCP_CSKY_TRACE);
            gen_helper_exception(tcg_env, t0);
        }
        ctx->maybe_change_flow = 1;
#endif
        ctx->base.is_jmp = DISAS_JUMP;

        break;
    case 0x17: /* jsri */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        t0 = tcg_temp_new();
        t1 = tcg_temp_new();
        t3 = tcg_constant_i32(ctx->pc);
        addr =  (ctx->pc + (imm << 2)) & 0xfffffffc;
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
        tcg_gen_movi_tl(t1, addr);
        tcg_gen_movi_tl(t0, addr);
        tcg_gen_qemu_ld_i32(t0, t0, ctx->mem_idx, MO_TEUL);
        if (gen_mem_trace()) {
            gen_helper_trace_ld32u(tcg_env, t3, t0, t1);
        }
        store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
        if ((ctx->trace_mode == BRAN_TRACE_MODE)
            || (ctx->trace_mode == INST_TRACE_MODE)) {
            tcg_gen_movi_i32(t0, EXCP_CSKY_TRACE);
            gen_helper_exception(tcg_env, t0);
        }
        ctx->maybe_change_flow = 1;
#endif
        ctx->base.is_jmp = DISAS_JUMP;

        break;
    case 0x18: /* cmphsi */
        check_insn_except(ctx, CPU_E801);
        tcg_gen_setcondi_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], imm + 1);
        break;
    case 0x19:  /* cmplti */
        check_insn_except(ctx, CPU_E801);
        tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rx], imm + 1);
        break;
    case 0x1a: /* cmpnei */
        check_insn_except(ctx, CPU_E801);
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rx], imm);
        break;
    case 0x1e:   /* pop */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        if (ctx->bctm) {
            t0 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[sp], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(NULL, 0);
            gen_set_label(l1);
            pop(ctx, imm & 0x1ff);
            break;
        } else {
            pop(ctx, imm & 0x1ff);
            break;
        }
    case 0x1f:    /* push */
        check_insn_except(ctx, CPU_E801 | CPU_E802);
        if (ctx->bctm) {
            t0 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[sp], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(NULL, 0);
            gen_set_label(l1);
            push(ctx, imm & 0x1ff);
            gen_goto_tb(ctx, 1, ctx->pc + 4);
            ctx->base.is_jmp = DISAS_TB_JUMP;
            break;
        } else {
            push(ctx, imm & 0x1ff);
            break;
        }
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void gen_vfp_ld(DisasContext *s, int dp, TCGv addr)
{
    if (dp) {
        /*doesn't cause alignment exception*/
#if !defined TARGET_BIG_ENDIAN
        tcg_gen_qemu_ld_i32(cpu_F0s, addr, s->mem_idx, MO_TEUL);
        tcg_gen_addi_i32(addr, addr, 4);
        tcg_gen_qemu_ld_i32(cpu_F1s, addr, s->mem_idx, MO_TEUL);
        tcg_gen_concat_i32_i64(cpu_F0d, cpu_F0s, cpu_F1s);
#else
        tcg_gen_qemu_ld_i32(cpu_F0s, addr, s->mem_idx, MO_TEUL);
        tcg_gen_addi_i32(addr, addr, 4);
        tcg_gen_qemu_ld_i32(cpu_F1s, addr, s->mem_idx, MO_TEUL);
        tcg_gen_concat_i32_i64(cpu_F0d, cpu_F1s, cpu_F0s);
#endif
    } else {
        tcg_gen_qemu_ld_i32(cpu_F0s, addr, s->mem_idx, MO_TEUL);
    }
}


static inline void gen_vfp_st(DisasContext *s, int dp, TCGv addr)
{
    if (dp) {
        /*doesn't cause alignment exception*/
#if !defined TARGET_BIG_ENDIAN
        tcg_gen_extrl_i64_i32(cpu_F0s, cpu_F0d);
        tcg_gen_qemu_st_i32(cpu_F0s, addr, s->mem_idx, MO_TEUL);
        tcg_gen_shri_i64(cpu_F0d, cpu_F0d, 32);
        tcg_gen_extrl_i64_i32(cpu_F1s, cpu_F0d);
        tcg_gen_addi_i32(addr, addr, 4);
        tcg_gen_qemu_st_i32(cpu_F1s, addr, s->mem_idx, MO_TEUL);
#else
        tcg_gen_extrl_i64_i32(cpu_F0s, cpu_F0d);
        tcg_gen_shri_i64(cpu_F0d, cpu_F0d, 32);
        tcg_gen_extrl_i64_i32(cpu_F1s, cpu_F0d);
        tcg_gen_qemu_st_i32(cpu_F1s, addr, s->mem_idx, MO_TEUL);
        tcg_gen_addi_i32(addr, addr, 4);
        tcg_gen_qemu_st_i32(cpu_F0s, addr, s->mem_idx, MO_TEUL);
#endif
    } else {
        tcg_gen_qemu_st_i32(cpu_F0s, addr, s->mem_idx, MO_TEUL);
    }
}

#define vfp_reg_offset(reg) offsetof(CPUCSKYState, \
    vfp.reg[reg].fpu[0])

#define tcg_gen_ld_f32 tcg_gen_ld_i32
#define tcg_gen_ld_f64 tcg_gen_ld_i64
#define tcg_gen_st_f32 tcg_gen_st_i32
#define tcg_gen_st_f64 tcg_gen_st_i64

static inline void gen_mov_F0_vreg(int dp, int reg)
{
    if (dp) {
        tcg_gen_ld_f64(cpu_F0d, tcg_env, vfp_reg_offset(reg));
    } else {
        tcg_gen_ld_f32(cpu_F0s, tcg_env, vfp_reg_offset(reg));
    }
}

static inline void gen_mov_F0_vreg_hi(int dp, int reg)
{
    if (dp) {
        tcg_gen_ld_f64(cpu_F0d, tcg_env, vfp_reg_offset(reg) + 4);
    } else {
        tcg_gen_ld_f32(cpu_F0s, tcg_env, vfp_reg_offset(reg) + 4);
    }
}

static inline void gen_mov_F1_vreg(int dp, int reg)
{
    if (dp) {
        tcg_gen_ld_f64(cpu_F1d, tcg_env, vfp_reg_offset(reg));
    } else {
        tcg_gen_ld_f32(cpu_F1s, tcg_env, vfp_reg_offset(reg));
    }
}

static inline void gen_mov_F1_vreg_hi(int dp, int reg)
{
    if (dp) {
        tcg_gen_ld_f64(cpu_F1d, tcg_env, vfp_reg_offset(reg) + 4);
    } else {
        tcg_gen_ld_f32(cpu_F1s, tcg_env, vfp_reg_offset(reg) + 4);
    }
}

static inline void gen_mov_vreg_F0(int dp, int reg)
{
    if (dp) {
        tcg_gen_st_f64(cpu_F0d, tcg_env, vfp_reg_offset(reg));
    } else {
        tcg_gen_st_f32(cpu_F0s, tcg_env, vfp_reg_offset(reg));
    }
}

static inline void gen_mov_vreg_F0_hi(int dp, int reg)
{
    if (dp) {
        tcg_gen_st_f64(cpu_F0d, tcg_env, vfp_reg_offset(reg) + 4);
    } else {
        tcg_gen_st_f32(cpu_F0s, tcg_env, vfp_reg_offset(reg) + 4);
    }
}

static inline void gen_vfp_F1_ld0(int dp)
{
    if (dp) {
        tcg_gen_movi_i64(cpu_F1d, 0);
    } else {
        tcg_gen_movi_i32(cpu_F1s, 0);
    }
}

static inline void gen_vfp_add(int dp)
{
    if (dp) {
        gen_helper_vfp_addd(cpu_F0d, cpu_F0d, cpu_F1d, tcg_env);
    } else {
        gen_helper_vfp_adds(cpu_F0s, cpu_F0s, cpu_F1s, tcg_env);
    }
}

static inline void gen_vfp_sub(int dp)
{
    if (dp) {
        gen_helper_vfp_subd(cpu_F0d, cpu_F0d, cpu_F1d, tcg_env);
    } else {
        gen_helper_vfp_subs(cpu_F0s, cpu_F0s, cpu_F1s, tcg_env);
    }
}

static inline void gen_vfp_mul(int dp)
{
    if (dp) {
        gen_helper_vfp_muld(cpu_F0d, cpu_F0d, cpu_F1d, tcg_env);
    } else {
        gen_helper_vfp_muls(cpu_F0s, cpu_F0s, cpu_F1s, tcg_env);
    }
}

static inline void gen_vfp_div(int dp)
{
    if (dp) {
        gen_helper_vfp_divd(cpu_F0d, cpu_F0d, cpu_F1d, tcg_env);
    } else {
        gen_helper_vfp_divs(cpu_F0s, cpu_F0s, cpu_F1s, tcg_env);
    }
}



static inline void gen_vfp_abs(int dp)
{
    if (dp) {
        gen_helper_vfp_absd(cpu_F0d, cpu_F0d);
    } else {
        gen_helper_vfp_abss(cpu_F0s, cpu_F0s);
    }
}

static inline void gen_vfp_neg(int dp)
{
    if (dp) {
        gen_helper_vfp_negd(cpu_F0d, cpu_F0d);
    } else {
        gen_helper_vfp_negs(cpu_F0s, cpu_F0s);
    }
}

static inline void gen_vfp_sqrt(int dp)
{
    if (dp) {
        gen_helper_vfp_sqrtd(cpu_F0d, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_sqrts(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_recip(int dp)
{
    if (dp) {
        gen_helper_vfp_recipd(cpu_F0d, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_recips(cpu_F0s, cpu_F0s, tcg_env);
    }
}


static inline void gen_vfp_cmp_ge(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_ged(cpu_F0d, cpu_F1d, tcg_env);
    } else {
        gen_helper_vfp_cmp_ges(cpu_F0s, cpu_F1s, tcg_env);
    }
}

static inline void gen_vfp_cmp_l(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_ld(cpu_F0d, cpu_F1d, tcg_env);
    } else {
        gen_helper_vfp_cmp_ls(cpu_F0s, cpu_F1s, tcg_env);
    }
}

static inline void gen_vfp_cmp_ls(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_lsd(cpu_F0d, cpu_F1d, tcg_env);
    } else {
        gen_helper_vfp_cmp_lss(cpu_F0s, cpu_F1s, tcg_env);
    }
}

static inline void gen_vfp_cmp_ne(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_ned(cpu_F0d, cpu_F1d, tcg_env);
    } else {
        gen_helper_vfp_cmp_nes(cpu_F0s, cpu_F1s, tcg_env);
    }
}

static inline void gen_vfp_cmp_isNAN(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_isNANd(cpu_F0d, cpu_F1d, tcg_env);
    } else {
        gen_helper_vfp_cmp_isNANs(cpu_F0s, cpu_F1s, tcg_env);
    }
}

static inline void gen_vfp_tosirn(int dp)
{
    if (dp) {
        gen_helper_vfp_tosirnd(cpu_F0s, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_tosirns(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_tosirz(int dp)
{
    if (dp) {
        gen_helper_vfp_tosirzd(cpu_F0s, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_tosirzs(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_tosirpi(int dp)
{
    if (dp) {
        gen_helper_vfp_tosirpid(cpu_F0s, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_tosirpis(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_tosirni(int dp)
{
    if (dp) {
        gen_helper_vfp_tosirnid(cpu_F0s, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_tosirnis(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_touirn(int dp)
{
    if (dp) {
        gen_helper_vfp_touirnd(cpu_F0s, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_touirns(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_touirz(int dp)
{
    if (dp) {
        gen_helper_vfp_touirzd(cpu_F0s, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_touirzs(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_touirpi(int dp)
{
    if (dp) {
        gen_helper_vfp_touirpid(cpu_F0s, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_touirpis(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_touirni(int dp)
{
    if (dp) {
        gen_helper_vfp_touirnid(cpu_F0s, cpu_F0d, tcg_env);
    } else {
        gen_helper_vfp_touirnis(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_uito(int dp)
{
    if (dp) {
        gen_helper_vfp_uitod(cpu_F0d, cpu_F0s, tcg_env);
    } else {
        gen_helper_vfp_uitos(cpu_F0s, cpu_F0s, tcg_env);
    }
}

static inline void gen_vfp_sito(int dp)
{
    if (dp) {
        gen_helper_vfp_sitod(cpu_F0d, cpu_F0s, tcg_env);
    } else {
        gen_helper_vfp_sitos(cpu_F0s, cpu_F0s, tcg_env);
    }
}

/* Move between integer and VFP cores.  */
static TCGv gen_vfp_mrs(void)
{
    TCGv tmp = new_tmp();
    tcg_gen_mov_i32(tmp, cpu_F0s);
    return tmp;
}

static void gen_vfp_msr(TCGv tmp)
{
    tcg_gen_mov_i32(cpu_F0s, tmp);
    dead_tmp(tmp);
}

static inline void fpu_insn_fmovi(int32_t insn)
{
    int vrz = insn & 0xf;
    int imm = (((insn >> 21) & 0xf) << 7) | (((insn >> 4) & 0xf) << 3) | 0x800;
    int pos = (insn >> 16) & 0xf;
    int sign = (insn >> 20) & 0x1;
    int dp = (insn >> 9) & 0x1;
    TCGv_i32 t0, t1, t2;
    t0 = tcg_constant_i32(imm);
    t1 = tcg_constant_i32(pos);
    t2 = tcg_constant_i32(sign);
    if (dp) {
        gen_helper_vfp_fmovid(cpu_F0d, t0, t1, t2, tcg_env);
    } else {
        gen_helper_vfp_fmovis(cpu_F0s, t0, t1, t2, tcg_env);
    }
    gen_mov_vreg_F0(dp, vrz);
}

static void disas_vfp_insn(CPUCSKYState *env, DisasContext *s, uint32_t insn)
{
    int op1, op2, dp, imm, shift, i;
    int vrx, vry, vrz;
    int rx, ry, rz;

    TCGv addr;
    TCGv tmp;
    op1 = (insn >> 8) & 0xff;
    op2 = (insn >> 4) & 0xf;

    switch (op1) {
    case 0x0: /*single-alu*/
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0:/* fadds */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fsubs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fmovs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fabss */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_abs(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fnegs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x1:/*single-compare*/
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0:/* fcmpzhss */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ge(dp);
            break;
        case 0x2:/* fcmpzlss  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ls(dp);
            break;
        case 0x4:/* fcmpznes  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ne(dp);
            break;
        case 0x6:/* fcmpzuos  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vrx);
            gen_vfp_cmp_isNAN(dp);
            break;
        case 0x8:/* fcmphss */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_ge(dp);
            break;
        case 0xa:/* fcmplts */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_l(dp);
            break;
        case 0xc:/* fcmpnes */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_ne(dp);
            break;
        case 0xe:/* fcmpuos */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_isNAN(dp);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x2:/* single-mul */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0:/* fmuls */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fnmuls  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fmacs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fmscs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fnmacs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fnmscs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x3:/* single-div */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0:/* fdivs*/
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_div(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* frecips  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_recip(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x4:/* fsqrts  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_sqrt(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x8:/* double-alu */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0:/* faddd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fsubd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fmovd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fabsd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_abs(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fnegd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;

        }
        break;

    case 0x9:/* double-compare */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0:/* fcmpzhsd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ge(dp);
            break;
        case 0x2:/* fcmpzlsd  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ls(dp);
            break;
        case 0x4:/* fcmpzned  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ne(dp);
            break;
        case 0x6:/* fcmpzuod  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vrx);
            gen_vfp_cmp_isNAN(dp);
            break;
        case 0x8:/* fcmphsd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_ge(dp);
            break;
        case 0xa:/* fcmpltd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_l(dp);
            break;
        case 0xc:/* fcmpned */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_ne(dp);
            break;
        case 0xe:/* fcmpuod */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_isNAN(dp);
            break;
        default:
            goto wrong;
            break;

        }
        break;

    case 0xa:/* double-mul */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0:/* fmuld */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fnmuld  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fmacd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fmscd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fnmacd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fnmscd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xb:/* double-div */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0:/* fdivd*/
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_div(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* frecipd  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_recip(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x4:/* fsqrtd  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_sqrt(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x10:/* simd-alu */
        switch (op2) {
        case 0x0:/* faddm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_add(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0x2:/* fsubm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0x8:/* fmovm */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fabsm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_abs(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_vfp_abs(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0xe:/* fnegm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x12:/* simd-mul  --fixe case 0x11:*/
        switch (op2) {
        case 0x0:/* fmulm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0x2:/* fnmulm  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0x8:/* fmacm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg_hi(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0xa:/* fmscm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg_hi(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0xc:/* fnmacm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg_hi(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0xe:/* fnmscm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg_hi(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        default:
            goto wrong;
            break;

        }
        break;

    case 0x18:/* for-sti */
        switch (op2) {
            /* fstosi */
        case 0x0:/* fstosi.rn */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirn(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fstosi.rz */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirz(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x4:/* fstosi.rpi */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirpi(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x6:/* fstosi.rni */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirni(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
            /* fstoui */
        case 0x8:/* fstoui.rn */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirn(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fstoui.rz */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirz(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fstoui.rpi */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirpi(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fstoui.rni */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirni(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;

        }
        break;

    case 0x19:/* for-dti */
        switch (op2) {
            /* fdtosi */
        case 0x0:/* fdtosi.rn */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirn(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fdtosi.rz */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirz(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x4:/* fdtosi.rpi */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirpi(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x6:/* fdtosi.rni */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirni(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
            /* fdtoui */
        case 0x8:/* fdtoui.rn */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirn(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fdtoui.rz */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirz(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fdtoui.rpi */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirpi(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fdtoui.rni */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirni(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x1a:/* for-misc */
        switch (op2) {
        case 0x0:/* fsitos */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_sito(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fuitos  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_uito(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fsitod */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            dp = 1;
            gen_vfp_sito(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fuitod */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            dp = 1;
            gen_vfp_uito(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fdtos */
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            dp = 1;
            gen_mov_F0_vreg(dp, vrx);
            gen_helper_vfp_tosd(cpu_F0s, cpu_F0d, tcg_env);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fstod */
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            dp = 0;
            gen_mov_F0_vreg(dp, vrx);
            gen_helper_vfp_tods(cpu_F0d, cpu_F0s, tcg_env);
            dp = 1;
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x1b:/* for-fmvr */
        switch (op2) {
        case 0x0:
        case 0x1:/* fmfvrh*/
            vrx = (insn >> 16) & 0xf;
            rz = insn & 0x1f;
            gen_mov_F0_vreg_hi(0, vrx);
            tmp = gen_vfp_mrs();
            tcg_gen_mov_i32(cpu_R[rz], tmp);
            break;
        case 0x2:
        case 0x3:/* fmfvrl */
            vrx = (insn >> 16) & 0xf;
            rz = insn & 0x1f;
            gen_mov_F0_vreg(0, vrx);
            tmp = gen_vfp_mrs();
            tcg_gen_mov_i32(cpu_R[rz], tmp);
            break;
        case 0x4:/* fmtvrh */
            rx = (insn >> 16) & 0x1f;
            vrz = insn & 0xf;
            tmp = load_reg(s, rx);
            gen_vfp_msr(tmp);
            gen_mov_vreg_F0_hi(0, vrz);
            break;
        case 0x6:/* fmtvrl */
            rx = (insn >> 16) & 0x1f;
            vrz = insn & 0xf;
            tmp = load_reg(s, rx);
            gen_vfp_msr(tmp);
            gen_mov_vreg_F0(0, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    /* fmovis */
    case 0x1c:
    case 0x1e:
        fpu_insn_fmovi(insn);
        break;
     /* flsu */
    case 0x20:/* flds */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_vfp_ld(s, 0, addr);
        gen_mov_vreg_F0(0, vrz);
        dead_tmp(addr);
        break;
    case 0x21:/* fldd */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_vfp_ld(s, 1, addr);
        gen_mov_vreg_F0(1, vrz);
        dead_tmp(addr);
        break;
    case 0x22:/* fldm */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 3);
        gen_vfp_ld(s, 1, addr);
        gen_mov_vreg_F0(1, vrz);
        dead_tmp(addr);
        break;
    case 0x24:/* fsts */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_mov_F0_vreg(0, vrz);
        gen_vfp_st(s, 0, addr);
        dead_tmp(addr);
        break;
    case 0x25:/* fstd */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_mov_F0_vreg(1, vrz);
        gen_vfp_st(s, 1, addr);
        dead_tmp(addr);
        break;
    case 0x26:/* fstm */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 3);
        gen_mov_F0_vreg(1, vrz);
        gen_vfp_st(s, 1, addr);
        dead_tmp(addr);
        break;
    case 0x28:/* fldrs */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_vfp_ld(s, 0, addr);
        gen_mov_vreg_F0(0, vrz);
        dead_tmp(addr);
        break;
    case 0x29:/* fldrd */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_vfp_ld(s, 1, addr);
        gen_mov_vreg_F0(1, vrz);
        dead_tmp(addr);
        break;
    case 0x2a:/* fldrm */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_vfp_ld(s, 1, addr);
        gen_mov_vreg_F0(1, vrz);
        dead_tmp(addr);
        break;
    case 0x2c:/* fstrs */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_mov_F0_vreg(0, vrz);
        gen_vfp_st(s, 0, addr);
        dead_tmp(addr);
        break;
    case 0x2d:/* fstrd */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_mov_F0_vreg(1, vrz);
        gen_vfp_st(s, 1, addr);
        dead_tmp(addr);
        break;
    case 0x2e:/* fstrm */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_mov_F0_vreg(1, vrz);
        gen_vfp_st(s, 1, addr);
        dead_tmp(addr);
        break;
    case 0x30:/* fldms */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_vfp_ld(s, 0, addr);
            gen_mov_vreg_F0(0, vrz);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x31:/* fldmd */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_vfp_ld(s, 1, addr);
            gen_mov_vreg_F0(1, vrz);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x32:/* fldmm */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_vfp_ld(s, 1, addr);
            gen_mov_vreg_F0(1, vrz);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x34:/* fstms */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_mov_F0_vreg(0, vrz);
            gen_vfp_st(s, 0, addr);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x35:/* fstmd */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_mov_F0_vreg(1, vrz);
            gen_vfp_st(s, 1, addr);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x36:/* fstmm */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr = load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_mov_F0_vreg(1, vrz);
            gen_vfp_st(s, 1, addr);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
         }
        dead_tmp(addr);
        break;
    default:
wrong:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR, "unknown vdsp insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }

}

static void disas_fpu3_insn(CPUCSKYState *env, DisasContext *s, uint32_t insn)
{
    int op1, op2, go_wrong = 0;
    TCGv fpu3_insn = tcg_constant_tl(insn);

    op1 = (insn >> 8) & 0xff;
    op2 = (insn >> 5) & 0x7;

    switch (op1) {
    case 0x0: /* single-alu */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0: /* fadds */
            gen_helper_fpu3_fadds(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fsubs */
            gen_helper_fpu3_fsubs(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fmovs */
            gen_helper_fpu3_fmovs(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fabss */
            gen_helper_fpu3_fabss(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fnegs */
            gen_helper_fpu3_fnegs(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x1: /* single-compare */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0: /* fcmpzhss */
            gen_helper_fpu3_fcmpzhss(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fcmpzlts  */
            gen_helper_fpu3_fcmpzlts(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fcmpznes/fcmpnez */
            gen_helper_fpu3_fcmpznes(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fcmpzuos  */
            gen_helper_fpu3_fcmpzuos(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fcmphss */
            gen_helper_fpu3_fcmphss(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fcmplts */
            gen_helper_fpu3_fcmplts(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fcmpnes */
            gen_helper_fpu3_fcmpnes(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fcmpuos */
            gen_helper_fpu3_fcmpuos(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x2: /* single-mul */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0: /* fmuls */
            gen_helper_fpu3_fmuls(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fnmuls */
            gen_helper_fpu3_fnmuls(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fmacs */
            gen_helper_fpu3_fmacs(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fmscs */
            gen_helper_fpu3_fmscs(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fnmacs */
            gen_helper_fpu3_fnmacs(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fnmscs */
            gen_helper_fpu3_fnmscs(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x3: /* single-div */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0: /* fdivs*/
            gen_helper_fpu3_fdivs(tcg_env, fpu3_insn);
            break;
        case 0x1: /* frecips  */
            gen_helper_fpu3_frecips(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fsqrts  */
            gen_helper_fpu3_fsqrts(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fins.32 */
            gen_helper_fpu3_fins32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x5: /* single-compare */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0: /* fmaxnm.32 */
            gen_helper_fpu3_fmaxnm32(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fminnm.32 */
            gen_helper_fpu3_fminnm32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fcmphz.32 */
            gen_helper_fpu3_fcmphz32(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fcmplsz.32 */
            gen_helper_fpu3_fcmplsz32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x6: /* single-mul */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0: /* ffmula.32 */
            gen_helper_fpu3_ffmula32(tcg_env, fpu3_insn);
            break;
        case 0x1: /* ffmuls.32 */
            gen_helper_fpu3_ffmuls32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* ffnmula.32 */
            gen_helper_fpu3_ffnmula32(tcg_env, fpu3_insn);
            break;
        case 0x3: /* ffnmuls.32 */
            gen_helper_fpu3_ffnmuls32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x7: /* single-misc */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x1: /* fsel.32 */
            gen_helper_fpu3_fsel32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x8: /* double-alu */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0: /* faddd */
            gen_helper_fpu3_faddd(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fsubd */
            gen_helper_fpu3_fsubd(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fmovd */
            gen_helper_fpu3_fmovd(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fmovx.32 */
            gen_helper_fpu3_fmovx32(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fabsd */
            gen_helper_fpu3_fabsd(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fnegd */
            gen_helper_fpu3_fnegd(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x9: /* double-compare */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0: /* fcmpzhsd */
            gen_helper_fpu3_fcmpzhsd(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fcmpzltd  */
            gen_helper_fpu3_fcmpzltd(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fcmpzned  */
            gen_helper_fpu3_fcmpzned(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fcmpzuod  */
            gen_helper_fpu3_fcmpzuod(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fcmphsd */
            gen_helper_fpu3_fcmphsd(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fcmpltd */
            gen_helper_fpu3_fcmpltd(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fcmpned */
            gen_helper_fpu3_fcmpned(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fcmpuod */
            gen_helper_fpu3_fcmpuod(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xa: /* double-mul */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0: /* fmuld */
            gen_helper_fpu3_fmuld(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fnmuld  */
            gen_helper_fpu3_fnmuld(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fmacd */
            gen_helper_fpu3_fmacd(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fmscd */
            gen_helper_fpu3_fmscd(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fnmacd */
            gen_helper_fpu3_fnmacd(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fnmscd */
            gen_helper_fpu3_fnmscd(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xb: /* double-div */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0: /* fdivd */
            gen_helper_fpu3_fdivd(tcg_env, fpu3_insn);
            break;
        case 0x1: /* frecipd */
            gen_helper_fpu3_frecipd(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fsqrtd */
            gen_helper_fpu3_fsqrtd(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xd: /* double-compare */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0: /* fmaxnm.64 */
            gen_helper_fpu3_fmaxnm64(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fminnm.64 */
            gen_helper_fpu3_fminnm64(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fcmphz.64 */
            gen_helper_fpu3_fcmphz64(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fcmplsz.64 */
            gen_helper_fpu3_fcmplsz64(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xe: /* double-mul */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0: /* ffmula.64 */
            gen_helper_fpu3_ffmula64(tcg_env, fpu3_insn);
            break;
        case 0x1: /* ffmuls.64 */
            gen_helper_fpu3_ffmuls64(tcg_env, fpu3_insn);
            break;
        case 0x2: /* ffnmula.64 */
            gen_helper_fpu3_ffnmula64(tcg_env, fpu3_insn);
            break;
        case 0x3: /* ffnmuls.64 */
            gen_helper_fpu3_ffnmuls64(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xf: /* double-misc */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x1: /* fsel.64 */
            gen_helper_fpu3_fsel64(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x18: /* for-sti */
        switch (op2) {
            /* fstosi */
        case 0x0: /* fstosi.rn */
            gen_helper_fpu3_fstosirn(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fstosi.rz */
            gen_helper_fpu3_fstosirz(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fstosi.rpi */
            gen_helper_fpu3_fstosirpi(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fstosi.rni */
            gen_helper_fpu3_fstosirni(tcg_env, fpu3_insn);
            break;
            /* fstoui */
        case 0x4: /* fstoui.rn */
            gen_helper_fpu3_fstouirn(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fstoui.rz */
            gen_helper_fpu3_fstouirz(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fstoui.rpi */
            gen_helper_fpu3_fstouirpi(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fstoui.rni */
            gen_helper_fpu3_fstouirni(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x19: /* for-dti, fftoi.t1.t2.rm */
        switch (op2) {
            /* fdtosi */
        case 0x0: /* fdtosi.rn */
            gen_helper_fpu3_fdtosirn(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fdtosi.rz */
            gen_helper_fpu3_fdtosirz(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fdtosi.rpi */
            gen_helper_fpu3_fdtosirpi(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fdtosi.rni */
            gen_helper_fpu3_fdtosirni(tcg_env, fpu3_insn);
            break;
            /* fdtoui */
        case 0x4: /* fdtoui.rn */
            gen_helper_fpu3_fdtouirn(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fdtoui.rz */
            gen_helper_fpu3_fdtouirz(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fdtoui.rpi */
            gen_helper_fpu3_fdtouirpi(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fdtoui.rni */
            gen_helper_fpu3_fdtouirni(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x1a: /* for-misc */
        switch (op2) {
        case 0x0: /* fsitos */
            gen_helper_fpu3_fsitos(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fuitos */
            gen_helper_fpu3_fuitos(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fhtos */
            gen_helper_fpu3_fhtos(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fstoh */
            gen_helper_fpu3_fstoh(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fsitod */
            gen_helper_fpu3_fsitod(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fuitod */
            gen_helper_fpu3_fuitod(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fdtos */
            gen_helper_fpu3_fdtos(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fstod */
            gen_helper_fpu3_fstod(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x1b: /* for-fmvr */
        switch (op2) {
        case 0x0: /* fmfvrh */
            gen_helper_fpu3_fmfvrh(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fmfvrl */
            gen_helper_fpu3_fmfvrl(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fmtvrh */
            gen_helper_fpu3_fmtvrh(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fmtvrl */
            gen_helper_fpu3_fmtvrl(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x1c: /* for-hti */
        switch (op2) {
            /* fhtosi */
        case 0x0: /* fhtosi.rn */
            gen_helper_fpu3_fhtosirn(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fhtosi.rz */
            gen_helper_fpu3_fhtosirz(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fhtosi.rpi */
            gen_helper_fpu3_fhtosirpi(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fhtosi.rni */
            gen_helper_fpu3_fhtosirni(tcg_env, fpu3_insn);
            break;
            /* fhtoui */
        case 0x4: /* fhtoui.rn */
            gen_helper_fpu3_fhtouirn(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fhtoui.rz */
            gen_helper_fpu3_fhtouirz(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fhtoui.rpi */
            gen_helper_fpu3_fhtouirpi(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fhtoui.rni */
            gen_helper_fpu3_fhtouirni(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x1e:
        break;
    case 0x1f: /* fmvr */
        switch (op2) {
        case 0x0: /* fmfvr.64 */
            gen_helper_fpu3_fmfvr64(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fmfvr.16 */
            gen_helper_fpu3_fmfvr16(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fmfvr.32.2 */
            gen_helper_fpu3_fmfvr322(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fmtvr.64 */
            gen_helper_fpu3_fmtvr64(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fmtvr.16 */
            gen_helper_fpu3_fmtvr16(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fmtvr.32.2 */
            gen_helper_fpu3_fmtvr322(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

     /* flsu */
    case 0x20: /* flds */
        gen_helper_fpu3_flds(tcg_env, fpu3_insn);
        break;
    case 0x21: /* fldd */
        gen_helper_fpu3_fldd(tcg_env, fpu3_insn);
        break;
    case 0x22: /* fldm */
        gen_helper_fpu3_fldd(tcg_env, fpu3_insn);
        break;
    case 0x23: /* fldh */
        gen_helper_fpu3_fldh(tcg_env, fpu3_insn);
        break;
    case 0x24: /* fsts */
        gen_helper_fpu3_fsts(tcg_env, fpu3_insn);
        break;
    case 0x25: /* fstd */
        gen_helper_fpu3_fstd(tcg_env, fpu3_insn);
        break;
    case 0x26: /* fstm */
        gen_helper_fpu3_fstd(tcg_env, fpu3_insn);
        break;
    case 0x27: /* fsth */
        gen_helper_fpu3_fsth(tcg_env, fpu3_insn);
        break;
    case 0x28: /* fldrs */
        gen_helper_fpu3_fldrs(tcg_env, fpu3_insn);
        break;
    case 0x29: /* fldrd */
        gen_helper_fpu3_fldrd(tcg_env, fpu3_insn);
        break;
    case 0x2a: /* fldrm */
        gen_helper_fpu3_fldrd(tcg_env, fpu3_insn);
        break;
    case 0x2b: /* fldrh */
        gen_helper_fpu3_fldrh(tcg_env, fpu3_insn);
        break;
    case 0x2c: /* fstrs */
        gen_helper_fpu3_fstrs(tcg_env, fpu3_insn);
        break;
    case 0x2d: /* fstrd */
        gen_helper_fpu3_fstrd(tcg_env, fpu3_insn);
        break;
    case 0x2e: /* fstrm */
        gen_helper_fpu3_fstrd(tcg_env, fpu3_insn);
        break;
    case 0x2f: /* fstrh */
        gen_helper_fpu3_fstrh(tcg_env, fpu3_insn);
        break;
    case 0x30:
        switch (op2) {
        case 0x0: /* fldms/fldm.32 */
            gen_helper_fpu3_fldms(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fldmu.s/fldmu.32 */
            gen_helper_fpu3_fldmus(tcg_env, fpu3_insn);
            break;
        default:
            go_wrong = 1;
            goto wrong;
            break;
        }
        break;
    case 0x31:
        switch (op2) {
        case 0x0: /* fldmd/fldm.64 */
            gen_helper_fpu3_fldmd(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fldmu.d/fldmu.64 */
            gen_helper_fpu3_fldmud(tcg_env, fpu3_insn);
            break;
        default:
            go_wrong = 1;
            goto wrong;
            break;
        }
        break;
    case 0x32: /* fldmm */
        gen_helper_fpu3_fldmd(tcg_env, fpu3_insn);
        break;
    case 0x33:
        switch (op2) {
        case 0x0: /* fldmh/fldm.16 */
            gen_helper_fpu3_fldmh(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fldmu.h/fldmu.16 */
            gen_helper_fpu3_fldmuh(tcg_env, fpu3_insn);
            break;
        default:
            go_wrong = 1;
            goto wrong;
            break;
        }
        break;
    case 0x34: /* fstms */
        switch (op2) {
        case 0x0: /* fstms/fstms.32 */
            gen_helper_fpu3_fstms(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fstmu.s/fstmu.32 */
            gen_helper_fpu3_fstmus(tcg_env, fpu3_insn);
            break;
        default:
            go_wrong = 1;
            goto wrong;
            break;
        }
        break;
    case 0x35: /* fstmd */
        switch (op2) {
        case 0x0: /* fstmd/fstms.64 */
            gen_helper_fpu3_fstmd(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fstmu.d/fstmu.64 */
            gen_helper_fpu3_fstmud(tcg_env, fpu3_insn);
            break;
        default:
            go_wrong = 1;
            goto wrong;
            break;
        }
        break;
    case 0x36: /* fstmm */
        gen_helper_fpu3_fstmd(tcg_env, fpu3_insn);
        break;
    case 0x37: /* fstmh */
        switch (op2) {
        case 0x0: /* fstmh/fstmh.16 */
            gen_helper_fpu3_fstmh(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fstmu.h/fstmu.16 */
            gen_helper_fpu3_fstmuh(tcg_env, fpu3_insn);
            break;
        default:
            go_wrong = 1;
            goto wrong;
            break;
        }
        break;
    case 0x38:
        gen_save_pc(s->pc);
        gen_helper_fpu3_flrws(tcg_env, fpu3_insn);
        break;
    case 0x39:
        gen_save_pc(s->pc);
        gen_helper_fpu3_flrwd(tcg_env, fpu3_insn);
        break;
    //case 0x3a:
    //    gen_save_pc(s->pc);
    //    gen_helper_fpu3_flrwh(tcg_env, fpu3_insn);
    //    break;
    case 0x40: /* fftox.t1.t2 */
    case 0x41: /* fftox.t1.t2 */
        gen_helper_fpu3_fftox(tcg_env, fpu3_insn);
        break;
    case 0x42: /* fftoi.t1.t2 */
    case 0x43: /* fftoi.t1.t2 */
        gen_helper_fpu3_fftoi(tcg_env, fpu3_insn);
        break;
    case 0x44: /* fftofi.t.rm */
    case 0x45: /* fftofi.t.rm */
        gen_helper_fpu3_fftofi(tcg_env, fpu3_insn);
        break;
    case 0x48: /* fxtof.t1.t2 */
    case 0x49: /* fxtof.t1.t2 */
        gen_helper_fpu3_fxtof(tcg_env, fpu3_insn);
        break;
    case 0x4a: /* fitof.t1.t2 */
    case 0x4b: /* fitof.t1.t2 */
        gen_helper_fpu3_fitof(tcg_env, fpu3_insn);
        break;

    /*********** SIMD START **************/
    case 0x80:
    case 0x81:
        switch (op2) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6: /* vmuli.16 */
            gen_helper_fpu3_vmuli16(tcg_env, fpu3_insn);
            break;
        case 0x1:
        case 0x3:
        case 0x5:
        case 0x7: /* vmuli.32 */
            gen_helper_fpu3_vmuli32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x82:
    case 0x83:
        switch (op2) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6: /* vmulai.16 */
            gen_helper_fpu3_vmulai16(tcg_env, fpu3_insn);
            break;
        case 0x1:
        case 0x3:
        case 0x5:
        case 0x7: /* vmulai.32 */
            gen_helper_fpu3_vmulai32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x84:
    case 0x85:
        switch (op2) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6: /* vmulsi.16 */
            gen_helper_fpu3_vmulsi16(tcg_env, fpu3_insn);
            break;
        case 0x1:
        case 0x3:
        case 0x5:
        case 0x7: /* vmulsi.32 */
            gen_helper_fpu3_vmulsi32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x86:
    case 0x87:
        switch (op2) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6: /* vfmulai.16.e */
            gen_helper_fpu3_vfmulai16e(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x88:
    case 0x89:
        switch (op2) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6: /* vfmulsi.16.e */
            gen_helper_fpu3_vfmulsi16e(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x8a:
    case 0x8b:
        switch (op2) {
        case 0x0:
        case 0x4: /* vfmulxaai.16 */
            gen_helper_fpu3_vfmulxaai16(tcg_env, fpu3_insn);
            break;
        case 0x1:
        case 0x5: /* vfmulxaai.32 */
            gen_helper_fpu3_vfmulxaai32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x8c:
    case 0x8d:
        switch (op2) {
        case 0x0:
        case 0x4: /* vfmulxasi.16 */
            gen_helper_fpu3_vfmulxasi16(tcg_env, fpu3_insn);
            break;
        case 0x1:
        case 0x5: /* vfmulxasi.32 */
            gen_helper_fpu3_vfmulxasi32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x8e:
    case 0x8f:
        switch (op2) {
        case 0x0:
        case 0x4: /* vfmulxssi.16 */
            gen_helper_fpu3_vfmulxssi16(tcg_env, fpu3_insn);
            break;
        case 0x1:
        case 0x5: /* vfmulxssi.32 */
            gen_helper_fpu3_vfmulxssi32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x90:
    case 0x91:
        switch (op2) {
        case 0x0:
        case 0x4: /* vfmulxsai.16 */
            gen_helper_fpu3_vfmulxsai16(tcg_env, fpu3_insn);
            break;
        case 0x1:
        case 0x5: /* vfmulxsai.32 */
            gen_helper_fpu3_vfmulxsai32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xa0:
        switch (op2) {
        case 0x0: /* vmul.16 */
            gen_helper_fpu3_vmul16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vmul.32 */
            gen_helper_fpu3_vmul32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vmula.16 */
            gen_helper_fpu3_vmula16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vmula.32 */
            gen_helper_fpu3_vmula32(tcg_env, fpu3_insn);
            break;
        case 0x4: /* vmuls.16 */
            gen_helper_fpu3_vmuls16(tcg_env, fpu3_insn);
            break;
        case 0x5: /* vmuls.32 */
            gen_helper_fpu3_vmuls32(tcg_env, fpu3_insn);
            break;
        case 0x6: /* vfmula.16 */
            gen_helper_fpu3_vfmula16(tcg_env, fpu3_insn);
            break;
        case 0x7: /* vfmula.32 */
            gen_helper_fpu3_vfmula32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xa1:
        switch (op2) {
        case 0x0: /* vfmuls.16 */
            gen_helper_fpu3_vfmuls16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vfmuls.32 */
            gen_helper_fpu3_vfmuls32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vfmulxaa.16 */
            gen_helper_fpu3_vfmulxaa16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vfmulxaa.32 */
            gen_helper_fpu3_vfmulxaa32(tcg_env, fpu3_insn);
            break;
        case 0x4: /* vfmulxas.16 */
            gen_helper_fpu3_vfmulxas16(tcg_env, fpu3_insn);
            break;
        case 0x5: /* vfmulxas.32 */
            gen_helper_fpu3_vfmulxas32(tcg_env, fpu3_insn);
            break;
        case 0x6: /* vfmulxss.16 */
            gen_helper_fpu3_vfmulxss16(tcg_env, fpu3_insn);
            break;
        case 0x7: /* vfmulxss.32 */
            gen_helper_fpu3_vfmulxss32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xa2:
        switch (op2) {
        case 0x0: /* vfmulxsa.16 */
            gen_helper_fpu3_vfmulxsa16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vfmulxsa.32 */
            gen_helper_fpu3_vfmulxsa32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vfnmula.16 */
            gen_helper_fpu3_vfnmula16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vfnmula.32 */
            gen_helper_fpu3_vfnmula32(tcg_env, fpu3_insn);
            break;
        case 0x4: /* vfnmuls.16 */
            gen_helper_fpu3_vfnmuls16(tcg_env, fpu3_insn);
            break;
        case 0x5: /* vfnmuls.32 */
            gen_helper_fpu3_vfnmuls32(tcg_env, fpu3_insn);
            break;
        case 0x6: /* vfmula.16.e */
            gen_helper_fpu3_vfmula16e(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xa3:
        switch (op2) {
        case 0x0: /* vfmuls.16.e */
            gen_helper_fpu3_vfmuls16e(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xa4:
        switch (op2) {
        case 0x4: /* vfcmul.16 */
            gen_helper_fpu3_vfcmul16(tcg_env, fpu3_insn);
            break;
        case 0x5: /* vfcmul.32 */
            gen_helper_fpu3_vfcmul32(tcg_env, fpu3_insn);
            break;
        case 0x6: /* vfcmulc.16 */
            gen_helper_fpu3_vfcmulc16(tcg_env, fpu3_insn);
            break;
        case 0x7: /* vfcmulc.32 */
            gen_helper_fpu3_vfcmulc32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xa5:
        switch (op2) {
        case 0x0: /* vfcmuln.16 */
            gen_helper_fpu3_vfcmuln16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vfcmuln.32 */
            gen_helper_fpu3_vfcmuln32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vfcmulcn.16 */
            gen_helper_fpu3_vfcmulcn16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vfcmulcn.32 */
            gen_helper_fpu3_vfcmulcn32(tcg_env, fpu3_insn);
            break;
        case 0x4: /* vfcmula.16 */
            gen_helper_fpu3_vfcmula16(tcg_env, fpu3_insn);
            break;
        case 0x5: /* vfcmula.32 */
            gen_helper_fpu3_vfcmula32(tcg_env, fpu3_insn);
            break;
        case 0x6: /* vfcmulca.16 */
            gen_helper_fpu3_vfcmulca16(tcg_env, fpu3_insn);
            break;
        case 0x7: /* vfcmulca.32 */
            gen_helper_fpu3_vfcmulca32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xa6:
        switch (op2) {
        case 0x0: /* vfcmulna.16 */
            gen_helper_fpu3_vfcmulna16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vfcmulna.32 */
            gen_helper_fpu3_vfcmulna32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vfcmulcna.16 */
            gen_helper_fpu3_vfcmulcna16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vfcmulcna.32 */
            gen_helper_fpu3_vfcmulcna32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xa8:
        switch (op2) {
        case 0x0: /* vadd.16 */
            gen_helper_fpu3_vadd16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vadd.32 */
            gen_helper_fpu3_vadd32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vsub.16 */
            gen_helper_fpu3_vsub16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vsub.32 */
            gen_helper_fpu3_vsub32(tcg_env, fpu3_insn);
            break;
        case 0x4: /* vpadd.16 */
            gen_helper_fpu3_vpadd16(tcg_env, fpu3_insn);
            break;
        case 0x5: /* vpadd.32 */
            gen_helper_fpu3_vpadd32(tcg_env, fpu3_insn);
            break;
        case 0x6: /* vasx.16 */
            gen_helper_fpu3_vasx16(tcg_env, fpu3_insn);
            break;
        case 0x7: /* vasx.32 */
            gen_helper_fpu3_vasx32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xa9:
        switch (op2) {
        case 0x0: /* vsax.16 */
            gen_helper_fpu3_vsax16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vsax.32 */
            gen_helper_fpu3_vsax32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vsabs.16 */
            gen_helper_fpu3_vsabs16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vsabs.32 */
            gen_helper_fpu3_vsabs32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xaa:
        switch (op2) {
        case 0x0: /* vabs.16 */
            gen_helper_fpu3_vabs16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vabs.32 */
            gen_helper_fpu3_vabs32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vneg.16 */
            gen_helper_fpu3_vneg16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vneg.32 */
            gen_helper_fpu3_vneg32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xb0:
        switch (op2) {
        case 0x0: /* vcmpnez.16 */
            gen_helper_fpu3_vcmpne16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vcmpnez.32 */
            gen_helper_fpu3_vcmpne32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vcmphs.16 */
            gen_helper_fpu3_vcmphs16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vcmphs.32 */
            gen_helper_fpu3_vcmphs32(tcg_env, fpu3_insn);
            break;
        case 0x4: /* vcmplt.16 */
            gen_helper_fpu3_vcmplt16(tcg_env, fpu3_insn);
            break;
        case 0x5: /* vcmplt.32 */
            gen_helper_fpu3_vcmplt32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xb1:
        switch (op2) {
        case 0x0: /* vmax.16 */
            gen_helper_fpu3_vmax16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vmax.32 */
            gen_helper_fpu3_vmax32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vmin.16 */
            gen_helper_fpu3_vmin16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vmin.32 */
            gen_helper_fpu3_vmin32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xb2:
        switch (op2) {
        case 0x0: /* vmaxnm.16 */
            gen_helper_fpu3_vmaxnm16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vmaxnm.32 */
            gen_helper_fpu3_vmaxnm32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vminnm.16 */
            gen_helper_fpu3_vminnm16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vminnm.32 */
            gen_helper_fpu3_vminnm32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xb3:
        switch (op2) {
        case 0x0: /* vpmax.16 */
            gen_helper_fpu3_vpmax16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vpmax.32 */
            gen_helper_fpu3_vpmax32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vpmin.16 */
            gen_helper_fpu3_vpmin16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vpmin.32 */
            gen_helper_fpu3_vpmin32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xb4:
        switch (op2) {
        case 0x0: /* vcmpnez.16 */
            gen_helper_fpu3_vcmpnez16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vcmpnez.32 */
            gen_helper_fpu3_vcmpnez32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vcmphsz.16 */
            gen_helper_fpu3_vcmphsz16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vcmphsz.32 */
            gen_helper_fpu3_vcmphsz32(tcg_env, fpu3_insn);
            break;
        case 0x4: /* vcmpltz.16 */
            gen_helper_fpu3_vcmpltz16(tcg_env, fpu3_insn);
            break;
        case 0x5: /* vcmpltz.32 */
            gen_helper_fpu3_vcmpltz32(tcg_env, fpu3_insn);
            break;
        case 0x6: /* vcmphz.16 */
            gen_helper_fpu3_vcmphz16(tcg_env, fpu3_insn);
            break;
        case 0x7: /* vcmphz.32 */
            gen_helper_fpu3_vcmphz32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xb5:
        switch (op2) {
        case 0x0: /* vcmplsz.16 */
            gen_helper_fpu3_vcmplsz16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vcmplsz.32 */
            gen_helper_fpu3_vcmplsz32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xb8:
        switch (op2) {
        case 0x0: /* vrecpe.16 */
            gen_helper_fpu3_vrecpe16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vrecpe.32 */
            gen_helper_fpu3_vrecpe32(tcg_env, fpu3_insn);
            break;
        case 0x2: /* vrecps.16 */
            gen_helper_fpu3_vrecps16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* vrecps.32 */
            gen_helper_fpu3_vrecps32(tcg_env, fpu3_insn);
            break;
        case 0x4: /* vrsqrte.16 */
            gen_helper_fpu3_vrsqrte16(tcg_env, fpu3_insn);
            break;
        case 0x5: /* vrsqrte.32 */
            gen_helper_fpu3_vrsqrte32(tcg_env, fpu3_insn);
            break;
        case 0x6: /* vrsqrts.16 */
            gen_helper_fpu3_vrsqrts16(tcg_env, fpu3_insn);
            break;
        case 0x7: /* vrsqrts.32 */
            gen_helper_fpu3_vrsqrts32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xb9:
        switch (op2) {
        case 0x0: /* vexpe.16 */
            gen_helper_fpu3_vexpe16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* vexpe.32 */
            gen_helper_fpu3_vexpe32(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xd0: /* vhtos.t */
        gen_helper_fpu3_vhtos(tcg_env, fpu3_insn);
        break;
    case 0xd1: /* vstoh.t */
        gen_helper_fpu3_vstoh(tcg_env, fpu3_insn);
        break;
    case 0xd2: /* vdtos.t */
        gen_helper_fpu3_vdtos(tcg_env, fpu3_insn);
        break;
    case 0xd3: /* vftoi.t1.t2 */
        gen_helper_fpu3_vftoi(tcg_env, fpu3_insn);
        break;
    case 0xd4: /* vitof.t1.t2 */
        gen_helper_fpu3_vitof(tcg_env, fpu3_insn);
        break;
    case 0xd8: /* vftox.t1.t2 */
        gen_helper_fpu3_vftox(tcg_env, fpu3_insn);
        break;
    case 0xd9: /* vxtof.t1.t2 */
        gen_helper_fpu3_vxtof(tcg_env, fpu3_insn);
        break;
    case 0xda: /* vftoi.t1.t2.rm */
        gen_helper_fpu3_vftoirm(tcg_env, fpu3_insn);
        break;
    case 0xdb: /* vftofi.t1.t2.rm */
        gen_helper_fpu3_vftofirm(tcg_env, fpu3_insn);
        break;



    /*********** SIMD END ****************/

    case 0xc8: /* half-alu */
        switch (op2) {
        case 0x0: /* faddh */
            gen_helper_fpu3_faddh(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fsubh */
            gen_helper_fpu3_fsubh(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fmovh */
            gen_helper_fpu3_fmovh(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fabsh */
            gen_helper_fpu3_fabsh(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fnegh */
            gen_helper_fpu3_fnegh(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0xc9: /* half-compare */
        switch (op2) {
        case 0x0: /* fcmpzhsh */
            gen_helper_fpu3_fcmpzhsh(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fcmpzlth */
            gen_helper_fpu3_fcmpzlth(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fcmpzneh */
            gen_helper_fpu3_fcmpzneh(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fcmpzuoh */
            gen_helper_fpu3_fcmpzuoh(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fcmphsh */
            gen_helper_fpu3_fcmphsh(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fcmplth */
            gen_helper_fpu3_fcmplth(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fcmpneh */
            gen_helper_fpu3_fcmpneh(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fcmpuoh */
            gen_helper_fpu3_fcmpuoh(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xca: /* half-mul */
        switch (op2) {
        case 0x0: /* fmulh */
            gen_helper_fpu3_fmulh(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fnmulh */
            gen_helper_fpu3_fnmulh(tcg_env, fpu3_insn);
            break;
        case 0x4: /* fmach */
            gen_helper_fpu3_fmach(tcg_env, fpu3_insn);
            break;
        case 0x5: /* fmsch */
            gen_helper_fpu3_fmsch(tcg_env, fpu3_insn);
            break;
        case 0x6: /* fnmach */
            gen_helper_fpu3_fnmach(tcg_env, fpu3_insn);
            break;
        case 0x7: /* fnmsch */
            gen_helper_fpu3_fnmsch(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xcb: /* half-div */
        switch (op2) {
        case 0x0: /* fdivh */
            gen_helper_fpu3_fdivh(tcg_env, fpu3_insn);
            break;
        case 0x1: /* freciph */
            gen_helper_fpu3_freciph(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fsqrth */
            gen_helper_fpu3_fsqrth(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xcd: /* half-compare */
        switch (op2) {
        case 0x0: /* fmaxnm.16 */
            gen_helper_fpu3_fmaxnm16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* fminnm.16 */
            gen_helper_fpu3_fminnm16(tcg_env, fpu3_insn);
            break;
        case 0x2: /* fcmphz.16 */
            gen_helper_fpu3_fcmphz16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* fcmplsz.16 */
            gen_helper_fpu3_fcmplsz16(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xce: /* half-mul */
        switch (op2) {
        case 0x0: /* ffmula.16 */
            gen_helper_fpu3_ffmula16(tcg_env, fpu3_insn);
            break;
        case 0x1: /* ffmuls.16 */
            gen_helper_fpu3_ffmuls16(tcg_env, fpu3_insn);
            break;
        case 0x2: /* ffnmula.16 */
            gen_helper_fpu3_ffnmula16(tcg_env, fpu3_insn);
            break;
        case 0x3: /* ffnmuls.16 */
            gen_helper_fpu3_ffnmuls16(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xcf: /* half-misc */
        switch (op2) {
        case 0x1: /* fsel.16 */
            gen_helper_fpu3_fsel16(tcg_env, fpu3_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xe0 ... 0xe3: /* vmovi.t */
        gen_helper_fpu3_vmovi(tcg_env, fpu3_insn);
        break;
    case 0xe4 ... 0xe7: /* fmovi.t */
        gen_helper_fpu3_fmovi(tcg_env, fpu3_insn);
        break;
    default:
wrong:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR, "unknown fpu insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
    if (go_wrong) {
        return;
    }
// #if defined(CONFIG_USER_ONLY)
//     if (op1 >= 0x20 && op1 <= 0x3a && (watchpoint_count > 0)) {
//         gen_goto_tb(s, 1, s->pc + 4);
//         s->base.is_jmp = DISAS_UPDATE;
//     }
// #endif
}

static void disas_vdsp_insn128(DisasContext *s, uint32_t insn)
{
    int op1, op2, op3, wid, ldst = 0;
    op1 = (insn >> CSKY_VDSP_SOP_SHI_M) & CSKY_VDSP_SOP_MASK_M;
    op2 = (insn >> CSKY_VDSP_SOP_SHI_S) & CSKY_VDSP_SOP_MASK_S;
    op3 = (insn >> CSKY_VDSP_SOP_SHI_E) & CSKY_VDSP_SOP_MASK_E;
    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));

    TCGv vdsp_insn = tcg_constant_tl(insn);

    switch (op1) {
    case VDSP_VADD: /*VADD*/
        switch  (op2) {
        case 0x0:  /*VADD.T*/
            gen_helper_vdsp_vadd128(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VADD.ET*/
            gen_helper_vdsp_vadde128(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VCADD.T*/
            gen_helper_vdsp_vcadd128(tcg_env, vdsp_insn);
            break;
        case 0x3:       /*VCADD.ET*/
            gen_helper_vdsp_vcadde128(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VADD.XT.SL*/
            gen_helper_vdsp_vaddxsl128(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VADD.XT*/
            gen_helper_vdsp_vaddx128(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VADDH.T */
            gen_helper_vdsp_vaddh128(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VADDH.T.R*/
            gen_helper_vdsp_vaddhr128(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VADD.T.S*/
            gen_helper_vdsp_vadds128(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSUB: /*VSUB*/
        switch (op2) {
        case 0x0:       /*VSUB.T*/
            gen_helper_vdsp_vsub128(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VSUB.ET*/
            gen_helper_vdsp_vsube128(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VSABS.T*/
            gen_helper_vdsp_vsabs128(tcg_env, vdsp_insn);
            break;
        case 0x3:       /*VSABS.ET*/
            gen_helper_vdsp_vsabse128(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VSABSA.T*/
            gen_helper_vdsp_vsabsa128(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VSABSA.ET */
            gen_helper_vdsp_vsabsae128(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VSUB.XT */
            gen_helper_vdsp_vsubx128(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VSUBH.T*/
            gen_helper_vdsp_vsubh128(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VSUBH.T.R*/
            gen_helper_vdsp_vsubhr128(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VSUB.T.S*/
            gen_helper_vdsp_vsubs128(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMUL: /*VMUL*/
        switch (op2) {
        case 0x0:       /*VMUL.T*/
            gen_helper_vdsp_vmul128(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VMUL.ET*/
            gen_helper_vdsp_vmule128(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VMULA.T*/
            gen_helper_vdsp_vmula128(tcg_env, vdsp_insn);
            break;
        case 0x3:       /*VMULA.ET*/
            gen_helper_vdsp_vmulae128(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VMULS.T*/
            gen_helper_vdsp_vmuls128(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VMULS.ET*/
            gen_helper_vdsp_vmulse128(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
       break;
    case VDSP_VSH:  /*VSH*/
        switch (op2) {
        case 0x0:       /*VSHRI.T*/
        case 0x1:
            gen_helper_vdsp_vshri128(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VSHRI.T.R*/
        case 0x3:
            gen_helper_vdsp_vshrir128(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VSHR.T*/
            gen_helper_vdsp_vshr128(tcg_env, vdsp_insn);
            break;
        case 0x6:       /*VSHR.T.R*/
            gen_helper_vdsp_vshrr128(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VSHLI.T*/
        case 0x9:
            gen_helper_vdsp_vshli128(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VSHLI.T.S*/
        case 0xb:
            gen_helper_vdsp_vshlis128(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VSHL.T*/
            gen_helper_vdsp_vshl128(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VSHL.T.S*/
            gen_helper_vdsp_vshls128(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VCMP: /*VCMP*/
        switch (op2) {
        case 0x0:       /*VCMPHS.T*/
            gen_helper_vdsp_vcmphs128(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VCMPLT.T*/
            gen_helper_vdsp_vcmplt128(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VCMPNE.T*/
            gen_helper_vdsp_vcmpne128(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VCMPHSZ.T*/
            gen_helper_vdsp_vcmphsz128(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VCMPLTZ.T*/
            gen_helper_vdsp_vcmpltz128(tcg_env, vdsp_insn);
            break;
        case 0x6:       /*VCMPNEZ.T */
            gen_helper_vdsp_vcmpnez128(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VMAX.T*/
            gen_helper_vdsp_vmax128(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VMIN.T*/
            gen_helper_vdsp_vmin128(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VCMAX.T*/
            gen_helper_vdsp_vcmax128(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VCMIN.T*/
            gen_helper_vdsp_vcmin128(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VAND: /*VAND*/
        switch (op2) {
        case 0x0:       /*VAND.T*/
            gen_helper_vdsp_vand128(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VANDN.T*/
            gen_helper_vdsp_vandn128(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VOR.T*/
            gen_helper_vdsp_vor128(tcg_env, vdsp_insn);
            break;
        case 0x3:       /*VNOR.T*/
            gen_helper_vdsp_vnor128(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VXOR.T*/
            gen_helper_vdsp_vxor128(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VTST.T */
            gen_helper_vdsp_vtst128(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMOV: /*VMOV*/
        switch (op2) {
        case 0x0:       /*VMOV*/
            gen_helper_vdsp_vmov128(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VMOV.ET*/
            gen_helper_vdsp_vmove128(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VMOV.T.L*/
            gen_helper_vdsp_vmovl128(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VMOV.T.SL*/
            gen_helper_vdsp_vmovsl128(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VMOV.T.H*/
            gen_helper_vdsp_vmovh128(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VMOV.T.RH*/
            gen_helper_vdsp_vmovrh128(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VSTOU.T.SL*/
            gen_helper_vdsp_vstousl128(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSPE: /*VSPE*/
        switch (op2) {
        case 0x3:       /*VREV.T */
            gen_helper_vdsp_vrev128(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VDUP.T*/
            gen_helper_vdsp_vdup128(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VCNT1.T*/
            gen_helper_vdsp_vcnt1128(tcg_env, vdsp_insn);
            break;
        case 0x6:       /*VCLZ.T*/
            gen_helper_vdsp_vclz128(tcg_env, vdsp_insn);
            break;
        case 0x7:       /*VCLS.T*/
            gen_helper_vdsp_vcls128(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VBPERMZ.T*/
            gen_helper_vdsp_vbpermz128(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VBPERM.T*/
            gen_helper_vdsp_vbperm128(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VTRCH.T*/
            gen_helper_vdsp_vtrch128(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VTRCL.T*/
            gen_helper_vdsp_vtrcl128(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VICH.T*/
            gen_helper_vdsp_vich128(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VICL.T*/
            gen_helper_vdsp_vicl128(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VDCH.T*/
            gen_helper_vdsp_vdch128(tcg_env, vdsp_insn);
            break;
        case 0xf:      /*VDCL.T*/
            gen_helper_vdsp_vdcl128(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VABS: /*VABS*/
        switch (op2) {
        case 0x0:       /*VABS.T*/
            gen_helper_vdsp_vabs128(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VABS.T.S*/
            gen_helper_vdsp_vabss128(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VNEG.T*/
            gen_helper_vdsp_vneg128(tcg_env, vdsp_insn);
            break;
        case 0x6:       /*VNEG.T.S*/
            gen_helper_vdsp_vnegs128(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMVVR:        /*VM*VR*/
        switch (op2) {
        case 0x0:       /*VMFVR.U8*/
            gen_helper_vdsp_vmfvru8(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VMFVR.U16*/
            gen_helper_vdsp_vmfvru16(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VMFVR.U32*/
            gen_helper_vdsp_vmfvru32(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VMFVR.S8*/
            gen_helper_vdsp_vmfvrs8(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VMFVR.S16*/
            gen_helper_vdsp_vmfvrs16(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VMTVR.U8*/
            gen_helper_vdsp_vmtvru8(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VMTVR.U16*/
            gen_helper_vdsp_vmtvru16(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VMTVR.U32*/
            gen_helper_vdsp_vmtvru32(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VINS: /*VINS*/
        switch ((int)(8 * pow(2, wid))) {
        case 8:
            gen_helper_vdsp_vins8(tcg_env, vdsp_insn);
            break;
        case 16:
            gen_helper_vdsp_vins16(tcg_env, vdsp_insn);
            break;
        case 32:
            gen_helper_vdsp_vins32(tcg_env, vdsp_insn);
            break;
        }
        break;
    default:
        ldst = 1;
        switch (op3) {
        case 0x8:       /*VLDD*/
            gen_helper_vdsp_vldd(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VLDQ*/
            gen_helper_vdsp_vldq(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VSTD*/
            gen_helper_vdsp_vstd(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VSTQ*/
            gen_helper_vdsp_vstq(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VLDRD*/
            gen_helper_vdsp_vldrd(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VLDRQ*/
            gen_helper_vdsp_vldrq(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VSTRD*/
            gen_helper_vdsp_vstrd(tcg_env, vdsp_insn);
            break;
        case 0xf:      /*VSTRQ*/
            gen_helper_vdsp_vstrq(tcg_env, vdsp_insn);
            break;
        default:
            ldst = 0;
            goto wrong;
            break;
        }
        break;
    }
    if (!ldst) {
        return;
    }
// #if defined(CONFIG_USER_ONLY)
//     if (watchpoint_count > 0) {
//         gen_goto_tb(s, 1, s->pc + 4);
//         s->base.is_jmp = DISAS_UPDATE;
//     }
// #endif
    return;
wrong:
    generate_exception(s, EXCP_CSKY_UDEF);
    qemu_log_mask(LOG_GUEST_ERROR, "unknown vdsp insn pc=%x opc=%x\n",
              s->pc, insn);
}

static void disas_vdsp_insn64( DisasContext *s, uint32_t insn)
{
    int op1, op2, op3, wid, ldst = 0;
    op1 = (insn >> CSKY_VDSP_SOP_SHI_M) & CSKY_VDSP_SOP_MASK_M;
    op2 = (insn >> CSKY_VDSP_SOP_SHI_S) & CSKY_VDSP_SOP_MASK_S;
    op3 = (insn >> CSKY_VDSP_SOP_SHI_E) & CSKY_VDSP_SOP_MASK_E;
    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));

    TCGv vdsp_insn = tcg_constant_tl(insn);

    switch (op1) {
    case VDSP_VADD: /*VADD*/
        switch (op2) {
        case 0x0:  /*VADD.T*/
            gen_helper_vdsp_vadd64(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VADD.ET*/
            gen_helper_vdsp_vadde64(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VCADD.T*/
            gen_helper_vdsp_vcadd64(tcg_env, vdsp_insn);
            break;
        case 0x3:       /*VCADD.ET*/
            gen_helper_vdsp_vcadde64(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VADD.XT.SL*/
            gen_helper_vdsp_vaddxsl64(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VADD.XT*/
            gen_helper_vdsp_vaddx64(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VADDH.T */
            gen_helper_vdsp_vaddh64(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VADDH.T.R*/
            gen_helper_vdsp_vaddhr64(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VADD.T.S*/
            gen_helper_vdsp_vadds64(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSUB: /*VSUB*/
        switch (op2) {
        case 0x0:       /*VSUB.T*/
            gen_helper_vdsp_vsub64(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VSUB.ET*/
            gen_helper_vdsp_vsube64(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VSABS.T*/
            gen_helper_vdsp_vsabs64(tcg_env, vdsp_insn);
            break;
        case 0x3:       /*VSABS.ET*/
            gen_helper_vdsp_vsabse64(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VSABSA.T*/
            gen_helper_vdsp_vsabsa64(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VSABSA.ET */
            gen_helper_vdsp_vsabsae64(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VSUB.XT */
            gen_helper_vdsp_vsubx64(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VSUBH.T*/
            gen_helper_vdsp_vsubh64(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VSUBH.T.R*/
            gen_helper_vdsp_vsubhr64(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VSUB.T.S*/
            gen_helper_vdsp_vsubs64(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMUL: /*VMUL*/
        switch (op2) {
        case 0x0:       /*VMUL.T*/
            gen_helper_vdsp_vmul64(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VMUL.ET*/
            gen_helper_vdsp_vmule64(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VMULA.T*/
            gen_helper_vdsp_vmula64(tcg_env, vdsp_insn);
            break;
        case 0x3:       /*VMULA.ET*/
            gen_helper_vdsp_vmulae64(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VMULS.T*/
            gen_helper_vdsp_vmuls64(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VMULS.ET*/
            gen_helper_vdsp_vmulse64(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSH:  /*VSH*/
        switch (op2) {
        case 0x0:       /*VSHRI.T*/
        case 0x1:
            gen_helper_vdsp_vshri64(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VSHRI.T.R*/
        case 0x3:
            gen_helper_vdsp_vshrir64(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VSHR.T*/
            gen_helper_vdsp_vshr64(tcg_env, vdsp_insn);
            break;
        case 0x6:       /*VSHR.T.R*/
            gen_helper_vdsp_vshrr64(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VSHLI.T*/
        case 0x9:
            gen_helper_vdsp_vshli64(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VSHLI.T.S*/
        case 0xb:
            gen_helper_vdsp_vshlis64(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VSHL.T*/
            gen_helper_vdsp_vshl64(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VSHL.T.S*/
            gen_helper_vdsp_vshls64(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VCMP: /*VCMP*/
        switch (op2) {
        case 0x0:       /*VCMPHS.T*/
            gen_helper_vdsp_vcmphs64(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VCMPLT.T*/
            gen_helper_vdsp_vcmplt64(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VCMPNE.T*/
            gen_helper_vdsp_vcmpne64(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VCMPHSZ.T*/
            gen_helper_vdsp_vcmphsz64(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VCMPLTZ.T*/
            gen_helper_vdsp_vcmpltz64(tcg_env, vdsp_insn);
            break;
        case 0x6:       /*VCMPNEZ.T */
            gen_helper_vdsp_vcmpnez64(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VMAX.T*/
            gen_helper_vdsp_vmax64(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VMIN.T*/
            gen_helper_vdsp_vmin64(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VCMAX.T*/
            gen_helper_vdsp_vcmax64(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VCMIN.T*/
            gen_helper_vdsp_vcmin64(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VAND: /*VAND*/
        switch (op2) {
        case 0x0:       /*VAND.T*/
            gen_helper_vdsp_vand64(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VANDN.T*/
            gen_helper_vdsp_vandn64(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VOR.T*/
            gen_helper_vdsp_vor64(tcg_env, vdsp_insn);
            break;
        case 0x3:       /*VNOR.T*/
            gen_helper_vdsp_vnor64(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VXOR.T*/
            gen_helper_vdsp_vxor64(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VTST.T */
            gen_helper_vdsp_vtst64(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMOV: /*VMOV*/
        switch (op2) {
        case 0x0:       /*VMOV*/
            gen_helper_vdsp_vmov64(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VMOV.ET*/
            gen_helper_vdsp_vmove64(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VMOV.T.L*/
            gen_helper_vdsp_vmovl64(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VMOV.T.SL*/
            gen_helper_vdsp_vmovsl64(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VMOV.T.H*/
            gen_helper_vdsp_vmovh64(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VMOV.T.RH*/
            gen_helper_vdsp_vmovrh64(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VSTOU.T.SL*/
            gen_helper_vdsp_vstousl64(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSPE: /*VSPE*/
        switch (op2) {
        case 0x3:       /*VREV.T */
            gen_helper_vdsp_vrev64(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VDUP.T*/
            gen_helper_vdsp_vdup64(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VCNT1.T*/
            gen_helper_vdsp_vcnt164(tcg_env, vdsp_insn);
            break;
        case 0x6:       /*VCLZ.T*/
            gen_helper_vdsp_vclz64(tcg_env, vdsp_insn);
            break;
        case 0x7:       /*VCLS.T*/
            gen_helper_vdsp_vcls64(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VBPERMZ.T*/
            gen_helper_vdsp_vbpermz64(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VBPERM.T*/
            gen_helper_vdsp_vbperm64(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VTRCH.T*/
            gen_helper_vdsp_vtrch64(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VTRCL.T*/
            gen_helper_vdsp_vtrcl64(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VICH.T*/
            gen_helper_vdsp_vich64(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VICL.T*/
            gen_helper_vdsp_vicl64(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VDCH.T*/
            gen_helper_vdsp_vdch64(tcg_env, vdsp_insn);
            break;
        case 0xf:      /*VDCL.T*/
            gen_helper_vdsp_vdcl64(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VABS: /*VABS*/
        switch (op2) {
        case 0x0:       /*VABS.T*/
            gen_helper_vdsp_vabs64(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VABS.T.S*/
            gen_helper_vdsp_vabss64(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VNEG.T*/
            gen_helper_vdsp_vneg64(tcg_env, vdsp_insn);
            break;
        case 0x6:       /*VNEG.T.S*/
            gen_helper_vdsp_vnegs64(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMVVR:        /*VM*VR*/
        switch (op2) {
        case 0x0:       /*VMFVR.U8*/
            gen_helper_vdsp_vmfvru8(tcg_env, vdsp_insn);
            break;
        case 0x1:       /*VMFVR.U16*/
            gen_helper_vdsp_vmfvru16(tcg_env, vdsp_insn);
            break;
        case 0x2:       /*VMFVR.U32*/
            gen_helper_vdsp_vmfvru32(tcg_env, vdsp_insn);
            break;
        case 0x4:       /*VMFVR.S8*/
            gen_helper_vdsp_vmfvrs8(tcg_env, vdsp_insn);
            break;
        case 0x5:       /*VMFVR.S16*/
            gen_helper_vdsp_vmfvrs16(tcg_env, vdsp_insn);
            break;
        case 0x8:       /*VMTVR.U8*/
            gen_helper_vdsp_vmtvru8(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VMTVR.U16*/
            gen_helper_vdsp_vmtvru16(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VMTVR.U32*/
            gen_helper_vdsp_vmtvru32(tcg_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VINS: /*VINS*/
        switch ((int)(8 * pow(2, wid))) {
        case 8:
            gen_helper_vdsp_vins8(tcg_env, vdsp_insn);
            break;
        case 16:
            gen_helper_vdsp_vins16(tcg_env, vdsp_insn);
            break;
        case 32:
            gen_helper_vdsp_vins32(tcg_env, vdsp_insn);
            break;
        }
        break;
    default:
        ldst = 1;
        switch (op3) {
        case 0x8:       /*VLDD*/
            gen_helper_vdsp_vldd(tcg_env, vdsp_insn);
            break;
        case 0x9:       /*VLDQ*/
            gen_helper_vdsp_vldq(tcg_env, vdsp_insn);
            break;
        case 0xa:      /*VSTD*/
            gen_helper_vdsp_vstd(tcg_env, vdsp_insn);
            break;
        case 0xb:      /*VSTQ*/
            gen_helper_vdsp_vstq(tcg_env, vdsp_insn);
            break;
        case 0xc:      /*VLDRD*/
            gen_helper_vdsp_vldrd(tcg_env, vdsp_insn);
            break;
        case 0xd:      /*VLDRQ*/
            gen_helper_vdsp_vldrq(tcg_env, vdsp_insn);
            break;
        case 0xe:      /*VSTRD*/
            gen_helper_vdsp_vstrd(tcg_env, vdsp_insn);
            break;
        case 0xf:      /*VSTRQ*/
            gen_helper_vdsp_vstrq(tcg_env, vdsp_insn);
            break;
        default:
            ldst = 0;
            goto wrong;
            break;
        }
        break;
    }

    if (!ldst) {
        return;
    }
// #if defined(CONFIG_USER_ONLY)
//     if (watchpoint_count > 0) {
//         gen_goto_tb(s, 1, s->pc + 4);
//         s->base.is_jmp = DISAS_UPDATE;
//     }
// #endif
    return;
wrong:
    generate_exception(s, EXCP_CSKY_UDEF);
    qemu_log_mask(LOG_GUEST_ERROR, "unknown vdsp insn pc=%x opc=%x\n",
              s->pc, insn);
}

static inline void dspv2_insn_padd_8(int rz, int rx, int ry)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* rz[7:0] = rx[7:0] + ry[7:0]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff);
    tcg_gen_add_i32(t2, t0, t1);
    tcg_gen_andi_i32(t2, t2, 0xff);
    /* rz[15:8] = rx[15:8] + ry[15:8]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff00);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff00);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff00);
    tcg_gen_or_i32(t2, t2, t0);
    /* rz[23:16] = rx[23:16] + ry[23:16]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff0000);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff0000);
    tcg_gen_or_i32(t2, t2, t0);
    /* rz[31:24] = rx[31:24] + ry[31:24]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff000000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff000000);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff000000);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
}

static inline void dspv2_insn_padd_16(int rz, int rx, int ry)
{
    /* rz = {(hi_x + hi_y)[15:0], (lo_x + lo_y)[15:0]} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* lo_x + lo_y */
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_add_i32(t2, t0, t1);
    tcg_gen_andi_i32(t2, t2, 0xffff);
    /* hi_x + hi_y */
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
}

static inline void dspv2_insn_psub_8(int rz, int rx, int ry)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* rz[7:0] = rx[7:0] - ry[7:0]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff);
    tcg_gen_sub_i32(t2, t0, t1);
    tcg_gen_andi_i32(t2, t2, 0xff);
    /* rz[15:8] = rx[15:8] - ry[15:8]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff00);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff00);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff00);
    tcg_gen_or_i32(t2, t2, t0);
    /* rz[23:16] = rx[23:16] - ry[23:16]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff0000);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff0000);
    tcg_gen_or_i32(t2, t2, t0);
    /* rz[31:24] = rx[31:24] - ry[31:24]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff000000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff000000);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff000000);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
}

static inline void dspv2_insn_psub_16(int rz, int rx, int ry)
{
    /* rz = {(hi_x - hi_y)[15:0], (lo_x - lo_y)[15:0]} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_sub_i32(t2, t0, t1);
    tcg_gen_andi_i32(t2, t2, 0xffff);
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
}

static inline void dspv2_insn_addh_s32(int rz, int rx, int ry)
{
    /* rz = (rx[31:0] + ry[31:0]) / 2, signed */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_addh_u32(int rz, int rx, int ry)
{
    /* rz = (rx[31:0] + ry[31:0]) / 2, unsigned */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t0, cpu_R[rx]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_subh_s32(int rz, int rx, int ry)
{
    /* rz = (rx[31:0] - ry[31:0]) / 2, signed */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_sub_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_subh_u32(int rz, int rx, int ry)
{
    /* rz = (rx[31:0] - ry[31:0]) / 2, unsigned */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t0, cpu_R[rx]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_sub_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_add_64(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    csky_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    csky_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    tcg_gen_add_i64(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_sub_64(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    csky_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    csky_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    tcg_gen_sub_i64(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_add_s64_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    csky_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    csky_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    gen_helper_dspv2_add_s64_s(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_add_u64_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    csky_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    csky_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    gen_helper_dspv2_add_u64_s(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_sub_s64_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    csky_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    csky_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    gen_helper_dspv2_sub_s64_s(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_sub_u64_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    csky_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    csky_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    gen_helper_dspv2_sub_u64_s(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_sop_add_sub(DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    switch (thop) {
    case OP_PADD_8_1st:
    case OP_PADD_8_2nd:
        dspv2_insn_padd_8(rz, rx, ry);
        break;
    case OP_PADD_16_1st:
    case OP_PADD_16_2nd:
        dspv2_insn_padd_16(rz, rx, ry);
        break;
    case OP_PADD_U8_S:
        gen_helper_dspv2_padd_u8_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADD_S8_S:
        gen_helper_dspv2_padd_s8_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADD_U16_S:
        gen_helper_dspv2_padd_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADD_S16_S:
        gen_helper_dspv2_padd_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_ADD_U32_S:
        gen_helper_dspv2_add_u32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_ADD_S32_S:
        gen_helper_dspv2_add_s32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUB_8_1st:
    case OP_PSUB_8_2nd:
        dspv2_insn_psub_8(rz, rx, ry);
        break;
    case OP_PSUB_16_1st:
    case OP_PSUB_16_2nd:
        dspv2_insn_psub_16(rz, rx, ry);
        break;
    case OP_PSUB_U8_S:
        gen_helper_dspv2_psub_u8_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUB_S8_S:
        gen_helper_dspv2_psub_s8_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUB_U16_S:
        gen_helper_dspv2_psub_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUB_S16_S:
        gen_helper_dspv2_psub_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_SUB_U32_S:
        gen_helper_dspv2_sub_u32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_SUB_S32_S:
        gen_helper_dspv2_sub_s32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADDH_U8:
        gen_helper_dspv2_paddh_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADDH_S8:
        gen_helper_dspv2_paddh_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADDH_U16:
        gen_helper_dspv2_paddh_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADDH_S16:
        gen_helper_dspv2_paddh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_ADDH_U32:
        dspv2_insn_addh_u32(rz, rx, ry);
        break;
    case OP_ADDH_S32:
        dspv2_insn_addh_s32(rz, rx, ry);
        break;
    case OP_PSUBH_U8:
        gen_helper_dspv2_psubh_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUBH_S8:
        gen_helper_dspv2_psubh_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUBH_U16:
        gen_helper_dspv2_psubh_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUBH_S16:
        gen_helper_dspv2_psubh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_SUBH_U32:
        dspv2_insn_subh_u32(rz, rx, ry);
        break;
    case OP_SUBH_S32:
        dspv2_insn_subh_s32(rz, rx, ry);
        break;
    case OP_ADD_64_1st:
    case OP_ADD_64_2nd:
        dspv2_insn_add_64(rz, rx, ry);
        break;
    case OP_SUB_64_1st:
    case OP_SUB_64_2nd:
        dspv2_insn_sub_64(rz, rx, ry);
        break;
    case OP_ADD_U64_S:
        dspv2_insn_add_u64_s(rz, rx, ry);
        break;
    case OP_ADD_S64_S:
        dspv2_insn_add_s64_s(rz, rx, ry);
        break;
    case OP_SUB_U64_S:
        dspv2_insn_sub_u64_s(rz, rx, ry);
        break;
    case OP_SUB_S64_S:
        dspv2_insn_sub_s64_s(rz, rx, ry);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_pasx_16(int rz, int rx, int ry)
{
    /* rz[31:16] = (rx[31:16] + ry[15:0]),
     * rz[15:0] = (rx[15:0] - ry[31:16]) */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_shli_i32(t1, t1, 16);
    tcg_gen_add_i32(t2, t0, t1);
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_shri_i32(t1, t1, 16);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
}

static inline void dspv2_insn_psax_16(int rz, int rx, int ry)
{
    /* rz[31:16] = (rx[31:16] - ry[15:0]),
     * rz[15:0] = (rx[15:0] + ry[31:16]) */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_shli_i32(t1, t1, 16);
    tcg_gen_sub_i32(t2, t0, t1);
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_shri_i32(t1, t1, 16);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
}

static inline void dspv2_insn_max_u32(int rz, int rx, int ry)
{
    /* rz = max(rx, ry), unsigned */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcond_i32(TCG_COND_GTU, cpu_R[rx], cpu_R[ry], l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[rx]);
    gen_set_label(l2);
}

static inline void dspv2_insn_max_s32(int rz, int rx, int ry)
{
    /* rz = max(rx, ry), signed */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcond_i32(TCG_COND_GT, cpu_R[rx], cpu_R[ry], l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[rx]);
    gen_set_label(l2);
}

static inline void dspv2_insn_pmax_u16(int rz, int rx, int ry)
{
    /* rz = {max(rx[31:16], ry[31:16], max(rx[15:0], ry[15:0])}, unsigned */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    /* lo_rz = max(lo_rx, lo_ry) */
    tcg_gen_ext16u_i32(t0, cpu_R[rx]);
    tcg_gen_ext16u_i32(t1, cpu_R[ry]);
    tcg_gen_brcond_i32(TCG_COND_GTU, t0, t1, l1);
    tcg_gen_mov_i32(t0, t1);
    gen_set_label(l1);
    /* hi_rz = max(hi_rx, hi_ry) */
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(cpu_R[rz], cpu_R[ry], 0xffff0000);
    tcg_gen_brcond_i32(TCG_COND_GTU, t1, cpu_R[rz], l2);
    tcg_gen_mov_i32(t1, cpu_R[rz]);
    gen_set_label(l2);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_pmax_s16(int rz, int rx, int ry)
{
    /* rz = {max(rx[31:16], ry[31:16], max(rx[15:0], ry[15:0])}, signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    /* lo_rz = max(lo_rx, lo_ry) */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_brcond_i32(TCG_COND_GT, t0, t1, l1);
    tcg_gen_mov_i32(t0, t1);
    gen_set_label(l1);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    /* hi_rz = max(hi_rx, hi_ry) */
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(cpu_R[rz], cpu_R[ry], 0xffff0000);
    tcg_gen_brcond_i32(TCG_COND_GT, t1, cpu_R[rz], l2);
    tcg_gen_mov_i32(t1, cpu_R[rz]);
    gen_set_label(l2);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_min_u32(int rz, int rx, int ry)
{
    /* rz = min(rx, ry), unsigned */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcond_i32(TCG_COND_LTU, cpu_R[rx], cpu_R[ry], l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[rx]);
    gen_set_label(l2);
}

static inline void dspv2_insn_min_s32(int rz, int rx, int ry)
{
    /* rz = min(rx, ry), signed */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcond_i32(TCG_COND_LT, cpu_R[rx], cpu_R[ry], l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[rx]);
    gen_set_label(l2);
}

static inline void dspv2_insn_pmin_u16(int rz, int rx, int ry)
{
    /* rz = {min(rx[31:16], ry[31:16], min(rx[15:0], ry[15:0])}, unsigned */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    /* lo_rz = min(lo_rx, lo_ry) */
    tcg_gen_ext16u_i32(t0, cpu_R[rx]);
    tcg_gen_ext16u_i32(t1, cpu_R[ry]);
    tcg_gen_brcond_i32(TCG_COND_LTU, t0, t1, l1);
    tcg_gen_mov_i32(t0, t1);
    gen_set_label(l1);
    /* hi_rz = min(hi_rx, hi_ry) */
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(cpu_R[rz], cpu_R[ry], 0xffff0000);
    tcg_gen_brcond_i32(TCG_COND_LTU, t1, cpu_R[rz], l2);
    tcg_gen_mov_i32(t1, cpu_R[rz]);
    gen_set_label(l2);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_pmin_s16(int rz, int rx, int ry)
{
    /* rz = {min(rx[31:16], ry[31:16], min(rx[15:0], ry[15:0])}, signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    /* lo_rz = min(lo_rx, lo_ry) */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_brcond_i32(TCG_COND_LT, t0, t1, l1);
    tcg_gen_mov_i32(t0, t1);
    gen_set_label(l1);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    /* hi_rz = min(hi_rx, hi_ry) */
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(cpu_R[rz], cpu_R[ry], 0xffff0000);
    tcg_gen_brcond_i32(TCG_COND_LT, t1, cpu_R[rz], l2);
    tcg_gen_mov_i32(t1, cpu_R[rz]);
    gen_set_label(l2);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_sop_cmp(DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    switch (thop) {
    case OP_PASX_16_1st:
    case OP_PASX_16_2nd:
        dspv2_insn_pasx_16(rz, rx, ry);
        break;
    case OP_PSAX_16_1st:
    case OP_PSAX_16_2nd:
        dspv2_insn_psax_16(rz, rx, ry);
        break;
    case OP_PASX_U16_S:
        gen_helper_dspv2_pasx_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PASX_S16_S:
        gen_helper_dspv2_pasx_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSAX_U16_S:
        gen_helper_dspv2_psax_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSAX_S16_S:
        gen_helper_dspv2_psax_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PASXH_U16:
        gen_helper_dspv2_pasxh_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PASXH_S16:
        gen_helper_dspv2_pasxh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSAXH_U16:
        gen_helper_dspv2_psaxh_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSAXH_S16:
        gen_helper_dspv2_psaxh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPNE_8_1st:
    case OP_PCMPNE_8_2nd:
        gen_helper_dspv2_pcmpne_8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPNE_16_1st:
    case OP_PCMPNE_16_2nd:
        gen_helper_dspv2_pcmpne_16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPHS_U8:
        gen_helper_dspv2_pcmphs_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPHS_S8:
        gen_helper_dspv2_pcmphs_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPHS_U16:
        gen_helper_dspv2_pcmphs_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPHS_S16:
        gen_helper_dspv2_pcmphs_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPLT_U8:
        gen_helper_dspv2_pcmplt_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPLT_S8:
        gen_helper_dspv2_pcmplt_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPLT_U16:
        gen_helper_dspv2_pcmplt_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPLT_S16:
        gen_helper_dspv2_pcmplt_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMAX_U8:
        gen_helper_dspv2_pmax_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMAX_S8:
        gen_helper_dspv2_pmax_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMAX_U16:
        dspv2_insn_pmax_u16(rz, rx, ry);
        break;
    case OP_PMAX_S16:
        dspv2_insn_pmax_s16(rz, rx, ry);
        break;
    case OP_MAX_U32:
        dspv2_insn_max_u32(rz, rx, ry);
        break;
    case OP_MAX_S32:
        dspv2_insn_max_s32(rz, rx, ry);
        break;
    case OP_PMIN_U8:
        gen_helper_dspv2_pmin_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMIN_S8:
        gen_helper_dspv2_pmin_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMIN_U16:
        dspv2_insn_pmin_u16(rz, rx, ry);
        break;
    case OP_PMIN_S16:
        dspv2_insn_pmin_s16(rz, rx, ry);
        break;
    case OP_MIN_U32:
        dspv2_insn_min_u32(rz, rx, ry);
        break;
    case OP_MIN_S32:
        dspv2_insn_min_s32(rz, rx, ry);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_sel(int rz, int rx, int ry, int rs)
{
    /* for(i=0;i=31;i++) Rz[i] = Rs[i] ? Rx[i] : Ry[i] */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_and_i32(t0, cpu_R[rx], cpu_R[rs]);
    tcg_gen_not_i32(t1, cpu_R[rs]);
    tcg_gen_and_i32(cpu_R[rz], cpu_R[ry], t1);
    tcg_gen_or_i32(cpu_R[rz], cpu_R[rz], t0);
}

static inline void dspv2_sop_sel(DisasContext *s, uint32_t insn)
{
    int rz, rx, ry, rs;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    rs = (insn >> 5) & CSKY_DSPV2_REG_MASK;
    if ((insn & (1 << 10)) == 0) {
        dspv2_insn_sel(rz, rx, ry, rs);
    } else {
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
    }
}

static inline void dspv2_insn_mulaca_s8(int rz, int rx, int ry)
{
    /* rz = rx[7:0] * ry[7:0] + rx[15:8] * ry[15:8]
     *  + rx[23:16] * ry[23:16] + rx[31:24] * ry[31:24], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    /* rz = rz + ll_x * ll_y */
    tcg_gen_ext8s_i32(t0, cpu_R[rx]);
    tcg_gen_ext8s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t2, t0, t1);
    /* rz = rz + lh_x * lh_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 8);
    tcg_gen_ext8s_i32(t0, t0);
    tcg_gen_sari_i32(t1, cpu_R[ry], 8);
    tcg_gen_ext8s_i32(t1, t1);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_add_i32(t2, t2, t0);
    /* rz= rz + hl_x * hl_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext8s_i32(t0, t0);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_ext8s_i32(t1, t1);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_add_i32(t2, t2, t0);
    /* rz = rz +hh_x * hh_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 24);
    tcg_gen_ext8s_i32(t0, t0);
    tcg_gen_sari_i32(t1, cpu_R[ry], 24);
    tcg_gen_ext8s_i32(t1, t1);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_add_i32(cpu_R[rz], t2, t0);

}

static inline void dspv2_insn_divul(DisasContext *ctx, int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rx+1, rx} / ry */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_R[ry], 0, l1);
    TCGv_i64  t0 = tcg_temp_new_i64();
    TCGv_i64  t1 = tcg_temp_new_i64();
    csky_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_divu_i64(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv_i32 t2;
    t2 = tcg_constant_i32(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(tcg_env, t2);
    ctx->base.is_jmp = DISAS_NEXT;
    gen_set_label(l2);
}

static inline void dspv2_insn_divsl(DisasContext *ctx, int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rx+1, rx} / ry */
    TCGv_i64  t0 = tcg_temp_new_i64();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    TCGLabel *l3 = gen_new_label();
    csky_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_R[ry], 0, l1);
    tcg_gen_brcondi_i64(TCG_COND_NE, t0, 0x8000000000000000, l3);
    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_R[ry], 0xffffffff, l3);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_gen_br(l2);

    gen_set_label(l3);
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_div_i64(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv_i32 t2;
    t2 = tcg_constant_i32(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(tcg_env, t2);
    ctx->base.is_jmp = DISAS_NEXT;

    gen_set_label(l2);
}

static inline void dspv2_sop_misc(DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    if ((thop !=  OP_DIVUL) && (thop != OP_DIVSL)) {
        check_insn(s, ABIV2_DSP2);
    } else {
        check_insn(s, ABIV2_DSP2 | CPU_E803 | CPU_C860);
    }

    switch (thop) {
    case OP_PSABSA_U8_1st:
    case OP_PSABSA_U8_2nd:
        gen_helper_dspv2_psabsa_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSABSAA_U8_1st:
    case OP_PSABSAA_U8_2nd:
        gen_helper_dspv2_psabsaa_u8(cpu_R[rz], cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_DIVUL:
        dspv2_insn_divul(s, rz, rx, ry);
        break;
    case OP_DIVSL:
        dspv2_insn_divsl(s, rz, rx, ry);
        break;
    case OP_MULACA_S8:
        dspv2_insn_mulaca_s8(rz, rx, ry);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_asri_s32_r(int rz, int rx, int imm)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_sari_i64(t0, t0, imm);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_asr_s32_r(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_andi_i64(t1, t1, 0x3f);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_sar_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_lsri_u32_r(int rz, int rx, int imm)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_shri_i64(t0, t0, imm);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_lsr_u32_r(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_andi_i64(t1, t1, 0x3f);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_shr_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_pasri_s16(int rz, int rx, int imm)
{
    /* rz = {rx[31:16] >> imm, rx[15:0] >> imm} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t0, t0, imm);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_sari_i32(t1, t1, imm);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_pasr_s16(int rz, int rx, int ry)
{
    /* rz = {rx[31:16] >> ry, rx[15:0] >> ry} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t2, cpu_R[ry], 0x1f);
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sar_i32(t0, t0, t2);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_sar_i32(t1, t1, t2);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_pasri_s16_r(int rz, int rx, int imm)
{
    /* rz[31:16] = Round(rx[31:16] >> imm),
     * rz[15:0] = Round(rx[15:0] >> imm) */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_shri_i32(t2, t0, imm - 1);
    tcg_gen_andi_i32(t2, t2, 0x1);
    tcg_gen_sari_i32(t0, t0, imm);
    tcg_gen_add_i32(t0, t0, t2);
    tcg_gen_andi_i32(t0, t0, 0xffff);

    tcg_gen_sari_i32(t1, cpu_R[rx], imm);
    tcg_gen_addi_i32(t1, t1, 0x8000);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_pasr_s16_r(int rz, int rx, int ry)
{
    /* rz[31:16] = Round(rx[31:16] >> imm),
     * rz[15:0] = Round(rx[15:0] >> imm) */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();

    tcg_gen_andi_i32(t3, cpu_R[ry], 0x1f);
    tcg_gen_andi_i32(t2, cpu_R[rx], 0xffff);
    tcg_gen_shli_i32(t2, t2, 16);
    tcg_gen_sar_i32(t2, t2, t3);
    tcg_gen_addi_i32(t2, t2, 0x8000);
    tcg_gen_shri_i32(t2, t2, 16);
    tcg_gen_andi_i32(t0, t2, 0xffff);

    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_sar_i32(t1, t1, t3);
    tcg_gen_addi_i32(t1, t1, 0x8000);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_plsri_u16(int rz, int rx, int imm)
{
    /* rz = {rx[31:16] >> imm, rx[15:0] >> imm} logical shift */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(t0, cpu_R[rx]);
    tcg_gen_shri_i32(t0, t0, imm);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_shri_i32(t1, t1, imm);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_plsr_u16(int rz, int rx, int ry)
{
    /* rz = {rx[31:16] >> imm, rx[15:0] >> imm} logical shift */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t2, cpu_R[ry], 0x1f);
    tcg_gen_ext16u_i32(t0, cpu_R[rx]);
    tcg_gen_shr_i32(t0, t0, t2);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_shr_i32(t1, t1, t2);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_plsri_u16_r(int rz, int rx, int imm)
{
    /* rz[31:16] = Round(rx[31:16] >> imm),
     * rz[15:0] = Round(rx[15:0] >> imm) */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i32 t3 = tcg_temp_new_i32();

    tcg_gen_andi_i32(t3, cpu_R[rx], 0xffff);
    tcg_gen_ext_i32_i64(t0, t3);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_shri_i64(t0, t0, imm);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_andi_i64(t0, t0, 0xffff00000000);


    tcg_gen_andi_i32(t3, cpu_R[rx], 0xffff0000);
    tcg_gen_ext_i32_i64(t1, t3);
    tcg_gen_shli_i64(t1, t1, 32);
    tcg_gen_shri_i64(t1, t1, imm);
    tcg_gen_addi_i64(t1, t1, 0x800000000000);
    tcg_gen_andi_i64(t1, t1, 0xffff000000000000);


    tcg_gen_or_i64(t2, t0, t1);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t2);
}

static inline void dspv2_insn_plsr_u16_r(int rz, int rx, int ry)
{
    /* rz[31:16] = Round(rx[31:16] >> imm),
     * rz[15:0] = Round(rx[15:0] >> imm) */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i32 t3 = tcg_temp_new_i32();
    TCGv_i32 t4 = tcg_temp_new_i32();

    tcg_gen_andi_i32(t3, cpu_R[ry], 0x1f);
    tcg_gen_ext_i32_i64(t1, t3);
    tcg_gen_andi_i32(t4, cpu_R[rx], 0xffff);
    tcg_gen_ext_i32_i64(t0, t4);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_shr_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_andi_i64(t0, t0, 0xffff00000000);


    tcg_gen_andi_i32(t4, cpu_R[rx], 0xffff0000);
    tcg_gen_ext_i32_i64(t2, t4);
    tcg_gen_shli_i64(t2, t2, 32);
    tcg_gen_shr_i64(t2, t2, t1);
    tcg_gen_addi_i64(t2, t2, 0x800000000000);
    tcg_gen_andi_i64(t2, t2, 0xffff000000000000);


    tcg_gen_or_i64(t1, t0, t2);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t1);

}

static inline void dspv2_insn_plsli_u16(int rz, int rx, int imm)
{
    /* rz = {rx[31:16] << imm, rx[15:0] << imm} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_shli_i32(t0, t0, imm);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_shli_i32(t1, t1, imm);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_plsl_u16(int rz, int rx, int ry)
{
    /* rz = {rx[31:16] >> imm, rx[15:0] >> imm}, logical shift */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t2, cpu_R[ry], 0x1f);
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_shl_i32(t0, t0, t2);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_shl_i32(t1, t1, t2);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_sop_shift(DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry, imm;
    TCGv t0;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    imm = ry + 1;  /* bits ry equal to oimm. */

    switch (thop) {
    case OP_ASRI_S32_R:
        dspv2_insn_asri_s32_r(rz, rx, imm);
        break;
    case OP_ASR_S32_R:
        dspv2_insn_asr_s32_r(rz, rx, ry);
        break;
    case OP_LSRI_U32_R:
        dspv2_insn_lsri_u32_r(rz, rx, imm);
        break;
    case OP_LSR_U32_R:
        dspv2_insn_lsr_u32_r(rz, rx, ry);
        break;
    case OP_LSLI_U32_S:
        t0 = tcg_constant_tl(imm);
        gen_helper_dspv2_lsli_u32_s(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_LSLI_S32_S:
        t0 = tcg_constant_tl(imm);
        gen_helper_dspv2_lsli_s32_s(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_LSL_U32_S:
        gen_helper_dspv2_lsl_u32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_LSL_S32_S:
        gen_helper_dspv2_lsl_s32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PASRI_S16:
        dspv2_insn_pasri_s16(rz, rx, imm);
        break;
    case OP_PASR_S16:
        dspv2_insn_pasr_s16(rz, rx, ry);
        break;
    case OP_PASRI_S16_R:
        dspv2_insn_pasri_s16_r(rz, rx, imm);
        break;
    case OP_PASR_S16_R:
        dspv2_insn_pasr_s16_r(rz, rx, ry);
        break;
    case OP_PLSRI_U16:
        dspv2_insn_plsri_u16(rz, rx, imm);
        break;
    case OP_PLSR_U16:
        dspv2_insn_plsr_u16(rz, rx, ry);
        break;
    case OP_PLSRI_U16_R:
        dspv2_insn_plsri_u16_r(rz, rx, imm);
        break;
    case OP_PLSR_U16_R:
        dspv2_insn_plsr_u16_r(rz, rx, ry);
        break;
    case OP_PLSLI_U16:
        dspv2_insn_plsli_u16(rz, rx, imm);
        break;
    case OP_PLSL_U16:
        dspv2_insn_plsl_u16(rz, rx, ry);
        break;
    case OP_PLSLI_U16_S:
        t0 = tcg_constant_tl(imm);
        gen_helper_dspv2_plsli_u16_s(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_PLSLI_S16_S:
        t0 = tcg_constant_tl(imm);
        gen_helper_dspv2_plsli_s16_s(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_PLSL_U16_S:
        gen_helper_dspv2_plsl_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PLSL_S16_S:
        gen_helper_dspv2_plsl_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_pkg(int rz, int rx, int imm4a, int ry, int imm4b)
{
    /* rz = {(ry >> imm4b)[15:0], (rx >> imm4a)[15:0]}, logical shift */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_shri_i32(t0, cpu_R[rx], imm4a);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_shli_i32(t1, cpu_R[ry], 16 - imm4b);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_sop_pkg(DisasContext *s, uint32_t insn)
{
    int rz, rx, ry, imm4a, imm4b;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    imm4a = (insn >> 5) & 0xf;
    imm4b = ((insn >> 9) & 0xf) + 1;
    dspv2_insn_pkg(rz, rx, imm4a, ry, imm4b);

}

static inline void dspv2_insn_dexti(int rz, int rx, int ry, int imm)
{
        /* rz = ({ry, rx} >> imm)[31:0] */
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[ry]);
        tcg_gen_shri_i64(t0, t0, imm);
        tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_dext(int rz, int rx, int ry, int rs)
{
        /* rz = ({ry, rx} >> rs)[31:0]
         * NOTE: 0 <= rs <= 32. */
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i32 t1 = tcg_temp_new_i32();
        TCGv_i64 t2 = tcg_temp_new_i64();
        tcg_gen_andi_i32(t1, cpu_R[rs], 0x3f);
        tcg_gen_ext_i32_i64(t2, t1);
        tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[ry]);
        tcg_gen_shr_i64(t0, t0, t2);
        tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_sop_dext(DisasContext *s, uint32_t insn)
{
    int rz, rx, ry;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    if ((insn & (1 << 10)) == 0) {
        /* dexti */
        int imm5 = (insn >> 5) & 0x1f;
        dspv2_insn_dexti(rz, rx, ry, imm5);
    } else {
        /* dext */
        int rs = (insn >> 5) & CSKY_DSPV2_REG_MASK;
        dspv2_insn_dext(rz, rx, ry, rs);
    }
}

static inline void dspv2_insn_pkgll(int rz, int rx, int ry)
{
    /* rz = {ry[15:0], rx[15:0]} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_shli_i32(t1, t1, 16);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_insn_pkghh(int rz, int rx, int ry)
{
    /* rz = {ry[31:16], rx[31:16]} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_shri_i32(t0, t0, 16);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
}

static inline void dspv2_sop_pkg_clip(DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry, index;
    TCGv_i32 t0;
    TCGv_i64 t1 = tcg_temp_new_i64();
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    switch (thop) {
    case OP_PKGLL_1st:
    case OP_PKGLL_2nd:
        dspv2_insn_pkgll(rz, rx, ry);
        break;
    case OP_PKGHH_1st:
    case OP_PKGHH_2nd:
        dspv2_insn_pkghh(rz, rx, ry);
        break;
    case OP_PEXT_U8_E:
        if (ry == 0) {
            gen_helper_dspv2_pext_u8_e(t1, cpu_R[rx]);
            tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t1);
        } else {
            goto wrong;
        }
        break;
    case OP_PEXT_S8_E:
        if (ry == 0) {
            gen_helper_dspv2_pext_s8_e(t1, cpu_R[rx]);
            tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t1);
        } else {
            goto wrong;
        }
        break;
    case OP_PEXTX_U8_E:
        if (ry == 0) {
            gen_helper_dspv2_pextx_u8_e(t1, cpu_R[rx]);
            tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t1);
        } else {
            goto wrong;
        }
        break;
    case OP_PEXTX_S8_E:
        if (ry == 0) {
            gen_helper_dspv2_pextx_s8_e(t1, cpu_R[rx]);
            tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t1);
        } else {
            goto wrong;
        }
        break;
    case OP_NARL_1st:
    case OP_NARL_2nd:
        gen_helper_dspv2_narl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_NARH_1st:
    case OP_NARH_2nd:
        gen_helper_dspv2_narh(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_NARLX_1st:
    case OP_NARLX_2nd:
        gen_helper_dspv2_narlx(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_NARHX_1st:
    case OP_NARHX_2nd:
        gen_helper_dspv2_narhx(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_CLIPI_U32:
        t0 = tcg_constant_i32(ry);
        gen_helper_dspv2_clipi_u32(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_CLIPI_S32:
        t0 = tcg_constant_i32(ry);
        gen_helper_dspv2_clipi_s32(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_CLIP_U32:
        gen_helper_dspv2_clip_u32(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_CLIP_S32:
        gen_helper_dspv2_clip_s32(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCLIPI_U16:
        t0 = tcg_constant_i32(ry);
        gen_helper_dspv2_pclipi_u16(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_PCLIPI_S16:
        t0 = tcg_constant_i32(ry);
        gen_helper_dspv2_pclipi_s16(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_PCLIP_U16:
        gen_helper_dspv2_pclip_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCLIP_S16:
        gen_helper_dspv2_pclip_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PABS_S8_S:
        if (ry == 0) {
            gen_helper_dspv2_pabs_s8_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_PABS_S16_S:
        if (ry == 0) {
            gen_helper_dspv2_pabs_s16_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_ABS_S32_S:
        if (ry == 0) {
            gen_helper_dspv2_abs_s32_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_PNEG_S8_S:
        if (ry == 0) {
            gen_helper_dspv2_pneg_s8_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_PNEG_S16_S:
        if (ry == 0) {
            gen_helper_dspv2_pneg_s16_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_NEG_S32_S:
        if (ry == 0) {
            gen_helper_dspv2_neg_s32_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_DUP_8_begin ... OP_DUP_8_end:
        if (ry == 0) {
            index = (insn >> 5) & 0x3;
            t0 = tcg_constant_i32(index);
            gen_helper_dspv2_dup_8(cpu_R[rz], cpu_R[rx], t0);
        } else {
            goto wrong;
        }
        break;
    case OP_DUP_16_begin ... OP_DUP_16_end:
        if (ry == 0) {
            index = (insn >> 5) & 0x1;
            t0 = tcg_constant_i32(index);
            gen_helper_dspv2_dup_16(cpu_R[rz], cpu_R[rx], t0);
        } else {
            goto wrong;
        }
        break;
    default:
wrong:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_mul_u32(int rz, int rx, int ry)
{
#if TARGET_BIG_ENDIAN
    tcg_gen_mulu2_i32(cpu_R[(rz + 1) % 32], cpu_R[rz], cpu_R[rx], cpu_R[ry]);
#else
    tcg_gen_mulu2_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], cpu_R[rx], cpu_R[ry]);
#endif
}

static inline void dspv2_insn_mul_s32(int rz, int rx, int ry)
{
#if TARGET_BIG_ENDIAN
    tcg_gen_muls2_i32(cpu_R[(rz + 1) % 32], cpu_R[rz], cpu_R[rx], cpu_R[ry]);
#else
    tcg_gen_muls2_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], cpu_R[rx], cpu_R[ry]);
#endif
}

static inline void dspv2_insn_mula_u32(int rz, int rx, int ry)
{
    /* {r[z+1],rz} = (rx * ry) + {r[z+1],rz}, unsigned */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t0, cpu_R[rx]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    csky_concat_i32_i64(t1, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    tcg_gen_add_i64(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}


static inline void dspv2_insn_mula_s32(int rz, int rx, int ry)
{
    /* {r[z+1],rz} = (rx * ry) + {r[z+1],rz}, signed */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    csky_concat_i32_i64(t1, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    tcg_gen_add_i64(t0, t0, t1);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_muls_u32(int rz, int rx, int ry)
{
    /* {Rz+1[31:0],Rz[31:0]} = {Rz+1[31:0],Rz[31:0]} + Rx[31:0] X Ry[31:0] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t0, cpu_R[rx]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    csky_concat_i32_i64(t1, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    tcg_gen_sub_i64(t0, t1, t0);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_muls_s32(int rz, int rx, int ry)
{
    /* {Rz+1[31:0],Rz[31:0]} = {Rz+1[31:0],Rz[31:0]} + Rx[31:0] X Ry[31:0] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    csky_concat_i32_i64(t1, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    tcg_gen_sub_i64(t0, t1, t0);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_mula_u32_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
#if TARGET_BIG_ENDIAN
    gen_helper_dspv2_mula_u32_s(t0, cpu_R[(rz + 1) % 32], cpu_R[rz],
                                cpu_R[rx], cpu_R[ry]);
#else
    gen_helper_dspv2_mula_u32_s(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                cpu_R[rx], cpu_R[ry]);
#endif
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_mula_s32_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
#if TARGET_BIG_ENDIAN
    gen_helper_dspv2_mula_s32_s(t0, cpu_R[(rz + 1) % 32], cpu_R[rz],
                                cpu_R[rx], cpu_R[ry]);
#else
    gen_helper_dspv2_mula_s32_s(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                cpu_R[rx], cpu_R[ry]);
#endif
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_muls_u32_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
#if TARGET_BIG_ENDIAN
    gen_helper_dspv2_muls_u32_s(t0, cpu_R[(rz + 1) % 32], cpu_R[rz],
                                cpu_R[rx], cpu_R[ry]);
#else
    gen_helper_dspv2_muls_u32_s(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                cpu_R[rx], cpu_R[ry]);
#endif
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_muls_s32_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
#if TARGET_BIG_ENDIAN
    gen_helper_dspv2_muls_s32_s(t0, cpu_R[(rz + 1) % 32], cpu_R[rz],
                                cpu_R[rx], cpu_R[ry]);
#else
    gen_helper_dspv2_muls_s32_s(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                cpu_R[rx], cpu_R[ry]);
#endif
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_mul_s32_h(int rz, int rx, int ry)
{
    /* rz = (rx * ry)[63:32], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_muls2_i32(t0, cpu_R[rz], cpu_R[rx], cpu_R[ry]);
}

static inline void dspv2_insn_mul_s32_rh(int rz, int rx, int ry)
{
    /* rz = (rx * ry + 0x80000000)[63:32] , signed */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_mulll_s16(int rz, int rx, int ry)
{
    /* rz = rx[15:0] * ry[15:0], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_mul_i32(cpu_R[rz], t0, cpu_R[rz]);
}

static inline void dspv2_insn_mulhh_s16(int rz, int rx, int ry)
{
    /* rz = rx[31:16] * ry[31:16], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(cpu_R[rz], cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t0, cpu_R[rz]);
}

static inline void dspv2_insn_mulhl_s16(int rz, int rx, int ry)
{
    /* rz = rx[31:16] * ry[15:0], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_mul_i32(cpu_R[rz], t0, cpu_R[rz]);
}

static inline void dspv2_insn_mulall_s16_e(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
#if TARGET_BIG_ENDIAN
    gen_helper_dspv2_mulall_s16_e(t0, cpu_R[(rz + 1) % 32], cpu_R[rz],
                                  cpu_R[rx], cpu_R[ry]);
#else
    gen_helper_dspv2_mulall_s16_e(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                  cpu_R[rx], cpu_R[ry]);
#endif
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_mulahh_s16_e(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
#if TARGET_BIG_ENDIAN
    gen_helper_dspv2_mulahh_s16_e(t0, cpu_R[(rz + 1) % 32], cpu_R[rz],
                                  cpu_R[rx], cpu_R[ry]);
#else
    gen_helper_dspv2_mulahh_s16_e(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                  cpu_R[rx], cpu_R[ry]);
#endif
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_mulahl_s16_e(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
#if TARGET_BIG_ENDIAN
    gen_helper_dspv2_mulahl_s16_e(t0, cpu_R[(rz + 1) % 32], cpu_R[rz],
                                  cpu_R[rx], cpu_R[ry]);
#else
    gen_helper_dspv2_mulahl_s16_e(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                  cpu_R[rx], cpu_R[ry]);
#endif
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_pmul_u16(int rz, int rx, int ry)
{
    /* rz = lo_x * lo_y, rn = hi_x * hi_y, unsigned */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();
    /* lo_x, hi_x */
    tcg_gen_ext16u_i32(t2, cpu_R[rx]);
    tcg_gen_shri_i32(t3, cpu_R[rx], 16);
    /* lo_y, hi_y */
    tcg_gen_ext16u_i32(t0, cpu_R[ry]);
    tcg_gen_shri_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t2, t0);
    tcg_gen_mul_i32(cpu_R[(rz + 1) % 32], t3, t1);
}

static inline void dspv2_insn_pmul_s16(int rz, int rx, int ry)
{
    /* rz = lo_x * lo_y, rn = hi_x * hi_y, signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();
    /* lo_x, hi_x */
    tcg_gen_ext16s_i32(t2, cpu_R[rx]);
    tcg_gen_sari_i32(t3, cpu_R[rx], 16);
    /* lo_y, hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[ry]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t2, t0);
    tcg_gen_mul_i32(cpu_R[(rz + 1) % 32], t3, t1);
}

static inline void dspv2_insn_pmulx_u16(int rz, int rx, int ry)
{
    /* rz = lo_x * lo_y, rn = hi_x * hi_y, unsigned */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();
    /* lo_x, hi_x */
    tcg_gen_ext16u_i32(t2, cpu_R[rx]);
    tcg_gen_shri_i32(t3, cpu_R[rx], 16);
    /* lo_y, hi_y */
    tcg_gen_ext16u_i32(t0, cpu_R[ry]);
    tcg_gen_shri_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t2, t1);
    tcg_gen_mul_i32(cpu_R[(rz + 1) % 32], t3, t0);
}

static inline void dspv2_insn_pmulx_s16(int rz, int rx, int ry)
{
    /* rz = lo_x * lo_y, rn = hi_x * hi_y, signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();
    /* lo_x, hi_x */
    tcg_gen_ext16s_i32(t2, cpu_R[rx]);
    tcg_gen_sari_i32(t3, cpu_R[rx], 16);
    /* lo_y, hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[ry]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t2, t1);
    tcg_gen_mul_i32(cpu_R[(rz + 1) % 32], t3, t0);
}

static inline void dspv2_insn_prmul_s16(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_prmul_s16(t0, cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_insn_prmulx_s16(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_prmulx_s16(t0, cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
}

static inline void dspv2_sop_mul_1st(DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    if (thop != OP_MULA_32_L && thop != OP_MULALL_S16_S
        && thop != OP_MUL_U32 && thop != OP_MULA_U32
        && thop != OP_MUL_S32 && thop != OP_MULA_S32) {
        check_insn(s, ABIV2_DSP2);
    } else {
        check_insn(s, CPU_E803 | CPU_C860);
    }
    switch (thop) {
    case OP_MUL_U32:
        dspv2_insn_mul_u32(rz, rx, ry);
        break;
    case OP_MUL_S32:
        dspv2_insn_mul_s32(rz, rx, ry);
        break;
    case OP_MULA_U32:
        dspv2_insn_mula_u32(rz, rx, ry);
        break;
    case OP_MULA_S32:
        dspv2_insn_mula_s32(rz, rx, ry);
        break;
    case OP_MULS_U32:
        dspv2_insn_muls_u32(rz, rx, ry);
        break;
    case OP_MULS_S32:
        dspv2_insn_muls_s32(rz, rx, ry);
        break;
    case OP_MULA_U32_S:
        dspv2_insn_mula_u32_s(rz, rx, ry);
        break;
    case OP_MULA_S32_S:
        dspv2_insn_mula_s32_s(rz, rx, ry);
        break;
    case OP_MULS_U32_S:
        dspv2_insn_muls_u32_s(rz, rx, ry);
        break;
    case OP_MULS_S32_S:
        dspv2_insn_muls_s32_s(rz, rx, ry);
        break;
    case OP_MULA_32_L:
        gen_helper_dspv2_mula_32_l(cpu_R[rz], cpu_R[rz],
                                   cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MUL_S32_H:
        dspv2_insn_mul_s32_h(rz, rx, ry);
        break;
    case OP_MUL_S32_RH:
        dspv2_insn_mul_s32_rh(rz, rx, ry);
        break;
    case OP_RMUL_S32_H:
        gen_helper_dspv2_rmul_s32_h(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMUL_S32_RH:
        gen_helper_dspv2_rmul_s32_rh(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULA_S32_HS:
        gen_helper_dspv2_mula_s32_hs(cpu_R[rz], cpu_R[rz],
                                     cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULS_S32_HS:
        gen_helper_dspv2_muls_s32_hs(cpu_R[rz], cpu_R[rz],
                                     cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULA_S32_RHS:
        gen_helper_dspv2_mula_s32_rhs(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULS_S32_RHS:
        gen_helper_dspv2_muls_s32_rhs(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULLL_S16:
        dspv2_insn_mulll_s16(rz, rx, ry);
        break;
    case OP_MULHH_S16:
        dspv2_insn_mulhh_s16(rz, rx, ry);
        break;
    case OP_MULHL_S16:
        dspv2_insn_mulhl_s16(rz, rx, ry);
        break;
    case OP_RMULLL_S16:
        gen_helper_dspv2_rmulll_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULHH_S16:
        gen_helper_dspv2_rmulhh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULHL_S16:
        gen_helper_dspv2_rmulhl_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULALL_S16_S:
        gen_helper_dspv2_mulall_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAHH_S16_S:
        gen_helper_dspv2_mulahh_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAHL_S16_S:
        gen_helper_dspv2_mulahl_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULALL_S16_E:
        dspv2_insn_mulall_s16_e(rz, rx, ry);
        break;
    case OP_MULAHH_S16_E:
        dspv2_insn_mulahh_s16_e(rz, rx, ry);
        break;
    case OP_MULAHL_S16_E:
        dspv2_insn_mulahl_s16_e(rz, rx, ry);
        break;
    case OP_PMUL_U16:
        dspv2_insn_pmul_u16(rz, rx, ry);
        break;
    case OP_PMULX_U16:
        dspv2_insn_pmulx_u16(rz, rx, ry);
        break;
    case OP_PMUL_S16:
        dspv2_insn_pmul_s16(rz, rx, ry);
        break;
    case OP_PMULX_S16:
        dspv2_insn_pmulx_s16(rz, rx, ry);
        break;
    case OP_PRMUL_S16:
        dspv2_insn_prmul_s16(rz, rx, ry);
        break;
    case OP_PRMULX_S16:
        dspv2_insn_prmulx_s16(rz, rx, ry);
        break;
    case OP_PRMUL_S16_H:
        gen_helper_dspv2_prmul_s16_h(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PRMUL_S16_RH:
        gen_helper_dspv2_prmul_s16_rh(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PRMULX_S16_H:
        gen_helper_dspv2_prmulx_s16_h(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PRMULX_S16_RH:
        gen_helper_dspv2_prmulx_s16_rh(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_mulxl_s32(int rz, int rx, int ry)
{
    /* Rz[31:0] = {Rx[31:0] X Ry[15:0]}[47:16] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_ext16s_i64(t1, t1);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_mulxl_s32_r(int rz, int rx, int ry)
{
    /* Rz[31:0] = {Rx[31:0] X Ry[15:0]}[47:16] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_ext16s_i64(t1, t1);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x8000);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_mulxh_s32(int rz, int rx, int ry)
{
    /* Rz[31:0] = {Rx[31:0] X Ry[15:0]}[47:16] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_shri_i64(t1, t1, 16);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_mulxh_s32_r(int rz, int rx, int ry)
{
    /* Rz[31:0] = {Rx[31:0] X Ry[15:0]}[47:16] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_shri_i64(t1, t1, 16);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x8000);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
}

static inline void dspv2_insn_mulcs_s16(int rz, int rx, int ry)
{
    /* rz = rx[15:0] * ry[15:0] - rx[31:16] * ry[31:16], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* lo_rx * lo_ry */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t2, t0, t1);
    /* hi_rx * hi_ry */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_sub_i32(cpu_R[rz], t2, t0);
}

static inline void dspv2_insn_mulcsr_s16(int rz, int rx, int ry)
{
    /* rz = rx[31:16] * ry[31:16] - rx[15:0] * ry[15:0], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* lo_rx * lo_ry */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t2, t0, t1);
    /* hi_rx * hi_ry */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_sub_i32(cpu_R[rz], t0, t2);
}

static inline void dspv2_insn_mulcsx_s16(int rz, int rx, int ry)
{
    /* rz = rx[15:0] * ry[31:16] - rx[31:16] * ry[15:0], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* lo_rx * hi_ry */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t2, t0, t1);
    /* hi_rx * lo_ry */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_sub_i32(cpu_R[rz], t2, t0);
}

static inline void dspv2_insn_mulaca_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[15:0] * ry[15:0] + rx[31:16] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    csky_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * lo_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * hi_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
}

static inline void dspv2_insn_mulacax_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[31:16] * ry[15:0] + rx[15:0] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    csky_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * lo_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
}

static inline void dspv2_insn_mulacs_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[15:0] * ry[15:0] - rx[31:16] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    csky_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * lo_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} - hi_x * hi_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
}

static inline void dspv2_insn_mulacsr_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[31:16] * ry[31:16] - rx[15:0] * ry[15:0],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    csky_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} - lo_x * lo_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * hi_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
}

static inline void dspv2_insn_mulacsx_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[15:0] * ry[31:16] - rx[31:16] * ry[15:0],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    csky_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} - hi_x * lo_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
}

static inline void dspv2_insn_mulsca_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} - rx[15:0] * ry[15:0] - rx[31:16] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    csky_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * lo_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * hi_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
}

static inline void dspv2_insn_mulscax_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} - rx[31:16] * ry[15:0] - rx[15:0] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    csky_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * lo_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    csky_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
}

static inline void dspv2_sop_mul_2nd(DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    switch (thop) {
    case OP_MULXL_S32:
        dspv2_insn_mulxl_s32(rz, rx, ry);
        break;
    case OP_MULXL_S32_R:
        dspv2_insn_mulxl_s32_r(rz, rx, ry);
        break;
    case OP_MULXH_S32:
        dspv2_insn_mulxh_s32(rz, rx, ry);
        break;
    case OP_MULXH_S32_R:
        dspv2_insn_mulxh_s32_r(rz, rx, ry);
        break;
    case OP_RMULXL_S32:
        gen_helper_dspv2_rmulxl_s32(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULXL_S32_R:
        gen_helper_dspv2_rmulxl_s32_r(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULXH_S32:
        gen_helper_dspv2_rmulxh_s32(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULXH_S32_R:
        gen_helper_dspv2_rmulxh_s32_r(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAXL_S32_S:
        gen_helper_dspv2_mulaxl_s32_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAXL_S32_RS:
        gen_helper_dspv2_mulaxl_s32_rs(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAXH_S32_S:
        gen_helper_dspv2_mulaxh_s32_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAXH_S32_RS:
        gen_helper_dspv2_mulaxh_s32_rs(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULCA_S16_S:
        gen_helper_dspv2_mulca_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULCAX_S16_S:
        gen_helper_dspv2_mulcax_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULCS_S16:
        dspv2_insn_mulcs_s16(rz, rx, ry);
        break;
    case OP_MULCSR_S16:
        dspv2_insn_mulcsr_s16(rz, rx, ry);
        break;
    case OP_MULCSX_S16:
        dspv2_insn_mulcsx_s16(rz, rx, ry);
        break;
    case OP_MULACA_S16_S:
        gen_helper_dspv2_mulaca_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACAX_S16_S:
        gen_helper_dspv2_mulacax_s16_s(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACS_S16_S:
        gen_helper_dspv2_mulacs_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACSR_S16_S:
        gen_helper_dspv2_mulacsr_s16_s(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACSX_S16_S:
        gen_helper_dspv2_mulacsx_s16_s(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULSCA_S16_S:
        gen_helper_dspv2_mulsca_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULSCAX_S16_S:
        gen_helper_dspv2_mulscax_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACA_S16_E:
        dspv2_insn_mulaca_s16_e(rz, rx, ry);
        break;
    case OP_MULACAX_S16_E:
        dspv2_insn_mulacax_s16_e(rz, rx, ry);
        break;
    case OP_MULACS_S16_E:
        dspv2_insn_mulacs_s16_e(rz, rx, ry);
        break;
    case OP_MULACSR_S16_E:
        dspv2_insn_mulacsr_s16_e(rz, rx, ry);
        break;
    case OP_MULACSX_S16_E:
        dspv2_insn_mulacsx_s16_e(rz, rx, ry);
        break;
    case OP_MULSCA_S16_E:
        dspv2_insn_mulsca_s16_e(rz, rx, ry);
        break;
    case OP_MULSCAX_S16_E:
        dspv2_insn_mulscax_s16_e(rz, rx, ry);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static void disas_dspv2_insn( DisasContext *s, uint32_t insn)
{
    int sop;
    sop = (insn >> CSKY_DSPV2_SOP_SHI) & CSKY_DSPV2_SOP_MASK;
    if ((sop != DSPV2_MUL_1st) && (sop != DSPV2_MISC)) {
        check_insn(s, ABIV2_DSP2);
    }
    switch (sop) {
    case DSPV2_ADD_SUB:
        dspv2_sop_add_sub(s, insn);
        break;
    case DSPV2_CMP:
        dspv2_sop_cmp(s, insn);
        break;
    case DSPV2_SEL:
        dspv2_sop_sel(s, insn);
        break;
    case DSPV2_MISC:
        dspv2_sop_misc(s, insn);
        break;
    case DSPV2_SHIFT:
        dspv2_sop_shift(s, insn);
        break;
    case DSPV2_PKG_begin ... DSPV2_PKG_end:
        dspv2_sop_pkg(s, insn);
        break;
    case DSPV2_DEXT:
        dspv2_sop_dext(s, insn);
        break;
    case DSPV2_PKG_CLIP:
        dspv2_sop_pkg_clip(s, insn);
        break;
    case DSPV2_MUL_1st:
        dspv2_sop_mul_1st(s, insn);
        break;
    case DSPV2_MUL_2nd:
        dspv2_sop_mul_2nd(s, insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void vdsp2_sop_add_sub_cmp(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 5) & 0x3f;

    switch (op2) {
    case 0x0:       /* VADD.T */
        gen_helper_vdsp2_vadd(tcg_env, dsp_insn);
        break;
    case 0x1:       /* VADD.T.S */
        gen_helper_vdsp2_vadd_s(tcg_env, dsp_insn);
        break;
    case 0x30:      /* VADD.T.E */
        gen_helper_vdsp2_vadd_e(tcg_env, dsp_insn);
        break;
    case 0x2:       /* VADD.T.X */
        gen_helper_vdsp2_vadd_x(tcg_env, dsp_insn);
        break;
    case 0x3:       /* VADD.T.H */
        gen_helper_vdsp2_vadd_h(tcg_env, dsp_insn);
        break;
    case 0x4:       /* VADD.T.RH */
        gen_helper_vdsp2_vadd_rh(tcg_env, dsp_insn);
        break;
    case 0x5:       /* VADDH.T */
        gen_helper_vdsp2_vaddh(tcg_env, dsp_insn);
        break;
    case 0x6:       /* VADDH.T.R */
        gen_helper_vdsp2_vaddh_r(tcg_env, dsp_insn);
        break;
    case 0x8:       /* VSUB.T */
        gen_helper_vdsp2_vsub(tcg_env, dsp_insn);
        break;
    case 0x9:       /* VSUB.T.S */
        gen_helper_vdsp2_vsub_s(tcg_env, dsp_insn);
        break;
    case 0x31:      /* VSUB.T.E */
        gen_helper_vdsp2_vsub_e(tcg_env, dsp_insn);
        break;
    case 0xa:       /* VSUB.T.X */
        gen_helper_vdsp2_vsub_x(tcg_env, dsp_insn);
        break;
    case 0xb:       /* VSUB.T.H */
        gen_helper_vdsp2_vsub_h(tcg_env, dsp_insn);
        break;
    case 0xc:       /* VSUB.T.RH */
        gen_helper_vdsp2_vsub_rh(tcg_env, dsp_insn);
        break;
    case 0xd:       /* VSUBH.T */
        gen_helper_vdsp2_vsubh(tcg_env, dsp_insn);
        break;
    case 0xe:       /* VSUBH.T.R */
        gen_helper_vdsp2_vsubh_r(tcg_env, dsp_insn);
        break;
    case 0x38:      /* VNEG.T */
        gen_helper_vdsp2_vneg(tcg_env, dsp_insn);
        break;
    case 0x39:      /* VENG.T.S */
        gen_helper_vdsp2_vneg_s(tcg_env, dsp_insn);
        break;
    case 0x3a:      /* VABS.T */
        gen_helper_vdsp2_vabs(tcg_env, dsp_insn);
        break;
    case 0x3b:      /* VABS.T.S */
        gen_helper_vdsp2_vabs_s(tcg_env, dsp_insn);
        break;
    case 0x10:      /* VASX.T.S */
        gen_helper_vdsp2_vasx_s(tcg_env, dsp_insn);
        break;
    case 0x11:      /* VSAX.T.S */
        gen_helper_vdsp2_vsax_s(tcg_env, dsp_insn);
        break;
    case 0x12:      /* VASXH.T */
        gen_helper_vdsp2_vasxh(tcg_env, dsp_insn);
        break;
    case 0x13:      /* VSAXH.T */
        gen_helper_vdsp2_vsaxh(tcg_env, dsp_insn);
        break;
    case 0x2a:      /* VSABS.T.S */
        gen_helper_vdsp2_vsabs_s(tcg_env, dsp_insn);
        break;
    case 0x32:      /* VSABS.T.E */
        gen_helper_vdsp2_vsabs_e(tcg_env, dsp_insn);
        break;
    case 0x15:      /* VSABSA.T */
        gen_helper_vdsp2_vsabsa(tcg_env, dsp_insn);
        break;
    case 0x33:      /* VSABSA.T.E */
        gen_helper_vdsp2_vsabsa_e(tcg_env, dsp_insn);
        break;
    case 0x16:      /* VPADD.T */
        gen_helper_vdsp2_vpadd(tcg_env, dsp_insn);
        break;
    case 0x17:      /* VPADD.T.S */
        gen_helper_vdsp2_vpadd_s(tcg_env, dsp_insn);
        break;
    case 0x34:      /* VPADD.T.E */
        gen_helper_vdsp2_vpadd_e(tcg_env, dsp_insn);
        break;
    case 0x35:      /* VPADDA.T.E */
        gen_helper_vdsp2_vpadda_e(tcg_env, dsp_insn);
        break;
    case 0x20:      /* VCMPHS.T */
        gen_helper_vdsp2_vcmphs(tcg_env, dsp_insn);
        break;
    case 0x21:      /* VCMPLT.T */
        gen_helper_vdsp2_vcmplt(tcg_env, dsp_insn);
        break;
    case 0x22:      /* VCMPNE.T */
        gen_helper_vdsp2_vcmpne(tcg_env, dsp_insn);
        break;
    case 0x23:      /* VCMPNEZ.T */
        gen_helper_vdsp2_vcmpnez(tcg_env, dsp_insn);
        break;
    case 0x3c:      /* VCMPHSZ.T */
        gen_helper_vdsp2_vcmphsz(tcg_env, dsp_insn);
        break;
    case 0x3d:      /* VCMPLTZ.T */
        gen_helper_vdsp2_vcmpltz(tcg_env, dsp_insn);
        break;
    case 0x3e:      /* VCMPHZ.T */
        gen_helper_vdsp2_vcmphz(tcg_env, dsp_insn);
        break;
    case 0x3f:      /* VCMPLSZ.T */
        gen_helper_vdsp2_vcmplsz(tcg_env, dsp_insn);
        break;
    case 0x24:      /* VMAX.T */
        gen_helper_vdsp2_vmax(tcg_env, dsp_insn);
        break;
    case 0x25:      /* VMIN.T */
        gen_helper_vdsp2_vmin(tcg_env, dsp_insn);
        break;
    case 0x28:      /* VPMAX.T */
        gen_helper_vdsp2_vpmax(tcg_env, dsp_insn);
        break;
    case 0x29:      /* VPMIN.T */
        gen_helper_vdsp2_vpmin(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void vdsp2_sop_ins_pkg(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 7) & 0x7;

    switch (op2) {
    case 0x0:       /* VINS.T.1 */
        gen_helper_vdsp2_vins_1(tcg_env, dsp_insn);
        break;
    case 0x1:       /* VINS.T.2 */
        gen_helper_vdsp2_vins_2(tcg_env, dsp_insn);
        break;
    case 0x2:       /* VINS.T.3 */
        gen_helper_vdsp2_vins_3(tcg_env, dsp_insn);
        break;
    case 0x3:       /* VINS.T.4 */
        gen_helper_vdsp2_vins_4(tcg_env, dsp_insn);
        break;
    case 0x5:       /* VPKG.T.2 */
        gen_helper_vdsp2_vpkg_2(tcg_env, dsp_insn);
        break;
    case 0x6:       /* VPKG.T.3 */
        gen_helper_vdsp2_vpkg_3(tcg_env, dsp_insn);
        break;
    case 0x7:       /* VPKG.T.4 */
        gen_helper_vdsp2_vpkg_4(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void vdsp2_sop_mov_ele(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 5) & 0x1f;

    switch (op2) {
    case 0x1a:      /* VMOV.T.E */
        gen_helper_vdsp2_vmov_e(tcg_env, dsp_insn);
        break;
    case 0x0:       /* VMOV.T.L */
        gen_helper_vdsp2_vmov_l(tcg_env, dsp_insn);
        break;
    case 0x1:       /* VMOV.T.h */
        gen_helper_vdsp2_vmov_h(tcg_env, dsp_insn);
        break;
    case 0x2:       /* VMOV.T.SL */
        gen_helper_vdsp2_vmov_sl(tcg_env, dsp_insn);
        break;
    case 0x3:       /* VMOV.T.RH */
        gen_helper_vdsp2_vmov_rh(tcg_env, dsp_insn);
        break;
    case 0x8:       /* VREVQ/VREVH/VREVW/VREVD */
        gen_helper_vdsp2_vrevx(tcg_env, dsp_insn);
        break;
    case 0x18:      /* VSWP.T */
        gen_helper_vdsp2_vswp(tcg_env, dsp_insn);
        break;
    case 0x19:      /* VTRN.T */
        gen_helper_vdsp2_vtrn(tcg_env, dsp_insn);
        break;
    case 0x4:       /* VTBL.T */
        gen_helper_vdsp2_vtbl(tcg_env, dsp_insn);
        break;
    case 0x5:       /* VTBX.T */
        gen_helper_vdsp2_vtbx(tcg_env, dsp_insn);
        break;
    case 0xc:
    case 0xe:       /* VMOVI.T */
        gen_helper_vdsp2_vmovi(tcg_env, dsp_insn);
        break;
    case 0xd:       /* VMASKI.T */
        gen_helper_vdsp2_vmaski(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void vdsp2_sop_shift_reg(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 5) & 0x1f;

    switch (op2) {
    case 0x0:       /* VSHL.T */
        gen_helper_vdsp2_vshl(tcg_env, dsp_insn);
        break;
    case 0x1:       /* VSHL.T.S */
        gen_helper_vdsp2_vshl_s(tcg_env, dsp_insn);
        break;
    case 0x8:       /* VSHR.T */
        gen_helper_vdsp2_vshr(tcg_env, dsp_insn);
        break;
    case 0x9:       /* VSHR.T.R */
        gen_helper_vdsp2_vshr_r(tcg_env, dsp_insn);
        break;
    case 0x1f:      /* VSHT.T.S */
        gen_helper_vdsp2_vsht_s(tcg_env, dsp_insn);
        break;
    case 0x19:      /* VSHT.T.R */
        gen_helper_vdsp2_vsht_r(tcg_env, dsp_insn);
        break;
    case 0x18:      /* VSHT.T.RS */
        gen_helper_vdsp2_vsht_rs(tcg_env, dsp_insn);
        break;
    case 0x1c:      /* VEXH.T */
        gen_helper_vdsp2_vexh(tcg_env, dsp_insn);
        break;
    case 0x1d:      /* VEXL.T */
        gen_helper_vdsp2_vexl(tcg_env, dsp_insn);
        break;
    case 0x10 ... 0x17:      /* VEXTI.T */
        gen_helper_vdsp2_vexti(tcg_env, dsp_insn);
        break;
    case 0xc ... 0xf:       /* VEXT.T */
        gen_helper_vdsp2_vext(tcg_env, dsp_insn);
        break;
    case 0x1e:      /* VSHT.T */
        gen_helper_vdsp2_vsht(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void vdsp2_sop_shift_imm(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 6) & 0xf;

    switch (op2) {
    case 0x0:       /* VSHLI.T */
        gen_helper_vdsp2_vshli(tcg_env, dsp_insn);
        break;
    case 0x1:       /* VSHLI.T.S */
        gen_helper_vdsp2_vshli_s(tcg_env, dsp_insn);
        break;
    case 0x2:       /* VSHLI.T.E */
        gen_helper_vdsp2_vshli_e(tcg_env, dsp_insn);
        break;
    case 0x4:       /* VSHRI.T */
        gen_helper_vdsp2_vshri(tcg_env, dsp_insn);
        break;
    case 0x5:       /* VSHRI.T.R */
        gen_helper_vdsp2_vshri_r(tcg_env, dsp_insn);
        break;
    case 0x6:       /* VSHRI.T.L */
        gen_helper_vdsp2_vshri_l(tcg_env, dsp_insn);
        break;
    case 0x7:       /* VSHRI.T.LR */
        gen_helper_vdsp2_vshri_lr(tcg_env, dsp_insn);
        break;
    case 0x8:       /* VSHRI.T.LS */
        gen_helper_vdsp2_vshri_ls(tcg_env, dsp_insn);
        break;
    case 0x9:       /* VSHRI.T.LRS */
        gen_helper_vdsp2_vshri_lrs(tcg_env, dsp_insn);
        break;
    case 0xa:       /* VSHRIA.T */
        gen_helper_vdsp2_vshria(tcg_env, dsp_insn);
        break;
    case 0xb:       /* VSHRIA.T.R */
        gen_helper_vdsp2_vshria_r(tcg_env, dsp_insn);
        break;
    case 0xf:       /* VCLIP.T */
        gen_helper_vdsp2_vclip(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void vdsp2_sop_bit(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 4) & 0x3f;

    switch (op2) {
    case 0x0:       /* VAND.T */
        gen_helper_vdsp2_vand(tcg_env, dsp_insn);
        break;
    case 0x1:       /* VANDN.T */
        gen_helper_vdsp2_vandn(tcg_env, dsp_insn);
        break;
    case 0x2:       /* VXOR.T */
        gen_helper_vdsp2_vxor(tcg_env, dsp_insn);
        break;
    case 0x3:       /* VOR.T */
        gen_helper_vdsp2_vor(tcg_env, dsp_insn);
        break;
    case 0x4:       /* VORN.T */
        gen_helper_vdsp2_vorn(tcg_env, dsp_insn);
        break;
    case 0x8:       /* VNOT.T */
        gen_helper_vdsp2_vnot(tcg_env, dsp_insn);
        break;
    case 0x5:       /* VTST.T */
        gen_helper_vdsp2_vtst(tcg_env, dsp_insn);
        break;
    case 0xd:       /* VCLS.T(S) */
        gen_helper_vdsp2_vcls(tcg_env, dsp_insn);
        break;
    case 0xe:       /* VCLZ.T */
        gen_helper_vdsp2_vclz(tcg_env, dsp_insn);
        break;
    case 0xf:       /* VCNT1.T */
        gen_helper_vdsp2_vcnt1(tcg_env, dsp_insn);
        break;
    case 0x10 ... 0x13:     /* VSEL.T */
        gen_helper_vdsp2_vsel(tcg_env, dsp_insn);
        break;
    case 0x24:      /* VDUP.T.1 */
        gen_helper_vdsp2_vdup_1(tcg_env, dsp_insn);
        break;
    case 0x25:      /* VDUP.T.2 */
        gen_helper_vdsp2_vdup_2(tcg_env, dsp_insn);
        break;
    case 0x26:      /* VDUP.T.3 */
        gen_helper_vdsp2_vdup_3(tcg_env, dsp_insn);
        break;
    case 0x27:      /* VDUP.T.4 */
        gen_helper_vdsp2_vdup_4(tcg_env, dsp_insn);
        break;
    case 0x29:      /* VITL.T.2 */
        gen_helper_vdsp2_vitl_2(tcg_env, dsp_insn);
        break;
    case 0x31:      /* VDTL.T.2 */
        gen_helper_vdsp2_vdtl_2(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void vdsp2_sop_mtfvr_div(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 4) & 0x3f;

    switch (op2) {
    case 0x0:       /* VMTVR.T.1 */
    case 0x1:
        gen_helper_vdsp2_vmtvr_1(tcg_env, dsp_insn);
        break;
    case 0x2:       /* VMTVR.T.2 */
    case 0x3:
        gen_helper_vdsp2_vmtvr_2(tcg_env, dsp_insn);
        break;
    case 0x4:       /* VDUPG.T */
    case 0x5:
        gen_helper_vdsp2_vdupg(tcg_env, dsp_insn);
        break;
    case 0x8 ... 0xb:   /* VMFVR.T */
        gen_helper_vdsp2_vmfvr(tcg_env, dsp_insn);
        break;
    case 0xc:
    case 0xd:       /* VSEXT.T */
        gen_helper_vdsp2_vsext(tcg_env, dsp_insn);
        break;
    case 0x10:
    case 0x11:      /* VRECPE.T */
        gen_helper_vdsp2_vrecpe(tcg_env, dsp_insn);
        break;
    case 0x12:
    case 0x13:      /* VPECPS.T */
        gen_helper_vdsp2_vrecps(tcg_env, dsp_insn);
        break;
    case 0x14:
    case 0x15:      /* VRSQRTE.T */
        gen_helper_vdsp2_vrsqrte(tcg_env, dsp_insn);
        break;
    case 0x16:
    case 0x17:      /* VRSQRTS.T */
        gen_helper_vdsp2_vrsqrts(tcg_env, dsp_insn);
        break;
    case 0x18:
    case 0x19:      /* VEXPE.T */
        gen_helper_vdsp2_vexpe(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void vdsp2_sop_ld_st(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 8) & 0x1f;

    switch (op2) {
    case 0x4:       /* VLDRU.T.N */
        gen_helper_vdsp2_vldru_n(tcg_env, dsp_insn);
        break;
    case 0x0:      /* VLDU.T.N */
        gen_helper_vdsp2_vldu_n(tcg_env, dsp_insn);
        break;
    case 0x1:      /* VLDE.T */
        gen_helper_vdsp2_vlde(tcg_env, dsp_insn);
        break;
    case 0x5:     /* VLDX.T  */
        gen_helper_vdsp2_vldx(tcg_env, dsp_insn);
        break;
    case 0x10 ... 0x13:     /* VLD.T.N */
        gen_helper_vdsp2_vld_n(tcg_env, dsp_insn);
        break;
    case 0x14 ... 0x17:     /* VLRW.T.N */
        gen_helper_vdsp2_vlrw_n(tcg_env, dsp_insn);
        break;
    case 0x2:       /* VLDMU.T */
        gen_helper_vdsp2_vldmu(tcg_env, dsp_insn);
        break;
    case 0x6:       /* VLDMRU.T */
        gen_helper_vdsp2_vldmru(tcg_env, dsp_insn);
        break;
    case 0x3:       /* VLDM.T */
        gen_helper_vdsp2_vldm(tcg_env, dsp_insn);
        break;
    case 0xc:       /* VSTRU.T.N */
        gen_helper_vdsp2_vstru_n(tcg_env, dsp_insn);
        break;
    case 0x8:       /* VSTU.T.N */
        gen_helper_vdsp2_vstu_n(tcg_env, dsp_insn);
        break;
    case 0xd:     /* VSTX.T */
        gen_helper_vdsp2_vstx(tcg_env, dsp_insn);
        break;
    case 0xa:       /* VSTMU.T*/
        gen_helper_vdsp2_vstmu(tcg_env, dsp_insn);
        break;
    case 0xb:      /* VSTM.T */
        gen_helper_vdsp2_vstm(tcg_env, dsp_insn);
        break;
    case 0xe:       /* VSTMRU.T*/
        gen_helper_vdsp2_vstmru(tcg_env, dsp_insn);
        break;
    case 0x18 ... 0x1b:     /* VST.T.N */
        gen_helper_vdsp2_vst_n(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
// #if defined(CONFIG_USER_ONLY)
//     if (watchpoint_count > 0) {
//         gen_goto_tb(s, 1, s->pc + 4);
//         s->base.is_jmp = DISAS_UPDATE;
//     }
// #endif
}

static inline void vdsp2_sop_mul_reg(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 5) & 0x3f;

    switch (op2) {
    case 0x00:       /* VMUL.T */
        gen_helper_vdsp2_vmul(tcg_env, dsp_insn);
        break;
    case 0x01:       /* VMUL.T.H */
        gen_helper_vdsp2_vmulh(tcg_env, dsp_insn);
        break;
    case 0x02:       /* VMULACA.T */
        gen_helper_vdsp2_vmulaca(tcg_env, dsp_insn);
        break;
    case 0x18:       /* VMULACAA.T */
        gen_helper_vdsp2_vmulacaa(tcg_env, dsp_insn);
        break;
    case 0x03:       /* VRMULH.T.S */
        gen_helper_vdsp2_vrmulhs(tcg_env, dsp_insn);
        break;
    case 0x04:       /* VRMULH.T.RS */
        gen_helper_vdsp2_vrmulhrs(tcg_env, dsp_insn);
        break;
    case 0x08:       /* VRCMUL.T.RS */
        gen_helper_vdsp2_vrcmulrs(tcg_env, dsp_insn);
        break;
    case 0x09:       /* VRCMULC.T.RS */
        gen_helper_vdsp2_vrcmulcrs(tcg_env, dsp_insn);
        break;
    case 0x0a:       /* VRCMULN.T.RS */
        gen_helper_vdsp2_vrcmulnrs(tcg_env, dsp_insn);
        break;
    case 0x0b:       /* VRCMULCN.T.RS */
        gen_helper_vdsp2_vrcmulcnrs(tcg_env, dsp_insn);
        break;
    case 0x10:       /* VMULA.T */
        gen_helper_vdsp2_vmula(tcg_env, dsp_insn);
        break;
    case 0x11:       /* VMULS.T */
        gen_helper_vdsp2_vmuls(tcg_env, dsp_insn);
        break;
    case 0x12:       /* VRMULHA.T.RS */
        gen_helper_vdsp2_vrmulhars(tcg_env, dsp_insn);
        break;
    case 0x13:       /* VRMULHS.T.RS */
        gen_helper_vdsp2_vrmulhsrs(tcg_env, dsp_insn);
        break;
    case 0x14:       /* VRMULXAA.T.RS */
        gen_helper_vdsp2_vrmulxaars(tcg_env, dsp_insn);
        break;
    case 0x15:       /* VRMULXAS.T.RS */
        gen_helper_vdsp2_vrmulxasrs(tcg_env, dsp_insn);
        break;
    case 0x16:       /* VRMULXSS.T.RS */
        gen_helper_vdsp2_vrmulxssrs(tcg_env, dsp_insn);
        break;
    case 0x17:       /* VRMULXSA.T.RS */
        gen_helper_vdsp2_vrmulxsars(tcg_env, dsp_insn);
        break;
    case 0x20:       /* VMUL.T.E */
        gen_helper_vdsp2_vmule(tcg_env, dsp_insn);
        break;
    case 0x21:       /* VRMUL.T.SE */
        gen_helper_vdsp2_vrmulse(tcg_env, dsp_insn);
        break;
    case 0x30:       /* VMULA.T.E */
        gen_helper_vdsp2_vmulae(tcg_env, dsp_insn);
        break;
    case 0x31:       /* VMULS.T.E */
        gen_helper_vdsp2_vmulse(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void vdsp2_sop_mul_imm(DisasContext *s, uint32_t insn)
{
    TCGv dsp_insn = tcg_constant_tl(insn);
    int op2 = (insn >> 8) & 0x3f;

    switch (op2) {
    case 0x00:       /* VMULI.T */
        gen_helper_vdsp2_vmuli(tcg_env, dsp_insn);
        break;
    case 0x01:       /* VMULI.T.H */
        gen_helper_vdsp2_vmulih(tcg_env, dsp_insn);
        break;
    case 0x02:       /* VMULACAI.T */
        gen_helper_vdsp2_vmulacai(tcg_env, dsp_insn);
        break;
    case 0x03:       /* VMULACAAI.T */
        gen_helper_vdsp2_vmulacaai(tcg_env, dsp_insn);
        break;
    case 0x10:       /* VMULAI.T */
        gen_helper_vdsp2_vmulai(tcg_env, dsp_insn);
        break;
    case 0x11:       /* VMULSI.T */
        gen_helper_vdsp2_vmulsi(tcg_env, dsp_insn);
        break;
    case 0x12:       /* VRMULHI.T.S */
        gen_helper_vdsp2_vrmulhis(tcg_env, dsp_insn);
        break;
    case 0x13:       /* VRMULHI.T.RS */
        gen_helper_vdsp2_vrmulhirs(tcg_env, dsp_insn);
        break;
    case 0x14:       /* VRMULHAI.T.RS */
        gen_helper_vdsp2_vrmulhairs(tcg_env, dsp_insn);
        break;
    case 0x15:       /* VRMULHSI.T.RS */
        gen_helper_vdsp2_vrmulhsirs(tcg_env, dsp_insn);
        break;
    case 0x16:       /* VRMULXAAI.T.RS */
        gen_helper_vdsp2_vrmulxaairs(tcg_env, dsp_insn);
        break;
    case 0x17:       /* VRMULXASI.T.RS */
        gen_helper_vdsp2_vrmulxasirs(tcg_env, dsp_insn);
        break;
    case 0x18:       /* VRMULXSSI.T.RS */
        gen_helper_vdsp2_vrmulxssirs(tcg_env, dsp_insn);
        break;
    case 0x19:       /* VRMULXSAI.T.RS */
        gen_helper_vdsp2_vrmulxsairs(tcg_env, dsp_insn);
        break;
    case 0x20:       /* VMULI.T.E */
        gen_helper_vdsp2_vmulie(tcg_env, dsp_insn);
        break;
    case 0x21:       /* VRMULI.T.SE */
        gen_helper_vdsp2_vrmulise(tcg_env, dsp_insn);
        break;
    case 0x28:       /* VRCMULA.T.E */
    case 0x29:
        gen_helper_vdsp2_vrcmulae(tcg_env, dsp_insn);
        break;
    case 0x2a:       /* VRCMULCA.T.E */
    case 0x2b:
        gen_helper_vdsp2_vrcmulcae(tcg_env, dsp_insn);
        break;
    case 0x2c:       /* VRCMULNA.T.E */
    case 0x2d:
        gen_helper_vdsp2_vrcmulnae(tcg_env, dsp_insn);
        break;
    case 0x2e:       /* VRCMULCNA.T.E */
    case 0x2f:
        gen_helper_vdsp2_vrcmulcnae(tcg_env, dsp_insn);
        break;
    case 0x30:
    case 0x31:       /* VRMULSA.T.E */
        gen_helper_vdsp2_vrmulsae(tcg_env, dsp_insn);
        break;
    case 0x32:       /* VMULAI.T.E */
        gen_helper_vdsp2_vmulaie(tcg_env, dsp_insn);
        break;
    case 0x33:       /* VMULSI.T.E */
        gen_helper_vdsp2_vmulsie(tcg_env, dsp_insn);
        break;
    case 0x34:       /* VRMULSSI.T.E */
        gen_helper_vdsp2_vrmulssie(tcg_env, dsp_insn);
        break;
    case 0x35:       /* VRMULSAI.T.E */
        gen_helper_vdsp2_vrmulsaie(tcg_env, dsp_insn);
        break;
    case 0x36:
    case 0x37:       /* VRMULSS.T.E */
        gen_helper_vdsp2_vrmulsse(tcg_env, dsp_insn);
        break;
    case 0x22:
    case 0x23:      /* VRMULSHR.T.E */
        gen_helper_vdsp2_vrmulshre(tcg_env, dsp_insn);
        break;
    case 0x24:      /* VRMULSHRI.T.E */
        gen_helper_vdsp2_vrmulshrie(tcg_env, dsp_insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static void disas_vdsp2_insn(DisasContext *s, uint32_t insn)
{
    int sop;
    gen_save_pc(s->pc);

    if (((insn >> 14) & 0x3) == 0x0) {
        /* not multiply instructions. */

        sop = (insn >> 10) & 0xf;
        switch (sop) {
        case 0x0:   /* add sub cmp */
        case 0x1:
            vdsp2_sop_add_sub_cmp(s, insn);
            break;
        case 0x2:   /* ins pkg */
            vdsp2_sop_ins_pkg(s, insn);
            break;
        case 0x3:   /* mov element */
            vdsp2_sop_mov_ele(s, insn);
            break;
        case 0x4:   /* shift(imm) clip */
            vdsp2_sop_shift_imm(s, insn);
            break;
        case 0x5:   /* shift(register) vexh vexl vext */
            vdsp2_sop_shift_reg(s, insn);
            break;
        case 0x6:   /* bit dup itl dtl */
            vdsp2_sop_bit(s, insn);
            break;
        case 0x7:   /* mtvr mfvr div */
            vdsp2_sop_mtfvr_div(s, insn);
            break;
        case 0x8 ... 0xf:     /* FIXME: ld st*/
            vdsp2_sop_ld_st(s, insn);
            break;
        default:
            goto wrong;
        }
    } else if (((insn >> 14) & 0x3) == 0x1) {
        if (((insn >> 11) & 0x7) == 0x7) {
            vdsp2_sop_mul_reg(s, insn);
        } else {
            vdsp2_sop_mul_imm(s, insn);
        }
    } else {
        goto wrong;
    }

    return;

wrong:
    generate_exception(s, EXCP_CSKY_UDEF);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "unknown vdsp2 insn pc=%x opc=%x\n", s->pc, insn);

}

static inline void disas_dsp_insn(DisasContext *ctx, uint32_t insn)
{
    uint32_t op_dsp;
    op_dsp = (insn >> 15) & 0x1;

    if (op_dsp && (has_insn(ctx, ABIV2_DSP2 | CPU_E803 | CPU_C860))) {
        /* dsp2 for ck803s */
        disas_dspv2_insn(ctx, insn);
    } else if (!op_dsp && has_insn(ctx, ABIV2_VDSP128)) {
        /* vdsp 128-bits */
        disas_vdsp_insn128(ctx, insn);
    } else if (!op_dsp && has_insn(ctx, ABIV2_VDSP64)) {
        /* vdsp 64-bits */
        disas_vdsp_insn64(ctx, insn);
    } else if (!op_dsp && has_insn(ctx, ABIV2_VDSP2)) {
        /* vdsp2 for ck803s and ck860 */
        disas_vdsp2_insn(ctx, insn);
    } else {
        generate_exception(ctx, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dsp insn pc=%x opc=%x\n", ctx->pc, insn);
    }
}

static inline void cp(DisasContext *ctx, int cprz, int rx, uint32_t sop,
                       int imm)
{
    switch (sop) {
    case 0x0: /*cprgr*/
        break;
    case 0x1: /*cpwgr*/
        break;
    case 0x2: /*cprcr*/
        break;
    case 0x3: /*cpwcr*/
        break;
    case 0x4: /*cprc*/
        break;
    case 0x8: /*ldcpr*/
        break;
    case 0xa: /*stcpr*/
        break;
    case 0xc: /*cpop*/
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void disas_csky_32_insn(CPUCSKYState *env, DisasContext *ctx)
{
    uint32_t insn, op, sop, pcode;
    int rz, rx, ry, imm;

    insn = ctx->insn;
    op = (insn >> 26) & 0xf; /* bits:29-26 */

    switch (op) {
    case 0x0: /*special*/
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        sop = (insn >> 10) & 0x3f;
        rz = insn & 0x1f;
        special(ctx, rx, sop, rz, ry);
        break;
    case 0x1:/*arth_reg*/
        ry = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 10) & 0x3f;
        pcode = (insn >> 5) & 0x1f;
        rz = insn & 0x1f;
        arth_reg32(ctx, ry, rx, sop, pcode, rz);
        break;
    case 0x3:/*lrs_srs*/
        rz = (insn >> 21) & 0x1f;
        sop = (insn >> 18) & 0x7;
        imm = insn & 0x3ffff;
        lrs(ctx, rz, sop, imm);
        break;
    case 0x4:/*ldr*/
        ry = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 10) & 0x3f;
        pcode = (insn >> 5) & 0x1f;
        rz = insn & 0x1f;
        ldr(ctx, sop, pcode, rz, rx, ry);
        break;
    case 0x5:/*str*/
        ry = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 10) & 0x3f;
        pcode = (insn >> 5) & 0x1f;
        rz = insn & 0x1f;
        str(ctx, sop, pcode, rz, rx, ry);
        break;
    case 0x6:/*ldi*/
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 12) & 0xf;
        imm = insn & 0xfff;
        ldi(ctx, sop, rz, rx, imm);
        break;
    case 0x7:/*sti*/
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 12) & 0xf;
        imm = insn & 0xfff;
        sti(ctx, sop, rz, rx, imm);
        break;
    case 0x8:/*bsr*/
        imm = insn & 0x3ffffff;
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
        bsr32(ctx, imm);
        break;
    case 0x9:/*imm_2op*/
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 12) & 0xf;
        imm = insn & 0xfff;
        imm_2op(ctx, rz, rx, sop, imm);
        break;
    case 0xa:/*imm_1op*/
        sop = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        imm = insn & 0xffff;
        imm_1op(ctx, sop, rx, imm);
        break;
    case 0xb:/*ori*/
        check_insn_except(ctx, CPU_E801);
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        imm = insn & 0xffff;
        tcg_gen_ori_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    case 0xd:/*vfp*/
        if (has_insn(ctx, ABIV2_FPU3)) {
            disas_fpu3_insn(env, ctx, insn);
        } else {
            disas_vfp_insn(env, ctx, insn);
        }
        gen_save_pc(ctx->pc);
        gen_helper_vfp_check_exception(tcg_env);
        break;
    case 0xe:/*vdsp or dspv2 instructions. */
        disas_dsp_insn(ctx, insn);
        break;
    case 0xf:/*cp*/
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 12) & 0xf;
        imm = insn & 0xfff;
        cp(ctx, rz, rx, sop, imm);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
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
    const TranslationBlock *tb = dcbase->tb;

    dc->pc = dc->base.pc_first;
    dc->bctm = CSKY_TBFLAG_PSR_BM(tb->flags);
    dc->features = env->features;

#ifndef CONFIG_USER_ONLY
    dc->super = CSKY_TBFLAG_PSR_S(tb->flags);
    dc->trust = CSKY_TBFLAG_PSR_T(tb->flags);
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

    cpu_F0s = tcg_temp_new_i32();
    cpu_F1s = tcg_temp_new_i32();
    cpu_F0d = tcg_temp_new_i64();
    cpu_F1d = tcg_temp_new_i64();
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

    if (env->jcount_start != 0 || (cpu->csky_trace_features & CSKY_TRACE)) {
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
//        /*
//         * The address covered by the breakpoint must be included in
//         * [tb->pc, tb->pc + tb->size) in order to for it to be
//         * properly cleared -- thus we increment the PC here so that
//         * the generic logic setting tb->size later does the right thing.
//         */
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

    /* if trace range match, break the tb. */
    if ((!dc->trace_match) && tfilter.enable) {
        if (unlikely(trace_match_range(env, dc->pc))) {
            gen_save_pc(dc->pc);
            dc->base.is_jmp = DISAS_UPDATE;
            dc->base.num_insns--;
            return true;
        }
    }

    /* if conditional not exec, pass it. */
    if (dc->condexec_cond != 1) {
        TCGv_i32 t0;
        dc->condlabel = gen_new_label();
        t0 = load_cpu_field(sce_condexec_bits);
        tcg_gen_andi_i32(t0, t0, 1);
        tcg_gen_brcondi_i32(TCG_COND_NE, t0, 0x1, dc->condlabel);
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
    /* if in conditional exec mode, shift cond after translate one insn. */
    if (dc->condexec_cond != 1) {
        gen_set_label(dc->condlabel);
        t0 = load_cpu_field(sce_condexec_bits);
        tcg_gen_shri_i32(t0, t0, 1);
        store_cpu_field(t0, sce_condexec_bits);
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

    if ((dc->insn & 0xc000) != 0xc000) {
        /* 16 bit instruction*/
        disas_csky_16_insn(env, dc);
        dc->pc += 2;
    } else {
        /*32 bit instruction*/
        dc->insn = (dc->insn << 16) | cpu_lduw_code(env, dc->pc + 2);
        disas_csky_32_insn(env, dc);
        dc->pc += 4;
    }

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
        } else {
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
            /* special insns like mtcr/wait/nir */
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
        /* jcount to filter tb_trace */
        if (env->jcount_enable == 0) {
            csky_dump_tb_map(dcbase);
        } else if ((dcbase->pc_first > env->jcount_start) &&
                    (dcbase->pc_next < env->jcount_end)) {
            csky_dump_tb_map(dcbase);
        }
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
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;
    int i;

    for (i = 0; i < 32; i++) {
        qemu_fprintf(f, "R%02d=0x%08x", i, env->regs[i]);
        if ((i % 4) == 3) {
            qemu_fprintf(f, "\n");
        } else {
            qemu_fprintf(f, " ");
        }
    }

    if (flags & CPU_DUMP_FPU) {
        for (i = 0; i < 16; i++) {
            qemu_fprintf(f, "vr%02d=0x%16llx", i,
                        (long long unsigned int)env->vfp.reg[i].fpu[0]);
            if ((i % 3) == 2) {
                qemu_fprintf(f, "\n");
            } else {
                qemu_fprintf(f, " ");
            }
        }
    }

    qemu_fprintf(f, "pc=%08x\n", env->pc);

    env->cp0.psr &= ~0x8000c401;
    env->cp0.psr |= env->psr_s << 31;
    env->cp0.psr |= env->psr_tm << 14;
    env->cp0.psr |= env->psr_bm << 10;
    env->cp0.psr |= env->psr_c;
    qemu_fprintf(f, "psr=%08x\n", env->cp0.psr);
    qemu_fprintf(f, "sp=%08x\n", env->regs[14]);
    qemu_fprintf(f, "spv_sp=%08x\n", env->stackpoint.nt_ssp);
    qemu_fprintf(f, "epsr=%08x ", env->cp0.epsr);
    qemu_fprintf(f, "epc=%08x ", env->cp0.epc);
    qemu_fprintf(f, "cr18=%08x\n", env->cp0.capr);
}
