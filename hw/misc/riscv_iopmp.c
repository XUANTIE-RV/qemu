/*
 * QEMU RISC-V IOPMP (Input Output Physical Memory Protection)
 *
 * Copyright (c) 2023 Andes Tech. Corp.
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
#include "qapi/error.h"
#include "trace.h"
#include "exec/exec-all.h"
#include "exec/address-spaces.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/misc/riscv_iopmp.h"
#include "memory.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "trace.h"

#define TYPE_IOPMP_IOMMU_MEMORY_REGION "iopmp-iommu-memory-region"

REG32(VERSION, 0x00)
    FIELD(VERSION, VENDOR, 0, 24)
    FIELD(VERSION, SPECVER , 24, 8)
REG32(IMP, 0x04)
    FIELD(IMP, IMPID, 0, 32)
REG32(HWCFG0, 0x08)
    FIELD(HWCFG0, MODEL, 0, 4)
    FIELD(HWCFG0, TOR_EN, 4, 1)
    FIELD(HWCFG0, SPS_EN, 5, 1)
    FIELD(HWCFG0, USER_CFG_EN, 6, 1)
    FIELD(HWCFG0, PRIENT_PROG, 7, 1)
    FIELD(HWCFG0, RRID_TRANSL_EN, 8, 1)
    FIELD(HWCFG0, RRID_TRANSL_PROG, 9, 1)
    FIELD(HWCFG0, CHK_X, 10, 1)
    FIELD(HWCFG0, NO_X, 11, 1)
    FIELD(HWCFG0, NO_W, 12, 1)
    FIELD(HWCFG0, STALL_EN, 13, 1)
    FIELD(HWCFG0, PEIS, 14, 1)
    FIELD(HWCFG0, PEES, 15, 1)
    FIELD(HWCFG0, MFR_EN, 16, 1)
    FIELD(HWCFG0, MD_NUM, 24, 7)
    FIELD(HWCFG0, ENABLE, 31, 1)
REG32(HWCFG1, 0x0C)
    FIELD(HWCFG1, RRID_NUM, 0, 16)
    FIELD(HWCFG1, ENTRY_NUM, 16, 16)
REG32(HWCFG2, 0x10)
    FIELD(HWCFG2, PRIO_ENTRY, 0, 16)
    FIELD(HWCFG2, RRID_TRANSL, 16, 16)
REG32(ENTRYOFFSET, 0x14)
    FIELD(ENTRYOFFSET, OFFSET, 0, 32)
REG32(MDSTALL, 0x30)
    FIELD(MDSTALL, EXEMPT, 0, 1)
    FIELD(MDSTALL, MD, 1, 31)
REG32(MDSTALLH, 0x34)
    FIELD(MDSTALLH, MD, 0, 32)
REG32(RRIDSCP, 0x38)
    FIELD(RRIDSCP, RRID, 0, 16)
    FIELD(RRIDSCP, OP, 30, 2)
REG32(MDLCK, 0x40)
    FIELD(MDLCK, L, 0, 1)
    FIELD(MDLCK, MD, 1, 31)
REG32(MDLCKH, 0x44)
    FIELD(MDLCKH, MDH, 0, 32)
REG32(MDCFGLCK, 0x48)
    FIELD(MDCFGLCK, L, 0, 1)
    FIELD(MDCFGLCK, F, 1, 7)
REG32(ENTRYLCK, 0x4C)
    FIELD(ENTRYLCK, L, 0, 1)
    FIELD(ENTRYLCK, F, 1, 16)
REG32(ERR_CFG, 0x60)
    FIELD(ERR_CFG, L, 0, 1)
    FIELD(ERR_CFG, IE, 1, 1)
    FIELD(ERR_CFG, IRE, 2, 1)
    FIELD(ERR_CFG, IWE, 3, 1)
    FIELD(ERR_CFG, IXE, 4, 1)
    FIELD(ERR_CFG, RRE, 5, 1)
    FIELD(ERR_CFG, RWE, 6, 1)
    FIELD(ERR_CFG, RXE, 7, 1)
REG32(ERR_REQINFO, 0x64)
    FIELD(ERR_REQINFO, V, 0, 1)
    FIELD(ERR_REQINFO, TTYPE, 1, 2)
    FIELD(ERR_REQINFO, ETYPE, 4, 3)
    FIELD(ERR_REQINFO, SVC, 7, 1)
REG32(ERR_REQADDR, 0x68)
    FIELD(ERR_REQADDR, ADDR, 0, 32)
REG32(ERR_REQADDRH, 0x6C)
    FIELD(ERR_REQADDRH, ADDRH, 0, 32)
REG32(ERR_REQID, 0x70)
    FIELD(ERR_REQID, RRID, 0, 16)
    FIELD(ERR_REQID, EID, 16, 16)
REG32(ERR_MFR, 0x74)
    FIELD(ERR_MFR, SVW, 0, 16)
    FIELD(ERR_MFR, SVI, 16, 12)
    FIELD(ERR_MFR, SVS, 31, 1)
REG32(MDCFG0, 0x800)
    FIELD(MDCFG0, T, 0, 16)
REG32(SRCMD_EN0, 0x1000)
    FIELD(SRCMD_EN0, L, 0, 1)
    FIELD(SRCMD_EN0, MD, 1, 31)
REG32(SRCMD_ENH0, 0x1004)
    FIELD(SRCMD_ENH0, MDH, 0, 32)
REG32(SRCMD_R0, 0x1008)
    FIELD(SRCMD_R0, MD, 1, 31)
REG32(SRCMD_RH0, 0x100C)
    FIELD(SRCMD_RH0, MDH, 0, 32)
REG32(SRCMD_W0, 0x1010)
    FIELD(SRCMD_W0, MD, 1, 31)
REG32(SRCMD_WH0, 0x1014)
    FIELD(SRCMD_WH0, MDH, 0, 32)

FIELD(ENTRY_ADDR, ADDR, 0, 32)
FIELD(ENTRY_ADDRH, ADDRH, 0, 32)

FIELD(ENTRY_CFG, R, 0, 1)
FIELD(ENTRY_CFG, W, 1, 1)
FIELD(ENTRY_CFG, X, 2, 1)
FIELD(ENTRY_CFG, A, 3, 2)
FIELD(ENTRY_CFG, SIRE, 5, 1)
FIELD(ENTRY_CFG, SIWE, 6, 1)
FIELD(ENTRY_CFG, SIXE, 7, 1)
FIELD(ENTRY_CFG, SERE, 8, 1)
FIELD(ENTRY_CFG, SEWE, 9, 1)
FIELD(ENTRY_CFG, SEXE, 10, 1)

FIELD(ENTRY_USER_CFG, IM, 0, 32)

/* Offsets to SRCMD_EN(i) */
#define SRCMD_EN_OFFSET  0x0
#define SRCMD_ENH_OFFSET 0x4
#define SRCMD_R_OFFSET   0x8
#define SRCMD_RH_OFFSET  0xC
#define SRCMD_W_OFFSET   0x10
#define SRCMD_WH_OFFSET  0x14

