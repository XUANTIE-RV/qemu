/*
 * DesignWare APB UART Emulation for CSKY/RISC-V
 *
 * This file implements the DW_apb_uart IP core emulation following the
 * DesignWare DW_apb_uart Databook specification.
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include "hw/core/cpu.h"

/*
 * =============================================================================
 * DW_apb_uart Register Offset Definitions (active when DLAB=0 or DLAB=1)
 * =============================================================================
 */
#define REG_RBR_THR_DLL     0x00    /* RBR(R)/THR(W) when DLAB=0, DLL when DLAB=1 */
#define REG_IER_DLH         0x04    /* IER when DLAB=0, DLH when DLAB=1 */
#define REG_IIR_FCR         0x08    /* IIR(R) / FCR(W) */
#define REG_LCR             0x0C    /* Line Control Register */
#define REG_MCR             0x10    /* Modem Control Register */
#define REG_LSR             0x14    /* Line Status Register */
#define REG_MSR             0x18    /* Modem Status Register */
#define REG_USR             0x7C    /* UART Status Register */

/*
 * LCR (Line Control Register) bit definitions
 */
#define LCR_DLAB            0x80    /* Divisor Latch Access Bit */

/*
 * LSR (Line Status Register) bit definitions
 * Reference: DW_apb_uart Databook, Chapter 5.2.6
 */
#define LSR_DR              0x01    /* Data Ready: at least one char in RBR/FIFO */
#define LSR_OE              0x02    /* Overrun Error */
#define LSR_THRE            0x20    /* Transmitter Holding Register Empty */
#define LSR_TEMT            0x40    /* Transmitter Empty: THR and TSR both empty */

/*
 * USR (UART Status Register) bit definitions
 * Reference: DW_apb_uart Databook, Chapter 5.2.14
 */
#define USR_BUSY            0x01    /* UART Busy */
#define USR_TFNF            0x02    /* Transmit FIFO Not Full */
#define USR_TFE             0x04    /* Transmit FIFO Empty */
#define USR_RFNE            0x08    /* Receive FIFO Not Empty */
#define USR_RFF             0x10    /* Receive FIFO Full */

/*
 * IIR (Interrupt Identification Register) bit definitions
 * Reference: DW_apb_uart Databook, Chapter 5.2.3
 */
#define IIR_NO_INT          0x01    /* No interrupt pending (bit0=1) */
#define IIR_THR_EMPTY       0x02    /* THR Empty interrupt (priority 3) */
#define IIR_RX_DATA_AVAIL   0x04    /* Received Data Available (priority 2) */
#define IIR_FIFO_ENABLED    0xC0    /* FIFO enabled indicator bits */

/*
 * IER (Interrupt Enable Register) bit definitions
 * Reference: DW_apb_uart Databook, Chapter 5.2.2
 */
#define IER_ERBFI           0x01    /* Enable Received Data Available Interrupt */
#define IER_ETBEI           0x02    /* Enable THR Empty Interrupt */

/*
 * FCR (FIFO Control Register) bit definitions
 * Reference: DW_apb_uart Databook, Chapter 5.2.4
 */
#define FCR_FIFOE           0x01    /* FIFO Enable */
#define FCR_RFIFOR          0x02    /* Receiver FIFO Reset */
#define FCR_XFIFOR          0x04    /* Transmitter FIFO Reset */
#define FCR_RCVR_TRIGGER    0xC0    /* Receiver Trigger Level (bits 7:6) */

#define FIFO_DEPTH          16      /* DW_apb_uart FIFO depth */

/*
 * Update interrupt status based on IIR and IER settings.
 *
 * According to DW_apb_uart spec, interrupt is asserted when:
 * - THR Empty interrupt: IIR[3:0]=0x2 and IER[1](ETBEI)=1
 * - Received Data Available: IIR[3:0]=0x4 and IER[0](ERBFI)=1
 */
