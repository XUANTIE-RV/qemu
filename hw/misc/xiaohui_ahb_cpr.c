/*
 * Xiaohui AHB CPR.
 *
 * using to boot multicore for Xiaohui platform.
 *
 * Copyright (c) 2024 Hangzhou C-SKY MicroSystem Co., Ltd. All rights reserved.
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
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "trace.h"
#include "qemu/timer.h"
#include "sysemu/cpu-timers.h"
#include "qemu/log.h"
#include "hw/core/cpu.h"
#include "target/riscv/riscv-power.h"
#include "qemu/bitops.h"

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t release_bits;
    uint64_t rvba[9];
} xiaohui_cpr_state;

#define TYPE_XIAOHUI_CPR  "xiaohui_cpr"
#define XIAOHUI_CPR(obj)  OBJECT_CHECK(xiaohui_cpr_state, (obj), \
                                      TYPE_XIAOHUI_CPR)

#define XIAOHUI_CPR_RELEASE        0x0
#define XIAOHUI_CPR_CPU1_RVBA_LO   0x10
#define XIAOHUI_CPR_CPU1_RVBA_HI   0x14
#define XIAOHUI_CPR_CPU2_RVBA_LO   0x18
#define XIAOHUI_CPR_CPU2_RVBA_HI   0x1c
#define XIAOHUI_CPR_CPU3_RVBA_LO   0x20
#define XIAOHUI_CPR_CPU3_RVBA_HI   0x24
#define XIAOHUI_CPR_CPU4_RVBA_LO   0x28
#define XIAOHUI_CPR_CPU4_RVBA_HI   0x2c
#define XIAOHUI_CPR_CPU5_RVBA_LO   0x30
#define XIAOHUI_CPR_CPU5_RVBA_HI   0x34
#define XIAOHUI_CPR_CPU6_RVBA_LO   0x38
#define XIAOHUI_CPR_CPU6_RVBA_HI   0x3c
#define XIAOHUI_CPR_CPU7_RVBA_LO   0x40
#define XIAOHUI_CPR_CPU7_RVBA_HI   0x44
#define XIAOHUI_CPR_CPU8_RVBA_LO   0x48
#define XIAOHUI_CPR_CPU8_RVBA_HI   0x4c
#define XIAOHUI_CPR_CPU9_RVBA_LO   0x50
#define XIAOHUI_CPR_CPU9_RVBA_HI   0x54

/* Features not implemented */
#define XIAOHUI_CPR_MXLEN          0xa0
#define XIAOHUI_CPR_L2_FLUSH       0x4
#define XIAOHUI_CPR_DCP            0x8

static uint64_t xiaohui_cpr_read(void *opaque, hwaddr offset, unsigned size)
{
    xiaohui_cpr_state *s = XIAOHUI_CPR(opaque);
    int i = 0;
    switch (offset) {
    case XIAOHUI_CPR_RELEASE:
        return s->release_bits;
    case XIAOHUI_CPR_CPU1_RVBA_LO:
    case XIAOHUI_CPR_CPU2_RVBA_LO:
    case XIAOHUI_CPR_CPU3_RVBA_LO:
    case XIAOHUI_CPR_CPU4_RVBA_LO:
    case XIAOHUI_CPR_CPU5_RVBA_LO:
    case XIAOHUI_CPR_CPU6_RVBA_LO:
    case XIAOHUI_CPR_CPU7_RVBA_LO:
    case XIAOHUI_CPR_CPU8_RVBA_LO:
    case XIAOHUI_CPR_CPU9_RVBA_LO:
    {
        i = (offset - XIAOHUI_CPR_CPU1_RVBA_LO) / 8;
        return s->rvba[i] & UINT32_MAX;
    }
    case XIAOHUI_CPR_CPU1_RVBA_HI:
    case XIAOHUI_CPR_CPU2_RVBA_HI:
    case XIAOHUI_CPR_CPU3_RVBA_HI:
    case XIAOHUI_CPR_CPU4_RVBA_HI:
    case XIAOHUI_CPR_CPU5_RVBA_HI:
    case XIAOHUI_CPR_CPU6_RVBA_HI:
    case XIAOHUI_CPR_CPU7_RVBA_HI:
    case XIAOHUI_CPR_CPU8_RVBA_HI:
    case XIAOHUI_CPR_CPU9_RVBA_HI:
    {
        i = (offset - XIAOHUI_CPR_CPU1_RVBA_HI) / 8;
        return s->rvba[i] >> 32;
    }
    case XIAOHUI_CPR_MXLEN:
    case XIAOHUI_CPR_L2_FLUSH:
    case XIAOHUI_CPR_DCP:
        qemu_log_mask(LOG_UNIMP,
                      "xiaohui_cpr: unimplemented offset 0x%" HWADDR_PRIx,
                      offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xiaohui_cpr: invalid offset 0x%" HWADDR_PRIx,
                      offset);
        exit(0);
    }
    return 0;
}

