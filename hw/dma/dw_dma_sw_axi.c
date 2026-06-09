/*
 * DW AXI DMAC controller - AXI Bus Adapter
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

#define TYPE_DW_DMA_SW_AXI   "dw_dma_sw_axi"
#define DW_DMA_SW_AXI(obj)   OBJECT_CHECK(dw_dma_state, (obj), TYPE_DW_DMA_SW_AXI)

/* AXI supports up to 16 channels */
#define DW_DMA_AXI_NUM_CHANNELS  8
#define DW_DMA_AXI_REG_SIZE      0x2100

/* Common Registers (0x0 - 0x98) */
#define DW_DMA_REG_ID                0x0
#define DW_DMA_REG_COMPVER           0x8
#define DW_DMA_REG_CFG               0x10
/*
 * DMAC_ChEnReg (0x18): Channel Enable Register (for DMAX_NUM_CHANNELS <= 8)
 * DMAC_ChEnReg2 (0x18): Channel Enable Register 2 (for DMAX_NUM_CHANNELS > 8)
 *
 * Both registers share the same offset 0x18, but have different layouts:
 *
 * DMAC_ChEnReg (when DMAX_NUM_CHANNELS <= 8):
 *   Bits [7:0]:   CH1_EN ~ CH8_EN (Channel Enable)
 *   Bits [15:8]:  CH1_EN_WE ~ CH8_EN_WE (Write Enable, write-only)
 *   Bits [23:16]: CH1_SUSP ~ CH8_SUSP (Channel Suspend)
 *   Bits [31:24]: CH1_SUSP_WE ~ CH8_SUSP_WE (Suspend Write Enable, write-only)
 *   Bits [39:32]: CH1_ABORT ~ CH8_ABORT (Channel Abort)
 *   Bits [47:40]: CH1_ABORT_WE ~ CH8_ABORT_WE (Abort Write Enable, write-only)
 *
 * DMAC_ChEnReg2 (when DMAX_NUM_CHANNELS > 8):
 *   Bits [15:0]:  CH1_EN ~ CH16_EN (Channel 1-16 Enable)
 *   Bits [31:16]: CH1_EN_WE ~ CH16_EN_WE (Write Enable, write-only)
 *   Bits [47:32]: CH17_EN ~ CH32_EN (Channel 17-32 Enable)
 *   Bits [63:48]: CH17_EN_WE ~ CH32_EN_WE (Write Enable, write-only)
 *
 * DMAX_CHEN_REORG parameter impact:
 * - When DMAX_CHEN_REORG = 0 (default, our implementation):
 *   * This register is read-write (R/W)
 *   * Uses Write-Enable mechanism: CHx_EN_WE bits control writing to CHx_EN bits
 *   * CHx_EN registers (offset 0x1A8) do NOT exist
 *   * This is the ONLY way to enable/disable channels
 *
 * - When DMAX_CHEN_REORG = 1 (not implemented):
 *   * This register becomes read-only (RO)
 *   * Channel enable must be controlled via individual CHx_EN registers (offset 0x1A8)
 *   * CHx_EN registers (offset 0x1A8) exist and are read-write
 */

#define DW_DMA_REG_CHEN              0x18  /* For DMAX_NUM_CHANNELS <= 8 */
#define DW_DMA_REG_CHEN2             0x18  /* For DMAX_NUM_CHANNELS > 8 */
#define DW_DMA_REG_CHSUSP            0x20
#define DW_DMA_REG_CHABORT           0x28
#define DW_DMA_REG_INTSTATUS         0x30
#define DW_DMA_REG_INTCLEAR          0x38
#define DW_DMA_REG_COMMONREG_INTSTATUS_ENABLE  0x40
#define DW_DMA_REG_COMMONREG_INTSIGNAL_ENABLE  0x48
#define DW_DMA_REG_COMMONREG_INTSTATUSREG      0x50
#define DW_DMA_REG_RESETREG          0x58

/* Channel Registers Base (starts at 0x100) */
#define DW_DMA_CHAN_REG_BASE         0x100
#define DW_DMA_CHAN_REGS_SIZE        0x100

/* Channel Register Offsets (relative to channel base) */
#define DW_DMA_CHAN_REG_SAR                 0x0
#define DW_DMA_CHAN_REG_DAR                 0x8
#define DW_DMA_CHAN_REG_BLOCK_TS            0x10
#define DW_DMA_CHAN_REG_CTL                 0x18
#define DW_DMA_CHAN_REG_CFG                 0x20
#define DW_DMA_CHAN_REG_LLP                 0x28
#define DW_DMA_CHAN_REG_STATUS              0x30
#define DW_DMA_CHAN_REG_SWHSSRC             0x38
#define DW_DMA_CHAN_REG_SWHSDST             0x40
#define DW_DMA_CHAN_REG_BLK_TFR_RESUME      0x48
#define DW_DMA_CHAN_REG_AXI_ID              0x50
#define DW_DMA_CHAN_REG_AXI_QOS             0x58
#define DW_DMA_CHAN_REG_SSTAT               0x60
#define DW_DMA_CHAN_REG_DSTAT               0x68
#define DW_DMA_CHAN_REG_SSTATAR             0x70
#define DW_DMA_CHAN_REG_DSTATAR             0x78
#define DW_DMA_CHAN_REG_INTSTATUS_ENABLE    0x80
#define DW_DMA_CHAN_REG_INTSTATUS           0x88
#define DW_DMA_CHAN_REG_INTSIGNAL_ENABLE    0x90
#define DW_DMA_CHAN_REG_INTCLEAR            0x98
#define DW_DMA_CHAN_REG_CFG_EXTD            0xA0
/* Since we use DMAX_CHEN_REORG = 0, this register is NOT implemented. */
#define DW_DMA_CHAN_REG_EN            0xA8

/* DMAC Configuration Register (CFG) */
#define DW_DMA_CFG_INT_EN             (1 << 1)
#define DW_DMA_CFG_DMAC_EN            (1 << 0)

/* Control Register (CTL) register */
#define DW_DMA_CTL_SMS_SHIFT          0    /* Source Master Select */
#define DW_DMA_CTL_DMS_SHIFT          2    /* Destination Master Select */
#define DW_DMA_CTL_SINC_SHIFT         4    /* Source Address Increment */
#define DW_DMA_CTL_DINC_SHIFT         6    /* Destination Address Increment */
#define DW_DMA_CTL_SRC_TR_WIDTH_SHIFT 8    /* Source Transfer Width */
#define DW_DMA_CTL_DST_TR_WIDTH_SHIFT 11   /* Destination Transfer Width */
#define DW_DMA_CTL_SRC_MSIZE_SHIFT    14   /* Source Burst Length */
#define DW_DMA_CTL_DST_MSIZE_SHIFT    18   /* Destination Burst Length */
#define DW_DMA_CTL_ARLEN_EN           (1ULL << 38)  /* Source Burst Length Enable */
#define DW_DMA_CTL_ARLEN_SHIFT        39   /* Source Burst Length (0-255) */
#define DW_DMA_CTL_AWLEN_EN           (1ULL << 47)  /* Destination Burst Length Enable */
#define DW_DMA_CTL_AWLEN_SHIFT        48   /* Destination Burst Length (0-255) */
#define DW_DMA_CTL_SRC_STAT_EN        (1ULL << 56)  /* Source Status Enable */
#define DW_DMA_CTL_DST_STAT_EN        (1ULL << 57)  /* Destination Status Enable */
#define DW_DMA_CTL_IOC_BLKTFR         (1ULL << 58)  /* Block Transfer Complete Interrupt */
#define DW_DMA_CTL_SHADOWREG_OR_LLI_LAST   (1ULL << 62)  /* Last Shadow Register or LLI */
#define DW_DMA_CTL_SHADOWREG_OR_LLI_VALID  (1ULL << 63)  /* Shadow Register or LLI Valid */

/* Configuration Register (CFG) */
/* CFG_LO (bits 0-31) - most bits are reserved */
#define DW_DMA_CFG_SRC_MULTBLK_TYPE_SHIFT  0    /* Source Multi-Block Type (bits 0-1) */
#define DW_DMA_CFG_DST_MULTBLK_TYPE_SHIFT  2    /* Destination Multi-Block Type (bits 2-3) */

/* CHx_CFG - for DMAX_NUM_CHANNELS <= 8 */
#define DW_DMA_CFG_TT_FC_SHIFT     32   /* Transfer Type and Flow Controller (bits 34:32) */
#define DW_DMA_CFG_HS_SEL_SRC      (1ULL << 35)  /* Source Software Handshake (bit 35) */
#define DW_DMA_CFG_HS_SEL_DST      (1ULL << 36)  /* Destination Software Handshake (bit 36) */
#define DW_DMA_CFG_SRC_PER_SHIFT   39   /* Source Peripheral (bits 42:39) */
#define DW_DMA_CFG_DST_PER_SHIFT   44   /* Destination Peripheral (bits 47:44) */
#define DW_DMA_CFG_CH_PRIOR_SHIFT  49   /* Channel Priority (bits 51:49, 3 bits) */
#define DW_DMA_CFG_CH_PRIOR_MASK   0x7  /* 3-bit mask */
#define DW_DMA_CFG_LOCK_CH         (1ULL << 52)  /* Channel Lock (bit 52) */
#define DW_DMA_CFG_LOCK_CH_L_SHIFT 53   /* Channel Lock Level (bits 54:53) */
#define DW_DMA_CFG_SRC_OSR_LMT_SHIFT 55  /* Source Outstanding Request Limit (bits 58:55) */
#define DW_DMA_CFG_DST_OSR_LMT_SHIFT 59  /* Destination Outstanding Request Limit (bits 62:59) */

