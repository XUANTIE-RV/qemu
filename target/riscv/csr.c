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
#include "qemu/timer.h"
#include "cpu.h"
#include "tcg/tcg-cpu.h"
#include "pmu.h"
#include "time_helper.h"
#include "exec/exec-all.h"
#include "exec/tb-flush.h"
#include "sysemu/cpu-timers.h"
#include "qemu/guest-random.h"
#include "exec/address-spaces.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "internals.h"
#include "qemu/error-report.h"

#if !defined(CONFIG_USER_ONLY)
#include "riscv_xt_clic.h"
#include "mtt.h"
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
#if !defined(CONFIG_USER_ONLY)
RISCVException smstateen_acc_ok(CPURISCVState *env, int index, uint64_t bit)
{
    bool virt = env->virt_enabled;

    if (env->priv == PRV_M || !riscv_cpu_cfg(env)->ext_smstateen) {
        return RISCV_EXCP_NONE;
    }
    if (env->debugger) {
        return RISCV_EXCP_NONE;
    }

    if (!(env->mstateen[index] & bit)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (virt) {
        if (!(env->hstateen[index] & bit)) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }

        if (env->priv == PRV_U && !(env->sstateen[index] & bit)) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    if (env->priv == PRV_U && riscv_has_ext(env, RVS)) {
        if (!(env->sstateen[index] & bit)) {
            return RISCV_EXCP_ILLEGAL_INST;
        }
    }

    return RISCV_EXCP_NONE;
}

static RISCVException xt_fastm(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_xtheadfastm) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    return RISCV_EXCP_NONE;
}
#endif

RISCVException any(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

RISCVException fs(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env) &&
        !riscv_cpu_cfg(env)->ext_zfinx) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return smstateen_acc_ok(env, 0, SMSTATEEN0_FCSR);
    }
#endif
    return RISCV_EXCP_NONE;
}

RISCVException vs(CPURISCVState *env, int csrno)
{
    /* Relax for RVP */
    if ((csrno == CSR_VXSAT) && riscv_has_ext(env, RVP)) {
        return RISCV_EXCP_NONE;
    }

    if (riscv_cpu_cfg(env)->ext_zve32x
        || riscv_cpu_cfg(env)->ext_xtheadvector) {
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
    RISCVCPU *cpu = env_archcpu(env);
    int ctr_index;
    target_ulong ctr_mask;
    int base_csrno = CSR_CYCLE;
    bool rv32 = riscv_cpu_mxl(env) == MXL_RV32 ? true : false;

    if (rv32 && csrno >= CSR_CYCLEH) {
        /* Offset for RV32 hpmcounternh counters */
        base_csrno += 0x80;
    }
    ctr_index = csrno - base_csrno;
    ctr_mask = BIT(ctr_index);

    if ((csrno >= CSR_CYCLE && csrno <= CSR_INSTRET) ||
        (csrno >= CSR_CYCLEH && csrno <= CSR_INSTRETH)) {
        if (!riscv_cpu_cfg(env)->ext_zicntr) {
            return RISCV_EXCP_ILLEGAL_INST;
        }

        goto skip_ext_pmu_check;
    }

    if (!(cpu->pmu_avail_ctrs & ctr_mask)) {
        /* No counter is enabled in PMU or the counter is out of range */
        return RISCV_EXCP_ILLEGAL_INST;
    }

skip_ext_pmu_check:

    if (env->debugger) {
        return RISCV_EXCP_NONE;
    }

    if (env->priv < PRV_M && !get_field(env->mcounteren, ctr_mask)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (env->virt_enabled) {
        if (!get_field(env->hcounteren, ctr_mask) ||
            (env->priv == PRV_U && !get_field(env->scounteren, ctr_mask))) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    if (riscv_has_ext(env, RVS) && env->priv == PRV_U &&
        !get_field(env->scounteren, ctr_mask)) {
        return RISCV_EXCP_ILLEGAL_INST;
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

static RISCVException zcmt(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_zcmt) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

#if !defined(CONFIG_USER_ONLY)
    RISCVException ret = smstateen_acc_ok(env, 0, SMSTATEEN0_JVT);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }
#endif

    return RISCV_EXCP_NONE;
}

#if !defined(CONFIG_USER_ONLY)
static RISCVException mctr(CPURISCVState *env, int csrno)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint32_t pmu_avail_ctrs = cpu->pmu_avail_ctrs;
    int ctr_index;
    int base_csrno = CSR_MHPMCOUNTER3;

    if ((riscv_cpu_mxl(env) == MXL_RV32) && csrno >= CSR_MCYCLEH) {
        /* Offset for RV32 mhpmcounternh counters */
        csrno -= 0x80;
    }

    g_assert(csrno >= CSR_MHPMCOUNTER3 && csrno <= CSR_MHPMCOUNTER31);

    ctr_index = csrno - base_csrno;
    if ((BIT(ctr_index) & pmu_avail_ctrs >> 3) == 0) {
        /* The PMU is not enabled or counter is out of range */
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException mctr32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return mctr(env, csrno);
}

static RISCVException sscofpmf(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_sscofpmf) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException sscofpmf_32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return sscofpmf(env, csrno);
}

static RISCVException smcntrpmf(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_smcntrpmf) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException smcntrpmf_32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return smcntrpmf(env, csrno);
}

static RISCVException any32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return any(env, csrno);

}

static RISCVException aia_any(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_smaia) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return any(env, csrno);
}

static RISCVException aia_any32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_smaia) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return any32(env, csrno);
}

RISCVException smode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException tpe(CPURISCVState *env, int csrno)
{
    if (!(env->xmisa & MATRIX_MDMA)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    return smode(env, csrno);
}

static RISCVException sxcsrind_any(CPURISCVState *env, int csrno)
{
    RISCVCPU *cpu = env_archcpu(env);

    if (!cpu->cfg.ext_smcsrind) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return RISCV_EXCP_NONE;
}

static int sxcsrind_or_aia_any(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_smaia && !riscv_cpu_cfg(env)->ext_smcsrind) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return any(env, csrno);
}

static RISCVException smode32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return smode(env, csrno);
}

static RISCVException aia_smode(CPURISCVState *env, int csrno)
{
    int ret;

    if (!riscv_cpu_cfg(env)->ext_ssaia) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (csrno == CSR_STOPEI) {
        ret = smstateen_acc_ok(env, 0, SMSTATEEN0_IMSIC);
    } else {
        ret = smstateen_acc_ok(env, 0, SMSTATEEN0_AIA);
    }

    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    return smode(env, csrno);
}

static RISCVException aia_smode32(CPURISCVState *env, int csrno)
{
    int ret;

    if (!riscv_cpu_cfg(env)->ext_ssaia) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_AIA);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    return smode32(env, csrno);
}

static RISCVException scountinhibit_pred(CPURISCVState *env, int csrno)
{
    RISCVCPU *cpu = env_archcpu(env);

    if (!cpu->cfg.ext_ssccfg || !cpu->cfg.ext_smcdeleg) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return smode(env, csrno);
}

static RISCVException sxcsrind_smode(CPURISCVState *env, int csrno)
{
    RISCVCPU *cpu = env_archcpu(env);

    if (!cpu->cfg.ext_sscsrind) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return smode(env, csrno);
}

static int sxcsrind_or_aia_smode(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_ssaia && !riscv_cpu_cfg(env)->ext_sscsrind) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return smode(env, csrno);
}

static RISCVException hmode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVH)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException hmode32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return hmode(env, csrno);

}

static RISCVException sxcsrind_hmode(CPURISCVState *env, int csrno)
{
    RISCVCPU *cpu = env_archcpu(env);

    if (!cpu->cfg.ext_sscsrind) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return hmode(env, csrno);
}

static int sxcsrind_or_aia_hmode(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_ssaia && !riscv_cpu_cfg(env)->ext_sscsrind) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return hmode(env, csrno);
}

static RISCVException umode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVU)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException umode32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return umode(env, csrno);
}

static RISCVException mstateen(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_smstateen) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return any(env, csrno);
}

static RISCVException hstateen_pred(CPURISCVState *env, int csrno, int base)
{
    if (!riscv_cpu_cfg(env)->ext_smstateen) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    RISCVException ret = hmode(env, csrno);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    if (env->debugger) {
        return RISCV_EXCP_NONE;
    }

    if (env->priv < PRV_M) {
        if (!(env->mstateen[csrno - base] & SMSTATEEN_STATEEN)) {
            return RISCV_EXCP_ILLEGAL_INST;
        }
    }

    return RISCV_EXCP_NONE;
}

static RISCVException hstateen(CPURISCVState *env, int csrno)
{
    return hstateen_pred(env, csrno, CSR_HSTATEEN0);
}

static RISCVException hstateenh(CPURISCVState *env, int csrno)
{
    return hstateen_pred(env, csrno, CSR_HSTATEEN0H);
}

