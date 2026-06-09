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
#include "hw/intc/xt_clic_internal.h"

#define TYPE_XT_CLIC "csky_xt_clic"
#define XT_CLIC(obj) \
    OBJECT_CHECK(XTCLICState, (obj), TYPE_XT_CLIC)
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

void xt_clic_clean_pending(void *opaque, int irq);
bool xt_clic_edge_triggered(void *opaque, int irq);
bool xt_clic_shv_interrupt(void *opaque, int irq);
void xt_clic_get_next_interrupt(void *opaque);
bool xt_clic_is_clic_mode(CPURISCVState *env);
void xt_clic_set_irq(void *opaque, int irq, int level);
target_ulong xt_clic_find_suitable_interrupt(CPURISCVState *env, uint8_t write_mode);
#endif
