/*
 * C-SKY virtual CPU header
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CSKY_CPU_H
#define CSKY_CPU_H

#define ALIGNED_ONLY

#include "exec/cpu-defs.h"
#include "cpu-qom.h"
#include "exec/tracestub.h"
#include "exec/pctrace.h"
#include "fpu/softfloat.h"
#include "exec/gdbstub.h"

/* CSKY Exception definition */
#define EXCP_NONE                   -1
#define EXCP_CSKY_RESET             0
#define EXCP_CSKY_ALIGN             1
#define EXCP_CSKY_DATA_ABORT        2
#define EXCP_CSKY_DIV               3
#define EXCP_CSKY_UDEF              4
#define EXCP_CSKY_PRIVILEGE         5
#define EXCP_CSKY_TRACE             6
#define EXCP_CSKY_BKPT              7
#define EXCP_CSKY_URESTORE          8
#define EXCP_CSKY_IDLY4             9
#define EXCP_CSKY_IRQ               10
#define EXCP_CSKY_FIQ               11
#define EXCP_CSKY_HAI               12
#define EXCP_CSKY_FP                13
#define EXCP_CSKY_TLB_UNMATCH       14
#define EXCP_CSKY_TLB_MODIFY        15
#define EXCP_CSKY_TRAP0             16
#define EXCP_CSKY_TRAP1             17
#define EXCP_CSKY_TRAP2             18
#define EXCP_CSKY_TRAP3             19
#define EXCP_CSKY_TLB_READ_INVALID  20
#define EXCP_CSKY_TLB_WRITE_INVALID 21
#define EXCP_CSKY_SEMIHOST          28
#define EXCP_CSKY_FLOAT             30
#define EXCP_CSKY_CPU_END           31

#define EXCP_CSKY_EXIT              (EXCP_INTERRUPT - 0x1)
#define EXCP_CSKY_INT_EXIT          (EXCP_INTERRUPT - 0x2)
#define EXCP_CSKY_IN_RESET          (EXCP_INTERRUPT + 0x10000)

#define CPU_INTERRUPT_FIQ   CPU_INTERRUPT_TGT_EXT_1

/* MMU Control Registers */
struct CSKYMMU {
   uint32_t mir;        /* CR0 */
   uint32_t mrr;        /* CR1 */
   uint32_t mel0;       /* CR2 */
   uint32_t mel1;       /* CR3 */
   uint32_t meh;        /* CR4 */
   uint32_t mcr;        /* CR5 */
   uint32_t mpr;        /* CR6 */
   uint32_t mwr;        /* CR7 */
   uint32_t mcir;       /* CR8 */
   uint32_t cr9;        /* CR9 */
   uint32_t cr10;       /* CR10 */
   uint32_t cr11;       /* CR11 */
   uint32_t cr12;       /* CR12 */
   uint32_t cr13;       /* CR13 */
   uint32_t cr14;       /* CR14 */
   uint32_t cr15;       /* CR15 */
   uint32_t cr16;       /* CR16 */
   uint32_t mpgd0;      /* CR28, only for ck860 */
   uint32_t mpgd1;      /* CR29, mpgd reg in ck610/ck810 */
   uint32_t msa0;       /* CR30 */
   uint32_t msa1;       /* CR31 */
};

/* CSKY CPUCSKYState definition */
struct CPUArchState {
    uint32_t regs[32];
    /* target pc */
    uint32_t pc;
    /* C register PSR[0] */
    uint32_t psr_c;
    /* S register PSR[31] */
    uint32_t psr_s;
    /* T register PSR[30] */
    uint32_t psr_t;
    /* bm register PSR[10] */
    uint32_t psr_bm;
    /* TM register PSR[15:14] */
    uint32_t psr_tm;
    /* dsp control status register */
    uint32_t dcsr_v;
    /* dsp hi, lo, high_guard, lo_guard register */
    uint32_t hi;
    uint32_t lo;
    uint32_t hi_guard;
    uint32_t lo_guard;
    /* Banked Registers */
    uint32_t banked_regs[16];
    /* Idly4 counter */
    uint32_t idly4_counter;
    /* which instructions sequences should be translation */
    uint32_t sce_condexec_bits;
    /* sce sequence may be interrupted */
    uint32_t sce_condexec_bits_bk;
    /* interface for intc */
    struct {
        uint32_t avec_b;
        uint32_t fint_b;
        uint32_t int_b;
        uint32_t vec_b;
    } intc_signals;

