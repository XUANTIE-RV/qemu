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
#include "hw/intc/riscv_clic.h"

/*
 * The 2-bit trig WARL field specifies the trigger type and polarity for each
 * interrupt input. Bit 1, trig[0], is defined as "edge-triggered"
 * (0: level-triggered, 1: edge-triggered); while bit 2, trig[1], is defined as
 * "negative-edge" (0: positive-edge, 1: negative-edge). (Section 3.6)
 */

static inline TRIG_TYPE
riscv_clic_get_trigger_type(RISCVCLICState *clic, size_t irq_offset)
{
    return (clic->clicintattr[irq_offset] >> 1) & 0x3;
}

static inline bool
riscv_clic_is_edge_triggered(RISCVCLICState *clic, size_t irq_offset)
{
    return (clic->clicintattr[irq_offset] >> 1) & 0x1;
}

static inline bool
riscv_clic_is_shv_interrupt(RISCVCLICState *clic, size_t irq_offset)
{
    return (clic->clicintattr[irq_offset] & 0x1) && clic->nvbits;
}

static uint8_t
riscv_clic_get_interrupt_level(RISCVCLICState *clic, uint8_t intctl)
{
    int nlbits = clic->nlbits;

    uint8_t mask_il = ((1 << nlbits) - 1) << (8 - nlbits);
    uint8_t mask_padding = (1 << (8 - nlbits)) - 1;
    /* unused level bits are set to 1 */
    return (intctl & mask_il) | mask_padding;
}

static uint8_t
riscv_clic_get_interrupt_priority(RISCVCLICState *clic, uint8_t intctl)
{
    int npbits = clic->clicintctlbits - clic->nlbits;
    uint8_t mask_priority = ((1 << npbits) - 1) << (8 - npbits);
    uint8_t mask_padding = (1 << (8 - npbits)) - 1;

    if (npbits < 0) {
        return UINT8_MAX;
    }
    /* unused priority bits are set to 1 */
    return (intctl & mask_priority) | mask_padding;
}

static void
riscv_clic_intcfg_decode(RISCVCLICState *clic, uint16_t intcfg,
                         uint8_t *mode, uint8_t *level,
                         uint8_t *priority)
{
    *mode = intcfg >> 8;
    *level = riscv_clic_get_interrupt_level(clic, intcfg & 0xff);
    *priority = riscv_clic_get_interrupt_priority(clic, intcfg & 0xff);
}

/*
 * In a system with multiple harts, the M-mode CLIC regions for all the harts
 * are placed contiguously in the memory space, followed by the S-mode CLIC
 * regions for all harts. (Section 3.11)
 */
static size_t
riscv_clic_get_irq_offset(RISCVCLICState *clic, int mode, int hartid, int irq)
{
    size_t mode_offset = 0;
    size_t unit = clic->num_harts * clic->num_sources;

    switch (mode) {
    case PRV_M:
        mode_offset = 0;
        break;
    case PRV_S:
        mode_offset = unit;
        break;
    case PRV_U:
        mode_offset = clic->prv_s ? 2 * unit : unit;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid mode %d\n", mode);
        exit(1);
    }
    return mode_offset + hartid * clic->num_sources + irq;
}

