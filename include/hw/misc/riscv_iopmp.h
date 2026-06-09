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

#ifndef RISCV_IOPMP_H
#define RISCV_IOPMP_H

#include "hw/sysbus.h"
#include "qemu/typedefs.h"
#include "memory.h"
#include "exec/hwaddr.h"
#include "hw/stream.h"

#define TYPE_RISCV_IOPMP "riscv-iopmp"
OBJECT_DECLARE_SIMPLE_TYPE(RISCVIOPMPState, RISCV_IOPMP)

typedef struct RISCVIOPMPStreamSink {
    Object parent;
} RISCVIOPMPStreamSink;
#define TYPE_RISCV_IOPMP_STREAMSINK \
        "riscv-iopmp-streamsink"
DECLARE_INSTANCE_CHECKER(RISCVIOPMPStreamSink, RISCV_IOPMP_STREAMSINK,
                         TYPE_RISCV_IOPMP_STREAMSINK)
typedef struct {
    uint32_t addr_reg;
    uint32_t addrh_reg;
    uint32_t cfg_reg;
} RISCVIOPMPEntryRegs;

typedef struct {
    uint64_t sa;
    uint64_t ea;
} RISCVIOPMPEntryAddr;

typedef struct {
    union {
        uint32_t *srcmd_en;
        uint32_t *srcmd_perm;
    };
    union {
        uint32_t *srcmd_enh;
        uint32_t *srcmd_permh;
    };
    uint32_t *srcmd_r;
    uint32_t *srcmd_rh;
    uint32_t *srcmd_w;
    uint32_t *srcmd_wh;
    uint32_t *mdcfg;
    RISCVIOPMPEntryRegs *entry;
    uint32_t mdlck;
    uint32_t mdlckh;
    uint32_t entrylck;
    uint32_t mdcfglck;
    uint32_t mdstall;
    uint32_t mdstallh;
    uint32_t rridscp;
    uint32_t err_cfg;
    uint64_t err_reqaddr;
    uint32_t err_reqid;
    uint32_t err_info;
    uint32_t err_msiaddr;
    uint32_t err_msiaddrh;
} RISCVIOPMPRegs;

/*
 * Transaction(txn) information to identify whole transaction length, enabling
 * IOPMP to detect partially hit error
 */
typedef struct RISCVIOPMPTrasaction {
    bool running;
    bool error_reported;
    bool supported;
    uint32_t stage;
    hwaddr start_addr;
    hwaddr end_addr;
} RISCVIOPMPTrasaction;

typedef struct RISCVIOPMPState {
    SysBusDevice parent_obj;
    RISCVIOPMPEntryAddr *entry_addr;
    MemoryRegion mmio;
    IOMMUMemoryRegion iommu;
    RISCVIOPMPRegs regs;
    MemoryRegion *downstream;
    MemoryRegion blocked_r, blocked_w, blocked_x, blocked_rw, blocked_rx,
                 blocked_wx, blocked_rwx;
    MemoryRegion full_mr;

    /*
     * AddressSpace for full permission and no requirements forward transaction
     * information to next IOPMP stage
     */
    AddressSpace downstream_as;
    /* AddressSpace for limited permssion in current IOPMP stage */
    AddressSpace blocked_r_as, blocked_w_as, blocked_x_as, blocked_rw_as,
                 blocked_rx_as, blocked_wx_as, blocked_rwx_as;
    /* AddressSpace for full permssion in current IOPMP stage */
    AddressSpace full_as;
    qemu_irq irq;
    qemu_irq clic_irq;

    /* Receive txn info */
    RISCVIOPMPStreamSink txn_info_sink;
    /* Send txn info for next stage iopmp */
    StreamSink *send_ss;
    RISCVIOPMPTrasaction *transaction_state;
    QemuMutex iopmp_transaction_mutex;

    /*
     * Stall:
     * a while loop to check stall flags if stall_violation is not enabled
     */
    volatile bool is_stalled;
    volatile bool *rrid_stall;

    /* MFR extenstion */
    uint16_t *svw;
    uint16_t svi;

    /* Properties */
    /*
     * MDCFG Format 0: MDCFG table is implemented
     *              1: HWCFG.md_entry_num is fixed
     *              2: HWCFG.md_entry_num is programmable
     */
    uint32_t mdcfg_fmt;
    /*
     * SRCMD Format 0: SRCMD_EN is implemented
     *              1: 1 to 1 SRCMD mapping
     *              2: SRCMD_PERM is implemented
     */
    uint32_t srcmd_fmt;
    bool tor_en;
    /* SPS is only supported in srcmd_fmt0 */
    bool sps_en;
    /* Indicate prio_entry is programmable or not */
    bool default_prient_prog;
    bool rrid_transl_en;
    bool default_rrid_transl_prog;
    bool chk_x;
    bool no_x;
    bool no_w;
    bool stall_en;
    bool default_stall_violation_en;
    bool peis;
    bool pees;
    bool mfr_en;
    /* Indicate md_entry_num for mdcfg_fmt1/2 */
    uint32_t default_md_entry_num;
    uint32_t md_num;
    uint32_t rrid_num;
    uint32_t entry_num;
    /* Indicate number of priority entry */
    uint32_t default_prio_entry;
    uint32_t default_rrid_transl;
    /* MSI */
    bool default_msi_en;
    uint32_t default_msidata;
    uint32_t default_err_msiaddr;
    uint32_t default_err_msiaddrh;
    uint32_t msi_rrid;
    /* Note: entry_offset < 0 is not support in QEMU */
    int32_t entry_offset;
    /*
     * Data value to be returned for all read accesses that violate the security
     * check
     */
    uint32_t err_rdata;

    /* Current status for programmable parameters */
    bool prient_prog;
    bool rrid_transl_prog;
    uint32_t md_entry_num;
    uint32_t prio_entry;
    uint32_t rrid_transl;
    bool enable;
} RISCVIOPMPState;

DeviceState *iopmp_create(hwaddr addr, qemu_irq irq, qemu_irq clic_irq,
                          uint32_t rrid_num);
void iopmp_setup_system_memory(DeviceState *dev, const MemMapEntry *memmap,
                               uint32_t mapentry_num, uint32_t stage);
void iopmp_setup_sink(DeviceState *dev, StreamSink * ss);

#endif