    /* system control coprocessor (cp0) */
    struct {
        uint32_t psr;    /* CR0 */
        uint32_t vbr;    /* CR1 */
        uint32_t epsr;   /* CR2 */
        uint32_t fpsr;   /* CR3 */
        uint32_t epc;    /* CR4 */
        uint32_t fpc;    /* CR5 */
        uint32_t ss0;    /* CR6 */
        uint32_t ss1;    /* CR7 */
        uint32_t ss2;    /* CR8 */
        uint32_t ss3;    /* CR9 */
        uint32_t ss4;    /* CR10 */
        uint32_t gcr;    /* CR11 */
        uint32_t gsr;    /* CR12 */
        uint32_t cpidr[8];  /* CSKYV2 have 8 physic CR13 register */
        uint32_t cpidr_counter;
        uint32_t dcsr;    /* CR14 */
        uint32_t cpwr;    /* CR15 */
        uint32_t dummy;   /* no CR16 */
        uint32_t cfr;     /* CR17 */
        uint32_t ccr;     /* CR18 */
        uint32_t capr;    /* CR19 */
        uint32_t pacr[8]; /* CR20 */
        uint32_t prsr;    /* CR21 */
        uint32_t mpid;    /* CR30 */
        uint32_t chr;     /* CR31 */
    } cp0;

    /* stack point, sp used now is put in regs[14].
     * if cpu not has the feature ABIV2_TEE, only use nt_Xsp. */
    struct {
        uint32_t nt_usp;  /* Non-secured user sp */
        uint32_t nt_ssp;  /* CR<6,3>, Non-secured supervisor sp */
        uint32_t nt_asp;  /* AF = 1, Non-secured sp */
        uint32_t nt_int_sp; /* CR<15, 1>, ISE = 1, Non-secured interrupt sp. */
        uint32_t t_usp;   /* CR<7,3>, Secured user sp */
        uint32_t t_ssp;   /* Secured supervisor sp */
        uint32_t t_asp;   /* AF = 1, Secured sp */
        uint32_t t_int_sp;  /* CR<15, 1>, ISE = 1, Secured interrupt sp. */
    } stackpoint;

    /* registers for tee */
    struct {
        uint32_t t_psr;   /* CR<0,0>, T_PSR */
        uint32_t nt_psr;  /* CR<0,0>, CR<0,3>, NT_PSR */
        uint32_t t_vbr;   /* CR<1,0>, T_VBR */
        uint32_t nt_vbr;  /* CR<1,0>, CR<1,3>, NT_VBR */
        uint32_t t_epsr;  /* CR<2,0>, T_EPSR */
        uint32_t nt_epsr; /* CR<2,0>, CR<2,3>, NT_EPSR */
        uint32_t t_epc;   /* CR<4,0>, T_EPC */
        uint32_t nt_epc;  /* CR<4,0>, CR<4,3>, NT_EPC */
        uint32_t t_dcr;   /* CR<8,3>, T_DCR */
        uint32_t t_pcr;   /* CR<9,3>, T_PCR */
        uint32_t t_ebr;   /* CR<1,1>, T_EBR */
        uint32_t nt_ebr;  /* CR<1,1>, CR<10,3>, NT_EBR */
    } tee;

