/*
 * RISC-V CPU helpers for qemu.
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
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internals.h"
#include "pmu.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "instmap.h"
#include "tcg/tcg-op.h"
#include "trace.h"
#include "semihosting/common-semi.h"
#include "sysemu/cpu-timers.h"
#include "cpu_bits.h"
#include "debug.h"
#include "tcg/oversized-guest.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/intc/xt_clic.h"
#endif
#include "exec/tracestub.h"

/* CLIC hacking */
#ifndef CONFIG_USER_ONLY
bool riscv_cpu_local_irq_mode_enabled(CPURISCVState *env, int mode)
{
    switch (mode) {
    case PRV_M:
        return env->priv < PRV_M ||
               (env->priv == PRV_M && get_field(env->mstatus, MSTATUS_MIE));
    case PRV_S:
        return env->priv < PRV_S ||
               (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_SIE));
    default:
        return false;
    }
}

static target_ulong riscv_intr_pc(CPURISCVState *env, target_ulong tvec,
                                  target_ulong tvt, bool async, bool clic,
                                  int cause, int mode)
{
    int mode1 = tvec & 0b11, mode2 = tvec & 0b111111;
    target_ulong new_pc = 0;

    if (!async) {
        return tvec & ~0b11;
    }
    /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
    switch (mode1) {
    case 0b00:
        return tvec & ~0b11;
    case 0b01:
        return (tvec & ~0b11) + cause * 4;
    default:
        if (env->clic && (mode2 == 0b000011)) {
            assert(mode == PRV_M);
            /* Non-vectored, clicintattr[i].shv = 0 || cliccfg.nvbits = 0 */
            if (!xt_clic_shv_interrupt(env->clic, cause)) {
                /* NBASE = mtvec[XLEN-1:6]<<6 */
                return tvec & ~0b111111;
            } else {
                /*
                 * pc := M[TBASE + XLEN/8 * exccode)] & ~1,
                 * TBASE = mtvt[XLEN-1:6]<<6
                 */
                int size = 2 << env->xl;
                target_ulong tbase = (tvt & ~0b111111) + size * cause;
                void *host = tlb_vaddr_to_host(env, tbase, MMU_DATA_LOAD, mode);
                if (host != NULL) {
                    new_pc = ldn_p(host, size);
                    if (tlb_vaddr_to_host(env, new_pc, MMU_INST_FETCH, mode)) {
                        return new_pc;
                    }
                }
                qemu_log_mask(LOG_GUEST_ERROR,
                              "CLIC: load trap handler error!\n");
                exit(1);
            }
        }
        g_assert_not_reached();
    }
}
#endif /* CLIC hacking */

int riscv_env_mmu_index(CPURISCVState *env, bool ifetch)
{
#ifdef CONFIG_USER_ONLY
    return 0;
#else
    bool virt = env->virt_enabled;
    int mode = env->priv;

    /* All priv -> mmu_idx mapping are here */
    if (!ifetch) {
        uint64_t status = env->mstatus;

        if (mode == PRV_M && get_field(status, MSTATUS_MPRV)) {
            mode = get_field(env->mstatus, MSTATUS_MPP);
            virt = get_field(env->mstatus, MSTATUS_MPV) &&
                   (mode != PRV_M);
            if (virt) {
                status = env->vsstatus;
            }
        }
        if (mode == PRV_S && get_field(status, MSTATUS_SUM)) {
            mode = MMUIdx_S_SUM;
        }
    }

    return mode | (virt ? MMU_2STAGE_BIT : 0);
#endif
}

#ifndef CONFIG_USER_ONLY
bool riscv_env_smode_dbltrp_enabled(CPURISCVState *env)
{
    if (env->virt_enabled) {
        return (env->henvcfg & HENVCFG_DTE) != 0;
    } else {
        return (env->menvcfg & MENVCFG_DTE) != 0;
    }
}
#endif

void cpu_get_tb_cpu_state(CPURISCVState *env, vaddr *pc,
                          uint64_t *cs_base, uint32_t *pflags)
{
    RISCVCPU *cpu = env_archcpu(env);
    RISCVExtStatus fs, vs;
    CPURISCVTBFlags flags = {0, 0};
    bool pm_signext = riscv_cpu_virt_mem_enabled(env);

    *pc = env->xl == MXL_RV32 ? env->pc & UINT32_MAX : env->pc;
    *cs_base = 0;

    if (cpu->cfg.ext_zve32x || cpu->cfg.ext_xtheadvector) {
        /*
         * If env->vl equals to VLMAX, we can use generic vector operation
         * expanders (GVEC) to accerlate the vector operations.
         * However, as LMUL could be a fractional number. The maximum
         * vector size can be operated might be less than 8 bytes,
         * which is not supported by GVEC. So we set vl_eq_vlmax flag to true
         * only when maxsz >= 8 bytes.
         */

        /* lmul encoded as in DisasContext::lmul */
        int8_t lmul = sextract32(FIELD_EX64(env->vtype, VTYPE, VLMUL), 0, 3);
        uint32_t vsew = FIELD_EX64(env->vtype, VTYPE, VSEW);
        uint32_t vlmax = vext_get_vlmax(cpu->cfg.vlenb, vsew, lmul);
        uint32_t maxsz = vlmax << vsew;
        bool vl_eq_vlmax = (env->vstart == 0) && (vlmax == env->vl) &&
                           (maxsz >= 8);
        DP_TBFLAGS_ANY(flags, VILL, env->vill);
        DP_TBFLAGS_ANY(flags, SEW, vsew);
        DP_TBFLAGS_ANY(flags, LMUL, FIELD_EX64(env->vtype, VTYPE, VLMUL));
        DP_TBFLAGS_ANY(flags, VL_EQ_VLMAX, vl_eq_vlmax);
        DP_TBFLAGS_ANY(flags, VTA, FIELD_EX64(env->vtype, VTYPE, VTA));
        DP_TBFLAGS_ANY(flags, VMA, FIELD_EX64(env->vtype, VTYPE, VMA));
        DP_TBFLAGS_ANY(flags, VSTART_EQ_ZERO, env->vstart == 0);
    } else {
        DP_TBFLAGS_ANY(flags, VILL, 1);
    }
    if (cpu->cfg.ext_matrix) {
        DP_TBFLAGS_THEAD(flags, PWFP, !!(env->xmisa & MATRIX_PW_FLOAT));
        DP_TBFLAGS_THEAD(flags, PWINT, !!(env->xmisa & MATRIX_PW_INT));
        DP_TBFLAGS_THEAD(flags, SPARSITYFP,
                         !!(env->xmisa & MATRIX_SPARSITY_FLOAT));
        DP_TBFLAGS_THEAD(flags, SPARSITYINT,
                         !!(env->xmisa & MATRIX_SPARSITY_INT));
        DP_TBFLAGS_THEAD(flags, FPINTCVT,
                         !!(env->xmisa & MATRIX_FLOAT_INT_CVT));
        DP_TBFLAGS_THEAD(flags, F8F32, !!(env->xmisa & MATRIX_MULT_F8F32));
        DP_TBFLAGS_THEAD(flags, F8F16, !!(env->xmisa & MATRIX_MULT_F8F16));
        DP_TBFLAGS_THEAD(flags, I4I32, !!(env->xmisa & MATRIX_MULT_I4I32));
        DP_TBFLAGS_THEAD(flags, I8I32, !!(env->xmisa & MATRIX_MULT_I8I32));
        DP_TBFLAGS_THEAD(flags, I16I64, !!(env->xmisa & MATRIX_MULT_I16I64));
        DP_TBFLAGS_THEAD(flags, F16F16, !!(env->xmisa & MATRIX_MULT_F16F16));
        DP_TBFLAGS_THEAD(flags, F32F32, !!(env->xmisa & MATRIX_MULT_F32F32));
        DP_TBFLAGS_THEAD(flags, F64F64, !!(env->xmisa & MATRIX_MULT_F64F64));
        DP_TBFLAGS_THEAD(flags, F16F32, !!(env->xmisa & MATRIX_MULT_F16F32));
        DP_TBFLAGS_THEAD(flags, F32F64, !!(env->xmisa & MATRIX_MULT_F32F64));
        DP_TBFLAGS_THEAD(flags, MILL,
                         env->sizem > get_mrows(env) || env->sizem == 0);
        DP_TBFLAGS_THEAD(flags, NILL,
                         env->sizen > get_mrows(env) || env->sizen == 0);
        DP_TBFLAGS_THEAD(flags, KILL,
                         env->sizek > get_rlenb(env) || env->sizek == 0);
        DP_TBFLAGS_THEAD(flags, NPILL,
                         env->sizen > 2 * get_mrows(env) || env->sizen == 0);
    }
    DP_TBFLAGS_THEAD(flags, BF16, env->bf16);
#ifdef CONFIG_USER_ONLY
    fs = EXT_STATUS_DIRTY;
    vs = EXT_STATUS_DIRTY;
    DP_TBFLAGS_THEAD(flags, MS, EXT_STATUS_DIRTY);
#else
    DP_TBFLAGS_ANY(flags, PRIV, env->priv);

    flags.flags |= riscv_env_mmu_index(env, 0);
    fs = get_field(env->mstatus, MSTATUS_FS);
    vs = get_field(env->mstatus, MSTATUS_VS);

    if (env->virt_enabled) {
        DP_TBFLAGS_ANY(flags, VIRT_ENABLED, 1);
        /*
         * Merge DISABLED and !DIRTY states using MIN.
         * We will set both fields when dirtying.
         */
        fs = MIN(fs, get_field(env->mstatus_hs, MSTATUS_FS));
        vs = MIN(vs, get_field(env->mstatus_hs, MSTATUS_VS));
    }

    /* With Zfinx, floating point is enabled/disabled by Smstateen. */
    if (!riscv_has_ext(env, RVF)) {
        fs = (smstateen_acc_ok(env, 0, SMSTATEEN0_FCSR) == RISCV_EXCP_NONE)
             ? EXT_STATUS_DIRTY : EXT_STATUS_DISABLED;
    }

    if (cpu->cfg.debug && !icount_enabled()) {
        DP_TBFLAGS_ANY(flags, ITRIGGER, env->itrigger_enabled);
    }
    DP_TBFLAGS_THEAD(flags, MSD, env->mxstatus & MXSTATUS_MSD);
    if (!(env->mxstatus & MXSTATUS_MSD) && cpu->cfg.ext_matrix) {
        DP_TBFLAGS_THEAD(flags, MS, get_field(env->mstatus, MSTATUS_TH_MS));
    }
#endif

    DP_TBFLAGS_ANY(flags, FS, fs);
    DP_TBFLAGS_ANY(flags, VS, vs);
    DP_TBFLAGS_ANY(flags, XL, env->xl);
    DP_TBFLAGS_ANY(flags, AXL, cpu_address_xl(env));
    DP_TBFLAGS_ANY(flags, PM_PMM, riscv_pm_get_pmm(env));
    DP_TBFLAGS_ANY(flags, PM_SIGNEXTEND, pm_signext);
    DP_TBFLAGS_ANY(flags, XSSE, riscv_cpu_get_xsse(env));
    DP_TBFLAGS_ANY(flags, ELP, riscv_cpu_get_xlpe(env) && env->elp);

    *pflags = flags.flags;
    *cs_base = flags.flags2;
}