static RISCVException sstateen(CPURISCVState *env, int csrno)
{
    bool virt = env->virt_enabled;
    int index = csrno - CSR_SSTATEEN0;

    if (!riscv_cpu_cfg(env)->ext_smstateen) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    RISCVException ret = smode(env, csrno);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    if (env->debugger) {
        return RISCV_EXCP_NONE;
    }

    if (env->priv < PRV_M) {
        if (!(env->mstateen[index] & SMSTATEEN_STATEEN)) {
            return RISCV_EXCP_ILLEGAL_INST;
        }

        if (virt) {
            if (!(env->hstateen[index] & SMSTATEEN_STATEEN)) {
                return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
        }
    }

    return RISCV_EXCP_NONE;
}

static RISCVException sstc(CPURISCVState *env, int csrno)
{
    bool hmode_check = false;

    if (!riscv_cpu_cfg(env)->ext_sstc) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if ((csrno == CSR_VSTIMECMP) || (csrno == CSR_VSTIMECMPH)) {
        hmode_check = true;
    }

    RISCVException ret = hmode_check ? hmode(env, csrno) : smode(env, csrno);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    if (env->debugger) {
        return RISCV_EXCP_NONE;
    }

    if (env->priv == PRV_M) {
        return RISCV_EXCP_NONE;
    }

    /*
     * Check rdtime_fn after debugger check to allow GDB registration
     * during CPU realize before rdtime_fn is initialized by board code.
     */
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    /*
     * No need of separate function for rv32 as menvcfg stores both menvcfg
     * menvcfgh for RV32.
     */
    if (!(get_field(env->mcounteren, COUNTEREN_TM) &&
          get_field(env->menvcfg, MENVCFG_STCE))) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (env->virt_enabled) {
        if (!(get_field(env->hcounteren, COUNTEREN_TM) &&
              get_field(env->henvcfg, HENVCFG_STCE))) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    return RISCV_EXCP_NONE;
}

static RISCVException sstc_32(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return sstc(env, csrno);
}

static RISCVException satp(CPURISCVState *env, int csrno)
{
    if (env->priv == PRV_S && !env->virt_enabled &&
        get_field(env->mstatus, MSTATUS_TVM)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (env->priv == PRV_S && env->virt_enabled &&
        get_field(env->hstatus, HSTATUS_VTVM)) {
        return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
    }

    return smode(env, csrno);
}

static RISCVException hgatp(CPURISCVState *env, int csrno)
{
    if (env->priv == PRV_S && !env->virt_enabled &&
        get_field(env->mstatus, MSTATUS_TVM)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return hmode(env, csrno);
}

/*
 * M-mode:
 * Without ext_smctr raise illegal inst excep.
 * Otherwise everything is accessible to m-mode.
 *
 * S-mode:
 * Without ext_ssctr or mstateen.ctr raise illegal inst excep.
 * Otherwise everything other than mctrctl is accessible.
 *
 * VS-mode:
 * Without ext_ssctr or mstateen.ctr raise illegal inst excep.
 * Without hstateen.ctr raise virtual illegal inst excep.
 * Otherwise allow vsctrctl, sctrstatus, 0x200-0x2ff entry range.
 * Always raise illegal instruction exception for sctrdepth.
 */
static RISCVException ctr_mmode(CPURISCVState *env, int csrno)
{
    /* Check if smctr-ext is present */
    if (riscv_cpu_cfg(env)->ext_smctr) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException ctr_smode(CPURISCVState *env, int csrno)
{
    if ((env->priv == PRV_M && riscv_cpu_cfg(env)->ext_smctr) ||
        (env->priv == PRV_S && !env->virt_enabled &&
         riscv_cpu_cfg(env)->ext_ssctr)) {
        return smstateen_acc_ok(env, 0, SMSTATEEN0_CTR);
    }

    if (env->priv == PRV_S && env->virt_enabled &&
        riscv_cpu_cfg(env)->ext_ssctr) {
        if (csrno == CSR_SCTRSTATUS) {
            return smstateen_acc_ok(env, 0, SMSTATEEN0_CTR);
        }

        return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException ctr_vsmode(CPURISCVState *env, int csrno)
{
    if (env->priv == PRV_S && env->virt_enabled &&
        riscv_cpu_cfg(env)->ext_ssctr) {
        return smstateen_acc_ok(env, 0, SMSTATEEN0_CTR);
    }

    return ctr_smode(env, csrno);
}

static RISCVException aia_hmode(CPURISCVState *env, int csrno)
{
    int ret;

    if (!riscv_cpu_cfg(env)->ext_ssaia) {
        return RISCV_EXCP_ILLEGAL_INST;
     }

    if (csrno == CSR_VSTOPEI) {
        ret = smstateen_acc_ok(env, 0, SMSTATEEN0_IMSIC);
    } else {
        ret = smstateen_acc_ok(env, 0, SMSTATEEN0_AIA);
    }

    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    return hmode(env, csrno);
}

static RISCVException aia_hmode32(CPURISCVState *env, int csrno)
{
    int ret;

    if (!riscv_cpu_cfg(env)->ext_ssaia) {
        return RISCV_EXCP_ILLEGAL_INST;
     }

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_AIA);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    if (!riscv_cpu_cfg(env)->ext_ssaia) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return hmode32(env, csrno);
}

static RISCVException dbltrp_hmode(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_ssdbltrp) {
        return RISCV_EXCP_NONE;
    }

    return hmode(env, csrno);
}

static RISCVException pmp(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->pmp) {
        if (csrno <= CSR_PMPCFG15) {
            uint32_t reg_index = csrno - CSR_PMPCFG0;

            /* TODO: RV128 restriction check */
            if ((reg_index & 1) && (riscv_cpu_mxl(env) == MXL_RV64)) {
                return RISCV_EXCP_ILLEGAL_INST;
            }
        }

        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException smpmpdeleg(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_smpmpdeleg) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException have_mseccfg(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_smepmp) {
        return RISCV_EXCP_NONE;
    }
    if (riscv_cpu_cfg(env)->ext_zkr) {
        return RISCV_EXCP_NONE;
    }
    if (riscv_cpu_cfg(env)->ext_smmpm) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException smsdid(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_smsdid) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException debug(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->debug) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException sctx(CPURISCVState *env, int csrno)
{
    RISCVException ret = smstateen_acc_ok(env, 0, SMSTATEEN0_HSCONTXT);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }
    return debug(env, csrno);
}

static RISCVException mcachelock(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_xtheadcacherpl) {
        return RISCV_EXCP_NONE;
    }
    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException scachelock(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_xtheadcacherpl) {
        return smode(env, csrno);
    }
    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException ucachelock(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_xtheadcacherpl) {
        return umode(env, csrno);
    }
    return RISCV_EXCP_ILLEGAL_INST;
}
#endif

static RISCVException seed(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_zkr) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

#if !defined(CONFIG_USER_ONLY)
    if (env->debugger) {
        return RISCV_EXCP_NONE;
    }

    /*
     * With a CSR read-write instruction:
     * 1) The seed CSR is always available in machine mode as normal.
     * 2) Attempted access to seed from virtual modes VS and VU always raises
     * an exception(virtual instruction exception only if mseccfg.sseed=1).
     * 3) Without the corresponding access control bit set to 1, any attempted
     * access to seed from U, S or HS modes will raise an illegal instruction
     * exception.
     */
    if (env->priv == PRV_M) {
        return RISCV_EXCP_NONE;
    } else if (env->virt_enabled) {
        if (env->mseccfg & MSECCFG_SSEED) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        } else {
            return RISCV_EXCP_ILLEGAL_INST;
        }
    } else {
        if (env->priv == PRV_S && (env->mseccfg & MSECCFG_SSEED)) {
            return RISCV_EXCP_NONE;
        } else if (env->priv == PRV_U && (env->mseccfg & MSECCFG_USEED)) {
            return RISCV_EXCP_NONE;
        } else {
            return RISCV_EXCP_ILLEGAL_INST;
        }
    }
#else
    return RISCV_EXCP_NONE;
#endif
}

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
    if (riscv_has_ext(env, RVF)) {
        env->mstatus |= MSTATUS_FS;
    }
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
    if (riscv_has_ext(env, RVF)) {
        env->mstatus |= MSTATUS_FS;
    }
#endif
    env->frm = val & (FSR_RD >> FSR_RD_SHIFT);
    return RISCV_EXCP_NONE;
}

RISCVException read_fcsr(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = (riscv_cpu_get_fflags(env) << FSR_AEXC_SHIFT)
        | (env->frm << FSR_RD_SHIFT);
    return RISCV_EXCP_NONE;
}

RISCVException write_fcsr(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (riscv_has_ext(env, RVF)) {
        env->mstatus |= MSTATUS_FS;
    }
#endif
    env->frm = (val & FSR_RD) >> FSR_RD_SHIFT;
    riscv_cpu_set_fflags(env, (val & FSR_AEXC) >> FSR_AEXC_SHIFT);
    return RISCV_EXCP_NONE;
}

static
RISCVException read_fxcr(CPURISCVState *env, int csrno, target_ulong *val)
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

RISCVException read_vtype(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    uint64_t vill;
    switch (env->xl) {
    case MXL_RV32:
        vill = (uint32_t)env->vill << 31;
        break;
    case MXL_RV64:
        vill = (uint64_t)env->vill << 63;
        break;
    default:
        g_assert_not_reached();
    }
    *val = (target_ulong)vill | env->vtype;
    return RISCV_EXCP_NONE;
}

RISCVException read_vl(CPURISCVState *env, int csrno,
                              target_ulong *val)
{
    *val = env->vl;
    return RISCV_EXCP_NONE;
}

RISCVException read_vlenb(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = riscv_cpu_cfg(env)->vlenb;
    return RISCV_EXCP_NONE;
}

RISCVException read_vxrm(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = env->vxrm;
    return RISCV_EXCP_NONE;
}

RISCVException write_vxrm(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    env->mstatus |= MSTATUS_VS;
#endif
    env->vxrm = val;
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
    *val = env->mcsr | (env->mxsat << 2) | (riscv_cpu_get_mfflags(env) << 3);
    return RISCV_EXCP_NONE;
}
static RISCVException write_xmcsr(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->mcsr = val & (MCSR_RM | MCSR_SAT | MCSR_FFLAGS | MCSR_FRM | MCSR_XMSATEN);
    env->mxrm = get_field(val, MCSR_RM);
    env->mfrm = get_field(val, MCSR_FRM);
    env->mxsat = get_field(val, MCSR_SAT);
    env->xmsaten = get_field(val, MCSR_XMSATEN);
    riscv_cpu_set_mfflags(env, get_field(val, MCSR_FFLAGS));
    riscv_cpu_set_mfrm(env, get_field(val, MCSR_FRM));
    riscv_cpu_set_xmsaten(env, env->xmsaten);
#if !defined(CONFIG_USER_ONLY)
    if (!(env->mxstatus & MXSTATUS_MSD)) {
        env->mstatus |= MSTATUS_TH_MS;
    }
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmxrm(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->mxrm;
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmxrm(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!(env->mxstatus & MXSTATUS_MSD)) {
        env->mstatus |= MSTATUS_TH_MS;
    }
#endif
    env->mxrm = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmsat(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->mxsat;
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmsat(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!(env->mxstatus & MXSTATUS_MSD)) {
        env->mstatus |= MSTATUS_TH_MS;
    }
#endif
    env->mxsat = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmxadesc(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->ma_blksize | (env->ma_colidx << CSR_XMXADESC_COLIDX_SHIFT);
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmxadesc(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!(env->mxstatus & MXSTATUS_MSD)) {
        env->mstatus |= MSTATUS_TH_MS;
    }
#endif
    env->ma_blksize = val & CSR_XMXADESC_BLOCKSIZE;
    env->ma_colidx = (val >> CSR_XMXADESC_COLIDX_SHIFT) & CSR_XMXADESC_COLIDX;
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmxbdesc(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->mb_blksize | (env->mb_colidx << CSR_XMXADESC_COLIDX_SHIFT);
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmxbdesc(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!(env->mxstatus & MXSTATUS_MSD)) {
        env->mstatus |= MSTATUS_TH_MS;
    }
#endif
    env->mb_blksize = val & CSR_XMXADESC_BLOCKSIZE;
    env->mb_colidx = (val >> CSR_XMXADESC_COLIDX_SHIFT) & CSR_XMXADESC_COLIDX;
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmfflags(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = riscv_cpu_get_mfflags(env);
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmfflags(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!(env->mxstatus & MXSTATUS_MSD)) {
        env->mstatus |= MSTATUS_TH_MS;
    }
#endif
    riscv_cpu_set_mfflags(env, val & (MCSR_FFLAGS >> MCSR_FFLAGS_SHIFT));
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmfrm(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->mfrm;
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmfrm(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!(env->mxstatus & MXSTATUS_MSD)) {
        env->mstatus |= MSTATUS_TH_MS;
    }
#endif
    env->mfrm = val & (MCSR_FRM >> MCSR_FRM_SHIFT);
    riscv_cpu_set_mfrm(env, env->mfrm);
    return RISCV_EXCP_NONE;
}

static RISCVException read_xmsaten(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->xmsaten;
    return RISCV_EXCP_NONE;
}

static RISCVException write_xmsaten(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!(env->mxstatus & MXSTATUS_MSD)) {
        env->mstatus |= MSTATUS_TH_MS;
    }
#endif
    env->xmsaten = val & (MCSR_XMSATEN >> MCSR_XMSATEN_SHIFT);
    riscv_cpu_set_xmsaten(env, env->xmsaten);
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
#if !defined(CONFIG_USER_ONLY)
    if (!(env->mxstatus & MXSTATUS_MSD)) {
        env->mstatus |= MSTATUS_TH_MS;
    }
#endif
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

RISCVException read_vxsat(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->vxsat;
    return RISCV_EXCP_NONE;
}

RISCVException write_vxsat(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    env->mstatus |= MSTATUS_VS;
#endif
    env->vxsat = val;
    return RISCV_EXCP_NONE;
}

RISCVException read_vstart(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->vstart;
    return RISCV_EXCP_NONE;
}

RISCVException write_vstart(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    env->mstatus |= MSTATUS_VS;
#endif
    /*
     * The vstart CSR is defined to have only enough writable bits
     * to hold the largest element index, i.e. lg2(VLEN) bits.
     */
    env->vstart = val & ~(~0ULL << ctzl(riscv_cpu_cfg(env)->vlenb << 3));
    return RISCV_EXCP_NONE;
}

RISCVException read_vcsr(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = (env->vxrm << VCSR_VXRM_SHIFT) | (env->vxsat << VCSR_VXSAT_SHIFT);
    return RISCV_EXCP_NONE;
}

RISCVException write_vcsr(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    env->mstatus |= MSTATUS_VS;
#endif
    env->vxrm = (val & VCSR_VXRM) >> VCSR_VXRM_SHIFT;
    env->vxsat = (val & VCSR_VXSAT) >> VCSR_VXSAT_SHIFT;
    return RISCV_EXCP_NONE;
}

#if defined(CONFIG_USER_ONLY)
/* User Timers and Counters */
static target_ulong get_ticks(bool shift)
{
    int64_t val = cpu_get_host_ticks();
    target_ulong result = shift ? val >> 32 : val;

    return result;
}

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

static RISCVException read_hpmcounter(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = get_ticks(false);
    return RISCV_EXCP_NONE;
}

static RISCVException read_hpmcounterh(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    *val = get_ticks(true);
    return RISCV_EXCP_NONE;
}

#else /* CONFIG_USER_ONLY */

static RISCVException read_mcyclecfg(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    *val = env->mcyclecfg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mcyclecfg(CPURISCVState *env, int csrno,
                                      target_ulong val)
{
    env->mcyclecfg = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mcyclecfgh(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = env->mcyclecfgh;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mcyclecfgh(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    env->mcyclecfgh = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_minstretcfg(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    *val = env->minstretcfg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_minstretcfg(CPURISCVState *env, int csrno,
                                        target_ulong val)
{
    env->minstretcfg = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_minstretcfgh(CPURISCVState *env, int csrno,
                                        target_ulong *val)
{
    *val = env->minstretcfgh;
    return RISCV_EXCP_NONE;
}

static RISCVException write_minstretcfgh(CPURISCVState *env, int csrno,
                                         target_ulong val)
{
    env->minstretcfgh = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mhpmevent(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    int evt_index = csrno - CSR_MCOUNTINHIBIT;

    *val = env->mhpmevent_val[evt_index];

    return RISCV_EXCP_NONE;
}

static RISCVException write_mhpmevent(CPURISCVState *env, int csrno,
                                      target_ulong val)
{
    int evt_index = csrno - CSR_MCOUNTINHIBIT;
    uint64_t mhpmevt_val = val;

    env->mhpmevent_val[evt_index] = val;

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        mhpmevt_val = mhpmevt_val |
                      ((uint64_t)env->mhpmeventh_val[evt_index] << 32);
    }
    riscv_pmu_update_event_map(env, mhpmevt_val, evt_index);

    return RISCV_EXCP_NONE;
}

static RISCVException read_mhpmeventh(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    int evt_index = csrno - CSR_MHPMEVENT3H + 3;

    *val = env->mhpmeventh_val[evt_index];

    return RISCV_EXCP_NONE;
}

static RISCVException write_mhpmeventh(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    int evt_index = csrno - CSR_MHPMEVENT3H + 3;
    uint64_t mhpmevth_val = val;
    uint64_t mhpmevt_val = env->mhpmevent_val[evt_index];

    mhpmevt_val = mhpmevt_val | (mhpmevth_val << 32);
    env->mhpmeventh_val[evt_index] = val;

    riscv_pmu_update_event_map(env, mhpmevt_val, evt_index);

    return RISCV_EXCP_NONE;
}

static target_ulong riscv_pmu_ctr_get_fixed_counters_val(CPURISCVState *env,
                                                         int counter_idx,
                                                         bool upper_half)
{
    int inst = riscv_pmu_ctr_monitor_instructions(env, counter_idx);
    uint64_t *counter_arr_virt = env->pmu_fixed_ctrs[inst].counter_virt;
    uint64_t *counter_arr = env->pmu_fixed_ctrs[inst].counter;
    target_ulong result = 0;
    uint64_t curr_val = 0;
    uint64_t cfg_val = 0;

    if (counter_idx == 0) {
        cfg_val = upper_half ? ((uint64_t)env->mcyclecfgh << 32) :
                  env->mcyclecfg;
    } else if (counter_idx == 2) {
        cfg_val = upper_half ? ((uint64_t)env->minstretcfgh << 32) :
                  env->minstretcfg;
    } else {
        cfg_val = upper_half ?
                  ((uint64_t)env->mhpmeventh_val[counter_idx] << 32) :
                  env->mhpmevent_val[counter_idx];
        cfg_val &= MHPMEVENT_FILTER_MASK;
    }

    if (!cfg_val) {
        if (icount_enabled()) {
            if (inst)
                curr_val = icount_get_raw();
            else
                curr_val = icount_get();
        } else {
            curr_val = cpu_get_host_ticks();
        }

        goto done;
    }

    /* Update counter before reading. */
    riscv_pmu_update_fixed_ctrs(env, env->priv, env->virt_enabled);

    if (!(cfg_val & MCYCLECFG_BIT_MINH)) {
        curr_val += counter_arr[PRV_M];
    }

    if (!(cfg_val & MCYCLECFG_BIT_SINH)) {
        curr_val += counter_arr[PRV_S];
    }

    if (!(cfg_val & MCYCLECFG_BIT_UINH)) {
        curr_val += counter_arr[PRV_U];
    }

    if (!(cfg_val & MCYCLECFG_BIT_VSINH)) {
        curr_val += counter_arr_virt[PRV_S];
    }

    if (!(cfg_val & MCYCLECFG_BIT_VUINH)) {
        curr_val += counter_arr_virt[PRV_U];
    }

done:
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        result = upper_half ? curr_val >> 32 : curr_val;
    } else {
        result = curr_val;
    }

    return result;
}

static RISCVException riscv_pmu_write_ctr(CPURISCVState *env, target_ulong val,
                                          uint32_t ctr_idx)
{
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    uint64_t mhpmctr_val = val;

    counter->mhpmcounter_val = val;
    if (!get_field(env->mcountinhibit, BIT(ctr_idx)) &&
        (riscv_pmu_ctr_monitor_cycles(env, ctr_idx) ||
         riscv_pmu_ctr_monitor_instructions(env, ctr_idx))) {
        counter->mhpmcounter_prev = riscv_pmu_ctr_get_fixed_counters_val(env,
                                                                ctr_idx, false);
        if (ctr_idx > 2) {
            if (riscv_cpu_mxl(env) == MXL_RV32) {
                mhpmctr_val = mhpmctr_val |
                              ((uint64_t)counter->mhpmcounterh_val << 32);
            }
            riscv_pmu_setup_timer(env, mhpmctr_val, ctr_idx);
        }
     } else {
        /* Other counters can keep incrementing from the given value */
        counter->mhpmcounter_prev = val;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException riscv_pmu_write_ctrh(CPURISCVState *env, target_ulong val,
                                          uint32_t ctr_idx)
{
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    uint64_t mhpmctr_val = counter->mhpmcounter_val;
    uint64_t mhpmctrh_val = val;

    counter->mhpmcounterh_val = val;
    mhpmctr_val = mhpmctr_val | (mhpmctrh_val << 32);
    if (!get_field(env->mcountinhibit, BIT(ctr_idx)) &&
        (riscv_pmu_ctr_monitor_cycles(env, ctr_idx) ||
         riscv_pmu_ctr_monitor_instructions(env, ctr_idx))) {
        counter->mhpmcounterh_prev = riscv_pmu_ctr_get_fixed_counters_val(env,
                                                                 ctr_idx, true);
        if (ctr_idx > 2) {
            riscv_pmu_setup_timer(env, mhpmctr_val, ctr_idx);
        }
    } else {
        counter->mhpmcounterh_prev = val;
    }

    return RISCV_EXCP_NONE;
}

static int write_mhpmcounter(CPURISCVState *env, int csrno, target_ulong val)
{
    int ctr_idx = csrno - CSR_MCYCLE;

    return riscv_pmu_write_ctr(env, val, ctr_idx);
}

static int write_mhpmcounterh(CPURISCVState *env, int csrno, target_ulong val)
{
    int ctr_idx = csrno - CSR_MCYCLEH;

    return riscv_pmu_write_ctrh(env, val, ctr_idx);
}

RISCVException riscv_pmu_read_ctr(CPURISCVState *env, target_ulong *val,
                                         bool upper_half, uint32_t ctr_idx)
{
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    target_ulong ctr_prev = upper_half ? counter->mhpmcounterh_prev :
                                         counter->mhpmcounter_prev;
    target_ulong ctr_val = upper_half ? counter->mhpmcounterh_val :
                                        counter->mhpmcounter_val;

    if (get_field(env->mcountinhibit, BIT(ctr_idx))) {
        /*
         * Counter should not increment if inhibit bit is set. Just return the
         * current counter value.
         */
         *val = ctr_val;
         return RISCV_EXCP_NONE;
    }

    /*
     * The kernel computes the perf delta by subtracting the current value from
     * the value it initialized previously (ctr_val).
     */
    if (riscv_pmu_ctr_monitor_cycles(env, ctr_idx) ||
        riscv_pmu_ctr_monitor_instructions(env, ctr_idx)) {
        *val = riscv_pmu_ctr_get_fixed_counters_val(env, ctr_idx, upper_half) -
                                                    ctr_prev + ctr_val;
    } else {
        *val = ctr_val;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_hpmcounter(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    uint16_t ctr_index;

    if (csrno >= CSR_MCYCLE && csrno <= CSR_MHPMCOUNTER31) {
        ctr_index = csrno - CSR_MCYCLE;
    } else if (csrno >= CSR_CYCLE && csrno <= CSR_HPMCOUNTER31) {
        ctr_index = csrno - CSR_CYCLE;
    } else {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return riscv_pmu_read_ctr(env, val, false, ctr_index);
}

static RISCVException read_hpmcounterh(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    uint16_t ctr_index;

    if (csrno >= CSR_MCYCLEH && csrno <= CSR_MHPMCOUNTER31H) {
        ctr_index = csrno - CSR_MCYCLEH;
    } else if (csrno >= CSR_CYCLEH && csrno <= CSR_HPMCOUNTER31H) {
        ctr_index = csrno - CSR_CYCLEH;
    } else {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return riscv_pmu_read_ctr(env, val, true, ctr_index);
}

static int rmw_cd_mhpmcounter(CPURISCVState *env, int ctr_idx,
                              target_ulong *val, target_ulong new_val,
                              target_ulong wr_mask)
{
    if (wr_mask != 0 && wr_mask != -1) {
        return -EINVAL;
    }

    if (!wr_mask && val) {
        riscv_pmu_read_ctr(env, val, false, ctr_idx);
    } else if (wr_mask) {
        riscv_pmu_write_ctr(env, new_val, ctr_idx);
    } else {
        return -EINVAL;
    }

    return 0;
}

static int rmw_cd_mhpmcounterh(CPURISCVState *env, int ctr_idx,
                               target_ulong *val, target_ulong new_val,
                               target_ulong wr_mask)
{
    if (wr_mask != 0 && wr_mask != -1) {
        return -EINVAL;
    }

    if (!wr_mask && val) {
        riscv_pmu_read_ctr(env, val, true, ctr_idx);
    } else if (wr_mask) {
        riscv_pmu_write_ctrh(env, new_val, ctr_idx);
    } else {
        return -EINVAL;
    }

    return 0;
}

static int rmw_cd_mhpmevent(CPURISCVState *env, int evt_index,
                            target_ulong *val, target_ulong new_val,
                            target_ulong wr_mask)
{
    uint64_t mhpmevt_val = new_val;

    if (wr_mask != 0 && wr_mask != -1) {
        return -EINVAL;
    }

    if (!wr_mask && val) {
        *val = env->mhpmevent_val[evt_index];
        if (riscv_cpu_cfg(env)->ext_sscofpmf) {
            *val &= ~MHPMEVENT_BIT_MINH;
        }
    } else if (wr_mask) {
        wr_mask &= ~MHPMEVENT_BIT_MINH;
        mhpmevt_val = (new_val & wr_mask) |
                      (env->mhpmevent_val[evt_index] & ~wr_mask);
        if (riscv_cpu_mxl(env) == MXL_RV32) {
            mhpmevt_val = mhpmevt_val |
                          ((uint64_t)env->mhpmeventh_val[evt_index] << 32);
        }
        env->mhpmevent_val[evt_index] = mhpmevt_val;
        riscv_pmu_update_event_map(env, mhpmevt_val, evt_index);
    } else {
        return -EINVAL;
    }

    return 0;
}

static int rmw_cd_mhpmeventh(CPURISCVState *env, int evt_index,
                             target_ulong *val, target_ulong new_val,
                             target_ulong wr_mask)
{
    uint64_t mhpmevth_val;
    uint64_t mhpmevt_val = env->mhpmevent_val[evt_index];

    if (wr_mask != 0 && wr_mask != -1) {
        return -EINVAL;
    }

    if (!wr_mask && val) {
        *val = env->mhpmeventh_val[evt_index];
        if (riscv_cpu_cfg(env)->ext_sscofpmf) {
            *val &= ~MHPMEVENTH_BIT_MINH;
        }
    } else if (wr_mask) {
        wr_mask &= ~MHPMEVENTH_BIT_MINH;
        env->mhpmeventh_val[evt_index] =
            (new_val & wr_mask) | (env->mhpmeventh_val[evt_index] & ~wr_mask);
        mhpmevth_val = env->mhpmeventh_val[evt_index];
        mhpmevt_val = mhpmevt_val | (mhpmevth_val << 32);
        riscv_pmu_update_event_map(env, mhpmevt_val, evt_index);
    } else {
        return -EINVAL;
    }

    return 0;
}

static int rmw_cd_ctr_cfg(CPURISCVState *env, int cfg_index, target_ulong *val,
                            target_ulong new_val, target_ulong wr_mask)
{
    switch (cfg_index) {
    case 0:             /* CYCLECFG */
        if (wr_mask) {
            wr_mask &= ~MCYCLECFG_BIT_MINH;
            env->mcyclecfg = (new_val & wr_mask) | (env->mcyclecfg & ~wr_mask);
        } else {
            *val = env->mcyclecfg &= ~MHPMEVENTH_BIT_MINH;
        }
        break;
    case 2:             /* INSTRETCFG */
        if (wr_mask) {
            wr_mask &= ~MINSTRETCFG_BIT_MINH;
            env->minstretcfg = (new_val & wr_mask) |
                               (env->minstretcfg & ~wr_mask);
        } else {
            *val = env->minstretcfg &= ~MHPMEVENTH_BIT_MINH;
        }
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static int rmw_cd_ctr_cfgh(CPURISCVState *env, int cfg_index, target_ulong *val,
                            target_ulong new_val, target_ulong wr_mask)
{

    if (riscv_cpu_mxl(env) != MXL_RV32) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    switch (cfg_index) {
    case 0:         /* CYCLECFGH */
        if (wr_mask) {
            wr_mask &= ~MCYCLECFGH_BIT_MINH;
            env->mcyclecfgh = (new_val & wr_mask) |
                              (env->mcyclecfgh & ~wr_mask);
        } else {
            *val = env->mcyclecfgh;
        }
        break;
    case 2:          /* INSTRETCFGH */
        if (wr_mask) {
            wr_mask &= ~MINSTRETCFGH_BIT_MINH;
            env->minstretcfgh = (new_val & wr_mask) |
                                (env->minstretcfgh & ~wr_mask);
        } else {
            *val = env->minstretcfgh;
        }
        break;
    default:
        return -EINVAL;
    }
    return 0;
}


static RISCVException read_scountovf(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    int mhpmevt_start = CSR_MHPMEVENT3 - CSR_MCOUNTINHIBIT;
    int i;
    *val = 0;
    target_ulong *mhpm_evt_val;
    uint64_t of_bit_mask;

    /* Virtualize scountovf for counter delegation */
    if (riscv_cpu_cfg(env)->ext_sscofpmf &&
        riscv_cpu_cfg(env)->ext_ssccfg &&
        get_field(env->menvcfg, MENVCFG_CDE) &&
        env->virt_enabled) {
        return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
    }

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        mhpm_evt_val = env->mhpmeventh_val;
        of_bit_mask = MHPMEVENTH_BIT_OF;
    } else {
        mhpm_evt_val = env->mhpmevent_val;
        of_bit_mask = MHPMEVENT_BIT_OF;
    }

    for (i = mhpmevt_start; i < RV_MAX_MHPMEVENTS; i++) {
        if ((get_field(env->mcounteren, BIT(i))) &&
            (mhpm_evt_val[i] & of_bit_mask)) {
                    *val |= BIT(i);
            }
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_time(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    uint64_t delta = env->virt_enabled ? env->htimedelta : 0;
    assert(!env->virt_enabled || !env->xt_vmid_en);
    delta = env->xt_vmid_en ? env->xt_mtimedelta : delta;

    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->rdtime_fn(env->rdtime_fn_arg) + delta;
    return RISCV_EXCP_NONE;
}

static RISCVException read_timeh(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    uint64_t delta = env->virt_enabled ? env->htimedelta : 0;
    assert(!env->virt_enabled || !env->xt_vmid_en);
    delta = env->xt_vmid_en ? env->xt_mtimedelta : delta;

    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = (env->rdtime_fn(env->rdtime_fn_arg) + delta) >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vstimecmp(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    *val = env->vstimecmp;

    return RISCV_EXCP_NONE;
}

static RISCVException read_vstimecmph(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = env->vstimecmp >> 32;

    return RISCV_EXCP_NONE;
}

static RISCVException write_vstimecmp(CPURISCVState *env, int csrno,
                                      target_ulong val)
{
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        env->vstimecmp = deposit64(env->vstimecmp, 0, 32, (uint64_t)val);
    } else {
        env->vstimecmp = val;
    }

    riscv_timer_write_timecmp(env, env->vstimer, env->vstimecmp,
                              env->htimedelta, MIP_VSTIP);

    return RISCV_EXCP_NONE;
}

static RISCVException write_vstimecmph(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    env->vstimecmp = deposit64(env->vstimecmp, 32, 32, (uint64_t)val);
    riscv_timer_write_timecmp(env, env->vstimer, env->vstimecmp,
                              env->htimedelta, MIP_VSTIP);

    return RISCV_EXCP_NONE;
}

static RISCVException read_stimecmp(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    if (env->virt_enabled) {
        *val = env->vstimecmp;
    } else {
        *val = env->stimecmp;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_stimecmph(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    if (env->virt_enabled) {
        *val = env->vstimecmp >> 32;
    } else {
        *val = env->stimecmp >> 32;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException write_stimecmp(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    if (env->virt_enabled) {
        if (env->hvictl & HVICTL_VTI) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        return write_vstimecmp(env, csrno, val);
    }

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        env->stimecmp = deposit64(env->stimecmp, 0, 32, (uint64_t)val);
    } else {
        env->stimecmp = val;
    }
    if (env->xt_vmid_en) {
        riscv_timer_write_timecmp(env, env->stimer, env->stimecmp,
                                  env->xt_mtimedelta, MIP_STIP);
    } else {
        riscv_timer_write_timecmp(env, env->stimer, env->stimecmp, 0, MIP_STIP);
    }

    return RISCV_EXCP_NONE;
}

static RISCVException write_stimecmph(CPURISCVState *env, int csrno,
                                      target_ulong val)
{
    if (env->virt_enabled) {
        if (env->hvictl & HVICTL_VTI) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        return write_vstimecmph(env, csrno, val);
    }

    env->stimecmp = deposit64(env->stimecmp, 32, 32, (uint64_t)val);
    if (env->xt_vmid_en) {
        riscv_timer_write_timecmp(env, env->stimer, env->stimecmp,
                                  env->xt_mtimedelta, MIP_STIP);
    } else {
        riscv_timer_write_timecmp(env, env->stimer, env->stimecmp, 0, MIP_STIP);
    }
    return RISCV_EXCP_NONE;
}

#define VSTOPI_NUM_SRCS 5

#define LOCAL_INTERRUPTS (~(target_ulong)0x1FFF)

static const uint64_t delegable_ints =
    S_MODE_INTERRUPTS | VS_MODE_INTERRUPTS | MIP_LCOFIP | MIP_TPE_DMA;
static const uint64_t vs_delegable_ints =
    VS_MODE_INTERRUPTS | LOCAL_INTERRUPTS;
static const uint64_t all_ints = M_MODE_INTERRUPTS | S_MODE_INTERRUPTS |
                                     HS_MODE_INTERRUPTS | LOCAL_INTERRUPTS;
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
                         (1ULL << (RISCV_EXCP_SW_CHECK)) | \
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
    SSTATUS_SUM | SSTATUS_MXR | SSTATUS_VS | SSTATUS_SDT;

/*
 * Spec allows for bits 13:63 to be either read-only or writable.
 * So far we have interrupt LCOFIP in that region which is writable.
 *
 * Also, spec allows to inject virtual interrupts in this region even
 * without any hardware interrupts for that interrupt number.
 *
 * For now interrupt in 13:63 region are all kept writable. 13 being
 * LCOFIP and 14:63 being virtual only. Change this in future if we
 * introduce more interrupts that are not writable.
 */

/* Bit STIP can be an alias of mip.STIP that's why it's writable in mvip. */
static const target_ulong mvip_writable_mask = MIP_SSIP | MIP_STIP | MIP_SEIP |
                                    LOCAL_INTERRUPTS;
static const target_ulong mvien_writable_mask = MIP_SSIP | MIP_SEIP |
                                    LOCAL_INTERRUPTS;

static const target_ulong sip_writable_mask = SIP_SSIP | LOCAL_INTERRUPTS;
static const target_ulong hip_writable_mask = MIP_VSSIP;
static const target_ulong hvip_writable_mask = MIP_VSSIP | MIP_VSTIP |
                                    MIP_VSEIP | LOCAL_INTERRUPTS;
static const target_ulong hvien_writable_mask = LOCAL_INTERRUPTS;

static const target_ulong vsip_writable_mask = MIP_VSSIP | LOCAL_INTERRUPTS;

const bool valid_vm_1_10_32[16] = {
    [VM_1_10_MBARE] = true,
    [VM_1_10_SV32] = true
};

const bool valid_vm_1_10_64[16] = {
    [VM_1_10_MBARE] = true,
    [VM_1_10_SV39] = true,
    [VM_1_10_SV48] = true,
    [VM_1_10_SV57] = true
};

/* Machine Information Registers */
static RISCVException read_zero(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException write_ignore(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    return RISCV_EXCP_NONE;
}

static RISCVException read_mvendorid(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    *val = riscv_cpu_cfg(env)->mvendorid;
    return RISCV_EXCP_NONE;
}

static RISCVException read_marchid(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = riscv_cpu_cfg(env)->marchid;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mimpid(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = riscv_cpu_cfg(env)->mimpid;
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
    if ((status & MSTATUS_FS) == MSTATUS_FS ||
        (status & MSTATUS_VS) == MSTATUS_VS ||
        (status & MSTATUS_XS) == MSTATUS_XS ||
        (riscv_cpu_cfg(env)->ext_matrix &&
         !(env->mxstatus & MXSTATUS_MSD) &&
         ((status & MSTATUS_TH_MS) == MSTATUS_TH_MS))) {
        switch (xl) {
        case MXL_RV32:
            return status | MSTATUS32_SD;
        case MXL_RV64:
            return status | MSTATUS64_SD;
        case MXL_RV128:
            return MSTATUSH128_SD;
        default:
            g_assert_not_reached();
        }
    }
    return status;
}

RISCVException read_mstatus(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = add_status_sd(env, riscv_cpu_mxl(env), env->mstatus);
    return RISCV_EXCP_NONE;
}

static bool validate_vm(CPURISCVState *env, target_ulong vm)
{
    uint64_t mode_supported = riscv_cpu_cfg(env)->satp_mode.map;
    return get_field(mode_supported, (1 << vm));
}

static target_ulong legalize_xatp(CPURISCVState *env, target_ulong old_xatp,
                                  target_ulong val)
{
    target_ulong mask;
    bool vm;
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        vm = validate_vm(env, get_field(val, SATP32_MODE));
        mask = (val ^ old_xatp) & (SATP32_MODE | SATP32_ASID | SATP32_PPN);
    } else {
        vm = validate_vm(env, get_field(val, SATP64_MODE));
        mask = (val ^ old_xatp) & (SATP64_MODE | SATP64_ASID | SATP64_PPN);
    }

    if (vm && mask) {
        /*
         * The ISA defines SATP.MODE=Bare as "no translation", but we still
         * pass these through QEMU's TLB emulation as it improves
         * performance.  Flushing the TLB on SATP writes with paging
         * enabled avoids leaking those invalid cached mappings.
         */
        tlb_flush(env_cpu(env));
        return val;
    }
    return old_xatp;
}

static target_ulong legalize_mpp(CPURISCVState *env, target_ulong old_mpp,
                                 target_ulong val)
{
    bool valid = false;
    target_ulong new_mpp = get_field(val, MSTATUS_MPP);

    switch (new_mpp) {
    case PRV_M:
        valid = true;
        break;
    case PRV_S:
        valid = riscv_has_ext(env, RVS);
        break;
    case PRV_U:
        valid = riscv_has_ext(env, RVU);
        break;
    }

    /* Remain field unchanged if new_mpp value is invalid */
    if (!valid) {
        val = set_field(val, MSTATUS_MPP, old_mpp);
    }

    return val;
}

RISCVException write_mstatus(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    const RISCVCPUConfig *cfg = riscv_cpu_cfg(env);
    uint64_t mstatus = env->mstatus;
    uint64_t mask = 0;
    RISCVMXL xl = riscv_cpu_mxl(env);

    /*
     * MPP field have been made WARL since priv version 1.11. However,
     * legalization for it will not break any software running on 1.10.
     */
    val = legalize_mpp(env, get_field(mstatus, MSTATUS_MPP), val);

    /* flush tlb on mstatus fields that affect VM */
    if ((val ^ mstatus) & MSTATUS_MXR) {
        tlb_flush(env_cpu(env));
    }
    mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
        MSTATUS_SPP | MSTATUS_MPRV | MSTATUS_SUM |
        MSTATUS_MPP | MSTATUS_MXR | MSTATUS_TVM | MSTATUS_TSR |
        MSTATUS_TW;

    if (riscv_has_ext(env, RVF)) {
        mask |= MSTATUS_FS;
    }
    if (riscv_has_ext(env, RVV) || riscv_cpu_cfg(env)->ext_xtheadvector) {
        mask |= MSTATUS_VS;
    }

    if (!(env->mxstatus & MXSTATUS_MSD) && riscv_cpu_cfg(env)->ext_matrix) {
        mask |= MSTATUS_TH_MS;
    }
    if (riscv_env_smode_dbltrp_enabled(env)) {
        mask |= MSTATUS_SDT;
        if ((val & MSTATUS_SDT) != 0) {
            mask &= ~MSTATUS_SIE;
        }
    }

    if (cfg->ext_smdbltrp) {
        mask |= MSTATUS_MDT;
        if ((val & MSTATUS_MDT) != 0)
            mask &= ~MSTATUS_MIE;
    }

    if (xl != MXL_RV32 || env->debugger) {
        if (riscv_has_ext(env, RVH)) {
            mask |= MSTATUS_MPV | MSTATUS_GVA;
        }
        if ((val & MSTATUS64_UXL) != 0) {
            mask |= MSTATUS64_UXL;
        }
    }

    if (riscv_cpu_cfg(env)->ext_zicfilp) {
        mask |= MSTATUS_SPELP | MSTATUS_MPELP;
    }

    mstatus = (mstatus & ~mask) | (val & mask);

    env->mstatus = mstatus;

    /*
     * Except in debug mode, UXL/SXL can only be modified by higher
     * privilege mode. So xl will not be changed in normal mode.
     */
    if (env->debugger) {
        env->xl = cpu_recompute_xl(env);
    }
    bool lock = bql_locked();
    if (!lock) {
        bql_lock();
    }
    riscv_clic_get_next_interrupt(env);
    if (!lock) {
        bql_unlock();
    }

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
    uint64_t mask = riscv_has_ext(env, RVH) ? MSTATUS_MPV | MSTATUS_GVA : 0;

    if (riscv_cpu_cfg(env)->ext_smdbltrp) {
        mask |= MSTATUS_MDT;
        if ((val & MSTATUS_MDT) != 0)
            mask |= MSTATUS_MIE;
    }
    if (riscv_cpu_cfg(env)->ext_zicfilp) {
        mask |= MSTATUS_MPELP;
    }
    env->mstatus = (env->mstatus & ~mask) | (valh & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException read_mstatus_i128(CPURISCVState *env, int csrno,
                                        Int128 *val)
{
    *val = int128_make128(env->mstatus, add_status_sd(env, MXL_RV128,
                                                      env->mstatus));
    return RISCV_EXCP_NONE;
}

static RISCVException read_misa_i128(CPURISCVState *env, int csrno,
                                     Int128 *val)
{
    uint32_t misa_val = env->misa_ext;
    if (riscv_cpu_cfg(env)->ext_xtheadvector) {
        misa_val |= RVV;
    }
    *val = int128_make128(misa_val, (uint64_t)MXL_RV128 << 62);
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
    if (riscv_cpu_cfg(env)->ext_xtheadvector) {
        *val |= RVV;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_misa(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint32_t orig_misa_ext = env->misa_ext;
    Error *local_err = NULL;

    if (!riscv_cpu_cfg(env)->misa_w) {
        /* drop write to misa */
        return RISCV_EXCP_NONE;
    }

    /* Mask extensions that are not supported by this hart */
    val &= env->misa_ext_mask;

    /*
     * Suppress 'C' if next instruction is not aligned
     * TODO: this should check next_pc
     */
    if ((val & RVC) && (GETPC() & ~3) != 0) {
        val &= ~RVC;
    }

    /* Disable RVG if any of its dependencies are disabled */
    if (!(val & RVI && val & RVM && val & RVA &&
          val & RVF && val & RVD)) {
        val &= ~RVG;
    }

    /* If nothing changed, do nothing. */
    if (val == env->misa_ext) {
        return RISCV_EXCP_NONE;
    }

    env->misa_ext = val;
    riscv_cpu_validate_set_extensions(cpu, &local_err);
    if (local_err != NULL) {
        /* Rollback on validation error */
        qemu_log_mask(LOG_GUEST_ERROR, "Unable to write MISA ext value "
                      "0x%x, keeping existing MISA ext 0x%x\n",
                      env->misa_ext, orig_misa_ext);

        env->misa_ext = orig_misa_ext;

        return RISCV_EXCP_NONE;
    }

    if (!(env->misa_ext & RVF)) {
        env->mstatus &= ~MSTATUS_FS;
    }

    /* flush translation cache */
    tb_flush(env_cpu(env));
    env->xl = riscv_cpu_mxl(env);
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

static RISCVException rmw_mideleg64(CPURISCVState *env, int csrno,
                                    uint64_t *ret_val,
                                    uint64_t new_val, uint64_t wr_mask)
{
    uint64_t mask = wr_mask & delegable_ints;

    if (ret_val) {
        *ret_val = env->mideleg;
    }

    env->mideleg = (env->mideleg & ~mask) | (new_val & mask);

    if (riscv_has_ext(env, RVH)) {
        env->mideleg |= HS_MODE_INTERRUPTS;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mideleg(CPURISCVState *env, int csrno,
                                  target_ulong *ret_val,
                                  target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mideleg64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_midelegh(CPURISCVState *env, int csrno,
                                   target_ulong *ret_val,
                                   target_ulong new_val,
                                   target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mideleg64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

static RISCVException rmw_mie64(CPURISCVState *env, int csrno,
                                uint64_t *ret_val,
                                uint64_t new_val, uint64_t wr_mask)
{
    uint64_t mask = wr_mask & all_ints;
    bool clic = xt_clic_is_clic_mode(env) || xt_clic_v0p10_is_clic_mode(env);

    if (ret_val) {
        /* The xie CSR appears hardwired to zero in CLIC mode, (Section 4.3) */
        *ret_val = clic ? 0 : env->mie;
    }

    /* Writes to xie will be ignored and will not trap. (Section 4.3) */
    if (clic) {
        return RISCV_EXCP_NONE;
    }
    env->mie = (env->mie & ~mask) | (new_val & mask);

    if (!riscv_has_ext(env, RVH)) {
        env->mie &= ~((uint64_t)HS_MODE_INTERRUPTS);
    }

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mie(CPURISCVState *env, int csrno,
                              target_ulong *ret_val,
                              target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mie64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_mieh(CPURISCVState *env, int csrno,
                               target_ulong *ret_val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mie64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

static RISCVException rmw_mvien64(CPURISCVState *env, int csrno,
                                uint64_t *ret_val,
                                uint64_t new_val, uint64_t wr_mask)
{
    uint64_t mask = wr_mask & mvien_writable_mask;

    if (ret_val) {
        *ret_val = env->mvien;
    }

    env->mvien = (env->mvien & ~mask) | (new_val & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mvien(CPURISCVState *env, int csrno,
                              target_ulong *ret_val,
                              target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mvien64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_mvienh(CPURISCVState *env, int csrno,
                                target_ulong *ret_val,
                                target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mvien64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

static RISCVException read_mtopi(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    int irq;
    uint8_t iprio;

    irq = riscv_cpu_mirq_pending(env);
    if (irq <= 0 || irq > 63) {
        *val = 0;
    } else {
        iprio = env->miprio[irq];
        if (!iprio) {
            if (riscv_cpu_default_priority(irq) > IPRIO_DEFAULT_M) {
                iprio = IPRIO_MMAXIPRIO;
            }
        }
        *val = (irq & TOPI_IID_MASK) << TOPI_IID_SHIFT;
        *val |= iprio;
    }

    return RISCV_EXCP_NONE;
}

static int aia_xlate_vs_csrno(CPURISCVState *env, int csrno)
{
    if (!env->virt_enabled) {
        return csrno;
    }

    switch (csrno) {
    case CSR_SISELECT:
        return CSR_VSISELECT;
    case CSR_SIREG:
        return CSR_VSIREG;
    case CSR_STOPEI:
        return CSR_VSTOPEI;
    default:
        return csrno;
    };
}

static int sxcsrind_xlate_vs_csrno(CPURISCVState *env, int csrno)
{
    if (!env->virt_enabled) {
        return csrno;
    }

    switch (csrno) {
    case CSR_SISELECT:
        return CSR_VSISELECT;
    case CSR_SIREG:
    case CSR_SIREG2:
    case CSR_SIREG3:
    case CSR_SIREG4:
    case CSR_SIREG5:
    case CSR_SIREG6:
        return CSR_VSIREG + (csrno - CSR_SIREG);
    default:
        return csrno;
    };
}

static RISCVException rmw_xiselect(CPURISCVState *env, int csrno,
                                   target_ulong *val, target_ulong new_val,
                                   target_ulong wr_mask)
{
    target_ulong *iselect;
    int ret;

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_CSRIND);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /* Translate CSR number for VS-mode */
    csrno = sxcsrind_xlate_vs_csrno(env, csrno);

    /* Find the iselect CSR based on CSR number */
    switch (csrno) {
    case CSR_MISELECT:
        iselect = &env->miselect;
        break;
    case CSR_SISELECT:
        iselect = &env->siselect;
        break;
    case CSR_VSISELECT:
        iselect = &env->vsiselect;
        break;
    default:
         return RISCV_EXCP_ILLEGAL_INST;
    };

    if (val) {
        *val = *iselect;
    }
    if (!riscv_cpu_cfg(env)->ext_smcsrind && !riscv_cpu_cfg(env)->ext_sscsrind) {
        wr_mask &= ISELECT_MASK_AIA;
    }

    if (wr_mask) {
        *iselect = (*iselect & ~wr_mask) | (new_val & wr_mask);
    }

    return RISCV_EXCP_NONE;
}

static bool xiselect_aia_range(target_ulong isel)
{
    return (ISELECT_IPRIO0 <= isel && isel <= ISELECT_IPRIO15) ||
           (ISELECT_IMSIC_FIRST <= isel && isel <= ISELECT_IMSIC_LAST);
}

static bool xiselect_cd_range(target_ulong isel)
{
    return (ISELECT_CD_FIRST <= isel && isel <= ISELECT_CD_LAST);
}

static bool xiselect_ctr_range(target_ulong isel)
{
    return (CTR_ENTRIES_FIRST <= isel && isel <= CTR_ENTRIES_LAST);
}

static bool xiselect_clic_range(target_ulong isel)
{
    return (XT_CLIC_ENTRIES_FIRST <= isel && isel <= XT_CLIC_ENTRIES_LAST);
}

static bool xiselect_spmp_range(target_ulong isel)
{
    return (CSR_SPMPADDR0 <= isel && isel <= XT_CSR_MPMPDELEG);
}

static bool xiselect_mpmpswitch_range(target_ulong isel)
{
    return (isel == CSR_MPMPSWITCH0 || isel == CSR_MPMPSWITCH1);
}

#if defined(TARGET_RISCV64)
static bool xiselect_xt_custom_range(target_ulong isel)
{
    switch (isel)
    {
    case CSR_XT_MHINT3:
    case CSR_XT_MHINT7:
    case CSR_XT_MNASTATUS:
        return true;
    default:
        return false;
    }
}
#else
static bool xiselect_xt_custom_range(target_ulong isel)
{
    return false;
}
#endif

static int rmw_iprio(target_ulong xlen,
                     target_ulong iselect, uint8_t *iprio,
                     target_ulong *val, target_ulong new_val,
                     target_ulong wr_mask, int ext_irq_no)
{
    int i, firq, nirqs;
    target_ulong old_val;

    if (iselect < ISELECT_IPRIO0 || ISELECT_IPRIO15 < iselect) {
        return -EINVAL;
    }
    if (xlen != 32 && iselect & 0x1) {
        return -EINVAL;
    }

    nirqs = 4 * (xlen / 32);
    firq = ((iselect - ISELECT_IPRIO0) / (xlen / 32)) * (nirqs);

    old_val = 0;
    for (i = 0; i < nirqs; i++) {
        old_val |= ((target_ulong)iprio[firq + i]) << (IPRIO_IRQ_BITS * i);
    }

    if (val) {
        *val = old_val;
    }

    if (wr_mask) {
        new_val = (old_val & ~wr_mask) | (new_val & wr_mask);
        for (i = 0; i < nirqs; i++) {
            /*
             * M-level and S-level external IRQ priority always read-only
             * zero. This means default priority order is always preferred
             * for M-level and S-level external IRQs.
             */
            if ((firq + i) == ext_irq_no) {
                continue;
            }
            iprio[firq + i] = (new_val >> (IPRIO_IRQ_BITS * i)) & 0xff;
        }
    }

    return 0;
}

static int rmw_xctrsource(CPURISCVState *env, int isel, target_ulong *val,
                          target_ulong new_val, target_ulong wr_mask)
{
    /*
     * CTR arrays are treated as circular buffers and TOS always points to next
     * empty slot, keeping TOS - 1 always pointing to latest entry. Given entry
     * 0 is always the latest one, traversal is a bit different here. See the
     * below example.
     *
     * Depth = 16.
     *
     * idx    [0] [1] [2] [3] [4] [5] [6] [7] [8] [9] [A] [B] [C] [D] [E] [F]
     * TOS                                 H
     * entry   6   5   4   3   2   1   0   F   E   D   C   B   A   9   8   7
     */
    const uint64_t entry = isel - CTR_ENTRIES_FIRST;
    const uint64_t depth = 16 << get_field(env->sctrdepth, SCTRDEPTH_MASK);
    uint64_t idx;

    /* Entry greater than depth-1 is read-only zero */
    if (entry >= depth) {
        *val = 0;
        return 0;
    }

    idx = get_field(env->sctrstatus, SCTRSTATUS_WRPTR_MASK);
    idx = (idx - entry - 1) & (depth - 1);

    if (val) {
        *val = env->ctr_src[idx];
    }

    env->ctr_src[idx] = (env->ctr_src[idx] & ~wr_mask) | (new_val & wr_mask);

    return 0;
}

static int rmw_xctrtarget(CPURISCVState *env, int isel, target_ulong *val,
                          target_ulong new_val, target_ulong wr_mask)
{
    /*
     * CTR arrays are treated as circular buffers and TOS always points to next
     * empty slot, keeping TOS - 1 always pointing to latest entry. Given entry
     * 0 is always the latest one, traversal is a bit different here. See the
     * below example.
     *
     * Depth = 16.
     *
     * idx    [0] [1] [2] [3] [4] [5] [6] [7] [8] [9] [A] [B] [C] [D] [E] [F]
     * head                                H
     * entry   6   5   4   3   2   1   0   F   E   D   C   B   A   9   8   7
     */
    const uint64_t entry = isel - CTR_ENTRIES_FIRST;
    const uint64_t depth = 16 << get_field(env->sctrdepth, SCTRDEPTH_MASK);
    uint64_t idx;

    /* Entry greater than depth-1 is read-only zero */
    if (entry >= depth) {
        *val = 0;
        return 0;
    }

    idx = get_field(env->sctrstatus, SCTRSTATUS_WRPTR_MASK);
    idx = (idx - entry - 1) & (depth - 1);

    if (val) {
        *val = env->ctr_dst[idx];
    }

    env->ctr_dst[idx] = (env->ctr_dst[idx] & ~wr_mask) | (new_val & wr_mask);

    return 0;
}

static int rmw_xctrdata(CPURISCVState *env, int isel, target_ulong *val,
                        target_ulong new_val, target_ulong wr_mask)
{
    /*
     * CTR arrays are treated as circular buffers and TOS always points to next
     * empty slot, keeping TOS - 1 always pointing to latest entry. Given entry
     * 0 is always the latest one, traversal is a bit different here. See the
     * below example.
     *
     * Depth = 16.
     *
     * idx    [0] [1] [2] [3] [4] [5] [6] [7] [8] [9] [A] [B] [C] [D] [E] [F]
     * head                                H
     * entry   6   5   4   3   2   1   0   F   E   D   C   B   A   9   8   7
     */
    const uint64_t entry = isel - CTR_ENTRIES_FIRST;
    const uint64_t mask = wr_mask & CTRDATA_MASK;
    const uint64_t depth = 16 << get_field(env->sctrdepth, SCTRDEPTH_MASK);
    uint64_t idx;

    /* Entry greater than depth-1 is read-only zero */
    if (entry >= depth) {
        *val = 0;
        return 0;
    }

    idx = get_field(env->sctrstatus, SCTRSTATUS_WRPTR_MASK);
    idx = (idx - entry - 1) & (depth - 1);

    if (val) {
        *val = env->ctr_data[idx];
    }

    env->ctr_data[idx] = (env->ctr_data[idx] & ~mask) | (new_val & mask);

    return 0;
}

static RISCVException rmw_xireg_aia(CPURISCVState *env, int csrno,
                         target_ulong isel, target_ulong *val,
                         target_ulong new_val, target_ulong wr_mask)
{
    bool virt = false, isel_reserved = false;
    int ret = -EINVAL;
    RISCVException check;
    target_ulong priv, vgein;
    uint8_t *iprio;

    check = smstateen_acc_ok(env, 0, SMSTATEEN0_CSRIND);
    if (check != RISCV_EXCP_NONE) {
        return check;
    }
    /* Translate CSR number for VS-mode */
    csrno = aia_xlate_vs_csrno(env, csrno);

    /* VS-mode CSR number passed in has already been translated */
    switch (csrno) {
    case CSR_MIREG:
        if (!riscv_cpu_cfg(env)->ext_smaia) {
            goto done;
        }
        iprio = env->miprio;
        priv = PRV_M;
        break;
    case CSR_SIREG:
        if (!riscv_cpu_cfg(env)->ext_ssaia ||
            (env->priv == PRV_S && env->mvien & MIP_SEIP &&
            env->siselect >= ISELECT_IMSIC_EIDELIVERY &&
            env->siselect <= ISELECT_IMSIC_EIE63)) {
            goto done;
        }
        iprio = env->siprio;
        priv = PRV_S;
        break;
    case CSR_VSIREG:
        if (!riscv_cpu_cfg(env)->ext_ssaia) {
            goto done;
        }
        iprio = env->hviprio;
        priv = PRV_S;
        virt = true;
        break;
    default:
        goto done;
    };

    /* Find the selected guest interrupt file */
    vgein = (virt) ? get_field(env->hstatus, HSTATUS_VGEIN) : 0;

    if (ISELECT_IPRIO0 <= isel && isel <= ISELECT_IPRIO15) {
        /* Local interrupt priority registers not available for VS-mode */
        if (!virt) {
            ret = rmw_iprio(riscv_cpu_mxl_bits(env),
                            isel, iprio, val, new_val, wr_mask,
                            (priv == PRV_M) ? IRQ_M_EXT : IRQ_S_EXT);
        }
    } else if (ISELECT_IMSIC_FIRST <= isel && isel <= ISELECT_IMSIC_LAST) {
        /* IMSIC registers only available when machine implements it. */
        if (env->aia_ireg_rmw_fn[priv]) {
            /* Selected guest interrupt file should not be zero */
            if (virt && (!vgein || env->geilen < vgein)) {
                goto done;
            }
            /* Call machine specific IMSIC register emulation */
            ret = env->aia_ireg_rmw_fn[priv](env->aia_ireg_rmw_fn_arg[priv],
                                    AIA_MAKE_IREG(isel, priv, virt, vgein,
                                                  riscv_cpu_mxl_bits(env)),
                                    val, new_val, wr_mask);
        }
    } else {
        isel_reserved = true;
    }

done:
    /*
     * If AIA is not enabled, illegal instruction exception is always
     * returned regardless of whether we are in VS-mode or not
     */
    if (ret) {
        return (env->virt_enabled && virt && !isel_reserved) ?
               RISCV_EXCP_VIRT_INSTRUCTION_FAULT : RISCV_EXCP_ILLEGAL_INST;
    }

    return RISCV_EXCP_NONE;
}

static int rmw_xireg_cd(CPURISCVState *env, int csrno,
                        target_ulong isel, target_ulong *val,
                        target_ulong new_val, target_ulong wr_mask,
                        bool *shadow_virt)
{
    int ret = -EINVAL;
    int ctr_index = isel - ISELECT_CD_FIRST;
    int isel_hpm_start = ISELECT_CD_FIRST + 3;
    bool virt = (csrno >= CSR_VSIREG) && (csrno <= CSR_VSIREG6);

    if (!riscv_cpu_cfg(env)->ext_smcdeleg || !riscv_cpu_cfg(env)->ext_ssccfg) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    /*
     * An attempt to access any vsireg* from M or S mode raises an illegal
     * instruction exception
     */
    if (virt && ((env->priv == PRV_M) ||
                 ((env->priv == PRV_S) && !env->virt_enabled))) {
        *shadow_virt = true;
        return RISCV_EXCP_ILLEGAL_INST;
    }
    /*
     * An attempt from VS-mode to access any sireg* (really vsireg*)
     * raises an illegal instruction exception if menvcfg.CDE = 0
     */
    if (virt && env->virt_enabled) {
       if (!get_field(env->menvcfg, MENVCFG_CDE)) {
            *shadow_virt = true;
            return RISCV_EXCP_ILLEGAL_INST;
       } else {
            goto done;
       }
    }
    /* sireg rules below */
    /*
     * When the counter selected by siselect is not delegated to S-mode
     * (the corresponding bit in mcounteren = 0)
     */
    if (!get_field(env->mcounteren, BIT(ctr_index))) {
        goto done;
    }
    /* Attempts to access any sireg* when menvcfg.CDE = 0 */
    if (!get_field(env->menvcfg, MENVCFG_CDE)) {
        goto done;
    }

    /* Invalid siselect value for reserved */
    if (ctr_index == 1) {
        goto done;
    }

    /* sireg4 and sireg5 provides access RV32 only CSRs */
    if (((csrno == CSR_SIREG5) || (csrno == CSR_SIREG4)) &&
        (riscv_cpu_mxl(env) != MXL_RV32)) {
        goto done;
    }

    /* Check Sscofpmf dependancy */
    if (!riscv_cpu_cfg(env)->ext_sscofpmf && csrno == CSR_SIREG5 &&
        (isel_hpm_start <= isel && isel <= ISELECT_CD_LAST)) {
        goto done;
    }

    /* Check smcntrpmf dependancy */
    if (!riscv_cpu_cfg(env)->ext_smcntrpmf &&
        (csrno == CSR_SIREG2 || csrno == CSR_SIREG5) &&
        (ISELECT_CD_FIRST <= isel && isel < isel_hpm_start)) {
        goto done;
    }

    switch (csrno) {
    case CSR_SIREG:
        ret = rmw_cd_mhpmcounter(env, ctr_index, val, new_val, wr_mask);
        break;
    case CSR_SIREG4:
        ret = rmw_cd_mhpmcounterh(env, ctr_index, val, new_val, wr_mask);
        break;
    case CSR_SIREG2:
        if (ctr_index <= 2) {
            ret = rmw_cd_ctr_cfg(env, ctr_index, val, new_val, wr_mask);
        } else {
            ret = rmw_cd_mhpmevent(env, ctr_index, val, new_val, wr_mask);
        }
        break;
    case CSR_SIREG5:
        if (ctr_index <= 2) {
            ret = rmw_cd_ctr_cfgh(env, ctr_index, val, new_val, wr_mask);
        } else {
            ret = rmw_cd_mhpmeventh(env, ctr_index, val, new_val, wr_mask);
        }
        break;
    default:
        goto done;
    }

done:
    return ret;
}

static int rmw_xireg_ctr(CPURISCVState *env, int csrno,
                        target_ulong isel, target_ulong *val,
                        target_ulong new_val, target_ulong wr_mask)
{
    bool ext_sxctr = false;
    int ret = -EINVAL;

    if (CSR_MIREG <= csrno && csrno <= CSR_MIREG3) {
        ext_sxctr = riscv_cpu_cfg(env)->ext_smctr;
    } else if (CSR_SIREG <= csrno && csrno <= CSR_SIREG3) {
        ext_sxctr = riscv_cpu_cfg(env)->ext_ssctr;
    } else if (CSR_VSIREG <= csrno && csrno <= CSR_VSIREG3) {
        ext_sxctr = riscv_cpu_cfg(env)->ext_ssctr;
    }

    if (!ext_sxctr) {
        return -EINVAL;
    }

    if (csrno == CSR_MIREG || csrno == CSR_SIREG || csrno == CSR_VSIREG) {
        ret = rmw_xctrsource(env, isel, val, new_val, wr_mask);
    } else if (csrno == CSR_MIREG2 || csrno == CSR_SIREG2 ||
               csrno == CSR_VSIREG2) {
        ret = rmw_xctrtarget(env, isel, val, new_val, wr_mask);
    } else if (csrno == CSR_MIREG3 || csrno == CSR_SIREG3 ||
               csrno == CSR_VSIREG3) {
        ret = rmw_xctrdata(env, isel, val, new_val, wr_mask);
    }

    return ret;
}

static int rmw_xireg_xmtcmcsr(CPURISCVState *env, int csrno,
                              target_ulong *val,
                              target_ulong new_val,
                              target_ulong wr_mask)
{
    uint64_t xmtcmcsr_mask = XMTCMCSR_MCA_MASK | XMTCMCSR_MTCMEN_MASK;
    *val = env->xmtcmcsr & xmtcmcsr_mask;
    env->xmtcmcsr = ((env->xmtcmcsr & ~wr_mask) | (new_val & wr_mask)) &
                    xmtcmcsr_mask;
    return 0;
}

static bool xt_indirect_csr_ignore(int csrno, uint64_t isel)
{
    switch(isel) {
    case CSR_XT_PMACFG0:
    case CSR_XT_PMACFG2:
    case CSR_XT_PMAADDR0_64 ... CSR_XT_PMAADDR15_64:
    case CSR_XT_PMAADDR0_32 ... CSR_XT_PMAADDR31_32:
    case CSR_XT_L3_BADDR:
    case CSR_XT_MHINT4:
    case CSR_XT_MLOCKCNT:
    case CSR_XT_MTWCOUNTER:
        if (csrno == CSR_MIREG) {
            return true;
        }
        break;
    case CSR_XT_MEM_MCTRL:
        if ((csrno == CSR_MIREG) || (csrno == CSR_MIREG2) ||
            (csrno == CSR_MIREG3) || (csrno == CSR_MIREG4)) {
            return true;
        }
        break;
    case CSR_XT_L2_FLUSH:
        if (csrno == CSR_MIREG || csrno == CSR_MIREG2) {
            return true;
        }
        break;
    default:
        return false;
    }
    return false;
}

/*
 * Generic helper to process a packed CLIC register word.
 * It iterates through 4 interrupts, checks permissions, and performs
 * read/write operations using function pointers.
 */
static int process_packed_clic_word(CPURISCVState *env,
                                    uint32_t base_irq,
                                    target_ulong *val, target_ulong new_val,
                                    target_ulong wr_mask, int csrno,
                                    uint8_t (*getter)(void *, int, int),
                                    void (*setter)(void *, int, int, uint8_t))
{
    uint32_t read_val = 0;
    CPUState *cs = env_cpu(env);
    XTCLICV0P10State *clic = env->xt_clic_v0p10;

    /* Read phase */
    for (int j = 0; j < 4; j++) {
        uint32_t irq = base_irq + j;
        bool s_mode_csr = (csrno == CSR_SIREG || csrno == CSR_SIREG2);
        bool can_access = !s_mode_csr ||
                          (xt_clic_v0p10_get_intpriv(clic, cs->cpu_index, irq)
                            <= PRV_S);

        if (can_access) {
            read_val |= (uint32_t)getter(clic, cs->cpu_index, irq) << (j * 8);
        }
    }
    if (val) {
        *val = read_val;
    }
    /* Write phase */
    if (wr_mask) {
        new_val &= wr_mask;
        for (int j = 0; j < 4; j++) {
            uint32_t irq = base_irq + j;
            bool s_mode_csr = (csrno == CSR_SIREG || csrno == CSR_SIREG2);
            bool can_access = !s_mode_csr ||
                              (xt_clic_v0p10_get_intpriv(clic, cs->cpu_index, irq)
                                <= PRV_S);

            if (can_access) {
                uint8_t write_byte = (new_val >> (j * 8)) & 0xFF;
                setter(clic, cs->cpu_index, irq, write_byte);
            }
        }
        xt_clic_v0p10_next_interrupt(clic, cs->cpu_index);
    }
    return 0;
}

/*
 * Handles access to clicintctl and clicintattr registers.
 * These registers are packed, with each 32-bit word controlling 4 interrupts.
 */
static inline int rmw_clic_intctl_attr(CPURISCVState *env, int csrno,
                                       target_ulong isel, target_ulong *val,
                                       target_ulong new_val,
                                       target_ulong wr_mask)
{
    uint32_t i = isel - 0x1000;
    uint32_t base_irq = i * 4;

    if (base_irq + 3 >= CLIC_MAX_IRQS) {
        if (val) {
            *val = 0;
        }
        return -EINVAL;
    }

    if (csrno == CSR_MIREG || csrno == CSR_SIREG) {
        /*
         * Accessing clicintctl via mireg/sireg.
         * Note: S-mode cannot directly access M-mode CSRs. The logic
         * here assumes a CSR read/write function already trapped illegal
         * S-mode access to CSR_MIREG.
         */
        return process_packed_clic_word(env, base_irq, val, new_val, wr_mask,
                                        csrno,
                                        xt_clic_v0p10_get_intctl,
                                        xt_clic_v0p10_update_intctl);
    } else if (csrno == CSR_MIREG2 || csrno == CSR_SIREG2) {
        /* Accessing clicintattr via mireg2/sireg2 */
        return process_packed_clic_word(env, base_irq, val, new_val, wr_mask,
                                        csrno,
                                        xt_clic_v0p10_get_intattr,
                                        xt_clic_v0p10_update_intattr);
    }

    /* Should not be reached if called from the dispatcher correctly */
    if (val) {
        *val = 0;
    }
    return -EINVAL;
}

/*
 * Generic helper to process a 32-bit packed CLIC bitmap (ip or ie).
 * It iterates through 32 interrupts, checks permissions, and performs
 * read/write operations using function pointers.
 */
static int process_packed_clic_bitmap(CPURISCVState *env, int csrno,
                                      uint32_t base_irq,
                                      target_ulong *val, target_ulong new_val,
                                      target_ulong wr_mask,
                                      uint8_t (*getter)(void *, int, int),
                                      void (*setter)(void *, int, int, uint8_t))
{
    uint32_t read_val = 0;
    CPUState *cs = env_cpu(env);
    bool is_s_mode_csr = (csrno == CSR_SIREG || csrno == CSR_SIREG2);
    XTCLICV0P10State *clic = env->xt_clic_v0p10;
    /* Read phase */
    for (int j = 0; j < 32; j++) {
        uint32_t irq = base_irq + j;
        if (irq >= CLIC_MAX_IRQS) {
            break; /* Stop if we exceed the max IRQ number */
        }

        bool can_access = !is_s_mode_csr ||
                          (xt_clic_v0p10_get_intpriv(clic, cs->cpu_index, irq)
                            <= PRV_S);

        if (can_access && getter(clic, cs->cpu_index, irq)) {
            read_val |= (1U << j);
        }
    }
    if (val) {
        *val = read_val;
    }

    /* Write phase */
    if (wr_mask) {
        new_val &= wr_mask;
        for (int j = 0; j < 32; j++) {
            uint32_t irq = base_irq + j;
            if (irq >= CLIC_MAX_IRQS) {
                break;
            }

            bool can_access = !is_s_mode_csr ||
                              (xt_clic_v0p10_get_intpriv(clic, cs->cpu_index, irq)
                                <= PRV_S);

            if (can_access) {
                uint8_t bit_val = (new_val >> j) & 1;
                setter(clic, cs->cpu_index, irq, bit_val);
            }
        }
        xt_clic_v0p10_next_interrupt(clic, cs->cpu_index);
    }

    return 0;
}

/*
 * Specific setter for clicintip, which is only writable for
 * edge-triggered interrupts.
 */
static void xt_clic_update_intip_conditional(void *clic, int hartid, int irq,
                                             uint8_t val)
{
    /* Assumes clicintattr[irq] bit 1 is trig[0] (0=level, 1=edge) */
    bool is_edge_triggered = (xt_clic_v0p10_get_intattr(clic, hartid, irq) & 2);
    if (is_edge_triggered) {
        xt_clic_v0p10_update_intip(clic, hartid, irq, val);
    }
}

/*
 * Handles access to clicintip and clicintie registers.
 * These registers are packed, with each 32-bit word controlling 32 interrupts.
 */
static inline int rmw_clic_intip_ie(CPURISCVState *env, int csrno,
                                    target_ulong isel, target_ulong *val,
                                    target_ulong new_val,
                                    target_ulong wr_mask)
{
    uint32_t i = isel - 0x1400;
    uint32_t base_irq = i * 32;

    if (base_irq >= CLIC_MAX_IRQS) {
        if (val) {
            *val = 0;
        }
        return -EINVAL;
    }

    if (csrno == CSR_MIREG || csrno == CSR_SIREG) {
        /* Accessing clicintip via mireg/sireg */
        return process_packed_clic_bitmap(env, csrno, base_irq, val,
                                          new_val, wr_mask,
                                          xt_clic_v0p10_get_intip,
                                          xt_clic_update_intip_conditional);
    } else if (csrno == CSR_MIREG2 || csrno == CSR_SIREG2) {
        /* Accessing clicintie via mireg2/sireg2 */
        return process_packed_clic_bitmap(env, csrno, base_irq, val,
                                          new_val, wr_mask,
                                          xt_clic_v0p10_get_intie,
                                          xt_clic_v0p10_update_intie);
    }

    /* Should not be reached if called from the dispatcher correctly */
    if (val) {
        *val = 0;
    }
    return -EINVAL;
}

/*
 * Handles access to clicinttrig registers.
 * This is a placeholder as the trigger functionality is often optional.
 */
static inline int rmw_clic_inttrig(CPURISCVState *env, int csrno, target_ulong isel,
                                   target_ulong *val, target_ulong new_val,
                                   target_ulong wr_mask)
{
    uint32_t i = isel - 0x1480;

    if (i >= CLIC_MAX_IRQS) {
        if (val) {
            *val = 0;
        }
        return -EINVAL;
    }

    /*
     * The clicinttrig registers are optional and their implementation
     * can be complex. For now, treat as RAZ/WI (Read-As-Zero, Write-Ignored).
     */
    if (val) {
        *val = 0;
    }
    return 0;
}

/*
 * Handles access to mcliccfg and scliccfg registers.
 * This is only applicable if the smclicconfig extension is implemented.
 */
static inline int rmw_clic_cfg(CPURISCVState *env, int csrno, target_ulong isel,
                               target_ulong *val, target_ulong new_val,
                               target_ulong wr_mask)
{
    CPUState *cs = env_cpu(env);
    XTCLICV0P10State *clic = env->xt_clic_v0p10;

    bool is_s_mode_csr = (csrno == CSR_SIREG);
    if (is_s_mode_csr) {
        if (val) {
            *val = xt_clic_v0p10_get_scliccfg(clic, cs->cpu_index);
        }
        if (wr_mask) {
            new_val &= wr_mask;
            xt_clic_v0p10_update_scliccfg(clic, cs->cpu_index, new_val);
        }
    } else {
        if (val) {
            *val = xt_clic_v0p10_get_mcliccfg(clic, cs->cpu_index);
        }
        if (wr_mask) {
            new_val &= wr_mask;
            /* M-mode writes the full mcliccfg */
            xt_clic_v0p10_update_mcliccfg(clic, cs->cpu_index, new_val);
        }
    }
    return 0;
}

/*
 * Implements read-modify-write for CLIC indirect CSRs (xireg/xireg2).
 * This function acts as a dispatcher based on the value of xiselect.
 *
 * @env:     The CPU state.
 * @csrno:   The CSR number being accessed (e.g., CSR_MIREG).
 * @isel:    The current value of the corresponding xiselect CSR.
 * @val:     Pointer to store the value read from the CSR.
 * @new_val: The new value to be written.
 * @wr_mask: A mask indicating which bits of the CSR are being written.
 *           If zero, this is a read-only operation.
 *
 * Returns 0 on success, or -EINVAL for an invalid access.
 */
static int rmw_xireg_clic(CPURISCVState *env, int csrno,
                          target_ulong isel, target_ulong *val,
                          target_ulong new_val, target_ulong wr_mask)
{
    /*
     * FIXME: If an interrupt 'i' is not present in hardware, the
     * corresponding clicintip[i], clicintie[i], clicintattr[i], and
     * clicintctl[i] should appear as hardwired to zero.
     */

    g_assert(!env->virt_enabled);
    int ret = -EINVAL;
    bool lock = bql_locked();
    if (!lock) {
        bql_lock();
    }

    if (isel >= 0x1000 && isel <= 0x13FF) {
        ret = rmw_clic_intctl_attr(env, csrno, isel, val, new_val, wr_mask);
    } else if (isel >= 0x1400 && isel <= 0x147F) {
        ret = rmw_clic_intip_ie(env, csrno, isel, val, new_val, wr_mask);
    } else if (isel >= 0x1480 && isel <= 0x149F) {
        ret = rmw_clic_inttrig(env, csrno, isel, val, new_val, wr_mask);
    } else if (isel == 0x14A0) {
        ret = rmw_clic_cfg(env, csrno, isel, val, new_val, wr_mask);
    } else {
        /* Access to reserved or unimplemented regions. */
        if (val) {
            *val = 0;
        }
    }
    if (!lock) {
        bql_unlock();
    }
    return ret;
}

/**
 * get_spmpswitch_write_mask - Helper function to calculate the writable bits.
 * @env: A pointer to the CPU state.
 *
 * This function calculates a 64-bit mask representing the writable bits
 * of the spmpswitch register. A bit is writable only if the corresponding
 * SPMP entry is delegated and not locked (L-bit is 0).
 *
 * @return: A 64-bit mask where a '1' indicates a writable bit.
 */
static uint64_t get_spmpswitch_write_mask(CPURISCVState *env)
{
    pmp_table_t *pmp_state = &env->pmp_state;
    uint64_t write_enable_mask = 0;
    int num_spmp_entries = MAX_RISCV_PMPS - pmp_state->spmp_start;

    for (int i = 0; i < num_spmp_entries; i++) {
        /* SPMP entries are re-indexed from 0 from the S-mode perspective. */
        int global_idx = pmp_state->spmp_start + i;

        /* A bit in spmpswitch is read-only if the corresponding L-bit is set. */
        if (!(pmp_state->pmp[global_idx].cfg_reg & PMP_LOCK)) {
            write_enable_mask |= (1ULL << i);
        }
    }
    return write_enable_mask;
}

static int rmw_spmpcfg(CPURISCVState *env, int csrno,
                       target_ulong isel, target_ulong *val,
                       target_ulong new_val, target_ulong wr_mask)
{
    uint32_t reg_index = isel - CSR_SPMPADDR0;
    bool smode = (csrno == CSR_SIREG2);
    pmp_table_t *pmp_state = &env->pmp_state;
    int num_spmp_entries = MAX_RISCV_PMPS - pmp_state->spmp_start;
    int global_idx = pmp_state->spmp_start + reg_index;

    if (reg_index >= num_spmp_entries) {
        if (val) {
            *val = 0;
        }
        return 0;
    }
    if (val) {
        *val = spmpcfg_csr_read(env, global_idx);
    }
    new_val = new_val & wr_mask;
    if (smode) {
        if ((pmp_state->pmp[global_idx].cfg_reg & PMP_LOCK)) {
            return 0;
        }
    }
    if (wr_mask) {
        spmpcfg_csr_write(env, global_idx, new_val);
    }
    return 0;
}

static int rmw_spmpaddr(CPURISCVState *env, int csrno,
                        target_ulong isel, target_ulong *val,
                        target_ulong new_val, target_ulong wr_mask)
{
    target_ulong reg_index = isel - CSR_SPMPADDR0;
    bool smode = (csrno == CSR_SIREG);
    pmp_table_t *pmp_state = &env->pmp_state;
    int num_spmp_entries = MAX_RISCV_PMPS - pmp_state->spmp_start;
    target_ulong global_idx = pmp_state->spmp_start + reg_index;

    if (reg_index >= num_spmp_entries) {
        if (val) {
            *val = 0;
        }
        return 0;
    }
    if (val) {
        *val = spmpaddr_csr_read(env, global_idx);
    }
    new_val = new_val & wr_mask;
    if (smode) {
        if ((pmp_state->pmp[global_idx].cfg_reg & PMP_LOCK)) {
            return 0;
        }
        if (global_idx + 1 < MAX_RISCV_PMPS) {
            uint8_t next_cfg = pmp_state->pmp[global_idx + 1].cfg_reg;
            bool is_next_tor = (pmp_get_a_field(next_cfg) == PMP_AMATCH_TOR);

            if (is_next_tor && (next_cfg & PMP_LOCK)) {
                /*
                 * The next entry is a locked TOR. According to Spec 3.3,
                 * writes to the preceding address register are ignored.
                 */
                return 0;
            }
        }
    }
    if (wr_mask) {
        spmpaddr_csr_write(env, global_idx, new_val);
    }
    return 0;
}

static int rmw_spmp_ireg(CPURISCVState *env, int csrno,
                         target_ulong isel, target_ulong *val,
                         target_ulong new_val, target_ulong wr_mask)
{
    if ((csrno == CSR_SIREG) || (csrno == CSR_MIREG)) {
        rmw_spmpaddr(env, csrno, isel, val, new_val, wr_mask);
    } else if ((csrno >= CSR_SIREG2) && (csrno <= CSR_MIREG2)) {
        rmw_spmpcfg(env, csrno, isel, val, new_val, wr_mask);
    } else {
        return -EINVAL;
    }
    return 0;
}



/**
 * write_spmpswitch - CSR write handler for spmpswitch (low 32 or full 64 bits).
 * @env:  A pointer to the CPU state.
 * @val:  The new value being written to the CSR.
 * @wr_mask:  The mask of bits being written to the CSR.
 *
 * For RV64, this handles the full 64-bit register.
 * For RV32, this handles the lower 32 bits (spmpswitch).
 */
static int write_spmpswitch(CPURISCVState *env, target_ulong val,
                             target_ulong wr_mask, bool is_mmode)
{
    uint64_t old_full_val = env->spmpswitch;
    uint64_t new_full_val;
    uint64_t final_full_val;

    /*
     * M-mode retains write access to all spmpswitch bits even when L=1
     * (spec ch.4: "This restriction does not apply to the execution
     * environment, which retains write access"). S-mode is filtered by
     * the lock mask.
     */
    uint64_t write_mask = (is_mmode ? (uint64_t)-1 :
                           get_spmpswitch_write_mask(env)) & wr_mask;

    if (env->xl == MXL_RV32) {
        /* RV32: We are only modifying the lower 32 bits. */
        uint32_t new_low_32 = (uint32_t)val;
        new_full_val = (old_full_val & 0xFFFFFFFF00000000ULL) | new_low_32;
    } else {
        /* RV64: We are modifying the full 64 bits. */
        new_full_val = val;
    }

    /*
     * Apply the lock-bit constraints: only allow changes to writable bits.
     * - (old_full_val & ~write_mask): Preserves the original value of all
     *   read-only (locked) bits.
     * - (new_full_val & write_mask): Takes the new value for all writable bits.
     */
    final_full_val = (old_full_val & ~write_mask) | (new_full_val & write_mask);

    env->spmpswitch = final_full_val;

    return 0;
}

/**
 * write_spmpswitchh - CSR write handler for spmpswitchh (RV32 only).
 * @env:  A pointer to the CPU state.
 * @val:  The new value being written to the CSR (upper 32 bits).
 * @wr_mask:  The mask of bits being written to the CSR.
 */
static int write_spmpswitchh(CPURISCVState *env, target_ulong val,
                              target_ulong wr_mask, bool is_mmode)
{
    uint64_t old_full_val = env->spmpswitch;
    uint64_t new_full_val;
    uint64_t final_full_val;

    /* Get the 64-bit mask of which bits are fundamentally writable. */
    uint64_t write_mask = (is_mmode ? (uint64_t)-1 :
                           get_spmpswitch_write_mask(env)) &
                          ((uint64_t)wr_mask << 32);

    /* RV32: We are modifying the upper 32 bits. */
    uint32_t new_high_32 = (uint32_t)val;
    new_full_val = (old_full_val & 0x00000000FFFFFFFFULL) |
                   ((uint64_t)new_high_32 << 32);

    /* Apply the lock-bit constraints. */
    final_full_val = (old_full_val & ~write_mask) | (new_full_val & write_mask);

    env->spmpswitch = final_full_val;

    return 0;
}

static int rmw_spmpswitch(CPURISCVState *env, int csrno,
                          target_ulong isel, target_ulong *val,
                          target_ulong new_val, target_ulong wr_mask)
{
    bool rv32 = env->xl == MXL_RV32;
    bool is_mmode = (csrno == CSR_MIREG || csrno == CSR_MIREG2);
    if (csrno == CSR_SIREG || csrno == CSR_MIREG) {
        if (val) {
            if (rv32) {
                *val = (uint32_t)env->spmpswitch;
            } else {
                *val = env->spmpswitch;
            }
        }
        write_spmpswitch(env, new_val, wr_mask, is_mmode);
    } else if (csrno == CSR_SIREG2 || csrno == CSR_MIREG2) {
        if (!rv32) {
            return -EINVAL;
        }
        if (val) {
            *val = (uint32_t)(env->spmpswitch >> 32);
        }
        write_spmpswitchh(env, new_val, wr_mask, is_mmode);
    } else {
        return -EINVAL;
    }
    return 0;
}

/*
 * rmw_mpmpswitch - Indirect register access handler for mpmpswitch0/1.
 *
 * mpmpswitch0 (isel=0x800000c8) controls PMP entries 0-31.
 * mpmpswitch1 (isel=0x800000c9) controls PMP entries 32-63.
 * Only accessible from M-mode (via mireg).
 */
static int rmw_mpmpswitch(CPURISCVState *env, int csrno,
                          target_ulong isel, target_ulong *val,
                          target_ulong new_val, target_ulong wr_mask)
{
    if (!riscv_cpu_cfg(env)->ext_xtheadmpmpswitch ||
        !riscv_cpu_cfg(env)->pmp) {
        return -EINVAL;
    }

    if (csrno != CSR_MIREG) {
        return -EINVAL;
    }

    if (isel == CSR_MPMPSWITCH0) {
        if (val) {
            *val = (uint32_t)env->pmpswitch;
        }
        if (wr_mask) {
            uint32_t old_low = (uint32_t)env->pmpswitch;
            uint32_t new_low = (new_val & wr_mask) | (old_low & ~wr_mask);
            env->pmpswitch = (env->pmpswitch & 0xFFFFFFFF00000000ULL) |
                             new_low;
        }
    } else if (isel == CSR_MPMPSWITCH1) {
        if (val) {
            *val = (uint32_t)(env->pmpswitch >> 32);
        }
        if (wr_mask) {
            uint32_t old_high = (uint32_t)(env->pmpswitch >> 32);
            uint32_t new_high = (new_val & wr_mask) | (old_high & ~wr_mask);
            env->pmpswitch = (env->pmpswitch & 0x00000000FFFFFFFFULL) |
                             ((uint64_t)new_high << 32);
        }
    } else {
        return -EINVAL;
    }

    return 0;
}

static RISCVException write_mpmpdeleg(CPURISCVState *env, int csr,
                                      target_ulong val)
{
    pmp_table_t *s = &env->pmp_state;
    target_ulong pmpnum = val & 0x7f;
    uint32_t locked_pmp = 0;

    for (int i = 0; i < MAX_RISCV_PMPS; i++) {
        if (s->pmp[i].cfg_reg & PMP_LOCK) {
            locked_pmp = i;
            break;
        }
    }
    if (pmpnum <= locked_pmp) {
        return RISCV_EXCP_NONE;
    }

    if (pmpnum > MAX_RISCV_PMPS) {
        pmpnum = MAX_RISCV_PMPS;
    }

    s->spmp_start = pmpnum;
    env->mpmpdeleg = pmpnum;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mpmpdeleg(CPURISCVState *env, int csr,
                                     target_ulong *val)
{
    *val = env->mpmpdeleg;
    return RISCV_EXCP_NONE;
}

static int rmw_mpmpdeleg(CPURISCVState *env, int csrno,
                         target_ulong isel, target_ulong *val,
                         target_ulong new_val, target_ulong wr_mask)
{
    if (csrno == CSR_MIREG) {
        if (val) {
            *val = env->mpmpdeleg;
        }
        write_mpmpdeleg(env, csrno, new_val & wr_mask);
    } else {
        return -EINVAL;
    }
    return 0;
}

static int rmw_xireg_spmp(CPURISCVState *env, int csrno,
                          target_ulong isel, target_ulong *val,
                          target_ulong new_val, target_ulong wr_mask)
{
    if (isel >= CSR_SPMPADDR0 && isel <= CSR_SPMPADDR0 + MAX_RISCV_PMPS - 1) {
        return rmw_spmp_ireg(env, csrno, isel, val, new_val, wr_mask);
    } else if (isel == CSR_SPMPSWITCH) {
        return rmw_spmpswitch(env, csrno, isel, val, new_val, wr_mask);
    } else if (isel == XT_CSR_MPMPDELEG) {
        return rmw_mpmpdeleg(env, csrno, isel, val, new_val, wr_mask);
    } else {
        /* Access to reserved or unimplemented regions. */
        if (val) {
            *val = 0;
        }
        return -EINVAL;
    }
    return 0;
}

/*
 * Handle custom Xuantie indirect CSRs
 */
#if defined(TARGET_RISCV64)
static int rmw_xireg_xuantie_custom(CPURISCVState *env, int csrno,
                                    target_ulong isel, target_ulong *val,
                                    target_ulong new_val, target_ulong wr_mask)
{
    if (csrno == CSR_MIREG) {
        if (isel == CSR_XT_MHINT3) {
            if (val) {
                *val = env->mhint3;
            }
            if (wr_mask) {
                env->mhint3 = (env->mhint3 & ~wr_mask) | (new_val & wr_mask);
            }
            return 0;
        }

        if (isel == CSR_XT_MHINT7) {
            if (val) {
                *val = env->mhint7;
            }
            if (wr_mask) {
                env->mhint7 = (env->mhint7 & ~wr_mask) | (new_val & wr_mask);
            }
            return 0;
        }

        if (isel == CSR_XT_MNASTATUS) {
            if (val) {
                *val = env->mnastatus;
            }
            if (wr_mask) {
                env->mnastatus = (env->mnastatus & ~wr_mask) | (new_val & wr_mask);
            }
            return 0;
        }
    }

    return -EINVAL;
}
#else
static int rmw_xireg_xuantie_custom(CPURISCVState *env, int csrno,
                                    target_ulong isel, target_ulong *val,
                                    target_ulong new_val, target_ulong wr_mask)
{
    return -EINVAL;
}
#endif

/*
 * rmw_xireg_sxcsrind: Perform indirect access to xireg and xireg2-xireg6
 *
 * Perform indirect access to xireg and xireg2-xireg6.
 * This is a generic interface for all xireg CSRs. Apart from AIA, all other
 * extension using sxcsrind should be implemented here.
 */
static int rmw_xireg_sxcsrind(CPURISCVState *env, int csrno,
                              target_ulong isel, target_ulong *val,
                              target_ulong new_val, target_ulong wr_mask)
{
    bool virt = (csrno >= CSR_VSIREG) && (csrno <= CSR_VSIREG6);
    int ret = -EINVAL;
    bool shadow_virt = false;

    if (xiselect_cd_range(isel)) {
        ret = rmw_xireg_cd(env, csrno, isel, val, new_val, wr_mask,
                           &shadow_virt);
    } else if (xiselect_ctr_range(isel)) {
        ret = rmw_xireg_ctr(env, csrno, isel, val, new_val, wr_mask);
    } else if (env->xt_clic_v0p10 && xiselect_clic_range(isel)) {
        ret = rmw_xireg_clic(env, csrno, isel, val, new_val, wr_mask);
    } else if (xiselect_mpmpswitch_range(isel)) {
        ret = rmw_mpmpswitch(env, csrno, isel, val, new_val, wr_mask);
    } else if (xiselect_spmp_range(isel)) {
        ret = rmw_xireg_spmp(env, csrno, isel, val, new_val, wr_mask);
    } else if ((env->xmisa & MATRIX_MDMA) && (isel == (target_ulong)CSR_XMTCMCSR)) {
        ret = rmw_xireg_xmtcmcsr(env, csrno, val, new_val, wr_mask);
    } else if (xiselect_xt_custom_range(isel)) {
        ret = rmw_xireg_xuantie_custom(env, csrno, isel, val, new_val, wr_mask);
    } else if (xt_indirect_csr_ignore(csrno, isel)) {
        ret = 0;
    } else {
        /*
         * As per the specification, access to unimplented region is undefined
         * but recommendation is to raise illegal instruction exception.
         */
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (ret) {
        return (env->virt_enabled && virt && !shadow_virt) ?
               RISCV_EXCP_VIRT_INSTRUCTION_FAULT : RISCV_EXCP_ILLEGAL_INST;
    }

    return RISCV_EXCP_NONE;
}

static int rmw_xiregi(CPURISCVState *env, int csrno, target_ulong *val,
                      target_ulong new_val, target_ulong wr_mask)
{
    bool virt = false;
    int ret = -EINVAL;
    target_ulong isel;

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_CSRIND);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /* Translate CSR number for VS-mode */
    csrno = sxcsrind_xlate_vs_csrno(env, csrno);

    if (CSR_MIREG < csrno && csrno <= CSR_MIREG6 &&
        csrno != CSR_MIREG4 - 1) {
        isel = env->miselect;
    } else if (CSR_SIREG < csrno && csrno <= CSR_SIREG6 &&
               csrno != CSR_SIREG4 - 1) {
        isel = env->siselect;
    } else if (CSR_VSIREG < csrno && csrno <= CSR_VSIREG6 &&
               csrno != CSR_VSIREG4 - 1) {
        isel = env->vsiselect;
        virt = true;
    } else {
        goto done;
    }

    return rmw_xireg_sxcsrind(env, csrno, isel, val, new_val, wr_mask);

done:
    return (env->virt_enabled && virt) ?
           RISCV_EXCP_VIRT_INSTRUCTION_FAULT : RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException rmw_xireg(CPURISCVState *env, int csrno,
                                target_ulong *val, target_ulong new_val,
                                target_ulong wr_mask)
{
    bool virt = false;
    int ret = -EINVAL;
    target_ulong isel;

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_CSRIND);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /* Translate CSR number for VS-mode */
    csrno = sxcsrind_xlate_vs_csrno(env, csrno);

    /* Decode register details from CSR number */
    switch (csrno) {
    case CSR_MIREG:
        isel = env->miselect;
        break;
    case CSR_SIREG:
        isel = env->siselect;
        break;
    case CSR_VSIREG:
        isel = env->vsiselect;
        virt = true;
        break;
    default:
         goto done;
    };

    /*
     * Use the xiselect range to determine actual op on xireg.
     *
     * Since we only checked the existence of AIA or Indirect Access in the
     * predicate, we should check the existence of the exact extension when
     * we get to a specific range and return illegal instruction exception even
     * in VS-mode.
     */
    if (xiselect_aia_range(isel)) {
        return rmw_xireg_aia(env, csrno, isel, val, new_val, wr_mask);
    } else if (riscv_cpu_cfg(env)->ext_smcsrind ||
               riscv_cpu_cfg(env)->ext_sscsrind) {
        return rmw_xireg_sxcsrind(env, csrno, isel, val, new_val, wr_mask);
    } else {
        return RISCV_EXCP_ILLEGAL_INST;
    }

done:
    return (env->virt_enabled && virt) ?
           RISCV_EXCP_VIRT_INSTRUCTION_FAULT : RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException rmw_xtopei(CPURISCVState *env, int csrno,
                                 target_ulong *val, target_ulong new_val,
                                 target_ulong wr_mask)
{
    bool virt;
    int ret = -EINVAL;
    target_ulong priv, vgein;

    /* Translate CSR number for VS-mode */
    csrno = aia_xlate_vs_csrno(env, csrno);

    /* Decode register details from CSR number */
    virt = false;
    switch (csrno) {
    case CSR_MTOPEI:
        priv = PRV_M;
        break;
    case CSR_STOPEI:
        if (env->mvien & MIP_SEIP && env->priv == PRV_S) {
            goto done;
        }
        priv = PRV_S;
        break;
    case CSR_VSTOPEI:
        priv = PRV_S;
        virt = true;
        break;
    default:
        goto done;
    };

    /* IMSIC CSRs only available when machine implements IMSIC. */
    if (!env->aia_ireg_rmw_fn[priv]) {
        goto done;
    }

    /* Find the selected guest interrupt file */
    vgein = (virt) ? get_field(env->hstatus, HSTATUS_VGEIN) : 0;

    /* Selected guest interrupt file should be valid */
    if (virt && (!vgein || env->geilen < vgein)) {
        goto done;
    }

    /* Call machine specific IMSIC register emulation for TOPEI */
    ret = env->aia_ireg_rmw_fn[priv](env->aia_ireg_rmw_fn_arg[priv],
                    AIA_MAKE_IREG(ISELECT_IMSIC_TOPEI, priv, virt, vgein,
                                  riscv_cpu_mxl_bits(env)),
                    val, new_val, wr_mask);

done:
    if (ret) {
        return (env->virt_enabled && virt) ?
               RISCV_EXCP_VIRT_INSTRUCTION_FAULT : RISCV_EXCP_ILLEGAL_INST;
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
     * RISC-V CLIC xtvec Mode Configuration (Specification v0.10, v0.8)
     *
     * mode  submode  PC on interrupt
     * ====  =======  ======================================================
     * 00    xxxx     OBASE               # CLINT direct mode
     * 01    xxxx     OBASE + 4*exccode   # CLINT vectored mode
     * 11    0000     NBASE               # CLIC mode
     * 10    0000     Reserved
     * 1x    yyyy     Reserved
     *
     * where:
     * OBASE = xtvec[XLEN-1:2] << 2   # CLINT mode vector base is at least 4-byte aligned
     * NBASE = xtvec[XLEN-1:6] << 6   # CLIC mode vector base is at least 64-byte aligned
     * x     = any value (don't care)
     * yyyy  = any non-zero value
     */
    target_ulong mode = val & 0b11;
    switch (mode) {
    case 0b00:
    case 0b01:
        env->mtvec = val;
        break;
    case 0b11:
        if (env->xt_clic_v0p8 || env->xt_clic_v0p10) {
            env->mtvec = val & ~0x3c;
        } else {
            error_report("CLIC mode is not supported in this case, please check the isa");
            env->mtvec = (env->mtvec & 0x3) | (val & ~0x3);
        }
        break;
    default:
        /*
         * Mode 0b10 is reserved. According to RISC-V spec, writes with
         * reserved mode values should be ignored.
         * This can happen when OpenSBI sets stvec to the kernel entry
         * address temporarily, where the low bits may be 0b10.
         */
        if (env->xt_clic_v0p8 || env->xt_clic_v0p10) {
            env->mtvec = (env->mtvec & 0x3f) | (val & ~0x3f);
        } else {
            env->mtvec = (env->mtvec & 0x3) | (val & ~0x3);
        }
        break;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_mcountinhibit(CPURISCVState *env, int csrno,
                                         target_ulong *val)
{
    *val = env->mcountinhibit;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mcountinhibit(CPURISCVState *env, int csrno,
                                          target_ulong val)
{
    int cidx;
    PMUCTRState *counter;
    RISCVCPU *cpu = env_archcpu(env);
    uint32_t present_ctrs = cpu->pmu_avail_ctrs | COUNTEREN_CY | COUNTEREN_IR;
    target_ulong updated_ctrs = (env->mcountinhibit ^ val) & present_ctrs;
    uint64_t mhpmctr_val, prev_count, curr_count;

    /* WARL register - disable unavailable counters; TM bit is always 0 */
    env->mcountinhibit = val & present_ctrs;

    /* Check if any other counter is also monitoring cycles/instructions */
    for (cidx = 0; cidx < RV_MAX_MHPMCOUNTERS; cidx++) {
        if (!(updated_ctrs & BIT(cidx)) ||
            (!riscv_pmu_ctr_monitor_cycles(env, cidx) &&
            !riscv_pmu_ctr_monitor_instructions(env, cidx))) {
            continue;
        }

        counter = &env->pmu_ctrs[cidx];

        if (!get_field(env->mcountinhibit, BIT(cidx))) {
            counter->mhpmcounter_prev =
                riscv_pmu_ctr_get_fixed_counters_val(env, cidx, false);
            if (riscv_cpu_mxl(env) == MXL_RV32) {
                counter->mhpmcounterh_prev =
                    riscv_pmu_ctr_get_fixed_counters_val(env, cidx, true);
            }

            if (cidx > 2) {
                mhpmctr_val = counter->mhpmcounter_val;
                if (riscv_cpu_mxl(env) == MXL_RV32) {
                    mhpmctr_val = mhpmctr_val |
                            ((uint64_t)counter->mhpmcounterh_val << 32);
                }
                riscv_pmu_setup_timer(env, mhpmctr_val, cidx);
            }
        } else {
            curr_count = riscv_pmu_ctr_get_fixed_counters_val(env, cidx, false);

            mhpmctr_val = counter->mhpmcounter_val;
            prev_count = counter->mhpmcounter_prev;
            if (riscv_cpu_mxl(env) == MXL_RV32) {
                uint64_t tmp =
                    riscv_pmu_ctr_get_fixed_counters_val(env, cidx, true);

                curr_count = curr_count | (tmp << 32);
                mhpmctr_val = mhpmctr_val |
                    ((uint64_t)counter->mhpmcounterh_val << 32);
                prev_count = prev_count |
                    ((uint64_t)counter->mhpmcounterh_prev << 32);
            }

            /* Adjust the counter for later reads. */
            mhpmctr_val = curr_count - prev_count + mhpmctr_val;
            counter->mhpmcounter_val = mhpmctr_val;
            if (riscv_cpu_mxl(env) == MXL_RV32) {
                counter->mhpmcounterh_val = mhpmctr_val >> 32;
            }
        }
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_scountinhibit(CPURISCVState *env, int csrno,
                                         target_ulong *val)
{
    /* S-mode can only access the bits delegated by M-mode */
    if (!get_field(env->menvcfg, MENVCFG_CDE)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (env->virt_enabled) {
        return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
    }
    *val = env->mcountinhibit & env->mcounteren;
    return RISCV_EXCP_NONE;
}

static RISCVException write_scountinhibit(CPURISCVState *env, int csrno,
                                          target_ulong val)
{
    if (!get_field(env->menvcfg, MENVCFG_CDE)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (env->virt_enabled) {
        return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
    }
    write_mcountinhibit(env, csrno, val & env->mcounteren);
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
    RISCVCPU *cpu = env_archcpu(env);

    /* WARL register - disable unavailable counters */
    env->mcounteren = val & (cpu->pmu_avail_ctrs | COUNTEREN_CY | COUNTEREN_TM |
                             COUNTEREN_IR);
    return RISCV_EXCP_NONE;
}

/* Machine Trap Handling */
static RISCVException read_mscratch_i128(CPURISCVState *env, int csrno,
                                         Int128 *val)
{
    *val = int128_make128(env->mscratch, env->mscratchh);
    return RISCV_EXCP_NONE;
}

static RISCVException write_mscratch_i128(CPURISCVState *env, int csrno,
                                          Int128 val)
{
    env->mscratch = int128_getlo(val);
    env->mscratchh = int128_gethi(val);
    return RISCV_EXCP_NONE;
}

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
    const RISCVCPUConfig *cfg = riscv_cpu_cfg(env);
    uint64_t mask = MENVCFG_FIOM | MENVCFG_CBIE | MENVCFG_CBCFE |
                    MENVCFG_CBZE | MENVCFG_CDE | MENVCFG_DTSO;

    if (riscv_cpu_mxl(env) == MXL_RV64) {
        mask |= (cfg->ext_svpbmt ? MENVCFG_PBMTE : 0) |
                (cfg->ext_sstc ? MENVCFG_STCE : 0) |
                (cfg->ext_svadu ? MENVCFG_ADUE : 0) |
                (cfg->ext_smcdeleg ? MENVCFG_CDE : 0) |
                (cfg->ext_ssdbltrp ? MENVCFG_DTE : 0);
    }

    if (riscv_has_ext(env, RVS)) {
        mask |= (cfg->ext_zicfiss ? MENVCFG_SSE : 0);
        mask |= (cfg->ext_xtheadaioe ? MENVCFG_SUENQ : 0);
    }
    mask |= (cfg->ext_zicfilp ? MENVCFG_LPE : 0);

    /* Update PMM field only if the value is valid according to Zjpm v1.0 */
    if (cfg->ext_smnpm &&
        riscv_cpu_mxl(env) == MXL_RV64 &&
        get_field(val, MENVCFG_PMM) != PMM_FIELD_RESERVED) {
        mask |= MENVCFG_PMM;
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
    const RISCVCPUConfig *cfg = riscv_cpu_cfg(env);
    uint64_t mask = (cfg->ext_svpbmt ? MENVCFG_PBMTE : 0) |
                    (cfg->ext_sstc ? MENVCFG_STCE : 0) |
                    (cfg->ext_svadu ? MENVCFG_ADUE : 0) |
                    (cfg->ext_smcdeleg ? MENVCFG_CDE : 0) |
                    (cfg->ext_ssdbltrp ? MENVCFG_DTE : 0);
    uint64_t valh = (uint64_t)val << 32;

    env->menvcfg = (env->menvcfg & ~mask) | (valh & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException read_senvcfg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    RISCVException ret;
    uint64_t mask = 0;

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_HSENVCFG);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    *val = env->senvcfg & ~mask;
    return RISCV_EXCP_NONE;
}

static RISCVException write_senvcfg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    uint64_t mask = SENVCFG_FIOM | SENVCFG_CBIE | SENVCFG_CBCFE | SENVCFG_CBZE |
                    SENVCFG_DTSO;
    /* Update PMM field only if the value is valid according to Zjpm v1.0 */
    if (riscv_cpu_cfg(env)->ext_ssnpm &&
        riscv_cpu_mxl(env) == MXL_RV64 &&
        get_field(val, SENVCFG_PMM) != PMM_FIELD_RESERVED) {
        mask |= SENVCFG_PMM;
    }
    RISCVException ret;

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_HSENVCFG);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    if (riscv_has_ext(env, RVS) && riscv_cpu_cfg(env)->ext_zicfiss &&
        get_field(env->menvcfg, MENVCFG_SSE) &&
        env->virt_enabled ? get_field(env->henvcfg, HENVCFG_SSE) : true) {
        mask |= SENVCFG_SSE;
    }
    mask |= (riscv_cpu_cfg(env)->ext_zicfilp ? SENVCFG_LPE : 0);

    env->senvcfg = (env->senvcfg & ~mask) | (val & mask);
    return RISCV_EXCP_NONE;
}

static RISCVException read_henvcfg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    RISCVException ret;

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_HSENVCFG);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /*
     * henvcfg.pbmte is read_only 0 when menvcfg.pbmte = 0
     * henvcfg.stce is read_only 0 when menvcfg.stce = 0
     * henvcfg.adue is read_only 0 when menvcfg.adue = 0
     * henvcfg.dte is read_only 0 when menvcfg.dte = 0
     * henvcfg.sse is read_only 0 when menvcfg.sse = 0
     */
    *val = env->henvcfg & (~(HENVCFG_PBMTE | HENVCFG_STCE | HENVCFG_ADUE |
                             HENVCFG_DTE   | HENVCFG_SSE) | env->menvcfg);
    return RISCV_EXCP_NONE;
}

static RISCVException write_henvcfg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    uint64_t mask = HENVCFG_FIOM | HENVCFG_CBIE | HENVCFG_CBCFE | HENVCFG_CBZE |
                    HENVCFG_DTSO;
    uint64_t menvcfg_mask;
    RISCVException ret;

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_HSENVCFG);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    if (riscv_cpu_mxl(env) == MXL_RV64) {
        menvcfg_mask = HENVCFG_PBMTE | HENVCFG_STCE | HENVCFG_ADUE;
        if (riscv_cpu_cfg(env)->ext_ssdbltrp) {
            menvcfg_mask |= HENVCFG_DTE;
        }
        mask |= env->menvcfg & menvcfg_mask;
    }

    if (riscv_cpu_cfg(env)->ext_zicfiss) {
        mask |= env->menvcfg & HENVCFG_SSE;
    }

    if (riscv_cpu_cfg(env)->ext_zicfilp) {
        mask |= env->menvcfg & HENVCFG_LPE;
    }

    if (riscv_cpu_cfg(env)->ext_xtheadaioe) {
        mask |= HENVCFG_SUENQ;
    }

    /* Update PMM field only if the value is valid according to Zjpm v1.0 */
    if (riscv_cpu_cfg(env)->ext_ssnpm &&
        riscv_cpu_mxl(env) == MXL_RV64 &&
        get_field(val, HENVCFG_PMM) != PMM_FIELD_RESERVED) {
        mask |= HENVCFG_PMM;
    }

    env->henvcfg = (env->henvcfg & ~mask) | (val & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException read_henvcfgh(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    RISCVException ret;

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_HSENVCFG);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    *val = (env->henvcfg & (~(HENVCFG_PBMTE | HENVCFG_STCE | HENVCFG_ADUE |
                              HENVCFG_DTE) | env->menvcfg)) >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException write_henvcfgh(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    uint64_t mask = env->menvcfg & (HENVCFG_PBMTE | HENVCFG_STCE |
                                    HENVCFG_ADUE | HENVCFG_DTE);
    uint64_t valh = (uint64_t)val << 32;
    RISCVException ret;

    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_HSENVCFG);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    env->henvcfg = (env->henvcfg & ~mask) | (valh & mask);
    return RISCV_EXCP_NONE;
}

static RISCVException read_mstateen(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->mstateen[csrno - CSR_MSTATEEN0];

    return RISCV_EXCP_NONE;
}

static RISCVException write_mstateen(CPURISCVState *env, int csrno,
                                     uint64_t wr_mask, target_ulong new_val)
{
    uint64_t *reg;

    reg = &env->mstateen[csrno - CSR_MSTATEEN0];
    *reg = (*reg & ~wr_mask) | (new_val & wr_mask);

    return RISCV_EXCP_NONE;
}

static RISCVException write_mstateen0(CPURISCVState *env, int csrno,
                                      target_ulong new_val)
{
    uint64_t wr_mask = 0;

    if (riscv_cpu_mxl(env) != MXL_RV32) {
        wr_mask |= SMSTATEEN_STATEEN;
        if (riscv_has_ext(env, RVS)) {
            wr_mask |= SMSTATEEN0_HSENVCFG;
        }

        if (riscv_cpu_cfg(env)->ext_smctr) {
            wr_mask |= SMSTATEEN0_CTR;
        }

        if (env->priv_ver >= PRIV_VERSION_1_13_0) {
            wr_mask |= SMSTATEEN0_P1P13;
        }

        /*
         * TODO: Do we need to check ssaia as well ? Can we enable ssaia without
         * smaia ?
         */
        if (riscv_cpu_cfg(env)->ext_smaia) {
            wr_mask |= SMSTATEEN0_CSRIND;
        }

        /*
         * As per the AIA specification, SMSTATEEN0_IMSIC is valid only if
         * IMSIC is implemented. However, that information is with MachineState
         * and we can't figure that out in csr.c. Just enable if Smaia is
         * available.
         */
        if (riscv_cpu_cfg(env)->ext_smaia) {
            wr_mask |= (SMSTATEEN0_AIA | SMSTATEEN0_IMSIC);
        }
    }

    if (!riscv_cpu_cfg(env)->ext_zcmt) {
        wr_mask |= SMSTATEEN0_JVT;
    }

    if (!riscv_has_ext(env, RVF)) {
        wr_mask |= SMSTATEEN0_FCSR;
    }

    return write_mstateen(env, csrno, wr_mask, new_val);
}

static RISCVException write_mstateen_1_3(CPURISCVState *env, int csrno,
                                         target_ulong new_val)
{
    return write_mstateen(env, csrno, SMSTATEEN_STATEEN, new_val);
}

static RISCVException read_mstateenh(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    *val = env->mstateen[csrno - CSR_MSTATEEN0H] >> 32;

    return RISCV_EXCP_NONE;
}

static RISCVException write_mstateenh(CPURISCVState *env, int csrno,
                                      uint64_t wr_mask, target_ulong new_val)
{
    uint64_t *reg, val;

    reg = &env->mstateen[csrno - CSR_MSTATEEN0H];
    val = (uint64_t)new_val << 32;
    val |= *reg & 0xFFFFFFFF;
    *reg = (*reg & ~wr_mask) | (val & wr_mask);

    return RISCV_EXCP_NONE;
}

static RISCVException write_mstateen0h(CPURISCVState *env, int csrno,
                                       target_ulong new_val)
{
    uint64_t wr_mask = SMSTATEEN_STATEEN;
    if (riscv_has_ext(env, RVS)) {
        wr_mask |= SMSTATEEN0_HSENVCFG;
    }

    if (riscv_cpu_cfg(env)->ext_smctr) {
        wr_mask |= SMSTATEEN0_CTR;
    }

    if (env->priv_ver >= PRIV_VERSION_1_13_0) {
        wr_mask |= SMSTATEEN0_P1P13;
    }

    /*
     * TODO: Do we need to check ssaia as well ? Can we enable ssaia without
     * smaia ?
     */
    if (riscv_cpu_cfg(env)->ext_smaia) {
        wr_mask |= SMSTATEEN0_CSRIND;
    }

    /*
     * As per the AIA specification, SMSTATEEN0_IMSIC is valid only if IMSIC is
     * implemented. However, that information is with MachineState and we can't
     * figure that out in csr.c. Just enable if Smaia is available.
     */
    if (riscv_cpu_cfg(env)->ext_smaia) {
        wr_mask |= (SMSTATEEN0_AIA | SMSTATEEN0_IMSIC);
    }

    return write_mstateenh(env, csrno, wr_mask, new_val);
}

static RISCVException write_mstateenh_1_3(CPURISCVState *env, int csrno,
                                          target_ulong new_val)
{
    return write_mstateenh(env, csrno, SMSTATEEN_STATEEN, new_val);
}

static RISCVException read_hstateen(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    int index = csrno - CSR_HSTATEEN0;

    *val = env->hstateen[index] & env->mstateen[index];

    return RISCV_EXCP_NONE;
}

static RISCVException write_hstateen(CPURISCVState *env, int csrno,
                                     uint64_t mask, target_ulong new_val)
{
    int index = csrno - CSR_HSTATEEN0;
    uint64_t *reg, wr_mask;

    reg = &env->hstateen[index];
    wr_mask = env->mstateen[index] & mask;
    *reg = (*reg & ~wr_mask) | (new_val & wr_mask);

    return RISCV_EXCP_NONE;
}

static RISCVException write_hstateen0(CPURISCVState *env, int csrno,
                                      target_ulong new_val)
{
    uint64_t wr_mask = 0;

    if (riscv_cpu_mxl(env) != MXL_RV32) {
        wr_mask = SMSTATEEN_STATEEN;
        if (riscv_has_ext(env, RVS)) {
            wr_mask |= SMSTATEEN0_HSENVCFG;
        }

        if (riscv_cpu_cfg(env)->ext_smctr) {
            wr_mask |= SMSTATEEN0_CTR;
        }

        if (riscv_cpu_cfg(env)->ext_ssaia) {
            wr_mask |= SMSTATEEN0_CSRIND;
        }

        /*
         * As per the AIA specification, SMSTATEEN0_IMSIC is valid only if
         * IMSIC is implemented. However, that information is with MachineState
         * and we can't figure that out in csr.c. Just enable if Ssaia is
         * available.
         */
        if (riscv_cpu_cfg(env)->ext_ssaia) {
            wr_mask |= (SMSTATEEN0_AIA | SMSTATEEN0_IMSIC);
        }
    }
    if (!riscv_cpu_cfg(env)->ext_zcmt) {
        wr_mask |= SMSTATEEN0_JVT;
    }

    if (!riscv_has_ext(env, RVF)) {
        wr_mask |= SMSTATEEN0_FCSR;
    }
    return write_hstateen(env, csrno, wr_mask, new_val);
}

static RISCVException write_hstateen_1_3(CPURISCVState *env, int csrno,
                                         target_ulong new_val)
{
    return write_hstateen(env, csrno, SMSTATEEN_STATEEN, new_val);
}

static RISCVException read_hstateenh(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    int index = csrno - CSR_HSTATEEN0H;

    *val = (env->hstateen[index] >> 32) & (env->mstateen[index] >> 32);

    return RISCV_EXCP_NONE;
}

static RISCVException write_hstateenh(CPURISCVState *env, int csrno,
                                      uint64_t mask, target_ulong new_val)
{
    int index = csrno - CSR_HSTATEEN0H;
    uint64_t *reg, wr_mask, val;

    reg = &env->hstateen[index];
    val = (uint64_t)new_val << 32;
    val |= *reg & 0xFFFFFFFF;
    wr_mask = env->mstateen[index] & mask;
    *reg = (*reg & ~wr_mask) | (val & wr_mask);

    return RISCV_EXCP_NONE;
}

static RISCVException write_hstateen0h(CPURISCVState *env, int csrno,
                                       target_ulong new_val)
{
    uint64_t wr_mask = SMSTATEEN_STATEEN;
    if (riscv_has_ext(env, RVS)) {
        wr_mask |= SMSTATEEN0_HSENVCFG;
    }

    if (riscv_cpu_cfg(env)->ext_smctr) {
        wr_mask |= SMSTATEEN0_CTR;
    }

    if (riscv_cpu_cfg(env)->ext_ssaia) {
        wr_mask |= SMSTATEEN0_CSRIND;
    }

    /*
     * As per the AIA specification, SMSTATEEN0_IMSIC is valid only if IMSIC is
     * implemented. However, that information is with MachineState and we can't
     * figure that out in csr.c. Just enable if Ssaia is available.
     */
    if (riscv_cpu_cfg(env)->ext_ssaia) {
        wr_mask |= (SMSTATEEN0_AIA | SMSTATEEN0_IMSIC);
    }

    return write_hstateenh(env, csrno, wr_mask, new_val);
}

static RISCVException write_hstateenh_1_3(CPURISCVState *env, int csrno,
                                          target_ulong new_val)
{
    return write_hstateenh(env, csrno, SMSTATEEN_STATEEN, new_val);
}

static RISCVException read_sstateen(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    bool virt = env->virt_enabled;
    int index = csrno - CSR_SSTATEEN0;

    *val = env->sstateen[index] & env->mstateen[index];
    if (virt) {
        *val &= env->hstateen[index];
    }

    return RISCV_EXCP_NONE;
}

static RISCVException write_sstateen(CPURISCVState *env, int csrno,
                                     uint64_t mask, target_ulong new_val)
{
    bool virt = env->virt_enabled;
    int index = csrno - CSR_SSTATEEN0;
    uint64_t wr_mask;
    uint64_t *reg;

    wr_mask = env->mstateen[index] & mask;
    if (virt) {
        wr_mask &= env->hstateen[index];
    }

    reg = &env->sstateen[index];
    *reg = (*reg & ~wr_mask) | (new_val & wr_mask);

    return RISCV_EXCP_NONE;
}

static RISCVException write_sstateen0(CPURISCVState *env, int csrno,
                                      target_ulong new_val)
{
    uint64_t wr_mask = 0;

    if (!riscv_cpu_cfg(env)->ext_zcmt) {
        wr_mask |= SMSTATEEN0_JVT;
    }
    if (!riscv_has_ext(env, RVF)) {
        wr_mask |= SMSTATEEN0_FCSR;
    }

    return write_sstateen(env, csrno, wr_mask, new_val);
}

static RISCVException write_sstateen_1_3(CPURISCVState *env, int csrno,
                                      target_ulong new_val)
{
    return write_sstateen(env, csrno, SMSTATEEN_STATEEN, new_val);
}

static RISCVException rmw_mip64(CPURISCVState *env, int csrno,
                                uint64_t *ret_val,
                                uint64_t new_val, uint64_t wr_mask)
{
    uint64_t old_mip, mask = wr_mask & delegable_ints;
    uint32_t gin;
    bool clic = xt_clic_is_clic_mode(env) || xt_clic_v0p10_is_clic_mode(env);

    /* The xip CSR appears hardwired to zero in CLIC mode. (Section 4.3) */
    if (clic) {
        if (ret_val) {
            *ret_val = 0;
        }
        return RISCV_EXCP_NONE;
    }
    if (mask & MIP_SEIP) {
        env->software_seip = new_val & MIP_SEIP;
        new_val |= env->external_seip * MIP_SEIP;
    }

    if (riscv_cpu_cfg(env)->ext_sstc && (env->priv == PRV_M) &&
        get_field(env->menvcfg, MENVCFG_STCE)) {
        /* sstc extension forbids STIP & VSTIP to be writeable in mip */
        mask = mask & ~(MIP_STIP | MIP_VSTIP);
    }

    if (mask) {
        old_mip = riscv_cpu_update_mip(env, mask, (new_val & mask));
    } else {
        old_mip = env->mip;
    }

    if (csrno != CSR_HVIP) {
        gin = get_field(env->hstatus, HSTATUS_VGEIN);
        old_mip |= (env->hgeip & ((target_ulong)1 << gin)) ? MIP_VSEIP : 0;
        old_mip |= env->vstime_irq ? MIP_VSTIP : 0;
    }

    if (ret_val) {
        *ret_val = old_mip;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mip(CPURISCVState *env, int csrno,
                              target_ulong *ret_val,
                              target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mip64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_miph(CPURISCVState *env, int csrno,
                               target_ulong *ret_val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mip64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

/*
 * The function is written for two use-cases:
 * 1- To access mvip csr as is for m-mode access.
 * 2- To access sip as a combination of mip and mvip for s-mode.
 *
 * Both report bits 1, 5, 9 and 13:63 but with the exception of
 * STIP being read-only zero in case of mvip when sstc extension
 * is present.
 * Also, sip needs to be read-only zero when both mideleg[i] and
 * mvien[i] are zero but mvip needs to be an alias of mip.
 */
static RISCVException rmw_mvip64(CPURISCVState *env, int csrno,
                                uint64_t *ret_val,
                                uint64_t new_val, uint64_t wr_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    target_ulong ret_mip = 0;
    RISCVException ret;
    uint64_t old_mvip;

    /*
     * mideleg[i]  mvien[i]
     *   0           0      No delegation. mvip[i] is alias of mip[i].
     *   0           1      mvip[i] becomes source of interrupt, mip bypassed.
     *   1           X      mip[i] is source of interrupt and mvip[i] aliases
     *                      mip[i].
     *
     *   So alias condition would be for bits:
     *      ((S_MODE_INTERRUPTS | LOCAL_INTERRUPTS) & (mideleg | ~mvien)) |
     *          (!sstc & MIP_STIP)
     *
     *   Non-alias condition will be for bits:
     *      (S_MODE_INTERRUPTS | LOCAL_INTERRUPTS) & (~mideleg & mvien)
     *
     *  alias_mask denotes the bits that come from mip nalias_mask denotes bits
     *  that come from hvip.
     */
    uint64_t alias_mask = ((S_MODE_INTERRUPTS | LOCAL_INTERRUPTS) &
        (env->mideleg | ~env->mvien)) | MIP_STIP;
    uint64_t nalias_mask = (S_MODE_INTERRUPTS | LOCAL_INTERRUPTS) &
        (~env->mideleg & env->mvien);
    uint64_t wr_mask_mvip;
    uint64_t wr_mask_mip;

    /*
     * mideleg[i]  mvien[i]
     *   0           0      sip[i] read-only zero.
     *   0           1      sip[i] alias of mvip[i].
     *   1           X      sip[i] alias of mip[i].
     *
     *  Both alias and non-alias mask remain same for sip except for bits
     *  which are zero in both mideleg and mvien.
     */
    if (csrno == CSR_SIP) {
        /* Remove bits that are zero in both mideleg and mvien. */
        alias_mask &= (env->mideleg | env->mvien);
        nalias_mask &= (env->mideleg | env->mvien);
    }

    /*
     * If sstc is present, mvip.STIP is not an alias of mip.STIP so clear
     * that our in mip returned value.
     */
    if (cpu->cfg.ext_sstc && (env->priv == PRV_M) &&
        get_field(env->menvcfg, MENVCFG_STCE)) {
        alias_mask &= ~MIP_STIP;
    }

    wr_mask_mip = wr_mask & alias_mask & mvip_writable_mask;
    wr_mask_mvip = wr_mask & nalias_mask & mvip_writable_mask;

    /*
     * For bits set in alias_mask, mvip needs to be alias of mip, so forward
     * this to rmw_mip.
     */
    ret = rmw_mip(env, CSR_MIP, &ret_mip, new_val, wr_mask_mip);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    old_mvip = env->mvip;

    /*
     * Write to mvip. Update only non-alias bits. Alias bits were updated
     * in mip in rmw_mip above.
     */
    if (wr_mask_mvip) {
        env->mvip = (env->mvip & ~wr_mask_mvip) | (new_val & wr_mask_mvip);

        /*
         * Given mvip is separate source from mip, we need to trigger interrupt
         * from here separately. Normally this happen from riscv_cpu_update_mip.
         */
        riscv_cpu_interrupt(env);
    }

    if (ret_val) {
        ret_mip &= alias_mask;
        old_mvip &= nalias_mask;

        *ret_val = old_mvip | ret_mip;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mvip(CPURISCVState *env, int csrno,
                              target_ulong *ret_val,
                              target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mvip64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_mviph(CPURISCVState *env, int csrno,
                               target_ulong *ret_val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mvip64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

/* Supervisor Trap Setup */
static RISCVException read_sstatus_i128(CPURISCVState *env, int csrno,
                                        Int128 *val)
{
    uint64_t mask = sstatus_v1_10_mask;
    uint64_t sstatus = env->mstatus & mask;
    if (env->xl != MXL_RV32 || env->debugger) {
        mask |= SSTATUS64_UXL;
    }

    *val = int128_make128(sstatus, add_status_sd(env, MXL_RV128, sstatus));
    return RISCV_EXCP_NONE;
}

RISCVException read_sstatus(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    target_ulong mask = (sstatus_v1_10_mask);
    if (env->xl != MXL_RV32 || env->debugger) {
        mask |= SSTATUS64_UXL;
    }

    if (!(env->mxstatus & MXSTATUS_MSD) && riscv_cpu_cfg(env)->ext_matrix) {
        mask |= MSTATUS_TH_MS;
    }
    if (riscv_cpu_cfg(env)->ext_zicfilp) {
        mask |= SSTATUS_SPELP;
    }
    /* TODO: Use SXL not MXL. */
    *val = add_status_sd(env, riscv_cpu_mxl(env), env->mstatus & mask);
    /*
     * Access to sstatus.spelp accesses the homonymous fields of mstatus
     * when V=0 and the homonymous fields of vsstatus when V=1.
     */
    if (env->virt_enabled) {
        *val = (*val & ~SSTATUS_SPELP) | (env->vsstatus & VSSTATUS_SPELP);
    }
    return RISCV_EXCP_NONE;
}

RISCVException write_sstatus(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    target_ulong mask = (sstatus_v1_10_mask);

    if (env->xl != MXL_RV32 || env->debugger) {
        if ((val & SSTATUS64_UXL) != 0) {
            mask |= SSTATUS64_UXL;
        }
    }
    if (!(env->mxstatus & MXSTATUS_MSD) && riscv_cpu_cfg(env)->ext_matrix) {
        mask |= MSTATUS_TH_MS;
    }

    if (riscv_cpu_cfg(env)->ext_zicfilp) {
        if (env->virt_enabled) {
            env->vsstatus = (env->vsstatus & ~SSTATUS_SPELP) |
                            (val & SSTATUS_SPELP);
        } else {
            mask |= SSTATUS_SPELP;
        }
    }

    target_ulong newval = (env->mstatus & ~mask) | (val & mask);
    return write_mstatus(env, CSR_MSTATUS, newval);
}

static RISCVException rmw_vsie64(CPURISCVState *env, int csrno,
                                 uint64_t *ret_val,
                                 uint64_t new_val, uint64_t wr_mask)
{
    uint64_t alias_mask = (LOCAL_INTERRUPTS | VS_MODE_INTERRUPTS) &
                            env->hideleg;
    uint64_t nalias_mask = LOCAL_INTERRUPTS & (~env->hideleg & env->hvien);
    uint64_t rval, rval_vs, vsbits;
    uint64_t wr_mask_vsie;
    uint64_t wr_mask_mie;
    RISCVException ret;

    /* Bring VS-level bits to correct position */
    vsbits = new_val & (VS_MODE_INTERRUPTS >> 1);
    new_val &= ~(VS_MODE_INTERRUPTS >> 1);
    new_val |= vsbits << 1;

    vsbits = wr_mask & (VS_MODE_INTERRUPTS >> 1);
    wr_mask &= ~(VS_MODE_INTERRUPTS >> 1);
    wr_mask |= vsbits << 1;

    wr_mask_mie = wr_mask & alias_mask;
    wr_mask_vsie = wr_mask & nalias_mask;

    ret = rmw_mie64(env, csrno, &rval, new_val, wr_mask_mie);

    rval_vs = env->vsie & nalias_mask;
    env->vsie = (env->vsie & ~wr_mask_vsie) | (new_val & wr_mask_vsie);

    if (ret_val) {
        rval &= alias_mask;
        vsbits = rval & VS_MODE_INTERRUPTS;
        rval &= ~VS_MODE_INTERRUPTS;
        *ret_val = rval | (vsbits >> 1) | rval_vs;
    }

    return ret;
}

static RISCVException rmw_vsie(CPURISCVState *env, int csrno,
                               target_ulong *ret_val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_vsie64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_vsieh(CPURISCVState *env, int csrno,
                                target_ulong *ret_val,
                                target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_vsie64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

static RISCVException rmw_sie64(CPURISCVState *env, int csrno,
                                uint64_t *ret_val,
                                uint64_t new_val, uint64_t wr_mask)
{
    uint64_t nalias_mask = (S_MODE_INTERRUPTS | LOCAL_INTERRUPTS) &
        (~env->mideleg & env->mvien);
    uint64_t alias_mask = (S_MODE_INTERRUPTS | LOCAL_INTERRUPTS) & env->mideleg;
    uint64_t sie_mask = wr_mask & nalias_mask;
    RISCVException ret;

    /*
     * mideleg[i]  mvien[i]
     *   0           0      sie[i] read-only zero.
     *   0           1      sie[i] is a separate writable bit.
     *   1           X      sie[i] alias of mie[i].
     *
     *  Both alias and non-alias mask remain same for sip except for bits
     *  which are zero in both mideleg and mvien.
     */
    if (env->virt_enabled) {
        if (env->hvictl & HVICTL_VTI) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        ret = rmw_vsie64(env, CSR_VSIE, ret_val, new_val, wr_mask);
        if (ret_val) {
            *ret_val &= alias_mask;
        }
    } else {
        ret = rmw_mie64(env, csrno, ret_val, new_val, wr_mask & alias_mask);
        if (ret_val) {
            *ret_val &= alias_mask;
            *ret_val |= env->sie & nalias_mask;
        }

        env->sie = (env->sie & ~sie_mask) | (new_val & sie_mask);
    }

    return ret;
}

static RISCVException rmw_sie(CPURISCVState *env, int csrno,
                              target_ulong *ret_val,
                              target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_sie64(env, csrno, &rval, new_val, wr_mask);
    if (ret == RISCV_EXCP_NONE && ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_sieh(CPURISCVState *env, int csrno,
                               target_ulong *ret_val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_sie64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

static RISCVException read_stvec(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    if (env->debugger && (env->priv == PRV_M) && env->virt_enabled) {
        *val = env->stvec_hs;
    } else {
        *val = env->stvec;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_stvec(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    target_ulong stvec = env->stvec;
    /*
     * RISC-V CLIC xtvec Mode Configuration (Specification v0.10, v0.8)
     *
     * mode  submode  PC on interrupt
     * ====  =======  ======================================================
     * 00    xxxx     OBASE               # CLINT direct mode
     * 01    xxxx     OBASE + 4*exccode   # CLINT vectored mode
     * 11    0000     NBASE               # CLIC mode
     * 10    0000     Reserved
     * 1x    yyyy     Reserved
     *
     * where:
     * OBASE = xtvec[XLEN-1:2] << 2   # CLINT mode vector base is at least 4-byte aligned
     * NBASE = xtvec[XLEN-1:6] << 6   # CLIC mode vector base is at least 64-byte aligned
     * x     = any value (don't care)
     * yyyy  = any non-zero value
     */
    target_ulong mode = val & 0b11;
    switch (mode) {
    case 0b00:
    case 0b01:
        stvec = val;
        break;
    case 0b11:
        if (env->xt_clic_v0p8 || env->xt_clic_v0p10) {
            stvec = val & ~0x3c;
        } else {
            error_report("CLIC mode is not supported in this case, please check the isa");
            stvec = (stvec & 0x3) | (val & ~0x3);
        }
        break;
    default:
        /*
         * Mode 0b10 is reserved. According to RISC-V spec, writes with
         * reserved mode values should be ignored.
         * This can happen when OpenSBI sets stvec to the kernel entry
         * address temporarily, where the low bits may be 0b10.
         */
        if (env->xt_clic_v0p8 || env->xt_clic_v0p10) {
            stvec = (stvec & 0x3f) | (val & ~0x3f);
        } else {
            stvec = (stvec & 0x3) | (val & ~0x3);
        }
        break;
    }

    if (env->debugger && (env->priv == PRV_M) && env->virt_enabled) {
        env->stvec_hs = stvec;
    } else {
        env->stvec = stvec;
    }
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
    RISCVCPU *cpu = env_archcpu(env);

    /* WARL register - disable unavailable counters */
    env->scounteren = val & (cpu->pmu_avail_ctrs | COUNTEREN_CY | COUNTEREN_TM |
                             COUNTEREN_IR);
    return RISCV_EXCP_NONE;
}

/* Supervisor Trap Handling */
static RISCVException read_sscratch_i128(CPURISCVState *env, int csrno,
                                         Int128 *val)
{
    *val = int128_make128(env->sscratch, env->sscratchh);
    return RISCV_EXCP_NONE;
}

static RISCVException write_sscratch_i128(CPURISCVState *env, int csrno,
                                          Int128 val)
{
    env->sscratch = int128_getlo(val);
    env->sscratchh = int128_gethi(val);
    return RISCV_EXCP_NONE;
}

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

static RISCVException rmw_hvip64(CPURISCVState *env, int csrno,
                                 uint64_t *ret_val,
                                 uint64_t new_val, uint64_t wr_mask);

static RISCVException rmw_vsip64(CPURISCVState *env, int csrno,
                                 uint64_t *ret_val,
                                 uint64_t new_val, uint64_t wr_mask)
{
    RISCVException ret;
    uint64_t rval, mask = env->hideleg & VS_MODE_INTERRUPTS;
    uint64_t vsbits;

    /* Add virtualized bits into vsip mask. */
    mask |= env->hvien & ~env->hideleg;

    /* Bring VS-level bits to correct position */
    vsbits = new_val & (VS_MODE_INTERRUPTS >> 1);
    new_val &= ~(VS_MODE_INTERRUPTS >> 1);
    new_val |= vsbits << 1;
    vsbits = wr_mask & (VS_MODE_INTERRUPTS >> 1);
    wr_mask &= ~(VS_MODE_INTERRUPTS >> 1);
    wr_mask |= vsbits << 1;

    ret = rmw_hvip64(env, csrno, &rval, new_val,
                     wr_mask & mask & vsip_writable_mask);
    if (ret_val) {
        rval &= mask;
        vsbits = rval & VS_MODE_INTERRUPTS;
        rval &= ~VS_MODE_INTERRUPTS;
        *ret_val = rval | (vsbits >> 1);
    }

    return ret;
}

static RISCVException rmw_vsip(CPURISCVState *env, int csrno,
                               target_ulong *ret_val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_vsip64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_vsiph(CPURISCVState *env, int csrno,
                                target_ulong *ret_val,
                                target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_vsip64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

static RISCVException rmw_sip64(CPURISCVState *env, int csrno,
                                uint64_t *ret_val,
                                uint64_t new_val, uint64_t wr_mask)
{
    RISCVException ret;
    uint64_t mask = (env->mideleg | env->mvien) & sip_writable_mask;

    if (env->virt_enabled) {
        if (env->hvictl & HVICTL_VTI) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        ret = rmw_vsip64(env, CSR_VSIP, ret_val, new_val, wr_mask);
    } else {
        ret = rmw_mvip64(env, csrno, ret_val, new_val, wr_mask & mask);
    }

    if (ret_val) {
        *ret_val &= (env->mideleg | env->mvien) &
            (S_MODE_INTERRUPTS | LOCAL_INTERRUPTS);
    }

    return ret;
}

static RISCVException rmw_sip(CPURISCVState *env, int csrno,
                              target_ulong *ret_val,
                              target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_sip64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_siph(CPURISCVState *env, int csrno,
                               target_ulong *ret_val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_sip64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

/* Supervisor Protection and Translation */
static RISCVException read_satp(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    if (!riscv_cpu_cfg(env)->mmu) {
        *val = 0;
        return RISCV_EXCP_NONE;
    }
    *val = env->satp;
    return RISCV_EXCP_NONE;
}

static RISCVException write_satp(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    if (!riscv_cpu_cfg(env)->mmu) {
        return RISCV_EXCP_NONE;
    }

    env->satp = legalize_xatp(env, env->satp, val);
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_sctrdepth(CPURISCVState *env, int csrno,
                                    target_ulong *ret_val,
                                    target_ulong new_val, target_ulong wr_mask)
{
    uint64_t mask = wr_mask & SCTRDEPTH_MASK;

    if (ret_val) {
        *ret_val = env->sctrdepth & SCTRDEPTH_MASK;
    }

    env->sctrdepth = (env->sctrdepth & ~mask) | (new_val & mask);

    /* Correct depth. */
    if (wr_mask & SCTRDEPTH_MASK) {
        uint64_t depth = get_field(env->sctrdepth, SCTRDEPTH_MASK);

        if (depth > SCTRDEPTH_MAX) {
            env->sctrdepth =
                set_field(env->sctrdepth, SCTRDEPTH_MASK, SCTRDEPTH_MAX);
        }

        /* Update sctrstatus.WRPTR with a legal value */
        depth = 16 << depth;
        env->sctrstatus =
            env->sctrstatus & (~SCTRSTATUS_WRPTR_MASK | (depth - 1));
    }

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mctrctl(CPURISCVState *env, int csrno,
                                    target_ulong *ret_val,
                                    target_ulong new_val, target_ulong wr_mask)
{
    uint64_t mask = wr_mask & MCTRCTL_MASK;

    if (ret_val) {
        *ret_val = env->mctrctl & MCTRCTL_MASK;
    }

    env->mctrctl = (env->mctrctl & ~mask) | (new_val & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_sctrctl(CPURISCVState *env, int csrno,
                                    target_ulong *ret_val,
                                    target_ulong new_val, target_ulong wr_mask)
{
    uint64_t mask = wr_mask & SCTRCTL_MASK;
    RISCVException ret;

    ret = rmw_mctrctl(env, csrno, ret_val, new_val, mask);
    if (ret_val) {
        *ret_val &= SCTRCTL_MASK;
    }

    return ret;
}

static RISCVException rmw_sctrstatus(CPURISCVState *env, int csrno,
                                     target_ulong *ret_val,
                                     target_ulong new_val, target_ulong wr_mask)
{
    uint32_t depth = 16 << get_field(env->sctrdepth, SCTRDEPTH_MASK);
    uint32_t mask = wr_mask & SCTRSTATUS_MASK;

    if (ret_val) {
        *ret_val = env->sctrstatus & SCTRSTATUS_MASK;
    }

    env->sctrstatus = (env->sctrstatus & ~mask) | (new_val & mask);

    /* Update sctrstatus.WRPTR with a legal value */
    env->sctrstatus = env->sctrstatus & (~SCTRSTATUS_WRPTR_MASK | (depth - 1));

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_vsctrctl(CPURISCVState *env, int csrno,
                                    target_ulong *ret_val,
                                    target_ulong new_val, target_ulong wr_mask)
{
    uint64_t mask = wr_mask & VSCTRCTL_MASK;

    if (ret_val) {
        *ret_val = env->vsctrctl & VSCTRCTL_MASK;
    }

    env->vsctrctl = (env->vsctrctl & ~mask) | (new_val & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException check_sqoscfg(CPURISCVState *env, int csrno)
{
    RISCVCPU *cpu = env_archcpu(env);

    if (!cpu->cfg.ext_ssqosid) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    /*
     * Even though this is an S-mode CSR the spec says that we need to throw
     * and virt instruction fault if a guest tries to access it.
     */
    return hmode(env, csrno);
}

static RISCVException read_sqoscfg(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = env->sqoscfg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_sqoscfg(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    env->sqoscfg = val & (SQOSCFG_RCID | SQOSCFG_MCID);
    return RISCV_EXCP_NONE;
}

static RISCVException read_vstopi(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    int irq, ret;
    target_ulong topei;
    uint64_t vseip, vsgein;
    uint32_t iid, iprio, hviid, hviprio, gein;
    uint32_t s, scount = 0, siid[VSTOPI_NUM_SRCS], siprio[VSTOPI_NUM_SRCS];

    gein = get_field(env->hstatus, HSTATUS_VGEIN);
    hviid = get_field(env->hvictl, HVICTL_IID);
    hviprio = get_field(env->hvictl, HVICTL_IPRIO);

    if (gein) {
        vsgein = (env->hgeip & (1ULL << gein)) ? MIP_VSEIP : 0;
        vseip = env->mie & (env->mip | vsgein) & MIP_VSEIP;
        if (gein <= env->geilen && vseip) {
            siid[scount] = IRQ_S_EXT;
            siprio[scount] = IPRIO_MMAXIPRIO + 1;
            if (env->aia_ireg_rmw_fn[PRV_S]) {
                /*
                 * Call machine specific IMSIC register emulation for
                 * reading TOPEI.
                 */
                ret = env->aia_ireg_rmw_fn[PRV_S](
                        env->aia_ireg_rmw_fn_arg[PRV_S],
                        AIA_MAKE_IREG(ISELECT_IMSIC_TOPEI, PRV_S, true, gein,
                                      riscv_cpu_mxl_bits(env)),
                        &topei, 0, 0);
                if (!ret && topei) {
                    siprio[scount] = topei & IMSIC_TOPEI_IPRIO_MASK;
                }
            }
            scount++;
        }
    } else {
        if (hviid == IRQ_S_EXT && hviprio) {
            siid[scount] = IRQ_S_EXT;
            siprio[scount] = hviprio;
            scount++;
        }
    }

    if (env->hvictl & HVICTL_VTI) {
        if (hviid != IRQ_S_EXT) {
            siid[scount] = hviid;
            siprio[scount] = hviprio;
            scount++;
        }
    } else {
        irq = riscv_cpu_vsirq_pending(env);
        if (irq != IRQ_S_EXT && 0 < irq && irq <= 63) {
            siid[scount] = irq;
            siprio[scount] = env->hviprio[irq];
            scount++;
        }
    }

    iid = 0;
    iprio = UINT_MAX;
    for (s = 0; s < scount; s++) {
        if (siprio[s] < iprio) {
            iid = siid[s];
            iprio = siprio[s];
        }
    }

    if (iid) {
        if (env->hvictl & HVICTL_IPRIOM) {
            if (iprio > IPRIO_MMAXIPRIO) {
                iprio = IPRIO_MMAXIPRIO;
            }
            if (!iprio) {
                if (riscv_cpu_default_priority(iid) > IPRIO_DEFAULT_S) {
                    iprio = IPRIO_MMAXIPRIO;
                }
            }
        } else {
            iprio = 1;
        }
    } else {
        iprio = 0;
    }

    *val = (iid & TOPI_IID_MASK) << TOPI_IID_SHIFT;
    *val |= iprio;

    return RISCV_EXCP_NONE;
}

static RISCVException read_stopi(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    int irq;
    uint8_t iprio;

    if (env->virt_enabled) {
        return read_vstopi(env, CSR_VSTOPI, val);
    }

    irq = riscv_cpu_sirq_pending(env);
    if (irq <= 0 || irq > 63) {
        *val = 0;
    } else {
        iprio = env->siprio[irq];
        if (!iprio) {
            if (riscv_cpu_default_priority(irq) > IPRIO_DEFAULT_S) {
                iprio = IPRIO_MMAXIPRIO;
           }
        }
        *val = (irq & TOPI_IID_MASK) << TOPI_IID_SHIFT;
        *val |= iprio;
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
    uint64_t mask = (target_ulong)-1;

    /* Update HUPMM field only if the value is valid according to Zjpm v1.0 */
    if (!riscv_cpu_cfg(env)->ext_ssnpm ||
        riscv_cpu_mxl(env) != MXL_RV64 ||
        get_field(val, HSTATUS_HUPMM) == PMM_FIELD_RESERVED) {
        mask &= ~HSTATUS_HUPMM;
    }
    env->hstatus = (env->hstatus & ~mask) | (val & mask);

    if (riscv_cpu_mxl(env) != MXL_RV32 && get_field(val, HSTATUS_VSXL) != 2) {
        qemu_log_mask(LOG_UNIMP,
                      "QEMU does not support mixed HSXLEN options.");
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

static RISCVException read_hedelegh(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    RISCVException ret;
    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_P1P13);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /* Reserved, now read zero */
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hedelegh(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    RISCVException ret;
    ret = smstateen_acc_ok(env, 0, SMSTATEEN0_P1P13);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /* Reserved, now write ignore */
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_hvien64(CPURISCVState *env, int csrno,
                                    uint64_t *ret_val,
                                    uint64_t new_val, uint64_t wr_mask)
{
    uint64_t mask = wr_mask & hvien_writable_mask;

    if (ret_val) {
        *ret_val = env->hvien;
    }

    env->hvien = (env->hvien & ~mask) | (new_val & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_hvien(CPURISCVState *env, int csrno,
                               target_ulong *ret_val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_hvien64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_hvienh(CPURISCVState *env, int csrno,
                                   target_ulong *ret_val,
                                   target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_hvien64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

static RISCVException rmw_hideleg64(CPURISCVState *env, int csrno,
                                    uint64_t *ret_val,
                                    uint64_t new_val, uint64_t wr_mask)
{
    uint64_t mask = wr_mask & vs_delegable_ints;

    if (ret_val) {
        *ret_val = env->hideleg & vs_delegable_ints;
    }

    env->hideleg = (env->hideleg & ~mask) | (new_val & mask);
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_hideleg(CPURISCVState *env, int csrno,
                                  target_ulong *ret_val,
                                  target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_hideleg64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_hidelegh(CPURISCVState *env, int csrno,
                                   target_ulong *ret_val,
                                   target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_hideleg64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

/*
 * The function is written for two use-cases:
 * 1- To access hvip csr as is for HS-mode access.
 * 2- To access vsip as a combination of hvip, and mip for vs-mode.
 *
 * Both report bits 2, 6, 10 and 13:63.
 * vsip needs to be read-only zero when both hideleg[i] and
 * hvien[i] are zero.
 */
static RISCVException rmw_hvip64(CPURISCVState *env, int csrno,
                                 uint64_t *ret_val,
                                 uint64_t new_val, uint64_t wr_mask)
{
    RISCVException ret;
    uint64_t old_hvip;
    uint64_t ret_mip;

    /*
     * For bits 10, 6 and 2, vsip[i] is an alias of hip[i]. These bits are
     * present in hip, hvip and mip. Where mip[i] is alias of hip[i] and hvip[i]
     * is OR'ed in hip[i] to inject virtual interrupts from hypervisor. These
     * bits are actually being maintained in mip so we read them from there.
     * This way we have a single source of truth and allows for easier
     * implementation.
     *
     * For bits 13:63 we have:
     *
     * hideleg[i]  hvien[i]
     *   0           0      No delegation. vsip[i] readonly zero.
     *   0           1      vsip[i] is alias of hvip[i], sip bypassed.
     *   1           X      vsip[i] is alias of sip[i], hvip bypassed.
     *
     *  alias_mask denotes the bits that come from sip (mip here given we
     *  maintain all bits there). nalias_mask denotes bits that come from
     *  hvip.
     */
    uint64_t alias_mask = (env->hideleg | ~env->hvien) | VS_MODE_INTERRUPTS;
    uint64_t nalias_mask = (~env->hideleg & env->hvien);
    uint64_t wr_mask_hvip;
    uint64_t wr_mask_mip;

    /*
     * Both alias and non-alias mask remain same for vsip except:
     *  1- For VS* bits if they are zero in hideleg.
     *  2- For 13:63 bits if they are zero in both hideleg and hvien.
     */
    if (csrno == CSR_VSIP) {
        /* zero-out VS* bits that are not delegated to VS mode. */
        alias_mask &= (env->hideleg | ~VS_MODE_INTERRUPTS);

        /*
         * zero-out 13:63 bits that are zero in both hideleg and hvien.
         * nalias_mask mask can not contain any VS* bits so only second
         * condition applies on it.
         */
        nalias_mask &= (env->hideleg | env->hvien);
        alias_mask &= (env->hideleg | env->hvien);
    }

    wr_mask_hvip = wr_mask & nalias_mask & hvip_writable_mask;
    wr_mask_mip = wr_mask & alias_mask & hvip_writable_mask;

    /* Aliased bits, bits 10, 6, 2 need to come from mip. */
    ret = rmw_mip64(env, csrno, &ret_mip, new_val, wr_mask_mip);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    old_hvip = env->hvip;

    if (wr_mask_hvip) {
        env->hvip = (env->hvip & ~wr_mask_hvip) | (new_val & wr_mask_hvip);

        /*
         * Given hvip is separate source from mip, we need to trigger interrupt
         * from here separately. Normally this happen from riscv_cpu_update_mip.
         */
        riscv_cpu_interrupt(env);
    }

    if (ret_val) {
        /* Only take VS* bits from mip. */
        ret_mip &= alias_mask;

        /* Take in non-delegated 13:63 bits from hvip. */
        old_hvip &= nalias_mask;

        *ret_val = ret_mip | old_hvip;
    }

    return ret;
}

static RISCVException rmw_hvip(CPURISCVState *env, int csrno,
                               target_ulong *ret_val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_hvip64(env, csrno, &rval, new_val, wr_mask);
    if (ret_val) {
        *ret_val = rval;
    }

    return ret;
}

static RISCVException rmw_hviph(CPURISCVState *env, int csrno,
                                target_ulong *ret_val,
                                target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_hvip64(env, csrno, &rval,
        ((uint64_t)new_val) << 32, ((uint64_t)wr_mask) << 32);
    if (ret_val) {
        *ret_val = rval >> 32;
    }

    return ret;
}

static RISCVException rmw_hip(CPURISCVState *env, int csrno,
                              target_ulong *ret_value,
                              target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, csrno, ret_value, new_value,
                      write_mask & hip_writable_mask);

    if (ret_value) {
        *ret_value &= HS_MODE_INTERRUPTS;
    }
    return ret;
}

static RISCVException rmw_hie(CPURISCVState *env, int csrno,
                              target_ulong *ret_val,
                              target_ulong new_val, target_ulong wr_mask)
{
    uint64_t rval;
    RISCVException ret;

    ret = rmw_mie64(env, csrno, &rval, new_val, wr_mask & HS_MODE_INTERRUPTS);
    if (ret_val) {
        *ret_val = rval & HS_MODE_INTERRUPTS;
    }

    return ret;
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
    RISCVCPU *cpu = env_archcpu(env);

    /* WARL register - disable unavailable counters */
    env->hcounteren = val & (cpu->pmu_avail_ctrs | COUNTEREN_CY | COUNTEREN_TM |
                             COUNTEREN_IR);
    return RISCV_EXCP_NONE;
}

static RISCVException read_hgeie(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    if (val) {
        *val = env->hgeie;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_hgeie(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    /* Only GEILEN:1 bits implemented and BIT0 is never implemented */
    val &= ((((target_ulong)1) << env->geilen) - 1) << 1;
    env->hgeie = val;
    /* Update mip.SGEIP bit */
    riscv_cpu_update_mip(env, MIP_SGEIP,
                         BOOL_TO_MASK(!!(env->hgeie & env->hgeip)));
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
    if (env->priv == PRV_M) {
        env->htinst = val;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_hgeip(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    if (val) {
        *val = env->hgeip;
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
    env->hgatp = legalize_xatp(env, env->hgatp, val);
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

    if (riscv_cpu_cfg(env)->ext_sstc && env->rdtime_fn) {
        riscv_timer_write_timecmp(env, env->vstimer, env->vstimecmp,
                                  env->htimedelta, MIP_VSTIP);
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_mtimedelta(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->xt_mtimedelta;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mtimedelta(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        env->xt_mtimedelta = deposit64(env->xt_mtimedelta, 0, 32,
                                       (uint64_t)val);
    } else {
        env->xt_mtimedelta = val;
    }

    if (riscv_cpu_cfg(env)->ext_sstc && env->rdtime_fn) {
        riscv_timer_write_timecmp(env, env->stimer, env->stimecmp,
                                  env->xt_mtimedelta, MIP_STIP);
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

    if (riscv_cpu_cfg(env)->ext_sstc && env->rdtime_fn) {
        riscv_timer_write_timecmp(env, env->vstimer, env->vstimecmp,
                                  env->htimedelta, MIP_VSTIP);
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_mvmid(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = ((uint32_t)env->xt_vmid_en << 31) | env->xt_vmid;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mvmid(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->xt_vmid = val & XT_CSR_VMID_MASK;
    env->xt_vmid_en = !!(val & XT_CSR_VMID_EN_MASK);
    return RISCV_EXCP_NONE;
}

static RISCVException read_mtimedeltah(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->xt_mtimedelta >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mtimedeltah(CPURISCVState *env, int csrno,
                                        target_ulong val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    env->xt_mtimedelta = deposit64(env->xt_mtimedelta, 32, 32, (uint64_t)val);

    if (riscv_cpu_cfg(env)->ext_sstc && env->rdtime_fn) {
        riscv_timer_write_timecmp(env, env->stimer, env->stimecmp,
                                  env->xt_mtimedelta, MIP_STIP);
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_hvictl(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->hvictl;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hvictl(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->hvictl = val & HVICTL_VALID_MASK;
    return RISCV_EXCP_NONE;
}

static RISCVException read_hvipriox(CPURISCVState *env, int first_index,
                         uint8_t *iprio, target_ulong *val)
{
    int i, irq, rdzero, num_irqs = 4 * (riscv_cpu_mxl_bits(env) / 32);

    /* First index has to be a multiple of number of irqs per register */
    if (first_index % num_irqs) {
        return (env->virt_enabled) ?
               RISCV_EXCP_VIRT_INSTRUCTION_FAULT : RISCV_EXCP_ILLEGAL_INST;
    }

    /* Fill-up return value */
    *val = 0;
    for (i = 0; i < num_irqs; i++) {
        if (riscv_cpu_hviprio_index2irq(first_index + i, &irq, &rdzero)) {
            continue;
        }
        if (rdzero) {
            continue;
        }
        *val |= ((target_ulong)iprio[irq]) << (i * 8);
    }

    return RISCV_EXCP_NONE;
}

static RISCVException write_hvipriox(CPURISCVState *env, int first_index,
                          uint8_t *iprio, target_ulong val)
{
    int i, irq, rdzero, num_irqs = 4 * (riscv_cpu_mxl_bits(env) / 32);

    /* First index has to be a multiple of number of irqs per register */
    if (first_index % num_irqs) {
        return (env->virt_enabled) ?
               RISCV_EXCP_VIRT_INSTRUCTION_FAULT : RISCV_EXCP_ILLEGAL_INST;
    }

    /* Fill-up priority array */
    for (i = 0; i < num_irqs; i++) {
        if (riscv_cpu_hviprio_index2irq(first_index + i, &irq, &rdzero)) {
            continue;
        }
        if (rdzero) {
            iprio[irq] = 0;
        } else {
            iprio[irq] = (val >> (i * 8)) & 0xff;
        }
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_hviprio1(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    return read_hvipriox(env, 0, env->hviprio, val);
}

static RISCVException write_hviprio1(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    return write_hvipriox(env, 0, env->hviprio, val);
}

static RISCVException read_hviprio1h(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    return read_hvipriox(env, 4, env->hviprio, val);
}

static RISCVException write_hviprio1h(CPURISCVState *env, int csrno,
                                      target_ulong val)
{
    return write_hvipriox(env, 4, env->hviprio, val);
}

static RISCVException read_hviprio2(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    return read_hvipriox(env, 8, env->hviprio, val);
}

static RISCVException write_hviprio2(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    return write_hvipriox(env, 8, env->hviprio, val);
}

static RISCVException read_hviprio2h(CPURISCVState *env, int csrno,
                                     target_ulong *val)
{
    return read_hvipriox(env, 12, env->hviprio, val);
}

static RISCVException write_hviprio2h(CPURISCVState *env, int csrno,
                                      target_ulong val)
{
    return write_hvipriox(env, 12, env->hviprio, val);
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
    if ((val & VSSTATUS64_UXL) == 0) {
        mask &= ~VSSTATUS64_UXL;
    }
    env->vsstatus = (env->vsstatus & ~mask) | (uint64_t)val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vstvec(CPURISCVState *env, int csrno,
                                  target_ulong *val)
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
    env->vsatp = legalize_xatp(env, env->vsatp, val);
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
    uint64_t tmp = mseccfg_csr_read(env);

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        *val = tmp & UINT32_MAX;
    } else {
        *val = tmp;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_mseccfgh(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = mseccfg_csr_read(env) >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mseccfg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    uint64_t tmp = val;
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        tmp = deposit64(env->mseccfg, 0, 32, val);
    }
    mseccfg_csr_write(env, tmp);
    return RISCV_EXCP_NONE;
}

static RISCVException write_mseccfgh(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    uint64_t tmp = deposit64(env->mseccfg, 32, 32, val);
    tmp = tmp &~ MSECCFG_PMM;
    mseccfg_csr_write(env, tmp);
    return RISCV_EXCP_NONE;
}

static RISCVException read_pmpcfg(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    uint32_t reg_index = csrno - CSR_PMPCFG0;

    *val = pmpcfg_csr_read(env, reg_index);
    return RISCV_EXCP_NONE;
}

static RISCVException write_pmpcfg(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    uint32_t reg_index = csrno - CSR_PMPCFG0;

    pmpcfg_csr_write(env, reg_index, val);
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

static RISCVException read_mmpt(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    if (riscv_cpu_xlen(env) == 32) {
        uint32_t value = 0;
        value |= env->mttmode << MMPT_MODE_SHIFT_32;
        value |= (env->sdid << MMPT_SDID_SHIFT_32) & MMPT_SDID_MASK_32;
        value |= env->mttppn & MMPT_PPN_MASK_32;
        *val = value;
    } else if (riscv_cpu_xlen(env) == 64) {
        uint64_t value_64 = 0;
        uint32_t mode_value = env->mttmode;
        /* mtt_mode_t convert to mttp.mode value */
        if (mode_value) {
            mode_value -= SMMTT43 - SMMTT34;
        }
        value_64 |= (uint64_t)mode_value << MMPT_MODE_SHIFT_64;
        value_64 |= ((uint64_t)env->sdid << MMPT_SDID_SHIFT_64)
                    & MMPT_SDID_MASK_64;
        value_64 |= (uint64_t)env->mttppn & MMPT_PPN_MASK_64;
        *val = value_64;
    } else {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_mmpt(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    /* Fixme: if mode is bare, the remaining fields in mmpt must be zero */
    if (riscv_cpu_xlen(env) == 32) {
        /* Only write the legal value */
        uint32_t mode_value = (val & MMPT_MODE_MASK_32) >> MMPT_MODE_SHIFT_32;
        if (mode_value <= SMMTT34) {
            env->mttmode = mode_value;
        }
        env->sdid = (val & MMPT_SDID_MASK_32) >> MMPT_SDID_SHIFT_32;
        env->mttppn = val & MMPT_PPN_MASK_32;
    } else if (riscv_cpu_xlen(env) == 64) {
        uint32_t mode_value = (val & MMPT_MODE_MASK_64) >> MMPT_MODE_SHIFT_64;
        /* check legal value */
        if (mode_value < SMMTTMAX) {
            /* convert to mtt_mode_t */
            if (mode_value) {
                mode_value += SMMTT43 - SMMTT34;
            }
            env->mttmode = mode_value;
        }
        env->sdid = (val & MMPT_SDID_MASK_64) >> MMPT_SDID_SHIFT_64;
        env->mttppn = val & MMPT_PPN_MASK_64;
    } else {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_msdcfg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->msdcfg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_msdcfg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->msdcfg = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_tselect(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = tselect_csr_read(env);
    return RISCV_EXCP_NONE;
}

static RISCVException write_tselect(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    tselect_csr_write(env, val);
    return RISCV_EXCP_NONE;
}

static RISCVException read_tdata(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    if (env->trigger_cur >= RV_MAX_TRIGGERS) {
        /* return 0 in tdata1 to end the trigger enumeration */
       if (csrno == CSR_TDATA1) {
           *val = 0;
           return RISCV_EXCP_NONE;
       } else {
           g_assert_not_reached();
       }
    }

    if (!tdata_available(env, csrno - CSR_TDATA1)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = tdata_csr_read(env, csrno - CSR_TDATA1);
    return RISCV_EXCP_NONE;
}

static RISCVException write_tdata(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    if (!tdata_available(env, csrno - CSR_TDATA1)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    tdata_csr_write(env, csrno - CSR_TDATA1, val);
    return RISCV_EXCP_NONE;
}

static RISCVException read_tinfo(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = tinfo_csr_read(env);
    return RISCV_EXCP_NONE;
}

static RISCVException read_mcontext(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->mcontext;
    return RISCV_EXCP_NONE;
}

static RISCVException read_scontext(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->scontext;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mcontext(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    bool rv32 = riscv_cpu_mxl(env) == MXL_RV32 ? true : false;
    int32_t mask;

    if (riscv_has_ext(env, RVH)) {
        /* Spec suggest 7-bit for RV32 and 14-bit for RV64 w/ H extension */
        mask = rv32 ? MCONTEXT32_HCONTEXT : MCONTEXT64_HCONTEXT;
    } else {
        /* Spec suggest 6-bit for RV32 and 13-bit for RV64 w/o H extension */
        mask = rv32 ? MCONTEXT32 : MCONTEXT64;
    }

    env->mcontext = val & mask;
    return RISCV_EXCP_NONE;
}

static RISCVException write_scontext(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    bool rv32 = riscv_cpu_sxl(env) == MXL_RV32;
    int32_t mask;

    /* Spec suggest 16-bit for RV32 and 32-bit for RV64 */
    mask = rv32 ? SCONTEXT32 : SCONTEXT64;

    env->scontext = val & mask;
    return RISCV_EXCP_NONE;
}

#endif

/* Crypto Extension */
target_ulong riscv_new_csr_seed(target_ulong new_value,
                                target_ulong write_mask)
{
    uint16_t random_v;
    Error *random_e = NULL;
    int random_r;
    target_ulong rval;

    random_r = qemu_guest_getrandom(&random_v, 2, &random_e);
    if (unlikely(random_r < 0)) {
        /*
         * Failed, for unknown reasons in the crypto subsystem.
         * The best we can do is log the reason and return a
         * failure indication to the guest.  There is no reason
         * we know to expect the failure to be transitory, so
         * indicate DEAD to avoid having the guest spin on WAIT.
         */
        qemu_log_mask(LOG_UNIMP, "%s: Crypto failure: %s",
                      __func__, error_get_pretty(random_e));
        error_free(random_e);
        rval = SEED_OPST_DEAD;
    } else {
        rval = random_v | SEED_OPST_ES16;
    }

    return rval;
}

static RISCVException rmw_seed(CPURISCVState *env, int csrno,
                               target_ulong *ret_value,
                               target_ulong new_value,
                               target_ulong write_mask)
{
    target_ulong rval;

    rval = riscv_new_csr_seed(new_value, write_mask);

    if (ret_value) {
        *ret_value = rval;
    }

    return RISCV_EXCP_NONE;
}

/*
 * riscv_csrrw - read and/or update control and status register
 *
 * csrr   <->  riscv_csrrw(env, csrno, ret_value, 0, 0);
 * csrrw  <->  riscv_csrrw(env, csrno, ret_value, value, -1);
 * csrrs  <->  riscv_csrrw(env, csrno, ret_value, -1, value);
 * csrrc  <->  riscv_csrrw(env, csrno, ret_value, 0, value);
 */

static inline RISCVException riscv_csrrw_check(CPURISCVState *env,
                                               int csrno,
                                               bool write)
{
    /* check privileges and return RISCV_EXCP_ILLEGAL_INST if check fails */
    bool read_only = get_field(csrno, 0xC00) == 3;
    int csr_min_priv = csr_ops[csrno].min_priv_ver;

    /* ensure the CSR extension is enabled */
    if (!riscv_cpu_cfg(env)->ext_zicsr) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    /* ensure CSR is implemented by checking predicate */
    if (!csr_ops[csrno].predicate) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    /* privileged spec version check */
    if (env->priv_ver < csr_min_priv) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    /* read / write check */
    if (write && read_only) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    /*
     * The predicate() not only does existence check but also does some
     * access control check which triggers for example virtual instruction
     * exception in some cases. When writing read-only CSRs in those cases
     * illegal instruction exception should be triggered instead of virtual
     * instruction exception. Hence this comes after the read / write check.
     */
    RISCVException ret = csr_ops[csrno].predicate(env, csrno);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

#if !defined(CONFIG_USER_ONLY)
    int csr_priv, effective_priv = env->priv;

    if (riscv_has_ext(env, RVH) && env->priv == PRV_S &&
        !env->virt_enabled) {
        /*
         * We are in HS mode. Add 1 to the effective privilege level to
         * allow us to access the Hypervisor CSRs.
         */
        effective_priv++;
    }

    csr_priv = get_field(csrno, 0x300);
    if (!env->debugger && (effective_priv < csr_priv)) {
        if (csr_priv == (PRV_S + 1) && env->virt_enabled) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException riscv_csrrw_do64(CPURISCVState *env, int csrno,
                                       target_ulong *ret_value,
                                       target_ulong new_value,
                                       target_ulong write_mask)
{
    RISCVException ret;
    target_ulong old_value = 0;

    /* execute combined read/write operation if it exists */
    if (csr_ops[csrno].op) {
        return csr_ops[csrno].op(env, csrno, ret_value, new_value, write_mask);
    }

    /*
     * ret_value == NULL means that rd=x0 and we're coming from helper_csrw()
     * and we can't throw side effects caused by CSR reads.
     */
    if (ret_value) {
        /* if no accessor exists then return failure */
        if (!csr_ops[csrno].read) {
            return RISCV_EXCP_ILLEGAL_INST;
        }
        /* read old value */
        ret = csr_ops[csrno].read(env, csrno, &old_value);
        if (ret != RISCV_EXCP_NONE) {
            return ret;
        }
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

static bool xt_csr_e908a_ignore(int csrno)
{
    switch (csrno) {
    /* S mode */
    case CSR_SHCR:
    case CSR_SHINT:
    case CSR_SHINT2:
    case CSR_SHPMCR:
    case CSR_SHPMSR:
    case CSR_SHPMER:
    case CSR_SBEADDR:
    case CSR_SCYCLE:
    case CSR_SINSTRET:
    case CSR_SHPMCOUNTER3 ... CSR_SHPMCOUNTER31:
    case CSR_SCYCLEH:
    case CSR_SINSTRETH:
    case CSR_SHPMCOUNTER3H ... CSR_SHPMCOUNTER31H:
    /* M mode */
    case CSR_MHCR:
    case CSR_MCOR:
    case CSR_MCCR2:
    case CSR_MHINT:
    case CSR_MCOUNTERWEN:
    case CSR_MHINT2H:
    case CSR_MHINT2:
    case CSR_MHINT3:
    case CSR_MCINS:
    case CSR_MCINDEX:
    case CSR_MCDATA0:
    case CSR_MCDATA1:
    case CSR_MEICR:
    case CSR_MBEADDR:
    case CSR_MCPER:
    case CSR_MCINDEXH:
    case CSR_MCDATA0H:
    case CSR_MCDATA1H:
    case CSR_MIEBR:
    case CSR_MDEBR:
    case CSR_MHPMCR:
    case CSR_MHPMSR:
    case CSR_MHPMER:
    case CSR_MHALTCAUSE:
    case CSR_MDBGINFO:
    case CSR_MPCFIFO:
    case CSR_MDBGFIFO2:
    case CSR_MFPPCR:
    case CSR_MFLASHCR:
    case CSR_MRASCR:
    case CSR_MSTCCR:
    case CSR_MWATCHDOG:
    case CSR_MBUSTIME1:
    case CSR_MBUSTIME2:
    case CSR_MBUSTIME3:
    case CSR_MERRINJ:
    case CSR_MTEBR0:
    case CSR_MTEBR1:
        return true;
        break;
    default:
        return false;
        break;
    }
}

static bool xt_csr_ignore(CPURISCVState *env, int csrno)
{
    CPUState *cs = env_cpu(env);

    if (object_dynamic_cast(OBJECT(cs), TYPE_RISCV_CPU_XT_E908A1FDVKT)) {
        if (xt_csr_e908a_ignore(csrno)) {
            return true;
        }
        return false;
    }

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
    case CSR_SCYCLE ... CSR_SHPMCOUNTER31:
    case CSR_MHINT3:
    case CSR_MHINT4:
    case CSR_MBEADDR:
    case CSR_MCPER:
    case CSR_MCINDEXH:
    case CSR_MCDATA0H:
    case CSR_MCDATA1H:
    case CSR_MHPMCR:
    case CSR_MHPMSR:
    case CSR_MHPMER:
    case CSR_MZONEID:
    case CSR_ML2PID:
    case CSR_ML2WP:
    case CSR_ML2WPH:
    case CSR_MCERH:
    case CSR_MCER2H:
    case CSR_MHALTCAUSE:
    case CSR_MDBGINFO:
    case CSR_MPCFIFO:
    case CSR_MDBGFIFO2:
    case CSR_SHINT:
    case CSR_SHINT2:
    case CSR_SHPMINHIBIT:
    case CSR_SHPMCR:
    case CSR_SHPMSR:
    case CSR_SHPMER:
    case CSR_SL2PID:
    case CSR_SL2WP:
    case CSR_SL2WPH:
    case CSR_SBEADDR:
    case CSR_SCERH:
    case CSR_SCER2H:
    case CSR_SCYCLEH:
    case CSR_SINSTRETH ... CSR_SHPMCOUNTER31H:
    case CSR_MTNADDR2:
    case CSR_MTNCR:
    case CSR_MTNSR:
    case CSR_MTNER:
    case CSR_MTNADDR:
    case CSR_MDEBUG_TN_PC:
    case CSR_TNLOWPOWER:
    case CSR_XT_CESTATUS:
    case CSR_XT_SER:
    case CSR_XT_MCDATA2:
    case CSR_MIEBR:
    case CSR_MDEBR:
    case CSR_MFPPCR:
    case CSR_MONCHIPBA:
    case CSR_MFLASHCR:
    case CSR_MRASCR:
    case CSR_MSTCCR:
    case CSR_MWATCHDOG:
    case CSR_MBUSTIME1:
    case CSR_MBUSTIME2:
    case CSR_MBUSTIME3:
    case CSR_MERRINJ:
    case CSR_MTEBR0:
    case CSR_MTEBR1:
    case CSR_MLOCKWAY:
    case CSR_MLOCKADDR:
    case CSR_MUNLOCKCTL:
        return true;
        break;
    default:
        return false;
        break;
    }
}

RISCVException riscv_csrr(CPURISCVState *env, int csrno,
                           target_ulong *ret_value)
{
    if (xt_csr_ignore(env, csrno)) {
        return RISCV_EXCP_NONE;
    }

    RISCVException ret = riscv_csrrw_check(env, csrno, false);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    return riscv_csrrw_do64(env, csrno, ret_value, 0, 0);
}

RISCVException riscv_csrrw(CPURISCVState *env, int csrno,
                           target_ulong *ret_value,
                           target_ulong new_value, target_ulong write_mask)
{
    if (xt_csr_ignore(env, csrno)) {
        return RISCV_EXCP_NONE;
    }

    RISCVException ret = riscv_csrrw_check(env, csrno, true);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    return riscv_csrrw_do64(env, csrno, ret_value, new_value, write_mask);
}

static RISCVException riscv_csrrw_do128(CPURISCVState *env, int csrno,
                                        Int128 *ret_value,
                                        Int128 new_value,
                                        Int128 write_mask)
{
    RISCVException ret;
    Int128 old_value;

    /* read old value */
    ret = csr_ops[csrno].read128(env, csrno, &old_value);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /* write value if writable and write mask set, otherwise drop writes */
    if (int128_nz(write_mask)) {
        new_value = int128_or(int128_and(old_value, int128_not(write_mask)),
                              int128_and(new_value, write_mask));
        if (csr_ops[csrno].write128) {
            ret = csr_ops[csrno].write128(env, csrno, new_value);
            if (ret != RISCV_EXCP_NONE) {
                return ret;
            }
        } else if (csr_ops[csrno].write) {
            /* avoids having to write wrappers for all registers */
            ret = csr_ops[csrno].write(env, csrno, int128_getlo(new_value));
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

RISCVException riscv_csrr_i128(CPURISCVState *env, int csrno,
                               Int128 *ret_value)
{
    RISCVException ret;

    ret = riscv_csrrw_check(env, csrno, false);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    if (csr_ops[csrno].read128) {
        return riscv_csrrw_do128(env, csrno, ret_value,
                                 int128_zero(), int128_zero());
    }

    /*
     * Fall back to 64-bit version for now, if the 128-bit alternative isn't
     * at all defined.
     * Note, some CSRs don't need to extend to MXLEN (64 upper bits non
     * significant), for those, this fallback is correctly handling the
     * accesses
     */
    target_ulong old_value;
    ret = riscv_csrrw_do64(env, csrno, &old_value,
                           (target_ulong)0,
                           (target_ulong)0);
    if (ret == RISCV_EXCP_NONE && ret_value) {
        *ret_value = int128_make64(old_value);
    }
    return ret;
}

RISCVException riscv_csrrw_i128(CPURISCVState *env, int csrno,
                                Int128 *ret_value,
                                Int128 new_value, Int128 write_mask)
{
    RISCVException ret;

    ret = riscv_csrrw_check(env, csrno, true);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    if (csr_ops[csrno].read128) {
        return riscv_csrrw_do128(env, csrno, ret_value, new_value, write_mask);
    }

    /*
     * Fall back to 64-bit version for now, if the 128-bit alternative isn't
     * at all defined.
     * Note, some CSRs don't need to extend to MXLEN (64 upper bits non
     * significant), for those, this fallback is correctly handling the
     * accesses
     */
    target_ulong old_value;
    ret = riscv_csrrw_do64(env, csrno, &old_value,
                           int128_getlo(new_value),
                           int128_getlo(write_mask));
    if (ret == RISCV_EXCP_NONE && ret_value) {
        *ret_value = int128_make64(old_value);
    }
    return ret;
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
    if (!write_mask) {
        ret = riscv_csrr(env, csrno, ret_value);
    } else {
        ret = riscv_csrrw(env, csrno, ret_value, new_value, write_mask);
    }
#if !defined(CONFIG_USER_ONLY)
    env->debugger = false;
#endif
    return ret;
}

static RISCVException read_jvt(CPURISCVState *env, int csrno,
                               target_ulong *val)
{
    *val = env->jvt;
    return RISCV_EXCP_NONE;
}

static RISCVException write_jvt(CPURISCVState *env, int csrno,
                                target_ulong val)
{
    env->jvt = val;
    return RISCV_EXCP_NONE;
}

/* Xuantie priviledged CSR callbacks */
#if !defined(CONFIG_USER_ONLY)
static int read_mxstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mxstatus | (env->priv << 30);
    return RISCV_EXCP_NONE;
}

static int write_mxstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mxstatus = val;
    /*
     * This extension does not affect instruction translation or register
     * reads/writes; it only impacts page table walks. Therefore, it can
     * be handled by updating the CPU configuration (CFG) only;
     * otherwise the change would need to be propagated via tb_flags.
     */
    bool *maee = (bool *)&(riscv_cpu_cfg(env)->ext_xtheadmaee);
    *maee = !!(val & TH_SXSTATUS_MAEE);
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
   } else if (strstr(typename, "r908")) {
       if (size_field >= 0b0011 && size_field <= 0b1010) {
            return (1 << (size_field)) * 1024;
       } else {
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

static uint64_t xt_get_fastm_base(uint64_t fastm)
{
    uint64_t ones = cto64(fastm);
    /* For PA56, [43: 0] is valid */
    return extract64(fastm, ones + 1, 44 - ones) << (ones + 12) ;
}

static uint64_t xt_get_fastm_size(uint64_t fastm)
{
    uint64_t ones = cto64(fastm);
    return 1ULL << (12 + ones);
}

static MemoryRegion *xtfastm;

static RISCVException
xt_update_fastm(uint64_t new_fastm, uint64_t old_fastm, CPURISCVState *env)
{
    uint64_t old_base, old_size, new_base, new_size;
    bool old_enable, new_enable;

    old_base = xt_get_fastm_base(old_fastm);
    old_size = xt_get_fastm_size(old_fastm);
    old_enable = !!get_field(old_fastm, MFASTM_ENABLE);
    new_base = xt_get_fastm_base(new_fastm);
    new_size = xt_get_fastm_size(new_fastm);
    new_enable = !!get_field(new_fastm, MFASTM_ENABLE);

    if ((old_size != new_size) && old_size && old_enable) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (new_size == 0) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (!bql_locked()) {
        bql_lock();
    }
    /* When configuration change, delete old tcm */
    if (xtfastm) {
        if (new_enable != old_enable) {
            /* Reenable Old memory region */
            memory_region_set_enabled(xtfastm, new_enable);
        }
        if (old_base != new_base) {
            /* Rebase memory region */
            memory_region_set_address(xtfastm, new_base);
        }
    }
    /* Old fastm has been removed or never exist, and new configuration request */
    if ((xtfastm == NULL) && new_enable) {
        xtfastm = g_new(MemoryRegion, 1);
        memory_region_init_ram(xtfastm, NULL, "riscv_fastm",
                               new_size, &error_fatal);
        /* Add new memory region */
        memory_region_add_subregion_overlap(get_system_memory(), new_base,
                                            xtfastm, 1);
    }
    bql_unlock();
    return RISCV_EXCP_NONE;
}

static RISCVException read_mfastm(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    if (!riscv_cpu_cfg(env)->ext_xtheadfastm) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (riscv_cpu_mxl(env) == MXL_RV64) {
        *val = env->fastmcr;
    } else {
        g_assert_not_reached();
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_mfastm(CPURISCVState *env, int csrno, target_ulong val)
{
    RISCVException ret = RISCV_EXCP_NONE;
    if (!riscv_cpu_cfg(env)->ext_xtheadfastm) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    uint64_t old_fastm = env->fastmcr_old, new_fastm;
    if (riscv_cpu_mxl(env) == MXL_RV64) {
        new_fastm = val;
        ret = xt_update_fastm(new_fastm, old_fastm, env);
        if (ret == RISCV_EXCP_NONE) {
            env->fastmcr = new_fastm;
            env->fastmcr_old = new_fastm;
        }
    } else {
        g_assert_not_reached();
    }
    return ret;
}

static int write_mdtcmcr(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong base_mask = (riscv_cpu_mxl(env) == MXL_RV32) ?
                                 MDTCMCR_BASE_32 : MDTCMCR_BASE_64;
    target_ulong old_base = get_field(env->mdtcmcr, base_mask);
    target_ulong old_enable = get_field(env->mdtcmcr, MDTCMCR_EN);
    target_ulong new_base = get_field(val, base_mask);
    target_ulong new_enable = get_field(val, MDTCMCR_EN);
    target_ulong size = get_tcm_size(env, get_field(env->mdtcmcr,
                                                    MDTCMCR_SIZE));
    target_ulong base = val & base_mask;
    target_ulong mask = MDTCMCR_EN | MDTCMCR_ECC_EN | MDTCMCR_INTERLEAVE;
    bool legal_base;

    if (size == 0) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    legal_base = (base % size) == 0;
    if (legal_base) {
        mask |= base_mask;
    }

    env->mdtcmcr = (env->mdtcmcr & ~mask) | (val & mask);

    /* When configuration change, delete old tcm */
    if (env->dtcm) {
        if (new_enable != old_enable) {
            /* Reenable Old memory region */
            if (!bql_locked()) {
                bql_lock();
                memory_region_set_enabled(env->dtcm, new_enable);
                bql_unlock();
            } else {
                memory_region_set_enabled(env->dtcm, new_enable);
            }
        }
        if ((old_base != new_base) && legal_base) {
            /* Rebase memory region */
            if (!bql_locked()) {
                bql_lock();
                memory_region_set_address(env->dtcm, base);
                bql_unlock();
            } else {
                memory_region_set_address(env->dtcm, base);
            }
        }
    }
    /* Old tcm has been removed or never exist, and new configuration request */
    if ((env->dtcm == NULL) && new_enable) {
        if (!legal_base) {
            /* Stricter than specification check */
            return RISCV_EXCP_ILLEGAL_INST;
        }
        env->dtcm = g_new(MemoryRegion, 1);
        if (!bql_locked()) {
            bql_lock();
            memory_region_init_ram(env->dtcm, NULL, "riscv_dtcm",
                                   size, &error_fatal);
            /* Add new memory region */
            memory_region_add_subregion_overlap(get_system_memory(), base,
                                                env->dtcm, 1);
            bql_unlock();
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
    target_ulong old_base = get_field(env->mitcmcr, base_mask);
    target_ulong old_enable = get_field(env->mitcmcr, MITCMCR_EN);
    target_ulong new_base = get_field(val, base_mask);
    target_ulong new_enable = get_field(val, MITCMCR_EN);
    target_ulong size = get_tcm_size(env, get_field(env->mitcmcr,
                                                    MITCMCR_SIZE));
    target_ulong base = val & base_mask;
    target_ulong mask = MITCMCR_EN | MITCMCR_ECC_EN | MITCMCR_INTERLEAVE;
    bool legal_base;

    if (size == 0) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    legal_base = (base % size) == 0;
    if (legal_base) {
        mask |= base_mask;
    }
    env->mitcmcr = (env->mitcmcr & ~mask) | (val & mask);
    /* When configuration change, delete old tcm */
    if (env->itcm) {
        if (new_enable != old_enable) {
            /* Reenable Old memory region */
            if (!bql_locked()) {
                bql_lock();
                memory_region_set_enabled(env->itcm, new_enable);
                bql_unlock();
            } else {
                memory_region_set_enabled(env->itcm, new_enable);
            }
        }
        if ((old_base != new_base) && legal_base) {
            /* Rebase memory region */
            if (!bql_locked()) {
                bql_lock();
                memory_region_set_address(env->itcm, base);
                bql_unlock();
            } else {
                memory_region_set_address(env->itcm, base);
            }
        }
    }
    /* Old tcm has been removed or never exist, and new configuration request */
    if ((env->itcm == NULL) && new_enable) {
        if (!legal_base) {
            /* Stricter than specification check */
            return RISCV_EXCP_ILLEGAL_INST;
        }
        env->itcm = g_new(MemoryRegion, 1);
        if (!bql_locked()) {
            bql_lock();
            memory_region_init_ram(env->itcm, NULL, "riscv_itcm",
                                   size, &error_fatal);
            /* Add new memory region */
            memory_region_add_subregion_overlap(get_system_memory(), base,
                                                env->itcm, 1);
            bql_unlock();
        }
    }
    return RISCV_EXCP_NONE;
}

static int read_sxstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sxstatus | (env->priv << 30);
    return RISCV_EXCP_NONE;
}

static int write_sxstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sxstatus = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mrvbr(CPURISCVState *env, int csrno, target_ulong *val)
{
    RISCVCPU *cpu = env_archcpu(env);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    *val = mcc->mrvbr;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mrvbr(CPURISCVState *env, int csrno, target_ulong val)
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

/*
 * tlbp/tlbr operations do not support actually,
 * just an aggrement with OS
 */
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
    /*
     * Note: when more than one of the most significant
     * bits are asserted at the same time, these operations
     * are implemented according to the priority as follows:
     *     Tlb invalid all operation
     *     Tlb invalid operation
     *     Tlb probe operation
     *     Tlb writing index operation
     *     Tlb writing random operation
     *     Tlb reading operation
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

RISCVException suenq(CPURISCVState *env, int csrno)
{
    const RISCVCPUConfig *cfg = riscv_cpu_cfg(env);
    if (!cfg->ext_xtheadaioe) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (env->debugger) {
        return smode(env, csrno);
    }
    if (!get_field(env->menvcfg, MENVCFG_SUENQ) && env->priv != PRV_M) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (get_field(env->menvcfg, MENVCFG_SUENQ)) {
        if (env->priv < PRV_S && env->virt_enabled) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        if (env->priv == PRV_S && env->virt_enabled
            && !get_field(env->henvcfg, HENVCFG_SUENQ)) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }
    return smode(env, csrno);
}

static int read_suenq(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->suenq;
    return RISCV_EXCP_NONE;
}

static int write_suenq(CPURISCVState *env, int csrno, target_ulong val)
{
    env->suenq = val & (CSR_SUENQ_DATA | CSR_SUENQ_DL);
    return RISCV_EXCP_NONE;
}

static int read_cpuid(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->cpuid;
    return RISCV_EXCP_NONE;
}
#endif

/* CLIC CSR callbacks */
#if !defined(CONFIG_USER_ONLY)

static inline bool clic_device_exists(CPURISCVState *env)
{
    return env->xt_clic_v0p8 || env->xt_clic_v0p10;
}

RISCVException clic(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_smclic_v0p8 &&
        !riscv_cpu_cfg(env)->ext_smclic_v0p10) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return RISCV_EXCP_NONE;
}

RISCVException clic_v0p8(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_smclic_v0p8) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

RISCVException clic_v0p10(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_smclic_v0p10) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException sclic(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_ssclic_v0p10 ||
        !riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    return clic(env, csrno);
}

static RISCVException sclic_v0p8(CPURISCVState *env, int csrno)
{
    if (!riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    return clic_v0p8(env, csrno);
}

static RISCVException sclic_v0p10(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_ssclic_v0p10 ||
        !riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    return clic_v0p10(env, csrno);
}

static int read_mintstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    switch (csrno) {
    case CSR_MINTSTATUS_0p8:
        if (!env->xt_clic_v0p8) {
            return RISCV_EXCP_ILLEGAL_INST;
        }
        break;
    case CSR_MINTSTATUS_0p10:
        if (!env->xt_clic_v0p10) {
            return RISCV_EXCP_ILLEGAL_INST;
        }
        break;
    default:
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->mintstatus;
    return RISCV_EXCP_NONE;
}

static int read_sintstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    switch (csrno) {
    case CSR_SINTSTATUS_0p8:
        if (!env->xt_clic_v0p8) {
            return RISCV_EXCP_ILLEGAL_INST;
        }
        break;
    case CSR_SINTSTATUS_0p10:
        if (!env->xt_clic_v0p10) {
            return RISCV_EXCP_ILLEGAL_INST;
        }
        break;
    default:
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->mintstatus & MINTSTATUS_SIL;
    return RISCV_EXCP_NONE;
}

static int read_mintthresh(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (env->xt_clic_v0p10) {
        *val = env->mintthresh & MINTTHRESH_MTH_V0P10;
    } else {
        *val = env->mintthresh & MINTTHRESH_MTH;
    }
    return RISCV_EXCP_NONE;
}

static int write_mintthresh(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (env->xt_clic_v0p10) {
        /*
        * The th field is held in the least-significant 8 bits of the CSR,
        * and zero should be written to the upper bits.
        */
        env->mintthresh = val & MINTTHRESH_MTH_V0P10;
    } else {
        env->mintthresh = val & MINTTHRESH_MTH;
    }
    return RISCV_EXCP_NONE;
}

static int read_sintthresh(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->sintthresh;
    return RISCV_EXCP_NONE;
}

static int write_sintthresh(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (env->xt_clic_v0p10) {
        /*
        * The th field is held in the least-significant 8 bits of the CSR,
        * and zero should be written to the upper bits.
        */
        env->sintthresh = val & MINTTHRESH_MTH_V0P10;
    } else {
        env->sintthresh = val & MINTTHRESH_MTH;
    }
    return RISCV_EXCP_NONE;
}

static int read_mtvt(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->mtvt;
    return RISCV_EXCP_NONE;
}

static int write_mtvt(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    env->mtvt = val & ~((1ULL << 6) - 1);
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mscratchcsw(CPURISCVState *env, int csrno,
                                      target_ulong *ret_value,
                                      target_ulong new_value,
                                      target_ulong write_mask)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    target_ulong rs1 = (env->mscratch & ~write_mask) | (new_value & write_mask);
    target_ulong mpp = get_field(env->mcause, MSTATUS_MPP);

    if (mpp != PRV_M) {
        if (ret_value) {
            *ret_value = env->mscratch;
        }
        env->mscratch = rs1;
    } else if (ret_value) {
        *ret_value = rs1;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mscratchcsl(CPURISCVState *env, int csrno,
                                      target_ulong *ret_value,
                                      target_ulong new_value,
                                      target_ulong write_mask)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    target_ulong rs1 = (env->mscratch & ~write_mask) | (new_value & write_mask);
    target_ulong pil = get_field(env->mcause, MCAUSE_MPIL);
    target_ulong mil = get_field(env->mintstatus, MINTSTATUS_MIL);

    if ((pil == 0) != (mil == 0)) {
        if (ret_value) {
            *ret_value = env->mscratch;
        }
        env->mscratch = rs1;
    } else if (ret_value) {
        *ret_value = rs1;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_sscratchcsw(CPURISCVState *env, int csrno,
                                      target_ulong *ret_value,
                                      target_ulong new_value,
                                      target_ulong write_mask)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    target_ulong rs1 = (env->sscratch & ~write_mask) | (new_value & write_mask);

    if (env->priv == PRV_S) {
        /* Supervisor mode: check sstatus.SPP */
        target_ulong spp = get_field(env->mstatus, SSTATUS_SPP);

        if (spp) {
            /* sstatus.SPP is set: rd = rs1, sscratch unchanged */
            if (ret_value) {
                *ret_value = rs1;
            }
        } else {
            /* sstatus.SPP is clear: swap sscratch and rs1 */
            if (ret_value) {
                *ret_value = env->sscratch;
            }
            env->sscratch = rs1;
        }
    } else if (env->priv == PRV_M) {
        /* Machine mode: check mstatus.MPP */
        target_ulong mpp = get_field(env->mstatus, MSTATUS_MPP);
        if (mpp == PRV_M) {
            /* MPP is Machine: rd = rs1, sscratch unchanged */
            if (ret_value) {
                *ret_value = rs1;
            }
        } else {
            /* MPP is Supervisor: swap sscratch and rs1 */
            if (ret_value) {
                *ret_value = env->sscratch;
            }
            env->sscratch = rs1;
        }
    } else {
        g_assert_not_reached();
    }
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_sscratchcsl(CPURISCVState *env, int csrno,
                                      target_ulong *ret_value,
                                      target_ulong new_value,
                                      target_ulong write_mask)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    target_ulong rs1 = (env->sscratch & ~write_mask) | (new_value & write_mask);
    target_ulong spil = get_field(env->scause, SCAUSE_SPIL);
    target_ulong sil = get_field(env->mintstatus, MINTSTATUS_SIL);
    target_ulong pil = get_field(env->mcause, MCAUSE_MPIL);
    target_ulong mil = get_field(env->mintstatus, MINTSTATUS_MIL);

    if (env->priv == PRV_S) {
        if ((spil == 0) != (sil == 0)) {
            if (ret_value) {
                *ret_value = env->sscratch;
            }
            env->sscratch = rs1;
        } else if (ret_value) {
            *ret_value = rs1;
        }
    } else if (env->priv == PRV_M) {
        if ((pil == 0) != (mil == 0)) {
            if (ret_value) {
                *ret_value = env->sscratch;
            }
            env->sscratch = rs1;
        } else if (ret_value) {
            *ret_value = rs1;
        }
    } else {
        g_assert_not_reached();
    }

    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mnxti(CPURISCVState *env, int csrno,
                                target_ulong *ret_value,
                                target_ulong new_value, target_ulong write_mask)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    target_ulong clic_il, clic_irq;
    target_ulong suitable_irq = 0;

    if (!riscv_clic_is_clic_mode(env)) {
        /*
        * The system is not in a CLIC mode.
        * According to hardware implementation, snxti and mnxti registers
        * are accessible even in non-CLIC mode, so we return 0 instead of
        * raising an illegal instruction exception.
        */
       if (ret_value) {
           *ret_value = 0;
       }
        return RISCV_EXCP_NONE;
    }

    if (write_mask) {
        env->mstatus |= new_value & (write_mask & MSTATUS_MIE);
    }

    BQL_LOCK_GUARD();

    if (csrno == CSR_MNXTI) {
        suitable_irq = riscv_clic_find_suitable_interrupt(env, PRV_M);
    } else if (csrno == CSR_SNXTI) {
        suitable_irq = riscv_clic_find_suitable_interrupt(env, PRV_S);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                        "CSR: rmw xnxti with unsupported mode\n");
        exit(1);
    }

    if (suitable_irq) {
        riscv_clic_decode_exccode(suitable_irq, NULL, &clic_il, &clic_irq);
        if (write_mask) {
            bool edge = riscv_clic_edge_triggered(env, clic_irq);
            if (edge) {
                riscv_clic_clean_pending(env, clic_irq);
            }
            if (csrno == CSR_MNXTI) {
                env->mintstatus = set_field(env->mintstatus,
                                            MINTSTATUS_MIL, clic_il);
                env->mcause = set_field(env->mcause, MCAUSE_EXCCODE, clic_irq);
            } else {
                env->mintstatus = set_field(env->mintstatus,
                                            MINTSTATUS_SIL, clic_il);
                env->scause = set_field(env->scause, MCAUSE_EXCCODE, clic_irq);
            }
        }
        if (ret_value) {
            *ret_value = (((csrno == CSR_MNXTI) ? env->mtvt : env->stvt) &
                          ~0x3f) + sizeof(target_ulong) * clic_irq;
        }
    } else {
        /* No interrupt or Not in a CLIC mode */
        if (ret_value) {
            *ret_value = 0;
        }
    }
    return RISCV_EXCP_NONE;
}

static int read_stvt(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->stvt;
    return RISCV_EXCP_NONE;
}

static int write_stvt(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!clic_device_exists(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    if (riscv_cpu_sxl(env) == MXL_RV32) {
        env->stvt = val & STVT_MASK_RV32;
    } else {
        env->stvt = val & STVT_MASK_RV64;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_snxti(CPURISCVState *env, int csrno,
                                target_ulong *ret_value,
                                target_ulong new_value, target_ulong write_mask)
{
    return rmw_mnxti(env, csrno, ret_value, new_value, write_mask);
}

static RISCVException
rmw_rplcntl(CPURISCVState *env, int csrno,
            target_ulong *ret_val, target_ulong new_val, target_ulong wr_mask)
{
    return RISCV_EXCP_NONE;
}

static RISCVException
rmw_cachelock(CPURISCVState *env, int csrno,
            target_ulong *ret_val, target_ulong new_val, target_ulong wr_mask)
{
    return RISCV_EXCP_NONE;
}

static RISCVException
rmw_cacherplpri(CPURISCVState *env, int csrno,
                target_ulong *ret_val, target_ulong new_val, target_ulong wr_mask)
{
    return RISCV_EXCP_NONE;
}
#endif

static RISCVException zicfiss_ssp(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_cfg(env)->ext_zicfiss) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#if !defined(CONFIG_USER_ONLY)
    bool virt = env->virt_enabled;

    if (env->priv < PRV_M && (env->menvcfg & MENVCFG_SSE) == 0) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (virt) {
        if (env->priv == PRV_S && (env->henvcfg & HENVCFG_SSE) == 0) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        if (env->priv == PRV_U && ((env->senvcfg & SENVCFG_SSE) == 0 ||
                                   (env->henvcfg & HENVCFG_SSE) == 0)) {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    if (env->priv == PRV_U && (env->senvcfg & SENVCFG_SSE) == 0) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException
read_ssp(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->ssp & (~0x11ULL);
    return RISCV_EXCP_NONE;
}

static RISCVException
write_ssp(CPURISCVState *env, int csrno, target_ulong val)
{
    env->ssp = val & (~0x11ULL);
    return RISCV_EXCP_NONE;
}

static RISCVException
read_xmdmaidle(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->xmdmaidle & XMDMAIDLE_IDLE_MASK;
    return RISCV_EXCP_NONE;
}

static RISCVException
read_xmsyncdsr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->tpe_sync_done;
    return RISCV_EXCP_NONE;
}

static RISCVException
read_xmsynctr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->xmsynctr & (XMSYNCTR_CHAIN_NEXT_ID | XMSYNCTR_CHAIN_HEAD |
                            XMSYNCTR_SEND_SEMA);;
    return RISCV_EXCP_NONE;
}


/*
 * Checks if all harts in the synchronization chain have marked themselves
 * as ready. This function traverses the circular list of harts.
 */
static bool is_sync_chain_complete(int start_hartid)
{
    CPUState *cpu = cpu_by_arch_id(start_hartid);
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *next_env = &rvcpu->env;

    if (!qatomic_load_acquire(&next_env->tpe_self_done)) {
        return false;
    }

    /* Traverse the circular chain until we return to the start. */
    while (cpu != current_cpu) {
        /*
         * Reading tpe_next_hartid after the acquire load is safe because
         * the acquire semantics prevent memory reordering.
         */
        cpu = cpu_by_arch_id(next_env->tpe_next_hartid);
        rvcpu = RISCV_CPU(cpu);
        next_env = &rvcpu->env;
        /*
         * Use an acquire load to check the flag. This ensures that if we
         * read 'true', we are guaranteed to see the 'tpe_next_hartid'
         * value that was written before the flag was set.
         */
        if (!qatomic_load_acquire(&next_env->tpe_self_done)) {
            return false;
        }
    }

    return true;
}

/*
 * Broadcasts the synchronization completion signal (tpe_sync_done = true)
 * to all harts in the chain.
 */
static void broadcast_sync_completion(CPURISCVState *env)
{
    int next_hartid = env->tpe_next_hartid;
    CPUState *cpu = cpu_by_arch_id(next_hartid);

    /* Use a release store to make the completion signal visible. */
    qatomic_store_release(&env->tpe_sync_done, true);

    /* Traverse the chain and set the flag on all other harts. */
    while (cpu != current_cpu) {
        RISCVCPU *rvcpu = RISCV_CPU(cpu);
        CPURISCVState *next_env = &rvcpu->env;

        qatomic_store_release(&next_env->tpe_sync_done, true);
        cpu = cpu_by_arch_id(next_env->tpe_next_hartid);
    }
}

/*
 * Handles writes to the xmsynctr CSR to coordinate hart synchronization.
 * This is the entry point for a hart initiating a sync operation.
 */
static RISCVException write_xmsynctr(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    int send_sema = (int)get_field(val, XMSYNCTR_SEND_SEMA);
    int next_hartid = (int)get_field(val, XMSYNCTR_CHAIN_NEXT_ID);
    env->xmsynctr = val & (XMSYNCTR_CHAIN_NEXT_ID | XMSYNCTR_CHAIN_HEAD |
                           XMSYNCTR_SEND_SEMA);
    /* Exit early if this write is not a synchronization start signal. */
    if (!send_sema) {
        return RISCV_EXCP_NONE;
    }

    /*
     * Prepare this hart's state. The write to 'tpe_next_hartid' is a
     * plain store, as its visibility is guaranteed by the release below.
     */
    env->tpe_sync_done = false;
    env->tpe_next_hartid = next_hartid;

    /*
     * Use a release store to set the 'self_done' flag. This ensures
     * that the write to 'tpe_next_hartid' above is visible to other
     * harts before this flag is seen as true.
     */
    qatomic_store_release(&env->tpe_self_done, true);

    /*
     * Check if all other harts in the synchronization chain are also ready.
     * If not, this hart has done its part and will simply wait.
     */
    if (!is_sync_chain_complete(next_hartid)) {
        return RISCV_EXCP_NONE;
    }
    smp_mb();
    /*
     * All harts are now ready. This hart, being the last to signal,
     * will broadcast the final completion signal to all harts.
     */
    broadcast_sync_completion(env);

    return RISCV_EXCP_NONE;
}

#if !defined(CONFIG_USER_ONLY)
static RISCVException
read_xmdmaerrinfo(CPURISCVState *env, int csrno, target_ulong *val)
{
    uint64_t mask = XMDMAERRINFO_VEC_MASK | XMDMAERRINFO_TYPE_MASK |
                    XMDMAERRINFO_ID_MASK | XMDMAERRINFO_V_MASK;
    *val = env->xmdmaerrinfo & mask;
    return RISCV_EXCP_NONE;
}

static RISCVException
write_xmdmaerrinfo(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = XMDMAERRINFO_VEC_MASK | XMDMAERRINFO_TYPE_MASK |
                    XMDMAERRINFO_ID_MASK | XMDMAERRINFO_V_MASK;
    env->xmdmaerrinfo = (env->xmdmaerrinfo & ~mask) | (val & mask);
    return RISCV_EXCP_NONE;
}
#endif

/*
 * Control and Status Register function table
 * riscv_csr_operations::predicate() must be provided for an implemented CSR
 */
riscv_csr_operations csr_ops[CSR_TABLE_SIZE] = {
    /* User Floating-Point CSRs */
    [CSR_FFLAGS]   = { "fflags",   fs,     read_fflags,  write_fflags },
    [CSR_FRM]      = { "frm",      fs,     read_frm,     write_frm    },
    [CSR_FCSR]     = { "fcsr",     fs,     read_fcsr,    write_fcsr   },
    /* Vector CSRs */
    [CSR_VSTART]   = { "vstart",   vs,     read_vstart,  write_vstart },
    [CSR_VXSAT]    = { "vxsat",    vs,     read_vxsat,   write_vxsat  },
    [CSR_VXRM]     = { "vxrm",     vs,     read_vxrm,    write_vxrm   },
    [CSR_VCSR]     = { "vcsr",     vs,     read_vcsr,    write_vcsr   },
    [CSR_VL]       = { "vl",       vs,     read_vl                    },
    [CSR_VTYPE]    = { "vtype",    vs,     read_vtype                 },
    [CSR_VLENB]    = { "vlenb",    vs,     read_vlenb                 },
    /* User Timers and Counters */
    [CSR_CYCLE]    = { "cycle",    ctr,    read_hpmcounter  },
    [CSR_INSTRET]  = { "instret",  ctr,    read_hpmcounter  },
    [CSR_CYCLEH]   = { "cycleh",   ctr32,  read_hpmcounterh },
    [CSR_INSTRETH] = { "instreth", ctr32,  read_hpmcounterh },

    /*
     * In privileged mode, the monitor will have to emulate TIME CSRs only if
     * rdtime callback is not provided by machine/platform emulation.
     */
    [CSR_TIME]  = { "time",  ctr,   read_time  },
    [CSR_TIMEH] = { "timeh", ctr32, read_timeh },

    /* Crypto Extension */
    [CSR_SEED] = { "seed", seed, NULL, NULL, rmw_seed },

    /* Zcmt Extension */
    [CSR_JVT] = {"jvt", zcmt, read_jvt, write_jvt},

    /* Matrix CSRs */
    [CSR_MRSTART]  = { "xmrstart", ms,    read_xmrstart, write_xmrstart},
    [CSR_MCSR]     = { "xmcsr",    ms,    read_xmcsr,    write_xmcsr   },
    [CSR_MSIZE]    = { "xmsize",   ms,    read_xmsize,   write_xmsize  },
    [CSR_MREGSIZE] = { "xmlenb",   ms,    read_xmlenb                },
    [CSR_MLENB]    = { "xrlenb",   ms,    read_xrlenb                },
    [CSR_XMISA]    = { "xmisa",    ms,    read_xmisa                 },

    /* Matrix v0.5 CSRs */
    [CSR_XMXRM]     = { "xmxrm",    ms,    read_xmxrm,    write_xmxrm   },
    [CSR_XMSAT]     = { "xmsat",    ms,    read_xmsat,    write_xmsat   },
    [CSR_XMFFLAGS]  = { "xmfflags", ms,    read_xmfflags, write_xmfflags   },
    [CSR_XMFRM]     = { "xmfrm",    ms,    read_xmfrm,    write_xmfrm   },
    [CSR_XMSATEN]   = { "xmsaten",  ms,    read_xmsaten,  write_xmsaten   },
    [CSR_XMXADESC]  = { "xmxadesc", ms,    read_xmxadesc, write_xmxadesc   },
    [CSR_XMXBDESC]  = { "xmxbdesc", ms,    read_xmxbdesc, write_xmxbdesc   },
    [CSR_XMDMAIDLE] = { "xmdmaidle",  ms,  read_xmdmaidle },
    [CSR_XMSYNCTR] =  { "xmsynctr",  any,  read_xmsynctr, write_xmsynctr},
    [CSR_XMSYNCDSR] = { "xmsyncdsr",  any,  read_xmsyncdsr},

    /* Xuantie CSRs */
    [CSR_FXCR] =     { "fxcr",     fs,     read_fxcr,    write_fxcr   },
    /* Shadow Stack Pointer */
    [CSR_SSP]  = { "ssp",  zicfiss_ssp, read_ssp,  write_ssp },

#if !defined(CONFIG_USER_ONLY)
    /* Machine Timers and Counters */
    [CSR_MCYCLE]    = { "mcycle",    any,   read_hpmcounter,
                        write_mhpmcounter                    },
    [CSR_MINSTRET]  = { "minstret",  any,   read_hpmcounter,
                        write_mhpmcounter                    },
    [CSR_MCYCLEH]   = { "mcycleh",   any32, read_hpmcounterh,
                        write_mhpmcounterh                   },
    [CSR_MINSTRETH] = { "minstreth", any32, read_hpmcounterh,
                        write_mhpmcounterh                   },

    /* Machine Information Registers */
    [CSR_MVENDORID] = { "mvendorid", any,   read_mvendorid },
    [CSR_MARCHID]   = { "marchid",   any,   read_marchid   },
    [CSR_MIMPID]    = { "mimpid",    any,   read_mimpid    },
    [CSR_MHARTID]   = { "mhartid",   any,   read_mhartid   },

    [CSR_MCONFIGPTR]  = { "mconfigptr", any,   read_zero,
                          .min_priv_ver = PRIV_VERSION_1_12_0 },
    /* Machine Trap Setup */
    [CSR_MSTATUS]     = { "mstatus",    any,   read_mstatus, write_mstatus,
                          NULL,                read_mstatus_i128           },
    [CSR_MISA]        = { "misa",       any,   read_misa,    write_misa,
                          NULL,                read_misa_i128              },
    [CSR_MIDELEG]     = { "mideleg",    any,   NULL, NULL,   rmw_mideleg   },
    [CSR_MEDELEG]     = { "medeleg",    any,   read_medeleg, write_medeleg },
    [CSR_MIE]         = { "mie",        any,   NULL, NULL,   rmw_mie       },
    [CSR_MTVEC]       = { "mtvec",      any,   read_mtvec,   write_mtvec   },
    [CSR_MCOUNTEREN]  = { "mcounteren", umode, read_mcounteren,
                          write_mcounteren                                 },

    [CSR_MSTATUSH]    = { "mstatush",   any32, read_mstatush,
                          write_mstatush                                   },
    [CSR_MEDELEGH]    = { "medelegh",   any32, read_zero, write_ignore,
                          .min_priv_ver = PRIV_VERSION_1_13_0              },
    [CSR_HEDELEGH]    = { "hedelegh",   hmode32, read_hedelegh, write_hedelegh,
                          .min_priv_ver = PRIV_VERSION_1_13_0              },

    /* Machine Trap Handling */
    [CSR_MSCRATCH] = { "mscratch", any,  read_mscratch, write_mscratch,
                       NULL, read_mscratch_i128, write_mscratch_i128   },
    [CSR_MEPC]     = { "mepc",     any,  read_mepc,     write_mepc     },
    [CSR_MCAUSE]   = { "mcause",   any,  read_mcause,   write_mcause   },
    [CSR_MTVAL]    = { "mtval",    any,  read_mtval,    write_mtval    },
    [CSR_MIP]      = { "mip",      any,  NULL,    NULL, rmw_mip        },

    /* Machine-Level Window to Indirectly Accessed Registers (AIA) */
    [CSR_MISELECT] = { "miselect", sxcsrind_or_aia_any,   NULL, NULL,
                       rmw_xiselect                                    },
    [CSR_MIREG]    = { "mireg",    sxcsrind_or_aia_any,   NULL, NULL,
                       rmw_xireg                                       },

    /* Machine Indirect Register Alias */
    [CSR_MIREG2]   = { "mireg2", sxcsrind_any, NULL, NULL, rmw_xiregi,
                       .min_priv_ver = PRIV_VERSION_1_12_0          },
    [CSR_MIREG3]   = { "mireg3", sxcsrind_any, NULL, NULL, rmw_xiregi,
                       .min_priv_ver = PRIV_VERSION_1_12_0          },
    [CSR_MIREG4]   = { "mireg4", sxcsrind_any, NULL, NULL, rmw_xiregi,
                       .min_priv_ver = PRIV_VERSION_1_12_0          },
    [CSR_MIREG5]   = { "mireg5", sxcsrind_any, NULL, NULL, rmw_xiregi,
                       .min_priv_ver = PRIV_VERSION_1_12_0          },
    [CSR_MIREG6]   = { "mireg6", sxcsrind_any, NULL, NULL, rmw_xiregi,
                       .min_priv_ver = PRIV_VERSION_1_12_0          },

    /* Machine-Level Interrupts (AIA) */
    [CSR_MTOPEI]   = { "mtopei",   aia_any, NULL, NULL, rmw_xtopei },
    [CSR_MTOPI]    = { "mtopi",    aia_any, read_mtopi },

    /* Virtual Interrupts for Supervisor Level (AIA) */
    [CSR_MVIEN]    = { "mvien",    aia_any, NULL, NULL, rmw_mvien   },
    [CSR_MVIP]     = { "mvip",     aia_any, NULL, NULL, rmw_mvip    },

    /* Machine-Level High-Half CSRs (AIA) */
    [CSR_MIDELEGH] = { "midelegh", aia_any32, NULL, NULL, rmw_midelegh },
    [CSR_MIEH]     = { "mieh",     aia_any32, NULL, NULL, rmw_mieh     },
    [CSR_MVIENH]   = { "mvienh",   aia_any32, NULL, NULL, rmw_mvienh   },
    [CSR_MVIPH]    = { "mviph",    aia_any32, NULL, NULL, rmw_mviph    },
    [CSR_MIPH]     = { "miph",     aia_any32, NULL, NULL, rmw_miph     },

    /* Execution environment configuration */
    [CSR_MENVCFG]  = { "menvcfg",  umode, read_menvcfg,  write_menvcfg,
                       .min_priv_ver = PRIV_VERSION_1_12_0              },
    [CSR_MENVCFGH] = { "menvcfgh", umode32, read_menvcfgh, write_menvcfgh,
                       .min_priv_ver = PRIV_VERSION_1_12_0              },
    [CSR_SENVCFG]  = { "senvcfg",  smode, read_senvcfg,  write_senvcfg,
                       .min_priv_ver = PRIV_VERSION_1_12_0              },
    [CSR_HENVCFG]  = { "henvcfg",  hmode, read_henvcfg, write_henvcfg,
                       .min_priv_ver = PRIV_VERSION_1_12_0              },
    [CSR_HENVCFGH] = { "henvcfgh", hmode32, read_henvcfgh, write_henvcfgh,
                       .min_priv_ver = PRIV_VERSION_1_12_0              },

    /* Smstateen extension CSRs */
    [CSR_MSTATEEN0] = { "mstateen0", mstateen, read_mstateen, write_mstateen0,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_MSTATEEN0H] = { "mstateen0h", mstateen, read_mstateenh,
                          write_mstateen0h,
                         .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_MSTATEEN1] = { "mstateen1", mstateen, read_mstateen,
                        write_mstateen_1_3,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_MSTATEEN1H] = { "mstateen1h", mstateen, read_mstateenh,
                         write_mstateenh_1_3,
                         .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_MSTATEEN2] = { "mstateen2", mstateen, read_mstateen,
                        write_mstateen_1_3,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_MSTATEEN2H] = { "mstateen2h", mstateen, read_mstateenh,
                         write_mstateenh_1_3,
                         .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_MSTATEEN3] = { "mstateen3", mstateen, read_mstateen,
                        write_mstateen_1_3,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_MSTATEEN3H] = { "mstateen3h", mstateen, read_mstateenh,
                         write_mstateenh_1_3,
                         .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HSTATEEN0] = { "hstateen0", hstateen, read_hstateen, write_hstateen0,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HSTATEEN0H] = { "hstateen0h", hstateenh, read_hstateenh,
                         write_hstateen0h,
                         .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HSTATEEN1] = { "hstateen1", hstateen, read_hstateen,
                        write_hstateen_1_3,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HSTATEEN1H] = { "hstateen1h", hstateenh, read_hstateenh,
                         write_hstateenh_1_3,
                         .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HSTATEEN2] = { "hstateen2", hstateen, read_hstateen,
                        write_hstateen_1_3,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HSTATEEN2H] = { "hstateen2h", hstateenh, read_hstateenh,
                         write_hstateenh_1_3,
                         .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HSTATEEN3] = { "hstateen3", hstateen, read_hstateen,
                        write_hstateen_1_3,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_HSTATEEN3H] = { "hstateen3h", hstateenh, read_hstateenh,
                         write_hstateenh_1_3,
                         .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_SSTATEEN0] = { "sstateen0", sstateen, read_sstateen, write_sstateen0,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_SSTATEEN1] = { "sstateen1", sstateen, read_sstateen,
                        write_sstateen_1_3,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_SSTATEEN2] = { "sstateen2", sstateen, read_sstateen,
                        write_sstateen_1_3,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_SSTATEEN3] = { "sstateen3", sstateen, read_sstateen,
                        write_sstateen_1_3,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },

    /* Supervisor Counter Delegation */
    [CSR_SCOUNTINHIBIT] = {"scountinhibit", scountinhibit_pred,
                            read_scountinhibit, write_scountinhibit,
                           .min_priv_ver = PRIV_VERSION_1_12_0 },

    /* Supervisor Trap Setup */
    [CSR_SSTATUS]    = { "sstatus",    smode, read_sstatus,    write_sstatus,
                         NULL,                read_sstatus_i128              },
    [CSR_SIE]        = { "sie",        smode, NULL,   NULL,    rmw_sie       },
    [CSR_STVEC]      = { "stvec",      smode, read_stvec,      write_stvec   },
    [CSR_SCOUNTEREN] = { "scounteren", smode, read_scounteren,
                         write_scounteren                                    },

    /* Supervisor Trap Handling */
    [CSR_SSCRATCH] = { "sscratch", smode, read_sscratch, write_sscratch,
                       NULL, read_sscratch_i128, write_sscratch_i128    },
    [CSR_SEPC]     = { "sepc",     smode, read_sepc,     write_sepc     },
    [CSR_SCAUSE]   = { "scause",   smode, read_scause,   write_scause   },
    [CSR_STVAL]    = { "stval",    smode, read_stval,    write_stval    },
    [CSR_SIP]      = { "sip",      smode, NULL,    NULL, rmw_sip        },
    [CSR_STIMECMP] = { "stimecmp", sstc, read_stimecmp, write_stimecmp,
                       .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_STIMECMPH] = { "stimecmph", sstc_32, read_stimecmph, write_stimecmph,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_VSTIMECMP] = { "vstimecmp", sstc, read_vstimecmp,
                        write_vstimecmp,
                        .min_priv_ver = PRIV_VERSION_1_12_0 },
    [CSR_VSTIMECMPH] = { "vstimecmph", sstc_32, read_vstimecmph,
                         write_vstimecmph,
                         .min_priv_ver = PRIV_VERSION_1_12_0 },

    /* Supervisor Protection and Translation */
    [CSR_SATP]     = { "satp",     satp, read_satp,     write_satp     },

    /* Supervisor-Level Quality of Service Identifier */
    [CSR_SQOSCFG]  = { "sqoscfg",  check_sqoscfg, read_sqoscfg, write_sqoscfg },

    /* Supervisor-Level Window to Indirectly Accessed Registers (AIA) */
    [CSR_SISELECT]   = { "siselect",   sxcsrind_or_aia_smode, NULL, NULL,
                         rmw_xiselect                                       },
    [CSR_SIREG]      = { "sireg",      sxcsrind_or_aia_smode, NULL, NULL,
                         rmw_xireg                                          },

    /* Supervisor Indirect Register Alias */
    [CSR_SIREG2]      = { "sireg2", sxcsrind_smode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_SIREG3]      = { "sireg3", sxcsrind_smode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_SIREG4]      = { "sireg4", sxcsrind_smode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_SIREG5]      = { "sireg5", sxcsrind_smode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_SIREG6]      = { "sireg6", sxcsrind_smode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },

    /* Supervisor-Level Interrupts (AIA) */
    [CSR_STOPEI]     = { "stopei",     aia_smode, NULL, NULL, rmw_xtopei },
    [CSR_STOPI]      = { "stopi",      aia_smode, read_stopi },

    /* Supervisor-Level High-Half CSRs (AIA) */
    [CSR_SIEH]       = { "sieh",   aia_smode32, NULL, NULL, rmw_sieh },
    [CSR_SIPH]       = { "siph",   aia_smode32, NULL, NULL, rmw_siph },

    [CSR_HSTATUS]     = { "hstatus",     hmode,   read_hstatus, write_hstatus,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HEDELEG]     = { "hedeleg",     hmode,   read_hedeleg, write_hedeleg,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HIDELEG]     = { "hideleg",     hmode,   NULL,   NULL, rmw_hideleg,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HVIP]        = { "hvip",        hmode,   NULL,   NULL, rmw_hvip,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HIP]         = { "hip",         hmode,   NULL,   NULL, rmw_hip,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HIE]         = { "hie",         hmode,   NULL,   NULL, rmw_hie,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HCOUNTEREN]  = { "hcounteren",  hmode,   read_hcounteren,
                          write_hcounteren,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HGEIE]       = { "hgeie",       hmode,   read_hgeie,   write_hgeie,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HTVAL]       = { "htval",       hmode,   read_htval,   write_htval,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HTINST]      = { "htinst",      hmode,   read_htinst,  write_htinst,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HGEIP]       = { "hgeip",       hmode,   read_hgeip,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HGATP]       = { "hgatp",       hgatp,   read_hgatp,   write_hgatp,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HTIMEDELTA]  = { "htimedelta",  hmode,   read_htimedelta,
                          write_htimedelta,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_HTIMEDELTAH] = { "htimedeltah", hmode32, read_htimedeltah,
                          write_htimedeltah,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },

    [CSR_VSSTATUS]    = { "vsstatus",    hmode,   read_vsstatus,
                          write_vsstatus,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSIP]        = { "vsip",        hmode,   NULL,    NULL, rmw_vsip,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSIE]        = { "vsie",        hmode,   NULL,    NULL, rmw_vsie ,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSTVEC]      = { "vstvec",      hmode,   read_vstvec,   write_vstvec,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSSCRATCH]   = { "vsscratch",   hmode,   read_vsscratch,
                          write_vsscratch,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSEPC]       = { "vsepc",       hmode,   read_vsepc,    write_vsepc,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSCAUSE]     = { "vscause",     hmode,   read_vscause,  write_vscause,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSTVAL]      = { "vstval",      hmode,   read_vstval,   write_vstval,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSATP]       = { "vsatp",       hmode,   read_vsatp,    write_vsatp,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },

    [CSR_MTVAL2]      = { "mtval2",      dbltrp_hmode,   read_mtval2,   write_mtval2,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_MTINST]      = { "mtinst",      hmode,   read_mtinst,   write_mtinst,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },

    /* Virtual Interrupts and Interrupt Priorities (H-extension with AIA) */
    [CSR_HVIEN]       = { "hvien",       aia_hmode, NULL, NULL, rmw_hvien },
    [CSR_HVICTL]      = { "hvictl",      aia_hmode, read_hvictl,
                          write_hvictl                                      },
    [CSR_HVIPRIO1]    = { "hviprio1",    aia_hmode, read_hviprio1,
                          write_hviprio1                                    },
    [CSR_HVIPRIO2]    = { "hviprio2",    aia_hmode, read_hviprio2,
                          write_hviprio2                                    },
    /*
     * VS-Level Window to Indirectly Accessed Registers (H-extension with AIA)
     */
    [CSR_VSISELECT]   = { "vsiselect",   sxcsrind_or_aia_hmode, NULL, NULL,
                          rmw_xiselect                                      },
    [CSR_VSIREG]      = { "vsireg",      sxcsrind_or_aia_hmode, NULL, NULL,
                          rmw_xireg                                         },

    /* Virtual Supervisor Indirect Alias */
    [CSR_VSIREG2]     = { "vsireg2", sxcsrind_hmode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSIREG3]     = { "vsireg3", sxcsrind_hmode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSIREG4]     = { "vsireg4", sxcsrind_hmode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSIREG5]     = { "vsireg5", sxcsrind_hmode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },
    [CSR_VSIREG6]     = { "vsireg6", sxcsrind_hmode, NULL, NULL, rmw_xiregi,
                          .min_priv_ver = PRIV_VERSION_1_12_0                },

    /* VS-Level Interrupts (H-extension with AIA) */
    [CSR_VSTOPEI]     = { "vstopei",     aia_hmode, NULL, NULL, rmw_xtopei },
    [CSR_VSTOPI]      = { "vstopi",      aia_hmode, read_vstopi },

    /* Hypervisor and VS-Level High-Half CSRs (H-extension with AIA) */
    [CSR_HIDELEGH]    = { "hidelegh",    aia_hmode32, NULL, NULL,
                          rmw_hidelegh                                      },
    [CSR_HVIENH]      = { "hvienh",      aia_hmode32, NULL, NULL, rmw_hvienh },
    [CSR_HVIPH]       = { "hviph",       aia_hmode32, NULL, NULL, rmw_hviph },
    [CSR_HVIPRIO1H]   = { "hviprio1h",   aia_hmode32, read_hviprio1h,
                          write_hviprio1h                                   },
    [CSR_HVIPRIO2H]   = { "hviprio2h",   aia_hmode32, read_hviprio2h,
                          write_hviprio2h                                   },
    [CSR_VSIEH]       = { "vsieh",       aia_hmode32, NULL, NULL, rmw_vsieh },
    [CSR_VSIPH]       = { "vsiph",       aia_hmode32, NULL, NULL, rmw_vsiph },

    /* Physical Memory Protection */
    [CSR_MSECCFG]    = { "mseccfg",   have_mseccfg, read_mseccfg, write_mseccfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_MSECCFGH]   = { "mseccfgh",   have_mseccfg, read_mseccfgh, write_mseccfgh,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG0]    = { "pmpcfg0",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG1]    = { "pmpcfg1",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG2]    = { "pmpcfg2",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG3]    = { "pmpcfg3",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG4]    = { "pmpcfg4",   pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG5]    = { "pmpcfg5",   pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG6]    = { "pmpcfg6",   pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG7]    = { "pmpcfg7",   pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG8]    = { "pmpcfg8",   pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG9]    = { "pmpcfg9",   pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG10]   = { "pmpcfg10",  pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG11]   = { "pmpcfg11",  pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG12]   = { "pmpcfg12",  pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG13]   = { "pmpcfg13",  pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG14]   = { "pmpcfg14",  pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPCFG15]   = { "pmpcfg15",  pmp, read_pmpcfg,  write_pmpcfg,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
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
    [CSR_PMPADDR14]  = { "pmpaddr14", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR15]  = { "pmpaddr15", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR16]  = { "pmpaddr16", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR17]  = { "pmpaddr17", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR18]  = { "pmpaddr18", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR19]  = { "pmpaddr19", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR20]  = { "pmpaddr20", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR21]  = { "pmpaddr21", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR22]  = { "pmpaddr22", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR23]  = { "pmpaddr23", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR24]  = { "pmpaddr24", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR25]  = { "pmpaddr25", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR26]  = { "pmpaddr26", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR27]  = { "pmpaddr27", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR28]  = { "pmpaddr28", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR29]  = { "pmpaddr29", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR30]  = { "pmpaddr30", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR31]  = { "pmpaddr31", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR32]  = { "pmpaddr32", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR33]  = { "pmpaddr33", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR34]  = { "pmpaddr34", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR35]  = { "pmpaddr35", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR36]  = { "pmpaddr36", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR37]  = { "pmpaddr37", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR38]  = { "pmpaddr38", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR39]  = { "pmpaddr39", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR40]  = { "pmpaddr40", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR41]  = { "pmpaddr41", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR42]  = { "pmpaddr42", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR43]  = { "pmpaddr43", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR44]  = { "pmpaddr44", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR45]  = { "pmpaddr45", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR46]  = { "pmpaddr46", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR47]  = { "pmpaddr47", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR48]  = { "pmpaddr48", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR49]  = { "pmpaddr49", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR50]  = { "pmpaddr50", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR51]  = { "pmpaddr51", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR52]  = { "pmpaddr52", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR53]  = { "pmpaddr53", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR54]  = { "pmpaddr54", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR55]  = { "pmpaddr55", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR56]  = { "pmpaddr56", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR57]  = { "pmpaddr57", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR58]  = { "pmpaddr58", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR59]  = { "pmpaddr59", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR60]  = { "pmpaddr60", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR61]  = { "pmpaddr61", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR62]  = { "pmpaddr62", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    [CSR_PMPADDR63]  = { "pmpaddr63", pmp, read_pmpaddr, write_pmpaddr,
                         .min_priv_ver = PRIV_VERSION_1_12_0           },
    /* Debug CSRs */
    [CSR_TSELECT]   =  { "tselect",  debug, read_tselect,  write_tselect  },
    [CSR_TDATA1]    =  { "tdata1",   debug, read_tdata,    write_tdata    },
    [CSR_TDATA2]    =  { "tdata2",   debug, read_tdata,    write_tdata    },
    [CSR_TDATA3]    =  { "tdata3",   debug, read_tdata,    write_tdata    },
    [CSR_TINFO]     =  { "tinfo",    debug, read_tinfo,    write_ignore   },
    [CSR_MCONTEXT]  =  { "mcontext", debug, read_mcontext, write_mcontext },
    [CSR_SCONTEXT]  =  { "scontext", sctx, read_scontext, write_scontext },

    [CSR_MCTRCTL]       = { "mctrctl",       ctr_mmode, NULL, NULL,
                                rmw_mctrctl },
    [CSR_SCTRCTL]       = { "sctrctl",       ctr_smode, NULL, NULL,
                                rmw_sctrctl },
    [CSR_SCTRDEPTH]       = { "sctrdepth",       ctr_smode, NULL, NULL,
                                rmw_sctrdepth },
    [CSR_SCTRSTATUS]       = { "sctrstatus",       ctr_smode, NULL, NULL,
                                rmw_sctrstatus },
    [CSR_VSCTRCTL]      = { "vsctrctl",      ctr_vsmode, NULL, NULL,
                                rmw_vsctrctl },
    /* Performance Counters */
    [CSR_HPMCOUNTER3]    = { "hpmcounter3",    ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER4]    = { "hpmcounter4",    ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER5]    = { "hpmcounter5",    ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER6]    = { "hpmcounter6",    ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER7]    = { "hpmcounter7",    ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER8]    = { "hpmcounter8",    ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER9]    = { "hpmcounter9",    ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER10]   = { "hpmcounter10",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER11]   = { "hpmcounter11",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER12]   = { "hpmcounter12",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER13]   = { "hpmcounter13",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER14]   = { "hpmcounter14",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER15]   = { "hpmcounter15",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER16]   = { "hpmcounter16",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER17]   = { "hpmcounter17",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER18]   = { "hpmcounter18",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER19]   = { "hpmcounter19",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER20]   = { "hpmcounter20",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER21]   = { "hpmcounter21",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER22]   = { "hpmcounter22",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER23]   = { "hpmcounter23",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER24]   = { "hpmcounter24",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER25]   = { "hpmcounter25",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER26]   = { "hpmcounter26",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER27]   = { "hpmcounter27",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER28]   = { "hpmcounter28",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER29]   = { "hpmcounter29",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER30]   = { "hpmcounter30",   ctr,    read_hpmcounter },
    [CSR_HPMCOUNTER31]   = { "hpmcounter31",   ctr,    read_hpmcounter },

    [CSR_MHPMCOUNTER3]   = { "mhpmcounter3",   mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER4]   = { "mhpmcounter4",   mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER5]   = { "mhpmcounter5",   mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER6]   = { "mhpmcounter6",   mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER7]   = { "mhpmcounter7",   mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER8]   = { "mhpmcounter8",   mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER9]   = { "mhpmcounter9",   mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER10]  = { "mhpmcounter10",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER11]  = { "mhpmcounter11",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER12]  = { "mhpmcounter12",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER13]  = { "mhpmcounter13",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER14]  = { "mhpmcounter14",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER15]  = { "mhpmcounter15",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER16]  = { "mhpmcounter16",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER17]  = { "mhpmcounter17",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER18]  = { "mhpmcounter18",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER19]  = { "mhpmcounter19",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER20]  = { "mhpmcounter20",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER21]  = { "mhpmcounter21",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER22]  = { "mhpmcounter22",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER23]  = { "mhpmcounter23",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER24]  = { "mhpmcounter24",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER25]  = { "mhpmcounter25",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER26]  = { "mhpmcounter26",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER27]  = { "mhpmcounter27",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER28]  = { "mhpmcounter28",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER29]  = { "mhpmcounter29",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER30]  = { "mhpmcounter30",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },
    [CSR_MHPMCOUNTER31]  = { "mhpmcounter31",  mctr,    read_hpmcounter,
                             write_mhpmcounter                         },

    [CSR_MCOUNTINHIBIT]  = { "mcountinhibit",  any, read_mcountinhibit,
                             write_mcountinhibit,
                             .min_priv_ver = PRIV_VERSION_1_11_0       },

    [CSR_MCYCLECFG]      = { "mcyclecfg",   smcntrpmf, read_mcyclecfg,
                             write_mcyclecfg,
                             .min_priv_ver = PRIV_VERSION_1_12_0       },
    [CSR_MINSTRETCFG]    = { "minstretcfg", smcntrpmf, read_minstretcfg,
                             write_minstretcfg,
                             .min_priv_ver = PRIV_VERSION_1_12_0       },

    [CSR_MHPMEVENT3]     = { "mhpmevent3",     any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT4]     = { "mhpmevent4",     any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT5]     = { "mhpmevent5",     any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT6]     = { "mhpmevent6",     any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT7]     = { "mhpmevent7",     any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT8]     = { "mhpmevent8",     any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT9]     = { "mhpmevent9",     any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT10]    = { "mhpmevent10",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT11]    = { "mhpmevent11",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT12]    = { "mhpmevent12",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT13]    = { "mhpmevent13",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT14]    = { "mhpmevent14",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT15]    = { "mhpmevent15",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT16]    = { "mhpmevent16",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT17]    = { "mhpmevent17",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT18]    = { "mhpmevent18",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT19]    = { "mhpmevent19",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT20]    = { "mhpmevent20",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT21]    = { "mhpmevent21",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT22]    = { "mhpmevent22",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT23]    = { "mhpmevent23",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT24]    = { "mhpmevent24",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT25]    = { "mhpmevent25",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT26]    = { "mhpmevent26",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT27]    = { "mhpmevent27",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT28]    = { "mhpmevent28",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT29]    = { "mhpmevent29",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT30]    = { "mhpmevent30",    any,    read_mhpmevent,
                             write_mhpmevent                           },
    [CSR_MHPMEVENT31]    = { "mhpmevent31",    any,    read_mhpmevent,
                             write_mhpmevent                           },

    [CSR_MCYCLECFGH]     = { "mcyclecfgh",   smcntrpmf_32, read_mcyclecfgh,
                             write_mcyclecfgh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MINSTRETCFGH]   = { "minstretcfgh", smcntrpmf_32, read_minstretcfgh,
                             write_minstretcfgh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },

    [CSR_MHPMEVENT3H]    = { "mhpmevent3h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT4H]    = { "mhpmevent4h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT5H]    = { "mhpmevent5h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT6H]    = { "mhpmevent6h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT7H]    = { "mhpmevent7h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT8H]    = { "mhpmevent8h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT9H]    = { "mhpmevent9h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT10H]   = { "mhpmevent10h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT11H]   = { "mhpmevent11h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT12H]   = { "mhpmevent12h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT13H]   = { "mhpmevent13h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT14H]   = { "mhpmevent14h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT15H]   = { "mhpmevent15h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT16H]   = { "mhpmevent16h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT17H]   = { "mhpmevent17h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT18H]   = { "mhpmevent18h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT19H]   = { "mhpmevent19h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT20H]   = { "mhpmevent20h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT21H]   = { "mhpmevent21h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT22H]   = { "mhpmevent22h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT23H]   = { "mhpmevent23h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT24H]   = { "mhpmevent24h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT25H]   = { "mhpmevent25h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT26H]   = { "mhpmevent26h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT27H]   = { "mhpmevent27h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT28H]   = { "mhpmevent28h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT29H]   = { "mhpmevent29h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT30H]   = { "mhpmevent30h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },
    [CSR_MHPMEVENT31H]   = { "mhpmevent31h",    sscofpmf_32,  read_mhpmeventh,
                             write_mhpmeventh,
                             .min_priv_ver = PRIV_VERSION_1_12_0        },

    [CSR_HPMCOUNTER3H]   = { "hpmcounter3h",   ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER4H]   = { "hpmcounter4h",   ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER5H]   = { "hpmcounter5h",   ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER6H]   = { "hpmcounter6h",   ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER7H]   = { "hpmcounter7h",   ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER8H]   = { "hpmcounter8h",   ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER9H]   = { "hpmcounter9h",   ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER10H]  = { "hpmcounter10h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER11H]  = { "hpmcounter11h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER12H]  = { "hpmcounter12h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER13H]  = { "hpmcounter13h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER14H]  = { "hpmcounter14h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER15H]  = { "hpmcounter15h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER16H]  = { "hpmcounter16h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER17H]  = { "hpmcounter17h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER18H]  = { "hpmcounter18h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER19H]  = { "hpmcounter19h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER20H]  = { "hpmcounter20h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER21H]  = { "hpmcounter21h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER22H]  = { "hpmcounter22h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER23H]  = { "hpmcounter23h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER24H]  = { "hpmcounter24h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER25H]  = { "hpmcounter25h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER26H]  = { "hpmcounter26h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER27H]  = { "hpmcounter27h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER28H]  = { "hpmcounter28h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER29H]  = { "hpmcounter29h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER30H]  = { "hpmcounter30h",  ctr32,  read_hpmcounterh },
    [CSR_HPMCOUNTER31H]  = { "hpmcounter31h",  ctr32,  read_hpmcounterh },

    [CSR_MHPMCOUNTER3H]  = { "mhpmcounter3h",  mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER4H]  = { "mhpmcounter4h",  mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER5H]  = { "mhpmcounter5h",  mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER6H]  = { "mhpmcounter6h",  mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER7H]  = { "mhpmcounter7h",  mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER8H]  = { "mhpmcounter8h",  mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER9H]  = { "mhpmcounter9h",  mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER10H] = { "mhpmcounter10h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER11H] = { "mhpmcounter11h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER12H] = { "mhpmcounter12h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER13H] = { "mhpmcounter13h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER14H] = { "mhpmcounter14h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER15H] = { "mhpmcounter15h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER16H] = { "mhpmcounter16h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER17H] = { "mhpmcounter17h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER18H] = { "mhpmcounter18h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER19H] = { "mhpmcounter19h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER20H] = { "mhpmcounter20h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER21H] = { "mhpmcounter21h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER22H] = { "mhpmcounter22h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER23H] = { "mhpmcounter23h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER24H] = { "mhpmcounter24h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER25H] = { "mhpmcounter25h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER26H] = { "mhpmcounter26h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER27H] = { "mhpmcounter27h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER28H] = { "mhpmcounter28h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER29H] = { "mhpmcounter29h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER30H] = { "mhpmcounter30h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_MHPMCOUNTER31H] = { "mhpmcounter31h", mctr32,  read_hpmcounterh,
                             write_mhpmcounterh                         },
    [CSR_SCOUNTOVF]      = { "scountovf", sscofpmf,  read_scountovf,
                             .min_priv_ver = PRIV_VERSION_1_12_0 },
    /* Xuantie priviledged CSR */
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
    [CSR_MFASTM] =     {"mtnfastmba", xt_fastm, read_mfastm, write_mfastm   },
    [CSR_SUENQ] =      {"suenq", suenq, read_suenq, write_suenq   },

    /* CLIC CSR */
    /* Machine Mode Core Level Interrupt Controller */
    [CSR_MINTSTATUS_0p8] = { "mintstatus", clic_v0p8,  read_mintstatus },
    [CSR_MINTSTATUS_0p10] = { "mintstatus", clic_v0p10,  read_mintstatus },
    [CSR_MINTTHRESH] = { "mintthresh", clic,  read_mintthresh,
                         write_mintthresh },
    [CSR_MNXTI]          = { "mnxti",      clic,  NULL, NULL, rmw_mnxti },
    [CSR_MTVT] = { "mtvt", clic,  read_mtvt,  write_mtvt      },
    [CSR_MSCRATCHCSW] =  { "mscratchcsw", clic,  NULL, NULL,  rmw_mscratchcsw },
    [CSR_MSCRATCHCSL] =  { "mscratchcswl", clic,  NULL, NULL, rmw_mscratchcsl },

    /* Supervisor Mode Core Level Interrupt Controller */
    [CSR_SINTSTATUS_0p8] = { "sintstatus", sclic_v0p8,  read_sintstatus },
    [CSR_SINTSTATUS_0p10] = { "sintstatus", sclic_v0p10,  read_sintstatus },
    [CSR_SINTTHRESH] = { "sintthresh", sclic,  read_sintthresh,
                         write_sintthresh },
    [CSR_STVT] = { "stvt", sclic,  read_stvt, write_stvt       },
    [CSR_SNXTI]          = { "snxti",      clic,  NULL, NULL, rmw_snxti },
    [CSR_SSCRATCHCSW] =  { "sscratchcsw", clic,  NULL, NULL,  rmw_sscratchcsw },
    [CSR_SSCRATCHCSL] =  { "sscratchcswl", clic,  NULL, NULL, rmw_sscratchcsl },

    /* Supervisor Domain Identifier and Protection Registers */
    [CSR_MMPT] =    { "mmpt",   smsdid,  read_mmpt,   write_mmpt   },
    [CSR_MSDCFG] =  { "msdcfg", smsdid,  read_msdcfg, write_msdcfg },

    /* SPMP */
    [CSR_MPMPDELEG] = { "mpmpdeleg", smpmpdeleg, read_mpmpdeleg, write_mpmpdeleg,
                        .min_priv_ver = PRIV_VERSION_1_13_0        },

    /* Matrix v0.5 CSRs */
    [CSR_XMDMAERRINFO]     = { "xmdmaerrinfo",    tpe,    read_xmdmaerrinfo,
                               write_xmdmaerrinfo  },
    [XT_CSR_MTIMEDELTA]    = { "mtimedelta",    any,    read_mtimedelta,
                               write_mtimedelta    },
    [XT_CSR_MTIMEDELTAH]    = { "mtimedeltah",    any,    read_mtimedeltah,
                               write_mtimedeltah    },
    [XT_CSR_MVMID]    = { "mvmid",    any, read_mvmid, write_mvmid  },

    /* Cache Lock CSRs */
    [CSR_MRplCntlSt]    = { "mrplcntl",      mcachelock, NULL, NULL, rmw_rplcntl  },
    [CSR_MCacheLock]    = { "mcachelock",    mcachelock, NULL, NULL, rmw_cachelock  },
    [CSR_MCacheRplPri0] = { "mcacherplpri0", mcachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_MCacheRplPri1] = { "mcacherplpri1", mcachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_MCacheRplPri2] = { "mcacherplpri2", mcachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_MCacheRplPri3] = { "mcacherplpri3", mcachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_MCacheRplPri4] = { "mcacherplpri4", mcachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_MCacheRplPri5] = { "mcacherplpri5", mcachelock, NULL, NULL, rmw_cacherplpri  },

    [CSR_SRplCntlSt]    = { "srplcntl",      scachelock, NULL, NULL, rmw_rplcntl  },
    [CSR_SCacheLock]    = { "scachelock",    scachelock, NULL, NULL, rmw_cachelock  },
    [CSR_SCacheRplPri0] = { "scacherplpri0", scachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_SCacheRplPri1] = { "scacherplpri1", scachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_SCacheRplPri2] = { "scacherplpri2", scachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_SCacheRplPri3] = { "scacherplpri3", scachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_SCacheRplPri4] = { "scacherplpri4", scachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_SCacheRplPri5] = { "scacherplpri5", scachelock, NULL, NULL, rmw_cacherplpri  },

    [CSR_URplCntlSt]    = { "urplcntl",      ucachelock, NULL, NULL, rmw_rplcntl  },
    [CSR_UCacheLock]    = { "ucachelock",    ucachelock, NULL, NULL, rmw_cachelock  },
    [CSR_UCacheRplPri0] = { "ucacherplpri0", ucachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_UCacheRplPri1] = { "ucacherplpri1", ucachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_UCacheRplPri2] = { "ucacherplpri2", ucachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_UCacheRplPri3] = { "ucacherplpri3", ucachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_UCacheRplPri4] = { "ucacherplpri4", ucachelock, NULL, NULL, rmw_cacherplpri  },
    [CSR_UCacheRplPri5] = { "ucacherplpri5", ucachelock, NULL, NULL, rmw_cacherplpri  },
#endif /* !CONFIG_USER_ONLY */
};
