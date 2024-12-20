/*
 * RISC-V Emulation Helpers for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 * Copyright (c) 2022      VRULL GmbH
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
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "qemu/log.h"

#if !defined(CONFIG_USER_ONLY)
#include "hw/intc/xt_clic.h"
#include "qemu/main-loop.h"
#endif

void helper_tb_trace(CPURISCVState *env, target_ulong tb_pc)
{
    int trace_index = env->trace_index % TB_TRACE_NUM;
    env->trace_info[trace_index].tb_pc = tb_pc;
    env->trace_info[trace_index].notjmp = false;
    env->trace_index++;
    if (env->jcount_enable == 0) {
        qemu_log_mask(CPU_TB_TRACE, "0x" TARGET_FMT_lx "\n", tb_pc);
    } else if ((tb_pc > env->jcount_start) &&
                (tb_pc < env->jcount_end)) {
        qemu_log_mask(CPU_TB_TRACE, "0x" TARGET_FMT_lx "\n", tb_pc);
    }
}

void helper_tag_pctrace(CPURISCVState *env, target_ulong tb_pc)
{
    if (env->trace_index < 1) {
        return;
    }
    int trace_index = (env->trace_index - 1) % TB_TRACE_NUM;
    if (env->trace_info[trace_index].tb_pc == tb_pc) {
        env->trace_info[trace_index].notjmp = true;
    }
}

#ifdef CONFIG_USER_ONLY
extern long long total_jcount;
void helper_jcount(CPURISCVState *env, target_ulong tb_pc, uint32_t icount)
{
    if ((tb_pc >= env->jcount_start) && (tb_pc < env->jcount_end)) {
        total_jcount += icount;
    }
}
#else
void helper_jcount(CPURISCVState *env, target_ulong tb_pc, uint32_t icount)
{
}
#endif

/* Exceptions processing helpers */
G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      uint32_t exception, uintptr_t pc)
{
    CPUState *cs = env_cpu(env);
    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

void helper_raise_exception(CPURISCVState *env, uint32_t exception)
{
    riscv_raise_exception(env, exception, 0);
}

target_ulong helper_csrr(CPURISCVState *env, int csr)
{
    /*
     * The seed CSR must be accessed with a read-write instruction. A
     * read-only instruction such as CSRRS/CSRRC with rs1=x0 or CSRRSI/
     * CSRRCI with uimm=0 will raise an illegal instruction exception.
     */
    if (csr == CSR_SEED) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong val = 0;
    RISCVException ret = riscv_csrr(env, csr, &val);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return val;
}

void helper_csrw(CPURISCVState *env, int csr, target_ulong src)
{
    target_ulong mask = env->xl == MXL_RV32 ? UINT32_MAX : (target_ulong)-1;
    RISCVException ret = riscv_csrrw(env, csr, NULL, src, mask);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
}

target_ulong helper_csrrw(CPURISCVState *env, int csr,
                          target_ulong src, target_ulong write_mask)
{
    target_ulong val = 0;
    RISCVException ret = riscv_csrrw(env, csr, &val, src, write_mask);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return val;
}

target_ulong helper_csrr_i128(CPURISCVState *env, int csr)
{
    Int128 rv = int128_zero();
    RISCVException ret = riscv_csrr_i128(env, csr, &rv);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    env->retxh = int128_gethi(rv);
    return int128_getlo(rv);
}

void helper_csrw_i128(CPURISCVState *env, int csr,
                      target_ulong srcl, target_ulong srch)
{
    RISCVException ret = riscv_csrrw_i128(env, csr, NULL,
                                          int128_make128(srcl, srch),
                                          UINT128_MAX);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
}

target_ulong helper_csrrw_i128(CPURISCVState *env, int csr,
                       target_ulong srcl, target_ulong srch,
                       target_ulong maskl, target_ulong maskh)
{
    Int128 rv = int128_zero();
    RISCVException ret = riscv_csrrw_i128(env, csr, &rv,
                                          int128_make128(srcl, srch),
                                          int128_make128(maskl, maskh));

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    env->retxh = int128_gethi(rv);
    return int128_getlo(rv);
}


/*
 * check_zicbo_envcfg
 *
 * Raise virtual exceptions and illegal instruction exceptions for
 * Zicbo[mz] instructions based on the settings of [mhs]envcfg as
 * specified in section 2.5.1 of the CMO specification.
 */
static void check_zicbo_envcfg(CPURISCVState *env, target_ulong envbits,
                                uintptr_t ra)
{
#ifndef CONFIG_USER_ONLY
    if ((env->priv < PRV_M) && !get_field(env->menvcfg, envbits)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    if (env->virt_enabled &&
        (((env->priv <= PRV_S) && !get_field(env->henvcfg, envbits)) ||
         ((env->priv < PRV_S) && !get_field(env->senvcfg, envbits)))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, ra);
    }

    if ((env->priv < PRV_S) && !get_field(env->senvcfg, envbits)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
#endif
}

void helper_cbo_zero(CPURISCVState *env, target_ulong address)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint16_t cbozlen = cpu->cfg.cboz_blocksize;
    int mmu_idx = riscv_env_mmu_index(env, false);
    uintptr_t ra = GETPC();
    void *mem;

    check_zicbo_envcfg(env, MENVCFG_CBZE, ra);

    /* Mask off low-bits to align-down to the cache-block. */
    address &= ~(cbozlen - 1);

    /*
     * cbo.zero requires MMU_DATA_STORE access. Do a probe_write()
     * to raise any exceptions, including PMP.
     */
    mem = probe_write(env, address, cbozlen, mmu_idx, ra);

    if (likely(mem)) {
        memset(mem, 0, cbozlen);
    } else {
        /*
         * This means that we're dealing with an I/O page. Section 4.2
         * of cmobase v1.0.1 says:
         *
         * "Cache-block zero instructions store zeros independently
         * of whether data from the underlying memory locations are
         * cacheable."
         *
         * Write zeros in address + cbozlen regardless of not being
         * a RAM page.
         */
        for (int i = 0; i < cbozlen; i++) {
            cpu_stb_mmuidx_ra(env, address + i, 0, mmu_idx, ra);
        }
    }
}

/*
 * check_zicbom_access
 *
 * Check access permissions (LOAD, STORE or FETCH as specified in
 * section 2.5.2 of the CMO specification) for Zicbom, raising
 * either store page-fault (non-virtualized) or store guest-page
 * fault (virtualized).
 */
static void check_zicbom_access(CPURISCVState *env,
                                target_ulong address,
                                uintptr_t ra)
{
    RISCVCPU *cpu = env_archcpu(env);
    int mmu_idx = riscv_env_mmu_index(env, false);
    uint16_t cbomlen = cpu->cfg.cbom_blocksize;
    void *phost;
    int ret;

    /* Mask off low-bits to align-down to the cache-block. */
    address &= ~(cbomlen - 1);

    /*
     * Section 2.5.2 of cmobase v1.0.1:
     *
     * "A cache-block management instruction is permitted to
     * access the specified cache block whenever a load instruction
     * or store instruction is permitted to access the corresponding
     * physical addresses. If neither a load instruction nor store
     * instruction is permitted to access the physical addresses,
     * but an instruction fetch is permitted to access the physical
     * addresses, whether a cache-block management instruction is
     * permitted to access the cache block is UNSPECIFIED."
     */
    ret = probe_access_flags(env, address, cbomlen, MMU_DATA_LOAD,
                             mmu_idx, true, &phost, ra);
    if (ret != TLB_INVALID_MASK) {
        /* Success: readable */
        return;
    }

    /*
     * Since not readable, must be writable. On failure, store
     * fault/store guest amo fault will be raised by
     * riscv_cpu_tlb_fill(). PMP exceptions will be caught
     * there as well.
     */
    probe_write(env, address, cbomlen, mmu_idx, ra);
}

void helper_cbo_clean_flush(CPURISCVState *env, target_ulong address)
{
    uintptr_t ra = GETPC();
    check_zicbo_envcfg(env, MENVCFG_CBCFE, ra);
    check_zicbom_access(env, address, ra);

    /* We don't emulate the cache-hierarchy, so we're done. */
}

void helper_cbo_inval(CPURISCVState *env, target_ulong address)
{
    uintptr_t ra = GETPC();
    check_zicbo_envcfg(env, MENVCFG_CBIE, ra);
    check_zicbom_access(env, address, ra);

    /* We don't emulate the cache-hierarchy, so we're done. */
}

#ifndef CONFIG_USER_ONLY
target_ulong helper_sret(CPURISCVState *env, target_ulong curr_pc)
{
    uint64_t mstatus;
    target_ulong prev_priv, prev_virt = env->virt_enabled;
    const target_ulong src_priv = env->priv;
    const bool src_virt = env->virt_enabled;

    if (!(env->priv >= PRV_S)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong retpc = env->sepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS, GETPC());
    }

    if (get_field(env->mstatus, MSTATUS_TSR) && !(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    if (env->virt_enabled && get_field(env->hstatus, HSTATUS_VTSR)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    mstatus = env->mstatus;
    prev_priv = get_field(mstatus, MSTATUS_SPP);
    mstatus = set_field(mstatus, MSTATUS_SIE,
                        get_field(mstatus, MSTATUS_SPIE));
    mstatus = set_field(mstatus, MSTATUS_SPIE, 1);
    mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);
    if (riscv_cpu_cfg(env)->ext_ssdbltrp) {
        if (env->virt_enabled) {
            if (get_field(env->henvcfg, HENVCFG_DTE)) {
                mstatus = set_field(mstatus, MSTATUS_SDT, 0);
            }
        } else {
            if (get_field(env->menvcfg, MENVCFG_DTE)) {
                mstatus = set_field(mstatus, MSTATUS_SDT, 0);
            }
        }
    }
    if (env->priv_ver >= PRIV_VERSION_1_12_0) {
        mstatus = set_field(mstatus, MSTATUS_MPRV, 0);
    }
    env->mstatus = mstatus;

    if (riscv_has_ext(env, RVH) && !env->virt_enabled) {
        /* We support Hypervisor extensions and virtulisation is disabled */
        target_ulong hstatus = env->hstatus;

        prev_virt = get_field(hstatus, HSTATUS_SPV);

        hstatus = set_field(hstatus, HSTATUS_SPV, 0);

        env->hstatus = hstatus;

        if (prev_virt) {
            riscv_cpu_swap_hypervisor_regs(env);
        }
    }

    if (xt_clic_is_clic_mode(env)) {
        target_ulong spil = get_field(env->scause, SCAUSE_SPIL);
        env->mintstatus = set_field(env->mintstatus, MINTSTATUS_SIL, spil);
        env->scause = set_field(env->scause, SCAUSE_SPIE, 1);
        env->scause = set_field(env->scause, SCAUSE_SPP, PRV_U);
        bql_lock();
        xt_clic_get_next_interrupt(env->clic);
        bql_unlock();
    }
    riscv_cpu_set_mode(env, prev_priv, prev_virt);

    /* The new priv is set, we can use same function to get lpe*/
    if (riscv_cpu_get_xlpe(env)) {
        /* return from vs mode */
        if (src_virt) {
            env->elp = get_field(env->vsstatus, VSSTATUS_SPELP);
            env->vsstatus = set_field(env->vsstatus, VSSTATUS_SPELP,
                                      NO_LP_EXPECTED);
        } else {
            env->elp = get_field(env->mstatus, MSTATUS_SPELP);
            env->mstatus = set_field(env->mstatus, MSTATUS_SPELP,
                                     NO_LP_EXPECTED);
        }
    } else {
        env->elp = NO_LP_EXPECTED;
    }

    riscv_ctr_add_entry(env, curr_pc, retpc, CTRDATA_TYPE_EXCEP_INT_RET,
                        src_priv, src_virt);
    return retpc;
}

static target_ulong do_excp_return(CPURISCVState *env, target_ulong ra, target_ulong curr_pc)
{
    const target_ulong src_priv = env->priv;
    const bool src_virt = env->virt_enabled;

    if (!(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong retpc = env->mepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS, GETPC());
    }

    /* if CLIC mode, copy mcause.mpil into minstatus.mil */
    if ((env->mtvec & 0b111110) == 0b000010) {
        target_ulong mpil = get_field(env->mcause, MCAUSE_MPIL);
        env->mintstatus = set_field(env->mintstatus, MINTSTATUS_MIL, mpil);
        if ((mpil == 0) && (env->mexstatus & MEXSTATUS_SPSWAP)
            && ((target_long)env->mcause < 0)) {
            target_ulong tmp = env->mscratch;
            env->mscratch = env->gpr[2];
            env->gpr[2] = tmp;
        }
    }

    uint64_t mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_MPP);

    if (riscv_cpu_cfg(env)->pmp &&
        !pmp_get_num_rules(env) && (prev_priv != PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ACCESS_FAULT, GETPC());
    }

    target_ulong prev_virt = get_field(env->mstatus, MSTATUS_MPV) &&
                             (prev_priv != PRV_M);
    mstatus = set_field(mstatus, MSTATUS_MIE,
                        get_field(mstatus, MSTATUS_MPIE));
    mstatus = set_field(mstatus, MSTATUS_MPIE, 1);
    mstatus = set_field(mstatus, MSTATUS_MPP,
                        riscv_has_ext(env, RVU) ? PRV_U : PRV_M);
    mstatus = set_field(mstatus, MSTATUS_MPV, 0);
    if (riscv_cpu_cfg(env)->ext_smdbltrp) {
        mstatus = set_field(mstatus, MSTATUS_MDT, 0);
    }
    if ((env->priv_ver >= PRIV_VERSION_1_12_0) && (prev_priv != PRV_M)) {
        mstatus = set_field(mstatus, MSTATUS_MPRV, 0);
    }
    env->mstatus = mstatus;

    if (riscv_has_ext(env, RVH) && prev_virt) {
        riscv_cpu_swap_hypervisor_regs(env);
    }
    /* FIXME: Add Xuantie check */
    env->excp_vld = 0;

    if (xt_clic_is_clic_mode(env)) {
        target_ulong mpil = get_field(env->mcause, MCAUSE_MPIL);
        env->mintstatus = set_field(env->mintstatus, MINTSTATUS_MIL, mpil);
        env->mcause = set_field(env->mcause, MCAUSE_MPIE, 1);
        env->mcause = set_field(env->mcause, MCAUSE_MPP, PRV_U);
        bql_lock();
        xt_clic_get_next_interrupt(env->clic);
        bql_unlock();
    }
    riscv_cpu_set_mode(env, prev_priv, prev_virt);

    /* The new priv is set, we can use same function to get lpe*/
    if (riscv_cpu_get_xlpe(env)) {
        env->elp = get_field(env->mstatus, MSTATUS_MPELP);
    } else {
        env->elp = NO_LP_EXPECTED;
    }
    env->mstatus = set_field(env->mstatus, MSTATUS_MPELP, NO_LP_EXPECTED);

    riscv_ctr_add_entry(env, curr_pc, retpc, CTRDATA_TYPE_EXCEP_INT_RET,
                        src_priv, src_virt);
    return retpc;
}

target_ulong helper_mret(CPURISCVState *env, target_ulong curr_pc)
{
    return do_excp_return(env, GETPC(), curr_pc);
}

/*
 * Indirect calls
 * – jalr x1, rs where rs != x5;
 * – jalr x5, rs where rs != x1;
 * – c.jalr rs1 where rs1 != x5;
 *
 * Indirect jumps
 * – jalr x0, rs where rs != x1 and rs != x5;
 * – c.jr rs1 where rs1 != x1 and rs1 != x5.
 *
 * Returns
 * – jalr rd, rs where (rs == x1 or rs == x5) and rd != x1 and rd != x5;
 * – c.jr rs1 where rs1 == x1 or rs1 == x5.
 *
 * Co-routine swap
 * – jalr x1, x5;
 * – jalr x5, x1;
 * – c.jalr x5.
 *
 * Other indirect jumps
 * – jalr rd, rs where rs != x1, rs != x5, rd != x0, rd != x1 and rd != x5.
 */
void helper_ctr_jalr(CPURISCVState *env, target_ulong src, target_ulong dest,
                     target_ulong rd, target_ulong rs1)
{
    target_ulong curr_priv = env->priv;
    bool curr_virt = env->virt_enabled;

    if (riscv_cpu_get_xlpe(env) &&
        rs1 != 1 && rs1 != 5 && rs1 != 7) {
        env->elp = LP_EXPECTED;
    } else {
        env->elp = NO_LP_EXPECTED;
    }

    if ((rd == 1 && rs1 != 5) || (rd == 5 && rs1 != 1)) {
        riscv_ctr_add_entry(env, src, dest, CTRDATA_TYPE_INDIRECT_CALL,
                            curr_priv, curr_virt);
    } else if (rd == 0 && rs1 != 1 && rs1 != 5) {
        riscv_ctr_add_entry(env, src, dest, CTRDATA_TYPE_INDIRECT_JUMP,
                            curr_priv, curr_virt);
    } else if ((rs1 == 1 || rs1 == 5) && (rd != 1 && rd != 5)) {
        riscv_ctr_add_entry(env, src, dest, CTRDATA_TYPE_RETURN,
                            curr_priv, curr_virt);
    } else if ((rs1 == 1 && rd == 5) || (rs1 == 5 && rd == 1)) {
        riscv_ctr_add_entry(env, src, dest, CTRDATA_TYPE_CO_ROUTINE_SWAP,
                            curr_priv, curr_virt);
    } else {
        riscv_ctr_add_entry(env, src, dest,
                            CTRDATA_TYPE_OTHER_INDIRECT_JUMP, curr_priv,
                            curr_virt);
    }
}

/*
 * Direct calls
 * – jal x1;
 * – jal x5;
 * – c.jal.
 *
 * Direct jumps
 * – jal x0;
 * – c.j;
 *
 * Other direct jumps
 * – jal rd where rd != x1 and rd != x5 and rd != x0;
 */
void helper_ctr_jal(CPURISCVState *env, target_ulong src, target_ulong dest,
                    target_ulong rd)
{
    target_ulong priv = env->priv;
    bool virt = env->virt_enabled;

    /*
     * If rd is x1 or x5 link registers, treat this as direct call otherwise
     * its a direct jump.
     */
    if (rd == 1 || rd == 5) {
        riscv_ctr_add_entry(env, src, dest, CTRDATA_TYPE_DIRECT_CALL, priv,
                            virt);
    } else if (rd == 0) {
        riscv_ctr_add_entry(env, src, dest, CTRDATA_TYPE_DIRECT_JUMP, priv,
                            virt);
    } else {
        riscv_ctr_add_entry(env, src, dest, CTRDATA_TYPE_OTHER_DIRECT_JUMP,
                            priv, virt);
    }
}

void helper_ctr_branch(CPURISCVState *env, target_ulong src, target_ulong dest,
                       target_ulong branch_taken)
{
    target_ulong curr_priv = env->priv;
    bool curr_virt = env->virt_enabled;

    if (branch_taken) {
        riscv_ctr_add_entry(env, src, dest, CTRDATA_TYPE_TAKEN_BRANCH,
                            curr_priv, curr_virt);
    } else {
        riscv_ctr_add_entry(env, src, dest, CTRDATA_TYPE_NONTAKEN_BRANCH,
                            curr_priv, curr_virt);
    }
}

void helper_ctr_clear(CPURISCVState *env)
{
    riscv_ctr_clear(env);
}

void helper_wfi(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    bool rvs = riscv_has_ext(env, RVS);
    bool prv_u = env->priv == PRV_U;
    bool prv_s = env->priv == PRV_S;

    if (((prv_s || (!rvs && prv_u)) && get_field(env->mstatus, MSTATUS_TW)) ||
        (rvs && prv_u && !env->virt_enabled)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (env->virt_enabled &&
               (prv_u || (prv_s && get_field(env->hstatus, HSTATUS_VTW)))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
        cpu_loop_exit(cs);
    }
}

void helper_wrs_nto(CPURISCVState *env)
{
    if (env->virt_enabled && (env->priv == PRV_S || env->priv == PRV_U) &&
        get_field(env->hstatus, HSTATUS_VTW) &&
        !get_field(env->mstatus, MSTATUS_TW)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else if (env->priv != PRV_M && get_field(env->mstatus, MSTATUS_TW)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
}

void helper_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    if (!env->virt_enabled &&
        (env->priv == PRV_U ||
         (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)))) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (env->virt_enabled &&
               (env->priv == PRV_U || get_field(env->hstatus, HSTATUS_VTVM))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        tlb_flush(cs);
    }
}

void helper_tlb_flush_all(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    tlb_flush_all_cpus_synced(cs);
}

void helper_hyp_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);

    if (env->virt_enabled) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    if (env->priv == PRV_M ||
        (env->priv == PRV_S && !env->virt_enabled)) {
        tlb_flush(cs);
        return;
    }

    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
}

