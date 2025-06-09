/*
 * CSKY timer emulation
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
#include "qemu/main-loop.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "hw/ptimer.h"
#include "hw/timer/csky_timer.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"

#define TIMER_CTRL_ENABLE         (1 << 0)
#define TIMER_CTRL_MODE           (1 << 1)
#define TIMER_CTRL_IE             (1 << 2)
#define TIMER_CTRL_CLOCK          (1 << 3)

uint32_t csky_timer_freq = 1000000000ll;

static void csky_timer_update(csky_timer_state *s, int index)
{
    int i, irq_num;
    /* Update interrupts.  */
    if (s->int_level[index] && !(s->control[index] & TIMER_CTRL_IE)) {
        if (s->irqs[index]) {
            qemu_irq_raise(s->irqs[index]);
        }
        if (s->num_clic_irqs) {
            for (i = 0; i < s->num_harts; i++) {
                irq_num = index * s->num_harts + i;
                if (s->clic_irqs[irq_num]) {
                    qemu_irq_raise(s->clic_irqs[irq_num]);
                }
            }
        }
    } else {
        if (s->irqs[index]) {
            qemu_irq_lower(s->irqs[index]);
        }
        if (s->num_clic_irqs) {
            for (i = 0; i < s->num_harts; i++) {
                irq_num = index * s->num_harts + i;
                if (s->clic_irqs[irq_num]) {
                    qemu_irq_lower(s->clic_irqs[irq_num]);
                }
            }
        }
    }
}

static uint32_t csky_timer_read(csky_timer_state *s, hwaddr offset, int index)
{
    switch (offset >> 2) {
    case 0: /* TimerN LoadCount */
        return s->limit[index];
    case 1: /* TimerN CurrentValue */
        return ptimer_get_count(s->timer[index]);
    case 2: /* TimerN ControlReg */
        return s->control[index];
    case 3: /* TimerN EOI */
        s->int_level[index] = 0;
        csky_timer_update(s, index);
        return 0;
    case 4: /* TimerN IntStatus */
        return s->int_level[index] && !(s->control[index] & TIMER_CTRL_IE);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timer_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void csky_timer_reload(csky_timer_state *s, int reload, int index)
{
    uint32_t limit;
    if (s->control[index] & TIMER_CTRL_MODE) {
        limit = s->limit[index];
    } else {
        limit = s->limit[index];
    }
    ptimer_set_limit(s->timer[index], limit, reload);
}

static void csky_timer_write(csky_timer_state *s, hwaddr offset,
                             uint64_t value, int index)
{
    switch (offset >> 2) {
    case 0: /*TimerN LoadCount*/
        s->limit[index] = value;
        if (s->control[index] & TIMER_CTRL_ENABLE) {
            ptimer_transaction_begin(s->timer[index]);
            csky_timer_reload(s, 0, index);
            ptimer_run(s->timer[index], 0);
            ptimer_transaction_commit(s->timer[index]);
        }
        break;
    case 2: /*TimerN ControlReg*/
        ptimer_transaction_begin(s->timer[index]);
        if (s->control[index] & TIMER_CTRL_ENABLE) {
            /* Pause the timer if it is running. */
            ptimer_stop(s->timer[index]);
        }
        s->control[index] = value;
        csky_timer_reload(s, s->control[index] & TIMER_CTRL_ENABLE, index);
        ptimer_set_freq(s->timer[index], s->freq[index]);
        if (s->control[index] & TIMER_CTRL_ENABLE) {
            /* Restart the timer if still enabled. */
            ptimer_run(s->timer[index], 0);
        }
        ptimer_transaction_commit(s->timer[index]);
        break;

    case 1: /*TimerN CurrentValue*/
    case 3: /*TimerN EOI*/
    case 4: /*TimerN IntStatus*/
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timer_write: Bad offset %x\n", (int)offset);
    }
    csky_timer_update(s, index);
}

static void csky_timer_tick0(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 0);
    s->int_level[0] = 1;
    csky_timer_update(s, 0);
}

static void csky_timer_tick1(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 1);
    s->int_level[1] = 1;
    csky_timer_update(s, 1);
}

static void csky_timer_tick2(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 2);
    s->int_level[2] = 1;
    csky_timer_update(s, 2);
}

static void csky_timer_tick3(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 3);
    s->int_level[3] = 1;
    csky_timer_update(s, 3);
}