    /* FPU registers */
    struct {
        float32 fr[32];     /* FPU general registers */
        uint32_t fpcid;     /* Provide the information about FPC. */
        uint32_t fcr;       /* Control register of FPC */
        uint32_t fsr;       /* Status register of FPC */
        uint32_t fir;       /* Instruction register of FPC */
        uint32_t fesr;      /* Status register for exception process */
        uint32_t feinst1;   /* The exceptional instruction */
        uint32_t feinst2;   /* The exceptional instruction */
        float_status fp_status;
        float_status standard_fp_status;
    } cp1;

    /* VFP coprocessor state.  */
    struct {
        union VDSP {
            float64  fpu[2];
            float64  f64[2];
            float32  f32[4];
            float16  f16[8];
            uint64_t udspl[2];
            int64_t  dspl[2];
            uint32_t udspi[4];
            int32_t  dspi[4];
            uint16_t udsps[8];
            int16_t  dsps[8];
            uint8_t  udspc[16];
            int8_t   dspc[16];
        } reg[32];
        uint32_t fid;
        uint32_t fcr;
        uint32_t fesr;
        /*
         *fp_status is the "normal" fp status. standard_fp_status retains
         * values corresponding to the ARM "Standard FPSCR Value", ie
         * default-NaN, flush-to-zero, round-to-nearest and is used by
         * any operations (generally Neon) which the architecture defines
         * as controlled by the standard FPSCR value rather than the FPSCR.
         *
         * To avoid having to transfer exception bits around, we simply
         * say that the FPSCR cumulative exception flags are the logical
         * OR of the flags in the two fp statuses. This relies on the
         * only thing which needs to read the exception flags being
         * an explicit FPSCR read.
         */
        float_status fp_status;
        float_status standard_fp_status;
    } vfp;
    /* trace registers */
    struct {
            uint32_t tcr;
            uint32_t ter;
            uint32_t tsr;
            uint32_t cyc;
            uint32_t sync;
            uint32_t hw_trgr;
            uint32_t addr_cmpr_config[2];
            uint32_t addr_cmpr[2];
            uint32_t asid;
            uint32_t data_cmpr_config[2];
            uint32_t data_cmpr[2];
            uint32_t channel;
            uint32_t data;
            uint32_t status;
    } cp13;
    /* MPTimer coprocessor state. */
    struct {
        uint32_t ctlr;
        uint32_t isr;
        uint32_t ccvr_hi;
        uint32_t ccvr_lo;
        uint32_t cmvr_hi;
        uint32_t cmvr_lo;
        uint32_t lvr;
    } mptimer;
    /* intc base addr */
    uint32_t intc_bar;
    uint32_t excl_val;
    uint32_t excl_addr;
    struct CSKYMMU mmu;     /* mmu control registers used now. */
    struct CSKYMMU nt_mmu;  /* Non-Trust mmu control registers. */
    struct CSKYMMU t_mmu;   /* Non-Trust mmu control registers. */

#if !defined(CONFIG_USER_ONLY)
    struct CPUCSKYTLBContext *tlb_context;
#endif

    uint32_t tls_value;
    bool in_reset;

    /* These fields after the common ones so they are preserved on reset.  */

    /* Internal CPU feature flags.  */
    uint64_t features;

    /* pctrace */
    uint32_t pctrace;

    /* binstart */
    uint32_t binstart;

    uint32_t cpuid;

    void *nvic;
    void *mptimerdev;
    uint32_t mmu_default;
    uint32_t full_mmu;

    uint32_t tb_trace;
    uint32_t jcount_enable;
    uint32_t jcount_start;
    uint32_t jcount_end;
    target_ulong last_pc;
    uint32_t cpu_freq;
    bool     hbreak;
    bool     exit_bkpt;

    struct csky_boot_info *boot_info;
    struct csky_trace_info *trace_info;
    uint32_t trace_index;
    struct CPUArchState *next_cpu;
    uint32_t tb_count;
    uint32_t exit_addr;
};

/**
 * CSKYCPU:
 * @env: #CPUCSKYState
 *
 * A CSKY CPU.
 */
struct ArchCPU {
    /*< private >*/
    CPUState parent_obj;

