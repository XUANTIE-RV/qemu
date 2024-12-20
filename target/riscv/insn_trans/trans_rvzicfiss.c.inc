/*
 * RISC-V translation routines for Zicfiss.
 *
 * Copyright (c) 2024 Alibaba Group.
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

#define REQUIRE_ZICFISS(ctx) do {         \
    if (!ctx->cfg_ptr->ext_zicfiss) {     \
        return false;                     \
    }                                     \
} while (0)


static bool trans_ssrdp(DisasContext *ctx, arg_ssrdp *a)
{
    REQUIRE_ZICFISS(ctx);
#ifndef CONFIG_USER_ONLY
    if (!ctx->xsse) {
        return false;
    }

    return do_csrr(ctx, a->rd, CSR_SSP);
#else
    return false;
#endif
}

static bool trans_sspush(DisasContext *ctx, arg_sspush *a)
{
    REQUIRE_ZICFISS(ctx);
#ifndef CONFIG_USER_ONLY
   if (!ctx->xsse) {
        return false;
    }
    /* sspush only accept x1 and x5 as rs2 */
    if (a->rs2 != 1 && a->rs2 != 5) {
        return false;
    }
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_sspush(tcg_env, data);
    return true;
#else
    return false;
#endif
}

static bool trans_sspopchk(DisasContext *ctx, arg_sspopchk *a)
{
    REQUIRE_ZICFISS(ctx);
#ifndef CONFIG_USER_ONLY
   if (!ctx->xsse) {
        return false;
    }

    if (a->rs1 != 1 && a->rs1 != 5) {
        return false;
    }
    TCGv data = get_gpr(ctx, a->rs1, EXT_NONE);
    gen_helper_sspopchk(tcg_env, data);
    return true;
#else
    return false;
#endif
}

static bool trans_ssamoswap_w(DisasContext *ctx, arg_amoswap_w *a)
{
#ifndef CONFIG_USER_ONLY
    REQUIRE_ZICFISS(ctx);
    if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
        gen_helper_exit_atomic(tcg_env);
        ctx->base.is_jmp = DISAS_NORETURN;
        return true;
    }
    TCGv rs1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv rs2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv_ptr dest = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(dest, tcg_env,
                     offsetof(CPURISCVState, gpr) +
                     a->rd * sizeof(target_ulong));
    gen_helper_ssamoswap_w(tcg_env, rs1, rs2, dest);
    return true;
#else
    return false;
#endif
}

static bool trans_ssamoswap_d(DisasContext *ctx, arg_amoswap_d *a)
{
#ifndef CONFIG_USER_ONLY
    REQUIRE_ZICFISS(ctx);
    if (get_xl(ctx) != MXL_RV64) {
        return false;
    }
    if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
        gen_helper_exit_atomic(tcg_env);
        ctx->base.is_jmp = DISAS_NORETURN;
        return true;
    }
    TCGv rs1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv rs2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv_ptr dest = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(dest, tcg_env,
                     offsetof(CPURISCVState, gpr) +
                     a->rd * sizeof(target_ulong));
    gen_helper_ssamoswap_d(tcg_env, rs1, rs2, dest);
    return true;

#else
    return false;
#endif
}
