/*
 * CSKY tcip v1 emulation.
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
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "system/trace.h"
#include "target/csky/cpu.h"
#include "hw/intc/csky_tcip_v1.h"
#include "hw/irq.h"
#include "migration/vmstate.h"

/* CoreTim */
#define CT_CSR_COUNTFLAG    (1 << 16)
#define CT_CSR_INTERNAL_CLK (1 << 2)
#define CT_CSR_TICKINT      (1 << 1)
#define CT_CSR_ENABLE       (1 << 0)

/* VIC */
#define VIC_ISR_VEC         0xff
#define VIC_ISR_INT         (1 << 10)
#define VIC_ISR_PEND_SHF    12
#define PR0                 0x300
#define PR8                 0x31c
#define PR31                0x37c
#define VIC_IPTR_EN         0x80000000
#define GET_IPTR_PRI(a)     ((a & 0xc0) >> 6)
#define GET_IRQ_PRI(s, a)   (s->vic_pr[a - 32])
#define GET_TSPR_PRI(s)     ((s->vic_tspr & 0xc0) >> 6)
#define IS_IRQ_ACTIVE(s, irq)   \
    ((s->vic_iabr & (1 << (irq - 32))) != 0)

#define IS_TSPEND_ACTIVE(s)     \
    ((s->vic_tsabr & 0x1) != 0)

/*
 * find the highest priority interrupt source
 * first poll priority_bitmap[0], then priority_bitmap[1], ...[2], ...[3]
 * count the number of trailing 0-bits starting from the least
 * significant bit position use builtin function ctz
 */
static uint32_t find_highest_priority_vec(csky_tcip_v1_state *s,
                                          uint32_t priority_bitmap[])
{
    uint32_t i;
    uint32_t int_vec = 0;
    for (i = 0; i < 4; i++) {
        if (priority_bitmap[i] == 0) {
            continue;
        }
        int_vec = __builtin_ctz(priority_bitmap[i]) + 32;
        break;
    }

    if ((s->vic_tspend & 0x1)
        && (int_vec == 0 || GET_TSPR_PRI(s) < GET_IRQ_PRI(s, int_vec))) {
        int_vec = 22;
    }
    return int_vec;
}

static int csky_nvic_get_pending(csky_tcip_v1_state *s)
{
    uint32_t int_req = 0;
    uint32_t int_best = 0;
    uint32_t priority_bitmap[4] = {0, 0, 0, 0};
    uint32_t i;

    int_req = s->vic_ispr & s->vic_iser;

    /* There is no irq, clear the interrupt_request. */
    if ((int_req == 0) && ((s->vic_tspend & 0x1) == 0)) {
        return 0;
    }

    /*
     * generate the bitmap for each priority level and each interrupt source.
     * priority_bitmap[i] stands for priority level i, 0<=i<=3
     * and each bit of priority_bitmap[i] stands for one interrupt source
     */
    while (int_req != 0) {
        i = __builtin_clz(int_req);
        priority_bitmap[s->vic_pr[31 - i]] |= 1 << (31 - i);
        int_req &= ~(1 << (31 - i));
    }

    int_best = find_highest_priority_vec(s, priority_bitmap);
    return int_best;
}
/*
 * Description:
 *     Update the interrupt flag according the vic state and
 *     give the flag to cpu.
 * Argument:
 *     s  --- the pointer to the vic state
 * Return:
 *     void
 */