RISCVPmPmm riscv_pm_get_pmm(CPURISCVState *env)
{
    int pmm = 0;
#ifndef CONFIG_USER_ONLY
    int priv_mode = cpu_address_mode(env);
    /* Get current PMM field */
    switch (priv_mode) {
    case PRV_M:
        pmm = riscv_cpu_cfg(env)->ext_smmpm ?
                  get_field(env->mseccfg, MSECCFG_PMM) : PMM_FIELD_DISABLED;
        break;
    case PRV_S:
        pmm = riscv_cpu_cfg(env)->ext_smnpm ?
                  get_field(env->menvcfg, MENVCFG_PMM) : PMM_FIELD_DISABLED;
        break;
    case PRV_U:
        pmm = riscv_cpu_cfg(env)->ext_ssnpm ?
                  get_field(env->senvcfg, SENVCFG_PMM) : PMM_FIELD_DISABLED;
        break;
    default:
        g_assert_not_reached();
    }
#endif
    return pmm;
}

bool riscv_cpu_virt_mem_enabled(CPURISCVState *env)
{
    bool virt_mem_en = false;
#ifndef CONFIG_USER_ONLY
    int satp_mode = 0;
    int priv_mode = cpu_address_mode(env);
    /* Get current PMM field */
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        satp_mode = get_field(env->satp, SATP32_MODE);
    } else {
        satp_mode = get_field(env->satp, SATP64_MODE);
    }
    virt_mem_en = ((satp_mode != VM_1_10_MBARE) && (priv_mode != PRV_M));
#endif
    return virt_mem_en;
}

int riscv_pm_get_pmlen(RISCVPmPmm pmm)
{
    switch (pmm) {
    case PMM_FIELD_DISABLED:
        return 0;
    case PMM_FIELD_PMLEN7:
        return 7;
    case PMM_FIELD_PMLEN16:
        return 16;
    default:
        g_assert_not_reached();
    }
    return -1;
}

#ifndef CONFIG_USER_ONLY

/*
 * The HS-mode is allowed to configure priority only for the
 * following VS-mode local interrupts:
 *
 * 0  (Reserved interrupt, reads as zero)
 * 1  Supervisor software interrupt
 * 4  (Reserved interrupt, reads as zero)
 * 5  Supervisor timer interrupt
 * 8  (Reserved interrupt, reads as zero)
 * 13 (Reserved interrupt)
 * 14 "
 * 15 "
 * 16 "
 * 17 "
 * 18 "
 * 19 "
 * 20 "
 * 21 "
 * 22 "
 * 23 "
 */

static const int hviprio_index2irq[] = {
    0, 1, 4, 5, 8, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
static const int hviprio_index2rdzero[] = {
    1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

int riscv_cpu_hviprio_index2irq(int index, int *out_irq, int *out_rdzero)
{
    if (index < 0 || ARRAY_SIZE(hviprio_index2irq) <= index) {
        return -EINVAL;
    }

    if (out_irq) {
        *out_irq = hviprio_index2irq[index];
    }

    if (out_rdzero) {
        *out_rdzero = hviprio_index2rdzero[index];
    }

    return 0;
}

/*
 * Default priorities of local interrupts are defined in the
 * RISC-V Advanced Interrupt Architecture specification.
 *
 * ----------------------------------------------------------------
 *  Default  |
 *  Priority | Major Interrupt Numbers
 * ----------------------------------------------------------------
 *  Highest  | 47, 23, 46, 45, 22, 44,
 *           | 43, 21, 42, 41, 20, 40
 *           |
 *           | 11 (0b),  3 (03),  7 (07)
 *           |  9 (09),  1 (01),  5 (05)
 *           | 12 (0c)
 *           | 10 (0a),  2 (02),  6 (06)
 *           |
 *           | 39, 19, 38, 37, 18, 36,
 *  Lowest   | 35, 17, 34, 33, 16, 32
 * ----------------------------------------------------------------
 */
static const uint8_t default_iprio[64] = {
    /* Custom interrupts 48 to 63 */
    [63] = IPRIO_MMAXIPRIO,
    [62] = IPRIO_MMAXIPRIO,
    [61] = IPRIO_MMAXIPRIO,
    [60] = IPRIO_MMAXIPRIO,
    [59] = IPRIO_MMAXIPRIO,
    [58] = IPRIO_MMAXIPRIO,
    [57] = IPRIO_MMAXIPRIO,
    [56] = IPRIO_MMAXIPRIO,
    [55] = IPRIO_MMAXIPRIO,
    [54] = IPRIO_MMAXIPRIO,
    [53] = IPRIO_MMAXIPRIO,
    [52] = IPRIO_MMAXIPRIO,
    [51] = IPRIO_MMAXIPRIO,
    [50] = IPRIO_MMAXIPRIO,
    [49] = IPRIO_MMAXIPRIO,
    [48] = IPRIO_MMAXIPRIO,

    /* Custom interrupts 24 to 31 */
    [31] = IPRIO_MMAXIPRIO,
    [30] = IPRIO_MMAXIPRIO,
    [29] = IPRIO_MMAXIPRIO,
    [28] = IPRIO_MMAXIPRIO,
    [27] = IPRIO_MMAXIPRIO,
    [26] = IPRIO_MMAXIPRIO,
    [25] = IPRIO_MMAXIPRIO,
    [24] = IPRIO_MMAXIPRIO,

    [47] = IPRIO_DEFAULT_UPPER,
    [23] = IPRIO_DEFAULT_UPPER + 1,
    [46] = IPRIO_DEFAULT_UPPER + 2,
    [45] = IPRIO_DEFAULT_UPPER + 3,
    [22] = IPRIO_DEFAULT_UPPER + 4,
    [44] = IPRIO_DEFAULT_UPPER + 5,

    [43] = IPRIO_DEFAULT_UPPER + 6,
    [21] = IPRIO_DEFAULT_UPPER + 7,
    [42] = IPRIO_DEFAULT_UPPER + 8,
    [41] = IPRIO_DEFAULT_UPPER + 9,
    [20] = IPRIO_DEFAULT_UPPER + 10,
    [40] = IPRIO_DEFAULT_UPPER + 11,

    [11] = IPRIO_DEFAULT_M,
    [3]  = IPRIO_DEFAULT_M + 1,
    [7]  = IPRIO_DEFAULT_M + 2,

    [9]  = IPRIO_DEFAULT_S,
    [1]  = IPRIO_DEFAULT_S + 1,
    [5]  = IPRIO_DEFAULT_S + 2,

    [12] = IPRIO_DEFAULT_SGEXT,

    [10] = IPRIO_DEFAULT_VS,
    [2]  = IPRIO_DEFAULT_VS + 1,
    [6]  = IPRIO_DEFAULT_VS + 2,

    [39] = IPRIO_DEFAULT_LOWER,
    [19] = IPRIO_DEFAULT_LOWER + 1,
    [38] = IPRIO_DEFAULT_LOWER + 2,
    [37] = IPRIO_DEFAULT_LOWER + 3,
    [18] = IPRIO_DEFAULT_LOWER + 4,
    [36] = IPRIO_DEFAULT_LOWER + 5,

    [35] = IPRIO_DEFAULT_LOWER + 6,
    [17] = IPRIO_DEFAULT_LOWER + 7,
    [34] = IPRIO_DEFAULT_LOWER + 8,
    [33] = IPRIO_DEFAULT_LOWER + 9,
    [16] = IPRIO_DEFAULT_LOWER + 10,
    [32] = IPRIO_DEFAULT_LOWER + 11,
};

uint8_t riscv_cpu_default_priority(int irq)
{
    if (irq < 0 || irq > 63) {
        return IPRIO_MMAXIPRIO;
    }

    return default_iprio[irq] ? default_iprio[irq] : IPRIO_MMAXIPRIO;
};

static int riscv_cpu_pending_to_irq(CPURISCVState *env,
                                    int extirq, unsigned int extirq_def_prio,
                                    uint64_t pending, uint8_t *iprio)
{
    int irq, best_irq = RISCV_EXCP_NONE;
    unsigned int prio, best_prio = UINT_MAX;

    if (!pending) {
        return RISCV_EXCP_NONE;
    }

    irq = ctz64(pending);
    if (!((extirq == IRQ_M_EXT) ? riscv_cpu_cfg(env)->ext_smaia :
                                  riscv_cpu_cfg(env)->ext_ssaia)) {
        return irq;
    }

    pending = pending >> irq;
    while (pending) {
        prio = iprio[irq];
        if (!prio) {
            if (irq == extirq) {
                prio = extirq_def_prio;
            } else {
                prio = (riscv_cpu_default_priority(irq) < extirq_def_prio) ?
                       1 : IPRIO_MMAXIPRIO;
            }
        }
        if ((pending & 0x1) && (prio <= best_prio)) {
            best_irq = irq;
            best_prio = prio;
        }
        irq++;
        pending = pending >> 1;
    }

    return best_irq;
}

/*
 * Doesn't report interrupts inserted using mvip from M-mode firmware or
 * using hvip bits 13:63 from HS-mode. Those are returned in
 * riscv_cpu_sirq_pending() and riscv_cpu_vsirq_pending().
 */
uint64_t riscv_cpu_all_pending(CPURISCVState *env)
{
    uint32_t gein = get_field(env->hstatus, HSTATUS_VGEIN);
    uint64_t vsgein = (env->hgeip & (1ULL << gein)) ? MIP_VSEIP : 0;
    uint64_t vstip = (env->vstime_irq) ? MIP_VSTIP : 0;

    return (env->mip | vsgein | vstip) & env->mie;
}

int riscv_cpu_mirq_pending(CPURISCVState *env)
{
    uint64_t irqs = riscv_cpu_all_pending(env) & ~env->mideleg &
                    ~(MIP_SGEIP | MIP_VSSIP | MIP_VSTIP | MIP_VSEIP);

    return riscv_cpu_pending_to_irq(env, IRQ_M_EXT, IPRIO_DEFAULT_M,
                                    irqs, env->miprio);
}

int riscv_cpu_sirq_pending(CPURISCVState *env)
{
    uint64_t irqs = riscv_cpu_all_pending(env) & env->mideleg &
                    ~(MIP_VSSIP | MIP_VSTIP | MIP_VSEIP);
    uint64_t irqs_f = env->mvip & env->mvien & ~env->mideleg & env->sie;

    return riscv_cpu_pending_to_irq(env, IRQ_S_EXT, IPRIO_DEFAULT_S,
                                    irqs | irqs_f, env->siprio);
}

int riscv_cpu_vsirq_pending(CPURISCVState *env)
{
    uint64_t irqs = riscv_cpu_all_pending(env) & env->mideleg & env->hideleg;
    uint64_t irqs_f_vs = env->hvip & env->hvien & ~env->hideleg & env->vsie;
    uint64_t vsbits;

    /* Bring VS-level bits to correct position */
    vsbits = irqs & VS_MODE_INTERRUPTS;
    irqs &= ~VS_MODE_INTERRUPTS;
    irqs |= vsbits >> 1;

    return riscv_cpu_pending_to_irq(env, IRQ_S_EXT, IPRIO_DEFAULT_S,
                                    (irqs | irqs_f_vs), env->hviprio);
}

static int riscv_cpu_local_irq_pending(CPURISCVState *env)
{
    uint64_t irqs, pending, mie, hsie, vsie, irqs_f, irqs_f_vs;
    uint64_t vsbits, irq_delegated;
    int virq;

    /* Determine interrupt enable state of all privilege modes */
    if (env->virt_enabled) {
        mie = 1;
        hsie = 1;
        vsie = (env->priv < PRV_S) ||
               (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_SIE));
    } else {
        mie = (env->priv < PRV_M) ||
              (env->priv == PRV_M && get_field(env->mstatus, MSTATUS_MIE));
        hsie = (env->priv < PRV_S) ||
               (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_SIE));
        vsie = 0;
    }

    /* Determine all pending interrupts */
    pending = riscv_cpu_all_pending(env);

    /* Check M-mode interrupts */
    irqs = pending & ~env->mideleg & -mie;
    if (irqs) {
        return riscv_cpu_pending_to_irq(env, IRQ_M_EXT, IPRIO_DEFAULT_M,
                                        irqs, env->miprio);
    }

    /* Check for virtual S-mode interrupts. */
    irqs_f = env->mvip & (env->mvien & ~env->mideleg) & env->sie;

    /* Check HS-mode interrupts */
    irqs =  ((pending & env->mideleg & ~env->hideleg) | irqs_f) & -hsie;
    if (irqs) {
        return riscv_cpu_pending_to_irq(env, IRQ_S_EXT, IPRIO_DEFAULT_S,
                                        irqs, env->siprio);
    }

    /* Check for virtual VS-mode interrupts. */
    irqs_f_vs = env->hvip & env->hvien & ~env->hideleg & env->vsie;

    /* Check VS-mode interrupts */
    irq_delegated = pending & env->mideleg & env->hideleg;

    /* Bring VS-level bits to correct position */
    vsbits = irq_delegated & VS_MODE_INTERRUPTS;
    irq_delegated &= ~VS_MODE_INTERRUPTS;
    irq_delegated |= vsbits >> 1;

    irqs = (irq_delegated | irqs_f_vs) & -vsie;
    if (irqs) {
        virq = riscv_cpu_pending_to_irq(env, IRQ_S_EXT, IPRIO_DEFAULT_S,
                                        irqs, env->hviprio);
        if (virq <= 0 || (virq > 12 && virq <= 63)) {
            return virq;
        } else {
            return virq + 1;
        }
    }

    /* Indicate no pending interrupt */
    return RISCV_EXCP_NONE;
}

bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        CPURISCVState *env = &cpu->env;
        int interruptno = riscv_cpu_local_irq_pending(env);
        if (interruptno >= 0) {
            cs->exception_index = RISCV_EXCP_INT_FLAG | interruptno;
            riscv_cpu_do_interrupt(cs);
            return true;
        }
    }
    /* CLIC hacking */
    if (interrupt_request & CPU_INTERRUPT_CLIC) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        CPURISCVState *env = &cpu->env;
        int mode = (env->exccode >> 12) & 0b11;
        bool enabled = riscv_cpu_local_irq_mode_enabled(env, mode);
        if (enabled) {
            cs->exception_index = RISCV_EXCP_INT_FLAG | RISCV_EXCP_INT_CLIC |
                                  env->exccode;
            cs->interrupt_request = cs->interrupt_request & ~CPU_INTERRUPT_CLIC;
            riscv_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}

/* Return true is floating point support is currently enabled */
bool riscv_cpu_fp_enabled(CPURISCVState *env)
{
    if (env->mstatus & MSTATUS_FS) {
        if (env->virt_enabled && !(env->mstatus_hs & MSTATUS_FS)) {
            return false;
        }
        return true;
    }

    return false;
}

/* Return true is vector support is currently enabled */
bool riscv_cpu_vector_enabled(CPURISCVState *env)
{
    if (env->mstatus & MSTATUS_VS) {
        if (env->virt_enabled && !(env->mstatus_hs & MSTATUS_VS)) {
            return false;
        }
        return true;
    }

    return false;
}

/* Return true is matrix support is currently enabled */
bool riscv_cpu_matrix_enabled(CPURISCVState *env)
{
    if (riscv_cpu_cfg(env)->ext_matrix) {
        if (env->mxstatus & MXSTATUS_MSD) {
            return true;
        } else {
            return env->mstatus & MSTATUS_TH_MS;
        }
    }
    return false;
}

void riscv_cpu_swap_hypervisor_regs(CPURISCVState *env)
{
    uint64_t mstatus_mask = MSTATUS_MXR | MSTATUS_SUM |
                            MSTATUS_SPP | MSTATUS_SPIE | MSTATUS_SIE |
                            MSTATUS64_UXL | MSTATUS_VS;

    if (riscv_has_ext(env, RVF)) {
        mstatus_mask |= MSTATUS_FS;
    }
     /*
      * If zicfilp extension available and henvcfg.LPE = 1,
      * then apply SPELP mask on mstatus
      */
     if (env_archcpu(env)->cfg.ext_zicfilp &&
         get_field(env->henvcfg, HENVCFG_LPE)) {
        mstatus_mask |= MSTATUS_SPELP;
     }
    bool current_virt = env->virt_enabled;
    uint64_t dte = current_virt ? env->menvcfg & MENVCFG_DTE :
                                  env->henvcfg & HENVCFG_DTE;

    if (riscv_cpu_cfg(env)->ext_ssdbltrp && dte) {
                mstatus_mask |= MSTATUS_SDT;
    }
    g_assert(riscv_has_ext(env, RVH));

    if (current_virt) {
        /* Current V=1 and we are about to change to V=0 */
        env->vsstatus = env->mstatus & mstatus_mask;
        env->mstatus &= ~mstatus_mask;
        env->mstatus |= env->mstatus_hs;

        env->vstvec = env->stvec;
        env->stvec = env->stvec_hs;

        env->vsscratch = env->sscratch;
        env->sscratch = env->sscratch_hs;

        env->vsepc = env->sepc;
        env->sepc = env->sepc_hs;

        env->vscause = env->scause;
        env->scause = env->scause_hs;

        env->vstval = env->stval;
        env->stval = env->stval_hs;

        env->vsatp = env->satp;
        env->satp = env->satp_hs;
    } else {
        /* Current V=0 and we are about to change to V=1 */
        env->mstatus_hs = env->mstatus & mstatus_mask;
        env->mstatus &= ~mstatus_mask;
        env->mstatus |= env->vsstatus;

        env->stvec_hs = env->stvec;
        env->stvec = env->vstvec;

        env->sscratch_hs = env->sscratch;
        env->sscratch = env->vsscratch;

        env->sepc_hs = env->sepc;
        env->sepc = env->vsepc;

        env->scause_hs = env->scause;
        env->scause = env->vscause;

        env->stval_hs = env->stval;
        env->stval = env->vstval;

        env->satp_hs = env->satp;
        env->satp = env->vsatp;
    }
}

target_ulong riscv_cpu_get_geilen(CPURISCVState *env)
{
    if (!riscv_has_ext(env, RVH)) {
        return 0;
    }

    return env->geilen;
}

void riscv_cpu_set_geilen(CPURISCVState *env, target_ulong geilen)
{
    if (!riscv_has_ext(env, RVH)) {
        return;
    }

    if (geilen > (TARGET_LONG_BITS - 1)) {
        return;
    }

    env->geilen = geilen;
}

int riscv_cpu_claim_interrupts(RISCVCPU *cpu, uint64_t interrupts)
{
    CPURISCVState *env = &cpu->env;
    if (env->miclaim & interrupts) {
        return -1;
    } else {
        env->miclaim |= interrupts;
        return 0;
    }
}

void riscv_cpu_interrupt(CPURISCVState *env)
{
    uint64_t gein, vsgein = 0, vstip = 0, irqf = 0;
    CPUState *cs = env_cpu(env);

    BQL_LOCK_GUARD();

    if (env->virt_enabled) {
        gein = get_field(env->hstatus, HSTATUS_VGEIN);
        vsgein = (env->hgeip & (1ULL << gein)) ? MIP_VSEIP : 0;
        irqf = env->hvien & env->hvip & env->vsie;
    } else {
        irqf = env->mvien & env->mvip & env->sie;
    }

    vstip = env->vstime_irq ? MIP_VSTIP : 0;

    if (env->mip | vsgein | vstip | irqf) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

uint64_t riscv_cpu_update_mip(CPURISCVState *env, uint64_t mask, uint64_t value)
{
    uint64_t old = env->mip;

    if (xt_clic_is_clic_mode(env)) {
        return old;
    }
    /* No need to update mip for VSTIP */
    mask = ((mask == MIP_VSTIP) && env->vstime_irq) ? 0 : mask;

    BQL_LOCK_GUARD();

    if (MIP_LCOFIP & value & mask) {
        riscv_ctr_freeze(env, MCTRCTL_LCOFIFRZ);
    }

    env->mip = (env->mip & ~mask) | (value & mask);

    riscv_cpu_interrupt(env);

    return old;
}

void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(void *),
                             void *arg)
{
    env->rdtime_fn = fn;
    env->rdtime_fn_arg = arg;
}

void riscv_cpu_set_aia_ireg_rmw_fn(CPURISCVState *env, uint32_t priv,
                                   int (*rmw_fn)(void *arg,
                                                 target_ulong reg,
                                                 target_ulong *val,
                                                 target_ulong new_val,
                                                 target_ulong write_mask),
                                   void *rmw_fn_arg)
{
    if (priv <= PRV_M) {
        env->aia_ireg_rmw_fn[priv] = rmw_fn;
        env->aia_ireg_rmw_fn_arg[priv] = rmw_fn_arg;
    }
}

void riscv_ctr_freeze(CPURISCVState *env, uint64_t freeze_mask)
{
    assert((freeze_mask & (~(MCTRCTL_BPFRZ | MCTRCTL_LCOFIFRZ))) == 0);

    if (env->mctrctl & freeze_mask) {
        env->sctrstatus |= SCTRSTATUS_FROZEN;
    }
}

void riscv_ctr_clear(CPURISCVState *env)
{
    memset(env->ctr_src, 0x0, sizeof(env->ctr_src));
    memset(env->ctr_dst, 0x0, sizeof(env->ctr_dst));
    memset(env->ctr_data, 0x0, sizeof(env->ctr_data));
}

static uint64_t riscv_ctr_priv_to_mask(target_ulong priv, bool virt)
{
    switch (priv) {
    case PRV_M:
        return MCTRCTL_M_ENABLE;
    case PRV_S:
        if (virt) {
            return VSCTRCTL_VS_ENABLE;
        }
        return MCTRCTL_S_ENABLE;
    case PRV_U:
        if (virt) {
            return VSCTRCTL_VU_ENABLE;
        }
        return MCTRCTL_U_ENABLE;
    }

    g_assert_not_reached();
}

static uint64_t riscv_ctr_get_control(CPURISCVState *env, target_long priv,
                                      bool virt)
{
    switch (priv) {
    case PRV_M:
        return env->mctrctl;
    case PRV_S:
    case PRV_U:
        if (virt) {
            return env->vsctrctl;
        }
        return env->mctrctl;
    }

    g_assert_not_reached();
}

/*
 * Special cases for traps and trap returns:
 *
 * 1- Traps, and trap returns, between enabled modes are recorded as normal.
 * 2- Traps from an inhibited mode to an enabled mode, and trap returns from an
 * enabled mode back to an inhibited mode, are partially recorded.  In such
 * cases, the PC from the inhibited mode (source PC for traps, and target PC
 * for trap returns) is 0.
 *
 * 3- Trap returns from an inhibited mode to an enabled mode are not recorded.
 * Traps from an enabled mode to an inhibited mode, known as external traps,
 * receive special handling.
 * By default external traps are not recorded, but a handshake mechanism exists
 * to allow partial recording.  Software running in the target mode of the trap
 * can opt-in to allowing CTR to record traps into that mode even when the mode
 * is inhibited.  The MTE, STE, and VSTE bits allow M-mode, S-mode, and VS-mode,
 * respectively, to opt-in. When an External Trap occurs, and xTE=1, such that
 * x is the target privilege mode of the trap, will CTR record the trap. In such
 * cases, the target PC is 0.
 */
