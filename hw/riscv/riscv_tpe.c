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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "block/aio.h"
#include <stdint.h>
#include <memory.h>
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/riscv/riscv_tpe.h"

/*
 * Enqueue a work node for processing.
 * Note: Caller must hold s->lock.
 */
static void tpe_dma_enqueue_locked(RISCVTPEState *s, WorkNode *node)
{
    assert(node->type != 0);
    QTAILQ_INSERT_TAIL(&s->work_list, node, node);
}

/*
 * Copy tensor descriptors from memory to TCM.
 * Used to preload descriptor tables.
 */
static void process_desc_to_tcm(WorkNode *node)
{
    CPUState *cpu = cpu_by_arch_id(node->cpu_index);
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
    RISCVTPEState *s = env->tpe;
    uint64_t len = node->desc_num * sizeof(TensorDescriptor);
    address_space_write(&s->tcm_as, node->tcm_addr, MEMTXATTRS_UNSPECIFIED,
                        (const void *)node->mem_addr_r, len);
}

static void copy_desc_to_cross(WorkNode *node)
{
    CPUState *cpu = cpu_by_arch_id(node->cpu_index);
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
    RISCVTPEState *s = env->tpe;
    uint64_t len = node->desc_num * sizeof(TensorDescriptor);
    address_space_write(&s->cross_as, node->tcm_addr, MEMTXATTRS_UNSPECIFIED,
                        (const void *)node->mem_addr_r, len);
}

/*
 * Copy 4-bit element with nibble preservation.
 * Even index: low nibble; odd index: high nibble.
 * Other nibble is preserved via read-modify-write.
 * Data in memory: high 4 bits; in TCM: low 4 bits.
 */
static void copy_element_p(CPUArchState *env, uint64_t dst_base,
                           uint64_t src_base,
                           int src_idx, uint64_t dst_idx,
                           int mmu_idx, bool mem2tcm)
{
    uint64_t dst_addr = dst_base + dst_idx / 2;
    uint64_t src_addr = src_base + src_idx / 2;
    uint8_t tcm_byte, mem_byte;
    RISCVTPEState *s = env->tpe;

    if (mem2tcm) {
        address_space_read(&s->tcm_as, dst_addr, MEMTXATTRS_UNSPECIFIED,
                           &tcm_byte, 1);
        uint8_t elem = (src_idx & 1) ?
            (*(uint8_t *)src_addr >> 4) & 0x0F :
            *(uint8_t *)src_addr & 0x0F;
        tcm_byte = (dst_idx & 1) ?
            deposit64(tcm_byte, 4, 4, elem) :
            deposit64(tcm_byte, 0, 4, elem);
        address_space_write(&s->tcm_as, dst_addr, MEMTXATTRS_UNSPECIFIED,
                            &tcm_byte, 1);
    } else {
        mem_byte = *(uint8_t *)dst_addr;
        address_space_read(&s->tcm_as, src_addr, MEMTXATTRS_UNSPECIFIED,
                           &tcm_byte, 1);
        uint8_t elem = (src_idx & 1) ?
            extract64(tcm_byte, 4, 4) :
            extract64(tcm_byte, 0, 4);
        mem_byte = (dst_idx & 1) ?
            deposit64(mem_byte, 4, 4, elem) :
            deposit64(mem_byte, 0, 4, elem);
        *(uint8_t *)dst_addr = mem_byte;
    }
}

/*
 * Generic copy function for 8/16/32-bit elements.
 * Uses size to dispatch read/write via MMU.
 */