    CPUCSKYState env;
};

/* functions statement */
CSKYCPU *cpu_csky_init(const char *cpu_model);
void csky_translate_init(void);
int csky_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int size, int rw,
                              int mmu_idx);
int cpu_csky_signal_handler(int host_signum, void *pinfo, void *puc);
void csky_cpu_list(void);

G_NORETURN void csky_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                             MMUAccessType access_type,
                                             int mmu_idx, uintptr_t retaddr);
int csky_cpu_gdb_read_register(CPUState *cs, GByteArray *buf, int n);
int csky_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n);
void csky_cpu_do_interrupt(CPUState *cs);
bool csky_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr);
hwaddr csky_cpu_get_phys_page_debug(CPUState *env, vaddr addr);
bool csky_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
void csky_nommu_init(CPUCSKYState *env);
void csky_cpu_dump_state(CPUState *cs, FILE *f, int flags);
target_ulong csky_do_semihosting(CPUCSKYState *env);

void csky_nvic_update_irq(void *opaque);
bool csky_nvic_secure_irq(void *opaque, uint32_t irq);
void csky_nvic_acknowledge_irq(void *opaque, uint32_t irq);
void csky_nvic_complete_irq(void *opaque);

#define CSKY_CPU_TYPE_SUFFIX "-" TYPE_CSKY_CPU
#define CSKY_CPU_TYPE_NAME(name) (name CSKY_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_CSKY_CPU

#define cpu_signal_handler  cpu_csky_signal_handler
#define cpu_list    csky_cpu_list

/* FIXME MMU modes definitions */
#define MMU_USER_IDX  0
#define CSKY_USERMODE 0

#include "exec/cpu-all.h"

/* bit usage in tb flags field. */
#define CSKY_TBFLAG_SCE_CONDEXEC_SHIFT  0
#define CSKY_TBFLAG_SCE_CONDEXEC_MASK   (0x1F << CSKY_TBFLAG_SCE_CONDEXEC_SHIFT)
#define CSKY_TBFLAG_PSR_S_SHIFT         5
#define CSKY_TBFLAG_PSR_S_MASK          (0x1 << CSKY_TBFLAG_PSR_S_SHIFT)
#define CSKY_TBFLAG_CPID_SHIFT          6
#define CSKY_TBFLAG_CPID_MASK           (0xF << CSKY_TBFLAG_CPID_SHIFT)
#define CSKY_TBFLAG_ASID_SHIFT          10
#define CSKY_TBFLAG_MP_ASID_MASK        (0xFFF << CSKY_TBFLAG_ASID_SHIFT)
#define CSKY_TBFLAG_ASID_MASK           (0xFF << CSKY_TBFLAG_ASID_SHIFT)
#define CSKY_TBFLAG_PSR_BM_SHIFT        22
#define CSKY_TBFLAG_PSR_BM_MASK         (0x1 << CSKY_TBFLAG_PSR_BM_SHIFT)
#define CSKY_TBFLAG_PSR_TM_SHIFT        23
#define CSKY_TBFLAG_PSR_TM_MASK         (0x3 << CSKY_TBFLAG_PSR_TM_SHIFT)
#define CSKY_TBFLAG_PSR_T_SHIFT         25
#define CSKY_TBFLAG_PSR_T_MASK          (0x1 << CSKY_TBFLAG_PSR_T_SHIFT)
#define CSKY_TBFLAG_IDLY4_SHIFT         26
#define CSKY_TBFLAG_IDLY4_MASK          (0x7 << CSKY_TBFLAG_IDLY4_SHIFT)
#define CSKY_TBFLAG_SINGLESTEP_SHIFT    29
#define CSKY_TBFLAG_SINGLESTEP_MASK     (0x1 << CSKY_TBFLAG_SINGLESTEP_SHIFT)
/* TB flags[29:31] are unused */


