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

typedef enum XiaohuiAIAType {
    XIAOHUI_AIA_TYPE_NONE = 0,
    XIAOHUI_AIA_TYPE_APLIC,
    XIAOHUI_AIA_TYPE_APLIC_IMSIC,
} XiaohuiAIAType;

typedef enum XiaohuiAclintType {
    XIAOHUI_ACLINT_TYPE_CLINT = 0,
    XIAOHUI_ACLINT_TYPE_ACLINT,
    XIAOHUI_ACLINT_TYPE_PER_HART,
} XiaohuiAclintType;

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
    XIAOHUI_APLIC_M = 9,
    XIAOHUI_APLIC_S = 10,
    XIAOHUI_ACLINT = 11,
    XIAOHUI_DMA = 12,
    XIAOHUI_IOPMP = 13,
    XIAOHUI_IMSIC_M = 14,
    XIAOHUI_IMSIC_S = 15,
    XIAOHUI_PCU = 16,
    XIAOHUI_ACLINT_DEFAULT_TIMEBASE_FREQ = 25000000,
    XIAOHUI_TIMER_DEFAULT_TIMEBASE_FREQ = 25000000,
};

#define XIAOHUI_CLIC_IRQ_NUMS 4096
#define XIAOHUI_CLIC_INTCTLBITS 3

#define FDT_IMSIC_INT_CELLS   0
#define FDT_APLIC_INT_CELLS   2

#define XIAOHUI_IRQCHIP_NUM_MSIS 2047

/*
 * APLIC layout used only when aia=aplic-imsic:
 *   APLIC_M: 0x0008000000, 16 KiB
 *   APLIC_S: 0x0008004000, 16 KiB
 * For aia=aplic (direct delivery mode), the original 32 KiB layout from
 * xiaohui_memmap[XIAOHUI_APLIC_M/S] is kept unchanged.
 */
#define XIAOHUI_APLIC_IMSIC_APLIC_M_BASE   0x0008000000ULL
#define XIAOHUI_APLIC_IMSIC_APLIC_S_BASE   0x0008004000ULL
#define XIAOHUI_APLIC_IMSIC_APLIC_SIZE     (16 * 1024)

/* IOPMP IRQ */
#define IOPMP_IRQ 18

extern const MemMapEntry xiaohui_memmap[];

/* Per-hart ACLINT base addresses (for per-hart mode) */
extern const uint64_t xiaohui_per_hart_aclint_base[16];
