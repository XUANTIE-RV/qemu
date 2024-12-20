/*
 * CSKY PMU.
 *
 * only for Smartl boots TEE.
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
#include "hw/sysbus.h"
#include "qemu/module.h"
#include "sysemu/reset.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/smartl.h"
#include "qemu/log.h"

#define RESET_ADDR 4
#define RESET_TRIG 0
typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint64_t address;
} thead_pmu_state;

#define TYPE_THEAD_PMU  "thead_pmu"
#define THEAD_PMU(obj)  OBJECT_CHECK(thead_pmu_state, (obj), \
                                     TYPE_THEAD_PMU)

static uint64_t thead_pmu_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0;
}
static void thead_pmu_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
    thead_pmu_state *s = (thead_pmu_state *)opaque;
    RISCVSmartlState *ms = RISCV_SMARTL_MACHINE(qdev_get_machine());
    switch (offset) {
    case RESET_ADDR:
        s->address = value;
        break;
    case RESET_TRIG:
        cpu_reset(CPU(ms->harts[1]));
        ms->harts[1]->env.pc = s->address;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead_pmu: error offset write\n");
        break;
    }
}

static const MemoryRegionOps thead_pmu_ops = {
    .read = thead_pmu_read,
    .write = thead_pmu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void thead_pmu_init(Object *obj)
{
    thead_pmu_state *s = THEAD_PMU(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &thead_pmu_ops, s,
                          TYPE_THEAD_PMU, 0x400);
    sysbus_init_mmio(sbd, &s->iomem);
}


static void thead_pmu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);
    dc->desc = "csky type: PMU";
    dc->user_creatable = true;
}

static const TypeInfo thead_pmu_info = {
    .name          = TYPE_THEAD_PMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(thead_pmu_state),
    .instance_init = thead_pmu_init,
    .class_init    = thead_pmu_class_init,
};


static void thead_pmu_register_types(void)
{
    type_register_static(&thead_pmu_info);
}

type_init(thead_pmu_register_types)

