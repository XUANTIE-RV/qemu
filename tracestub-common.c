/*
 * CSKY trace server stub
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

#include "exec/tracestub.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/config-file.h"
#include "sysemu/runstate.h"

QemuOptsList qemu_csky_trace_opts = {
    .name = "csky-trace",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_csky_trace_opts.head),
    .desc = {},
};

void csky_trace_set_cpu(const char *cpu_type) {}

#ifdef CONFIG_USER_ONLY
int traceserver_start(int port, int debug_mode)
{ return 0; }
#else
int traceserver_start(const char *device)
{ return 0; }
inline bool gen_tb_trace(void)
{ return 0; }
void extern_helper_trace_tb_exit(uint32_t subtype, uint32_t offset) {}
#endif
