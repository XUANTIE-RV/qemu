/*
 * QEMU RISC-V IOPMP (Input Output Physical Memory Protection)
 *
 * Copyright (c) 2023-2025 Andes Tech. Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include "qemu/main-loop.h"
#include "hw/stream.h"
#include "hw/misc/riscv_iopmp_txn_info.h"

#define TYPE_RISCV_IOPMP_IOMMU_MEMORY_REGION "riscv-iopmp-iommu-memory-region"

REG32(VERSION, 0x00)
    FIELD(VERSION, VENDOR, 0, 24)
    FIELD(VERSION, SPECVER , 24, 8)
REG32(IMPLEMENTATION, 0x04)
    FIELD(IMPLEMENTATION, IMPID, 0, 32)
REG32(HWCFG0, 0x08)
    FIELD(HWCFG0, MDCFG_FMT, 0, 2)
    FIELD(HWCFG0, SRCMD_FMT, 2, 2)
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
    FIELD(HWCFG0, MD_ENTRY_NUM, 17, 7)
    FIELD(HWCFG0, MD_NUM, 24, 6)
    FIELD(HWCFG0, ADDRH_EN, 30, 1)
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
    FIELD(RRIDSCP, STAT, 30, 2)
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
    FIELD(ERR_CFG, RS, 2, 1)
    FIELD(ERR_CFG, MSI_EN, 3, 1)
    FIELD(ERR_CFG, STALL_VIOLATION_EN, 4, 1)
    FIELD(ERR_CFG, MSIDATA, 8, 11)
REG32(ERR_INFO, 0x64)
    FIELD(ERR_INFO, V, 0, 1)
    FIELD(ERR_INFO, TTYPE, 1, 2)
    FIELD(ERR_INFO, MSI_WERR, 3, 1)
    FIELD(ERR_INFO, ETYPE, 4, 4)
    FIELD(ERR_INFO, SVC, 8, 1)
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
REG32(ERR_MSIADDR, 0x78)
REG32(ERR_MSIADDRH, 0x7C)
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
REG32(SRCMD_PERM0, 0x1000)
REG32(SRCMD_PERMH0, 0x1004)

FIELD(ENTRY_ADDR, ADDR, 0, 32)
FIELD(ENTRY_ADDRH, ADDRH, 0, 32)

FIELD(ENTRY_CFG, R, 0, 1)
FIELD(ENTRY_CFG, W, 1, 1)
FIELD(ENTRY_CFG, X, 2, 1)
FIELD(ENTRY_CFG, A, 3, 2)
FIELD(ENTRY_CFG, SIE, 5, 3)
FIELD(ENTRY_CFG, SIRE, 5, 1)
FIELD(ENTRY_CFG, SIWE, 6, 1)
FIELD(ENTRY_CFG, SIXE, 7, 1)
FIELD(ENTRY_CFG, SEE, 8, 3)
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

/* Offsets to SRCMD_PERM(i) */
#define SRCMD_PERM_OFFSET  0x0
#define SRCMD_PERMH_OFFSET 0x4

/* Offsets to ENTRY_ADDR(i) */
#define ENTRY_ADDR_OFFSET     0x0
#define ENTRY_ADDRH_OFFSET    0x4
#define ENTRY_CFG_OFFSET      0x8
#define ENTRY_USER_CFG_OFFSET 0xC

#define IOPMP_MAX_MD_NUM                63
#define IOPMP_MAX_RRID_NUM              32
#define IOPMP_SRCMDFMT0_MAX_RRID_NUM    65535
#define IOPMP_SRCMDFMT2_MAX_RRID_NUM    32
#define IOPMP_MAX_ENTRY_NUM             65535

/* The ids of iopmp are temporary */
#define VENDER_VIRT                     0
#define SPECVER_0_7_0                   70
#define IMPID_0_7_0                     70

typedef enum {
    ERR_INFO_TTYPE_NOERROR,
    ERR_INFO_TTYPE_READ,
    ERR_INFO_TTYPE_WRITE,
    ERR_INFO_TTYPE_FETCH
} IOPMPErrInfoTTYPE;

typedef enum {
    ERR_INFO_ETYPE_NOERROR,
    ERR_INFO_ETYPE_READ,
    ERR_INFO_ETYPE_WRITE,
    ERR_INFO_ETYPE_FETCH,
    ERR_INFO_ETYPE_PARHIT,
    ERR_INFO_ETYPE_NOHIT,
    ERR_INFO_ETYPE_RRID,
    ERR_INFO_ETYPE_USER,
    ERR_INFO_ETYPE_STALL
} IOPMPErrInfoETYPE;

typedef enum {
    IOPMP_ENTRY_NO_HIT,
    IOPMP_ENTRY_PAR_HIT,
    IOPMP_ENTRY_HIT
} IOPMPEntryHit;

typedef enum {
    IOPMP_AMATCH_OFF,  /* Null (off)                            */
    IOPMP_AMATCH_TOR,  /* Top of Range                          */
    IOPMP_AMATCH_NA4,  /* Naturally aligned four-byte region    */
    IOPMP_AMATCH_NAPOT /* Naturally aligned power-of-two region */
} IOPMPAddrMode;

typedef enum {
    IOPMP_ACCESS_READ  = 1,
    IOPMP_ACCESS_WRITE = 2,
    IOPMP_ACCESS_FETCH = 3
} IOPMPAccessType;

typedef enum {
    IOPMP_NONE = 0,
    IOPMP_RO   = 1,
    IOPMP_WO   = 2,
    IOPMP_RW   = 3,
    IOPMP_XO   = 4,
    IOPMP_RX   = 5,
    IOPMP_WX   = 6,
    IOPMP_RWX  = 7,
} IOPMPPermission;

typedef enum {
    RRIDSCP_OP_QUERY = 0,
    RRIDSCP_OP_STALL = 1,
    RRIDSCP_OP_NO_STALL = 2,
    RRIDSCP_OP_RESERVED = 3,
} RRIDSCPOp;

typedef enum {
    RRIDSCP_STAT_NOT_IMPL = 0,
    RRIDSCP_STAT_STALL = 1,
    RRIDSCP_STAT_NO_STALL = 2,
    RRIDSCP_STAT_RRID_NO_IMPL = 3,
} RRIDSCPStat;

typedef struct EntryRange {
    int md;
    /* Index of entry array */
    int start_idx;
    int end_idx;
} EntryRange;

static void iopmp_iommu_notify(RISCVIOPMPState *s)
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