static void riscv_clic_next_interrupt(void *opaque, int hartid)
{
    /*
     * Scan active list for highest priority pending interrupts
     * comparing against this harts mintstatus register and interrupt
     * the core if we have a higher priority interrupt to deliver
     */
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(hartid));
    CPURISCVState *env = &cpu->env;
    RISCVCLICState *clic = (RISCVCLICState *)opaque;

    int il[4] = {
        MAX(get_field(env->mintstatus, MINTSTATUS_UIL),
            clic->mintthresh), /* PRV_U */
        MAX(get_field(env->mintstatus, MINTSTATUS_SIL),
            clic->sintthresh), /* PRV_S */
        0,                     /* reserverd */
        MAX(get_field(env->mintstatus, MINTSTATUS_MIL),
            clic->uintthresh)  /* PRV_M */
    };

    /* Get sorted list of enabled interrupts for this hart */
    size_t hart_offset = hartid * clic->num_sources;
    CLICActiveInterrupt *active = &clic->active_list[hart_offset];
    size_t active_count = clic->active_count[hartid];
    uint8_t mode, level, priority;

    /* Loop through the enabled interrupts sorted by mode+priority+level */
    while (active_count) {
        size_t irq_offset;
        riscv_clic_intcfg_decode(clic, active->intcfg, &mode, &level,
                                 &priority);
        if (mode < env->priv || (mode == env->priv && level <= il[mode])) {
            /*
             * No pending interrupts with high enough mode+priority+level
             * break and clear pending interrupt for this hart
             */
            break;
        }
        irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, active->irq);
        /* Check pending interrupt with high enough mode+priority+level */
        if (clic->clicintip[irq_offset]) {
            /* Clean vector edge-triggered pending */
            if (riscv_clic_is_edge_triggered(clic, irq_offset) &&
                riscv_clic_is_shv_interrupt(clic, irq_offset)) {
                clic->clicintip[irq_offset] = 0;
            }
            /* Post pending interrupt for this hart */
            clic->exccode[hartid] = active->irq | mode << 12 | level << 14;
            qemu_set_irq(clic->cpu_irqs[hartid], 1);
            return;
        }
        /* Check next enabled interrupt */
        active_count--;
        active++;
    }
}

/*
 * Any interrupt i that is not accessible to S-mode or U-Mode
 * appears as hard-wired zeros in clicintip[i], clicintie[i],
 * clicintattr[i], and clicintctl[i].(Section 3.9)(Section 3.10)
 */
static bool
riscv_clic_check_visible(RISCVCLICState *clic, int mode, int hartid, int irq)
{
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);
    if (!clic->prv_s && !clic->prv_u) { /* M */
        return mode == PRV_M;
    } else if (!clic->prv_s) { /* M/U */
        switch (clic->nmbits) {
        case 0:
            return mode == PRV_M;
        case 1:
            return clic->clicintattr[irq_offset] & 0x80 ? (mode == PRV_M) :
                                                          (mode == PRV_U);
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                "clic: nmbits can only be 0 or 1 for M/U hart");
            exit(1);
        }
    } else { /* M/S/U */
        switch (clic->nmbits) {
        case 0:
            return mode == PRV_M;
        case 1:
            return clic->clicintattr[irq_offset] & 0x80 ? (mode == PRV_M) :
                                                          (mode == PRV_S);
        case 2:
            return mode == clic->clicintattr[irq_offset] >> 6;
        case 3:
            qemu_log_mask(LOG_GUEST_ERROR,
                "clic: nmbits can only be 0 or 1 or 2 for M/S/U hart");
            exit(1);
        }
    }
    return false;
}

/*
 * For level-triggered interrupts, software writes to pending bits are
 * ignored completely. (Section 3.4)
 */
static bool
riscv_clic_validate_intip(RISCVCLICState *clic, int mode, int hartid, int irq)
{
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);
    return riscv_clic_is_edge_triggered(clic, irq_offset);
}

static void
riscv_clic_update_intip(RISCVCLICState *clic, int mode, int hartid,
                        int irq, uint64_t value)
{
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);
    clic->clicintip[irq_offset] = !!value;
    riscv_clic_next_interrupt(clic, hartid);
}

/*
 * For security purpose, the field can only be set to a privilege
 * level that is equal mode to or lower than the currently running
 * privilege level.(Section 3.6)
 */

static bool riscv_clic_validate_intattr(RISCVCLICState *clic, uint64_t value)
{
    int mode = extract64(value, 6, 2);

    if (!qtest_enabled()) {
        CPURISCVState *env = cpu_env(current_cpu);
        if (env->priv < mode) {
            return false;
        }
    }
    return true;
}

