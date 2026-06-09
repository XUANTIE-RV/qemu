/*
 * QEMU RISC-V IOPMP dispatcher
 *
 * Receives transaction information from the requestor and forwards it to the
 * corresponding IOPMP device.
 *
 * Copyright (c) 2023-2024 Andes Tech. Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#ifndef RISCV_IOPMP_DISPATCHER_H
#define RISCV_IOPMP_DISPATCHER_H

#include "hw/sysbus.h"
#include "qemu/typedefs.h"
#include "memory.h"
#include "hw/stream.h"
#include "hw/misc/riscv_iopmp_txn_info.h"
#include "exec/hwaddr.h"

#define TYPE_RISCV_IOPMP_DISP "riscv-iopmp-dispatcher"
OBJECT_DECLARE_SIMPLE_TYPE(RISCVIOPMPDispState, RISCV_IOPMP_DISP)

#define TYPE_RISCV_IOPMP_DISP_SS "riscv-iopmp-dispatcher-streamsink"
OBJECT_DECLARE_SIMPLE_TYPE(riscv_iopmp_disp_ss, RISCV_IOPMP_DISP_SS)

typedef struct riscv_iopmp_disp_ss {
    Object parent;
} riscv_iopmp_disp_ss;

typedef struct SinkMemMapEntry {
    StreamSink *sink;
    MemMapEntry map;
} SinkMemMapEntry;

typedef struct RISCVIOPMPDispState {
    SysBusDevice parent_obj;
    riscv_iopmp_disp_ss txn_info_sink;
    SinkMemMapEntry **SinkMemMap;
    /* The maximum number of cascading stages of IOPMP */
    uint32_t stage_num;
    /* The maximum number of parallel IOPMP devices within a single stage. */
    uint32_t target_num;
} RISCVIOPMPDispState;

void iopmp_dispatcher_add_target(DeviceState *dev, StreamSink *sink,
    uint64_t base, uint64_t size, uint32_t stage, uint32_t id);
#endif
