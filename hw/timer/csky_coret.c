/*
 * CSKY coret emulation
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
#include "hw/ptimer.h"
#include "chardev/char.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/timer/csky_coret.h"

/* CoreTim */
#define CT_CSR_COUNTFLAG    (1 << 16)
#define CT_CSR_INTERNAL_CLK (1 << 2)
#define CT_CSR_TICKINT      (1 << 1)
#define CT_CSR_ENABLE       (1 << 0)

uint32_t coretim_freq = 1000000000ll;

static void csky_coret_update(csky_coret_state *s, int value)
{
    /* Update interrupts.  */
    if (value) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint64_t csky_coret_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_coret_state *s = (csky_coret_state *)opaque;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_coret_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x10: /* CoreTim CSR */
        ret = s->coret_csr;
        s->coret_csr &= ~CT_CSR_COUNTFLAG;
        csky_coret_update(s, 0);
        break;
    case 0x14: /* CoreTim ReloadValue */
        ret = s->coret_rvr;
        break;
    case 0x18: /* CoreTim CurrentValue */
        if (s->coret_csr & CT_CSR_ENABLE) {
            ret = ptimer_get_count(s->timer);
        }
        break;
    case 0x1c:
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_coret_read: Bad register offset 0x%x\n",
                      (int)offset);
    }

    return ret;
}

static void csky_coret_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    csky_coret_state *s = (csky_coret_state *)opaque;
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_coret_write: 0x%x must word align write\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x10:  /* CoreTim CSR */
        s->coret_csr = (s->coret_csr & CT_CSR_COUNTFLAG) | (value & 0x7);

        ptimer_transaction_begin(s->timer);
        ptimer_set_limit(s->timer, s->coret_rvr, s->coret_csr & CT_CSR_ENABLE);
        if (s->coret_csr & CT_CSR_ENABLE) {
            ptimer_run(s->timer, 0);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case 0x14:  /* CoreTim ReloadValue */
        s->coret_rvr = value & 0x00ffffff;
        ptimer_transaction_begin(s->timer);
        if (s->coret_rvr == 0) {
            ptimer_stop(s->timer);
        } else if (s->coret_csr & CT_CSR_ENABLE) {
            ptimer_set_limit(s->timer, s->coret_rvr, 0);
            ptimer_run(s->timer, 0);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case 0x18:  /* CoreTim CurrentValue */
        ptimer_transaction_begin(s->timer);
        ptimer_set_limit(s->timer, s->coret_rvr, 1);
        ptimer_transaction_commit(s->timer);
        s->coret_csr &= ~CT_CSR_COUNTFLAG;
        break;
    case 0x1c:
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_coret_write: Bad register offset 0x%x\n",
                      (int)offset);
        return;
    }
}

static const MemoryRegionOps csky_coret_ops = {
    .read = csky_coret_read,
    .write = csky_coret_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void csky_coret_tick(void *opaque)
{
    csky_coret_state *s = (csky_coret_state *)opaque;

    ptimer_set_limit(s->timer, s->coret_rvr, 1);
    s->coret_csr |= CT_CSR_COUNTFLAG;
    if (s->coret_csr & CT_CSR_TICKINT) {
        csky_coret_update(s, 1);
    }
}

static void csky_coret_init(Object *obj)
{
    csky_coret_state *s = CSKY_CORET(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->coret_csr = CT_CSR_INTERNAL_CLK;

    /* CSKY CoreTim intialization */
    s->timer = ptimer_init(csky_coret_tick, s, PTIMER_POLICY_LEGACY);

    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, coretim_freq);
    ptimer_transaction_commit(s->timer);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, obj, &csky_coret_ops,
                          s, TYPE_CSKY_CORET, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_coret = {
    .name = TYPE_CSKY_CORET,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(timer, csky_coret_state),
        VMSTATE_UINT32(coret_csr, csky_coret_state),
        VMSTATE_UINT32(coret_rvr, csky_coret_state),
        VMSTATE_END_OF_LIST()
    }
};

void csky_coret_set_freq(uint32_t freq)
{
    coretim_freq = freq;
}

static void csky_coret_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);

    dc->vmsd  = &vmstate_coret;
    dc->desc = "csky type: TIMER";
    dc->user_creatable = true;
}

static const TypeInfo csky_coret_info = {
    .name          = TYPE_CSKY_CORET,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = csky_coret_init,
    .instance_size = sizeof(csky_coret_state),
    .class_init    = csky_coret_class_init,
};

static void csky_coret_register_types(void)
{
    type_register_static(&csky_coret_info);
}

type_init(csky_coret_register_types)
