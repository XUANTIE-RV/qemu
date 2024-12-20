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

enum {
    XIAOHUI_SRAM = 0,
    XIAOHUI_PLIC = 1,
    XIAOHUI_CLINT = 2,
    XIAOHUI_AHB_CPR = 3,
    XIAOHUI_TIMER = 4,
    XIAOHUI_UART0 = 5,
    XIAOHUI_TEST = 6,
    XIAOHUI_DRAM = 7,
    XIAOHUI_CLIC = 8,
    XIAOHUI_ACLINT_DEFAULT_TIMEBASE_FREQ = 25000000,
    XIAOHUI_TIMER_DEFAULT_TIMEBASE_FREQ = 25000000,
};

#define XIAOHUI_CLIC_IRQ_NUMS 4096
#define XIAOHUI_CLIC_INTCTLBITS 3

extern const MemMapEntry xiaohui_memmap[];
