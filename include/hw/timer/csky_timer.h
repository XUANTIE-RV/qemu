/*
 * CSKY timer header
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

#ifndef HW_CSKY_TIMER_H
#define HW_CSKY_TIMER_H

#include "hw/ptimer.h"
#define TYPE_CSKY_TIMER     "csky_timer"
#define CSKY_TIMER(obj)     OBJECT_CHECK(csky_timer_state, (obj), \
                                          TYPE_CSKY_TIMER)
typedef struct csky_timer_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ptimer_state *timer[4];
    uint32_t control[4];
    uint32_t limit[4];
    int freq[4];
    int int_level[4];
    qemu_irq irq[4];
    qemu_irq clic_irq[4];
} csky_timer_state;


/* csky_timer.c */
void csky_timer_set_freq(uint32_t freq);

#endif
