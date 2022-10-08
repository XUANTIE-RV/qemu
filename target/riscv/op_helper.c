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
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"

#if !defined(CONFIG_USER_ONLY)
#include "hw/intc/riscv_clic.h"
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
    int trace_index = (env->trace_index -1) % TB_TRACE_NUM;
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
void QEMU_NORETURN riscv_raise_exception(CPURISCVState *env,
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
    target_ulong val = 0;
    RISCVException ret = riscv_csrrw(env, csr, &val, 0, 0);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return val;
}

void helper_csrw(CPURISCVState *env, int csr, target_ulong src)
{
    target_ulong mask = cpu_get_xl(env) == MXL_RV32 ? UINT32_MAX :
                                                      (target_ulong)-1;
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

/* helper_zicbo_envcfg
 *
 * Raise virtual exceptions and illegal instruction exceptions for
 * Zicbo[mz] instructions based on the settings of [mhs]envcfg as
 * specified in section 2.5.1 of the CMO specification.
 */
static void helper_zicbo_envcfg(CPURISCVState *env, target_ulong envbits,
                                uintptr_t ra)
{
#ifndef CONFIG_USER_ONLY
    /* Check for virtual instruction exceptions first, as we don't see
     * VU and VS reflected in env->priv (these are just the translated
     * U and S stated with virtualisation enabled.
     */
    if (riscv_cpu_virt_enabled(env) &&
        (((env->priv < PRV_H) && !get_field(env->henvcfg, envbits)) ||
         ((env->priv < PRV_S) && !get_field(env->senvcfg, envbits)))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, ra);
    }

    if (((env->priv < PRV_M) && !get_field(env->menvcfg, envbits)) ||
        ((env->priv < PRV_S) && !get_field(env->senvcfg, envbits))) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
#endif
}

/* helper_zicbom_access
 *
 * Check access permissions (LOAD, STORE or FETCH as specified in section
 * 2.5.2 of the CMO specification) for Zicbom, raising either store
 * page-fault (non-virtualised) or store guest-page fault (virtualised).
 */
static void helper_zicbom_access(CPURISCVState *env, target_ulong address,
                                 uintptr_t ra)
{
    void* phost;
    int ret = TLB_INVALID_MASK;
    MMUAccessType access_type = MMU_DATA_LOAD;

    while (ret == TLB_INVALID_MASK && access_type <= MMU_INST_FETCH) {
        ret = probe_access_flags(env, address, access_type++,
                                 cpu_mmu_index(env, false),
                                 true, &phost, ra);
    }

    if (ret == TLB_INVALID_MASK) {
        uint32_t exc = RISCV_EXCP_STORE_PAGE_FAULT;

#ifndef CONFIG_USER_ONLY
        /* User-mode emulation does not have virtualisation. */
        if (riscv_cpu_virt_enabled(env)) {
            exc = RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT;
        }
#endif
        riscv_raise_exception(env, exc, ra);
    }
}

void helper_cbo_clean_flush(CPURISCVState *env, target_ulong address)
{
    uintptr_t ra = GETPC();
    helper_zicbo_envcfg(env, MENVCFG_CBCFE, ra);
    helper_zicbom_access(env, address, ra);
}

void helper_cbo_inval(CPURISCVState *env, target_ulong address)
{
    uintptr_t ra = GETPC();
    helper_zicbo_envcfg(env, MENVCFG_CBIE, ra);
    helper_zicbom_access(env, address, ra);
}

void helper_cbo_zero(CPURISCVState *env, target_ulong address)
{
    uintptr_t ra = GETPC();
    helper_zicbo_envcfg(env, MENVCFG_CBZE, ra);

    /* Get the size of the cache block for zero instructions. */
    RISCVCPU *cpu = env_archcpu(env);
    uint16_t cbozlen = cpu->cfg.cbozlen;

    /* Mask off low-bits to align-down to the cache-block. */
    address &= ~(cbozlen - 1);

    void* mem = probe_access(env, address, cbozlen, MMU_DATA_STORE,
                             cpu_mmu_index(env, false), GETPC());

    /* Zero the block */
    memset(mem, 0, cbozlen);
}

#ifndef CONFIG_USER_ONLY

void helper_switch_context_xl(CPURISCVState *env)
{
    RISCVMXL xl = cpu_get_xl(env);
    if (xl < env->misa_mxl_max) {
        switch (xl) {
        case MXL_RV32:
            for (int i = 1; i < 32; i++) {
                env->gpr[i] = (int32_t)env->gpr[i];
            }
            env->pc = (int32_t)env->pc;
            /*
             * For the read-only bits of the previous-width CSR, the bits at the
             * same positions in the temporary register are set to zeros.
             */
            if ((env->priv == PRV_U) && (env->misa_ext & RVV)) {
                env->vl = 0;
                env->vtype = 0;
            }
            break;
        default:
            break;
        }
    }
}

