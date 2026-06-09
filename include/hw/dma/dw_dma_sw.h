/*
 * DW DMA Common Implementation - Software Handshake Only Version
 * Common states shared between AHB and AXI bus implementations
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

#ifndef DW_DMA_SW_H
#define DW_DMA_SW_H

#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/stream.h"

/* Forward declaration */
typedef struct dw_dma_bus_ops dw_dma_bus_ops;

/* Transfer Types and Flow Controllers */
#define DW_DMA_FC_D_M2M     0x0   /* DMA: Memory-to-Memory */
#define DW_DMA_FC_D_M2P     0x1   /* DMA: Memory-to-Peripheral */
#define DW_DMA_FC_D_P2M     0x2   /* DMA: Peripheral-to-Memory */
#define DW_DMA_FC_D_P2P     0x3   /* DMA: Peripheral-to-Peripheral */
#define DW_DMA_FC_P_P2M     0x4   /* Peripheral FC: Peripheral-to-Memory */
#define DW_DMA_FC_SP_P2P    0x5   /* Source Peripheral FC: Peripheral-to-Peripheral */
#define DW_DMA_FC_P_M2P     0x6   /* Peripheral FC: Memory-to-Peripheral */
#define DW_DMA_FC_DP_P2P    0x7   /* Destination Peripheral FC: Peripheral-to-Peripheral */

/* Channel States */
typedef enum {
    DW_DMA_CH_DISABLED = 0,
    DW_DMA_CH_ENABLED,
    DW_DMA_CH_SUSPENDED,
    DW_DMA_CH_TRANSFERRING,
    DW_DMA_CH_COMPLETED
} dw_dma_channel_state;

typedef struct {
    /* Address registers */
    uint64_t src;                    /* Source address */
    uint64_t dest;                   /* Destination address */
    uint64_t llp;                    /* Linked List Pointer (not implemented) */

    /* Transfer control parameters */
    uint32_t block_ts;               /* Block transfer size (items) */
    uint32_t src_tr_width;           /* Source transfer width (bytes: 1/2/4/8/16/32/64/128) */
    uint32_t dst_tr_width;           /* Destination transfer width (bytes) */
    uint32_t src_msize;              /* Source burst length (items: 1/4/8/16/32/64/128/256/512/1024) */
    uint32_t dst_msize;              /* Destination burst length (items) */
    uint32_t sinc;                   /* Source address increment (0=inc, 1=dec, 2=fixed) */
    uint32_t dinc;                   /* Destination address increment */

    /* Transfer type and flow control */
    uint32_t tt_fc;                  /* Transfer type and flow controller (0-7) */

    /* Handshake configuration */
    bool hs_sel_src;                 /* Source software handshake select */
    bool hs_sel_dst;                 /* Destination software handshake select */
    bool src_hs_pol;                 /* Source handshake polarity */
    bool dst_hs_pol;                 /* Destination handshake polarity */
    uint32_t src_per;                /* Source peripheral number (0-15) */
    uint32_t dest_per;               /* Destination peripheral number (0-15) */

    /* Interrupt and priority */
    bool int_en;                     /* Interrupt enable */
    uint32_t ch_prior;               /* Channel priority (0-7) */

    /* AXI-specific fields (AHB ignores these) */
    uint32_t ar_cache;               /* AXI arcache (4-bit) */
    uint32_t aw_cache;               /* AXI awcache (4-bit) */
    uint32_t ar_prot;                /* AXI arprot (3-bit) */
    uint32_t aw_prot;                /* AXI awprot (3-bit) */
    bool arlen_en;                   /* Source burst length enable */
    uint32_t arlen;                  /* Source burst length (0-255) */
    bool awlen_en;                   /* Destination burst length enable */
    uint32_t awlen;                  /* Destination burst length (0-255) */
    uint32_t src_osr_lmt;            /* Source outstanding request limit (0-15) */
    uint32_t dst_osr_lmt;            /* Destination outstanding request limit (0-15) */
    bool lock_ch;                    /* Channel lock */
    uint32_t lock_ch_l;              /* Channel lock level (0-1) */
    bool nonposted_lastwrite_en;     /* Non-posted last write enable */
    bool src_stat_en;                /* Source status enable */
    bool dst_stat_en;                /* Destination status enable */
    bool ioc_blktfr;                 /* Block transfer complete interrupt */

    /* Unimplemented feature fields (for register read/write only) */
    bool src_gather_en;              /* Source gather enable (not implemented) */
    bool dst_scatter_en;             /* Destination scatter enable (not implemented) */
    uint32_t sgr;                    /* Source gather register (not implemented) */
    uint32_t dsr;                    /* Destination scatter register (not implemented) */
    bool llp_dst_en;                 /* Destination LLP enable (not implemented) */
    bool llp_src_en;                 /* Source LLP enable (not implemented) */
    uint32_t lms;                    /* LLP Master select (not implemented) */

    /* Channel state */
    int chan_enable;                 /* Channel enable flag */
    dw_dma_channel_state state;      /* Channel state */
    bool ch_susp;                    /* Channel suspend (read-only status) */
    bool fifo_empty;                 /* FIFO empty (read-only status) */
    uint32_t block_size;             /* Current remaining block size (runtime) */
    uint32_t block_size_cmplt;       /* Current block size done (runtime) */

    /* Software handshake registers */
    uint32_t sw_hs_src_reg;          /* Per-channel SW handshake source (AXI) */
    uint32_t sw_hs_dst_reg;          /* Per-channel SW handshake destination (AXI) */

    /* Interrupt enable registers (AXI only) */
    uint64_t intstatus_enable;       /* Interrupt status enable register */
    uint64_t intsignal_enable;       /* Interrupt signal enable register */

    /* Transfer status information */
    hwaddr current_src_addr;         /* Current source address (for status query) */
    hwaddr current_dest_addr;        /* Current destination address (for status query) */
} dw_dma_channel;

