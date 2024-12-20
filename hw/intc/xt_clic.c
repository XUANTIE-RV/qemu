/*
 * RISC-V CLIC(Core Local Interrupt Controller) for QEMU.
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
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "sysemu/qtest.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/intc/xt_clic.h"
#include "gdbstub/internals.h"

static int xt_clic_get_hartid(void)
{
    if (!current_cpu) {
        return gdbserver_state.g_cpu->cpu_index;
    }
    return current_cpu->cpu_index;
}

/*
 * The 2-bit trig WARL field specifies the trigger type and polarity for each
 * interrupt input. Bit 1, trig[0], is defined as "edge-triggered"
 * (0: level-triggered, 1: edge-triggered); while bit 2, trig[1], is defined as
 * "negative-edge" (0: positive-edge, 1: negative-edge). (Section 3.6)
 */

static inline TRIG_TYPE
xt_clic_get_trigger_type(XTCLICState *clic, size_t irq_offset)
{
    return (clic->clicintattr[irq_offset] >> 1) & 0x3;
}

static inline bool
xt_clic_is_edge_triggered(XTCLICState *clic, size_t irq_offset)
{
    return (clic->clicintattr[irq_offset] >> 1) & 0x1;
}

static inline bool
xt_clic_is_shv_interrupt(XTCLICState *clic, size_t irq_offset)
{
    return (clic->clicintattr[irq_offset] & 0x1) && clic->nvbits;
}

static uint8_t
xt_clic_get_interrupt_level(XTCLICState *clic, int hartid, uint8_t intctl)
{
    int nlbits = clic->nlbits[hartid];

    uint8_t mask_il = ((1 << nlbits) - 1) << (8 - nlbits);
    uint8_t mask_padding = (1 << (8 - nlbits)) - 1;
    /* unused level bits are set to 1 */
    return (intctl & mask_il) | mask_padding;
}

static uint8_t
xt_clic_get_interrupt_priority(XTCLICState *clic, int hartid, uint8_t intctl)
{
    int npbits = clic->clicintctlbits - clic->nlbits[hartid];
    uint8_t mask_priority = ((1 << npbits) - 1) << (8 - npbits);
    uint8_t mask_padding = (1 << (8 - npbits)) - 1;

    if (npbits < 0) {
        return UINT8_MAX;
    }
    /* unused priority bits are set to 1 */
    return (intctl & mask_priority) | mask_padding;
}

static void
xt_clic_intcfg_decode(XTCLICState *clic, int hartid, uint16_t intcfg,
                      uint8_t *mode,  uint8_t *level, uint8_t *priority)
{
    *mode = intcfg >> 8;
    *level = xt_clic_get_interrupt_level(clic, hartid, intcfg & 0xff);
    *priority = xt_clic_get_interrupt_priority(clic, hartid, intcfg & 0xff);
}

static void xt_clic_next_interrupt(void *opaque, int hartid)
{
    /*
     * Scan active list for highest priority pending interrupts
     * comparing against this harts mintstatus register and interrupt
     * the core if we have a higher priority interrupt to deliver
     */
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(hartid));
    CPURISCVState *env = &cpu->env;
    XTCLICState *clic = (XTCLICState *)opaque;

    int il = MAX(get_field(env->mintstatus, MINTSTATUS_MIL), clic->mintthresh[hartid]);

    /* Get sorted list of enabled interrupts for this hart */
    size_t hart_offset = hartid * clic->num_sources;
    CLICActiveInterrupt *active = &clic->active_list[hart_offset];
    size_t active_count = clic->active_count[hartid];
    uint8_t mode, level, priority;

    /* Loop through the enabled interrupts sorted by mode+priority+level */
    while (active_count) {
        size_t irq_offset;
        xt_clic_intcfg_decode(clic, hartid, active->intcfg, &mode, &level,
                              &priority);
        if (level <= il) {
            /*
             * No pending interrupts with high enough mode+priority+level
             * break and clear pending interrupt for this hart
             */
            break;
        }
        irq_offset = active->irq + hartid * clic->num_sources;
        /* Check pending interrupt with high enough mode+priority+level */
        if (clic->clicintip[irq_offset]) {
            /* Post pending interrupt for this hart */
            env->exccode = active->irq | PRV_M << 12 | level << 14;
            cpu_interrupt(CPU(cpu), CPU_INTERRUPT_CLIC);
            return;
        }
        /* Check next enabled interrupt */
        active_count--;
        active++;
    }
}

