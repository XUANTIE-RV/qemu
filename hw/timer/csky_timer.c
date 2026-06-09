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

/* Timer Control Register bit definitions */
#define TIMER_CTRL_ENABLE         (1 << 0)  /* Timer enable bit */
#define TIMER_CTRL_MODE           (1 << 1)  /* Timer mode: 0=one-shot, 1=periodic */
#define TIMER_CTRL_IE             (1 << 2)  /* Interrupt enable bit */
#define TIMER_CTRL_CLOCK          (1 << 3)  /* Clock source selection */

/* Timer register offsets */
#define TIMER_REG_LOAD_COUNT      0x00      /* Load count register */
#define TIMER_REG_CURRENT_VALUE   0x04      /* Current value register */
#define TIMER_REG_CONTROL         0x08      /* Control register */
#define TIMER_REG_EOI             0x0C      /* End of interrupt register */
#define TIMER_REG_INT_STATUS      0x10      /* Interrupt status register */
#define TIMER_REG_SIZE            0x14      /* Size of one timer's register set */

/* Timer system register offsets (offset 0x100) */
#define TIMER_SYS_REG_INT_STATUS  0x00      /* System interrupt status */
#define TIMER_SYS_REG_EOI         0x04      /* System end of interrupt */
#define TIMER_SYS_REG_RAW_INT_STATUS 0x08   /* System raw interrupt status */

/* Number of timer channels */
#define NUM_TIMERS                4

/* Default timer frequency (1 GHz) */
uint32_t csky_timer_freq = 1000000000ll;

/**
 * Update interrupt state for a specific timer channel.
 *
 * Raises or lowers interrupts based on the current interrupt level and
 * interrupt enable settings for the specified timer.
 *
 * @param s: Pointer to the CSKY timer state
 * @param index: Timer channel index (0-3)
 */
static void csky_timer_update(csky_timer_state *s, int index)
{
    bool should_raise_irq = s->int_level[index] && !(s->control[index] & TIMER_CTRL_IE);
    int i;

    /* Update legacy interrupt line */
    if (s->irqs[index]) {
        if (should_raise_irq) {
            qemu_irq_raise(s->irqs[index]);
        } else {
            qemu_irq_lower(s->irqs[index]);
        }
    }

    /* Update CLIC (Core Local Interrupt Controller) interrupts for all harts */
    for (i = 0; i < s->num_harts; i++) {
        int clic_idx = s->num_harts * index + i;
        if (s->clic_irqs[clic_idx]) {
            if (should_raise_irq) {
                qemu_irq_raise(s->clic_irqs[clic_idx]);
            } else {
                qemu_irq_lower(s->clic_irqs[clic_idx]);
            }
        }
    }
}

/**
 * Read from a timer channel's register.
 *
 * @param s: Pointer to the CSKY timer state
 * @param offset: Register offset within the timer's register space
 * @param index: Timer channel index (0-3)
 * @return: Register value
 */
static uint32_t csky_timer_read(csky_timer_state *s, hwaddr offset, int index)
{
    uint32_t reg_offset = offset >> 2;

    switch (reg_offset) {
    case 0: /* LoadCount register */
        return s->limit[index];

    case 1: /* CurrentValue register */
        return ptimer_get_count(s->timer[index]);

    case 2: /* Control register */
        return s->control[index];

    case 3: /* End of Interrupt (EOI) register */
        s->int_level[index] = 0;
        csky_timer_update(s, index);
        return 0;

    case 4: /* Interrupt Status register */
        return s->int_level[index] && !(s->control[index] & TIMER_CTRL_IE);

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timer_read: Bad offset 0x%x for timer %d\n",
                      (uint32_t)offset, index);
        return 0;
    }
}

/**
 * Reload the timer count value.
 *
 * Sets the timer's limit value and optionally reloads the current counter.
 *
 * @param s: Pointer to the CSKY timer state
 * @param reload: Non-zero to reload the counter immediately
 * @param index: Timer channel index (0-3)
 */
