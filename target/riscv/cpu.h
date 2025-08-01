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
#include "hw/qdev-properties.h"
#include "exec/cpu-defs.h"
#include "exec/gdbstub.h"
#include "qemu/cpu-float.h"
#include "qom/object.h"
#include "qemu/int128.h"
#include "cpu_bits.h"
#include "cpu_cfg.h"
#include "qapi/qapi-types-common.h"
#include "cpu-qom.h"
#include "exec/pctrace.h"
#include "dsa.h"
#include "dsa_float.h"

typedef struct CPUArchState CPURISCVState;

#define CPU_RESOLVING_TYPE TYPE_RISCV_CPU
#define CPU_INTERRUPT_CLIC CPU_INTERRUPT_TGT_EXT_0

#if defined(TARGET_RISCV32)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE32
#elif defined(TARGET_RISCV64)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE64
#endif

#define TCG_GUEST_DEFAULT_MO 0

/*
 * RISC-V-specific extra insn start words:
 * 1: Original instruction opcode
 */
#define TARGET_INSN_START_EXTRA_WORDS 1

#define RV(x) ((target_ulong)1 << (x - 'A'))

/*
 * Update misa_bits[], misa_ext_info_arr[] and misa_ext_cfgs[]
 * when adding new MISA bits here.
 */
#define RVI RV('I')
#define RVE RV('E') /* E and I are mutually exclusive */
#define RVM RV('M')
#define RVA RV('A')
#define RVF RV('F')
#define RVD RV('D')
#define RVV RV('V')
#define RVC RV('C')
#define RVS RV('S')
#define RVU RV('U')
#define RVH RV('H')
#define RVG RV('G')
#define RVB RV('B')
#define RVP RV('P')

extern const uint32_t misa_bits[];
const char *riscv_get_misa_ext_name(uint32_t bit);
const char *riscv_get_misa_ext_description(uint32_t bit);

#define CPU_CFG_OFFSET(_prop) offsetof(struct RISCVCPUConfig, _prop)

typedef struct riscv_cpu_profile {
    struct riscv_cpu_profile *parent;
    const char *name;
    uint32_t misa_ext;
    bool enabled;
    bool user_set;
    int priv_spec;
    int satp_mode;
    const int32_t ext_offsets[];
} RISCVCPUProfile;

#define RISCV_PROFILE_EXT_LIST_END -1
#define RISCV_PROFILE_ATTR_UNUSED -1

extern RISCVCPUProfile *riscv_profiles[];

/* Privileged specification version */
#define PRIV_VER_1_10_0_STR "v1.10.0"
#define PRIV_VER_1_11_0_STR "v1.11.0"
#define PRIV_VER_1_12_0_STR "v1.12.0"
#define PRIV_VER_1_13_0_STR "v1.13.0"
enum {
    PRIV_VERSION_1_10_0 = 0,
    PRIV_VERSION_1_11_0,
    PRIV_VERSION_1_12_0,
    PRIV_VERSION_1_13_0,

    PRIV_VERSION_LATEST = PRIV_VERSION_1_13_0,
};

#define VEXT_VERSION_1_00_0 0x00010000
#define VEXT_VER_1_00_0_STR "v1.0"
#define PEXT_VERSION_0_09_4 0x00000904
#define PEXT_VER_0_09_4_STR "v0.9.4"

enum {
    TRANSLATE_SUCCESS,
    TRANSLATE_FAIL,
    TRANSLATE_PMP_FAIL,
    TRANSLATE_G_STAGE_FAIL,
    TRANSLATE_MTT_FAIL
};

/* Extension context status */
typedef enum {
    EXT_STATUS_DISABLED = 0,
    EXT_STATUS_INITIAL,
    EXT_STATUS_CLEAN,
    EXT_STATUS_DIRTY,
} RISCVExtStatus;

/* Enum holds PMM field values for Zjpm v0.8 extension */
typedef enum {
    PMM_FIELD_DISABLED = 0,
    PMM_FIELD_RESERVED = 1,
    PMM_FIELD_PMLEN7   = 2,
    PMM_FIELD_PMLEN16  = 3,
} RISCVPmPmm;

#define MMU_USER_IDX 3

#define MAX_RISCV_PMPS (16)

