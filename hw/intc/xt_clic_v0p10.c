/*
 * RISC-V CLIC(Core Local Interrupt Controller v0.10) for QEMU.
 *
 * Copyright (c) 2025 Alibaba Group. All rights reserved.
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
#include "hw/intc/xt_clic_v0p10.h"
#include "gdbstub/internals.h"

static bool xt_clic_reserved_irq(int irq)
{
    if (irq >= 16) {
        return false;
    }
    switch (irq) {
    case IRQ_S_SOFT:
    case IRQ_M_SOFT:
    case IRQ_S_TIMER:
    case IRQ_M_TIMER:
    case IRQ_S_EXT:
    case IRQ_M_EXT:
    case IRQ_PMU_OVF:
        return false;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "clic: reserved irq %d", irq);
        return true;
    }
}
/*
 * The 2-bit trig WARL field specifies the trigger type and polarity for each
 * interrupt input. Bit 1, trig[0], is defined as "edge-triggered"
 * (0: level-triggered, 1: edge-triggered); while bit 2, trig[1], is defined as
 * "negative-edge" (0: positive-edge, 1: negative-edge). (Section 3.4)
 */

static inline TRIG_TYPE
xt_clic_v0p10_get_trigger_type(XTCLICV0P10State *clic, size_t irq_offset)
{
    return (clic->clicintattr[irq_offset] >> 1) & 0x3;
}

static inline bool
xt_clic_v0p10_is_edge_triggered(XTCLICV0P10State *clic, size_t irq_offset)
{
    return (clic->clicintattr[irq_offset] >> 1) & 0x1;
}

static inline bool
xt_clic_v0p10_is_shv_interrupt(XTCLICV0P10State *clic, size_t irq_offset)
{
    return clic->nvbits && (clic->clicintattr[irq_offset] & 0x1);
}

static uint8_t
xt_clic_v0p10_il(XTCLICV0P10State *clic, int hartid,
                 uint8_t intctl, uint8_t mode)
{
    uint8_t  nlbits = (mode == PRV_M) ? clic->mnlbits[hartid] :
                                        clic->snlbits[hartid];
    if (nlbits > clic->clicintctlbits) {
        nlbits = clic->clicintctlbits;
    }
    return intctl | ((1 << (8 - nlbits)) - 1);
}

static uint8_t
xt_clic_v0p10_ip(XTCLICV0P10State *clic, int hartid,
                 int8_t intctl, uint8_t mode)
{
    uint8_t  nlbits = (mode == PRV_M) ? clic->mnlbits[hartid] :
                                        clic->snlbits[hartid];
    int npbits = clic->clicintctlbits - nlbits;
    uint8_t mask_padding = (1 << (8 - npbits)) - 1;
    uint8_t mask_priority = ((1 << nlbits) - 1) << (8 - nlbits);
    if (npbits < 0) {
        return UINT8_MAX;
    }
    /* unused priority bits are set to 1 */
    return intctl | mask_priority  | mask_padding;
}

static void
xt_clic_v0p10_intcfg_decode(XTCLICV0P10State *clic, int hartid, uint16_t intcfg,
                            uint8_t *mode,  uint8_t *level, uint8_t *priority)
{
    *mode = intcfg >> 8;
    *level = xt_clic_v0p10_il(clic, hartid, intcfg & 0xff, *mode);
    *priority = xt_clic_v0p10_ip(clic, hartid, intcfg & 0xff, *mode);
}

/*
 * Find the 'suitable' pending interrupt to implement Tail-Chaining.
 * Returns encode if an interrupt is found, otherwise 0.
 *
 * The suitable means:
 * 1. Must be a software vectored interrupt.
 * 2. Must be a horizontal interrupt.
 * 3. Must have a level greater than the saved interrupt level
 *    (held in mcause.mpil).
 *
 * Note: xcause.mpil is for Tail-Chaining, while mintstatus.m/sil is
 * for Interrupt Preemption.
 */