#define CSKY_TBFLAG_SCE_CONDEXEC(flag)  \
    (((flag) & CSKY_TBFLAG_SCE_CONDEXEC_MASK) >> CSKY_TBFLAG_SCE_CONDEXEC_SHIFT)
#define CSKY_TBFLAG_PSR_S(flag) \
    (((flag) & CSKY_TBFLAG_PSR_S_MASK) >> CSKY_TBFLAG_PSR_S_SHIFT)
#define CSKY_TBFLAG_PSR_BM(flag) \
    (((flag) & CSKY_TBFLAG_PSR_BM_MASK) >> CSKY_TBFLAG_PSR_BM_SHIFT)
#define CSKY_TBFLAG_CPID(flag)    \
    (((flag) & CSKY_TBFLAG_CPID_MASK) >> CSKY_TBFLAG_CPID_SHIFT)
#define CSKY_TBFLAG_PSR_TM(flag)    \
    (((flag) & CSKY_TBFLAG_PSR_TM_MASK) >> CSKY_TBFLAG_PSR_TM_SHIFT)
#define CSKY_TBFLAG_PSR_T(flag)    \
    (((flag) & CSKY_TBFLAG_PSR_T_MASK) >> CSKY_TBFLAG_PSR_T_SHIFT)

/* CPU id */
#define CSKY_CPUID_CK510        0x00000000
#define CSKY_CPUID_CK520        0x00000000
#define CSKY_CPUID_CK610        0x1000f002
#define CSKY_CPUID_CK620        0x1000f002

#define CSKY_CPUID_E801         0x0400000c
#define CSKY_CPUID_E802         0x0404000c
#define CSKY_CPUID_E803         0x0408000c
#define CSKY_CPUID_E804         0x040c000c
#define CSKY_CPUID_R807         0x0440000c
#define CSKY_CPUID_I805         0x0480000c
#define CSKY_CPUID_S802         0x04c0000c
#define CSKY_CPUID_S803         0x04c4000c
#define CSKY_CPUID_C807         0x0500000c
#define CSKY_CPUID_C810         0x0504000c
#define CSKY_CPUID_C860         0x0508000c

/* cpu features flags */
#define CPU_ABIV1               ((uint64_t)0x1 << 0)
#define CPU_ABIV2               ((uint64_t)0x1 << 1)
#define CPU_510                 ((uint64_t)0x1 << 2)
#define CPU_520                 ((uint64_t)0x1 << 3)
#define CPU_610                 ((uint64_t)0x1 << 4)
#define CPU_620                 ((uint64_t)0x1 << 5)
#define CPU_E801                ((uint64_t)0x1 << 6)
#define CPU_E802                ((uint64_t)0x1 << 7)
#define CPU_E803                ((uint64_t)0x1 << 8)
#define CPU_C807                ((uint64_t)0x1 << 10)
#define CPU_C810                ((uint64_t)0x1 << 11)
#define CPU_C860                ((uint64_t)0x1 << 13)
#define CSKY_MMU                ((uint64_t)0x1 << 16)
#define CSKY_MPU                ((uint64_t)0x1 << 17)
#define ABIV1_DSP               ((uint64_t)0x1 << 18)
#define ABIV1_FPU               ((uint64_t)0x1 << 19)
#define ABIV2_TEE               ((uint64_t)0x1 << 20)
#define ABIV2_DSP               ((uint64_t)0x1 << 21)
#define ABIV2_FPU               ((uint64_t)0x1 << 22)
#define ABIV2_FPU_SINGLE        ((uint64_t)0x1 << 23)
#define ABIV2_JAVA              ((uint64_t)0x1 << 26)
#define ABIV2_VDSP64            ((uint64_t)0x1 << 27)
#define ABIV2_VDSP128           ((uint64_t)0x1 << 28)
#define ABIV2_ELRW              ((uint64_t)0x1 << 29)
#define UNALIGNED_ACCESS        ((uint64_t)0x1 << 30)
#define ABIV2_DSP2              ((uint64_t)0x1 << 31)
#define ABIV2_VDSP2             ((uint64_t)0x1 << 32)
#define ABIV2_FPU3              ((uint64_t)0x1 << 36)
#define DENORMALIZE             ((uint64_t)0x1 << 37)


