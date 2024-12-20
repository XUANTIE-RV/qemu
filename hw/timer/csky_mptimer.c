/*
 * CSKY mptimer emulation.
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
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "hw/ptimer.h"
#include "hw/core/cpu.h"
#include "hw/timer/csky_mptimer.h"
#include "migration/vmstate.h"

#define THEAD_MPTIMER_CTLR_ENABLE         (1 << 0)
#define THEAD_MPTIMER_CTLR_DIVISION       (0xff << 8)
#define THEAD_MPTIMER_PTIM_CTLR_ENABLE    (1 << 0)
#define THEAD_MPTIMER_PTIM_CTLR_IE_MASK        (1 << 1)

#define QEMU_VIRT_CLOCK_FREQ 1000000000ll

uint32_t csky_mptimer_freq = 50000000;

static inline uint64_t csky_mptimer_get_cnt(csky_mptimer_state *s)
{
    uint64_t counter;
    counter = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    counter = counter / (QEMU_VIRT_CLOCK_FREQ / s->freq);
    return counter;
}

/* call from coprocessor instruction */
uint32_t csky_mptimer_get(csky_mptimer_state *s, int index, int pos)
{
    uint32_t value;
    csky_mptimer_unit *tu = &s->mptimer_unit[index];
    bql_lock();

    switch (pos) {
    case 0: /* PTIM_CTLR */
        value = tu->ctlr;
        break;
    case 1: /* PTIM_ISR */
        value = tu->isr;
        break;
    case 2: /* PTIM_CCVR_HI */
        value = extract64(csky_mptimer_get_cnt(s), 32, 32);
        break;
    case 3: /* PTIM_CCVR_LO */
        value =  extract64(csky_mptimer_get_cnt(s), 0, 32);
        break;
    case 4: /* PTIM_CMVR_HI */
        value =  extract64(tu->cmpr, 32, 32);
        break;
    case 5: /* PTIM_CMVR_LO */
        value =  extract64(tu->cmpr, 0, 32);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mptimer_get: Bad offset %x\n", (int)pos);
        value = 0;
        break;
    }
    bql_unlock();
    return value;
}

/* call from coporcessor instruction */
void  csky_mptimer_set(csky_mptimer_state *s, int index, int pos,
    uint32_t value)
{
    bql_lock();
    csky_mptimer_unit *tu = &s->mptimer_unit[index];

    switch (pos) {
    case 0: /* PTIM_CTLR */
        tu->ctlr = value;
        qemu_log_mask(LOG_GUEST_ERROR,
            "csky_mptimer_set: Set timer %d ctlr %x", index, value);
        break;
    case 1: /* PTIM_ISR */
        if (tu->isr & 0x1) {
            tu->isr &= ~(0x1);
            if (tu->int_level > 0) {
                qemu_irq_lower(tu->irq);
                tu->int_level = 0;
            }
        }
        break;
    case 4: /* PTIM_CMPR_HI */
        tu->cmpr = deposit64(tu->cmpr, 32, 32, value);
        break;
    case 5: /* PTIM_CMPR_LO */
        tu->cmpr = deposit64(tu->cmpr, 0, 32, value);
        break;
    case 6: /* PTIM_LVR */
        if (tu->ctlr & THEAD_MPTIMER_PTIM_CTLR_ENABLE) {
            tu->cmpr = csky_mptimer_get_cnt(s) + value;
            ptimer_transaction_begin(tu->timer);
            ptimer_set_limit(tu->timer, value, 1);
            ptimer_run(tu->timer, 1);
            ptimer_transaction_commit(tu->timer);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mptimer_set: Bad offset %x\n", (int)pos);
        break;
    }
    bql_unlock();
}

static void csky_mptimer_unit_tick(void *opaque)
{
    csky_mptimer_unit *tu = (csky_mptimer_unit *)opaque;
    if (tu->ctlr & THEAD_MPTIMER_PTIM_CTLR_ENABLE) {
        tu->isr |= 0x1; /* set TS */
        if (!(tu->ctlr & THEAD_MPTIMER_PTIM_CTLR_IE_MASK)) {
            qemu_set_irq(tu->irq, 1);
            tu->int_level = 1;
        }
    }
}

static uint64_t csky_mptimer_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_mptimer_state *s = (csky_mptimer_state *)opaque;
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mptimer_read: Bad read size\n");
    }
    switch (offset >> 2) {
    case 0: /* CNT_CTLR */
        return s->ctlr;
    case 1: /* CNT_ICVR_HI */
        return extract64(s->icvr, 32, 32);
    case 2: /* CNT_ICVR_LO */
        return extract64(s->icvr, 0, 32);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "csky_mptimer_read: Bad register offset %d\n", (int)offset);
        return 0;
    }
}