static void copy_element_generic(CPUArchState *env, uint64_t dst_base,
                                 uint64_t src_base,
                                 int src_idx, uint64_t dst_idx,
                                 int mmu_idx, bool mem2tcm,
                                 size_t size, int mmu_op)
{
    uint64_t dst_addr = dst_base + dst_idx * size;
    uint64_t src_addr = src_base + src_idx * size;
    uint64_t buffer = 0;
    RISCVTPEState *s = env->tpe;

    if (mem2tcm) {
        switch (size) {
        case 1:
            buffer = *(uint8_t *)src_addr;
            break;
        case 2:
            buffer = *(uint16_t *)src_addr;
            break;
        case 4:
            buffer = *(uint32_t *)src_addr;
            break;
        case 8:
            buffer = *(uint64_t *)src_addr;
            break;
        default:
            g_assert_not_reached();
        }
        address_space_write(&s->tcm_as, dst_addr, MEMTXATTRS_UNSPECIFIED,
                            &buffer, size);
    } else {
        address_space_read(&s->tcm_as, src_addr, MEMTXATTRS_UNSPECIFIED,
                           &buffer, size);
        switch (size) {
        case 1:
            *(uint8_t *)dst_addr = buffer;
            break;
        case 2:
            *(uint16_t *)dst_addr = buffer;
            break;
        case 4:
            *(uint32_t *)dst_addr = buffer;
            break;
        case 8:
            *(uint64_t *)dst_addr = buffer;
            break;
        default:
            g_assert_not_reached();
        }
    }
}

/*
 * Generate copy function for given bit width (8/16/32).
 * Wrapper around copy_element_generic.
 */
#define COPY_ELEMENT_FN(bits) \
static void copy_element_##bits(CPUArchState *env, uint64_t dst_base, \
                                uint64_t src_base, int src_idx, \
                                uint64_t dst_idx, int mmu_idx, bool mem2tcm) \
{ \
    copy_element_generic(env, dst_base, src_base, src_idx, dst_idx, mmu_idx, \
                         mem2tcm, (bits) / 8, \
                         mem2tcm ? MMU_DATA_LOAD : MMU_DATA_STORE); \
}

COPY_ELEMENT_FN(8)
COPY_ELEMENT_FN(16)
COPY_ELEMENT_FN(32)
COPY_ELEMENT_FN(64)

/*
 * Copy a tile defined by tensor descriptor.
 * Supports 4/8/16/32-bit elements.
 * Computes global address using coordinates, tile shape, and strides.
 */
static void copy_tile_by_rank(CPUArchState *env, TensorDescriptor *desc,
                              WorkNode *node, bool copy_to_tcm)
{
    uint64_t src_base = copy_to_tcm ? node->mem_addr_r : node->tcm_addr;
    uint64_t dst_base = copy_to_tcm ? node->tcm_addr : node->mem_addr_w;
    const uint32_t rank = desc->rank;
    const uint32_t elem_size = desc->element_size;
    uint32_t d[MAX_RANK] = {0};
    uint64_t elem_idx = 0;

    while (1) {
        uint64_t src_offset = 0;
        for (uint32_t axis = 0; axis < rank; axis++) {
            uint64_t global_index = (node->coords[axis] * desc->tile[axis]) +
                                    d[axis];
            src_offset += global_index * desc->strides[axis];
        }

        switch (elem_size) {
        case 4:
            copy_element_p(env, dst_base, src_base, src_offset, elem_idx++,
                           node->mmu_idx, copy_to_tcm);
            break;
        case 8:
            copy_element_8(env, dst_base, src_base, src_offset, elem_idx++,
                           node->mmu_idx, copy_to_tcm);
            break;
        case 16:
            copy_element_16(env, dst_base, src_base, src_offset, elem_idx++,
                            node->mmu_idx, copy_to_tcm);
            break;
        case 32:
            copy_element_32(env, dst_base, src_base, src_offset, elem_idx++,
                            node->mmu_idx, copy_to_tcm);
            break;
        }

        int current_dim = (int)rank - 1;
        while (current_dim >= 0) {
            d[current_dim]++;
            if (d[current_dim] < desc->tile[current_dim]) {
                break;
            }
            d[current_dim] = 0;
            current_dim--;
        }
        if (current_dim < 0) {
            break;
        }
    }
}

/*
 * Write zero byte to destination at given offset.
 * Used for padding in packed layouts.
 */
static void write_16_padding_zeros(CPUArchState *env, uint64_t dst_base,
                                   int mmu_idx)
{
    RISCVTPEState *s = env->tpe;
    uint8_t buffer[16] = {0};
    address_space_write(&s->tcm_as, dst_base, MEMTXATTRS_UNSPECIFIED,
                        buffer, 16);
}