static inline int riscv_clic_encode_priority(const CLICActiveInterrupt *i)
{
    return ((i->intcfg & 0x3ff) << 12) | /* Highest mode+level+priority */
           (i->irq & 0xfff);             /* Highest irq number */
}

static int riscv_clic_active_compare(const void *a, const void *b)
{
    return riscv_clic_encode_priority(b) - riscv_clic_encode_priority(a);
}

static void riscv_clic_enable_irq(RISCVCLICState *clic, int mode,
                                  int hartid, int irq)
{
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];

    active_list[*active_count].intcfg = (mode << 8) |
                                        clic->clicintctl[irq_offset];
    active_list[*active_count].irq = irq;
    (*active_count)++;

    /* Sort list of active interrupts */
    qsort(active_list, *active_count,
          sizeof(CLICActiveInterrupt),
          riscv_clic_active_compare);
}

/* Notice this irq must be enabled before call this function */
static void riscv_clic_disable_irq(RISCVCLICState *clic, int mode,
                                   int hartid, int irq)
{
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];

    CLICActiveInterrupt key = {
        (mode << 8) | clic->clicintctl[irq_offset], irq
    };
    CLICActiveInterrupt *result = bsearch(&key,
                                          active_list, *active_count,
                                          sizeof(CLICActiveInterrupt),
                                          riscv_clic_active_compare);
    assert(result);
    size_t elem = (result - active_list);
    size_t sz = (--(*active_count) - elem) * sizeof(CLICActiveInterrupt);
    memmove(&result[0], &result[1], sz);

    /* Sort list of active interrupts */
    qsort(active_list, *active_count,
          sizeof(CLICActiveInterrupt),
          riscv_clic_active_compare);
}

static void riscv_clic_update_intctl(RISCVCLICState *clic, int mode, int hartid,
                                     int irq, uint64_t new_intctl)
{
    size_t hart_offset = hartid * clic->num_sources;
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);
    CLICActiveInterrupt *active_list = &clic->active_list[hart_offset];
    size_t *active_count = &clic->active_count[hartid];

    CLICActiveInterrupt key = {
        (mode << 8) | clic->clicintctl[irq_offset], irq
    };
    CLICActiveInterrupt *result = bsearch(&key,
                                          active_list, *active_count,
                                          sizeof(CLICActiveInterrupt),
                                          riscv_clic_active_compare);

    if (result) {
        result->intcfg = (mode << 8) | new_intctl;
        qsort(active_list, *active_count,
              sizeof(CLICActiveInterrupt),
              riscv_clic_active_compare);
    }
    clic->clicintctl[irq_offset] = new_intctl;
    riscv_clic_next_interrupt(clic, hartid);
}

static void
riscv_clic_update_intie(RISCVCLICState *clic, int mode, int hartid,
                        int irq, uint64_t new_intie)
{
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);

    uint8_t old_intie = clic->clicintie[irq_offset];
    clic->clicintie[irq_offset] = !!new_intie;

    /* Add to or remove from list of active interrupts */
    if (new_intie && !old_intie) {
        riscv_clic_enable_irq(clic, mode, hartid, irq);
    } else if (!new_intie && old_intie) {
        riscv_clic_disable_irq(clic, mode, hartid, irq);
    }

    riscv_clic_next_interrupt(clic, hartid);
}

