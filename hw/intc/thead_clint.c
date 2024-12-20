/*
 * CSKY CLINT (Core Local Interruptor)
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * This provides real-time clock, timer and interprocessor interrupts.
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/intc/thead_clint.h"
#include "qemu/timer.h"

static uint64_t cpu_riscv_read_rtc(void)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                    10000000, NANOSECONDS_PER_SECOND);
}

static void thead_clint_mtimecmp_cb(void *opaque)
{
    qemu_irq irq = *(qemu_irq *)opaque;
    qemu_set_irq(irq, 1);
}

/*
 * Called when timecmp is written to update the QEMU timer or immediately
 * trigger timer interrupt if mtimecmp <= current timer value.
 */
static void thead_clint_write_timecmp(THEADCLINTState *s, int hartid,
                                      uint64_t value)
{
    uint64_t rtc = cpu_riscv_read_rtc();
    uint64_t cmp = s->mtimecmp[hartid] = value;
    uint64_t diff = cmp - rtc;
    uint64_t next_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                         muldiv64(diff, NANOSECONDS_PER_SECOND,
                                                  10000000);

    qemu_set_irq(s->pirq[2 * hartid + 1], 0);
    if (cmp <= rtc) {
        /*
         * if we're setting a timecmp value in the "past",
         * immediately raise the timer interrupt
         */
        qemu_set_irq(s->pirq[2 * hartid + 1], 1);
    } else {
        /* otherwise, set up the future timer interrupt */
        timer_mod(s->timer[hartid], next_ns);
    }
}

/* CPU wants to read rtc or timecmp register */
static uint64_t thead_clint_read(void *opaque, hwaddr addr, unsigned size)
{
    THEADCLINTState *clint = opaque;

    /* reads must be 4 byte aligned words */
    if ((addr & 0x3) != 0 || size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "clint: invalid read size %u: 0x%" HWADDR_PRIx "\n", size, addr);
        return 0;
    }

    switch (addr) {
    case 0:
        return clint->msip[0];
    case 4:
        return clint->msip[1];
    case 0x4000:
    case 0x4008: { /* timecmp_lo */
            uint64_t timecmp = clint->mtimecmp[(addr - 0x4000) >> 3];
            return timecmp & 0xFFFFFFFF;
        }
    case 0x4004:
    case 0x400c: { /* timecmp_hi */
            uint64_t timecmp = clint->mtimecmp[(addr - 0x4004) >> 3];
            return (timecmp >> 32) & 0xFFFFFFFF;
        }
    case 0xbff8: /* time_lo */
        return cpu_riscv_read_rtc() & 0xFFFFFFFF;
    case 0xbffc: /* time_hi */
        return (cpu_riscv_read_rtc() >> 32) & 0xFFFFFFFF;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "clint: invalid read: 0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    return 0;
}

/* CPU wrote to rtc or timecmp register */
static void thead_clint_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    THEADCLINTState *clint = opaque;
    int hartid;
    /* writes must be 4 byte aligned words */
    if ((addr & 0x3) != 0 || size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "clint: invalid write size %u: 0x%" HWADDR_PRIx "\n", size, addr);
        return;
    }
    switch (addr) {
    case 0:
    case 4:
        hartid = addr / 4;
        qemu_irq_pulse(clint->pirq[hartid * 2]);
        clint->msip[hartid] = 0x1;
        break;

    case 0x4000:
    case 0x4008: { /* timecmp_lo */
            uint64_t timecmp_hi;
            hartid = (addr - 0x4000) >> 3;
            timecmp_hi = clint->mtimecmp[hartid] >> 32;
            thead_clint_write_timecmp(clint, hartid,
                                      timecmp_hi << 32 | (value & 0xFFFFFFFF));
            break;
        }
    case 0x4004:
    case 0x400c: { /* timecmp_hi */
            uint64_t timecmp_lo;
            hartid = (addr - 0x4004) >> 3;
            timecmp_lo = clint->mtimecmp[hartid];
            thead_clint_write_timecmp(clint, hartid,
                                      value << 32 | (timecmp_lo & 0xFFFFFFFF));
            break;
        }
    case 0xbff8: /* time_lo */
        qemu_log_mask(LOG_UNIMP, "clint: time_lo write not implemented\n");
        break;
    case 0xbffc: /* time_hi */
        qemu_log_mask(LOG_UNIMP, "clint: time_hi write not implemented");
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clint: invalid write: 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static const MemoryRegionOps thead_clint_ops = {
    .read = thead_clint_read,
    .write = thead_clint_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void thead_clint_init(Object *obj)
{
    THEADCLINTState *s = THEAD_CLINT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &thead_clint_ops, s,
                          TYPE_THEAD_CLINT, 0x10000);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void thead_clint_realize(DeviceState *dev, Error **errp)
{
    THEADCLINTState *s = THEAD_CLINT(dev);
    s->mtimecmp = g_malloc0(sizeof(uint64_t) * s->num_harts);
    s->msip = g_malloc0(sizeof(uint32_t) * s->num_harts);
    s->pirq = g_malloc0(sizeof(qemu_irq) * 2 * s->num_harts);
    s->timer = g_malloc0(sizeof(QEMUTimer *) * s->num_harts);
    qdev_init_gpio_out(dev, s->pirq, 2 * s->num_harts);

    for (int i = 0; i < s->num_harts; i++) {
        s->timer[i] = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   &thead_clint_mtimecmp_cb,
                                   &s->pirq[2 * i + 1]);
    }
}

static Property thead_clint_properties[] = {
    DEFINE_PROP_INT64("num-harts", THEADCLINTState, num_harts, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void thead_clint_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);
    dc->desc = "cskysim type: INTC";
    device_class_set_props(dc, thead_clint_properties);
    dc->realize = thead_clint_realize;
    dc->user_creatable = true;
}

static const TypeInfo thead_clint_info = {
    .name          = TYPE_THEAD_CLINT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(THEADCLINTState),
    .instance_init = thead_clint_init,
    .class_init    = thead_clint_class_init,
};

static void thead_clint_register_types(void)
{
    type_register_static(&thead_clint_info);
}

type_init(thead_clint_register_types)

/*
 * Create CLINT device.
 */
DeviceState *thead_clint_create(hwaddr addr, qemu_irq *pirq, int64_t num_harts)
{
    int i;
    DeviceState *dev = qdev_new(TYPE_THEAD_CLINT);
    qdev_prop_set_uint64(dev, "num-harts", num_harts);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    for (i = 0; i < num_harts; i++) {
        qdev_connect_gpio_out(dev, 2 * i, pirq[2 * i]);
        qdev_connect_gpio_out(dev, 2 * i + 1, pirq[2 * i + 1]);
    }

    return dev;
}