/* Offsets to ENTRY_ADDR(i) */
#define ENTRY_ADDR_OFFSET     0x0
#define ENTRY_ADDRH_OFFSET    0x4
#define ENTRY_CFG_OFFSET      0x8
#define ENTRY_USER_CFG_OFFSET 0xC

static void iopmp_iommu_notify(IopmpState *s)
{
    IOMMUTLBEvent event = {
        .entry = {
            .iova = 0,
            .translated_addr = 0,
            .addr_mask = -1ULL,
            .perm = IOMMU_NONE,
        },
        .type = IOMMU_NOTIFIER_UNMAP,
    };

    for (int i = 0; i < s->rrid_num; i++) {
        memory_region_notify_iommu(&s->iommu, i, event);
    }
}

static void iopmp_decode_napot(uint64_t a, uint64_t *sa,
                               uint64_t *ea)
{
    /*
     * aaaa...aaa0   8-byte NAPOT range
     * aaaa...aa01   16-byte NAPOT range
     * aaaa...a011   32-byte NAPOT range
     * ...
     * aa01...1111   2^XLEN-byte NAPOT range
     * a011...1111   2^(XLEN+1)-byte NAPOT range
     * 0111...1111   2^(XLEN+2)-byte NAPOT range
     *  1111...1111   Reserved
     */

    a = (a << 2) | 0x3;
    *sa = a & (a + 1);
    *ea = a | (a + 1);
}

static void iopmp_update_rule(IopmpState *s, uint32_t entry_index)
{
    uint8_t this_cfg = s->regs.entry[entry_index].cfg_reg;
    uint64_t this_addr = s->regs.entry[entry_index].addr_reg |
                         ((uint64_t)s->regs.entry[entry_index].addrh_reg << 32);
    uint64_t prev_addr = 0u;
    uint64_t sa = 0u;
    uint64_t ea = 0u;

    if (entry_index >= 1u) {
        prev_addr = s->regs.entry[entry_index - 1].addr_reg |
                    ((uint64_t)s->regs.entry[entry_index - 1].addrh_reg << 32);
    }

    switch (FIELD_EX32(this_cfg, ENTRY_CFG, A)) {
    case IOPMP_AMATCH_OFF:
        sa = 0u;
        ea = -1;
        break;

    case IOPMP_AMATCH_TOR:
        sa = (prev_addr) << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = ((this_addr) << 2) - 1u;
        if (sa > ea) {
            sa = ea = 0u;
        }
        break;

    case IOPMP_AMATCH_NA4:
        sa = this_addr << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = (sa + 4u) - 1u;
        break;

    case IOPMP_AMATCH_NAPOT:
        iopmp_decode_napot(this_addr, &sa, &ea);
        break;

    default:
        sa = 0u;
        ea = 0u;
        break;
    }

    s->entry_addr[entry_index].sa = sa;
    s->entry_addr[entry_index].ea = ea;
    iopmp_iommu_notify(s);
}