target_ulong xt_clic_v0p10_find_suitable_interrupt(CPURISCVState *env,
                                                    uint8_t write_mode)
{
    /* Get sorted list of enabled interrupts for this hart */
    XTCLICV0P10State *clic = env->xt_clic_v0p10;
    CPUState *cs = env_cpu(env);
    int hartid = cs->cpu_index;
    size_t hart_offset = hartid * clic->num_sources;
    CLICActiveInterrupt *active = &clic->active_list[hart_offset];
    size_t active_count = clic->active_count[hartid];
    uint8_t mode, level, priority;

    /* Loop through the enabled interrupts sorted by mode + level + priority */
    while (active_count) {
        size_t irq_offset;
        irq_offset = active->irq + hartid * clic->num_sources;
        xt_clic_v0p10_intcfg_decode(clic, hartid, active->intcfg, &mode, &level,
                                    &priority);
        if (write_mode != mode) {
            goto find_next;
        }
        /* Check pending interrupt */
        if (clic->clicintip[irq_offset]) {
            int il = 0;
            if (write_mode == PRV_M) {
                il = MAX(get_field(env->mcause, MCAUSE_MPIL),
                         get_field(env->mintthresh, MINTTHRESH_MTH_V0P10));

            } else {
                il = MAX(get_field(env->scause, MCAUSE_MPIL),
                         get_field(env->sintthresh, MINTTHRESH_MTH_V0P10));
            }

            if (level <= il || xt_clic_v0p10_shv_interrupt(clic, hartid, active->irq)) {
                /*
                * No pending interrupts is suitable
                */
                goto find_next;
            }
            return active->irq | mode << 12 | level << 14;
        }
        /* Check next enabled interrupt */
find_next:
        active_count--;
        active++;
    }
    return 0;
}

void xt_clic_v0p10_next_interrupt(void *opaque, int hartid)
{
    /*
     * Scan active list for highest priority pending interrupts
     * comparing against this harts mintstatus register and interrupt
     * the core if we have a higher priority interrupt to deliver
     */
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(hartid));
    CPURISCVState *env = &cpu->env;
    XTCLICV0P10State *clic = (XTCLICV0P10State *)opaque;

    /* Get sorted list of enabled interrupts for this hart */
    size_t hart_offset = hartid * clic->num_sources;
    CLICActiveInterrupt *active = &clic->active_list[hart_offset];
    size_t active_count = clic->active_count[hartid];
    uint8_t mode, level, priority;

    /* Loop through the enabled interrupts sorted by mode + level + priority */
    while (active_count) {
        size_t irq_offset;
        xt_clic_v0p10_intcfg_decode(clic, hartid, active->intcfg, &mode, &level,
                                    &priority);
        int il = 0;
        if (mode == PRV_M) {
            il = MAX(get_field(env->mintstatus, MINTSTATUS_MIL),
                    get_field(env->mintthresh, MINTTHRESH_MTH_V0P10));
        } else if (mode == PRV_S){
            il = MAX(get_field(env->mintstatus, MINTSTATUS_SIL),
                    get_field(env->sintthresh, MINTTHRESH_MTH_V0P10));
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                              "CLIC: error privileg mode %d in interrupt!\n", mode);
            break;
        }

        if (level <= il) {
            /*
             * No pending interrupts with high enough mode + level + priority
             * break and clear pending interrupt for this hart
             */
            break;
        }
        irq_offset = active->irq + hartid * clic->num_sources;
        /* Check pending interrupt with high enough mode + level + priority */
        if (clic->clicintip[irq_offset]) {
            /* Post pending interrupt for this hart */
            env->exccode = active->irq | mode << 12 | level << 14;
            cpu_interrupt(CPU(cpu), CPU_INTERRUPT_CLIC);
            return;
        }
        /* Check next enabled interrupt */
        active_count--;
        active++;
    }
    cpu_reset_interrupt(CPU(cpu), CPU_INTERRUPT_CLIC);
}

static inline int xt_clic_v0p10_encode_priority(const CLICActiveInterrupt *i)
{
    return ((i->intcfg & 0x3ff) << 12) | /* Highest mode+level+priority */
           (i->irq & 0xfff);             /* Highest irq number */
}