#define CPU_E804                (CPU_E803)
#define CPU_I805                (CPU_E803 | ABIV2_VDSP2 | ABIV2_DSP2)

#define ABIV2_FLOAT_S           (ABIV2_FPU_SINGLE | ABIV2_FPU | ABIV2_FPU3)
#define ABIV2_FLOAT_D           (ABIV2_FPU | ABIV2_FPU3)
#define ABIV2_FLOAT_ALL         (ABIV2_FPU | ABIV2_FPU3)

/* get bit from psr */
#define PSR_CPID_MASK   0x0f000000
#define PSR_CPID(psr)   (((psr) & PSR_CPID_MASK) >> 24)
#define PSR_IE_MASK     0x00000040
#define PSR_IE(psr)     (((psr) & PSR_IE_MASK) >> 6)
#define PSR_EE_MASK     0x00000100
#define PSR_EE(psr)     (((psr) & PSR_EE_MASK) >> 8)
#define PSR_FE_MASK     0x00000010
#define PSR_FE(psr)     (((psr) & PSR_FE_MASK) >> 4)
#define PSR_S_MASK      0x80000000
#define PSR_S(psr)      (((psr) & PSR_S_MASK) >> 31)
#define PSR_BM_MASK     0x00000400
#define PSR_BM(psr)     (((psr) & PSR_BM_MASK) >> 10)
#define PSR_C_MASK      0x00000001
#define PSR_C(psr)      (((psr) & PSR_C_MASK) >> 0)
#define PSR_TM_MASK     0x0000c000
#define PSR_TM(psr)     (((psr) & PSR_TM_MASK) >> 14)
#define PSR_TP_MASK     0x00002000
#define PSR_TP(psr)     (((psr) & PSR_TP_MASK) >> 13)
#define PSR_VEC_MASK    0x00ff0000
#define PSR_VEC(psr)    (((psr) & PSR_VEC_MASK) >> 16)
/* get bit from psr when has tee */
#define PSR_T_MASK      0x40000000
#define PSR_T(psr)      (((psr) & PSR_T_MASK) >> 30)
#define PSR_SP_MASK     0x20000000
#define PSR_SP(psr)     (((psr) & PSR_SP_MASK) >> 29)
#define PSR_HS_MASK     0x10000000
#define PSR_HS(psr)     (((psr) & PSR_HS_MASK) >> 28)
#define PSR_SC_MASK     0x08000000
#define PSR_SC(psr)     (((psr) & PSR_SC_MASK) >> 27)
#define PSR_SD_MASK     0x04000000
#define PSR_SD(psr)     (((psr) & PSR_SD_MASK) >> 26)
#define PSR_ST_MASK     0x02000000
#define PSR_ST(psr)     (((psr) & PSR_ST_MASK) >> 25)

/* MMU MCIR bit MASK */
#define CSKY_MCIR_TLBP_SHIFT        31
#define CSKY_MCIR_TLBP_MASK         (1 << CSKY_MCIR_TLBP_SHIFT)
#define CSKY_MCIR_TLBR_SHIFT        30
#define CSKY_MCIR_TLBR_MASK         (1 << CSKY_MCIR_TLBR_SHIFT)
#define CSKY_MCIR_TLBWI_SHIFT       29
#define CSKY_MCIR_TLBWI_MASK        (1 << CSKY_MCIR_TLBWI_SHIFT)
#define CSKY_MCIR_TLBWR_SHIFT       28
#define CSKY_MCIR_TLBWR_MASK        (1 << CSKY_MCIR_TLBWR_SHIFT)
#define CSKY_MCIR_TLBINV_SHIFT      27
#define CSKY_MCIR_TLBINV_MASK       (1 << CSKY_MCIR_TLBINV_SHIFT)
#define CSKY_MCIR_TLBINV_ALL_SHIFT  26
#define CSKY_MCIR_TLBINV_ALL_MASK   (1 << CSKY_MCIR_TLBINV_ALL_SHIFT)
#define CSKY_MCIR_TLBINV_IDX_SHIFT  25
#define CSKY_MCIR_TLBINV_IDX_MASK   (1 << CSKY_MCIR_TLBINV_IDX_SHIFT)
#define CSKY_MCIR_TTLBINV_ALL_SHIFT 24
#define CSKY_MCIR_TTLBINV_ALL_MASK  (1 << CSKY_MCIR_TTLBINV_ALL_SHIFT)