static void
riscv_clic_hart_write(RISCVCLICState *clic, hwaddr addr,
                      uint64_t value, unsigned size,
                      int mode, int hartid, int irq)
{
    int req = extract32(addr, 0, 2);
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);

    if (hartid >= clic->num_harts) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid hartid %u: 0x%" HWADDR_PRIx "\n",
                      hartid, addr);
        return;
    }

    if (irq >= clic->num_sources) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid irq %u: 0x%" HWADDR_PRIx "\n", irq, addr);
        return;
    }

    switch (req) {
    case 0: /* clicintip[i] */
        if (riscv_clic_validate_intip(clic, mode, hartid, irq)) {
            /*
             * The actual pending bit is located at bit 0 (i.e., the
             * leastsignificant bit). In case future extensions expand the bit
             * field, from FW perspective clicintip[i]=zero means no interrupt
             * pending, and clicintip[i]!=0 (not just 1) indicates an
             * interrupt is pending. (Section 3.4)
             */
            if (value != clic->clicintip[irq_offset]) {
                riscv_clic_update_intip(clic, mode, hartid, irq, value);
            }
        }
        break;
    case 1: /* clicintie[i] */
        if (clic->clicintie[irq_offset] != value) {
            riscv_clic_update_intie(clic, mode, hartid, irq, value);
        }
        break;
    case 2: /* clicintattr[i] */
        if (riscv_clic_validate_intattr(clic, value)) {
            if (clic->clicintattr[irq_offset] != value) {
                /* When nmbits=2, check WARL */
                bool invalid = (clic->nmbits == 2) &&
                               (extract64(value, 6, 2) == 0b10);
                if (invalid) {
                    uint8_t old_mode = extract32(clic->clicintattr[irq_offset],
                                                 6, 2);
                    value = deposit32(value, 6, 2, old_mode);
                }
                clic->clicintattr[irq_offset] = value;
                riscv_clic_next_interrupt(clic, hartid);
            }
        }
        break;
    case 3: /* clicintctl[i] */
        if (value != clic->clicintctl[irq_offset]) {
            riscv_clic_update_intctl(clic, mode, hartid, irq, value);
        }
        break;
    }
}

static uint64_t
riscv_clic_hart_read(RISCVCLICState *clic, hwaddr addr, int mode,
                     int hartid, int irq)
{
    int req = extract32(addr, 0, 2);
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);

    if (hartid >= clic->num_harts) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid hartid %u: 0x%" HWADDR_PRIx "\n",
                      hartid, addr);
        return 0;
    }

    if (irq >= clic->num_sources) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid irq %u: 0x%" HWADDR_PRIx "\n", irq, addr);
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

/* Return target interrupt mode */
static int riscv_clic_get_mode(RISCVCLICState *clic, hwaddr addr)
{
    int mode = addr / (4 * clic->num_harts * clic->num_sources);
    switch (mode) {
    case 0:
        return PRV_M;
    case 1:
        assert(clic->prv_s || clic->prv_u);
        return clic->prv_s ? PRV_S : PRV_U;
    case 2:
        assert(clic->prv_s && clic->prv_u);
        return PRV_U;
    default:
        g_assert_not_reached();
        break;
    }
}

/* Return target hart id */
static int riscv_clic_get_hartid(RISCVCLICState *clic, hwaddr addr)
{
    int mode_unit = 4 * clic->num_harts * clic->num_sources;
    int hart_unit = 4 * clic->num_sources;

    return (addr % mode_unit) / hart_unit;
}

/* Return target interrupt number */
static int riscv_clic_get_irq(RISCVCLICState *clic, hwaddr addr)
{
    int hart_unit = 4 * clic->num_sources;
    return (addr % hart_unit) / 4;
}