/*
 * CTR arrays are implemented as circular buffers and new entry is stored at
 * sctrstatus.WRPTR, but they are presented to software as moving circular
 * buffers. Which means, software get's the illusion that whenever a new entry
 * is added the whole buffer is moved by one place and the new entry is added at
 * the start keeping new entry at idx 0 and older ones follow.
 *
 * Depth = 16.
 *
 * buffer [0] [1] [2] [3] [4] [5] [6] [7] [8] [9] [A] [B] [C] [D] [E] [F]
 * WRPTR                                   W
 * entry   7   6   5   4   3   2   1   0   F   E   D   C   B   A   9   8
 *
 * When a new entry is added:
 * buffer [0] [1] [2] [3] [4] [5] [6] [7] [8] [9] [A] [B] [C] [D] [E] [F]
 * WRPTR                                       W
 * entry   8   7   6   5   4   3   2   1   0   F   E   D   C   B   A   9
 *
 * entry here denotes the logical entry number that software can access
 * using ctrsource, ctrtarget and ctrdata registers. So xiselect 0x200
 * will return entry 0 i-e buffer[8] and 0x201 will return entry 1 i-e
 * buffer[7]. Here is how we convert entry to buffer idx.
 *
 *    entry = isel - CTR_ENTRIES_FIRST;
 *    idx = (sctrstatus.WRPTR - entry - 1) & (depth - 1);
 */
void riscv_ctr_add_entry(CPURISCVState *env, target_long src, target_long dst,
                         uint64_t type, target_ulong src_priv, bool src_virt)
{
    bool tgt_virt = env->virt_enabled;
    uint64_t src_mask = riscv_ctr_priv_to_mask(src_priv, src_virt);
    uint64_t tgt_mask = riscv_ctr_priv_to_mask(env->priv, tgt_virt);
    uint64_t src_ctrl = riscv_ctr_get_control(env, src_priv, src_virt);
    uint64_t tgt_ctrl = riscv_ctr_get_control(env, env->priv, tgt_virt);
    uint64_t depth, head;
    bool ext_trap = false;

    if (env->sctrstatus & SCTRSTATUS_FROZEN) {
        return;
    }

    /*
     * With RAS Emul enabled, only allow Indirect, drirect calls, Function
     * returns and Co-routine swap types.
     */
    if (env->mctrctl & MCTRCTL_RASEMU &&
        type != CTRDATA_TYPE_INDIRECT_CALL &&
        type != CTRDATA_TYPE_DIRECT_CALL &&
        type != CTRDATA_TYPE_RETURN &&
        type != CTRDATA_TYPE_CO_ROUTINE_SWAP) {
        return;
    }

    if (type == CTRDATA_TYPE_EXCEPTION || type == CTRDATA_TYPE_INTERRUPT) {
        /* Case 2 for traps. */
        if (!(src_ctrl & src_mask) && (tgt_ctrl & tgt_mask)) {
            src = 0;
        } else if ((src_ctrl & src_mask) && !(tgt_ctrl & tgt_mask)) {
            /* Check if target priv-mode has allowed external trap recording. */
            if ((env->priv == PRV_M && !(tgt_ctrl & MCTRCTL_MTE)) ||
                (env->priv == PRV_S && !(tgt_ctrl & MCTRCTL_STE))) {
                return;
            }

            ext_trap = true;
            dst = 0;
        } else if (!(src_ctrl & src_mask) && !(tgt_ctrl & tgt_mask)) {
            return;
        }
    } else if (type == CTRDATA_TYPE_EXCEP_INT_RET) {
        /*
         * Case 3 for trap returns.  Trap returns from inhibited mode are not
         * recorded.
         */
        if (!(src_ctrl & src_mask)) {
            return;
        }

        /* Case 2 for trap returns. */
        if (!(tgt_ctrl & tgt_mask)) {
            dst = 0;
        }
    } else if (!(tgt_ctrl & tgt_mask)) {
        return;
    }

    /* Ignore filters in case of RASEMU mode or External trap. */
    if (!(tgt_ctrl & MCTRCTL_RASEMU) && !ext_trap) {
        /*
         * Check if the specific type is inhibited. Not taken branch filter is
         * an enable bit and needs to be checked separatly.
         */
        bool check = tgt_ctrl & BIT_ULL(type + MCTRCTL_INH_START);
        if ((type == CTRDATA_TYPE_NONTAKEN_BRANCH && !check) ||
            (type != CTRDATA_TYPE_NONTAKEN_BRANCH && check)) {
            return;
        }
    }

    head = get_field(env->sctrstatus, SCTRSTATUS_WRPTR_MASK);

    depth = 16 << get_field(env->sctrdepth, SCTRDEPTH_MASK);
    if (tgt_ctrl & MCTRCTL_RASEMU && type == CTRDATA_TYPE_RETURN) {
        head = (head - 1) & (depth - 1);

        env->ctr_src[head] &= ~CTRSOURCE_VALID;
        env->sctrstatus =
            set_field(env->sctrstatus, SCTRSTATUS_WRPTR_MASK, head);
        return;
    }

    /* In case of Co-routine SWAP we overwrite latest entry. */
    if (tgt_ctrl & MCTRCTL_RASEMU && type == CTRDATA_TYPE_CO_ROUTINE_SWAP) {
        head = (head - 1) & (depth - 1);
    }

    env->ctr_src[head] = src | CTRSOURCE_VALID;
    env->ctr_dst[head] = dst & ~CTRTARGET_MISP;
    env->ctr_data[head] = set_field(0, CTRDATA_TYPE_MASK, type);

    head = (head + 1) & (depth - 1);

    env->sctrstatus = set_field(env->sctrstatus, SCTRSTATUS_WRPTR_MASK, head);
}

void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv, bool virt_en)
{
    g_assert(newpriv <= PRV_M && newpriv != PRV_RESERVED);

    if (newpriv != env->priv || env->virt_enabled != virt_en) {
        if (icount_enabled()) {
            riscv_itrigger_update_priv(env);
        }

        riscv_pmu_update_fixed_ctrs(env, newpriv, virt_en);
    }

    /* tlb_flush is unnecessary as mode is contained in mmu_idx */
    env->priv = newpriv;
    env->xl = cpu_recompute_xl(env);

    /*
     * Clear the load reservation - otherwise a reservation placed in one
     * context/process can be used by another, resulting in an SC succeeding
     * incorrectly. Version 2.2 of the ISA specification explicitly requires
     * this behaviour, while later revisions say that the kernel "should" use
     * an SC instruction to force the yielding of a load reservation on a
     * preemptive context switch. As a result, do both.
     */
    env->load_res = -1;

    if (riscv_has_ext(env, RVH)) {
        /* Flush the TLB on all virt mode changes. */
        if (env->virt_enabled != virt_en) {
            tlb_flush(env_cpu(env));
        }

        env->virt_enabled = virt_en;
        if (virt_en) {
            /*
             * The guest external interrupts from an interrupt controller are
             * delivered only when the Guest/VM is running (i.e. V=1). This
             * means any guest external interrupt which is triggered while the
             * Guest/VM is not running (i.e. V=0) will be missed on QEMU
             * resulting in guest with sluggish response to serial console
             * input and other I/O events.
             *
             * To solve this, we check and inject interrupt after setting V=1.
             */
            riscv_cpu_update_mip(env, 0, 0);
        }
    }
}

static bool legal_sstack_access(int access_type, bool sstack_inst,
                                bool sstack_attribute)
{
    /*
     * Read/write/execution permissions are checked as usual. Shadow
     * stack enforcement is just that (1) instruction type must match
     * the attribute unless (2) a non-SS load to an SS region.
     */
    return (sstack_inst == sstack_attribute) ||
           ((access_type == MMU_DATA_LOAD) && sstack_attribute);
}
/*
 * get_physical_address_pmp - check PMP permission for this physical address
 *
 * Match the PMP region and check permission for this physical address and it's
 * TLB page. Returns 0 if the permission checking was successful
 *
 * @env: CPURISCVState
 * @prot: The returned protection attributes
 * @addr: The physical address to be checked permission
 * @access_type: The type of MMU access
 * @mode: Indicates current privilege level.
 */
int get_physical_address_pmp(CPURISCVState *env, int *prot, hwaddr addr,
                             int size, MMUAccessType access_type,
                             int mode)
{
    pmp_priv_t pmp_priv;
    bool pmp_has_privs;

    if (!riscv_cpu_cfg(env)->pmp) {
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TRANSLATE_SUCCESS;
    }

    pmp_has_privs = pmp_hart_has_privs(env, addr, size, 1 << access_type,
                                       &pmp_priv, mode);
    if (!pmp_has_privs) {
        *prot = 0;
        return TRANSLATE_PMP_FAIL;
    }

    *prot = pmp_priv_to_page_prot(pmp_priv);

    return TRANSLATE_SUCCESS;
}

/*
 * get_physical_address_mtt - check mtt permission for this physical address
 *
 * Lookup the Memory tracking table and check permission for this
 * physical address. Returns 0 if the permission checking was successful
 *
 * @env: CPURISCVState
 * @prot: The returned protection attributes
 * @addr: The physical address to be checked permission
 * @access_type: The type of MMU access
 * @mode: Indicates current privilege level.
 */
static int get_physical_address_mtt(CPURISCVState *env, int *prot, hwaddr addr,
                                    MMUAccessType access_type, int mode)
{
    mtt_access_t mtt_access;
    bool mtt_has_access;

    /*
     * If the extension is not supported or the mttp.mode is Bare,
     * there is no protection, return success.
     */
    if (!riscv_cpu_cfg(env)->ext_smmtt || env->mttmode == 0) {
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TRANSLATE_SUCCESS;
    }

    /*
     * MTT is checked for all accesses to physical memory, unless the
     * effective privilege mode is M.
     *
     * Data accesses in M-mode when the MPRV bit in mstatus is set and
     * the MPP field in mstatus contains S or U are subject to MTT checks.
     *
     * In riscv_env_mmu_index, The MPRV and MPP bits are already checked and
     * encoded to mmu_idx, So we do not need to check it here.
     */
    if (mode == PRV_M) {
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TRANSLATE_SUCCESS;
    }

    mtt_has_access = mtt_check_access(env, addr,
                                      &mtt_access, access_type);
    if (!mtt_has_access) {
        *prot = 0;
        return TRANSLATE_MTT_FAIL;
    }

    *prot = mtt_access_to_page_prot(mtt_access);

    return TRANSLATE_SUCCESS;
}

/*
 * get_physical_address - get the physical address for this virtual address
 *
 * Do a page table walk to obtain the physical address corresponding to a
 * virtual address. Returns 0 if the translation was successful
 *
 * Adapted from Spike's mmu_t::translate and mmu_t::walk
 *
 * @env: CPURISCVState
 * @physical: This will be set to the calculated physical address
 * @prot: The returned protection attributes
 * @addr: The virtual address or guest physical address to be translated
 * @fault_pte_addr: If not NULL, this will be set to fault pte address
 *                  when a error occurs on pte address translation.
 *                  This will already be shifted to match htval.
 * @access_type: The type of MMU access
 * @mmu_idx: Indicates current privilege level
 * @first_stage: Are we in first stage translation?
 *               Second stage is used for hypervisor guest translation
 * @two_stage: Are we going to perform two stage translation
 * @is_debug: Is this access from a debugger or the monitor?
 */