static void iopmp_msi_send(RISCVIOPMPState *s)
{
    MemTxResult result;
    uint64_t addr = ((uint64_t)(s->regs.err_msiaddrh) << 32) |
                    s->regs.err_msiaddr;
    address_space_stl_le(&address_space_memory, addr,
                         FIELD_EX32(s->regs.err_cfg, ERR_CFG, MSIDATA),
                         (MemTxAttrs){.requester_id = s->msi_rrid}, &result);
    if (result != MEMTX_OK) {
        s->regs.err_info = FIELD_DP32(s->regs.err_info, ERR_INFO, MSI_WERR, 1);
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

static void iopmp_update_rule(RISCVIOPMPState *s, uint32_t entry_index)
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
    RISCVIOPMPState *s = RISCV_IOPMP(opaque);
    uint32_t rz = 0;
    uint32_t offset, idx;
    /* Start value for ERR_MFR.svi */
    uint16_t svi_s;

    switch (addr) {
    case A_VERSION:
        rz = FIELD_DP32(rz, VERSION, VENDOR, VENDER_VIRT);
        rz = FIELD_DP32(rz, VERSION, SPECVER, SPECVER_0_7_0);
        break;
    case A_IMPLEMENTATION:
        rz = IMPID_0_7_0;
        break;
    case A_HWCFG0:
        rz = FIELD_DP32(rz, HWCFG0, MDCFG_FMT, s->mdcfg_fmt);
        rz = FIELD_DP32(rz, HWCFG0, SRCMD_FMT, s->srcmd_fmt);
        rz = FIELD_DP32(rz, HWCFG0, TOR_EN, s->tor_en);
        rz = FIELD_DP32(rz, HWCFG0, SPS_EN, s->sps_en);
        rz = FIELD_DP32(rz, HWCFG0, USER_CFG_EN, 0);
        rz = FIELD_DP32(rz, HWCFG0, PRIENT_PROG, s->prient_prog);
        rz = FIELD_DP32(rz, HWCFG0, RRID_TRANSL_EN, s->rrid_transl_en);
        rz = FIELD_DP32(rz, HWCFG0, RRID_TRANSL_PROG, s->rrid_transl_prog);
        rz = FIELD_DP32(rz, HWCFG0, CHK_X, s->chk_x);
        rz = FIELD_DP32(rz, HWCFG0, NO_X, s->no_x);
        rz = FIELD_DP32(rz, HWCFG0, NO_W, s->no_w);
        rz = FIELD_DP32(rz, HWCFG0, STALL_EN, s->stall_en);
        rz = FIELD_DP32(rz, HWCFG0, PEIS, s->peis);
        rz = FIELD_DP32(rz, HWCFG0, PEES, s->pees);
        rz = FIELD_DP32(rz, HWCFG0, MFR_EN, s->mfr_en);
        rz = FIELD_DP32(rz, HWCFG0, MD_ENTRY_NUM, s->md_entry_num);
        rz = FIELD_DP32(rz, HWCFG0, MD_NUM, s->md_num);
        rz = FIELD_DP32(rz, HWCFG0, ADDRH_EN, 1);
        rz = FIELD_DP32(rz, HWCFG0, ENABLE, s->enable);
        break;
    case A_HWCFG1:
        rz = FIELD_DP32(rz, HWCFG1, RRID_NUM, s->rrid_num);
        rz = FIELD_DP32(rz, HWCFG1, ENTRY_NUM, s->entry_num);
        break;
    case A_HWCFG2:
        rz = FIELD_DP32(rz, HWCFG2, PRIO_ENTRY, s->prio_entry);
        rz = FIELD_DP32(rz, HWCFG2, RRID_TRANSL, s->rrid_transl);
        break;
    case A_ENTRYOFFSET:
        rz = s->entry_offset;
        break;
    case A_MDSTALL:
        if (s->stall_en) {
            rz = s->regs.mdstall;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
        }
        break;
    case A_MDSTALLH:
        if (s->stall_en && s->md_num > 31) {
            rz = s->regs.mdstallh;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
        }
        break;
    case A_RRIDSCP:
        if (s->stall_en) {
            rz = s->regs.rridscp;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
        }
        break;
    case A_ERR_CFG:
        rz = s->regs.err_cfg;
        break;
    case A_MDLCK:
        if (s->srcmd_fmt == 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                __func__, (int)addr);
        } else {
            rz = s->regs.mdlck;
        }
        break;
    case A_MDLCKH:
        if (s->md_num < 31 || s->srcmd_fmt == 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                __func__, (int)addr);
        } else {
            rz = s->regs.mdlckh;
        }
        break;
    case A_MDCFGLCK:
        if (s->mdcfg_fmt != 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
            break;
        }
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
    case A_ERR_INFO:
        rz = s->regs.err_info;
        break;
    case A_ERR_MFR:
        if (!s->mfr_en) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
            break;
        }
        svi_s = s->svi;
        s->regs.err_info = FIELD_DP32(s->regs.err_info, ERR_INFO, SVC, 0);
        while (1) {
            if (s->svw[s->svi]) {
                if (rz == 0) {
                    /* First svw is found */
                    rz = FIELD_DP32(rz, ERR_MFR, SVW, s->svw[s->svi]);
                    rz = FIELD_DP32(rz, ERR_MFR, SVI, s->svi);
                    rz = FIELD_DP32(rz, ERR_MFR, SVS, 1);
                    /* Clear svw after read */
                    s->svw[s->svi] = 0;
                } else {
                    /* Other subsequent violation exists */
                    s->regs.err_info = FIELD_DP32(s->regs.err_info, ERR_INFO,
                                                  SVC, 1);
                    break;
                }
            }
            s->svi++;
            if (s->svi > (s->rrid_num / 16) + 1) {
                s->svi = 0;
            }
            if (svi_s == s->svi) {
                /* rounded back to the same value */
                break;
            }
        }
        /* Set svi for next read */
        s->svi = FIELD_DP32(rz, ERR_MFR, SVI, s->svi);
        break;
    case A_ERR_MSIADDR:
        rz = s->regs.err_msiaddr;
        break;
    case A_ERR_MSIADDRH:
        rz = s->regs.err_msiaddrh;
        break;

    default:
        if (s->mdcfg_fmt == 0 &&
            addr >= A_MDCFG0 &&
            addr <= A_MDCFG0 + 4 * (s->md_num - 1)) {
            offset = addr - A_MDCFG0;
            if (offset % 4) {
                rz = 0;
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                              __func__, (int)addr);
            } else {
                idx = offset >> 2;
                rz = s->regs.mdcfg[idx];
            }
        } else if (s->srcmd_fmt == 0 &&
                   addr >= A_SRCMD_EN0 &&
                   addr <= A_SRCMD_WH0 + 32 * (s->rrid_num - 1)) {
            offset = addr - A_SRCMD_EN0;
            idx = offset >> 5;
            offset &= 0x1f;

            if (s->sps_en || offset <= SRCMD_ENH_OFFSET) {
                switch (offset) {
                case SRCMD_EN_OFFSET:
                    rz = s->regs.srcmd_en[idx];
                    break;
                case SRCMD_ENH_OFFSET:
                    if (s->md_num > 31) {
                        rz = s->regs.srcmd_enh[idx];
                    } else {
                        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                      __func__, (int)addr);
                    }
                    break;
                case SRCMD_R_OFFSET:
                    rz = s->regs.srcmd_r[idx];
                    break;
                case SRCMD_RH_OFFSET:
                    if (s->md_num > 31) {
                        rz = s->regs.srcmd_rh[idx];
                    } else {
                        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                      __func__, (int)addr);
                    }
                    break;
                case SRCMD_W_OFFSET:
                    rz = s->regs.srcmd_w[idx];
                    break;
                case SRCMD_WH_OFFSET:
                    if (s->md_num > 31) {
                        rz = s->regs.srcmd_wh[idx];
                    } else {
                        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                      __func__, (int)addr);
                    }
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
        } else if (s->srcmd_fmt == 2 &&
                   addr >= A_SRCMD_PERM0 &&
                   addr <= A_SRCMD_PERMH0 + 32 * (s->md_num - 1)) {
            offset = addr - A_SRCMD_PERM0;
            idx = offset >> 5;
            offset &= 0x1f;
            switch (offset) {
            case SRCMD_PERM_OFFSET:
                rz = s->regs.srcmd_perm[idx];
                break;
            case SRCMD_PERMH_OFFSET:
                if (s->rrid_num > 16) {
                    rz = s->regs.srcmd_permh[idx];
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                  __func__, (int)addr);
                }
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                __func__, (int)addr);
                break;
            }
        } else if (addr >= s->entry_offset &&
                   addr <= s->entry_offset + ENTRY_USER_CFG_OFFSET +
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

static void update_rrid_stall(RISCVIOPMPState *s)
{
    bool exempt = FIELD_EX32(s->regs.mdstall, MDSTALL, EXEMPT);
    uint64_t stall_by_md = ((uint64_t)s->regs.mdstall |
                            ((uint64_t)s->regs.mdstallh << 32)) >> 1;
    uint64_t srcmd_en;
    bool reduction_or;
    if (s->srcmd_fmt != 2) {
        for (int rrid = 0; rrid < s->rrid_num; rrid++) {
            srcmd_en = ((uint64_t)s->regs.srcmd_en[rrid] |
                        ((uint64_t)s->regs.srcmd_enh[rrid] << 32)) >> 1;
            reduction_or = 0;
            if (srcmd_en & stall_by_md) {
                reduction_or = 1;
            }
            s->rrid_stall[rrid] = exempt ^ reduction_or;
        }
    } else {
        for (int rrid = 0; rrid < s->rrid_num; rrid++) {
            if (stall_by_md) {
                s->rrid_stall[rrid] = 1;
            } else {
                s->rrid_stall[rrid] = 0;
            }
        }
    }
    iopmp_iommu_notify(s);
}

static inline void resume_stall(RISCVIOPMPState *s)
{
    for (int rrid = 0; rrid < s->rrid_num; rrid++) {
        s->rrid_stall[rrid] = 0;
    }
    iopmp_iommu_notify(s);
}

static void
iopmp_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    RISCVIOPMPState *s = RISCV_IOPMP(opaque);
    uint32_t offset, idx;
    uint32_t value32 = value;
    uint64_t mdlck;
    uint32_t value_f;
    uint32_t rrid;
    uint32_t op;
    trace_iopmp_write(addr, value32);

    switch (addr) {
    case A_VERSION: /* RO */
        break;
    case A_IMPLEMENTATION: /* RO */
        break;
    case A_HWCFG0:
        if (FIELD_EX32(value32, HWCFG0, RRID_TRANSL_PROG)) {
            /* W1C */
            s->rrid_transl_prog = 0;
        }
        if (FIELD_EX32(value32, HWCFG0, PRIENT_PROG)) {
            /* W1C */
            s->prient_prog = 0;
        }
        if (!s->enable && s->mdcfg_fmt == 2) {
            /* Locked by enable bit */
            s->md_entry_num = FIELD_EX32(value32, HWCFG0, MD_ENTRY_NUM);
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
            iopmp_iommu_notify(s);
        }
        if (s->rrid_transl_prog) {
            s->rrid_transl = FIELD_EX32(value32, HWCFG2, RRID_TRANSL);
            iopmp_iommu_notify(s);
        }
        break;
    case A_ENTRYOFFSET:
        break;
    case A_MDSTALL:
        if (s->stall_en) {
            s->regs.mdstall = value32;
            if (value32) {
                s->is_stalled = 1;
            } else {
                /* Resume if stall, stallh == 0 */
                if (s->regs.mdstallh == 0) {
                    s->is_stalled = 0;
                }
            }
            update_rrid_stall(s);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
        }
        break;
    case A_MDSTALLH:
        if (s->stall_en) {
            s->regs.mdstallh = value32;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
        }
        break;
    case A_RRIDSCP:
        if (s->stall_en) {
            rrid = FIELD_EX32(value32, RRIDSCP, RRID);
            op = FIELD_EX32(value32, RRIDSCP, OP);
            if (op == RRIDSCP_OP_RESERVED) {
                break;
            }
            s->regs.rridscp = value32;
            if (rrid > s->rrid_num) {
                s->regs.rridscp = FIELD_DP32(s->regs.rridscp, RRIDSCP, STAT,
                                             RRIDSCP_STAT_RRID_NO_IMPL);
                break;
            }
            switch (op) {
            case RRIDSCP_OP_QUERY:
                if (s->is_stalled) {
                    s->regs.rridscp =
                        FIELD_DP32(s->regs.rridscp, RRIDSCP, STAT,
                                    0x2 >> s->rrid_stall[rrid]);
                } else {
                    s->regs.rridscp = FIELD_DP32(s->regs.rridscp, RRIDSCP,
                                                    STAT,
                                                    RRIDSCP_STAT_NO_STALL);
                }
                break;
            case RRIDSCP_OP_STALL:
                s->rrid_stall[rrid] = 1;
                break;
            case RRIDSCP_OP_NO_STALL:
                s->rrid_stall[rrid] = 0;
                break;
            default:
                break;
            }
            if (s->is_stalled) {
                iopmp_iommu_notify(s);
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
        }
        break;
    case A_ERR_CFG:
        if (!FIELD_EX32(s->regs.err_cfg, ERR_CFG, L)) {
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, L,
                FIELD_EX32(value32, ERR_CFG, L));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, IE,
                FIELD_EX32(value32, ERR_CFG, IE));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, RS,
                FIELD_EX32(value32, ERR_CFG, RS));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, MSI_EN,
                FIELD_EX32(value32, ERR_CFG, MSI_EN));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG,
                STALL_VIOLATION_EN, FIELD_EX32(value32, ERR_CFG,
                                               STALL_VIOLATION_EN));
            s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, MSIDATA,
                FIELD_EX32(value32, ERR_CFG, MSIDATA));
        }
        break;
    case A_MDLCK:
        if (s->srcmd_fmt == 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
        } else if (!FIELD_EX32(s->regs.mdlck, MDLCK, L)) {
            /* sticky to 1 */
            s->regs.mdlck |= value32;
            if (s->md_num <= 31) {
                s->regs.mdlck = extract32(s->regs.mdlck, 0, s->md_num + 1);
            }
        }
        break;
    case A_MDLCKH:
        if (s->md_num < 31 || s->srcmd_fmt == 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                          __func__, (int)addr);
        } else if (!FIELD_EX32(s->regs.mdlck, MDLCK, L)) {
            /* sticky to 1 */
            s->regs.mdlckh |= value32;
            s->regs.mdlck = extract32(s->regs.mdlck, 0, s->md_num - 31);
        }
        break;
    case A_MDCFGLCK:
        if (s->mdcfg_fmt != 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                __func__, (int)addr);
            break;
        }
        if (!FIELD_EX32(s->regs.mdcfglck, MDCFGLCK, L)) {
            value_f = FIELD_EX32(value32, MDCFGLCK, F);
            if (value_f > FIELD_EX32(s->regs.mdcfglck, MDCFGLCK, F)) {
                s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, F,
                                              value_f);
            }
            s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, L,
                                          FIELD_EX32(value32, MDCFGLCK, L));
        }
        break;
    case A_ENTRYLCK:
        if (!(FIELD_EX32(s->regs.entrylck, ENTRYLCK, L))) {
            value_f = FIELD_EX32(value32, ENTRYLCK, F);
            if (value_f > FIELD_EX32(s->regs.entrylck, ENTRYLCK, F)) {
                s->regs.entrylck = FIELD_DP32(s->regs.entrylck, ENTRYLCK, F,
                                              value_f);
            }
            s->regs.entrylck = FIELD_DP32(s->regs.entrylck, ENTRYLCK, L,
                                          FIELD_EX32(value32, ENTRYLCK, L));
        }
    case A_ERR_REQADDR: /* RO */
        break;
    case A_ERR_REQADDRH: /* RO */
        break;
    case A_ERR_REQID: /* RO */
        break;
    case A_ERR_INFO:
        if (FIELD_EX32(value32, ERR_INFO, V)) {
            s->regs.err_info = FIELD_DP32(s->regs.err_info, ERR_INFO, V, 0);
            qemu_set_irq(s->irq, 0);
            if (s->clic_irq) {
                qemu_set_irq(s->clic_irq, 0);
            }
        }
        if (FIELD_EX32(value32, ERR_INFO, MSI_WERR)) {
            s->regs.err_info = FIELD_DP32(s->regs.err_info, ERR_INFO, MSI_WERR,
                                          0);
        }
        break;
    case A_ERR_MFR:
        s->svi = FIELD_EX32(value32, ERR_MFR, SVI);
        break;
    case A_ERR_MSIADDR:
        if (!FIELD_EX32(s->regs.err_cfg, ERR_CFG, L)) {
            s->regs.err_msiaddr = value32;
        }
        break;

    case A_ERR_MSIADDRH:
        if (!FIELD_EX32(s->regs.err_cfg, ERR_CFG, L)) {
            s->regs.err_msiaddrh = value32;
        }
        break;

    default:
        if (s->mdcfg_fmt == 0 &&
            addr >= A_MDCFG0 &&
            addr <= A_MDCFG0 + 4 * (s->md_num - 1)) {
            offset = addr - A_MDCFG0;
            if (offset % 4) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                              __func__, (int)addr);
            } else {
                idx = offset >> 2;
                s->regs.mdcfg[idx] = FIELD_EX32(value32, MDCFG0, T);
                iopmp_iommu_notify(s);
            }
        } else if (s->srcmd_fmt == 0 &&
                   addr >= A_SRCMD_EN0 &&
                   addr <= A_SRCMD_WH0 + 32 * (s->rrid_num - 1)) {
            offset = addr - A_SRCMD_EN0;
            idx = offset >> 5;
            offset &= 0x1f;

            if (offset % 4 || (!s->sps_en && offset > SRCMD_ENH_OFFSET)) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                              __func__, (int)addr);
            } else if (FIELD_EX32(s->regs.srcmd_en[idx], SRCMD_EN0, L) == 0) {
                /* MD field is protected by mdlck */
                value32 = (value32 & ~s->regs.mdlck) |
                          (s->regs.srcmd_en[idx] & s->regs.mdlck);
                iopmp_iommu_notify(s);
                switch (offset) {
                case SRCMD_EN_OFFSET:
                    s->regs.srcmd_en[idx] =
                        FIELD_DP32(s->regs.srcmd_en[idx], SRCMD_EN0, L,
                                   FIELD_EX32(value32, SRCMD_EN0, L));
                    s->regs.srcmd_en[idx] =
                        FIELD_DP32(s->regs.srcmd_en[idx], SRCMD_EN0, MD,
                                   FIELD_EX32(value32, SRCMD_EN0, MD));
                    if (s->md_num <= 31) {
                        s->regs.srcmd_en[idx] = extract32(s->regs.srcmd_en[idx],
                                                          0, s->md_num + 1);
                    }
                    break;
                case SRCMD_ENH_OFFSET:
                    if (s->md_num > 31) {
                        s->regs.srcmd_enh[idx] = value32;
                        s->regs.srcmd_enh[idx] =
                            extract32(s->regs.srcmd_enh[idx], 0,
                                      s->md_num - 31);
                    } else {
                        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                      __func__, (int)addr);
                    }
                    break;
                case SRCMD_R_OFFSET:
                    s->regs.srcmd_r[idx] =
                        FIELD_DP32(s->regs.srcmd_r[idx], SRCMD_R0, MD,
                                   FIELD_EX32(value32, SRCMD_R0, MD));
                    if (s->md_num <= 31) {
                        s->regs.srcmd_r[idx] = extract32(s->regs.srcmd_r[idx],
                                                         0, s->md_num + 1);
                    }
                    break;
                case SRCMD_RH_OFFSET:
                    if (s->md_num > 31) {
                        s->regs.srcmd_rh[idx] = value32;
                        s->regs.srcmd_rh[idx] =
                            extract32(s->regs.srcmd_rh[idx], 0,
                                      s->md_num - 31);
                    } else {
                        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                      __func__, (int)addr);
                    }
                    break;
                case SRCMD_W_OFFSET:
                    s->regs.srcmd_w[idx] =
                        FIELD_DP32(s->regs.srcmd_w[idx], SRCMD_W0, MD,
                                   FIELD_EX32(value32, SRCMD_W0, MD));
                    if (s->md_num <= 31) {
                        s->regs.srcmd_w[idx] = extract32(s->regs.srcmd_w[idx],
                                                         0, s->md_num + 1);
                    }
                    break;
                case SRCMD_WH_OFFSET:
                    if (s->md_num > 31) {
                        s->regs.srcmd_wh[idx] = value32;
                        s->regs.srcmd_wh[idx] =
                            extract32(s->regs.srcmd_wh[idx], 0,
                                      s->md_num - 31);
                    } else {
                        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                      __func__, (int)addr);
                    }
                    break;
                default:
                    break;
                }
            }
        } else if (s->srcmd_fmt == 2 &&
                   addr >= A_SRCMD_PERM0 &&
                   addr <= A_SRCMD_PERMH0 + 32 * (s->md_num - 1)) {
            offset = addr - A_SRCMD_PERM0;
            idx = offset >> 5;
            offset &= 0x1f;
            /* mdlck lock bit is removed */
            mdlck = ((uint64_t)s->regs.mdlck |
                     ((uint64_t)s->regs.mdlckh << 32)) >> 1;
            iopmp_iommu_notify(s);
            switch (offset) {
            case SRCMD_PERM_OFFSET:
                /* srcmd_perm[md] is protect by mdlck */
                if (((mdlck >> idx) & 0x1) == 0) {
                    s->regs.srcmd_perm[idx] = value32;
                }
                if (s->rrid_num <= 16) {
                    s->regs.srcmd_perm[idx] = extract32(s->regs.srcmd_perm[idx],
                                                        0, 2 * s->rrid_num);
                }
                break;
            case SRCMD_PERMH_OFFSET:
                if (s->rrid_num > 16) {
                    if (((mdlck >> idx) & 0x1) == 0) {
                        s->regs.srcmd_permh[idx] = value32;
                    }
                    s->regs.srcmd_permh[idx] =
                        extract32(s->regs.srcmd_permh[idx], 0,
                                  2 * (s->rrid_num - 16));
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                  __func__, (int)addr);
                }
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                __func__, (int)addr);
                break;
            }
        } else if (addr >= s->entry_offset &&
                   addr <= s->entry_offset + ENTRY_USER_CFG_OFFSET
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
                    if (!s->tor_en &&
                        FIELD_EX32(s->regs.entry[idx].cfg_reg,
                                   ENTRY_CFG, A) == IOPMP_AMATCH_TOR) {
                        s->regs.entry[idx].cfg_reg =
                            FIELD_DP32(s->regs.entry[idx].cfg_reg, ENTRY_CFG, A,
                                       IOPMP_AMATCH_OFF);
                    }
                    if (!s->peis) {
                        s->regs.entry[idx].cfg_reg =
                            FIELD_DP32(s->regs.entry[idx].cfg_reg, ENTRY_CFG,
                                       SIE, 0);
                    }
                    if (!s->pees) {
                        s->regs.entry[idx].cfg_reg =
                            FIELD_DP32(s->regs.entry[idx].cfg_reg, ENTRY_CFG,
                                       SEE, 0);
                    }
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

static void apply_sps_permission(RISCVIOPMPState *s, int rrid, int md, int *cfg)
{
    uint64_t srcmd_r, srcmd_w;
    srcmd_r = ((uint64_t)s->regs.srcmd_rh[rrid]) << 32 | s->regs.srcmd_r[rrid];
    srcmd_w = ((uint64_t)s->regs.srcmd_wh[rrid]) << 32 | s->regs.srcmd_w[rrid];
    if (((srcmd_r >> (md + 1)) & 0x1) == 0) {
        /* remove r&x permission and error suppression */
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, R, 0);
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, X, 0);
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, SIRE, 0);
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, SERE, 0);
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, SIXE, 0);
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, SEXE, 0);
    }
    if (((srcmd_w >> (md + 1)) & 0x1) == 0) {
        /* remove w permission and error suppression */
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, W, 0);
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, SIWE, 0);
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, SEWE, 0);
    }
}

