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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/dma/dw_dma_sw.h"
#include "hw/dma/dw_dma_sw_common.h"
#include "hw/misc/riscv_iopmp_txn_info.h"
#include "target/riscv/cpu.h"

#define MAX_FIFO_SIZE 256
uint32_t dw_dma_cal_memory_burst_length(dw_dma_state *s, dw_dma_channel *ch,
                                        uint32_t max_fifo_items)
{
    uint32_t remaining_items = ch->block_size;
    uint32_t burst_items;

    /* For memory transfers, burst length is based on FIFO and block needs */
    burst_items = (max_fifo_items < remaining_items) ?
                    max_fifo_items : remaining_items;
    return burst_items;
}

/* ========== Interrupt Management ========== */

void dw_dma_update_int(dw_dma_state *s)
{
    /*
     * Delegate interrupt update to bus-specific implementation.
     * AHB and AXI have different interrupt enable hierarchies:
     * - AHB: 2-level (ch->int_en + Mask)
     * - AXI: 4-5 level (ch->int_en + intstatus_enable + intsignal_enable + Mask + s->int_en)
     */
    if (s->bus_ops && s->bus_ops->update_int) {
        s->bus_ops->update_int(s);
    }
}

/* ========== Peripheral Type Check Functions ========== */

bool dw_dma_src_is_peripheral(dw_dma_channel *ch)
{
    switch (ch->tt_fc) {
    case DW_DMA_FC_D_P2M:
    case DW_DMA_FC_D_P2P:
    case DW_DMA_FC_P_P2M:
    case DW_DMA_FC_SP_P2P:
    case DW_DMA_FC_DP_P2P:
        return true;
    default:
        break;
    }
    return false;
}

bool dw_dma_dst_is_peripheral(dw_dma_channel *ch)
{
    switch (ch->tt_fc) {
    case DW_DMA_FC_D_M2P:
    case DW_DMA_FC_D_P2P:
    case DW_DMA_FC_P_M2P:
    case DW_DMA_FC_SP_P2P:
    case DW_DMA_FC_DP_P2P:
        return true;
    default:
        break;
    }
    return false;
}

/* ========== Handshake Functions ========== */

bool dw_dma_handshake_ready(dw_dma_state *s, dw_dma_channel *ch,
                            int channel, bool require_burst)
{
    bool src_side_needed = dw_dma_src_is_peripheral(ch);
    bool dst_side_needed = dw_dma_dst_is_peripheral(ch);

    if (!src_side_needed && !dst_side_needed) {
        /* Memory-to-memory, no handshake needed */
        return true;
    }

    /* Use bus-specific operations to get handshake status */
    if (require_burst) {
        /* Burst requires both Req and Sgl set */
        bool src_ok = !src_side_needed ||
                     (s->bus_ops->get_sw_hs_req(s, channel, true) &&
                      s->bus_ops->get_sw_hs_sgl(s, channel, true));
        bool dst_ok = !dst_side_needed ||
                     (s->bus_ops->get_sw_hs_req(s, channel, false) &&
                      s->bus_ops->get_sw_hs_sgl(s, channel, false));
        return src_ok && dst_ok;
    } else {
        /* Single transaction only requires Sgl set */
        bool src_ok = !src_side_needed ||
                     s->bus_ops->get_sw_hs_sgl(s, channel, true);
        bool dst_ok = !dst_side_needed ||
                     s->bus_ops->get_sw_hs_sgl(s, channel, false);
        return src_ok && dst_ok;
    }
}

bool dw_dma_in_single_transaction_region(dw_dma_channel *ch)
{
    /* Memory-to-memory transfers never use single transactions */
    if (ch->tt_fc == DW_DMA_FC_D_M2M) {
        return false;
    }

    uint32_t src_burst_size_bytes = ch->src_msize * ch->src_tr_width;
    uint32_t dst_burst_size_bytes = ch->dst_msize * ch->dst_tr_width;
    uint32_t remaining_bytes = ch->block_size * ch->src_tr_width;

    bool src_in_single = false;
    bool dst_in_single = false;

    if (dw_dma_src_is_peripheral(ch)) {
        src_in_single = (remaining_bytes < src_burst_size_bytes);
    }

    if (dw_dma_dst_is_peripheral(ch)) {
        dst_in_single = (remaining_bytes < dst_burst_size_bytes);
    }

    return src_in_single || dst_in_single;
}