static uint64_t iopmp_read(void *opaque, hwaddr addr, unsigned size)
{
    IopmpState *s = IOPMP(opaque);
    uint32_t rz = 0;
    uint32_t offset, idx;

    switch (addr) {
    case A_VERSION:
        rz = VENDER_VIRT << R_VERSION_VENDOR_SHIFT |
             SPECVER_0_9_1 << R_VERSION_SPECVER_SHIFT;
        break;
    case A_IMP:
        rz = IMPID_0_9_1;
        break;
    case A_HWCFG0:
        rz = s->model << R_HWCFG0_MODEL_SHIFT |
             1 << R_HWCFG0_TOR_EN_SHIFT |
             0 << R_HWCFG0_SPS_EN_SHIFT |
             0 << R_HWCFG0_USER_CFG_EN_SHIFT |
             s->prient_prog << R_HWCFG0_PRIENT_PROG_SHIFT |
             s->rrid_transl_en << R_HWCFG0_RRID_TRANSL_EN_SHIFT |
             s->rrid_transl_prog << R_HWCFG0_RRID_TRANSL_PROG_SHIFT |
             0 << R_HWCFG0_CHK_X_SHIFT |
             0 << R_HWCFG0_NO_X_SHIFT |
             0 << R_HWCFG0_NO_W_SHIFT |
             0 << R_HWCFG0_STALL_EN_SHIFT |
             0 << R_HWCFG0_PEIS_SHIFT |
             0 << R_HWCFG0_PEES_SHIFT |
             0 << R_HWCFG0_MFR_EN_SHIFT |
             s->md_num << R_HWCFG0_MD_NUM_SHIFT |
             s->enable << R_HWCFG0_ENABLE_SHIFT ;
        break;
    case A_HWCFG1:
        rz = s->rrid_num << R_HWCFG1_RRID_NUM_SHIFT |
             s->entry_num << R_HWCFG1_ENTRY_NUM_SHIFT;
        break;
    case A_HWCFG2:
        rz = s->prio_entry << R_HWCFG2_PRIO_ENTRY_SHIFT |
             s->rrid_transl << R_HWCFG2_RRID_TRANSL_SHIFT;
        break;
    case A_ENTRYOFFSET:
        rz = s->entry_offset;
        break;
    case A_ERR_CFG:
        rz = s->regs.err_cfg;
        break;
    case A_MDLCK:
        rz = s->regs.mdlck;
        break;
    case A_MDLCKH:
        rz = s->regs.mdlckh;
        break;
    case A_MDCFGLCK:
        rz = s->regs.mdcfglck;
        break;
    case A_ENTRYLCK:
        rz = s->regs.entrylck;
        break;
    case A_ERR_REQADDR:
        rz = s->regs.err_reqaddr & UINT32_MAX;
        break;
    case A_ERR_REQADDRH:
        rz = s->regs.err_reqaddr >> 32;
        break;
    case A_ERR_REQID:
        rz = s->regs.err_reqid;
        break;
    case A_ERR_REQINFO:
        rz = s->regs.err_reqinfo;
        break;

    default:
        if (addr >= A_MDCFG0 &&
            addr < A_MDCFG0 + 4 * (s->md_num - 1)) {
            offset = addr - A_MDCFG0;
            idx = offset >> 2;
            if (idx == 0 && offset == 0) {
                rz = s->regs.mdcfg[idx];
            } else {
                /* Only MDCFG0 is implemented in rapid-k model */
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                __func__, (int)addr);
            }
        } else if (addr >= A_SRCMD_EN0 &&
                   addr < A_SRCMD_WH0 + 32 * (s->rrid_num - 1)) {
            offset = addr - A_SRCMD_EN0;
            idx = offset >> 5;
            offset &= 0x1f;

            switch (offset) {
            case SRCMD_EN_OFFSET:
                rz = s->regs.srcmd_en[idx];
                break;
            case SRCMD_ENH_OFFSET:
                rz = s->regs.srcmd_enh[idx];
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                    __func__, (int)addr);
                break;
            }
        } else if (addr >= s->entry_offset &&
                   addr < s->entry_offset + ENTRY_USER_CFG_OFFSET +
                          16 * (s->entry_num - 1)) {
            offset = addr - s->entry_offset;
            idx = offset >> 4;
            offset &= 0xf;

            switch (offset) {
            case ENTRY_ADDR_OFFSET:
                rz = s->regs.entry[idx].addr_reg;
                break;
            case ENTRY_ADDRH_OFFSET:
                rz = s->regs.entry[idx].addrh_reg;
                break;
            case ENTRY_CFG_OFFSET:
                rz = s->regs.entry[idx].cfg_reg;
                break;
            case ENTRY_USER_CFG_OFFSET:
                /* Does not support user customized permission */
                rz = 0;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                    __func__, (int)addr);
                break;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                            __func__, (int)addr);
        }
        break;
    }
    trace_iopmp_read(addr, rz);
    return rz;
}

