/*
 * CSKY exit.
 *
 * using to exit QEMU from target linux.
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
#include "trace.h"
#include "qemu/timer.h"
#include "sysemu/cpu-timers.h"
#include "qemu/log.h"

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
} csky_exit_state;

#define TYPE_CSKY_EXIT  "csky_exit"
#define CSKY_EXIT(obj)  OBJECT_CHECK(csky_exit_state, (obj), \
                                      TYPE_CSKY_EXIT)

#define EXIT_RETURN_VALUE   0x0
#define PLATFORM_ID         0x4
#define EXIT_LABEL          0x40
#define EXIT_GET_CYCLE      0x44
#define ICOUNT_LOW          0x48
#define ICOUNT_HIGH         0x4c

extern void gdb_exit(int code);

static uint64_t csky_exit_read(void *opaque, hwaddr offset, unsigned size)
{
    if (offset == PLATFORM_ID) {
        return 0x51454D55;  /* "QEMU" ascii */
    } else if (offset == ICOUNT_LOW) {
        return icount_get_raw() & 0xffffffff;
    } else if (offset == ICOUNT_HIGH) {
        return icount_get_raw() >> 32;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_exit_read: invalid read address.\n");
        return 0;
    }
}

static void csky_exit_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
    switch (offset) {
    case EXIT_RETURN_VALUE:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_exit_write: exit(%d)\n", (int)value);
        gdb_exit(0);
        exit(value);
        break;
    case EXIT_LABEL:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "label: %d\n", (int)value);
        break;
    case EXIT_GET_CYCLE:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "current cycle: %" PRIu64 "\n", icount_get_raw());
        break;
    default:
        exit(0);
    }
}

static const MemoryRegionOps csky_exit_ops = {
    .read = csky_exit_read,
    .write = csky_exit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void csky_exit_init(Object *obj)
{
    csky_exit_state *s = CSKY_EXIT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_exit_ops, s,
                          TYPE_CSKY_EXIT, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}


static void csky_exit_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);
    dc->desc = "csky type: EXIT";
    dc->user_creatable = true;
}

static const TypeInfo csky_exit_info = {
    .name          = TYPE_CSKY_EXIT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_exit_state),
    .instance_init = csky_exit_init,
    .class_init    = csky_exit_class_init,
};


static void csky_exit_register_types(void)
{
    type_register_static(&csky_exit_info);
}

type_init(csky_exit_register_types)

