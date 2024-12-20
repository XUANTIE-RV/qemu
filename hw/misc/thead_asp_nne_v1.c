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
#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qemu/main-loop.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "hw/misc/thead_asp_nne_api_v1.h"
#include "hw/misc/nne_model_wrap.h"
#include <stdint.h>

#define TYPE_THEAD_ASP_NNE_V1 "thead_asp_nne_v1"
#define THEAD_ASP_NNE_V1(obj)  OBJECT_CHECK(thead_asp_nne_v1_state, (obj), TYPE_THEAD_ASP_NNE_V1)

static void thead_asp_set_ce_irq(void *opaque, qemu_irq irq, int level)
{
    thead_asp_nne_v1_state *s = (thead_asp_nne_v1_state *)opaque;

    if (!s->sche) {
        qemu_set_irq(irq, level);
    }
}

static void thead_asp_set_ce_error(void *opaque)
{
    thead_asp_nne_v1_state *s = (thead_asp_nne_v1_state *)opaque;

    s->irq_raw = FIELD_DP32(s->irq_raw, IRQ_RAW, CE_ERROR_IRQ_RAW, 1);
    if (FIELD_EX32(s->irq_enable, IRQ_ENABLE, CE_ERROR_IRQ_ENABLE)) {
        s->irq_status = FIELD_DP32(s->irq_status, IRQ_STATUS,
                                   CE_ERROR_IRQ_STATUS, 1);
        qemu_set_irq(s->error_irq, 1);
    }
}

static void thead_asp_set_sche_error(void *opaque)
{
    thead_asp_nne_v1_state *s = (thead_asp_nne_v1_state *)opaque;

    s->irq_raw = FIELD_DP32(s->irq_raw, IRQ_RAW, SCHE_ERROR_IRQ_RAW, 1);
    if (FIELD_EX32(s->irq_enable, IRQ_ENABLE, SCHE_ERROR_IRQ_ENABLE)) {
        s->irq_status = FIELD_DP32(s->irq_status, IRQ_STATUS,
                                   SCHE_ERROR_IRQ_STATUS, 1);
        qemu_set_irq(s->error_irq, 1);
    }
}

static void thead_asp_set_sche_misalign(void *opaque)
{
    thead_asp_nne_v1_state *s = (thead_asp_nne_v1_state *)opaque;

    s->irq_raw = FIELD_DP32(s->irq_raw, IRQ_RAW,
                            SRAM_D_ADDR_MISALIGN_IRQ_RAW, 1);
    if (FIELD_EX32(s->irq_enable, IRQ_ENABLE,
                   SRAM_D_ADDR_MISALIGN_IRQ_ENABLE)) {
        s->irq_status = FIELD_DP32(s->irq_status, IRQ_STATUS,
                                   SRAM_D_ADDR_MISALIGN_IRQ_STATUS, 1);
        qemu_set_irq(s->error_irq, 1);
    }
}

static void thead_asp_set_nne_error(void *opaque)
{
    thead_asp_nne_v1_state *s = (thead_asp_nne_v1_state *)opaque;
    if (s->sche) {
        thead_asp_set_ce_error(s);
    } else {
        qemu_set_irq(s->error_irq, 1);
    }
}

static void thead_asp_sche_fsm(void *opaque)
{
    thead_asp_nne_v1_state *s = (thead_asp_nne_v1_state *)opaque;
    struct {
        uint32_t addr;
        uint32_t value;
    } cmd;
    hwaddr offset;
    uint32_t index = 0;

    while (s->cur_token_num) {
        index = s->token_num - s->cur_token_num;
        memset(&cmd, 0, sizeof(cmd));
        /*
         * Fix: read pool len cmd and give an ack to DMA.
         * Currently we only read from an fixed address.
         */
        cpu_physical_memory_read(s->cmd_addr + index * sizeof(cmd),
                                 &cmd, sizeof(cmd));
        offset = cmd.addr - (s->iomem).addr;
        s->cur_token_num--;

        if (!s->cur_token_num && (offset != R_SCHE_COUN)) {
            /* Error: last cmd is not SCHE_COUN */
            thead_asp_set_sche_error(s);
        }
        switch (offset) {
        case R_SCHE_COUN:
            if (!cmd.value && s->cur_token_num) { /* Error: remain cmds */
                thead_asp_set_sche_error(s);
            }
            if (cmd.value && !s->cur_token_num) { /* Error: no enough cmds */
                thead_asp_set_sche_error(s);
            }
            s->sche_coun = cmd.value;
            break;
        case R_TRI_SRAM_A_EN:
            /* Todo: add DMA in nne_state and transmit data to sram a */
            s->tri_sram_a_en = (bool)(cmd.value);
            break;
        case R_TRI_SRAM_B_EN:
            /* Todo: add DMA in nne_state and transmit data to sram b */
            s->tri_sram_b_en = (bool)(cmd.value);
            break;
        case R_TRI_SRAM_C_EN:
            /* Todo: add DMA in nne_state and transmit data from sram c */
            s->tri_sram_c_en = (bool)(cmd.value);
            break;
        default:
            cpu_physical_memory_write(cmd.addr, &cmd.value, sizeof(cmd.value));
            break;
        }
    }
    /* Todo: trigger irq done */
    s->sche_busy = false;
    s->irq_raw = FIELD_DP32(s->irq_raw, IRQ_RAW, SCHE_DONE_IRQ_RAW, 1);
    if (FIELD_EX32(s->irq_enable, IRQ_ENABLE, SCHE_DONE_IRQ_ENABLE)) {
        s->irq_status = FIELD_DP32(s->irq_status, IRQ_STATUS,
                                   SCHE_DONE_IRQ_STATUS, 1);
        qemu_set_irq(s->done_irq, 1);
    }
}