static void
iopmp_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IopmpState *s = IOPMP(opaque);
    uint32_t offset, idx;
    uint32_t value32 = value;

    trace_iopmp_write(addr, value32);

    switch (addr) {
    case A_VERSION: /* RO */
        break;
    case A_IMP: /* RO */
        break;
    case A_HWCFG0:
        if (FIELD_EX32(value32, HWCFG0, PRIENT_PROG)) {
            /* W1C */
            s->prient_prog = 0;
        }
        if (FIELD_EX32(value32, HWCFG0, RRID_TRANSL_PROG)) {
            /* W1C */
            s->rrid_transl_prog = 0;
        }
        if (FIELD_EX32(value32, HWCFG0, ENABLE)) {
            /* W1S */
            s->enable = 1;
            iopmp_iommu_notify(s);
        }
        break;
    case A_HWCFG1: /* RO */
        break;
    case A_HWCFG2:
        if (s->prient_prog) {
            s->prio_entry = FIELD_EX32(value32, HWCFG2, PRIO_ENTRY);
        }
        if (s->rrid_transl_en && s->rrid_transl_prog) {
            s->rrid_transl = FIELD_EX32(value32, HWCFG2, RRID_TRANSL);
        }
        break;
    case A_ERR_CFG:
        if (!FIELD_EX32(s->regs.err_cfg, ERR_CFG, L)) {
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, L,
                FIELD_EX32(value32, ERR_CFG, L));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, IE,
                FIELD_EX32(value32, ERR_CFG, IE));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, IRE,
                FIELD_EX32(value32, ERR_CFG, IRE));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, RRE,
                FIELD_EX32(value32, ERR_CFG, RRE));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, IWE,
                FIELD_EX32(value32, ERR_CFG, IWE));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, RWE,
                FIELD_EX32(value32, ERR_CFG, RWE));
        }
        break;
    case A_MDLCK:
        if (!FIELD_EX32(s->regs.mdlck, MDLCK, L)) {
            s->regs.mdlck = value32;
        }
        break;
    case A_MDLCKH:
        if (!FIELD_EX32(s->regs.mdlck, MDLCK, L)) {
            s->regs.mdlckh = value32;
        }
        break;
    case A_MDCFGLCK:
        if (!FIELD_EX32(s->regs.mdcfglck, MDCFGLCK, L)) {
            s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, F,
                                          FIELD_EX32(value32, MDCFGLCK, F));
            s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, L,
                                          FIELD_EX32(value32, MDCFGLCK, L));
        }
        break;
    case A_ENTRYLCK:
        if (!(FIELD_EX32(s->regs.entrylck, ENTRYLCK, L))) {
            s->regs.entrylck = FIELD_DP32(s->regs.entrylck, ENTRYLCK, F,
                                          FIELD_EX32(value32, ENTRYLCK, F));
            s->regs.entrylck = FIELD_DP32(s->regs.entrylck, ENTRYLCK, L,
                                          FIELD_EX32(value32, ENTRYLCK, L));
        }
    case A_ERR_REQADDR: /* RO */
        break;
    case A_ERR_REQADDRH: /* RO */
        break;
    case A_ERR_REQID: /* RO */
        break;
    case A_ERR_REQINFO:
        if (FIELD_EX32(value32, ERR_REQINFO, V)) {
            s->regs.err_reqinfo = FIELD_DP32(s->regs.err_reqinfo,
                                             ERR_REQINFO, V, 0);
            qemu_set_irq(s->irq, 0);
        }
        break;

    default:
        if (addr >= A_MDCFG0 &&
            addr < A_MDCFG0 + 4 * (s->md_num - 1)) {
            offset = addr - A_MDCFG0;
            idx = offset >> 2;
            /* RO in rapid-k model */
            if (idx > 0) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                              __func__, (int)addr);
            }
        } else if (addr >= A_SRCMD_EN0 &&
                   addr < A_SRCMD_WH0 + 32 * (s->rrid_num - 1)) {
            offset = addr - A_SRCMD_EN0;
            idx = offset >> 5;
            offset &= 0x1f;

            if (offset % 4) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                              __func__, (int)addr);
            } else if (FIELD_EX32(s->regs.srcmd_en[idx], SRCMD_EN0, L)
                        == 0) {
                switch (offset) {
                case SRCMD_EN_OFFSET:
                    s->regs.srcmd_en[idx] =
                        FIELD_DP32(s->regs.srcmd_en[idx], SRCMD_EN0, L,
                                   FIELD_EX32(value32, SRCMD_EN0, L));

                    /* MD field is protected by mdlck */
                    value32 = (value32 & ~s->regs.mdlck) |
                              (s->regs.srcmd_en[idx] & s->regs.mdlck);
                    s->regs.srcmd_en[idx] =
                        FIELD_DP32(s->regs.srcmd_en[idx], SRCMD_EN0, MD,
                                   FIELD_EX32(value32, SRCMD_EN0, MD));
                    break;
                case SRCMD_ENH_OFFSET:
                    value32 = (value32 & ~s->regs.mdlckh) |
                              (s->regs.srcmd_enh[idx] & s->regs.mdlckh);
                    s->regs.srcmd_enh[idx] =
                        FIELD_DP32(s->regs.srcmd_enh[idx], SRCMD_ENH0, MDH,
                                   value32);
                    break;
                default:
                    break;
                }
            }
        } else if (addr >= s->entry_offset &&
                   addr < s->entry_offset + ENTRY_USER_CFG_OFFSET
                          + 16 * (s->entry_num - 1)) {
            offset = addr - s->entry_offset;
            idx = offset >> 4;
            offset &= 0xf;

            /* index < ENTRYLCK_F is protected */
            if (idx >= FIELD_EX32(s->regs.entrylck, ENTRYLCK, F)) {
                switch (offset) {
                case ENTRY_ADDR_OFFSET:
                    s->regs.entry[idx].addr_reg = value32;
                    break;
                case ENTRY_ADDRH_OFFSET:
                    s->regs.entry[idx].addrh_reg = value32;
                    break;
                case ENTRY_CFG_OFFSET:
                    s->regs.entry[idx].cfg_reg = value32;
                    break;
                case ENTRY_USER_CFG_OFFSET:
                    /* Does not support user customized permission */
                    break;
                default:
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                  __func__, (int)addr);
                    break;
                }
                iopmp_update_rule(s, idx);
                if (idx + 1 < s->entry_num &&
                    FIELD_EX32(s->regs.entry[idx + 1].cfg_reg, ENTRY_CFG, A) ==
                    IOPMP_AMATCH_TOR) {
                        iopmp_update_rule(s, idx + 1);
                }
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n", __func__,
                          (int)addr);
        }
    }
}