#define CSKY_VPN_MASK            0xfffff000
#define CSKY_VPN_SHIFT           12
#define CSKY_ASID_SHIFT          0

/* C860 tlb instruction's reg mask */
#define CSKY_MP_ASID_MASK        0xfff

/* CK610 CK807 CK810 instruction's reg mask */
#define CSKY_ASID_MASK           0xff

static inline CPUCSKYState *csky_cpu_get_env(CPUState *obj)
{
    return &((CSKYCPU *)obj)->env;
}

static inline bool csky_has_feature(CPUCSKYState *env, uint64_t feature)
{
    if (env->features & feature) {
        return true;
    } else {
        return false;
    }
}
static inline uint32_t csky_env_get_asid(CPUCSKYState *env)
{
    uint32_t asid;
    if (env->features & CPU_C860) {
        asid = env->mmu.meh & CSKY_MP_ASID_MASK;
    } else {
        asid = env->mmu.meh & CSKY_ASID_MASK;
    }
    return asid;

}

#define ENV_GET_MMU(e) csky_has_feature(e, CSKY_MMU)
#define ENV_GET_ASID(e) csky_env_get_asid(e)

static inline int csky_env_mmu_index(CPUCSKYState *env, bool ifetch)
{
    return PSR_S(env->cp0.psr);
}

static inline void cpu_get_tb_cpu_state(CPUCSKYState *env, vaddr *pc,
                                        uint64_t *cs_base, uint32_t *pflags)
{
    uint32_t mask;
    CPUState *cpu = env_cpu(env);
    *pc = env->pc;
    *cs_base = 0;

    if (env->features & CPU_C860) {
        mask = CSKY_MP_ASID_MASK;
    } else {
        mask = CSKY_ASID_MASK;
    }
#if defined(TARGET_CSKYV2)
    *pflags = (env->psr_s << CSKY_TBFLAG_PSR_S_SHIFT)
        | (env->psr_bm << CSKY_TBFLAG_PSR_BM_SHIFT)
        | (env->sce_condexec_bits << CSKY_TBFLAG_SCE_CONDEXEC_SHIFT)
        | ((env->mmu.meh & mask) << CSKY_TBFLAG_ASID_SHIFT)
        | (env->psr_tm << CSKY_TBFLAG_PSR_TM_SHIFT)
        | (env->psr_t << CSKY_TBFLAG_PSR_T_SHIFT)
        | (env->idly4_counter << CSKY_TBFLAG_IDLY4_SHIFT)
        | (cpu->singlestep_enabled << CSKY_TBFLAG_SINGLESTEP_SHIFT);
#else
    *pflags = (PSR_CPID(env->cp0.psr) << CSKY_TBFLAG_CPID_SHIFT)
        | (env->psr_s << CSKY_TBFLAG_PSR_S_SHIFT)
        | ((env->mmu.meh & mask) << CSKY_TBFLAG_ASID_SHIFT)
        | (env->psr_tm << CSKY_TBFLAG_PSR_TM_SHIFT)
        | (env->idly4_counter << CSKY_TBFLAG_IDLY4_SHIFT)
        | (cpu->singlestep_enabled << CSKY_TBFLAG_SINGLESTEP_SHIFT);
#endif
}

#endif /* CSKY_CPU_H */
