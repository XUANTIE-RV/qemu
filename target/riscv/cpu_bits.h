/* RISC-V ISA constants */

#ifndef TARGET_RISCV_CPU_BITS_H
#define TARGET_RISCV_CPU_BITS_H

#define get_field(reg, mask) (((reg) & \
                 (uint64_t)(mask)) / ((mask) & ~((mask) << 1)))
#define set_field(reg, mask, val) (((reg) & ~(uint64_t)(mask)) | \
                 (((uint64_t)(val) * ((mask) & ~((mask) << 1))) & \
                 (uint64_t)(mask)))

#define CSR_TH_SXSTATUS 0x5c0

/* TH_SXSTATUS bits */
#define TH_SXSTATUS_UCME        BIT(16)
#define TH_SXSTATUS_MAEE        BIT(21)
#define TH_SXSTATUS_THEADISAEE  BIT(22)

/* Extension context status mask */
#define EXT_STATUS_MASK     0x3ULL

/* Floating point round mode */
#define FSR_RD_SHIFT        5
#define FSR_RD              (0x7 << FSR_RD_SHIFT)

/* Floating point accrued exception flags */
#define FPEXC_NX            0x01
#define FPEXC_UF            0x02
#define FPEXC_OF            0x04
#define FPEXC_DZ            0x08
#define FPEXC_NV            0x10

/* Floating point status register bits */
#define FSR_AEXC_SHIFT      0
#define FSR_NVA             (FPEXC_NV << FSR_AEXC_SHIFT)
#define FSR_OFA             (FPEXC_OF << FSR_AEXC_SHIFT)
#define FSR_UFA             (FPEXC_UF << FSR_AEXC_SHIFT)
#define FSR_DZA             (FPEXC_DZ << FSR_AEXC_SHIFT)
#define FSR_NXA             (FPEXC_NX << FSR_AEXC_SHIFT)
#define FSR_AEXC            (FSR_NVA | FSR_OFA | FSR_UFA | FSR_DZA | FSR_NXA)

/* Control and Status Registers */

/* User Trap Setup */
#define CSR_USTATUS         0x000
#define CSR_UIE             0x004
#define CSR_UTVEC           0x005

/* User Trap Handling */
#define CSR_USCRATCH        0x040
#define CSR_UEPC            0x041
#define CSR_UCAUSE          0x042
#define CSR_UTVAL           0x043
#define CSR_UIP             0x044

/* User Floating-Point CSRs */
#define CSR_FFLAGS          0x001
#define CSR_FRM             0x002
#define CSR_FCSR            0x003

/* User Vector CSRs */
#define CSR_VSTART          0x008
#define CSR_VXSAT           0x009
#define CSR_VXRM            0x00a
#define CSR_VCSR            0x00f
#define CSR_VL              0xc20
#define CSR_VTYPE           0xc21
#define CSR_VLENB           0xc22

/* VCSR fields */
#define VCSR_VXSAT_SHIFT    0
#define VCSR_VXSAT          (0x1 << VCSR_VXSAT_SHIFT)
#define VCSR_VXRM_SHIFT     1
#define VCSR_VXRM           (0x3 << VCSR_VXRM_SHIFT)

/* User Timers and Counters */
#define CSR_CYCLE           0xc00
#define CSR_TIME            0xc01
#define CSR_INSTRET         0xc02
#define CSR_HPMCOUNTER3     0xc03
#define CSR_HPMCOUNTER4     0xc04
#define CSR_HPMCOUNTER5     0xc05
#define CSR_HPMCOUNTER6     0xc06
#define CSR_HPMCOUNTER7     0xc07
#define CSR_HPMCOUNTER8     0xc08
#define CSR_HPMCOUNTER9     0xc09
#define CSR_HPMCOUNTER10    0xc0a
#define CSR_HPMCOUNTER11    0xc0b
#define CSR_HPMCOUNTER12    0xc0c
#define CSR_HPMCOUNTER13    0xc0d
#define CSR_HPMCOUNTER14    0xc0e
#define CSR_HPMCOUNTER15    0xc0f
#define CSR_HPMCOUNTER16    0xc10
#define CSR_HPMCOUNTER17    0xc11
#define CSR_HPMCOUNTER18    0xc12
#define CSR_HPMCOUNTER19    0xc13
#define CSR_HPMCOUNTER20    0xc14
#define CSR_HPMCOUNTER21    0xc15
#define CSR_HPMCOUNTER22    0xc16
#define CSR_HPMCOUNTER23    0xc17
#define CSR_HPMCOUNTER24    0xc18
#define CSR_HPMCOUNTER25    0xc19
#define CSR_HPMCOUNTER26    0xc1a
#define CSR_HPMCOUNTER27    0xc1b
#define CSR_HPMCOUNTER28    0xc1c
#define CSR_HPMCOUNTER29    0xc1d
#define CSR_HPMCOUNTER30    0xc1e
#define CSR_HPMCOUNTER31    0xc1f
#define CSR_CYCLEH          0xc80
#define CSR_TIMEH           0xc81
#define CSR_INSTRETH        0xc82
#define CSR_HPMCOUNTER3H    0xc83
#define CSR_HPMCOUNTER4H    0xc84
#define CSR_HPMCOUNTER5H    0xc85
#define CSR_HPMCOUNTER6H    0xc86
#define CSR_HPMCOUNTER7H    0xc87
#define CSR_HPMCOUNTER8H    0xc88
#define CSR_HPMCOUNTER9H    0xc89
#define CSR_HPMCOUNTER10H   0xc8a
#define CSR_HPMCOUNTER11H   0xc8b
#define CSR_HPMCOUNTER12H   0xc8c
#define CSR_HPMCOUNTER13H   0xc8d
#define CSR_HPMCOUNTER14H   0xc8e
#define CSR_HPMCOUNTER15H   0xc8f
#define CSR_HPMCOUNTER16H   0xc90
#define CSR_HPMCOUNTER17H   0xc91
#define CSR_HPMCOUNTER18H   0xc92
#define CSR_HPMCOUNTER19H   0xc93
#define CSR_HPMCOUNTER20H   0xc94
#define CSR_HPMCOUNTER21H   0xc95
#define CSR_HPMCOUNTER22H   0xc96
#define CSR_HPMCOUNTER23H   0xc97
#define CSR_HPMCOUNTER24H   0xc98
#define CSR_HPMCOUNTER25H   0xc99
#define CSR_HPMCOUNTER26H   0xc9a
#define CSR_HPMCOUNTER27H   0xc9b
#define CSR_HPMCOUNTER28H   0xc9c
#define CSR_HPMCOUNTER29H   0xc9d
#define CSR_HPMCOUNTER30H   0xc9e
#define CSR_HPMCOUNTER31H   0xc9f

/* Machine Timers and Counters */
#define CSR_MCYCLE          0xb00
#define CSR_MINSTRET        0xb02
#define CSR_MCYCLEH         0xb80
#define CSR_MINSTRETH       0xb82

/* Machine Information Registers */
#define CSR_MVENDORID       0xf11
#define CSR_MARCHID         0xf12
#define CSR_MIMPID          0xf13
#define CSR_MHARTID         0xf14
#define CSR_MCONFIGPTR      0xf15

/* Machine Trap Setup */
#define CSR_MSTATUS         0x300
#define CSR_MISA            0x301
#define CSR_MEDELEG         0x302
#define CSR_MIDELEG         0x303
#define CSR_MIE             0x304
#define CSR_MTVEC           0x305
#define CSR_MCOUNTEREN      0x306

/* 32-bit only */
#define CSR_MSTATUSH        0x310
#define CSR_MEDELEGH        0x312
#define CSR_HEDELEGH        0x612

/* Machine Trap Handling */
#define CSR_MSCRATCH        0x340
#define CSR_MEPC            0x341
#define CSR_MCAUSE          0x342
#define CSR_MTVAL           0x343
#define CSR_MIP             0x344

/* Machine-Level Window to Indirectly Accessed Registers (AIA) */
#define CSR_MISELECT        0x350
#define CSR_MIREG           0x351

/* Machine Indirect Register Alias */
#define CSR_MIREG2          0x352
#define CSR_MIREG3          0x353
#define CSR_MIREG4          0x355
#define CSR_MIREG5          0x356
#define CSR_MIREG6          0x357

/* Machine-Level Interrupts (AIA) */
#define CSR_MTOPEI          0x35c
#define CSR_MTOPI           0xfb0

/* Virtual Interrupts for Supervisor Level (AIA) */
#define CSR_MVIEN           0x308
#define CSR_MVIP            0x309

