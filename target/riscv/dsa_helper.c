/*
 * RISC-V DSA Helpers for QEMU.
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "vector_internals.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "tcg/tcg-op.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "gdbstub/helpers.h"
#include "qemu/cutils.h"
#include "dsa.h"
#include "dsa_float.h"

#include <gmodule.h>
#include <glib.h>

/* Return the length of dsa insn, return 0 means it is not a dsa insn */
bool decode_dsa(CPURISCVState *env, uint32_t insn, uint32_t length)
{
    bool ret;
    if (length == 2) {
        ret = env->dsa_ops->is_dsa_insn_16(insn);
    } else {
        ret = env->dsa_ops->is_dsa_insn_32(insn);
    }

    if (ret) {
        TCGv_i32 i = tcg_constant_i32(insn);
        TCGv_i32 l = tcg_constant_i32(length);
        gen_helper_dsa(tcg_env, i, l);
    }
    return ret;
}

void helper_dsa(CPURISCVState *env, uint32_t insn, uint32_t length)
{
    RISCVException ret;
    ret = env->dsa_ops->exec_dsa_insn(env, env->qdsa_ops,
                                      env->qdsa_float_ops, insn, length);
    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
}

static bool dsa_get_gpr(uint64_t *val, uint32_t gprno)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;

    if (gprno >= 32) {
        return false;
    }
    if (gprno == 0) {
        *val = 0;
    } else {
        *val = env->gpr[gprno];
    }
    return true;

}

static bool dsa_set_gpr(uint64_t val, uint32_t gprno)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;

    if (gprno >= 32) {
        return false;
    }

    if (gprno != 0) {
        env->gpr[gprno] = val;
    }
    return true;
}

static bool dsa_get_fpr(uint64_t *val, uint32_t fprno)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;

    if (fprno >= 32) {
        return false;
    }

    *val = env->fpr[fprno];
    return true;

}

static bool dsa_set_fpr(uint64_t val, uint32_t fprno)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;

    if (fprno >= 32) {
        return false;
    }

    env->fpr[fprno] = val;
    return true;
}

static bool dsa_get_csr(uint64_t *val, uint32_t csrno)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;

    RISCVException ret = riscv_csrr(env, csrno, (target_ulong *)val);
    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return true;

}

static bool dsa_set_csr(uint64_t val, uint32_t csrno)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;
    target_ulong mask = env->xl == MXL_RV32 ? UINT32_MAX : (target_ulong)-1;
    RISCVException ret = riscv_csrrw(env, csrno, NULL, val, mask);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return true;
}

static bool dsa_get_vector_element(void *val, uint32_t vregno,
                                   uint32_t ele_size, uint32_t ele_no)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;
    if (vregno >= 32 || ele_no * ele_size >= cpu->cfg.vlenb) {
        return false;
    }
    void *vreg_addr = (void *)(&env->vreg[0]) + vregno * cpu->cfg.vlenb;
    uint32_t offset;
    switch (ele_size) {
    case 1:
        offset = H1(ele_no * ele_size);
        break;
    case 2:
        offset = H2(ele_no * ele_size);
        break;
    case 4:
        offset = H4(ele_no * ele_size);
        break;
    case 8:
        offset = H8(ele_no * ele_size);
        break;
    default:
        g_assert_not_reached();
    }
    void *ele_addr = vreg_addr + offset;
    memcpy(val, ele_addr, ele_size);
    return true;
}

static bool dsa_set_vector_element(void *val, uint32_t vregno,
                                   uint32_t ele_size, uint32_t ele_no)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;
    if (vregno >= 32 || ele_no * ele_size >= cpu->cfg.vlenb) {
        return false;
    }
    void *vreg_addr = (void *)(&env->vreg[0]) + vregno * cpu->cfg.vlenb;
    uint32_t offset;
    switch (ele_size) {
    case 1:
        offset = H1(ele_no * ele_size);
        break;
    case 2:
        offset = H2(ele_no * ele_size);
        break;
    case 4:
        offset = H4(ele_no * ele_size);
        break;
    case 8:
        offset = H8(ele_no * ele_size);
        break;
    default:
        g_assert_not_reached();
    }
    void *ele_addr = vreg_addr + offset;
    memcpy(ele_addr, val, ele_size);
    return true;
}

static bool dsa_load_data(void *val, uint64_t vaddr, uint32_t size)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;
    vaddr = adjust_addr(env, vaddr);
    switch (size) {
    case 1:
        *(uint8_t *)val = cpu_ldub_data(env, vaddr);
        break;
    case 2:
        *(uint16_t *)val = cpu_lduw_data(env, vaddr);
        break;
    case 4:
        *(uint32_t *)val = cpu_ldl_data(env, vaddr);
        break;
    case 8:
        *(uint64_t *)val = cpu_ldq_data(env, vaddr);
        break;
    default:
        return false;
        break;
    };
    return true;
}

static bool dsa_store_data(void *val, uint64_t vaddr, uint32_t size)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;
    vaddr = adjust_addr(env, vaddr);
    switch (size) {
    case 1:
        cpu_stb_data(env, vaddr, *(uint8_t *)val);
        break;
    case 2:
        cpu_stw_data(env, vaddr, *(uint16_t *)val);
        break;
    case 4:
        cpu_stl_data(env, vaddr, *(uint32_t *)val);
        break;
    case 8:
        cpu_stq_data(env, vaddr, *(uint64_t *)val);
        break;
    default:
        return false;
        break;
    };
    return true;
}

