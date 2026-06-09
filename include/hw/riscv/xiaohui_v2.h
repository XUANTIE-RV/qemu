/*
 * RISCV Xiaohui System emulation.
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
#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "hw/dma/dw_dma_sw.h"

enum {
    XIAOHUI_V2_SRAM = 0,
    XIAOHUI_V2_APLIC_M = 1,
    XIAOHUI_V2_APLIC_S = 2,
    XIAOHUI_V2_CLINT = 3,
    XIAOHUI_V2_AHB_CPR = 4,
    XIAOHUI_V2_TIMER = 5,
    XIAOHUI_V2_UART0 = 6,
    XIAOHUI_V2_TEST = 7,
    XIAOHUI_V2_DRAM = 8,
    XIAOHUI_V2_CLIC = 9,
    XIAOHUI_V2_DMA = 10,
    XIAOHUI_V2_IOPMP = 11,
    XIAOHUI_V2_ACLINT_DEFAULT_TIMEBASE_FREQ = 25000000,
    XIAOHUI_V2_TIMER_DEFAULT_TIMEBASE_FREQ = 25000000,
};

#define FDT_APLIC_INT_CELLS   2
#define XIAOHUI_V2_CLIC_IRQ_NUMS 4096
#define XIAOHUI_V2_CLIC_INTCTLBITS 3

/* IOPMP IRQ */
#define IOPMP_IRQ 32

extern const MemMapEntry xiaohui_v2_memmap[];