/* Machine-Level High-Half CSRs (AIA) */
#define CSR_MIDELEGH        0x313
#define CSR_MIEH            0x314
#define CSR_MVIENH          0x318
#define CSR_MVIPH           0x319
#define CSR_MIPH            0x354

/* Supervisor Trap Setup */
#define CSR_SSTATUS         0x100
#define CSR_SIE             0x104
#define CSR_STVEC           0x105
#define CSR_SCOUNTEREN      0x106

/* Supervisor Configuration CSRs */
#define CSR_SENVCFG         0x10A

/* Supervisor state CSRs */
#define CSR_SSTATEEN0       0x10C
#define CSR_SSTATEEN1       0x10D
#define CSR_SSTATEEN2       0x10E
#define CSR_SSTATEEN3       0x10F

/* Supervisor Counter Delegation */
#define CSR_SCOUNTINHIBIT   0x120

/* Supervisor Trap Handling */
#define CSR_SSCRATCH        0x140
#define CSR_SEPC            0x141
#define CSR_SCAUSE          0x142
#define CSR_STVAL           0x143
#define CSR_SIP             0x144

/* Sstc supervisor CSRs */
#define CSR_STIMECMP        0x14D
#define CSR_STIMECMPH       0x15D

/* Supervisor Protection and Translation */
#define CSR_SPTBR           0x180
#define CSR_SATP            0x180
#define CSR_SQOSCFG         0x181

/* Supervisor-Level Window to Indirectly Accessed Registers (AIA) */
#define CSR_SISELECT        0x150
#define CSR_SIREG           0x151

/* Supervisor Indirect Register Alias */
#define CSR_SIREG2          0x152
#define CSR_SIREG3          0x153
#define CSR_SIREG4          0x155
#define CSR_SIREG5          0x156
#define CSR_SIREG6          0x157

/* Supervisor-Level Interrupts (AIA) */
#define CSR_STOPEI          0x15c
#define CSR_STOPI           0xdb0

/* Supervisor-Level High-Half CSRs (AIA) */
#define CSR_SIEH            0x114
#define CSR_SIPH            0x154

/* Machine-Level Control transfer records CSRs */
#define CSR_MCTRCTL         0x34e

/* Supervisor-Level Control transfer records CSRs */
#define CSR_SCTRCTL         0x14e
#define CSR_SCTRSTATUS      0x14f
#define CSR_SCTRDEPTH       0x15f

/* VS-Level Control transfer records CSRs */
#define CSR_VSCTRCTL        0x24e

/* Hpervisor CSRs */
#define CSR_HSTATUS         0x600
#define CSR_HEDELEG         0x602
#define CSR_HIDELEG         0x603
#define CSR_HIE             0x604
#define CSR_HCOUNTEREN      0x606
#define CSR_HGEIE           0x607
#define CSR_HTVAL           0x643
#define CSR_HVIP            0x645
#define CSR_HIP             0x644
#define CSR_HTINST          0x64A
#define CSR_HGEIP           0xE12
#define CSR_HGATP           0x680
#define CSR_HTIMEDELTA      0x605
#define CSR_HTIMEDELTAH     0x615

/* Hypervisor Configuration CSRs */
#define CSR_HENVCFG         0x60A
#define CSR_HENVCFGH        0x61A

/* Hypervisor state CSRs */
#define CSR_HSTATEEN0       0x60C
#define CSR_HSTATEEN0H      0x61C
#define CSR_HSTATEEN1       0x60D
#define CSR_HSTATEEN1H      0x61D
#define CSR_HSTATEEN2       0x60E
#define CSR_HSTATEEN2H      0x61E
#define CSR_HSTATEEN3       0x60F
#define CSR_HSTATEEN3H      0x61F

/* Virtual CSRs */
#define CSR_VSSTATUS        0x200
#define CSR_VSIE            0x204
#define CSR_VSTVEC          0x205
#define CSR_VSSCRATCH       0x240
#define CSR_VSEPC           0x241
#define CSR_VSCAUSE         0x242
#define CSR_VSTVAL          0x243
#define CSR_VSIP            0x244
#define CSR_VSATP           0x280

/* Sstc virtual CSRs */
#define CSR_VSTIMECMP       0x24D
#define CSR_VSTIMECMPH      0x25D

#define CSR_MTINST          0x34a
#define CSR_MTVAL2          0x34b

/* Virtual Interrupts and Interrupt Priorities (H-extension with AIA) */
#define CSR_HVIEN           0x608
#define CSR_HVICTL          0x609
#define CSR_HVIPRIO1        0x646
#define CSR_HVIPRIO2        0x647

/* VS-Level Window to Indirectly Accessed Registers (H-extension with AIA) */
#define CSR_VSISELECT       0x250
#define CSR_VSIREG          0x251

/* Virtual Supervisor Indirect Alias */
#define CSR_VSIREG2         0x252
#define CSR_VSIREG3         0x253
#define CSR_VSIREG4         0x255
#define CSR_VSIREG5         0x256
#define CSR_VSIREG6         0x257

/* VS-Level Interrupts (H-extension with AIA) */
#define CSR_VSTOPEI         0x25c
#define CSR_VSTOPI          0xeb0

/* Hypervisor and VS-Level High-Half CSRs (H-extension with AIA) */
#define CSR_HIDELEGH        0x613
#define CSR_HVIENH          0x618
#define CSR_HVIPH           0x655
#define CSR_HVIPRIO1H       0x656
#define CSR_HVIPRIO2H       0x657
#define CSR_VSIEH           0x214
#define CSR_VSIPH           0x254

/* Machine Configuration CSRs */
#define CSR_MENVCFG         0x30A
#define CSR_MENVCFGH        0x31A

/* Machine state CSRs */
#define CSR_MSTATEEN0       0x30C
#define CSR_MSTATEEN0H      0x31C
#define CSR_MSTATEEN1       0x30D
#define CSR_MSTATEEN1H      0x31D
#define CSR_MSTATEEN2       0x30E
#define CSR_MSTATEEN2H      0x31E
#define CSR_MSTATEEN3       0x30F
#define CSR_MSTATEEN3H      0x31F

/* Common defines for all smstateen */
#define SMSTATEEN_MAX_COUNT 4
#define SMSTATEEN0_CS       (1ULL << 0)
#define SMSTATEEN0_FCSR     (1ULL << 1)
#define SMSTATEEN0_JVT      (1ULL << 2)
#define SMSTATEEN0_P1P13    (1ULL << 56)
#define SMSTATEEN0_CTR      (1ULL << 54)
#define SMSTATEEN0_HSCONTXT (1ULL << 57)
#define SMSTATEEN0_IMSIC    (1ULL << 58)
#define SMSTATEEN0_AIA      (1ULL << 59)
#define SMSTATEEN0_CSRIND   (1ULL << 60)
#define SMSTATEEN0_HSENVCFG (1ULL << 62)
#define SMSTATEEN_STATEEN   (1ULL << 63)

/* Enhanced Physical Memory Protection (ePMP) */
#define CSR_MSECCFG         0x747
#define CSR_MSECCFGH        0x757
/* Physical Memory Protection */
#define CSR_PMPCFG0         0x3a0
#define CSR_PMPCFG1         0x3a1
#define CSR_PMPCFG2         0x3a2
#define CSR_PMPCFG3         0x3a3
#define CSR_PMPADDR0        0x3b0
#define CSR_PMPADDR1        0x3b1
#define CSR_PMPADDR2        0x3b2
#define CSR_PMPADDR3        0x3b3
#define CSR_PMPADDR4        0x3b4
#define CSR_PMPADDR5        0x3b5
#define CSR_PMPADDR6        0x3b6
#define CSR_PMPADDR7        0x3b7
#define CSR_PMPADDR8        0x3b8
#define CSR_PMPADDR9        0x3b9
#define CSR_PMPADDR10       0x3ba
#define CSR_PMPADDR11       0x3bb
#define CSR_PMPADDR12       0x3bc
#define CSR_PMPADDR13       0x3bd
#define CSR_PMPADDR14       0x3be
#define CSR_PMPADDR15       0x3bf

/* Debug/Trace Registers (shared with Debug Mode) */
#define CSR_TSELECT         0x7a0
#define CSR_TDATA1          0x7a1
#define CSR_TDATA2          0x7a2
#define CSR_TDATA3          0x7a3
#define CSR_TINFO           0x7a4
#define CSR_MCONTEXT        0x7a8

/* Debug Mode Registers */
#define CSR_DCSR            0x7b0
#define CSR_DPC             0x7b1
#define CSR_DSCRATCH        0x7b2

