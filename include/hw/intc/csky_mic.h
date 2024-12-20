/*
 * CSKY multicore interrupt controller header.
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
#ifndef HW_THEAD_MIC_H
#define HW_THEAD_MIC_H

#include "exec/memory.h"

#define TYPE_THEAD_MIC   "csky_mic"
#define THEAD_MIC(obj)   OBJECT_CHECK(csky_mic_state, (obj), TYPE_THEAD_MIC)

/* csky_mic.c */
void csky_mic_init_cpu(CPUArchState *env);

#endif