void helper_hyp_gvma_tlb_flush(CPURISCVState *env)
{
    if (env->priv == PRV_S && !env->virt_enabled &&
        get_field(env->mstatus, MSTATUS_TVM)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    helper_hyp_tlb_flush(env);
}

static int check_access_hlsv(CPURISCVState *env, bool x, uintptr_t ra)
{
    if (env->priv == PRV_M) {
        /* always allowed */
    } else if (env->virt_enabled) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, ra);
    } else if (env->priv == PRV_U && !get_field(env->hstatus, HSTATUS_HU)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    int mode = get_field(env->hstatus, HSTATUS_SPVP);
    if (!x && mode == PRV_S && get_field(env->vsstatus, MSTATUS_SUM)) {
        mode = MMUIdx_S_SUM;
    }
    return mode | MMU_2STAGE_BIT;
}

target_ulong helper_hyp_hlv_bu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);

    return cpu_ldb_mmu(env, addr, oi, ra);
}

target_ulong helper_hyp_hlv_hu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_TEUW, mmu_idx);

    return cpu_ldw_mmu(env, addr, oi, ra);
}

target_ulong helper_hyp_hlv_wu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_TEUL, mmu_idx);

    return cpu_ldl_mmu(env, addr, oi, ra);
}

target_ulong helper_hyp_hlv_d(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_TEUQ, mmu_idx);

    return cpu_ldq_mmu(env, addr, oi, ra);
}

