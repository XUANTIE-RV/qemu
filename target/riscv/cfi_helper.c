/*
 * RISC-V CFI Extension Helpers for QEMU.
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
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

void HELPER(sspush)(CPURISCVState *env, target_ulong rs2)
{
    int xlen = riscv_cpu_xlen(env);
    /* If the virtual address in ssp is not XLEN aligned, raise exception */
    if (env->ssp % (xlen / 8)) {
        riscv_raise_exception(env, RISCV_EXCP_STORE_AMO_ACCESS_FAULT, GETPC());
    }
    abi_ptr addr = env->ssp - xlen / 8;
    int mmu_idx = riscv_env_mmu_index(env, false) & MMU_IDX_SS_ACCESS;
    switch (xlen) {
    case 64:
        cpu_stq_mmuidx_ra(env, addr, rs2, mmu_idx, GETPC());
        break;
    case 32:
        cpu_stl_mmuidx_ra(env, addr, rs2, mmu_idx, GETPC());
        break;
    default:
        g_assert_not_reached();
        break;
    }
    env->ssp = addr;
}

void HELPER(sspopchk)(CPURISCVState *env, target_ulong rs1)
{
    int xlen = riscv_cpu_xlen(env);
    if (env->ssp % (xlen / 8)) {
        riscv_raise_exception(env, RISCV_EXCP_STORE_AMO_ACCESS_FAULT, GETPC());
    }
    abi_ptr addr = env->ssp;
    int mmu_idx = riscv_env_mmu_index(env, false) & MMU_IDX_SS_ACCESS;
    target_ulong temp = 0;
    switch (xlen) {
    case 64:
        temp = cpu_ldq_mmuidx_ra(env, addr, mmu_idx, GETPC());
        break;
    case 32:
        temp = cpu_ldl_mmuidx_ra(env, addr, mmu_idx, GETPC());
        break;
    default:
        g_assert_not_reached();
        break;
    }
    if (temp != rs1) {
        env->cfi_violation_code = RISCV_EXCP_SW_CHECK_BCFI_VIOLATION_CODE;
        riscv_raise_exception(env, RISCV_EXCP_SW_CHECK, GETPC());
    }
    env->ssp += xlen / 8;
}

static void ssamoswap_check(CPURISCVState *env)
{
    /*
     * if privilege_mode != M && menvcfg.sse == 0
     *      raise illegal-instruction exception
     * if S-mode not implemented
     *      raise illegal-instruction exception
     * else if privilege_mode == U && senvcfg.sse == 0
     *      raise illegal-instruction exception
     * else if privilege_mode == VS && henvcfg.sse == 0
     *      raise virtual instruction exception
     * else if privilege_mode == VU && senvcfg.sse == 0
     *      raise virtual instruction exception
     */
    if (env->priv != PRV_M && (env->menvcfg & MENVCFG_SSE) == 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
    if (!riscv_has_ext(env, RVS)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
    if (!env->virt_enabled && env->priv == PRV_U &&
        (env->senvcfg & SENVCFG_SSE) == 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
    if (env->virt_enabled && env->priv == PRV_S &&
        (env->henvcfg & HENVCFG_SSE) == 0) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }
    if (env->virt_enabled && env->priv == PRV_U &&
        (env->senvcfg & SENVCFG_SSE) == 0) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }
}

void HELPER(ssamoswap_d)(CPURISCVState *env, target_ulong rs1,
                         target_ulong rs2, void *rd)
{
    ssamoswap_check(env);
    int mmu_idx = riscv_env_mmu_index(env, false) & MMU_IDX_SS_ACCESS;
    abi_ptr addr = rs1;
    if (addr % 8) {
        riscv_raise_exception(env, RISCV_EXCP_STORE_AMO_ACCESS_FAULT, GETPC());
    }

    *((uint64_t *)rd) = cpu_ldq_mmuidx_ra(env, addr, mmu_idx, GETPC());
    cpu_stq_mmuidx_ra(env, addr, rs2, mmu_idx, GETPC());
}

void HELPER(ssamoswap_w)(CPURISCVState *env, target_ulong rs1,
                         target_ulong rs2, void *rd)
{
    ssamoswap_check(env);
    uint32_t s1 = rs1;
    uint32_t s2 = rs2;
    int xl = riscv_cpu_mxl(env);
    int xlen = 0;
    int mmu_idx = riscv_env_mmu_index(env, false) & MMU_IDX_SS_ACCESS;
    abi_ptr addr = s1;
    switch (xl) {
    case MXL_RV32:
        xlen = 64;
        break;
    case MXL_RV64:
        xlen = 32;
        break;
    default:
        g_assert_not_reached();
        break;
    }
    if (addr % (xlen / 8)) {
        riscv_raise_exception(env, RISCV_EXCP_STORE_AMO_ACCESS_FAULT, GETPC());
    }
    if (xl == MXL_RV32) {
        *((uint32_t *)rd) = cpu_ldl_mmuidx_ra(env, addr, mmu_idx, GETPC());
    } else {
        *((uint64_t *)rd) = (uint64_t)(int64_t)(int32_t)
                            cpu_ldl_mmuidx_ra(env, addr, mmu_idx, GETPC());
    }
    cpu_stl_mmuidx_ra(env, addr, s2, mmu_idx, GETPC());
}

void HELPER(lpad)(CPURISCVState *env, target_ulong pc, target_ulong lpl, target_ulong x7)
{
    if (riscv_cpu_get_xlpe(env) == false ||
        env->elp == NO_LP_EXPECTED) {
        return;
    }

    if ((pc % 4 != 0) ||
        (lpl != 0 && lpl != x7)) {
        env->cfi_violation_code = RISCV_EXCP_SW_CHECK_FCFI_VIOLATION_CODE;
        riscv_raise_exception(env, RISCV_EXCP_SW_CHECK, GETPC());
    }
    env->elp = NO_LP_EXPECTED;
}