static void csky_timer_reload(csky_timer_state *s, int reload, int index)
{
    uint32_t limit = s->limit[index];
    ptimer_set_limit(s->timer[index], limit, reload);
}

/**
 * Write to a timer channel's register.
 *
 * @param s: Pointer to the CSKY timer state
 * @param offset: Register offset within the timer's register space
 * @param value: Value to write
 * @param index: Timer channel index (0-3)
 */
static void csky_timer_write(csky_timer_state *s, hwaddr offset,
                             uint64_t value, int index)
{
    uint32_t reg_offset = offset >> 2;

    switch (reg_offset) {
    case 0: /* LoadCount register */
        s->limit[index] = (uint32_t)value;
        if (s->control[index] & TIMER_CTRL_ENABLE) {
            ptimer_transaction_begin(s->timer[index]);
            csky_timer_reload(s, 0, index);
            ptimer_run(s->timer[index], 0);
            ptimer_transaction_commit(s->timer[index]);
        }
        break;

    case 2: /* Control register */
        ptimer_transaction_begin(s->timer[index]);

        /* Stop timer if it was previously running */
        if (s->control[index] & TIMER_CTRL_ENABLE) {
            ptimer_stop(s->timer[index]);
        }

        /* Update control register */
        s->control[index] = (uint32_t)value;
        csky_timer_reload(s, s->control[index] & TIMER_CTRL_ENABLE, index);
        ptimer_set_freq(s->timer[index], s->freq[index]);

        /* Start timer if newly enabled */
        if (s->control[index] & TIMER_CTRL_ENABLE) {
            ptimer_run(s->timer[index], 0);
        }

        ptimer_transaction_commit(s->timer[index]);
        break;

    /* Read-only registers - ignore writes */
    case 1: /* CurrentValue register */
    case 3: /* EOI register */
    case 4: /* Interrupt Status register */
        return;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timer_write: Bad offset 0x%x for timer %d\n",
                      (uint32_t)offset, index);
        return;
    }

    csky_timer_update(s, index);
}

/**
 * Timer callback function for timer channel 0.
 * Called when the timer expires.
 */
static void csky_timer_tick0(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 0);
    s->int_level[0] = 1;
    csky_timer_update(s, 0);
}

/**
 * Timer callback function for timer channel 1.
 * Called when the timer expires.
 */
static void csky_timer_tick1(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 1);
    s->int_level[1] = 1;
    csky_timer_update(s, 1);
}

/**
 * Timer callback function for timer channel 2.
 * Called when the timer expires.
 */
static void csky_timer_tick2(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 2);
    s->int_level[2] = 1;
    csky_timer_update(s, 2);
}

/**
 * Timer callback function for timer channel 3.
 * Called when the timer expires.
 */
static void csky_timer_tick3(void *opaque)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    csky_timer_reload(s, 1, 3);
    s->int_level[3] = 1;
    csky_timer_update(s, 3);
}

/**
 * Read from the timer device's memory-mapped registers.
 *
 * Handles reads for individual timer channels (0-3) and system registers.
 *
 * @param opaque: Pointer to the CSKY timer state
 * @param offset: Byte offset within the device's memory space
 * @param size: Size of the read operation
 * @return: Register value
 */