static void csky_vic_v1_update(csky_tcip_v1_state *s)
{
    uint32_t int_req = 0;
    uint32_t int_best = 0;
    uint32_t int_active = 0;
    uint32_t pr_best = 0;
    uint32_t pr_active = 0;
    uint32_t priority_bitmap[4] = {0, 0, 0, 0};
    uint32_t i;
    uint32_t flag = 0;
    uint32_t iptr_en = s->vic_iptr & VIC_IPTR_EN;
    uint32_t iptr_pri = GET_IPTR_PRI(s->vic_iptr);

    s->vic_ispr |= s->vic_source;
    s->vic_ispr &= ~s->vic_iabr;        /* if irq is active, it will not pend */
    int_req = s->vic_ispr & s->vic_iser;
    int_active = s->vic_isr & VIC_ISR_VEC;

    if (int_active >= 32) {
        pr_active = GET_IRQ_PRI(s, int_active);
    } else if (int_active == 22) {
        pr_active = GET_TSPR_PRI(s);
    }

    /* There is no irq, clear the interrupt_request. */
    if ((int_req == 0) && ((s->vic_tspend & 0x1) == 0)) {
        qemu_set_irq(s->irq, 0);
        return;
    }

    /*
     * generate the bitmap for each priority level and each interrupt source.
     * priority_bitmap[i] stands for priority level i, 0<=i<=3
     * and each bit of priority_bitmap[i] stands for one interrupt source
     */
    while (int_req != 0) {
        i = __builtin_clz(int_req);
        priority_bitmap[s->vic_pr[31 - i]] |= 1 << (31 - i);
        int_req &= ~(1 << (31 - i));
    }

    int_best = find_highest_priority_vec(s, priority_bitmap);

    if (int_best >= 32) {
        pr_best = GET_IRQ_PRI(s, int_best);
    } else if (int_best == 22) {
        pr_best = GET_TSPR_PRI(s);
    } else {
        return;
    }

    /* If the irq are handling now, just pending it and update the isr. */
    if (((int_best >= 32) && IS_IRQ_ACTIVE(s, int_best))
        || ((int_best == 22) && IS_TSPEND_ACTIVE(s))) {
        s->vic_isr = int_active | (int_best << VIC_ISR_PEND_SHF);
        return;
    }

    /* Response or Pending the new irq. */
    if ((s->vic_iabr == 0 && s->vic_tsabr == 0)
        || ((pr_best < pr_active)
            && ((iptr_en == 0) || (pr_best < iptr_pri)))) {

        flag = int_best | VIC_ISR_INT;
        qemu_set_irq(s->irq, flag);
    } else {
        /* New irq, but can not nest the last irq. */
        s->vic_isr = (int_active | (int_best << VIC_ISR_PEND_SHF));
    }
    return;
}

/*
 * Description:
 *     Interrupt request from other devices to VIC.
 * Argument:
 *     opaque  ---  the pointer to VIC state.
 *     irq     ---  vector number of the interrupt request
 *     level   ---  set or clear the corresponding interrupt
 * Return:
 *     void
 */
static void csky_vic_v1_set_irq(void *opaque, int irq, int level)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    if (level) {
        s->vic_source |= 1 << irq;
    } else {
        s->vic_source &= ~(1 << irq);
    }
    csky_vic_v1_update(s);
}

/*
 * Description:
 *     Read the value of TCIP registers.
 * Argument:
 *     opaque  --  the pointer of the TCIP state.
 *     offset  --  the offset of the register which will be read.
 * Return:
 *     The value of the corresponding TCIP register.
 */
static uint64_t csky_tcip_v1_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x000: /*ISER*/
    case 0x080: /*ICER*/
        ret = s->vic_iser;
        break;
    case 0x040: /*IWER*/
    case 0x0c0: /*IWDR*/
        ret =  s->vic_iwer;
        break;
    case 0x140: /*ISSR*/
    case 0x1c0: /*ICSR*/
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                ret = s->vic_issr;
            } else {
                ret = 0;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_tcip_v1_read: Bad register offset 0x%x\n",
                          (int)offset);
        }
        break;
    case 0x100: /*ISPR*/
    case 0x180: /*ICPR*/
        ret = s->vic_ispr;
        break;
    case 0x200: /*IABR*/
        ret = s->vic_iabr;
        break;
    case PR0 ... PR8: /* PR[32], only support 32 irqs now, big endian */
        ret =  ((s->vic_pr[offset - PR0] << 6)
                | (s->vic_pr[offset - PR0 + 1] << 14)
                | (s->vic_pr[offset - PR0 + 2] << 22)
                | (s->vic_pr[offset - PR0 + 3] << 30));
        break;
    case 0xb00: /* VIC_ISR */
        ret = s->vic_isr;
        break;
    case 0xb04: /* VIC_IPTR */
        ret = s->vic_iptr;
        break;
    case 0xb08: /*TSPEND*/
        ret = s->vic_tspend;
        break;
    case 0xb0c: /*TSABR*/
        ret = s->vic_tsabr;
        break;
    case 0xb10: /*TSPR*/
        ret = s->vic_tspr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_read: Bad register offset 0x%x\n",
                      (int)offset);
    }

    return ret;
}

/*
 * Description:
 *     Write the value to TCIP registers.
 * Argument:
 *     opaque  --  the pointer of the TCIP state.
 *     offset  --  the offset of the register.
 *     value   --  the value which will be writen.
 * Return:
 *     Void
 */