static int get_physical_address(CPURISCVState *env, hwaddr *physical,
                                int *ret_prot, vaddr addr,
                                target_ulong *fault_pte_addr,
                                int access_type, int mmu_idx,
                                bool first_stage, bool two_stage,
                                bool is_debug)
{
    /*
     * NOTE: the env->pc value visible here will not be
     * correct, but the value visible to the exception handler
     * (riscv_cpu_do_interrupt) is correct
     */
    MemTxResult res;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    int mode = mmuidx_priv(mmu_idx);
    bool use_background = false;
    hwaddr ppn;
    int napot_bits = 0;
    target_ulong napot_mask;
    bool sstack_inst = get_field(mmu_idx, MMU_IDX_SS_ACCESS);
    bool sstack_page = false;

    /*
     * Check if we should use the background registers for the two
     * stage translation. We don't need to check if we actually need
     * two stage translation as that happened before this function
     * was called. Background registers will be used if the guest has
     * forced a two stage translation to be on (in HS or M mode).
     */
    if (!env->virt_enabled && two_stage) {
        use_background = true;
    }

    if (mode == PRV_M || !riscv_cpu_cfg(env)->mmu) {
        *physical = addr;
        *ret_prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TRANSLATE_SUCCESS;
    }

    *ret_prot = 0;

    hwaddr base;
    int levels, ptidxbits, ptesize, vm, widened;

    if (first_stage == true) {
        if (use_background) {
            if (riscv_cpu_mxl(env) == MXL_RV32) {
                base = (hwaddr)get_field(env->vsatp, SATP32_PPN) << PGSHIFT;
                vm = get_field(env->vsatp, SATP32_MODE);
            } else {
                base = (hwaddr)get_field(env->vsatp, SATP64_PPN) << PGSHIFT;
                vm = get_field(env->vsatp, SATP64_MODE);
            }
        } else {
            if (riscv_cpu_mxl(env) == MXL_RV32) {
                base = (hwaddr)get_field(env->satp, SATP32_PPN) << PGSHIFT;
                vm = get_field(env->satp, SATP32_MODE);
            } else {
                base = (hwaddr)get_field(env->satp, SATP64_PPN) << PGSHIFT;
                vm = get_field(env->satp, SATP64_MODE);
            }
        }
        widened = 0;
    } else {
        if (riscv_cpu_mxl(env) == MXL_RV32) {
            base = (hwaddr)get_field(env->hgatp, SATP32_PPN) << PGSHIFT;
            vm = get_field(env->hgatp, SATP32_MODE);
        } else {
            base = (hwaddr)get_field(env->hgatp, SATP64_PPN) << PGSHIFT;
            vm = get_field(env->hgatp, SATP64_MODE);
        }
        widened = 2;
    }

    switch (vm) {
    case VM_1_10_SV32:
      levels = 2; ptidxbits = 10; ptesize = 4; break;
    case VM_1_10_SV39:
      levels = 3; ptidxbits = 9; ptesize = 8; break;
    case VM_1_10_SV48:
      levels = 4; ptidxbits = 9; ptesize = 8; break;
    case VM_1_10_SV57:
      levels = 5; ptidxbits = 9; ptesize = 8; break;
    case VM_1_10_MBARE:
        *physical = addr;
        *ret_prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TRANSLATE_SUCCESS;
    default:
      g_assert_not_reached();
    }

    CPUState *cs = env_cpu(env);
    int va_bits = PGSHIFT + levels * ptidxbits + widened;

    if (first_stage == true) {
        target_ulong mask, masked_msbs;

        if (TARGET_LONG_BITS > (va_bits - 1)) {
            mask = (1L << (TARGET_LONG_BITS - (va_bits - 1))) - 1;
        } else {
            mask = 0;
        }
        masked_msbs = (addr >> (va_bits - 1)) & mask;

        if (masked_msbs != 0 && masked_msbs != mask) {
            return TRANSLATE_FAIL;
        }
    } else {
        if (vm != VM_1_10_SV32 && addr >> va_bits != 0) {
            return TRANSLATE_FAIL;
        }
    }

    bool pbmte = env->menvcfg & MENVCFG_PBMTE;
    bool svade = riscv_cpu_cfg(env)->ext_svade;
    bool svadu = riscv_cpu_cfg(env)->ext_svadu;
    bool adue = svadu ? env->menvcfg & MENVCFG_ADUE : !svade;

    if (first_stage && two_stage && env->virt_enabled) {
        pbmte = pbmte && (env->henvcfg & HENVCFG_PBMTE);
        adue = adue && (env->henvcfg & HENVCFG_ADUE);
    }

    int ptshift = (levels - 1) * ptidxbits;
    target_ulong pte;
    hwaddr pte_addr;
    int i;

#if !TCG_OVERSIZED_GUEST
restart:
#endif
    for (i = 0; i < levels; i++, ptshift -= ptidxbits) {
        target_ulong idx;
        if (i == 0) {
            idx = (addr >> (PGSHIFT + ptshift)) &
                           ((1 << (ptidxbits + widened)) - 1);
        } else {
            idx = (addr >> (PGSHIFT + ptshift)) &
                           ((1 << ptidxbits) - 1);
        }

        /* check that physical address of PTE is legal */

        if (two_stage && first_stage) {
            int vbase_prot;
            hwaddr vbase;

            /* Do the second stage translation on the base PTE address. */
            int vbase_ret = get_physical_address(env, &vbase, &vbase_prot,
                                                 base, NULL, MMU_DATA_LOAD,
                                                 MMUIdx_U, false, true,
                                                 is_debug);

            if (vbase_ret != TRANSLATE_SUCCESS) {
                if (fault_pte_addr) {
                    *fault_pte_addr = (base + idx * ptesize) >> 2;
                }
                return TRANSLATE_G_STAGE_FAIL;
            }

            pte_addr = vbase + idx * ptesize;
        } else {
            pte_addr = base + idx * ptesize;
        }

        int pmp_prot;
        int pmp_ret = get_physical_address_pmp(env, &pmp_prot, pte_addr,
                                               sizeof(target_ulong),
                                               MMU_DATA_LOAD, PRV_S);
        if (pmp_ret != TRANSLATE_SUCCESS) {
            return TRANSLATE_PMP_FAIL;
        }

        int mtt_prot;
        int mtt_ret = get_physical_address_mtt(env, &mtt_prot, pte_addr,
                                               MMU_DATA_LOAD, PRV_S);
        if (mtt_ret != TRANSLATE_SUCCESS) {
            return TRANSLATE_MTT_FAIL;
        }

        if (riscv_cpu_mxl(env) == MXL_RV32) {
            pte = address_space_ldl(cs->as, pte_addr, attrs, &res);
        } else {
            pte = address_space_ldq(cs->as, pte_addr, attrs, &res);
        }

        if (res != MEMTX_OK) {
            return TRANSLATE_FAIL;
        }

        if (riscv_cpu_sxl(env) == MXL_RV32) {
            if (pbmte && riscv_cpu_cfg(env)->ext_xtheadpbmt) {
                pte &= ~0xC0000000;
            }
            ppn = pte >> PTE_PPN_SHIFT;
        } else {
            if (!riscv_cpu_cfg(env)->ext_xtheadmaee) {
                if (pte & PTE_RESERVED) {
                    return TRANSLATE_FAIL;
                }

                if ((!pbmte && (pte & PTE_PBMT)) ||
                    (pbmte && ((pte & PTE_PBMT) == PTE_PBMT))) {
                    return TRANSLATE_FAIL;
                }

                if (!riscv_cpu_cfg(env)->ext_svnapot && (pte & PTE_N)) {
                    return TRANSLATE_FAIL;
                }
            }
            ppn = (pte & (target_ulong)PTE_PPN_MASK) >> PTE_PPN_SHIFT;
        }

        /*
         * When backward CFI is enabled, the R=0, W=1, X=0 reserved encoding
         * is used to mark Shadow Stack (SS) pages. If back CFI enabled, allow
         * normal loads on SS pages, regular stores raise store access fault
         * and avoid hitting the reserved-encoding case. Only shadow stack
         * stores are allowed on SS pages. Shadow stack loads and stores on
         * regular memory (non-SS) raise load and store/AMO access fault.
         * Second stage translations don't participate in Shadow Stack.
         */
        sstack_page = (riscv_cpu_get_xsse(env) && first_stage &&
                       ((pte & (PTE_R | PTE_W | PTE_X)) == PTE_W));

        if (!(pte & PTE_V)) {
            /* Invalid PTE */
            return TRANSLATE_FAIL;
        }
        if (pte & (PTE_R | PTE_W | PTE_X)) {
            goto leaf;
        }

        /* Inner PTE, continue walking */
        if (pte & (PTE_D | PTE_A | PTE_U)) {
            return TRANSLATE_FAIL;
        }
        if ((pte & PTE_ATTR) && !riscv_cpu_cfg(env)->ext_xtheadmaee) {
            return TRANSLATE_FAIL;
        }
        base = ppn << PGSHIFT;
    }

    /* No leaf pte at any translation level. */
    return TRANSLATE_FAIL;

 leaf:
    if (ppn & ((1ULL << ptshift) - 1)) {
        /* Misaligned PPN */
        return TRANSLATE_FAIL;
    }
    if (!riscv_cpu_cfg(env)->ext_xtheadmaee) {
        if (!pbmte && (pte & PTE_PBMT)) {
            /* Reserved without Svpbmt. */
            return TRANSLATE_FAIL;
        }
    }

    /* Check for reserved combinations of RWX flags. */
    switch (pte & (PTE_R | PTE_W | PTE_X)) {
    case PTE_W:
    /* If shadow stack page, then only PTE_W is no more reserved */
        if (sstack_page) {
            break;
        }
        return TRANSLATE_FAIL;
    case PTE_W | PTE_X:
        return TRANSLATE_FAIL;
    }

    /* Illegal combo of instruction type and page attribute */
    if (!legal_sstack_access(access_type, sstack_inst, sstack_page)) {
        /* shadow stack instruction and RO page then it's a page fault */
        if (sstack_inst && ((pte & (PTE_R | PTE_W | PTE_X)) == PTE_R)) {
            return TRANSLATE_FAIL;
        }
        /* In all other cases it's an access fault, so raise PMP_FAIL */
        return TRANSLATE_PMP_FAIL;
    }

    int prot = 0;
    if (pte & PTE_R) {
        prot |= PAGE_READ;
    }
    if (pte & PTE_W) {
        prot |= PAGE_WRITE;
    }
    if (pte & PTE_X) {
        bool mxr = false;

        /*
         * Use mstatus for first stage or for the second stage without
         * virt_enabled (MPRV+MPV)
         */
        if (first_stage || !env->virt_enabled) {
            mxr = get_field(env->mstatus, MSTATUS_MXR);
        }

        /* MPRV+MPV case, check VSSTATUS */
        if (first_stage && two_stage && !env->virt_enabled) {
            mxr |= get_field(env->vsstatus, MSTATUS_MXR);
        }

        /*
         * Setting MXR at HS-level overrides both VS-stage and G-stage
         * execute-only permissions
         */
        if (env->virt_enabled) {
            mxr |= get_field(env->mstatus_hs, MSTATUS_MXR);
        }

        if (mxr) {
            prot |= PAGE_READ;
        }
        prot |= PAGE_EXEC;
    }

    if (pte & PTE_U) {
        if (mode != PRV_U) {
            if (!mmuidx_sum(mmu_idx)) {
                return TRANSLATE_FAIL;
            }
            /* SUM allows only read+write, not execute. */
            prot &= PAGE_READ | PAGE_WRITE;
        }
    } else if (mode != PRV_S) {
        /* Supervisor PTE flags when not S mode */
        return TRANSLATE_FAIL;
    }

    if (!((prot >> access_type) & 1)) {
        /* Access check failed */
        return TRANSLATE_FAIL;
    }

    target_ulong updated_pte = pte;

    /*
     * If ADUE is enabled, set accessed and dirty bits.
     * Otherwise raise an exception if necessary.
     */
    if (adue) {
        updated_pte |= PTE_A | (access_type == MMU_DATA_STORE ? PTE_D : 0);
    } else if (!(pte & PTE_A) ||
               (access_type == MMU_DATA_STORE && !(pte & PTE_D))) {
        return TRANSLATE_FAIL;
    }

    /* Page table updates need to be atomic with MTTCG enabled */
    if (updated_pte != pte && !is_debug) {
        if (!adue) {
            return TRANSLATE_FAIL;
        }

        /*
         * - if accessed or dirty bits need updating, and the PTE is
         *   in RAM, then we do so atomically with a compare and swap.
         * - if the PTE is in IO space or ROM, then it can't be updated
         *   and we return TRANSLATE_FAIL.
         * - if the PTE changed by the time we went to update it, then
         *   it is no longer valid and we must re-walk the page table.
         */
        MemoryRegion *mr;
        hwaddr l = sizeof(target_ulong), addr1;
        mr = address_space_translate(cs->as, pte_addr, &addr1, &l,
                                     false, MEMTXATTRS_UNSPECIFIED);
        if (memory_region_is_ram(mr)) {
            target_ulong *pte_pa = qemu_map_ram_ptr(mr->ram_block, addr1);
#if TCG_OVERSIZED_GUEST
            /*
             * MTTCG is not enabled on oversized TCG guests so
             * page table updates do not need to be atomic
             */
            *pte_pa = pte = updated_pte;
#else
            target_ulong old_pte = qatomic_cmpxchg(pte_pa, pte, updated_pte);
            if (old_pte != pte) {
                goto restart;
            }
            pte = updated_pte;
#endif
        } else {
            /*
             * Misconfigured PTE in ROM (AD bits are not preset) or
             * PTE is in IO space and can't be updated atomically.
             */
            return TRANSLATE_FAIL;
        }
    }

    /* For superpage mappings, make a fake leaf PTE for the TLB's benefit. */
    target_ulong vpn = addr >> PGSHIFT;

    if (!riscv_cpu_cfg(env)->ext_xtheadmaee &&
        riscv_cpu_cfg(env)->ext_svnapot && (pte & PTE_N)) {
        napot_bits = ctzl(ppn) + 1;
        if ((i != (levels - 1)) || (napot_bits != 4)) {
            return TRANSLATE_FAIL;
        }
    }

    napot_mask = (1 << napot_bits) - 1;
    *physical = (((ppn & ~napot_mask) | (vpn & napot_mask) |
                  (vpn & (((target_ulong)1 << ptshift) - 1))
                 ) << PGSHIFT) | (addr & ~TARGET_PAGE_MASK);

    /*
     * Remove write permission unless this is a store, or the page is
     * already dirty, so that we TLB miss on later writes to update
     * the dirty bit.
     */
    if (access_type != MMU_DATA_STORE && !(pte & PTE_D)) {
        prot &= ~PAGE_WRITE;
    }
    *ret_prot = prot;

    return TRANSLATE_SUCCESS;
}