static uint64_t csky_timers_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    int timer_index;
    int i;
    uint32_t ret;

    /* Only 32-bit reads are supported */
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_read: Bad read size %u\n", size);
    }

    timer_index = offset / TIMER_REG_SIZE;

    switch (timer_index) {
    case 0: /* Timer channel 0 */
    case 1: /* Timer channel 1 */
    case 2: /* Timer channel 2 */
    case 3: /* Timer channel 3 */
        return csky_timer_read(s, offset % TIMER_REG_SIZE, timer_index);

    case 8: /* Timer System Registers */
        switch ((offset % TIMER_REG_SIZE) >> 2) {
        case 0: /* System interrupt status */
            ret = ((s->int_level[0] && !(s->control[0] & TIMER_CTRL_IE)) |
                   ((s->int_level[1] && !(s->control[1] & TIMER_CTRL_IE)) << 1) |
                   ((s->int_level[2] && !(s->control[2] & TIMER_CTRL_IE)) << 2) |
                   ((s->int_level[3] && !(s->control[3] & TIMER_CTRL_IE)) << 3));
            return ret;

        case 1: /* System EOI - clear all interrupts */
            for (i = 0; i < NUM_TIMERS; i++) {
                s->int_level[i] = 0;
                csky_timer_update(s, i);
            }
            return 0;

        case 2: /* System raw interrupt status */
            return (s->int_level[0] | (s->int_level[1] << 1) |
                    (s->int_level[2] << 2) | (s->int_level[3] << 3));

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_timers_read: Bad system register offset 0x%x\n",
                          (uint32_t)offset);
            return 0;
        }

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_read: Invalid timer index %d\n", timer_index);
        return 0;
    }
}

/**
 * Write to the timer device's memory-mapped registers.
 *
 * Handles writes for individual timer channels (0-3). System registers
 * are read-only and writes are ignored.
 *
 * @param opaque: Pointer to the CSKY timer state
 * @param offset: Byte offset within the device's memory space
 * @param value: Value to write
 * @param size: Size of the write operation
 */
static void csky_timers_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    csky_timer_state *s = (csky_timer_state *)opaque;
    int timer_index;

    timer_index = offset / TIMER_REG_SIZE;

    /* Only support writes to timer channels 0-3 */
    if (timer_index >= NUM_TIMERS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_write: Invalid timer index %d\n", timer_index);
        return;
    }

    /* Only 32-bit writes are supported */
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_timers_write: Bad write size %u\n", size);
    }

    csky_timer_write(s, offset % TIMER_REG_SIZE, value, timer_index);
}

