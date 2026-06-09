/*
 * RISC-V CLIC(Core Local Interrupt Controller) v0.10 interface.
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

#ifndef XT_CLIC_V0P10_H
#define XT_CLIC_V0P10_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/intc/xt_clic_internal.h"

#define TYPE_XT_CLIC_V0P10 "csky_xt_clic_v0p10"
#define XT_CLIC_V0P10(obj) \
    OBJECT_CHECK(XTCLICV0P10State, (obj), TYPE_XT_CLIC_V0P10)

#define CLIC_MAX_IRQS 0x1024
typedef struct XTCLICV0P10State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    /* Implementaion parameters */
    bool nvbits;
    uint32_t num_harts;
    uint32_t num_sources;
    uint32_t clic_size;
    uint32_t clicintctlbits;

    /* Global configuration */
    uint8_t *nmbits;
    uint8_t *mnlbits;
    uint8_t *snlbits;

    /* Aperture configuration */
    uint8_t *clicintip;
    uint8_t *clicintie;
    uint8_t *clicintattr;
    uint8_t *clicintctl;

    /* QEMU implementaion related fields */
    CLICActiveInterrupt *active_list;
    size_t *active_count;
} XTCLICV0P10State;

DeviceState *xt_clic_v0p10_create(bool vector,
                                  uint32_t num_harts, uint32_t num_sources,
                                  uint8_t clicintctlbits);

void xt_clic_v0p10_decode_exccode(uint32_t exccode, int *mode, int *il,
                                  int *irq);
void xt_clic_v0p10_clean_pending(void *opaque, int hartid, int irq);
bool xt_clic_v0p10_edge_triggered(void *opaque, int hartid, int irq);
bool xt_clic_v0p10_shv_interrupt(void *opaque, int hartid, int irq);
void xt_clic_v0p10_get_next_interrupt(void *opaque);
bool xt_clic_v0p10_is_clic_mode(CPURISCVState *env);
void xt_clic_v0p10_next_interrupt(void *opaque, int hartid);
void xt_clic_v0p10_update_intctl(void *opaque, int hartid, int irq,
                                 uint8_t new_intctl);
uint8_t xt_clic_v0p10_get_intctl(void *opaque, int hartid, int irq);
uint8_t xt_clic_v0p10_get_intip(void *opaque, int hartid, int irq);
void xt_clic_v0p10_update_mcliccfg(void *opaque, int hartid, uint32_t val);
void xt_clic_v0p10_update_scliccfg(void *opaque, int hartid, uint32_t val);
uint32_t xt_clic_v0p10_get_mcliccfg(void *opaque, int hartid);
uint32_t xt_clic_v0p10_get_scliccfg(void *opaque, int hartid);
uint8_t xt_clic_v0p10_get_intpriv(void *opaque, int hartid, int irq);
uint8_t xt_clic_v0p10_get_intil(void *opaque, int hartid, int irq);
uint8_t xt_clic_v0p10_get_intattr(void *opaque, int hartid, int irq);
void xt_clic_v0p10_update_intattr(void *opaque, int hartid, int irq,
                                  uint8_t val);
uint8_t xt_clic_v0p10_get_intie(void *opaque, int hartid, int irq);
void xt_clic_v0p10_update_intie(void *opaque, int hartid, int irq,
                                uint8_t val);
void xt_clic_v0p10_update_intip(void *opaque, int hartid, int irq,
                                uint8_t val);
void xt_clic_v0p10_set_irq(void *opaque, int irq, int level);
target_ulong xt_clic_v0p10_find_suitable_interrupt(CPURISCVState *env, uint8_t cur_mode);
#endif