static void csky_uart_update_irq(csky_uart_state *s)
{
    uint32_t iir_id = s->iir & 0x0F;
    bool irq_pending = false;

    /* Check THR Empty interrupt condition */
    if ((iir_id == IIR_THR_EMPTY) && (s->ier & IER_ETBEI)) {
        irq_pending = true;
    }

    /* Check Received Data Available interrupt condition */
    if ((iir_id == IIR_RX_DATA_AVAIL) && (s->ier & IER_ERBFI)) {
        irq_pending = true;
    }

    /* Assert interrupt to both standard IRQ and CLIC */
    if (s->irq) {
        qemu_set_irq(s->irq, irq_pending);
    }
    if (s->clic_irq) {
        qemu_set_irq(s->clic_irq, irq_pending);
    }
}

/*
 * Read a character from RX FIFO (FIFO mode) or RBR (non-FIFO mode).
 * Updates LSR, USR, and IIR accordingly.
 */
static uint32_t csky_uart_read_rx_fifo(csky_uart_state *s)
{
    uint32_t data;

    if (s->fcr & FCR_FIFOE) {
        /* FIFO mode: read from circular buffer */
        s->usr &= ~USR_RFF;  /* FIFO no longer full after read */
        data = s->rx_fifo[s->rx_pos];

        if (s->rx_count > 0) {
            s->rx_count--;
            s->rx_pos = (s->rx_pos + 1) % FIFO_DEPTH;
        }

        if (s->rx_count == 0) {
            s->lsr &= ~LSR_DR;      /* No data ready */
            s->usr &= ~USR_RFNE;    /* FIFO empty */
        }
    } else {
        /* Non-FIFO mode: single character buffer */
        data = s->rx_fifo[0];
        s->rx_count = 0;
        s->lsr &= ~LSR_DR;
        s->usr &= ~(USR_RFF | USR_RFNE);
    }

    /* Clear RX interrupt after read */
    s->iir = (s->iir & ~0x0F) | IIR_NO_INT;
    csky_uart_update_irq(s);
    qemu_chr_fe_accept_input(&s->chr);

    return data;
}

/*
 * MMIO read handler for DW_apb_uart registers.
 *
 * Register access depends on DLAB (Divisor Latch Access Bit) in LCR[7]:
 * - DLAB=1: Access DLL/DLH for baud rate configuration
 * - DLAB=0: Access RBR/THR/IER for normal operation
 */