static bool dsa_get_matrix_element(void *val, uint32_t mregno,
                                   uint32_t ele_size, uint32_t rowno,
                                   uint32_t colno)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;
    target_ulong rlenb = get_rlenb(env);
    bool half_byte = (ele_size == 0);
    if (mregno >= 8 || colno * ele_size >= rlenb || rowno * 4 >= rlenb) {
        return false;
    }
    /*
     * mrowlen >> 3 = mrowlen / 8 = rlenb
     *     -- represent the number of bytes in a row
     * mrowlen >> 5 = mrowlen / 32
     *     -- represent the number of rows in total
     */
    void *mreg_addr = (void *)(&env->mreg[0]) +
                      (mregno * cpu->cfg.mrowlen * cpu->cfg.mrowlen >> 8);
    uint32_t offset;
    if (half_byte) {
        offset = rowno * rlenb + colno / 2;
    } else {
        offset = rowno * rlenb + colno * ele_size;
    }
    void *ele_addr = mreg_addr + offset;
    if (half_byte) {
        if (colno % 2 == 0) {
            *(uint8_t *)val = *(uint8_t *)ele_addr & 0x0f;
        } else {
            *(uint8_t *)val = *(uint8_t *)ele_addr >> 4;
        }
    } else {
        memcpy(val, ele_addr, ele_size);
    }
    return true;
}

static bool dsa_set_matrix_element(void *val, uint32_t mregno,
                                   uint32_t ele_size, uint32_t rowno,
                                   uint32_t colno)
{
    RISCVCPU *cpu = RISCV_CPU(current_cpu);
    CPURISCVState *env = &cpu->env;
    target_ulong rlenb = get_rlenb(env);
    bool half_byte = (ele_size == 0);
    if (mregno >= 8 || colno * ele_size >= rlenb || rowno * 4 >= rlenb) {
        return false;
    }
    /*
     * mrowlen >> 3 = mrowlen / 8 = rlenb
     *     -- represent the number of bytes in a row
     * mrowlen >> 5 = mrowlen / 32
     *     -- represent the number of rows in total
     */
    void *mreg_addr = (void *)(&env->mreg[0]) +
                      (mregno * cpu->cfg.mrowlen * cpu->cfg.mrowlen >> 8);
    uint32_t offset;
    if (half_byte) {
        offset = rowno * rlenb + colno / 2;
    } else {
        offset = rowno * rlenb + colno * ele_size;
    }
    void *ele_addr = mreg_addr + offset;
    if (half_byte) {
        if (colno % 2 == 0) {
            *(uint8_t *)ele_addr = (*(uint8_t *)ele_addr & 0xf0) |
                                   (*(uint8_t *)val & 0x0f);
        } else {
            *(uint8_t *)ele_addr = (*(uint8_t *)ele_addr & 0x0f) |
                                   (*(uint8_t *)val << 4);
        }
    } else {
        memcpy(ele_addr, val, ele_size);
    }
    return true;
}

static bool dsa_get_reg_address(void *env_base, uint64_t *offset, char *regname)
{
    CPURISCVState *env = (CPURISCVState *)env_base;
    RISCVCPU *cpu = env_archcpu(env);
    long num;
    if (qemu_strtol(&regname[1], NULL, 10, &num) != 0) {
        return false;
    }
    switch (regname[0]) {
    case 'x':
        if (num >= 32) {
            return false;
        }
        *offset = (void *)&env->gpr[num] - (void *)env;
        break;
    case 'f':
        if (num >= 32) {
            return false;
        }
        *offset = (void *)&env->fpr[num] - (void *)env;
        break;
    case 'v':
        if (num >= 32) {
            return false;
        }
        void *vreg_addr = (void *)(&env->vreg[0]) + num * cpu->cfg.vlenb;
        *offset = vreg_addr - (void *)env;
        break;
    case 'm':
        if (num >= 8) {
            return false;
        }
        void *mreg_addr = (void *)(&env->mreg[0]) +
                    (num * cpu->cfg.mrowlen * cpu->cfg.mrowlen >> 8);
        *offset = mreg_addr - (void *)env;
        break;
    }

    return true;
}

#define DSA_WRAP_IEEE_FUNC(return_type, base_func, type) \
static \
return_type dsa_##base_func(type a, float_status *s) \
{ \
    return base_func(a, true, s); \
}

DSA_WRAP_IEEE_FUNC(float16, float32_to_float16, float32)
DSA_WRAP_IEEE_FUNC(float32, float16_to_float32, float16)
DSA_WRAP_IEEE_FUNC(float16, float64_to_float16, float64)
DSA_WRAP_IEEE_FUNC(float64, float16_to_float64, float16)