/*
 * Copy tile with 48+16 byte packing.
 * Copy 48 valid bytes, then zero 16 bytes.
 * Innermost dimension steps by 64.
 */
static void copy_tile_by_rank_padding(CPUArchState *env, TensorDescriptor *desc,
                                      WorkNode *node, bool copy_to_tcm)
{
    uint64_t src_base = copy_to_tcm ? node->mem_addr_r : node->tcm_addr;
    uint64_t dst_base = copy_to_tcm ? node->tcm_addr : node->mem_addr_w;
    const uint32_t rank = desc->rank;
    uint32_t d[MAX_RANK] = {0};
    uint64_t elem_idx = 0;
    assert(copy_to_tcm);

    while (1) {
        uint64_t src_offset = 0;
        for (uint32_t axis = 0; axis < rank; axis++) {
            uint64_t global_index = (node->coords[axis] * desc->tile[axis]) +
                                    d[axis];
            src_offset += global_index * desc->strides[axis];
        }
        src_offset = src_offset * 6 / 8;
        for (int j = 0; j < 6; j++) {
            copy_element_64(env, dst_base, src_base + src_offset, j,
                           elem_idx++,
                           node->mmu_idx, copy_to_tcm);
        }
        write_16_padding_zeros(env, dst_base + elem_idx * 8, node->mmu_idx);
        elem_idx += 2;

        int current_dim = (int)rank - 1;
        while (current_dim >= 0) {
            if (current_dim == (int)rank - 1) {
                d[current_dim] += 64;
                if (d[current_dim] < desc->tile[current_dim]) {
                    break;
                }
                d[current_dim] = 0;
                current_dim--;
            } else {
                d[current_dim]++;
                if (d[current_dim] < desc->tile[current_dim]) {
                    break;
                }
                d[current_dim] = 0;
                current_dim--;
            }
        }
        if (current_dim < 0) {
            break;
        }
    }
}

static void process_mem_to_tcm(WorkNode *node)
{
    CPUState *cpu = cpu_by_arch_id(node->cpu_index);
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
    RISCVTPEState *s = env->tpe;
    int irq_num = node->cpu_index - s->hartid_base;
    TensorDescriptor desc;
    if (node->dma_id >= RISCV_TPE_MAX_DMA_ID) {
        if (!(env->xmdmaerrinfo & XMDMAERRINFO_V_MASK)) {
            env->xmdmaerrinfo = (env->xmdmaerrinfo & ~XMDMAERRINFO_VEC_MASK) |
                                RISCV_TPE_ERROR_DMA_ID | XMDMAERRINFO_V_MASK;
            qemu_irq_raise(s->dma_irqs[irq_num]);
        }
        return;
    }
    address_space_read(&s->tcm_as, node->desc_addr, MEMTXATTRS_UNSPECIFIED,
                       &desc, sizeof(desc));
    if (desc.element_size == 6) {
        copy_tile_by_rank_padding(env, &desc, node, true);
    } else {
        copy_tile_by_rank(env, &desc, node, true);
    }
    env->xmdmaidle |= 1 << node->dma_id;
}

static void process_tcm_to_mem(WorkNode *node)
{
    CPUState *cpu = cpu_by_arch_id(node->cpu_index);
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
    TensorDescriptor desc;
    RISCVTPEState *s = env->tpe;
    int irq_num = node->cpu_index - s->hartid_base;
    if (node->dma_id > RISCV_TPE_MAX_DMA_ID) {
        if (!(env->xmdmaerrinfo & XMDMAERRINFO_V_MASK)) {
            env->xmdmaerrinfo = (env->xmdmaerrinfo & ~XMDMAERRINFO_VEC_MASK) |
                                RISCV_TPE_ERROR_DMA_ID | XMDMAERRINFO_V_MASK;
            qemu_irq_raise(s->dma_irqs[irq_num]);
        }
        return;
    }
    address_space_read(&s->tcm_as, node->desc_addr, MEMTXATTRS_UNSPECIFIED,
                       &desc, sizeof(desc));
    if (desc.element_size == 6) {
        copy_tile_by_rank_padding(env, &desc, node, false);
    } else {
        copy_tile_by_rank(env, &desc, node, false);
    }
    env->xmdmaidle |= 1 << node->dma_id;
}