static void
riscv_clic_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    RISCVCLICState *clic = opaque;
    hwaddr clic_size = clic->clic_size;
    int hartid, mode, irq;

    if (addr < clic_size) {
        if (addr < 0x1000) {
            assert(addr % 4 == 0);
            int index = addr / 4;
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
                        clic->nlbits = nlbits;
                    }
                    /* Valid values are given by implemented priviledges */
                    if (clic->prv_s && clic->prv_u) {
                        if (nmbits <= 2) {
                            clic->nmbits = nmbits;
                        }
                    } else if (clic->prv_u) {
                        if (nmbits <= 1) {
                            clic->nmbits = nmbits;
                        }
                    } else {
                        assert(!clic->prv_s);
                        if (nmbits == 0) {
                            clic->nmbits = 0;
                        }
                    }
                    break;
                }
            case 1: /* clicinfo, read-only register */
                qemu_log_mask(LOG_GUEST_ERROR,
                              "clic: write read-only clicinfo.\n");
                break;
            case 0x10 ... 0x2F: /* clicinttrig */
                {
                    uint32_t interrupt_number = value & MAKE_64BIT_MASK(0, 13);
                    if (interrupt_number <= clic->num_sources) {
                        value &= ~MAKE_64BIT_MASK(13, 18);
                        clic->clicinttrig[index - 0x10] = value;
                    }
                    break;
                }
            case 2: /* mintthresh */
                if (!strcmp(clic->version, "v0.8")) {
                    clic->mintthresh = value;
                    break;
                }
                qemu_log_mask(LOG_GUEST_ERROR,
                              "clic: invalid write addr: 0x%" HWADDR_PRIx "\n",
                              addr);
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                              "clic: invalid write addr: 0x%" HWADDR_PRIx "\n",
                              addr);
                return;
            }
        } else {
            addr -= 0x1000;
            hartid = riscv_clic_get_hartid(clic, addr);
            mode = riscv_clic_get_mode(clic, addr);
            irq = riscv_clic_get_irq(clic, addr);

            if (riscv_clic_check_visible(clic, mode, hartid, irq)) {
                riscv_clic_hart_write(clic, addr, value, size, mode,
                                      hartid, irq);
            }
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid write: 0x%" HWADDR_PRIx "\n", addr);
    }
}

static uint64_t riscv_clic_read(void *opaque, hwaddr addr, unsigned size)
{
    RISCVCLICState *clic = opaque;
    hwaddr clic_size = clic->clic_size;
    int hartid, mode, irq;

    if (addr < clic_size) {
        if (addr < 0x1000) {
            assert(addr % 4 == 0);
            int index = addr / 4;
            switch (index) {
            case 0: /* cliccfg */
                return clic->nvbits |
                       (clic->nlbits << 1) |
                       (clic->nmbits << 5);
            case 1: /* clicinfo */
                /*
                 * clicinfo register layout
                 *
                 * Bits Field
                 * 31 reserved (WARL 0)
                 * 30:25 num_trigger
                 * 24:21 CLICINTCTLBITS
                 * 20:13 version (for version control)
                 * 12:0 num_interrupt
                 */
                return (clic->clicintctlbits << 21) & ~INT32_MIN;
            case 0x10 ... 0x2F: /* clicinttrig */
                /*
                 * clicinttrig register layout
                 *
                 * Bits Field
                 * 31 enable
                 * 30:13 reserved (WARL 0)
                 * 12:0 interrupt_number
                 */
                return clic->clicinttrig[index - 0x10] &
                       ~MAKE_64BIT_MASK(13, 18);
            case 2: /* mintthresh */
                if (!strcmp(clic->version, "v0.8")) {
                    return clic->mintthresh;
                    break;
                }
                qemu_log_mask(LOG_GUEST_ERROR,
                              "clic: invalid read : 0x%" HWADDR_PRIx "\n",
                              addr);
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                              "clic: invalid read : 0x%" HWADDR_PRIx "\n",
                              addr);
                break;
            }
        } else {
            addr -= 0x1000;
            hartid = riscv_clic_get_hartid(clic, addr);
            mode = riscv_clic_get_mode(clic, addr);
            irq = riscv_clic_get_irq(clic, addr);

            if (riscv_clic_check_visible(clic, mode, hartid, irq)) {
                return riscv_clic_hart_read(clic, addr, mode, hartid, irq);
            }
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid read: 0x%" HWADDR_PRIx "\n", addr);
    }
    return 0;
}