static void apply_srcmdperm(RISCVIOPMPState *s, int rrid, int md, int *cfg)
{
    uint64_t srcmd_perm = ((uint64_t)s->regs.srcmd_permh[md]) << 32 |
                          s->regs.srcmd_perm[md];

    if (((srcmd_perm >> (2 * rrid)) & 0x1)) {
        /* add r&x permission */
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, R, 1);
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, X, 1);
    }
    if (((srcmd_perm >> (2 * rrid + 1)) & 0x1)) {
        /* add w permission */
        *cfg = FIELD_DP32(*cfg, ENTRY_CFG, W, 1);
    }
}

static inline void apply_no_chk_x(int *cfg)
{
    /* Use read permission for fetch */
    *cfg = FIELD_DP32(*cfg, ENTRY_CFG, X, FIELD_EX32(*cfg, ENTRY_CFG, R));
}

/* Apply permssions which are not specified by entry_cfg directly */
static inline void apply_extend_perms(RISCVIOPMPState *s, int rrid, int md,
                                      int *cfg)
{
    if (s->sps_en) {
        apply_sps_permission(s, rrid, md, cfg);
    }
    if (s->srcmd_fmt == 2) {
        apply_srcmdperm(s, rrid, md, cfg);
    }
    if (!s->chk_x) {
        apply_no_chk_x(cfg);
    }
}