/* Performance Counters */
#define CSR_MHPMCOUNTER3    0xb03
#define CSR_MHPMCOUNTER4    0xb04
#define CSR_MHPMCOUNTER5    0xb05
#define CSR_MHPMCOUNTER6    0xb06
#define CSR_MHPMCOUNTER7    0xb07
#define CSR_MHPMCOUNTER8    0xb08
#define CSR_MHPMCOUNTER9    0xb09
#define CSR_MHPMCOUNTER10   0xb0a
#define CSR_MHPMCOUNTER11   0xb0b
#define CSR_MHPMCOUNTER12   0xb0c
#define CSR_MHPMCOUNTER13   0xb0d
#define CSR_MHPMCOUNTER14   0xb0e
#define CSR_MHPMCOUNTER15   0xb0f
#define CSR_MHPMCOUNTER16   0xb10
#define CSR_MHPMCOUNTER17   0xb11
#define CSR_MHPMCOUNTER18   0xb12
#define CSR_MHPMCOUNTER19   0xb13
#define CSR_MHPMCOUNTER20   0xb14
#define CSR_MHPMCOUNTER21   0xb15
#define CSR_MHPMCOUNTER22   0xb16
#define CSR_MHPMCOUNTER23   0xb17
#define CSR_MHPMCOUNTER24   0xb18
#define CSR_MHPMCOUNTER25   0xb19
#define CSR_MHPMCOUNTER26   0xb1a
#define CSR_MHPMCOUNTER27   0xb1b
#define CSR_MHPMCOUNTER28   0xb1c
#define CSR_MHPMCOUNTER29   0xb1d
#define CSR_MHPMCOUNTER30   0xb1e
#define CSR_MHPMCOUNTER31   0xb1f

/* Machine counter-inhibit register */
#define CSR_MCOUNTINHIBIT   0x320

/* Machine counter configuration registers */
#define CSR_MCYCLECFG       0x321
#define CSR_MINSTRETCFG     0x322

#define CSR_MHPMEVENT3      0x323
#define CSR_MHPMEVENT4      0x324
#define CSR_MHPMEVENT5      0x325
#define CSR_MHPMEVENT6      0x326
#define CSR_MHPMEVENT7      0x327
#define CSR_MHPMEVENT8      0x328
#define CSR_MHPMEVENT9      0x329
#define CSR_MHPMEVENT10     0x32a
#define CSR_MHPMEVENT11     0x32b
#define CSR_MHPMEVENT12     0x32c
#define CSR_MHPMEVENT13     0x32d
#define CSR_MHPMEVENT14     0x32e
#define CSR_MHPMEVENT15     0x32f
#define CSR_MHPMEVENT16     0x330
#define CSR_MHPMEVENT17     0x331
#define CSR_MHPMEVENT18     0x332
#define CSR_MHPMEVENT19     0x333
#define CSR_MHPMEVENT20     0x334
#define CSR_MHPMEVENT21     0x335
#define CSR_MHPMEVENT22     0x336
#define CSR_MHPMEVENT23     0x337
#define CSR_MHPMEVENT24     0x338
#define CSR_MHPMEVENT25     0x339
#define CSR_MHPMEVENT26     0x33a
#define CSR_MHPMEVENT27     0x33b
#define CSR_MHPMEVENT28     0x33c
#define CSR_MHPMEVENT29     0x33d
#define CSR_MHPMEVENT30     0x33e
#define CSR_MHPMEVENT31     0x33f

#define CSR_MCYCLECFGH      0x721
#define CSR_MINSTRETCFGH    0x722

#define CSR_MHPMEVENT3H     0x723
#define CSR_MHPMEVENT4H     0x724
#define CSR_MHPMEVENT5H     0x725
#define CSR_MHPMEVENT6H     0x726
#define CSR_MHPMEVENT7H     0x727
#define CSR_MHPMEVENT8H     0x728
#define CSR_MHPMEVENT9H     0x729
#define CSR_MHPMEVENT10H    0x72a
#define CSR_MHPMEVENT11H    0x72b
#define CSR_MHPMEVENT12H    0x72c
#define CSR_MHPMEVENT13H    0x72d
#define CSR_MHPMEVENT14H    0x72e
#define CSR_MHPMEVENT15H    0x72f
#define CSR_MHPMEVENT16H    0x730
#define CSR_MHPMEVENT17H    0x731
#define CSR_MHPMEVENT18H    0x732
#define CSR_MHPMEVENT19H    0x733
#define CSR_MHPMEVENT20H    0x734
#define CSR_MHPMEVENT21H    0x735
#define CSR_MHPMEVENT22H    0x736
#define CSR_MHPMEVENT23H    0x737
#define CSR_MHPMEVENT24H    0x738
#define CSR_MHPMEVENT25H    0x739
#define CSR_MHPMEVENT26H    0x73a
#define CSR_MHPMEVENT27H    0x73b
#define CSR_MHPMEVENT28H    0x73c
#define CSR_MHPMEVENT29H    0x73d
#define CSR_MHPMEVENT30H    0x73e
#define CSR_MHPMEVENT31H    0x73f

#define CSR_MHPMCOUNTER3H   0xb83
#define CSR_MHPMCOUNTER4H   0xb84
#define CSR_MHPMCOUNTER5H   0xb85
#define CSR_MHPMCOUNTER6H   0xb86
#define CSR_MHPMCOUNTER7H   0xb87
#define CSR_MHPMCOUNTER8H   0xb88
#define CSR_MHPMCOUNTER9H   0xb89
#define CSR_MHPMCOUNTER10H  0xb8a
#define CSR_MHPMCOUNTER11H  0xb8b
#define CSR_MHPMCOUNTER12H  0xb8c
#define CSR_MHPMCOUNTER13H  0xb8d
#define CSR_MHPMCOUNTER14H  0xb8e
#define CSR_MHPMCOUNTER15H  0xb8f
#define CSR_MHPMCOUNTER16H  0xb90
#define CSR_MHPMCOUNTER17H  0xb91
#define CSR_MHPMCOUNTER18H  0xb92
#define CSR_MHPMCOUNTER19H  0xb93
#define CSR_MHPMCOUNTER20H  0xb94
#define CSR_MHPMCOUNTER21H  0xb95
#define CSR_MHPMCOUNTER22H  0xb96
#define CSR_MHPMCOUNTER23H  0xb97
#define CSR_MHPMCOUNTER24H  0xb98
#define CSR_MHPMCOUNTER25H  0xb99
#define CSR_MHPMCOUNTER26H  0xb9a
#define CSR_MHPMCOUNTER27H  0xb9b
#define CSR_MHPMCOUNTER28H  0xb9c
#define CSR_MHPMCOUNTER29H  0xb9d
#define CSR_MHPMCOUNTER30H  0xb9e
#define CSR_MHPMCOUNTER31H  0xb9f

#define CSR_SCOUNTOVF       0xda0

/* Crypto Extension */
#define CSR_SEED            0x015

/* Zcmt Extension */
#define CSR_JVT             0x017

/* mstatus CSR bits */
#define MSTATUS_UIE         0x00000001
#define MSTATUS_SIE         0x00000002
#define MSTATUS_MIE         0x00000008
#define MSTATUS_UPIE        0x00000010
#define MSTATUS_SPIE        0x00000020
#define MSTATUS_UBE         0x00000040
#define MSTATUS_MPIE        0x00000080
#define MSTATUS_SPP         0x00000100
#define MSTATUS_VS          0x00000600
#define MSTATUS_MPP         0x00001800
#define MSTATUS_FS          0x00006000
#define MSTATUS_XS          0x00018000
#define MSTATUS_MPRV        0x00020000
#define MSTATUS_SUM         0x00040000 /* since: priv-1.10 */
#define MSTATUS_MXR         0x00080000
#define MSTATUS_TVM         0x00100000 /* since: priv-1.10 */
#define MSTATUS_TW          0x00200000 /* since: priv-1.10 */
#define MSTATUS_TSR         0x00400000 /* since: priv-1.10 */
#define MSTATUS_SPELP       0x00800000
#define MSTATUS_TH_MS       0x06000000 /* since: priv-1.10 */
#define MSTATUS_SDT         0x01000000 /* Ssdbltrp extension */
#define MSTATUS_GVA         0x4000000000ULL
#define MSTATUS_MPV         0x8000000000ULL
#define MSTATUS_MPELP       0x20000000000ULL
#define MSTATUS_MDT         0x40000000000ULL /* Smdbltrp extension */

#define MSTATUS64_UXL       0x0000000300000000ULL
#define MSTATUS64_SXL       0x0000000C00000000ULL

#define MSTATUS32_SD        0x80000000
#define MSTATUS64_SD        0x8000000000000000ULL
#define MSTATUSH128_SD      0x8000000000000000ULL

#define MISA32_MXL          0xC0000000
#define MISA64_MXL          0xC000000000000000ULL

