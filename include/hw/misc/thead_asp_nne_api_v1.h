/* CSKY ASP-NNE emulation.
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
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include "hw/registerfields.h"
//#include "nne_model_wrap.h"

/* Define registers */

/* SCHE registers */
REG32(CFG_MODE,      0x0)
REG32(SCHE_CFG,      0x4)
REG32(IRQ_ENABLE,    0x10)
REG32(IRQ_RAW,       0x14)
REG32(IRQ_STATUS,    0x18)
REG32(IRQ_CLR,       0x1c)
REG32(TAR_TOKEN_NUM, 0x20)
REG32(TRI_SRAM_A_EN, 0x30)
REG32(TRI_SRAM_B_EN, 0x34)
REG32(TRI_SRAM_C_EN, 0x38)
REG32(SCHE_COUN,     0x40)
REG32(SRAM_D_P_BADDR,  0x60)
REG32(SRAM_D_Q_BADDR,  0x64)
REG32(SRAM_D_POOL_LEN, 0x68)
REG32(SCHE_FULL_TIME,  0x80)
REG32(CE_FULL_TIME,    0x84)
REG32(CUR_TOKEN_NUM,   0x88)
REG32(IP_ID,           0xFC)

/* CE registers */
REG32(NNE_CE_EN,        0x200)
REG32(NNE_TP_OP_TYPE,   0x204)
REG32(NNE_TP_ID_OD_TYPE,    0x208)
REG32(NNE_TP_INDP_SIZE, 0x210)
REG32(NNE_TP_OUTDP_SIZE,    0x214)
REG32(NNE_TP_ID_H_SIZE, 0x218)
REG32(NNE_TP_OD_H_SIZE, 0x21c)
REG32(NNE_TP_ID_W_SIZE, 0x220)
REG32(NNE_TP_OD_W_SIZE, 0x224)
REG32(NNE_TP_IW_SIZE,   0x228)
REG32(NNE_TP_STD_SIZE,  0x22c)
REG32(NNE_TP_PAD_SIZE,  0x230)
REG32(NNE_TP_DLT_DM,    0x234)
REG32(NNE_TP_OD_OFT,    0x240)
REG32(NNE_TP_IW_OFT,    0x244)
REG32(NNE_VP_ACT_MIN,   0x248)
REG32(NNE_VP_ACT_MAX,   0x24c)
REG32(NNE_TP_LUT_VALUE, 0x250)
REG32(NNE_TP_IB_ADR,    0x260)
REG32(NNE_TP_OM_ADR,    0x264)
REG32(NNE_TP_OS_ADR,    0x268)
REG32(NNE_TP_IW_ADR,    0x26c)
REG32(NNE_TP_ID_ADR,    0x270)
REG32(NNE_TP_OD_ADR,    0x274)
REG32(NNE_TP_ID_ADR_LINE_STD,   0x280)
REG32(NNE_TP_ID_ADR_SURF_STD,   0x284)
REG32(NNE_TP_IW_ADR_LINE_STD,   0x288)
REG32(NNE_TP_IW_ADR_SURF_STD,   0x28c)
REG32(NNE_TP_IW_ADR_BLOC_STD,   0x300)
REG32(NNE_TP_OD_ADR_LINE_STD,   0x304)
REG32(NNE_TP_OD_ADR_SURF_STD,   0x308)
REG32(NNE_SYS_PROT, 0x310)
REG32(NNE_HW_ATTR,  0x314)
REG32(NNE_ISR,  0x320)

/* Define fileds in registers */

/* SCHE fields */
FIELD(CFG_MODE, SCHE, 0, 1)
FIELD(SCHE_CFG, ENABLE, 0, 1)
FIELD(SCHE_CFG, BUSY, 1, 1)

FIELD(IRQ_ENABLE, SCHE_DONE_IRQ_ENABLE, 0, 1)
FIELD(IRQ_ENABLE, SCHE_ERROR_IRQ_ENABLE, 1, 1)
FIELD(IRQ_ENABLE, SRAM_D_ADDR_MISALIGN_IRQ_ENABLE, 2, 1)
FIELD(IRQ_ENABLE, CE_ERROR_IRQ_ENABLE, 3, 1)

