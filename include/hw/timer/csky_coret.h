/*
 * CSKY coret header
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

#ifndef HW_CSKY_CORET_H
#define HW_CSKY_CORET_H

#include "hw/ptimer.h"
#define TYPE_CSKY_CORET     "csky_coret"
#define CSKY_CORET(obj)     OBJECT_CHECK(csky_coret_state, (obj), \
                                         TYPE_CSKY_CORET)

typedef struct csky_coret_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ptimer_state *timer;
    uint32_t coret_csr;
    uint32_t coret_rvr;
    qemu_irq irq;
} csky_coret_state;

/* csky_coret.c */
void csky_coret_set_freq(uint32_t freq);

#endif