static void csky_tcip_v1_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_write: 0x%x must word align write\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x000: /* ISER */
        s->vic_iser |= value;
        break;
    case 0x040: /* IWER */
        s->vic_iwer |= value;
        break;
    case 0x080: /* ICER */
        s->vic_iser &= ~value;
        break;
    case 0x0c0: /* IWDR */
        s->vic_iwer &= ~value;
        break;
    case 0x100: /* ISPR */
        s->vic_ispr |= value;
        break;
    case 0x140: /* ISSR */
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                s->vic_issr |= value;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_tcip_v1_write: Bad register offset 0x%x\n",
                          (int)offset);
        }
        break;
    case 0x180: /* ICPR */
        s->vic_ispr &= ~value;
        break;
    case 0x1c0: /* ICSR */
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                s->vic_issr &= ~value;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_tcip_v1_write: Bad register offset 0x%x\n",
                          (int)offset);
        }
        break;
    case 0x200: /* IABR */
        /*
         * Useguide says "if write 0 to iabr, it will clear the active state,
         * but write 1 to iabr, it maybe cause an unpredictable error",
         * so in qemu when write to iabr, it will clear iabr anyway.
         */
        s->vic_iabr = 0;
        break;
    case PR0 ... PR8: /* PR[32], only support 32 irqs now, big endian */
        s->vic_pr[offset - PR0] = (value >> 6) & 0x3;
        s->vic_pr[offset - PR0 + 1] = (value >> 14) & 0x3;
        s->vic_pr[offset - PR0 + 2] = (value >> 22) & 0x3;
        s->vic_pr[offset - PR0 + 3] = (value >> 30) & 0x3;
        break;
    case 0xb00: /* ISR */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempt to write a read-only register ISR!\n");
        break;
    case 0xb04: /* IPTR */
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                s->vic_iptr = value;
            }
        } else {
            s->vic_iptr = value;
        }
        break;
    case 0xb08: /* TSPEND */
        s->vic_tspend = value;
        break;
    case 0xb0c: /* TSABR */
        s->vic_tsabr = 0;
        break;
    case 0xb10: /* TSPR */
        s->vic_tspr = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_write: Bad register offset 0x%x\n",
                      (int)offset);
        return;
    }
    csky_vic_v1_update(s);
}

static const MemoryRegionOps csky_tcip_v1_ops = {
    .read = csky_tcip_v1_read,
    .write = csky_tcip_v1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void csky_tcip_v1_reset(DeviceState *d)
{
    csky_tcip_v1_state *s = THEAD_TCIP_V1(d);

    s->vic_iser = 0;
    s->vic_iwer = 0;
    s->vic_source = 0;
    s->vic_ispr = 0;
    s->vic_tspend = 0;
    s->vic_tspr = 0;
    s->vic_iabr = 0;
    s->vic_tsabr = 0;
    s->vic_isr = 0;
    s->vic_iptr = 0;
    s->vic_issr = 0;
    csky_vic_v1_update(s);
}

/*
 * Description:
 *     Interrupt handler: generate signals to cpu.
 * Argument:
 *     opaque  --  the pointer to CPU state.
 *     irq     --  vector number of interrupt.
 *     level   --  contains some important information to help
 *                 handle the interrupt.
 * Return:
 *     void
 */
static void csky_vic_v1_cpu_handler(void *opaque, int irq, int level)
{
    CPUCSKYState *env = (CPUCSKYState *)opaque;
    CPUState *cs = CPU(env_cpu(env));

    env->intc_signals.vec_b = level & VIC_ISR_VEC;
    env->intc_signals.avec_b = 0;
    env->intc_signals.fint_b = 0;
    env->intc_signals.int_b = (level & VIC_ISR_INT) >> 10;

    if (level & VIC_ISR_INT) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

/*
 * Description:
 *     Allocate irq for VIC and register the handler.
 * Argument:
 *     env  --  the pointer to CPU state.
 * Return:
 *     the pointer to the allocated irq
 */
qemu_irq *csky_vic_v1_init_cpu(CPUCSKYState *env)
{
    return qemu_allocate_irqs(csky_vic_v1_cpu_handler, env, 1);
}

/**
 * csky_nvic_update_irq: update irq if has irq pending.
 * @opaque: the NVIC
 */
void csky_nvic_update_irq(void *opaque)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;

    csky_vic_v1_update(s);
}

/**
 * csky_nvic_secure_irq: return if irq is from secured world or not.
 * @opaque: the NVIC
 */
bool csky_nvic_secure_irq(void *opaque, uint32_t irq)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;

    return (s->vic_issr & (1 << (irq - 32))) != 0;
}

