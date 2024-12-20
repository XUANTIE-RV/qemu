/*
 * RISC-V CLIC(Core Local Interrupt Controller) interface.
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

#ifndef XT_CLIC_H
#define XT_CLIC_H

#include "hw/irq.h"
#include "hw/sysbus.h"

#define TYPE_XT_CLIC "csky_xt_clic"
#define XT_CLIC(obj) \
    OBJECT_CHECK(XTCLICState, (obj), TYPE_XT_CLIC)

/*
 * CLIC per hart active interrupts
 *
 * We maintain per hart lists of enabled interrupts sorted by
 * mode+level+priority. The sorting is done on the configuration path
 * so that the interrupt delivery fastpath can linear scan enabled
 * interrupts in priority order.
 */
typedef struct CLICActiveInterrupt {
    uint16_t intcfg;
    uint16_t irq;
} CLICActiveInterrupt;

typedef enum TRIG_TYPE {
    POSITIVE_LEVEL,
    POSITIVE_EDGE,
    NEG_LEVEL,
    NEG_EDGE,
} TRIG_TYPE;

typedef struct XTCLICState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    /* Implementaion parameters */
    bool nvbits;
    uint32_t num_harts;
    uint32_t num_sources;
    uint32_t clic_size;
    uint32_t clicintctlbits;
    uint64_t mclicbase;

    /* Global configuration */
    uint8_t *nmbits;
    uint8_t *nlbits;
    uint32_t *clicinfo;

    /* Aperture configuration */
    uint8_t *clicintip;
    uint8_t *clicintie;
    uint8_t *clicintattr;
    uint8_t *clicintctl;

    /* Complatible with v0.8 */
    uint32_t *mintthresh;

    /* QEMU implementaion related fields */
    CLICActiveInterrupt *active_list;
    size_t *active_count;
    MemoryRegion mmio;
} XTCLICState;

DeviceState *xt_clic_create(hwaddr addr, bool vector,
                            uint32_t num_harts, uint32_t num_sources,
                            uint8_t clicintctlbits);

void xt_clic_decode_exccode(uint32_t exccode, int *mode, int *il, int *irq);
void xt_clic_clean_pending(void *opaque, int irq);
bool xt_clic_edge_triggered(void *opaque, int irq);
bool xt_clic_shv_interrupt(void *opaque, int irq);
void xt_clic_get_next_interrupt(void *opaque);
bool xt_clic_is_clic_mode(CPURISCVState *env);
#endif
