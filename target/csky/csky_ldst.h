/*
 * C-SKY load store header
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

#ifndef CSKY_LDST_H
#define CSKY_LDST_H

#include "cpu.h"
#include "translate.h"
void csky_cpu_watchpoint_match(CPUCSKYState *env, uint32_t vaddr,
    int len, uint32_t flags);

void csky_cpu_stb_data_ra(CPUCSKYState *env, uint32_t vaddr, uint8_t data,
                          uintptr_t ra);
void csky_cpu_stw_data_ra(CPUCSKYState *env, uint32_t vaddr, uint16_t data,
                          uintptr_t ra);
void csky_cpu_stl_data_ra(CPUCSKYState *env, uint32_t vaddr, uint32_t data,
                          uintptr_t ra);
void csky_cpu_stq_data_ra(CPUCSKYState *env, uint32_t vaddr, uint64_t data,
                          uintptr_t ra);
uint8_t csky_cpu_ldub_data_ra(CPUCSKYState *env, uint32_t vaddr,
                              uintptr_t ra);
uint16_t csky_cpu_lduw_data_ra(CPUCSKYState *env, uint32_t vaddr,
                               uintptr_t ra);
uint32_t csky_cpu_ldl_data_ra(CPUCSKYState *env, uint32_t vaddr,
                              uintptr_t ra);
uint64_t csky_cpu_ldq_data_ra(CPUCSKYState *env, uint32_t vaddr,
                              uintptr_t ra);

uint32_t csky_cpu_ldl_code_ra(CPUCSKYState *env, uint32_t vaddr,
                              uintptr_t ra);
#endif