target_ulong helper_sret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    uint64_t mstatus;
    target_ulong prev_priv, prev_virt;

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

    if (riscv_has_ext(env, RVH) && riscv_cpu_virt_enabled(env) &&
        get_field(env->hstatus, HSTATUS_VTSR)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    mstatus = env->mstatus;

    if (riscv_has_ext(env, RVH) && !riscv_cpu_virt_enabled(env)) {
        /* We support Hypervisor extensions and virtulisation is disabled */
        target_ulong hstatus = env->hstatus;

        prev_priv = get_field(mstatus, MSTATUS_SPP);
        prev_virt = get_field(hstatus, HSTATUS_SPV);

        hstatus = set_field(hstatus, HSTATUS_SPV, 0);
        mstatus = set_field(mstatus, MSTATUS_SPP, 0);
        mstatus = set_field(mstatus, SSTATUS_SIE,
                            get_field(mstatus, SSTATUS_SPIE));
        mstatus = set_field(mstatus, SSTATUS_SPIE, 1);

        env->mstatus = mstatus;
        env->hstatus = hstatus;

        if (prev_virt) {
            riscv_cpu_swap_hypervisor_regs(env);
        }

        riscv_cpu_set_virt_enabled(env, prev_virt);
    } else {
        prev_priv = get_field(mstatus, MSTATUS_SPP);

        mstatus = set_field(mstatus, MSTATUS_SIE,
                            get_field(mstatus, MSTATUS_SPIE));
        mstatus = set_field(mstatus, MSTATUS_SPIE, 1);
        mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);
        env->mstatus = mstatus;

        if (riscv_clic_is_clic_mode(env)) {
            CPUState *cs = env_cpu(env);
            target_ulong spil = get_field(env->scause, SCAUSE_SPIL);
            env->mintstatus = set_field(env->mintstatus, MINTSTATUS_SIL, spil);
            env->scause = set_field(env->scause, SCAUSE_SPIE, 0);
            env->scause = set_field(env->scause, SCAUSE_SPP, PRV_U);
            qemu_mutex_lock_iothread();
            riscv_clic_get_next_interrupt(env->clic, cs->cpu_index);
            qemu_mutex_unlock_iothread();
        }
    }

    riscv_cpu_set_mode(env, prev_priv);
    return retpc;
}

static target_ulong do_excp_return(CPURISCVState *env, target_ulong cpu_pc_deb,
                                   target_ulong ra)
{
    if (!(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    target_ulong retpc = env->mepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS, ra);
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

    if (!pmp_get_num_rules(env) && (prev_priv != PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    target_ulong prev_virt = get_field(env->mstatus, MSTATUS_MPV);
    mstatus = set_field(mstatus, MSTATUS_MIE,
                        get_field(mstatus, MSTATUS_MPIE));
    mstatus = set_field(mstatus, MSTATUS_MPIE, 1);
    mstatus = set_field(mstatus, MSTATUS_MPP, PRV_U);
    mstatus = set_field(mstatus, MSTATUS_MPV, 0);
    env->mstatus = mstatus;
    riscv_cpu_set_mode(env, prev_priv);

    if (riscv_has_ext(env, RVH)) {
        if (prev_virt) {
            riscv_cpu_swap_hypervisor_regs(env);
        }

        riscv_cpu_set_virt_enabled(env, prev_virt);
    }

    if (riscv_clic_is_clic_mode(env)) {
        CPUState *cs = env_cpu(env);
        target_ulong mpil = get_field(env->mcause, MCAUSE_MPIL);
        env->mintstatus = set_field(env->mintstatus, MINTSTATUS_MIL, mpil);
        env->mcause = set_field(env->mcause, MCAUSE_MPIE, 0);
        env->mcause = set_field(env->mcause, MCAUSE_MPP, PRV_U);
        qemu_mutex_lock_iothread();
        riscv_clic_get_next_interrupt(env->clic, cs->cpu_index);
        qemu_mutex_unlock_iothread();
    }
    env->excp_vld = 0;
    return retpc;
}

target_ulong helper_mret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    return do_excp_return(env, cpu_pc_deb, GETPC());
}

void helper_wfi(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    bool rvs = riscv_has_ext(env, RVS);
    bool prv_u = env->priv == PRV_U;
    bool prv_s = env->priv == PRV_S;

    if (((prv_s || (!rvs && prv_u)) && get_field(env->mstatus, MSTATUS_TW)) ||
        (rvs && prv_u && !riscv_cpu_virt_enabled(env))) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (riscv_cpu_virt_enabled(env) && (prv_u ||
        (prv_s && get_field(env->hstatus, HSTATUS_VTW)))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
        cpu_loop_exit(cs);
    }
}

void helper_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    if (!(env->priv >= PRV_S) ||
        (env->priv == PRV_S &&
         get_field(env->mstatus, MSTATUS_TVM))) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (riscv_has_ext(env, RVH) && riscv_cpu_virt_enabled(env) &&
               get_field(env->hstatus, HSTATUS_VTVM)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        tlb_flush(cs);
    }
}

void helper_hyp_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);

    if (env->priv == PRV_S && riscv_cpu_virt_enabled(env)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    if (env->priv == PRV_M ||
        (env->priv == PRV_S && !riscv_cpu_virt_enabled(env))) {
        tlb_flush(cs);
        return;
    }

    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
}