/*
 * For level-triggered interrupts, software writes to pending bits are
 * ignored completely. (Section 3.4)
 */
static bool
xt_clic_validate_intip(XTCLICState *clic, int irq)
{
    return xt_clic_is_edge_triggered(clic, irq + clic->num_sources *
                                                 xt_clic_get_hartid());
}

static void
xt_clic_update_intip(XTCLICState *clic, int hartid, int irq, uint64_t value)
{
    size_t irq_offset = irq + clic->num_sources * hartid;
    clic->clicintip[irq_offset] = !!value;
    xt_clic_next_interrupt(clic, hartid);
}

static inline int xt_clic_encode_priority(const CLICActiveInterrupt *i)
{
    return ((i->intcfg & 0x3ff) << 12) | /* Highest mode+level+priority */
           (i->irq & 0xfff);             /* Highest irq number */
}

static int xt_clic_active_compare(const void *a, const void *b)
{
    return xt_clic_encode_priority(b) - xt_clic_encode_priority(a);
}

static void xt_clic_enable_irq(XTCLICState *clic, int irq)
{
    int hartid = xt_clic_get_hartid();
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = irq + hart_offset;
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];

    active_list[*active_count].intcfg = (PRV_M << 8) |
                                        clic->clicintctl[irq_offset];
    active_list[*active_count].irq = irq;
    (*active_count)++;

    /* Sort list of active interrupts */
    qsort(active_list, *active_count,
          sizeof(CLICActiveInterrupt),
          xt_clic_active_compare);
}

/* Notice this irq must be enabled before call this function */
static void xt_clic_disable_irq(XTCLICState *clic, int irq)
{
    int hartid = xt_clic_get_hartid();
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = irq + hart_offset;
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];

    CLICActiveInterrupt key = {
        (PRV_M << 8) | clic->clicintctl[irq_offset], irq
    };
    CLICActiveInterrupt *result = bsearch(&key,
                                          active_list, *active_count,
                                          sizeof(CLICActiveInterrupt),
                                          xt_clic_active_compare);
    assert(result);
    size_t elem = (result - active_list);
    size_t sz = (--(*active_count) - elem) * sizeof(CLICActiveInterrupt);
    memmove(&result[0], &result[1], sz);

    /* Sort list of active interrupts */
    qsort(active_list, *active_count,
          sizeof(CLICActiveInterrupt),
          xt_clic_active_compare);
}

static void xt_clic_update_intctl(XTCLICState *clic,
                                  int irq, uint64_t new_intctl)
{
    int hartid = xt_clic_get_hartid();
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = irq + hart_offset;
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];

    CLICActiveInterrupt key = {
        (PRV_M << 8) | clic->clicintctl[irq_offset], irq
    };
    CLICActiveInterrupt *result = bsearch(&key,
                                          active_list, *active_count,
                                          sizeof(CLICActiveInterrupt),
                                          xt_clic_active_compare);

    if (result) {
        result->intcfg = (PRV_M << 8) | new_intctl;
        qsort(active_list, *active_count,
              sizeof(CLICActiveInterrupt),
              xt_clic_active_compare);
    }
    clic->clicintctl[irq_offset] = new_intctl;
    xt_clic_next_interrupt(clic, hartid);
}

static void
xt_clic_update_intie(XTCLICState *clic, int irq, uint64_t new_intie)
{
    int hartid = xt_clic_get_hartid();
    size_t irq_offset = irq + clic->num_sources * hartid;


    uint8_t old_intie = clic->clicintie[irq_offset];
    clic->clicintie[irq_offset] = !!new_intie;

    /* Add to or remove from list of active interrupts */
    if (new_intie && !old_intie) {
        xt_clic_enable_irq(clic, irq);
    } else if (!new_intie && old_intie) {
        xt_clic_disable_irq(clic, irq);
    }

    xt_clic_next_interrupt(clic, hartid);
}

