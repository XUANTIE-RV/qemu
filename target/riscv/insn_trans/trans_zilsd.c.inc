/*
 * RISC-V translation routines for the Zilsd & Zclsd Extension.
 *
 * Copyright (c) 2025 Nucleisys, Inc.
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

#define REQUIRE_ZILSD(ctx) do {    \
    if (!ctx->cfg_ptr->ext_zilsd)  \
        return false;              \
} while (0)

#define REQUIRE_ZCLSD(ctx) do {    \
    if (!ctx->cfg_ptr->ext_zclsd)  \
        return false;              \
} while (0)

static bool gen_load_i64(DisasContext *ctx, arg_ld *a)
{
    if ((a->rd) % 2) {
        return false;
    }

    TCGv dest_low = dest_gpr(ctx, a->rd);
    TCGv dest_high = dest_gpr(ctx, a->rd + 1);
    TCGv addr = get_address(ctx, a->rs1, a->imm);
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_qemu_ld_i64(tmp, addr, ctx->mem_idx,
                        (ctx->xt_mm ? MO_UNALN : MO_ALIGN_4) | MO_TESQ
                         | MO_ATOM_IFALIGN_PAIR);

    if (a->rd == 0) {
        return true;
    }

    tcg_gen_extr_i64_tl(dest_low, dest_high, tmp);

    gen_set_gpr(ctx, a->rd, dest_low);
    gen_set_gpr(ctx, a->rd + 1, dest_high);

    return true;
}

static bool trans_zilsd_ld(DisasContext *ctx, arg_zilsd_ld *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZILSD(ctx);
    return gen_load_i64(ctx, a);
}

static bool trans_zclsd_ld(DisasContext *ctx, arg_zclsd_ld *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZCLSD(ctx);
    return gen_load_i64(ctx, a);
}

static bool trans_zclsd_ldsp(DisasContext *ctx, arg_zclsd_ldsp *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZCLSD(ctx);

    if (a->rd == 0) {
        return false;
    }
    return gen_load_i64(ctx, a);
}

static bool gen_store_i64(DisasContext *ctx, arg_sd *a)
{
    if ((a->rs2) % 2) {
        return false;
    }

    TCGv data_low = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv data_high = get_gpr(ctx, a->rs2 + 1, EXT_NONE);
    TCGv addr = get_address(ctx, a->rs1, a->imm);
    TCGv_i64 tmp = tcg_temp_new_i64();

    if (a->rs2 == 0) {
        tmp = tcg_constant_i64(0);
    } else {
        tcg_gen_concat_tl_i64(tmp, data_low, data_high);
    }
    tcg_gen_qemu_st_i64(tmp, addr, ctx->mem_idx,
                        (ctx->xt_mm ? MO_UNALN : MO_ALIGN_4) | MO_TESQ
                         | MO_ATOM_IFALIGN_PAIR);

    return true;
}

static bool trans_zilsd_sd(DisasContext *ctx, arg_zilsd_sd *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZILSD(ctx);
    return gen_store_i64(ctx, a);
}

static bool trans_zclsd_sd(DisasContext *ctx, arg_zclsd_sd *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZCLSD(ctx);
    return gen_store_i64(ctx, a);
}
