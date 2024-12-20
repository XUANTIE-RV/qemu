/*
 * CSKY mic controller
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
#include "hw/irq.h"
#include "qemu/log.h"
#include "system/trace.h"
#include "cpu.h"
#include "hw/intc/csky_mic.h"
#include "migration/vmstate.h"


#define THEAD_CIRQ_INDEX(irq) ((irq - 32) / 32)
#define THEAD_CIRQ_MASK(irq) (1 << (irq % 32))

#define MAX_SOFT_IRQ 15
#define MAX_PRIVATE_IRQ 31
#define MAX_PUBLIC_IRQ 223
#define THEAD_PUBLIC_IRQ_NUM (MAX_PUBLIC_IRQ - MAX_PRIVATE_IRQ)
#define THEAD_CPU_CORES 8

typedef struct csky_mic_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion cpuiomem[THEAD_CPU_CORES];
    qemu_irq parent_irq[THEAD_CPU_CORES];
    qemu_irq parent_fiq[THEAD_CPU_CORES];
/* general registers */
    uint32_t cictlr;
    uint32_t cisetr;
    uint32_t cidr;
    uint32_t cityper[THEAD_PUBLIC_IRQ_NUM / 32];
    uint32_t cicfgr[THEAD_PUBLIC_IRQ_NUM / 16];
    uint32_t ciprtr[THEAD_PUBLIC_IRQ_NUM / 4];
    uint32_t cier[THEAD_PUBLIC_IRQ_NUM / 32];
    uint32_t ciar[THEAD_PUBLIC_IRQ_NUM / 32];
    uint32_t cipr[THEAD_PUBLIC_IRQ_NUM / 32];
    uint32_t cidestr[THEAD_PUBLIC_IRQ_NUM];
/* lieutenant registers */
    uint32_t psictlr[THEAD_CPU_CORES];
    uint32_t pisetr[THEAD_CPU_CORES];
    uint32_t psityper[THEAD_CPU_CORES];
    uint32_t sicfgr[THEAD_CPU_CORES];
    uint32_t picfgr[THEAD_CPU_CORES];
    uint32_t siprtr[THEAD_CPU_CORES][4];
    uint32_t piprtr[THEAD_CPU_CORES][4];
    uint32_t psier[THEAD_CPU_CORES];
    uint32_t psipr[THEAD_CPU_CORES];
    uint32_t psiar[THEAD_CPU_CORES];
    uint32_t ipmr[THEAD_CPU_CORES];
    uint32_t hppir[THEAD_CPU_CORES];
    uint32_t ireadyr[THEAD_CPU_CORES];
    uint32_t iapr[THEAD_CPU_CORES][32];
    uint32_t hrpr[THEAD_CPU_CORES];
} csky_mic_state;