/* Match entry in memory domain */
static int match_entry_md(IopmpState *s, int md_idx, hwaddr saddr,
                          hwaddr e_addr, int *entry_idx)
{
    int entry_idx_s, entry_idx_e;
    int result = ENTRY_NO_HIT;
    int i = 0;
    entry_idx_s = md_idx * s->regs.mdcfg[0];
    entry_idx_e = (md_idx + 1) * s->regs.mdcfg[0];

    if (entry_idx_s >= s->entry_num) {
        return result;
    }
    if (entry_idx_e > s->entry_num) {
        entry_idx_e = s->entry_num;
    }
    i = entry_idx_s;
    for (i = entry_idx_s; i < entry_idx_e; i++) {
        if (FIELD_EX32(s->regs.entry[i].cfg_reg, ENTRY_CFG, A) ==
            IOPMP_AMATCH_OFF) {
            continue;
        }
        if (saddr >= s->entry_addr[i].sa && saddr <= s->entry_addr[i].ea) {
            /* Check end address */
            if (e_addr >= s->entry_addr[i].sa &&
                e_addr <= s->entry_addr[i].ea) {
                *entry_idx = i;
                return ENTRY_HIT;
            } else if (i >= s->prio_entry) {
                /* Continue for non-prio_entry */
                continue;
            } else {
                *entry_idx = i;
                return ENTRY_PAR_HIT;
            }
        } else if (e_addr >= s->entry_addr[i].sa &&
                   e_addr <= s->entry_addr[i].ea) {
            /* Only end address matches the entry */
            if (i >= s->prio_entry) {
                continue;
            } else {
                *entry_idx = i;
                return ENTRY_PAR_HIT;
            }
        } else if (saddr < s->entry_addr[i].sa &&
                   e_addr > s->entry_addr[i].ea) {
            if (i >= s->prio_entry) {
                continue;
            } else {
                *entry_idx = i;
                return ENTRY_PAR_HIT;
            }
        }
    }
    return result;
}

static int match_entry(IopmpState *s, int rrid, hwaddr saddr, hwaddr e_addr,
                       int *match_md_idx, int *match_entry_idx)
{
    int cur_result = ENTRY_NO_HIT;
    int result = ENTRY_NO_HIT;
    /* Remove lock bit */
    uint64_t srcmd_en = ((uint64_t)s->regs.srcmd_en[rrid] |
                         ((uint64_t)s->regs.srcmd_enh[rrid] << 32)) >> 1;

    for (int md_idx = 0; md_idx < s->md_num; md_idx++) {
        if (srcmd_en & (1ULL << md_idx)) {
            cur_result = match_entry_md(s, md_idx, saddr, e_addr,
                                        match_entry_idx);
            if (cur_result == ENTRY_HIT || cur_result == ENTRY_PAR_HIT) {
                *match_md_idx = md_idx;
                return cur_result;
            }
        }
    }
    return result;
}

