/*
 * CSKY UART emulation
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/char/csky_uart.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"

/* lsr:line status register */
#define lsr_TEMT 0x40
#define lsr_THRE 0x20   /* no new data has been written to the THR or TX FIFO */
#define lsr_OE   0x2    /* overruun error */

/* at least one character in the RBR or the receiver FIFO */
#define lsr_DR   0x1

/* flags: USR user status register */
#define usr_REF  0x10   /* Receive FIFO Full */
#define usr_RFNE 0x8    /* Receive FIFO not empty */
#define usr_TFE  0x4    /* transmit FIFO empty */
#define usr_TFNF 0x2    /* transmit FIFO not full */

/* interrupt type */
#define INT_NONE 0x1   /* no interrupt */
#define INT_TX 0x2     /* Transmitter holding register empty */
#define INT_RX 0x4     /* Receiver data available */

static void csky_uart_update(csky_uart_state *s)
{
    uint32_t flags = 0;

    flags = (s->iir & 0xf) == INT_TX && (s->ier & 0x2) != 0;
    flags |= (s->iir & 0xf) == INT_RX && (s->ier & 0x1) != 0;
    if (s->irq) {
        qemu_set_irq(s->irq, flags != 0);
    }
    if (s->clic_irq) {
        qemu_set_irq(s->clic_irq, flags != 0);
    }
}

static uint64_t csky_uart_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_uart_state *s = (csky_uart_state *)opaque;
    uint32_t c;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch ((offset & 0xfff) >> 2) {
    case 0x0: /* RBR,DLL */
        if (s->lcr & 0x80) {
            ret = s->dll;
        } else if (s->fcr & 0x1) {
            s->usr &= ~usr_REF;   /* receive fifo not full */
            c = s->rx_fifo[s->rx_pos];
            if (s->rx_count > 0) {
                s->rx_count--;
                if (++s->rx_pos == 16) {
                    s->rx_pos = 0;
                }
            }
            if (s->rx_count == 0) {
                s->lsr &= ~lsr_DR;
                s->usr &= ~usr_RFNE;    /* receive fifo empty */
            }
            s->iir = (s->iir & ~0xf) | INT_NONE;
            csky_uart_update(s);
            qemu_chr_fe_accept_input(&s->chr);
            ret =  c;
        } else {
            s->usr &= ~usr_REF;
            s->usr &= ~usr_RFNE;
            s->lsr &= ~lsr_DR;
            s->iir = (s->iir & ~0xf) | INT_NONE;
            csky_uart_update(s);
            s->rx_count--;
            qemu_chr_fe_accept_input(&s->chr);
            ret =  s->rx_fifo[0];
        }
        break;
    case 0x1: /* DLH, IER */
        if (s->lcr & 0x80) {
            ret = s->dlh;
        } else {
            ret = s->ier;
        }
        break;
    case 0x2: /* IIR */
        if ((s->iir & 0xf) == INT_TX) {
            s->iir = (s->iir & ~0xf) | INT_NONE;
            csky_uart_update(s);
            ret = (s->iir & ~0xf) | INT_TX;
        } else {
            ret = s->iir;
        }
        break;
    case 0x3: /* LCR */
        ret = s->lcr;
        break;
    case 0x4: /* MCR */
        ret = s->mcr;
        break;
    case 0x5: /* LSR */
        ret = s->lsr;
        break;
    case 0x6: /* MSR */
        ret = s->msr;
        break;
    case 0x1f: /* USR */
        ret = s->usr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_read: Bad offset %x\n", (int)offset);
    }

    return ret;
}

static void csky_uart_fcr_update(csky_uart_state *s)
{
    /* update rx_trigger */
    if (s->fcr & 0x1) {
        /* fifo enabled */
        switch ((s->fcr >> 6) & 0x3) {
        case 0:
            s->rx_trigger = 1;
            break;
        case 1:
            s->rx_trigger = 4;
            break;
        case 2:
            s->rx_trigger = 8;
            break;
        case 3:
            s->rx_trigger = 14;
            break;
        default:
            s->rx_trigger = 1;
            break;
        }
    } else {
        s->rx_trigger = 1;
    }

    /* reset rx_fifo */
    if (s->fcr & 0x2) {
        s->rx_pos = 0;
        s->rx_count = 0;
    }
}

static void csky_uart_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    csky_uart_state *s = (csky_uart_state *)opaque;
    unsigned char ch;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_write: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset >> 2) {
    case 0x0: /*dll, thr */
        if (s->lcr & 0x80) {
            s->dll = value;
        } else {
            ch = value;
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
            s->lsr |= (lsr_THRE | lsr_TEMT);
            if ((s->iir & 0xf) != INT_RX) {
                s->iir = (s->iir & ~0xf) | INT_TX;
            }
            csky_uart_update(s);
        }
        break;
    case 0x1: /* DLH, IER */
        if (s->lcr & 0x80) {
            s->dlh = value;
        } else {
            s->ier = value;
            s->iir = (s->iir & ~0xf) | INT_TX;
            csky_uart_update(s);
        }
        break;
    case 0x2: /* FCR */
        if ((s->fcr & 0x1) ^ (value & 0x1)) {
            /* change fifo enable bit, reset rx_fifo */
            s->rx_pos = 0;
            s->rx_count = 0;
        }
        s->fcr = value;
        csky_uart_fcr_update(s);
        break;
    case 0x3: /* LCR */
        s->lcr = value;
        break;
    case 0x4: /* MCR */
        s->mcr = value;
        break;
    case 0x5: /* LSR read only*/
        return;
    case 0x6: /* MSR read only*/
        return;
    case 0x1f: /* USR read only*/
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_write: Bad offset %x\n", (int)offset);
    }
}