static uint64_t csky_uart_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_uart_state *s = (csky_uart_state *)opaque;
    uint64_t ret = 0;
    uint32_t reg_index = (offset & 0xFFF) >> 2;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "csky_uart_read: offset=0x%x, pc=0x%lx\n",
                  (int)offset, current_cpu->cc->get_pc(current_cpu));

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_read: offset=0x%x requires 32-bit aligned access\n",
                      (int)offset);
    }

    switch (reg_index) {
    case 0x00:  /* RBR (Receive Buffer Register) / DLL */
        if (s->lcr & LCR_DLAB) {
            ret = s->dll;   /* DLAB=1: read Divisor Latch Low */
        } else {
            ret = csky_uart_read_rx_fifo(s);  /* DLAB=0: read received data */
        }
        break;

    case 0x01:  /* IER / DLH */
        if (s->lcr & LCR_DLAB) {
            ret = s->dlh;   /* DLAB=1: read Divisor Latch High */
        } else {
            ret = s->ier;   /* DLAB=0: read Interrupt Enable Register */
        }
        break;

    case 0x02:  /* IIR (Interrupt Identification Register) - Read Only */
        /*
         * Per DW spec: Reading IIR when THR Empty is the source clears
         * the interrupt, but we return the original value before clearing.
         */
        ret = s->iir;
        if ((s->iir & 0x0F) == IIR_THR_EMPTY) {
            s->iir = (s->iir & ~0x0F) | IIR_NO_INT;
            csky_uart_update_irq(s);
        }
        break;

    case 0x03:  /* LCR (Line Control Register) */
        ret = s->lcr;
        break;

    case 0x04:  /* MCR (Modem Control Register) */
        ret = s->mcr;
        break;

    case 0x05:  /* LSR (Line Status Register) - Read Only */
        ret = s->lsr;
        break;

    case 0x06:  /* MSR (Modem Status Register) - Read Only */
        ret = s->msr;
        break;

    case 0x1F:  /* USR (UART Status Register) - offset 0x7C */
        ret = s->usr;
        break;

    /*
     * Receiver Output Enable Register
     * This register is only valid when the DW_apb_uart is configured to have
     * RS485 interface implemented. If the RS485 is not implemented, this
     * register does not exist and reading from this register address will
     * return zero.
     */
    case (0xB4 / 4):
    /*
     * Divisor Latch Fraction Register
     * This register is only valid when the DW_apb_uart is configured to have
     * Fractional Baudrate Divisor implemented. If Fractional Baudrate divisor
     * is not implemented, this register does not exist and reading from this
     * register address will return zero.
     */
    case (0xC0 / 4):
        ret = 0;
        break;
    /*
     * 23:16 FIFO_MODE R Encoding of FIFO_MODE configuration parameter value.
     * Values:
     *  0x0 (FIFO_MODE_0): FIFO mode is 0
     *  0x1 (FIFO_MODE_16): FIFO mode is 16
     *  0x2 (FIFO_MODE_32): FIFO mode is 32
     *  0x4 (FIFO_MODE_64): FIFO mode is 64
     *  0x8 (FIFO_MODE_128): FIFO mode is 128
     *  0x10 (FIFO_MODE_256): FIFO mode is 256
     *  0x20 (FIFO_MODE_512): FIFO mode is 512
     *  0x40 (FIFO_MODE_1024): FIFO mode is 1024
     *  0x80 (FIFO_MODE_2048): FIFO mode is 2048
     * Value After Reset: UART_ENCODED_FIFO_MODE
     */
    case (0xF4 / 4):  /* Component Parameter Register */
        ret = 1 << 16;
        break;
    /*
     * UART Component Version
     * UCV register is valid only when the DW_apb_uart is configured to have additional
     * features implemented. If additional features are not implemented, this register
     * does not exist and reading from this register address returns 0.
     * Reset value: 0x3430342a
     */
    case (0xF8 / 4):
        ret = 0x3430342a;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_read: invalid offset 0x%x\n", (int)offset);
        break;
    }

    return ret;
}

/*
 * Update FIFO control settings based on FCR register.
 *
 * FCR[7:6] - RCVR Trigger Level:
 *   00 = 1 character, 01 = 1/4 full (4 chars)
 *   10 = 1/2 full (8 chars), 11 = 2 less than full (14 chars)
 *
 * FCR[2] - XFIFOR: Transmit FIFO Reset (self-clearing)
 * FCR[1] - RFIFOR: Receive FIFO Reset (self-clearing)
 * FCR[0] - FIFOE: FIFO Enable
 */
static void csky_uart_update_fcr(csky_uart_state *s)
{
    /* Set RX trigger level based on FCR[7:6] */
    if (s->fcr & FCR_FIFOE) {
        static const int trigger_levels[] = {1, 4, 8, 14};
        int level_index = (s->fcr & FCR_RCVR_TRIGGER) >> 6;
        s->rx_trigger = trigger_levels[level_index];
    } else {
        s->rx_trigger = 1;  /* Non-FIFO mode: trigger on single char */
    }

    /* Handle Receiver FIFO Reset (self-clearing bit) */
    if (s->fcr & FCR_RFIFOR) {
        s->rx_pos = 0;
        s->rx_count = 0;
    }
}

/*
 * MMIO write handler for DW_apb_uart registers.
 *
 * Register access depends on DLAB (Divisor Latch Access Bit) in LCR[7]:
 * - DLAB=1: Access DLL/DLH for baud rate configuration
 * - DLAB=0: Access THR/IER for normal operation
 */