static void raise_mmu_exception(CPURISCVState *env, target_ulong address,
                                MMUAccessType access_type, bool pmp_violation,
                                bool first_stage, bool two_stage,
                                bool two_stage_indirect)
{
    CPUState *cs = env_cpu(env);

    switch (access_type) {
    case MMU_INST_FETCH:
        if (pmp_violation) {
            cs->exception_index = RISCV_EXCP_INST_ACCESS_FAULT;
        } else if (env->virt_enabled && !first_stage) {
            cs->exception_index = RISCV_EXCP_INST_GUEST_PAGE_FAULT;
        } else {
            cs->exception_index = RISCV_EXCP_INST_PAGE_FAULT;
        }
        break;
    case MMU_DATA_LOAD:
        if (pmp_violation) {
            cs->exception_index = RISCV_EXCP_LOAD_ACCESS_FAULT;
        } else if (two_stage && !first_stage) {
            cs->exception_index = RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT;
        } else {
            cs->exception_index = RISCV_EXCP_LOAD_PAGE_FAULT;
        }
        break;
    case MMU_DATA_STORE:
        if (pmp_violation) {
            cs->exception_index = RISCV_EXCP_STORE_AMO_ACCESS_FAULT;
        } else if (two_stage && !first_stage) {
            cs->exception_index = RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT;
        } else {
            cs->exception_index = RISCV_EXCP_STORE_PAGE_FAULT;
        }
        break;
    default:
        g_assert_not_reached();
    }
    env->badaddr = address;
    env->two_stage_lookup = two_stage;
    env->two_stage_indirect_lookup = two_stage_indirect;
}

hwaddr riscv_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    hwaddr phys_addr;
    int prot;
    int mmu_idx = riscv_env_mmu_index(&cpu->env, false);

    if (get_physical_address(env, &phys_addr, &prot, addr, NULL, 0, mmu_idx,
                             true, env->virt_enabled, true)) {
        return -1;
    }

    if (env->virt_enabled) {
        if (get_physical_address(env, &phys_addr, &prot, phys_addr, NULL,
                                 0, MMUIdx_U, false, true, true)) {
            return -1;
        }
    }

    return phys_addr & TARGET_PAGE_MASK;
}

void riscv_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (access_type == MMU_DATA_STORE) {
        cs->exception_index = RISCV_EXCP_STORE_AMO_ACCESS_FAULT;
    } else if (access_type == MMU_DATA_LOAD) {
        cs->exception_index = RISCV_EXCP_LOAD_ACCESS_FAULT;
    } else {
        cs->exception_index = RISCV_EXCP_INST_ACCESS_FAULT;
    }

    env->badaddr = addr;
    env->two_stage_lookup = mmuidx_2stage(mmu_idx);
    env->two_stage_indirect_lookup = false;
    cpu_loop_exit_restore(cs, retaddr);
}

void riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                   MMUAccessType access_type, int mmu_idx,
                                   uintptr_t retaddr)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    switch (access_type) {
    case MMU_INST_FETCH:
        cs->exception_index = RISCV_EXCP_INST_ADDR_MIS;
        break;
    case MMU_DATA_LOAD:
        cs->exception_index = RISCV_EXCP_LOAD_ADDR_MIS;
        break;
    case MMU_DATA_STORE:
        cs->exception_index = RISCV_EXCP_STORE_AMO_ADDR_MIS;
        break;
    default:
        g_assert_not_reached();
    }
    env->badaddr = addr;
    env->two_stage_lookup = mmuidx_2stage(mmu_idx);
    env->two_stage_indirect_lookup = false;
    cpu_loop_exit_restore(cs, retaddr);
}


static void pmu_tlb_fill_incr_ctr(RISCVCPU *cpu, MMUAccessType access_type)
{
    enum riscv_pmu_event_idx pmu_event_type;

    switch (access_type) {
    case MMU_INST_FETCH:
        pmu_event_type = RISCV_PMU_EVENT_CACHE_ITLB_PREFETCH_MISS;
        break;
    case MMU_DATA_LOAD:
        pmu_event_type = RISCV_PMU_EVENT_CACHE_DTLB_READ_MISS;
        break;
    case MMU_DATA_STORE:
        pmu_event_type = RISCV_PMU_EVENT_CACHE_DTLB_WRITE_MISS;
        break;
    default:
        return;
    }

    riscv_pmu_incr_ctr(cpu, pmu_event_type);
}

bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    vaddr im_address;
    hwaddr pa = 0;
    int prot, prot2, prot_pmp, mtt_prot;
    bool pmp_violation = false;
    bool first_stage_error = true;
    bool two_stage_lookup = mmuidx_2stage(mmu_idx);
    bool two_stage_indirect_error = false;
    int ret = TRANSLATE_FAIL;
    int mode = mmuidx_priv(mmu_idx);
    /* default TLB page size */
    target_ulong tlb_size = TARGET_PAGE_SIZE;
    bool sstack = get_field(mmu_idx, MMU_IDX_SS_ACCESS);
    if (sstack) {
        access_type = MMU_DATA_STORE;
    }

    env->guest_phys_fault_addr = 0;

    qemu_log_mask(CPU_LOG_MMU, "%s ad %" VADDR_PRIx " rw %d mmu_idx %d\n",
                  __func__, address, access_type, mmu_idx);

    pmu_tlb_fill_incr_ctr(cpu, access_type);
    if (two_stage_lookup) {
        /* Two stage lookup */
        ret = get_physical_address(env, &pa, &prot, address,
                                   &env->guest_phys_fault_addr, access_type,
                                   mmu_idx, true, true, false);

        /*
         * A G-stage exception may be triggered during two state lookup.
         * And the env->guest_phys_fault_addr has already been set in
         * get_physical_address().
         */
        if (ret == TRANSLATE_G_STAGE_FAIL) {
            first_stage_error = false;
            two_stage_indirect_error = true;
        }
        qemu_log_mask(CPU_LOG_MMU,
                      "%s 1st-stage address=%" VADDR_PRIx " ret %d physical "
                      HWADDR_FMT_plx " prot %d\n",
                      __func__, address, ret, pa, prot);

        if (ret == TRANSLATE_SUCCESS) {
            /* Second stage lookup */
            im_address = pa;

            ret = get_physical_address(env, &pa, &prot2, im_address, NULL,
                                       access_type, MMUIdx_U, false, true,
                                       false);

            /*
             * Shadow stack instructions that access memory require the
             * G-stage page table to have read-write permission for the
             * accessed memory; else a store/AMO guest-page fault exception
             * is raised
             */
            if (sstack && (prot2 & (PAGE_READ | PAGE_WRITE)) !=
                          (PAGE_READ | PAGE_WRITE)) {
                ret = TRANSLATE_G_STAGE_FAIL;
                first_stage_error = false;
            }
            qemu_log_mask(CPU_LOG_MMU,
                          "%s 2nd-stage address=%" VADDR_PRIx
                          " ret %d physical "
                          HWADDR_FMT_plx " prot %d\n",
                          __func__, im_address, ret, pa, prot2);

            prot &= prot2;

            if (ret == TRANSLATE_SUCCESS) {
                ret = get_physical_address_mtt(env, &mtt_prot, pa,
                                               access_type, mode);
                qemu_log_mask(CPU_LOG_MMU,
                              "%s MTT address=" HWADDR_FMT_plx " ret %d prot"
                              " %d\n",
                              __func__, pa, ret, mtt_prot);
                prot &= mtt_prot;
                if (ret != TRANSLATE_MTT_FAIL) {
                    ret = get_physical_address_pmp(env, &prot_pmp, pa,
                                                   size, access_type, mode);
                    tlb_size = pmp_get_tlb_size(env, pa);
                    qemu_log_mask(CPU_LOG_MMU,
                                  "%s PMP address=" HWADDR_FMT_plx " ret %d"
                                  "prot %d tlb_size " TARGET_FMT_lu "\n",
                                  __func__, pa, ret, prot_pmp, tlb_size);

                    prot &= prot_pmp;
                }
            } else {
                /*
                 * Guest physical address translation failed, this is a HS
                 * level exception
                 */
                first_stage_error = false;
                if (ret != TRANSLATE_PMP_FAIL) {
                    env->guest_phys_fault_addr = (im_address |
                                                  (address &
                                                   (TARGET_PAGE_SIZE - 1))) >> 2;
                }
            }
        }
    } else {
        /* Single stage lookup */
        ret = get_physical_address(env, &pa, &prot, address, NULL,
                                   access_type, mmu_idx, true, false, false);

        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " ret %d physical "
                      HWADDR_FMT_plx " prot %d\n",
                      __func__, address, ret, pa, prot);

        if (ret == TRANSLATE_SUCCESS) {
            ret = get_physical_address_mtt(env, &mtt_prot, pa,
                                           access_type, mode);
            qemu_log_mask(CPU_LOG_MMU,
                          "%s MTT address=" HWADDR_FMT_plx " ret %d prot %d\n",
                          __func__, pa, ret, mtt_prot);
            prot &= mtt_prot;
            if (ret != TRANSLATE_MTT_FAIL) {
                ret = get_physical_address_pmp(env, &prot_pmp, pa,
                                               size, access_type, mode);
                tlb_size = pmp_get_tlb_size(env, pa);

                qemu_log_mask(CPU_LOG_MMU,
                              "%s PMP address=" HWADDR_FMT_plx " ret %d prot"
                              " %d tlb_size " TARGET_FMT_lu "\n",
                              __func__, pa, ret, prot_pmp, tlb_size);
                prot &= prot_pmp;
            }
        }
    }
    /*
     * MTT violation manifest as instruction, load, or store access-fault
     * exceptions, as same as PMP violation in raise_mmu_exception. So we
     * just make pmp_violation = true to raise exception.
     */
    if (ret == TRANSLATE_PMP_FAIL || ret == TRANSLATE_MTT_FAIL) {
        pmp_violation = true;
    }

    if (ret == TRANSLATE_SUCCESS) {
        tlb_set_page(cs, address & ~(tlb_size - 1), pa & ~(tlb_size - 1),
                     sstack ? (PAGE_READ | PAGE_WRITE) : prot,
                     mmu_idx, tlb_size);
        return true;
    } else if (probe) {
        return false;
    } else {
        raise_mmu_exception(env, address, access_type, pmp_violation,
                            first_stage_error, two_stage_lookup,
                            two_stage_indirect_error);
        cpu_loop_exit_restore(cs, retaddr);
    }

    return true;
}