static int xt_clic_v0p10_active_compare(const void *a, const void *b)
{
    return xt_clic_v0p10_encode_priority(b) - xt_clic_v0p10_encode_priority(a);
}

static void xt_clic_v0p10_enable_irq(XTCLICV0P10State *clic, int hartid, int irq)
{
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = irq + hart_offset;
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];
    uint8_t mode = xt_clic_v0p10_get_intpriv(clic, hartid, irq);

    active_list[*active_count].intcfg = (mode << 8) |
                                        clic->clicintctl[irq_offset];
    active_list[*active_count].irq = irq;
    (*active_count)++;

    /* Sort list of active interrupts */
    qsort(active_list, *active_count,
          sizeof(CLICActiveInterrupt),
          xt_clic_v0p10_active_compare);
}

/* Notice this irq must be enabled before call this function */
static void xt_clic_v0p10_disable_irq(XTCLICV0P10State *clic, int hartid ,int irq)
{
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = irq + hart_offset;
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];
    uint8_t mode = xt_clic_v0p10_get_intpriv(clic, hartid, irq);

    CLICActiveInterrupt key = {
        (mode << 8) | clic->clicintctl[irq_offset], irq
    };
    CLICActiveInterrupt *result = bsearch(&key,
                                          active_list, *active_count,
                                          sizeof(CLICActiveInterrupt),
                                          xt_clic_v0p10_active_compare);
    assert(result);
    size_t elem = (result - active_list);
    size_t sz = (--(*active_count) - elem) * sizeof(CLICActiveInterrupt);
    memmove(&result[0], &result[1], sz);

    /* Sort list of active interrupts */
    qsort(active_list, *active_count,
          sizeof(CLICActiveInterrupt),
          xt_clic_v0p10_active_compare);
}

void xt_clic_v0p10_update_intctl(void *opaque, int hartid,
                                 int irq, uint8_t new_intctl)
{
    XTCLICV0P10State *clic = (XTCLICV0P10State *)opaque;
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = irq + hart_offset;
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];
    uint8_t mode = xt_clic_v0p10_get_intpriv(clic, hartid, irq);
    if (xt_clic_reserved_irq(irq)) {
        return;
    }

    CLICActiveInterrupt key = {
        (mode << 8) | clic->clicintctl[irq_offset], irq
    };
    CLICActiveInterrupt *result = bsearch(&key,
                                          active_list, *active_count,
                                          sizeof(CLICActiveInterrupt),
                                          xt_clic_v0p10_active_compare);

    if (result) {
        result->intcfg = (mode << 8) | new_intctl;
        qsort(active_list, *active_count,
              sizeof(CLICActiveInterrupt),
              xt_clic_v0p10_active_compare);
    }
    clic->clicintctl[irq_offset] = new_intctl;
    xt_clic_v0p10_next_interrupt(clic, hartid);
}

void xt_clic_v0p10_set_irq(void *opaque, int irq, int level)
{
    XTCLICV0P10State *clic = opaque;
    TRIG_TYPE type;
    int hartid;
    size_t irq_offset;

    hartid = irq / clic->num_sources;
    irq = irq % clic->num_sources;
    irq_offset = irq + clic->num_sources * hartid;
    type = xt_clic_v0p10_get_trigger_type(clic, irq_offset);

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
            xt_clic_v0p10_update_intip(clic, hartid, irq, level);
            break;
        case NEG_LEVEL:
            xt_clic_v0p10_update_intip(clic, hartid, irq, !level);
            break;
        case NEG_EDGE:
            break;
        }
    } else {
        switch (type) {
        case POSITIVE_LEVEL:
            xt_clic_v0p10_update_intip(clic, hartid, irq, level);
            break;
        case POSITIVE_EDGE:
            break;
        case NEG_LEVEL:
        case NEG_EDGE:
            xt_clic_v0p10_update_intip(clic, hartid, irq, !level);
            break;
        }
    }
}