static void csky_uart_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    csky_uart_state *s = (csky_uart_state *)opaque;
    uint32_t reg_index = offset >> 2;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "csky_uart_write: offset=0x%x, value=0x%lx, pc=0x%lx\n",
                  (int)offset, (unsigned long)value,
                  current_cpu->cc->get_pc(current_cpu));

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_write: offset=0x%x requires 32-bit aligned access\n",
                      (int)offset);
    }

    switch (reg_index) {
    case 0x00:  /* THR (Transmit Holding Register) / DLL */
        if (s->lcr & LCR_DLAB) {
            /* DLAB=1: write Divisor Latch Low for baud rate */
            s->dll = value;
        } else {
            /* DLAB=0: transmit character */
            unsigned char ch = value;
            qemu_chr_fe_write_all(&s->chr, &ch, 1);

            /* Update LSR: THR and shift register are empty after TX */
            s->lsr |= (LSR_THRE | LSR_TEMT);

            /* Generate THR Empty interrupt if no RX interrupt pending */
            if ((s->iir & 0x0F) != IIR_RX_DATA_AVAIL) {
                s->iir = (s->iir & ~0x0F) | IIR_THR_EMPTY;
            }
            csky_uart_update_irq(s);
        }
        break;

    case 0x01:  /* IER / DLH */
        if (s->lcr & LCR_DLAB) {
            /* DLAB=1: write Divisor Latch High for baud rate */
            s->dlh = value;
        } else {
            /* DLAB=0: configure interrupt enables */
            s->ier = value;
            /* Trigger THR Empty interrupt check after IER update */
            s->iir = (s->iir & ~0x0F) | IIR_THR_EMPTY;
            csky_uart_update_irq(s);
        }
        break;

    case 0x02:  /* FCR (FIFO Control Register) - Write Only */
        /* Reset FIFO if FIFO enable bit changes */
        if ((s->fcr & FCR_FIFOE) != (value & FCR_FIFOE)) {
            s->rx_pos = 0;
            s->rx_count = 0;
        }
        s->fcr = value;
        csky_uart_update_fcr(s);
        break;

    case 0x03:  /* LCR (Line Control Register) */
        s->lcr = value;
        break;

    case 0x04:  /* MCR (Modem Control Register) */
        s->mcr = value;
        break;

    case 0x05:  /* LSR - Read Only, ignore writes */
    case 0x06:  /* MSR - Read Only, ignore writes */
    case 0x1F:  /* USR - Read Only, ignore writes */
    case (0xB4 / 4):  /* Reserved */
    case (0xC0 / 4):  /* Reserved */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_write: invalid offset 0x%x\n", (int)offset);
        break;
    }
}

/*
 * Check if UART can receive more data.
 *
 * Returns the number of bytes that can be accepted:
 * - FIFO mode: available space in RX FIFO (up to 16 bytes)
 * - Non-FIFO mode: 1 if RBR is empty, 0 otherwise
 */
static int csky_uart_can_receive(void *opaque)
{
    csky_uart_state *s = (csky_uart_state *)opaque;

    if (s->fcr & FCR_FIFOE) {
        /* FIFO mode: return available space */
        return FIFO_DEPTH - s->rx_count;
    } else {
        /* Non-FIFO mode: can only hold one character */
        return (s->rx_count == 0) ? 1 : 0;
    }
}


/*
 * Receive data from the character backend.
 *
 * Handles both FIFO and non-FIFO modes:
 * - Non-FIFO: stores single character in rx_fifo[0]
 * - FIFO: stores in circular buffer with wrap-around
 *
 * Updates LSR (Data Ready, Overrun Error), USR (FIFO status),
 * and triggers RX interrupt.
 */
static void csky_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    csky_uart_state *s = (csky_uart_state *)opaque;

    if (size < 1) {
        return;
    }

    /* Check for overrun: new data when FIFO is already full */
    if (s->usr & USR_RFF) {
        s->lsr |= LSR_OE;  /* Set overrun error in LSR */
    }

    if (!(s->fcr & FCR_FIFOE)) {
        /* Non-FIFO mode: single character buffer */
        s->rx_fifo[0] = *buf;
        s->rx_count = 1;
        s->usr |= (USR_RFF | USR_RFNE);  /* Buffer full and not empty */
        s->lsr |= LSR_DR;                 /* Data ready */
        s->iir = (s->iir & ~0x0F) | IIR_RX_DATA_AVAIL;
        csky_uart_update_irq(s);
        return;
    }

    /* FIFO mode: store in circular buffer */
    int write_pos = (s->rx_pos + s->rx_count) % FIFO_DEPTH;
    s->rx_fifo[write_pos] = *buf;
    s->rx_count++;

    /* Update status registers */
    s->lsr |= LSR_DR;       /* Data ready */
    s->usr |= USR_RFNE;     /* FIFO not empty */

    if (s->rx_count >= FIFO_DEPTH) {
        s->usr |= USR_RFF;  /* FIFO full */
    }

    /* Trigger RX interrupt */
    s->iir = (s->iir & ~0x0F) | IIR_RX_DATA_AVAIL;
    csky_uart_update_irq(s);
}

