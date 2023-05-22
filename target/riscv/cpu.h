/*
 * QEMU RISC-V CPU
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

#ifndef RISCV_CPU_H
#define RISCV_CPU_H

#include "hw/core/cpu.h"
#include "hw/registerfields.h"
#include "exec/cpu-defs.h"
#include "fpu/softfloat-types.h"
#include "qom/object.h"
#include "cpu_bits.h"
#include "exec/pctrace.h"

#define TCG_GUEST_DEFAULT_MO 0

#define TYPE_RISCV_CPU "riscv-cpu"

#define RISCV_CPU_TYPE_SUFFIX "-" TYPE_RISCV_CPU
#define RISCV_CPU_TYPE_NAME(name) (name RISCV_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_RISCV_CPU
#define CPU_INTERRUPT_CLIC CPU_INTERRUPT_TGT_EXT_0

#define TYPE_RISCV_CPU_ANY              RISCV_CPU_TYPE_NAME("any")
#define TYPE_RISCV_CPU_BASE32           RISCV_CPU_TYPE_NAME("rv32")
#define TYPE_RISCV_CPU_BASE64           RISCV_CPU_TYPE_NAME("rv64")
#define TYPE_RISCV_CPU_IBEX             RISCV_CPU_TYPE_NAME("lowrisc-ibex")
#define TYPE_RISCV_CPU_SHAKTI_C         RISCV_CPU_TYPE_NAME("shakti-c")
#define TYPE_RISCV_CPU_SIFIVE_E31       RISCV_CPU_TYPE_NAME("sifive-e31")
#define TYPE_RISCV_CPU_SIFIVE_E34       RISCV_CPU_TYPE_NAME("sifive-e34")
#define TYPE_RISCV_CPU_SIFIVE_E51       RISCV_CPU_TYPE_NAME("sifive-e51")
#define TYPE_RISCV_CPU_SIFIVE_U34       RISCV_CPU_TYPE_NAME("sifive-u34")
#define TYPE_RISCV_CPU_SIFIVE_U54       RISCV_CPU_TYPE_NAME("sifive-u54")
#define TYPE_RISCV_CPU_E902             RISCV_CPU_TYPE_NAME("e902")
#define TYPE_RISCV_CPU_E902T            RISCV_CPU_TYPE_NAME("e902t")
#define TYPE_RISCV_CPU_E902M            RISCV_CPU_TYPE_NAME("e902m")
#define TYPE_RISCV_CPU_E902MT           RISCV_CPU_TYPE_NAME("e902mt")
#define TYPE_RISCV_CPU_E906             RISCV_CPU_TYPE_NAME("e906")
#define TYPE_RISCV_CPU_E906F            RISCV_CPU_TYPE_NAME("e906f")
#define TYPE_RISCV_CPU_E906FD           RISCV_CPU_TYPE_NAME("e906fd")
#define TYPE_RISCV_CPU_E906FDP          RISCV_CPU_TYPE_NAME("e906fdp")
#define TYPE_RISCV_CPU_E906P            RISCV_CPU_TYPE_NAME("e906p")
#define TYPE_RISCV_CPU_E906FP           RISCV_CPU_TYPE_NAME("e906fp")
#define TYPE_RISCV_CPU_E907             RISCV_CPU_TYPE_NAME("e907")
#define TYPE_RISCV_CPU_E907F            RISCV_CPU_TYPE_NAME("e907f")
#define TYPE_RISCV_CPU_E907FD           RISCV_CPU_TYPE_NAME("e907fd")
#define TYPE_RISCV_CPU_E907FDP          RISCV_CPU_TYPE_NAME("e907fdp")
#define TYPE_RISCV_CPU_E907P            RISCV_CPU_TYPE_NAME("e907p")
#define TYPE_RISCV_CPU_E907FP           RISCV_CPU_TYPE_NAME("e907fp")
#define TYPE_RISCV_CPU_C910             RISCV_CPU_TYPE_NAME("c910")
#define TYPE_RISCV_CPU_C910V            RISCV_CPU_TYPE_NAME("c910v")
#define TYPE_RISCV_CPU_C920             RISCV_CPU_TYPE_NAME("c920")
#define TYPE_RISCV_CPU_C906             RISCV_CPU_TYPE_NAME("c906")
#define TYPE_RISCV_CPU_C906FD           RISCV_CPU_TYPE_NAME("c906fd")
#define TYPE_RISCV_CPU_C906FDV          RISCV_CPU_TYPE_NAME("c906fdv")
#define TYPE_RISCV_CPU_C908             RISCV_CPU_TYPE_NAME("c908")
#define TYPE_RISCV_CPU_C908V            RISCV_CPU_TYPE_NAME("c908v")
#define TYPE_RISCV_CPU_C960             RISCV_CPU_TYPE_NAME("c960")
#define TYPE_RISCV_CPU_R910             RISCV_CPU_TYPE_NAME("r910")
#define TYPE_RISCV_CPU_R920             RISCV_CPU_TYPE_NAME("r920")

#if defined(TARGET_RISCV32)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE32
#elif defined(TARGET_RISCV64)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE64
#endif

#define RV(x) ((target_ulong)1 << (x - 'A'))

#define RVI RV('I')
#define RVE RV('E') /* E and I are mutually exclusive */
#define RVM RV('M')
#define RVA RV('A')
#define RVF RV('F')
#define RVD RV('D')
#define RVV RV('V')
#define RVP RV('P')
#define RVC RV('C')
#define RVS RV('S')
#define RVU RV('U')
#define RVH RV('H')