typedef enum {
    MXL_RV32  = 1,
    MXL_RV64  = 2,
    MXL_RV128 = 3,
} RISCVMXL;

/* sstatus CSR bits */
#define SSTATUS_UIE         0x00000001
#define SSTATUS_SIE         0x00000002
#define SSTATUS_UPIE        0x00000010
#define SSTATUS_SPIE        0x00000020
#define SSTATUS_SPP         0x00000100
#define SSTATUS_VS          0x00000600
#define SSTATUS_FS          0x00006000
#define SSTATUS_XS          0x00018000
#define SSTATUS_SUM         0x00040000 /* since: priv-1.10 */
#define SSTATUS_MXR         0x00080000
#define SSTATUS_SPELP       MSTATUS_SPELP
#define SSTATUS_SDT         0x01000000 /* Ssdbltrp extension */

#define SSTATUS64_UXL       0x0000000300000000ULL

#define SSTATUS32_SD        0x80000000
#define SSTATUS64_SD        0x8000000000000000ULL

/* hstatus CSR bits */
#define HSTATUS_VSBE         0x00000020
#define HSTATUS_GVA          0x00000040
#define HSTATUS_SPV          0x00000080
#define HSTATUS_SPVP         0x00000100
#define HSTATUS_HU           0x00000200
#define HSTATUS_VGEIN        0x0003F000
#define HSTATUS_VTVM         0x00100000
#define HSTATUS_VTW          0x00200000
#define HSTATUS_VTSR         0x00400000
#define HSTATUS_VSXL         0x300000000

#define HSTATUS32_WPRI       0xFF8FF87E
#define HSTATUS64_WPRI       0xFFFFFFFFFF8FF87EULL

#define COUNTEREN_CY         (1 << 0)
#define COUNTEREN_TM         (1 << 1)
#define COUNTEREN_IR         (1 << 2)
#define COUNTEREN_HPM3       (1 << 3)

/* vsstatus CSR bits */
#define VSSTATUS64_UXL       0x0000000300000000ULL
#define VSSTATUS_SPELP       MSTATUS_SPELP

/* Privilege modes */
#define PRV_U 0
#define PRV_S 1
#define PRV_RESERVED 2
#define PRV_M 3

/* RV32 satp CSR field masks */
#define SATP32_MODE         0x80000000
#define SATP32_ASID         0x7fc00000
#define SATP32_PPN          0x003fffff

/* RV64 satp CSR field masks */
#define SATP64_MODE         0xF000000000000000ULL
#define SATP64_ASID         0x0FFFF00000000000ULL
#define SATP64_PPN          0x00000FFFFFFFFFFFULL

/* VM modes (satp.mode) privileged ISA 1.10 */
#define VM_1_10_MBARE       0
#define VM_1_10_SV32        1
#define VM_1_10_SV39        8
#define VM_1_10_SV48        9
#define VM_1_10_SV57        10
#define VM_1_10_SV64        11

/* Page table entry (PTE) fields */
#define PTE_V               0x001 /* Valid */
#define PTE_R               0x002 /* Read */
#define PTE_W               0x004 /* Write */
#define PTE_X               0x008 /* Execute */
#define PTE_U               0x010 /* User */
#define PTE_G               0x020 /* Global */
#define PTE_A               0x040 /* Accessed */
#define PTE_D               0x080 /* Dirty */
#define PTE_SOFT            0x300 /* Reserved for Software */
#define PTE_PBMT            0x6000000000000000ULL /* Page-based memory types */
#define PTE_N               0x8000000000000000ULL /* NAPOT translation */
#define PTE_RESERVED        0x1FC0000000000000ULL /* Reserved bits */
#define PTE_ATTR            (PTE_N | PTE_PBMT) /* All attributes bits */

/* Page table PPN shift amount */
#define PTE_PPN_SHIFT       10

/* Page table PPN mask */
#define PTE_PPN_MASK        0x3FFFFFFFFFFC00ULL

/* Leaf page shift amount */
#define PGSHIFT             12

/* Default Reset Vector address */
#define DEFAULT_RSTVEC      0x1000

/* Exception causes */
typedef enum RISCVException {
    RISCV_EXCP_NONE = -1, /* sentinel value */
    RISCV_EXCP_INST_ADDR_MIS = 0x0,
    RISCV_EXCP_INST_ACCESS_FAULT = 0x1,
    RISCV_EXCP_ILLEGAL_INST = 0x2,
    RISCV_EXCP_BREAKPOINT = 0x3,
    RISCV_EXCP_LOAD_ADDR_MIS = 0x4,
    RISCV_EXCP_LOAD_ACCESS_FAULT = 0x5,
    RISCV_EXCP_STORE_AMO_ADDR_MIS = 0x6,
    RISCV_EXCP_STORE_AMO_ACCESS_FAULT = 0x7,
    RISCV_EXCP_U_ECALL = 0x8,
    RISCV_EXCP_S_ECALL = 0x9,
    RISCV_EXCP_VS_ECALL = 0xa,
    RISCV_EXCP_M_ECALL = 0xb,
    RISCV_EXCP_INST_PAGE_FAULT = 0xc, /* since: priv-1.10.0 */
    RISCV_EXCP_LOAD_PAGE_FAULT = 0xd, /* since: priv-1.10.0 */
    RISCV_EXCP_STORE_PAGE_FAULT = 0xf, /* since: priv-1.10.0 */
    RISCV_EXCP_SW_CHECK = 0x12, /* since: priv-1.13.0 */
    RISCV_EXCP_HW_ERR = 0x13, /* since: priv-1.13.0 */
    RISCV_EXCP_DOUBLE_TRAP = 0x10, /* Ssdbltrp extension */
    RISCV_EXCP_INST_GUEST_PAGE_FAULT = 0x14,
    RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT = 0x15,
    RISCV_EXCP_VIRT_INSTRUCTION_FAULT = 0x16,
    RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT = 0x17,
    RISCV_EXCP_SEMIHOST = 0x3f,
} RISCVException;

#define RISCV_EXCP_SW_CHECK_FCFI_VIOLATION_CODE 2
#define RISCV_EXCP_SW_CHECK_BCFI_VIOLATION_CODE 3

#define RISCV_EXCP_INT_CLIC                0x40000000
#define RISCV_EXCP_INT_FLAG                0x80000000
#define RISCV_EXCP_INT_MASK                0x7fffffff

/* Interrupt causes */
#define IRQ_U_SOFT                         0
#define IRQ_S_SOFT                         1
#define IRQ_VS_SOFT                        2
#define IRQ_M_SOFT                         3
#define IRQ_U_TIMER                        4
#define IRQ_S_TIMER                        5
#define IRQ_VS_TIMER                       6
#define IRQ_M_TIMER                        7
#define IRQ_U_EXT                          8
#define IRQ_S_EXT                          9
#define IRQ_VS_EXT                         10
#define IRQ_M_EXT                          11
#define IRQ_S_GEXT                         12
#define IRQ_PMU_OVF                        13
#define IRQ_LOCAL_MAX                      16
#define IRQ_LOCAL_GUEST_MAX                (TARGET_LONG_BITS - 1)

/* mip masks */
#define MIP_USIP                           (1 << IRQ_U_SOFT)
#define MIP_SSIP                           (1 << IRQ_S_SOFT)
#define MIP_VSSIP                          (1 << IRQ_VS_SOFT)
#define MIP_MSIP                           (1 << IRQ_M_SOFT)
#define MIP_UTIP                           (1 << IRQ_U_TIMER)
#define MIP_STIP                           (1 << IRQ_S_TIMER)
#define MIP_VSTIP                          (1 << IRQ_VS_TIMER)
#define MIP_MTIP                           (1 << IRQ_M_TIMER)
#define MIP_UEIP                           (1 << IRQ_U_EXT)
#define MIP_SEIP                           (1 << IRQ_S_EXT)
#define MIP_VSEIP                          (1 << IRQ_VS_EXT)
#define MIP_MEIP                           (1 << IRQ_M_EXT)
#define MIP_SGEIP                          (1 << IRQ_S_GEXT)
#define MIP_LCOFIP                         (1 << IRQ_PMU_OVF)

/* sip masks */
#define SIP_SSIP                           MIP_SSIP
#define SIP_STIP                           MIP_STIP
#define SIP_SEIP                           MIP_SEIP
#define SIP_LCOFIP                         MIP_LCOFIP

/* MIE masks */
#define MIE_SEIE                           (1 << IRQ_S_EXT)
#define MIE_UEIE                           (1 << IRQ_U_EXT)
#define MIE_STIE                           (1 << IRQ_S_TIMER)
#define MIE_UTIE                           (1 << IRQ_U_TIMER)
#define MIE_SSIE                           (1 << IRQ_S_SOFT)
#define MIE_USIE                           (1 << IRQ_U_SOFT)

