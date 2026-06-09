/*
 * DW DMA Common Implementation - Software Handshake Only Version
 * Common functionality shared between AHB and AXI bus implementations
 *
 * Written by Tang Tiancheng <tangtiancheng.ttc@alibaba-inc.com>
 *
 * Copyright (c) 2025 Alibaba Group. All rights reserved.
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

#ifndef DW_DMA_SW_COMMON_H
#define DW_DMA_SW_COMMON_H

#include "hw/dma/dw_dma_sw.h"

bool dw_dma_do_single_transfer(dw_dma_state *s, int channel, bool is_burst);
void dw_dma_do_transfer(dw_dma_state *s, int channel);

uint32_t dw_dma_cal_memory_burst_length(dw_dma_state *s, dw_dma_channel *ch,
                                        uint32_t max_fifo_items);
bool dw_dma_handshake_ready(dw_dma_state *s, dw_dma_channel *ch,
                            int channel, bool require_burst);
bool dw_dma_in_single_transaction_region(dw_dma_channel *ch);

void dw_dma_write_sw_regs(dw_dma_state *s, uint32_t *soft_hs_reg, uint64_t value);
void dw_dma_write_sw_regs_lst(dw_dma_state *s, uint32_t *soft_hs_reg, uint64_t value);

bool dw_dma_src_is_peripheral(dw_dma_channel *ch);
bool dw_dma_dst_is_peripheral(dw_dma_channel *ch);

void dw_dma_common_reset(dw_dma_state *s);
void dw_dma_common_realize(dw_dma_state *s, const dw_dma_bus_ops *bus_ops,
                          Error **errp);

void dw_dma_update_int(dw_dma_state *s);

#endif /* DW_DMA_SW_COMMON_H */