/* ['A' + 26, (TARGET_LONG_BITS - 2)) are used for non standard extensions */
#define RVXTHEAD RV('A' + 26)

/* S extension denotes that Supervisor mode exists, however it is possible
   to have a core that support S mode but does not have an MMU and there
   is currently no bit in misa to indicate whether an MMU exists or not
   so a cpu features bitfield is required, likewise for optional PMP support */
enum {
    RISCV_FEATURE_MMU,
    RISCV_FEATURE_PMP,
    RISCV_FEATURE_EPMP,
    RISCV_FEATURE_MISA
};

/* Privileged specification version */
enum {
    PRIV_VERSION_1_10_0 = 0,
    PRIV_VERSION_1_11_0,
    PRIV_VERSION_1_12_0,
};

#define VEXT_VERSION_0_07_1 0x00000701
#define VEXT_VERSION_1_00_0 0x00010000
#define PEXT_VERSION_0_09_4 0x00000904

enum {
    TRANSLATE_SUCCESS,
    TRANSLATE_FAIL,
    TRANSLATE_PMP_FAIL,
    TRANSLATE_G_STAGE_FAIL
};

#define MMU_USER_IDX 3

#define MAX_RISCV_PMPS (64)

/* MMU MCIR bit MASK */
#define CSKY_SMCIR_TLBP_SHIFT        31
#define CSKY_SMCIR_TLBP_MASK         (1 << CSKY_SMCIR_TLBP_SHIFT)
#define CSKY_SMCIR_TLBR_SHIFT        30
#define CSKY_SMCIR_TLBR_MASK         (1 << CSKY_SMCIR_TLBR_SHIFT)
#define CSKY_SMCIR_TLBWI_SHIFT       29
#define CSKY_SMCIR_TLBWI_MASK        (1 << CSKY_SMCIR_TLBWI_SHIFT)
#define CSKY_SMCIR_TLBWR_SHIFT       28
#define CSKY_SMCIR_TLBWR_MASK        (1 << CSKY_SMCIR_TLBWR_SHIFT)
#define CSKY_SMCIR_TLBINV_SHIFT      27
#define CSKY_SMCIR_TLBINV_MASK       (1 << CSKY_SMCIR_TLBINV_SHIFT)
#define CSKY_SMCIR_TLBINV_ALL_SHIFT  26
#define CSKY_SMCIR_TLBINV_ALL_MASK   (1 << CSKY_SMCIR_TLBINV_ALL_SHIFT)
#define CSKY_SMCIR_TLBINV_IDX_SHIFT  25
#define CSKY_SMCIR_TLBINV_IDX_MASK   (1 << CSKY_SMCIR_TLBINV_IDX_SHIFT)
#define CSKY_SMCIR_TTLBINV_ALL_SHIFT 24
#define CSKY_SMCIR_TTLBINV_ALL_MASK  (1 << CSKY_SMCIR_TTLBINV_ALL_SHIFT)