static inline bool addr_in_entry(RISCVIOPMPState *s, hwaddr addr, int idx)
{
    return addr >= s->entry_addr[idx].sa && addr <= s->entry_addr[idx].ea;
}

/*
 * entry_range_list: The entry ranges from SRCMD and MDCFG to match
 * entry_idx: matched priority entry index or first non-priority entry index
 * cfg: entry cfg for matched priority entry and overlap permission and
 *           supression of matched on-priority entries
 * iopmp_tlb_size: If the entire TLB shares the same permission, the value is
 *                 set to TARGET_PAGE_SIZE, indicating that the cached
 *                 translation result can be reused. Otherwise, the value is 1,
 *                 meaning the address must be translated for each access.
 */
static IOPMPEntryHit match_entry_range(RISCVIOPMPState *s, int rrid,
                                       GSList *entry_range_list, hwaddr addr,
                                       hwaddr sa, hwaddr ea,
                                       int *entry_idx, int *cfg,
                                       hwaddr *iopmp_tlb_size)
{
    EntryRange *range;
    IOPMPEntryHit result = IOPMP_ENTRY_NO_HIT;
    *iopmp_tlb_size = TARGET_PAGE_SIZE;
    *cfg = 0;
    int i = 0;
    int s_idx, e_idx;
    int md;
    int curr_cfg;
    /*
     * Although a transaction may require multiple TLBs, consider the TLB of
     * the current translated address instead of the transaction's start and
     * end address.
     */
    hwaddr tlb_sa = addr & ~(TARGET_PAGE_SIZE - 1);
    hwaddr tlb_ea = tlb_sa + TARGET_PAGE_SIZE - 1;
    int tlb_cfg = 0;

    while (entry_range_list) {
        range = (EntryRange *)entry_range_list->data;
        s_idx = range->start_idx;
        e_idx = range->end_idx;
        md = range->md;
        if (e_idx > s->entry_num) {
            e_idx = s->entry_num;
        }
        for (i = s_idx; i < e_idx; i++) {
            if (FIELD_EX32(s->regs.entry[i].cfg_reg, ENTRY_CFG, A) ==
                IOPMP_AMATCH_OFF) {
                continue;
            }

            if (i < s->prio_entry) {
                if (iopmp_tlb_size != NULL &&
                    *iopmp_tlb_size == TARGET_PAGE_SIZE) {
                    if (!(s->entry_addr[i].sa <= tlb_sa &&
                          s->entry_addr[i].ea >= tlb_ea) &&
                        ((s->entry_addr[i].sa >= tlb_sa &&
                          s->entry_addr[i].sa <= tlb_ea) ||
                         (s->entry_addr[i].ea >= tlb_sa &&
                          s->entry_addr[i].ea <= tlb_ea))) {
                        /*
                         * A higher-priority entry exists within the TLB page
                         * but does not cover the entire page. This means
                         * that at least two kinds of permissions are present
                         * within the TLB page.
                         */
                        *iopmp_tlb_size = 1;
                    }
                }
                if (addr_in_entry(s, sa, i)) {
                    if (addr_in_entry(s, ea, i)) {
                            *entry_idx = i;
                            *cfg = s->regs.entry[i].cfg_reg;
                            apply_extend_perms(s, rrid, md, cfg);
                            return IOPMP_ENTRY_HIT;
                    } else {
                        *entry_idx = i;
                        return IOPMP_ENTRY_PAR_HIT;
                    }
                } else if (addr_in_entry(s, ea, i)) {
                    *entry_idx = i;
                    return IOPMP_ENTRY_PAR_HIT;
                } else if (sa < s->entry_addr[i].sa &&
                           ea > s->entry_addr[i].ea) {
                    *entry_idx = i;
                    return IOPMP_ENTRY_PAR_HIT;
                }
            } else {
                if (addr_in_entry(s, sa, i) && addr_in_entry(s, ea, i)) {
                    result = IOPMP_ENTRY_HIT;
                    if (*entry_idx == -1) {
                        /* record first matched non-priorty entry */
                        *entry_idx = i;
                    }
                    curr_cfg = s->regs.entry[i].cfg_reg;
                    apply_extend_perms(s, rrid, md, &curr_cfg);
                    *cfg |= curr_cfg;
                    /* Check this entry is also match tlb or not */
                    if (*iopmp_tlb_size != 1 && addr_in_entry(s, tlb_sa, i) &&
                        addr_in_entry(s, tlb_ea, i)) {
                        tlb_cfg |= curr_cfg;
                        if ((tlb_cfg & 0x7) == 0x7) {
                            /* Already have RWX permission */
                            *cfg = tlb_cfg;
                            return result;
                        }
                    }
                    if ((*cfg & 0x7) == 0x7 && *iopmp_tlb_size == 1) {
                        /*
                         * Already have RWX permission and a higher priority
                         * entry in the same TLB page, checking the
                         * next non-priority entry is unnecessary
                         */
                        return result;
                    }
                }
            }
        }
        entry_range_list = entry_range_list->next;
    }
    if (result == IOPMP_ENTRY_HIT && (*cfg & 0x7) != (tlb_cfg & 0x7)) {
        /*
         * For non-priority entry hit, if the tlb permssion is different to
         * matched entries permssion, reduce iopmp_tlb_size
         */
        *iopmp_tlb_size = 1;
    }
    return result;
}