static void csky_mptimer_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    csky_mptimer_state *s = (csky_mptimer_state *)opaque;
    switch (offset >> 2) {
    case 0: /* CNT_CTLR */
        s->ctlr = value;
        break;
    case 1: /* CNT_ICVR_HI */
        s->icvr = deposit64(s->icvr, 32, 32, value);
        break;
    case 2: /* CNT_ICVR_LO */
        s->icvr = deposit64(s->icvr, 0, 32, value);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mptimer_write: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps csky_mptimer_ops = {
    .read = csky_mptimer_read,
    .write = csky_mptimer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void csky_mptimer_set_freq(uint32_t freq)
{
    csky_mptimer_freq = freq;
}

static void csky_mptimer_init(Object *obj)
{
    int i;
    csky_mptimer_state *s = THEAD_MPTIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    s->freq = csky_mptimer_freq;
    for (i = 0; i < THEAD_MPTIMER_MAX_CPUS; i++) {
        csky_mptimer_unit *tu = &s->mptimer_unit[i];
        tu->timer = ptimer_init(csky_mptimer_unit_tick, tu, PTIMER_POLICY_LEGACY);
        ptimer_transaction_begin(tu->timer);
        ptimer_set_freq(tu->timer, s->freq);
        ptimer_transaction_commit(tu->timer);
        sysbus_init_irq(sbd, &tu->irq);
    }
    s->icvr = 0;
    s->ctlr |= THEAD_MPTIMER_CTLR_ENABLE;
    memory_region_init_io(&s->iomem, obj, &csky_mptimer_ops, s,
                              "csky_mptimer", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

}
static const VMStateDescription vmstate_csky_mptimer_unit = {
    .name = "csky_mptimer_unit",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctlr, csky_mptimer_unit),
        VMSTATE_UINT32(isr, csky_mptimer_unit),
        VMSTATE_UINT32(int_level, csky_mptimer_unit),
        VMSTATE_UINT64(cmpr, csky_mptimer_unit),
        VMSTATE_PTIMER(timer, csky_mptimer_unit),
        VMSTATE_END_OF_LIST()
    }
};
static const VMStateDescription vmstate_csky_mptimer = {
    .name = TYPE_THEAD_MPTIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctlr, csky_mptimer_state),
        VMSTATE_UINT64(icvr, csky_mptimer_state),
        VMSTATE_UINT32(freq, csky_mptimer_state),
        VMSTATE_STRUCT_ARRAY(mptimer_unit, csky_mptimer_state,
            THEAD_MPTIMER_MAX_CPUS, 1, vmstate_csky_mptimer_unit,
            csky_mptimer_unit),
        VMSTATE_END_OF_LIST()
    }
};

static void csky_mptimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_csky_mptimer;
}

static const TypeInfo csky_mptimer_info = {
    .name          = TYPE_THEAD_MPTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_mptimer_state),
    .instance_init = csky_mptimer_init,
    .class_init    = csky_mptimer_class_init,
};

static void csky_mptimer_register_types(void)
{
    type_register_static(&csky_mptimer_info);
}

type_init(csky_mptimer_register_types)