static target_ulong riscv_transformed_insn(CPURISCVState *env,
                                           target_ulong insn,
                                           target_ulong taddr)
{
    target_ulong xinsn = 0;
    target_ulong access_rs1 = 0, access_imm = 0, access_size = 0;

    /*
     * Only Quadrant 0 and Quadrant 2 of RVC instruction space need to
     * be uncompressed. The Quadrant 1 of RVC instruction space need
     * not be transformed because these instructions won't generate
     * any load/store trap.
     */

    if ((insn & 0x3) != 0x3) {
        /* Transform 16bit instruction into 32bit instruction */
        switch (GET_C_OP(insn)) {
        case OPC_RISC_C_OP_QUAD0: /* Quadrant 0 */
            switch (GET_C_FUNC(insn)) {
            case OPC_RISC_C_FUNC_FLD_LQ:
                if (riscv_cpu_xlen(env) != 128) { /* C.FLD (RV32/64) */
                    xinsn = OPC_RISC_FLD;
                    xinsn = SET_RD(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_LD_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_LW: /* C.LW */
                xinsn = OPC_RISC_LW;
                xinsn = SET_RD(xinsn, GET_C_RS2S(insn));
                access_rs1 = GET_C_RS1S(insn);
                access_imm = GET_C_LW_IMM(insn);
                access_size = 4;
                break;
            case OPC_RISC_C_FUNC_FLW_LD:
                if (riscv_cpu_xlen(env) == 32) { /* C.FLW (RV32) */
                    xinsn = OPC_RISC_FLW;
                    xinsn = SET_RD(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_LW_IMM(insn);
                    access_size = 4;
                } else { /* C.LD (RV64/RV128) */
                    xinsn = OPC_RISC_LD;
                    xinsn = SET_RD(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_LD_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_FSD_SQ:
                if (riscv_cpu_xlen(env) != 128) { /* C.FSD (RV32/64) */
                    xinsn = OPC_RISC_FSD;
                    xinsn = SET_RS2(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_SD_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_SW: /* C.SW */
                xinsn = OPC_RISC_SW;
                xinsn = SET_RS2(xinsn, GET_C_RS2S(insn));
                access_rs1 = GET_C_RS1S(insn);
                access_imm = GET_C_SW_IMM(insn);
                access_size = 4;
                break;
            case OPC_RISC_C_FUNC_FSW_SD:
                if (riscv_cpu_xlen(env) == 32) { /* C.FSW (RV32) */
                    xinsn = OPC_RISC_FSW;
                    xinsn = SET_RS2(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_SW_IMM(insn);
                    access_size = 4;
                } else { /* C.SD (RV64/RV128) */
                    xinsn = OPC_RISC_SD;
                    xinsn = SET_RS2(xinsn, GET_C_RS2S(insn));
                    access_rs1 = GET_C_RS1S(insn);
                    access_imm = GET_C_SD_IMM(insn);
                    access_size = 8;
                }
                break;
            default:
                break;
            }
            break;
        case OPC_RISC_C_OP_QUAD2: /* Quadrant 2 */
            switch (GET_C_FUNC(insn)) {
            case OPC_RISC_C_FUNC_FLDSP_LQSP:
                if (riscv_cpu_xlen(env) != 128) { /* C.FLDSP (RV32/64) */
                    xinsn = OPC_RISC_FLD;
                    xinsn = SET_RD(xinsn, GET_C_RD(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_LDSP_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_LWSP: /* C.LWSP */
                xinsn = OPC_RISC_LW;
                xinsn = SET_RD(xinsn, GET_C_RD(insn));
                access_rs1 = 2;
                access_imm = GET_C_LWSP_IMM(insn);
                access_size = 4;
                break;
            case OPC_RISC_C_FUNC_FLWSP_LDSP:
                if (riscv_cpu_xlen(env) == 32) { /* C.FLWSP (RV32) */
                    xinsn = OPC_RISC_FLW;
                    xinsn = SET_RD(xinsn, GET_C_RD(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_LWSP_IMM(insn);
                    access_size = 4;
                } else { /* C.LDSP (RV64/RV128) */
                    xinsn = OPC_RISC_LD;
                    xinsn = SET_RD(xinsn, GET_C_RD(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_LDSP_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_FSDSP_SQSP:
                if (riscv_cpu_xlen(env) != 128) { /* C.FSDSP (RV32/64) */
                    xinsn = OPC_RISC_FSD;
                    xinsn = SET_RS2(xinsn, GET_C_RS2(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_SDSP_IMM(insn);
                    access_size = 8;
                }
                break;
            case OPC_RISC_C_FUNC_SWSP: /* C.SWSP */
                xinsn = OPC_RISC_SW;
                xinsn = SET_RS2(xinsn, GET_C_RS2(insn));
                access_rs1 = 2;
                access_imm = GET_C_SWSP_IMM(insn);
                access_size = 4;
                break;
            case 7:
                if (riscv_cpu_xlen(env) == 32) { /* C.FSWSP (RV32) */
                    xinsn = OPC_RISC_FSW;
                    xinsn = SET_RS2(xinsn, GET_C_RS2(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_SWSP_IMM(insn);
                    access_size = 4;
                } else { /* C.SDSP (RV64/RV128) */
                    xinsn = OPC_RISC_SD;
                    xinsn = SET_RS2(xinsn, GET_C_RS2(insn));
                    access_rs1 = 2;
                    access_imm = GET_C_SDSP_IMM(insn);
                    access_size = 8;
                }
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }

        /*
         * Clear Bit1 of transformed instruction to indicate that
         * original insruction was a 16bit instruction
         */
        xinsn &= ~((target_ulong)0x2);
    } else {
        /* Transform 32bit (or wider) instructions */
        switch (MASK_OP_MAJOR(insn)) {
        case OPC_RISC_ATOMIC:
            xinsn = insn;
            access_rs1 = GET_RS1(insn);
            access_size = 1 << GET_FUNCT3(insn);
            break;
        case OPC_RISC_LOAD:
        case OPC_RISC_FP_LOAD:
            xinsn = SET_I_IMM(insn, 0);
            access_rs1 = GET_RS1(insn);
            access_imm = GET_IMM(insn);
            access_size = 1 << GET_FUNCT3(insn);
            break;
        case OPC_RISC_STORE:
        case OPC_RISC_FP_STORE:
            xinsn = SET_S_IMM(insn, 0);
            access_rs1 = GET_RS1(insn);
            access_imm = GET_STORE_IMM(insn);
            access_size = 1 << GET_FUNCT3(insn);
            break;
        case OPC_RISC_SYSTEM:
            if (MASK_OP_SYSTEM(insn) == OPC_RISC_HLVHSV) {
                xinsn = insn;
                access_rs1 = GET_RS1(insn);
                access_size = 1 << ((GET_FUNCT7(insn) >> 1) & 0x3);
                access_size = 1 << access_size;
            }
            break;
        default:
            break;
        }
    }

    if (access_size) {
        xinsn = SET_RS1(xinsn, (taddr - (env->gpr[access_rs1] + access_imm)) &
                               (access_size - 1));
    }

    return xinsn;
}
#endif /* !CONFIG_USER_ONLY */

/*
 * Handle Traps
 *
 * Adapted from Spike's processor_t::take_trap.
 *
 */
void riscv_cpu_do_interrupt(CPUState *cs)
{
#if !defined(CONFIG_USER_ONLY)

    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    bool virt = env->virt_enabled;
    const RISCVCPUConfig *cfg = riscv_cpu_cfg(env);
    bool write_gva = false;
    bool smode_exception;
    bool vsmode_exception;
    uint64_t s;
    int mode, level;
#ifndef _WIN32
    if (env->priv == PRV_U && is_bbv_tb_trans_registered()) {
        qemu_plugin_other_process_cb();
    }
#endif

    /*
     * cs->exception is 32-bits wide unlike mcause which is XLEN-bits wide
     * so we mask off the MSB and separate into trap type and cause.
     */
    bool async = !!(cs->exception_index & RISCV_EXCP_INT_FLAG);
    bool clic = !!(cs->exception_index & RISCV_EXCP_INT_CLIC);
    target_ulong cause = cs->exception_index & RISCV_EXCP_INT_MASK;
    target_ulong exccode = clic ? cause & 0xfff : cause;
    uint64_t deleg = async ? env->mideleg : env->medeleg;
    bool s_injected = env->mvip & (1 << cause) & env->mvien &&
        !(env->mip & (1 << cause));
    bool vs_injected = env->hvip & (1 << cause) & env->hvien &&
        !(env->mip & (1 << cause));
    const bool prev_virt = env->virt_enabled;
    const target_ulong prev_priv = env->priv;
    bool smode_double_trap = false;
    uint64_t hdeleg = async ? env->hideleg : env->hedeleg;
    target_ulong tval = 0;
    target_ulong tinst = 0;
    target_ulong htval = 0;
    target_ulong mtval2 = 0;
    target_ulong src;

    if (!async) {
        /* set tval to badaddr for traps with address information */
        switch (cause) {
        case RISCV_EXCP_SEMIHOST:
            do_common_semihosting(cs);
            env->pc += 4;
            return;
        case RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT:
        case RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT:
        case RISCV_EXCP_LOAD_ADDR_MIS:
        case RISCV_EXCP_STORE_AMO_ADDR_MIS:
        case RISCV_EXCP_LOAD_ACCESS_FAULT:
        case RISCV_EXCP_STORE_AMO_ACCESS_FAULT:
        case RISCV_EXCP_LOAD_PAGE_FAULT:
        case RISCV_EXCP_STORE_PAGE_FAULT:
            write_gva = env->two_stage_lookup;
            tval = env->badaddr;
            if (env->two_stage_indirect_lookup) {
                /*
                 * special pseudoinstruction for G-stage fault taken while
                 * doing VS-stage page table walk.
                 */
                tinst = (riscv_cpu_xlen(env) == 32) ? 0x00002000 : 0x00003000;
            } else {
                /*
                 * The "Addr. Offset" field in transformed instruction is
                 * non-zero only for misaligned access.
                 */
                tinst = riscv_transformed_insn(env, env->bins, tval);
            }
            break;
        case RISCV_EXCP_INST_GUEST_PAGE_FAULT:
        case RISCV_EXCP_INST_ADDR_MIS:
        case RISCV_EXCP_INST_ACCESS_FAULT:
        case RISCV_EXCP_INST_PAGE_FAULT:
            write_gva = env->two_stage_lookup;
            tval = env->badaddr;
            if (env->two_stage_indirect_lookup) {
                /*
                 * special pseudoinstruction for G-stage fault taken while
                 * doing VS-stage page table walk.
                 */
                tinst = (riscv_cpu_xlen(env) == 32) ? 0x00002000 : 0x00003000;
            }
            break;
        case RISCV_EXCP_ILLEGAL_INST:
        case RISCV_EXCP_VIRT_INSTRUCTION_FAULT:
            tval = env->bins;
            break;
        case RISCV_EXCP_BREAKPOINT:
            tval = env->badaddr;
            if (cs->watchpoint_hit) {
                tval = cs->watchpoint_hit->hitaddr;
                cs->watchpoint_hit = NULL;
            }
            riscv_ctr_freeze(env, MCTRCTL_BPFRZ);
            break;
        case RISCV_EXCP_SW_CHECK:
            tval = env->cfi_violation_code;
            break;
        default:
            break;
        }
        /* ecall is dispatched as one cause so translate based on mode */
        if (cause == RISCV_EXCP_U_ECALL) {
            assert(env->priv <= 3);

            if (env->priv == PRV_M) {
                cause = RISCV_EXCP_M_ECALL;
            } else if (env->priv == PRV_S && env->virt_enabled) {
                cause = RISCV_EXCP_VS_ECALL;
            } else if (env->priv == PRV_S && !env->virt_enabled) {
                cause = RISCV_EXCP_S_ECALL;
            } else if (env->priv == PRV_U) {
                cause = RISCV_EXCP_U_ECALL;
            }
        }
    }

    if (clic) {
        mode = (cause >> 12) & 3;
        level = (cause >> 14) & 0xff;
        cause &= 0xfff;
        cause |= get_field(env->mstatus, MSTATUS_MPP) << 28;
        switch (mode) {
        case PRV_M:
            cause |= get_field(env->mintstatus, MINTSTATUS_MIL) << 16;
            cause |= get_field(env->mstatus, MSTATUS_MIE) << 27;
            env->mintstatus = set_field(env->mintstatus, MINTSTATUS_MIL, level);
            break;
        case PRV_S:
            cause |= get_field(env->mintstatus, MINTSTATUS_SIL) << 16;
            cause |= get_field(env->mstatus, MSTATUS_SPIE) << 27;
            env->mintstatus = set_field(env->mintstatus, MINTSTATUS_SIL, level);
            break;
        }
    } else {
        mode = env->priv <= PRV_S &&
         (((deleg >> cause) & 1) || s_injected || vs_injected) ? PRV_S : PRV_M;
    }

    trace_riscv_trap(env->mhartid, async, exccode, env->pc, tval,
                     riscv_cpu_get_trap_name(exccode, async, clic));

    qemu_log_mask(CPU_LOG_INT,
                  "%s: hart:"TARGET_FMT_ld", async:%d, cause:"TARGET_FMT_lx", "
                  "epc:0x"TARGET_FMT_lx", tval:0x"TARGET_FMT_lx", desc=%s\n",
                  __func__, env->mhartid, async, exccode, env->pc, tval,
                  riscv_cpu_get_trap_name(exccode, async, clic));

    smode_exception = env->priv <= PRV_S && cause < 64 &&
                      (((deleg >> cause) & 1) || s_injected || vs_injected);
    vsmode_exception = env->virt_enabled &&
                       (((hdeleg >> cause) & 1) || vs_injected);
    /* Check S-mode double trap condition */
    if (cfg->ext_ssdbltrp && smode_exception) {
        uint64_t dte = env->menvcfg & MENVCFG_DTE;
        if (riscv_has_ext(env, RVH)) {
            if (vsmode_exception) {
                /* Trap to VS mode, use henvcfg instead of menvcfg*/
                dte = env->henvcfg & HENVCFG_DTE;
            } else if (env->virt_enabled) {
                /* Trap into HS mode, from virt
                 * We can not have a double trap when switching from one mode to
                 * another since sret clears the SDT flag, so when trapping in
                 * S-mode, SDT is cleared
                 * */
                dte = 0;
            }
        }
        smode_double_trap = dte && (env->mstatus & MSTATUS_SDT);
    }

    if (smode_exception && !smode_double_trap) {
        /* handle the trap in S-mode */
        if (riscv_has_ext(env, RVH)) {
            if (vsmode_exception) {
                /* Trap to VS mode */
                /*
                 * See if we need to adjust cause. Yes if its VS mode interrupt
                 * no if hypervisor has delegated one of hs mode's interrupt
                 */
                if (async && (cause == IRQ_VS_TIMER || cause == IRQ_VS_SOFT ||
                              cause == IRQ_VS_EXT)) {
                    cause = cause - 1;
                }
                write_gva = false;
                if (riscv_cpu_get_xlpe(env)) {
                    env->vsstatus = set_field(env->vsstatus, VSSTATUS_SPELP,
                                              env->elp);
                }
            } else if (env->virt_enabled) {
                /* Trap into HS mode, from virt */
                if (riscv_cpu_get_xlpe(env)) {
                    env->mstatus = set_field(env->mstatus, MSTATUS_SPELP,
                                             env->elp);
                }
                riscv_cpu_swap_hypervisor_regs(env);
                env->hstatus = set_field(env->hstatus, HSTATUS_SPVP,
                                         env->priv);
                env->hstatus = set_field(env->hstatus, HSTATUS_SPV, true);

                htval = env->guest_phys_fault_addr;

                virt = false;
            } else {
                /* Trap into HS mode */
                env->hstatus = set_field(env->hstatus, HSTATUS_SPV, false);
                htval = env->guest_phys_fault_addr;
                if (riscv_cpu_get_xlpe(env)) {
                    env->mstatus = set_field(env->mstatus, MSTATUS_SPELP,
                                             env->elp);
                }
            }
            env->hstatus = set_field(env->hstatus, HSTATUS_GVA, write_gva);
        }

        s = env->mstatus;
        s = set_field(s, MSTATUS_SPIE, get_field(s, MSTATUS_SIE));
        s = set_field(s, MSTATUS_SPP, env->priv);
        s = set_field(s, MSTATUS_SIE, 0);
        if (riscv_env_smode_dbltrp_enabled(env)) {
            s = set_field(s, MSTATUS_SDT, 1);
        }
        env->mstatus = s;
        env->scause = cause | ((target_ulong)async << (TARGET_LONG_BITS - 1));
        env->sepc = env->pc;
        env->stval = tval;
        env->htval = htval;
        env->htinst = tinst;
        env->pc = riscv_intr_pc(env, env->stvec, env->stvt, async,
                                clic, cause & 0xfff, PRV_S);
        riscv_cpu_set_mode(env, PRV_S, virt);

        src = env->sepc;
    } else {
        /* handle the trap in M-mode */
        if (riscv_has_ext(env, RVH)) {
            if (env->virt_enabled) {
                riscv_cpu_swap_hypervisor_regs(env);
            }
            env->mstatus = set_field(env->mstatus, MSTATUS_MPV,
                                     env->virt_enabled);
            if (env->virt_enabled && tval) {
                env->mstatus = set_field(env->mstatus, MSTATUS_GVA, 1);
            }

            mtval2 = env->guest_phys_fault_addr;

            /* Trapping to M mode, virt is disabled */
            virt = false;
        }
        if (riscv_cpu_get_xlpe(env)) {
            env->mstatus = set_field(env->mstatus, MSTATUS_MPELP,
                                     env->elp);
        }

        s = env->mstatus;
        s = set_field(s, MSTATUS_MPIE, get_field(s, MSTATUS_MIE));
        s = set_field(s, MSTATUS_MPP, env->priv);
        s = set_field(s, MSTATUS_MIE, 0);
        if (riscv_cpu_cfg(env)->ext_smdbltrp) {
            if (env->mstatus & MSTATUS_MDT)
                cpu_abort(CPU(cpu), "M-mode double trap\n");

            s = set_field(s, MSTATUS_MDT, 1);
        }
        env->mstatus = s;
        env->mcause = cause | ~(((target_ulong)-1) >> async);
        if (smode_double_trap) {
            env->mtval2 = env->mcause;
            env->mcause = RISCV_EXCP_DOUBLE_TRAP;
        } else {
            env->mtval2 = mtval2;
        }
        env->mepc = env->pc;
        env->mtval = tval;
        env->mtinst = tinst;
        if (tfilter.enable) {
            if (env->mcause & (RISCV_EXCP_INT_FLAG)) {
                write_trace_8_24(INST_EXCP, 8,
                                 (env->mcause & RISCV_EXCP_INT_MASK) + 32,
                                 env->mepc);
            } else {
                write_trace_8_24(INST_EXCP, 8, env->mcause, env->mepc);
            }
        }
        cause = cause & 0xfff;
        if (clic) {
            target_ulong mpil = get_field(env->mcause, MCAUSE_MPIL);
            assert((target_long)env->mcause < 0);
            if ((mpil == 0) && (env->mexstatus & MEXSTATUS_SPSWAP)) {
                target_ulong tmp = env->mscratch;
                env->mscratch = env->gpr[2];
                env->gpr[2] = tmp;
            }
            /* Automatically clear pending for edge and vector interrupt */
            if (xt_clic_shv_interrupt(env->clic, cause) &&
                xt_clic_edge_triggered(env->clic, cause)) {
                xt_clic_clean_pending(env->clic, cause);
            }
        }
        env->pc = riscv_intr_pc(env, env->mtvec, env->mtvt, async,
                                clic, cause, PRV_M);
        /* FIXME: Add Xuantie check */
        env->excp_vld = 1;
        riscv_cpu_set_mode(env, PRV_M, virt);
        src = env->mepc;
    }

    riscv_ctr_add_entry(env, src, env->pc,
                        async ? CTRDATA_TYPE_INTERRUPT : CTRDATA_TYPE_EXCEPTION,
                        prev_priv, prev_virt);

    /*
     * NOTE: it is not necessary to yield load reservations here. It is only
     * necessary for an SC from "another hart" to cause a load reservation
     * to be yielded. Refer to the memory consistency model section of the
     * RISC-V ISA Specification.
     */

    env->elp = NO_LP_EXPECTED;
    env->two_stage_lookup = false;
    env->two_stage_indirect_lookup = false;
    env->exccode = 0;
#endif
    cs->exception_index = RISCV_EXCP_NONE; /* mark handled to qemu */
}

bool riscv_cpu_get_xsse(CPURISCVState *env)
{
    if (!riscv_cpu_cfg(env)->ext_zicfiss) {
        return false;
    }

    switch (env->priv) {
    case PRV_U:
        return (env->senvcfg & SENVCFG_SSE) ? true : false;
#ifndef CONFIG_USER_ONLY
    case PRV_S:
        if (env->virt_enabled) {
            return (env->henvcfg & HENVCFG_SSE) ? true : false;
        }
        return (env->menvcfg & MENVCFG_SSE) ? true : false;
    case PRV_M:
        return false;
#endif
    default:
        g_assert_not_reached();
    }
}

bool riscv_cpu_get_xlpe(CPURISCVState *env)
{
    /* no cfi extension, return false */
    if (!env_archcpu(env)->cfg.ext_zicfilp) {
        return false;
    }

    switch (env->priv) {
        case PRV_U:
            if (riscv_has_ext(env, RVS)) {
                return env->senvcfg & SENVCFG_LPE;
            }
            return env->menvcfg & MENVCFG_LPE;
#ifndef CONFIG_USER_ONLY
        case PRV_S:
            if (env->virt_enabled) {
                return env->henvcfg & HENVCFG_LPE;
            }
            return env->menvcfg & MENVCFG_LPE;
        case PRV_M:
            return env->mseccfg & MSECCFG_MLPE;
#endif
        default:
            g_assert_not_reached();
    }
}
