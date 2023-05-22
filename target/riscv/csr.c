/*
 * RISC-V Control and Status Registers.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
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
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "internals.h"

#if !defined(CONFIG_USER_ONLY)
#include "hw/intc/riscv_clic.h"
#include "hw/intc/sifive_clint.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#endif

/* CSR function table public API */
void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops)
{
    *ops = csr_ops[csrno & (CSR_TABLE_SIZE - 1)];
}

void riscv_set_csr_ops(int csrno, riscv_csr_operations *ops)
{
    csr_ops[csrno & (CSR_TABLE_SIZE - 1)] = *ops;
}

/* Predicates */
static RISCVException fs(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    /* loose check condition for fcsr in vector extension */
    if ((csrno == CSR_FCSR) && (env->misa_ext & RVV) && (env->vext_ver == VEXT_VERSION_0_07_1)) {
        return RISCV_EXCP_NONE;
    }
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException vs(CPURISCVState *env, int csrno)
{
    /* Relax for RVP */
    if ((csrno == CSR_VXSAT) && (env->misa_ext & RVP)) {
        return RISCV_EXCP_NONE;
    }
    if (env->misa_ext & RVV) {
        /* Relax for 0.7.1 */
        if (env->vext_ver == VEXT_VERSION_0_07_1) {
            return RISCV_EXCP_NONE;
        }
#if !defined(CONFIG_USER_ONLY)
        if (!env->debugger && !riscv_cpu_vector_enabled(env)) {
            return RISCV_EXCP_ILLEGAL_INST;
        }
#endif
        return RISCV_EXCP_NONE;
    }
    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException ms(CPURISCVState *env, int csrno)
{
    CPUState *cs = env_cpu(env);
    RISCVCPU *cpu = RISCV_CPU(cs);

    if (cpu->cfg.ext_matrix) {
#if !defined(CONFIG_USER_ONLY)
        if (!env->debugger && !riscv_cpu_matrix_enabled(env)) {
            return RISCV_EXCP_ILLEGAL_INST;
        }
#endif
        return RISCV_EXCP_NONE;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException ctr(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    CPUState *cs = env_cpu(env);
    RISCVCPU *cpu = RISCV_CPU(cs);

    if (!cpu->cfg.ext_counters) {
        /* The Counters extensions is not enabled */
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (riscv_cpu_virt_enabled(env)) {
        switch (csrno) {
        case CSR_CYCLE:
            if (!get_field(env->hcounteren, HCOUNTEREN_CY) &&
                get_field(env->mcounteren, HCOUNTEREN_CY)) {
                return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_TIME:
            if (!get_field(env->hcounteren, HCOUNTEREN_TM) &&
                get_field(env->mcounteren, HCOUNTEREN_TM)) {
                return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_INSTRET:
            if (!get_field(env->hcounteren, HCOUNTEREN_IR) &&
                get_field(env->mcounteren, HCOUNTEREN_IR)) {
                return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_HPMCOUNTER3...CSR_HPMCOUNTER31:
            if (!get_field(env->hcounteren, 1 << (csrno - CSR_HPMCOUNTER3)) &&
                get_field(env->mcounteren, 1 << (csrno - CSR_HPMCOUNTER3))) {
                return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        }
        if (riscv_cpu_mxl(env) == MXL_RV32) {
            switch (csrno) {
            case CSR_CYCLEH:
                if (!get_field(env->hcounteren, HCOUNTEREN_CY) &&
                    get_field(env->mcounteren, HCOUNTEREN_CY)) {
                    return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_TIMEH:
                if (!get_field(env->hcounteren, HCOUNTEREN_TM) &&
                    get_field(env->mcounteren, HCOUNTEREN_TM)) {
                    return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_INSTRETH:
                if (!get_field(env->hcounteren, HCOUNTEREN_IR) &&
                    get_field(env->mcounteren, HCOUNTEREN_IR)) {
                    return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_HPMCOUNTER3H...CSR_HPMCOUNTER31H:
                if (!get_field(env->hcounteren, 1 << (csrno - CSR_HPMCOUNTER3H)) &&
                    get_field(env->mcounteren, 1 << (csrno - CSR_HPMCOUNTER3H))) {
                    return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            }
        }
    }
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException ctr32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return ctr(env, csrno);
}

#if !defined(CONFIG_USER_ONLY)
static RISCVException any(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

static RISCVException any32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return any(env, csrno);

}

static RISCVException smode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException hmode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS) &&
        riscv_has_ext(env, RVH)) {
        /* Hypervisor extension is supported */
        if ((env->priv == PRV_S && !riscv_cpu_virt_enabled(env)) ||
            env->priv == PRV_M) {
            return RISCV_EXCP_NONE;
        } else {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException hmode32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        if (riscv_cpu_virt_enabled(env)) {
            return RISCV_EXCP_ILLEGAL_INST;
        } else {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    return hmode(env, csrno);

}

static RISCVException pmp(CPURISCVState *env, int csrno)
{
    if (riscv_feature(env, RISCV_FEATURE_PMP)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException clic(CPURISCVState *env, int csrno)
{
    return env->clic ? RISCV_EXCP_NONE : RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException epmp(CPURISCVState *env, int csrno)
{
    if (env->priv == PRV_M && riscv_feature(env, RISCV_FEATURE_EPMP)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

#endif

/* User Floating-Point CSRs */
static RISCVException read_fflags(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = riscv_cpu_get_fflags(env);
    return RISCV_EXCP_NONE;
}

static RISCVException write_fflags(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    env->mstatus |= MSTATUS_FS;
#endif
    riscv_cpu_set_fflags(env, val & (FSR_AEXC >> FSR_AEXC_SHIFT));
    return RISCV_EXCP_NONE;
}

static RISCVException read_frm(CPURISCVState *env, int csrno,
                               target_ulong *val)
{
    *val = env->frm;
    return RISCV_EXCP_NONE;
}

static RISCVException write_frm(CPURISCVState *env, int csrno,
                                target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = val & (FSR_RD >> FSR_RD_SHIFT);
    return RISCV_EXCP_NONE;
}

static RISCVException read_fcsr(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = (riscv_cpu_get_fflags(env) << FSR_AEXC_SHIFT)
        | (env->frm << FSR_RD_SHIFT);
    if (env->vext_ver == VEXT_VERSION_0_07_1) {
        *val |= env->vxrm << FSR_VXRM_SHIFT;
        *val |= env->vxsat << FSR_VXSAT_SHIFT;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_fcsr(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = (val & FSR_RD) >> FSR_RD_SHIFT;
    if (env->vext_ver == VEXT_VERSION_0_07_1) {
        env->vxrm = (val & FSR_VXRM) >> FSR_VXRM_SHIFT;
        env->vxsat = (val & FSR_VXSAT) >> FSR_VXSAT_SHIFT;
    }
    riscv_cpu_set_fflags(env, (val & FSR_AEXC) >> FSR_AEXC_SHIFT);
    return RISCV_EXCP_NONE;
}

static RISCVException read_fxcr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = (env->bf16 << FXCR_BF16_SHIFT) |
           (riscv_cpu_get_fflags(env) << FSR_AEXC_SHIFT) |
           (env->frm << FXCR_RD_SHIFT);
    return RISCV_EXCP_NONE;
}

static int write_fxcr(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    env->mstatus |= MSTATUS_FS;
#endif
    env->bf16 = (val & FXCR_BF16) >> FXCR_BF16_SHIFT;
    env->frm = (val & FXCR_RD) >> FXCR_RD_SHIFT;
    riscv_cpu_set_fflags(env, (val & FSR_AEXC) >> FSR_AEXC_SHIFT);
    return RISCV_EXCP_NONE;
}

static RISCVException read_vtype(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->vtype;
    return RISCV_EXCP_NONE;
}

static int read_vlenb(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env_archcpu(env)->cfg.vlen >> 3;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vl(CPURISCVState *env, int csrno,
                              target_ulong *val)
{
    *val = env->vl;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vxrm(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = env->vxrm;
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmrstart(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->mrstart;
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmrstart(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    qemu_log_mask(LOG_UNIMP, "No support for writing to xmrstart.");
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmcsr(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->mcsr;
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmcsr(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->mcsr = val & (MCSR_RM | MCSR_SAT);
    env->mxrm = get_field(val, MCSR_RM);
    env->mxsat = get_field(val, MCSR_SAT);
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmsize(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = (env->sizek << 16) | (env->sizen << 8) | (env->sizem);
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmsize(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->sizek = get_field(val, 0xffff0000);
    env->sizen = get_field(val, 0xff00);
    env->sizem = get_field(val, 0xff);
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmlenb(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = get_mlenb(env);
    return RISCV_EXCP_NONE;
}

static RISCVException read_xrlenb(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = get_rlenb(env);
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmisa(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->xmisa;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vxrm(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    if (env->vext_ver == VEXT_VERSION_1_00_0) {
#if !defined(CONFIG_USER_ONLY)
        env->mstatus |= MSTATUS_VS;
#endif
    }
    env->vxrm = val & (FSR_VXRM >> FSR_VXRM_SHIFT);
    return RISCV_EXCP_NONE;
}

static RISCVException read_vxsat(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->vxsat;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vxsat(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    if (env->vext_ver == VEXT_VERSION_1_00_0) {
#if !defined(CONFIG_USER_ONLY)
        env->mstatus |= MSTATUS_VS;
#endif
    }
    env->vxsat = val & (FSR_VXSAT >> FSR_VXSAT_SHIFT);
    return RISCV_EXCP_NONE;
}

static RISCVException read_vstart(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->vstart;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vstart(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_vector_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_VS;
#endif

    /*
     * The vstart CSR is defined to have only enough writable bits
     * to hold the largest element index, i.e. lg2(VLEN) bits.
     */
    env->vstart = val & ~(~0ULL << ctzl(env_archcpu(env)->cfg.vlen));
    return RISCV_EXCP_NONE;
}

static int read_vcsr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = (env->vxrm << VCSR_VXRM_SHIFT) | (env->vxsat << VCSR_VXSAT_SHIFT);
    return RISCV_EXCP_NONE;
}

static int write_vcsr(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    env->mstatus |= MSTATUS_VS;
#endif

    env->vxrm = (val & VCSR_VXRM) >> VCSR_VXRM_SHIFT;
    env->vxsat = (val & VCSR_VXSAT) >> VCSR_VXSAT_SHIFT;
    return RISCV_EXCP_NONE;
}

/* User Timers and Counters */
static RISCVException read_instret(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (icount_enabled()) {
        *val = icount_get();
    } else {
        *val = cpu_get_host_ticks();
    }
#else
    *val = cpu_get_host_ticks();
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException read_instreth(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (icount_enabled()) {
        *val = icount_get() >> 32;
    } else {
        *val = cpu_get_host_ticks() >> 32;
    }
#else
    *val = cpu_get_host_ticks() >> 32;
#endif
    return RISCV_EXCP_NONE;
}

#if defined(CONFIG_USER_ONLY)
static RISCVException read_time(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = cpu_get_host_ticks();
    return RISCV_EXCP_NONE;
}

static RISCVException read_timeh(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = cpu_get_host_ticks() >> 32;
    return RISCV_EXCP_NONE;
}

#else /* CONFIG_USER_ONLY */

static RISCVException read_time(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->rdtime_fn(env->rdtime_fn_arg) + delta;
    return RISCV_EXCP_NONE;
}

static RISCVException read_timeh(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = (env->rdtime_fn(env->rdtime_fn_arg) + delta) >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException sstc(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    target_ulong envcfg = env->menvcfg;
    long unsigned int stce_mask = MENVCFG_STCE;
    CPUState *cs = env_cpu(env);
    RISCVCPU *cpu = RISCV_CPU(cs);

    if (!cpu->cfg.ext_sstc) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (env->priv == PRV_M) {
        return RISCV_EXCP_NONE;
    }

    if (csrno == CSR_STIMECMPH) {
        envcfg = env->menvcfg >> 32;
        stce_mask = MENVCFGH_STCE;
    }

    if (env->priv != PRV_S)
        return RISCV_EXCP_ILLEGAL_INST;

    if (!get_field(env->mcounteren, COUNTEREN_TM) ||
        !get_field(envcfg, stce_mask)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif
        return RISCV_EXCP_NONE;
}

static RISCVException read_stimecmp(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    if (!qemu_mutex_iothread_locked()) {
        qemu_mutex_lock_iothread();
        *val = env->stimecmp;
        qemu_mutex_unlock_iothread();
    } else {
        *val = env->stimecmp;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_stimecmph(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    if (!qemu_mutex_iothread_locked()) {
        qemu_mutex_lock_iothread();
        *val = env->stimecmp >> 32;
        qemu_mutex_unlock_iothread();
    } else {
        *val = env->stimecmp >> 32;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException write_stimecmp(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    RISCVCPU *cpu = env_archcpu(env);
    bool locked = false;
    uint64_t timer_val = val;
    if (!qemu_mutex_iothread_locked()) {
        locked = true;
        qemu_mutex_lock_iothread();
    }

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        timer_val = deposit64(env->stimecmp, 0, 32, val);
    }
    sifive_clint_write_stimecmp(cpu, timer_val,
                                SIFIVE_CLINT_TIMEBASE_FREQ);

    if (locked) {
        qemu_mutex_unlock_iothread();
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_stimecmph(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint64_t timer_val = 0;
    bool locked = false;
    if (!qemu_mutex_iothread_locked()) {
        locked = true;
        qemu_mutex_lock_iothread();
    }

    timer_val = (uint64_t)val << 32 | env->stimecmp;
    sifive_clint_write_stimecmp(cpu, timer_val,
                                SIFIVE_CLINT_TIMEBASE_FREQ);
    if (locked) {
        qemu_mutex_unlock_iothread();
    }
    return RISCV_EXCP_NONE;
}

/* Machine constants */

#define M_MODE_INTERRUPTS  (MIP_MSIP | MIP_MTIP | MIP_MEIP)
#define S_MODE_INTERRUPTS  (MIP_SSIP | MIP_STIP | MIP_SEIP)
#define VS_MODE_INTERRUPTS (MIP_VSSIP | MIP_VSTIP | MIP_VSEIP)

static const target_ulong delegable_ints = S_MODE_INTERRUPTS |
                                           VS_MODE_INTERRUPTS;
static const target_ulong vs_delegable_ints = VS_MODE_INTERRUPTS;
static const target_ulong all_ints = M_MODE_INTERRUPTS | S_MODE_INTERRUPTS |
                                     VS_MODE_INTERRUPTS;
#define DELEGABLE_EXCPS ((1ULL << (RISCV_EXCP_INST_ADDR_MIS)) | \
                         (1ULL << (RISCV_EXCP_INST_ACCESS_FAULT)) | \
                         (1ULL << (RISCV_EXCP_ILLEGAL_INST)) | \
                         (1ULL << (RISCV_EXCP_BREAKPOINT)) | \
                         (1ULL << (RISCV_EXCP_LOAD_ADDR_MIS)) | \
                         (1ULL << (RISCV_EXCP_LOAD_ACCESS_FAULT)) | \
                         (1ULL << (RISCV_EXCP_STORE_AMO_ADDR_MIS)) | \
                         (1ULL << (RISCV_EXCP_STORE_AMO_ACCESS_FAULT)) | \
                         (1ULL << (RISCV_EXCP_U_ECALL)) | \
                         (1ULL << (RISCV_EXCP_S_ECALL)) | \
                         (1ULL << (RISCV_EXCP_VS_ECALL)) | \
                         (1ULL << (RISCV_EXCP_M_ECALL)) | \
                         (1ULL << (RISCV_EXCP_INST_PAGE_FAULT)) | \
                         (1ULL << (RISCV_EXCP_LOAD_PAGE_FAULT)) | \
                         (1ULL << (RISCV_EXCP_STORE_PAGE_FAULT)) | \
                         (1ULL << (RISCV_EXCP_INST_GUEST_PAGE_FAULT)) | \
                         (1ULL << (RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT)) | \
                         (1ULL << (RISCV_EXCP_VIRT_INSTRUCTION_FAULT)) | \
                         (1ULL << (RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT)))
static const target_ulong vs_delegable_excps = DELEGABLE_EXCPS &
    ~((1ULL << (RISCV_EXCP_S_ECALL)) |
      (1ULL << (RISCV_EXCP_VS_ECALL)) |
      (1ULL << (RISCV_EXCP_M_ECALL)) |
      (1ULL << (RISCV_EXCP_INST_GUEST_PAGE_FAULT)) |
      (1ULL << (RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT)) |
      (1ULL << (RISCV_EXCP_VIRT_INSTRUCTION_FAULT)) |
      (1ULL << (RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT)));
static const target_ulong sstatus_v1_10_mask = SSTATUS_SIE | SSTATUS_SPIE |
    SSTATUS_UIE | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS |
    SSTATUS_SUM | SSTATUS_MXR;
static const target_ulong sip_writable_mask = SIP_SSIP | MIP_USIP | MIP_UEIP;
static const target_ulong hip_writable_mask = MIP_VSSIP;
static const target_ulong hvip_writable_mask = MIP_VSSIP | MIP_VSTIP | MIP_VSEIP;
static const target_ulong vsip_writable_mask = MIP_VSSIP;

static const char valid_vm_1_10_32[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV32] = 1
};

static const char valid_vm_1_10_64[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV39] = 1,
    [VM_1_10_SV48] = 1,
    [VM_1_10_SV57] = 1
};

/* Machine Information Registers */
static RISCVException read_zero(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mhartid(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->mhartid;
    return RISCV_EXCP_NONE;
}

/* Machine Trap Setup */

/* We do not store SD explicitly, only compute it on demand. */
static uint64_t add_status_sd(CPURISCVState *env, RISCVMXL xl, uint64_t status)
{
    bool vs_dirty = false;
    if (env->vext_ver == VEXT_VERSION_1_00_0) {
        if ((status & MSTATUS_VS) == MSTATUS_VS) {
            vs_dirty = true;
        }
    }
    if (vs_dirty || (status & MSTATUS_FS) == MSTATUS_FS ||
        (status & MSTATUS_XS) == MSTATUS_XS) {
        switch (xl) {
        case MXL_RV32:
            return status | MSTATUS32_SD;
        case MXL_RV64:
            return status | MSTATUS64_SD;
        default:
            g_assert_not_reached();
        }
    }
    return status;
}

static RISCVException read_mstatus(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = add_status_sd(env, riscv_cpu_mxl(env), env->mstatus);
    return RISCV_EXCP_NONE;
}

static int validate_vm(CPURISCVState *env, target_ulong vm)
{
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        return valid_vm_1_10_32[vm & 0xf];
    } else {
        return valid_vm_1_10_64[vm & 0xf];
    }
}

static RISCVException write_mstatus(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    uint64_t mstatus = env->mstatus;
    uint64_t mask = 0;

    /* flush tlb on mstatus fields that affect VM */
    if ((val ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP | MSTATUS_MPV |
            MSTATUS_MPRV | MSTATUS_SUM)) {
        tlb_flush(env_cpu(env));
    }
    mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
        MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
        MSTATUS_MPP | MSTATUS_MXR | MSTATUS_TVM | MSTATUS_TSR |
        MSTATUS_TW;
    if (env->vext_ver == VEXT_VERSION_1_00_0) {
        mask |= MSTATUS_VS;
    }
    if (riscv_cpu_mxl(env) != MXL_RV32 || env->debugger) {
        /*
         * RV32: MPV and GVA are not in mstatus. The current plan is to
         * add them to mstatush. For now, we just don't support it.
         */
        mask |= MSTATUS_MPV | MSTATUS_GVA;
        if ((val & MSTATUS64_UXL) != 0) {
            mask |= MSTATUS64_UXL;
        }
    }

    mstatus = (mstatus & ~mask) | (val & mask);

    if (riscv_cpu_mxl(env) == MXL_RV64) {
        /* SXL field is for now read only */
        mstatus = set_field(mstatus, MSTATUS64_SXL, MXL_RV64);
    }
    env->mstatus = mstatus;

    return RISCV_EXCP_NONE;
}

static RISCVException read_mstatush(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->mstatus >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mstatush(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    uint64_t valh = (uint64_t)val << 32;
    uint64_t mask = MSTATUS_MPV | MSTATUS_GVA;

    if ((valh ^ env->mstatus) & (MSTATUS_MPV)) {
        tlb_flush(env_cpu(env));
    }

    env->mstatus = (env->mstatus & ~mask) | (valh & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException read_misa(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    target_ulong misa;

    switch (env->misa_mxl) {
    case MXL_RV32:
        misa = (target_ulong)MXL_RV32 << 30;
        break;
#ifdef TARGET_RISCV64
    case MXL_RV64:
        misa = (target_ulong)MXL_RV64 << 62;
        break;
#endif
    default:
        g_assert_not_reached();
    }

    *val = misa | env->misa_ext;
    return RISCV_EXCP_NONE;
}

static RISCVException write_misa(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MISA)) {
        /* drop write to misa */
        return RISCV_EXCP_NONE;
    }

    /* 'I' or 'E' must be present */
    if (!(val & (RVI | RVE))) {
        /* It is not, drop write to misa */
        return RISCV_EXCP_NONE;
    }

    /* 'E' excludes all other extensions */
    if (val & RVE) {
        /* when we support 'E' we can do "val = RVE;" however
         * for now we just drop writes if 'E' is present.
         */
        return RISCV_EXCP_NONE;
    }

    /*
     * misa.MXL writes are not supported by QEMU.
     * Drop writes to those bits.
     */

    /* Mask extensions that are not supported by this hart */
    val &= env->misa_ext_mask;

    /* Mask extensions that are not supported by QEMU */
    val &= (RVI | RVE | RVM | RVA | RVF | RVD | RVC | RVS | RVU | RVV);

    /* 'D' depends on 'F', so clear 'D' if 'F' is not present */
    if ((val & RVD) && !(val & RVF)) {
        val &= ~RVD;
    }

    /* Suppress 'C' if next instruction is not aligned
     * TODO: this should check next_pc
     */
    if ((val & RVC) && (GETPC() & ~3) != 0) {
        val &= ~RVC;
    }

    /* If nothing changed, do nothing. */
    if (val == env->misa_ext) {
        return RISCV_EXCP_NONE;
    }

    /* flush translation cache */
    tb_flush(env_cpu(env));
    env->misa_ext = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_medeleg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->medeleg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_medeleg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->medeleg = (env->medeleg & ~DELEGABLE_EXCPS) | (val & DELEGABLE_EXCPS);
    return RISCV_EXCP_NONE;
}

static RISCVException read_mideleg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->mideleg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mideleg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->mideleg = (env->mideleg & ~delegable_ints) | (val & delegable_ints);
    if (riscv_has_ext(env, RVH)) {
        env->mideleg |= VS_MODE_INTERRUPTS;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_mie(CPURISCVState *env, int csrno,
                               target_ulong *val)
{
    /* The xie CSR appears hardwired to zero in CLIC mode, (Section 4.3) */
    *val = riscv_clic_is_clic_mode(env) ? 0 : env->mie;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mie(CPURISCVState *env, int csrno,
                                target_ulong val)
{
    /* Writes to xie will be ignored and will not trap. (Section 4.3) */
    if (!riscv_clic_is_clic_mode(env)) {
        env->mie = (env->mie & ~all_ints) | (val & all_ints);
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_mtvec(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->mtvec;
    return RISCV_EXCP_NONE;
}

/*
 * bits [1:0] encode mode; 0 = direct, 1 = vectored, 3 = CLIC,
 * others reserved
 */
static RISCVException write_mtvec(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    /*
     * If only basic mode is supported, writes to bit 1 are ignored and
     * it is always set to zero (current behavior).
     * If only CLIC mode is supported, writes to bit 1 are also ignored and
     * it is always set to one. CLIC mode hardwires xtvec bits 2-5 to zero
     * (assuming no further CLIC extensions are supported).
     */
    if (env->clic) {
        val = (val & ~0x3f) | 0b000011;
    } else {
        val = val & ~0x2;
    }
    env->mtvec = val;
    return RISCV_EXCP_NONE;
}

static int read_mtvt(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtvt;
    return RISCV_EXCP_NONE;
}

static int write_mtvt(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mtvt = val & ~((1ULL << 6) - 1);
    return RISCV_EXCP_NONE;
}

static RISCVException read_mcounteren(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = env->mcounteren;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mcounteren(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    env->mcounteren = val;
    return RISCV_EXCP_NONE;
}

/* Machine Trap Handling */
static RISCVException read_mscratch(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->mscratch;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mscratch(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    env->mscratch = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mepc(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    *val = env->mepc;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mepc(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    env->mepc = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mcause(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    *val = env->mcause;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mcause(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    env->mcause = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mtval(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->mtval;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mtval(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->mtval = val;
    return RISCV_EXCP_NONE;
}

/* Execution environment configuration setup */
static RISCVException read_menvcfg(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->menvcfg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_menvcfg(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    uint64_t mask = MENVCFG_FIOM | MENVCFG_CBIE | MENVCFG_CBCFE | MENVCFG_CBZE;

    if (riscv_cpu_mxl(env) == MXL_RV64) {
        mask |= MENVCFG_PBMTE | MENVCFG_STCE;
    }
    env->menvcfg = (env->menvcfg & ~mask) | (val & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException read_menvcfgh(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->menvcfg >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException write_menvcfgh(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    uint64_t mask = MENVCFG_PBMTE | MENVCFG_STCE;
    uint64_t valh = (uint64_t)val << 32;

    env->menvcfg = (env->menvcfg & ~mask) | (valh & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException read_senvcfg(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->senvcfg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_senvcfg(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    uint64_t mask = SENVCFG_FIOM | SENVCFG_CBIE | SENVCFG_CBCFE | SENVCFG_CBZE;

    env->senvcfg = (env->senvcfg & ~mask) | (val & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException read_henvcfg(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->henvcfg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_henvcfg(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    uint64_t mask = HENVCFG_FIOM | HENVCFG_CBIE | HENVCFG_CBCFE | HENVCFG_CBZE;

    if (riscv_cpu_mxl(env) == MXL_RV64) {
        mask |= HENVCFG_PBMTE | HENVCFG_STCE;
    }

    env->henvcfg = (env->henvcfg & ~mask) | (val & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException read_henvcfgh(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->henvcfg >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException write_henvcfgh(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    uint64_t mask = HENVCFG_PBMTE | HENVCFG_STCE;
    uint64_t valh = (uint64_t)val << 32;

    env->henvcfg = (env->henvcfg & ~mask) | (valh & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mip(CPURISCVState *env, int csrno,
                              target_ulong *ret_value,
                              target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    target_ulong mask = write_mask & delegable_ints & ~env->miclaim;
    uint32_t old_mip;

     /* The xip CSR appears hardwired to zero in CLIC mode. (Section 4.3) */
    if (riscv_clic_is_clic_mode(env)) {
        *ret_value = 0;
        return RISCV_EXCP_NONE;
    }
    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip;
    }

    return RISCV_EXCP_NONE;
}

static int read_mintstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mintstatus;
    return RISCV_EXCP_NONE;
}

static int read_mintthresh(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mintthresh;
    return RISCV_EXCP_NONE;
}

static int write_mintthresh(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mintthresh = val;
    return RISCV_EXCP_NONE;
}

static int read_mxstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mxstatus | (env->priv << 30);
    return RISCV_EXCP_NONE;
}

static int write_mxstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mxstatus = val;
    return RISCV_EXCP_NONE;
}

static int read_mexstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mexstatus | (env->excp_vld << 5);
    return RISCV_EXCP_NONE;
}

static int write_mexstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mexstatus = val;
    return RISCV_EXCP_NONE;
}

static int read_mdtcmcr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mdtcmcr;
    return RISCV_EXCP_NONE;
}

static uint32_t get_tcm_size(CPURISCVState *env, target_ulong size_field)
{
    const char *typename = object_get_typename((Object *)env_archcpu(env));
    if (strstr(typename, "r910")) {
        switch (size_field) {
        case 0b100:
            return 16 * 1024;
        case 0b101:
            return 32 * 1024;
        case 0b110:
            return 64 * 1024;
        default:
            return 0;
        }
   } else if (strstr(typename, "e907")) {
        if (size_field < 3) {
            return 0;
        } else {
            return (1 << (size_field - 1)) * 1024;
        }
   }
   return 0;
}

static int write_mdtcmcr(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong base_mask = (riscv_cpu_mxl(env) == MXL_RV32) ?
                                 MDTCMCR_BASE_32 : MDTCMCR_BASE_64;
    target_ulong old_size = get_field(env->mdtcmcr, MDTCMCR_SIZE);
    target_ulong old_base = get_field(env->mdtcmcr, base_mask);
    target_ulong old_enable = get_field(env->mdtcmcr, MDTCMCR_EN);
    target_ulong new_size = get_field(val, MDTCMCR_SIZE);
    target_ulong new_base = get_field(val, base_mask);
    target_ulong new_enable = get_field(val, MDTCMCR_EN);
    target_ulong size = get_tcm_size(env, new_size);
    target_ulong base = val & base_mask;
    target_ulong mask = MDTCMCR_EN | MDTCMCR_ECC_EN | MDTCMCR_INTERLEAVE;

    bool legal_size = size != 0;
    bool legal_base = (base % size) == 0;

    if (legal_size) {
        mask |= MDTCMCR_SIZE;
    }
    if (legal_base) {
        mask |= base_mask;
    }
    env->mdtcmcr = val & mask;

    /* When configuration change, delete old tcm */
    if (env->dtcm) {
        if (new_enable != old_enable) {
            /* Reenable Old memory region */
            if (!qemu_mutex_iothread_locked()) {
                qemu_mutex_lock_iothread();
                memory_region_set_enabled(env->dtcm, new_enable);
                qemu_mutex_unlock_iothread();
            } else {
                memory_region_set_enabled(env->dtcm, new_enable);
            }
        }
        if  ((old_size != new_size) && legal_size) {
            /* Resize memory region */
            if (!qemu_mutex_iothread_locked()) {
                qemu_mutex_lock_iothread();
                memory_region_set_size(env->dtcm, size);
                qemu_mutex_unlock_iothread();
            } else {
                memory_region_set_size(env->dtcm, size);
            }

        }
        if ((old_base != new_base) && legal_base) {
            /* Rebase memory region */
            if (!qemu_mutex_iothread_locked()) {
                qemu_mutex_lock_iothread();
                memory_region_set_address(env->dtcm, base);
                qemu_mutex_unlock_iothread();
            } else {
                memory_region_set_address(env->dtcm, base);
            }
        }
    }
    /* Old tcm has been removed or never exist, and new configuration request */
    if ((env->dtcm == NULL) && new_enable) {
        if (!legal_size || !legal_base) { /* Stricter than specification check */
            return RISCV_EXCP_ILLEGAL_INST;
        }
        env->dtcm = g_new(MemoryRegion, 1);
        if (!qemu_mutex_iothread_locked()) {
            qemu_mutex_lock_iothread();
            memory_region_init_ram(env->dtcm, NULL, "riscv_dtcm",
                                   size, &error_fatal);
            /* Add new memory region */
            memory_region_add_subregion_overlap(get_system_memory(), base,
                                                env->dtcm, 1);
            qemu_mutex_unlock_iothread();
        }
    }

    return RISCV_EXCP_NONE;
}

static int read_mitcmcr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mitcmcr;
    return RISCV_EXCP_NONE;
}

static int write_mitcmcr(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong base_mask = (riscv_cpu_mxl(env) == MXL_RV32) ?
                                 MITCMCR_BASE_32 : MITCMCR_BASE_64;
    target_ulong old_size = get_field(env->mitcmcr, MITCMCR_SIZE);
    target_ulong old_base = get_field(env->mitcmcr, base_mask);
    target_ulong old_enable = get_field(env->mitcmcr, MITCMCR_EN);
    target_ulong new_size = get_field(val, MITCMCR_SIZE);
    target_ulong new_base = get_field(val, base_mask);
    target_ulong new_enable = get_field(val, MITCMCR_EN);
    target_ulong size = get_tcm_size(env, new_size);
    target_ulong base = val & base_mask;
    target_ulong mask = MITCMCR_EN | MITCMCR_ECC_EN | MITCMCR_INTERLEAVE;

    bool legal_size = size != 0;
    bool legal_base = (base % size) == 0;

    if (legal_size) {
        mask |= MITCMCR_SIZE;
    }
    if (legal_base) {
        mask |= base_mask;
    }
    env->mitcmcr = val & mask;

    /* When configuration change, delete old tcm */
    if (env->itcm) {
        if (new_enable != old_enable) {
            /* Reenable Old memory region */
            if (!qemu_mutex_iothread_locked()) {
                qemu_mutex_lock_iothread();
                memory_region_set_enabled(env->itcm, new_enable);
                qemu_mutex_unlock_iothread();
            } else {
                memory_region_set_enabled(env->itcm, new_enable);
            }
        }
        if  ((old_size != new_size) && legal_size) {
            /* Resize memory region */
            if (!qemu_mutex_iothread_locked()) {
                qemu_mutex_lock_iothread();
                memory_region_set_size(env->itcm, size);
                qemu_mutex_unlock_iothread();
            } else {
                memory_region_set_enabled(env->itcm, new_enable);
            }

        }
        if ((old_base != new_base) && legal_base) {
            /* Rebase memory region */
            if (!qemu_mutex_iothread_locked()) {
                qemu_mutex_lock_iothread();
                memory_region_set_address(env->itcm, base);
                qemu_mutex_unlock_iothread();
            } else {
                memory_region_set_address(env->itcm, base);
            }
        }
    }
    /* Old tcm has been removed or never exist, and new configuration request */
    if ((env->itcm == NULL) && new_enable) {
        if (!legal_size || !legal_base) { /* Stricter than specification check */
            return RISCV_EXCP_ILLEGAL_INST;
        }
        env->itcm = g_new(MemoryRegion, 1);
        if (!qemu_mutex_iothread_locked()) {
            qemu_mutex_lock_iothread();
            memory_region_init_ram(env->itcm, NULL, "riscv_itcm",
                                   size, &error_fatal);
            /* Add new memory region */
            memory_region_add_subregion_overlap(get_system_memory(), base,
                                                env->itcm, 1);
            qemu_mutex_unlock_iothread();
        }
    }

    return RISCV_EXCP_NONE;
}

static int read_sxstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sxstatus;
    return RISCV_EXCP_NONE;
}

static int write_sxstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sxstatus = val;
    return RISCV_EXCP_NONE;
}

static int read_mrvbr(CPURISCVState *env, int csrno, target_ulong *val)
{
    RISCVCPU *cpu = env_archcpu(env);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    *val = mcc->mrvbr;
    return RISCV_EXCP_ILLEGAL_INST;
}

static int write_mrvbr(CPURISCVState *env, int csrno, target_ulong val)
{
    RISCVCPU *cpu = env_archcpu(env);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);

    mcc->mrvbr = val;
    return RISCV_EXCP_NONE;
}

static int read_mrmr(CPURISCVState *env, int csrno, target_ulong *val)
{
    RISCVCPU *cpu ;
    RISCVCPUClass *mcc;

    cpu = env_archcpu(env);
    mcc = RISCV_CPU_GET_CLASS(cpu);
    *val = mcc->mrmr;
    return RISCV_EXCP_NONE;
}

static int write_mrmr(CPURISCVState *env, int csrno, target_ulong val)
{
    uint32_t index;
    uint32_t i = 0;
    CPUState *cs = CPU(env_archcpu(env));
    RISCVCPU *cpu = RISCV_CPU(cs);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);

    index = (mcc->mrmr ^ val) & 0xff;
    mcc->mrmr = val;
    if (index == 0) {
        return RISCV_EXCP_NONE;
    }
    for (i = 1; i < 8; i++) {
        index = index >> 1;
        if (index & 0x1) {
            CPURISCVState *last_env = env;
            while (cs->cpu_index != i) {
                last_env = last_env->next_cpu;
                cs = CPU(env_archcpu(last_env));
            }
            if (last_env->in_reset) {
                last_env->pc = 0x80000000 + ldl_phys(cs->as, mcc->mrvbr);
                last_env->in_reset = false;
            }
        }
    }
    cpu_exit(current_cpu);/* let the release core run asap*/
    return RISCV_EXCP_NONE;
}

static int read_smcir(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->smcir;
    return RISCV_EXCP_NONE;
}

static void riscv_tlbinv_idx(CPURISCVState *env)
{
    CPUState *cs = CPU(env_archcpu(env));
    tlb_flush(cs);
}

static void riscv_tlbinv_all(CPURISCVState *env)
{
    CPUState *cs = CPU(env_archcpu(env));
    tlb_flush(cs);
}

static void riscv_tlbinv(CPURISCVState *env)
{
    CPUState *cs = CPU(env_archcpu(env));
    tlb_flush(cs);
}

static void riscv_tlbwi(CPURISCVState *env)
{
    CPUState *cs = CPU(env_archcpu(env));
    tlb_flush(cs);
}

static void riscv_tlbwr(CPURISCVState *env)
{
    CPUState *cs = CPU(env_archcpu(env));
    tlb_flush(cs);
}

/* tlbp/tlbr operations do not support actually,
    just an aggrement with OS*/
static void riscv_tlbp(CPURISCVState *env)
{
    /* Fixme, always find a index */
    env->smir = 2;
}

static void riscv_tlbr(CPURISCVState *env)
{
    /* Fixme */
    CPUState *cs = CPU(env_archcpu(env));
    tlb_flush(cs);
}

static int write_smcir(CPURISCVState *env, int csrno, target_ulong val)
{
    /* Note: when more than one of the most significant
       bits are asserted at the same time, these operations
       are implemented according to the priority as follows:
           Tlb invalid all operation
           Tlb invalid operation
           Tlb probe operation
           Tlb writing index operation
           Tlb writing random operation
           Tlb reading operation
    */
    if (val & CSKY_SMCIR_TLBINV_IDX_MASK) {
        riscv_tlbinv_idx(env);
    } else if (val & CSKY_SMCIR_TLBINV_ALL_MASK) {
        riscv_tlbinv_all(env);
    } else if (val & CSKY_SMCIR_TLBINV_MASK) {
        riscv_tlbinv(env);
    } else if (val & CSKY_SMCIR_TLBP_MASK) {
        riscv_tlbp(env);
    } else if (val & CSKY_SMCIR_TLBWI_MASK) {
        riscv_tlbwi(env);
    } else if (val & CSKY_SMCIR_TLBWR_MASK) {
        riscv_tlbwr(env);
    } else if (val & CSKY_SMCIR_TLBR_MASK) {
        riscv_tlbr(env);
    }
    return RISCV_EXCP_NONE;
}

static int read_smir(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->smir;
    return RISCV_EXCP_NONE;
}

static int write_smir(CPURISCVState *env, int csrno, target_ulong val)
{
    env->smir = val;
    return RISCV_EXCP_NONE;
}

static int read_smeh(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->smeh;
    return RISCV_EXCP_NONE;
}

static int write_smeh(CPURISCVState *env, int csrno, target_ulong val)
{
    env->smeh = val;
    return RISCV_EXCP_NONE;
}

static int read_smlo0(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->smlo0;
    return RISCV_EXCP_NONE;
}

static int write_smlo0(CPURISCVState *env, int csrno, target_ulong val)
{
    env->smlo0 = val;
    return RISCV_EXCP_NONE;
}

static int read_cpuid(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->cpuid;
    return RISCV_EXCP_NONE;
}

/* Supervisor Trap Setup */
static RISCVException read_sstatus(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    target_ulong mask = (sstatus_v1_10_mask);
    if (env->vext_ver == VEXT_VERSION_1_00_0) {
        mask |= SSTATUS_VS;
    }
    if (env_archcpu(env)->cfg.ext_matrix) {
        mask |= SSTATUS_MS;
    }
    if (cpu_get_xl(env) != MXL_RV32 || env->debugger) {
        mask |= SSTATUS64_UXL;
    }
    /* TODO: Use SXL not MXL. */
    *val = add_status_sd(env, riscv_cpu_mxl(env), env->mstatus & mask);
    return RISCV_EXCP_NONE;
}

static RISCVException write_sstatus(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    target_ulong mask = (sstatus_v1_10_mask);

    if (env->vext_ver == VEXT_VERSION_1_00_0) {
        mask |= SSTATUS_VS;
    }
    if (env_archcpu(env)->cfg.ext_matrix) {
        mask |= SSTATUS_MS;
    }

    if (cpu_get_xl(env) != MXL_RV32 || env->debugger) {
        mask |= SSTATUS64_UXL;
    }
    target_ulong newval = (env->mstatus & ~mask) | (val & mask);
    return write_mstatus(env, CSR_MSTATUS, newval);
}

static RISCVException read_vsie(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    /* Shift the VS bits to their S bit location in vsie */
    *val = (env->mie & env->hideleg & VS_MODE_INTERRUPTS) >> 1;
    return RISCV_EXCP_NONE;
}

static RISCVException read_sie(CPURISCVState *env, int csrno,
                               target_ulong *val)
{
    if (riscv_cpu_virt_enabled(env)) {
        read_vsie(env, CSR_VSIE, val);
    } else {
        /* The xie CSR appears hardwired to zero in CLIC mode. (Section 4.3) */
        *val = riscv_clic_is_clic_mode(env) ? 0 : env->mie & env->mideleg;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsie(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    /* Shift the S bits to their VS bit location in mie */
    target_ulong newval = (env->mie & ~VS_MODE_INTERRUPTS) |
                          ((val << 1) & env->hideleg & VS_MODE_INTERRUPTS);
    return write_mie(env, CSR_MIE, newval);
}

static int write_sie(CPURISCVState *env, int csrno, target_ulong val)
{
    if (riscv_cpu_virt_enabled(env)) {
        write_vsie(env, CSR_VSIE, val);
    } else {
        target_ulong newval = (env->mie & ~S_MODE_INTERRUPTS) |
                              (val & S_MODE_INTERRUPTS);
        /* Writes to xie will be ignored and will not trap. (Section 4.3) */
        if (riscv_clic_is_clic_mode(env)) {
            return RISCV_EXCP_NONE;;
        }
        write_mie(env, CSR_MIE, newval);
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_stvec(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->stvec;
    return RISCV_EXCP_NONE;
}

static RISCVException write_stvec(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    /*
     * If only basic mode is supported, writes to bit 1 are ignored and
     * it is always set to zero (current behavior).
     * If only CLIC mode is supported, writes to bit 1 are also ignored and
     * it is always set to one. CLIC mode hardwires xtvec bits 2-5 to zero
     * (assuming no further CLIC extensions are supported).
     */
    if (env->clic) {
        val = (val & ~0x3f) | 0b000011;
    } else {
        val = val & ~0x2;
    }
    env->stvec = val;
    return RISCV_EXCP_NONE;
}

static int read_stvt(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->stvt;
    return RISCV_EXCP_NONE;
}

static int write_stvt(CPURISCVState *env, int csrno, target_ulong val)
{
    env->stvt = val & ~((1ULL << 6) - 1);
    return RISCV_EXCP_NONE;
}

static RISCVException read_scounteren(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = env->scounteren;
    return RISCV_EXCP_NONE;
}

static RISCVException write_scounteren(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    env->scounteren = val;
    return RISCV_EXCP_NONE;
}

/* Supervisor Trap Handling */
static RISCVException read_sscratch(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->sscratch;
    return RISCV_EXCP_NONE;
}

static RISCVException write_sscratch(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    env->sscratch = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_sepc(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = env->sepc;
    return RISCV_EXCP_NONE;
}

static RISCVException write_sepc(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    env->sepc = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_scause(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->scause;
    return RISCV_EXCP_NONE;
}

static RISCVException write_scause(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->scause = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_stval(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->stval;
    return RISCV_EXCP_NONE;
}

static RISCVException write_stval(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->stval = val;
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_vsip(CPURISCVState *env, int csrno,
                               target_ulong *ret_value,
                               target_ulong new_value, target_ulong write_mask)
{
    /* Shift the S bits to their VS bit location in mip */
    int ret = rmw_mip(env, 0, ret_value, new_value << 1,
                      (write_mask << 1) & vsip_writable_mask & env->hideleg);

    if (ret_value) {
        *ret_value &= VS_MODE_INTERRUPTS;
        /* Shift the VS bits to their S bit location in vsip */
        *ret_value >>= 1;
    }
    return ret;
}

static RISCVException rmw_sip(CPURISCVState *env, int csrno,
                              target_ulong *ret_value,
                              target_ulong new_value, target_ulong write_mask)
{
    int ret;

    if (riscv_cpu_virt_enabled(env)) {
        ret = rmw_vsip(env, CSR_VSIP, ret_value, new_value, write_mask);
    } else {
        /* The xip CSR appears hardwired to zero in CLIC mode. (Section 4.3) */
        if (riscv_clic_is_clic_mode(env)) {
            *ret_value = 0;
            return RISCV_EXCP_NONE;
        }
        ret = rmw_mip(env, CSR_MSTATUS, ret_value, new_value,
                      write_mask & env->mideleg & sip_writable_mask);
    }

    if (ret_value) {
        *ret_value &= env->mideleg;
    }
    return ret;
}

static int read_sintstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    target_ulong mask = SINTSTATUS_SIL | SINTSTATUS_UIL;
    *val = env->mintstatus & mask;
    return RISCV_EXCP_NONE;
}

static int read_sintthresh(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sintthresh;
    return RISCV_EXCP_NONE;
}

static int write_sintthresh(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sintthresh = val;
    return RISCV_EXCP_NONE;
}

/* Supervisor Protection and Translation */
static RISCVException read_satp(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        *val = 0;
        return RISCV_EXCP_NONE;
    }

    if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
        return RISCV_EXCP_ILLEGAL_INST;
    } else {
        *val = env->satp;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException write_satp(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    target_ulong vm, mask, asid;

    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        return RISCV_EXCP_NONE;
    }

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        vm = validate_vm(env, get_field(val, SATP32_MODE));
        mask = (val ^ env->satp) & (SATP32_MODE | SATP32_ASID | SATP32_PPN);
        asid = (val ^ env->satp) & SATP32_ASID;
    } else {
        vm = validate_vm(env, get_field(val, SATP64_MODE));
        mask = (val ^ env->satp) & (SATP64_MODE | SATP64_ASID | SATP64_PPN);
        asid = (val ^ env->satp) & SATP64_ASID;
    }

    if (vm && mask) {
        if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
            return RISCV_EXCP_ILLEGAL_INST;
        } else {
            if (asid) {
                tlb_flush(env_cpu(env));
            }
            env->satp = val;
        }
    }
    return RISCV_EXCP_NONE;
}

/* Hypervisor Extensions */
static RISCVException read_hstatus(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->hstatus;
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        /* We only support 64-bit VSXL */
        *val = set_field(*val, HSTATUS_VSXL, 2);
    }
    /* We only support little endian */
    *val = set_field(*val, HSTATUS_VSBE, 0);
    return RISCV_EXCP_NONE;
}

static RISCVException write_hstatus(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->hstatus = val;
    if (riscv_cpu_mxl(env) != MXL_RV32 && get_field(val, HSTATUS_VSXL) != 2) {
        qemu_log_mask(LOG_UNIMP, "QEMU does not support mixed HSXLEN options.");
    }
    if (get_field(val, HSTATUS_VSBE) != 0) {
        qemu_log_mask(LOG_UNIMP, "QEMU does not support big endian guests.");
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_hedeleg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->hedeleg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hedeleg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->hedeleg = val & vs_delegable_excps;
    return RISCV_EXCP_NONE;
}

static RISCVException read_hideleg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->hideleg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hideleg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->hideleg = val & vs_delegable_ints;
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_hvip(CPURISCVState *env, int csrno,
                               target_ulong *ret_value,
                               target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, 0, ret_value, new_value,
                      write_mask & hvip_writable_mask);

    if (ret_value) {
        *ret_value &= hvip_writable_mask;
    }
    return ret;
}

static RISCVException rmw_hip(CPURISCVState *env, int csrno,
                              target_ulong *ret_value,
                              target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, 0, ret_value, new_value,
                      write_mask & hip_writable_mask);

    if (ret_value) {
        *ret_value &= hip_writable_mask;
    }
    return ret;
}

static RISCVException read_hie(CPURISCVState *env, int csrno,
                               target_ulong *val)
{
    *val = env->mie & VS_MODE_INTERRUPTS;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hie(CPURISCVState *env, int csrno,
                                target_ulong val)
{
    target_ulong newval = (env->mie & ~VS_MODE_INTERRUPTS) | (val & VS_MODE_INTERRUPTS);
    return write_mie(env, CSR_MIE, newval);
}

static RISCVException read_hcounteren(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = env->hcounteren;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hcounteren(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    env->hcounteren = val;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hgeie(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    if (val) {
        qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_htval(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->htval;
    return RISCV_EXCP_NONE;
}

static RISCVException write_htval(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->htval = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_htinst(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->htinst;
    return RISCV_EXCP_NONE;
}

static RISCVException write_htinst(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    return RISCV_EXCP_NONE;
}

static RISCVException write_hgeip(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    if (val) {
        qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_hgatp(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->hgatp;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hgatp(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->hgatp = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_htimedelta(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->htimedelta;
    return RISCV_EXCP_NONE;
}

static RISCVException write_htimedelta(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        env->htimedelta = deposit64(env->htimedelta, 0, 32, (uint64_t)val);
    } else {
        env->htimedelta = val;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_htimedeltah(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->htimedelta >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException write_htimedeltah(CPURISCVState *env, int csrno,
                                        target_ulong val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    env->htimedelta = deposit64(env->htimedelta, 32, 32, (uint64_t)val);
    return RISCV_EXCP_NONE;
}

/* Virtual CSR Registers */
static RISCVException read_vsstatus(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->vsstatus;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsstatus(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    uint64_t mask = (target_ulong)-1;
    env->vsstatus = (env->vsstatus & ~mask) | (uint64_t)val;
    return RISCV_EXCP_NONE;
}

static int read_vstvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstvec;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vstvec(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->vstvec = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vsscratch(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    *val = env->vsscratch;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsscratch(CPURISCVState *env, int csrno,
                                      target_ulong val)
{
    env->vsscratch = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vsepc(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->vsepc;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsepc(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->vsepc = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vscause(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->vscause;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vscause(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->vscause = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vstval(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->vstval;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vstval(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->vstval = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vsatp(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->vsatp;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsatp(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->vsatp = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mtval2(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->mtval2;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mtval2(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->mtval2 = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mtinst(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->mtinst;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mtinst(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->mtinst = val;
    return RISCV_EXCP_NONE;
}

/* Physical Memory Protection */
static RISCVException read_mseccfg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = mseccfg_csr_read(env);
    return RISCV_EXCP_NONE;
}

static RISCVException write_mseccfg(CPURISCVState *env, int csrno,
                         target_ulong val)
{
    mseccfg_csr_write(env, val);
    return RISCV_EXCP_NONE;
}

static bool check_pmp_reg_index(CPURISCVState *env, uint32_t reg_index)
{
    if ((reg_index & 1) && (riscv_cpu_mxl(env) == MXL_RV64)) {
        return false;
    }
    return true;
}

static RISCVException read_pmpcfg(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    uint32_t reg_index = csrno - CSR_PMPCFG0;

    if (!check_pmp_reg_index(env, reg_index)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    *val = pmpcfg_csr_read(env, csrno - CSR_PMPCFG0);
    return RISCV_EXCP_NONE;
}

static RISCVException write_pmpcfg(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    uint32_t reg_index = csrno - CSR_PMPCFG0;

    if (!check_pmp_reg_index(env, reg_index)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    pmpcfg_csr_write(env, csrno - CSR_PMPCFG0, val);
    return RISCV_EXCP_NONE;
}

static RISCVException read_pmpaddr(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = pmpaddr_csr_read(env, csrno - CSR_PMPADDR0);
    return RISCV_EXCP_NONE;
}

static RISCVException write_pmpaddr(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    pmpaddr_csr_write(env, csrno - CSR_PMPADDR0, val);
    return RISCV_EXCP_NONE;
}

#endif

static bool c910_csr_ignore(int csrno)
{
    switch (csrno) {
    case CSR_MHCR:
    case CSR_MCOR:
    case CSR_MCCR2:
    case CSR_MCER2:
    case CSR_MHINT:
    case CSR_MCER:
    case CSR_MCOUNTERWEN:
    case CSR_MCOUNTERINTEN:
    case CSR_MCOUNTEROF:
    case CSR_MHINT2:
    case CSR_USP:
    case CSR_MCINS:
    case CSR_MCINDEX:
    case CSR_MCDATA0:
    case CSR_MCDATA1:
    case CSR_MEICR:
    case CSR_MEICR2:
    case CSR_MRADDR:
    case CSR_MNMICAUSE:
    case CSR_MNMIPC:
    case CSR_MSMPR:
    case CSR_MIESR:
    case CSR_MSBEPA:
    case CSR_MSBEPA2:
    case CSR_MAPBADDR:
    case CSR_SHCR:
    case CSR_SCER2:
    case CSR_SCER:
    case CSR_SCOUNTERINTEN:
    case CSR_SCOUNTEROF:
    case CSR_SIESR:
    case CSR_SSBEPA:
    case CSR_SSBEPA2:
    case CSR_CYCLE_C910 ... CSR_SHPMCOUNTER31:
        return true;
        break;
    default:
        return false;
        break;
    }
}

/*
 * riscv_csrrw - read and/or update control and status register
 *
 * csrr   <->  riscv_csrrw(env, csrno, ret_value, 0, 0);
 * csrrw  <->  riscv_csrrw(env, csrno, ret_value, value, -1);
 * csrrs  <->  riscv_csrrw(env, csrno, ret_value, -1, value);
 * csrrc  <->  riscv_csrrw(env, csrno, ret_value, 0, value);
 */

RISCVException riscv_csrrw(CPURISCVState *env, int csrno,
                           target_ulong *ret_value,
                           target_ulong new_value, target_ulong write_mask)
{
    RISCVException ret;
    target_ulong old_value;
    RISCVCPU *cpu = env_archcpu(env);
    int read_only = get_field(csrno, 0xC00) == 3;
    int csr_min_priv = csr_ops[csrno].min_priv_ver;

    if (c910_csr_ignore(csrno)) {
        return RISCV_EXCP_NONE;
    }
    /* check privileges and return -1 if check fails */
#if !defined(CONFIG_USER_ONLY)
    int effective_priv = env->priv;

    if (riscv_has_ext(env, RVH) &&
        env->priv == PRV_S &&
        !riscv_cpu_virt_enabled(env)) {
        /*
         * We are in S mode without virtualisation, therefore we are in HS Mode.
         * Add 1 to the effective privledge level to allow us to access the
         * Hypervisor CSRs.
         */
        effective_priv++;
    }

    if ((write_mask && read_only) ||
        (!env->debugger && (effective_priv < get_field(csrno, 0x300)))) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#else
    if (write_mask && read_only) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif

    /* ensure the CSR extension is enabled. */
    if (!cpu->cfg.ext_icsr) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (env->priv_ver < csr_min_priv) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    /* check predicate */
    if (!csr_ops[csrno].predicate) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    ret = csr_ops[csrno].predicate(env, csrno);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /* execute combined read/write operation if it exists */
    if (csr_ops[csrno].op) {
        return csr_ops[csrno].op(env, csrno, ret_value, new_value, write_mask);
    }

    /* if no accessor exists then return failure */
    if (!csr_ops[csrno].read) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    /* read old value */
    ret = csr_ops[csrno].read(env, csrno, &old_value);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /* write value if writable and write mask set, otherwise drop writes */
    if (write_mask) {
        new_value = (old_value & ~write_mask) | (new_value & write_mask);
        if (csr_ops[csrno].write) {
            ret = csr_ops[csrno].write(env, csrno, new_value);
            if (ret != RISCV_EXCP_NONE) {
                return ret;
            }
        }
    }

    /* return old value */
    if (ret_value) {
        *ret_value = old_value;
    }

    return RISCV_EXCP_NONE;
}

/*
 * Debugger support.  If not in user mode, set env->debugger before the
 * riscv_csrrw call and clear it after the call.
 */
RISCVException riscv_csrrw_debug(CPURISCVState *env, int csrno,
                                 target_ulong *ret_value,
                                 target_ulong new_value,
                                 target_ulong write_mask)
{
    RISCVException ret;
#if !defined(CONFIG_USER_ONLY)
    env->debugger = true;
#endif
    ret = riscv_csrrw(env, csrno, ret_value, new_value, write_mask);
#if !defined(CONFIG_USER_ONLY)
    env->debugger = false;
#endif
    return ret;
}

/* Control and Status Register function table */
riscv_csr_operations csr_ops[CSR_TABLE_SIZE] = {
    /* User Floating-Point CSRs */
    [CSR_FFLAGS]   = { "fflags",   fs,     read_fflags,  write_fflags },
    [CSR_FRM]      = { "frm",      fs,     read_frm,     write_frm    },
    [CSR_FCSR]     = { "fcsr",     fs,     read_fcsr,    write_fcsr   },
    [CSR_FXCR] =     { "fxcr",     fs,     read_fxcr,    write_fxcr   },
    /* Vector CSRs */
    [CSR_VSTART]   = { "vstart",   vs,     read_vstart,  write_vstart },
    [CSR_VXSAT]    = { "vxsat",    vs,     read_vxsat,   write_vxsat  },
    [CSR_VXRM]     = { "vxrm",     vs,     read_vxrm,    write_vxrm   },
    [CSR_VCSR]     = { "vcsr",     vs,     read_vcsr,    write_vcsr   },
    [CSR_VL]       = { "vl",       vs,     read_vl                    },
    [CSR_VTYPE]    = { "vtype",    vs,     read_vtype                 },
    [CSR_VLENB]    = { "vlenb",    vs,     read_vlenb                 },

    /* Matrix CSRs */
    [CSR_MRSTART]  = { "xmrstart", ms,    read_xmrstart, write_xmrstart},
    [CSR_MCSR]     = { "xmcsr",    ms,    read_xmcsr,    write_xmcsr   },
    [CSR_MSIZE]    = { "xmsize",   ms,    read_xmsize,   write_xmsize  },
    [CSR_MREGSIZE] = { "xmlenb",   ms,    read_xmlenb                },
    [CSR_MLENB]    = { "xrlenb",   ms,    read_xrlenb                },
    [CSR_XMISA]    = { "xmisa",    ms,    read_xmisa                 },

    /* User Timers and Counters */
    [CSR_CYCLE]    = { "cycle",    ctr,    read_instret  },
    [CSR_INSTRET]  = { "instret",  ctr,    read_instret  },
    [CSR_CYCLEH]   = { "cycleh",   ctr32,  read_instreth },
    [CSR_INSTRETH] = { "instreth", ctr32,  read_instreth },

    /*
     * In privileged mode, the monitor will have to emulate TIME CSRs only if
     * rdtime callback is not provided by machine/platform emulation.
     */
    [CSR_TIME]  = { "time",  ctr,   read_time  },
    [CSR_TIMEH] = { "timeh", ctr32, read_timeh },

#if !defined(CONFIG_USER_ONLY)
    /* Machine Timers and Counters */
    [CSR_MCYCLE]    = { "mcycle",    any,   read_instret  },
    [CSR_MINSTRET]  = { "minstret",  any,   read_instret  },
    [CSR_MCYCLEH]   = { "mcycleh",   any32, read_instreth },
    [CSR_MINSTRETH] = { "minstreth", any32, read_instreth },

    /* Machine Information Registers */
    [CSR_MVENDORID] = { "mvendorid", 	any,   		read_zero    },
    [CSR_MARCHID]   = { "marchid",   	any,   		read_zero    },
    [CSR_MIMPID]    = { "mimpid",    	any,   		read_zero    },
    [CSR_MHARTID]   = { "mhartid",   	any,   		read_mhartid },
    [CSR_MCONFIGPTR]  = { "mconfigptr", any,   read_zero,
                                        .min_priv_ver = PRIV_VERSION_1_12_0 },

    /* Machine Trap Setup */
    [CSR_MSTATUS]     = { "mstatus",    any,   read_mstatus,     write_mstatus     },
    [CSR_MISA]        = { "misa",       any,   read_misa,        write_misa        },
    [CSR_MIDELEG]     = { "mideleg",    any,   read_mideleg,     write_mideleg     },
    [CSR_MEDELEG]     = { "medeleg",    any,   read_medeleg,     write_medeleg     },
    [CSR_MIE]         = { "mie",        any,   read_mie,         write_mie         },
    [CSR_MTVEC]       = { "mtvec",      any,   read_mtvec,       write_mtvec       },
    [CSR_MCOUNTEREN]  = { "mcounteren", any,   read_mcounteren,  write_mcounteren  },

    [CSR_MSTATUSH]    = { "mstatush",   any32, read_mstatush,    write_mstatush    },

    /* Machine Trap Handling */
    [CSR_MSCRATCH] = { "mscratch", any,  read_mscratch, write_mscratch },
    [CSR_MEPC]     = { "mepc",     any,  read_mepc,     write_mepc     },
    [CSR_MCAUSE]   = { "mcause",   any,  read_mcause,   write_mcause   },
    [CSR_MTVAL]    = { "mtval",    any,  read_mtval,    write_mtval    },
    [CSR_MIP]      = { "mip",      any,  NULL,    NULL, rmw_mip        },

    /* Execution environment configuration */
    [CSR_MENVCFG]  = { "menvcfg",  any,   read_menvcfg,  write_menvcfg,
                                          .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_MENVCFGH] = { "menvcfgh", any32, read_menvcfgh, write_menvcfgh,
                                          .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_SENVCFG]  = { "senvcfg",  smode, read_senvcfg,  write_senvcfg,
                                          .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HENVCFG]  = { "henvcfg",  hmode, read_henvcfg, write_henvcfg,
                                          .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HENVCFGH] = { "henvcfgh", hmode32, read_henvcfgh, write_henvcfgh,
                                          .min_priv_ver = PRIV_VERSION_1_12_0 },

    /* Supervisor Trap Setup */
    [CSR_SSTATUS]    = { "sstatus",    smode, read_sstatus,    write_sstatus    },
    [CSR_SIE]        = { "sie",        smode, read_sie,        write_sie        },
    [CSR_STVEC]      = { "stvec",      smode, read_stvec,      write_stvec      },
    [CSR_SCOUNTEREN] = { "scounteren", smode, read_scounteren, write_scounteren },

    /* Supervisor Trap Handling */
    [CSR_SSCRATCH] = { "sscratch", smode, read_sscratch, write_sscratch },
    [CSR_SEPC]     = { "sepc",     smode, read_sepc,     write_sepc     },
    [CSR_SCAUSE]   = { "scause",   smode, read_scause,   write_scause   },
    [CSR_STVAL]    = { "stval",    smode, read_stval,   write_stval   },
    [CSR_SIP]      = { "sip",      smode, NULL,    NULL, rmw_sip        },
    [CSR_STIMECMP] = { "stimecmp", sstc, read_stimecmp, write_stimecmp  },
    [CSR_STIMECMPH] = { "stimecmph", sstc, read_stimecmph, write_stimecmph },
    /* Supervisor Protection and Translation */
    [CSR_SATP]     = { "satp",     smode, read_satp,    write_satp      },

    [CSR_HSTATUS]     = { "hstatus",     hmode,   read_hstatus,     write_hstatus     },
    [CSR_HEDELEG]     = { "hedeleg",     hmode,   read_hedeleg,     write_hedeleg     },
    [CSR_HIDELEG]     = { "hideleg",     hmode,   read_hideleg,     write_hideleg     },
    [CSR_HVIP]        = { "hvip",        hmode,   NULL,   NULL,     rmw_hvip          },
    [CSR_HIP]         = { "hip",         hmode,   NULL,   NULL,     rmw_hip           },
    [CSR_HIE]         = { "hie",         hmode,   read_hie,         write_hie         },
    [CSR_HCOUNTEREN]  = { "hcounteren",  hmode,   read_hcounteren,  write_hcounteren  },
    [CSR_HGEIE]       = { "hgeie",       hmode,   read_zero,        write_hgeie       },
    [CSR_HTVAL]       = { "htval",       hmode,   read_htval,       write_htval       },
    [CSR_HTINST]      = { "htinst",      hmode,   read_htinst,      write_htinst      },
    [CSR_HGEIP]       = { "hgeip",       hmode,   read_zero,        write_hgeip       },
    [CSR_HGATP]       = { "hgatp",       hmode,   read_hgatp,       write_hgatp       },
    [CSR_HTIMEDELTA]  = { "htimedelta",  hmode,   read_htimedelta,  write_htimedelta  },
    [CSR_HTIMEDELTAH] = { "htimedeltah", hmode32, read_htimedeltah, write_htimedeltah },

    [CSR_VSSTATUS]    = { "vsstatus",    hmode,   read_vsstatus,    write_vsstatus    },
    [CSR_VSIP]        = { "vsip",        hmode,   NULL,    NULL,    rmw_vsip          },
    [CSR_VSIE]        = { "vsie",        hmode,   read_vsie,        write_vsie        },
    [CSR_VSTVEC]      = { "vstvec",      hmode,   read_vstvec,      write_vstvec      },
    [CSR_VSSCRATCH]   = { "vsscratch",   hmode,   read_vsscratch,   write_vsscratch   },
    [CSR_VSEPC]       = { "vsepc",       hmode,   read_vsepc,       write_vsepc       },
    [CSR_VSCAUSE]     = { "vscause",     hmode,   read_vscause,     write_vscause     },
    [CSR_VSTVAL]      = { "vstval",      hmode,   read_vstval,      write_vstval      },
    [CSR_VSATP]       = { "vsatp",       hmode,   read_vsatp,       write_vsatp       },

    [CSR_MTVAL2]      = { "mtval2",      hmode,   read_mtval2,      write_mtval2      },
    [CSR_MTINST]      = { "mtinst",      hmode,   read_mtinst,      write_mtinst      },

    /* Physical Memory Protection */
    [CSR_MSECCFG]    = { "mseccfg",  epmp, read_mseccfg, write_mseccfg,
                                     .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_PMPCFG0]    = { "pmpcfg0",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG1]    = { "pmpcfg1",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG2]    = { "pmpcfg2",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG3]    = { "pmpcfg3",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPADDR0]   = { "pmpaddr0",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR1]   = { "pmpaddr1",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR2]   = { "pmpaddr2",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR3]   = { "pmpaddr3",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR4]   = { "pmpaddr4",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR5]   = { "pmpaddr5",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR6]   = { "pmpaddr6",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR7]   = { "pmpaddr7",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR8]   = { "pmpaddr8",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR9]   = { "pmpaddr9",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR10]  = { "pmpaddr10", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR11]  = { "pmpaddr11", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR12]  = { "pmpaddr12", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR13]  = { "pmpaddr13", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR14] =  { "pmpaddr14", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR15] =  { "pmpaddr15", pmp, read_pmpaddr, write_pmpaddr },

    /* Performance Counters */
    [CSR_HPMCOUNTER3]    = { "hpmcounter3",    ctr,    read_zero },
    [CSR_HPMCOUNTER4]    = { "hpmcounter4",    ctr,    read_zero },
    [CSR_HPMCOUNTER5]    = { "hpmcounter5",    ctr,    read_zero },
    [CSR_HPMCOUNTER6]    = { "hpmcounter6",    ctr,    read_zero },
    [CSR_HPMCOUNTER7]    = { "hpmcounter7",    ctr,    read_zero },
    [CSR_HPMCOUNTER8]    = { "hpmcounter8",    ctr,    read_zero },
    [CSR_HPMCOUNTER9]    = { "hpmcounter9",    ctr,    read_zero },
    [CSR_HPMCOUNTER10]   = { "hpmcounter10",   ctr,    read_zero },
    [CSR_HPMCOUNTER11]   = { "hpmcounter11",   ctr,    read_zero },
    [CSR_HPMCOUNTER12]   = { "hpmcounter12",   ctr,    read_zero },
    [CSR_HPMCOUNTER13]   = { "hpmcounter13",   ctr,    read_zero },
    [CSR_HPMCOUNTER14]   = { "hpmcounter14",   ctr,    read_zero },
    [CSR_HPMCOUNTER15]   = { "hpmcounter15",   ctr,    read_zero },
    [CSR_HPMCOUNTER16]   = { "hpmcounter16",   ctr,    read_zero },
    [CSR_HPMCOUNTER17]   = { "hpmcounter17",   ctr,    read_zero },
    [CSR_HPMCOUNTER18]   = { "hpmcounter18",   ctr,    read_zero },
    [CSR_HPMCOUNTER19]   = { "hpmcounter19",   ctr,    read_zero },
    [CSR_HPMCOUNTER20]   = { "hpmcounter20",   ctr,    read_zero },
    [CSR_HPMCOUNTER21]   = { "hpmcounter21",   ctr,    read_zero },
    [CSR_HPMCOUNTER22]   = { "hpmcounter22",   ctr,    read_zero },
    [CSR_HPMCOUNTER23]   = { "hpmcounter23",   ctr,    read_zero },
    [CSR_HPMCOUNTER24]   = { "hpmcounter24",   ctr,    read_zero },
    [CSR_HPMCOUNTER25]   = { "hpmcounter25",   ctr,    read_zero },
    [CSR_HPMCOUNTER26]   = { "hpmcounter26",   ctr,    read_zero },
    [CSR_HPMCOUNTER27]   = { "hpmcounter27",   ctr,    read_zero },
    [CSR_HPMCOUNTER28]   = { "hpmcounter28",   ctr,    read_zero },
    [CSR_HPMCOUNTER29]   = { "hpmcounter29",   ctr,    read_zero },
    [CSR_HPMCOUNTER30]   = { "hpmcounter30",   ctr,    read_zero },
    [CSR_HPMCOUNTER31]   = { "hpmcounter31",   ctr,    read_zero },

    [CSR_MHPMCOUNTER3]   = { "mhpmcounter3",   any,    read_zero },
    [CSR_MHPMCOUNTER4]   = { "mhpmcounter4",   any,    read_zero },
    [CSR_MHPMCOUNTER5]   = { "mhpmcounter5",   any,    read_zero },
    [CSR_MHPMCOUNTER6]   = { "mhpmcounter6",   any,    read_zero },
    [CSR_MHPMCOUNTER7]   = { "mhpmcounter7",   any,    read_zero },
    [CSR_MHPMCOUNTER8]   = { "mhpmcounter8",   any,    read_zero },
    [CSR_MHPMCOUNTER9]   = { "mhpmcounter9",   any,    read_zero },
    [CSR_MHPMCOUNTER10]  = { "mhpmcounter10",  any,    read_zero },
    [CSR_MHPMCOUNTER11]  = { "mhpmcounter11",  any,    read_zero },
    [CSR_MHPMCOUNTER12]  = { "mhpmcounter12",  any,    read_zero },
    [CSR_MHPMCOUNTER13]  = { "mhpmcounter13",  any,    read_zero },
    [CSR_MHPMCOUNTER14]  = { "mhpmcounter14",  any,    read_zero },
    [CSR_MHPMCOUNTER15]  = { "mhpmcounter15",  any,    read_zero },
    [CSR_MHPMCOUNTER16]  = { "mhpmcounter16",  any,    read_zero },
    [CSR_MHPMCOUNTER17]  = { "mhpmcounter17",  any,    read_zero },
    [CSR_MHPMCOUNTER18]  = { "mhpmcounter18",  any,    read_zero },
    [CSR_MHPMCOUNTER19]  = { "mhpmcounter19",  any,    read_zero },
    [CSR_MHPMCOUNTER20]  = { "mhpmcounter20",  any,    read_zero },
    [CSR_MHPMCOUNTER21]  = { "mhpmcounter21",  any,    read_zero },
    [CSR_MHPMCOUNTER22]  = { "mhpmcounter22",  any,    read_zero },
    [CSR_MHPMCOUNTER23]  = { "mhpmcounter23",  any,    read_zero },
    [CSR_MHPMCOUNTER24]  = { "mhpmcounter24",  any,    read_zero },
    [CSR_MHPMCOUNTER25]  = { "mhpmcounter25",  any,    read_zero },
    [CSR_MHPMCOUNTER26]  = { "mhpmcounter26",  any,    read_zero },
    [CSR_MHPMCOUNTER27]  = { "mhpmcounter27",  any,    read_zero },
    [CSR_MHPMCOUNTER28]  = { "mhpmcounter28",  any,    read_zero },
    [CSR_MHPMCOUNTER29]  = { "mhpmcounter29",  any,    read_zero },
    [CSR_MHPMCOUNTER30]  = { "mhpmcounter30",  any,    read_zero },
    [CSR_MHPMCOUNTER31]  = { "mhpmcounter31",  any,    read_zero },

    [CSR_MHPMEVENT3]     = { "mhpmevent3",     any,    read_zero },
    [CSR_MHPMEVENT4]     = { "mhpmevent4",     any,    read_zero },
    [CSR_MHPMEVENT5]     = { "mhpmevent5",     any,    read_zero },
    [CSR_MHPMEVENT6]     = { "mhpmevent6",     any,    read_zero },
    [CSR_MHPMEVENT7]     = { "mhpmevent7",     any,    read_zero },
    [CSR_MHPMEVENT8]     = { "mhpmevent8",     any,    read_zero },
    [CSR_MHPMEVENT9]     = { "mhpmevent9",     any,    read_zero },
    [CSR_MHPMEVENT10]    = { "mhpmevent10",    any,    read_zero },
    [CSR_MHPMEVENT11]    = { "mhpmevent11",    any,    read_zero },
    [CSR_MHPMEVENT12]    = { "mhpmevent12",    any,    read_zero },
    [CSR_MHPMEVENT13]    = { "mhpmevent13",    any,    read_zero },
    [CSR_MHPMEVENT14]    = { "mhpmevent14",    any,    read_zero },
    [CSR_MHPMEVENT15]    = { "mhpmevent15",    any,    read_zero },
    [CSR_MHPMEVENT16]    = { "mhpmevent16",    any,    read_zero },
    [CSR_MHPMEVENT17]    = { "mhpmevent17",    any,    read_zero },
    [CSR_MHPMEVENT18]    = { "mhpmevent18",    any,    read_zero },
    [CSR_MHPMEVENT19]    = { "mhpmevent19",    any,    read_zero },
    [CSR_MHPMEVENT20]    = { "mhpmevent20",    any,    read_zero },
    [CSR_MHPMEVENT21]    = { "mhpmevent21",    any,    read_zero },
    [CSR_MHPMEVENT22]    = { "mhpmevent22",    any,    read_zero },
    [CSR_MHPMEVENT23]    = { "mhpmevent23",    any,    read_zero },
    [CSR_MHPMEVENT24]    = { "mhpmevent24",    any,    read_zero },
    [CSR_MHPMEVENT25]    = { "mhpmevent25",    any,    read_zero },
    [CSR_MHPMEVENT26]    = { "mhpmevent26",    any,    read_zero },
    [CSR_MHPMEVENT27]    = { "mhpmevent27",    any,    read_zero },
    [CSR_MHPMEVENT28]    = { "mhpmevent28",    any,    read_zero },
    [CSR_MHPMEVENT29]    = { "mhpmevent29",    any,    read_zero },
    [CSR_MHPMEVENT30]    = { "mhpmevent30",    any,    read_zero },
    [CSR_MHPMEVENT31]    = { "mhpmevent31",    any,    read_zero },

    [CSR_HPMCOUNTER3H]   = { "hpmcounter3h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER4H]   = { "hpmcounter4h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER5H]   = { "hpmcounter5h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER6H]   = { "hpmcounter6h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER7H]   = { "hpmcounter7h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER8H]   = { "hpmcounter8h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER9H]   = { "hpmcounter9h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER10H]  = { "hpmcounter10h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER11H]  = { "hpmcounter11h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER12H]  = { "hpmcounter12h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER13H]  = { "hpmcounter13h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER14H]  = { "hpmcounter14h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER15H]  = { "hpmcounter15h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER16H]  = { "hpmcounter16h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER17H]  = { "hpmcounter17h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER18H]  = { "hpmcounter18h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER19H]  = { "hpmcounter19h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER20H]  = { "hpmcounter20h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER21H]  = { "hpmcounter21h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER22H]  = { "hpmcounter22h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER23H]  = { "hpmcounter23h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER24H]  = { "hpmcounter24h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER25H]  = { "hpmcounter25h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER26H]  = { "hpmcounter26h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER27H]  = { "hpmcounter27h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER28H]  = { "hpmcounter28h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER29H]  = { "hpmcounter29h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER30H]  = { "hpmcounter30h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER31H]  = { "hpmcounter31h",  ctr32,  read_zero },

    [CSR_MHPMCOUNTER3H]  = { "mhpmcounter3h",  any32,  read_zero },
    [CSR_MHPMCOUNTER4H]  = { "mhpmcounter4h",  any32,  read_zero },
    [CSR_MHPMCOUNTER5H]  = { "mhpmcounter5h",  any32,  read_zero },
    [CSR_MHPMCOUNTER6H]  = { "mhpmcounter6h",  any32,  read_zero },
    [CSR_MHPMCOUNTER7H]  = { "mhpmcounter7h",  any32,  read_zero },
    [CSR_MHPMCOUNTER8H]  = { "mhpmcounter8h",  any32,  read_zero },
    [CSR_MHPMCOUNTER9H]  = { "mhpmcounter9h",  any32,  read_zero },
    [CSR_MHPMCOUNTER10H] = { "mhpmcounter10h", any32,  read_zero },
    [CSR_MHPMCOUNTER11H] = { "mhpmcounter11h", any32,  read_zero },
    [CSR_MHPMCOUNTER12H] = { "mhpmcounter12h", any32,  read_zero },
    [CSR_MHPMCOUNTER13H] = { "mhpmcounter13h", any32,  read_zero },
    [CSR_MHPMCOUNTER14H] = { "mhpmcounter14h", any32,  read_zero },
    [CSR_MHPMCOUNTER15H] = { "mhpmcounter15h", any32,  read_zero },
    [CSR_MHPMCOUNTER16H] = { "mhpmcounter16h", any32,  read_zero },
    [CSR_MHPMCOUNTER17H] = { "mhpmcounter17h", any32,  read_zero },
    [CSR_MHPMCOUNTER18H] = { "mhpmcounter18h", any32,  read_zero },
    [CSR_MHPMCOUNTER19H] = { "mhpmcounter19h", any32,  read_zero },
    [CSR_MHPMCOUNTER20H] = { "mhpmcounter20h", any32,  read_zero },
    [CSR_MHPMCOUNTER21H] = { "mhpmcounter21h", any32,  read_zero },
    [CSR_MHPMCOUNTER22H] = { "mhpmcounter22h", any32,  read_zero },
    [CSR_MHPMCOUNTER23H] = { "mhpmcounter23h", any32,  read_zero },
    [CSR_MHPMCOUNTER24H] = { "mhpmcounter24h", any32,  read_zero },
    [CSR_MHPMCOUNTER25H] = { "mhpmcounter25h", any32,  read_zero },
    [CSR_MHPMCOUNTER26H] = { "mhpmcounter26h", any32,  read_zero },
    [CSR_MHPMCOUNTER27H] = { "mhpmcounter27h", any32,  read_zero },
    [CSR_MHPMCOUNTER28H] = { "mhpmcounter28h", any32,  read_zero },
    [CSR_MHPMCOUNTER29H] = { "mhpmcounter29h", any32,  read_zero },
    [CSR_MHPMCOUNTER30H] = { "mhpmcounter30h", any32,  read_zero },
    [CSR_MHPMCOUNTER31H] = { "mhpmcounter31h", any32,  read_zero },

    /* Machine Mode Core Level Interrupt Controller */
    [CSR_MTVT] = { "mtvt", clic,  read_mtvt,  write_mtvt      },
    [CSR_MINTSTATUS] = { "mintstatus", clic,  read_mintstatus },
    [CSR_MINTTHRESH] = { "mintthresh", clic,  read_mintthresh,
                         write_mintthresh },

    /* Supervisor Mode Core Level Interrupt Controller */
    [CSR_SINTSTATUS] = { "sintstatus", clic,  read_sintstatus },
    [CSR_SINTTHRESH] = { "sintthresh", clic,  read_sintthresh,
                         write_sintthresh },
    [CSR_STVT] = { "stvt", clic,  read_stvt, write_stvt       },

    [CSR_MXSTATUS] =   {"mxstatus", any, read_mxstatus, write_mxstatus    },
    [CSR_MEXSTATUS] =  {"mexstatus", any, read_mexstatus, write_mexstatus },
    [CSR_MDTCMCR]   =  {"mdtcmcr", any, read_mdtcmcr, write_mdtcmcr       },
    [CSR_MITCMCR]   =  {"mitcmcr", any, read_mitcmcr, write_mitcmcr       },
    [CSR_MRMR] =       {"mrmr", any, read_mrmr, write_mrmr                },
    [CSR_MRVBR] =      {"mrvbr", any, read_mrvbr, write_mrvbr             },
    [CSR_CPUID] =      {"cpuid", any, read_cpuid                          },
    [CSR_SXSTATUS] =   {"sxstatus", any, read_sxstatus, write_sxstatus    },
    [CSR_SMCIR] =      {"smcir", any, read_smcir, write_smcir             },
    [CSR_SMIR] =       {"smir", any, read_smir, write_smir                },
    [CSR_SMEH] =       {"smeh", any, read_smeh, write_smeh                },
    [CSR_SMLO0] =      {"smlo0", any, read_smlo0, write_smlo0             },
#endif /* !CONFIG_USER_ONLY */
};