/* Memory region operations for the CSKY timer device */
static const MemoryRegionOps csky_timer_ops = {
    .read = csky_timers_read,
    .write = csky_timers_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/**
 * Set the default frequency for CSKY timers.
 *
 * @param freq: Timer frequency in Hz
 */
void csky_timer_set_freq(uint32_t freq)
{
    csky_timer_freq = freq;
}

/**
 * Realize the CSKY timer device.
 *
 * Initializes the memory region and GPIO outputs for interrupts.
 *
 * @param dev: Pointer to the device state
 * @param errp: Pointer to error information
 */
static void csky_timer_realize(DeviceState *dev, Error **errp)
{
    csky_timer_state *s = CSKY_TIMER(dev);

    /* Initialize memory-mapped I/O region */
    memory_region_init_io(&s->iomem, OBJECT(dev), &csky_timer_ops, s,
                          TYPE_CSKY_TIMER, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    /* Allocate and initialize GPIO outputs for interrupts */
    s->irqs = g_new(qemu_irq, NUM_TIMERS);
    s->clic_irqs = g_new(qemu_irq, NUM_TIMERS * s->num_harts);
    qdev_init_gpio_out(dev, s->irqs, NUM_TIMERS);
    qdev_init_gpio_out(dev, s->clic_irqs, NUM_TIMERS * s->num_harts);
}

/**
 * Create and initialize a CSKY timer device.
 *
 * Creates the timer device, sets its properties, maps it to the specified
 * address, and connects the interrupt lines.
 *
 * @param addr: Base address for the timer device's memory-mapped registers
 * @param irq: Array of legacy interrupt lines (size must be NUM_TIMERS)
 * @param clic_irq: Array of CLIC interrupt lines (or NULL if not used)
 * @param harts_num: Number of hardware threads (harts)
 * @param clic_irqs_num: Number of CLIC IRQs per hart
 * @return: Pointer to the created device
 */
DeviceState *
csky_timer_create(hwaddr addr, qemu_irq *irq,
                  qemu_irq *clic_irq, uint32_t harts_num,
                  uint32_t clic_irqs_num)
{
    int i, j;
    DeviceState *dev = qdev_new(TYPE_CSKY_TIMER);

    /* Set device properties */
    qdev_prop_set_uint32(dev, "num-harts", harts_num);
    qdev_prop_set_uint32(dev, "num-clic-irqs", clic_irqs_num);

    /* Realize and map the device */
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    /* Connect legacy interrupt lines */
    for (i = 0; i < NUM_TIMERS; i++) {
        qdev_connect_gpio_out(dev, i, irq[i]);
    }

    /* Connect CLIC interrupt lines if provided */
    if (clic_irq != NULL) {
        for (i = 0; i < NUM_TIMERS; i++) {
            for (j = 0; j < harts_num; j++) {
                int gpio_idx = NUM_TIMERS + harts_num * i + j;
                int irq_idx = i + j * clic_irqs_num;
                qdev_connect_gpio_out(dev, gpio_idx, clic_irq[irq_idx]);
            }
        }
    }

    return dev;
}

/**
 * Initialize the CSKY timer device.
 *
 * Creates and initializes the ptimer instances for each timer channel.
 *
 * @param obj: Pointer to the timer object
 */
static void csky_timer_init(Object *obj)
{
    csky_timer_state *s = CSKY_TIMER(obj);

    /* Initialize timer channel 0 */
    s->freq[0] = csky_timer_freq;
    s->timer[0] = ptimer_init(csky_timer_tick0, s, PTIMER_POLICY_LEGACY);

    /* Initialize timer channel 1 */
    s->freq[1] = csky_timer_freq;
    s->timer[1] = ptimer_init(csky_timer_tick1, s, PTIMER_POLICY_LEGACY);

    /* Initialize timer channel 2 */
    s->freq[2] = csky_timer_freq;
    s->timer[2] = ptimer_init(csky_timer_tick2, s, PTIMER_POLICY_LEGACY);

    /* Initialize timer channel 3 */
    s->freq[3] = csky_timer_freq;
    s->timer[3] = ptimer_init(csky_timer_tick3, s, PTIMER_POLICY_LEGACY);
}

/* VM state description for migration/save-restore support */
static const VMStateDescription vmstate_csky_timer = {
    .name = TYPE_CSKY_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER_ARRAY(timer, csky_timer_state, NUM_TIMERS),
        VMSTATE_UINT32_ARRAY(control, csky_timer_state, NUM_TIMERS),
        VMSTATE_UINT32_ARRAY(limit, csky_timer_state, NUM_TIMERS),
        VMSTATE_INT32_ARRAY(freq, csky_timer_state, NUM_TIMERS),
        VMSTATE_INT32_ARRAY(int_level, csky_timer_state, NUM_TIMERS),
        VMSTATE_END_OF_LIST()
    }
};

/* Device properties */
static Property csky_timer_properties[] = {
    DEFINE_PROP_UINT32("num-harts", csky_timer_state,
                       num_harts, 1),
    DEFINE_PROP_UINT32("num-clic-irqs", csky_timer_state,
                       num_clic_irqs, 0),
    DEFINE_PROP_END_OF_LIST(),
};

/**
 * Initialize the CSKY timer device class.
 *
 * @param klass: Pointer to the object class
 * @param data: Additional data (unused)
 */
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

/* Type information for the CSKY timer device */
static const TypeInfo csky_timer_info = {
    .name          = TYPE_CSKY_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_timer_state),
    .instance_init = csky_timer_init,
    .class_init    = csky_timer_class_init,
};

/**
 * Register the CSKY timer device type.
 */
static void csky_timer_register_types(void)
{
    type_register_static(&csky_timer_info);
}

type_init(csky_timer_register_types)