static void entry_range_list_data_free(gpointer data)
{
    EntryRange *range = (EntryRange *)data;
    g_free(range);
}

static IOPMPEntryHit match_entry_srcmd(RISCVIOPMPState *s, int rrid,
                                       hwaddr addr, hwaddr start_addr,
                                       hwaddr end_addr, int *match_entry_idx,
                                       int *cfg, hwaddr *iopmp_tlb_size)
{
    IOPMPEntryHit result = IOPMP_ENTRY_NO_HIT;
    GSList *entry_range_list = NULL;
    uint64_t srcmd_en;
    int k;
    EntryRange *range;
    int md_idx;
    if (s->srcmd_fmt == 1) {
        range = g_malloc(sizeof(*range));
        md_idx = rrid;
        range->md = md_idx;
        if (s->mdcfg_fmt == 0) {
            if (md_idx > 0) {
                range->start_idx = FIELD_EX32(s->regs.mdcfg[md_idx - 1],
                                              MDCFG0, T);
            } else {
                range->start_idx = 0;
            }
            range->end_idx = FIELD_EX32(s->regs.mdcfg[md_idx], MDCFG0, T);
        } else {
            k = s->md_entry_num + 1;
            range->start_idx = md_idx * k;
            range->end_idx = (md_idx + 1) * k;
        }
        entry_range_list = g_slist_append(entry_range_list, range);
    } else {
        for (md_idx = 0; md_idx < s->md_num; md_idx++) {
            srcmd_en = ((uint64_t)s->regs.srcmd_en[rrid] |
                        ((uint64_t)s->regs.srcmd_enh[rrid] << 32)) >> 1;
            range = NULL;
            if (s->srcmd_fmt == 2) {
                /* All entries are needed to be checked in srcmd_fmt2 */
                srcmd_en = -1;
            }
            if (srcmd_en & (1ULL << md_idx)) {
                range = g_malloc(sizeof(*range));
                range->md = md_idx;
                if (s->mdcfg_fmt == 0) {
                    if (md_idx > 0) {
                        range->start_idx = FIELD_EX32(s->regs.mdcfg[md_idx - 1],
                                                      MDCFG0, T);
                    } else {
                        range->start_idx = 0;
                    }
                    range->end_idx = FIELD_EX32(s->regs.mdcfg[md_idx],
                                                MDCFG0, T);
                } else {
                    k = s->md_entry_num + 1;
                    range->start_idx = md_idx * k;
                    range->end_idx = (md_idx + 1) * k;
                }
            }
            /*
             * There is no more memory domain after it enconter an invalid
             * mdcfg. Note that the behavior of mdcfg(t+1).f < mdcfg(t).f is
             * implementation-dependent.
             */
            if (range != NULL) {
                if (range->end_idx < range->start_idx) {
                    break;
                }
                entry_range_list = g_slist_append(entry_range_list, range);
            }
        }
    }
    result = match_entry_range(s, rrid, entry_range_list, addr, start_addr,
                               end_addr, match_entry_idx, cfg, iopmp_tlb_size);
    g_slist_free_full(entry_range_list, entry_range_list_data_free);
    return result;
}

static MemTxResult iopmp_error_reaction(RISCVIOPMPState *s, uint32_t rrid,
                                        uint32_t eid, hwaddr addr,
                                        uint32_t etype, uint32_t ttype,
                                        uint32_t cfg, uint64_t *data)
{
    uint32_t error_id = 0;
    uint32_t error_info = 0;
    int offset;
    /* interrupt enable regarding the access */
    int ie;
    /* bus error enable */
    int be;
    int error_record;

    if (etype >= ERR_INFO_ETYPE_READ && etype <= ERR_INFO_ETYPE_WRITE) {
        offset = etype - ERR_INFO_ETYPE_READ;
        ie = (FIELD_EX32(s->regs.err_cfg, ERR_CFG, IE) &&
              !extract32(cfg, R_ENTRY_CFG_SIRE_SHIFT + offset, 1));
        be = (!FIELD_EX32(s->regs.err_cfg, ERR_CFG, RS) &&
              !extract32(cfg, R_ENTRY_CFG_SERE_SHIFT + offset, 1));
    } else {
        ie = FIELD_EX32(s->regs.err_cfg, ERR_CFG, IE);
        be = !FIELD_EX32(s->regs.err_cfg, ERR_CFG, RS);
    }
    error_record = (ie | be) && !(s->transaction_state[rrid].running &&
                                  s->transaction_state[rrid].error_reported);
    if (error_record) {
        if (s->transaction_state[rrid].running) {
            s->transaction_state[rrid].error_reported = true;
        }
        /* Update error information if the error is not suppressed */
        if (!FIELD_EX32(s->regs.err_info, ERR_INFO, V)) {
            error_id = FIELD_DP32(error_id, ERR_REQID, EID, eid);
            error_id = FIELD_DP32(error_id, ERR_REQID, RRID, rrid);
            error_info = FIELD_DP32(error_info, ERR_INFO, ETYPE, etype);
            error_info = FIELD_DP32(error_info, ERR_INFO, TTYPE, ttype);
            s->regs.err_info = error_info;
            s->regs.err_info = FIELD_DP32(s->regs.err_info, ERR_INFO, V, 1);
            s->regs.err_reqid = error_id;
            /* addr[LEN+2:2] */
            s->regs.err_reqaddr = addr >> 2;
            if (ie) {
                if (FIELD_EX32(s->regs.err_cfg, ERR_CFG, MSI_EN)) {
                    iopmp_msi_send(s);
                } else {
                    qemu_set_irq(s->irq, 1);
                    if (s->clic_irq) {
                        qemu_set_irq(s->clic_irq, 1);
                    }
                }
            }
        } else if (s->mfr_en) {
            s->svw[rrid / 16] |= (1 << (rrid % 16));
            s->regs.err_info = FIELD_DP32(s->regs.err_info, ERR_INFO, SVC, 1);
        }
    }
    if (be) {
        return MEMTX_ERROR;
    } else {
        if (data) {
            *data = s->err_rdata;
        }
        return MEMTX_OK;
    }
}

