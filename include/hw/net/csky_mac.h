/*
 * CSKY MAC header.
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

#include "net/net.h"
#include "exec/memory.h"
#include "hw/irq.h"

#define TYPE_CSKY_MAC "csky_mac"
#define CSKY_MAC(obj) OBJECT_CHECK(csky_mac_state, (obj), TYPE_CSKY_MAC)

#define NUM_BD 128
/* buffer descriptor*/
typedef struct {
    uint32_t status;
    uint32_t buffer_addr;
} csky_mac_bd;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    NICState *nic;
    NICConf conf;
    qemu_irq irq;
    uint32_t moder;
    uint32_t int_source;
    uint32_t int_mask;
    uint32_t ipgt;
    uint32_t ipgr1;
    uint32_t ipgr2;
    uint32_t packetlen;
    uint32_t collconf;
    uint32_t tx_bd_num;
    uint32_t ctrlmoder;
    uint32_t mii_moder;
    uint32_t mii_command;
    uint32_t mii_address;
    uint32_t mii_tx_data;
    uint32_t mii_rx_data;
    uint32_t mii_status;
    uint32_t eth_hash0_adr;
    uint32_t eth_hash1_adr;
    uint32_t eth_tx_ctrl;
    csky_mac_bd bd_buffer[NUM_BD];
    int32_t next_rx;
} csky_mac_state;


void csky_mac_create(uint32_t base, qemu_irq irq);