/* ========== DMA Transfer Functions ========== */

static void dma_txn_info_push(StreamSink *sink, uint8_t *buf, bool eop)
{
    if (sink == NULL) {
        return;
    }
    if (eop) {
        while (stream_push(sink, buf, sizeof(RISCVIOPMPTxnInfo), true) == 0) {
            ;
        }
    } else {
        while (stream_push(sink, buf, sizeof(RISCVIOPMPTxnInfo), false) == 0) {
            ;
        }
    }
}

bool dw_dma_do_single_transfer(dw_dma_state *s, int channel, bool is_burst)
{
    dw_dma_channel *ch = &s->chan[channel];
    uint32_t transfer_size;
    uint8_t buffer[MAX_FIFO_SIZE];
    MemTxResult result;
    uint32_t src_items_to_transfer;
    bool src_is_peripheral = dw_dma_src_is_peripheral(ch);
    bool dst_is_peripheral = dw_dma_dst_is_peripheral(ch);

    /* Calculate transfer size */
    if (ch->tt_fc == DW_DMA_FC_D_M2M) {
        /* Memory-to-memory: use FIFO-based burst */
        uint32_t max_fifo_items = MAX_FIFO_SIZE / ch->src_tr_width;
        src_items_to_transfer = dw_dma_cal_memory_burst_length(s, ch, max_fifo_items);
        transfer_size = src_items_to_transfer * ch->src_tr_width;
    } else {
        /* Peripheral transfers */
        if (is_burst) {
            uint32_t src_burst_size_bytes = ch->src_msize * ch->src_tr_width;
            uint32_t dst_burst_size_bytes = ch->dst_msize * ch->dst_tr_width;
            if (src_is_peripheral && dst_is_peripheral) {
                transfer_size = (src_burst_size_bytes < dst_burst_size_bytes) ?
                               src_burst_size_bytes : dst_burst_size_bytes;
            } else if (src_is_peripheral) {
                transfer_size = src_burst_size_bytes;
            } else {
                transfer_size = dst_burst_size_bytes;
            }
        } else {
            transfer_size = src_is_peripheral ? ch->src_tr_width : ch->dst_tr_width;
        }
    }

    if (transfer_size > MAX_FIFO_SIZE) {
        transfer_size = MAX_FIFO_SIZE;
    }

    src_items_to_transfer = transfer_size / ch->src_tr_width;
    if (src_items_to_transfer > ch->block_size) {
        src_items_to_transfer = ch->block_size;
        transfer_size = src_items_to_transfer * ch->src_tr_width;
        qemu_log_mask(LOG_GUEST_ERROR, "Transferred bytes exceeds block size!");
    }
    if (ch->tt_fc <= DW_DMA_FC_D_P2P) {
        ch->block_size_cmplt = src_items_to_transfer;
    }

    if (transfer_size == 0 || src_items_to_transfer == 0) {
        return false;
    }

    ch->current_src_addr = ch->src;
    ch->current_dest_addr = ch->dest;

    /* Perform memory transfer */
    if (s->target_sink) {
        RISCVIOPMPTxnInfo info;
        MemTxAttrs attrs;
        attrs.requester_id = s->rrid;

        info.rrid = s->rrid;
        info.start_addr = ch->src;
        info.end_addr = info.start_addr + transfer_size;
        info.stage = 0;
        dma_txn_info_push(s->target_sink, (uint8_t *)&info, 0);
        if (address_space_rw(&address_space_memory, ch->src,
                             attrs, buffer, transfer_size, 0) == MEMTX_OK) {
            dma_txn_info_push(s->target_sink, (uint8_t *)&info, 1);
            info.start_addr = ch->dest;
            info.end_addr = info.start_addr + transfer_size;
            dma_txn_info_push(s->target_sink, (uint8_t *)&info, 0);
            if (address_space_rw(&address_space_memory, ch->dest,
                                 attrs, buffer, transfer_size, 1) == MEMTX_OK) {
                dma_txn_info_push(s->target_sink, (uint8_t *)&info, 1);
            } else {
                s->err_int |= (1u << channel);
                return false;
            }
        } else {
            s->err_int |= (1u << channel);
            return false;
        }
    } else {
        result = dma_memory_read(s->dma_as, ch->src, buffer,
                                transfer_size, MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            s->err_int |= (1u << channel);
            return false;
        }

        result = dma_memory_write(s->dma_as, ch->dest, buffer,
                                 transfer_size, MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            s->err_int |= (1u << channel);
            return false;
        }
    }

    /* Update addresses based on increment mode */
    if (ch->sinc == 0) {
        ch->src += transfer_size;
    } else if (ch->sinc == 1) {
        ch->src -= transfer_size;
    }

    if (ch->dinc == 0) {
        ch->dest += transfer_size;
    } else if (ch->dinc == 1) {
        ch->dest -= transfer_size;
    }

    ch->block_size -= src_items_to_transfer;

    /* Set transaction completion interrupts for peripheral transfers */
    if (src_is_peripheral) {
        s->srctran_int |= (1u << channel);
    }
    if (dst_is_peripheral) {
        s->dsttran_int |= (1u << channel);
    }

    return true;
}

