/*
 * DW_ahb_dmac 2.24a controller - AHB Bus Adapter
 * Software Handshake Only Version
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
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/dma/dw_dma_sw.h"
#include "hw/dma/dw_dma_sw_common.h"
#include "target/riscv/cpu.h"

#define TYPE_DW_DMA_SW_AHB   "dw_dma_sw_ahb"
#define DW_DMA_SW_AHB(obj)   OBJECT_CHECK(dw_dma_state, (obj), TYPE_DW_DMA_SW_AHB)

/* AHB supports up to 8 channels */
#define DW_DMA_AHB_NUM_CHANNELS  8
#define DW_DMA_AHB_REG_SIZE      0x400

/* Channel Registers */
#define DW_DMA_CHAN_REG_SAR           0x0
#define DW_DMA_CHAN_REG_DAR           0x8
#define DW_DMA_CHAN_REG_LLP           0x10
#define DW_DMA_CHAN_REG_CTL_LO        0x18
#define DW_DMA_CHAN_REG_CTL_HI        0x1c
#define DW_DMA_CHAN_REG_SSTAT         0x20
#define DW_DMA_CHAN_REG_DSTAT         0x28
#define DW_DMA_CHAN_REG_SSTATAR       0x30
#define DW_DMA_CHAN_REG_DSTATAR       0x38
#define DW_DMA_CHAN_REG_CFG_LO        0x40
#define DW_DMA_CHAN_REG_CFG_HI        0x44
#define DW_DMA_CHAN_REG_SGR           0x48
#define DW_DMA_CHAN_REG_DSR           0x50
#define DW_DMA_CHAN_REGS_SIZE         0x58
#define DW_DMA_CHANS                  8

/* Interrupt Registers */
#define DW_DMA_INT_REG_RawTfr         0x2c0
#define DW_DMA_INT_REG_RawBlock       0x2c8
#define DW_DMA_INT_REG_RawSrcTran     0x2d0
#define DW_DMA_INT_REG_RawDstTran     0x2d8
#define DW_DMA_INT_REG_RawErr         0x2e0
#define DW_DMA_INT_REG_StatusTfr      0x2e8
#define DW_DMA_INT_REG_StatusBlock    0x2f0
#define DW_DMA_INT_REG_StatusSrcTran  0x2f8
#define DW_DMA_INT_REG_StatusDstTran  0x300
#define DW_DMA_INT_REG_StatusErr      0x308
#define DW_DMA_INT_REG_MaskTfr        0x310
#define DW_DMA_INT_REG_MaskBlock      0x318
#define DW_DMA_INT_REG_MaskSrcTran    0x320
#define DW_DMA_INT_REG_MaskDstTran    0x328
#define DW_DMA_INT_REG_MaskErr        0x330
#define DW_DMA_INT_REG_ClearTfr       0x338
#define DW_DMA_INT_REG_ClearBlock     0x340
#define DW_DMA_INT_REG_ClearSrcTran   0x348
#define DW_DMA_INT_REG_ClearDstTran   0x350
#define DW_DMA_INT_REG_ClearErr       0x358
#define DW_DMA_INT_REG_StatusInt      0x360

/* Software Handshake Registers */
#define DW_DMA_SW_HS_REG_ReqSrcReg    0x368
#define DW_DMA_SW_HS_REG_ReqDstReg    0x370
#define DW_DMA_SW_HS_REG_SglReqSrcReg 0x378
#define DW_DMA_SW_HS_REG_SglReqDstReg 0x380
#define DW_DMA_SW_HS_REG_LstSrcReg    0x388
#define DW_DMA_SW_HS_REG_LstDstReg    0x390

/* Configuration Registers */
#define DW_DMA_CFG_REG_DmaCfgReg      0x398
#define DW_DMA_CFG_REG_ChEnReg        0x3a0
#define DW_DMA_CFG_REG_DmaIdReg       0x3a8
#define DW_DMA_CFG_REG_DmaTestReg     0x3b0
#define DW_DMA_CFG_REG_CompParams6    0x3c8
#define DW_DMA_CFG_REG_CompParams5    0x3d0
#define DW_DMA_CFG_REG_CompParams4    0x3d8
#define DW_DMA_CFG_REG_CompParams3    0x3e0
#define DW_DMA_CFG_REG_CompParams2    0x3e8
#define DW_DMA_CFG_REG_CompParams1    0x3f0
#define DW_DMA_CFG_REG_DmaCompsID     0x3f8
#define DW_DMA_CFG_REG_CompType       0x3f8
#define DW_DMA_CFG_REG_CompVersion    0x3fc