#if !defined(CONFIG_USER_ONLY)
#include "pmp.h"
#include "debug.h"
#include "mtt.h"
#endif

#define RV_VLEN_MAX 4096
#define RV_MAX_MHPMEVENTS 32
#define RV_MAX_MHPMCOUNTERS 32

FIELD(VTYPE, VLMUL, 0, 3)
FIELD(VTYPE, VSEW, 3, 3)
FIELD(VTYPE, VTA, 6, 1)
FIELD(VTYPE, VMA, 7, 1)
FIELD(VTYPE, VEDIV, 8, 2)
FIELD(VTYPE, RESERVED, 10, sizeof(target_ulong) * 8 - 11)

FIELD(MSIZE, SIZEM, 0, 8)
FIELD(MSIZE, SIZEN, 8, 8)
FIELD(MSIZE, SIZEK, 16, 16)

#define RV_RLEN_MAX 4096
#define RV_MACC_LEN 32

#define LP_EXPECTED true
#define NO_LP_EXPECTED false

/* See the commentary above the TBFLAG field definitions.  */
typedef struct CPURISCVTBFlags {
    uint32_t flags;
    target_ulong flags2;
} CPURISCVTBFlags;

typedef struct PMUCTRState {
    /* Current value of a counter */
    target_ulong mhpmcounter_val;
    /* Current value of a counter in RV32 */
    target_ulong mhpmcounterh_val;
    /* Snapshot values of counter */
    target_ulong mhpmcounter_prev;
    /* Snapshort value of a counter in RV32 */
    target_ulong mhpmcounterh_prev;
    /* Value beyond UINT32_MAX/UINT64_MAX before overflow interrupt trigger */
    target_ulong irq_overflow_left;
} PMUCTRState;

typedef struct PMUFixedCtrState {
        /* Track cycle and icount for each privilege mode */
        uint64_t counter[4];
        uint64_t counter_prev[4];
        /* Track cycle and icount for each privilege mode when V = 1*/
        uint64_t counter_virt[2];
        uint64_t counter_virt_prev[2];
} PMUFixedCtrState;

struct CPUArchState {
    target_ulong gpr[32];
    target_ulong gprh[32]; /* 64 top bits of the 128-bit registers */

    /* vector coprocessor state. */
    uint64_t vreg[32 * RV_VLEN_MAX / 64] QEMU_ALIGNED(16);
    target_ulong vxrm;
    target_ulong vxsat;
    target_ulong vl;
    target_ulong vstart;
    target_ulong vtype;
    bool vill;

    target_ulong pc;
    target_ulong load_res;
    target_ulong load_val;

    /* Floating-Point state */
    uint64_t fpr[32]; /* assume both F and D extensions */
    target_ulong frm;
    float_status fp_status;

    /* matrix state */
    target_ulong mrstart;
    target_ulong mcsr;
    target_ulong mxsat;
    target_ulong mxrm;
    float_status mfp_status;
    target_ulong xmisa;
    uint64_t mreg[8 * RV_RLEN_MAX / RV_MACC_LEN  * RV_RLEN_MAX / 64] QEMU_ALIGNED(16);
    target_ulong sizem;
    target_ulong sizen;
    target_ulong sizek;

    target_ulong badaddr;
    target_ulong bins;

    target_ulong guest_phys_fault_addr;

    target_ulong priv_ver;
    target_ulong vext_ver;
    target_ulong pext_ver;

    /* RISCVMXL, but uint32_t for vmstate migration */
    uint32_t misa_mxl;      /* current mxl */
    uint32_t misa_ext;      /* current extensions */
    uint32_t misa_ext_mask; /* max ext for this cpu */
    uint32_t xl;            /* current xlen */

    /* 128-bit helpers upper part return value */
    target_ulong retxh;

    target_ulong jvt;
    bool elp;
    target_ulong ssp; /* Shadow Stack Pointer */
    target_ulong cfi_violation_code;

    target_ulong priv;
    /* CSRs for execution environment configuration */
    uint64_t menvcfg;
    target_ulong senvcfg;

#ifdef CONFIG_USER_ONLY
    uint32_t elf_flags;
#endif

#ifndef CONFIG_USER_ONLY
    /* This contains QEMU specific information about the virt state. */
    bool virt_enabled;
    target_ulong geilen;
    uint64_t resetvec;