static void
xt_clic_hart_write(XTCLICState *clic, hwaddr addr,
                   uint64_t value, unsigned size)
{
    int req = extract32(addr, 0, 2);
    size_t irq = addr / 4;
    int hartid = xt_clic_get_hartid();
    size_t irq_offset = irq + clic->num_sources * hartid;

    if (irq >= clic->num_sources) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid irq %lu: 0x%" HWADDR_PRIx "\n", irq, addr);
        return;
    }
    switch (req) {
    case 0: /* clicintip[i] */
        if (xt_clic_validate_intip(clic, irq)) {
            /*
             * The actual pending bit is located at bit 0 (i.e., the
             * leastsignificant bit). In case future extensions expand the bit
             * field, from FW perspective clicintip[i]=zero means no interrupt
             * pending, and clicintip[i]!=0 (not just 1) indicates an
             * interrupt is pending. (Section 3.4)
             */
            if (value != clic->clicintip[irq]) {
                xt_clic_update_intip(clic, hartid, irq, value);
            }
        }
        break;
    case 1: /* clicintie[i] */
        if (clic->clicintie[irq_offset] != value) {
            xt_clic_update_intie(clic, irq, value);
        }
        break;
    case 2: /* clicintattr[i] */
        if (clic->clicintattr[irq_offset] != value) {
            /* When nmbits=2, check WARL */
            bool invalid = (clic->nmbits[hartid] == 2) &&
                           (extract64(value, 6, 2) == 0b10);
            if (invalid) {
                uint8_t old_mode = extract32(clic->clicintattr[irq_offset],
                                             6, 2);
                value = deposit32(value, 6, 2, old_mode);
            }
            clic->clicintattr[irq_offset] = deposit32(value, 6, 2, 0b11);
            xt_clic_next_interrupt(clic, hartid);
        }
        break;
    case 3: /* clicintctl[i] */
        if (value != clic->clicintctl[irq_offset]) {
            xt_clic_update_intctl(clic, irq, value);
        }
        break;
    }
}

static uint64_t
xt_clic_hart_read(XTCLICState *clic, hwaddr addr)
{
    int req = extract32(addr, 0, 2);
    size_t irq = addr / 4;
    size_t irq_offset = irq + clic->num_sources *
                              xt_clic_get_hartid();

    if (irq >= clic->num_sources) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid irq %lu: 0x%" HWADDR_PRIx "\n", irq, addr);
        return 0;
    }
    switch (req) {
    case 0: /* clicintip[i] */
        return clic->clicintip[irq_offset];
    case 1: /* clicintie[i] */
        return clic->clicintie[irq_offset];
    case 2: /* clicintattr[i] */
        /*
         * clicintattr register layout
         * Bits Field
         * 7:6 mode
         * 5:3 reserved (WPRI 0)
         * 2:1 trig
         * 0 shv
         */
        return clic->clicintattr[irq_offset] & ~0x38;
    case 3: /* clicintctrl */
        /*
         * The implemented bits are kept left-justified in the most-significant
         * bits of each 8-bit clicintctl[i] register, with the lower
         * unimplemented bits treated as hardwired to 1.(Section 3.7)
         */
        return clic->clicintctl[irq_offset] |
               ((1 << (8 - clic->clicintctlbits)) - 1);
    }

    return 0;
}

static void
xt_clic_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    XTCLICState *clic = opaque;
    int hartid = xt_clic_get_hartid();

    if (addr < 0x1000) {
        int index = addr / 4;
        if (index > 3) {
            return;
        }
        uint64_t wr_mask = MAKE_64BIT_MASK((addr - index * 4) * 8,
                                           size * 8);
        switch (index) {
        case 0: /* cliccfg */
            {
                uint8_t nlbits = extract32(value, 1, 4);
                uint8_t nmbits = extract32(value, 5, 2);

                /*
                 * The 4-bit cliccfg.nlbits WARL field.
                 * Valid values are 0—8.
                 */
                if (nlbits <= 8) {
                    clic->nlbits[hartid] = nlbits;
                }

                if (nmbits == 0) {
                    clic->nmbits[hartid] = 0;
                }
                break;
            }
        case 1: /* clicinfo, read-only register */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "clic: write read-only clicinfo.\n");
            break;
        case 2: /* mintthresh */
            clic->mintthresh[hartid] = (clic->mintthresh[hartid] & ~wr_mask) |
                                       (value & wr_mask);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "clic: invalid write addr: 0x%" HWADDR_PRIx "\n",
                          addr);
            return;
        }
    } else {
        addr -= 0x1000;
        xt_clic_hart_write(clic, addr, value, size);
    }
}