typedef struct CPURISCVState CPURISCVState;

#if !defined(CONFIG_USER_ONLY)
#include "pmp.h"
#endif

#define RV_VLEN_MAX 1024
FIELD(VTYPE_7, VLMUL, 0, 2)
FIELD(VTYPE_7, VSEW, 2, 3)
FIELD(VTYPE_7, VEDIV, 5, 2)
FIELD(VTYPE_7, RESERVED, 7, sizeof(target_ulong) * 8 - 9)
FIELD(VTYPE_7, VILL, sizeof(target_ulong) * 8 - 1, 1)
FIELD(VTYPE_7, VILL_OLEN32, 31, 1)
FIELD(VTYPE_7, RESERVED_OLEN32, 7, 23)

FIELD(VTYPE, VLMUL, 0, 3)
FIELD(VTYPE, VSEW, 3, 3)
FIELD(VTYPE, VTA, 6, 1)
FIELD(VTYPE, VMA, 7, 1)
FIELD(VTYPE, VEDIV, 8, 2)
FIELD(VTYPE, RESERVED, 10, sizeof(target_ulong) * 8 - 11)
FIELD(VTYPE, VILL, sizeof(target_ulong) * 8 - 1, 1)
FIELD(VTYPE, VILL_OLEN32, 31, 1)
FIELD(VTYPE, RESERVED_OLEN32, 7, 24)

FIELD(MSIZE, SIZEM, 0, 8)
FIELD(MSIZE, SIZEN, 8, 8)
FIELD(MSIZE, SIZEK, 16, 16)

#define RV_RLEN_MAX 4096
#define RV_MACC_LEN 32

/* See the commentary above the TBFLAG field definitions.  */
typedef struct CPURISCVTBFlags {
    uint32_t flags;
    target_ulong flags2;
} CPURISCVTBFlags;

struct CPURISCVState {
    target_ulong gpr[32];
    uint64_t fpr[32]; /* assume both F and D extensions */

    /* vector coprocessor state. */
    uint64_t vreg[32 * RV_VLEN_MAX / 64] QEMU_ALIGNED(16);
    target_ulong vxrm;
    target_ulong vxsat;
    target_ulong vl;
    target_ulong vstart;
    target_ulong vtype;

    /* matrix state */
    target_ulong mrstart;
    target_ulong mcsr;
    target_ulong mxsat;
    target_ulong mxrm;
    target_ulong xmisa;

    target_ulong pc;
    target_ulong load_res;
    target_ulong load_val;

    target_ulong frm;

    target_ulong badaddr;
    target_ulong guest_phys_fault_addr;

    target_ulong priv_ver;
    target_ulong bext_ver;
    target_ulong vext_ver;
    target_ulong pext_ver;

    /* RISCVMXL, but uint32_t for vmstate migration */
    uint32_t misa_mxl;      /* current mxl */
    uint32_t misa_mxl_max;  /* max mxl for this cpu */
    uint32_t misa_ext;      /* current extensions */
    uint32_t misa_ext_mask; /* max ext for this cpu */

    uint32_t features;

#ifdef CONFIG_USER_ONLY
    uint32_t elf_flags;
#endif

#ifndef CONFIG_USER_ONLY
    target_ulong priv;
    target_ulong excp_vld;
    /* This contains QEMU specific information about the virt state. */
    target_ulong virt;
    target_ulong resetvec;

    target_ulong mhartid;
    /*
     * For RV32 this is 32-bit mstatus and 32-bit mstatush.
     * For RV64 this is a 64-bit mstatus.
     */
    uint64_t mstatus;

    target_ulong mip;

    uint32_t miclaim;
    uint32_t mintstatus; /* clic-spec */
    target_ulong mintthresh; /* clic-spec */
    target_ulong mclicbase; /* clic-spec */

    target_ulong mie;
    target_ulong mideleg;

