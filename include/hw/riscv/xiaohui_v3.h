/*
 * RISCV Xiaohui System emulation.
 *
 * Copyright (c) 2026 Alibaba Group. All rights reserved.
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
    XIAOHUI_V3_SRAM = 0,
    XIAOHUI_V3_UART0 = 1,
    XIAOHUI_V3_TEST = 2,
    XIAOHUI_V3_DRAM = 3,
    XIAOHUI_V3_APLIC_M = 4,
    XIAOHUI_V3_APLIC_S = 5,
    XIAOHUI_V3_IMSIC_M = 6,
    XIAOHUI_V3_IMSIC_S = 7,
    XIAOHUI_V3_ACLINT = 8,
    XIAOHUI_V3_TIMER = 9,
    XIAOHUI_V3_AHB_CPR = 10,
    XIAOHUI_V3_PCU = 11,
    XIAOHUI_V3_TIMER_DEFAULT_TIMEBASE_FREQ = 25000000,
    XIAOHUI_V3_ACLINT_DEFAULT_TIMEBASE_FREQ = 25000000,
    XIAOHUI_V3_IRQCHIP_NUM_MSIS = 2047
};

extern MemMapEntry xiaohui_v3_memmap[];
#define TYPE_XIAOHUI_V3_MACHINE MACHINE_TYPE_NAME("xiaohui_v3")