static uint64_t xt_clic_read(void *opaque, hwaddr addr, unsigned size)
{
    XTCLICState *clic = opaque;
    int hartid = xt_clic_get_hartid();
    uint64_t tmp = 0;

    if (addr < 0x1000) {
        int index = addr / 4;
        switch (index) {
        case 0: /* cliccfg */
            if (addr == 0x0) {
                return clic->nvbits |
                       (clic->nlbits[hartid] << 1) |
                       (clic->nmbits[hartid] << 5);
            } else {
                return 0;
            }
        case 1: /* clicinfo */
            /*
             * clicinfo register layout
             *
             * Bits Field
             * 31-25 reserved (WARL 0)
             * 24:21 CLICINTCTLBITS
             * 20:13 version (for version control)
             * 12:0 num_interrupt
             */
            tmp = ((clic->clicintctlbits << 21) | clic->num_sources) &
                  MAKE_64BIT_MASK(0, 25);
            return extract64(tmp, 8 * (addr - 0x4), 8 * size);
        case 2: /* mintthresh */
            return extract64(clic->mintthresh[hartid], 8 * (addr - 0x8),
                             size * 8);
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "clic: invalid read : 0x%" HWADDR_PRIx "\n",
                          addr);
            break;
        }
    } else {
        addr -= 0x1000;
        uint64_t value = 0;
        while (size) {
            size -= 1;
            value |= xt_clic_hart_read(clic, addr + size) << (size * 8);
        }
        return value;
    }
    return 0;
}

static void xt_clic_set_irq(void *opaque, int irq, int level)
{
    XTCLICState *clic = opaque;
    TRIG_TYPE type;
    int hartid;
    size_t irq_offset;

    hartid = irq / clic->num_sources;
    irq = irq % clic->num_sources;
    irq_offset = irq + clic->num_sources * hartid;
    type = xt_clic_get_trigger_type(clic, irq_offset);

    /*
     * In general, the edge-triggered interrupt state should be kept in pending
     * bit, while the level-triggered interrupt should be kept in the level
     * state of the incoming wire.
     *
     * For CLIC, model the level-triggered interrupt by read-only pending bit.
     */
    if (level) {
        switch (type) {
        case POSITIVE_LEVEL:
        case POSITIVE_EDGE:
            xt_clic_update_intip(clic, hartid, irq, level);
            break;
        case NEG_LEVEL:
            xt_clic_update_intip(clic, hartid, irq, !level);
            break;
        case NEG_EDGE:
            break;
        }
    } else {
        switch (type) {
        case POSITIVE_LEVEL:
            xt_clic_update_intip(clic, hartid, irq, level);
            break;
        case POSITIVE_EDGE:
            break;
        case NEG_LEVEL:
        case NEG_EDGE:
            xt_clic_update_intip(clic, hartid, irq, !level);
            break;
        }
    }
}

static const MemoryRegionOps xt_clic_ops = {
    .read = xt_clic_read,
    .write = xt_clic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void xt_clic_realize(DeviceState *dev, Error **errp)
{
    XTCLICState *clic = XT_CLIC(dev);
    int irqs = clic->num_harts * clic->num_sources;
    clic->clic_size = clic->num_harts * (clic->num_sources * 4 + 0x1000);
    memory_region_init_io(&clic->mmio, OBJECT(dev), &xt_clic_ops, clic,
                          TYPE_XT_CLIC, clic->clic_size);


    clic->nmbits = g_new0(uint8_t, clic->num_harts);
    clic->nlbits = g_new0(uint8_t, clic->num_harts);
    clic->clicinfo = g_new0(uint32_t, clic->num_harts);

    clic->clicintip = g_new0(uint8_t, irqs);
    clic->clicintie = g_new0(uint8_t, irqs);
    clic->clicintattr = g_new0(uint8_t, irqs);
    clic->clicintctl = g_new0(uint8_t, irqs);

    clic->mintthresh = g_new0(uint32_t, clic->num_harts);
    clic->active_list = g_new0(CLICActiveInterrupt, irqs);
    clic->active_count = g_new0(size_t, clic->num_harts);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &clic->mmio);

    /* Allocate irq through gpio, so that we can use qtest */
    qdev_init_gpio_in(dev, xt_clic_set_irq, irqs);
    for (int i = 0; i < clic->num_harts; i++) {
        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(i));
        cpu->env.clic = clic;
        cpu->env.mclicbase = clic->mclicbase;
    }
}