static void xiaohui_cpr_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    xiaohui_cpr_state *s = XIAOHUI_CPR(opaque);
    int i = 0;
    switch (offset) {
    case XIAOHUI_CPR_MXLEN:
    case XIAOHUI_CPR_L2_FLUSH:
    case XIAOHUI_CPR_DCP:
        qemu_log_mask(LOG_UNIMP,
                      "xiaohui_cpr: unimplemented offset 0x%" HWADDR_PRIx,
                      offset);
        break;
    case XIAOHUI_CPR_RELEASE:
    {
        CPUState *cpu = NULL;
        i = 0;
        s->release_bits = value;
        while (value) {
            i++;
            if (value & 0x1) {
                cpu = qemu_get_cpu(i);
                /* Release a not exist CPU is ignore */
                if (cpu != NULL) {
                    riscv_cpu_release(cpu, s->rvba[i - 1]);
                }
            }
            value = value >> 1;
        }
        break;
    }
    case XIAOHUI_CPR_CPU1_RVBA_LO:
    case XIAOHUI_CPR_CPU2_RVBA_LO:
    case XIAOHUI_CPR_CPU3_RVBA_LO:
    case XIAOHUI_CPR_CPU4_RVBA_LO:
    case XIAOHUI_CPR_CPU5_RVBA_LO:
    case XIAOHUI_CPR_CPU6_RVBA_LO:
    case XIAOHUI_CPR_CPU7_RVBA_LO:
    case XIAOHUI_CPR_CPU8_RVBA_LO:
    case XIAOHUI_CPR_CPU9_RVBA_LO:
    {
        i = (offset - XIAOHUI_CPR_CPU1_RVBA_LO) / 8;
        s->rvba[i] = deposit64(s->rvba[0], 0, 32, value);
        break;
    }
    case XIAOHUI_CPR_CPU1_RVBA_HI:
    case XIAOHUI_CPR_CPU2_RVBA_HI:
    case XIAOHUI_CPR_CPU3_RVBA_HI:
    case XIAOHUI_CPR_CPU4_RVBA_HI:
    case XIAOHUI_CPR_CPU5_RVBA_HI:
    case XIAOHUI_CPR_CPU6_RVBA_HI:
    case XIAOHUI_CPR_CPU7_RVBA_HI:
    case XIAOHUI_CPR_CPU8_RVBA_HI:
    case XIAOHUI_CPR_CPU9_RVBA_HI:
    {
        i = (offset - XIAOHUI_CPR_CPU1_RVBA_HI) / 8;
        s->rvba[i] = deposit64(s->rvba[0], 32, 32, value);
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xiaohui_cpr: invalid offset 0x%" HWADDR_PRIx,
                      offset);
        exit(0);
    }
}

static const MemoryRegionOps xiaohui_cpr_ops = {
    .read = xiaohui_cpr_read,
    .write = xiaohui_cpr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void xiaohui_cpr_init(Object *obj)
{
    xiaohui_cpr_state *s = XIAOHUI_CPR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &xiaohui_cpr_ops, s,
                          TYPE_XIAOHUI_CPR, 0x10000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const TypeInfo xiaohui_cpr_info = {
    .name          = TYPE_XIAOHUI_CPR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(xiaohui_cpr_state),
    .instance_init = xiaohui_cpr_init,
};

static void xiaohui_cpr_register_types(void)
{
    type_register_static(&xiaohui_cpr_info);
}

type_init(xiaohui_cpr_register_types)