void helper_hyp_gvma_tlb_flush(CPURISCVState *env)
{
    if (env->priv == PRV_S && !riscv_cpu_virt_enabled(env) &&
        get_field(env->mstatus, MSTATUS_TVM)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    helper_hyp_tlb_flush(env);
}

target_ulong helper_hyp_hlvx_hu(CPURISCVState *env, target_ulong address)
{
    int mmu_idx = cpu_mmu_index(env, true) | TB_FLAGS_PRIV_HYP_ACCESS_MASK;

    return cpu_lduw_mmuidx_ra(env, address, mmu_idx, GETPC());
}

target_ulong helper_hyp_hlvx_wu(CPURISCVState *env, target_ulong address)
{
    int mmu_idx = cpu_mmu_index(env, true) | TB_FLAGS_PRIV_HYP_ACCESS_MASK;

    return cpu_ldl_mmuidx_ra(env, address, mmu_idx, GETPC());
}

#endif /* !CONFIG_USER_ONLY */

target_ulong helper_mulah(target_ulong src1, target_ulong src2,
                          target_ulong dst)
{
    int16_t a, b;
    int32_t c, d;
    a = (int16_t)(src1);
    b = (int16_t)(src2);
    c = (int32_t)dst;
    d = (int32_t)a * (int32_t)b;
    d = c + d;
    return (target_long)d;
}

target_ulong helper_mulsh(target_ulong src1, target_ulong src2,
                          target_ulong dst)
{
    int16_t a, b;
    int32_t c, d;
    a = (int16_t)(src1);
    b = (int16_t)(src2);
    c = (int32_t)dst;
    d = (int32_t)a * (int32_t)b;
    d = c - d;
    return (target_long)d;
}

target_ulong helper_tstnbz(target_ulong src, target_ulong ol)
{
    int i;
    target_ulong res = 0;
    int cnt = ol == MXL_RV32 ? 4 : sizeof(target_ulong);
    for (i = 0; i < cnt; i++) {
        if ((src & 0xff) == 0) {
            res |= ((target_ulong)0xff << (i * 8));
        }
        src = src >> 8;
    }
    return res;
}

target_ulong helper_ff0(target_ulong src, target_ulong ol)
{
    int i = 0;
    int bits = ol == MXL_RV32 ? 32 : sizeof(target_ulong) * 8;

    for (i = 0; i < bits; i++) {
        if ((src & ((target_ulong)1 << (bits - 1 - i))) == 0) {
            break;
        }
    }
    return i;
}

target_ulong helper_ff1(target_ulong src, target_ulong ol)
{
    int i = 0;
    int bits =  ol == MXL_RV32 ? 32 : sizeof(target_ulong) * 8;

    for (i = 0; i < bits; i++) {
        if ((src & ((target_ulong)1 << (bits - 1 - i))) != 0) {
            break;
        }
    }
    return i;
}

target_ulong helper_srri(target_ulong src, target_ulong imm, target_ulong ol)
{
    target_ulong i = 0;
    int bits =  ol == MXL_RV32 ? 32 : sizeof(target_ulong) * 8;

    i = (src >> imm) | (src << (bits - imm));
    return i;
}

target_ulong helper_tst(target_ulong src, target_ulong imm)
{
    target_ulong i = 0;

    if (src & (1ULL << imm)) {
        i = 1;
    }

    return i;
}

target_ulong helper_srriw(target_ulong src, target_ulong imm)
{
    target_ulong i = 0;
    uint32_t tmp = src & 0xffffffff;
    tmp = ((tmp >> imm) | (tmp << (32 - imm)));
    if (tmp & 0x80000000) {
        i = 0xffffffff00000000 | tmp;
    } else {
        i = tmp;
    }
    return i;
}

#ifndef CONFIG_USER_ONLY
void helper_ipush(CPURISCVState *env)
{
    target_ulong base = env->gpr[2];
    int i = 4;
    /* TODO: probe the memory */
    for(; i <= 72; i += 4) {
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

target_ulong helper_ipop(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    target_ulong base = env->gpr[2];
    int i = 68;
    /* TODO: probe the memory */
    for(; i >= 0; i -= 4) {
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
    return do_excp_return(env, cpu_pc_deb, GETPC());
}
#endif