/* Control Register (CTL) Bit Fields */
#define DW_DMA_CTL_LO_INT_EN              (1 << 0)
#define DW_DMA_CTL_LO_DST_TR_WIDTH_SHIFT  1
#define DW_DMA_CTL_LO_SRC_TR_WIDTH_SHIFT  4
#define DW_DMA_CTL_LO_DINC_SHIFT          7
#define DW_DMA_CTL_LO_SINC_SHIFT          9
#define DW_DMA_CTL_LO_DEST_MSIZE_SHIFT    11
#define DW_DMA_CTL_LO_SRC_MSIZE_SHIFT     14
#define DW_DMA_CTL_LO_SRC_GATHER_EN       (1 << 17)
#define DW_DMA_CTL_LO_DST_SCATTER_EN      (1 << 18)
#define DW_DMA_CTL_LO_TT_FC_SHIFT         20
#define DW_DMA_CTL_LO_LLP_DST_EN          (1 << 27)
#define DW_DMA_CTL_LO_LLP_SRC_EN          (1 << 28)
#define DW_DMA_CTL_HI_BLOCK_TS_MASK       0xFFF

/* Configuration Register (CFG) Bit Fields */
#define DW_DMA_CFG_LO_CH_PRIOR_SHIFT      5
#define DW_DMA_CFG_LO_CH_SUSP             (1 << 8)
#define DW_DMA_CFG_LO_FIFO_EMPTY          (1 << 9)
#define DW_DMA_CFG_LO_HS_SEL_DST          (1 << 10)
#define DW_DMA_CFG_LO_HS_SEL_SRC          (1 << 11)
#define DW_DMA_CFG_LO_DST_HS_POL          (1 << 18)
#define DW_DMA_CFG_LO_SRC_HS_POL          (1 << 19)
#define DW_DMA_CFG_HI_FCMODE              (1 << 0)
#define DW_DMA_CFG_HI_FIFO_MODE           (1 << 1)
#define DW_DMA_CFG_HI_PROTCTL_SHIFT       2
#define DW_DMA_CFG_HI_SRC_PER_SHIFT       7
#define DW_DMA_CFG_HI_DEST_PER_SHIFT      11

/* Reserved bit masks */
#define DW_DMA_CTL_LO_RESERVED            (1 << 19)
#define DW_DMA_CTL_HI_RESERVED            (0x7FFFF << 13)
#define DW_DMA_CFG_LO_RESERVED            (0x1F | 1 << 9)
#define DW_DMA_CFG_HI_RESERVED            (0x1FFFF << 15)

/* Helper macro for write-enable registers */
#define UPDATE_REG_WITH_WE(reg, value) \
    do { \
        uint32_t we = ((value) >> 8) & 0xFF; \
        (reg) = ((reg) & ~we) | (we & ((value) & 0xFF)); \
    } while (0)

/* Decode transfer width to bytes */
static uint32_t dw_dma_ahb_decode_width(uint32_t encoded_width)
{
    switch (encoded_width) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 4;
    case 3: return 8;
    case 4: return 16;
    case 5 ... 7: return 32;
    default: return 1;
    }
}

/* Decode burst length */
static uint32_t dw_dma_ahb_decode_msize(uint32_t encoded_msize)
{
    switch (encoded_msize) {
    case 0: return 1;
    case 1: return 4;
    case 2: return 8;
    case 3: return 16;
    case 4: return 32;
    case 5: return 64;
    case 6: return 128;
    case 7: return 256;
    default: return 1;
    }
}

/* Encode width from bytes to register value */
static uint32_t dw_dma_ahb_encode_width(uint32_t width_bytes)
{
    switch (width_bytes) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    case 16: return 4;
    case 32: return 5;
    default: return 0;
    }
}

/* Encode msize from items to register value */
static uint32_t dw_dma_ahb_encode_msize(uint32_t msize_items)
{
    switch (msize_items) {
    case 1: return 0;
    case 4: return 1;
    case 8: return 2;
    case 16: return 3;
    case 32: return 4;
    case 64: return 5;
    case 128: return 6;
    case 256: return 7;
    default: return 0;
    }
}

static bool dw_dma_ahb_get_sw_hs_req(dw_dma_state *s, int channel, bool is_src)
{
    uint32_t reg = is_src ? s->req_src_reg : s->req_dst_reg;
    return (reg & (1u << channel)) != 0;
}

static bool dw_dma_ahb_get_sw_hs_sgl(dw_dma_state *s, int channel, bool is_src)
{
    uint32_t reg = is_src ? s->sgl_req_src_reg : s->sgl_req_dst_reg;
    return (reg & (1u << channel)) != 0;
}

static bool dw_dma_ahb_get_sw_hs_lst(dw_dma_state *s, int channel, bool is_src)
{
    uint32_t reg = is_src ? s->lst_src_reg : s->lst_dst_reg;
    return (reg & (1u << channel)) != 0;
}

static void dw_dma_ahb_clear_sw_hs(dw_dma_state *s, int channel)
{
    uint32_t channel_mask = 1u << channel;
    s->req_src_reg     &= ~channel_mask;
    s->sgl_req_src_reg &= ~channel_mask;
    s->req_dst_reg     &= ~channel_mask;
    s->sgl_req_dst_reg &= ~channel_mask;
}

