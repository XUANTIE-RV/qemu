/*
 * C-SKY load store
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
#include "csky_ldst.h"
#include "exec/cpu_ldst.h"

#if defined(CONFIG_USER_ONLY)
void csky_cpu_watchpoint_match(CPUCSKYState *env, uint32_t vaddr,
    int len, uint32_t flags)
{
    // CPUWatchpoint *wp;
    // CPUState *cpu = env_cpu(env);
    // if (watchpoint_count == 0) {
    //     return;
    // }
    // QTAILQ_FOREACH(wp, &cpu->watchpoints, entry) {
    //     /* maybe match many times in one insn, just trigger once */
    //     if (cpu->watchpoint_hit) {
    //         return;
    //     }
    //     if (watchpoint_address_matches(wp, vaddr, len) &&
    //         (wp->flags & flags)){
    //         if (flags == BP_MEM_READ) {
    //             wp->flags |= BP_WATCHPOINT_HIT_READ;
    //         } else {
    //             wp->flags |= BP_WATCHPOINT_HIT_WRITE;
    //         }
    //         wp->hitaddr = vaddr;
    //         if (!cpu->watchpoint_hit) {
    //             cpu->watchpoint_hit = wp;
    //             cpu_interrupt(cpu, CPU_INTERRUPT_DEBUG);
    //             return;
    //         }
    //     } else {
    //         wp->flags &= ~BP_WATCHPOINT_HIT;
    //     }
    // }
    return;
}
#else
void csky_cpu_watchpoint_match(CPUCSKYState *env, uint32_t vaddr,
    int len, uint32_t flags)
{
    return;
}
#endif

void csky_cpu_stb_data_ra(CPUCSKYState *env, uint32_t vaddr, uint8_t data,
                          uintptr_t retaddr)
{

    csky_cpu_watchpoint_match(env, vaddr, 1, BP_MEM_WRITE);
    cpu_stb_data_ra(env, vaddr, data, retaddr);
}

void csky_cpu_stw_data_ra(CPUCSKYState *env, uint32_t vaddr, uint16_t data,
                          uintptr_t retaddr)
{
    csky_cpu_watchpoint_match(env, vaddr, 2, BP_MEM_WRITE);
    cpu_stw_data_ra(env, vaddr, data, retaddr);
}

void csky_cpu_stl_data_ra(CPUCSKYState *env, uint32_t vaddr, uint32_t data,
                          uintptr_t retaddr)
{
    csky_cpu_watchpoint_match(env, vaddr, 4,  BP_MEM_WRITE);
    cpu_stl_data_ra(env, vaddr, data, retaddr);
}

void csky_cpu_stq_data_ra(CPUCSKYState *env, uint32_t vaddr, uint64_t data,
                          uintptr_t retaddr)
{
    csky_cpu_watchpoint_match(env, vaddr, 4,  BP_MEM_WRITE);
    csky_cpu_watchpoint_match(env, vaddr + 4, 4, BP_MEM_WRITE);
    cpu_stq_data_ra(env, vaddr, data, retaddr);
}

uint8_t csky_cpu_ldub_data_ra(CPUCSKYState *env, uint32_t vaddr,
                              uintptr_t retaddr)
{

    csky_cpu_watchpoint_match(env, vaddr, 1, BP_MEM_READ);
    return cpu_ldub_data_ra(env, vaddr, retaddr);
}

uint16_t csky_cpu_lduw_data_ra(CPUCSKYState *env, uint32_t vaddr,
                               uintptr_t retaddr)
{

    csky_cpu_watchpoint_match(env, vaddr, 2, BP_MEM_READ);
    return cpu_lduw_data_ra(env, vaddr, retaddr);
}

uint32_t csky_cpu_ldl_data_ra(CPUCSKYState *env, uint32_t vaddr,
                              uintptr_t retaddr)
{

    csky_cpu_watchpoint_match(env, vaddr, 4, BP_MEM_READ);
    return cpu_ldl_data_ra(env, vaddr, retaddr);
}

uint64_t csky_cpu_ldq_data_ra(CPUCSKYState *env, uint32_t vaddr,
                              uintptr_t retaddr)
{

    csky_cpu_watchpoint_match(env, vaddr, 4, BP_MEM_READ);
    csky_cpu_watchpoint_match(env, vaddr + 4, 4, BP_MEM_READ);
    return cpu_ldq_data_ra(env, vaddr, retaddr);
}

uint32_t csky_cpu_ldl_code_ra(CPUCSKYState *env, uint32_t vaddr,
                              uintptr_t retaddr)
{

    csky_cpu_watchpoint_match(env, vaddr, 4, BP_MEM_READ);
#ifndef CONFIG_USER_ONLY
    return cpu_ldl_code(env, vaddr); // FIXME: cpu_ldl_code_ra is trashed
#else
    return cpu_ldl_code(env, vaddr);
#endif
}