/* Machine constants */
#define M_MODE_INTERRUPTS  ((uint64_t)(MIP_MSIP | MIP_MTIP | MIP_MEIP))
#define S_MODE_INTERRUPTS  ((uint64_t)(MIP_SSIP | MIP_STIP | MIP_SEIP))
#define VS_MODE_INTERRUPTS ((uint64_t)(MIP_VSSIP | MIP_VSTIP | MIP_VSEIP))
#define HS_MODE_INTERRUPTS ((uint64_t)(MIP_SGEIP | VS_MODE_INTERRUPTS))

/* Execution environment configuration bits */
#define MENVCFG_FIOM                       BIT(0)
#define MENVCFG_LPE                        BIT(2)
#define MENVCFG_SSE                        BIT(3)
#define MENVCFG_CBIE                       (3UL << 4)
#define MENVCFG_CBCFE                      BIT(6)
#define MENVCFG_CBZE                       BIT(7)
#define MENVCFG_CDE                        (1ULL << 60)
#define MENVCFG_PMM                        (3ULL << 32)
#define MENVCFG_DTE                        (1ULL << 59) /* Ssdbltrp extension */
#define MENVCFG_ADUE                       (1ULL << 61)
#define MENVCFG_PBMTE                      (1ULL << 62)
#define MENVCFG_STCE                       (1ULL << 63)

/* For RV32 */
#define MENVCFGH_DTE                       BIT(27) /* Ssdbltrp extension */
#define MENVCFGH_ADUE                      BIT(29)
#define MENVCFGH_PBMTE                     BIT(30)
#define MENVCFGH_STCE                      BIT(31)

#define SENVCFG_FIOM                       MENVCFG_FIOM
#define SENVCFG_LPE                        MENVCFG_LPE
#define SENVCFG_SSE                        MENVCFG_SSE
#define SENVCFG_CBIE                       MENVCFG_CBIE
#define SENVCFG_CBCFE                      MENVCFG_CBCFE
#define SENVCFG_CBZE                       MENVCFG_CBZE
#define SENVCFG_PMM                        MENVCFG_PMM

#define HENVCFG_FIOM                       MENVCFG_FIOM
#define HENVCFG_LPE                        MENVCFG_LPE
#define HENVCFG_SSE                        MENVCFG_SSE
#define HENVCFG_CBIE                       MENVCFG_CBIE
#define HENVCFG_CBCFE                      MENVCFG_CBCFE
#define HENVCFG_CBZE                       MENVCFG_CBZE
#define HENVCFG_PMM                        MENVCFG_PMM
#define HENVCFG_DTE                        MENVCFG_DTE
#define HENVCFG_ADUE                       MENVCFG_ADUE
#define HENVCFG_PBMTE                      MENVCFG_PBMTE
#define HENVCFG_STCE                       MENVCFG_STCE

/* For RV32 */
#define HENVCFGH_DTE                        MENVCFGH_DTE
#define HENVCFGH_ADUE                       MENVCFGH_ADUE
#define HENVCFGH_PBMTE                      MENVCFGH_PBMTE
#define HENVCFGH_STCE                       MENVCFGH_STCE

/* mctrctl CSR bits. */
#define MCTRCTL_U_ENABLE        BIT_ULL(0)
#define MCTRCTL_S_ENABLE        BIT_ULL(1)
#define MCTRCTL_M_ENABLE        BIT_ULL(2)
#define MCTRCTL_RASEMU          BIT_ULL(7)
#define MCTRCTL_STE             BIT_ULL(8)
#define MCTRCTL_MTE             BIT_ULL(9)
#define MCTRCTL_BPFRZ           BIT_ULL(11)
#define MCTRCTL_LCOFIFRZ        BIT_ULL(12)
#define MCTRCTL_EXCINH          BIT_ULL(33)
#define MCTRCTL_INTRINH         BIT_ULL(34)
#define MCTRCTL_TRETINH         BIT_ULL(35)
#define MCTRCTL_NTBREN          BIT_ULL(36)
#define MCTRCTL_TKBRINH         BIT_ULL(37)
#define MCTRCTL_INDCALL_INH     BIT_ULL(40)
#define MCTRCTL_DIRCALL_INH     BIT_ULL(41)
#define MCTRCTL_INDJUMP_INH     BIT_ULL(42)
#define MCTRCTL_DIRJUMP_INH     BIT_ULL(43)
#define MCTRCTL_CORSWAP_INH     BIT_ULL(44)
#define MCTRCTL_RET_INH         BIT_ULL(45)
#define MCTRCTL_INDOJUMP_INH    BIT_ULL(46)
#define MCTRCTL_DIROJUMP_INH    BIT_ULL(47)

#define MCTRCTL_INH_START       32U

#define MCTRCTL_MASK (MCTRCTL_M_ENABLE | MCTRCTL_S_ENABLE |       \
                      MCTRCTL_U_ENABLE | MCTRCTL_RASEMU |         \
                      MCTRCTL_MTE | MCTRCTL_STE |                 \
                      MCTRCTL_BPFRZ | MCTRCTL_LCOFIFRZ |          \
                      MCTRCTL_EXCINH | MCTRCTL_INTRINH |          \
                      MCTRCTL_TRETINH | MCTRCTL_NTBREN |          \
                      MCTRCTL_TKBRINH | MCTRCTL_INDCALL_INH |     \
                      MCTRCTL_DIRCALL_INH | MCTRCTL_INDJUMP_INH | \
                      MCTRCTL_DIRJUMP_INH | MCTRCTL_CORSWAP_INH | \
                      MCTRCTL_RET_INH | MCTRCTL_INDOJUMP_INH |    \
                      MCTRCTL_DIROJUMP_INH)

/* sctrctl CSR bits. */
#define SCTRCTL_U_ENABLE          MCTRCTL_U_ENABLE
#define SCTRCTL_S_ENABLE          MCTRCTL_S_ENABLE
#define SCTRCTL_RASEMU            MCTRCTL_RASEMU
#define SCTRCTL_STE               MCTRCTL_STE
#define SCTRCTL_BPFRZ             MCTRCTL_BPFRZ
#define SCTRCTL_LCOFIFRZ          MCTRCTL_LCOFIFRZ
#define SCTRCTL_EXCINH            MCTRCTL_EXCINH
#define SCTRCTL_INTRINH           MCTRCTL_INTRINH
#define SCTRCTL_TRETINH           MCTRCTL_TRETINH
#define SCTRCTL_NTBREN            MCTRCTL_NTBREN
#define SCTRCTL_TKBRINH           MCTRCTL_TKBRINH
#define SCTRCTL_INDCALL_INH       MCTRCTL_INDCALL_INH
#define SCTRCTL_DIRCALL_INH       MCTRCTL_DIRCALL_INH
#define SCTRCTL_INDJUMP_INH       MCTRCTL_INDJUMP_INH
#define SCTRCTL_DIRJUMP_INH       MCTRCTL_DIRJUMP_INH
#define SCTRCTL_CORSWAP_INH       MCTRCTL_CORSWAP_INH
#define SCTRCTL_RET_INH           MCTRCTL_RET_INH
#define SCTRCTL_INDOJUMP_INH      MCTRCTL_INDOJUMP_INH
#define SCTRCTL_DIROJUMP_INH      MCTRCTL_DIROJUMP_INH

#define SCTRCTL_MASK (SCTRCTL_S_ENABLE | SCTRCTL_U_ENABLE |       \
                      SCTRCTL_RASEMU | SCTRCTL_STE |              \
                      SCTRCTL_BPFRZ | SCTRCTL_LCOFIFRZ |          \
                      SCTRCTL_EXCINH | SCTRCTL_INTRINH |          \
                      SCTRCTL_TRETINH | SCTRCTL_NTBREN |          \
                      SCTRCTL_TKBRINH | SCTRCTL_INDCALL_INH |     \
                      SCTRCTL_DIRCALL_INH | SCTRCTL_INDJUMP_INH | \
                      SCTRCTL_DIRJUMP_INH | SCTRCTL_CORSWAP_INH | \
                      SCTRCTL_RET_INH | SCTRCTL_INDOJUMP_INH |    \
                      SCTRCTL_DIROJUMP_INH)

/* sctrstatus CSR bits. */
#define SCTRSTATUS_WRPTR_MASK       0xFF
#define SCTRSTATUS_FROZEN           BIT(31)
#define SCTRSTATUS_MASK             (SCTRSTATUS_WRPTR_MASK | SCTRSTATUS_FROZEN)