    target_ulong satp;   /* since: priv-1.10.0 */
    target_ulong stval;
    target_ulong medeleg;

    target_ulong stvec;
    target_ulong stvt; /* clic-spec */
    target_ulong sepc;
    target_ulong scause;
    target_ulong sintthresh; /* clic-spec */

    target_ulong mtvec;
    target_ulong mtvt; /* clic-spec */
    target_ulong mepc;
    target_ulong mcause;
    target_ulong mtval;  /* since: priv-1.10.0 */

    /* Hypervisor CSRs */
    target_ulong hstatus;
    target_ulong hedeleg;
    target_ulong hideleg;
    target_ulong hcounteren;
    target_ulong htval;
    target_ulong htinst;
    target_ulong hgatp;
    uint64_t htimedelta;

    /* Virtual CSRs */
    /*
     * For RV32 this is 32-bit vsstatus and 32-bit vsstatush.
     * For RV64 this is a 64-bit vsstatus.
     */
    uint64_t vsstatus;
    target_ulong vstvec;
    target_ulong vsscratch;
    target_ulong vsepc;
    target_ulong vscause;
    target_ulong vstval;
    target_ulong vsatp;

    target_ulong mtval2;
    target_ulong mtinst;

    /* HS Backup CSRs */
    target_ulong stvec_hs;
    target_ulong sscratch_hs;
    target_ulong sepc_hs;
    target_ulong scause_hs;
    target_ulong stval_hs;
    target_ulong satp_hs;
    uint64_t mstatus_hs;

    /* Signals whether the current exception occurred with two-stage address
       translation active. */
    bool two_stage_lookup;

    target_ulong scounteren;
    target_ulong mcounteren;

    target_ulong sscratch;
    target_ulong mscratch;

    /* temporary htif regs */
    uint64_t mfromhost;
    uint64_t mtohost;
    uint64_t timecmp;
    uint64_t stimecmp;

    /* physical memory protection */
    pmp_table_t pmp_state;
    target_ulong mseccfg;

    /* machine specific rdtime callback */
    uint64_t (*rdtime_fn)(uint32_t);
    uint32_t rdtime_fn_arg;

    /* True if in debugger mode.  */
    bool debugger;

    /* csky c910 extends */
    uint64_t mxstatus;
    uint64_t mrmr;
    uint64_t mrvbr;
    uint64_t cpuid;
    uint64_t sxstatus;
    uint64_t smcir;
    uint64_t smir;
    uint64_t smlo0;
    uint64_t smeh;
    /* csky e906 extends */
    uint64_t mexstatus;

    /* tcm */
    MemoryRegion *dtcm;
    MemoryRegion *itcm;
    target_ulong mdtcmcr;
    target_ulong mitcmcr;

    CPURISCVState * next_cpu;
    bool     in_reset;

    /* CSRs for execution enviornment configuration */
    uint64_t menvcfg;
    target_ulong senvcfg;
    uint64_t henvcfg;
#endif
    bool     bf16;
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
    uint64_t elf_start;
    target_ulong last_pc;
    float_status fp_status;

    uint64_t mreg[8 * RV_RLEN_MAX / RV_MACC_LEN  * RV_RLEN_MAX / 64] QEMU_ALIGNED(16);
    target_ulong sizem;
    target_ulong sizen;
    target_ulong sizek;
    /* Fields from here on are preserved across CPU reset. */
    QEMUTimer *timer; /* Internal timer */
    QEMUTimer *stimer; /* Internal timer */
    void *clic;       /* clic interrupt controller */
    uint32_t exccode; /* clic irq encode */

    uint32_t pctrace;
    uint32_t tb_trace;
#ifdef TARGET_RISCV64
    uint64_t jcount_enable;
    uint64_t jcount_start;
    uint64_t jcount_end;
#else
    uint32_t jcount_enable;
    uint32_t jcount_start;
    uint32_t jcount_end;
#endif
    struct csky_trace_info *trace_info;
    uint32_t trace_index;
    target_ulong overflow_flag;
};

OBJECT_DECLARE_TYPE(RISCVCPU, RISCVCPUClass,
                    RISCV_CPU)