static IOMMUTLBEntry iopmp_translate(IOMMUMemoryRegion *iommu, hwaddr addr,
                                     IOMMUAccessFlags flags, int iommu_idx)
{
    int rrid = iommu_idx;
    RISCVIOPMPState *s = RISCV_IOPMP(container_of(iommu,
                                                  RISCVIOPMPState, iommu));
    hwaddr start_addr, end_addr;
    int entry_idx = -1;
    hwaddr iopmp_tlb_size = TARGET_PAGE_SIZE;
    int match_cfg = 0;
    IOPMPEntryHit result;
    IOPMPPermission iopmp_perm;
    bool lock = false;
    IOMMUTLBEntry entry = {
        .target_as = &s->downstream_as,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = 0,
        .perm = IOMMU_NONE,
    };

    if (!s->enable) {
        /* Bypass IOPMP */
        entry.addr_mask = TARGET_PAGE_SIZE - 1,
        entry.perm = IOMMU_RW;
        return entry;
    }

    /* unknown RRID */
    if (rrid >= s->rrid_num) {
        entry.target_as = &s->blocked_rwx_as;
        entry.perm = IOMMU_RW;
        return entry;
    }

    if (s->is_stalled && s->rrid_stall[rrid]) {
        if (FIELD_EX32(s->regs.err_cfg, ERR_CFG, STALL_VIOLATION_EN)) {
            entry.target_as = &s->blocked_rwx_as;
            entry.perm = IOMMU_RW;
            return entry;
        } else {
            if (bql_locked()) {
                bql_unlock();
                lock = true;
            }
            while (s->is_stalled && s->rrid_stall[rrid]) {
                ;
            }
            if (lock) {
                bql_lock();
            }
        }
    }

    if (s->transaction_state[rrid].running == true) {
        start_addr = s->transaction_state[rrid].start_addr;
        end_addr = s->transaction_state[rrid].end_addr;
    } else {
        /* No transaction information, use the same address */
        start_addr = addr;
        end_addr = addr;
    }
    result = match_entry_srcmd(s, rrid, addr, start_addr, end_addr, &entry_idx,
                               &match_cfg, &iopmp_tlb_size);
    entry.addr_mask = iopmp_tlb_size - 1;
    /* Remove permission for no_x, no_w*/
    if (s->chk_x && s->no_x) {
        match_cfg = FIELD_DP32(match_cfg, ENTRY_CFG, X, 0);
    }
    if (s->no_w) {
        match_cfg = FIELD_DP32(match_cfg, ENTRY_CFG, W, 0);
    }
    if (result == IOPMP_ENTRY_HIT) {
        iopmp_perm = match_cfg & IOPMP_RWX;
        if (flags) {
            if ((iopmp_perm & flags) == 0) {
                /* Permission denied */
                entry.target_as = &s->blocked_rwx_as;
                entry.perm = IOMMU_RW;
            } else {
                entry.target_as = &s->downstream_as;
                if (s->rrid_transl_en) {
                    /* Indirectly access for rrid_transl */
                    entry.target_as = &s->full_as;
                }
                entry.perm = iopmp_perm;
            }
        } else {
            /* CPU access with IOMMU_NONE flag */
            if (iopmp_perm & IOPMP_XO) {
                if ((iopmp_perm & IOPMP_RW) == IOPMP_RW) {
                    entry.target_as = &s->downstream_as;
                    if (s->rrid_transl_en) {
                        entry.target_as = &s->full_as;
                    }
                } else if ((iopmp_perm & IOPMP_RW) == IOPMP_RO) {
                    entry.target_as = &s->blocked_w_as;
                } else if ((iopmp_perm & IOPMP_RW) == IOPMP_WO) {
                    entry.target_as = &s->blocked_r_as;
                } else {
                    entry.target_as = &s->blocked_rw_as;
                }
            } else {
                if ((iopmp_perm & IOPMP_RW) == IOMMU_RW) {
                    entry.target_as = &s->blocked_x_as;
                } else if ((iopmp_perm & IOPMP_RW) == IOPMP_RO) {
                    entry.target_as = &s->blocked_wx_as;
                } else if ((iopmp_perm & IOPMP_RW) == IOPMP_WO) {
                    entry.target_as = &s->blocked_rx_as;
                } else {
                    entry.target_as = &s->blocked_rwx_as;
                }
            }
            entry.perm = IOMMU_RW;
        }
    } else {
        /* CPU access with IOMMU_NONE flag no_hit or par_hit */
        entry.target_as = &s->blocked_rwx_as;
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

static MemTxResult iopmp_permssion_write(void *opaque, hwaddr addr,
                                         uint64_t value, unsigned size,
                                         MemTxAttrs attrs)
{
    MemTxResult result;
    int rrid = attrs.requester_id;
    bool sent_info = false;
    RISCVIOPMPTxnInfo signal;
    RISCVIOPMPState *s = RISCV_IOPMP(opaque);
    if (s->rrid_transl_en) {
        if (s->transaction_state[rrid].running && s->send_ss) {
            sent_info = true;
            signal.rrid = s->rrid_transl;
            signal.start_addr = s->transaction_state[rrid].start_addr;
            signal.end_addr = s->transaction_state[rrid].end_addr;
            signal.stage = s->transaction_state[rrid].stage + 1;
            /* Send transaction information to next stage iopmp. */
            stream_push(s->send_ss, (uint8_t *)&signal, sizeof(signal), 0);
        }
        attrs.requester_id = s->rrid_transl;
    }
    result = address_space_write(&s->downstream_as, addr, attrs, &value, size);
    if (sent_info) {
        stream_push(s->send_ss, (uint8_t *)&signal, sizeof(signal), 1);
    }
    return result;
}

static MemTxResult iopmp_permssion_read(void *opaque, hwaddr addr,
                                        uint64_t *pdata, unsigned size,
                                        MemTxAttrs attrs)
{
    MemTxResult result;
    int rrid = attrs.requester_id;
    bool sent_info = false;
    RISCVIOPMPTxnInfo signal;
    RISCVIOPMPState *s = RISCV_IOPMP(opaque);
    if (s->rrid_transl_en) {
        if (s->transaction_state[rrid].running && s->send_ss) {
            sent_info = true;
            signal.rrid = s->rrid_transl;
            signal.start_addr = s->transaction_state[rrid].start_addr;
            signal.end_addr = s->transaction_state[rrid].end_addr;
            signal.stage = s->transaction_state[rrid].stage + 1;
            /* Send transaction information to next stage iopmp. */
            stream_push(s->send_ss, (uint8_t *)&signal, sizeof(signal), 0);
        }
        attrs.requester_id = s->rrid_transl;
    }
    result = address_space_read(&s->downstream_as, addr, attrs, pdata, size);
    if (sent_info) {
        stream_push(s->send_ss, (uint8_t *)&signal, sizeof(signal), 1);
    }
    return result;
}

static MemTxResult iopmp_handle_block(void *opaque, hwaddr addr,
                                      uint64_t *data, unsigned size,
                                      MemTxAttrs attrs,
                                      IOPMPAccessType access_type)
{
    RISCVIOPMPState *s = RISCV_IOPMP(opaque);
    int entry_idx;
    int rrid = attrs.requester_id;
    int result;
    hwaddr start_addr, end_addr;
    IOPMPErrInfoETYPE etype;
    IOPMPErrInfoTTYPE ttype;
    ttype = access_type;
    hwaddr iopmp_tlb_size = TARGET_PAGE_SIZE;
    int match_cfg = 0;
    /* unknown RRID */
    if (rrid >= s->rrid_num) {
        etype = ERR_INFO_ETYPE_RRID;
        return iopmp_error_reaction(s, rrid, 0, addr, etype, ttype, 0, data);
    }

    if (s->is_stalled && s->rrid_stall[rrid]) {
        etype = ERR_INFO_ETYPE_STALL;
        return iopmp_error_reaction(s, rrid, 0, addr, etype, ttype, 0, data);
    }

    if ((access_type == IOPMP_ACCESS_FETCH && s->no_x) ||
        (access_type == IOPMP_ACCESS_WRITE && s->no_w)) {
        etype = ERR_INFO_ETYPE_NOHIT;
        return iopmp_error_reaction(s, rrid, 0, addr, etype, ttype, 0, data);
    }

    if (s->transaction_state[rrid].running == true) {
        start_addr = s->transaction_state[rrid].start_addr;
        end_addr = s->transaction_state[rrid].end_addr;
    } else {
        /* No transaction information, use the same address */
        start_addr = addr;
        end_addr = addr;
    }

    /* matching again to get eid */
    result = match_entry_srcmd(s, rrid, addr, start_addr, end_addr,
                               &entry_idx, &match_cfg,
                               &iopmp_tlb_size);
    if (result == IOPMP_ENTRY_HIT) {
        etype = access_type;
    } else if (result == IOPMP_ENTRY_PAR_HIT) {
        /* Error supperssion per entry is only for all-byte matched entry */
        etype = ERR_INFO_ETYPE_PARHIT;
    } else {
        etype = ERR_INFO_ETYPE_NOHIT;
        entry_idx = 0;
    }
    return iopmp_error_reaction(s, rrid, entry_idx, start_addr, etype, ttype,
                                match_cfg, data);
}

static MemTxResult iopmp_block_write(void *opaque, hwaddr addr, uint64_t value,
                                     unsigned size, MemTxAttrs attrs)
{
    return iopmp_handle_block(opaque, addr, NULL, size, attrs,
                              IOPMP_ACCESS_WRITE);
}

static MemTxResult iopmp_block_read(void *opaque, hwaddr addr, uint64_t *pdata,
                                    unsigned size, MemTxAttrs attrs)
{
    return iopmp_handle_block(opaque, addr, pdata, size, attrs,
                              IOPMP_ACCESS_READ);
}

static MemTxResult iopmp_block_fetch(void *opaque, hwaddr addr, uint64_t *pdata,
                                     unsigned size, MemTxAttrs attrs)
{
    RISCVIOPMPState *s = RISCV_IOPMP(opaque);
    if (s->chk_x) {
        return iopmp_handle_block(opaque, addr, pdata, size, attrs,
                                  IOPMP_ACCESS_FETCH);
    }
    /* Use read reaction for fetch if chk_x == 0 */
    return iopmp_handle_block(opaque, addr, pdata, size, attrs,
                              IOPMP_ACCESS_READ);
}

static const MemoryRegionOps iopmp_block_rw_ops = {
    .fetch_with_attrs = iopmp_permssion_read,
    .read_with_attrs = iopmp_block_read,
    .write_with_attrs = iopmp_block_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static const MemoryRegionOps iopmp_block_w_ops = {
    .fetch_with_attrs = iopmp_permssion_read,
    .read_with_attrs = iopmp_permssion_read,
    .write_with_attrs = iopmp_block_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static const MemoryRegionOps iopmp_block_r_ops = {
    .fetch_with_attrs = iopmp_permssion_read,
    .read_with_attrs = iopmp_block_read,
    .write_with_attrs = iopmp_permssion_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static const MemoryRegionOps iopmp_block_rwx_ops = {
    .fetch_with_attrs = iopmp_block_fetch,
    .read_with_attrs = iopmp_block_read,
    .write_with_attrs = iopmp_block_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static const MemoryRegionOps iopmp_block_wx_ops = {
    .fetch_with_attrs = iopmp_block_fetch,
    .read_with_attrs = iopmp_permssion_read,
    .write_with_attrs = iopmp_block_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static const MemoryRegionOps iopmp_block_rx_ops = {
    .fetch_with_attrs = iopmp_block_fetch,
    .read_with_attrs = iopmp_block_read,
    .write_with_attrs = iopmp_permssion_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static const MemoryRegionOps iopmp_block_x_ops = {
    .fetch_with_attrs = iopmp_block_fetch,
    .read_with_attrs = iopmp_permssion_read,
    .write_with_attrs = iopmp_permssion_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static const MemoryRegionOps iopmp_full_ops = {
    .fetch_with_attrs = iopmp_permssion_read,
    .read_with_attrs = iopmp_permssion_read,
    .write_with_attrs = iopmp_permssion_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static void iopmp_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    RISCVIOPMPState *s = RISCV_IOPMP(dev);
    uint64_t size, mmio_size;

    size = -1ULL;

    if (s->srcmd_fmt > 2) {
        error_setg(errp, "Invalid IOPMP srcmd_fmt");
        error_append_hint(errp, "Valid values are 0, 1, and 2.\n");
        return;
    }

    if (s->mdcfg_fmt > 2) {
        error_setg(errp, "Invalid IOPMP mdcfg_fmt");
        error_append_hint(errp, "Valid values are 0, 1, and 2.\n");
        return;
    }

    if (s->srcmd_fmt != 0) {
        /* SPS is only supported in srcmd_fmt0 */
        s->sps_en = false;
    }

    s->md_num = MIN(s->md_num, IOPMP_MAX_MD_NUM);
    if (s->srcmd_fmt == 1) {
        /* Each RRID has one MD */
        s->md_num = MIN(s->md_num, s->rrid_num);
    }
    s->md_entry_num = s->default_md_entry_num;
    /* If md_entry_num is fixed, entry_num = md_num * (md_entry_num + 1) */
    if (s->mdcfg_fmt == 1) {
        s->entry_num = s->md_num * (s->md_entry_num + 1);
    }

    s->prient_prog = s->default_prient_prog;
    if (s->srcmd_fmt == 0) {
        s->rrid_num = MIN(s->rrid_num, IOPMP_SRCMDFMT0_MAX_RRID_NUM);
    } else if (s->srcmd_fmt == 1) {
        s->rrid_num = MIN(s->rrid_num, s->md_num);
    } else {
        s->rrid_num = MIN(s->rrid_num, IOPMP_SRCMDFMT2_MAX_RRID_NUM);
    }
    s->prio_entry = MIN(s->default_prio_entry, s->entry_num);
    s->rrid_transl_prog = s->default_rrid_transl_prog;
    s->rrid_transl = s->default_rrid_transl;

    s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, MSI_EN,
                                 s->default_msi_en);
    s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, MSIDATA,
                                 s->default_msidata);
    s->regs.err_msiaddr = s->default_err_msiaddr;
    s->regs.err_msiaddrh = s->default_err_msiaddrh;

    s->regs.mdcfg = g_malloc0(s->md_num * sizeof(uint32_t));
    if (s->srcmd_fmt != 2) {
        s->regs.srcmd_en = g_malloc0(s->rrid_num * sizeof(uint32_t));
        s->regs.srcmd_enh = g_malloc0(s->rrid_num * sizeof(uint32_t));
    } else {
        /* srcmd_perm */
        s->regs.srcmd_perm = g_malloc0(s->md_num * sizeof(uint32_t));
        s->regs.srcmd_permh = g_malloc0(s->md_num * sizeof(uint32_t));
    }

    if (s->sps_en) {
        s->regs.srcmd_r = g_malloc0(s->rrid_num * sizeof(uint32_t));
        s->regs.srcmd_rh = g_malloc0(s->rrid_num * sizeof(uint32_t));
        s->regs.srcmd_w = g_malloc0(s->rrid_num * sizeof(uint32_t));
        s->regs.srcmd_wh = g_malloc0(s->rrid_num * sizeof(uint32_t));
    }

    if (s->stall_en) {
        s->rrid_stall = g_malloc0(s->rrid_num * sizeof(bool));
    }

    if (s->mfr_en) {
        s->svw = g_malloc0((s->rrid_num / 16 + 1) * sizeof(uint16_t));
    }

    s->regs.entry = g_malloc0(s->entry_num * sizeof(RISCVIOPMPEntryRegs));
    s->entry_addr = g_malloc0(s->entry_num * sizeof(RISCVIOPMPEntryAddr));
    s->transaction_state = g_malloc0(s->rrid_num *
                                     sizeof(RISCVIOPMPTrasaction));
    mmio_size = MAX(A_SRCMD_EN0 + 32 * s->rrid_num,
                    s->entry_offset + 16 * s->entry_num);
    qemu_mutex_init(&s->iopmp_transaction_mutex);

    memory_region_init_iommu(&s->iommu, sizeof(s->iommu),
                             TYPE_RISCV_IOPMP_IOMMU_MEMORY_REGION,
                             obj, "riscv-iopmp-sysbus-iommu", UINT64_MAX);
    memory_region_init_io(&s->mmio, obj, &iopmp_ops,
                          s, "riscv-iopmp-regs", mmio_size);
    sysbus_init_mmio(sbd, &s->mmio);

    memory_region_init_io(&s->blocked_rw, NULL, &iopmp_block_rw_ops, s,
                          "riscv-iopmp-blocked-rw", size);
    memory_region_init_io(&s->blocked_w, NULL, &iopmp_block_w_ops, s,
                          "riscv-iopmp-blocked-w", size);
    memory_region_init_io(&s->blocked_r, NULL, &iopmp_block_r_ops, s,
                          "riscv-iopmp-blocked-r", size);
    memory_region_init_io(&s->blocked_rwx, NULL, &iopmp_block_rwx_ops, s,
                          "riscv-iopmp-blocked-rwx", size);
    memory_region_init_io(&s->blocked_wx, NULL, &iopmp_block_wx_ops, s,
                          "riscv-iopmp-blocked-wx", size);
    memory_region_init_io(&s->blocked_rx, NULL, &iopmp_block_rx_ops, s,
                          "riscv-iopmp-blocked-rx", size);
    memory_region_init_io(&s->blocked_x, NULL, &iopmp_block_x_ops, s,
                          "riscv-iopmp-blocked-x", size);
    memory_region_init_io(&s->full_mr, NULL, &iopmp_full_ops, s,
                          "riscv-iopmp-full", size);

    address_space_init(&s->blocked_rw_as, &s->blocked_rw,
                       "riscv-iopmp-blocked-rw-as");
    address_space_init(&s->blocked_w_as, &s->blocked_w,
                       "riscv-iopmp-blocked-w-as");
    address_space_init(&s->blocked_r_as, &s->blocked_r,
                       "riscv-iopmp-blocked-r-as");
    address_space_init(&s->blocked_rwx_as, &s->blocked_rwx,
                       "riscv-iopmp-blocked-rwx-as");
    address_space_init(&s->blocked_wx_as, &s->blocked_wx,
                       "riscv-iopmp-blocked-wx-as");
    address_space_init(&s->blocked_rx_as, &s->blocked_rx,
                       "riscv-iopmp-blocked-rx-as");
    address_space_init(&s->blocked_x_as, &s->blocked_x,
                       "riscv-iopmp-blocked-x-as");
    address_space_init(&s->full_as, &s->full_mr, "riscv-iopmp-full-as");

    object_initialize_child(OBJECT(s), "riscv_iopmp_streamsink",
                            &s->txn_info_sink,
                            TYPE_RISCV_IOPMP_STREAMSINK);
}

static void iopmp_reset_enter(Object *obj, ResetType type)
{
    RISCVIOPMPState *s = RISCV_IOPMP(obj);

    qemu_set_irq(s->irq, 0);
    if (s->clic_irq != NULL) {
        qemu_set_irq(s->clic_irq, 0);
    }
    if (s->srcmd_fmt != 2) {
        memset(s->regs.srcmd_en, 0, s->rrid_num * sizeof(uint32_t));
        memset(s->regs.srcmd_enh, 0, s->rrid_num * sizeof(uint32_t));
    } else {
        memset(s->regs.srcmd_en, 0, s->md_num * sizeof(uint32_t));
        memset(s->regs.srcmd_enh, 0, s->md_num * sizeof(uint32_t));
    }

    if (s->sps_en) {
        memset(s->regs.srcmd_r, 0, s->rrid_num * sizeof(uint32_t));
        memset(s->regs.srcmd_rh, 0, s->rrid_num * sizeof(uint32_t));
        memset(s->regs.srcmd_w, 0, s->rrid_num * sizeof(uint32_t));
        memset(s->regs.srcmd_wh, 0, s->rrid_num * sizeof(uint32_t));
    }

    if (s->stall_en) {
        memset((void *)s->rrid_stall, 0, s->rrid_num * sizeof(bool));
        s->is_stalled = 0;
    }

    if (s->mfr_en) {
        memset(s->svw, 0, (s->rrid_num / 16 + 1) * sizeof(uint16_t));
    }

    memset(s->regs.entry, 0, s->entry_num * sizeof(RISCVIOPMPEntryRegs));
    memset(s->entry_addr, 0, s->entry_num * sizeof(RISCVIOPMPEntryAddr));
    memset(s->transaction_state, 0,
           s->rrid_num * sizeof(RISCVIOPMPTrasaction));

    s->regs.mdlck = 0;
    s->regs.mdlckh = 0;
    s->regs.entrylck = 0;
    s->regs.mdcfglck = 0;
    s->regs.mdstall = 0;
    s->regs.mdstallh = 0;
    s->regs.rridscp = 0;
    s->regs.err_cfg = 0;
    s->regs.err_reqaddr = 0;
    s->regs.err_reqid = 0;
    s->regs.err_info = 0;

    s->prient_prog = s->default_prient_prog;
    s->rrid_transl_prog = s->default_rrid_transl_prog;
    s->md_entry_num = s->default_md_entry_num;
    s->rrid_transl = s->default_rrid_transl;
    s->prio_entry = MIN(s->default_prio_entry, s->entry_num);
    s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, MSI_EN,
                                 s->default_msi_en);
    s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, STALL_VIOLATION_EN,
                                 s->default_stall_violation_en);
    s->regs.err_cfg = FIELD_DP32(s->regs.err_cfg, ERR_CFG, MSIDATA,
                                 s->default_msidata);
    s->regs.err_msiaddr = s->default_err_msiaddr;
    s->regs.err_msiaddrh = s->default_err_msiaddrh;
    s->enable = 0;
}

static void iopmp_reset_hold(Object *obj)
{
    RISCVIOPMPState *s = RISCV_IOPMP(obj);

    qemu_set_irq(s->irq, 0);
    if (s->clic_irq != NULL) {
        qemu_set_irq(s->clic_irq, 0);
    }
}

static int iopmp_attrs_to_index(IOMMUMemoryRegion *iommu, MemTxAttrs attrs)
{
    return attrs.requester_id;
}

static int iopmp_num_indexes(IOMMUMemoryRegion *iommu)
{
    RISCVIOPMPState *s = RISCV_IOPMP(container_of(iommu, RISCVIOPMPState, iommu));
    return s->rrid_num;
}

static void iopmp_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = iopmp_translate;
    imrc->attrs_to_index = iopmp_attrs_to_index;
    imrc->num_indexes = iopmp_num_indexes;
}

static Property iopmp_property[] = {
    DEFINE_PROP_UINT32("mdcfg_fmt", RISCVIOPMPState, mdcfg_fmt, 1),
    DEFINE_PROP_UINT32("srcmd_fmt", RISCVIOPMPState, srcmd_fmt, 0),
    DEFINE_PROP_BOOL("tor_en", RISCVIOPMPState, tor_en, true),
    DEFINE_PROP_BOOL("sps_en", RISCVIOPMPState, sps_en, false),
    DEFINE_PROP_BOOL("prient_prog", RISCVIOPMPState, default_prient_prog, true),
    DEFINE_PROP_BOOL("rrid_transl_en", RISCVIOPMPState, rrid_transl_en, false),
    DEFINE_PROP_BOOL("rrid_transl_prog", RISCVIOPMPState,
                     default_rrid_transl_prog, false),
    DEFINE_PROP_BOOL("chk_x", RISCVIOPMPState, chk_x, true),
    DEFINE_PROP_BOOL("no_x", RISCVIOPMPState, no_x, false),
    DEFINE_PROP_BOOL("no_w", RISCVIOPMPState, no_w, false),
    DEFINE_PROP_BOOL("stall_en", RISCVIOPMPState, stall_en, false),
    DEFINE_PROP_BOOL("peis", RISCVIOPMPState, peis, true),
    DEFINE_PROP_BOOL("pees", RISCVIOPMPState, pees, true),
    DEFINE_PROP_BOOL("mfr_en", RISCVIOPMPState, mfr_en, true),
    DEFINE_PROP_UINT32("md_entry_num", RISCVIOPMPState, default_md_entry_num,
                       5),
    DEFINE_PROP_UINT32("md_num", RISCVIOPMPState, md_num, 8),
    DEFINE_PROP_UINT32("rrid_num", RISCVIOPMPState, rrid_num, 16),
    DEFINE_PROP_UINT32("entry_num", RISCVIOPMPState, entry_num, 48),
    DEFINE_PROP_UINT32("prio_entry", RISCVIOPMPState, default_prio_entry,
                       65535),
    DEFINE_PROP_UINT32("rrid_transl", RISCVIOPMPState, default_rrid_transl,
                       0x0),
    DEFINE_PROP_INT32("entry_offset", RISCVIOPMPState, entry_offset, 0x4000),
    DEFINE_PROP_UINT32("err_rdata", RISCVIOPMPState, err_rdata, 0x0),
    DEFINE_PROP_BOOL("msi_en", RISCVIOPMPState, default_msi_en, false),
    DEFINE_PROP_UINT32("msidata", RISCVIOPMPState, default_msidata, 12),
    DEFINE_PROP_BOOL("stall_violation_en", RISCVIOPMPState,
                     default_stall_violation_en, true),
    DEFINE_PROP_UINT32("err_msiaddr", RISCVIOPMPState, default_err_msiaddr,
                       0x24000000),
    DEFINE_PROP_UINT32("err_msiaddrh", RISCVIOPMPState, default_err_msiaddrh,
                       0x0),
    DEFINE_PROP_UINT32("msi_rrid", RISCVIOPMPState, msi_rrid, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void iopmp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    device_class_set_props(dc, iopmp_property);
    dc->realize = iopmp_realize;
    rc->phases.enter = iopmp_reset_enter;
    rc->phases.hold = iopmp_reset_hold;
}

static void iopmp_init(Object *obj)
{
    RISCVIOPMPState *s = RISCV_IOPMP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->clic_irq);
}

static const TypeInfo iopmp_info = {
    .name = TYPE_RISCV_IOPMP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVIOPMPState),
    .instance_init = iopmp_init,
    .class_init = iopmp_class_init,
};

static const TypeInfo iopmp_iommu_memory_region_info = {
    .name = TYPE_RISCV_IOPMP_IOMMU_MEMORY_REGION,
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .class_init = iopmp_iommu_memory_region_class_init,
};

DeviceState *iopmp_create(hwaddr addr, qemu_irq irq, qemu_irq clic_irq,
                          uint32_t rrid_num)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_IOPMP);
    qdev_prop_set_uint32(dev, "rrid_num", rrid_num);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    if (clic_irq) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, clic_irq);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}