/**
 * csky_nvic_acknowledge_irq: make highest priority pending exception active
 * @opaque: the NVIC
 */
void csky_nvic_acknowledge_irq(void *opaque, uint32_t irq)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    int pending;

    if (irq >= 32) {
        s->vic_isr = irq;
        s->vic_ispr &= ~(1 << (irq - 32));
        s->vic_iabr |= 1 << (irq - 32);
    } else if (irq == 22) {
        s->vic_isr = irq;
        s->vic_tspend = 0;
        s->vic_tsabr = 1;
    }

    pending = csky_nvic_get_pending(s);

    s->vic_isr |= (pending << VIC_ISR_PEND_SHF);
}

/**
 * csky_nvic_complete_irq: complete specified interrupt or exception
 * @opaque: the NVIC
 */
void csky_nvic_complete_irq(void *opaque)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    uint32_t irq = s->vic_isr & 0xff;

    /* if irq >= 32, it is a vic interrupt. */
    if (irq >= 32) {
        s->vic_isr &= ~0xff;
        s->vic_isr |= ((s->env->cp0.psr >> 16) & 0xff);
        s->vic_iabr &= ~(1 << (irq - 32));
        /*
         * if the finished irq is same as VIC_IPTR(threshold),
         * clean the en bit of VIC_IPTR.
         */
        if (irq == ((s->vic_iptr && 0xff00) >> 8)) {
            s->vic_iptr &= ~(1 << 31);
        }
    } else if (irq == 22) {
        s->vic_isr &= ~0xff;
        s->vic_isr |= ((s->env->cp0.psr >> 16) & 0xff);
        s->vic_tsabr = 0;
        /*
         * if the finished irq is same as VIC_IPTR(threshold),
         * clean the en bit of VIC_IPTR.
         */
        if (irq == ((s->vic_iptr && 0xff00) >> 8)) {
            s->vic_iptr &= ~(1 << 31);
        }
    }
}

/*
 * Description:
 *     Initial TCIP.
 * Argument:
 *     obj  --  the pointer to csky_tcip_v1_info.
 * Return:
 *     success or failure
 */
static void csky_tcip_v1_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    csky_tcip_v1_state *s = THEAD_TCIP_V1(obj);
    CSKYCPU *cpu = CSKY_CPU(qemu_get_cpu(0));
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    if (cpu == NULL) {
        return;
    }

    s->env = &cpu->env;
    cpu->env.nvic = s;
    s->vic_iser = 0;
    s->vic_iwer = 0;
    s->vic_source = 0;
    s->vic_ispr = 0;
    s->vic_tspend = 0;
    s->vic_tspr = 0;
    s->vic_iabr = 0;
    s->vic_tsabr = 0;
    s->vic_isr = 0;
    s->vic_iptr = 0;
    s->vic_issr = 0;

    /* CSKY VIC intialization */
    qdev_init_gpio_in(dev, csky_vic_v1_set_irq, 32);
    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, obj, &csky_tcip_v1_ops,
                          s, TYPE_THEAD_TCIP_V1, 0xe00);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_tcip_v1 = {
    .name = TYPE_THEAD_TCIP_V1,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(timer, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_source, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_iser, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_iwer, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_ispr, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_iabr, csky_tcip_v1_state),
        VMSTATE_UINT32_ARRAY(vic_pr, csky_tcip_v1_state, 32),
        VMSTATE_UINT32(vic_issr, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_isr, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_iptr, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_tspend, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_tsabr, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_tspr, csky_tcip_v1_state),
        VMSTATE_END_OF_LIST()
    }
};

static void csky_tcip_v1_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);

    dc->vmsd  = &vmstate_tcip_v1;
    dc->reset = csky_tcip_v1_reset;
    dc->desc = "cskysim type: INTC";
    dc->user_creatable = true;
}

static const TypeInfo csky_tcip_v1_info = {
    .name          = TYPE_THEAD_TCIP_V1,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = csky_tcip_v1_init,
    .instance_size = sizeof(csky_tcip_v1_state),
    .class_init    = csky_tcip_v1_class_init,
};

static void csky_tcip_v1_register_types(void)
{
    type_register_static(&csky_tcip_v1_info);
}

type_init(csky_tcip_v1_register_types)