    target_ulong mhartid;
    /*
     * For RV32 this is 32-bit mstatus and 32-bit mstatush.
     * For RV64 this is a 64-bit mstatus.
     */
    uint64_t mstatus;

    uint64_t mip;
    /*
     * MIP contains the software writable version of SEIP ORed with the
     * external interrupt value. The MIP register is always up-to-date.
     * To keep track of the current source, we also save booleans of the values
     * here.
     */
    bool external_seip;
    bool software_seip;

    uint64_t miclaim;

    uint64_t mie;
    uint64_t mideleg;

    /*
     * When mideleg[i]=0 and mvien[i]=1, sie[i] is no more
     * alias of mie[i] and needs to be maintained separately.
     */
    uint64_t sie;

    /*
     * When hideleg[i]=0 and hvien[i]=1, vsie[i] is no more
     * alias of sie[i] (mie[i]) and needs to be maintained separately.
     */
    uint64_t vsie;

    target_ulong satp;   /* since: priv-1.10.0 */
    target_ulong stval;
    target_ulong medeleg;

    target_ulong stvec;
    target_ulong sepc;
    target_ulong scause;

    target_ulong mtvec;
    target_ulong mepc;
    target_ulong mcause;
    target_ulong mtval;  /* since: priv-1.10.0 */

    uint64_t mctrctl;
    uint32_t sctrdepth;
    uint32_t sctrstatus;
    uint64_t vsctrctl;

    uint64_t ctr_src[16 << SCTRDEPTH_MAX];
    uint64_t ctr_dst[16 << SCTRDEPTH_MAX];
    uint64_t ctr_data[16 << SCTRDEPTH_MAX];
    target_ulong sqoscfg;

    /* Machine and Supervisor interrupt priorities */
    uint8_t miprio[64];
    uint8_t siprio[64];

    /* AIA CSRs */
    target_ulong miselect;
    target_ulong siselect;
    uint64_t mvien;
    uint64_t mvip;

    /* Hypervisor CSRs */
    target_ulong hstatus;
    target_ulong hedeleg;
    uint64_t hideleg;
    uint32_t hcounteren;
    target_ulong htval;
    target_ulong htinst;
    target_ulong hgatp;
    target_ulong hgeie;
    target_ulong hgeip;
    uint64_t htimedelta;
    uint64_t hvien;

    /*
     * Bits VSSIP, VSTIP and VSEIP in hvip are maintained in mip. Other bits
     * from 0:12 are reserved. Bits 13:63 are not aliased and must be separately
     * maintain in hvip.
     */
    uint64_t hvip;

    /* Hypervisor controlled virtual interrupt priorities */
    target_ulong hvictl;
    uint8_t hviprio[64];

    /* Upper 64-bits of 128-bit CSRs */
    uint64_t mscratchh;
    uint64_t sscratchh;

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

    /* AIA VS-mode CSRs */
    target_ulong vsiselect;

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

    /*
     * Signals whether the current exception occurred with two-stage address
     * translation active.
     */
    bool two_stage_lookup;
    /*
     * Signals whether the current exception occurred while doing two-stage
     * address translation for the VS-stage page table walk.
     */
    bool two_stage_indirect_lookup;

    uint32_t scounteren;
    uint32_t mcounteren;

    uint32_t scountinhibit;
    uint32_t mcountinhibit;

    /* PMU cycle & instret privilege mode filtering */
    target_ulong mcyclecfg;
    target_ulong mcyclecfgh;
    target_ulong minstretcfg;
    target_ulong minstretcfgh;

    /* PMU counter state */
    PMUCTRState pmu_ctrs[RV_MAX_MHPMCOUNTERS];

    /* PMU event selector configured values. First three are unused */
    target_ulong mhpmevent_val[RV_MAX_MHPMEVENTS];

    /* PMU event selector configured values for RV32 */
    target_ulong mhpmeventh_val[RV_MAX_MHPMEVENTS];

    PMUFixedCtrState pmu_fixed_ctrs[2];

    target_ulong sscratch;
    target_ulong mscratch;

    /* Sstc CSRs */
    uint64_t stimecmp;

    uint64_t vstimecmp;

    /* physical memory protection */
    pmp_table_t pmp_state;
    uint64_t mseccfg;