static void riscv_clic_set_irq(void *opaque, int id, int level)
{
    RISCVCLICState *clic = opaque;
    int irq, hartid, mode;
    hwaddr addr = 4 * id;
    TRIG_TYPE type;

    hartid = riscv_clic_get_hartid(clic, addr);
    mode = riscv_clic_get_mode(clic, addr);
    irq = riscv_clic_get_irq(clic, addr);
    type = riscv_clic_get_trigger_type(clic, id);

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
            riscv_clic_update_intip(clic, mode, hartid, irq, level);
            break;
        case NEG_LEVEL:
            riscv_clic_update_intip(clic, mode, hartid, irq, !level);
            break;
        case NEG_EDGE:
            break;
        }
    } else {
        switch (type) {
        case POSITIVE_LEVEL:
            riscv_clic_update_intip(clic, mode, hartid, irq, level);
            break;
        case POSITIVE_EDGE:
            break;
        case NEG_LEVEL:
        case NEG_EDGE:
            riscv_clic_update_intip(clic, mode, hartid, irq, !level);
            break;
        }
    }
}

static void riscv_clic_cpu_irq_handler(void *opaque, int irq, int level)
{
    CPURISCVState *env = (CPURISCVState *)opaque;
    RISCVCLICState *clic = env->clic;
    CPUState *cpu = env_cpu(env);

    if (level) {
        env->exccode = clic->exccode[cpu->cpu_index];
        cpu_interrupt(env_cpu(env), CPU_INTERRUPT_CLIC);
    }
}

static const MemoryRegionOps riscv_clic_ops = {
    .read = riscv_clic_read,
    .write = riscv_clic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8
    }
};

static void riscv_clic_realize(DeviceState *dev, Error **errp)
{
    RISCVCLICState *clic = RISCV_CLIC(dev);
    size_t harts_x_sources = clic->num_harts * clic->num_sources;
    int irqs, i;

    if (clic->prv_s && clic->prv_u) {
        irqs = 3 * harts_x_sources;
    } else if (clic->prv_s || clic->prv_u) {
        irqs = 2 * harts_x_sources;
    } else {
        irqs = harts_x_sources;
    }

    clic->clic_size = irqs * 4 + 0x1000;
    memory_region_init_io(&clic->mmio, OBJECT(dev), &riscv_clic_ops, clic,
                          TYPE_RISCV_CLIC, clic->clic_size);

    clic->clicintip = g_new0(uint8_t, irqs);
    clic->clicintie = g_new0(uint8_t, irqs);
    clic->clicintattr = g_new0(uint8_t, irqs);
    clic->clicintctl = g_new0(uint8_t, irqs);
    clic->active_list = g_new0(CLICActiveInterrupt, irqs);
    clic->active_count = g_new0(size_t, clic->num_harts);
    clic->exccode = g_new0(uint32_t, clic->num_harts);
    clic->cpu_irqs = g_new0(qemu_irq, clic->num_harts);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &clic->mmio);

    /* Allocate irq through gpio, so that we can use qtest */
    qdev_init_gpio_in(dev, riscv_clic_set_irq, irqs);
    qdev_init_gpio_out(dev, clic->cpu_irqs, clic->num_harts);

    for (i = 0; i < clic->num_harts; i++) {
        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(i));
        qemu_irq irq = qemu_allocate_irq(riscv_clic_cpu_irq_handler,
                                         &cpu->env, 1);
        qdev_connect_gpio_out(dev, i, irq);
        cpu->env.clic = clic;
        cpu->env.mclicbase = clic->mclicbase;
    }
}