static int csky_uart_can_receive(void *opaque)
{
    /* always can receive data */
    csky_uart_state *s = (csky_uart_state *)opaque;

    if (s->fcr & 0x1) { /* fifo enabled */
        /*
         * Fixme: May be we should return the 16 - s->rx_count,
         * However, we lack for the real use case
         */
        return s->rx_count < 16;
    } else {
        return s->rx_count < 1;
    }
}


static void csky_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    csky_uart_state *s = (csky_uart_state *)opaque;
    int slot;

    if (size < 1) {
        return;
    }

    if (s->usr & usr_REF) {
        s->lsr |= lsr_OE;  /* overrun error */
    }

    if (!(s->fcr & 0x1)) { /* none fifo mode */
        s->rx_fifo[0] = *buf;
        s->usr |= usr_REF;
        s->usr |= usr_RFNE;
        s->iir = (s->iir & ~0xf) | INT_RX;
        s->lsr |= lsr_DR;
        s->rx_count++;
        csky_uart_update(s);
        return;
    }

    /* fifo mode */
    slot = s->rx_pos + s->rx_count;
    if (slot >= 16) {
        slot -= 16;
    }
    s->rx_fifo[slot] = *buf;
    s->rx_count++;
    s->lsr |= lsr_DR;
    s->usr |= usr_RFNE;     /* receive fifo not empty */
    if (s->rx_count == 16) {
        s->usr |= usr_REF;    /* receive fifo full */
    }
    s->iir = (s->iir & ~0xf) | INT_RX;
    csky_uart_update(s);
    return;
}

static void csky_uart_event(void *opaque, QEMUChrEvent event)
{
}

static const MemoryRegionOps csky_uart_ops = {
    .read = csky_uart_read,
    .write = csky_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_csky_uart = {
    .name = TYPE_CSKY_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(dll, csky_uart_state),
        VMSTATE_UINT32(dlh, csky_uart_state),
        VMSTATE_UINT32(ier, csky_uart_state),
        VMSTATE_UINT32(iir, csky_uart_state),
        VMSTATE_UINT32(fcr, csky_uart_state),
        VMSTATE_UINT32(lcr, csky_uart_state),
        VMSTATE_UINT32(mcr, csky_uart_state),
        VMSTATE_UINT32(lsr, csky_uart_state),
        VMSTATE_UINT32(msr, csky_uart_state),
        VMSTATE_UINT32(usr, csky_uart_state),
        VMSTATE_UINT32_ARRAY(rx_fifo, csky_uart_state, 16),
        VMSTATE_INT32(rx_pos, csky_uart_state),
        VMSTATE_INT32(rx_count, csky_uart_state),
        VMSTATE_INT32(rx_trigger, csky_uart_state),
        VMSTATE_END_OF_LIST()
    }
};

static Property csky_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", csky_uart_state, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void csky_uart_init(Object *obj)
{
    csky_uart_state *s = CSKY_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_uart_ops, s,
                          TYPE_CSKY_UART, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->clic_irq);

    s->rx_trigger = 1;
    s->dlh = 0x4;
    s->iir = 0x1;
    s->lsr = 0x60;
    s->usr = 0x6;
}

static void csky_uart_realize(DeviceState *dev, Error **errp)
{
    csky_uart_state *s = CSKY_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, csky_uart_can_receive, csky_uart_receive,
                             csky_uart_event, NULL, s, NULL, true);
}

static void csky_uart_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);

    dc->realize = csky_uart_realize;
    dc->vmsd = &vmstate_csky_uart;
    device_class_set_props(dc, csky_uart_properties);
    dc->desc = "cskysim type: UART";
    dc->user_creatable = true;
}

static const TypeInfo csky_uart_info = {
    .name          = TYPE_CSKY_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_uart_state),
    .instance_init = csky_uart_init,
    .class_init    = csky_uart_class_init,
};


static void csky_uart_register_types(void)
{
    type_register_static(&csky_uart_info);
}

type_init(csky_uart_register_types)

DeviceState *
csky_uart_create(hwaddr addr, qemu_irq irq, qemu_irq clic_irq, Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_CSKY_UART);
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(s, 0, addr);
    if (irq) {
        sysbus_connect_irq(s, 0, irq);
    }
    if (clic_irq) {
        sysbus_connect_irq(s, 1, clic_irq);
    }
    g_assert(irq || clic_irq);

    return dev;
}