    /* trigger module */
    target_ulong trigger_cur;
    target_ulong tdata1[RV_MAX_TRIGGERS];
    target_ulong tdata2[RV_MAX_TRIGGERS];
    target_ulong tdata3[RV_MAX_TRIGGERS];
    target_ulong mcontext;
    target_ulong scontext;
    struct CPUBreakpoint *cpu_breakpoint[RV_MAX_TRIGGERS];
    struct CPUWatchpoint *cpu_watchpoint[RV_MAX_TRIGGERS];
    QEMUTimer *itrigger_timer[RV_MAX_TRIGGERS];
    int64_t last_icount;
    bool itrigger_enabled;

    /* machine specific rdtime callback */
    uint64_t (*rdtime_fn)(void *);
    void *rdtime_fn_arg;

    /* machine specific AIA ireg read-modify-write callback */
#define AIA_MAKE_IREG(__isel, __priv, __virt, __vgein, __xlen) \
    ((((__xlen) & 0xff) << 24) | \
     (((__vgein) & 0x3f) << 20) | \
     (((__virt) & 0x1) << 18) | \
     (((__priv) & 0x3) << 16) | \
     (__isel & 0xffff))
#define AIA_IREG_ISEL(__ireg)                  ((__ireg) & 0xffff)
#define AIA_IREG_PRIV(__ireg)                  (((__ireg) >> 16) & 0x3)
#define AIA_IREG_VIRT(__ireg)                  (((__ireg) >> 18) & 0x1)
#define AIA_IREG_VGEIN(__ireg)                 (((__ireg) >> 20) & 0x3f)
#define AIA_IREG_XLEN(__ireg)                  (((__ireg) >> 24) & 0xff)
    int (*aia_ireg_rmw_fn[4])(void *arg, target_ulong reg,
        target_ulong *val, target_ulong new_val, target_ulong write_mask);
    void *aia_ireg_rmw_fn_arg[4];

    /* True if in debugger mode.  */
    bool debugger;

    /* CSRs for execution environment configuration */
    uint64_t mstateen[SMSTATEEN_MAX_COUNT];
    uint64_t hstateen[SMSTATEEN_MAX_COUNT];
    uint64_t sstateen[SMSTATEEN_MAX_COUNT];
    uint64_t henvcfg;

    /* Xuantie system mode extends */
    uint64_t mxstatus;
    uint64_t mrmr;
    uint64_t mrvbr;
    uint64_t cpuid;
    uint64_t sxstatus;
    uint64_t smcir;
    uint64_t smir;
    uint64_t smlo0;
    uint64_t smeh;
    uint64_t mexstatus;
    CPURISCVState *next_cpu;
    bool     in_reset;
    bool     clint_clic;
    target_ulong excp_vld;

    /* tcm */
    MemoryRegion *dtcm;
    MemoryRegion *itcm;
    target_ulong mdtcmcr;
    target_ulong mitcmcr;
    uint64_t fastmcr;
    uint64_t fastmcr_old;

    /* CLIC */
    uint32_t mintstatus;
    target_ulong mintthresh;
    target_ulong sintthresh;
    target_ulong mclicbase; /* clic-spec */
    target_ulong stvt;
    target_ulong mtvt; /* base address of the trap vector table */

    /* Smsdid */
    uint32_t mttmode;
    uint32_t sdid;
    uint64_t mttppn;
    uint32_t msdcfg;


#endif
    /* Xuantie extends */
    bool bf16;
    bool utn_sat; /* Titan 2D reduce/conversion to fp8 saturation mode */
    uint64_t elf_start;
    uint32_t pctrace;
    uint32_t tb_trace;
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
    target_ulong last_pc;
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

    /* Fields from here on are preserved across CPU reset. */
    QEMUTimer *stimer; /* Internal timer for S-mode interrupt */
    QEMUTimer *vstimer; /* Internal timer for VS-mode interrupt */
    bool vstime_irq;

    hwaddr kernel_addr;
    hwaddr fdt_addr;

    /* CLIC */
    void *clic;
    uint32_t exccode; /* clic irq encode */

    bool dsa_en;
    riscv_dsa_ops *dsa_ops;
    qemu_dsa_ops *qdsa_ops;
    qemu_float_ops *qdsa_float_ops;
#ifdef CONFIG_KVM
    /* kvm timer */
    bool kvm_timer_dirty;
    uint64_t kvm_timer_time;
    uint64_t kvm_timer_compare;
    uint64_t kvm_timer_state;
    uint64_t kvm_timer_frequency;
#endif /* CONFIG_KVM */
};

