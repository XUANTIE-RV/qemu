/*
 * QEMU RISC-V IOPMP dispatcher
 *
 * Receives transaction information from the requestor and forwards it to the
 * corresponding IOPMP device.
 *
 * Copyright (c) 2023-2025 Andes Tech. Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace.h"
#include "exec/exec-all.h"
#include "exec/address-spaces.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/misc/riscv_iopmp_dispatcher.h"
#include "memory.h"
#include "hw/irq.h"
#include "hw/misc/riscv_iopmp_txn_info.h"

static void riscv_iopmp_dispatcher_realize(DeviceState *dev, Error **errp)
{
    int i;
    RISCVIOPMPDispState *s = RISCV_IOPMP_DISP(dev);

    s->SinkMemMap = g_new(SinkMemMapEntry *, s->stage_num);
    for (i = 0; i < s->stage_num; i++) {
        s->SinkMemMap[i] = g_new(SinkMemMapEntry, s->target_num);
    }

    object_initialize_child(OBJECT(s), "iopmp_dispatcher_txn_info",
                            &s->txn_info_sink,
                            TYPE_RISCV_IOPMP_DISP_SS);
}

static Property iopmp_dispatcher_properties[] = {
    DEFINE_PROP_UINT32("stage-num", RISCVIOPMPDispState, stage_num, 2),
    DEFINE_PROP_UINT32("target-num", RISCVIOPMPDispState, target_num, 10),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_iopmp_dispatcher_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, iopmp_dispatcher_properties);
    dc->realize = riscv_iopmp_dispatcher_realize;
}

static const TypeInfo riscv_iopmp_dispatcher_info = {
    .name = TYPE_RISCV_IOPMP_DISP,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(RISCVIOPMPDispState),
    .class_init = riscv_iopmp_dispatcher_class_init,
};

static size_t dispatcher_txn_info_push(StreamSink *txn_info_sink,
                                       unsigned char *buf,
                                       size_t len, bool eop)
{
    uint64_t addr;
    uint32_t stage;
    int i, j;
    riscv_iopmp_disp_ss *ss =
        RISCV_IOPMP_DISP_SS(txn_info_sink);
    RISCVIOPMPDispState *s = RISCV_IOPMP_DISP(container_of(ss,
        RISCVIOPMPDispState, txn_info_sink));
    RISCVIOPMPTxnInfo *signal;
    if (len != sizeof(RISCVIOPMPTxnInfo)) {
        return 0;
    }
    signal = (RISCVIOPMPTxnInfo *)buf;
    addr = signal->start_addr;
    stage = signal->stage;
    for (i = stage; i < s->stage_num; i++) {
        for (j = 0; j < s->target_num; j++) {
            if (s->SinkMemMap[i][j].map.base <= addr &&
                addr < s->SinkMemMap[i][j].map.base
                + s->SinkMemMap[i][j].map.size) {
                    return stream_push(s->SinkMemMap[i][j].sink, buf, len, eop);
            }
        }
    }
    /* Always pass if target is not protected by IOPMP*/
    return 1;
}

static void riscv_iopmp_disp_ss_class_init(
    ObjectClass *klass, void *data)
{
    StreamSinkClass *ssc = STREAM_SINK_CLASS(klass);
    ssc->push = dispatcher_txn_info_push;
}

static const TypeInfo riscv_iopmp_disp_ss_info = {
    .name = TYPE_RISCV_IOPMP_DISP_SS,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(riscv_iopmp_disp_ss),
    .class_init = riscv_iopmp_disp_ss_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SINK },
        { }
    },
};

void iopmp_dispatcher_add_target(DeviceState *dev, StreamSink *sink,
    uint64_t base, uint64_t size, uint32_t stage, uint32_t id)
{
    RISCVIOPMPDispState *s = RISCV_IOPMP_DISP(dev);
    if (stage < s->stage_num && id < s->target_num) {
        s->SinkMemMap[stage][id].map.base = base;
        s->SinkMemMap[stage][id].map.size = size;
        s->SinkMemMap[stage][id].sink = sink;
    }
}

static void
iopmp_dispatcher_register_types(void)
{
    type_register_static(&riscv_iopmp_dispatcher_info);
    type_register_static(&riscv_iopmp_disp_ss_info);
}

type_init(iopmp_dispatcher_register_types);