static bool tile_crosspage_by_rank(CPUArchState *env, TensorDescriptor *desc,
                                   WorkNode *node, bool copy_to_tcm)
{
    CPUState *cpu = env_cpu(env);
    const uint32_t rank = desc->rank;
    const uint32_t elem_size = desc->element_size;
    uint64_t len;
    uint64_t pagelen;
    uint64_t addr = copy_to_tcm ? node->mem_addr_r : node->mem_addr_w;
    bool ret;
    uint64_t src_offset = 0;
    uint64_t dst_offset = 1;

    for (uint32_t axis = 0; axis < rank; axis++) {
        uint64_t global_index = (node->coords[axis] * desc->tile[axis]) +
                                (desc->tile[axis] - 1);
        src_offset += global_index * desc->strides[axis];
        dst_offset *= desc->tile[axis];
    }

    switch (elem_size) {
    case 4:
        src_offset = src_offset / 2 + 1;
        dst_offset = dst_offset / 2;
        break;
    case 8:
        src_offset = src_offset + 1;
        dst_offset = dst_offset;
        break;
    case 16:
        src_offset = src_offset * 2 + 2;
        dst_offset = dst_offset * 2;
        break;
    case 32:
        src_offset = src_offset * 4 + 4;
        dst_offset = dst_offset * 4;
        break;
    }
    len = copy_to_tcm ? src_offset : dst_offset;
    ret = riscv_get_pagelen(cpu, addr, len, MMU_DATA_LOAD,
                            node->mmu_idx, &pagelen);
    if (ret) {
        return len <= pagelen - (addr & (pagelen - 1));
    } else {
        return false;
    }
}

static bool tile_crosspage_by_rank_padding(CPUArchState *env,
                                           TensorDescriptor *desc,
                                           WorkNode *node, bool copy_to_tcm)
{
    CPUState *cpu = env_cpu(env);
    const uint32_t rank = desc->rank;
    uint64_t pagelen;
    bool ret;
    uint64_t src_offset = 0;
    assert(copy_to_tcm);

    for (uint32_t axis = 0; axis < rank; axis++) {
        uint64_t global_index = (node->coords[axis] * desc->tile[axis]) +
                                (desc->tile[axis] - 1);
        src_offset += global_index * desc->strides[axis];
    }
    src_offset = src_offset * 6 / 8;
    ret = riscv_get_pagelen(cpu, node->mem_addr_r, src_offset + 48,
                            MMU_DATA_LOAD, node->mmu_idx, &pagelen);
    if (ret) {
        return src_offset + 48 <= pagelen - (node->mem_addr_r & (pagelen - 1));
    } else {
        return false;
    }
}

static bool tpe_crosspage_copy(WorkNode *node, bool copy_to_tcm)
{
    CPUState *cpu = cpu_by_arch_id(node->cpu_index);
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
    TensorDescriptor desc;
    RISCVTPEState *s = env->tpe;
    address_space_read(&s->cross_as, node->desc_addr, MEMTXATTRS_UNSPECIFIED,
                       &desc, sizeof(desc));
    if (desc.element_size == 6) {
        return tile_crosspage_by_rank_padding(env, &desc, node, copy_to_tcm);
    } else {
        return tile_crosspage_by_rank(env, &desc, node, copy_to_tcm);
    }
}

static bool tpe_crosspage_desc(WorkNode *node)
{
    CPUState *cpu = cpu_by_arch_id(node->cpu_index);
    uint64_t len = node->desc_num * sizeof(TensorDescriptor);
    uint64_t pagelen;
    bool ret = riscv_get_pagelen(cpu, node->mem_addr_r, len, MMU_DATA_LOAD,
                                 node->mmu_idx, &pagelen);
    if (ret) {
        return len <= pagelen - (node->mem_addr_r & (pagelen - 1));
    } else {
        return false;
    }
}

