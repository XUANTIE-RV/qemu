/*
 * CSKY interrupt controller header.
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
#ifndef HW_THEAD_INTC_H
#define HW_THEAD_INTC_H

#include "hw/sysbus.h"
#include "exec/memory.h"
#include "hw/irq.h"

/* csky_intc.c */
#define TYPE_THEAD_INTC   "csky_intc"
#define THEAD_INTC(obj)   OBJECT_CHECK(csky_intc_state, (obj), TYPE_THEAD_INTC)

typedef struct csky_intc_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SysBusDevice busdev;
    uint32_t int_icr;
    uint32_t int_isr;
    uint32_t int_source; /*irq from device*/
    uint32_t int_ifr;
    uint32_t int_ipr;
    uint32_t int_nier;
    uint32_t int_nipr;
    uint32_t int_fier;
    uint32_t int_fipr;
    uint32_t pr[32];
    qemu_irq irq;
} csky_intc_state;

qemu_irq *csky_intc_init_cpu(CPUArchState *env);

#endif