static Property riscv_clic_properties[] = {
    DEFINE_PROP_BOOL("prv-s", RISCVCLICState, prv_s, false),
    DEFINE_PROP_BOOL("prv-u", RISCVCLICState, prv_u, false),
    DEFINE_PROP_BOOL("vector", RISCVCLICState, nvbits, false),
    DEFINE_PROP_UINT32("num-harts", RISCVCLICState, num_harts, 0),
    DEFINE_PROP_UINT32("num-sources", RISCVCLICState, num_sources, 0),
    DEFINE_PROP_UINT32("clicintctlbits", RISCVCLICState, clicintctlbits, 0),
    DEFINE_PROP_UINT64("mclicbase", RISCVCLICState, mclicbase, 0),
    DEFINE_PROP_STRING("version", RISCVCLICState, version),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_clic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);

    dc->realize = riscv_clic_realize;
    device_class_set_props(dc, riscv_clic_properties);
    dc->desc = "cskysim type: INTC";
    dc->user_creatable = true;
}

static const TypeInfo riscv_clic_info = {
    .name          = TYPE_RISCV_CLIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVCLICState),
    .class_init    = riscv_clic_class_init,
};

static void riscv_clic_register_types(void)
{
    type_register_static(&riscv_clic_info);
}

type_init(riscv_clic_register_types)

/*
 * riscv_clic_create:
 *
 * @addr: base address of M-Mode CLIC memory-mapped registers
 * @prv_s: have smode region
 * @prv_u: have umode region
 * @vector: the selective interrupt hardware vectoring is implemented or not
 * @num_harts: number of CPU harts
 * @num_sources: number of interrupts supporting by each aperture
 * @clicintctlbits: bits are actually implemented in the clicintctl registers
 * @version: clic version, such as "v0.9"
 *
 * Returns: the device object
 */
DeviceState *riscv_clic_create(hwaddr addr, bool prv_s, bool prv_u,
                               bool vector, uint32_t num_harts,
                               uint32_t num_sources,
                               uint8_t clicintctlbits,
                               const char *version)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_CLIC);

    assert(num_sources <= 4096);
    assert(num_harts <= 1024);
    assert(clicintctlbits <= 8);
    assert(!strcmp(version, "v0.8") || !strcmp(version, "v0.9"));

    qdev_prop_set_bit(dev, "prv-s", prv_s);
    qdev_prop_set_bit(dev, "prv-u", prv_u);
    qdev_prop_set_bit(dev, "vector", vector);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "num-sources", num_sources);
    qdev_prop_set_uint32(dev, "clicintctlbits", clicintctlbits);
    qdev_prop_set_uint64(dev, "mclicbase", addr);
    qdev_prop_set_string(dev, "version", version);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}

void riscv_clic_get_next_interrupt(void *opaque, int hartid)
{
    RISCVCLICState *clic = opaque;
    riscv_clic_next_interrupt(clic, hartid);
}

bool riscv_clic_shv_interrupt(void *opaque, int mode, int hartid, int irq)
{
    RISCVCLICState *clic = opaque;
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);
    return riscv_clic_is_shv_interrupt(clic, irq_offset);
}

bool riscv_clic_edge_triggered(void *opaque, int mode, int hartid, int irq)
{
    RISCVCLICState *clic = opaque;
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);
    return riscv_clic_is_edge_triggered(clic, irq_offset);
}

void riscv_clic_clean_pending(void *opaque, int mode, int hartid, int irq)
{
    RISCVCLICState *clic = opaque;
    size_t irq_offset = riscv_clic_get_irq_offset(clic, mode, hartid, irq);
    clic->clicintip[irq_offset] = 0;
}

/*
 * The new CLIC interrupt-handling mode is encoded as a new state in
 * the existing WARL xtvec register, where the low two bits of  are 11.
 */
bool riscv_clic_is_clic_mode(CPURISCVState *env)
{
    target_ulong xtvec = (env->priv == PRV_M) ? env->mtvec : env->stvec;
    return env->clic && ((xtvec & 0x3) == 3);
}

void riscv_clic_decode_exccode(uint32_t exccode, int *mode,
                               int *il, int *irq)
{
    *irq = extract32(exccode, 0, 12);
    *mode = extract32(exccode, 12, 2);
    *il = extract32(exccode, 14, 8);
}