typedef enum XTPowerState {
    XT_POWER_ON = 0,
    XT_POWER_OFF,
} XTPowerState;

/*
 * RISCVCPU:
 * @env: #CPURISCVState
 *
 * A RISCV CPU.
 */
struct ArchCPU {
    CPUState parent_obj;

    CPURISCVState env;

    GDBFeature dyn_csr_feature;
    GDBFeature dyn_vreg_feature;
    GDBFeature dyn_mreg_feature;

    /* Configuration Settings */
    RISCVCPUConfig cfg;

    QEMUTimer *pmu_timer;
    /* A bitmask of Available programmable counters */
    uint32_t pmu_avail_ctrs;
    /* Mapping of events to counters */
    GHashTable *pmu_event_ctr_map;
    const GPtrArray *decoders;
    /* extended by Xuantie for xiaohui platform */
    XTPowerState power_state;
};

/**
 * RISCVCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * A RISCV CPU model.
 */
struct RISCVCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
    uint32_t misa_mxl_max;  /* max mxl for this cpu */
    uint64_t mrvbr;
    uint64_t mrmr;
};

static inline int riscv_has_ext(CPURISCVState *env, target_ulong ext)
{
    return (env->misa_ext & ext) != 0;
}

#include "cpu_user.h"

extern const char * const riscv_int_regnames[];
extern const char * const riscv_int_regnamesh[];
extern const char * const riscv_fpr_regnames[];

const char *riscv_cpu_get_trap_name(target_ulong cause, bool async, bool clic);
void riscv_cpu_do_interrupt(CPUState *cpu);
int riscv_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, DumpState *s);
int riscv_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, DumpState *s);
int riscv_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int riscv_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
int riscv_cpu_hviprio_index2irq(int index, int *out_irq, int *out_rdzero);
uint8_t riscv_cpu_default_priority(int irq);
uint64_t riscv_cpu_all_pending(CPURISCVState *env);
int riscv_cpu_mirq_pending(CPURISCVState *env);
int riscv_cpu_sirq_pending(CPURISCVState *env);
int riscv_cpu_vsirq_pending(CPURISCVState *env);
bool riscv_cpu_fp_enabled(CPURISCVState *env);
target_ulong riscv_cpu_get_geilen(CPURISCVState *env);
void riscv_cpu_set_geilen(CPURISCVState *env, target_ulong geilen);
bool riscv_cpu_vector_enabled(CPURISCVState *env);
bool riscv_cpu_matrix_enabled(CPURISCVState *env);
void riscv_cpu_set_virt_enabled(CPURISCVState *env, bool enable);
int riscv_env_mmu_index(CPURISCVState *env, bool ifetch);
bool riscv_cpu_get_xsse(CPURISCVState *env);
bool riscv_cpu_get_xlpe(CPURISCVState *env);
G_NORETURN void  riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                               MMUAccessType access_type,
                                               int mmu_idx, uintptr_t retaddr);
bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
char *riscv_isa_string(RISCVCPU *cpu);
int riscv_cpu_max_xlen(RISCVCPUClass *mcc);
bool riscv_cpu_option_set(const char *optname);

#ifndef CONFIG_USER_ONLY
bool riscv_env_smode_dbltrp_enabled(CPURISCVState *env);
void riscv_isa_write_fdt(RISCVCPU *cpu, void *fdt, char *nodename);
void riscv_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr);
hwaddr riscv_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
void riscv_cpu_swap_hypervisor_regs(CPURISCVState *env);
int riscv_cpu_claim_interrupts(RISCVCPU *cpu, uint64_t interrupts);
uint64_t riscv_cpu_update_mip(CPURISCVState *env, uint64_t mask,
                              uint64_t value);
bool riscv_cpu_local_irq_mode_enabled(CPURISCVState *env, int mode);
void riscv_cpu_interrupt(CPURISCVState *env);
#define BOOL_TO_MASK(x) (-!!(x)) /* helper for riscv_cpu_update_mip value */
void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(void *),
                             void *arg);
