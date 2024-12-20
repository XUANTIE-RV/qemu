/*
 * CSKY GMAC IP emulation.
 *
 * Author: wanghb
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
#include "hw/ptimer.h"
#include "exec/memory.h"
#include "hw/irq.h"

#define TYPE_CSKY_MAC_V2 "csky_mac_v2"
#define CSKY_MAC_V2(obj) \
    OBJECT_CHECK(csky_mac_v2_state, (obj), TYPE_CSKY_MAC_V2)

/* buffer descriptor*/
typedef struct {
    uint32_t status1;
    uint32_t status2;
    uint32_t buffer1;
    uint32_t buffer2;
} csky_mac_v2_bd;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    NICState *nic;
    NICConf conf;
    qemu_irq irq;
    ptimer_state *timer;
    /* MAC Reg */
    uint32_t config;
    uint32_t frame_filter;
    uint32_t hash_tab_high;
    uint32_t hash_tab_low;
    uint32_t mii_addr;
    uint32_t mii_data;
    uint32_t debug;
    uint32_t int_status;
    uint32_t int_mask;
    uint32_t watchdog_timeout;
    /* MAC MDA Reg */
    uint32_t bus_mode;
    uint32_t tx_poll_demand;
    uint32_t rx_poll_demand;
    uint32_t rx_des_list_addr;
    uint32_t tx_des_list_addr;
    uint32_t status;
    uint32_t operation_mode;
    uint32_t int_en;
    uint32_t missed_frame_buf_flow_ctrl;
    uint32_t rx_int_watchdog_timer;
    uint32_t ahb_axi_status;
    uint32_t cur_tx_des_addr;
    uint32_t cur_rx_des_addr;
    uint32_t cur_tx_buf_addr;
    uint32_t cur_rx_buf_addr;
} csky_mac_v2_state;

void csky_mac_v2_create(uint32_t base, qemu_irq irq);

