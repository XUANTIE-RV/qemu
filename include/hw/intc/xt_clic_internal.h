/*
 * RISC-V CLIC(Core Local Interrupt Controller) common interface.
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

#ifndef XT_CLIC_INTERNAL_H
#define XT_CLIC_INTERNAL_H
/*
 * CLIC per hart active interrupts
 *
 * We maintain per hart lists of enabled interrupts sorted by
 * mode+level+priority. The sorting is done on the configuration pa
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
#endif