static void iopmp_error_reaction(IopmpState *s, uint32_t id, hwaddr start,
                                 hwaddr end, uint32_t info)
{
    if (!FIELD_EX32(s->regs.err_reqinfo, ERR_REQINFO, V)) {
        s->regs.err_reqinfo = info;
        s->regs.err_reqinfo = FIELD_DP32(s->regs.err_reqinfo, ERR_REQINFO, V,
                                         1);
        s->regs.err_reqid = id;
        /* addr[LEN+2:2] */
        s->regs.err_reqaddr = start >> 2;

        if (FIELD_EX32(info, ERR_REQINFO, TTYPE) == ERR_REQINFO_TTYPE_READ &&
            FIELD_EX32(s->regs.err_cfg, ERR_CFG, IE) &&
            FIELD_EX32(s->regs.err_cfg, ERR_CFG, IRE)) {
            qemu_set_irq(s->irq, 1);
        }
        if (FIELD_EX32(info, ERR_REQINFO, TTYPE) == ERR_REQINFO_TTYPE_WRITE &&
            FIELD_EX32(s->regs.err_cfg, ERR_CFG, IE) &&
            FIELD_EX32(s->regs.err_cfg, ERR_CFG, IWE)) {
            qemu_set_irq(s->irq, 1);
        }
    }
}

static IOMMUTLBEntry iopmp_translate(IOMMUMemoryRegion *iommu, hwaddr addr,
                                     IOMMUAccessFlags flags, int iommu_idx)
{
    int pci_id = 0;
    int rrid = iommu_idx;
    IopmpState *s;
    MemoryRegion *mr = MEMORY_REGION(iommu);
    hwaddr start_addr, end_addr;
    int entry_idx = -1;
    int md_idx = -1;
    int result;
    uint32_t error_info = 0;
    uint32_t error_id = 0;
    IOMMUTLBEntry entry = {
        .target_as = NULL,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = 0,
        .perm = IOMMU_NONE,
    };

    /* Find IOPMP of iommu */
    if (strncmp(mr->name, "riscv-iopmp-sysbus-iommu", 24) != 0) {
        sscanf(mr->name, "riscv-iopmp-pci-iommu%d", &pci_id);
        iopmp_pci_addressspcace *pci_s = container_of(iommu,
                                                      iopmp_pci_addressspcace,
                                                      iommu);
        s = IOPMP(pci_s->iopmp);
        /* If device does not specify rrid, use id from pci */
        if (rrid == 0) {
            rrid = pci_id;
        }
    } else {
        s = IOPMP(container_of(iommu, IopmpState, iommu));
    }
    entry.target_as = &s->downstream_as;

    /* Because IOMMU doesn't have the tansaction legth, use the same address */
    start_addr = addr;
    end_addr = addr;

    if (!s->enable) {
        /* Bypass IOPMP */
        entry.perm = IOMMU_RW;
        return entry;
    }

    /* unknown RRID */
    if (rrid >= s->rrid_num) {
        error_id = FIELD_DP32(error_id, ERR_REQID, RRID, rrid);
        error_info = FIELD_DP32(error_info, ERR_REQINFO, ETYPE,
                                ERR_REQINFO_ETYPE_RRID);
        error_info = FIELD_DP32(error_info, ERR_REQINFO, TTYPE, flags);
        iopmp_error_reaction(s, error_id, start_addr, end_addr, error_info);
        entry.target_as = &s->blocked_io_as;
        entry.perm = IOMMU_RW;
        return entry;
    }

    result = match_entry(s, rrid, start_addr, end_addr, &md_idx, &entry_idx);
    if (result == ENTRY_HIT) {
        entry.perm = s->regs.entry[entry_idx].cfg_reg & 0x7;
        if ((entry.perm & flags) == 0) {
            /* permission denied */
            error_id = FIELD_DP32(error_id, ERR_REQID, EID, entry_idx);
            error_id = FIELD_DP32(error_id, ERR_REQID, RRID, rrid);
            error_info = FIELD_DP32(error_info, ERR_REQINFO, ETYPE,
                                    ERR_REQINFO_ETYPE_READ + flags - 1);
            error_info = FIELD_DP32(error_info, ERR_REQINFO, TTYPE, flags);
            iopmp_error_reaction(s, error_id, start_addr, end_addr, error_info);
            entry.target_as = &s->blocked_io_as;
            entry.perm = IOMMU_RW;
        } else {
            entry.addr_mask = s->entry_addr[entry_idx].ea -
                              s->entry_addr[entry_idx].sa;
            if (s->rrid_transl_en) {
                /* pass to next iopmp */
                if (s->next_iommu) {
                    /* Get result from next IOPMP */
                    entry = iopmp_translate(s->next_iommu, addr, flags,
                                            s->rrid_transl);
                    return entry;
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR, "Next iopmp is not found.");
                }
            }
        }
    } else {
        if (result == ENTRY_PAR_HIT) {
            error_id = FIELD_DP32(error_id, ERR_REQID, EID, entry_idx);
            error_id = FIELD_DP32(error_id, ERR_REQID, RRID, rrid);
            error_info = FIELD_DP32(error_info, ERR_REQINFO, ETYPE,
                                    ERR_REQINFO_ETYPE_PARHIT);
            error_info = FIELD_DP32(error_info, ERR_REQINFO, TTYPE, flags);
            iopmp_error_reaction(s, error_id, start_addr, end_addr, error_info);
        } else {
            error_id = FIELD_DP32(error_id, ERR_REQID, RRID, rrid);
            error_info = FIELD_DP32(error_info, ERR_REQINFO, ETYPE,
                                    ERR_REQINFO_ETYPE_NOHIT);
            error_info = FIELD_DP32(error_info, ERR_REQINFO, TTYPE, flags);
            iopmp_error_reaction(s, error_id, start_addr, end_addr, error_info);
        }
        entry.target_as = &s->blocked_io_as;
        entry.perm = IOMMU_RW;
    }
    return entry;
}