static void thead_asp_nne_core(thead_asp_nne_v1_state *s)
{
    switch (s->tp_op_type) {
    case NNE_FC:
    {
        uint32_t input_size = s->tp_bats * s->tp_indp;
        uint32_t output_size = s->tp_bats * s->tp_outdp;
        uint32_t filter_size = s->tp_outdp * s->tp_indp;
        uint32_t bias_size = s->tp_outdp;

        void *input_data = g_malloc(input_size);
        void *filter_data = g_malloc(filter_size);
        void *bias_data = g_malloc(bias_size);
        void *output_data = g_malloc(output_size);

        cpu_physical_memory_read(s->tp_id_adr, input_data, input_size);
        cpu_physical_memory_read(s->tp_iw_adr, filter_data, filter_size);
        cpu_physical_memory_read(s->tp_ib_adr, bias_data, bias_size);
        cpu_physical_memory_read(s->tp_od_adr, output_data, output_size);

        thead_asp_nne_fc_op_wrap(
            s->tp_id_iw_type,
            s->tp_od_type,
            s->tp_indp,
            s->tp_outdp,
            s->tp_bats,
            s->tp_iw_oft,
            s->tp_od_oft,
            s->tp_om_adr,
            s->tp_os_adr,
            s->vp_act_min,
            s->vp_act_max,
            s->tp_id_adr_line_std,
            s->tp_iw_adr_line_std,
            s->tp_od_adr_line_std,
            input_data,
            filter_data,
            bias_data,
            output_data);
        cpu_physical_memory_write(s->tp_od_adr, output_data, output_size);
        g_free(input_data);
        g_free(filter_data);
        g_free(bias_data);
        s->done_interrupt = true;
        thead_asp_set_ce_irq(s, s->done_irq, 1);
        break;
    }

    case NNE_CONV:
    {
        uint32_t input_size = s->tp_bats * s->tp_id_h * s->tp_id_w * s->tp_indp;
        uint32_t output_size = s->tp_bats * s->tp_od_h * s->tp_od_w * s->tp_outdp;
        uint32_t filter_size = s->tp_outdp * s->tp_iw_h * s->tp_iw_w * s->tp_indp;
        uint32_t bias_size = s->tp_outdp;

        void *input_data = g_malloc(input_size);
        void *filter_data = g_malloc(filter_size);
        void *bias_data = g_malloc(bias_size);
        void *output_data = g_malloc(output_size);
        void *om_data = g_malloc(s->tp_outdp);
        void *os_data = g_malloc(s->tp_outdp);

        cpu_physical_memory_read(s->tp_id_adr, input_data, input_size);
        cpu_physical_memory_read(s->tp_iw_adr, filter_data, filter_size);
        cpu_physical_memory_read(s->tp_ib_adr, bias_data, bias_size);
        cpu_physical_memory_read(s->tp_od_adr, output_data, output_size);
        cpu_physical_memory_read(s->tp_om_adr, om_data, s->tp_outdp);
        cpu_physical_memory_read(s->tp_os_adr, os_data, s->tp_outdp);

        thead_asp_nne_conv_op_wrap(
            s->tp_id_iw_type,
            s->tp_od_type,
            s->tp_indp,
            s->tp_outdp,
            s->tp_id_h,
            s->tp_od_h,
            s->tp_id_w,
            s->tp_od_w,
            s->tp_iw_h,
            s->tp_iw_w,
            s->tp_std_h,
            s->tp_std_w,
            s->tp_pad_h,
            s->tp_pad_w,
            s->tp_bats,
            s->tp_iw_oft,
            s->tp_od_oft,
            s->tp_om_adr,
            s->tp_os_adr,
            s->vp_act_min,
            s->vp_act_max,
            s->tp_id_adr_line_std,
            s->tp_id_adr_surf_std,
            s->tp_iw_adr_line_std,
            s->tp_iw_adr_surf_std,
            s->tp_iw_adr_bloc_std,
            s->tp_od_adr_line_std,
            s->tp_od_adr_surf_std,
            om_data,
            os_data,
            input_data,
            filter_data,
            bias_data,
            output_data);
        cpu_physical_memory_write(s->tp_od_adr, output_data, output_size);
        g_free(input_data);
        g_free(filter_data);
        g_free(bias_data);
        g_free(om_data);
        g_free(os_data);
        s->done_interrupt = true;
        thead_asp_set_ce_irq(s, s->done_irq, 1);
        break;
    }
    case NNE_DWCONV:
    {
        uint32_t input_size = s->tp_bats * s->tp_id_h * s->tp_id_w * s->tp_indp;
        uint32_t output_size = s->tp_bats * s->tp_od_h * s->tp_od_w * s->tp_outdp;
        uint32_t filter_size = s->tp_outdp * s->tp_iw_h * s->tp_iw_w;
        uint32_t bias_size = s->tp_outdp;

        void *input_data = g_malloc(input_size);
        void *filter_data = g_malloc(filter_size);
        void *bias_data = g_malloc(bias_size);
        void *output_data = g_malloc(output_size);
        void *om_data = g_malloc(s->tp_outdp);
        void *os_data = g_malloc(s->tp_outdp);

        cpu_physical_memory_read(s->tp_id_adr, input_data, input_size);
        cpu_physical_memory_read(s->tp_iw_adr, filter_data, filter_size);
        cpu_physical_memory_read(s->tp_ib_adr, bias_data, bias_size);
        cpu_physical_memory_read(s->tp_od_adr, output_data, output_size);
        cpu_physical_memory_read(s->tp_om_adr, om_data, s->tp_outdp);
        cpu_physical_memory_read(s->tp_os_adr, os_data, s->tp_outdp);

        thead_asp_nne_dw_conv_op_wrap(
            s->tp_id_iw_type,
            s->tp_od_type,
            s->tp_indp,
            s->tp_outdp,
            s->tp_id_h,
            s->tp_od_h,
            s->tp_id_w,
            s->tp_od_w,
            s->tp_iw_h,
            s->tp_iw_w,
            s->tp_std_h,
            s->tp_std_w,
            s->tp_pad_h,
            s->tp_pad_w,
            s->tp_bats,
            s->tp_iw_oft,
            s->tp_od_oft,
            s->tp_om_adr,
            s->tp_os_adr,
            s->vp_act_min,
            s->vp_act_max,
            s->tp_id_adr_line_std,
            s->tp_id_adr_surf_std,
            s->tp_iw_adr_line_std,
            s->tp_iw_adr_surf_std,
            s->tp_iw_adr_bloc_std,
            s->tp_od_adr_line_std,
            s->tp_od_adr_surf_std,
            om_data,
            os_data,
            input_data,
            filter_data,
            bias_data,
            output_data);
        cpu_physical_memory_write(s->tp_od_adr, output_data, output_size);
        g_free(input_data);
        g_free(filter_data);
        g_free(bias_data);
        g_free(om_data);
        g_free(os_data);
        s->done_interrupt = true;
        thead_asp_set_ce_irq(s, s->done_irq, 1);
        break;
    }
    case NNE_MAXPOOL:
    {
        uint32_t input_size = s->tp_bats * s->tp_id_h * s->tp_id_w * s->tp_indp;
        uint32_t output_size = s->tp_bats * s->tp_od_h * s->tp_od_w * s->tp_outdp;

        void *input_data = g_malloc(input_size);
        void *output_data = g_malloc(output_size);

        cpu_physical_memory_read(s->tp_id_adr, input_data, input_size);
        cpu_physical_memory_read(s->tp_od_adr, output_data, output_size);

        thead_asp_nne_max_pool_op_wrap(
            s->tp_id_iw_type,
            s->tp_od_type,
            s->tp_indp,
            s->tp_outdp,
            s->tp_id_h,
            s->tp_od_h,
            s->tp_id_w,
            s->tp_od_w,
            s->tp_iw_h,
            s->tp_iw_w,
            s->tp_std_h,
            s->tp_std_w,
            s->tp_pad_h,
            s->tp_pad_w,
            s->tp_bats,
            s->vp_act_min,
            s->vp_act_max,
            s->tp_id_adr_line_std,
            s->tp_id_adr_surf_std,
            s->tp_od_adr_line_std,
            s->tp_od_adr_surf_std,
            input_data,
            output_data);
        cpu_physical_memory_write(s->tp_od_adr, output_data, output_size);
        g_free(input_data);
        g_free(output_data);
        s->done_interrupt = true;
        thead_asp_set_ce_irq(s, s->done_irq, 1);
        break;
    }
    case NNE_AVGPOOL:
    {
        uint32_t input_size = s->tp_bats * s->tp_id_h * s->tp_id_w * s->tp_indp;
        uint32_t output_size = s->tp_bats * s->tp_od_h * s->tp_od_w * s->tp_outdp;

        void *input_data = g_malloc(input_size);
        void *output_data = g_malloc(output_size);

        cpu_physical_memory_read(s->tp_id_adr, input_data, input_size);
        cpu_physical_memory_read(s->tp_od_adr, output_data, output_size);

        thead_asp_nne_ave_pool_op_wrap(
            s->tp_id_iw_type,
            s->tp_od_type,
            s->tp_indp,
            s->tp_outdp,
            s->tp_id_h,
            s->tp_od_h,
            s->tp_id_w,
            s->tp_od_w,
            s->tp_iw_h,
            s->tp_iw_w,
            s->tp_std_h,
            s->tp_std_w,
            s->tp_pad_h,
            s->tp_pad_w,
            s->tp_bats,
            s->vp_act_min,
            s->vp_act_max,
            s->tp_id_adr_line_std,
            s->tp_id_adr_surf_std,
            s->tp_od_adr_line_std,
            s->tp_od_adr_surf_std,
            input_data,
            output_data);
        cpu_physical_memory_write(s->tp_od_adr, output_data, output_size);
        g_free(input_data);
        g_free(output_data);
        s->done_interrupt = true;
        thead_asp_set_ce_irq(s, s->done_irq, 1);
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead_asp_nne_v1: not supported operator\n");

    }
}

static uint64_t thead_asp_nne_v1_read(void *opaque, hwaddr offset, unsigned size)
{
    thead_asp_nne_v1_state *s = (thead_asp_nne_v1_state *)opaque;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead_asp_nne_v1_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case R_CFG_MODE:
        ret = s->sche;
        break;
    case R_SCHE_CFG:
        ret = FIELD_DP32(ret, SCHE_CFG, ENABLE, s->sche_enable);
        ret = FIELD_DP32(ret, SCHE_CFG, BUSY, s->sche_busy);
        break;
    case R_IRQ_ENABLE:
        ret = s->irq_enable;
        break;
    case R_IRQ_RAW:
        ret = s->irq_raw;
        break;
    case R_IRQ_STATUS:
        ret = s->irq_status;
        break;
    case R_IRQ_CLR:
        ret = 0;
        break;
    case R_TAR_TOKEN_NUM:
        ret = s->token_num;
        break;
    case R_TRI_SRAM_A_EN:
        ret = s->tri_sram_a_en;
        break;
    case R_TRI_SRAM_B_EN:
        ret = s->tri_sram_b_en;
        break;
    case R_TRI_SRAM_C_EN:
        ret = s->tri_sram_c_en;
        break;
    case R_SCHE_COUN:
        ret = s->sche_coun;
        break;
    case R_SRAM_D_P_BADDR:
        ret = s->sram_d_p_baddr;
        break;
    case R_SRAM_D_Q_BADDR:
        ret = s->sram_d_q_baddr;
        break;
    case R_SRAM_D_POOL_LEN:
        ret = s->sram_d_pool_len;
        break;
    case R_SCHE_FULL_TIME:
    case R_CE_FULL_TIME:
        ret = 0;
        break;
    case R_CUR_TOKEN_NUM:
        ret = s->token_num;
        break;
    case R_IP_ID:
        ret = 0;
        break;
    case R_NNE_CE_EN:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead_asp_nne_v1_write: NNE enable\n");
        ret = s->ce_enable;
        break;
    case R_NNE_TP_OP_TYPE:
        ret = s->tp_op_type;
        break;
    case R_NNE_TP_ID_OD_TYPE:
        ret = FIELD_DP32(ret, NNE_TP_ID_OD_TYPE, IW_MEM, s->tp_iw_mem);
        ret = FIELD_DP32(ret, NNE_TP_ID_OD_TYPE, ID_TYPE, s->tp_id_iw_type);
        ret = FIELD_DP32(ret, NNE_TP_ID_OD_TYPE, OD_TYPE, s->tp_od_type);
        break;
    case R_NNE_TP_INDP_SIZE:
        ret = FIELD_DP32(ret, NNE_TP_INDP_SIZE, INDP_VALUE, s->tp_indp);
        break;
    case R_NNE_TP_OUTDP_SIZE:
        ret = FIELD_DP32(ret, NNE_TP_OUTDP_SIZE, OUTDP_VALUE, s->tp_outdp);
        break;
    case R_NNE_TP_ID_H_SIZE:
        ret = FIELD_DP32(ret, NNE_TP_ID_H_SIZE, ID_HEIGHT, s->tp_id_h);
        break;
    case R_NNE_TP_OD_H_SIZE:
        ret = FIELD_DP32(ret, NNE_TP_OD_H_SIZE, OD_HEIGHT, s->tp_od_h);
        break;
    case R_NNE_TP_ID_W_SIZE:
        ret = FIELD_DP32(ret, NNE_TP_ID_W_SIZE, ID_WIDTH, s->tp_id_w);
        break;
    case R_NNE_TP_OD_W_SIZE:
        ret = FIELD_DP32(ret, NNE_TP_OD_W_SIZE, OD_WIDTH, s->tp_od_w);
        break;
    case R_NNE_TP_IW_SIZE:
        ret = FIELD_DP32(ret, NNE_TP_IW_SIZE, IW_HEIGHT, s->tp_iw_h);
        ret = FIELD_DP32(ret, NNE_TP_IW_SIZE, IW_WIDTH, s->tp_iw_w);
        break;
    case R_NNE_TP_STD_SIZE:
        ret = FIELD_DP32(ret, NNE_TP_STD_SIZE, STRIDE_HEIGHT, s->tp_std_h);
        ret = FIELD_DP32(ret, NNE_TP_STD_SIZE, STRIDE_WIDTH, s->tp_std_w);
        break;
    case R_NNE_TP_DLT_DM:
        ret = FIELD_DP32(ret, NNE_TP_DLT_DM, DILATION_WIDTH_FACTOR, s->tp_dlt_w);
        ret = FIELD_DP32(ret, NNE_TP_DLT_DM, DILATION_HEIGHT_FACTOR, s->tp_dlt_h);
        ret = FIELD_DP32(ret, NNE_TP_DLT_DM, DEPTH_MULTIPLIER_VALUE, s->tp_dp_mult);
        break;
    case R_NNE_TP_PAD_SIZE:
        ret = FIELD_DP32(ret, NNE_TP_PAD_SIZE, ID_BATCH, s->tp_bats);
        ret = FIELD_DP32(ret, NNE_TP_PAD_SIZE, PAD_HEIGHT, s->tp_pad_h);
        ret = FIELD_DP32(ret, NNE_TP_PAD_SIZE, PAD_WIDTH, s->tp_pad_w);
        break;
    case R_NNE_TP_OD_OFT:
        ret = s->tp_od_oft;
        break;
    case R_NNE_TP_IW_OFT:
        ret = s->tp_iw_oft;
        break;
    case R_NNE_VP_ACT_MIN:
        ret = s->vp_act_min;
        break;
    case R_NNE_VP_ACT_MAX:
        ret = s->vp_act_max;
        break;
    case R_NNE_TP_IB_ADR:
        ret = s->tp_ib_adr;
        break;
    case R_NNE_TP_OM_ADR:
        ret = s->tp_om_adr;
        break;
    case R_NNE_TP_OS_ADR:
        ret = s->tp_os_adr;
        break;
    case R_NNE_TP_IW_ADR:
        ret = s->tp_iw_adr;
        break;
    case R_NNE_TP_ID_ADR:
        ret = s->tp_id_adr;
        break;
    case R_NNE_TP_OD_ADR:
        ret = s->tp_od_adr;
        break;
    case R_NNE_TP_ID_ADR_LINE_STD:
        ret = s->tp_id_adr_line_std;
        break;
    case R_NNE_TP_ID_ADR_SURF_STD:
        ret = s->tp_id_adr_surf_std;
        break;
    case R_NNE_TP_IW_ADR_LINE_STD:
        ret = s->tp_iw_adr_line_std;
        break;
    case R_NNE_TP_IW_ADR_SURF_STD:
        ret = s->tp_iw_adr_surf_std;
        break;
    case R_NNE_TP_IW_ADR_BLOC_STD:
        ret = s->tp_iw_adr_bloc_std;
        break;
    case R_NNE_TP_OD_ADR_LINE_STD:
        ret = s->tp_od_adr_line_std;
        break;
    case R_NNE_TP_OD_ADR_SURF_STD:
        ret = s->tp_od_adr_surf_std;
        break;
    case R_NNE_SYS_PROT:
        ret = s->sys_prot;
        break;
    case R_NNE_HW_ATTR:
        ret = FIELD_DP32(ret, NNE_HW_ATTR, VPC_PARALLELISM, s->hw_vpc_parallelism);
        ret = FIELD_DP32(ret, NNE_HW_ATTR, TPPC_PARALLELISM, s->hw_tppc_parallelism);
        break;
    case R_NNE_ISR:
        ret = FIELD_DP32(ret, NNE_ISR, DONE_INTERRUPT, s->done_interrupt);
        ret = FIELD_DP32(ret, NNE_ISR, CONFIG_ERROR, s->config_error);
        ret = FIELD_DP32(ret, NNE_ISR, UNALIGN_ERROR, s->unalign_error);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead_asp_nne_v1_read: Bad offset %x\n", (int)offset);
    }
    return ret;
}

