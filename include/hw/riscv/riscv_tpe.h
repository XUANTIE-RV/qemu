/*
 * RISC-V TPE (Tensor Processing Engine) Device
 *
 * Each TPE has a private 384KB TCM.
 * Supports data movement between DDR and TCM.
 * TCM access does not require privilege checks (e.g., PMP).
 *
 * Copyright (c) 2025 Alibaba Group. All rights reserved.
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
#ifndef HW_RISCV_TPE_H
#define HW_RISCV_TPE_H

#include "hw/sysbus.h"
#define TYPE_RISCV_TPE "riscv.tpe"

#define RISCV_TPE(obj) \
    OBJECT_CHECK(RISCVTPEState, (obj), TYPE_RISCV_TPE)

#define MAX_RANK 5
#define RISCV_TPE_MAX_HARTS 32
#define RISCV_TPE_MAX_DMA_ID 8
#define RISCV_TPE_ERROR_DMA_ID 14
/*
 * Tensor descriptor with hardware-compatible layout.
 * Packed to match DMA engine command format exactly.
 */
typedef struct __attribute__((packed)) {
    uint8_t rank;
    uint8_t rsv1;
    uint8_t element_size;
    uint8_t rsv2;
    uint32_t tile[MAX_RANK];     /* Tile shape per dimension */
    uint32_t strides[MAX_RANK];  /* Memory strides per dimension */
    uint32_t tensor[MAX_RANK];   /* Full tensor shape */
} TensorDescriptor;

typedef enum WorkNodeType {
    INVALID_OP,
    COPY_TCM_TO_MEM,
    COPY_MEM_TO_TCM,
    COPY_DESC_TO_TCM,
    LEGAL_OP_NUM
} WorkNodeType;

/*
 * Work node representing a DMA-like operation.
 * Contains source/dest addresses, coordinates, and type.
 */
typedef struct WorkNode {
    int cpu_index;
    int mmu_idx;
    uint64_t mem_addr_r;      /* Source memory address (read) */
    uint64_t mem_addr_w;      /* Dest memory address (write) */
    uint64_t tcm_addr;        /* TCM base address */
    uint64_t desc_addr;       /* Descriptor address in memory */
    int desc_num;             /* Number of descriptors */
    int coords[5];            /* Starting coordinates */
    int cur_coord_idx;        /* For building coords incrementally */
    int dma_id;
    bool done;
    WorkNodeType type;
    QTAILQ_ENTRY(WorkNode) node;  /* Linked list entry */
} WorkNode;

/*
 * TPE device state.
 * One instance per TPE unit.
 */
typedef struct RISCVTPEState {
    SysBusDevice parent_obj;

    MemoryRegion *tcm_mr;     /* 设备的内存区域 */
    AddressSpace tcm_as;      /* 自定义地址空间 */
    MemoryRegion *tcm_root_mr;     /* 根内存区域 */
    MemoryRegion *cross_mr;     /* Cross page check */
    AddressSpace cross_as;      /* Cross page check */
    MemoryRegion *cross_root_mr;     /* Cross page check */
    QemuMutex lock;                    /* Protects work_list */
    WorkNode *tmp_nodes;               /* One tmp node per hart */
    WorkNode *work_nodes;     /* RISCV_TPE_MAX_DMA_ID work nodes per hart */
    uint32_t tcm_size;
    uint32_t num_harts;
    uint32_t hartid_base;
    qemu_irq *dma_irqs;                /* IRQ outputs for harts */
    QTAILQ_HEAD(, WorkNode) work_list; /* Pending work queue */
    QEMUBH *bh;                        /* Bottom-half handler */
    void *tcm;                         /* Allocated TCM memory */
    MemReentrancyGuard mem_reentrancy_guard;
} RISCVTPEState;



DeviceState *riscv_tpe_create(uint32_t hartid_base,
                              uint32_t num_harts, uint32_t size);
void tpe_dma_push_mem_r(void *opaque, uint64_t mem_addr, int cpu_index);
void tpe_dma_push_mem_w(void *opaque, uint64_t mem_addr, int cpu_index);
void tpe_dma_push_tcm_pa(void *opaque, uint64_t tcm_addr, int cpu_index);
void tpe_dma_push_desc_pa(void *opaque, uint64_t desc_addr, int cpu_index);
void tpe_dma_push_coord(void *opaque, uint64_t val, int cpu_index);
void tpe_dma_copy_desc(void *opaque, int num, int mmu_idx, int cpu_index);
void tpe_dma_copy_tcm_mem(void *opaque, target_ulong id, int mmu_idx, int cpu_index);
void tpe_dma_copy_mem_tcm(void *opaque, target_ulong id, int mmu_idx, int cpu_index);
void tpe_tcm_memory_read(void *opaque, uint64_t addr, void *buf,
                         uint64_t len);
void tpe_tcm_memory_write(void *opaque, uint64_t addr, const void *buf,
                          uint64_t len);
#endif