static const VMStateDescription vmstate_csky_mic = {
    .name = TYPE_THEAD_MIC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cictlr, csky_mic_state),
        VMSTATE_UINT32(cisetr, csky_mic_state),
        VMSTATE_UINT32(cidr, csky_mic_state),
        VMSTATE_UINT32_ARRAY(cityper, csky_mic_state, THEAD_PUBLIC_IRQ_NUM / 32),
        VMSTATE_UINT32_ARRAY(cicfgr, csky_mic_state, THEAD_PUBLIC_IRQ_NUM / 16),
        VMSTATE_UINT32_ARRAY(ciprtr, csky_mic_state, THEAD_PUBLIC_IRQ_NUM / 4),
        VMSTATE_UINT32_ARRAY(cier, csky_mic_state, THEAD_PUBLIC_IRQ_NUM / 32),
        VMSTATE_UINT32_ARRAY(ciar, csky_mic_state, THEAD_PUBLIC_IRQ_NUM / 32),
        VMSTATE_UINT32_ARRAY(cipr, csky_mic_state, THEAD_PUBLIC_IRQ_NUM / 32),
        VMSTATE_UINT32_ARRAY(cidestr, csky_mic_state, THEAD_PUBLIC_IRQ_NUM),
        VMSTATE_UINT32_ARRAY(psictlr, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(pisetr, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(psityper, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(sicfgr, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(picfgr, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_2DARRAY(siprtr, csky_mic_state, THEAD_CPU_CORES, 4),
        VMSTATE_UINT32_2DARRAY(piprtr, csky_mic_state, THEAD_CPU_CORES, 4),
        VMSTATE_UINT32_ARRAY(psier, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(psiar, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(psipr, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(hppir, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(ipmr, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(hrpr, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_ARRAY(ireadyr, csky_mic_state, THEAD_CPU_CORES),
        VMSTATE_UINT32_2DARRAY(iapr, csky_mic_state, THEAD_CPU_CORES, 32),
        VMSTATE_END_OF_LIST()
    }
};

static uint32_t csky_mic_write_cacr(csky_mic_state *s, int value, int cpu,
                                    MemTxAttrs attrs)
{
    int i, j;
    assert(s->ireadyr[cpu] == value);
    s->ireadyr[cpu] = -1;
    qemu_set_irq(s->parent_irq[cpu], 0);
    for (i = 0; i < 6; i++) {
        if (s->cipr[i] != 0) {
            j = __builtin_ctz(s->cipr[i]);
            if (s->ireadyr[0] == -1) {
                s->ireadyr[0] = 32 + j;
                qemu_set_irq(s->parent_irq[0], 1);
                return 0;
            } else {
                break;
            }
        }
    }
    if (s->psipr[cpu] != 0) {
        j = __builtin_ctz(s->psipr[cpu]);
        s->ireadyr[cpu] = j;
        qemu_set_irq(s->parent_irq[cpu], 1);
    }
    return 0;
}

static uint32_t csky_mic_read_ireadyr(csky_mic_state *s, int cpu,
                                      MemTxAttrs attrs)
{
    uint32_t iready;
    iready = s->ireadyr[cpu];
    if (iready == 15) {
        s->psipr[cpu] &= ~(1 << 15);
    }
    return iready;
}

enum THEADIrqType {
    PRIVATE_IRQ = 1,
    SOFT_IRQ,
    PUBLIC_IRQ,
    UNSUPPORT_IRQ
};

static enum THEADIrqType csky_mic_get_irqtype(int irq)
{
    enum THEADIrqType type;

    if (irq <= MAX_PRIVATE_IRQ) {
        type = PRIVATE_IRQ;
    } else if (irq <= MAX_SOFT_IRQ) {
        type = SOFT_IRQ;
    } else if (irq <= MAX_PUBLIC_IRQ) {
        type = PUBLIC_IRQ;
    } else {
        type = UNSUPPORT_IRQ;
    }
    return type;
}

static int csky_mic_irq_wrap(int irq)
{
    if (irq > MAX_PUBLIC_IRQ) {
        irq = (irq - MAX_PUBLIC_IRQ - 1) % 16 + 16;
    }
    return irq;
}

/* fixme: get target cpu for public irq */
static int csky_mic_get_cpu(int irq)
{
    int target_cpu = 0;
    if (irq > MAX_PUBLIC_IRQ) {
        target_cpu = ((irq - MAX_PUBLIC_IRQ - 1) / 16) + 1;
    }
    return target_cpu;
}

static bool csky_mic_irq_enable_test(void *opaque, int irq, int cpu)
{
    enum THEADIrqType type;
    bool enable = false;

    csky_mic_state *s = (csky_mic_state *)opaque;
    irq = csky_mic_irq_wrap(irq);
    type = csky_mic_get_irqtype(irq);

    switch (type) {
    case PRIVATE_IRQ:
    case SOFT_IRQ:
        enable = (s->psier[cpu] & (1 << irq)) ? true : false;
        break;
    case PUBLIC_IRQ:
        enable = (s->cier[THEAD_CIRQ_INDEX(irq)] & THEAD_CIRQ_MASK(irq)) ?
            true : false;
        break;
    default:
        break;
    }
    return enable;
}

static bool csky_mic_is_busy(void *opaque, int cpu)
{
    bool busy = false;
    csky_mic_state *s = (csky_mic_state *)opaque;
    busy = (s->ireadyr[cpu] == -1) ? false : true;
    return busy;
}

static void csky_mic_set_irq(void *opaque, int irq, int level)
{
    csky_mic_state *s = (csky_mic_state *)opaque;
    int target_cpu = 0;
    enum THEADIrqType type;

    bool enable, busy;
    target_cpu = csky_mic_get_cpu(irq);
    enable = csky_mic_irq_enable_test(s, irq, target_cpu);
    if (enable == false) {
        return;
    }

    irq = csky_mic_irq_wrap(irq);
    type = csky_mic_get_irqtype(irq);
    busy = csky_mic_is_busy(s, target_cpu);
    switch (type) {
    case PRIVATE_IRQ:
    case SOFT_IRQ:
        if (level) {
            s->psipr[target_cpu] |= (1 << irq);
            if (!busy) {
                s->ireadyr[target_cpu] = irq;
                qemu_set_irq(s->parent_irq[target_cpu], 1);
            }
        } else {
            s->psipr[target_cpu] &= ~(1 << irq);
        }
        break;
    case PUBLIC_IRQ:
        if (level) {
            s->cipr[THEAD_CIRQ_INDEX(irq)] |= THEAD_CIRQ_MASK(irq);
            if (!busy) {
                s->ireadyr[target_cpu] = irq;
                qemu_set_irq(s->parent_irq[target_cpu], 1);
            }
        } else {
                s->cipr[THEAD_CIRQ_INDEX(irq)] &= ~THEAD_CIRQ_MASK(irq);
        }
        break;
    default:
        break;
    }
}
static MemTxResult csky_mic_dist_write(void *opaque, hwaddr offset,
    uint64_t value, unsigned size, MemTxAttrs attrs)
{
    csky_mic_state *s = (csky_mic_state *)opaque;
    if ((offset & 0x3) || (size != 4)) {
        return MEMTX_ERROR;
    }

    switch (offset) {
    case 0x00:
        s->cictlr = value;
        break;
    case 0x80 ... 0x94:
        s->cityper[(offset - 0x80) / 4] = value;
        break;
    case 0x100 ... 0x12c:
        s->cicfgr[(offset - 0x100) / 4] = value;
        break;
    case 0x200 ... 0x2bc:
        s->ciprtr[(offset - 0x200) / 4] = value;
        break;
    case 0x600 ... 0x614:
        s->cier[(offset - 0x600) / 4] |= value;
        break;
    case 0x680 ... 0x694:
        s->cier[(offset - 0x680) / 4] &= ~value;
        break;
    case 0x700 ... 0x714:
        s->cipr[(offset - 0x700) / 4] |= value;
        break;
    case 0x780 ... 0x794:
        s->cipr[(offset - 0x780) / 4] &= ~value;
        break;
    case 0x800 ... 0x814:
        s->ciar[(offset - 0x800) / 4] |= value;
        break;
    case 0x880 ... 0x894:
        s->ciar[(offset - 0x880) / 4] &= ~value;
        break;
    case 0x1000 ... 0x12fc:
        s->cidestr[(offset - 0x1000) / 4] = value;
        break;
    default:
        return MEMTX_ERROR;
    }

    return MEMTX_OK;
}
static MemTxResult csky_mic_dist_read(void *opaque, hwaddr offset,
    uint64_t *value, unsigned size, MemTxAttrs attrs)
{
    csky_mic_state *s = (csky_mic_state *)opaque;
    if ((offset & 0x3) || (size != 4)) {
        return MEMTX_ERROR;
    }

    switch (offset) {
    case 0x00:
        *value = s->cictlr;
        break;
    case 0x08:
        *value = s->cisetr;
        break;
    case 0x10:
        *value = s->cidr;
        break;
    case 0x80 ... 0x94:
        *value = s->cityper[(offset - 0x80) / 4];
        break;
    case 0x100 ... 0x12c:
        *value = s->cicfgr[(offset - 0x100) / 4];
        break;
    case 0x200 ... 0x2bc:
        *value = s->ciprtr[(offset - 0x200) / 4];
        break;
    case 0x600 ... 0x614:
        *value = s->cier[(offset - 0x600) / 4];
        break;
    case 0x680 ... 0x694:
        *value = s->cier[(offset - 0x680) / 4];
        break;
    case 0x700 ... 0x714:
        *value = s->cipr[(offset - 0x700) / 4];
        break;
    case 0x780 ... 0x794:
        *value = s->cipr[(offset - 0x780) / 4];
        break;
    case 0x800 ... 0x814:
        *value = s->ciar[(offset - 0x800) / 4];
        break;
    case 0x880 ... 0x894:
        *value = s->cipr[(offset - 0x880) / 4];
        break;
    case 0x1000 ... 0x12fc:
        *value = s->cidestr[(offset - 0x1000) / 4];
        break;
    default:
        return MEMTX_ERROR;
    }
    return MEMTX_OK;
}

static MemTxResult csky_mic_cpu_write(csky_mic_state *s,
    int cpu, int offset, int32_t value, MemTxAttrs attrs)
{
    uint32_t irq, i;
    uint8_t cpuid;
    if (offset & 0x3) {
        return MEMTX_ERROR;
    }

    switch (offset) {
    case 0x00:
        s->psictlr[cpu] = value;
        break;
    case 0x10:
        s->psityper[cpu] = value;
        break;
    case 0x14:
        s->sicfgr[cpu] = value;
        break;
    case 0x18:
        s->picfgr[cpu] = value;
        break;
    case 0x20 ... 0x2c:
        s->siprtr[cpu][(offset -  0x20) / 4] = value;
        break;
    case 0x30 ... 0x3c:
        s->piprtr[cpu][(offset -  0x30) / 4] = value;
        break;
    case 0x40:
        s->psier[cpu] |= value;
        break;
    case 0x44:
        s->psier[cpu] &= ~value;
        break;
    case 0x48: /* psispr */
        s->psipr[cpu] |= value;
        break;
    case 0x4c: /* psicpr */
        s->psipr[cpu] &= ~value;
        break;
    case 0x50:
        s->psiar[cpu] |= value;
        break;
    case 0x54:
        s->psiar[cpu] &= ~value;
        break;
    case 0x60: /* sigr */
        irq = value & 0xf;
        cpuid = (value >> 8) & 0xff;
        for (i = 0; i < 8; i++) {
            if ((cpuid >> i) & 0x1) {
                s->psipr[i] |= (1 << 15);
                if (s->ireadyr[i] == -1) {
                    s->ireadyr[i] = 15;
                    qemu_set_irq(s->parent_irq[i], 1);
                }
            }
        }
        break;
    case 0x68:
        s->hppir[cpu] = value;
        break;
    case 0xa0:/* senr */
        irq = value & 0xff;
        if (irq <= MAX_PRIVATE_IRQ) {
            s->psier[cpu] |= (1 << irq);
        } else if (irq <= MAX_PUBLIC_IRQ) {
            s->cier[THEAD_CIRQ_INDEX(irq)] |= THEAD_CIRQ_MASK(irq);
        } else {
            return MEMTX_ERROR;
        }
        break;
    case 0xa4: /* cenr */
        irq = value & 0xff;
        if (irq <= MAX_PRIVATE_IRQ) {
            s->psier[cpu] &= ~(1 << irq);
        } else if (irq <= MAX_PUBLIC_IRQ) {
            s->cier[THEAD_CIRQ_INDEX(irq)] &= ~THEAD_CIRQ_MASK(irq);
        } else {
            return MEMTX_ERROR;
        }
        break;
    case 0xa8: /* spdr */
    case 0xac: /* cpdr */
    case 0xb0: /* sacr */
        return MEMTX_ERROR;
    case 0xb4:
        csky_mic_write_cacr(s, value, cpu, attrs);
        break;
    default:
        return MEMTX_ERROR;
    }
    return MEMTX_OK;
}

static MemTxResult csky_mic_cpu_read(csky_mic_state *s,
    int cpu, int offset, uint64_t *value, MemTxAttrs attrs)
{
    if (offset & 0x3) {
        return MEMTX_ERROR;
    }

    switch (offset) {
    case 0x00:
        *value = s->psictlr[cpu];
        break;
    case 0x08:
        *value = s->pisetr[cpu];
        break;
    case 0x10:
        *value = s->psityper[cpu];
        break;
    case 0x14:
        *value = s->sicfgr[cpu];
        break;
    case 0x18:
        *value = s->picfgr[cpu];
        break;
    case 0x20 ... 0x2c:
        *value = s->siprtr[cpu][(offset - 0x20) / 4];
        break;
    case 0x30 ... 0x3c:
        *value = s->piprtr[cpu][(offset - 0x30) / 4];
        break;
    case 0x40:
    case 0x44:
        *value = s->psier[cpu];
        break;
    case 0x48:
    case 0x4c:
        *value = s->psipr[cpu];
        break;
    case 0x50:
    case 0x54:
        *value = s->psiar[cpu];
        break;
    case 0x64:
        *value = s->ipmr[cpu];
        break;
    case 0x68:
        *value = s->hppir[cpu];
        break;
    case 0x6c:
        *value = csky_mic_read_ireadyr(s, cpu, attrs);
        break;
    case 0x70 ... 0x8c:
        *value = s->iapr[cpu][(offset - 0x70) / 4];
        break;
    case 0x90:
        *value = s->hrpr[cpu];
        break;
    default:
        return MEMTX_ERROR;
    }
    return MEMTX_OK;
}

static MemTxResult csky_mic_thiscpu_write(void *opaque, hwaddr addr,
    uint64_t value, unsigned size, MemTxAttrs attrs)
{
    csky_mic_state *s = (csky_mic_state *)opaque;
    return csky_mic_cpu_write(s, current_cpu->cpu_index,
                addr, value, attrs);
}
static MemTxResult csky_mic_thiscpu_read(void *opaque, hwaddr addr,
    uint64_t *value, unsigned size, MemTxAttrs attrs)
{
    csky_mic_state *s = (csky_mic_state *)opaque;
    return csky_mic_cpu_read(s, current_cpu->cpu_index,
                addr, value, attrs);
}

static const MemoryRegionOps csky_mic_ops[2] = {
    {
        .read_with_attrs = csky_mic_dist_read,
        .write_with_attrs = csky_mic_dist_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
    },
    {
        .read_with_attrs = csky_mic_thiscpu_read,
        .write_with_attrs = csky_mic_thiscpu_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
    }
};
static void csky_mic_cpu_handler(void *opaque, int irq, int level)
{
    DeviceState *dev = DEVICE(opaque);
    CPUCSKYState *env = &(CSKY_CPU(dev)->env);
    CPUState *cs = &(CSKY_CPU(dev)->parent_obj);

    env->intc_signals.avec_b = 1;
    if (level == 0) {
        env->intc_signals.fint_b = 0;
        env->intc_signals.int_b = 0;
        env->intc_signals.avec_b = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        if (irq) { /* register firq = 1 for every core */
            env->intc_signals.fint_b = 1;
            env->intc_signals.int_b = 0;
        } else {
            env->intc_signals.fint_b = 0;
            env->intc_signals.int_b = 1;
        }
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

void csky_mic_init_cpu(CPUCSKYState *env)
{
    DeviceState *dev = DEVICE(env_cpu(env));
    qdev_init_gpio_in(dev, csky_mic_cpu_handler, 2);
}

static void csky_mic_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    csky_mic_state *s = THEAD_MIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    char name[64] = {0};
    int i;

    memory_region_init_io(&s->iomem, obj, csky_mic_ops,
                          s, "mic_dist", 0x8000);
    sysbus_init_mmio(sbd, &s->iomem);

    /*
     * fixme: every core can see regs belong to other cores,
     * but can't read/write them in the right way. This will
     * replaced by a coherent method with hardware.
     */
    for (i = 0; i < THEAD_CPU_CORES; i++) {
        sprintf(name, "mic_cpu_%d", i);
        memory_region_init_io(&s->cpuiomem[i], obj, &csky_mic_ops[1],
                          s, name, 0x1000);
        memset(name, 0, sizeof(char) * 64);
        sysbus_init_mmio(sbd, &s->cpuiomem[i]);
    }

    qdev_init_gpio_in(dev, csky_mic_set_irq,
        THEAD_PUBLIC_IRQ_NUM  + THEAD_CPU_CORES * 32);

    /* register firq as the first irq */
    for (i = 0; i < THEAD_CPU_CORES; i++) {
        sysbus_init_irq(sbd, &s->parent_irq[i]);
    }
    for (i = 0; i < THEAD_CPU_CORES; i++) {
        sysbus_init_irq(sbd, &s->parent_fiq[i]);
    }
    /* use -1 as an idle flag */
    for (i = 0; i < THEAD_CPU_CORES; i++) {
        s->ireadyr[i] = -1;
    }
}

static void csky_mic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->vmsd = &vmstate_csky_mic;
}

static const TypeInfo csky_mic_info = {
    .name          = TYPE_THEAD_MIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_mic_state),
    .instance_init = csky_mic_init,
    .class_init    = csky_mic_class_init,
};

static void csky_register_types(void)
{
    type_register_static(&csky_mic_info);
}

type_init(csky_register_types)