static void xt_clic_v0p10_realize(DeviceState *dev, Error **errp)
{
    XTCLICV0P10State *clic = XT_CLIC_V0P10(dev);
    int irqs = clic->num_harts * clic->num_sources;

    clic->nmbits = g_new0(uint8_t, clic->num_harts);
    clic->mnlbits = g_new0(uint8_t, clic->num_harts);
    clic->snlbits = g_new0(uint8_t, clic->num_harts);

    clic->clicintip = g_new0(uint8_t, irqs);
    clic->clicintie = g_new0(uint8_t, irqs);
    clic->clicintattr = g_new0(uint8_t, irqs);
    clic->clicintctl = g_new0(uint8_t, irqs);

    clic->active_list = g_new0(CLICActiveInterrupt, irqs);
    clic->active_count = g_new0(size_t, clic->num_harts);

    /* Allocate irq through gpio, so that we can use qtest */
    qdev_init_gpio_in(dev, xt_clic_v0p10_set_irq, irqs);
    for (int i = 0; i < clic->num_harts; i++) {
        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(i));
        cpu->env.xt_clic_v0p10 = clic;
    }
}

static Property xt_clic_v0p10_properties[] = {
    DEFINE_PROP_BOOL("vector", XTCLICV0P10State, nvbits, false),
    DEFINE_PROP_UINT32("num-sources", XTCLICV0P10State, num_sources, 0),
    DEFINE_PROP_UINT32("num-harts", XTCLICV0P10State, num_harts, 0),
    DEFINE_PROP_UINT32("clicintctlbits", XTCLICV0P10State, clicintctlbits, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void xt_clic_v0p10_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);

    dc->realize = xt_clic_v0p10_realize;
    device_class_set_props(dc, xt_clic_v0p10_properties);
    dc->desc = "cskysim type: INTC";
    dc->user_creatable = true;
}

static const TypeInfo xt_clic_v0p10_info = {
    .name          = TYPE_XT_CLIC_V0P10,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XTCLICV0P10State),
    .class_init    = xt_clic_v0p10_class_init,
};

static void xt_clic_v0p10_register_types(void)
{
    type_register_static(&xt_clic_v0p10_info);
}

type_init(xt_clic_v0p10_register_types)

/*
 * xt_clic_create:
 *
 * @vector: the selective interrupt hardware vectoring is implemented or not
 * @num_sources: number of interrupts supporting by each aperture
 * @clicintctlbits: bits are actually implemented in the clicintctl registers
 *
 * Returns: the device object
 */
DeviceState *xt_clic_v0p10_create(bool vector, uint32_t num_harts,
                                  uint32_t num_sources,
                                  uint8_t clicintctlbits)
{
    DeviceState *dev = qdev_new(TYPE_XT_CLIC_V0P10);

    assert(num_sources <= 4096);
    assert(clicintctlbits <= 8);

    qdev_prop_set_bit(dev, "vector", vector);
    qdev_prop_set_uint32(dev, "num-sources", num_sources);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "clicintctlbits", clicintctlbits);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    return dev;
}

void xt_clic_v0p10_get_next_interrupt(void *opaque)
{
    XTCLICV0P10State *clic = opaque;
    if (current_cpu) {
        xt_clic_v0p10_next_interrupt(clic, current_cpu->cpu_index);
    }
}

bool xt_clic_v0p10_shv_interrupt(void *opaque, int hartid, int irq)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;
    return xt_clic_v0p10_is_shv_interrupt(clic, irq_offset);
}

bool xt_clic_v0p10_edge_triggered(void *opaque, int hartid, int irq)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;
    return xt_clic_v0p10_is_edge_triggered(clic, irq_offset);
}

void xt_clic_v0p10_clean_pending(void *opaque, int hartid, int irq)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;
    clic->clicintip[irq_offset] = 0;
    xt_clic_v0p10_next_interrupt(clic, hartid);
}

/*
 * The new CLIC interrupt-handling mode is encoded as a new state in
 * the existing WARL xtvec register, where the low two bits of  are 11.
 */
bool xt_clic_v0p10_is_clic_mode(CPURISCVState *env)
{
    /*
     * CLIC mode is a global setting: when mtvec.mode == 0b11 and
     * submode == 0000, all privilege levels run in CLIC mode.
     * Always check mtvec regardless of current privilege level.
     */
    return env->xt_clic_v0p10 && ((env->mtvec & 0x3f) == 0x3);
}