/**
 * RISCVCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A RISCV CPU model.
 */
struct RISCVCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
    DeviceReset parent_reset;
    uint64_t mrvbr;
    uint64_t mrmr;
};

/**
 * RISCVCPU:
 * @env: #CPURISCVState
 *
 * A RISCV CPU.
 */
struct RISCVCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/
    CPUNegativeOffsetState neg;
    CPURISCVState env;

    char *dyn_csr_xml;
    char *dyn_vreg_xml;
    char *dyn_mreg_xml;

    /* Configuration Settings */
    struct {
        bool ext_i;
        bool ext_e;
        bool ext_g;
        bool ext_m;
        bool ext_a;
        bool ext_f;
        bool ext_d;
        bool ext_c;
        bool ext_s;
        bool ext_u;
        bool ext_h;
        bool ext_v;
        bool ext_p;
        bool ext_zba;
        bool ext_zbb;
        bool ext_zbc;
        bool ext_zbs;
        bool ext_counters;
        bool ext_ifencei;
        bool ext_icsr;
        bool ext_icbom;
        bool ext_icboz;
        bool ext_svinval;
        bool ext_svnapot;
        bool ext_svpbmt;
        bool ext_psfoperand;
        bool ext_thead;
        bool ext_matrix;
        bool ext_sstc;

        char *priv_spec;
        char *user_spec;
        char *bext_spec;
        char *vext_spec;
        char *pext_spec;
        uint16_t vlen;
        uint16_t mrowlen;
        uint16_t elen;
        uint16_t cbozlen;
        uint16_t datapath;
        bool mmu;
        bool pmp;
        bool epmp;
        bool fpu;
        uint64_t resetvec;
    } cfg;
};

static inline int riscv_has_ext(CPURISCVState *env, target_ulong ext)
{
    return (env->misa_ext & ext) != 0;
}

static inline bool riscv_feature(CPURISCVState *env, int feature)
{
    return env->features & (1ULL << feature);
}


#include "cpu_user.h"

extern const char * const riscv_int_regnames[];
extern const char * const riscv_fpr_regnames[];

#define ENV_GET_CPU(e) CPU(riscv_env_get_cpu(e))
#define ENV_OFFSET offsetof(RISCVCPU, env)

#define ENV_GET_MMU(e) riscv_feature(e, RISCV_FEATURE_MMU)

#ifdef CONFIG_USER_ONLY
    #define ENV_GET_ASID(e) 0
#else
    #define ENV_GET_ASID(e) env->satp && SATP32_ASID
#endif

const char *riscv_cpu_get_trap_name(target_ulong cause, bool async, bool clic);
void riscv_cpu_do_interrupt(CPUState *cpu);
int riscv_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, void *opaque);
int riscv_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, void *opaque);
int riscv_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int riscv_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
bool riscv_cpu_fp_enabled(CPURISCVState *env);
bool riscv_cpu_vector_enabled(CPURISCVState *env);
bool riscv_cpu_matrix_enabled(CPURISCVState *env);
bool riscv_cpu_virt_enabled(CPURISCVState *env);
void riscv_cpu_set_virt_enabled(CPURISCVState *env, bool enable);
bool riscv_cpu_force_hs_excep_enabled(CPURISCVState *env);
void riscv_cpu_set_force_hs_excep(CPURISCVState *env, bool enable);
bool riscv_cpu_two_stage_lookup(int mmu_idx);
int riscv_cpu_mmu_index(CPURISCVState *env, bool ifetch);
hwaddr riscv_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
void  riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                    MMUAccessType access_type, int mmu_idx,
                                    uintptr_t retaddr);
bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
void riscv_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr);
char *riscv_isa_string(RISCVCPU *cpu);
void riscv_cpu_list(void);

#define cpu_signal_handler riscv_cpu_signal_handler
#define cpu_list riscv_cpu_list
#define cpu_mmu_index riscv_cpu_mmu_index