/* CHx_CFG2 - for DMAX_NUM_CHANNELS > 8 */
#define DW_DMA_CFG2_SRC_PER_SHIFT  4    /* Source Peripheral (bits x:4, in low 32 bits) */
#define DW_DMA_CFG2_DST_PER_SHIFT  11  /* Destination Peripheral (bits x:11, in low 32 bits) */
#define DW_DMA_CFG2_CH_PRIOR_SHIFT 47  /* Channel Priority (bits 51:47, 5 bits) */
#define DW_DMA_CFG2_CH_PRIOR_MASK  0x1F /* 5-bit mask */

/* Channel Status Register */
#define DW_DMA_STATUS_CMPLTD_BLK_TFR_SIZE_MASK  0x1FFFF

#define DW_DMA_CHEN2_CH1_16_EN_SHIFT      0   /* Bits 0-15: CH1_EN ~ CH16_EN */
#define DW_DMA_CHEN2_CH1_16_EN_WE_SHIFT   16  /* Bits 16-31: CH1_EN_WE ~ CH16_EN_WE */
#define DW_DMA_CHEN2_CH17_32_EN_SHIFT     32  /* Bits 32-47: CH17_EN ~ CH32_EN */
#define DW_DMA_CHEN2_CH17_32_EN_WE_SHIFT  48  /* Bits 48-63: CH17_EN_WE ~ CH32_EN_WE */

static void dw_dma_axi_reset(DeviceState *dev);

/* Decode transfer width to bytes (AXI uses different encoding) */
static uint32_t dw_dma_axi_decode_width(uint32_t encoded_width)
{
    switch (encoded_width) {
    case 0: return 1;    /* 8 bits */
    case 1: return 2;    /* 16 bits */
    case 2: return 4;    /* 32 bits */
    case 3: return 8;    /* 64 bits */
    case 4: return 16;   /* 128 bits */
    case 5: return 32;   /* 256 bits */
    case 6: return 64;   /* 512 bits */
    default: return 1;
    }
}

/* Decode burst length (AXI uses different encoding) */
static uint32_t dw_dma_axi_decode_msize(uint32_t encoded_msize)
{
    /* AXI: msize is actual burst length items (1, 4, 8, 16, ..., 1024) */
    switch (encoded_msize) {
    case 0: return 1;
    case 1: return 4;
    case 2: return 8;
    case 3: return 16;
    case 4: return 32;
    case 5: return 64;
    case 6: return 128;
    case 7: return 256;
    case 8: return 512;
    case 9: return 1024;
    default: return 1;
    }
}

/* Encode width from bytes to register value */
static uint32_t dw_dma_axi_encode_width(uint32_t width_bytes)
{
    switch (width_bytes) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    case 16: return 4;
    case 32: return 5;
    case 64: return 6;
    default: return 0;
    }
}

/* Encode msize from items to register value */
static uint32_t dw_dma_axi_encode_msize(uint32_t msize_items)
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
    case 512: return 8;
    case 1024: return 9;
    default: return 0;
    }
}

static bool dw_dma_axi_get_sw_hs_req(dw_dma_state *s, int channel, bool is_src)
{
    uint32_t reg = is_src ? s->chan[channel].sw_hs_src_reg :
                            s->chan[channel].sw_hs_dst_reg;
    return (reg & 0x1) != 0;  /* Bit 0 is the request bit */
}

static bool dw_dma_axi_get_sw_hs_sgl(dw_dma_state *s, int channel, bool is_src)
{
    uint32_t reg = is_src ? s->chan[channel].sw_hs_src_reg :
                            s->chan[channel].sw_hs_dst_reg;
    return (reg & 0x2) != 0;  /* Bit 1 is the single request bit */
}

static bool dw_dma_axi_get_sw_hs_lst(dw_dma_state *s, int channel, bool is_src)
{
    uint32_t reg = is_src ? s->chan[channel].sw_hs_src_reg :
                            s->chan[channel].sw_hs_dst_reg;
    return (reg & 0x4) != 0;  /* Bit 2 is the last request bit */
}

static void dw_dma_axi_clear_sw_hs(dw_dma_state *s, int channel)
{
    s->chan[channel].sw_hs_src_reg = 0;
    s->chan[channel].sw_hs_dst_reg = 0;
}

/* AXI-specific interrupt update function */
static void dw_dma_axi_update_int(dw_dma_state *s)
{
    /*
     * AXI interrupt flow (4-5 level enable):
     * 1. Raw interrupt status (block_int, tfr_int, etc.)
     * 2. Channel-level int_en (global channel interrupt enable)
     * 3. Per-interrupt-type intstatus_enable (bit 0=BLOCK, 1=TFR, 3=SRC, 4=DST, 5=ERR)
     * 4. Per-interrupt-type intsignal_enable (bit 0=BLOCK, 1=TFR, 3=SRC, 4=DST, 5=ERR)
     * 5. Global interrupt enable (s->int_en, DMAC_CFGREG bit 1)
     *
     * Additional: Common register interrupts
     */

    /* Calculate enabled interrupt mask for each interrupt type */
    uint32_t enabled_block_mask = 0;
    uint32_t enabled_tfr_mask = 0;
    uint32_t enabled_src_mask = 0;
    uint32_t enabled_dst_mask = 0;
    uint32_t enabled_err_mask = 0;

    for (int i = 0; i < s->num_channels; i++) {
        uint32_t ch_bit = (1u << i);
        uint32_t intstatus_en = s->chan[i].intstatus_enable;
        uint32_t intsignal_en = s->chan[i].intsignal_enable;

        /* Check each interrupt type individually */
        if ((intstatus_en & (1 << 0)) && (intsignal_en & (1 << 0))) {
            enabled_block_mask |= ch_bit;  /* BLOCK_TFR_DONE */
        }
        if ((intstatus_en & (1 << 1)) && (intsignal_en & (1 << 1))) {
            enabled_tfr_mask |= ch_bit;    /* DMA_TFR_DONE */
        }
        if ((intstatus_en & (1 << 3)) && (intsignal_en & (1 << 3))) {
            enabled_src_mask |= ch_bit;    /* SRC_TRANSCOMP */
        }
        if ((intstatus_en & (1 << 4)) && (intsignal_en & (1 << 4))) {
            enabled_dst_mask |= ch_bit;    /* DST_TRANSCOMP */
        }
        if ((intstatus_en & (1 << 5)) && (intsignal_en & (1 << 5))) {
            enabled_err_mask |= ch_bit;    /* ERR */
        }
    }

    /* Calculate final interrupt status for each type */
    uint32_t status_block = s->block_int   & enabled_block_mask;
    uint32_t status_tfr   = s->tfr_int     & enabled_tfr_mask;
    uint32_t status_src   = s->srctran_int & enabled_src_mask;
    uint32_t status_dst   = s->dsttran_int & enabled_dst_mask;
    uint32_t status_err   = s->err_int     & enabled_err_mask;

    /* Combine into 5-bit status register */
    s->status_int = 0;
    if (status_block) {
        /* BLOCK_TFR_DONE */
        s->status_int |= (1 << 0);
    }
    if (status_tfr) {
        /* DMA_TFR_DONE */
        s->status_int |= (1 << 1);
    }
    if (status_src) {
        /* SRC_TRANSCOMP */
        s->status_int |= (1 << 3);
    }
    if (status_dst) {
        /* DST_TRANSCOMP */
        s->status_int |= (1 << 4);
    }
    if (status_err) {
        /* ERR */
        s->status_int |= (1 << 5);
    }

    /* Check if any channel or common register interrupt is active */
    bool has_channel_int = (s->status_int != 0);
    bool has_common_int = (s->common_int_status != 0 &&
                          (s->common_intstatus_enable & 0x1) &&
                          (s->common_intsignal_enable & 0x1));

    /* Update IRQ lines - only if global interrupt enable (INT_EN) is set */
    bool irq_active = (has_channel_int || has_common_int) && s->int_en;
    qemu_set_irq(s->irq, irq_active);
    qemu_set_irq(s->clic_irq, irq_active);
}

static const dw_dma_bus_ops dw_dma_axi_ops = {
    .bus_type = "AXI",
    .get_sw_hs_req = dw_dma_axi_get_sw_hs_req,
    .get_sw_hs_sgl = dw_dma_axi_get_sw_hs_sgl,
    .get_sw_hs_lst = dw_dma_axi_get_sw_hs_lst,
    .clear_sw_hs = dw_dma_axi_clear_sw_hs,
    .update_int = dw_dma_axi_update_int,
};

/* ========== VM Migration State ========== */