/*
 * Character device event handler.
 * Currently no special handling for break/hangup events.
 */
static void csky_uart_event(void *opaque, QEMUChrEvent event)
{
    /* No action required for character device events */
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
        VMSTATE_END_OF_LIST()
    }
};

static Property csky_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", csky_uart_state, chr),
    DEFINE_PROP_END_OF_LIST(),
};

/*
 * Initialize UART device instance.
 *
 * Sets up memory-mapped I/O region, IRQ lines, and default register values
 * according to DW_apb_uart reset state.
 */
static void csky_uart_init(Object *obj)
{
    csky_uart_state *s = CSKY_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* Initialize MMIO region (4KB address space) */
    memory_region_init_io(&s->iomem, OBJECT(s), &csky_uart_ops, s,
                          TYPE_CSKY_UART, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize IRQ outputs: standard IRQ and CLIC IRQ */
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->clic_irq);

    /*
     * Set default register values per DW_apb_uart reset state:
     * - rx_trigger = 1: trigger interrupt on single character
     * - dlh = 0x4: default divisor latch high value
     * - iir = 0x1: no interrupt pending (IIR_NO_INT)
     * - lsr = 0x60: THRE and TEMT set (transmitter empty)
     * - usr = 0x6: TFE and TFNF set (TX FIFO empty and not full)
     */
    s->rx_trigger = 1;
    s->dlh = 0x04;
    s->iir = IIR_NO_INT;
    s->lsr = LSR_THRE | LSR_TEMT;
    s->usr = USR_TFE | USR_TFNF;
}

/*
 * Realize (finalize) UART device.
 *
 * Connects character backend handlers for RX/TX operations.
 */
static void csky_uart_realize(DeviceState *dev, Error **errp)
{
    csky_uart_state *s = CSKY_UART(dev);

    /* Register character device handlers for receive operations */
    qemu_chr_fe_set_handlers(&s->chr,
                             csky_uart_can_receive,  /* can_receive callback */
                             csky_uart_receive,      /* receive callback */
                             csky_uart_event,        /* event callback */
                             NULL,                   /* be_change callback */
                             s,                      /* opaque data */
                             NULL,                   /* context */
                             true);                  /* set_open */
}

/*
 * Initialize UART device class.
 *
 * Registers device callbacks, VM state for migration, and properties.
 */
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

/*
 * Create and initialize a DW_apb_uart device instance.
 *
 * @addr: Base address for MMIO mapping
 * @irq: Standard interrupt line (can be NULL if clic_irq is provided)
 * @clic_irq: CLIC interrupt line (can be NULL if irq is provided)
 * @chr: Character device backend for serial I/O
 *
 * Returns: Pointer to the created DeviceState
 *
 * Note: At least one of irq or clic_irq must be non-NULL.
 */
DeviceState *csky_uart_create(hwaddr addr, qemu_irq irq, qemu_irq clic_irq,
                              Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *sbd;

    /* Create new UART device instance */
    dev = qdev_new(TYPE_CSKY_UART);
    sbd = SYS_BUS_DEVICE(dev);

    /* Configure character device backend */
    qdev_prop_set_chr(dev, "chardev", chr);

    /* Realize device and map MMIO region */
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, addr);

    /* Connect interrupt lines */
    if (irq) {
        sysbus_connect_irq(sbd, 0, irq);
    }
    if (clic_irq) {
        sysbus_connect_irq(sbd, 1, clic_irq);
    }

    /* Ensure at least one interrupt line is connected */
    g_assert(irq || clic_irq);

    return dev;
}