typedef struct dw_dma_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq clic_irq;
    AddressSpace *dma_as;

    /* Bus-specific operations (AHB/AXI) */
    const dw_dma_bus_ops *bus_ops;

    /* Number of channels (8 for AHB, 16 for AXI) */
    uint32_t num_channels;

    /* Global DMA controller state */
    int dma_enable;
    int int_en;  /* Global interrupt enable (DMAC_CFGREG bit 1) */

    /* Interrupt management */
    uint32_t tfr_int;
    uint32_t block_int;
    uint32_t srctran_int;
    uint32_t dsttran_int;
    uint32_t err_int;
    uint32_t tfr_int_mask;
    uint32_t block_int_mask;
    uint32_t srctran_int_mask;
    uint32_t dsttran_int_mask;
    uint32_t err_int_mask;
    uint32_t status_int;

    /* Channels - fixed array, actual number determined by num_channels */
    dw_dma_channel chan[16];  /* Maximum 16 channels (for AXI) */

    /* Software handshake registers (AHB uses global, AXI uses per-channel) */
    uint32_t req_src_reg;
    uint32_t req_dst_reg;
    uint32_t sgl_req_src_reg;
    uint32_t sgl_req_dst_reg;
    uint32_t lst_src_reg;
    uint32_t lst_dst_reg;

    /* Common interrupt enable registers (AXI only) */
    uint64_t common_intstatus_enable;  /* Common interrupt status enable */
    uint64_t common_intsignal_enable;  /* Common interrupt signal enable */

    /* System-level interrupt status (simplified implementation) */
    uint64_t common_int_status;        /* Common register interrupt status */

    /* Configuration */
    uint32_t dma_comp_params[6];
    StreamSink *target_sink;
    uint32_t rrid;
} dw_dma_state;

struct dw_dma_bus_ops {
    const char *bus_type;

    /* Software handshake operations */
    bool (*get_sw_hs_req)(dw_dma_state *s, int channel, bool is_src);
    bool (*get_sw_hs_sgl)(dw_dma_state *s, int channel, bool is_src);
    bool (*get_sw_hs_lst)(dw_dma_state *s, int channel, bool is_src);
    void (*clear_sw_hs)(dw_dma_state *s, int channel);

    /* Interrupt management */
    void (*update_int)(dw_dma_state *s);
};

/* Device setup */
void dw_dma_setup_sink(DeviceState *dev, StreamSink *sink);

#endif /* DW_DMA_SW_H */