/*
 * Alias subregions from the source memory region to the destination memory
 * region
 */
static void alias_memory_subregions(MemoryRegion *src_mr, MemoryRegion *dst_mr)
{
    int32_t priority;
    hwaddr addr;
    MemoryRegion *alias, *subregion;
    QTAILQ_FOREACH(subregion, &src_mr->subregions, subregions_link) {
        priority = subregion->priority;
        addr = subregion->addr;
        alias = g_malloc0(sizeof(MemoryRegion));
        memory_region_init_alias(alias, NULL, subregion->name, subregion, 0,
                                 memory_region_size(subregion));
        memory_region_add_subregion_overlap(dst_mr, addr, alias, priority);
    }
}

/*
 * Create downstream of system memory for IOPMP, and overlap memory region
 * specified in memmap with IOPMP translator. Make sure subregions are added to
 * system memory before call this function.
 */
void iopmp_setup_system_memory(DeviceState *dev, const MemMapEntry *memmap,
                               uint32_t map_entry_num, uint32_t stage)
{
    RISCVIOPMPState *s = RISCV_IOPMP(dev);
    uint32_t i;
    MemoryRegion *iommu_alias;
    MemoryRegion *target_mr = get_system_memory();
    MemoryRegion *downstream = g_malloc0(sizeof(MemoryRegion));
    memory_region_init(downstream, NULL, "iopmp_downstream",
                       memory_region_size(target_mr));
    /* Create a downstream which does not have iommu of iopmp */
    alias_memory_subregions(target_mr, downstream);

    for (i = 0; i < map_entry_num; i++) {
        /* Access to protected regions is managed through IOPMP */
        iommu_alias = g_new(MemoryRegion, 1);
        memory_region_init_alias(iommu_alias, NULL, "iommu_alias",
                                 MEMORY_REGION(&s->iommu), memmap[i].base,
                                 memmap[i].size);
        memory_region_add_subregion_overlap(target_mr, memmap[i].base,
                                            iommu_alias, 1);
    }
    s->downstream = downstream;
    address_space_init(&s->downstream_as, s->downstream,
                       "riscv-iopmp-downstream-as");
}