/* sctrdepth CSR bits. */
#define SCTRDEPTH_MASK              0x7
#define SCTRDEPTH_MIN               0U  /* 16 Entries. */
#define SCTRDEPTH_MAX               4U  /* 256 Entries. */

/* vsctrctl CSR bits. */
#define VSCTRCTL_VU_ENABLE         MCTRCTL_U_ENABLE
#define VSCTRCTL_VS_ENABLE         MCTRCTL_S_ENABLE
#define VSCTRCTL_RASEMU            MCTRCTL_RASEMU
#define VSCTRCTL_VSTE              MCTRCTL_STE
#define VSCTRCTL_BPFRZ             MCTRCTL_BPFRZ
#define VSCTRCTL_LCOFIFRZ          MCTRCTL_LCOFIFRZ
#define VSCTRCTL_EXCINH            MCTRCTL_EXCINH
#define VSCTRCTL_INTRINH           MCTRCTL_INTRINH
#define VSCTRCTL_TRETINH           MCTRCTL_TRETINH
#define VSCTRCTL_NTBREN            MCTRCTL_NTBREN
#define VSCTRCTL_TKBRINH           MCTRCTL_TKBRINH
#define VSCTRCTL_INDCALL_INH       MCTRCTL_INDCALL_INH
#define VSCTRCTL_DIRCALL_INH       MCTRCTL_DIRCALL_INH
#define VSCTRCTL_INDJUMP_INH       MCTRCTL_INDJUMP_INH
#define VSCTRCTL_DIRJUMP_INH       MCTRCTL_DIRJUMP_INH
#define VSCTRCTL_CORSWAP_INH       MCTRCTL_CORSWAP_INH
#define VSCTRCTL_RET_INH           MCTRCTL_RET_INH
#define VSCTRCTL_INDOJUMP_INH      MCTRCTL_INDOJUMP_INH
#define VSCTRCTL_DIROJUMP_INH      MCTRCTL_DIROJUMP_INH

#define VSCTRCTL_MASK (VSCTRCTL_VS_ENABLE | VSCTRCTL_VU_ENABLE |     \
                       VSCTRCTL_RASEMU | VSCTRCTL_VSTE |             \
                       VSCTRCTL_BPFRZ | VSCTRCTL_LCOFIFRZ |          \
                       VSCTRCTL_EXCINH | VSCTRCTL_INTRINH |          \
                       VSCTRCTL_TRETINH | VSCTRCTL_NTBREN |          \
                       VSCTRCTL_TKBRINH | VSCTRCTL_INDCALL_INH |     \
                       VSCTRCTL_DIRCALL_INH | VSCTRCTL_INDJUMP_INH | \
                       VSCTRCTL_DIRJUMP_INH | VSCTRCTL_CORSWAP_INH | \
                       VSCTRCTL_RET_INH | VSCTRCTL_INDOJUMP_INH |    \
                       VSCTRCTL_DIROJUMP_INH)

#define CTR_ENTRIES_FIRST                  0x200
#define CTR_ENTRIES_LAST                   0x2ff

#define CTRSOURCE_VALID                    BIT(0)
#define CTRTARGET_MISP                     BIT(0)

#define CTRDATA_TYPE_MASK                   0xF
#define CTRDATA_CCV                         BIT(15)
#define CTRDATA_CCM_MASK                    0xFFF0000
#define CTRDATA_CCE_MASK                    0xF0000000

#define CTRDATA_MASK            (CTRDATA_TYPE_MASK | CTRDATA_CCV |  \
                                 CTRDATA_CCM_MASK | CTRDATA_CCE_MASK)

#define CTRDATA_TYPE_NONE                   0
#define CTRDATA_TYPE_EXCEPTION              1
#define CTRDATA_TYPE_INTERRUPT              2
#define CTRDATA_TYPE_EXCEP_INT_RET          3
#define CTRDATA_TYPE_NONTAKEN_BRANCH        4
#define CTRDATA_TYPE_TAKEN_BRANCH           5
#define CTRDATA_TYPE_RESERVED_0             6
#define CTRDATA_TYPE_RESERVED_1             7
#define CTRDATA_TYPE_INDIRECT_CALL          8
#define CTRDATA_TYPE_DIRECT_CALL            9
#define CTRDATA_TYPE_INDIRECT_JUMP          10
#define CTRDATA_TYPE_DIRECT_JUMP            11
#define CTRDATA_TYPE_CO_ROUTINE_SWAP        12
#define CTRDATA_TYPE_RETURN                 13
#define CTRDATA_TYPE_OTHER_INDIRECT_JUMP    14
#define CTRDATA_TYPE_OTHER_DIRECT_JUMP      15

/* MISELECT, SISELECT, and VSISELECT bits (AIA) */
#define ISELECT_IPRIO0                     0x30
#define ISELECT_IPRIO15                    0x3f
#define ISELECT_IMSIC_EIDELIVERY           0x70
#define ISELECT_IMSIC_EITHRESHOLD          0x72
#define ISELECT_IMSIC_EIP0                 0x80
#define ISELECT_IMSIC_EIP63                0xbf
#define ISELECT_IMSIC_EIE0                 0xc0
#define ISELECT_IMSIC_EIE63                0xff
#define ISELECT_IMSIC_FIRST                ISELECT_IMSIC_EIDELIVERY
#define ISELECT_IMSIC_LAST                 ISELECT_IMSIC_EIE63
#define ISELECT_MASK_AIA                   0x1ff

/* [M|S|VS]SELCT value for Indirect CSR Access Extension */
#define ISELECT_CD_FIRST                   0x40
#define ISELECT_CD_LAST                    0x5f
#define ISELECT_MASK_SXCSRIND              0xfff

/* Dummy [M|S|VS]ISELECT value for emulating [M|S|VS]TOPEI CSRs */
#define ISELECT_IMSIC_TOPEI                (ISELECT_MASK_AIA + 1)

/* IMSIC bits (AIA) */
#define IMSIC_TOPEI_IID_SHIFT              16
#define IMSIC_TOPEI_IID_MASK               0x7ff
#define IMSIC_TOPEI_IPRIO_MASK             0x7ff
#define IMSIC_EIPx_BITS                    32
#define IMSIC_EIEx_BITS                    32

/* MTOPI and STOPI bits (AIA) */
#define TOPI_IID_SHIFT                     16
#define TOPI_IID_MASK                      0xfff
#define TOPI_IPRIO_MASK                    0xff

/* Interrupt priority bits (AIA) */
#define IPRIO_IRQ_BITS                     8
#define IPRIO_MMAXIPRIO                    255
#define IPRIO_DEFAULT_UPPER                4
#define IPRIO_DEFAULT_MIDDLE               (IPRIO_DEFAULT_UPPER + 12)
#define IPRIO_DEFAULT_M                    IPRIO_DEFAULT_MIDDLE
#define IPRIO_DEFAULT_S                    (IPRIO_DEFAULT_M + 3)
#define IPRIO_DEFAULT_SGEXT                (IPRIO_DEFAULT_S + 3)
#define IPRIO_DEFAULT_VS                   (IPRIO_DEFAULT_SGEXT + 1)
#define IPRIO_DEFAULT_LOWER                (IPRIO_DEFAULT_VS + 3)

/* HVICTL bits (AIA) */
#define HVICTL_VTI                         0x40000000
#define HVICTL_IID                         0x0fff0000
#define HVICTL_IPRIOM                      0x00000100
#define HVICTL_IPRIO                       0x000000ff
#define HVICTL_VALID_MASK                  \
    (HVICTL_VTI | HVICTL_IID | HVICTL_IPRIOM | HVICTL_IPRIO)

/* seed CSR bits */
#define SEED_OPST                        (0b11 << 30)
#define SEED_OPST_BIST                   (0b00 << 30)
#define SEED_OPST_WAIT                   (0b01 << 30)
#define SEED_OPST_ES16                   (0b10 << 30)
#define SEED_OPST_DEAD                   (0b11 << 30)
/* PMU related bits */
#define MIE_LCOFIE                         (1 << IRQ_PMU_OVF)

#define MCYCLECFG_BIT_MINH                 BIT_ULL(62)
#define MCYCLECFGH_BIT_MINH                BIT(30)
#define MCYCLECFG_BIT_SINH                 BIT_ULL(61)
#define MCYCLECFGH_BIT_SINH                BIT(29)
#define MCYCLECFG_BIT_UINH                 BIT_ULL(60)
#define MCYCLECFGH_BIT_UINH                BIT(28)
#define MCYCLECFG_BIT_VSINH                BIT_ULL(59)
#define MCYCLECFGH_BIT_VSINH               BIT(27)
#define MCYCLECFG_BIT_VUINH                BIT_ULL(58)
#define MCYCLECFGH_BIT_VUINH               BIT(26)

