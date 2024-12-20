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

#ifndef RISCV_IOPMP_H
#define RISCV_IOPMP_H

#include "hw/sysbus.h"
#include "qemu/typedefs.h"
#include "memory.h"
#include "hw/pci/pci_bus.h"

#define TYPE_IOPMP "iopmp"
#define IOPMP(obj) OBJECT_CHECK(IopmpState, (obj), TYPE_IOPMP)

#define IOPMP_MAX_MD_NUM            63
#define IOPMP_MAX_RRID_NUM          65535
#define IOPMP_MAX_ENTRY_NUM         65535

#define VENDER_VIRT                 0
#define SPECVER_0_9_1               91
#define IMPID_0_9_1                 91

#define RRE_ERROR                   0
#define RRE_SUCCESS_VALUE           1

#define RWE_ERROR                   0
#define RWE_SUCCESS                 1

#define ERR_REQINFO_TTYPE_READ      1
#define ERR_REQINFO_TTYPE_WRITE     2
#define ERR_REQINFO_TTYPE_FETCH     3
#define ERR_REQINFO_ETYPE_NOERROR   0
#define ERR_REQINFO_ETYPE_READ      1
#define ERR_REQINFO_ETYPE_WRITE     2
#define ERR_REQINFO_ETYPE_FETCH     3
#define ERR_REQINFO_ETYPE_PARHIT    4
#define ERR_REQINFO_ETYPE_NOHIT     5
#define ERR_REQINFO_ETYPE_RRID      6
#define ERR_REQINFO_ETYPE_USER      7

#define IOPMP_MODEL_FULL            0
#define IOPMP_MODEL_RAPIDK          0x1
#define IOPMP_MODEL_DYNAMICK        0x2
#define IOPMP_MODEL_ISOLATION       0x3
#define IOPMP_MODEL_COMPACTK        0x4

#define ENTRY_NO_HIT                0
#define ENTRY_PAR_HIT               1
#define ENTRY_HIT                   2

typedef enum {
    IOPMP_AMATCH_OFF,  /* Null (off)                            */
    IOPMP_AMATCH_TOR,  /* Top of Range                          */
    IOPMP_AMATCH_NA4,  /* Naturally aligned four-byte region    */
    IOPMP_AMATCH_NAPOT /* Naturally aligned power-of-two region */
} iopmp_am_t;

typedef struct {
    uint32_t addr_reg;
    uint32_t addrh_reg;
    uint32_t  cfg_reg;
} iopmp_entry_t;

typedef struct {
    uint64_t sa;
    uint64_t ea;
} iopmp_addr_t;

typedef struct {
    uint32_t *srcmd_en;
    uint32_t *srcmd_enh;
    uint32_t *mdcfg;
    iopmp_entry_t *entry;
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
    uint32_t err_reqinfo;
} iopmp_regs;


typedef struct iopmp_error_info {
    uint32_t reqinfo;
    hwaddr start_addr;
    hwaddr end_addr;
} iopmp_error_info;

typedef struct iopmp_pci_as {
    void *iopmp;
    IOMMUMemoryRegion iommu;
    AddressSpace as;
} iopmp_pci_addressspcace;

typedef struct IopmpState {
    SysBusDevice parent_obj;
    iopmp_addr_t *entry_addr;
    MemoryRegion mmio;
    IOMMUMemoryRegion iommu;
    IOMMUMemoryRegion *next_iommu;
    iopmp_regs regs;
    MemoryRegion *downstream;
    MemoryRegion blocked_io;
    MemoryRegion stall_io;
    uint32_t model;
    uint32_t k;
    bool rrid_transl_prog;
    bool prient_prog;
    bool default_rrid_transl_prog;
    bool default_prient_prog;
    bool rrid_transl_en;
    uint32_t rrid_transl;

    AddressSpace iopmp_sysbus_as;
    iopmp_pci_addressspcace **iopmp_pci;
    AddressSpace downstream_as;
    AddressSpace blocked_io_as;
    qemu_irq irq;
    bool enable;
    uint32_t prio_entry;

    uint32_t rrid_num;
    uint32_t md_num;
    uint32_t entry_num;
    uint32_t entry_offset;
    uint32_t fabricated_v;
} IopmpState;

void cascade_iopmp(DeviceState *cur_dev, DeviceState *next_dev);
void iopmp_setup_pci(DeviceState *iopmp_dev, PCIBus *bus);

#endif