static size_t txn_info_push(StreamSink *txn_info_sink, unsigned char *buf,
                            size_t len, bool eop)
{
    RISCVIOPMPStreamSink *ss = RISCV_IOPMP_STREAMSINK(txn_info_sink);
    RISCVIOPMPState *s = RISCV_IOPMP(container_of(ss, RISCVIOPMPState,
                                      txn_info_sink));
    RISCVIOPMPTxnInfo *signal;
    uint32_t rrid;

    if (len != sizeof(RISCVIOPMPTxnInfo)) {
        return 0;
    }
    signal = (RISCVIOPMPTxnInfo *)buf;
    rrid = signal->rrid;

    if (s->transaction_state[rrid].running) {
        if (eop) {
            /* Finish the transaction */
            qemu_mutex_lock(&s->iopmp_transaction_mutex);
            s->transaction_state[rrid].running = 0;
            qemu_mutex_unlock(&s->iopmp_transaction_mutex);
            return 1;
        } else {
            /* Transaction is already running */
            return 0;
        }
    } else {
        /* Get the transaction info */
        s->transaction_state[rrid].supported = 1;
        qemu_mutex_lock(&s->iopmp_transaction_mutex);
        s->transaction_state[rrid].running = 1;
        qemu_mutex_unlock(&s->iopmp_transaction_mutex);

        s->transaction_state[rrid].start_addr = signal->start_addr;
        s->transaction_state[rrid].end_addr = signal->end_addr;
        s->transaction_state[rrid].error_reported = 0;
        s->transaction_state[rrid].stage = signal->stage;
        return 1;
    }
    return 0;
}

void iopmp_setup_sink(DeviceState *dev, StreamSink * ss)
{
     RISCVIOPMPState *s = RISCV_IOPMP(dev);
     s->send_ss = ss;
}

static void riscv_iopmp_streamsink_class_init(ObjectClass *klass, void *data)
{
    StreamSinkClass *ssc = STREAM_SINK_CLASS(klass);
    ssc->push = txn_info_push;
}

static const TypeInfo txn_info_sink = {
    .name = TYPE_RISCV_IOPMP_STREAMSINK,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(RISCVIOPMPStreamSink),
    .class_init = riscv_iopmp_streamsink_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SINK },
        { }
    },
};

static void iopmp_register_types(void)
{
    type_register_static(&iopmp_info);
    type_register_static(&txn_info_sink);
    type_register_static(&iopmp_iommu_memory_region_info);
}

type_init(iopmp_register_types);
