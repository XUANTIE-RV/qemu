/*
 * RISCV SMARTL System emulation.
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_SMARTL_H
#define HW_SMARTL_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "qom/object.h"

#define SMARTL_CLIC_IRQ_NUMS 256
#define SMARTL_CLIC_VERSION "v0.8"
#define SMARTL_CLIC_HARTS 1
#define SMARTL_CLIC_INTCTLBITS 3

#define TYPE_RISCV_SMARTL_MACHINE MACHINE_TYPE_NAME("smartl")
typedef struct RISCVSmartlState RISCVSmartlState;
DECLARE_INSTANCE_CHECKER(RISCVSmartlState, RISCV_SMARTL_MACHINE,
                         TYPE_RISCV_SMARTL_MACHINE)

struct RISCVSmartlState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    RISCVCPU *harts[2];
};

enum {
    SMARTL_SRAM0,
    SMARTL_SRAM1,
    SMARTL_SRAM2,
    SMARTL_SRAM3,
    SMARTL_SYSTEMMAP,
    SMARTL_TIMER,
    SMARTL_UART,
    SMARTL_TIMER2,
    SMARTL_UART2,
    SMARTL_CLINT,
    SMARTL_CLIC,
    SMARTL_EXIT,
    SMARTL_PMU,
};

#endif