void helper_hyp_hsv_b(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);

    cpu_stb_mmu(env, addr, val, oi, ra);
}

void helper_hyp_hsv_h(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_TEUW, mmu_idx);

    cpu_stw_mmu(env, addr, val, oi, ra);
}

void helper_hyp_hsv_w(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_TEUL, mmu_idx);

    cpu_stl_mmu(env, addr, val, oi, ra);
}

void helper_hyp_hsv_d(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_TEUQ, mmu_idx);

    cpu_stq_mmu(env, addr, val, oi, ra);
}

/*
 * TODO: These implementations are not quite correct.  They perform the
 * access using execute permission just fine, but the final PMP check
 * is supposed to have read permission as well.  Without replicating
 * a fair fraction of cputlb.c, fixing this requires adding new mmu_idx
 * which would imply that exact check in tlb_fill.
 */
target_ulong helper_hyp_hlvx_hu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, true, ra);
    MemOpIdx oi = make_memop_idx(MO_TEUW, mmu_idx);

    return cpu_ldw_code_mmu(env, addr, oi, GETPC());
}

target_ulong helper_hyp_hlvx_wu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, true, ra);
    MemOpIdx oi = make_memop_idx(MO_TEUL, mmu_idx);

    return cpu_ldl_code_mmu(env, addr, oi, ra);
}