void xt_clic_v0p10_decode_exccode(uint32_t exccode, int *mode,
                                  int *il, int *irq)
{
    *irq = extract32(exccode, 0, 12);
    *mode = extract32(exccode, 12, 2);
    *il = extract32(exccode, 14, 8);
}

uint8_t xt_clic_v0p10_get_intctl(void *opaque, int hartid, int irq)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;
    /*
     * The implemented bits are kept left-justified in the most-significant
     * bits of each 8-bit clicintctl[i] register, with the lower
     * unimplemented bits treated as hardwired to 1.(Section 3.7)
     */
    return clic->clicintctl[irq_offset] |
            ((1 << (8 - clic->clicintctlbits)) - 1);
}

uint8_t xt_clic_v0p10_get_intip(void *opaque, int hartid, int irq)
{
   XTCLICV0P10State *clic = opaque;
   size_t irq_offset = irq + clic->num_sources * hartid;
   return clic->clicintip[irq_offset];
}

void xt_clic_v0p10_update_mcliccfg(void *opaque, int hartid, uint32_t val)
{
    XTCLICV0P10State *clic = opaque;

    size_t hart_offset = hartid * clic->num_sources;
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];

    uint8_t mnlbits = extract32(val, 0, 4);
    uint8_t nmbits = extract32(val, 4, 2);
    uint8_t snlbits = extract32(val, 16, 4);
    /*
     * The 4-bit cliccfg.nlbits WARL field.
     * Valid values are 0—8.
     */
    if (mnlbits <= 8) {
        clic->mnlbits[hartid] = mnlbits;
    }
    if (snlbits <= 8) {
        clic->snlbits[hartid] = snlbits;
    }
    if (nmbits <= 1) {
        clic->nmbits[hartid] = nmbits;
    }
    for (int i = 0; i < *active_count; i++) {
        uint16_t irq = active_list[i].irq;
        uint8_t mode = xt_clic_v0p10_get_intpriv(clic, hartid, irq);
        size_t irq_offset = irq + clic->num_sources * hartid;
        active_list[i].intcfg = (mode << 8) | clic->clicintctl[irq_offset];
    }
    qsort(active_list, *active_count,
          sizeof(CLICActiveInterrupt),
          xt_clic_v0p10_active_compare);
    xt_clic_v0p10_next_interrupt(clic, hartid);
}

void xt_clic_v0p10_update_scliccfg(void *opaque, int hartid, uint32_t val)
{
    XTCLICV0P10State *clic = opaque;

    uint8_t snlbits = extract32(val, 16, 4);
    /*
     * The 4-bit cliccfg.nlbits WARL field.
     * Valid values are 0—8.
     */
    if (snlbits <= 8) {
        clic->snlbits[hartid] = snlbits;
    }
}

uint32_t xt_clic_v0p10_get_mcliccfg(void *opaque, int hartid)
{
    XTCLICV0P10State *clic = opaque;
    return clic->mnlbits[hartid] | (clic->nmbits[hartid] << 4) |
           (clic->snlbits[hartid] << 16);
}

uint32_t xt_clic_v0p10_get_scliccfg(void *opaque, int hartid)
{
    XTCLICV0P10State *clic = opaque;
    return clic->snlbits[hartid] << 16;
}

/*
 * Interrupt Mode Table
 * priv-modes nmbits clicintattr[i].mode Interpretation
 * M          0         xx               M-mode interrupt
 * M/S        1         0x               S-mode interrupt
 * M/S        1         1x               M-mode interrupt
 *            2         xx               Reserved
 *            3         xx               Reserved
 */
uint8_t xt_clic_v0p10_get_intpriv(void *opaque, int hartid, int irq)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;

    switch (clic->nmbits[hartid]) {
    case 0:
        return PRV_M;
    case 1:
        return clic->clicintattr[irq_offset] & 0x80 ? (PRV_M) : (PRV_S);
    case 2:
    case 3:
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "clic: nmbits can only be 0 or 1 for M/S/U hart");
        g_assert_not_reached();
    }
}