void dw_dma_do_transfer(dw_dma_state *s, int channel)
{
    dw_dma_channel *ch = &s->chan[channel];

    if (!s->dma_enable || !ch->chan_enable || channel >= s->num_channels) {
        return;
    }

    ch->state = DW_DMA_CH_TRANSFERRING;

    bool transfer_done = false;

    if (ch->tt_fc == DW_DMA_FC_D_M2M) {
        /* Memory-to-memory: transfer entire block without handshake */
        while (ch->block_size > 0) {
            transfer_done = dw_dma_do_single_transfer(s, channel, true);
            if (!transfer_done) {
                break;
            }
        }
    } else {
        /* Peripheral transfers: use handshake logic */
        bool in_single_region = dw_dma_in_single_transaction_region(ch);

        if (!in_single_region) {
            /* Burst region */
            if (dw_dma_handshake_ready(s, ch, channel, true)) {
                transfer_done = dw_dma_do_single_transfer(s, channel, true);
            }
        } else {
            /* Single transaction region */
            bool single_ready = dw_dma_handshake_ready(s, ch, channel, false);
            bool burst_ready = dw_dma_handshake_ready(s, ch, channel, true);

            if (single_ready && !burst_ready) {
                transfer_done = dw_dma_do_single_transfer(s, channel, false);
            } else if (burst_ready) {
                transfer_done = dw_dma_do_single_transfer(s, channel, true);
            }
        }
    }

    if (!transfer_done && ch->tt_fc != DW_DMA_FC_D_M2M) {
        /* No transfer for peripheral, channel remains enabled */
        return;
    }

    /* Clear handshake requests after successful transfer */
    if (s->bus_ops && s->bus_ops->clear_sw_hs) {
        s->bus_ops->clear_sw_hs(s, channel);
    }

    /* Check if block completed */
    if (ch->block_size == 0) {
        s->block_int |= (1u << channel);
        s->tfr_int   |= (1u << channel);
        ch->chan_enable = 0;
        ch->state = DW_DMA_CH_COMPLETED;

        /* Note: When DMAC_EN is written to 0 while channels are still transferring,
         * the read logic in dw_dma_axi_read() automatically checks for active channels
         * and returns the appropriate value. No explicit state update is needed here.
         */
    }

    dw_dma_update_int(s);
}

/* ========== Software Handshake Functions ========== */

void dw_dma_write_sw_regs(dw_dma_state *s, uint32_t *soft_hs_reg, uint64_t value)
{
    uint32_t we = (value >> 8) & 0xFF;
    uint32_t data_bits = value & 0xFF;

    for (int i = 0; i < s->num_channels; i++) {
        uint32_t mask = (1u << i);

        if ((we & mask) && s->chan[i].chan_enable) {
            if (data_bits & mask) {
                *soft_hs_reg |= mask;
            } else {
                *soft_hs_reg &= ~mask;
            }
        }

        if (s->dma_enable && (*soft_hs_reg & mask) && s->chan[i].chan_enable) {
            dw_dma_do_transfer(s, i);
        }
    }
}

