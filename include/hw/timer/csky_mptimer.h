/*
 * CSKY multicore timer header.
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

#ifndef HW_CSKY_MPTIMER_H
#define HW_CSKY_MPTIMER_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "hw/irq.h"

#define TYPE_THEAD_MPTIMER "csky_mptimer"
#define THEAD_MPTIMER(obj) \
    OBJECT_CHECK(csky_mptimer_state, (obj), TYPE_THEAD_MPTIMER)

#define THEAD_MPTIMER_MAX_CPUS 8

/* State of a single timer. */
typedef struct csky_mptimer_unit {
    uint32_t ctlr;
    uint32_t isr;
    uint32_t int_level;
    uint64_t cmpr;
    struct ptimer_state *timer;
    qemu_irq irq;
} csky_mptimer_unit;

typedef struct csky_mptimer_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    uint32_t ctlr;
    uint64_t icvr;
    uint32_t freq;
    MemoryRegion iomem;
    csky_mptimer_unit mptimer_unit[THEAD_MPTIMER_MAX_CPUS];
} csky_mptimer_state;

/* csky_mptimer.c */
void csky_mptimer_set_freq(uint32_t freq);
uint32_t csky_mptimer_get(csky_mptimer_state *s, int index, int pos);
void csky_mptimer_set(csky_mptimer_state *s, int index, int pos,
                      uint32_t value);

#endif