FIELD(IRQ_RAW, SCHE_DONE_IRQ_RAW, 0, 1)
FIELD(IRQ_RAW, SCHE_ERROR_IRQ_RAW, 1, 1)
FIELD(IRQ_RAW, SRAM_D_ADDR_MISALIGN_IRQ_RAW, 2, 1)
FIELD(IRQ_RAW, CE_ERROR_IRQ_RAW, 3, 1)

FIELD(IRQ_STATUS, SCHE_DONE_IRQ_STATUS, 0, 1)
FIELD(IRQ_STATUS, SCHE_ERROR_IRQ_STATUS, 1, 1)
FIELD(IRQ_STATUS, SRAM_D_ADDR_MISALIGN_IRQ_STATUS, 2, 1)
FIELD(IRQ_STATUS, CE_ERROR_IRQ_STATUS, 3, 1)

FIELD(IRQ_CLR, SCHE_DONE_IRQ_CLR, 0, 1)
FIELD(IRQ_CLR, SCHE_ERROR_IRQ_CLR, 1, 1)
FIELD(IRQ_CLR, SRAM_D_ADDR_MISALIGN_IRQ_CLR, 2, 1)
FIELD(IRQ_CLR, CE_ERROR_IRQ_CLR, 3, 1)

FIELD(TAR_TOKEN_NUM, TOKEN_NUM, 0, 10)

FIELD(TRI_SRAM_A_EN, ENABLE, 0, 1)
FIELD(TRI_SRAM_B_EN, ENABLE, 0, 1)
FIELD(TRI_SRAM_C_EN, ENABLE, 0, 1)

FIELD(SCHE_COUN, CONTINUE, 0, 1)

FIELD(SRAM_D_POOL_LEN, LEN, 0, 7)

FIELD(SCHE_FULL_TIME, CYCLES, 0, 20)

FIELD(CE_FULL_TIME, CYCLES, 0, 16)

FIELD(CUR_TOKEN_NUM, REMAIN, 0, 10)

/* NNE_CE_EN */
FIELD(NNE_CE_EN, ENABLE, 0, 1)

/* NNE_TP_OP_TYPE */
FIELD(NNE_TP_OP_TYPE, OP, 0, 4)
enum NNE_OP {
    NNE_FC = 1,
    NNE_CONV,
    NNE_DWCONV,
    NNE_MAXPOOL,
    NNE_AVGPOOL,
    NNE_OP_NUMS
};

enum NNE_DATA_TYPE {
    NNE_UINT8 = 1,
    NNE_INT8,
    NNE_DATA_TYPE_NUMS
};

/* NNE_TP_ID_OD_TYPE */
FIELD(NNE_TP_ID_OD_TYPE, OD_TYPE, 0, 4)
FIELD(NNE_TP_ID_OD_TYPE, ID_TYPE, 4, 4)
FIELD(NNE_TP_ID_OD_TYPE, IW_MEM, 8, 4) /* FIXME */
FIELD(NNE_TP_ID_OD_TYPE, Q_TYPE, 12, 1)

/* NNE_TP_INDP_SIZE */
FIELD(NNE_TP_INDP_SIZE, INDP_VALUE, 0, 16)

/* NNE_TP_OUTDP_SIZE */
FIELD(NNE_TP_OUTDP_SIZE, OUTDP_VALUE, 0, 16)

/* NNE_TP_ID_H_SIZE */
FIELD(NNE_TP_ID_H_SIZE, ID_HEIGHT, 0, 16)

/* NNE_TP_OD_H_SIZE */
FIELD(NNE_TP_OD_H_SIZE, OD_HEIGHT, 0, 16)

/* NNE_TP_ID_W_SIZE */
FIELD(NNE_TP_ID_W_SIZE, ID_WIDTH, 0, 16)

/* NNE_TP_OD_W_SIZE */
FIELD(NNE_TP_OD_W_SIZE, OD_WIDTH, 0, 16)