static uint64_t csky_timers_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    int n;
    int i;
    uint32_t ret;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_read: Bad read size\n");
    }

    n = offset / 0x14;
    switch (n) {
    case 0: /*TimerN*/
    case 1:
    case 2:
    case 3:
        return csky_timer_read(s, offset % 0x14, n);
    case 8: /*Timer System Register*/
        switch ((offset % 0x14) >> 2) {
        case 0: /*TimersIntStatus*/
            ret = ((s->int_level[0] && !(s->control[0] & TIMER_CTRL_IE)) |
                   ((s->int_level[1] &&
                     !(s->control[1] & TIMER_CTRL_IE)) << 1) |
                   ((s->int_level[2] &&
                     !(s->control[2] & TIMER_CTRL_IE)) << 2) |
                   ((s->int_level[3] &&
                     !(s->control[3] & TIMER_CTRL_IE)) << 3));
            return ret;
        case 1: /*TimersEOI*/
            for (i = 0; i <= 3; i++) {
                s->int_level[i] = 0;
                csky_timer_update(s, i);
            }
            return 0;
        case 2: /*TimersRawIntStatus*/
            return (s->int_level[0] | (s->int_level[1] << 1) |
                    (s->int_level[2] << 2) | (s->int_level[3] << 3));

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_timers_read: Bad offset %x\n", (int)offset);
            return 0;
        }

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_read: Bad timer %d\n", n);
        return 0;
    }
}

static void csky_timers_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    int n;

    n = offset / 0x14;
    if (n > 3) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_write: Bad timer %d\n", n);
    }

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_write: Bad write size\n");
    }

    csky_timer_write(s, offset % 0x14, value, n);
}

static const MemoryRegionOps csky_timer_ops = {
    .read = csky_timers_read,
    .write = csky_timers_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void csky_timer_set_freq(uint32_t freq)
{
    csky_timer_freq = freq;
}

static void csky_timer_realize(DeviceState *dev, Error **errp)
{
    csky_timer_state *s = CSKY_TIMER(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &csky_timer_ops, s,
                          TYPE_CSKY_TIMER, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    s->irqs = g_new(qemu_irq, 4);
    s->clic_irqs = g_new(qemu_irq, s->num_harts * 4);
    qdev_init_gpio_out(dev, s->irqs, 4);
    qdev_init_gpio_out(dev, s->clic_irqs, s->num_harts * 4);
}

DeviceState *
csky_timer_create(hwaddr addr, qemu_irq *irq,
                  qemu_irq *clic_irq, uint32_t harts_num,
                  uint32_t clic_irqs_num)
{
    int i, j;
    DeviceState *dev = qdev_new(TYPE_CSKY_TIMER);
    qdev_prop_set_uint32(dev, "num-harts", harts_num);
    qdev_prop_set_uint32(dev, "num-clic-irqs", clic_irqs_num);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    for (i = 0; i < 4; i++) {
        qdev_connect_gpio_out(dev, i, irq[i]);
    }
    if (clic_irq == NULL) {
        return dev;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < harts_num; j++) {
            qdev_connect_gpio_out(dev, 4 + i * harts_num + j,
                                  clic_irq[j * clic_irqs_num + i]);
        }
    }
    return dev;
}

static void csky_timer_init(Object *obj)
{
    csky_timer_state *s = CSKY_TIMER(obj);

    s->freq[0] = csky_timer_freq;
    s->timer[0] = ptimer_init(csky_timer_tick0, s, PTIMER_POLICY_LEGACY);

    s->freq[1] = csky_timer_freq;
    s->timer[1] = ptimer_init(csky_timer_tick1, s, PTIMER_POLICY_LEGACY);

    s->freq[2] = csky_timer_freq;
    s->timer[2] = ptimer_init(csky_timer_tick2, s, PTIMER_POLICY_LEGACY);

    s->freq[3] = csky_timer_freq;
    s->timer[3] = ptimer_init(csky_timer_tick3, s, PTIMER_POLICY_LEGACY);
}

static const VMStateDescription vmstate_csky_timer = {
    .name = TYPE_CSKY_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER_ARRAY(timer, csky_timer_state, 4),
        VMSTATE_UINT32_ARRAY(control, csky_timer_state, 4),
        VMSTATE_UINT32_ARRAY(limit, csky_timer_state, 4),
        VMSTATE_INT32_ARRAY(freq, csky_timer_state, 4),
        VMSTATE_INT32_ARRAY(int_level, csky_timer_state, 4),
        VMSTATE_END_OF_LIST()
    }
};

static Property csky_timer_properties[] = {
    DEFINE_PROP_UINT32("num-harts", csky_timer_state,
        num_harts, 1),
    DEFINE_PROP_UINT32("num-clic-irqs", csky_timer_state,
        num_clic_irqs, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void csky_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);

    dc->realize = csky_timer_realize;
    device_class_set_props(dc, csky_timer_properties);
    dc->vmsd = &vmstate_csky_timer;
    dc->desc = "cskysim type: TIMER";
    dc->user_creatable = true;
}

static const TypeInfo csky_timer_info = {
    .name          = TYPE_CSKY_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_timer_state),
    .instance_init = csky_timer_init,
    .class_init    = csky_timer_class_init,
};

static void csky_timer_register_types(void)
{
    type_register_static(&csky_timer_info);
}

type_init(csky_timer_register_types)