static bool tpe_crosspage(WorkNode *node)
{
    switch (node->type) {
    case COPY_TCM_TO_MEM:
        return tpe_crosspage_copy(node, false);
    case COPY_MEM_TO_TCM:
        return tpe_crosspage_copy(node, true);
    case COPY_DESC_TO_TCM:
        return tpe_crosspage_desc(node);
    default:
        g_assert_not_reached();
    }
}

/*
 * Bottom-half handler: process all queued work nodes.
 * Runs in QEMU main loop context.
 */
static void tpe_dma_bh_handler(void *opaque)
{
    RISCVTPEState *s = opaque;
    WorkNode *node, *tmp;

    qemu_mutex_lock(&s->lock);
    QTAILQ_FOREACH_SAFE(node, &s->work_list, node, tmp) {
        switch (node->type) {
        case COPY_TCM_TO_MEM:
            process_tcm_to_mem(node);
            break;
        case COPY_MEM_TO_TCM:
            process_mem_to_tcm(node);
            break;
        case COPY_DESC_TO_TCM:
            process_desc_to_tcm(node);
            break;
        default:
            g_assert_not_reached();
        }
        QTAILQ_REMOVE(&s->work_list, node, node);
        if (node->type == COPY_DESC_TO_TCM) {
            g_free(node);
        } else {
            memset(node, 0, sizeof(WorkNode));
        }
    }
    qemu_mutex_unlock(&s->lock);
}

static void riscv_tpe_realize(DeviceState *dev, Error **errp)
{
    RISCVTPEState *s = RISCV_TPE(dev);
    static int tpe_seq;
    s->tcm = g_malloc0(s->tcm_size);
    s->tmp_nodes = g_malloc0(sizeof(WorkNode) * s->num_harts);
    s->work_nodes = g_malloc0(sizeof(WorkNode) * s->num_harts *
                             RISCV_TPE_MAX_DMA_ID);
    s->dma_irqs = g_new(qemu_irq, s->num_harts);
    qdev_init_gpio_out(dev, s->dma_irqs, s->num_harts);
    qemu_mutex_init(&s->lock);
    QTAILQ_INIT(&s->work_list);
    s->bh = qemu_bh_new_guarded(tpe_dma_bh_handler, s,
                                &s->mem_reentrancy_guard);
    s->tcm_root_mr = g_new(MemoryRegion, 1);
    memory_region_init(s->tcm_root_mr, OBJECT(dev),
                       g_strdup_printf("rscv.tpe.tcm.root.mr.%d", tpe_seq),
                       UINT64_MAX);
    address_space_init(&s->tcm_as, s->tcm_root_mr,
                       g_strdup_printf("riscv.tcm.as.%d", tpe_seq));
    s->tcm_mr = g_new(MemoryRegion, 1);
    memory_region_init_ram(s->tcm_mr, OBJECT(dev),
                           g_strdup_printf("rscv.tpe.tcm.mr.%d", tpe_seq),
                           s->tcm_size, &error_fatal);
    memory_region_add_subregion(s->tcm_root_mr, INT64_MIN, s->tcm_mr);
    /* Initialize a temp address space for cross page check */
    s->cross_root_mr = g_new(MemoryRegion, 1);
    memory_region_init(s->cross_root_mr, OBJECT(dev),
                       g_strdup_printf("rscv.tpe.cross.root.mr.%d", tpe_seq),
                       UINT64_MAX);
    address_space_init(&s->cross_as, s->cross_root_mr,
                       g_strdup_printf("riscv.tpe.cross.as.%d", tpe_seq));
    s->cross_mr = g_new(MemoryRegion, 1);
    memory_region_init_ram(s->cross_mr, OBJECT(dev),
                           g_strdup_printf("rscv.tpe.cross.mr.%d", tpe_seq),
                           s->tcm_size, &error_fatal);
    memory_region_add_subregion(s->cross_root_mr, INT64_MIN, s->cross_mr);
    tpe_seq++;
}