static const MemoryRegionOps iopmp_ops = {
    .read = iopmp_read,
    .write = iopmp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4}
};

static MemTxResult iopmp_block_write(void *opaque, hwaddr addr, uint64_t value,
                                     unsigned size, MemTxAttrs attrs)
{
    IopmpState *s = IOPMP(opaque);

    switch (FIELD_EX32(s->regs.err_cfg, ERR_CFG, RWE)) {
    case RWE_ERROR:
        return MEMTX_ERROR;
        break;
    case RWE_SUCCESS:
        return MEMTX_OK;
        break;
    default:
        break;
    }
    return MEMTX_OK;
}

static MemTxResult iopmp_block_read(void *opaque, hwaddr addr, uint64_t *pdata,
                                    unsigned size, MemTxAttrs attrs)
{
    IopmpState *s = IOPMP(opaque);

    switch (FIELD_EX32(s->regs.err_cfg, ERR_CFG, RRE)) {
    case RRE_ERROR:
        return MEMTX_ERROR;
        break;
    case RRE_SUCCESS_VALUE:
        *pdata = s->fabricated_v;
        return MEMTX_OK;
        break;
    default:
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps iopmp_block_io_ops = {
    .read_with_attrs = iopmp_block_read,
    .write_with_attrs = iopmp_block_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static void iopmp_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    IopmpState *s = IOPMP(dev);
    uint64_t size;

    if (!s->downstream) {
        s->downstream = get_system_memory();
    }
    size = memory_region_size(s->downstream);
    s->model = IOPMP_MODEL_RAPIDK;
    s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, F, s->md_num);
    s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, L, 1);

    s->prient_prog = s->default_prient_prog;
    s->rrid_num = MIN(s->rrid_num, IOPMP_MAX_RRID_NUM);
    s->md_num = MIN(s->md_num, IOPMP_MAX_MD_NUM);
    s->entry_num = s->md_num * s->k;
    s->prio_entry = MIN(s->prio_entry, s->entry_num);

    s->regs.mdcfg = g_malloc0(s->md_num * sizeof(uint32_t));
    s->regs.mdcfg[0] = s->k;

    s->regs.srcmd_en = g_malloc0(s->rrid_num * sizeof(uint32_t));
    s->regs.srcmd_enh = g_malloc0(s->rrid_num * sizeof(uint32_t));
    s->regs.entry = g_malloc0(s->entry_num * sizeof(iopmp_entry_t));
    s->entry_addr = g_malloc0(s->entry_num * sizeof(iopmp_addr_t));
    s->iopmp_pci = g_malloc0(s->rrid_num * sizeof(iopmp_pci_addressspcace *));

    s->rrid_transl_prog = s->default_rrid_transl_prog;

    memory_region_init_iommu(&s->iommu, sizeof(s->iommu),
                             TYPE_IOPMP_IOMMU_MEMORY_REGION,
                             obj, "riscv-iopmp-sysbus-iommu", UINT64_MAX);
    address_space_init(&s->iopmp_sysbus_as, MEMORY_REGION(&s->iommu), "iommu");
    memory_region_init_io(&s->mmio, obj, &iopmp_ops,
                          s, "iopmp-regs", 0x100000);
    sysbus_init_mmio(sbd, &s->mmio);
    memory_region_init_io(&s->blocked_io, obj, &iopmp_block_io_ops,
                          s, "iopmp-blocked-io", size);
    address_space_init(&s->downstream_as, s->downstream,
                       "iopmp-downstream-as");
    address_space_init(&s->blocked_io_as, &s->blocked_io,
                       "iopmp-blocked-io-as");
}