void riscv_cpu_set_aia_ireg_rmw_fn(CPURISCVState *env, uint32_t priv,
                                   int (*rmw_fn)(void *arg,
                                                 target_ulong reg,
                                                 target_ulong *val,
                                                 target_ulong new_val,
                                                 target_ulong write_mask),
                                   void *rmw_fn_arg);

RISCVException smstateen_acc_ok(CPURISCVState *env, int index, uint64_t bit);
#endif
void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv, bool virt_en);

void riscv_ctr_freeze(CPURISCVState *env, uint64_t freeze_mask);
void riscv_ctr_add_entry(CPURISCVState *env, target_long src, target_long dst,
                         uint64_t type, target_ulong prev_priv, bool prev_virt);
void riscv_ctr_clear(CPURISCVState *env);

void riscv_translate_init(void);
G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      uint32_t exception, uintptr_t pc);

target_ulong riscv_cpu_get_fflags(CPURISCVState *env);
void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong);

target_ulong riscv_cpu_get_mfflags(CPURISCVState *env);
void riscv_cpu_set_mfflags(CPURISCVState *env, target_ulong);
void riscv_cpu_set_mfrm(CPURISCVState *env, uint32_t);

#include "exec/cpu-all.h"

FIELD(TB_FLAGS_ANY, MEM_IDX, 0, 3)
FIELD(TB_FLAGS_ANY, FS, 3, 2)
/* Vector flags */
FIELD(TB_FLAGS_ANY, VS, 5, 2)
FIELD(TB_FLAGS_ANY, LMUL, 7, 3)
FIELD(TB_FLAGS_ANY, SEW, 10, 3)
FIELD(TB_FLAGS_ANY, VL_EQ_VLMAX, 13, 1)
FIELD(TB_FLAGS_ANY, VILL, 14, 1)
FIELD(TB_FLAGS_ANY, VSTART_EQ_ZERO, 15, 1)
/* The combination of MXL/SXL/UXL that applies to the current cpu mode. */
FIELD(TB_FLAGS_ANY, XL, 16, 2)
FIELD(TB_FLAGS_ANY, VTA, 18, 1)
FIELD(TB_FLAGS_ANY, VMA, 19, 1)
FIELD(TB_FLAGS_ANY, ITRIGGER, 20, 1)
FIELD(TB_FLAGS_ANY, VIRT_ENABLED, 21, 1)
FIELD(TB_FLAGS_ANY, PRIV, 22, 2)
FIELD(TB_FLAGS_ANY, AXL, 24, 2)
/* If pointer masking should be applied and address sign extended */
FIELD(TB_FLAGS_ANY, PM_PMM, 26, 2)
FIELD(TB_FLAGS_ANY, PM_SIGNEXTEND, 28, 1)
FIELD(TB_FLAGS_ANY, XSSE, 29, 1)
FIELD(TB_FLAGS_ANY, ELP, 30, 1)

/* matrix flags*/
FIELD(TB_FLAGS_THEAD, PWFP, 0, 1)
FIELD(TB_FLAGS_THEAD, PWINT, 1, 1)
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
FIELD(TB_FLAGS_THEAD, F16F32, 14, 1)
FIELD(TB_FLAGS_THEAD, F32F64, 15, 1)
FIELD(TB_FLAGS_THEAD, BF16, 20, 1)
FIELD(TB_FLAGS_THEAD, MSD, 21, 1)
FIELD(TB_FLAGS_THEAD, SPARSITYFP, 22, 1)
FIELD(TB_FLAGS_THEAD, SPARSITYINT, 23, 1)
FIELD(TB_FLAGS_THEAD, FPINTCVT, 24, 1)
FIELD(TB_FLAGS_THEAD, F8F32, 25, 1)
FIELD(TB_FLAGS_THEAD, F8F16, 26, 1)

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
#define riscv_cpu_mxl_bits(env) (1UL << (4 + riscv_cpu_mxl(env)))

static inline const RISCVCPUConfig *riscv_cpu_cfg(CPURISCVState *env)
{
    return &env_archcpu(env)->cfg;
}

#if !defined(CONFIG_USER_ONLY)
static inline int cpu_address_mode(CPURISCVState *env)
{
    int mode = env->priv;

    if (mode == PRV_M && get_field(env->mstatus, MSTATUS_MPRV)) {
        mode = get_field(env->mstatus, MSTATUS_MPP);
    }
    return mode;
}

