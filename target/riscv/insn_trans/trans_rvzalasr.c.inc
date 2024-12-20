/*
 * RISC-V translation routines for the Zzlasr Standard Extension.
 *
 * Copyright (c) 2023 Brendan Sweeney, brs@berkeley.edu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define REQUIRE_ZALASR(ctx) do { \
    if (!ctx->cfg_ptr->ext_zalasr) { \
        return false; \
    } \
} while (0)

static bool gen_l_aq(DisasContext *ctx, arg_atomic *a, MemOp mop)
{
    TCGv src1;

    decode_save_opc(ctx);
    src1 = get_address(ctx, a->rs1, 0);
    if (a->rl) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    }
    tcg_gen_qemu_ld_tl(load_val, src1, ctx->mem_idx, mop);
    if (a->aq) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    }
    /* Put data in load_val. */
    gen_set_gpr(ctx, a->rd, load_val);

    return true;
}

static bool trans_lb_aq(DisasContext *ctx, arg_lb_aq *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_l_aq(ctx, a, (MO_ALIGN | MO_SB));
}

static bool trans_lh_aq(DisasContext *ctx, arg_lh_aq *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_l_aq(ctx, a, (MO_ALIGN | MO_TESW));
}

static bool trans_lw_aq(DisasContext *ctx, arg_lw_aq *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_l_aq(ctx, a, (MO_ALIGN | MO_TESL));
}

static bool trans_ld_aq(DisasContext *ctx, arg_lw_aq *a)
{
    REQUIRE_ZALASR(ctx);
    REQUIRE_64_OR_128BIT(ctx);
    return gen_l_aq(ctx, a, (MO_ALIGN | MO_TESQ));
}

static bool gen_s_rl(DisasContext *ctx, arg_atomic *a, MemOp mop)
{
    TCGv src1, src2;

    decode_save_opc(ctx);
    src1 = get_address(ctx, a->rs1, 0);

    src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    if (a->rl) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    }
    tcg_gen_qemu_st_tl(src2, src1, ctx->mem_idx, mop);
    if (a->aq) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    }

    return true;
}

static bool trans_sb_rl(DisasContext *ctx, arg_sb_rl *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_s_rl(ctx, a, (MO_ALIGN | MO_SB));
}

static bool trans_sh_rl(DisasContext *ctx, arg_sh_rl *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_s_rl(ctx, a, (MO_ALIGN | MO_TESW));
}

static bool trans_sw_rl(DisasContext *ctx, arg_sw_rl *a)
{
    REQUIRE_ZALASR(ctx);
    return gen_s_rl(ctx, a, (MO_ALIGN | MO_TESL));
}

static bool trans_sd_rl(DisasContext *ctx, arg_sd_rl *a)
{
    REQUIRE_ZALASR(ctx);
    REQUIRE_64_OR_128BIT(ctx);
    return gen_s_rl(ctx, a, (MO_ALIGN | MO_TEUQ));
}