#define MINSTRETCFG_BIT_MINH               BIT_ULL(62)
#define MINSTRETCFGH_BIT_MINH              BIT(30)
#define MINSTRETCFG_BIT_SINH               BIT_ULL(61)
#define MINSTRETCFGH_BIT_SINH              BIT(29)
#define MINSTRETCFG_BIT_UINH               BIT_ULL(60)
#define MINSTRETCFGH_BIT_UINH              BIT(28)
#define MINSTRETCFG_BIT_VSINH              BIT_ULL(59)
#define MINSTRETCFGH_BIT_VSINH             BIT(27)
#define MINSTRETCFG_BIT_VUINH              BIT_ULL(58)
#define MINSTRETCFGH_BIT_VUINH             BIT(26)

#define MHPMEVENT_BIT_OF                   BIT_ULL(63)
#define MHPMEVENTH_BIT_OF                  BIT(31)
#define MHPMEVENT_BIT_MINH                 BIT_ULL(62)
#define MHPMEVENTH_BIT_MINH                BIT(30)
#define MHPMEVENT_BIT_SINH                 BIT_ULL(61)
#define MHPMEVENTH_BIT_SINH                BIT(29)
#define MHPMEVENT_BIT_UINH                 BIT_ULL(60)
#define MHPMEVENTH_BIT_UINH                BIT(28)
#define MHPMEVENT_BIT_VSINH                BIT_ULL(59)
#define MHPMEVENTH_BIT_VSINH               BIT(27)
#define MHPMEVENT_BIT_VUINH                BIT_ULL(58)
#define MHPMEVENTH_BIT_VUINH               BIT(26)

#define MHPMEVENT_FILTER_MASK              (MHPMEVENT_BIT_MINH  | \
                                            MHPMEVENT_BIT_SINH  | \
                                            MHPMEVENT_BIT_UINH  | \
                                            MHPMEVENT_BIT_VSINH | \
                                            MHPMEVENT_BIT_VUINH)
#define MHPMEVENT_SSCOF_MASK               _ULL(0xFFFF000000000000)
#define MHPMEVENT_IDX_MASK                 0xFFFFF
#define MHPMEVENT_SSCOF_RESVD              16

/* JVT CSR bits */
#define JVT_MODE                           0x3F
#define JVT_BASE                           (~0x3F)

/* Debug Sdtrig CSR masks */
#define MCONTEXT32                         0x0000003F
#define MCONTEXT64                         0x0000000000001FFFULL
#define MCONTEXT32_HCONTEXT                0x0000007F
#define MCONTEXT64_HCONTEXT                0x0000000000003FFFULL

/* Xuantie custom CSRs */
#define TH_MSTATUS_VS     0x01800000

#define TH_FSR_VXRM_SHIFT      9
#define TH_FSR_VXRM            (0x3 << TH_FSR_VXRM_SHIFT)

#define TH_FSR_VXSAT_SHIFT     8
#define TH_FSR_VXSAT           (0x1 << TH_FSR_VXSAT_SHIFT)

#define TH_VTYPE_LMUL_SHIFT    0
#define TH_VTYPE_LMUL          (0x3 << TH_VTYPE_LMUL_SHIFT)

#define TH_VTYPE_SEW_SHIFT     2
#define TH_VTYPE_SEW           (0x7 << TH_VTYPE_SEW_SHIFT)

#define TH_VTYPE_CLEAR_SHIFT   5
#define TH_VTYPE_CLEAR         (0x7 << TH_VTYPE_CLEAR_SHIFT)

/* Matrix CSR */
#define CSR_MRSTART         0x801
#define CSR_MCSR            0x802
#define CSR_MSIZE           0x803
#define CSR_MREGSIZE        0xCC0
#define CSR_MLENB           0xCC1
#define CSR_XMISA           0xCC2

#define MXSTATUS_MSD        0x1

#define MEXSTATUS_EXPT      0x20
#define MEXSTATUS_SPSWAP    0x20000

/* Matrix Sub Extension */
#define MATRIX_PW_FLOAT         0x80000000
#define MATRIX_SPARSITY_FLOAT   0x40000000
#define MATRIX_FLOAT_INT_CVT    0x20000000
#define MATRIX_PW_INT           0x10000000
#define MATRIX_SPARSITY_INT     0x8000000
#define MATRIX_MULT_F8F32       0x200
#define MATRIX_MULT_F8F16       0x100
#define MATRIX_MULT_F32F64      0x80
#define MATRIX_MULT_F16F32      0x40
#define MATRIX_MULT_F64F64      0x20
#define MATRIX_MULT_F32F32      0x10
#define MATRIX_MULT_F16F16      0x8
#define MATRIX_MULT_I16I64      0x4
#define MATRIX_MULT_I8I32       0x2
#define MATRIX_MULT_I4I32       0x1

/* Matrix Status */
#define MCSR_RM            0x3
#define MCSR_SAT           0x4
#define MCSR_FFLAGS        0xf8
#define MCSR_FRM           0x700

/* Xuantie CSR */

/* C908x CSR */
#define CSR_MTNADDR2 0x7e5
#define CSR_MTNCR 0x7e6
#define CSR_MTNSR 0x7e7
#define CSR_MTNER 0x7e8
#define CSR_MTNADDR 0x7e9
#define CSR_MDEBUG_TN_PC 0x7cf
#define CSR_MFASTM          0x7eb
#define CSR_TNLOWPOWER 0x7ea

#define CSR_FXCR            0x800

#define CSR_TH_UTNMODE      0x8da
#define TH_UTNMODE_SAT      0x1
#define MFASTM_ENABLE       0x8000000000000000
 
#define CSR_MXSTATUS        0x7c0
#define CSR_MHCR            0x7c1
#define CSR_MCOR            0x7c2
#define CSR_MCCR2           0x7c3
#define CSR_MCER2           0x7c4
#define CSR_MHINT           0x7c5
#define CSR_MRMR            0x7c6
#define CSR_MRVBR           0x7c7
#define CSR_MCER            0x7c8
#define CSR_MCOUNTERWEN     0x7c9
#define CSR_MCOUNTERINTEN   0x7ca
#define CSR_MCOUNTEROF      0x7cb
#define CSR_MHINT2          0x7cc
#define CSR_MHINT3          0x7cd
#define CSR_MHINT4          0x7ce
#define CSR_USP             0x7d1
#define CSR_MCINS           0x7d2
#define CSR_MCINDEX         0x7d3
#define CSR_MCDATA0         0x7d4
#define CSR_MCDATA1         0x7d5
#define CSR_MEICR           0x7d6
#define CSR_MEICR2          0x7d7
#define CSR_MBEADDR         0x7d8
#define CSR_MCPER           0x7d9
#define CSR_MCINDEXH        0x7da
#define CSR_MCDATA0H        0x7db
#define CSR_MCDATA1H        0x7dc
#define CSR_MRADDR          0x7e0
#define CSR_MEXSTATUS       0x7e1
#define CSR_MNMICAUSE       0x7e2
#define CSR_MNMIPC          0x7e3
#define CSR_MHPMCR          0x7f0
#define CSR_MHPMSR          0x7f1
#define CSR_MHPMER          0x7f2
#define CSR_MSMPR           0x7f3
#define CSR_MZONEID         0x7f5
#define CSR_ML2PID          0x7f6
#define CSR_ML2WP           0x7f7
#define CSR_MDTCMCR         0x7f8
#define CSR_MITCMCR         0x7f9
#define CSR_MIESR           0x7fa
#define CSR_MSBEPA          0x7fb
#define CSR_MSBEPA2         0x7fc
#define CSR_ML2WPH          0x7fd
#define CSR_MCERH           0x7fe
#define CSR_MCER2H          0x7ff
#define CSR_CPUID           0xfc0
#define CSR_MAPBADDR        0xfc1
#define CSR_MHALTCAUSE      0xfe0
#define CSR_MDBGINFO        0xfe1
#define CSR_MPCFIFO         0xfe2
#define CSR_MDBGFIFO2       0xfe3
#define CSR_SXSTATUS        0x5c0
#define CSR_SHCR            0x5c1
#define CSR_SCER2           0x5c2
#define CSR_SCER            0x5c3
#define CSR_SCOUNTERINTEN   0x5c4
#define CSR_SCOUNTEROF      0x5c5
#define CSR_SHINT           0x5c6
#define CSR_SHINT2          0x5c7
#define CSR_SHPMINHIBIT     0x5c8
#define CSR_SHPMCR          0x5c9
#define CSR_SHPMSR          0x5ca
#define CSR_SHPMER          0x5cb
#define CSR_SL2PID          0x5cc
#define CSR_SL2WP           0x5cd
#define CSR_SIESR           0x5ce
#define CSR_SL2WPH          0x5cf
#define CSR_SBEADDR         0x5d0
#define CSR_SSBEPA          0x5d1
#define CSR_SSBEPA2         0x5d2
#define CSR_SCERH           0x5d3
#define CSR_SCER2H          0x5d4
#define CSR_CYCLE_C910      0x5e0
#define CSR_SHPMCOUNTER1    0x5e1
#define CSR_SHPMCOUNTER2    0x5e2
#define CSR_SHPMCOUNTER3    0x5e3
#define CSR_SHPMCOUNTER4    0x5e4
#define CSR_SHPMCOUNTER5    0x5e5
#define CSR_SHPMCOUNTER6    0x5e6
#define CSR_SHPMCOUNTER7    0x5e7
#define CSR_SHPMCOUNTER8    0x5e8
#define CSR_SHPMCOUNTER9    0x5e9
#define CSR_SHPMCOUNTER10   0x5ea
#define CSR_SHPMCOUNTER11   0x5eb
#define CSR_SHPMCOUNTER12   0x5ec
#define CSR_SHPMCOUNTER13   0x5ed
#define CSR_SHPMCOUNTER14   0x5ee
#define CSR_SHPMCOUNTER15   0x5ef
#define CSR_SHPMCOUNTER16   0x5f0
#define CSR_SHPMCOUNTER17   0x5f1
#define CSR_SHPMCOUNTER18   0x5f2
#define CSR_SHPMCOUNTER19   0x5f3
#define CSR_SHPMCOUNTER20   0x5f4
#define CSR_SHPMCOUNTER21   0x5f5
#define CSR_SHPMCOUNTER22   0x5f6
#define CSR_SHPMCOUNTER23   0x5f7
#define CSR_SHPMCOUNTER24   0x5f8
#define CSR_SHPMCOUNTER25   0x5f9
#define CSR_SHPMCOUNTER26   0x5fa
#define CSR_SHPMCOUNTER27   0x5fb
#define CSR_SHPMCOUNTER28   0x5fc
#define CSR_SHPMCOUNTER29   0x5fd
#define CSR_SHPMCOUNTER30   0x5fe
#define CSR_SHPMCOUNTER31   0x5ff
#define CSR_SMIR            0x9c0
#define CSR_SMLO0           0x9c1
#define CSR_SMEH            0x9c2
#define CSR_SMCIR           0x9c3