void helper_ipush(CPURISCVState *env)
{
    target_ulong base = env->gpr[2];
    int i = 4;
    /* TODO: probe the memory */
    for (; i <= 72; i += 4) {
        switch (i) {
        case 4:
            cpu_stl_data(env, base - i, env->mcause);
            break;
        case 8:
            cpu_stl_data(env, base - i, env->mepc);
            break;
        case 12: /* X1 */
            cpu_stl_data(env, base - i, env->gpr[1]);
            break;
        case 16: /* X5-X7 */
        case 20:
        case 24:
            cpu_stl_data(env, base - i, env->gpr[i / 4 + 1]);
            break;
        case 28: /* X10-X17 */
        case 32:
        case 36:
        case 40:
        case 44:
        case 48:
        case 52:
        case 56:
            cpu_stl_data(env, base - i, env->gpr[i / 4 + 3]);
            break;
        case 60: /* X28-X31 */
        case 64:
        case 68:
        case 72:
            cpu_stl_data(env, base - i, env->gpr[i / 4 + 13]);
            break;
        }
    }
    env->gpr[2] -= 72;
    env->mstatus = set_field(env->mstatus, MSTATUS_MIE, 1);
}

target_ulong helper_ipop(CPURISCVState *env, target_ulong curr_pc)
{
    target_ulong base = env->gpr[2];
    int i = 68;
    /* TODO: probe the memory */
    for (; i >= 0; i -= 4) {
        switch (i) {
        case 0: /* X31-X28 */
        case 4:
        case 8:
        case 12:
            env->gpr[31 - i / 4] = cpu_ldl_data(env, base + i);
            break;
        case 16: /* X17-X10 */
        case 20:
        case 24:
        case 28:
        case 32:
        case 36:
        case 40:
        case 44:
            env->gpr[21 - i / 4] = cpu_ldl_data(env, base + i);
            break;
        case 48: /* X7-X5 */
        case 52:
        case 56:
            env->gpr[19 - i / 4] = cpu_ldl_data(env, base + i);
            break;
        case 60: /* X1 */
            env->gpr[1] = cpu_ldl_data(env, base + i);
            break;
        case 64:
            env->mepc = cpu_ldl_data(env, base + i);
            break;
        case 68:
            env->mcause = cpu_ldl_data(env, base + i);
            break;
        }
    }
    env->gpr[2] += 72;
    return do_excp_return(env, GETPC(), curr_pc);
}

#endif /* !CONFIG_USER_ONLY */