#ifndef CONFIG_USER_ONLY
void riscv_cpu_swap_hypervisor_regs(CPURISCVState *env);
int riscv_cpu_claim_interrupts(RISCVCPU *cpu, uint32_t interrupts);
uint32_t riscv_cpu_update_mip(RISCVCPU *cpu, uint32_t mask, uint32_t value);
#define BOOL_TO_MASK(x) (-!!(x)) /* helper for riscv_cpu_update_mip value */
void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(uint32_t),
                             uint32_t arg);
#endif
void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv);

void riscv_translate_init(void);
int riscv_cpu_signal_handler(int host_signum, void *pinfo, void *puc);
void QEMU_NORETURN riscv_raise_exception(CPURISCVState *env,
                                         uint32_t exception, uintptr_t pc);

target_ulong riscv_cpu_get_fflags(CPURISCVState *env);
void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong);

#define TB_FLAGS_ANY_PRIV_MMU_MASK                3
#define TB_FLAGS_ANY_PRIV_HYP_ACCESS_MASK   (1 << 2)
#define TB_FLAGS_ANY_MSTATUS_FS MSTATUS_FS
#define TB_FLAGS_ANY_MSTATUS_VS MSTATUS_VS

typedef CPURISCVState CPUArchState;
typedef RISCVCPU ArchCPU;
#include "exec/cpu-all.h"

FIELD(TB_FLAGS_ANY, MEM_IDX, 0, 3)
FIELD(TB_FLAGS_ANY, LMUL, 3, 3)
FIELD(TB_FLAGS_ANY, SEW, 6, 3)
/* Skip MSTATUS_VS (0x600) bits */
FIELD(TB_FLAGS_ANY, VL_EQ_VLMAX, 11, 1)
FIELD(TB_FLAGS_ANY, VILL, 12, 1)
/* Skip MSTATUS_FS (0x6000) bits */
/* Is a Hypervisor instruction load/store allowed? */
FIELD(TB_FLAGS_ANY, HLSX, 15, 1)
/* The combination of MXL/SXL/UXL that applies to the current cpu mode. */
FIELD(TB_FLAGS_ANY, XL, 16, 2)

FIELD(TB_FLAGS_THEAD, PWI32, 0, 1)
FIELD(TB_FLAGS_THEAD, PWI64, 1, 1)
FIELD(TB_FLAGS_THEAD, I4I32, 2, 1)
FIELD(TB_FLAGS_THEAD, I8I32, 3, 1)
FIELD(TB_FLAGS_THEAD, I16I64, 4, 1)
FIELD(TB_FLAGS_THEAD, F16F16, 5, 1)
FIELD(TB_FLAGS_THEAD, F32F32, 6, 1)
FIELD(TB_FLAGS_THEAD, F64F64, 7, 1)
FIELD(TB_FLAGS_THEAD, MS, 8, 2)
FIELD(TB_FLAGS_THEAD, MILL, 10, 1)
FIELD(TB_FLAGS_THEAD, NILL, 11, 1)
FIELD(TB_FLAGS_THEAD, KILL, 12, 1)
FIELD(TB_FLAGS_THEAD, NPILL, 13, 1)
FIELD(TB_FLAGS_THEAD, BF16, 20, 1)

/*
 * Helpers for using the above.
 */
#define DP_TBFLAGS_ANY(DST, WHICH, VAL) \
    (DST.flags = FIELD_DP32(DST.flags, TB_FLAGS_ANY, WHICH, VAL))
#define DP_TBFLAGS_THEAD(DST, WHICH, VAL) \
    (DST.flags2 = FIELD_DP32(DST.flags2, TB_FLAGS_THEAD, WHICH, VAL))

#define EX_TBFLAGS_ANY(IN, WHICH)   FIELD_EX32(IN.flags, TB_FLAGS_ANY, WHICH)
#define EX_TBFLAGS_THEAD(IN, WHICH) FIELD_EX32(IN.flags2, TB_FLAGS_THEAD, WHICH)

#ifdef TARGET_RISCV32
#define riscv_cpu_mxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_mxl(CPURISCVState *env)
{
    return env->misa_mxl;
}
#endif

#ifdef TARGET_RISCV32
#define riscv_cpu_sxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_sxl(CPURISCVState *env)
{
#ifdef CONFIG_USER_ONLY
    return env->misa_mxl;
#else
    return get_field(env->mstatus, MSTATUS64_SXL);
#endif
}
#endif