static void iopmp_reset(DeviceState *dev)
{
    IopmpState *s = IOPMP(dev);

    qemu_set_irq(s->irq, 0);
    memset(s->regs.srcmd_en, 0, s->rrid_num * sizeof(uint32_t));
    memset(s->regs.srcmd_enh, 0, s->rrid_num * sizeof(uint32_t));
    memset(s->entry_addr, 0, s->entry_num * sizeof(iopmp_addr_t));

    s->regs.mdlck = 0;
    s->regs.mdlckh = 0;
    s->regs.entrylck = 0;
    s->regs.mdstall = 0;
    s->regs.mdstallh = 0;
    s->regs.rridscp = 0;
    s->regs.err_cfg = 0;
    s->regs.err_reqaddr = 0;
    s->regs.err_reqid = 0;
    s->regs.err_reqinfo = 0;

    s->prient_prog = s->default_prient_prog;
    s->rrid_transl_prog = s->default_rrid_transl_prog;
    s->enable = 0;

    s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, F, s->md_num);
    s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, L, 1);
    s->regs.mdcfg[0] = s->k;
}

static int iopmp_attrs_to_index(IOMMUMemoryRegion *iommu, MemTxAttrs attrs)
{
    return attrs.requester_id;
}

static void iopmp_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = iopmp_translate;
    imrc->attrs_to_index = iopmp_attrs_to_index;
}

static Property iopmp_property[] = {
    DEFINE_PROP_BOOL("rrid_transl_en", IopmpState, rrid_transl_en, false),
    DEFINE_PROP_BOOL("rrid_transl_prog", IopmpState, default_rrid_transl_prog,
                     true),
    DEFINE_PROP_BOOL("prient_prog", IopmpState, default_prient_prog, true),
    DEFINE_PROP_UINT32("k", IopmpState, k, 6),
    DEFINE_PROP_UINT32("prio_entry", IopmpState, prio_entry, 48),
    DEFINE_PROP_UINT32("rrid_num", IopmpState, rrid_num, 16),
    DEFINE_PROP_UINT32("md_num", IopmpState, md_num, 8),
    DEFINE_PROP_UINT32("entry_offset", IopmpState, entry_offset, 0x4000),
    DEFINE_PROP_UINT32("fabricated_v", IopmpState, fabricated_v, 0x0),
    DEFINE_PROP_LINK("downstream", IopmpState, downstream, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void iopmp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, iopmp_property);
    dc->realize = iopmp_realize;
    dc->reset = iopmp_reset;
}

static void iopmp_init(Object *obj)
{
    IopmpState *s = IOPMP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
}

static const TypeInfo iopmp_info = {
    .name = TYPE_IOPMP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IopmpState),
    .instance_init = iopmp_init,
    .class_init = iopmp_class_init,
};

static const TypeInfo
iopmp_iommu_memory_region_info = {
    .name = TYPE_IOPMP_IOMMU_MEMORY_REGION,
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .class_init = iopmp_iommu_memory_region_class_init,
};

void
cascade_iopmp(DeviceState *cur_dev, DeviceState *next_dev)
{
    IopmpState *s = IOPMP(cur_dev);
    IopmpState *next_s = IOPMP(next_dev);

    s->rrid_transl_en = true;
    s->next_iommu = &next_s->iommu;
}

static AddressSpace *iopmp_find_add_as(PCIBus *bus, void *opaque, int devfn)
{
    IopmpState *s = opaque;
    uint32_t id = (PCI_BUILD_BDF(pci_bus_num(bus), devfn) >> 3) % s->rrid_num;
    iopmp_pci_addressspcace *iopmp_pci = s->iopmp_pci[id];

    if (iopmp_pci == NULL) {
        g_autofree char *name = NULL;
        name = g_strdup_printf("riscv-iopmp-pci-iommu%d", id);
        iopmp_pci = g_new0(iopmp_pci_addressspcace, 1);
        iopmp_pci->iopmp = opaque;
        memory_region_init_iommu(&iopmp_pci->iommu,
                                 sizeof(iopmp_pci->iommu),
                                 TYPE_IOPMP_IOMMU_MEMORY_REGION,
                                 OBJECT(s), name, UINT64_MAX);
        address_space_init(&iopmp_pci->as,
                           MEMORY_REGION(&iopmp_pci->iommu), "iommu");
    }
    return &iopmp_pci->as;
}

static const PCIIOMMUOps iopmp_iommu_ops = {
    .get_address_space = iopmp_find_add_as,
};

void iopmp_setup_pci(DeviceState *iopmp_dev, PCIBus *bus)
{
    IopmpState *s = IOPMP(iopmp_dev);
    pci_setup_iommu(bus, &iopmp_iommu_ops, s);
}

static void
iopmp_register_types(void)
{
    type_register_static(&iopmp_info);
    type_register_static(&iopmp_iommu_memory_region_info);
}

type_init(iopmp_register_types);