static void assign_dsa_float_ops(CPURISCVState *env)
{
    qemu_float_ops *ops = env->qdsa_float_ops;
    ops->fp_status = &env->fp_status;
    ops->mfp_status = &env->mfp_status;

    #define ASSIGN_FUNCTION(name) \
        do { \
            ops->name = name; \
        } while (0)

    #define ASSIGN_WARP_FUNCTION(name) \
        do { \
            ops->name = dsa_##name; \
        } while (0)

    ASSIGN_FUNCTION(float_raise);
    ASSIGN_FUNCTION(float16_squash_input_denormal);
    ASSIGN_FUNCTION(float32_squash_input_denormal);
    ASSIGN_FUNCTION(float64_squash_input_denormal);
    ASSIGN_FUNCTION(bfloat16_squash_input_denormal);
    ASSIGN_FUNCTION(float8e4_squash_input_denormal);
    ASSIGN_FUNCTION(float8e5_squash_input_denormal);

    ASSIGN_FUNCTION(int16_to_float16_scalbn);
    ASSIGN_FUNCTION(int32_to_float16_scalbn);
    ASSIGN_FUNCTION(int64_to_float16_scalbn);
    ASSIGN_FUNCTION(uint16_to_float16_scalbn);
    ASSIGN_FUNCTION(uint32_to_float16_scalbn);
    ASSIGN_FUNCTION(uint64_to_float16_scalbn);
    ASSIGN_FUNCTION(int8_to_float16);
    ASSIGN_FUNCTION(int16_to_float16);
    ASSIGN_FUNCTION(int32_to_float16);
    ASSIGN_FUNCTION(int64_to_float16);
    ASSIGN_FUNCTION(uint8_to_float16);
    ASSIGN_FUNCTION(uint16_to_float16);
    ASSIGN_FUNCTION(uint32_to_float16);
    ASSIGN_FUNCTION(uint64_to_float16);

    ASSIGN_FUNCTION(int16_to_float32_scalbn);
    ASSIGN_FUNCTION(int32_to_float32_scalbn);
    ASSIGN_FUNCTION(int64_to_float32_scalbn);
    ASSIGN_FUNCTION(uint16_to_float32_scalbn);
    ASSIGN_FUNCTION(uint32_to_float32_scalbn);
    ASSIGN_FUNCTION(uint64_to_float32_scalbn);
    ASSIGN_FUNCTION(int16_to_float32);
    ASSIGN_FUNCTION(int32_to_float32);
    ASSIGN_FUNCTION(int64_to_float32);
    ASSIGN_FUNCTION(uint16_to_float32);
    ASSIGN_FUNCTION(uint32_to_float32);
    ASSIGN_FUNCTION(uint64_to_float32);

    ASSIGN_FUNCTION(int16_to_float64_scalbn);
    ASSIGN_FUNCTION(int32_to_float64_scalbn);
    ASSIGN_FUNCTION(int64_to_float64_scalbn);
    ASSIGN_FUNCTION(uint16_to_float64_scalbn);
    ASSIGN_FUNCTION(uint32_to_float64_scalbn);
    ASSIGN_FUNCTION(uint64_to_float64_scalbn);
    ASSIGN_FUNCTION(int16_to_float64);
    ASSIGN_FUNCTION(int32_to_float64);
    ASSIGN_FUNCTION(int64_to_float64);
    ASSIGN_FUNCTION(uint16_to_float64);
    ASSIGN_FUNCTION(uint32_to_float64);
    ASSIGN_FUNCTION(uint64_to_float64);

/* ---------------- f16 ------------------ */
    ASSIGN_WARP_FUNCTION(float32_to_float16);
    ASSIGN_WARP_FUNCTION(float16_to_float32);
    ASSIGN_WARP_FUNCTION(float64_to_float16);
    ASSIGN_WARP_FUNCTION(float16_to_float64);

    ASSIGN_FUNCTION(float16_to_int8_scalbn);
    ASSIGN_FUNCTION(float16_to_int16_scalbn);
    ASSIGN_FUNCTION(float16_to_int32_scalbn);
    ASSIGN_FUNCTION(float16_to_int64_scalbn);
    ASSIGN_FUNCTION(float16_to_int8);
    ASSIGN_FUNCTION(float16_to_int16);
    ASSIGN_FUNCTION(float16_to_int32);
    ASSIGN_FUNCTION(float16_to_int64);
    ASSIGN_FUNCTION(float16_to_int16_round_to_zero);
    ASSIGN_FUNCTION(float16_to_int32_round_to_zero);
    ASSIGN_FUNCTION(float16_to_int64_round_to_zero);
    ASSIGN_FUNCTION(float16_to_uint8_scalbn);
    ASSIGN_FUNCTION(float16_to_uint16_scalbn);
    ASSIGN_FUNCTION(float16_to_uint32_scalbn);
    ASSIGN_FUNCTION(float16_to_uint64_scalbn);
    ASSIGN_FUNCTION(float16_to_uint8);
    ASSIGN_FUNCTION(float16_to_uint16);
    ASSIGN_FUNCTION(float16_to_uint32);
    ASSIGN_FUNCTION(float16_to_uint64);
    ASSIGN_FUNCTION(float16_to_uint16_round_to_zero);
    ASSIGN_FUNCTION(float16_to_uint32_round_to_zero);
    ASSIGN_FUNCTION(float16_to_uint64_round_to_zero);
    ASSIGN_FUNCTION(float16_round_to_int);
    ASSIGN_FUNCTION(float16_add);
    ASSIGN_FUNCTION(float16_sub);
    ASSIGN_FUNCTION(float16_mul);
    ASSIGN_FUNCTION(float16_muladd);
    ASSIGN_FUNCTION(float16_div);
    ASSIGN_FUNCTION(float16_scalbn);
    ASSIGN_FUNCTION(float16_min);
    ASSIGN_FUNCTION(float16_max);
    ASSIGN_FUNCTION(float16_minnum);
    ASSIGN_FUNCTION(float16_maxnum);
    ASSIGN_FUNCTION(float16_minnummag);
    ASSIGN_FUNCTION(float16_maxnummag);
    ASSIGN_FUNCTION(float16_minimum_number);
    ASSIGN_FUNCTION(float16_maximum_number);
    ASSIGN_FUNCTION(float16_sqrt);
    ASSIGN_FUNCTION(float16_compare);
    ASSIGN_FUNCTION(float16_compare_quiet);
    ASSIGN_FUNCTION(float16_is_quiet_nan);
    ASSIGN_FUNCTION(float16_is_signaling_nan);
    ASSIGN_FUNCTION(float16_silence_nan);

    ASSIGN_FUNCTION(float16_is_any_nan);
    ASSIGN_FUNCTION(float16_is_neg);
    ASSIGN_FUNCTION(float16_is_infinity);
    ASSIGN_FUNCTION(float16_is_zero);
    ASSIGN_FUNCTION(float16_is_zero_or_denormal);
    ASSIGN_FUNCTION(float16_is_normal);
    ASSIGN_FUNCTION(float16_abs);
    ASSIGN_FUNCTION(float16_chs);
    ASSIGN_FUNCTION(float16_set_sign);
    ASSIGN_FUNCTION(float16_eq);
    ASSIGN_FUNCTION(float16_le);
    ASSIGN_FUNCTION(float16_lt);
    ASSIGN_FUNCTION(float16_unordered);
    ASSIGN_FUNCTION(float16_eq_quiet);
    ASSIGN_FUNCTION(float16_le_quiet);
    ASSIGN_FUNCTION(float16_lt_quiet);
    ASSIGN_FUNCTION(float16_unordered_quiet);

    ASSIGN_FUNCTION(float16_default_nan);

/* ---------------- bf16 ------------------ */
    ASSIGN_FUNCTION(bfloat16_round_to_int);
    ASSIGN_FUNCTION(float32_to_bfloat16);
    ASSIGN_FUNCTION(bfloat16_to_float32);
    ASSIGN_FUNCTION(float64_to_bfloat16);
    ASSIGN_FUNCTION(bfloat16_to_float64);
    ASSIGN_FUNCTION(bfloat16_to_int8_scalbn);
    ASSIGN_FUNCTION(bfloat16_to_int16_scalbn);
    ASSIGN_FUNCTION(bfloat16_to_int32_scalbn);
    ASSIGN_FUNCTION(bfloat16_to_int64_scalbn);
    ASSIGN_FUNCTION(bfloat16_to_int8);
    ASSIGN_FUNCTION(bfloat16_to_int16);
    ASSIGN_FUNCTION(bfloat16_to_int32);
    ASSIGN_FUNCTION(bfloat16_to_int64);
    ASSIGN_FUNCTION(bfloat16_to_int8_round_to_zero);
    ASSIGN_FUNCTION(bfloat16_to_int16_round_to_zero);
    ASSIGN_FUNCTION(bfloat16_to_int32_round_to_zero);
    ASSIGN_FUNCTION(bfloat16_to_int64_round_to_zero);
    ASSIGN_FUNCTION(bfloat16_to_uint8_scalbn);
    ASSIGN_FUNCTION(bfloat16_to_uint16_scalbn);
    ASSIGN_FUNCTION(bfloat16_to_uint32_scalbn);
    ASSIGN_FUNCTION(bfloat16_to_uint64_scalbn);
    ASSIGN_FUNCTION(bfloat16_to_uint8);
    ASSIGN_FUNCTION(bfloat16_to_uint16);
    ASSIGN_FUNCTION(bfloat16_to_uint32);
    ASSIGN_FUNCTION(bfloat16_to_uint64);

    ASSIGN_FUNCTION(bfloat16_to_uint8_round_to_zero);
    ASSIGN_FUNCTION(bfloat16_to_uint16_round_to_zero);
    ASSIGN_FUNCTION(bfloat16_to_uint32_round_to_zero);
    ASSIGN_FUNCTION(bfloat16_to_uint64_round_to_zero);
    ASSIGN_FUNCTION(int8_to_bfloat16_scalbn);
    ASSIGN_FUNCTION(int16_to_bfloat16_scalbn);
    ASSIGN_FUNCTION(int32_to_bfloat16_scalbn);
    ASSIGN_FUNCTION(int64_to_bfloat16_scalbn);
    ASSIGN_FUNCTION(uint8_to_bfloat16_scalbn);
    ASSIGN_FUNCTION(uint16_to_bfloat16_scalbn);
    ASSIGN_FUNCTION(uint32_to_bfloat16_scalbn);
    ASSIGN_FUNCTION(uint64_to_bfloat16_scalbn);
    ASSIGN_FUNCTION(int8_to_bfloat16);
    ASSIGN_FUNCTION(int16_to_bfloat16);
    ASSIGN_FUNCTION(int32_to_bfloat16);
    ASSIGN_FUNCTION(int64_to_bfloat16);
    ASSIGN_FUNCTION(uint8_to_bfloat16);
    ASSIGN_FUNCTION(uint16_to_bfloat16);
    ASSIGN_FUNCTION(uint32_to_bfloat16);
    ASSIGN_FUNCTION(uint64_to_bfloat16);
    ASSIGN_FUNCTION(bfloat16_add);
    ASSIGN_FUNCTION(bfloat16_sub);
    ASSIGN_FUNCTION(bfloat16_mul);
    ASSIGN_FUNCTION(bfloat16_div);
    ASSIGN_FUNCTION(bfloat16_muladd);
    ASSIGN_FUNCTION(bfloat16_scalbn);
    ASSIGN_FUNCTION(bfloat16_min);
    ASSIGN_FUNCTION(bfloat16_max);
    ASSIGN_FUNCTION(bfloat16_minnum);
    ASSIGN_FUNCTION(bfloat16_maxnum);
    ASSIGN_FUNCTION(bfloat16_minnummag);
    ASSIGN_FUNCTION(bfloat16_maxnummag);
    ASSIGN_FUNCTION(bfloat16_minimum_number);
    ASSIGN_FUNCTION(bfloat16_maximum_number);
    ASSIGN_FUNCTION(bfloat16_sqrt);
    ASSIGN_FUNCTION(bfloat16_compare);
    ASSIGN_FUNCTION(bfloat16_compare_quiet);
    ASSIGN_FUNCTION(bfloat16_is_quiet_nan);
    ASSIGN_FUNCTION(bfloat16_is_signaling_nan);
    ASSIGN_FUNCTION(bfloat16_silence_nan);
    ASSIGN_FUNCTION(bfloat16_default_nan);
    ASSIGN_FUNCTION(bfloat16_is_any_nan);
    ASSIGN_FUNCTION(bfloat16_is_neg);
    ASSIGN_FUNCTION(bfloat16_is_infinity);
    ASSIGN_FUNCTION(bfloat16_is_zero);
    ASSIGN_FUNCTION(bfloat16_is_zero_or_denormal);
    ASSIGN_FUNCTION(bfloat16_is_normal);
    ASSIGN_FUNCTION(bfloat16_abs);
    ASSIGN_FUNCTION(bfloat16_chs);
    ASSIGN_FUNCTION(bfloat16_set_sign);
    ASSIGN_FUNCTION(bfloat16_eq);
    ASSIGN_FUNCTION(bfloat16_le);
    ASSIGN_FUNCTION(bfloat16_lt);
    ASSIGN_FUNCTION(bfloat16_unordered);
    ASSIGN_FUNCTION(bfloat16_eq_quiet);
    ASSIGN_FUNCTION(bfloat16_le_quiet);
    ASSIGN_FUNCTION(bfloat16_lt_quiet);
    ASSIGN_FUNCTION(bfloat16_unordered_quiet);

/* ---------------- float 8e4 ------------------ */
    ASSIGN_FUNCTION(float8e4_round_to_int);
    ASSIGN_FUNCTION(bfloat16_to_float8e4);
    ASSIGN_FUNCTION(float8e4_to_bfloat16);
    ASSIGN_FUNCTION(float16_to_float8e4);
    ASSIGN_FUNCTION(float8e4_to_float16);
    ASSIGN_FUNCTION(float32_to_float8e4);
    ASSIGN_FUNCTION(float8e4_to_float32);
    ASSIGN_FUNCTION(float64_to_float8e4);
    ASSIGN_FUNCTION(float8e4_to_float64);
    ASSIGN_FUNCTION(float8e4_to_int8_scalbn);
    ASSIGN_FUNCTION(float8e4_to_int16_scalbn);
    ASSIGN_FUNCTION(float8e4_to_int32_scalbn);
    ASSIGN_FUNCTION(float8e4_to_int64_scalbn);
    ASSIGN_FUNCTION(float8e4_to_int8);
    ASSIGN_FUNCTION(float8e4_to_int16);
    ASSIGN_FUNCTION(float8e4_to_int32);
    ASSIGN_FUNCTION(float8e4_to_int64);
    ASSIGN_FUNCTION(float8e4_to_int8_round_to_zero);
    ASSIGN_FUNCTION(float8e4_to_int16_round_to_zero);
    ASSIGN_FUNCTION(float8e4_to_int32_round_to_zero);
    ASSIGN_FUNCTION(float8e4_to_int64_round_to_zero);
    ASSIGN_FUNCTION(float8e4_to_uint8_scalbn);
    ASSIGN_FUNCTION(float8e4_to_uint16_scalbn);
    ASSIGN_FUNCTION(float8e4_to_uint32_scalbn);
    ASSIGN_FUNCTION(float8e4_to_uint64_scalbn);
    ASSIGN_FUNCTION(float8e4_to_uint8);
    ASSIGN_FUNCTION(float8e4_to_uint16);
    ASSIGN_FUNCTION(float8e4_to_uint32);
    ASSIGN_FUNCTION(float8e4_to_uint64);
    ASSIGN_FUNCTION(float8e4_to_uint8_round_to_zero);
    ASSIGN_FUNCTION(float8e4_to_uint16_round_to_zero);
    ASSIGN_FUNCTION(float8e4_to_uint32_round_to_zero);
    ASSIGN_FUNCTION(float8e4_to_uint64_round_to_zero);
    ASSIGN_FUNCTION(int8_to_float8e4_scalbn);
    ASSIGN_FUNCTION(int16_to_float8e4_scalbn);
    ASSIGN_FUNCTION(int32_to_float8e4_scalbn);
    ASSIGN_FUNCTION(int64_to_float8e4_scalbn);
    ASSIGN_FUNCTION(uint8_to_float8e4_scalbn);
    ASSIGN_FUNCTION(uint16_to_float8e4_scalbn);
    ASSIGN_FUNCTION(uint32_to_float8e4_scalbn);
    ASSIGN_FUNCTION(uint64_to_float8e4_scalbn);
    ASSIGN_FUNCTION(int8_to_float8e4);
    ASSIGN_FUNCTION(int16_to_float8e4);
    ASSIGN_FUNCTION(int32_to_float8e4);
    ASSIGN_FUNCTION(int64_to_float8e4);
    ASSIGN_FUNCTION(uint8_to_float8e4);
    ASSIGN_FUNCTION(uint16_to_float8e4);
    ASSIGN_FUNCTION(uint32_to_float8e4);
    ASSIGN_FUNCTION(uint64_to_float8e4);

    ASSIGN_FUNCTION(float8e4_add);
    ASSIGN_FUNCTION(float8e4_sub);
    ASSIGN_FUNCTION(float8e4_mul);
    ASSIGN_FUNCTION(float8e4_div);
    ASSIGN_FUNCTION(float8e4_muladd);
    ASSIGN_FUNCTION(float8e4_scalbn);
    ASSIGN_FUNCTION(float8e4_min);
    ASSIGN_FUNCTION(float8e4_max);
    ASSIGN_FUNCTION(float8e4_minnum);
    ASSIGN_FUNCTION(float8e4_maxnum);
    ASSIGN_FUNCTION(float8e4_minnummag);
    ASSIGN_FUNCTION(float8e4_maxnummag);
    ASSIGN_FUNCTION(float8e4_minimum_number);
    ASSIGN_FUNCTION(float8e4_maximum_number);
    ASSIGN_FUNCTION(float8e4_sqrt);
    ASSIGN_FUNCTION(float8e4_compare);
    ASSIGN_FUNCTION(float8e4_compare_quiet);
    ASSIGN_FUNCTION(float8e4_is_quiet_nan);
    ASSIGN_FUNCTION(float8e4_is_signaling_nan);
    ASSIGN_FUNCTION(float8e4_silence_nan);
    ASSIGN_FUNCTION(float8e4_default_nan);

    ASSIGN_FUNCTION(float8e4_is_any_nan);
    ASSIGN_FUNCTION(float8e4_is_neg);
    ASSIGN_FUNCTION(float8e4_is_infinity);
    ASSIGN_FUNCTION(float8e4_is_zero);
    ASSIGN_FUNCTION(float8e4_is_zero_or_denormal);
    ASSIGN_FUNCTION(float8e4_is_normal);
    ASSIGN_FUNCTION(float8e4_abs);
    ASSIGN_FUNCTION(float8e4_chs);
    ASSIGN_FUNCTION(float8e4_set_sign);
    ASSIGN_FUNCTION(float8e4_eq);
    ASSIGN_FUNCTION(float8e4_le);
    ASSIGN_FUNCTION(float8e4_lt);
    ASSIGN_FUNCTION(float8e4_unordered);
    ASSIGN_FUNCTION(float8e4_eq_quiet);
    ASSIGN_FUNCTION(float8e4_le_quiet);
    ASSIGN_FUNCTION(float8e4_lt_quiet);
    ASSIGN_FUNCTION(float8e4_unordered_quiet);

/* ---------------- float 8e5 ------------------ */
    ASSIGN_FUNCTION(float8e5_round_to_int);
    ASSIGN_FUNCTION(bfloat16_to_float8e5);
    ASSIGN_FUNCTION(float8e5_to_bfloat16);
    ASSIGN_FUNCTION(float16_to_float8e5);
    ASSIGN_FUNCTION(float8e5_to_float16);
    ASSIGN_FUNCTION(float32_to_float8e5);
    ASSIGN_FUNCTION(float8e5_to_float32);
    ASSIGN_FUNCTION(float64_to_float8e5);
    ASSIGN_FUNCTION(float8e5_to_float64);
    ASSIGN_FUNCTION(float8e5_to_int8_scalbn);
    ASSIGN_FUNCTION(float8e5_to_int16_scalbn);
    ASSIGN_FUNCTION(float8e5_to_int32_scalbn);
    ASSIGN_FUNCTION(float8e5_to_int64_scalbn);
    ASSIGN_FUNCTION(float8e5_to_int8);
    ASSIGN_FUNCTION(float8e5_to_int16);
    ASSIGN_FUNCTION(float8e5_to_int32);
    ASSIGN_FUNCTION(float8e5_to_int64);
    ASSIGN_FUNCTION(float8e5_to_int8_round_to_zero);
    ASSIGN_FUNCTION(float8e5_to_int16_round_to_zero);
    ASSIGN_FUNCTION(float8e5_to_int32_round_to_zero);
    ASSIGN_FUNCTION(float8e5_to_int64_round_to_zero);
    ASSIGN_FUNCTION(float8e5_to_uint8_scalbn);
    ASSIGN_FUNCTION(float8e5_to_uint16_scalbn);
    ASSIGN_FUNCTION(float8e5_to_uint32_scalbn);
    ASSIGN_FUNCTION(float8e5_to_uint64_scalbn);
    ASSIGN_FUNCTION(float8e5_to_uint8);
    ASSIGN_FUNCTION(float8e5_to_uint16);
    ASSIGN_FUNCTION(float8e5_to_uint32);
    ASSIGN_FUNCTION(float8e5_to_uint64);
    ASSIGN_FUNCTION(float8e5_to_uint8_round_to_zero);
    ASSIGN_FUNCTION(float8e5_to_uint16_round_to_zero);
    ASSIGN_FUNCTION(float8e5_to_uint32_round_to_zero);
    ASSIGN_FUNCTION(float8e5_to_uint64_round_to_zero);
    ASSIGN_FUNCTION(int8_to_float8e5_scalbn);
    ASSIGN_FUNCTION(int16_to_float8e5_scalbn);
    ASSIGN_FUNCTION(int32_to_float8e5_scalbn);
    ASSIGN_FUNCTION(int64_to_float8e5_scalbn);
    ASSIGN_FUNCTION(uint8_to_float8e5_scalbn);
    ASSIGN_FUNCTION(uint16_to_float8e5_scalbn);
    ASSIGN_FUNCTION(uint32_to_float8e5_scalbn);
    ASSIGN_FUNCTION(uint64_to_float8e5_scalbn);
    ASSIGN_FUNCTION(int8_to_float8e5);
    ASSIGN_FUNCTION(int16_to_float8e5);
    ASSIGN_FUNCTION(int32_to_float8e5);
    ASSIGN_FUNCTION(int64_to_float8e5);
    ASSIGN_FUNCTION(uint8_to_float8e5);
    ASSIGN_FUNCTION(uint16_to_float8e5);
    ASSIGN_FUNCTION(uint32_to_float8e5);
    ASSIGN_FUNCTION(uint64_to_float8e5);

    ASSIGN_FUNCTION(float8e5_add);
    ASSIGN_FUNCTION(float8e5_sub);
    ASSIGN_FUNCTION(float8e5_mul);
    ASSIGN_FUNCTION(float8e5_div);
    ASSIGN_FUNCTION(float8e5_muladd);
    ASSIGN_FUNCTION(float8e5_scalbn);
    ASSIGN_FUNCTION(float8e5_min);
    ASSIGN_FUNCTION(float8e5_max);
    ASSIGN_FUNCTION(float8e5_minnum);
    ASSIGN_FUNCTION(float8e5_maxnum);
    ASSIGN_FUNCTION(float8e5_minnummag);
    ASSIGN_FUNCTION(float8e5_maxnummag);
    ASSIGN_FUNCTION(float8e5_minimum_number);
    ASSIGN_FUNCTION(float8e5_maximum_number);
    ASSIGN_FUNCTION(float8e5_sqrt);
    ASSIGN_FUNCTION(float8e5_compare);
    ASSIGN_FUNCTION(float8e5_compare_quiet);
    ASSIGN_FUNCTION(float8e5_is_quiet_nan);
    ASSIGN_FUNCTION(float8e5_is_signaling_nan);
    ASSIGN_FUNCTION(float8e5_silence_nan);
    ASSIGN_FUNCTION(float8e5_default_nan);

    ASSIGN_FUNCTION(float8e5_is_any_nan);
    ASSIGN_FUNCTION(float8e5_is_neg);
    ASSIGN_FUNCTION(float8e5_is_infinity);
    ASSIGN_FUNCTION(float8e5_is_zero);
    ASSIGN_FUNCTION(float8e5_is_zero_or_denormal);
    ASSIGN_FUNCTION(float8e5_is_normal);
    ASSIGN_FUNCTION(float8e5_abs);
    ASSIGN_FUNCTION(float8e5_chs);
    ASSIGN_FUNCTION(float8e5_set_sign);
    ASSIGN_FUNCTION(float8e5_eq);
    ASSIGN_FUNCTION(float8e5_le);
    ASSIGN_FUNCTION(float8e5_lt);
    ASSIGN_FUNCTION(float8e5_unordered);
    ASSIGN_FUNCTION(float8e5_eq_quiet);
    ASSIGN_FUNCTION(float8e5_le_quiet);
    ASSIGN_FUNCTION(float8e5_lt_quiet);
    ASSIGN_FUNCTION(float8e5_unordered_quiet);

/* ---------------- f32 ------------------ */
    ASSIGN_FUNCTION(float32_to_int16_scalbn);
    ASSIGN_FUNCTION(float32_to_int32_scalbn);
    ASSIGN_FUNCTION(float32_to_int64_scalbn);
    ASSIGN_FUNCTION(float32_to_int16);
    ASSIGN_FUNCTION(float32_to_int32);
    ASSIGN_FUNCTION(float32_to_int64);
    ASSIGN_FUNCTION(float32_to_int16_round_to_zero);
    ASSIGN_FUNCTION(float32_to_int32_round_to_zero);
    ASSIGN_FUNCTION(float32_to_int64_round_to_zero);
    ASSIGN_FUNCTION(float32_to_uint16_scalbn);
    ASSIGN_FUNCTION(float32_to_uint32_scalbn);
    ASSIGN_FUNCTION(float32_to_uint64_scalbn);
    ASSIGN_FUNCTION(float32_to_uint16);
    ASSIGN_FUNCTION(float32_to_uint32);
    ASSIGN_FUNCTION(float32_to_uint64);
    ASSIGN_FUNCTION(float32_to_uint16_round_to_zero);
    ASSIGN_FUNCTION(float32_to_uint32_round_to_zero);
    ASSIGN_FUNCTION(float32_to_uint64_round_to_zero);
    ASSIGN_FUNCTION(float32_to_float64);

    ASSIGN_FUNCTION(float32_round_to_int);
    ASSIGN_FUNCTION(float32_add);
    ASSIGN_FUNCTION(float32_sub);
    ASSIGN_FUNCTION(float32_mul);
    ASSIGN_FUNCTION(float32_div);
    ASSIGN_FUNCTION(float32_rem);
    ASSIGN_FUNCTION(float32_muladd);
    ASSIGN_FUNCTION(float32_sqrt);
    ASSIGN_FUNCTION(float32_exp2);
    ASSIGN_FUNCTION(float32_log2);
    ASSIGN_FUNCTION(float32_compare);
    ASSIGN_FUNCTION(float32_compare_quiet);
    ASSIGN_FUNCTION(float32_min);
    ASSIGN_FUNCTION(float32_max);
    ASSIGN_FUNCTION(float32_minnum);
    ASSIGN_FUNCTION(float32_maxnum);
    ASSIGN_FUNCTION(float32_minnummag);
    ASSIGN_FUNCTION(float32_maxnummag);
    ASSIGN_FUNCTION(float32_minimum_number);
    ASSIGN_FUNCTION(float32_maximum_number);
    ASSIGN_FUNCTION(float32_is_quiet_nan);
    ASSIGN_FUNCTION(float32_is_signaling_nan);
    ASSIGN_FUNCTION(float32_silence_nan);
    ASSIGN_FUNCTION(float32_scalbn);

    ASSIGN_FUNCTION(float32_abs);
    ASSIGN_FUNCTION(float32_chs);
    ASSIGN_FUNCTION(float32_is_infinity);
    ASSIGN_FUNCTION(float32_is_neg);
    ASSIGN_FUNCTION(float32_is_zero);
    ASSIGN_FUNCTION(float32_is_any_nan);
    ASSIGN_FUNCTION(float32_is_zero_or_denormal);
    ASSIGN_FUNCTION(float32_is_normal);
    ASSIGN_FUNCTION(float32_is_denormal);
    ASSIGN_FUNCTION(float32_is_zero_or_normal);
    ASSIGN_FUNCTION(float32_set_sign);
    ASSIGN_FUNCTION(float32_eq);
    ASSIGN_FUNCTION(float32_le);
    ASSIGN_FUNCTION(float32_lt);
    ASSIGN_FUNCTION(float32_unordered);
    ASSIGN_FUNCTION(float32_eq_quiet);
    ASSIGN_FUNCTION(float32_le_quiet);
    ASSIGN_FUNCTION(float32_lt_quiet);
    ASSIGN_FUNCTION(float32_unordered_quiet);

    ASSIGN_FUNCTION(packFloat32);
    ASSIGN_FUNCTION(float32_default_nan);

/* ---------------- f64 ------------------ */
    ASSIGN_FUNCTION(float64_to_int16_scalbn);
    ASSIGN_FUNCTION(float64_to_int32_scalbn);
    ASSIGN_FUNCTION(float64_to_int64_scalbn);
    ASSIGN_FUNCTION(float64_to_int16);
    ASSIGN_FUNCTION(float64_to_int32);
    ASSIGN_FUNCTION(float64_to_int64);
    ASSIGN_FUNCTION(float64_to_int16_round_to_zero);
    ASSIGN_FUNCTION(float64_to_int32_round_to_zero);
    ASSIGN_FUNCTION(float64_to_int64_round_to_zero);
    ASSIGN_FUNCTION(float64_to_int32_modulo);
    ASSIGN_FUNCTION(float64_to_int64_modulo);
    ASSIGN_FUNCTION(float64_to_uint16_scalbn);
    ASSIGN_FUNCTION(float64_to_uint32_scalbn);
    ASSIGN_FUNCTION(float64_to_uint64_scalbn);
    ASSIGN_FUNCTION(float64_to_uint16);
    ASSIGN_FUNCTION(float64_to_uint32);
    ASSIGN_FUNCTION(float64_to_uint64);
    ASSIGN_FUNCTION(float64_to_uint16_round_to_zero);
    ASSIGN_FUNCTION(float64_to_uint32_round_to_zero);
    ASSIGN_FUNCTION(float64_to_uint64_round_to_zero);
    ASSIGN_FUNCTION(float64_to_float32);

    ASSIGN_FUNCTION(float64_round_to_int);
    ASSIGN_FUNCTION(float64_add);
    ASSIGN_FUNCTION(float64_sub);
    ASSIGN_FUNCTION(float64_mul);
    ASSIGN_FUNCTION(float64_div);
    ASSIGN_FUNCTION(float64_rem);
    ASSIGN_FUNCTION(float64_muladd);
    ASSIGN_FUNCTION(float64_sqrt);
    ASSIGN_FUNCTION(float64_log2);
    ASSIGN_FUNCTION(float64_compare);
    ASSIGN_FUNCTION(float64_compare_quiet);
    ASSIGN_FUNCTION(float64_min);
    ASSIGN_FUNCTION(float64_max);
    ASSIGN_FUNCTION(float64_minnum);
    ASSIGN_FUNCTION(float64_maxnum);
    ASSIGN_FUNCTION(float64_minnummag);
    ASSIGN_FUNCTION(float64_maxnummag);
    ASSIGN_FUNCTION(float64_minimum_number);
    ASSIGN_FUNCTION(float64_maximum_number);
    ASSIGN_FUNCTION(float64_is_quiet_nan);
    ASSIGN_FUNCTION(float64_is_signaling_nan);
    ASSIGN_FUNCTION(float64_silence_nan);
    ASSIGN_FUNCTION(float64_scalbn);
    ASSIGN_FUNCTION(float64_abs);
    ASSIGN_FUNCTION(float64_chs);
    ASSIGN_FUNCTION(float64_is_infinity);
    ASSIGN_FUNCTION(float64_is_neg);
    ASSIGN_FUNCTION(float64_is_zero);
    ASSIGN_FUNCTION(float64_is_any_nan);
    ASSIGN_FUNCTION(float64_is_zero_or_denormal);
    ASSIGN_FUNCTION(float64_is_normal);
    ASSIGN_FUNCTION(float64_is_denormal);
    ASSIGN_FUNCTION(float64_is_zero_or_normal);
    ASSIGN_FUNCTION(float64_set_sign);
    ASSIGN_FUNCTION(float64_eq);
    ASSIGN_FUNCTION(float64_le);
    ASSIGN_FUNCTION(float64_lt);
    ASSIGN_FUNCTION(float64_unordered);
    ASSIGN_FUNCTION(float64_eq_quiet);
    ASSIGN_FUNCTION(float64_le_quiet);
    ASSIGN_FUNCTION(float64_lt_quiet);
    ASSIGN_FUNCTION(float64_unordered_quiet);

    ASSIGN_FUNCTION(float64_default_nan);
    ASSIGN_FUNCTION(float64r32_add);
    ASSIGN_FUNCTION(float64r32_sub);
    ASSIGN_FUNCTION(float64r32_mul);
    ASSIGN_FUNCTION(float64r32_div);
    ASSIGN_FUNCTION(float64r32_muladd);
    ASSIGN_FUNCTION(float64r32_sqrt);

    #undef ASSIGN_FUNCTION
    #undef ASSIGN_WARP_FUNCTION
}