static Property riscv_tpe_properties[] = {
    DEFINE_PROP_UINT32("hartid-base", RISCVTPEState, hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", RISCVTPEState, num_harts, 1),
    DEFINE_PROP_UINT32("tcm-size", RISCVTPEState, tcm_size, 320 * 1024),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_tpe_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = riscv_tpe_realize;
    device_class_set_props(dc, riscv_tpe_properties);
}

static const TypeInfo riscv_tpe_info = {
    .name          = TYPE_RISCV_TPE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVTPEState),
    .class_init    = riscv_tpe_class_init,
};

/*
 * Create and initialize a TPE device.
 * Connects DMA IRQs to specified harts.
 */
DeviceState *riscv_tpe_create(uint32_t hartid_base, uint32_t num_harts,
                              uint32_t size)
{
    int i;
    RISCVTPEState *tpe;
    DeviceState *dev = qdev_new(TYPE_RISCV_TPE);

    g_assert(num_harts <= RISCV_TPE_MAX_HARTS);

    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "tcm-size", size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    tpe = RISCV_TPE(dev);
    for (i = 0; i < num_harts; i++) {
        CPUState *cpu = cpu_by_arch_id(hartid_base + i);
        RISCVCPU *rvcpu = RISCV_CPU(cpu);
        CPURISCVState *env = &rvcpu->env;
        env->tpe = tpe;
        qdev_connect_gpio_out(dev, i,
                              qdev_get_gpio_in(DEVICE(rvcpu), IRQ_TPE_DMA));
    }
    return dev;
}

static void riscv_tpe_register_types(void)
{
    type_register_static(&riscv_tpe_info);
}

type_init(riscv_tpe_register_types)

void tpe_dma_push_mem_r(void *opaque, uint64_t mem_addr, int cpu_index)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    int local_index = cpu_index - s->hartid_base;
    WorkNode *node = &s->tmp_nodes[local_index];
    node->cpu_index = cpu_index;
    node->mem_addr_r = mem_addr;
}

void tpe_dma_push_mem_w(void *opaque, uint64_t mem_addr, int cpu_index)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    int local_index = cpu_index - s->hartid_base;
    WorkNode *node = &s->tmp_nodes[local_index];
    node->cpu_index = cpu_index;
    node->mem_addr_w = mem_addr;
}

void tpe_dma_push_tcm_pa(void *opaque, uint64_t tcm_addr, int cpu_index)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    int local_index = cpu_index - s->hartid_base;
    WorkNode *node = &s->tmp_nodes[local_index];
    node->cpu_index = cpu_index;
    node->tcm_addr = tcm_addr;
}

void tpe_dma_push_desc_pa(void *opaque, uint64_t desc_addr, int cpu_index)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    int local_index = cpu_index - s->hartid_base;
    WorkNode *node = &s->tmp_nodes[local_index];
    node->cpu_index = cpu_index;
    node->desc_addr = desc_addr;
}

void tpe_dma_push_coord(void *opaque, uint64_t val, int cpu_index)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    int local_index = cpu_index - s->hartid_base;
    WorkNode *node = &s->tmp_nodes[local_index];
    if (node->cur_coord_idx < 5) {
        node->coords[node->cur_coord_idx++] = val;
        node->cpu_index = cpu_index;
    }
}

void tpe_dma_copy_desc(void *opaque, int num, int mmu_idx, int cpu_index)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    int local_index = cpu_index - s->hartid_base;
    WorkNode *node = &s->tmp_nodes[local_index];
    WorkNode *work_node = g_malloc0(sizeof(WorkNode));
    CPUState *cpu = cpu_by_arch_id(cpu_index);
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
    void *h = tlb_vaddr_to_host(env, node->mem_addr_r, MMU_DATA_LOAD,
                                mmu_idx);
    if (!node->mem_addr_r || !h) {
        error_report("TPE: DMA memory addr 0x%lx is not allocated",
                     node->mem_addr_r);
        exit(0);
    }
    node->desc_num = num;
    node->mmu_idx = mmu_idx;
    node->cpu_index = cpu_index;
    node->type = COPY_DESC_TO_TCM;
    if (!tpe_crosspage(node)) {
        warn_report("TPE: DMA copy desc cross page");
        exit(0);
    }
    node->mem_addr_r = (uintptr_t)h;
    qemu_mutex_lock(&s->lock);
    memcpy(work_node, &s->tmp_nodes[local_index], sizeof(WorkNode));
    memset(node, 0, sizeof(WorkNode));
    copy_desc_to_cross(work_node);
    tpe_dma_enqueue_locked(s, work_node);
    qemu_mutex_unlock(&s->lock);
    qemu_bh_schedule(s->bh);
}

