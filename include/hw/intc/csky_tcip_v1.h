/*
 * CSKY nested vector interrupt controller header.
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
#ifndef HW_THEAD_TCIP_V1_H
#define HW_THEAD_TCIP_V1_H

#include "hw/ptimer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "exec/memory.h"

#define TYPE_THEAD_TCIP_V1   "csky_tcip_v1"
#define THEAD_TCIP_V1(obj)   OBJECT_CHECK(csky_tcip_v1_state, (obj), \
                                         TYPE_THEAD_TCIP_V1)

typedef struct csky_tcip_v1_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ptimer_state *timer;
    uint32_t vic_source; /* irq from device */
    uint32_t vic_iser;
    uint32_t vic_iwer;
    uint32_t vic_ispr;
    uint32_t vic_iabr;
    uint32_t vic_pr[32]; /*
                          * for each vic_pr[i], only the
                          * lowest two bits are valid
                          */
    uint32_t vic_isr;
    uint32_t vic_iptr;
    uint32_t vic_issr;
    uint32_t vic_tspend;
    uint32_t vic_tsabr;
    uint32_t vic_tspr;   /* tspending set priority register */
    CPUCSKYState *env;
    qemu_irq irq;
} csky_tcip_v1_state;

/* csky_tcip_v1.c */
qemu_irq *csky_vic_v1_init_cpu(CPUArchState *env);

#endif