void dsa_finalize(RISCVCPU *cpu, Error **errp)
{
    CPURISCVState *env = &cpu->env;
    GModule  *module;
    gpointer sym;
    env->dsa_ops = g_malloc0(sizeof(riscv_dsa_ops));
    env->qdsa_ops = g_malloc0(sizeof(qemu_dsa_ops));
    env->qdsa_float_ops = g_malloc0(sizeof(qemu_float_ops));

    /* Handle dsa options */
    QemuOptsList *ret;
    QemuOpts *opts;
    char *str = NULL;
    ret = qemu_find_opts("dsa");
    if (ret) {
        opts = qemu_opts_find(ret, NULL);
        if (opts) {
            str = qemu_opt_get_del(opts, "file");
            if (str != NULL) {
                env->dsa_en = true;
            } else {
                env->dsa_en = false;
            }
        }
    } else {
        env->dsa_en = false;
    }

    if (!env->dsa_en) {
        return;
    }

    /* Load the dsa.so */
    module = g_module_open(str, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (module == NULL) {
        error_setg(errp, "Could not load %s: %s", str, g_module_error());
        env->dsa_en = false;
        return;
    }

    /* Initialize the functions provided by qemu for dsa */
    env->qdsa_ops->get_gpr = dsa_get_gpr;
    env->qdsa_ops->set_gpr = dsa_set_gpr;
    env->qdsa_ops->get_fpr = dsa_get_fpr;
    env->qdsa_ops->set_fpr = dsa_set_fpr;
    env->qdsa_ops->get_csr = dsa_get_csr;
    env->qdsa_ops->set_csr = dsa_set_csr;
    env->qdsa_ops->get_vector_element = dsa_get_vector_element;
    env->qdsa_ops->set_vector_element = dsa_set_vector_element;
    env->qdsa_ops->set_matrix_element = dsa_set_matrix_element;
    env->qdsa_ops->get_matrix_element = dsa_get_matrix_element;
    env->qdsa_ops->load_data = dsa_load_data;
    env->qdsa_ops->store_data = dsa_store_data;
    env->qdsa_ops->get_reg_address = dsa_get_reg_address;

    assign_dsa_float_ops(env);

    /* Initialize the functions provided by dsa */
    if (!g_module_symbol(module, "dsa_init", &sym)) {
        error_setg(errp, "Could not load %s: %s", str, g_module_error());
        env->dsa_en = false;
        g_module_close(module);
        return;
    }

    dsa_init_fn init = (dsa_init_fn)sym;
    /* symbol was found; it could be NULL though */
    if (init == NULL) {
        error_setg(errp, "Could not load %s: dsa_init is NULL", str);
        env->dsa_en = false;
        g_module_close(module);
        return;
    }

    init(env, env->dsa_ops, env->qdsa_ops);

    if (env->dsa_ops->version != DSA_VER_0_1) {
        error_setg(errp, "DSA version not supported,\
 QEMU support version %s: 0x%x now", "DSA_VER_0_1", DSA_VER_0_1);
        env->dsa_en = false;
        g_module_close(module);
        return;
    }

    /* Couldn't find disasm function, set NULL but still continue */
    if (!g_module_symbol(module, "dsa_disasm_inst", &sym)) {
        cpu->cfg.dsa_disasm = NULL;
    } else {
        cpu->cfg.dsa_disasm = (dsa_disasm_fn)sym;
    }

    return ;
}