static const VMStateDescription vmstate_dw_dma_channel_axi = {
    .name = "dw_dma_channel_axi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        /* Address registers */
        VMSTATE_UINT64(src, dw_dma_channel),
        VMSTATE_UINT64(dest, dw_dma_channel),
        VMSTATE_UINT64(llp, dw_dma_channel),

        /* Transfer control parameters */
        VMSTATE_UINT32(block_ts, dw_dma_channel),
        VMSTATE_UINT32(src_tr_width, dw_dma_channel),
        VMSTATE_UINT32(dst_tr_width, dw_dma_channel),
        VMSTATE_UINT32(src_msize, dw_dma_channel),
        VMSTATE_UINT32(dst_msize, dw_dma_channel),
        VMSTATE_UINT32(sinc, dw_dma_channel),
        VMSTATE_UINT32(dinc, dw_dma_channel),

        /* Transfer type and flow control */
        VMSTATE_UINT32(tt_fc, dw_dma_channel),

        /* Handshake configuration */
        VMSTATE_BOOL(hs_sel_src, dw_dma_channel),
        VMSTATE_BOOL(hs_sel_dst, dw_dma_channel),
        VMSTATE_BOOL(src_hs_pol, dw_dma_channel),
        VMSTATE_BOOL(dst_hs_pol, dw_dma_channel),
        VMSTATE_UINT32(src_per, dw_dma_channel),
        VMSTATE_UINT32(dest_per, dw_dma_channel),

        /* Interrupt and priority */
        VMSTATE_BOOL(int_en, dw_dma_channel),
        VMSTATE_UINT32(ch_prior, dw_dma_channel),

        /* AXI-specific fields */
        VMSTATE_UINT32(ar_cache, dw_dma_channel),
        VMSTATE_UINT32(aw_cache, dw_dma_channel),
        VMSTATE_UINT32(ar_prot, dw_dma_channel),
        VMSTATE_UINT32(aw_prot, dw_dma_channel),
        VMSTATE_BOOL(arlen_en, dw_dma_channel),
        VMSTATE_UINT32(arlen, dw_dma_channel),
        VMSTATE_BOOL(awlen_en, dw_dma_channel),
        VMSTATE_UINT32(awlen, dw_dma_channel),
        VMSTATE_UINT32(src_osr_lmt, dw_dma_channel),
        VMSTATE_UINT32(dst_osr_lmt, dw_dma_channel),
        VMSTATE_BOOL(lock_ch, dw_dma_channel),
        VMSTATE_UINT32(lock_ch_l, dw_dma_channel),
        VMSTATE_BOOL(nonposted_lastwrite_en, dw_dma_channel),
        VMSTATE_BOOL(src_stat_en, dw_dma_channel),
        VMSTATE_BOOL(dst_stat_en, dw_dma_channel),
        VMSTATE_BOOL(ioc_blktfr, dw_dma_channel),

        /* Unimplemented feature fields (for register read/write only) */
        VMSTATE_BOOL(src_gather_en, dw_dma_channel),
        VMSTATE_BOOL(dst_scatter_en, dw_dma_channel),
        VMSTATE_UINT32(sgr, dw_dma_channel),
        VMSTATE_UINT32(dsr, dw_dma_channel),
        VMSTATE_BOOL(llp_dst_en, dw_dma_channel),
        VMSTATE_BOOL(llp_src_en, dw_dma_channel),
        VMSTATE_UINT32(lms, dw_dma_channel),

        /* Channel state */
        VMSTATE_INT32(chan_enable, dw_dma_channel),
        VMSTATE_UINT32(state, dw_dma_channel),
        VMSTATE_BOOL(ch_susp, dw_dma_channel),
        VMSTATE_BOOL(fifo_empty, dw_dma_channel),
        VMSTATE_UINT32(block_size, dw_dma_channel),
        VMSTATE_UINT32(block_size_cmplt, dw_dma_channel),

        /* Software handshake registers (AXI per-channel) */
        VMSTATE_UINT32(sw_hs_src_reg, dw_dma_channel),
        VMSTATE_UINT32(sw_hs_dst_reg, dw_dma_channel),

        /* Interrupt enable registers (AXI only) */
        VMSTATE_UINT64(intstatus_enable, dw_dma_channel),
        VMSTATE_UINT64(intsignal_enable, dw_dma_channel),

        /* Transfer status information */
        VMSTATE_UINT64(current_src_addr, dw_dma_channel),
        VMSTATE_UINT64(current_dest_addr, dw_dma_channel),

        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_dw_dma_axi = {
    .name = TYPE_DW_DMA_SW_AXI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        /* Global DMA controller state */
        VMSTATE_INT32(dma_enable, dw_dma_state),
        VMSTATE_INT32(int_en, dw_dma_state),

        /* Interrupt management */
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

        /* Common interrupt enable registers (AXI only) */
        VMSTATE_UINT64(common_intstatus_enable, dw_dma_state),
        VMSTATE_UINT64(common_intsignal_enable, dw_dma_state),

        /* System-level interrupt status */
        VMSTATE_UINT64(common_int_status, dw_dma_state),

        /* Configuration */
        VMSTATE_UINT32(num_channels, dw_dma_state),

        /* Channels */
        VMSTATE_STRUCT_VARRAY_UINT32(chan, dw_dma_state, num_channels, 0,
                                     vmstate_dw_dma_channel_axi, dw_dma_channel),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t dw_dma_axi_read(void *opaque, hwaddr offset, unsigned size)
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

    /* Common registers: 0x0 - 0x98 */
    if (offset < DW_DMA_CHAN_REG_BASE) {
        switch (offset) {
        case DW_DMA_REG_ID:
            ret = 0x746a1;  /* Component ID */
            break;
        case DW_DMA_REG_COMPVER:
            ret = 0x3231302A;  /* Version 1.20a */
            break;
        case DW_DMA_REG_CFG:
            {
            bool has_active_channels = false;
            for (int i = 0; i < DW_DMA_AXI_NUM_CHANNELS; i++) {
                if (s->chan[i].state == DW_DMA_CH_TRANSFERRING) {
                    has_active_channels = true;
                    break;
                }
            }
            if (s->dma_enable || has_active_channels) {
                ret |= DW_DMA_CFG_DMAC_EN;
            }

            /* Bit 1: INT_EN - Global interrupt enable (R/W) */
            if (s->int_en) {
                ret |= DW_DMA_CFG_INT_EN;
            }
            }
            break;
        case DW_DMA_REG_CFG + 4:
            /* High 32 bits of CFG register (reserved in AXI) */
            ret = 0;
            break;
        case DW_DMA_REG_CHEN:
            /* DW_DMA_REG_CHEN2 */
            if (DW_DMA_AXI_NUM_CHANNELS <= 8) {
                /* DMAC_ChEnReg: 64-bit register for <= 8 channels */
                /* Bits [7:0]: CH1_EN ~ CH8_EN */
                /* Bits [15:8]: CH1_EN_WE ~ CH8_EN_WE (write-only, read returns 0) */
                /* Bits [23:16]: CH1_SUSP ~ CH8_SUSP */
                /* Bits [31:24]: CH1_SUSP_WE ~ CH8_SUSP_WE (write-only, read returns 0) */
                /* Bits [39:32]: CH1_ABORT ~ CH8_ABORT (auto-cleared, read returns 0) */
                /* Bits [47:40]: CH1_ABORT_WE ~ CH8_ABORT_WE (write-only, read returns 0) */
                for (int i = 0; i < 8 && i < DW_DMA_AXI_NUM_CHANNELS; i++) {
                    /* Bits 0-7: Channel enable status */
                    if (s->chan[i].chan_enable) {
                        ret |= (1ULL << i);
                    }
                    /* Bits 8-15: EN_WE (write-only, read returns 0) */

                    /* Bits 16-23: Channel suspend status */
                    if (s->chan[i].ch_susp) {
                        ret |= (1ULL << (16 + i));
                    }
                    /* Bits 24-31: SUSP_WE (write-only, read returns 0) */

                    /* Bits 32-39: ABORT (auto-cleared, read returns 0) */
                    /* Bits 40-47: ABORT_WE (write-only, read returns 0) */
                }
                if (size == 4) {
                    ret = ret & 0xFFFFFFFF;
                }
            } else {
                /* DMAC_ChEnReg2: 64-bit register for > 8 channels */
                /* Low 32 bits: Group 1 (CH1-CH16) */
                /* Bits 0-15: CH1_EN ~ CH16_EN */
                /* Bits 16-31: CH1_EN_WE ~ CH16_EN_WE (write-only, read returns 0) */
                for (int i = 0; i < 16 && i < DW_DMA_AXI_NUM_CHANNELS; i++) {
                    if (s->chan[i].chan_enable) {
                        ret |= (1ULL << i);  /* Bits 0-15 */
                    }
                    /* Bits 16-31 are write-only, always return 0 */
                }
                if (size == 4) {
                    ret = ret & 0xFFFFFFFF;
                }
            }
            break;
        case DW_DMA_REG_CHEN + 4:
            /* DW_DMA_REG_CHEN2 + 4 */
            if (DW_DMA_AXI_NUM_CHANNELS <= 8) {
                /* High 32 bits of DMAC_ChEnReg for <= 8 channels */
                /* Bits [39:32]: CH1_ABORT ~ CH8_ABORT (auto-cleared, read returns 0) */
                /* Bits [47:40]: CH1_ABORT_WE ~ CH8_ABORT_WE (write-only, read returns 0) */
                /* Bits [63:48]: Reserved */
                ret = 0;
            } else {
                /* High 32 bits: Group 2 (CH17-CH32) */
                /* Bits 32-47: CH17_EN ~ CH32_EN */
                /* Bits 48-63: CH17_EN_WE ~ CH32_EN_WE (write-only, read returns 0) */
                for (int i = 16; i < 32 && i < DW_DMA_AXI_NUM_CHANNELS; i++) {
                    if (s->chan[i].chan_enable) {
                        ret |= (1ULL << (i - 16));  /* Bits 0-15 of high word = bits 32-47 of full register */
                    }
                    /* Bits 16-31 of high word (48-63 of full register) are write-only, always return 0 */
                }
            }
            break;
        case DW_DMA_REG_CHSUSP:
            /* DMAC_CHSUSPREG: bits 0-31 are SUSP status, bits 32-63 are SUSP_WE (write-only) */
            ret = 0;
            for (int i = 0; i < DW_DMA_AXI_NUM_CHANNELS; i++) {
                if (s->chan[i].ch_susp) {
                    ret |= (1ULL << i);
                }
            }
            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_REG_CHSUSP + 4:
            ret = 0;
            break;
        case DW_DMA_REG_CHABORT:
            ret = 0;
            break;
        case DW_DMA_REG_CHABORT + 4:
            /* High 32 bits of ABORT register (reserved) */
            ret = 0;
            break;
        case DW_DMA_REG_INTSTATUS:
            ret = 0;
            for (int i = 0; i < 32 && i < DW_DMA_AXI_NUM_CHANNELS; i++) {
                /* Check if channel has any active interrupt that is enabled */
                uint32_t ch_raw_int = 0;
                uint32_t ch_enabled_int = 0;

                /* Collect raw interrupt status */
                if (s->block_int & (1 << i)) {
                    /* BLOCK_TFR_DONE */
                    ch_raw_int |= (1 << 0);
                }
                if (s->tfr_int & (1 << i)) {
                    /* DMA_TFR_DONE */
                    ch_raw_int |= (1 << 1);
                }
                if (s->srctran_int & (1 << i)) {
                    /* SRC_TRANSCOMP */
                    ch_raw_int |= (1 << 3);
                }
                if (s->dsttran_int & (1 << i)) {
                    /* DST_TRANSCOMP */
                    ch_raw_int |= (1 << 4);
                }
                if (s->err_int & (1 << i)) {
                    /* ERR */
                    ch_raw_int |= (1 << 5);
                }

                /* Apply channel interrupt status enable mask */
                ch_enabled_int = ch_raw_int & s->chan[i].intstatus_enable;

                /* Set summary bit if any enabled interrupt is active */
                if (ch_enabled_int != 0) {
                    ret |= (1ULL << i);
                }
            }
            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_REG_INTSTATUS + 4:
            ret = 0;
            /* Bit 32 (bit 0 of high word): CommonReg interrupt status */
            if (s->common_int_status != 0 && (s->common_intstatus_enable & 0x1)) {
                ret |= 0x1;
            }
            break;
        case DW_DMA_REG_INTCLEAR:
            ret = 0;  /* Write-only register */
            break;
        case DW_DMA_REG_COMMONREG_INTSTATUS_ENABLE:
            ret = s->common_intstatus_enable;
            break;
        case DW_DMA_REG_COMMONREG_INTSTATUS_ENABLE + 4:
            /* High 32 bits (reserved) */
            ret = 0;
            break;
        case DW_DMA_REG_COMMONREG_INTSIGNAL_ENABLE:
            ret = s->common_intsignal_enable;
            break;
        case DW_DMA_REG_COMMONREG_INTSIGNAL_ENABLE + 4:
            /* High 32 bits (reserved) */
            ret = 0;
            break;
        case DW_DMA_REG_COMMONREG_INTSTATUSREG:
            /*
             * DMAC_COMMONREG_INTSTATUSREG:
             * Common register space interrupt status (read-only)
             *
             * Simplified implementation:
             * - Bits 0-8: Register interface errors (not implemented, always 0)
             * - Bits 9-20: AXI Manager Interface ECC errors (not implemented, always 0)
             * - All other bits: Reserved (0)
             *
             * Note: In a full implementation, these would track:
             *   - Register decode errors
             *   - Write to read-only register errors
             *   - Read from write-only register errors
             *   - ECC correctable/uncorrectable errors
             * For QEMU simulation, we simplify by not implementing these checks.
             */
            ret = s->common_int_status;
            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_REG_COMMONREG_INTSTATUSREG + 4:
            /* High 32 bits (reserved, always 0) */
            ret = 0;
            break;
        case DW_DMA_REG_RESETREG:
            /* RESETREG read returns 0 (reset is write-only) */
            ret = 0;
            break;
        case DW_DMA_REG_RESETREG + 4:
            /* High 32 bits of RESETREG (reserved) */
            ret = 0;
            break;
        default:
            break;
        }
        return ret;
    }

    /* Channel registers: 0x100 - 0x10FF (16 channels × 0x100) */
    if (offset >= DW_DMA_CHAN_REG_BASE &&
        offset < (DW_DMA_CHAN_REG_BASE + DW_DMA_AXI_NUM_CHANNELS * DW_DMA_CHAN_REGS_SIZE)) {
        channel = (offset - DW_DMA_CHAN_REG_BASE) / DW_DMA_CHAN_REGS_SIZE;
        if (channel >= DW_DMA_AXI_NUM_CHANNELS) {
            return 0;
        }
        ch = &s->chan[channel];
        offset = (offset - DW_DMA_CHAN_REG_BASE) % DW_DMA_CHAN_REGS_SIZE;

        switch (offset) {
        case DW_DMA_CHAN_REG_SAR:
            ret = ch->src;
            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_SAR + 4:
            ret = ch->src >> 32;
            break;
        case DW_DMA_CHAN_REG_DAR:
            ret = ch->dest;
            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_DAR + 4:
            ret = ch->dest >> 32;
            break;
        case DW_DMA_CHAN_REG_BLOCK_TS:
            ret = ch->block_ts;
            break;
        case DW_DMA_CHAN_REG_BLOCK_TS + 4:
            /* High 32 bits of BLOCK_TS (BLOCK_TS is only 22-bit) */
            ret = 0;
            break;
        case DW_DMA_CHAN_REG_CTL:
            /* Build CTL register from channel fields */
            ret = 0;
            ret |= (ch->sinc << DW_DMA_CTL_SINC_SHIFT);
            ret |= (ch->dinc << DW_DMA_CTL_DINC_SHIFT);
            ret |= ((uint64_t)dw_dma_axi_encode_width(ch->src_tr_width) << DW_DMA_CTL_SRC_TR_WIDTH_SHIFT);
            ret |= ((uint64_t)dw_dma_axi_encode_width(ch->dst_tr_width) << DW_DMA_CTL_DST_TR_WIDTH_SHIFT);
            ret |= ((uint64_t)dw_dma_axi_encode_msize(ch->src_msize) << DW_DMA_CTL_SRC_MSIZE_SHIFT);
            ret |= ((uint64_t)dw_dma_axi_encode_msize(ch->dst_msize) << DW_DMA_CTL_DST_MSIZE_SHIFT);
            if (ch->arlen_en) {
                ret |= DW_DMA_CTL_ARLEN_EN;
                ret |= ((uint64_t)ch->arlen << DW_DMA_CTL_ARLEN_SHIFT);
            }
            if (ch->awlen_en) {
                ret |= DW_DMA_CTL_AWLEN_EN;
                ret |= ((uint64_t)ch->awlen << DW_DMA_CTL_AWLEN_SHIFT);
            }
            if (ch->src_stat_en) {
                ret |= DW_DMA_CTL_SRC_STAT_EN;
            }
            if (ch->dst_stat_en) {
                ret |= DW_DMA_CTL_DST_STAT_EN;
            }
            if (ch->ioc_blktfr) {
                ret |= DW_DMA_CTL_IOC_BLKTFR;
            }

            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_CTL + 4:
            /* High 32 bits of CTL */
            ret = 0;
            if (ch->arlen_en) {
                ret |= (1 << (38 - 32));  /* ARLEN_EN */
                ret |= ((ch->arlen & 0xFF) << (39 - 32));
            }
            if (ch->awlen_en) {
                ret |= (1 << (47 - 32));  /* AWLEN_EN */
                ret |= ((ch->awlen & 0xFF) << (48 - 32));
            }
            if (ch->src_stat_en) {
                ret |= (1 << (56 - 32));
            }
            if (ch->dst_stat_en) {
                ret |= (1 << (57 - 32));
            }
            if (ch->ioc_blktfr) {
                ret |= (1 << (58 - 32));
            }
            break;
        case DW_DMA_CHAN_REG_CFG:
            /* Build CFG register from channel fields */
            ret = 0;
            ret |= ((uint64_t)ch->tt_fc << DW_DMA_CFG_TT_FC_SHIFT);
            if (ch->hs_sel_src) {
                ret |= DW_DMA_CFG_HS_SEL_SRC;
            }
            if (ch->hs_sel_dst) {
                ret |= DW_DMA_CFG_HS_SEL_DST;
            }

            /* Handle different layouts based on channel count */
            if (DW_DMA_AXI_NUM_CHANNELS <= 8) {
                /* CHx_CFG layout: SRC_PER and DST_PER in high 32 bits */
                ret |= ((uint64_t)ch->src_per << DW_DMA_CFG_SRC_PER_SHIFT);
                ret |= ((uint64_t)ch->dest_per << DW_DMA_CFG_DST_PER_SHIFT);
                ret |= ((uint64_t)(ch->ch_prior & DW_DMA_CFG_CH_PRIOR_MASK) << DW_DMA_CFG_CH_PRIOR_SHIFT);
            } else {
                /* CHx_CFG2 layout: SRC_PER in low 32 bits, 5-bit CH_PRIOR */
                ret |= ((uint64_t)ch->src_per << DW_DMA_CFG2_SRC_PER_SHIFT);
                /* For CFG2, DST_PER position is not clearly specified, keep using CFG position */
                ret |= ((uint64_t)ch->dest_per << DW_DMA_CFG2_DST_PER_SHIFT);
                ret |= ((uint64_t)(ch->ch_prior & DW_DMA_CFG2_CH_PRIOR_MASK) << DW_DMA_CFG2_CH_PRIOR_SHIFT);
            }

            if (ch->lock_ch) {
                ret |= DW_DMA_CFG_LOCK_CH;
            }
            ret |= ((uint64_t)ch->lock_ch_l << DW_DMA_CFG_LOCK_CH_L_SHIFT);
            ret |= ((uint64_t)ch->src_osr_lmt << DW_DMA_CFG_SRC_OSR_LMT_SHIFT);
            ret |= ((uint64_t)ch->dst_osr_lmt << DW_DMA_CFG_DST_OSR_LMT_SHIFT);

            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_CFG + 4:
            /* High 32 bits of CFG */
            ret = 0;
            ret |= (ch->tt_fc << (DW_DMA_CFG_TT_FC_SHIFT - 32));
            if (ch->hs_sel_src) {
                ret |= (DW_DMA_CFG_HS_SEL_SRC >> 32);
            }
            if (ch->hs_sel_dst) {
                ret |= (DW_DMA_CFG_HS_SEL_DST >> 32);
            }

            /* Handle different layouts based on channel count */
            if (DW_DMA_AXI_NUM_CHANNELS <= 8) {
                /* CHx_CFG layout */
                ret |= ((uint64_t)ch->src_per << (DW_DMA_CFG_SRC_PER_SHIFT - 32));
                ret |= ((uint64_t)ch->dest_per << (DW_DMA_CFG_DST_PER_SHIFT - 32));
                ret |= ((uint64_t)(ch->ch_prior & DW_DMA_CFG_CH_PRIOR_MASK) << (DW_DMA_CFG_CH_PRIOR_SHIFT - 32));
            } else {
                /* CHx_CFG2 layout: SRC_PER not in high 32 bits, 5-bit CH_PRIOR */
                ret |= ((uint64_t)ch->dest_per << (DW_DMA_CFG_DST_PER_SHIFT - 32));
                ret |= ((uint64_t)(ch->ch_prior & DW_DMA_CFG2_CH_PRIOR_MASK) << (DW_DMA_CFG2_CH_PRIOR_SHIFT - 32));
            }

            if (ch->lock_ch) {
                ret |= (DW_DMA_CFG_LOCK_CH >> 32);
            }
            ret |= ((uint64_t)ch->lock_ch_l << (DW_DMA_CFG_LOCK_CH_L_SHIFT - 32));
            ret |= ((uint64_t)ch->src_osr_lmt << (DW_DMA_CFG_SRC_OSR_LMT_SHIFT - 32));
            ret |= ((uint64_t)ch->dst_osr_lmt << (DW_DMA_CFG_DST_OSR_LMT_SHIFT - 32));
            break;
        case DW_DMA_CHAN_REG_INTSTATUS_ENABLE:
            if (size == 8) {
                ret = ch->intstatus_enable;
            } else {
                ret = ch->intstatus_enable & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_INTSTATUS_ENABLE + 4:
            /* High 32 bits (reserved) */
            if (size == 4) {
                ret = ch->intstatus_enable >> 32;
            }
            break;
        case DW_DMA_CHAN_REG_INTSTATUS:
            ret = 0;
            /* Bit 0: BLOCK_TFR_DONE */
            if (s->block_int & (1 << channel)) {
                ret |= (1 << 0);
            }
            /* Bit 1: DMA_TFR_DONE */
            if (s->tfr_int & (1 << channel)) {
                ret |= (1 << 1);
            }
            /* Bit 2: Reserved (always 0) */
            /* Bit 3: SRC_TRANSCOMP */
            if (s->srctran_int & (1 << channel)) {
                ret |= (1 << 3);
            }
            /* Bit 4: DST_TRANSCOMP */
            if (s->dsttran_int & (1 << channel)) {
                ret |= (1 << 4);
            }
            /* Bits 5+: Error interrupts */
            if (s->err_int & (1 << channel)) {
                ret |= (1 << 5);
            }
            break;
        case DW_DMA_CHAN_REG_INTSTATUS + 4:
            /* High 32 bits (reserved) */
            ret = 0;
            break;
        case DW_DMA_CHAN_REG_INTSIGNAL_ENABLE:
            if (size == 8) {
                ret = ch->intsignal_enable;
            } else {
                ret = ch->intsignal_enable & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_INTSIGNAL_ENABLE + 4:
            /* High 32 bits (reserved) */
            if (size == 4) {
                ret = ch->intsignal_enable >> 32;
            }
            break;
        case DW_DMA_CHAN_REG_INTCLEAR:
            /* CHx_INTCLEARREG: Write-only register, read returns 0 */
            ret = 0;
            break;
        case DW_DMA_CHAN_REG_INTCLEAR + 4:
            /* High 32 bits (reserved) */
            ret = 0;
            break;
        case DW_DMA_CHAN_REG_LLP:
            ret = ch->llp;
            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_LLP + 4:
            ret = ch->llp >> 32;
            break;
        case DW_DMA_CHAN_REG_STATUS:
            ret = ch->block_size_cmplt & DW_DMA_STATUS_CMPLTD_BLK_TFR_SIZE_MASK;
            break;
        case DW_DMA_CHAN_REG_SWHSSRC:
            /* CHx_SWHSSRCREG: Software handshake source register */
            /* Read: WE bits (1, 3, 5) always return 0 */
            ret = ch->sw_hs_src_reg & 0x15;  /* Mask: bits 0, 2, 4 (0x15 = 0b010101) */
            break;
        case DW_DMA_CHAN_REG_SWHSDST:
            /* CHx_SWHSDSTREG: Software handshake destination register */
            /* Read: WE bits (1, 3, 5) always return 0 */
            ret = ch->sw_hs_dst_reg & 0x15;  /* Mask: bits 0, 2, 4 (0x15 = 0b010101) */
            break;
        case DW_DMA_CHAN_REG_SSTAT:
            /* CHx_SSTAT: Source Status Register (read-only) */
            ret = ch->current_src_addr;
            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_SSTAT + 4:
            /* High 32 bits of SSTAT */
            ret = ch->current_src_addr >> 32;
            break;
        case DW_DMA_CHAN_REG_DSTAT:
            /* CHx_DSTAT: Destination Status Register (read-only) */
            ret = ch->current_dest_addr;
            if (size == 4) {
                ret = ret & 0xFFFFFFFF;
            }
            break;
        case DW_DMA_CHAN_REG_DSTAT + 4:
            /* High 32 bits of DSTAT */
            ret = ch->current_dest_addr >> 32;
            break;
        case DW_DMA_CHAN_REG_SSTATAR:
            /* CHx_SSTATAR: Source Status Fetch Address Register (write-only, read returns 0) */
            ret = 0;
            break;
        case DW_DMA_CHAN_REG_SSTATAR + 4:
            /* High 32 bits */
            ret = 0;
            break;
        case DW_DMA_CHAN_REG_DSTATAR:
            /* CHx_DSTATAR: Destination Status Fetch Address Register (write-only, read returns 0) */
            ret = 0;
            break;
        case DW_DMA_CHAN_REG_DSTATAR + 4:
            /* High 32 bits */
            ret = 0;
            break;
        default:
            break;
        }
    }

    return ret;
}

/* Handle CHEN2 register write operations */
static void dw_dma_axi_handle_chen2_write(dw_dma_state *s, hwaddr offset,
                                          uint64_t value, unsigned size)
{
    uint64_t chen_val = 0;

    /* Calculate chen_val based on offset and size */
    if (offset == DW_DMA_REG_CHEN2) {
        if (size == 8) {
            chen_val = value;
        } else {
            /* 32-bit write to low 32 bits */
            chen_val = value & 0xFFFFFFFF;
        }
    } else {
        /* 32-bit write to high 32 bits */
        chen_val = (value & 0xFFFFFFFF) << 32;
    }

    if (!s->dma_enable) {
        return;
    }

    if (DW_DMA_AXI_NUM_CHANNELS <= 8) {
        /* DMAC_ChEnReg: for <= 8 channels */
        /* Bits [7:0]: CH1_EN ~ CH8_EN */
        /* Bits [15:8]: CH1_EN_WE ~ CH8_EN_WE */
        /* Bits [23:16]: CH1_SUSP ~ CH8_SUSP */
        /* Bits [31:24]: CH1_SUSP_WE ~ CH8_SUSP_WE */
        /* Bits [39:32]: CH1_ABORT ~ CH8_ABORT */
        /* Bits [47:40]: CH1_ABORT_WE ~ CH8_ABORT_WE */

        uint32_t en = chen_val & 0xFF;
        uint32_t en_we = (chen_val >> 8) & 0xFF;
        uint32_t susp = (chen_val >> 16) & 0xFF;
        uint32_t susp_we = (chen_val >> 24) & 0xFF;
        uint32_t abort = (chen_val >> 32) & 0xFF;
        uint32_t abort_we = (chen_val >> 40) & 0xFF;

        for (int i = 0; i < 8 && i < DW_DMA_AXI_NUM_CHANNELS; i++) {
            /* Handle channel enable/disable */
            if (en_we & (1 << i)) {
                if (en & (1 << i)) {
                    s->chan[i].chan_enable = 1;
                    s->chan[i].state = DW_DMA_CH_ENABLED;
                    dw_dma_do_transfer(s, i);
                } else {
                    s->chan[i].chan_enable = 0;
                    s->chan[i].state = DW_DMA_CH_DISABLED;
                }
            }

            /* Handle channel suspend/resume */
            if (susp_we & (1 << i)) {
                if (susp & (1 << i)) {
                    /* Request channel suspend */
                    if (s->chan[i].chan_enable) {
                        s->chan[i].ch_susp = true;
                        if (s->chan[i].state != DW_DMA_CH_SUSPENDED) {
                            s->chan[i].state = DW_DMA_CH_SUSPENDED;
                        }
                    }
                } else {
                    /* Clear channel suspend (resume) */
                    s->chan[i].ch_susp = false;
                    if (s->chan[i].chan_enable && s->chan[i].state == DW_DMA_CH_SUSPENDED) {
                        s->chan[i].state = DW_DMA_CH_ENABLED;
                        dw_dma_do_transfer(s, i);
                    }
                }
            }

            /* Handle channel abort */
            if (abort_we & (1 << i)) {
                if (abort & (1 << i)) {
                    /* Abort channel immediately */
                    s->chan[i].chan_enable = 0;
                    s->chan[i].state = DW_DMA_CH_DISABLED;
                    s->chan[i].ch_susp = false;
                }
            }
        }
    } else {
        /* DMAC_ChEnReg2: 64-bit register for > 8 channels */
        /* Grouped layout: */
        /* Low 32 bits (0-31): Group 1 (CH1-CH16) */
        /*   Bits 0-15: CH1_EN ~ CH16_EN */
        /*   Bits 16-31: CH1_EN_WE ~ CH16_EN_WE */
        /* High 32 bits (32-63): Group 2 (CH17-CH32) */
        /*   Bits 32-47: CH17_EN ~ CH32_EN */
        /*   Bits 48-63: CH17_EN_WE ~ CH32_EN_WE */

        /* Process all 32 channels in ChEnReg2 grouped layout */
        /* Group 0 (CH1-16): bits 0-31, Group 1 (CH17-32): bits 32-63 */
        for (int group = 0; group < 2; group++) {
            int base_ch = group * 16;  /* 0 for Group 0, 16 for Group 1 */
            int shift_en = (group == 0) ? DW_DMA_CHEN2_CH1_16_EN_SHIFT : DW_DMA_CHEN2_CH17_32_EN_SHIFT;
            int shift_we = (group == 0) ? DW_DMA_CHEN2_CH1_16_EN_WE_SHIFT : DW_DMA_CHEN2_CH17_32_EN_WE_SHIFT;

            uint32_t group_en = (chen_val >> shift_en) & 0xFFFF;
            uint32_t group_en_we = (chen_val >> shift_we) & 0xFFFF;

            for (int i = 0; i < 16 && (base_ch + i) < DW_DMA_AXI_NUM_CHANNELS; i++) {
                if (group_en_we & (1U << i)) {
                    int ch_idx = base_ch + i;
                    if (group_en & (1U << i)) {
                        s->chan[ch_idx].chan_enable = 1;
                        s->chan[ch_idx].state = DW_DMA_CH_ENABLED;
                        dw_dma_do_transfer(s, ch_idx);
                    } else {
                        s->chan[ch_idx].chan_enable = 0;
                        s->chan[ch_idx].state = DW_DMA_CH_DISABLED;
                    }
                }
            }
        }
    }
}

static void dw_dma_axi_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    dw_dma_state *s = (dw_dma_state *)opaque;
    dw_dma_channel *ch;
    unsigned int channel;

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

    /* Common registers: 0x0 - 0x98 */
    if (offset < DW_DMA_CHAN_REG_BASE) {
        switch (offset) {
        case DW_DMA_REG_CFG:
            /* Bit 1: INT_EN - Global interrupt enable (R/W) */
            s->int_en = (value & DW_DMA_CFG_INT_EN) != 0;

            /* Bit 0: DMAC_EN - DMA controller enable (R/W) */
            bool requested_enable = (value & DW_DMA_CFG_DMAC_EN) != 0;

            if (requested_enable) {
                /* Enable DMAC */
                s->dma_enable = 1;
            } else {
                /* Request to disable DMAC */
                bool has_active_channels = false;

                /* Disable all non-transferring channels immediately */
                for (int i = 0; i < DW_DMA_AXI_NUM_CHANNELS; i++) {
                    if (s->chan[i].state == DW_DMA_CH_TRANSFERRING) {
                        has_active_channels = true;
                    } else {
                        /* Immediately disable non-active channels */
                        s->chan[i].chan_enable = 0;
                        s->chan[i].state = DW_DMA_CH_DISABLED;
                    }
                }

                /* Only fully disable DMAC if no channels are active
                 * Otherwise, keep dma_enable=1 until channels complete
                 * (the read logic will handle returning the correct value)
                 */
                if (!has_active_channels) {
                    s->dma_enable = 0;
                }
                /* If has_active_channels, we keep dma_enable=1 but the
                 * channels know they should terminate gracefully */
            }
            break;
        case DW_DMA_REG_CFG + 4:
            /* High 32 bits of CFG register (reserved in AXI, ignore writes) */
            break;
        case DW_DMA_REG_CHEN2:
        case DW_DMA_REG_CHEN2 + 4:
            dw_dma_axi_handle_chen2_write(s, offset, value, size);
            break;
        case DW_DMA_REG_CHSUSP:
        case DW_DMA_REG_CHSUSP + 4:
            {
                /* DMAC_CHSUSPREG: Channel Suspend Register */
                uint64_t susp_val = 0;
                if (offset == DW_DMA_REG_CHSUSP) {
                    if (size == 8) {
                        susp_val = value;
                    } else {
                        susp_val = value & 0xFFFFFFFF;
                    }
                } else {
                    susp_val = (value & 0xFFFFFFFF) << 32;
                }

                if (s->dma_enable) {
                    uint32_t susp_we = (susp_val >> 32) & 0xFFFFFFFF;  /* Bits 32-63 are write enable */
                    uint32_t susp = susp_val & 0xFFFFFFFF;  /* Bits 0-31 are suspend request */

                    for (int i = 0; i < DW_DMA_AXI_NUM_CHANNELS; i++) {
                        if (susp_we & (1 << i)) {
                            if (susp & (1 << i)) {
                                /* Request channel suspend */
                                if (s->chan[i].chan_enable) {
                                    s->chan[i].ch_susp = true;
                                    if (s->chan[i].state != DW_DMA_CH_SUSPENDED) {
                                        s->chan[i].state = DW_DMA_CH_SUSPENDED;
                                    }
                                }
                            } else {
                                /* Clear channel suspend (resume) */
                                s->chan[i].ch_susp = false;
                                if (s->chan[i].chan_enable && s->chan[i].state == DW_DMA_CH_SUSPENDED) {
                                    s->chan[i].state = DW_DMA_CH_ENABLED;
                                    /* Resume transfer if applicable */
                                    dw_dma_do_transfer(s, i);
                                }
                            }
                        }
                    }
                }
            }
            break;
        case DW_DMA_REG_CHABORT:
        case DW_DMA_REG_CHABORT + 4:
            {
                /* DMAC_CHABORTREG: Channel Abort Register */
                uint64_t abort_val = 0;
                if (offset == DW_DMA_REG_CHABORT) {
                    if (size == 8) {
                        abort_val = value;
                    } else {
                        abort_val = value & 0xFFFFFFFF;
                    }
                } else {
                    abort_val = (value & 0xFFFFFFFF) << 32;
                }

                if (s->dma_enable) {
                    uint32_t abort_we = (abort_val >> 16) & 0xFFFF;
                    uint32_t abort = abort_val & 0xFFFF;

                    for (int i = 0; i < DW_DMA_AXI_NUM_CHANNELS; i++) {
                        if (abort_we & (1 << i)) {
                            if (abort & (1 << i)) {
                                /* Abort channel immediately */
                                s->chan[i].chan_enable = 0;
                                s->chan[i].state = DW_DMA_CH_DISABLED;
                                s->chan[i].ch_susp = false;
                                /* Note: Real hardware would set CH_ABORTED status bit */
                            }
                        }
                    }
                }
            }
            break;
        case DW_DMA_REG_INTCLEAR:
        case DW_DMA_REG_INTCLEAR + 4:
            {
                /* DMAC_COMMONREG_INTCLEARREG: Interrupt Clear Register */
                uint64_t clear_val = 0;
                if (offset == DW_DMA_REG_INTCLEAR) {
                    if (size == 8) {
                        clear_val = value;
                    } else {
                        clear_val = value & 0xFFFFFFFF;
                    }
                } else {
                    clear_val = (value & 0xFFFFFFFF) << 32;
                }

                /* Clear interrupt bits for specified channels */
                uint32_t clear_mask = clear_val & 0xFFFFFFFF;
                s->tfr_int &= ~clear_mask;
                s->block_int &= ~clear_mask;
                s->srctran_int &= ~clear_mask;
                s->dsttran_int &= ~clear_mask;
                s->err_int &= ~clear_mask;
                /* Bit 32 would clear CommonReg interrupt if we had one */
            }
            break;
        case DW_DMA_REG_COMMONREG_INTSTATUS_ENABLE:
            s->common_intstatus_enable = value & 0x1;
            break;
        case DW_DMA_REG_COMMONREG_INTSTATUS_ENABLE + 4:
            /* High 32 bits (reserved, ignore writes) */
            break;
        case DW_DMA_REG_COMMONREG_INTSIGNAL_ENABLE:
            s->common_intsignal_enable = value & 0x1;
            break;
        case DW_DMA_REG_COMMONREG_INTSIGNAL_ENABLE + 4:
            /* High 32 bits (reserved, ignore writes) */
            break;
        case DW_DMA_REG_COMMONREG_INTSTATUSREG:
        case DW_DMA_REG_COMMONREG_INTSTATUSREG + 4:
            /* DMAC_COMMONREG_INTSTATUSREG: Read-only register, ignore writes */
            break;
        case DW_DMA_REG_RESETREG:
            if (value & 0x1) {
                dw_dma_axi_reset(opaque);
            }
            break;
        case DW_DMA_REG_RESETREG + 4:
            /* High 32 bits of RESETREG (reserved, ignore writes) */
            break;
        default:
            break;
        }
        dw_dma_axi_update_int(s);
        return;
    }

    /* Channel registers: 0x100 - 0x10FF */
    if (offset >= DW_DMA_CHAN_REG_BASE &&
        offset < (DW_DMA_CHAN_REG_BASE + DW_DMA_AXI_NUM_CHANNELS * DW_DMA_CHAN_REGS_SIZE)) {
        channel = (offset - DW_DMA_CHAN_REG_BASE) / DW_DMA_CHAN_REGS_SIZE;
        if (channel >= DW_DMA_AXI_NUM_CHANNELS) {
            return;
        }
        ch = &s->chan[channel];
        offset = (offset - DW_DMA_CHAN_REG_BASE) % DW_DMA_CHAN_REGS_SIZE;

        switch (offset) {
        case DW_DMA_CHAN_REG_SAR:
            if (size == 8) {
                ch->src = value;
            } else {
                ch->src = (ch->src & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFFF);
            }
            break;
        case DW_DMA_CHAN_REG_SAR + 4:
            ch->src = (ch->src & 0xFFFFFFFF) | ((value & 0xFFFFFFFF) << 32);
            break;
        case DW_DMA_CHAN_REG_DAR:
            if (size == 8) {
                ch->dest = value;
            } else {
                ch->dest = (ch->dest & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFFF);
            }
            break;
        case DW_DMA_CHAN_REG_DAR + 4:
            ch->dest = (ch->dest & 0xFFFFFFFF) | ((value & 0xFFFFFFFF) << 32);
            break;
        case DW_DMA_CHAN_REG_BLOCK_TS:
            ch->block_ts = value & 0x3FFFFF;  /* 22-bit field */
            if (ch->state != DW_DMA_CH_TRANSFERRING) {
                ch->block_size = ch->block_ts + 1; /* AXI Specially requires +1 */
            }
            break;
        case DW_DMA_CHAN_REG_BLOCK_TS + 4:
            /* High 32 bits of BLOCK_TS (reserved, ignore writes) */
            break;
        case DW_DMA_CHAN_REG_CTL:
            if (size == 8) {
                /* 64-bit write: update all fields */
                ch->sinc = (value >> DW_DMA_CTL_SINC_SHIFT) & 0x3;
                ch->dinc = (value >> DW_DMA_CTL_DINC_SHIFT) & 0x3;
                ch->src_tr_width = dw_dma_axi_decode_width((value >> DW_DMA_CTL_SRC_TR_WIDTH_SHIFT) & 0x7);
                ch->dst_tr_width = dw_dma_axi_decode_width((value >> DW_DMA_CTL_DST_TR_WIDTH_SHIFT) & 0x7);
                ch->src_msize = dw_dma_axi_decode_msize((value >> DW_DMA_CTL_SRC_MSIZE_SHIFT) & 0xF);
                ch->dst_msize = dw_dma_axi_decode_msize((value >> DW_DMA_CTL_DST_MSIZE_SHIFT) & 0xF);
                ch->arlen_en = (value & DW_DMA_CTL_ARLEN_EN) != 0;
                ch->arlen = (value >> DW_DMA_CTL_ARLEN_SHIFT) & 0xFF;
                ch->awlen_en = (value & DW_DMA_CTL_AWLEN_EN) != 0;
                ch->awlen = (value >> DW_DMA_CTL_AWLEN_SHIFT) & 0xFF;
                ch->src_stat_en = (value & DW_DMA_CTL_SRC_STAT_EN) != 0;
                ch->dst_stat_en = (value & DW_DMA_CTL_DST_STAT_EN) != 0;
                ch->ioc_blktfr = (value & DW_DMA_CTL_IOC_BLKTFR) != 0;
            } else {
                /* 32-bit write to CTL_LO: update low 32-bit fields only */
                ch->sinc = (value >> DW_DMA_CTL_SINC_SHIFT) & 0x3;
                ch->dinc = (value >> DW_DMA_CTL_DINC_SHIFT) & 0x3;
                ch->src_tr_width = dw_dma_axi_decode_width((value >> DW_DMA_CTL_SRC_TR_WIDTH_SHIFT) & 0x7);
                ch->dst_tr_width = dw_dma_axi_decode_width((value >> DW_DMA_CTL_DST_TR_WIDTH_SHIFT) & 0x7);
                ch->src_msize = dw_dma_axi_decode_msize((value >> DW_DMA_CTL_SRC_MSIZE_SHIFT) & 0xF);
                ch->dst_msize = dw_dma_axi_decode_msize((value >> DW_DMA_CTL_DST_MSIZE_SHIFT) & 0xF);
            }
            break;
        case DW_DMA_CHAN_REG_CTL + 4:
            /* 32-bit write to CTL_HI: update high 32-bit fields only */
            ch->arlen_en = (value & (DW_DMA_CTL_ARLEN_EN >> 32)) != 0;
            ch->arlen = (value >> (DW_DMA_CTL_ARLEN_SHIFT - 32)) & 0xFF;
            ch->awlen_en = (value & (DW_DMA_CTL_AWLEN_EN >> 32)) != 0;
            ch->awlen = (value >> (DW_DMA_CTL_AWLEN_SHIFT - 32)) & 0xFF;
            ch->src_stat_en = (value & (DW_DMA_CTL_SRC_STAT_EN >> 32)) != 0;
            ch->dst_stat_en = (value & (DW_DMA_CTL_DST_STAT_EN >> 32)) != 0;
            ch->ioc_blktfr = (value & (DW_DMA_CTL_IOC_BLKTFR >> 32)) != 0;
            break;
        case DW_DMA_CHAN_REG_CFG:
            if (size == 8) {
                /* 64-bit write: parse all CFG fields */
                ch->tt_fc = (value >> DW_DMA_CFG_TT_FC_SHIFT) & 0x7;
                ch->hs_sel_src = (value & DW_DMA_CFG_HS_SEL_SRC) != 0;
                ch->hs_sel_dst = (value & DW_DMA_CFG_HS_SEL_DST) != 0;

                /* Handle different layouts based on channel count */
                if (DW_DMA_AXI_NUM_CHANNELS <= 8) {
                    /* CHx_CFG layout */
                    ch->src_per = (value >> DW_DMA_CFG_SRC_PER_SHIFT) & 0xF;
                    ch->dest_per = (value >> DW_DMA_CFG_DST_PER_SHIFT) & 0xF;
                    ch->ch_prior = (value >> DW_DMA_CFG_CH_PRIOR_SHIFT) & DW_DMA_CFG_CH_PRIOR_MASK;
                } else {
                    /* CHx_CFG2 layout */
                    ch->src_per = (value >> DW_DMA_CFG2_SRC_PER_SHIFT) & 0xF;
                    ch->dest_per = (value >> DW_DMA_CFG2_DST_PER_SHIFT) & 0xF;
                    ch->ch_prior = (value >> DW_DMA_CFG2_CH_PRIOR_SHIFT) & DW_DMA_CFG2_CH_PRIOR_MASK;
                }

                ch->lock_ch = (value & DW_DMA_CFG_LOCK_CH) != 0;
                ch->lock_ch_l = (value >> DW_DMA_CFG_LOCK_CH_L_SHIFT) & 0x3;
                ch->src_osr_lmt = (value >> DW_DMA_CFG_SRC_OSR_LMT_SHIFT) & 0xF;
                ch->dst_osr_lmt = (value >> DW_DMA_CFG_DST_OSR_LMT_SHIFT) & 0xF;
            } else {
                /* 32-bit write to CFG_LO: mostly reserved, ignore */
            }
            /* Force software handshaking */
            ch->hs_sel_src = true;
            ch->hs_sel_dst = true;
            break;
        case DW_DMA_CHAN_REG_CFG + 4:
            /* 32-bit write to CFG high 32 bits: parse all fields */
            ch->tt_fc = (value >> (DW_DMA_CFG_TT_FC_SHIFT - 32)) & 0x7;
            ch->hs_sel_src = (value & (DW_DMA_CFG_HS_SEL_SRC >> 32)) != 0;
            ch->hs_sel_dst = (value & (DW_DMA_CFG_HS_SEL_DST >> 32)) != 0;

            /* Handle different layouts based on channel count */
            if (DW_DMA_AXI_NUM_CHANNELS <= 8) {
                /* CHx_CFG layout */
                ch->src_per = (value >> (DW_DMA_CFG_SRC_PER_SHIFT - 32)) & 0xF;
                ch->dest_per = (value >> (DW_DMA_CFG_DST_PER_SHIFT - 32)) & 0xF;
                ch->ch_prior = (value >> (DW_DMA_CFG_CH_PRIOR_SHIFT - 32)) & DW_DMA_CFG_CH_PRIOR_MASK;
            } else {
                /* CHx_CFG2 layout: SRC_PER not in high 32 bits */
                ch->dest_per = (value >> (DW_DMA_CFG_DST_PER_SHIFT - 32)) & 0xF;
                ch->ch_prior = (value >> (DW_DMA_CFG2_CH_PRIOR_SHIFT - 32)) & DW_DMA_CFG2_CH_PRIOR_MASK;
            }

            ch->lock_ch = (value & (DW_DMA_CFG_LOCK_CH >> 32)) != 0;
            ch->lock_ch_l = (value >> (DW_DMA_CFG_LOCK_CH_L_SHIFT - 32)) & 0x3;
            ch->src_osr_lmt = (value >> (DW_DMA_CFG_SRC_OSR_LMT_SHIFT - 32)) & 0xF;
            ch->dst_osr_lmt = (value >> (DW_DMA_CFG_DST_OSR_LMT_SHIFT - 32)) & 0xF;
            /* Force software handshaking */
            ch->hs_sel_src = true;
            ch->hs_sel_dst = true;
            break;
        case DW_DMA_CHAN_REG_LLP:
            if (size == 8) {
                ch->llp = value & ~0x3FUL;  /* 64-byte aligned */
            } else {
                ch->llp = (ch->llp & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFC0);
            }
            break;
        case DW_DMA_CHAN_REG_LLP + 4:
            ch->llp = (ch->llp & 0xFFFFFFFF) | ((value & 0xFFFFFFFF) << 32);
            break;
        case DW_DMA_CHAN_REG_SWHSSRC:
            if (s->dma_enable && ch->chan_enable) {
                /* WE=1 and REQ=1 */
                if ((value & 0x2) && (value & 0x1)) {
                    ch->sw_hs_src_reg |= 0x1;
                }
                /* WE=1 and SGLREQ=1 */
                if ((value & 0x8) && (value & 0x4)) {
                    ch->sw_hs_src_reg |= 0x4;
                }
                /* WE=1 and LST=1 */
                if ((value & 0x20) && (value & 0x10)) {
                    ch->sw_hs_src_reg |= 0x10;
                }

                /* Trigger transfer if any handshake bit is set */
                dw_dma_do_transfer(s, channel);
            }
            break;
        case DW_DMA_CHAN_REG_SWHSDST:
            if (s->dma_enable && ch->chan_enable) {
                /* WE=1 and REQ=1 */
                if ((value & 0x2) && (value & 0x1)) {
                    ch->sw_hs_dst_reg |= 0x1;
                }
                /* WE=1 and SGLREQ=1 */
                if ((value & 0x8) && (value & 0x4)) {
                    ch->sw_hs_dst_reg |= 0x4;
                }
                /* WE=1 and LST=1 */
                if ((value & 0x20) && (value & 0x10)) {
                    ch->sw_hs_dst_reg |= 0x10;
                }

                /* Trigger transfer if any handshake bit is set */
                dw_dma_do_transfer(s, channel);
            }
            break;
        case DW_DMA_CHAN_REG_INTSTATUS_ENABLE:
            ch->intstatus_enable = value & 0x3FULL;
            break;
        case DW_DMA_CHAN_REG_INTSTATUS_ENABLE + 4:
            /* High 32 bits (reserved, ignore writes) */
            break;
        case DW_DMA_CHAN_REG_INTSTATUS:
            /* CHx_INTSTATUS: Read-only register, ignore writes */
            break;
        case DW_DMA_CHAN_REG_INTSTATUS + 4:
            /* High 32 bits (reserved, ignore writes) */
            break;
        case DW_DMA_CHAN_REG_INTSIGNAL_ENABLE:
            ch->intsignal_enable = value & 0x3FULL;
            break;
        case DW_DMA_CHAN_REG_INTSIGNAL_ENABLE + 4:
            /* High 32 bits (reserved, ignore writes) */
            break;
        case DW_DMA_CHAN_REG_INTCLEAR:
            /* CHx_INTCLEARREG: Channel interrupt clear register (write-only)
             * Per DW_axi_dmac databook:
             * - bit 0: Clear_BLOCK_TFR_DONE_IntStat
             * - bit 1: Clear_DMA_TFR_DONE_IntStat
             * - bit 2: Reserved
             * - bit 3: Clear_SRC_TRANSCOMP_IntStat
             * - bit 4: Clear_DST_TRANSCOMP_IntStat
             * - bit 5: Clear_SRC_DEC_ERR_IntStat
             * - bit 6: Clear_DST_DEC_ERR_IntStat
             * - bit 7: Clear_SRC_SLV_ERR_IntStat
             * - bit 8: Clear_DST_SLV_ERR_IntStat
             * - bits 9-13: LLI and shadow register error clear bits
             * - bits 14-35: Additional error and status clear bits
             * Note: QEMU implementation simplifies error handling.
             */
            {
                uint64_t clear_mask = value;
                uint32_t ch_mask = 1 << channel;

                /* Clear basic transfer completion interrupts */
                if (clear_mask & (1ULL << 0)) {
                    s->block_int &= ~ch_mask;
                }
                if (clear_mask & (1ULL << 1)) {
                    s->tfr_int &= ~ch_mask;
                }
                /* Bit 2 is reserved, ignore */
                if (clear_mask & (1ULL << 3)) {
                    s->srctran_int &= ~ch_mask;
                }
                if (clear_mask & (1ULL << 4)) {
                    s->dsttran_int &= ~ch_mask;
                }

                /* Any bit from 5 to 35 set */
                if (clear_mask & 0xFFFFFFFE0ULL) {
                    s->err_int &= ~ch_mask;
                }
            }
            break;
        case DW_DMA_CHAN_REG_INTCLEAR + 4:
            /* High 32 bits (reserved, ignore writes) */
            break;
        case DW_DMA_CHAN_REG_SSTAT:
        case DW_DMA_CHAN_REG_SSTAT + 4:
        case DW_DMA_CHAN_REG_DSTAT:
        case DW_DMA_CHAN_REG_DSTAT + 4:
            /* CHx_SSTAT and CHx_DSTAT: Read-only status registers, ignore writes */
            break;
        case DW_DMA_CHAN_REG_SSTATAR:
        case DW_DMA_CHAN_REG_SSTATAR + 4:
            /* CHx_SSTATAR: Source Status Fetch Address Register (write-only) */
            /* In QEMU, we don't need to implement status fetch functionality */
            /* This register is used in hardware to fetch transfer status, which we track internally */
            break;
        case DW_DMA_CHAN_REG_DSTATAR:
        case DW_DMA_CHAN_REG_DSTATAR + 4:
            /* CHx_DSTATAR: Destination Status Fetch Address Register (write-only) */
            /* In QEMU, we don't need to implement status fetch functionality */
            break;
        default:
            break;
        }
    }

    dw_dma_axi_update_int(s);
}

static const MemoryRegionOps dw_dma_axi_ops_mem = {
    .read = dw_dma_axi_read,
    .write = dw_dma_axi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

/* ========== Device Lifecycle Functions ========== */

static void dw_dma_axi_reset(DeviceState *dev)
{
    dw_dma_state *s = DW_DMA_SW_AXI(dev);

    /* Call common reset which sets Mask registers to 0 */
    dw_dma_common_reset(s);
    s->common_int_status = 0;
    s->common_intstatus_enable = UINT64_MAX;
    s->common_intsignal_enable = UINT64_MAX;
    for (int i = 0; i < s->num_channels; i++) {
        s->chan[i].intstatus_enable = UINT64_MAX;
        s->chan[i].intsignal_enable = UINT64_MAX;
        s->chan[i].block_size_cmplt = 0;
    }
}

static void dw_dma_axi_realize(DeviceState *dev, Error **errp)
{
    dw_dma_state *s = DW_DMA_SW_AXI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Initialize state */
    s->num_channels = DW_DMA_AXI_NUM_CHANNELS;
    s->bus_ops = &dw_dma_axi_ops;

    /* Initialize each channel (chan is a static array) */
    for (int i = 0; i < s->num_channels; i++) {
        s->chan[i].hs_sel_src = true;  /* Force software handshaking */
        s->chan[i].hs_sel_dst = true;
    }

    /* Initialize memory region */
    memory_region_init_io(&s->iomem, OBJECT(dev), &dw_dma_axi_ops_mem,
                          s, TYPE_DW_DMA_SW_AXI, DW_DMA_AXI_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize interrupts - both normal IRQ and CLIC IRQ */
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->clic_irq);
}

static void dw_dma_axi_unrealize(DeviceState *dev)
{
    /* Nothing to clean up - chan is a static array */
}

static void dw_dma_axi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = dw_dma_axi_realize;
    dc->unrealize = dw_dma_axi_unrealize;
    dc->reset = dw_dma_axi_reset;
    dc->vmsd = &vmstate_dw_dma_axi;
    dc->desc = "DesignWare AXI DMA Controller (Software Handshake Only)";
}

static const TypeInfo dw_dma_axi_info = {
    .name          = TYPE_DW_DMA_SW_AXI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(dw_dma_state),
    .class_init    = dw_dma_axi_class_init,
};

static void dw_dma_axi_register_types(void)
{
    type_register_static(&dw_dma_axi_info);
}

type_init(dw_dma_axi_register_types)