static bool thead_asp_nne_check_max(void *opaque, uint32_t value,
                                    uint32_t fc_max, uint32_t conv_max)
{
    bool illegal = false;
    thead_asp_nne_v1_state *s = (thead_asp_nne_v1_state *)opaque;
    switch (s->tp_op_type) {
    case NNE_FC:
        illegal = value > fc_max;
        break;
    case NNE_CONV:
    case NNE_DWCONV:
    case NNE_MAXPOOL:
    case NNE_AVGPOOL:
        illegal = value > conv_max;
        break;
    default:
        illegal = true;
        break;
    }
    return illegal;
}

static void thead_asp_nne_v1_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size)
{
    thead_asp_nne_v1_state *s = (thead_asp_nne_v1_state *)opaque;
    uint32_t tmp;
    bool error = false;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead_asp_nne_v1_write: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case R_CFG_MODE:
        s->sche = FIELD_EX32(value, CFG_MODE, SCHE);
        break;
    case R_SCHE_CFG:
        s->sche_enable = FIELD_EX32(value, SCHE_CFG, ENABLE);
        if (s->sche_enable && s->sche) {
            s->sche_busy = true;
            thead_asp_sche_fsm(s);
        }
        break;
    case R_IRQ_ENABLE:
        s->irq_enable = value & MAKE_64BIT_MASK(0, 4);
        break;
    case R_IRQ_CLR:
        s->irq_raw &= ~(value & MAKE_64BIT_MASK(0, 4));
        s->irq_status &= ~(value & MAKE_64BIT_MASK(0, 4));
        break;
    case R_TAR_TOKEN_NUM:
        s->cur_token_num =
            s->token_num = FIELD_EX32(value, TAR_TOKEN_NUM, TOKEN_NUM);
        break;
    case R_SRAM_D_P_BADDR:
        if (value % 32) {
            thead_asp_set_sche_misalign(s);
        } else {
            s->sram_d_p_baddr = value;
        }
        break;
    case R_SRAM_D_Q_BADDR:
        if (value % 32) {
            thead_asp_set_sche_misalign(s);
        } else {
            s->sram_d_q_baddr = value;
        }
        break;
    case R_SRAM_D_POOL_LEN:
        tmp = FIELD_EX32(value, SRAM_D_POOL_LEN, LEN);
        if (tmp > 1023) {
            thead_asp_set_sche_error(s);
        } else {
            s->sram_d_pool_len = tmp;
        }
        break;
    case R_NNE_CE_EN:
        s->ce_enable = FIELD_EX32(value, NNE_CE_EN, ENABLE);
        if (s->ce_enable) {
            thead_asp_nne_core(s);
        }
        break;
    case R_NNE_TP_OP_TYPE:
        {
            uint32_t op = FIELD_EX32(value, NNE_TP_OP_TYPE, OP);
            if (op >= NNE_OP_NUMS) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "thead_asp_nne_v1_write: "
                              "not supported operator\n");
                s->config_error = true;
                thead_asp_set_nne_error(s);
            } else {
                s->tp_op_type = op;
            }
        }
        break;
    case R_NNE_TP_ID_OD_TYPE:
        s->tp_per_channel_quantization = FIELD_EX32(value, NNE_TP_ID_OD_TYPE, Q_TYPE);
        uint32_t od_type = FIELD_EX32(value, NNE_TP_ID_OD_TYPE, OD_TYPE);
        uint32_t id_type = FIELD_EX32(value, NNE_TP_ID_OD_TYPE, ID_TYPE);
        if (od_type >= NNE_DATA_TYPE_NUMS || id_type >= NNE_DATA_TYPE_NUMS) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported data type\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        } else {
            /* FIXME: input weight and input data always have same type */
            s->tp_od_type = od_type;
            s->tp_id_iw_type = id_type;
        }
        /* FIXME: why give the parallel value here in id_od_type */
        uint32_t iw_mem = FIELD_EX32(value, NNE_TP_ID_OD_TYPE, IW_MEM);
        error = false;
        switch (iw_mem) {
        case 0x1:
        case 0x2:
            error = s->tp_op_type == NNE_FC || s->tp_op_type == NNE_CONV;
            break;
        case 0x3:
            error = s->tp_op_type == NNE_FC || s->tp_op_type == NNE_CONV ||
                    s->tp_op_type == NNE_DWCONV;
            break;
        default:
            error = true;
            break;
        }
        if (!error) {
            s->tp_iw_mem = iw_mem;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported weight memory parallel value\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    case R_NNE_TP_INDP_SIZE:
    {
        uint32_t indp = FIELD_EX32(value, NNE_TP_INDP_SIZE, INDP_VALUE);
        if (thead_asp_nne_check_max(s, indp, MAX_INDP_SIZE_FC, MAX_INDP_SIZE_CONV)) {
            s->tp_indp = indp;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported input depth value\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    }
    case R_NNE_TP_OUTDP_SIZE:
    {
        uint32_t outdp = FIELD_EX32(value, NNE_TP_OUTDP_SIZE, OUTDP_VALUE);
        if (thead_asp_nne_check_max(s, outdp, MAX_OUTDP_SIZE_FC, MAX_OUTDP_SIZE_CONV)) {
            s->tp_outdp = outdp;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported output depth value\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    }
    case R_NNE_TP_ID_H_SIZE:
    {
        uint32_t id_h = FIELD_EX32(value, NNE_TP_ID_H_SIZE, ID_HEIGHT);
        if (thead_asp_nne_check_max(s, id_h, MAX_ID_H_SIZE_FC, MAX_ID_H_SIZE_CONV)) {
            s->tp_id_h = id_h;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported input data height\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    }
    case R_NNE_TP_OD_H_SIZE:
    {
        uint32_t od_h = FIELD_EX32(value, NNE_TP_OD_H_SIZE, OD_HEIGHT);
        if (thead_asp_nne_check_max(s, od_h, MAX_OD_H_SIZE_FC, MAX_OD_H_SIZE_CONV)) {
            s->tp_od_h = od_h;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported output data height\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    }
    case R_NNE_TP_ID_W_SIZE:
    {
        uint32_t id_w = FIELD_EX32(value, NNE_TP_ID_W_SIZE, ID_WIDTH);
        if (thead_asp_nne_check_max(s, id_w, MAX_ID_W_SIZE_FC, MAX_ID_W_SIZE_CONV)) {
            s->tp_id_w = id_w;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported input data width\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    }
    case R_NNE_TP_OD_W_SIZE:
    {
        uint32_t od_w = FIELD_EX32(value, NNE_TP_OD_W_SIZE, OD_WIDTH);
        if (thead_asp_nne_check_max(s, od_w, MAX_OD_W_SIZE_FC, MAX_OD_W_SIZE_CONV)) {
            s->tp_od_w = od_w;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported output data width\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    }
    case R_NNE_TP_IW_SIZE:
    {
        uint32_t iw_h = FIELD_EX32(value, NNE_TP_IW_SIZE, IW_HEIGHT);
        uint32_t iw_w = FIELD_EX32(value, NNE_TP_IW_SIZE, IW_WIDTH);

        if (thead_asp_nne_check_max(s, iw_w, MAX_IW_W_SIZE_FC, MAX_IW_W_SIZE_CONV) &&
            thead_asp_nne_check_max(s, iw_h, MAX_IW_H_SIZE_FC, MAX_IW_H_SIZE_CONV)) {
            s->tp_iw_w = iw_w;
            s->tp_iw_h = iw_h;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported input weight data width\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    }
    case R_NNE_TP_STD_SIZE:
    {
        uint32_t stride_h = FIELD_EX32(value, NNE_TP_STD_SIZE, STRIDE_HEIGHT);
        uint32_t stride_w = FIELD_EX32(value, NNE_TP_STD_SIZE, STRIDE_WIDTH);

        if (thead_asp_nne_check_max(s, stride_w, MAX_STD_W_SIZE_FC, MAX_STD_W_SIZE_CONV) &&
            thead_asp_nne_check_max(s, stride_h, MAX_STD_H_SIZE_FC, MAX_STD_H_SIZE_CONV)) {
            s->tp_std_w = stride_w;
            s->tp_std_h = stride_h;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported stride value\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    }
    case R_NNE_TP_PAD_SIZE:
    {
        /* FIXME: */
        uint32_t bats = FIELD_EX32(value, NNE_TP_PAD_SIZE, ID_BATCH);
        uint32_t pad_h = FIELD_EX32(value, NNE_TP_PAD_SIZE, PAD_HEIGHT);
        uint32_t pad_w = FIELD_EX32(value, NNE_TP_PAD_SIZE, PAD_WIDTH);
        if (thead_asp_nne_check_max(s, pad_w, MAX_PAD_W_SIZE_FC, MAX_PAD_W_SIZE_CONV) &&
            thead_asp_nne_check_max(s, pad_h, MAX_PAD_H_SIZE_FC, MAX_PAD_H_SIZE_CONV) &&
            thead_asp_nne_check_max(s, bats, MAX_ID_BATCH_FC, MAX_ID_BATCH_CONV)) {
            s->tp_bats = bats;
            s->tp_pad_h = pad_h;
            s->tp_pad_w = pad_w;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "thead_asp_nne_v1_write: "
                          "not supported padding value\n");
            s->config_error = true;
            thead_asp_set_nne_error(s);
        }
        break;
    }
    case R_NNE_TP_OD_OFT:
        s->tp_od_oft = value;
        break;
    case R_NNE_TP_IW_OFT:
        s->tp_iw_oft = FIELD_EX32(value, NNE_TP_IW_OFT, IW_OFFSET);
        break;
    case R_NNE_VP_ACT_MIN:
        if (s->tp_op_type == NNE_MAXPOOL) {
            s->vp_act_min = (int8_t)FIELD_EX32(value, NNE_VP_ACT_MIN, MAXPOOL_MIN);
        } else {
            s->vp_act_min = value;
        }
        break;
    case R_NNE_VP_ACT_MAX:
        if (s->tp_op_type == NNE_MAXPOOL) {
            s->vp_act_max = (int8_t)FIELD_EX32(value, NNE_VP_ACT_MAX, MAXPOOL_MAX);
        } else {
            s->vp_act_max = value;
        }
        break;
    case R_NNE_TP_IB_ADR:
        s->tp_ib_adr = value;
        break;
    case R_NNE_TP_OM_ADR:
        if (s->tp_per_channel_quantization) {
            if (value & MAKE_64BIT_MASK(0, 5)) {
                s->unalign_error = true;
                thead_asp_set_nne_error(s);
                break;
            }
        }
        s->tp_om_adr = value;
        break;
    case R_NNE_TP_OS_ADR:
        if (s->tp_per_channel_quantization) {
            if (value & MAKE_64BIT_MASK(0, 5)) {
                s->unalign_error = true;
                thead_asp_set_nne_error(s);
                break;
            }
        }
        s->tp_os_adr = value;
        break;
    case R_NNE_TP_IW_ADR:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_iw_adr = value;
        break;
    case R_NNE_TP_ID_ADR:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_id_adr = value;
        break;
    case R_NNE_TP_OD_ADR:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_od_adr = value;
        break;
    case R_NNE_TP_ID_ADR_LINE_STD:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_id_adr_line_std = value & MAKE_64BIT_MASK(0, 16);
        break;
    case R_NNE_TP_ID_ADR_SURF_STD:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_id_adr_surf_std = value & MAKE_64BIT_MASK(0, 16);
        break;
    case R_NNE_TP_IW_ADR_LINE_STD:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_iw_adr_line_std = value & MAKE_64BIT_MASK(0, 16);
        break;
    case R_NNE_TP_IW_ADR_SURF_STD:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_iw_adr_surf_std = value & MAKE_64BIT_MASK(0, 16);
        break;
    case R_NNE_TP_IW_ADR_BLOC_STD:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_iw_adr_bloc_std = value & MAKE_64BIT_MASK(0, 16);
        break;
    case R_NNE_TP_OD_ADR_LINE_STD:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_od_adr_line_std = value & MAKE_64BIT_MASK(0, 16);
        break;
    case R_NNE_TP_OD_ADR_SURF_STD:
        if (value & MAKE_64BIT_MASK(0, 5)) {
            s->unalign_error = true;
            thead_asp_set_nne_error(s);
            break;
        }
        s->tp_od_adr_surf_std = value & MAKE_64BIT_MASK(0, 16);
        break;
    case R_NNE_SYS_PROT:
        s->sys_prot = !!FIELD_EX32(value, NNE_SYS_PROT, WRITE_PROTECT);
        break;
    case R_NNE_ISR:
        if (value == 1) {
            s->done_interrupt = false;
            s->config_error = false;
            s->unalign_error = false;
            if (!s->sche) {
                qemu_set_irq(s->error_irq, 0);
                qemu_set_irq(s->done_irq, 0);
            }
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thead_asp_nne_v1_write: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps thead_asp_nne_v1_ops = {
    .read = thead_asp_nne_v1_read,
    .write = thead_asp_nne_v1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_thead_asp_nne_v1 = {
    .name = TYPE_THEAD_ASP_NNE_V1,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(ce_enable, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_op_type, thead_asp_nne_v1_state),
        VMSTATE_BOOL(tp_per_channel_quantization, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_iw_mem, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_id_iw_type, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_od_type, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_indp, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_outdp, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_id_h, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_od_h, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_id_w, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_od_w, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_iw_h, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_iw_w, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_std_h, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_std_w, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_bats, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_pad_h, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_pad_w, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_dp_mult, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_dlt_h, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_dlt_w, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_od_oft, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_iw_oft, thead_asp_nne_v1_state),
        VMSTATE_INT32(vp_act_min, thead_asp_nne_v1_state),
        VMSTATE_INT32(vp_act_max, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_lut_value, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_ib_adr, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_om_adr, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_os_adr, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_iw_adr, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_id_adr, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_od_adr, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_id_adr_line_std, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_id_adr_surf_std, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_iw_adr_line_std, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_iw_adr_surf_std, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_iw_adr_bloc_std, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_od_adr_line_std, thead_asp_nne_v1_state),
        VMSTATE_UINT32(tp_od_adr_surf_std, thead_asp_nne_v1_state),
        VMSTATE_BOOL(sys_prot, thead_asp_nne_v1_state),
        VMSTATE_UINT32(hw_vpc_parallelism, thead_asp_nne_v1_state),
        VMSTATE_UINT32(hw_tppc_parallelism, thead_asp_nne_v1_state),
        VMSTATE_BOOL(config_error, thead_asp_nne_v1_state),
        VMSTATE_BOOL(unalign_error, thead_asp_nne_v1_state),
        VMSTATE_BOOL(done_interrupt, thead_asp_nne_v1_state),
        VMSTATE_END_OF_LIST()
    }
};

static void thead_asp_nne_v1_init(Object *obj)
{
    thead_asp_nne_v1_state *s = THEAD_ASP_NNE_V1(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &thead_asp_nne_v1_ops, s,
                          TYPE_THEAD_ASP_NNE_V1, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->done_irq);
    sysbus_init_irq(sbd, &s->error_irq);

    thead_asp_nne_v1_register_init(s);
}

static Property thead_asp_nne_v1_properties[] = {
    DEFINE_PROP_UINT32("sram_a_addr", thead_asp_nne_v1_state, sa_addr, 0),
    DEFINE_PROP_UINT32("sram_b_addr", thead_asp_nne_v1_state, sb_addr, 0),
    DEFINE_PROP_UINT32("sram_c_addr", thead_asp_nne_v1_state, sc_addr, 0),
    DEFINE_PROP_UINT32("cmd_addr", thead_asp_nne_v1_state, cmd_addr, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void thead_asp_nne_v1_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    //set_bit(DEVICE_CATEGORY_CSKYSIM, dc->categories);
    dc->vmsd = &vmstate_thead_asp_nne_v1;
    device_class_set_props(dc, thead_asp_nne_v1_properties);
    //dc->desc = "cskysim type: THEAD_ASP_NNE_V1";
    //dc->user_creatable = true;
}

static const TypeInfo thead_asp_nne_v1_info = {
    .name          = TYPE_THEAD_ASP_NNE_V1,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(thead_asp_nne_v1_state),
    .instance_init = thead_asp_nne_v1_init,
    .class_init    = thead_asp_nne_v1_class_init,
};

static void thead_asp_nne_v1_register_types(void)
{
    type_register_static(&thead_asp_nne_v1_info);
}

type_init(thead_asp_nne_v1_register_types)