/* NNE_TP_IW_SIZE */
FIELD(NNE_TP_IW_SIZE, IW_WIDTH, 0, 4)
FIELD(NNE_TP_IW_SIZE, IW_HEIGHT, 4, 4)

/* NNE_TP_STD_SIZE */
FIELD(NNE_TP_STD_SIZE, STRIDE_WIDTH, 0, 4)
FIELD(NNE_TP_STD_SIZE, STRIDE_HEIGHT, 4, 4)

/* NNE_TP_PAD_SIZE */
FIELD(NNE_TP_PAD_SIZE, ID_BATCH, 16, 4)
FIELD(NNE_TP_PAD_SIZE, PAD_HEIGHT, 8, 8)
FIELD(NNE_TP_PAD_SIZE, PAD_WIDTH, 0, 8)

/* NNE_TP_DLT_DM */
FIELD(NNE_TP_DLT_DM, DILATION_WIDTH_FACTOR, 0, 3)
FIELD(NNE_TP_DLT_DM, DILATION_HEIGHT_FACTOR, 4, 4)
FIELD(NNE_TP_DLT_DM, DEPTH_MULTIPLIER_VALUE, 8, 8)

/* NNE_TP_IW_OFT */
FIELD(NNE_TP_IW_OFT, IW_OFFSET, 0, 16)

/* NNE_VP_ACT_MIN */
FIELD(NNE_VP_ACT_MIN, MAXPOOL_MIN, 0, 8)

/* NNE_VP_ACT_MAX */
FIELD(NNE_VP_ACT_MAX, MAXPOOL_MAX, 0, 8)

/* NNE_TP_ID_ADR_LINE_STD */
FIELD(NNE_TP_ID_ADR_LINE_STD, ID_ADDRESS_LINE_STRIDE, 0, 16)

/* NNE_TP_ID_ADR_SURF_STD */
FIELD(NNE_TP_ID_ADR_SURF_STD, ID_ADDRESS_SURFACE_STRIDE, 0, 16)

/* NNE_TP_IW_ADR_LINE_STD */
FIELD(NNE_TP_IW_ADR_LINE_STD, IW_ADDRESS_LINE_STRIDE, 0, 16)

/* NNE_TP_IW_ADR_SURF_STD */
FIELD(NNE_TP_IW_ADR_SURF_STD, IW_ADDRESS_SURFACE_STRIDE, 0, 16)

/* NNE_TP_IW_ADR_BLOC_STD */
FIELD(NNE_TP_IW_ADR_BLOC_STD, IW_ADDRESS_BLOCK_STRIDE, 0, 16)

/* NNE_TP_OD_ADR_LINE_STD */
FIELD(NNE_TP_OD_ADR_LINE_STD, OD_ADDRESS_LINE_STRIDE, 0, 16)

/* NNE_TP_OD_ADR_SURF_STD */
FIELD(NNE_TP_OD_ADR_SURF_STD, OD_ADDRESS_SURF_STRIDE, 0, 16)

/* NNE_SYS_PROT */
FIELD(NNE_SYS_PROT, WRITE_PROTECT, 0, 1)

/* NNE_HW_ATTR */
FIELD(NNE_HW_ATTR, TPPC_PARALLELISM, 0, 8)
FIELD(NNE_HW_ATTR, VPC_PARALLELISM, 8, 4)

/* NNE_ISR */
FIELD(NNE_ISR, DONE_INTERRUPT, 0, 1)
FIELD(NNE_ISR, UNALIGN_ERROR, 1, 1)
FIELD(NNE_ISR, CONFIG_ERROR, 1, 1)

/* Define macros */
#define MAX_INDP_SIZE_CONV 512
#define MAX_INDP_SIZE_FC 4096

#define MAX_OUTDP_SIZE_CONV 512
#define MAX_OUTDP_SIZE_FC 4096

#define MAX_ID_H_SIZE_CONV 512
#define MAX_ID_H_SIZE_FC 0

#define MAX_OD_H_SIZE_CONV 512
#define MAX_OD_H_SIZE_FC 0