/* AHB-specific interrupt update function */
static void dw_dma_ahb_update_int(dw_dma_state *s)
{
    /*
     * AHB interrupt flow (2-level enable):
     * 1. Raw interrupt status (block_int, tfr_int, etc.)
     * 2. Channel-level int_en (CTLx.INT_EN)
     * 3. Global interrupt type masks (block_int_mask, tfr_int_mask, etc.)
     * 4. StatusInt (combined 5-bit status register)
     *
     * Note: AHB does NOT have:
     * - Per-interrupt-type intstatus_enable/intsignal_enable
     * - Global interrupt enable (s->int_en)
     * - Common register interrupts
     */

    /* Calculate channel-level interrupt enable mask */
    uint32_t enabled_mask = 0;
    for (int i = 0; i < s->num_channels; i++) {
        if (s->chan[i].int_en) {
            enabled_mask |= (1u << i);
        }
    }

    /* Calculate final interrupt status for each type */
    uint32_t status_block = s->block_int   & s->block_int_mask   & enabled_mask;
    uint32_t status_tfr   = s->tfr_int     & s->tfr_int_mask     & enabled_mask;
    uint32_t status_src   = s->srctran_int & s->srctran_int_mask & enabled_mask;
    uint32_t status_dst   = s->dsttran_int & s->dsttran_int_mask & enabled_mask;
    uint32_t status_err   = s->err_int     & s->err_int_mask     & enabled_mask;

    /* Combine into 5-bit status register (StatusInt) */
    s->status_int = 0;
    if (status_block) {
        s->status_int |= (1 << 0);  /* BLOCK_TFR_DONE */
    }
    if (status_tfr) {
        s->status_int |= (1 << 1);  /* DMA_TFR_DONE */
    }
    if (status_src) {
        s->status_int |= (1 << 2);  /* SRC_TRANSCOMP */
    }
    if (status_dst) {
        s->status_int |= (1 << 3);  /* DST_TRANSCOMP */
    }
    if (status_err) {
        s->status_int |= (1 << 4);  /* ERR */
    }

    /* Update IRQ lines - AHB has no global enable, so directly drive IRQ */
    bool irq_active = (s->status_int != 0);
    qemu_set_irq(s->irq, irq_active);
    qemu_set_irq(s->clic_irq, irq_active);
}

static const dw_dma_bus_ops dw_dma_ahb_ops = {
    .bus_type = "AHB",
    .get_sw_hs_req = dw_dma_ahb_get_sw_hs_req,
    .get_sw_hs_sgl = dw_dma_ahb_get_sw_hs_sgl,
    .get_sw_hs_lst = dw_dma_ahb_get_sw_hs_lst,
    .clear_sw_hs = dw_dma_ahb_clear_sw_hs,
    .update_int = dw_dma_ahb_update_int,
};