#define CSR_SCYCLEH          0x9e0
#define CSR_SINSTRETH        0x9e2
#define CSR_SHPMCOUNTER3H    0x9e3
#define CSR_SHPMCOUNTER4H    0x9e4
#define CSR_SHPMCOUNTER5H    0x9e5
#define CSR_SHPMCOUNTER6H    0x9e6
#define CSR_SHPMCOUNTER7H    0x9e7
#define CSR_SHPMCOUNTER8H    0x9e8
#define CSR_SHPMCOUNTER9H    0x9e9
#define CSR_SHPMCOUNTER10H   0x9ea
#define CSR_SHPMCOUNTER11H   0x9eb
#define CSR_SHPMCOUNTER12H   0x9ec
#define CSR_SHPMCOUNTER13H   0x9ed
#define CSR_SHPMCOUNTER14H   0x9ee
#define CSR_SHPMCOUNTER15H   0x9ef
#define CSR_SHPMCOUNTER16H   0x9f0
#define CSR_SHPMCOUNTER17H   0x9f1
#define CSR_SHPMCOUNTER18H   0x9f2
#define CSR_SHPMCOUNTER19H   0x9f3
#define CSR_SHPMCOUNTER20H   0x9f4
#define CSR_SHPMCOUNTER21H   0x9f5
#define CSR_SHPMCOUNTER22H   0x9f6
#define CSR_SHPMCOUNTER23H   0x9f7
#define CSR_SHPMCOUNTER24H   0x9f8
#define CSR_SHPMCOUNTER25H   0x9f9
#define CSR_SHPMCOUNTER26H   0x9fa
#define CSR_SHPMCOUNTER27H   0x9fb
#define CSR_SHPMCOUNTER28H   0x9fc
#define CSR_SHPMCOUNTER29H   0x9fd
#define CSR_SHPMCOUNTER30H   0x9fe
#define CSR_SHPMCOUNTER31H   0x9ff

/* Floating point round mode in fxcr */
#define FXCR_RD_SHIFT       24
#define FXCR_RD             (0x7 << FXCR_RD_SHIFT)
/* BF16 in fxcr */
#define FXCR_BF16_SHIFT     31
#define FXCR_BF16           (0x1 << FXCR_BF16_SHIFT)

/* TCM support */
#define MDTCMCR_EN          0x1
#define MDTCMCR_ECC_EN      0x2
#define MDTCMCR_INTERLEAVE  0x4
#define MDTCMCR_SIZE        0xf0
#define MDTCMCR_BASE_32     0xfffff000
#define MDTCMCR_BASE_64     0xfffffffffffff000

#define MITCMCR_EN          0x1
#define MITCMCR_ECC_EN      0x2
#define MITCMCR_INTERLEAVE  0x4
#define MITCMCR_SIZE        0xf0
#define MITCMCR_BASE_32     0xfffff000
#define MITCMCR_BASE_64     0xfffffffffffff000

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

/* CLIC */
#define CSR_MINTSTATUS      0x346
#define CSR_SINTSTATUS      0x146
#define CSR_MCLICBASE       0x350
#define CSR_MSCRATCHCSW     0x348
#define CSR_MSCRATCHCSL     0x349

/* mintstatus */
#define MINTSTATUS_MIL                     0xff000000 /* mil[7:0] */
#define MINTSTATUS_SIL                     0x0000ff00 /* sil[7:0] */
#define MINTSTATUS_UIL                     0x000000ff /* uil[7:0] */

/* sintstatus */
#define SINTSTATUS_SIL                     0x0000ff00 /* sil[7:0] */
#define SINTSTATUS_UIL                     0x000000ff /* uil[7:0] */

#define CSR_MINTTHRESH      0x347
#define CSR_SINTTHRESH      0x147

#define CSR_MTVT            0x307
#define CSR_STVT            0x107

/* FIXME: only exist in 0.9 spec */
#define CSR_MNXTI           0x345
#define CSR_SNXTI           0x145
/* mcause */
#define MCAUSE_MINHV                       0x40000000 /* minhv */
#define MCAUSE_MPP                         0x30000000 /* mpp[1:0] */
#define MCAUSE_MPIE                        0x08000000 /* mpie */
#define MCAUSE_MPIL                        0x00ff0000 /* mpil[7:0] */
#define MCAUSE_EXCCODE                     0x00000fff /* exccode[11:0] */
/* scause */
#define SCAUSE_SINHV                       0x40000000 /* sinhv */
#define SCAUSE_SPP                         0x10000000 /* spp */
#define SCAUSE_SPIE                        0x08000000 /* spie */
#define SCAUSE_SPIL                        0x00ff0000 /* spil[7:0] */
#define SCAUSE_EXCCODE                     0x00000fff /* exccode[11:0] */

/* Smsdid */
#define CSR_MTTP        0xbc0
#define CSR_MSDCFG      0xbd1

#define MTTP_MODE_MASK_32   0xC0000000
#define MTTP_SDID_MASK_32   0x3F000000
#define MTTP_PPN_MASK_32    0x003FFFFF

#define MTTP_MODE_SHIFT_32  30
#define MTTP_SDID_SHIFT_32  24

#define MTTP_MODE_MASK_64   0xF000000000000000ULL
#define MTTP_SDID_MASK_64   0x0FC0000000000000ULL
#define MTTP_PPN_MASK_64    0x00000FFFFFFFFFFFULL

#define MPTE_L3_VALID       0x0000100000000000ULL
#define MPTE_L3_RESERVED    0xFFFFE00000000000ULL

#define MPTE_L2_RESERVED_64    0xFFFF800000000000ULL
#define MPTE_L2_RESERVED_32    0xFE000000

#define MPTE_L1_RESERVED_64    0xFFFFFFFF00000000ULL
#define MPTE_L1_RESERVED_32    0xFFFF0000

#define MTTP_MODE_SHIFT_64  60
#define MTTP_SDID_SHIFT_64  54

/* Zicfiss */
#define CSR_SSP             0x011
/* SQOSCFG BITS (QOSID) */
#define SQOSCFG_RCID                      0x00000FFF
#define SQOSCFG_MCID                      0x0FFF0000

/* Debug trace */
#define CSR_SCONTEXT  0x5a8
#define SCONTEXT32    0xFFFF
#define SCONTEXT64    0xFFFFFFFF
#endif