static Property xt_clic_properties[] = {
    DEFINE_PROP_BOOL("vector", XTCLICState, nvbits, false),
    DEFINE_PROP_UINT32("num-sources", XTCLICState, num_sources, 0),
    DEFINE_PROP_UINT32("num-harts", XTCLICState, num_harts, 0),
    DEFINE_PROP_UINT32("clicintctlbits", XTCLICState, clicintctlbits, 0),
    DEFINE_PROP_UINT64("mclicbase", XTCLICState, mclicbase, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void xt_clic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);

    dc->realize = xt_clic_realize;
    device_class_set_props(dc, xt_clic_properties);
    dc->desc = "cskysim type: INTC";
    dc->user_creatable = true;
}

static const TypeInfo xt_clic_info = {
    .name          = TYPE_XT_CLIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XTCLICState),
    .class_init    = xt_clic_class_init,
};

static void xt_clic_register_types(void)
{
    type_register_static(&xt_clic_info);
}

type_init(xt_clic_register_types)

/*
 * xt_clic_create:
 *
 * @addr: base address of M-Mode CLIC memory-mapped registers
 * @vector: the selective interrupt hardware vectoring is implemented or not
 * @num_sources: number of interrupts supporting by each aperture
 * @clicintctlbits: bits are actually implemented in the clicintctl registers
 *
 * Returns: the device object
 */
DeviceState *xt_clic_create(hwaddr addr, bool vector, uint32_t num_harts,
                            uint32_t num_sources,
                            uint8_t clicintctlbits)
{
    DeviceState *dev = qdev_new(TYPE_XT_CLIC);

    assert(num_sources <= 4096);
    assert(clicintctlbits <= 8);

    qdev_prop_set_bit(dev, "vector", vector);
    qdev_prop_set_uint32(dev, "num-sources", num_sources);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "clicintctlbits", clicintctlbits);
    qdev_prop_set_uint64(dev, "mclicbase", addr);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}

void xt_clic_get_next_interrupt(void *opaque)
{
    XTCLICState *clic = opaque;
    xt_clic_next_interrupt(clic, current_cpu->cpu_index);
}

bool xt_clic_shv_interrupt(void *opaque, int irq)
{
    XTCLICState *clic = opaque;
    size_t irq_offset = irq + clic->num_sources *
                              xt_clic_get_hartid();
    return xt_clic_is_shv_interrupt(clic, irq_offset);
}

bool xt_clic_edge_triggered(void *opaque, int irq)
{
    XTCLICState *clic = opaque;
    size_t irq_offset = irq + clic->num_sources *
                              xt_clic_get_hartid();
    return xt_clic_is_edge_triggered(clic, irq_offset);
}

void xt_clic_clean_pending(void *opaque, int irq)
{
    XTCLICState *clic = opaque;
    size_t irq_offset = irq + clic->num_sources *
                              xt_clic_get_hartid();
    clic->clicintip[irq_offset] = 0;
}

/*
 * The new CLIC interrupt-handling mode is encoded as a new state in
 * the existing WARL xtvec register, where the low two bits of  are 11.
 */
bool xt_clic_is_clic_mode(CPURISCVState *env)
{
    target_ulong xtvec = (env->priv == PRV_M) ? env->mtvec : env->stvec;
    return env->clic && ((xtvec & 0x3) == 3);
}

void xt_clic_decode_exccode(uint32_t exccode, int *mode,
                            int *il, int *irq)
{
    *irq = extract32(exccode, 0, 12);
    *mode = extract32(exccode, 12, 2);
    *il = extract32(exccode, 14, 8);
}
