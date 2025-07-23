/*
 * RISCV SMARTM System emulation.
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

#ifndef HW_SMARTM_H
#define HW_SMARTM_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "qom/object.h"

#define SMARTM_CLIC_IRQ_NUMS 256
#define SMARTM_CLIC_VERSION "v0.8"
#define SMARTM_CLIC_HARTS 1
#define SMARTM_CLIC_INTCTLBITS 3

#define TYPE_RISCV_SMARTM_MACHINE MACHINE_TYPE_NAME("smartm")
typedef struct RISCVSmartlState RISCVSmartlState;
DECLARE_INSTANCE_CHECKER(RISCVSmartlState, RISCV_SMARTM_MACHINE,
                         TYPE_RISCV_SMARTM_MACHINE)

struct RISCVSmartlState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    RISCVCPU *harts[2];
};

enum {
    SMARTM_SRAM0,
    SMARTM_SRAM1,
    SMARTM_SRAM2,
    SMARTM_TIMER,
    SMARTM_UART,
    SMARTM_TIMER2,
    SMARTM_CLINT,
    SMARTM_CLIC,
    SMARTM_EXIT,
    SMARTM_PMU,
};

#endif