static inline RISCVMXL cpu_get_xl(CPURISCVState *env, target_ulong mode)
{
    RISCVMXL xl = env->misa_mxl;
    /*
     * When emulating a 32-bit-only cpu, use RV32.
     * When emulating a 64-bit cpu, and MXL has been reduced to RV32,
     * MSTATUSH doesn't have UXL/SXL, therefore XLEN cannot be widened
     * back to RV64 for lower privs.
     */
    if (xl != MXL_RV32) {
        switch (mode) {
        case PRV_M:
            break;
        case PRV_U:
            xl = get_field(env->mstatus, MSTATUS64_UXL);
            break;
        default: /* PRV_S */
            xl = get_field(env->mstatus, MSTATUS64_SXL);
            break;
        }
    }
    return xl;
}
#endif

#if defined(TARGET_RISCV32)
#define cpu_recompute_xl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL cpu_recompute_xl(CPURISCVState *env)
{
#if !defined(CONFIG_USER_ONLY)
    return cpu_get_xl(env, env->priv);
#else
    return env->misa_mxl;
#endif
}
#endif

#if defined(TARGET_RISCV32)
#define cpu_address_xl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL cpu_address_xl(CPURISCVState *env)
{
#ifdef CONFIG_USER_ONLY
    return env->xl;
#else
    int mode = cpu_address_mode(env);

    return cpu_get_xl(env, mode);
#endif
}
#endif

static inline int riscv_cpu_xlen(CPURISCVState *env)
{
    return 16 << env->xl;
}

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
static inline uint32_t vext_get_vlmax(uint32_t vlenb, uint32_t vsew,
                                      int8_t lmul)
{
    uint32_t vlen = vlenb << 3;

    /*
     * We need to use 'vlen' instead of 'vlenb' to
     * preserve the '+ 3' in the formula. Otherwise
     * we risk a negative shift if vsew < lmul.
     */
    return vlen >> (vsew + 3 - lmul);
}

void cpu_get_tb_cpu_state(CPURISCVState *env, vaddr *pc,
                          uint64_t *cs_base, uint32_t *pflags);

bool riscv_cpu_is_32bit(RISCVCPU *cpu);

bool riscv_cpu_virt_mem_enabled(CPURISCVState *env);
RISCVPmPmm riscv_pm_get_pmm(CPURISCVState *env);
int riscv_pm_get_pmlen(RISCVPmPmm pmm);

RISCVException riscv_csrr(CPURISCVState *env, int csrno,
                          target_ulong *ret_value);

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

RISCVException riscv_csrr_i128(CPURISCVState *env, int csrno,
                               Int128 *ret_value);
RISCVException riscv_csrrw_i128(CPURISCVState *env, int csrno,
                                Int128 *ret_value,
                                Int128 new_value, Int128 write_mask);

typedef RISCVException (*riscv_csr_read128_fn)(CPURISCVState *env, int csrno,
                                               Int128 *ret_value);
typedef RISCVException (*riscv_csr_write128_fn)(CPURISCVState *env, int csrno,
                                             Int128 new_value);

typedef struct {
    const char *name;
    riscv_csr_predicate_fn predicate;
    riscv_csr_read_fn read;
    riscv_csr_write_fn write;
    riscv_csr_op_fn op;
    riscv_csr_read128_fn read128;
    riscv_csr_write128_fn write128;
    /* The default priv spec version should be PRIV_VERSION_1_10_0 (i.e 0) */
    uint32_t min_priv_ver;
} riscv_csr_operations;

/* CSR function table constants */
enum {
    CSR_TABLE_SIZE = 0x1000
};

/*
 * The event id are encoded based on the encoding specified in the
 * SBI specification v0.3
 */

enum riscv_pmu_event_idx {
    RISCV_PMU_EVENT_HW_CPU_CYCLES = 0x01,
    RISCV_PMU_EVENT_HW_INSTRUCTIONS = 0x02,
    RISCV_PMU_EVENT_CACHE_DTLB_READ_MISS = 0x10019,
    RISCV_PMU_EVENT_CACHE_DTLB_WRITE_MISS = 0x1001B,
    RISCV_PMU_EVENT_CACHE_ITLB_PREFETCH_MISS = 0x10021,
};