/*
 * A simplification for VLMAX
 * = (1 << LMUL) * VLEN / (8 * (1 << SEW))
 * = (VLEN << LMUL) / (8 << SEW)
 * = (VLEN << LMUL) >> (SEW + 3)
 * = VLEN >> (SEW + 3 - LMUL)
 */
static inline uint32_t vext_get_vlmax_7(RISCVCPU *cpu, target_ulong vtype)
{
    uint8_t sew = 0, lmul = 0;

    if (cpu->env.vext_ver == VEXT_VERSION_0_07_1) {
        sew = FIELD_EX64(vtype, VTYPE_7, VSEW);
        lmul = FIELD_EX64(vtype, VTYPE_7, VLMUL);
    }
    return cpu->cfg.vlen >> (sew + 3 - lmul);
}

/*
 * Encode LMUL to lmul as follows:
 *     LMUL    vlmul    lmul
 *      1       000       0
 *      2       001       1
 *      4       010       2
 *      8       011       3
 *      -       100       -
 *     1/8      101      -3
 *     1/4      110      -2
 *     1/2      111      -1
 *
 * then, we can calculate VLMAX = vlen >> (vsew + 3 - lmul)
 * e.g. vlen = 256 bits, SEW = 16, LMUL = 1/8
 *      => VLMAX = vlen >> (1 + 3 - (-3))
 *               = 256 >> 7
 *               = 2
 */
static inline uint32_t vext_get_vlmax(RISCVCPU *cpu, target_ulong vtype)
{
    uint8_t sew = FIELD_EX64(vtype, VTYPE, VSEW);
    int8_t lmul = sextract32(FIELD_EX64(vtype, VTYPE, VLMUL), 0, 3);
    return cpu->cfg.vlen >> (sew + 3 - lmul);
}

RISCVMXL cpu_get_xl(CPURISCVState *env);
void cpu_get_tb_cpu_state(CPURISCVState *env, target_ulong *pc,
                          target_ulong *cs_base, uint32_t *pflags);

RISCVException riscv_csrrw(CPURISCVState *env, int csrno,
                           target_ulong *ret_value,
                           target_ulong new_value, target_ulong write_mask);
RISCVException riscv_csrrw_debug(CPURISCVState *env, int csrno,
                                 target_ulong *ret_value,
                                 target_ulong new_value,
                                 target_ulong write_mask);

static inline void riscv_csr_write(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    riscv_csrrw(env, csrno, NULL, val, MAKE_64BIT_MASK(0, TARGET_LONG_BITS));
}

static inline target_ulong riscv_csr_read(CPURISCVState *env, int csrno)
{
    target_ulong val = 0;
    riscv_csrrw(env, csrno, &val, 0, 0);
    return val;
}

typedef RISCVException (*riscv_csr_predicate_fn)(CPURISCVState *env,
                                                 int csrno);
typedef RISCVException (*riscv_csr_read_fn)(CPURISCVState *env, int csrno,
                                            target_ulong *ret_value);
typedef RISCVException (*riscv_csr_write_fn)(CPURISCVState *env, int csrno,
                                             target_ulong new_value);
typedef RISCVException (*riscv_csr_op_fn)(CPURISCVState *env, int csrno,
                                          target_ulong *ret_value,
                                          target_ulong new_value,
                                          target_ulong write_mask);

typedef struct {
    const char *name;
    riscv_csr_predicate_fn predicate;
    riscv_csr_read_fn read;
    riscv_csr_write_fn write;
    riscv_csr_op_fn op;
    /* The default priv spec version should be PRIV_VERSION_1_10_0 (i.e 0) */
    uint32_t min_priv_ver;
} riscv_csr_operations;

/* CSR function table constants */
enum {
    CSR_TABLE_SIZE = 0x1000
};

/* CSR function table */
extern riscv_csr_operations csr_ops[CSR_TABLE_SIZE];

void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops);
void riscv_set_csr_ops(int csrno, riscv_csr_operations *ops);

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs);

#endif /* RISCV_CPU_H */