void dw_dma_write_sw_regs_lst(dw_dma_state *s, uint32_t *soft_hs_reg, uint64_t value)
{
    uint32_t we = (value >> 8) & 0xFF;
    uint32_t data_bits = value & 0xFF;

    for (int i = 0; i < s->num_channels; i++) {
        uint32_t mask = (1u << i);

        if ((we & mask) && s->chan[i].chan_enable) {
            if (data_bits & mask) {
                *soft_hs_reg |= mask;
            } else {
                *soft_hs_reg &= ~mask;
            }
        }
    }
}

/* ========== Device Lifecycle Functions ========== */

void dw_dma_common_reset(dw_dma_state *s)
{
    s->dma_enable = 0;
    s->int_en = 0;  /* Global interrupt enable - reset value is 0 */
    s->tfr_int = 0;
    s->block_int = 0;
    s->srctran_int = 0;
    s->dsttran_int = 0;
    s->err_int = 0;
    s->tfr_int_mask = 0;
    s->block_int_mask = 0;
    s->srctran_int_mask = 0;
    s->dsttran_int_mask = 0;
    s->err_int_mask = 0;
    s->status_int = 0;
    s->req_src_reg = 0;
    s->req_dst_reg = 0;
    s->sgl_req_src_reg = 0;
    s->sgl_req_dst_reg = 0;
    s->lst_src_reg = 0;
    s->lst_dst_reg = 0;

    for (int i = 0; i < 6; i++) {
        s->dma_comp_params[i] = 0;
    }

    for (int i = 0; i < s->num_channels; i++) {
        dw_dma_channel *ch = &s->chan[i];

        /* Reset addresses */
        ch->src = 0;
        ch->dest = 0;
        ch->llp = 0;

        /* Reset transfer control parameters */
        ch->block_ts = 0;
        ch->src_tr_width = 1;
        ch->dst_tr_width = 1;
        ch->src_msize = 1;
        ch->dst_msize = 1;
        ch->sinc = 0;
        ch->dinc = 0;

        /* Reset transfer type and flow control */
        ch->tt_fc = 0;

        /* Reset handshake configuration */
        ch->hs_sel_src = true;
        ch->hs_sel_dst = true;
        ch->src_hs_pol = false;
        ch->dst_hs_pol = false;
        ch->src_per = 0;
        ch->dest_per = 0;

        /* Reset interrupt and priority */
        ch->int_en = false;
        ch->ch_prior = 0;

        /* Reset AXI-specific interrupt enable registers
         * Default to 0 for strict control (AXI behavior).
         * AHB will override these to 0x1F (all enabled) for transparent pass-through.
         */
        ch->intstatus_enable = 0;
        ch->intsignal_enable = 0;

        /* Reset AXI-specific fields */
        ch->ar_cache = 0;
        ch->aw_cache = 0;
        ch->ar_prot = 0;
        ch->aw_prot = 0;
        ch->arlen_en = false;
        ch->arlen = 0;
        ch->awlen_en = false;
        ch->awlen = 0;
        ch->src_osr_lmt = 0;
        ch->dst_osr_lmt = 0;
        ch->lock_ch = false;
        ch->lock_ch_l = 0;
        ch->nonposted_lastwrite_en = false;
        ch->src_stat_en = false;
        ch->dst_stat_en = false;
        ch->ioc_blktfr = false;

        /* Reset unimplemented features */
        ch->src_gather_en = false;
        ch->dst_scatter_en = false;
        ch->sgr = 0;
        ch->dsr = 0;
        ch->llp_dst_en = false;
        ch->llp_src_en = false;
        ch->lms = 0;

        /* Reset channel state */
        ch->chan_enable = 0;
        ch->state = DW_DMA_CH_DISABLED;
        ch->ch_susp = false;
        ch->fifo_empty = true;
        ch->block_size = 0;

        /* Reset software handshake */
        ch->sw_hs_src_reg = 0;
        ch->sw_hs_dst_reg = 0;

        /* Reset transfer status */
        ch->current_src_addr = 0;
        ch->current_dest_addr = 0;
    }
}

void dw_dma_common_realize(dw_dma_state *s, const dw_dma_bus_ops *bus_ops,
                          Error **errp)
{
    s->bus_ops = bus_ops;

    if (!s->dma_as) {
        s->dma_as = &address_space_memory;
    }
}

void dw_dma_setup_sink(DeviceState *dev, StreamSink *sink)
{
    dw_dma_state *s = (dw_dma_state *)dev;
    s->target_sink = sink;
}