#define MAX_OD_W_SIZE_CONV 256
#define MAX_OD_W_SIZE_FC 0

#define MAX_ID_W_SIZE_CONV 256
#define MAX_ID_W_SIZE_FC 0

#define MAX_IW_H_SIZE_CONV 8
#define MAX_IW_H_SIZE_FC 0

#define MAX_IW_W_SIZE_CONV 8
#define MAX_IW_W_SIZE_FC 0

#define MAX_STD_H_SIZE_CONV 8
#define MAX_STD_H_SIZE_FC 0

#define MAX_STD_W_SIZE_CONV 8
#define MAX_STD_W_SIZE_FC 0

#define MAX_PAD_H_SIZE_CONV 8
#define MAX_PAD_H_SIZE_FC 0

#define MAX_PAD_W_SIZE_CONV 8
#define MAX_PAD_W_SIZE_FC 0

#define MAX_ID_BATCH_CONV 0
#define MAX_ID_BATCH_FC 16

typedef struct thead_asp_nne_v1_state {
#ifndef CMODEL_SIM
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq done_irq;
    qemu_irq error_irq;
#endif
    /* SCHE state */
    bool sche;
    bool sche_enable;
    bool sche_busy;
    uint32_t irq_enable;
    uint32_t irq_raw;
    uint32_t irq_status;
    uint32_t irq_clr;
    uint32_t token_num;
    bool tri_sram_a_en;
    bool tri_sram_b_en;
    bool tri_sram_c_en;
    bool sche_coun;
    uint32_t sram_d_p_baddr;
    uint32_t sram_d_q_baddr;
    uint32_t sram_d_pool_len;
    uint32_t sche_full_time;
    uint32_t ce_full_time;
    uint32_t cur_token_num;
    uint32_t ip_id;
    uint32_t sa_addr;
    uint32_t sb_addr;
    uint32_t sc_addr;
    uint32_t cmd_addr;

    /* NNE state */
    bool ce_enable;
    enum NNE_OP tp_op_type;
    bool tp_per_channel_quantization;
    uint32_t tp_iw_mem;
    enum NNE_DATA_TYPE tp_id_iw_type;
    enum NNE_DATA_TYPE tp_od_type;
    uint32_t tp_indp;
    uint32_t tp_outdp;
    uint32_t tp_id_h;
    uint32_t tp_od_h;
    uint32_t tp_id_w;
    uint32_t tp_od_w;
    uint32_t tp_iw_h;
    uint32_t tp_iw_w;
    uint32_t tp_std_h;
    uint32_t tp_std_w;
    uint32_t tp_bats;
    uint32_t tp_pad_h;
    uint32_t tp_pad_w;
    uint32_t tp_dp_mult;
    uint32_t tp_dlt_h;
    uint32_t tp_dlt_w;
    uint32_t tp_od_oft;
    uint32_t tp_iw_oft;
    int32_t vp_act_min;
    int32_t vp_act_max;
    uint32_t tp_lut_value;
    uint32_t tp_ib_adr;
    uint32_t tp_om_adr;
    uint32_t tp_os_adr;
    uint32_t tp_iw_adr;
    uint32_t tp_id_adr;
    uint32_t tp_od_adr;
    uint32_t tp_id_adr_line_std;
    uint32_t tp_id_adr_surf_std;
    uint32_t tp_iw_adr_line_std;
    uint32_t tp_iw_adr_surf_std;
    uint32_t tp_iw_adr_bloc_std;
    uint32_t tp_od_adr_line_std;
    uint32_t tp_od_adr_surf_std;
    bool sys_prot;
    uint32_t hw_vpc_parallelism;
    uint32_t hw_tppc_parallelism;
    bool config_error;
    bool unalign_error;
    bool done_interrupt;
} thead_asp_nne_v1_state;

static void thead_asp_nne_v1_register_init(thead_asp_nne_v1_state *s)
{
    s->tp_dp_mult = 1;
    s->tp_dlt_h = 1;
    s->tp_dlt_w = 1;
    s->hw_vpc_parallelism = 1;
    s->hw_tppc_parallelism = 0x20;
}