/* used by tcg/tcg-cpu.c*/
void isa_ext_update_enabled(RISCVCPU *cpu, uint32_t ext_offset, bool en);
bool isa_ext_is_enabled(RISCVCPU *cpu, uint32_t ext_offset);
void riscv_cpu_set_misa_ext(CPURISCVState *env, uint32_t ext);
bool riscv_cpu_is_vendor(Object *cpu_obj);

typedef struct RISCVCPUMultiExtConfig {
    const char *name;
    uint32_t offset;
    bool enabled;
} RISCVCPUMultiExtConfig;

extern const RISCVCPUMultiExtConfig riscv_cpu_extensions[];
extern const RISCVCPUMultiExtConfig riscv_cpu_vendor_exts[];
extern const RISCVCPUMultiExtConfig riscv_cpu_experimental_exts[];
extern const RISCVCPUMultiExtConfig riscv_cpu_named_features[];
extern const RISCVCPUMultiExtConfig riscv_cpu_deprecated_exts[];

typedef struct isa_ext_data {
    const char *name;
    int min_version;
    int ext_enable_offset;
} RISCVIsaExtData;
extern const RISCVIsaExtData isa_edata_arr[];
char *riscv_cpu_get_name(RISCVCPU *cpu);

void riscv_cpu_finalize_features(RISCVCPU *cpu, Error **errp);
void riscv_add_satp_mode_properties(Object *obj);
bool riscv_cpu_accelerator_compatible(RISCVCPU *cpu);

/* CSR function table */
RISCVException fs(CPURISCVState *env, int csrno);
RISCVException vs(CPURISCVState *env, int csrno);
RISCVException any(CPURISCVState *env, int csrno);
RISCVException smode(CPURISCVState *env, int csrno);
RISCVException clic(CPURISCVState *env, int csrno);
RISCVException read_fcsr(CPURISCVState *env, int csrno,
                         target_ulong *val);
RISCVException write_fcsr(CPURISCVState *env, int csrno,
                          target_ulong val);
RISCVException read_vtype(CPURISCVState *env, int csrno,
                          target_ulong *val);
RISCVException read_vl(CPURISCVState *env, int csrno,
                       target_ulong *val);
RISCVException read_vlenb(CPURISCVState *env, int csrno,
                          target_ulong *val);
RISCVException read_vxrm(CPURISCVState *env, int csrno,
                         target_ulong *val);
RISCVException write_vxrm(CPURISCVState *env, int csrno,
                          target_ulong val);
RISCVException read_vxsat(CPURISCVState *env, int csrno,
                          target_ulong *val);
RISCVException write_vxsat(CPURISCVState *env, int csrno,
                           target_ulong val);
RISCVException read_vstart(CPURISCVState *env, int csrno,
                           target_ulong *val);
RISCVException write_vstart(CPURISCVState *env, int csrno,
                            target_ulong val);
RISCVException read_mstatus(CPURISCVState *env, int csrno,
                            target_ulong *val);
RISCVException write_mstatus(CPURISCVState *env, int csrno,
                             target_ulong val);
RISCVException write_sstatus(CPURISCVState *env, int csrno,
                             target_ulong val);
RISCVException read_sstatus(CPURISCVState *env, int csrno,
                            target_ulong *val);
RISCVException read_vcsr(CPURISCVState *env, int csrno, target_ulong *val);
RISCVException write_vcsr(CPURISCVState *env, int csrno, target_ulong val);
extern riscv_csr_operations csr_ops[CSR_TABLE_SIZE];

extern const bool valid_vm_1_10_32[], valid_vm_1_10_64[];

void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops);
void riscv_set_csr_ops(int csrno, riscv_csr_operations *ops);

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs);

target_ulong riscv_new_csr_seed(target_ulong new_value,
                                target_ulong write_mask);

uint8_t satp_mode_max_from_map(uint32_t map);
const char *satp_mode_str(uint8_t satp_mode, bool is_32_bit);

/* Implemented in th_csr.c */
void th_register_custom_csrs(RISCVCPU *cpu);

const char *priv_spec_to_str(int priv_version);

bool decode_dsa(CPURISCVState *env, uint32_t insn, uint32_t length);
void dsa_finalize(RISCVCPU *cpu, Error **errp);
#endif /* RISCV_CPU_H */