static const VMStateDescription vmstate_dw_dma_channel_ahb = {
    .name = "dw_dma_channel_ahb",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(src, dw_dma_channel),
        VMSTATE_UINT64(dest, dw_dma_channel),
        VMSTATE_UINT64(llp, dw_dma_channel),
        VMSTATE_UINT32(block_ts, dw_dma_channel),
        VMSTATE_UINT32(src_tr_width, dw_dma_channel),
        VMSTATE_UINT32(dst_tr_width, dw_dma_channel),
        VMSTATE_UINT32(src_msize, dw_dma_channel),
        VMSTATE_UINT32(dst_msize, dw_dma_channel),
        VMSTATE_UINT32(sinc, dw_dma_channel),
        VMSTATE_UINT32(dinc, dw_dma_channel),
        VMSTATE_UINT32(tt_fc, dw_dma_channel),
        VMSTATE_BOOL(int_en, dw_dma_channel),
        VMSTATE_BOOL(src_gather_en, dw_dma_channel),
        VMSTATE_BOOL(dst_scatter_en, dw_dma_channel),
        VMSTATE_BOOL(llp_dst_en, dw_dma_channel),
        VMSTATE_BOOL(llp_src_en, dw_dma_channel),
        VMSTATE_UINT32(ch_prior, dw_dma_channel),
        VMSTATE_BOOL(hs_sel_src, dw_dma_channel),
        VMSTATE_BOOL(hs_sel_dst, dw_dma_channel),
        VMSTATE_BOOL(src_hs_pol, dw_dma_channel),
        VMSTATE_BOOL(dst_hs_pol, dw_dma_channel),
        VMSTATE_UINT32(src_per, dw_dma_channel),
        VMSTATE_UINT32(dest_per, dw_dma_channel),
        VMSTATE_UINT32(sgr, dw_dma_channel),
        VMSTATE_UINT32(dsr, dw_dma_channel),
        VMSTATE_UINT32(lms, dw_dma_channel),
        VMSTATE_UINT32(block_size, dw_dma_channel),
        VMSTATE_INT32(chan_enable, dw_dma_channel),
        VMSTATE_UINT32(state, dw_dma_channel),
        VMSTATE_UINT64(current_src_addr, dw_dma_channel),
        VMSTATE_UINT64(current_dest_addr, dw_dma_channel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_dw_dma_ahb = {
    .name = "dw_dma_sw_ahb",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(dma_enable, dw_dma_state),
        VMSTATE_INT32(int_en, dw_dma_state),
        VMSTATE_UINT32(tfr_int, dw_dma_state),
        VMSTATE_UINT32(block_int, dw_dma_state),
        VMSTATE_UINT32(srctran_int, dw_dma_state),
        VMSTATE_UINT32(dsttran_int, dw_dma_state),
        VMSTATE_UINT32(err_int, dw_dma_state),
        VMSTATE_UINT32(tfr_int_mask, dw_dma_state),
        VMSTATE_UINT32(block_int_mask, dw_dma_state),
        VMSTATE_UINT32(srctran_int_mask, dw_dma_state),
        VMSTATE_UINT32(dsttran_int_mask, dw_dma_state),
        VMSTATE_UINT32(err_int_mask, dw_dma_state),
        VMSTATE_UINT32(status_int, dw_dma_state),
        VMSTATE_UINT32(req_src_reg, dw_dma_state),
        VMSTATE_UINT32(req_dst_reg, dw_dma_state),
        VMSTATE_UINT32(sgl_req_src_reg, dw_dma_state),
        VMSTATE_UINT32(sgl_req_dst_reg, dw_dma_state),
        VMSTATE_UINT32(lst_src_reg, dw_dma_state),
        VMSTATE_UINT32(lst_dst_reg, dw_dma_state),
        VMSTATE_UINT32_ARRAY(dma_comp_params, dw_dma_state, 6),
        VMSTATE_UINT32(num_channels, dw_dma_state),
        VMSTATE_STRUCT_VARRAY_UINT32(chan, dw_dma_state, num_channels, 0,
                                     vmstate_dw_dma_channel_ahb, dw_dma_channel),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t dw_dma_read_comp_params(dw_dma_state *s, hwaddr offset, unsigned size)
{
    switch (offset) {
    case DW_DMA_CFG_REG_CompParams1:
        return s->dma_comp_params[0];
    case DW_DMA_CFG_REG_CompParams2:
        return s->dma_comp_params[1];
    case DW_DMA_CFG_REG_CompParams3:
        return s->dma_comp_params[2];
    case DW_DMA_CFG_REG_CompParams4:
        return s->dma_comp_params[3];
    case DW_DMA_CFG_REG_CompParams5:
        return s->dma_comp_params[4];
    case DW_DMA_CFG_REG_CompParams6:
        return s->dma_comp_params[5];
    case DW_DMA_CFG_REG_CompType:
        if (size == 8) {
            return ((uint64_t)0x3232342A << 32) | 0x44571110;
        }
        return 0x44571110;
    case DW_DMA_CFG_REG_CompVersion:
        return 0x3232342A;
    default:
        return 0;
    }
}

static uint64_t dw_dma_ahb_read(void *opaque, hwaddr offset, unsigned size)
{
    dw_dma_state *s = (dw_dma_state *)opaque;
    dw_dma_channel *ch;
    unsigned int channel;
    uint64_t ret = 0;

    if (size != 4 && size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid access size %d at offset 0x%lx\n",
                      __func__, size, offset);
        return 0;
    }

    switch (offset) {
    /* Channel registers */
    case 0x000 ... 0x2BF:
        channel = offset / DW_DMA_CHAN_REGS_SIZE;
        if (channel >= DW_DMA_CHANS) {
            return 0;
        }
        ch = &s->chan[channel];

        switch (offset % DW_DMA_CHAN_REGS_SIZE) {
        case DW_DMA_CHAN_REG_SAR:
            ret = ch->src;
            break;
        case DW_DMA_CHAN_REG_DAR:
            ret = ch->dest;
            break;
        case DW_DMA_CHAN_REG_LLP:
            ret = ch->llp | ch->lms;
            break;
        case DW_DMA_CHAN_REG_CTL_LO:
            /* Build CTL_LO from channel fields */
            ret = 0;
            if (ch->int_en) {
                ret |= DW_DMA_CTL_LO_INT_EN;
            }
            ret |= (dw_dma_ahb_encode_width(ch->dst_tr_width) << DW_DMA_CTL_LO_DST_TR_WIDTH_SHIFT);
            ret |= (dw_dma_ahb_encode_width(ch->src_tr_width) << DW_DMA_CTL_LO_SRC_TR_WIDTH_SHIFT);
            ret |= (ch->dinc << DW_DMA_CTL_LO_DINC_SHIFT);
            ret |= (ch->sinc << DW_DMA_CTL_LO_SINC_SHIFT);
            ret |= (dw_dma_ahb_encode_msize(ch->dst_msize) << DW_DMA_CTL_LO_DEST_MSIZE_SHIFT);
            ret |= (dw_dma_ahb_encode_msize(ch->src_msize) << DW_DMA_CTL_LO_SRC_MSIZE_SHIFT);
            if (ch->src_gather_en) {
                ret |= DW_DMA_CTL_LO_SRC_GATHER_EN;
            }
            if (ch->dst_scatter_en) {
                ret |= DW_DMA_CTL_LO_DST_SCATTER_EN;
            }
            ret |= (ch->tt_fc << DW_DMA_CTL_LO_TT_FC_SHIFT);
            if (ch->llp_dst_en) {
                ret |= DW_DMA_CTL_LO_LLP_DST_EN;
            }
            if (ch->llp_src_en) {
                ret |= DW_DMA_CTL_LO_LLP_SRC_EN;
            }

            if (size == 8) {
                /* Include CTL_HI */
                ret |= ((uint64_t)(ch->block_ts & DW_DMA_CTL_HI_BLOCK_TS_MASK) << 32);
            }
            break;
        case DW_DMA_CHAN_REG_CTL_HI:
            ret = ch->block_ts & DW_DMA_CTL_HI_BLOCK_TS_MASK;
            break;
        case DW_DMA_CHAN_REG_SSTAT:
        case DW_DMA_CHAN_REG_DSTAT:
            ret = ch->state;
            break;
        case DW_DMA_CHAN_REG_SSTATAR:
            ret = ch->current_src_addr & 0xFFFFFFFF;
            break;
        case DW_DMA_CHAN_REG_DSTATAR:
            ret = ch->current_dest_addr & 0xFFFFFFFF;
            break;
        case DW_DMA_CHAN_REG_CFG_LO:
            /* Build CFG_LO from channel fields */
            ret = (ch->ch_prior << DW_DMA_CFG_LO_CH_PRIOR_SHIFT);
            if (ch->ch_susp) {
                ret |= DW_DMA_CFG_LO_CH_SUSP;
            }
            ret |= DW_DMA_CFG_LO_FIFO_EMPTY;  /* Always set FIFO_EMPTY */
            ret |= DW_DMA_CFG_LO_HS_SEL_SRC | DW_DMA_CFG_LO_HS_SEL_DST;  /* Force SW handshake */
            if (ch->dst_hs_pol) {
                ret |= DW_DMA_CFG_LO_DST_HS_POL;
            }
            if (ch->src_hs_pol) {
                ret |= DW_DMA_CFG_LO_SRC_HS_POL;
            }

            if (size == 8) {
                /* Include CFG_HI */
                uint32_t cfg_hi = 0;
                cfg_hi |= (ch->src_per << DW_DMA_CFG_HI_SRC_PER_SHIFT);
                cfg_hi |= (ch->dest_per << DW_DMA_CFG_HI_DEST_PER_SHIFT);
                ret |= ((uint64_t)cfg_hi << 32);
            }
            break;
        case DW_DMA_CHAN_REG_CFG_HI:
            ret = 0;
            ret |= (ch->src_per << DW_DMA_CFG_HI_SRC_PER_SHIFT);
            ret |= (ch->dest_per << DW_DMA_CFG_HI_DEST_PER_SHIFT);
            break;
        case DW_DMA_CHAN_REG_SGR:
            ret = ch->sgr;
            break;
        case DW_DMA_CHAN_REG_DSR:
            ret = ch->dsr;
            break;
        default:
            break;
        }
        break;

    /* Interrupt Registers */
    case DW_DMA_INT_REG_RawTfr:
        ret = s->tfr_int;
        break;
    case DW_DMA_INT_REG_RawBlock:
        ret = s->block_int;
        break;
    case DW_DMA_INT_REG_RawSrcTran:
        ret = s->srctran_int;
        break;
    case DW_DMA_INT_REG_RawDstTran:
        ret = s->dsttran_int;
        break;
    case DW_DMA_INT_REG_RawErr:
        ret = s->err_int;
        break;
    case DW_DMA_INT_REG_StatusTfr:
    case DW_DMA_INT_REG_StatusBlock:
    case DW_DMA_INT_REG_StatusSrcTran:
    case DW_DMA_INT_REG_StatusDstTran:
    case DW_DMA_INT_REG_StatusErr:
        {
            uint32_t int_en_mask = 0;
            for (int i = 0; i < DW_DMA_CHANS; i++) {
                if (s->chan[i].int_en) {
                    int_en_mask |= (1u << i);
                }
            }
            if (offset == DW_DMA_INT_REG_StatusTfr)
                ret = s->tfr_int & s->tfr_int_mask & int_en_mask;
            else if (offset == DW_DMA_INT_REG_StatusBlock)
                ret = s->block_int & s->block_int_mask & int_en_mask;
            else if (offset == DW_DMA_INT_REG_StatusSrcTran)
                ret = s->srctran_int & s->srctran_int_mask & int_en_mask;
            else if (offset == DW_DMA_INT_REG_StatusDstTran)
                ret = s->dsttran_int & s->dsttran_int_mask & int_en_mask;
            else if (offset == DW_DMA_INT_REG_StatusErr)
                ret = s->err_int & s->err_int_mask & int_en_mask;
        }
        break;
    case DW_DMA_INT_REG_MaskTfr:
        ret = s->tfr_int_mask;
        break;
    case DW_DMA_INT_REG_MaskBlock:
        ret = s->block_int_mask;
        break;
    case DW_DMA_INT_REG_MaskSrcTran:
        ret = s->srctran_int_mask;
        break;
    case DW_DMA_INT_REG_MaskDstTran:
        ret = s->dsttran_int_mask;
        break;
    case DW_DMA_INT_REG_MaskErr:
        ret = s->err_int_mask;
        break;
    case DW_DMA_INT_REG_StatusInt:
        ret = s->status_int;
        break;

    /* Software Handshake Registers */
    case DW_DMA_SW_HS_REG_ReqSrcReg:
        ret = s->req_src_reg;
        break;
    case DW_DMA_SW_HS_REG_ReqDstReg:
        ret = s->req_dst_reg;
        break;
    case DW_DMA_SW_HS_REG_SglReqSrcReg:
        ret = s->sgl_req_src_reg;
        break;
    case DW_DMA_SW_HS_REG_SglReqDstReg:
        ret = s->sgl_req_dst_reg;
        break;
    case DW_DMA_SW_HS_REG_LstSrcReg:
        ret = s->lst_src_reg;
        break;
    case DW_DMA_SW_HS_REG_LstDstReg:
        ret = s->lst_dst_reg;
        break;

    /* Configuration Registers */
    case DW_DMA_CFG_REG_DmaCfgReg:
        ret = s->dma_enable;
        break;
    case DW_DMA_CFG_REG_ChEnReg:
        ret = 0;
        for (int i = 0; i < DW_DMA_CHANS; i++) {
            if (s->chan[i].chan_enable) {
                ret |= (1u << i);
            }
        }
        break;
    case DW_DMA_CFG_REG_DmaIdReg:
        ret = 0x02080901;
        break;
    case DW_DMA_CFG_REG_DmaTestReg:
        ret = 0;
        break;
    case DW_DMA_CFG_REG_CompParams6:
    case DW_DMA_CFG_REG_CompParams5:
    case DW_DMA_CFG_REG_CompParams4:
    case DW_DMA_CFG_REG_CompParams3:
    case DW_DMA_CFG_REG_CompParams2:
    case DW_DMA_CFG_REG_CompParams1:
    case DW_DMA_CFG_REG_CompType:
    case DW_DMA_CFG_REG_CompVersion:
        ret = dw_dma_read_comp_params(s, offset, size);
        break;

    default:
        break;
    }

    return ret;
}

static void dw_dma_ahb_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    dw_dma_state *s = (dw_dma_state *)opaque;
    dw_dma_channel *ch;
    unsigned int channel;
    uint32_t tmp;

    if (size != 4 && size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid access size %d at offset 0x%lx\n",
                      __func__, size, offset);
        return;
    }

    /* Set IOPMP requestor ID from current CPU if available */
    if (current_cpu) {
        s->rrid = cpu_env(current_cpu)->xt_vmid;
    }

    switch (offset) {
    /* Channel registers */
    case 0x000 ... 0x2BF:
        channel = offset / DW_DMA_CHAN_REGS_SIZE;
        if (channel >= DW_DMA_CHANS) {
            return;
        }
        ch = &s->chan[channel];

        switch (offset % DW_DMA_CHAN_REGS_SIZE) {
        case DW_DMA_CHAN_REG_SAR:
            if (size == 8) {
                ch->src = value;
            } else {
                ch->src = value & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_DAR:
            if (size == 8) {
                ch->dest = value;
            } else {
                ch->dest = value & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_LLP:
            ch->llp = value & 0xFFFFFFFC;
            ch->lms = value & 0x3;
            break;
        case DW_DMA_CHAN_REG_CTL_LO:
            {
                uint32_t ctl_lo = value & 0xFFFFFFFF & ~DW_DMA_CTL_LO_RESERVED;
                uint32_t ctl_hi = (size == 8) ? ((value >> 32) & ~DW_DMA_CTL_HI_RESERVED) : ch->block_ts;

                if (ctl_lo & DW_DMA_CTL_LO_SRC_GATHER_EN) {
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Src Gather not supported\n", __func__);
                    return;
                }
                if (ctl_lo & DW_DMA_CTL_LO_DST_SCATTER_EN) {
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Dst Scatter not supported\n", __func__);
                    return;
                }

                /* Parse CTL_LO to channel fields */
                ch->int_en = (ctl_lo & DW_DMA_CTL_LO_INT_EN) != 0;
                ch->dst_tr_width = dw_dma_ahb_decode_width((ctl_lo >> DW_DMA_CTL_LO_DST_TR_WIDTH_SHIFT) & 0x7);
                ch->src_tr_width = dw_dma_ahb_decode_width((ctl_lo >> DW_DMA_CTL_LO_SRC_TR_WIDTH_SHIFT) & 0x7);
                ch->dinc = (ctl_lo >> DW_DMA_CTL_LO_DINC_SHIFT) & 0x3;
                ch->sinc = (ctl_lo >> DW_DMA_CTL_LO_SINC_SHIFT) & 0x3;
                ch->dst_msize = dw_dma_ahb_decode_msize((ctl_lo >> DW_DMA_CTL_LO_DEST_MSIZE_SHIFT) & 0x7);
                ch->src_msize = dw_dma_ahb_decode_msize((ctl_lo >> DW_DMA_CTL_LO_SRC_MSIZE_SHIFT) & 0x7);
                ch->src_gather_en = (ctl_lo & DW_DMA_CTL_LO_SRC_GATHER_EN) != 0;
                ch->dst_scatter_en = (ctl_lo & DW_DMA_CTL_LO_DST_SCATTER_EN) != 0;
                ch->tt_fc = (ctl_lo >> DW_DMA_CTL_LO_TT_FC_SHIFT) & 0x7;
                ch->llp_dst_en = (ctl_lo & DW_DMA_CTL_LO_LLP_DST_EN) != 0;
                ch->llp_src_en = (ctl_lo & DW_DMA_CTL_LO_LLP_SRC_EN) != 0;

                /* Parse CTL_HI */
                ch->block_ts = ctl_hi & DW_DMA_CTL_HI_BLOCK_TS_MASK;
                if (ch->state != DW_DMA_CH_TRANSFERRING) {
                    ch->block_size = ch->block_ts;
                }
            }
            break;
        case DW_DMA_CHAN_REG_CTL_HI:
            {
                uint32_t ctl_hi = value & ~DW_DMA_CTL_HI_RESERVED;
                ch->block_ts = ctl_hi & DW_DMA_CTL_HI_BLOCK_TS_MASK;
                if (ch->state != DW_DMA_CH_TRANSFERRING) {
                    ch->block_size = ch->block_ts;
                }
            }
            break;
        case DW_DMA_CHAN_REG_CFG_LO:
            /* Force software handshaking */
            if (!(value & DW_DMA_CFG_LO_HS_SEL_SRC && value & DW_DMA_CFG_LO_HS_SEL_DST)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Only software handshaking supported\n", __func__);
                return;
            }
            {
                uint32_t cfg_lo = value & ~DW_DMA_CFG_LO_RESERVED;
                ch->ch_prior = (cfg_lo >> DW_DMA_CFG_LO_CH_PRIOR_SHIFT) & 0x7;
                ch->ch_susp = (cfg_lo & DW_DMA_CFG_LO_CH_SUSP) != 0;
                ch->hs_sel_src = true;  /* Force SW */
                ch->hs_sel_dst = true;  /* Force SW */
                ch->dst_hs_pol = (cfg_lo & DW_DMA_CFG_LO_DST_HS_POL) != 0;
                ch->src_hs_pol = (cfg_lo & DW_DMA_CFG_LO_SRC_HS_POL) != 0;
            }
            break;
        case DW_DMA_CHAN_REG_CFG_HI:
            {
                uint32_t cfg_hi = value & ~DW_DMA_CFG_HI_RESERVED;
                ch->src_per = (cfg_hi >> DW_DMA_CFG_HI_SRC_PER_SHIFT) & 0xF;
                ch->dest_per = (cfg_hi >> DW_DMA_CFG_HI_DEST_PER_SHIFT) & 0xF;
                /* Force software handshaking */
                ch->hs_sel_src = true;
                ch->hs_sel_dst = true;
            }
            break;
        case DW_DMA_CHAN_REG_SGR:
            ch->sgr = value;
            break;
        case DW_DMA_CHAN_REG_DSR:
            ch->dsr = value;
            break;
        default:
            break;
        }
        break;

    /* Interrupt Registers */
    case DW_DMA_INT_REG_MaskTfr:
        UPDATE_REG_WITH_WE(s->tfr_int_mask, value);
        break;
    case DW_DMA_INT_REG_MaskBlock:
        UPDATE_REG_WITH_WE(s->block_int_mask, value);
        break;
    case DW_DMA_INT_REG_MaskSrcTran:
        UPDATE_REG_WITH_WE(s->srctran_int_mask, value);
        break;
    case DW_DMA_INT_REG_MaskDstTran:
        UPDATE_REG_WITH_WE(s->dsttran_int_mask, value);
        break;
    case DW_DMA_INT_REG_MaskErr:
        UPDATE_REG_WITH_WE(s->err_int_mask, value);
        break;
    case DW_DMA_INT_REG_ClearTfr:
        s->tfr_int &= ~(value & 0xFF);
        break;
    case DW_DMA_INT_REG_ClearBlock:
        s->block_int &= ~(value & 0xFF);
        break;
    case DW_DMA_INT_REG_ClearSrcTran:
        s->srctran_int &= ~(value & 0xFF);
        break;
    case DW_DMA_INT_REG_ClearDstTran:
        s->dsttran_int &= ~(value & 0xFF);
        break;
    case DW_DMA_INT_REG_ClearErr:
        s->err_int &= ~(value & 0xFF);
        break;

    /* Software Handshake Registers */
    case DW_DMA_SW_HS_REG_ReqSrcReg:
        dw_dma_write_sw_regs(s, &s->req_src_reg, value);
        break;
    case DW_DMA_SW_HS_REG_ReqDstReg:
        dw_dma_write_sw_regs(s, &s->req_dst_reg, value);
        break;
    case DW_DMA_SW_HS_REG_SglReqSrcReg:
        dw_dma_write_sw_regs(s, &s->sgl_req_src_reg, value);
        break;
    case DW_DMA_SW_HS_REG_SglReqDstReg:
        dw_dma_write_sw_regs(s, &s->sgl_req_dst_reg, value);
        break;
    case DW_DMA_SW_HS_REG_LstSrcReg:
        dw_dma_write_sw_regs_lst(s, &s->lst_src_reg, value);
        break;
    case DW_DMA_SW_HS_REG_LstDstReg:
        dw_dma_write_sw_regs_lst(s, &s->lst_dst_reg, value);
        break;

    /* Configuration Registers */
    case DW_DMA_CFG_REG_DmaCfgReg:
        s->dma_enable = value & 0x1;
        if (!s->dma_enable) {
            for (int i = 0; i < DW_DMA_CHANS; i++) {
                if (s->chan[i].state != DW_DMA_CH_TRANSFERRING) {
                    s->chan[i].chan_enable = 0;
                    s->chan[i].state = DW_DMA_CH_DISABLED;
                }
            }
        }
        break;
    case DW_DMA_CFG_REG_ChEnReg:
        if (s->dma_enable) {
            tmp = (value >> 8) & 0xFF;
            value &= 0xFF;
            for (int i = 0; i < DW_DMA_CHANS; i++) {
                if (tmp & (1u << i)) {
                    if (value & (1u << i)) {
                        s->chan[i].chan_enable = 1;
                        s->chan[i].state = DW_DMA_CH_ENABLED;
                        dw_dma_do_transfer(s, i);
                    } else {
                        s->chan[i].chan_enable = 0;
                        s->chan[i].state = DW_DMA_CH_DISABLED;
                    }
                }
            }
        }
        break;

    default:
        break;
    }

    dw_dma_update_int(s);
}

static const MemoryRegionOps dw_dma_ahb_ops_mem = {
    .read = dw_dma_ahb_read,
    .write = dw_dma_ahb_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void dw_dma_ahb_reset(DeviceState *dev)
{
    dw_dma_state *s = DW_DMA_SW_AHB(dev);
    dw_dma_common_reset(s);
}

static void dw_dma_ahb_realize(DeviceState *dev, Error **errp)
{
    dw_dma_state *s = DW_DMA_SW_AHB(dev);

    s->num_channels = DW_DMA_AHB_NUM_CHANNELS;
    dw_dma_common_realize(s, &dw_dma_ahb_ops, errp);

    memory_region_init_io(&s->iomem, OBJECT(s), &dw_dma_ahb_ops_mem, s,
                          TYPE_DW_DMA_SW_AHB, DW_DMA_AHB_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);

    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->clic_irq);
}

static Property dw_dma_ahb_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void dw_dma_ahb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = dw_dma_ahb_realize;
    dc->reset = dw_dma_ahb_reset;
    dc->vmsd = &vmstate_dw_dma_ahb;
    device_class_set_props(dc, dw_dma_ahb_properties);
    dc->desc = "DesignWare AHB DMA Controller (8 channels, SW Handshake Only)";
}

static const TypeInfo dw_dma_ahb_info = {
    .name = TYPE_DW_DMA_SW_AHB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(dw_dma_state),
    .class_init = dw_dma_ahb_class_init,
};

static void dw_dma_ahb_register_types(void)
{
    type_register_static(&dw_dma_ahb_info);
}

type_init(dw_dma_ahb_register_types)