/*
 *  CLICINTCTLBITS mnlbits clicintctl[i] interrupt levels
 *  0                2     ........      255
 *  1                2     L.......      127,255
 *  2                2     LL......      63,127,191,255
 *  3                3     LLL.....      31,63,95,127,159,191,223,255
 *  4                1     LPPP....      127,255
 *  "." bits are non-existent bits for level encoding, assumed to be 1
 *  "L" bits are available variable bits in level specification
 *  "P" bits are available variable bits in priority specification
 */
uint8_t xt_clic_v0p10_get_intil(void *opaque, int hartid, int irq)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;
    uint8_t mode = xt_clic_v0p10_get_intpriv(clic, hartid, irq);
    return xt_clic_v0p10_il(clic, hartid, clic->clicintctl[irq_offset], mode);
}

uint8_t xt_clic_v0p10_get_intattr(void *opaque, int hartid, int irq)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;
    return clic->clicintattr[irq_offset];
}

/*
 * Clicintattr is an 8-bit WARL read-write register to specify
 * various attributes for each interrupt.
 * bits:   7-6     5-3       2-1        0
 *         mode    WPRI(0)   trig       WARL(0)
 *
 * The smclicshv extends the clicintattr, but trigshv is not used actually.
 * bits:   9-8     7-6       5-3        2-1     0
 *         mode    trigshv   WPRI(0)    trig    shv
 *
 * To support smclicshv, we need to ensure the bit 0 can be write
 * when nvbits is 1 which means the smclicshv is enabled.
 */
void xt_clic_v0p10_update_intattr(void *opaque, int hartid, int irq,
                                  uint8_t val)
{
    XTCLICV0P10State *clic = opaque;
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = irq + clic->num_sources * hartid;
    if (xt_clic_reserved_irq(irq)) {
        return;
    }
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];
    uint8_t mode = xt_clic_v0p10_get_intpriv(clic, hartid, irq);
    CLICActiveInterrupt key = {
        (mode << 8) | clic->clicintctl[irq_offset], irq
    };
    CLICActiveInterrupt *result = bsearch(&key,
                                          active_list, *active_count,
                                          sizeof(CLICActiveInterrupt),
                                          xt_clic_v0p10_active_compare);

    clic->clicintattr[irq_offset] = val & (clic->nvbits | 0xc6);
    if (val & 0x38) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "clic: intattr[i].WPRI ([3,5] bits) is write preserved, "
            "you should not write to it with a non-zero value.");
    }
    mode = xt_clic_v0p10_get_intpriv(clic, hartid, irq);
    if (result) {
        result->intcfg = (mode << 8) | clic->clicintctl[irq_offset];
        qsort(active_list, *active_count,
              sizeof(CLICActiveInterrupt),
              xt_clic_v0p10_active_compare);
    }
    xt_clic_v0p10_next_interrupt(clic, hartid);
}

uint8_t xt_clic_v0p10_get_intie(void *opaque, int hartid, int irq)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;
    return clic->clicintie[irq_offset];
}

void xt_clic_v0p10_update_intie(void *opaque, int hartid, int irq,
                                uint8_t val)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;

    uint8_t old_intie = clic->clicintie[irq_offset];
    if (xt_clic_reserved_irq(irq)) {
        return;
    }
    clic->clicintie[irq_offset] = !!val;

    /* Add to or remove from list of active interrupts */
    if (val && !old_intie) {
        xt_clic_v0p10_enable_irq(clic, hartid, irq);
    } else if (!val && old_intie) {
        xt_clic_v0p10_disable_irq(clic, hartid, irq);
    }
    xt_clic_v0p10_next_interrupt(clic, hartid);
}

void xt_clic_v0p10_update_intip(void *opaque, int hartid, int irq,
                                uint8_t val)
{
    XTCLICV0P10State *clic = opaque;
    size_t irq_offset = irq + clic->num_sources * hartid;
    if (xt_clic_reserved_irq(irq)) {
        return;
    }
    clic->clicintip[irq_offset] = !!val;
    xt_clic_v0p10_next_interrupt(clic, hartid);
}