void tpe_dma_copy_tcm_mem(void *opaque, target_ulong id, int mmu_idx,
                          int cpu_index)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    int local_index = cpu_index - s->hartid_base;
    WorkNode *node = &s->tmp_nodes[local_index];
    WorkNode *work_node = s->work_nodes + RISCV_TPE_MAX_DMA_ID * local_index +
                          (id % RISCV_TPE_MAX_DMA_ID);
    CPUState *cpu = cpu_by_arch_id(cpu_index);
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
    void *h = tlb_vaddr_to_host(env, node->mem_addr_r, MMU_DATA_LOAD,
                                mmu_idx);
    if (!node->mem_addr_r || !h) {
        error_report("TPE: DMA memory addr 0x%lx is not allocated",
                     node->mem_addr_r);
        exit(0);
    }
    node->type = COPY_MEM_TO_TCM;
    node->mmu_idx = mmu_idx;
    node->cpu_index = cpu_index;
    node->dma_id = id;
    if (!tpe_crosspage(node)) {
        warn_report("TPE: DMA copy tile to tcm cross page");
        exit(0);
    }
    node->mem_addr_r = (uintptr_t)h;
    qemu_mutex_lock(&s->lock);
    memcpy(work_node, &s->tmp_nodes[local_index], sizeof(WorkNode));
    memset(node, 0, sizeof(WorkNode));
    tpe_dma_enqueue_locked(s, work_node);
    qemu_mutex_unlock(&s->lock);
    qemu_bh_schedule(s->bh);
}

void tpe_dma_copy_mem_tcm(void *opaque, target_ulong id, int mmu_idx,
                          int cpu_index)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    int local_index = cpu_index - s->hartid_base;
    WorkNode *node = &s->tmp_nodes[local_index];
    WorkNode *work_node = s->work_nodes + RISCV_TPE_MAX_DMA_ID * local_index +
                          (id % RISCV_TPE_MAX_DMA_ID);
    CPUState *cpu = cpu_by_arch_id(cpu_index);
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
    void *h = tlb_vaddr_to_host(env, node->mem_addr_w, MMU_DATA_STORE,
                                mmu_idx);
    if (!node->mem_addr_w || !h) {
        error_report("TPE: DMA memory addr 0x%lx is not allocated",
                     node->mem_addr_w);
        exit(0);
    }
    node->type = COPY_TCM_TO_MEM;
    node->mmu_idx = mmu_idx;
    node->cpu_index = cpu_index;
    node->dma_id = id;
    if (!tpe_crosspage(node)) {
        warn_report("TPE: DMA copy tile to ddr cross page");
        exit(0);
    }
    node->mem_addr_w = (uintptr_t)h;
    qemu_mutex_lock(&s->lock);
    memcpy(work_node, &s->tmp_nodes[local_index], sizeof(WorkNode));
    memset(node, 0, sizeof(WorkNode));
    tpe_dma_enqueue_locked(s, work_node);
    qemu_mutex_unlock(&s->lock);
    qemu_bh_schedule(s->bh);
}

void tpe_tcm_memory_read(void *opaque, uint64_t addr,
                         void *buf, uint64_t len)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    address_space_read(&s->tcm_as, addr, MEMTXATTRS_UNSPECIFIED,
                       buf, len);
}

void tpe_tcm_memory_write(void *opaque, uint64_t addr,
                          const void *buf, uint64_t len)
{
    RISCVTPEState *s = (RISCVTPEState *)opaque;
    address_space_write(&s->tcm_as, addr, MEMTXATTRS_UNSPECIFIED,
                        buf, len);
}
