/*
 * C-SKY FPU v3 helper routines
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
#include "cpu.h"
#include "translate.h"
#include "exec/helper-proto.h"
#include "csky_ldst.h"
#include "exec/exec-all.h"

#ifndef CONFIG_USER_ONLY
/*
 * This function checks watchpoint before real load operation.
 *
 * In softmmu mode, the TLB API probe_access is enough for watchpoint check.
 * In user mode, there is no watchpoint support now.
 *
 * It will trigger an exception if there is no mapping in TLB
 * and page table walk can't fill the TLB entry. Then the guest
 * software can return here after process the exception or never return.
 */
static void probe_pages(CPUCSKYState *env, target_ulong addr,
                        target_ulong len, uintptr_t ra)
{
    target_ulong pagelen = -(addr | TARGET_PAGE_MASK);
    target_ulong curlen = MIN(pagelen, len);

    probe_write(env, addr, curlen,
                csky_env_mmu_index(env, false), ra);
    if (len > curlen) {
        addr += curlen;
        curlen = len - curlen;
        probe_write(env, addr, curlen,
                    csky_env_mmu_index(env, false), ra);
    }
}
#else
static void probe_pages(CPUCSKYState *env, target_ulong addr,
                        target_ulong len, uintptr_t ra)
{}
#endif


#if TARGET_BIG_ENDIAN
static bool swap_endian (void *a, void *b, int len)
{
    uint64_t tmp = 0;
    bool ret = true;
    switch(len) {
    case 1:
        tmp = *(uint8_t*)a;
        *(uint8_t*)a = *(uint8_t*)b;
        *(uint8_t*)b = tmp;
        break;
    case 2:
        tmp = *(uint16_t*)a;
        *(uint16_t*)a = *(uint16_t*)b;
        *(uint16_t*)b = tmp;
        break;
    case 4:
        tmp = *(uint32_t*)a;
        *(uint32_t*)a = *(uint32_t*)b;
        *(uint32_t*)b = tmp;
        break;
    case 8:
        tmp = *(uint64_t*)a;
        *(uint64_t*)a = *(uint64_t*)b;
        *(uint64_t*)b = tmp;
        break;
    default:
        ret = false;
        break;
    }
    return ret;
}
#endif

void FPUV3_HELPER(faddh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;

    c = float16_add(a, b , &env->vfp.fp_status);
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fsubh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;

    c = float16_sub(a, b , &env->vfp.fp_status);
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fmovh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    a = env->vfp.reg[vrx].f16[0];
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;

    env->vfp.reg[vrz].f16[0] = a;
}

void FPUV3_HELPER(fabsh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    c = float16_abs(a);
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;

    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fnegh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    c = float16_chs(a);
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;

    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fcmpzhsh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = float16_zero;
    switch (float16_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }

}

void FPUV3_HELPER(fcmpzlth)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = float16_zero;
    switch (float16_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmpzneh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = float16_zero;
    switch (float16_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
}

void FPUV3_HELPER(fcmpzuoh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float16 a;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];

    if (float16_is_any_nan(a)) {
        if (float16_is_signaling_nan(a, &env->vfp.fp_status)) {
            float_raise(float_flag_invalid, &env->vfp.fp_status);
        }
        env->psr_c = 1;
    } else {
        env->psr_c = 0;
    }
}

void FPUV3_HELPER(fcmphsh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    switch (float16_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmplth)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    switch (float16_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmpneh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    switch (float16_compare_quiet(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
}

void FPUV3_HELPER(fcmpuoh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];

    if (float16_is_any_nan(a) || float16_is_any_nan(b)) {
        if (float16_is_signaling_nan(a, &env->vfp.fp_status)
            || float16_is_signaling_nan(b, &env->vfp.fp_status)) {
            float_raise(float_flag_invalid, &env->vfp.fp_status);
        }
        env->psr_c = 1;
    } else {
        env->psr_c = 0;
    }
}

void FPUV3_HELPER(fmaxnm16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = float16_maxnum(a, b , &env->vfp.fp_status);
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fminnm16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = float16_minnum(a, b , &env->vfp.fp_status);
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fcmphz16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = float16_zero;
    switch (float16_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmplsz16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = float16_zero;
    switch (float16_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fmulh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = float16_mul(a, b , &env->vfp.fp_status);
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;

    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fnmulh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = float16_mul(a, b , &env->vfp.fp_status);
    c = float16_chs(c);
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;

    env->vfp.reg[vrz].f16[0] = c;
}

/* fmula.16 */
void FPUV3_HELPER(fmach)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = env->vfp.reg[vrz].f16[0];
    d = float16_mul(a, b, &env->vfp.fp_status);
    c = float16_add(d, c, &env->vfp.fp_status);
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;

    env->vfp.reg[vrz].f16[0] = c;
}

/* fnmuls.16 */
void FPUV3_HELPER(fmsch)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = env->vfp.reg[vrz].f16[0];
    d = float16_mul(a, b, &env->vfp.fp_status);
    c = float16_sub(d, c, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

/* fmuls.16 */
void FPUV3_HELPER(fnmach)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = env->vfp.reg[vrz].f16[0];
    d = float16_mul(a, b, &env->vfp.fp_status);
    c = float16_sub(c, d, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

/* fnmula.16 */
void FPUV3_HELPER(fnmsch)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = env->vfp.reg[vrz].f16[0];
    c = float16_chs(c);
    d = float16_mul(a, b, &env->vfp.fp_status);
    c = float16_sub(c, d, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(ffmula16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = env->vfp.reg[vrz].f16[0];
    c = float16_muladd(a, b, c, 0, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(ffmuls16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = env->vfp.reg[vrz].f16[0];
    c = float16_muladd(a, b, c, float_muladd_negate_product,
                       &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(ffnmula16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = env->vfp.reg[vrz].f16[0];
    c = float16_muladd(a, b, c, float_muladd_negate_c |
                       float_muladd_negate_product,
                       &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(ffnmuls16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = env->vfp.reg[vrz].f16[0];
    c = float16_muladd(a, b, c, float_muladd_negate_c,
                       &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fdivh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];
    c = float16_div(a, b, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(freciph)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = float16_one;
    c = float16_div(b, a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fsqrth)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    c = float16_sqrt(a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}

void FPUV3_HELPER(fsel16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float16 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    b = env->vfp.reg[vry].f16[0];

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    if (env->psr_c == 1) {
        env->vfp.reg[vrz].f16[0] = a;
    } else {
        env->vfp.reg[vrz].f16[0] = b;
    }
}

void FPUV3_HELPER(fadds)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = float32_add(a, b , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fsubs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = float32_sub(a, b , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fmovs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    env->vfp.reg[vrz].f32[0] = env->vfp.reg[vrx].f32[0];
}

void FPUV3_HELPER(fabss)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    c = float32_abs(a);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fnegs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    c = float32_chs(a);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fcmpzhss)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = float32_zero;
    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmpzlts)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = float32_zero;
    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

/* fcmpnez */
void FPUV3_HELPER(fcmpznes)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = float32_zero;
    switch (float32_compare_quiet(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
}

/* fpu v1 */
void FPUV3_HELPER(fcmpzuos)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float32 a;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];

    if (float32_is_any_nan(a)) {
        if (float32_is_signaling_nan(a, &env->vfp.fp_status)) {
            float_raise(float_flag_invalid, &env->vfp.fp_status);
        }
        env->psr_c = 1;
    } else {
        env->psr_c = 0;
    }
}

void FPUV3_HELPER(fcmphss)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];

    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmplts)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];

    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }

}

void FPUV3_HELPER(fcmpnes)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];

    switch (float32_compare_quiet(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
}

/* fpu v1 */
void FPUV3_HELPER(fcmpuos)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];

    if (float32_is_any_nan(a) || float32_is_any_nan(b)) {
        if (float32_is_signaling_nan(a, &env->vfp.fp_status)
            || float32_is_signaling_nan(b, &env->vfp.fp_status)) {
            float_raise(float_flag_invalid, &env->vfp.fp_status);
        }
        env->psr_c = 1;
    } else {
        env->psr_c = 0;
    }
}

void FPUV3_HELPER(fmaxnm32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];

    c = float32_maxnum(a, b, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fminnm32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    if (float32_is_quiet_nan(a, &env->vfp.fp_status)) {
        env->vfp.reg[vrz].f32[0] = b;
    }

    if (float32_is_quiet_nan(b, &env->vfp.fp_status)) {
        env->vfp.reg[vrz].f32[0] = a;
    }

    c = float32_minnum(a, b, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fcmphz32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = float32_zero;
    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmplsz32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = float32_zero;
    switch (float32_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fmuls)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = float32_mul(a, b , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

/* fpu v1 */
void FPUV3_HELPER(fnmuls)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    d = float32_mul(a, b , &env->vfp.fp_status);
    c = float32_chs(d);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

/* fmula.32 */
void FPUV3_HELPER(fmacs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = env->vfp.reg[vrz].f32[0];
    d = float32_mul(a, b , &env->vfp.fp_status);
    c = float32_add(c, d , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

/* fpu v1 */
/* fnmuls.32*/
void FPUV3_HELPER(fmscs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = env->vfp.reg[vrz].f32[0];
    d = float32_mul(a, b, &env->vfp.fp_status);
    c = float32_sub(d, c, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

/* fmuls.32 */
void FPUV3_HELPER(fnmacs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = env->vfp.reg[vrz].f32[0];
    d = float32_mul(a, b , &env->vfp.fp_status);
    c = float32_sub(c, d , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

/* fpu v1 */
/* fnmula.32 */
void FPUV3_HELPER(fnmscs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = env->vfp.reg[vrz].f32[0];
    c = float32_chs(c);
    d = float32_mul(a, b, &env->vfp.fp_status);
    c = float32_sub(c, d, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(ffmula32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = env->vfp.reg[vrz].f32[0];
    c = float32_muladd(a, b, c, 0, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(ffmuls32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = env->vfp.reg[vrz].f32[0];
    c = float32_muladd(a, b, c, float_muladd_negate_product,
                       &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(ffnmula32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = env->vfp.reg[vrz].f32[0];
    c = float32_muladd(a, b, c, float_muladd_negate_c |
                       float_muladd_negate_product,
                       &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(ffnmuls32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = env->vfp.reg[vrz].f32[0];
    c = float32_muladd(a, b, c, float_muladd_negate_c,
                       &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fdivs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];
    c = float32_div(a, b , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(frecips)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = float32_one;
    c = float32_div(b, a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fsqrts)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    c = float32_sqrt(a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fsel32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float32 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    b = env->vfp.reg[vry].f32[0];

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    if (env->psr_c == 1) {
        env->vfp.reg[vrz].f32[0] = a;
    } else {
        env->vfp.reg[vrz].f32[0] = b;
    }
}

void FPUV3_HELPER(fins32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    c = env->vfp.reg[vrz].f32[0];

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = ((a & 0xffff) << 16) | (c & 0xffff);
}

void FPUV3_HELPER(faddd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = float64_add(a, b , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(fsubd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = float64_sub(a, b , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(fmovd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = a;
}

void FPUV3_HELPER(fmovx32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = (a >> 16) & 0xffff;
}

void FPUV3_HELPER(fabsd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    c = float64_abs(a);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(fnegd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    c = float64_chs(a);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(fcmpzhsd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = float64_zero;

    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmpzltd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = float64_zero;

    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmpzned)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = float64_zero;

    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
}

void FPUV3_HELPER(fcmpzuod)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float64 a;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];

    if (float64_is_any_nan(a)) {
        if (float64_is_signaling_nan(a, &env->vfp.fp_status)) {
            float_raise(float_flag_invalid, &env->vfp.fp_status);
        }
        env->psr_c = 1;
    } else {
        env->psr_c = 0;
    }
}

void FPUV3_HELPER(fcmphsd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];

    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmpltd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];

    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmpned)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];

    switch (float64_compare_quiet(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 1;
        break;
    }
}

void FPUV3_HELPER(fcmpuod)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];

    if (float64_is_any_nan(a) || float64_is_any_nan(b)) {
        if (float64_is_signaling_nan(a, &env->vfp.fp_status)
            || float64_is_signaling_nan(b, &env->vfp.fp_status)) {
            float_raise(float_flag_invalid, &env->vfp.fp_status);
        }
        env->psr_c = 1;
    } else {
        env->psr_c = 0;
    }
}

void FPUV3_HELPER(fmaxnm64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = float64_maxnum(a, b , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(fminnm64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = float64_minnum(a, b , &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(fcmphz64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = float64_zero;

    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 0;
        break;
    case -1:
        env->psr_c = 0;
        break;
    case 1:
        env->psr_c = 1;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fcmplsz64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = float64_zero;

    switch (float64_compare(a, b, &env->vfp.fp_status)) {
    case 0:
        env->psr_c = 1;
        break;
    case -1:
        env->psr_c = 1;
        break;
    case 1:
        env->psr_c = 0;
        break;
    case 2:
    default:
        env->psr_c = 0;
        break;
    }
}

void FPUV3_HELPER(fmuld)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = float64_mul(a, b, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

/* fpu v1 */
void FPUV3_HELPER(fnmuld)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    d = float64_mul(a, b, &env->vfp.fp_status);
    c = float64_chs(d);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

/* fmula.64 */
void FPUV3_HELPER(fmacd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = env->vfp.reg[vrz].f64[0];
    d = float64_mul(a, b, &env->vfp.fp_status);
    c = float64_add(d, c, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

/* fnmuls.64*/
void FPUV3_HELPER(fmscd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = env->vfp.reg[vrz].f64[0];
    d = float64_mul(a, b, &env->vfp.fp_status);
    c = float64_sub(d, c, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

/* fmuls.64 */
void FPUV3_HELPER(fnmacd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = env->vfp.reg[vrz].f64[0];
    d = float64_mul(a, b, &env->vfp.fp_status);
    c = float64_sub(c, d, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

/* fnmula.64 */
void FPUV3_HELPER(fnmscd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = env->vfp.reg[vrz].f64[0];
    c = float64_chs(c);
    d = float64_mul(a, b, &env->vfp.fp_status);
    c = float64_sub(c, d, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(ffmula64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = env->vfp.reg[vrz].f64[0];
    c = float64_muladd(a, b, c, 0, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(ffmuls64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = env->vfp.reg[vrz].f64[0];
    c = float64_muladd(a, b, c, float_muladd_negate_product,
                       &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(ffnmula64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = env->vfp.reg[vrz].f64[0];
    c = float64_muladd(a, b, c, float_muladd_negate_c |
                       float_muladd_negate_product,
                       &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(ffnmuls64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = env->vfp.reg[vrz].f64[0];
    c = float64_muladd(a, b, c, float_muladd_negate_c,
                       &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(fdivd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];
    c = float64_div(a, b, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(frecipd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a, b, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = float64_one;
    c = float64_div(b, a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(fsqrtd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a, c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    c = float64_sqrt(a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

void FPUV3_HELPER(fsel64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    float64 a, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    b = env->vfp.reg[vry].f64[0];

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    if (env->psr_c == 1) {
        env->vfp.reg[vrz].f64[0] = a;
    } else {
        env->vfp.reg[vrz].f64[0] = b;
    }
}

void FPUV3_HELPER(fhtosirn)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;
    int c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
    c = float16_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}


void FPUV3_HELPER(fhtosirz)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;
    int c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
    c = float16_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;


    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}


void FPUV3_HELPER(fhtosirpi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;
    int c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    env->vfp.fp_status.float_rounding_mode = float_round_up;
    c = float16_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}

void FPUV3_HELPER(fhtosirni)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;
    int c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    env->vfp.fp_status.float_rounding_mode = float_round_down;
    c = float16_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}

void FPUV3_HELPER(fhtouirn)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
    c = float16_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
}

void FPUV3_HELPER(fhtouirz)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
    c = float16_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
}


void FPUV3_HELPER(fhtouirpi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    env->vfp.fp_status.float_rounding_mode = float_round_up;
    c = float16_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
}


void FPUV3_HELPER(fhtouirni)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    env->vfp.fp_status.float_rounding_mode = float_round_down;
    c = float16_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
}


void FPUV3_HELPER(fstosirn)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    int32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
    c = float32_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}


void FPUV3_HELPER(fstosirz)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    int32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
    c = float32_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}


void FPUV3_HELPER(fstosirpi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    int32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    env->vfp.fp_status.float_rounding_mode = float_round_up;
    c = float32_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}

void FPUV3_HELPER(fstosirni)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    int32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    env->vfp.fp_status.float_rounding_mode = float_round_down;
    c = float32_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}

void FPUV3_HELPER(fstouirn)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
    c = float32_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}

void FPUV3_HELPER(fstouirz)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
    c = float32_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}


void FPUV3_HELPER(fstouirpi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    env->vfp.fp_status.float_rounding_mode = float_round_up;
    c = float32_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}


void FPUV3_HELPER(fstouirni)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    env->vfp.fp_status.float_rounding_mode = float_round_down;
    c = float32_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}

void FPUV3_HELPER(fdtosirn)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;
    int32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
    c = float64_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}


void FPUV3_HELPER(fdtosirz)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;
    int32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
    c = float64_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}


void FPUV3_HELPER(fdtosirpi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;
    int32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    env->vfp.fp_status.float_rounding_mode = float_round_up;
    c = float64_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}

void FPUV3_HELPER(fdtosirni)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;
    int32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    env->vfp.fp_status.float_rounding_mode = float_round_down;
    c = float64_to_int32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].dspi[0] = c;
}

void FPUV3_HELPER(fdtouirn)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
    c = float64_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
}

void FPUV3_HELPER(fdtouirz)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
    c = float64_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
}

void FPUV3_HELPER(fdtouirpi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    env->vfp.fp_status.float_rounding_mode = float_round_up;
    c = float64_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
}

void FPUV3_HELPER(fdtouirni)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;
    uint32_t c;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    env->vfp.fp_status.float_rounding_mode = float_round_down;
    c = float64_to_uint32(a, &env->vfp.fp_status);
    env->vfp.fp_status.float_rounding_mode = round_mode;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
}

void FPUV3_HELPER(fsitos)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    int32_t a;
    float32 c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].dspi[0];
    c = int32_to_float32(a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fuitos)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a;
    float32 c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].udspi[0];
    c = uint32_to_float32(a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fhtos)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 a;
    float32 c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f16[0];
    c = float16_to_float32(a, 1, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}

void FPUV3_HELPER(fstoh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    float16 c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    c = float32_to_float16(a, 1, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f16[0] = c;
}


void FPUV3_HELPER(fsitod)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    int32_t a;
    float64 c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].dspi[0];
    c = int32_to_float64(a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}


void FPUV3_HELPER(fuitod)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a;
    float64 c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].udspi[0];
    c = uint32_to_float64(a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}


void FPUV3_HELPER(fdtos)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float64 a;
    float32 c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f64[0];
    c = float64_to_float32(a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f32[0] = c;
}


void FPUV3_HELPER(fstod)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float32 a;
    float64 c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].f32[0];
    c = float32_to_float64(a, &env->vfp.fp_status);

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].f64[0] = c;
}

/* fpu v1 */
void FPUV3_HELPER(fmfvrh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].udspi[1];
    env->regs[vrz] = a;
}

/* fmfvr.32.1 */
void FPUV3_HELPER(fmfvrl)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, rz;
    uint32_t a;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    rz = (insn >> CSKY_FPUV3_REG_SHI_RZ) & CSKY_FPUV3_REG_MASK;

    a = env->vfp.reg[vrx].udspi[0];
    env->regs[rz] = a;
}

/* fpu v1 */
void FPUV3_HELPER(fmtvrh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->regs[vrx];

    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[1] = a;
}

/* fmtvr.32.1 */
void FPUV3_HELPER(fmtvrl)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, vrz;
    uint32_t a;

    rx = (insn >> CSKY_FPUV3_REG_SHI_RX) & CSKY_FPUV3_REG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->regs[rx];

    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = a;
}


void FPUV3_HELPER(fmfvr64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, vrz;
    uint64_t a;

    rx = (insn >> CSKY_FPUV3_REG_SHI_RZ) & CSKY_FPUV3_REG_MASK;
    ry = (insn >> CSKY_FPUV3_REG_SHI_RY) & CSKY_FPUV3_REG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrz].udspl[0];
    env->regs[rx] = a & 0xffffffff;
    env->regs[ry] = (a >> 32) & 0xffffffff;
}


void FPUV3_HELPER(fmfvr16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, vrx;
    uint16_t a;

    rx = (insn >> CSKY_FPUV3_REG_SHI_RZ) & CSKY_FPUV3_REG_MASK;
    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].udsps[0];
    env->regs[rx] = a & 0xffff;
}


void FPUV3_HELPER(fmfvr322)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, vrx;
    uint32_t a;

    rx = (insn >> CSKY_FPUV3_REG_SHI_RZ) & CSKY_FPUV3_REG_MASK;
    ry = (insn >> CSKY_FPUV3_REG_SHI_RY) & CSKY_FPUV3_REG_MASK;
    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;

    a = env->vfp.reg[vrx].udspi[0];
    env->regs[rx] = a;
    a = env->vfp.reg[vrx + 1].udspi[0];
    env->regs[ry] = a;
}


void FPUV3_HELPER(fmtvr64)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, vrz;
    uint32_t a, b;

    rx = (insn >> CSKY_FPUV3_REG_SHI_RX) & CSKY_FPUV3_REG_MASK;
    ry = (insn >> CSKY_FPUV3_REG_SHI_RY) & CSKY_FPUV3_REG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->regs[rx];
    b = env->regs[ry];

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspl[0] = ((uint64_t)b << 32) | a;
}

void FPUV3_HELPER(fmtvr16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, vrz;
    uint32_t a;

    rx = (insn >> CSKY_FPUV3_REG_SHI_RX) & CSKY_FPUV3_REG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->regs[rx];

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udsps[0] = a & 0xffff;
}


void FPUV3_HELPER(fmtvr322)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, vrz;
    float32 a;

    rx = (insn >> CSKY_FPUV3_REG_SHI_RX) & CSKY_FPUV3_REG_MASK;
    ry = (insn >> CSKY_FPUV3_REG_SHI_RY) & CSKY_FPUV3_REG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    a = env->regs[rx];

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = a;

    a = env->regs[ry];

    env->vfp.reg[vrz + 1].f64[0] = 0;
    env->vfp.reg[vrz + 1].f64[1] = 0;
    env->vfp.reg[vrz + 1].udspi[0] = a;
}

void FPUV3_HELPER(fldh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, offset;
    uint16_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn & 0xf) | ((insn >> 21) & 0x10);
    offset = (((insn >> 4) & 0xf) | ((insn >> 17) & 0xf0)) << 1;

    a = env->regs[vrx];
    c = csky_cpu_lduw_data_ra(env, a + offset, GETPC());
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udsps[0] = c;

    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, 2, a + offset);
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}

void FPUV3_HELPER(fsth)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, offset;
    uint16_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn & 0xf) | ((insn >> 21) & 0x10);
    offset = (((insn >> 4) & 0xf) | ((insn >> 17) & 0xf0)) << 1;

    a = env->regs[vrx];
    c = env->vfp.reg[vrz].udsps[0];
    csky_cpu_stw_data_ra(env, a + offset, c, GETPC());
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, 2, a + offset);
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}

void FPUV3_HELPER(fldrh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    uint32_t a, b, shift;
    uint16_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    shift = (insn >> 5) & 0x3;

    a = env->regs[vrx];
    b = env->regs[vry];
    c = csky_cpu_lduw_data_ra(env, a + (b << shift), GETPC());

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udsps[0] = c;
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, 2, a + (b << shift));
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}
void FPUV3_HELPER(fstrh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    uint32_t a, b, shift;
    uint16_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    shift = (insn >> 5) & 0x3;

    a = env->regs[vrx];
    b = env->regs[vry];
    c = env->vfp.reg[vrz].udsps[0];
    csky_cpu_stw_data_ra(env, a + (b << shift), c, GETPC());
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, 2, a + (b << shift));
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}

void FPUV3_HELPER(fldmh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint16_t t[32];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];
    for (i = 0; (i < imm) && (i + vrz < 32); i++) {
        t[i] = csky_cpu_lduw_data_ra(env, a + i * 2, GETPC());
    }
    while (i > 0) {
        i--;
        env->vfp.reg[vrz + i].f64[0] = 0;
        env->vfp.reg[vrz + i].f64[1] = 0;
        env->vfp.reg[vrz + i].udsps[0] = t[i];
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packnum, packlen, value;
        bytes = 2 * imm;
        packnum = bytes / 4;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, a);
        for (i = 0; i < packnum; i++) {
            value = (env->vfp.reg[vrz + 2 * i + 1].udsps[0] << 16)
                    | env->vfp.reg[vrz + 2 * i].udsps[0];
            write_trace_8_8(DATA_VALUE, packlen, 0, value);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + 2 * i].udsps[0]);
        }
    }
}


void FPUV3_HELPER(fstmh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint16_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];
    probe_pages(env, a, 2 * imm, GETPC());
    for (i = 0; i < imm; i++) {
        if (i + vrz >= 32) {
            break;
        }

        c = env->vfp.reg[vrz + i].udsps[0];
        csky_cpu_stw_data_ra(env, a + i * 2, c, GETPC());
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packnum, packlen, value;
        bytes = 2 * imm;
        packnum = bytes / 4;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, a);
        for (i = 0; i < packnum; i++) {
            value = (env->vfp.reg[vrz + 2 * i + 1].udsps[0] << 16)
                    | env->vfp.reg[vrz + 2 * i].udsps[0];
            write_trace_8_8(DATA_VALUE, packlen, 0, value);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + 2 * i].udsps[0]);
        }
    }
}


void FPUV3_HELPER(flrwh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrz;
    uint32_t offset;
    uint16_t c;

    vrz = (insn & 0xf) | ((insn >> 21) & 0x10);
    offset = (((insn >> 4) & 0xf) | ((insn >> 17) & 0xf0)) << 2;

    c = csky_cpu_lduw_data_ra(env, (env->pc + offset) & 0xfffffffc, GETPC());
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udsps[0] = c;
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, 2, (env->pc + offset) & 0xfffffffc);
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}


void FPUV3_HELPER(fldmuh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint16_t t[32];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];
    for (i = 0; (i < imm) && (i + vrz < 32); i++) {
        t[i] = csky_cpu_lduw_data_ra(env, a, GETPC());
        a = a + 2;
    }
    while (i > 0) {
        i--;
        env->vfp.reg[vrz + i].f64[0] = 0;
        env->vfp.reg[vrz + i].f64[1] = 0;
        env->vfp.reg[vrz + i].udsps[0] = t[i];
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packnum, packlen, value;
        bytes = 2 * imm;
        packnum = bytes / 4;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, env->regs[vrx]);
        for (i = 0; i < packnum; i++) {
            value = (env->vfp.reg[vrz + 2 * i + 1].udsps[0] << 16)
                    | env->vfp.reg[vrz + 2 * i].udsps[0];
            write_trace_8_8(DATA_VALUE, packlen, 0, value);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + 2 * i].udsps[0]);
        }
    }
    env->regs[vrx] = a;
}


void FPUV3_HELPER(fstmuh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint16_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];
    probe_pages(env, a, 2 * imm, GETPC());
    for (i = 0; i < imm; i++) {
        if (i + vrz >= 32) {
            break;
        }
        c = env->vfp.reg[vrz + i].udsps[0];
        csky_cpu_stw_data_ra(env, a, c, GETPC());

        a = a + 2;
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packnum, packlen, value;
        bytes = 2 * imm;
        packnum = bytes / 4;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, env->regs[vrx]);
        for (i = 0; i < packnum; i++) {
            value = (env->vfp.reg[vrz + 2 * i + 1].udsps[0] << 16)
                    | env->vfp.reg[vrz + 2 * i].udsps[0];
            write_trace_8_8(DATA_VALUE, packlen, 0, value);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + 2 * i].udsps[0]);
        }
    }
    env->regs[vrx] = a;
}


void FPUV3_HELPER(flds)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, offset;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn & 0xf) | ((insn >> 21) & 0x10);
    offset = (((insn >> 4) & 0xf) | ((insn >> 17) & 0xf0)) << 2;

    a = env->regs[vrx];
    c = csky_cpu_ldl_data_ra(env, a + offset, GETPC());
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, 4, a + offset);
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}


void FPUV3_HELPER(fsts)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, offset;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn & 0xf) | ((insn >> 21) & 0x10);
    offset = (((insn >> 4) & 0xf) | ((insn >> 17) & 0xf0)) << 2;

    a = env->regs[vrx];
    c = env->vfp.reg[vrz].udspi[0];
    csky_cpu_stl_data_ra(env, a + offset, c, GETPC());
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, 4, a + offset);
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}


void FPUV3_HELPER(fldrs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    uint32_t a, b, shift;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    shift = (insn >> 5) & 0x3;

    a = env->regs[vrx];
    b = env->regs[vry];
    c = csky_cpu_ldl_data_ra(env, a + (b << shift), GETPC());
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, 4, a + (b << shift));
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}
void FPUV3_HELPER(fstrs)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    uint32_t a, b, shift;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    shift = (insn >> 5) & 0x3;

    a = env->regs[vrx];
    b = env->regs[vry];
    c = env->vfp.reg[vrz].udspi[0];
    csky_cpu_stl_data_ra(env, a + (b << shift), c, GETPC());
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, 4, a + (b << shift));
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}

void FPUV3_HELPER(fldms)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint32_t t[32];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];
    for (i = 0; (i < imm) && (i + vrz < 32); i++) {
        t[i] = csky_cpu_ldl_data_ra(env, a + i * 4, GETPC());
    }
    while (i > 0) {
        i--;
        env->vfp.reg[vrz + i].f64[0] = 0;
        env->vfp.reg[vrz + i].f64[1] = 0;
        env->vfp.reg[vrz + i].udspi[0] = t[i];
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packlen;
        bytes = 4 * imm;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, a);
        for (i = 0; i < imm; i++) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[0]);
        }
    }
}


void FPUV3_HELPER(fstms)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];
    probe_pages(env, a, 4 * imm, GETPC());
    for (i = 0; i < imm; i++) {
        if (i + vrz >= 32) {
            break;
        }
        c = env->vfp.reg[vrz + i].udspi[0];
        csky_cpu_stl_data_ra(env, a + i * 4, c, GETPC());
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packlen;
        bytes = 4 * imm;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, a);
        for (i = 0; i < imm; i++) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[0]);
        }
    }
}


void FPUV3_HELPER(flrws)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrz;
    uint32_t offset;
    uint32_t c;

    vrz = (insn & 0xf) | ((insn >> 21) & 0x10);
    offset = (((insn >> 4) & 0xf) | ((insn >> 17) & 0xf0)) << 2;

    c = csky_cpu_ldl_data_ra(env, (env->pc + offset) & 0xfffffffc, GETPC());
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = c;
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, 4, (env->pc + offset) & 0xfffffffc);
        write_trace_8_8(DATA_VALUE, packlen, 0, c);
    }
}


void FPUV3_HELPER(fldmus)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint32_t t[32];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];
    for (i = 0; (i < imm) && (i + vrz < 32); i++) {
        t[i] = csky_cpu_ldl_data_ra(env, a, GETPC());
        a = a + 4;
    }
    while (i > 0) {
        i--;
        env->vfp.reg[vrz + i].f64[0] = 0;
        env->vfp.reg[vrz + i].f64[1] = 0;
        env->vfp.reg[vrz + i].udspi[0] = t[i];
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packlen;
        bytes = 4 * imm;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, env->regs[vrx]);
        for (i = 0; i < imm; i++) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[0]);
        }
    }
    env->regs[vrx] = a;
}


void FPUV3_HELPER(fstmus)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];
    probe_pages(env, a, 4 * imm, GETPC());
    for (i = 0; i < imm; i++) {
        if (i + vrz >= 32) {
            break;
        }

        c = env->vfp.reg[vrz + i].udspi[0];
        csky_cpu_stl_data_ra(env, a, c, GETPC());

        a = a + 4;
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packlen;
        bytes = 4 * imm;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, env->regs[vrx]);
        for (i = 0; i < imm; i++) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[0]);
        }
    }
    env->regs[vrx] = a;
}

void FPUV3_HELPER(fldd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, offset;
    uint32_t t0, t1;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn & 0xf) | ((insn >> 21) & 0x10);
    offset = (((insn >> 4) & 0xf) | ((insn >> 17) & 0xf0)) << 2;

    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;

    a = env->regs[vrx];
    t0 = csky_cpu_ldl_data_ra(env, a + offset, GETPC());
    t1 = csky_cpu_ldl_data_ra(env, a + offset + 4, GETPC());
    env->vfp.reg[vrz].udspi[0] = t0;
    env->vfp.reg[vrz].udspi[1] = t1;

#if TARGET_BIG_ENDIAN
    swap_endian(&env->vfp.reg[vrz].udspi[0], &env->vfp.reg[vrz].udspi[1], 4);
#endif
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, 8, a + offset);
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[0]);
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[1]);
    }
}

void FPUV3_HELPER(fstd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, offset;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn & 0xf) | ((insn >> 21) & 0x10);
    offset = (((insn >> 4) & 0xf) | ((insn >> 17) & 0xf0)) << 2;

    a = env->regs[vrx];

    probe_pages(env, a + offset, 8, GETPC());
#ifndef TARGET_BIG_ENDIAN
    c = env->vfp.reg[vrz].udspi[0];
    csky_cpu_stl_data_ra(env, a + offset, c, GETPC());
    c = env->vfp.reg[vrz].udspi[1];
    csky_cpu_stl_data_ra(env, a + offset + 4, c, GETPC());
#else
    c = env->vfp.reg[vrz].udspi[1];
    csky_cpu_stl_data_ra(env, a + offset, c, GETPC());
    c = env->vfp.reg[vrz].udspi[0];
    csky_cpu_stl_data_ra(env, a + offset + 4, c, GETPC());
#endif
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, 8, a + offset);
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[0]);
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[1]);
    }
}

void FPUV3_HELPER(fldrd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    uint32_t a, b, shift;
    uint32_t t0, t1;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    shift = (insn >> 5) & 0x3;

    a = env->regs[vrx];
    b = env->regs[vry];
    t0 = csky_cpu_ldl_data_ra(env, a + (b << shift), GETPC());
    t1 = csky_cpu_ldl_data_ra(env, a + (b << shift) + 4, GETPC());
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = t0;
    env->vfp.reg[vrz].udspi[1] = t1;
#if TARGET_BIG_ENDIAN
    swap_endian(&env->vfp.reg[vrz].udspi[0], &env->vfp.reg[vrz].udspi[1], 4);
#endif
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, 8, a + (b << shift));
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[0]);
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[1]);
    }
}

void FPUV3_HELPER(fstrd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz;
    uint32_t a, b, shift;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    shift = (insn >> 5) & 0x3;

    a = env->regs[vrx];
    b = env->regs[vry];
    probe_pages(env, a + (b << shift), 8, GETPC());
#ifndef TARGET_BIG_ENDIAN
    c = env->vfp.reg[vrz].udspi[0];
    csky_cpu_stl_data_ra(env, a + (b << shift), c, GETPC());
    c = env->vfp.reg[vrz].udspi[1];
    csky_cpu_stl_data_ra(env, a + (b << shift) + 4, c, GETPC());
#else
    c = env->vfp.reg[vrz].udspi[1];
    csky_cpu_stl_data_ra(env, a + (b << shift), c, GETPC());
    c = env->vfp.reg[vrz].udspi[0];
    csky_cpu_stl_data_ra(env, a + (b << shift) + 4, c, GETPC());
#endif
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, 8, a + (b << shift));
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[0]);
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[1]);
    }
}

void FPUV3_HELPER(fldmd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint32_t t[2][32];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];

    for (i = 0; (i < imm) && (i + vrz < 32); i++) {
        t[0][i] = csky_cpu_ldl_data_ra(env, a + i * 8, GETPC());
        t[1][i] = csky_cpu_ldl_data_ra(env, a + i * 8 + 4, GETPC());
    }
    while (i > 0) {
        i--;
        env->vfp.reg[vrz + i].f64[0] = 0;
        env->vfp.reg[vrz + i].f64[1] = 0;
        env->vfp.reg[vrz + i].udspi[0] = t[0][i];
        env->vfp.reg[vrz + i].udspi[1] = t[1][i];
#if TARGET_BIG_ENDIAN
        swap_endian(&env->vfp.reg[vrz + i].udspi[0],
            &env->vfp.reg[vrz + i].udspi[1], 4);
#endif
    }

    if (gen_mem_trace()) {
        uint32_t bytes, packlen;
        bytes = 8 * imm;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, a);
        for (i = 0; i < imm; i++) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[0]);
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[1]);
        }
    }
}


void FPUV3_HELPER(fstmd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];

    probe_pages(env, a, 8 * imm, GETPC());
    for (i = 0; i < imm; i++) {
        if (i + vrz >= 32) {
            break;
        }
#ifndef TARGET_BIG_ENDIAN
        c = env->vfp.reg[vrz + i].udspi[0];
        csky_cpu_stl_data_ra(env, a + i * 8, c, GETPC());
        c = env->vfp.reg[vrz + i].udspi[1];
        csky_cpu_stl_data_ra(env, a + i * 8 + 4, c, GETPC());
#else
        c = env->vfp.reg[vrz + i].udspi[1];
        csky_cpu_stl_data_ra(env, a + i * 8, c, GETPC());
        c = env->vfp.reg[vrz + i].udspi[0];
        csky_cpu_stl_data_ra(env, a + i * 8 + 4, c, GETPC());
#endif
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packlen;
        bytes = 8 * imm;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, a);
        for (i = 0; i < imm; i++) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[0]);
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[1]);
        }
    }
}


void FPUV3_HELPER(flrwd)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrz;
    uint32_t offset;
    uint32_t t0, t1;

    vrz = (insn & 0xf) | ((insn >> 21) & 0x10);
    offset = ((((insn >> 4) & 0xf) | ((insn >> 17) & 0xf0))) << 2;

    t0 = csky_cpu_ldl_data_ra(env, (env->pc + offset) & 0xfffffffc, GETPC());
    t1 = csky_cpu_ldl_data_ra(env, (env->pc + offset + 4) & 0xfffffffc, GETPC());
    env->vfp.reg[vrz].f64[0] = 0;
    env->vfp.reg[vrz].f64[1] = 0;
    env->vfp.reg[vrz].udspi[0] = t0;
    env->vfp.reg[vrz].udspi[1] = t1;
    if (gen_mem_trace()) {
        uint32_t packlen;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, 8, (env->pc + offset) & 0xfffffffc);
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[0]);
        write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vrz].udspi[1]);
    }
}


void FPUV3_HELPER(fldmud)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, imm;
    uint32_t a, i;
    uint32_t t[2][32];

    vrx = (insn >> CSKY_FPUV3_REG_SHI_RX) & CSKY_FPUV3_REG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];
    for (i = 0; (i < imm) && (i + vrz < 32); i++) {
        t[0][i] = csky_cpu_ldl_data_ra(env, a, GETPC());
        t[1][i] = csky_cpu_ldl_data_ra(env, a + 4, GETPC());
        a = a + 8;
    }
    while (i > 0) {
        i--;
        env->vfp.reg[vrz + i].f64[0] = 0;
        env->vfp.reg[vrz + i].f64[1] = 0;
        env->vfp.reg[vrz + i].udspi[0] = t[0][i];
        env->vfp.reg[vrz + i].udspi[1] = t[1][i];
 #if TARGET_BIG_ENDIAN
        swap_endian(&env->vfp.reg[vrz + i].udspi[0],
                    &env->vfp.reg[vrz + i].udspi[1], 4);
#endif
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packlen;
        bytes = 8 * imm;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, env->regs[vrx]);
        for (i = 0; i < imm; i++) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[0]);
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[1]);
        }
    }
    env->regs[vrx] = a;
}

void FPUV3_HELPER(fstmud)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    uint32_t a, i, imm;
    uint32_t c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = ((insn >> 21) & 0x1f) + 1;

    a = env->regs[vrx];

    probe_pages(env, a, 8 * imm, GETPC());
    for (i = 0; i < imm; i++) {
        if (i + vrz >= 32) {
            break;
        }

#ifndef TARGET_BIG_ENDIAN
        c = env->vfp.reg[vrz + i].udspi[0];
        csky_cpu_stl_data_ra(env, a, c, GETPC());
        c = env->vfp.reg[vrz + i].udspi[1];
        csky_cpu_stl_data_ra(env, a + 4, c, GETPC());
#else
        c = env->vfp.reg[vrz + i].udspi[1];
        csky_cpu_stl_data_ra(env, a, c, GETPC());
        c = env->vfp.reg[vrz + i].udspi[0];
        csky_cpu_stl_data_ra(env, a + 4, c, GETPC());
#endif
        a = a + 8;
    }
    if (gen_mem_trace()) {
        uint32_t bytes, packlen;
        bytes = 8 * imm;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, env->regs[vrx]);
        for (i = 0; i < imm; i++) {
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[0]);
            write_trace_8_8(DATA_VALUE, packlen, 0,
                env->vfp.reg[vrz + i].udspi[1]);
        }
    }
    env->regs[vrx] = a;
}

void FPUV3_HELPER(fftox)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, imm;
    uint16_t    res_u16;
    int16_t     res_s16;
    uint32_t    res_u32;
    int32_t     res_s32;
    float16     src_f16;
    float32     src_f32;
    float64     src_f64;
    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    /*
     * | T1 |  T2  | S |
     * +----+------+---+
     * | 8  | 7  6 | 5 |
     */
    result_type = (insn >> 5) & 0xf;
    if (result_type & 0x8) {
        /* T1 = 1, fix32 */
        imm = ((env->vfp.fcr >> 16) & 0x1f) + 1;
    } else {
        /* T1 = 0, fix16 */
        imm = ((env->vfp.fcr >> 16) & 0xf) + 1;
    }

    switch (result_type) {
    case 0x0: /* f16 to fixu16 */
        src_f16 = env->vfp.reg[vrx].f16[0];
        src_f16 = float16_scalbn(src_f16, imm, &env->vfp.fp_status);
        res_u16 = float16_to_uint16(src_f16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].udsps[0] = res_u16;
        break;
    case 0x1: /* f16 to fixs16 */
        src_f16 = env->vfp.reg[vrx].f16[0];
        src_f16 = float16_scalbn(src_f16, imm, &env->vfp.fp_status);
        res_s16 = float16_to_int16(src_f16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].dsps[0] = res_s16;
        break;
    //case 0x2: /* f32 to fixu16 */
    //    src_f32 = env->vfp.reg[vrx].f32[0];
    //    src_f32 = float32_scalbn(src_f32, imm, &env->vfp.fp_status);
    //    res_u16 = float32_to_uint16(src_f32, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].udsps[0] = res_u16;
    //    break;
    //case 0x3: /* f32 to fixs16 */
    //    src_f32 = env->vfp.reg[vrx].f32[0];
    //    src_f32 = float32_scalbn(src_f32, imm, &env->vfp.fp_status);
    //    res_s16 = float32_to_int16(src_f32, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].dsps[0] = res_s16;
    //    break;
    //case 0x4: /* f64 to fixu16 */
    //    src_f64 = env->vfp.reg[vrx].f64[0];
    //    src_f64 = float64_scalbn(src_f64, imm, &env->vfp.fp_status);
    //    res_u16 = float64_to_uint16(src_f64, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].udsps[0] = res_u16;
    //    break;
    //case 0x5: /* f64 to fixs16 */
    //    src_f64 = env->vfp.reg[vrx].f64[0];
    //    src_f64 = float64_scalbn(src_f64, imm, &env->vfp.fp_status);
    //    res_s16 = float64_to_int16(src_f64, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].dsps[0] = res_s16;
    //    break;
    case 0x8: /* f16 to fixu32 */
        src_f16 = env->vfp.reg[vrx].f16[0];
        src_f16 = float16_scalbn(src_f16, imm, &env->vfp.fp_status);
        res_u32 = float16_to_uint32(src_f16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].udspi[0] = res_u32;
        break;
    case 0x9: /* f16 to fixs32 */
        src_f16 = env->vfp.reg[vrx].f16[0];
        src_f16 = float16_scalbn(src_f16, imm, &env->vfp.fp_status);
        res_s32 = float16_to_int32(src_f16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].dspi[0] = res_s32;
        break;
    case 0xa: /* f32 to fixu32 */
        src_f32 = env->vfp.reg[vrx].f32[0];
        src_f32 = float32_scalbn(src_f32, imm, &env->vfp.fp_status);
        res_u32 = float32_to_uint32(src_f32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].udspi[0] = res_u32;
        break;
    case 0xb: /* f32 to fixs32 */
        src_f32 = env->vfp.reg[vrx].f32[0];
        src_f32 = float32_scalbn(src_f32, imm, &env->vfp.fp_status);
        res_s32 = float32_to_int32(src_f32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].dspi[0] = res_s32;
        break;
    case 0xc: /* f64 to fixu32 */
        src_f64 = env->vfp.reg[vrx].f64[0];
        src_f64 = float64_scalbn(src_f64, imm, &env->vfp.fp_status);
        res_u32 = float64_to_uint32(src_f64, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].udspi[0] = res_u32;
        break;
    case 0xd: /* f64 to fixs32 */
        src_f64 = env->vfp.reg[vrx].f64[0];
        src_f64 = float64_scalbn(src_f64, imm, &env->vfp.fp_status);
        res_s32 = float64_to_int32(src_f64, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].dspi[0] = res_s32;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}

void FPUV3_HELPER(fxtof)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, imm;
    uint16_t    src_u16;
    int16_t     src_s16;
    uint32_t    src_u32;
    int32_t     src_s32;
    float16     res_f16;
    float32     res_f32;
    float64     res_f64;
    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    /*
     * | T1 |  T2  | S |
     * +----+------+---+
     * | 8  | 7  6 | 5 |
     */
    result_type = (insn >> 5) & 0xf;
    if (result_type & 0x8) {
        /* T1 = 1, fix32 */
        imm = ((env->vfp.fcr >> 16) & 0x1f) + 1;
    } else {
        /* T1 = 0, fix16 */
        imm = ((env->vfp.fcr >> 16) & 0xf) + 1;
    }

    switch (result_type) {
    case 0x0: /* fixu16 to f16 */
        src_u16 = env->vfp.reg[vrx].udsps[0];
        res_f16 = uint16_to_float16(src_u16, &env->vfp.fp_status);
        res_f16 = float16_scalbn(res_f16, -imm, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f16[0] = res_f16;
        break;
    case 0x1: /* fixs16 to f16 */
        src_s16 = env->vfp.reg[vrx].dsps[0];
        res_f16 = int16_to_float16(src_s16, &env->vfp.fp_status);
        res_f16 = float16_scalbn(res_f16, -imm, &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[0] = res_f16;
        break;
    //case 0x2: /* fixu16 to f32 */
    //    src_u16 = env->vfp.reg[vrx].udsps[0];
    //    res_f32 = uint16_to_float32(src_u16, &env->vfp.fp_status);
    //    res_f32 = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].f32[0] = res_f32;
    //    break;
    //case 0x3: /* fixs16 to f32 */
    //    src_s16 = env->vfp.reg[vrx].dsps[0];
    //    res_f32 = int16_to_float32(src_s16, &env->vfp.fp_status);
    //    res_f32 = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].f32[0] = res_f32;
    //    break;
    //case 0x4: /* fixu16 to f64 */
    //    src_u16 = env->vfp.reg[vrx].udsps[0];
    //    res_f64 = uint16_to_float64(src_u16, &env->vfp.fp_status);
    //    res_f64 = float64_scalbn(res_f64, -imm, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].f64[0] = res_f64;
    //    break;
    //case 0x5: /* fixs16 to f64 */
    //    imm &= 0xf;
    //    src_s16 = env->vfp.reg[vrx].dsps[0];
    //    res_f64 = int16_to_float64(src_s16, &env->vfp.fp_status);
    //    res_f64 = float64_scalbn(res_f64, -imm, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].f64[0] = res_f64;
    //    break;
    case 0x8: /* fixu32 to f16 */
        src_u32 = env->vfp.reg[vrx].udspi[0];
        res_f16 = uint32_to_float16(src_u32, &env->vfp.fp_status);
        res_f16 = float16_scalbn(res_f16, -imm, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f16[0] = res_f16;
        break;
    case 0x9: /* fixs32 to f16 */
        src_s32 = env->vfp.reg[vrx].dspi[0];
        res_f16 = int32_to_float16(src_s32, &env->vfp.fp_status);
        res_f16 = float16_scalbn(res_f16, -imm, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f16[0] = res_f16;
        break;
    case 0xa: /* fixu32 to f32 */
        src_u32 = env->vfp.reg[vrx].udspi[0];
        res_f32 = uint32_to_float32(src_u32, &env->vfp.fp_status);
        res_f32 = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f32[0] = res_f32;
        break;
    case 0xb: /* fixs32 to f32 */
        src_s32 = env->vfp.reg[vrx].dsps[0];
        res_f32 = int32_to_float32(src_s32, &env->vfp.fp_status);
        res_f32 = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f32[0] = res_f32;
        break;
    case 0xc: /* fixu32 to f64 */
        src_u32 = env->vfp.reg[vrx].udspi[0];
        res_f64 = uint32_to_float64(src_u32, &env->vfp.fp_status);
        res_f64 = float64_scalbn(res_f64, -imm, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f64[0] = res_f64;
        break;
    case 0xd: /* fixs32 to f64 */
        src_s32 = env->vfp.reg[vrx].dspi[0];
        res_f64 = int32_to_float64(src_s32, &env->vfp.fp_status);
        res_f64 = float64_scalbn(res_f64, -imm, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f64[0] = res_f64;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}

void FPUV3_HELPER(fftoi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 src_f16;
    float32 src_f32;
    float64 src_f64;
    uint16_t res_u16;
    int16_t res_s16;
    uint32_t res_u32;
    int32_t res_s32;

    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    /*
     * | T1 |   T2  | S |
     * +----+---+---+---+
     * | 8  | 7 | 6 | 5 |
     */
    result_type = (insn >> 5) & 0xf;

    switch (result_type) {
    case 0x0: /* f16 to u16 */
        src_f16 = env->vfp.reg[vrx].f16[0];
        res_u16 = float16_to_uint16(src_f16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].udsps[0] = res_u16;
        break;
    case 0x1: /* f16 to s16 */
        src_f16 = env->vfp.reg[vrx].f16[0];
        res_s16 = float16_to_int16(src_f16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].dsps[0] = res_s16;
        break;
    //case 0x2: /* f32 to u16 */
    //    src_f32 = env->vfp.reg[vrx].f32[0];
    //    res_u16 = float32_to_uint16(src_f32, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].udsps[0] = res_u16;
    //    break;
    //case 0x3: /* f32 to s16 */
    //    src_f32 = env->vfp.reg[vrx].f32[0];
    //    res_s16 = float32_to_int16(src_f32, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].dsps[0] = res_s16;
    //    break;
    //case 0x4: /* f64 to u16 */
    //    src_f64 = env->vfp.reg[vrx].f64[0];
    //    res_u16 = float64_to_uint16(src_f64, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].udsps[0] = res_u16;
    //    break;
    //case 0x5: /* f64 to s16 */
    //    src_f64 = env->vfp.reg[vrx].f64[0];
    //    res_s16 = float64_to_int16(src_f64, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].dsps[0] = res_s16;
    //    break;

    case 0x8: /* f16 to u32 */
        src_f16 = env->vfp.reg[vrx].f16[0];
        res_u32 = float16_to_uint32(src_f16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].udspi[0] = res_u32;
        break;
    case 0x9: /* f16 to s32 */
        src_f16 = env->vfp.reg[vrx].f16[0];
        res_s32 = float16_to_int32(src_f16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].dspi[0] = res_s32;
        break;
    case 0xa: /* f32 to u32 */
        src_f32 = env->vfp.reg[vrx].f32[0];
        res_u32 = float32_to_uint32(src_f32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].udspi[0] = res_u32;
        break;
    case 0xb: /* f32 to s32 */
        src_f32 = env->vfp.reg[vrx].f32[0];
        res_s32 = float32_to_int32(src_f32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].dspi[0] = res_s32;
        break;
    case 0xc: /* f64 to u32 */
        src_f64 = env->vfp.reg[vrx].f64[0];
        res_u32 = float64_to_uint32(src_f64, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].udspi[0] = res_u32;
        break;
    case 0xd: /* f64 to s32 */
        src_f64 = env->vfp.reg[vrx].f64[0];
        res_s32 = float64_to_int32(src_f64, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].dspi[0] = res_s32;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}


void FPUV3_HELPER(fitof)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 res_f16;
    float32 res_f32;
    float64 res_f64;
    uint16_t src_u16;
    int16_t  src_s16;
    uint32_t src_u32;
    int32_t  src_s32;

    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    /*
     * | T1 |   T2  | S |
     * +----+---+---+---+
     * | 8  | 7 | 6 | 5 |
     */
    result_type = (insn >> 5) & 0xf;

    switch (result_type) {
    case 0x0: /* u16 to f16 */
        src_u16 = env->vfp.reg[vrx].udsps[0];
        res_f16 = uint16_to_float16(src_u16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f16[0] = res_f16;
        break;
    case 0x1: /* s16 to f16 */
        src_s16 = env->vfp.reg[vrx].dsps[0];
        res_f16 = int16_to_float16(src_s16, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f16[0] = res_f16;
        break;
    //case 0x2: /* u16 to f32 */
    //    src_u16 = env->vfp.reg[vrx].udsps[0];
    //    res_f32 = uint16_to_float32(src_u16, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].f32[0] = res_f32;
    //    break;
    //case 0x3: /* s16 to f32 */
    //    src_s16 = env->vfp.reg[vrx].dsps[0];
    //    res_f32 = int16_to_float32(src_s16, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].f32[0] = res_f32;
    //    break;
    //case 0x4: /* u16 to f64 */
    //    src_u16 = env->vfp.reg[vrx].udsps[0];
    //    res_f64 = uint16_to_float64(src_u16, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].f64[0] = res_f64;
    //    break;
    //case 0x5: /* s16 to f64 */
    //    src_s16 = env->vfp.reg[vrx].dsps[0];
    //    res_f64 = int16_to_float64(src_s16, &env->vfp.fp_status);
    //    env->vfp.reg[vrz].f64[0] = 0;
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    env->vfp.reg[vrz].f64[0] = res_f64;
    //    break;

    case 0x8: /* u32 to f16 */
        src_u32 = env->vfp.reg[vrx].udspi[0];
        res_f16 = uint32_to_float16(src_u32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f16[0] = res_f16;
        break;
    case 0x9: /* s32 to f16 */
        src_s32 = env->vfp.reg[vrx].dspi[0];
        res_f16 = int32_to_float16(src_s32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f16[0] = res_f16;
        break;
    case 0xa: /* u32 to f32 */
        src_u32 = env->vfp.reg[vrx].udspi[0];
        res_f32 = uint32_to_float32(src_u32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f32[0] = res_f32;
        break;
    case 0xb: /* s32 to f32 */
        src_s32 = env->vfp.reg[vrx].dspi[0];
        res_f32 = int32_to_float32(src_s32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f32[0] = res_f32;
        break;
    case 0xc: /* u32 to f64 */
        src_u32 = env->vfp.reg[vrx].udspi[0];
        res_f64 = uint32_to_float64(src_u32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f64[0] = res_f64;
        break;
    case 0xd: /* s32 to f64 */
        src_s32 = env->vfp.reg[vrx].dspi[0];
        res_f64 = int32_to_float64(src_s32, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f64[0] = res_f64;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}


void FPUV3_HELPER(fftofi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;
    float16 src_f16;
    float32 src_f32;
    float64 src_f64;
    uint32_t type, rm;
    int round_mode;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    type = (insn >> 7) & 0x3;
    rm = (insn >> 5) & 0x3;

    round_mode = env->vfp.fp_status.float_rounding_mode;

    switch (type) {
    case 0: /* f16 */
        switch (rm) {
        case 0:
            src_f16 = env->vfp.reg[vrx].f16[0];
            env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
            src_f16 = float16_round_to_int(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f16[0] = src_f16;
            break;
        case 1:
            src_f16 = env->vfp.reg[vrx].f16[0];
            env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
            src_f16 = float16_round_to_int(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f16[0] = src_f16;
            break;
        case 2:
            src_f16 = env->vfp.reg[vrx].f16[0];
            env->vfp.fp_status.float_rounding_mode = float_round_up;
            src_f16 = float16_round_to_int(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f16[0] = src_f16;
            break;
        case 3:
            src_f16 = env->vfp.reg[vrx].f16[0];
            env->vfp.fp_status.float_rounding_mode = float_round_down;
            src_f16 = float16_round_to_int(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f16[0] = src_f16;
            break;
        default:
            break;
        }
        break;
    case 1: /* f32 */
        switch (rm) {
        case 0:
            src_f32 = env->vfp.reg[vrx].f32[0];
            env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
            src_f32 = float32_round_to_int(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f32[0] = src_f32;
            break;
        case 1:
            src_f32 = env->vfp.reg[vrx].f32[0];
            env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
            src_f32 = float32_round_to_int(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f32[0] = src_f32;
            break;
        case 2:
            src_f32 = env->vfp.reg[vrx].f32[0];
            env->vfp.fp_status.float_rounding_mode = float_round_up;
            src_f32 = float32_round_to_int(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f32[0] = src_f32;
            break;
        case 3:
            src_f32 = env->vfp.reg[vrx].f32[0];
            env->vfp.fp_status.float_rounding_mode = float_round_down;
            src_f32 = float32_round_to_int(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f32[0] = src_f32;
            break;
        default:
            break;
        }
        break;
    case 2: /* f64 */
        switch (rm) {
        case 0:
            src_f64 = env->vfp.reg[vrx].f64[0];
            env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
            src_f64 = float64_round_to_int(src_f64, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f64[0] = src_f64;
            break;
        case 1:
            src_f64 = env->vfp.reg[vrx].f64[0];
            env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
            src_f64 = float64_round_to_int(src_f64, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f64[0] = src_f64;
            break;
        case 2:
            src_f64 = env->vfp.reg[vrx].f64[0];
            env->vfp.fp_status.float_rounding_mode = float_round_up;
            src_f64 = float64_round_to_int(src_f64, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f64[0] = src_f64;
            break;
        case 3:
            src_f64 = env->vfp.reg[vrx].f64[0];
            env->vfp.fp_status.float_rounding_mode = float_round_down;
            src_f64 = float64_round_to_int(src_f64, &env->vfp.fp_status);
            env->vfp.reg[vrz].f64[0] = 0;
            env->vfp.reg[vrz].f64[1] = 0;
            env->vfp.reg[vrz].f64[0] = src_f64;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    env->vfp.fp_status.float_rounding_mode = round_mode;
}


void FPUV3_HELPER(fmovi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrz, imm4, imm8, sign, type;
    float32 res_f32;
    float16 res_f16;
    union {
        float64 d;
        double  f;
    } val;

    type = (insn >> 6) & 0x3;
    sign = (insn >> 5) & 0x1;
    imm4 = (insn >> 16) & 0xf;
    imm8 = (((insn >> 20) & 0x3f) << 2) + ((insn >> 8) & 0x3);
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    /* calculate value. */
    val.f = ((imm8  << 3) + (1 << 11)) * 1.0 / (1 << imm4);
    if (sign) {
        val.f = val.f * (-1);
    }

    switch (type) {
    case 0x0:
        res_f16 = float64_to_float16(val.d, 1, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f16[0] = res_f16;
        break;
    case 0x1:
        res_f32 = float64_to_float32(val.d, &env->vfp.fp_status);
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f32[0] = res_f32;
        break;
    case 0x2:
        env->vfp.reg[vrz].f64[0] = 0;
        env->vfp.reg[vrz].f64[1] = 0;
        env->vfp.reg[vrz].f64[0] = val.d;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}

void FPUV3_HELPER(vadd16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_add(env->vfp.reg[vrx].f16[i],
                                               env->vfp.reg[vry].f16[i],
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vsub16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_sub(env->vfp.reg[vrx].f16[i],
                                               env->vfp.reg[vry].f16[i],
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vpadd16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float16 res[8];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        res[i] = float16_add(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vrx].f16[2 * i + 1],
                             &env->vfp.fp_status);
        res[i + 4] = float16_add(env->vfp.reg[vry].f16[2 * i],
                                 env->vfp.reg[vry].f16[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = res[i];
    }
}

void FPUV3_HELPER(vasx16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float16 res[8];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        res[2 * i + 1] = float16_add(env->vfp.reg[vrx].f16[2 * i + 1],
                                     env->vfp.reg[vry].f16[2 * i],
                                     &env->vfp.fp_status);
        res[2 * i] = float16_sub(env->vfp.reg[vrx].f16[2 * i],
                                 env->vfp.reg[vry].f16[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = res[i];
    }
}

void FPUV3_HELPER(vsax16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float16 res[8];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        res[2 * i + 1] = float16_sub(env->vfp.reg[vrx].f16[2 * i + 1],
                                     env->vfp.reg[vry].f16[2 * i],
                                     &env->vfp.fp_status);
        res[2 * i] = float16_add(env->vfp.reg[vrx].f16[2 * i],
                                 env->vfp.reg[vry].f16[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = res[i];
    }
}

void FPUV3_HELPER(vabs16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_abs(env->vfp.reg[vrx].f16[i]);
    }
}

void FPUV3_HELPER(vsabs16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        float16 a;
        a = float16_sub(env->vfp.reg[vrx].f16[i], env->vfp.reg[vry].f16[i],
                        &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[i] = float16_abs(a);
    }
}

void FPUV3_HELPER(vneg16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_chs(env->vfp.reg[vrx].f16[i]);
    }
}

void FPUV3_HELPER(vmax16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_max(env->vfp.reg[vrx].f16[i],
                                                  env->vfp.reg[vry].f16[i],
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmin16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_min(env->vfp.reg[vrx].f16[i],
                                                  env->vfp.reg[vry].f16[i],
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmaxnm16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_maxnum(env->vfp.reg[vrx].f16[i],
                                                  env->vfp.reg[vry].f16[i],
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vminnm16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_minnum(env->vfp.reg[vrx].f16[i],
                                                  env->vfp.reg[vry].f16[i],
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vpmax16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float16 res[8];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        res[i] = float16_max(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vrx].f16[2 * i + 1],
                             &env->vfp.fp_status);
        res[i + 4] = float16_max(env->vfp.reg[vry].f16[2 * i],
                                 env->vfp.reg[vry].f16[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = res[i];
    }
}

void FPUV3_HELPER(vpmin16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float16 res[8];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        res[i] = float16_min(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vrx].f16[2 * i + 1],
                             &env->vfp.fp_status);
        res[i + 4] = float16_min(env->vfp.reg[vry].f16[2 * i],
                                 env->vfp.reg[vry].f16[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = res[i];
    }
}

void FPUV3_HELPER(vcmpnez16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float16 b = float16_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        int res = float16_compare_quiet(env->vfp.reg[vrx].f16[i], b,
                                        &env->vfp.fp_status);
        if (res == float_relation_equal) {
            env->vfp.reg[vrz].f16[i] = make_float16(0);
        } else {
            env->vfp.reg[vrz].f16[i] = make_float16(0xffff);
        }
    }
}

void FPUV3_HELPER(vcmpne16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        int res = float16_compare_quiet(env->vfp.reg[vrx].f16[i],
                                        env->vfp.reg[vry].f16[i],
                                        &env->vfp.fp_status);
        if (res == float_relation_equal) {
            env->vfp.reg[vrz].f16[i] = make_float16(0);
        } else {
            env->vfp.reg[vrz].f16[i] = make_float16(0xffff);
        }
    }
}

void FPUV3_HELPER(vcmphsz16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float16 b = float16_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        int res = float16_compare(env->vfp.reg[vrx].f16[i], b,
                                  &env->vfp.fp_status);
        if (res == float_relation_greater || res == float_relation_equal) {
            env->vfp.reg[vrz].f16[i] = make_float16(0xffff);
        } else {
            env->vfp.reg[vrz].f16[i] = make_float16(0);
        }
    }
}

void FPUV3_HELPER(vcmphs16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        int res = float16_compare(env->vfp.reg[vrx].f16[i],
                                  env->vfp.reg[vry].f16[i],
                                  &env->vfp.fp_status);
        if (res == float_relation_greater || res == float_relation_equal) {
            env->vfp.reg[vrz].f16[i] = make_float16(0xffff);
        } else {
            env->vfp.reg[vrz].f16[i] = make_float16(0);
        }
    }
}

void FPUV3_HELPER(vcmpltz16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float16 b = float16_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        int res = float16_compare(env->vfp.reg[vrx].f16[i], b,
                                  &env->vfp.fp_status);
        if (res == float_relation_less) {
            env->vfp.reg[vrz].f16[i] = make_float16(0xffff);
        } else {
            env->vfp.reg[vrz].f16[i] = make_float16(0);
        }
    }
}

void FPUV3_HELPER(vcmplt16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        int res = float16_compare(env->vfp.reg[vrx].f16[i],
                                  env->vfp.reg[vry].f16[i],
                                  &env->vfp.fp_status);
        if (res == float_relation_less) {
            env->vfp.reg[vrz].f16[i] = make_float16(0xffff);
        } else {
            env->vfp.reg[vrz].f16[i] = make_float16(0);
        }
    }
}

void FPUV3_HELPER(vcmphz16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float16 b = float16_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        int res = float16_compare(env->vfp.reg[vrx].f16[i], b,
                                  &env->vfp.fp_status);
        if (res == float_relation_greater) {
            env->vfp.reg[vrz].f16[i] = make_float16(0xffff);
        } else {
            env->vfp.reg[vrz].f16[i] = make_float16(0);
        }
    }
}

void FPUV3_HELPER(vcmplsz16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float16 b = float16_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        int res = float16_compare(env->vfp.reg[vrx].f16[i], b,
                                  &env->vfp.fp_status);
        if (res == float_relation_less || res == float_relation_equal) {
            env->vfp.reg[vrz].f16[i] = make_float16(0xffff);
        } else {
            env->vfp.reg[vrz].f16[i] = make_float16(0);
        }
    }
}

void FPUV3_HELPER(vmuli16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, imm, i;
    float16 b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM3_SHI) & CSKY_FPUV3_IMM3_MASK;

    b = env->vfp.reg[vry].f16[imm];

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_mul(env->vfp.reg[vrx].f16[i], b,
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmulai16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, imm, i;
    float16 p, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM3_SHI) & CSKY_FPUV3_IMM3_MASK;

    b = env->vfp.reg[vry].f16[imm];

    for (i = 0; i < 8; i++) {
        p = float16_mul(env->vfp.reg[vrx].f16[i], b, &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[i] = float16_add(env->vfp.reg[vrz].f16[i],
                                               p, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmulsi16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, imm, i;
    float16 p, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM3_SHI) & CSKY_FPUV3_IMM3_MASK;

    b = env->vfp.reg[vry].f16[imm];

    for (i = 0; i < 8; i++) {
        p = float16_mul(env->vfp.reg[vrx].f16[i], b, &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[i] = float16_sub(env->vfp.reg[vrz].f16[i],
                                               p, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulai16e)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    float32 a, b;
    union VDSP c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM3_SHI) & CSKY_FPUV3_IMM3_MASK;

    b = float16_to_float32(env->vfp.reg[vry].f16[imm], 1, &env->vfp.fp_status);

    for (i = 0; i < 4; i++) {
        a = float16_to_float32(env->vfp.reg[vrx].f16[i], 1,
                               &env->vfp.fp_status);

        c.f32[i] = float32_muladd(a, b, env->vfp.reg[vrz].f32[i],
                                  0, &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = c.f32[i];
    }
}

void FPUV3_HELPER(vfmulsi16e)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    float32 a, b;
    union VDSP c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM3_SHI) & CSKY_FPUV3_IMM3_MASK;

    b = float16_to_float32(env->vfp.reg[vry].f16[imm], 1, &env->vfp.fp_status);

    for (i = 0; i < 4; i++) {
        a = float16_to_float32(env->vfp.reg[vrx].f16[i], 1,
                               &env->vfp.fp_status);

        c.f32[i] = float32_muladd(a, b, env->vfp.reg[vrz].f32[i],
                                  float_muladd_negate_product,
                                  &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = c.f32[i];
    }
}

void FPUV3_HELPER(vfmulxaai16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    union VDSP *regs = env->vfp.reg;
    float16 b1, b2;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM2_SHI) & CSKY_FPUV3_IMM2_MASK;

    b1 = regs[vry].f16[2 * imm];
    b2 = regs[vry].f16[2 * imm + 1];

    for (i = 0; i < 4; i++) {
        regs[vrz].f16[2 * i + 1] = float16_muladd(regs[vrx].f16[2 * i],
                                                  b2,
                                                  regs[vrz].f16[2 * i + 1],
                                                  0, &env->vfp.fp_status);
        regs[vrz].f16[2 * i] = float16_muladd(regs[vrx].f16[2 * i],
                                              b1,
                                              regs[vrz].f16[2 * i],
                                              0, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxasi16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    union VDSP *regs = env->vfp.reg;
    float16 b1, b2;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM2_SHI) & CSKY_FPUV3_IMM2_MASK;

    b1 = regs[vry].f16[2 * imm];
    b2 = regs[vry].f16[2 * imm + 1];

    for (i = 0; i < 4; i++) {
        regs[vrz].f16[2 * i + 1] = float16_muladd(regs[vrx].f16[2 * i + 1],
                                                  b1,
                                                  regs[vrz].f16[2 * i + 1],
                                                  0, &env->vfp.fp_status);
        regs[vrz].f16[2 * i] = float16_muladd(regs[vrx].f16[2 * i + 1],
                                              b2,
                                              regs[vrz].f16[2 * i],
                                              float_muladd_negate_product,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxssi16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    union VDSP *regs = env->vfp.reg;
    float16 b1, b2;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM2_SHI) & CSKY_FPUV3_IMM2_MASK;

    b1 = regs[vry].f16[2 * imm];
    b2 = regs[vry].f16[2 * imm + 1];

    for (i = 0; i < 4; i++) {
        regs[vrz].f16[2 * i + 1] = float16_muladd(regs[vrx].f16[2 * i],
                                                  b2,
                                                  regs[vrz].f16[2 * i + 1],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
        regs[vrz].f16[2 * i] = float16_muladd(regs[vrx].f16[2 * i],
                                              b1,
                                              regs[vrz].f16[2 * i],
                                              float_muladd_negate_product,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxsai16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    union VDSP *regs = env->vfp.reg;
    float16 b1, b2;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM2_SHI) & CSKY_FPUV3_IMM2_MASK;

    b1 = regs[vry].f16[2 * imm];
    b2 = regs[vry].f16[2 * imm + 1];

    for (i = 0; i < 4; i++) {
        regs[vrz].f16[2 * i + 1] = float16_muladd(regs[vrx].f16[2 * i + 1],
                                                  b1,
                                                  regs[vrz].f16[2 * i + 1],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
        regs[vrz].f16[2 * i] = float16_muladd(regs[vrx].f16[2 * i + 1],
                                              b2,
                                              regs[vrz].f16[2 * i],
                                              0,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmul16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_mul(env->vfp.reg[vrx].f16[i],
                                               env->vfp.reg[vry].f16[i],
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmula16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        float16 a;
        a = float16_mul(env->vfp.reg[vrx].f16[i], env->vfp.reg[vry].f16[i],
                        &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[i] = float16_add(env->vfp.reg[vrz].f16[i], a,
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmuls16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        float16 a;
        a = float16_mul(env->vfp.reg[vrx].f16[i], env->vfp.reg[vry].f16[i],
                        &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[i] = float16_sub(env->vfp.reg[vrz].f16[i], a,
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmula16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_muladd(env->vfp.reg[vrx].f16[i],
                                                  env->vfp.reg[vry].f16[i],
                                                  env->vfp.reg[vrz].f16[i],
                                                  0, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmuls16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_muladd(env->vfp.reg[vrx].f16[i],
                                                  env->vfp.reg[vry].f16[i],
                                                  env->vfp.reg[vrz].f16[i],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxaa16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP *regs = env->vfp.reg;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        regs[vrz].f16[2 * i + 1] = float16_muladd(regs[vrx].f16[2 * i],
                                                  regs[vry].f16[2 * i + 1],
                                                  regs[vrz].f16[2 * i + 1],
                                                  0, &env->vfp.fp_status);
        regs[vrz].f16[2 * i] = float16_muladd(regs[vrx].f16[2 * i],
                                              regs[vry].f16[2 * i],
                                              regs[vrz].f16[2 * i],
                                              0, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxas16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP *regs = env->vfp.reg;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        regs[vrz].f16[2 * i + 1] = float16_muladd(regs[vrx].f16[2 * i + 1],
                                                  regs[vry].f16[2 * i],
                                                  regs[vrz].f16[2 * i + 1],
                                                  0, &env->vfp.fp_status);
        regs[vrz].f16[2 * i] = float16_muladd(regs[vrx].f16[2 * i + 1],
                                              regs[vry].f16[2 * i + 1],
                                              regs[vrz].f16[2 * i],
                                              float_muladd_negate_product,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxss16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP *regs = env->vfp.reg;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        regs[vrz].f16[2 * i + 1] = float16_muladd(regs[vrx].f16[2 * i],
                                                  regs[vry].f16[2 * i + 1],
                                                  regs[vrz].f16[2 * i + 1],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
        regs[vrz].f16[2 * i] = float16_muladd(regs[vrx].f16[2 * i],
                                              regs[vry].f16[2 * i],
                                              regs[vrz].f16[2 * i],
                                              float_muladd_negate_product,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxsa16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP *regs = env->vfp.reg;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        regs[vrz].f16[2 * i + 1] = float16_muladd(regs[vrx].f16[2 * i + 1],
                                                  regs[vry].f16[2 * i],
                                                  regs[vrz].f16[2 * i + 1],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
        regs[vrz].f16[2 * i] = float16_muladd(regs[vrx].f16[2 * i + 1],
                                              regs[vry].f16[2 * i + 1],
                                              regs[vrz].f16[2 * i],
                                              0,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfnmula16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_muladd(env->vfp.reg[vrx].f16[i],
                                                  env->vfp.reg[vry].f16[i],
                                                  env->vfp.reg[vrz].f16[i],
                                                  float_muladd_negate_result,
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfnmuls16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        env->vfp.reg[vrz].f16[i] = float16_muladd(env->vfp.reg[vrx].f16[i],
                                                  env->vfp.reg[vry].f16[i],
                                                  env->vfp.reg[vrz].f16[i],
                                                  float_muladd_negate_c,
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmula16e)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float32 a, b;
        a = float16_to_float32(env->vfp.reg[vrx].f16[i], 1,
                               &env->vfp.fp_status);
        b = float16_to_float32(env->vfp.reg[vry].f16[i], 1,
                               &env->vfp.fp_status);

        c.f32[i] = float32_muladd(a, b, env->vfp.reg[vrz].f32[i],
                                  0, &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = c.f32[i];
    }
}

void FPUV3_HELPER(vfmuls16e)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP c;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float32 a, b;
        a = float16_to_float32(env->vfp.reg[vrx].f16[i], 1,
                               &env->vfp.fp_status);
        b = float16_to_float32(env->vfp.reg[vry].f16[i], 1,
                               &env->vfp.fp_status);

        c.f32[i] = float32_muladd(a, b, env->vfp.reg[vrz].f32[i],
                                  float_muladd_negate_product,
                                  &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = c.f32[i];
    }
}

void FPUV3_HELPER(vfcmul16)(CPUCSKYState *env, uint32_t insn)
{
    uint16_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float16 tmp, res0, res1;

        tmp = float16_mul(env->vfp.reg[vrx].f16[2 * i],
                          env->vfp.reg[vry].f16[2 * i + 1],
                          &env->vfp.fp_status);
        res1 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i],
                              tmp,
                              0,
                              &env->vfp.fp_status);

        tmp = float16_mul(env->vfp.reg[vrx].f16[2 * i],
                          env->vfp.reg[vry].f16[2 * i],
                          &env->vfp.fp_status);
        res0 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i + 1],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[2 * i] = res0;
        env->vfp.reg[vrz].f16[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulc16)(CPUCSKYState *env, uint32_t insn)
{
    uint16_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float16 tmp, res0, res1;

        tmp = float16_mul(env->vfp.reg[vrx].f16[2 * i],
                          env->vfp.reg[vry].f16[2 * i + 1],
                          &env->vfp.fp_status);
        res1 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);

        tmp = float16_mul(env->vfp.reg[vrx].f16[2 * i],
                          env->vfp.reg[vry].f16[2 * i],
                          &env->vfp.fp_status);
        res0 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i + 1],
                              tmp,
                              0,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[2 * i] = res0;
        env->vfp.reg[vrz].f16[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmuln16)(CPUCSKYState *env, uint32_t insn)
{
    uint16_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float16 tmp, res0, res1;

        tmp = float16_mul(env->vfp.reg[vrx].f16[2 * i],
                          env->vfp.reg[vry].f16[2 * i + 1],
                          &env->vfp.fp_status);
        res1 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i],
                              tmp,
                              float_muladd_negate_result,
                              &env->vfp.fp_status);

        tmp = float16_mul(env->vfp.reg[vrx].f16[2 * i],
                          env->vfp.reg[vry].f16[2 * i],
                          &env->vfp.fp_status);
        res0 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i + 1],
                              tmp,
                              float_muladd_negate_c,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[2 * i] = res0;
        env->vfp.reg[vrz].f16[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulcn16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float16 tmp, res0, res1;

        tmp = float16_mul(env->vfp.reg[vrx].f16[2 * i],
                          env->vfp.reg[vry].f16[2 * i + 1],
                          &env->vfp.fp_status);
        res1 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i],
                              tmp,
                              float_muladd_negate_c,
                              &env->vfp.fp_status);

        tmp = float16_mul(env->vfp.reg[vrx].f16[2 * i],
                          env->vfp.reg[vry].f16[2 * i],
                          &env->vfp.fp_status);
        res0 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i + 1],
                              tmp,
                              float_muladd_negate_result,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[2 * i] = res0;
        env->vfp.reg[vrz].f16[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmula16)(CPUCSKYState *env, uint32_t insn)
{
    uint16_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float16 tmp, res0, res1;

        tmp = float16_muladd(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vry].f16[2 * i + 1],
                             env->vfp.reg[vrz].f16[2 * i + 1],
                             0,
                             &env->vfp.fp_status);
        res1 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i],
                              tmp,
                              0,
                              &env->vfp.fp_status);

        tmp = float16_muladd(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vry].f16[2 * i],
                             env->vfp.reg[vrz].f16[2 * i],
                             0,
                             &env->vfp.fp_status);
        res0 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i + 1],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[2 * i] = res0;
        env->vfp.reg[vrz].f16[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulca16)(CPUCSKYState *env, uint32_t insn)
{
    uint16_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float16 tmp, res0, res1;

        tmp = float16_muladd(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vry].f16[2 * i + 1],
                             env->vfp.reg[vrz].f16[2 * i + 1],
                             0,
                             &env->vfp.fp_status);
        res1 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);

        tmp = float16_muladd(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vry].f16[2 * i],
                             env->vfp.reg[vrz].f16[2 * i],
                             0,
                             &env->vfp.fp_status);
        res0 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i + 1],
                              tmp,
                              0,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[2 * i] = res0;
        env->vfp.reg[vrz].f16[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulna16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float16 tmp, res0, res1;

        tmp = float16_muladd(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vry].f16[2 * i + 1],
                             env->vfp.reg[vrz].f16[2 * i + 1],
                             float_muladd_negate_product,
                             &env->vfp.fp_status);
        res1 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);

        tmp = float16_muladd(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vry].f16[2 * i],
                             env->vfp.reg[vrz].f16[2 * i],
                             float_muladd_negate_product,
                             &env->vfp.fp_status);
        res0 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i + 1],
                              tmp,
                              0,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[2 * i] = res0;
        env->vfp.reg[vrz].f16[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulcna16)(CPUCSKYState *env, uint32_t insn)
{
    uint16_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float16 tmp, res0, res1;

        tmp = float16_muladd(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vry].f16[2 * i + 1],
                             env->vfp.reg[vrz].f16[2 * i + 1],
                             float_muladd_negate_product,
                             &env->vfp.fp_status);
        res1 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i],
                              tmp,
                              0,
                              &env->vfp.fp_status);

        tmp = float16_muladd(env->vfp.reg[vrx].f16[2 * i],
                             env->vfp.reg[vry].f16[2 * i],
                             env->vfp.reg[vrz].f16[2 * i],
                             float_muladd_negate_product,
                             &env->vfp.fp_status);
        res0 = float16_muladd(env->vfp.reg[vrx].f16[2 * i + 1],
                              env->vfp.reg[vry].f16[2 * i + 1],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[2 * i] = res0;
        env->vfp.reg[vrz].f16[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vrecpe16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    union {
        float16 f;
        int16_t s;
    } val;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        val.f = env->vfp.reg[vrx].f16[i];
        val.s = (int16_t)0x7785 - val.s;
        env->vfp.reg[vrz].f16[i] = val.f;
    }
}

void FPUV3_HELPER(vrecps16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float32 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        a = env->vfp.reg[vrx].f16[i];
        b = env->vfp.reg[vry].f16[i];
        c = float16_two;    /* float16 2 */
        d = float16_muladd(a, b, c, float_muladd_negate_product,
                           &env->vfp.fp_status);

        env->vfp.reg[vrz].f16[i] = d;
    }
}

void FPUV3_HELPER(vrsqrte16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    union {
        float16 f;
        int16_t s;
    } val;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        val.f = env->vfp.reg[vrx].f16[i];
        val.s = (int16_t)0x59bb - (val.s >> 1);
        env->vfp.reg[vrz].f16[i] = val.f;
    }
}

void FPUV3_HELPER(vrsqrts16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float16 a, b, c, d, e;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        a = env->vfp.reg[vrx].f16[i];
        b = env->vfp.reg[vry].f16[i];
        c = float16_three;      /* float16 3 */
        d = float16_two;        /* float16 2 */
        e = float16_muladd(a, b, c, float_muladd_negate_product,
                           &env->vfp.fp_status);
        e = float16_div(e, d, &env->vfp.fp_status);
        env->vfp.reg[vrz].f16[i] = e;
    }
}

void FPUV3_HELPER(vexpe16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float16 a, b, c, d, float16_10, float16_neg_10;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    float16_10 = make_float16(0x4900);
    float16_neg_10 = make_float16(0xc900);

    for (i = 0; i < 8; i++) {
        a = env->vfp.reg[vrx].f16[i];
        if (float16_compare(a, float16_10, &env->vfp.fp_status)
            == float_relation_greater) {
            a = float16_10;
        } else if (float16_compare(a, float16_neg_10, &env->vfp.fp_status)
                   == float_relation_less) {
            a = float16_neg_10;
        }
        b = make_float16(0x65c5);   /* 1477.319722 */
        c = make_float16(0x7373);   /* 15293.356023 */
        d = float16_muladd(a, b, c, 0, &env->vfp.fp_status);
        env->vfp.reg[vrz].udsps[i] = float16_to_uint16(d, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vadd32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_add(env->vfp.reg[vrx].f32[i],
                                               env->vfp.reg[vry].f32[i],
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vsub32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_sub(env->vfp.reg[vrx].f32[i],
                                               env->vfp.reg[vry].f32[i],
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vpadd32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float32 res[4];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        res[i] = float32_add(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vrx].f32[2 * i + 1],
                             &env->vfp.fp_status);
        res[i + 2] = float32_add(env->vfp.reg[vry].f32[2 * i],
                                 env->vfp.reg[vry].f32[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = res[i];
    }
}

void FPUV3_HELPER(vasx32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float32 res[4];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        res[2 * i + 1] = float32_add(env->vfp.reg[vrx].f32[2 * i + 1],
                                     env->vfp.reg[vry].f32[2 * i],
                                     &env->vfp.fp_status);
        res[2 * i] = float32_sub(env->vfp.reg[vrx].f32[2 * i],
                                 env->vfp.reg[vry].f32[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = res[i];
    }
}

void FPUV3_HELPER(vsax32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float32 res[4];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        res[2 * i + 1] = float32_sub(env->vfp.reg[vrx].f32[2 * i + 1],
                                     env->vfp.reg[vry].f32[2 * i],
                                     &env->vfp.fp_status);
        res[2 * i] = float32_add(env->vfp.reg[vrx].f32[2 * i],
                                 env->vfp.reg[vry].f32[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = res[i];
    }
}

void FPUV3_HELPER(vabs32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_abs(env->vfp.reg[vrx].f32[i]);
    }
}

void FPUV3_HELPER(vsabs32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float32 a;
        a = float32_sub(env->vfp.reg[vrx].f32[i], env->vfp.reg[vry].f32[i],
                        &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[i] = float32_abs(a);
    }
}

void FPUV3_HELPER(vneg32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_chs(env->vfp.reg[vrx].f32[i]);
    }
}

void FPUV3_HELPER(vmovi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrz, imm4, imm8, sign, type, i;
    float32 res_f32;
    float16 res_f16;
    union {
        float32 f32_fake;
        float   f32;
    } val;

    type = (insn >> 6) & 0x1;
    sign = (insn >> 5) & 0x1;
    imm4 = (insn >> 16) & 0xf;
    imm8 = (((insn >> 20) & 0x3f) << 2) + ((insn >> 8) & 0x3);
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    val.f32 = ((imm8  << 3) + (1 << 11)) * 1.0 / (1 << imm4);
    if (sign) {
        val.f32_fake = float32_chs(val.f32_fake);
    }

    if (type == 0) {    /* f16 */
        res_f16 = float32_to_float16(val.f32_fake, 1, &env->vfp.fp_status);
        for (i = 0; i < 8; i++) {
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
    } else {    /* f32 */
        res_f32 = val.f32_fake;
        for (i = 0; i < 4; i++) {
            env->vfp.reg[vrz].f32[i] = res_f32;
        }
    }
}

void FPUV3_HELPER(vmax32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_max(env->vfp.reg[vrx].f32[i],
                                                  env->vfp.reg[vry].f32[i],
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmin32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_min(env->vfp.reg[vrx].f32[i],
                                                  env->vfp.reg[vry].f32[i],
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmaxnm32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_maxnum(env->vfp.reg[vrx].f32[i],
                                                  env->vfp.reg[vry].f32[i],
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vminnm32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_minnum(env->vfp.reg[vrx].f32[i],
                                                  env->vfp.reg[vry].f32[i],
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vpmax32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float32 res[4];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        res[i] = float32_max(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vrx].f32[2 * i + 1],
                             &env->vfp.fp_status);
        res[i + 2] = float32_max(env->vfp.reg[vry].f32[2 * i],
                                 env->vfp.reg[vry].f32[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = res[i];
    }
}

void FPUV3_HELPER(vpmin32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float32 res[4];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        res[i] = float32_min(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vrx].f32[2 * i + 1],
                             &env->vfp.fp_status);
        res[i + 2] = float32_min(env->vfp.reg[vry].f32[2 * i],
                                 env->vfp.reg[vry].f32[2 * i + 1],
                                 &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = res[i];
    }
}

void FPUV3_HELPER(vcmpnez32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float32 b = float32_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        int res = float32_compare_quiet(env->vfp.reg[vrx].f32[i], b,
                                        &env->vfp.fp_status);
        if (res == float_relation_equal) {
            env->vfp.reg[vrz].f32[i] = make_float32(0);
        } else {
            env->vfp.reg[vrz].f32[i] = make_float32(0xffffffff);
        }
    }
}

void FPUV3_HELPER(vcmpne32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        int res = float32_compare_quiet(env->vfp.reg[vrx].f32[i],
                                        env->vfp.reg[vry].f32[i],
                                        &env->vfp.fp_status);
        if (res == float_relation_equal) {
            env->vfp.reg[vrz].f32[i] = make_float32(0);
        } else {
            env->vfp.reg[vrz].f32[i] = make_float32(0xffffffff);
        }
    }
}

void FPUV3_HELPER(vcmphsz32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float32 b = float32_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        int res = float32_compare(env->vfp.reg[vrx].f32[i], b,
                                  &env->vfp.fp_status);
        if (res == float_relation_greater || res == float_relation_equal) {
            env->vfp.reg[vrz].f32[i] = make_float32(0xffffffff);
        } else {
            env->vfp.reg[vrz].f32[i] = make_float32(0);
        }
    }
}

void FPUV3_HELPER(vcmphs32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        int res = float32_compare(env->vfp.reg[vrx].f32[i],
                                  env->vfp.reg[vry].f32[i],
                                  &env->vfp.fp_status);
        if (res == float_relation_greater || res == float_relation_equal) {
            env->vfp.reg[vrz].f32[i] = make_float32(0xffffffff);
        } else {
            env->vfp.reg[vrz].f32[i] = make_float32(0);
        }
    }
}

void FPUV3_HELPER(vcmpltz32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float32 b = float32_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        int res = float32_compare(env->vfp.reg[vrx].f32[i], b,
                                  &env->vfp.fp_status);
        if (res == float_relation_less) {
            env->vfp.reg[vrz].f32[i] = make_float32(0xffffffff);
        } else {
            env->vfp.reg[vrz].f32[i] = make_float32(0);
        }
    }
}

void FPUV3_HELPER(vcmplt32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        int res = float32_compare(env->vfp.reg[vrx].f32[i],
                                  env->vfp.reg[vry].f32[i],
                                  &env->vfp.fp_status);
        if (res == float_relation_less) {
            env->vfp.reg[vrz].f32[i] = make_float32(0xffffffff);
        } else {
            env->vfp.reg[vrz].f32[i] = make_float32(0);
        }
    }
}

void FPUV3_HELPER(vcmphz32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float32 b = float32_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        int res = float32_compare(env->vfp.reg[vrx].f32[i], b,
                                  &env->vfp.fp_status);
        if (res == float_relation_greater) {
            env->vfp.reg[vrz].f32[i] = make_float32(0xffffffff);
        } else {
            env->vfp.reg[vrz].f32[i] = make_float32(0);
        }
    }
}

void FPUV3_HELPER(vcmplsz32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float32 b = float32_zero;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        int res = float32_compare(env->vfp.reg[vrx].f32[i], b,
                                  &env->vfp.fp_status);
        if (res == float_relation_less || res == float_relation_equal) {
            env->vfp.reg[vrz].f32[i] = make_float32(0xffffffff);
        } else {
            env->vfp.reg[vrz].f32[i] = make_float32(0);
        }
    }
}

void FPUV3_HELPER(vmuli32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, imm, i;
    float32 b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM3_SHI) & CSKY_FPUV3_IMM3_MASK;

    b = env->vfp.reg[vry].f32[imm];

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_mul(env->vfp.reg[vrx].f32[i], b,
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmulai32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, imm, i;
    float32 p, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM3_SHI) & CSKY_FPUV3_IMM3_MASK;

    b = env->vfp.reg[vry].f32[imm];

    for (i = 0; i < 4; i++) {
        p = float32_mul(env->vfp.reg[vrx].f32[i], b, &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[i] = float32_add(env->vfp.reg[vrz].f32[i],
                                               p, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmulsi32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, imm, i;
    float32 p, b;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM3_SHI) & CSKY_FPUV3_IMM3_MASK;

    b = env->vfp.reg[vry].f32[imm];

    for (i = 0; i < 4; i++) {
        p = float32_mul(env->vfp.reg[vrx].f32[i], b, &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[i] = float32_sub(env->vfp.reg[vrz].f32[i],
                                               p, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxaai32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    union VDSP *regs = env->vfp.reg;
    float32 b1, b2;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM2_SHI) & CSKY_FPUV3_IMM2_MASK;
    imm = imm & 0x1;

    b1 = regs[vry].f32[2 * imm];
    b2 = regs[vry].f32[2 * imm + 1];

    for (i = 0; i < 2; i++) {
        regs[vrz].f32[2 * i + 1] = float32_muladd(regs[vrx].f32[2 * i],
                                                  b2,
                                                  regs[vrz].f32[2 * i + 1],
                                                  0, &env->vfp.fp_status);
        regs[vrz].f32[2 * i] = float32_muladd(regs[vrx].f32[2 * i],
                                              b1,
                                              regs[vrz].f32[2 * i],
                                              0, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxasi32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    union VDSP *regs = env->vfp.reg;
    float32 b1, b2;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM2_SHI) & CSKY_FPUV3_IMM2_MASK;
    imm = imm & 0x1;

    b1 = regs[vry].f32[2 * imm];
    b2 = regs[vry].f32[2 * imm + 1];

    for (i = 0; i < 2; i++) {
        regs[vrz].f32[2 * i + 1] = float32_muladd(regs[vrx].f32[2 * i + 1],
                                                  b1,
                                                  regs[vrz].f32[2 * i + 1],
                                                  0, &env->vfp.fp_status);
        regs[vrz].f32[2 * i] = float32_muladd(regs[vrx].f32[2 * i + 1],
                                              b2,
                                              regs[vrz].f32[2 * i],
                                              float_muladd_negate_product,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxssi32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    union VDSP *regs = env->vfp.reg;
    float32 b1, b2;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM2_SHI) & CSKY_FPUV3_IMM2_MASK;
    imm = imm & 0x1;

    b1 = regs[vry].f32[2 * imm];
    b2 = regs[vry].f32[2 * imm + 1];

    for (i = 0; i < 2; i++) {
        regs[vrz].f32[2 * i + 1] = float32_muladd(regs[vrx].f32[2 * i],
                                                  b2,
                                                  regs[vrz].f32[2 * i + 1],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
        regs[vrz].f32[2 * i] = float32_muladd(regs[vrx].f32[2 * i],
                                              b1,
                                              regs[vrz].f32[2 * i],
                                              float_muladd_negate_product,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxsai32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i, imm;
    union VDSP *regs = env->vfp.reg;
    float32 b1, b2;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (insn >> CSKY_FPUV3_IMM2_SHI) & CSKY_FPUV3_IMM2_MASK;
    imm = imm & 0x1;

    b1 = regs[vry].f32[2 * imm];
    b2 = regs[vry].f32[2 * imm + 1];

    for (i = 0; i < 2; i++) {
        regs[vrz].f32[2 * i + 1] = float32_muladd(regs[vrx].f32[2 * i + 1],
                                                  b1,
                                                  regs[vrz].f32[2 * i + 1],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
        regs[vrz].f32[2 * i] = float32_muladd(regs[vrx].f32[2 * i + 1],
                                              b2,
                                              regs[vrz].f32[2 * i],
                                              0,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmul32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_mul(env->vfp.reg[vrx].f32[i],
                                               env->vfp.reg[vry].f32[i],
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmula32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float32 a;
        a = float32_mul(env->vfp.reg[vrx].f32[i], env->vfp.reg[vry].f32[i],
                        &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[i] = float32_add(env->vfp.reg[vrz].f32[i],
                                               a, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vmuls32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        float32 a;
        a = float32_mul(env->vfp.reg[vrx].f32[i], env->vfp.reg[vry].f32[i],
                        &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[i] = float32_sub(env->vfp.reg[vrz].f32[i], a,
                                               &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmula32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_muladd(env->vfp.reg[vrx].f32[i],
                                                  env->vfp.reg[vry].f32[i],
                                                  env->vfp.reg[vrz].f32[i],
                                                  0, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmuls32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_muladd(env->vfp.reg[vrx].f32[i],
                                                  env->vfp.reg[vry].f32[i],
                                                  env->vfp.reg[vrz].f32[i],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
     }
}

void FPUV3_HELPER(vfmulxaa32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP *regs = env->vfp.reg;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        regs[vrz].f32[2 * i + 1] = float32_muladd(regs[vrx].f32[2 * i],
                                                  regs[vry].f32[2 * i + 1],
                                                  regs[vrz].f32[2 * i + 1],
                                                  0, &env->vfp.fp_status);
        regs[vrz].f32[2 * i] = float32_muladd(regs[vrx].f32[2 * i],
                                              regs[vry].f32[2 * i],
                                              regs[vrz].f32[2 * i],
                                              0, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxas32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP *regs = env->vfp.reg;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        regs[vrz].f32[2 * i + 1] = float32_muladd(regs[vrx].f32[2 * i + 1],
                                                  regs[vry].f32[2 * i],
                                                  regs[vrz].f32[2 * i + 1],
                                                  0, &env->vfp.fp_status);
        regs[vrz].f32[2 * i] = float32_muladd(regs[vrx].f32[2 * i + 1],
                                              regs[vry].f32[2 * i + 1],
                                              regs[vrz].f32[2 * i],
                                              float_muladd_negate_product,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxss32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP *regs = env->vfp.reg;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        regs[vrz].f32[2 * i + 1] = float32_muladd(regs[vrx].f32[2 * i],
                                                  regs[vry].f32[2 * i + 1],
                                                  regs[vrz].f32[2 * i + 1],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
        regs[vrz].f32[2 * i] = float32_muladd(regs[vrx].f32[2 * i],
                                              regs[vry].f32[2 * i],
                                              regs[vrz].f32[2 * i],
                                              float_muladd_negate_product,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfmulxsa32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    union VDSP *regs = env->vfp.reg;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        regs[vrz].f32[2 * i + 1] = float32_muladd(regs[vrx].f32[2 * i + 1],
                                                  regs[vry].f32[2 * i],
                                                  regs[vrz].f32[2 * i + 1],
                                                  float_muladd_negate_product,
                                                  &env->vfp.fp_status);
        regs[vrz].f32[2 * i] = float32_muladd(regs[vrx].f32[2 * i + 1],
                                              regs[vry].f32[2 * i + 1],
                                              regs[vrz].f32[2 * i],
                                              0,
                                              &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfnmula32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_muladd(env->vfp.reg[vrx].f32[i],
                                                  env->vfp.reg[vry].f32[i],
                                                  env->vfp.reg[vrz].f32[i],
                                                  float_muladd_negate_result,
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfnmuls32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = float32_muladd(env->vfp.reg[vrx].f32[i],
                                                  env->vfp.reg[vry].f32[i],
                                                  env->vfp.reg[vrz].f32[i],
                                                  float_muladd_negate_c,
                                                  &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vfcmul32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        float32 tmp, res0, res1;

        tmp = float32_mul(env->vfp.reg[vrx].f32[2 * i],
                          env->vfp.reg[vry].f32[2 * i + 1],
                          &env->vfp.fp_status);
        res1 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i],
                              tmp,
                              0,
                              &env->vfp.fp_status);

        tmp = float32_mul(env->vfp.reg[vrx].f32[2 * i],
                          env->vfp.reg[vry].f32[2 * i],
                          &env->vfp.fp_status);
        res0 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i + 1],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[2 * i] = res0;
        env->vfp.reg[vrz].f32[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulc32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        float32 tmp, res0, res1;

        tmp = float32_mul(env->vfp.reg[vrx].f32[2 * i],
                          env->vfp.reg[vry].f32[2 * i + 1],
                          &env->vfp.fp_status);
        res1 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);

        tmp = float32_mul(env->vfp.reg[vrx].f32[2 * i],
                          env->vfp.reg[vry].f32[2 * i],
                          &env->vfp.fp_status);
        res0 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i + 1],
                              tmp,
                              0,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[2 * i] = res0;
        env->vfp.reg[vrz].f32[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmuln32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        float32 tmp, res0, res1;

        tmp = float32_mul(env->vfp.reg[vrx].f32[2 * i],
                          env->vfp.reg[vry].f32[2 * i + 1],
                          &env->vfp.fp_status);
        res1 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i],
                              tmp,
                              float_muladd_negate_result,
                              &env->vfp.fp_status);

        tmp = float32_mul(env->vfp.reg[vrx].f32[2 * i],
                          env->vfp.reg[vry].f32[2 * i],
                          &env->vfp.fp_status);
        res0 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i + 1],
                              tmp,
                              float_muladd_negate_c,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[2 * i] = res0;
        env->vfp.reg[vrz].f32[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulcn32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        float32 tmp, res0, res1;

        tmp = float32_mul(env->vfp.reg[vrx].f32[2 * i],
                          env->vfp.reg[vry].f32[2 * i + 1],
                          &env->vfp.fp_status);
        res1 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i],
                              tmp,
                              float_muladd_negate_c,
                              &env->vfp.fp_status);

        tmp = float32_mul(env->vfp.reg[vrx].f32[2 * i],
                          env->vfp.reg[vry].f32[2 * i],
                          &env->vfp.fp_status);
        res0 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i + 1],
                              tmp,
                              float_muladd_negate_result,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[2 * i] = res0;
        env->vfp.reg[vrz].f32[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmula32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        float32 tmp, res0, res1;

        tmp = float32_muladd(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vry].f32[2 * i + 1],
                             env->vfp.reg[vrz].f32[2 * i + 1],
                             0,
                             &env->vfp.fp_status);
        res1 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i],
                              tmp,
                              0,
                              &env->vfp.fp_status);

        tmp = float32_muladd(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vry].f32[2 * i],
                             env->vfp.reg[vrz].f32[2 * i],
                             0,
                             &env->vfp.fp_status);
        res0 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i + 1],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[2 * i] = res0;
        env->vfp.reg[vrz].f32[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulca32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        float32 tmp, res0, res1;

        tmp = float32_muladd(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vry].f32[2 * i + 1],
                             env->vfp.reg[vrz].f32[2 * i + 1],
                             0,
                             &env->vfp.fp_status);
        res1 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);

        tmp = float32_muladd(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vry].f32[2 * i],
                             env->vfp.reg[vrz].f32[2 * i],
                             0,
                             &env->vfp.fp_status);
        res0 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i + 1],
                              tmp,
                              0,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[2 * i] = res0;
        env->vfp.reg[vrz].f32[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulna32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        float32 tmp, res0, res1;

        tmp = float32_muladd(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vry].f32[2 * i + 1],
                             env->vfp.reg[vrz].f32[2 * i + 1],
                             float_muladd_negate_product,
                             &env->vfp.fp_status);
        res1 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);

        tmp = float32_muladd(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vry].f32[2 * i],
                             env->vfp.reg[vrz].f32[2 * i],
                             float_muladd_negate_product,
                             &env->vfp.fp_status);
        res0 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i + 1],
                              tmp,
                              0,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[2 * i] = res0;
        env->vfp.reg[vrz].f32[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vfcmulcna32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 2; i++) {
        float32 tmp, res0, res1;

        tmp = float32_muladd(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vry].f32[2 * i + 1],
                             env->vfp.reg[vrz].f32[2 * i + 1],
                             float_muladd_negate_product,
                             &env->vfp.fp_status);
        res1 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i],
                              tmp,
                              0,
                              &env->vfp.fp_status);

        tmp = float32_muladd(env->vfp.reg[vrx].f32[2 * i],
                             env->vfp.reg[vry].f32[2 * i],
                             env->vfp.reg[vrz].f32[2 * i],
                             float_muladd_negate_product,
                             &env->vfp.fp_status);
        res0 = float32_muladd(env->vfp.reg[vrx].f32[2 * i + 1],
                              env->vfp.reg[vry].f32[2 * i + 1],
                              tmp,
                              float_muladd_negate_product,
                              &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[2 * i] = res0;
        env->vfp.reg[vrz].f32[2 * i + 1] = res1;
    }
}

void FPUV3_HELPER(vrecpe32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    union {
        float32 f;
        int32_t s;
    } val;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        val.f = env->vfp.reg[vrx].f32[i];
        val.s = (int32_t)0x7ef0a3d7 - val.s;
        env->vfp.reg[vrz].f32[i] = val.f;
    }
}

void FPUV3_HELPER(vrecps32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float32 a, b, c, d;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        a = env->vfp.reg[vrx].f32[i];
        b = env->vfp.reg[vry].f32[i];
        c = float32_two;    /* float32 2 */
        d = float32_muladd(a, b, c, float_muladd_negate_product,
                           &env->vfp.fp_status);

        env->vfp.reg[vrz].f32[i] = d;
    }
}

void FPUV3_HELPER(vrsqrte32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    union {
        float32 f;
        int32_t s;
    } val;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        val.f = env->vfp.reg[vrx].f32[i];
        val.s = (int32_t)0x5f37642f - (val.s >> 1);
        env->vfp.reg[vrz].f32[i] = val.f;
    }
}

void FPUV3_HELPER(vrsqrts32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vry, vrz, i;
    float32 a, b, c, d, e;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vry = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        a = env->vfp.reg[vrx].f32[i];
        b = env->vfp.reg[vry].f32[i];
        c = float32_three;      /* float32 3 */
        d = float32_two;        /* float32 2 */
        e = float32_muladd(a, b, c, float_muladd_negate_product,
                           &env->vfp.fp_status);
        e = float32_div(e, d, &env->vfp.fp_status);
        env->vfp.reg[vrz].f32[i] = e;
    }
}

void FPUV3_HELPER(vexpe32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float32 a, b, c, d, float32_85, float32_neg_85;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    float32_85 = make_float32(0x42aa0000);
    float32_neg_85 = make_float32(0xc2aa0000);

    for (i = 0; i < 4; i++) {
        a = env->vfp.reg[vrx].f32[i];
        if (float32_compare(a, float32_85, &env->vfp.fp_status)
            == float_relation_greater) {
            a = float32_85;
        } else if (float32_compare(a, float32_neg_85, &env->vfp.fp_status)
                   == float_relation_less) {
            a = float32_neg_85;
        }
        b = make_float32(0x4b38aa3b);   /* 12102203.161561 */
        c = make_float32(0x4e7ddead);   /* 1064807268.529173 */
        d = float32_muladd(a, b, c, 0, &env->vfp.fp_status);
        env->vfp.reg[vrz].udspi[i] = float32_to_uint32(d, &env->vfp.fp_status);
    }
}

void FPUV3_HELPER(vhtos)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float32 res[8];

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 8; i++) {
        res[i] = float16_to_float32(env->vfp.reg[vrx].f16[i], 1,
                                    &env->vfp.fp_status);
    }
    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f32[i] = res[i];
        env->vfp.reg[vrz + 1].f32[i] = res[i + 4];
    }
}

void FPUV3_HELPER(vstoh)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    for (i = 0; i < 4; i++) {
        env->vfp.reg[vrz].f16[i] = float32_to_float16(env->vfp.reg[vrx].f32[i],
                                                      1, &env->vfp.fp_status);
    }
    env->vfp.reg[vrz].f64[1] = 0;
}

void FPUV3_HELPER(vdtos)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    env->vfp.reg[vrz].f32[0] = float64_to_float32(env->vfp.reg[vrx].f64[0],
                                                  &env->vfp.fp_status);
    env->vfp.reg[vrz].f32[1] = float64_to_float32(env->vfp.reg[vrx].f64[1],
                                                  &env->vfp.fp_status);
    env->vfp.reg[vrz].f64[1] = 0;
}

void FPUV3_HELPER(vftox)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, imm, i;
    uint16_t    res_u16;
    int16_t     res_s16;
    uint32_t    res_u32;
    uint32_t    res_u32s[8];
    int32_t     res_s32;
    int32_t     res_s32s[8];
    float16     src_f16;
    float32     src_f32;
    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    /*
     * | T1 | T2 | S |
     * +----+----+---+
     * | 7  | 6  | 5 |
     */
    result_type = (insn >> 5) & 0x7;
    if (result_type & 0x2) {
        /* T2 = 1, fix32 */
        imm = ((env->vfp.fcr >> 16) & 0x1f) + 1;
    } else {
        /* T2 = 0, fix16 */
        imm = ((env->vfp.fcr >> 16) & 0xf) + 1;
    }

    switch (result_type) {
    case 0x0: /* f16 to fixu16 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            src_f16 = float16_scalbn(src_f16, imm, &env->vfp.fp_status);
            res_u16 = float16_to_uint16(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].udsps[i] = res_u16;
        }
        break;
    case 0x1: /* f16 to fixs16 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            src_f16 = float16_scalbn(src_f16, imm, &env->vfp.fp_status);
            res_s16 = float16_to_int16(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].dsps[i] = res_s16;
        }
        break;
    case 0x2: /* f16 to fixu32 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            src_f16 = float16_scalbn(src_f16, imm, &env->vfp.fp_status);
            res_u32s[i] = float16_to_uint32(src_f16, &env->vfp.fp_status);
        }
        for (i = 0; i < 8; i++) {
            int j = i / 4;
            env->vfp.reg[vrz + j].udspi[i % 4] = res_u32s[i];
        }
        break;
    case 0x3: /* f16 to fixs32 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            src_f16 = float16_scalbn(src_f16, imm, &env->vfp.fp_status);
            res_s32s[i] = float16_to_int32(src_f16, &env->vfp.fp_status);
        }
        for (i = 0; i < 8; i++) {
            int j = i / 4;
            env->vfp.reg[vrz + j].dspi[i % 4] = res_s32s[i];
        }
        break;
    //case 0x4: /* f32 to fixu16 */
    //    for (i = 0; i < 4; i++) {
    //        src_f32 = env->vfp.reg[vrx].f32[i];
    //        src_f32 = float32_scalbn(src_f32, imm, &env->vfp.fp_status);
    //        res_u16 = float32_to_uint16(src_f32, &env->vfp.fp_status);
    //        env->vfp.reg[vrz].udsps[i] = res_u16;
    //    }
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    break;
    //case 0x5: /* f32 to fixs16 */
    //    for (i = 0; i < 4; i++) {
    //        src_f32 = env->vfp.reg[vrx].f32[i];
    //        src_f32 = float32_scalbn(src_f32, imm, &env->vfp.fp_status);
    //        res_s16 = float32_to_int16(src_f32, &env->vfp.fp_status);
    //        env->vfp.reg[vrz].dsps[i] = res_s16;
    //    }
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    break;
    case 0x6: /* f32 to fixu32 */
        for (i = 0; i < 4; i++) {
            src_f32 = env->vfp.reg[vrx].f32[i];
            src_f32 = float32_scalbn(src_f32, imm, &env->vfp.fp_status);
            res_u32 = float32_to_uint32(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].udspi[i] = res_u32;
        }
        break;
    case 0x7: /* f32 to fixs32 */
        for (i = 0; i < 4; i++) {
            src_f32 = env->vfp.reg[vrx].f32[i];
            src_f32 = float32_scalbn(src_f32, imm, &env->vfp.fp_status);
            res_s32 = float32_to_int32(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].dspi[i] = res_s32;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}

void FPUV3_HELPER(vxtof)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, imm, i;
    uint16_t    src_u16;
    int16_t     src_s16;
    uint32_t    src_u32;
    int32_t     src_s32;
    float16     res_f16;
    float32     res_f32;
    //float32     res_f32s[8];
    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    imm = (env->vfp.fcr >> 16) & 0x1f;

    /*
     * | T1 | T2 | S |
     * +----+----+---+
     * | 7  | 6  | 5 |
     */
    result_type = (insn >> 5) & 0x7;
    if (result_type & 0x4) {
        /* T1 = 1, fix32 */
        imm = ((env->vfp.fcr >> 16) & 0x1f) + 1;
    } else {
        /* T1 = 0, fix16 */
        imm = ((env->vfp.fcr >> 16) & 0xf) + 1;
    }


    switch (result_type) {
    case 0x0: /* fixu16 to f16 */
        for (i = 0; i < 8; i++) {
            src_u16 = env->vfp.reg[vrx].udsps[i];
            res_f16 = uint16_to_float16(src_u16, &env->vfp.fp_status);
            res_f16 = float16_scalbn(res_f16, -imm, &env->vfp.fp_status);
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
        break;
    case 0x1: /* fixs16 to f16 */
        for (i = 0; i < 8; i++) {
            src_s16 = env->vfp.reg[vrx].dsps[i];
            res_f16 = int16_to_float16(src_s16, &env->vfp.fp_status);
            res_f16 = float16_scalbn(res_f16, -imm, &env->vfp.fp_status);
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
        break;
    //case 0x2: /* fixu16 to f32 */
    //    for (i = 0; i < 8; i++) {
    //        src_u16 = env->vfp.reg[vrx].udsps[i];
    //        res_f32 = uint16_to_float32(src_u16, &env->vfp.fp_status);
    //        res_f32s[i] = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
    //    }
    //    for (i = 0; i < 8; i++) {
    //        int j = i / 4;
    //        env->vfp.reg[vrz + j].f32[i % 4] = res_f32s[i];
    //    }
    //    break;
    //case 0x3: /* fixs16 to f32 */
    //    for (i = 0; i < 8; i++) {
    //        src_s16 = env->vfp.reg[vrx].dsps[i];
    //        res_f32 = int16_to_float32(src_s16, &env->vfp.fp_status);
    //        res_f32s[i] = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
    //    }
    //    for (i = 0; i < 8; i++) {
    //        int j = i / 4;
    //        env->vfp.reg[vrz + j].f32[i % 4] = res_f32s[i];
    //    }
    //    break;
    case 0x4: /* fixu32 to f16 */
        for (i = 0; i < 4; i++) {
            src_u32 = env->vfp.reg[vrx].udspi[i];
            res_f32 = uint32_to_float32(src_u32, &env->vfp.fp_status);
            res_f32 = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
            res_f16 = float32_to_float16(res_f32, 1, &env->vfp.fp_status);
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
        env->vfp.reg[vrz].f64[1] = 0;
        break;
    case 0x5: /* fixs32 to f16 */
        for (i = 0; i < 4; i++) {
            src_s32 = env->vfp.reg[vrx].dspi[i];
            res_f32 = int32_to_float32(src_s32, &env->vfp.fp_status);
            res_f32 = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
            res_f16 = float32_to_float16(res_f32, 1, &env->vfp.fp_status);
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
        env->vfp.reg[vrz].f64[1] = 0;
        break;
    case 0x6: /* fixu32 to f32 */
        for (i = 0; i < 4; i++) {
            src_u32 = env->vfp.reg[vrx].udspi[i];
            res_f32 = uint32_to_float32(src_u32, &env->vfp.fp_status);
            res_f32 = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
            env->vfp.reg[vrz].f32[i] = res_f32;
        }
        break;
    case 0x7: /* fixs32 to f32 */
        for (i = 0; i < 4; i++) {
            src_s32 = env->vfp.reg[vrx].dspi[i];
            res_f32 = int32_to_float32(src_s32, &env->vfp.fp_status);
            res_f32 = float32_scalbn(res_f32, -imm, &env->vfp.fp_status);
            env->vfp.reg[vrz].f32[i] = res_f32;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}

void FPUV3_HELPER(vftoi)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float16 src_f16;
    float32 src_f32;
    uint16_t res_u16;
    int16_t res_s16;
    uint32_t res_u32;
    int32_t res_s32;
    uint32_t res_u32s[8];
    int32_t res_s32s[8];

    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    /*
     * | T1 | T2 | S |
     * +----+----+---+
     * | 7  | 6  | 5 |
     */
    result_type = (insn >> 5) & 0x7;

    switch (result_type) {
    case 0x0: /* f16 to u16 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            res_u16 = float16_to_uint16(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].udsps[i] = res_u16;
        }
        break;
    case 0x1: /* f16 to s16 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            res_s16 = float16_to_int16(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].dsps[i] = res_s16;
        }
        break;
    case 0x2: /* f16 to u32 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            res_u32s[i] = float16_to_uint32(src_f16, &env->vfp.fp_status);
        }
        for (i = 0; i < 8; i++) {
            int j = i / 4;
            env->vfp.reg[vrz + j].udspi[i % 4] = res_u32s[i];
        }
        break;
    case 0x3: /* f16 to s32 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            res_s32s[i] = float16_to_int32(src_f16, &env->vfp.fp_status);
        }
        for (i = 0; i < 8; i++) {
            int j = i / 4;
            env->vfp.reg[vrz + j].dspi[i % 4] = res_s32s[i];
        }
        break;
    //case 0x4: /* f32 to u16 */
    //    for (i = 0; i < 4; i++) {
    //        src_f32 = env->vfp.reg[vrx].f32[i];
    //        res_u16 = float32_to_uint16(src_f32, &env->vfp.fp_status);
    //        env->vfp.reg[vrz].udsps[i] = res_u16;
    //    }
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    break;
    //case 0x5: /* f32 to s16 */
    //    for (i = 0; i < 4; i++) {
    //        src_f32 = env->vfp.reg[vrx].f32[i];
    //        res_s16 = float32_to_int16(src_f32, &env->vfp.fp_status);
    //        env->vfp.reg[vrz].dsps[i] = res_s16;
    //    }
    //    env->vfp.reg[vrz].f64[1] = 0;
    //    break;
    case 0x6: /* f32 to u32 */
        for (i = 0; i < 4; i++) {
            src_f32 = env->vfp.reg[vrx].f32[i];
            res_u32 = float32_to_uint32(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].udspi[i] = res_u32;
        }
        break;
    case 0x7: /* f32 to s32 */
        for (i = 0; i < 4; i++) {
            src_f32 = env->vfp.reg[vrx].f32[i];
            res_s32 = float32_to_int32(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].dspi[i] = res_s32;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}

void FPUV3_HELPER(vitof)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, i;
    float16 res_f16;
    float32 res_f32;
    //float32 res_f32s[8];
    uint16_t src_u16;
    int16_t  src_s16;
    uint32_t src_u32;
    int32_t  src_s32;

    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;

    /*
     * | T1 | T2 | S |
     * +----+----+---+
     * | 7  | 6  | 5 |
     */
    result_type = (insn >> 5) & 0x7;

    switch (result_type) {
    case 0x0: /* u16 to f16 */
        for (i = 0; i < 8; i++) {
            src_u16 = env->vfp.reg[vrx].udsps[i];
            res_f16 = uint16_to_float16(src_u16, &env->vfp.fp_status);
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
        break;
    case 0x1: /* s16 to f16 */
        for (i = 0; i < 8; i++) {
            src_s16 = env->vfp.reg[vrx].dsps[i];
            res_f16 = int16_to_float16(src_s16, &env->vfp.fp_status);
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
        break;
    //case 0x2: /* u16 to f32 */
    //    for (i = 0; i < 8; i++) {
    //        src_u16 = env->vfp.reg[vrx].udsps[i];
    //        res_f32s[i] = uint16_to_float32(src_u16, &env->vfp.fp_status);
    //    }
    //    for (i = 0; i < 8; i++) {
    //        int j = i / 4;
    //        env->vfp.reg[vrz + j].f32[i % 4] = res_f32s[i];
    //    }
    //    break;
    //case 0x3: /* s16 to f32 */
    //    for (i = 0; i < 8; i++) {
    //        src_s16 = env->vfp.reg[vrx].dsps[i];
    //        res_f32s[i] = int16_to_float32(src_s16, &env->vfp.fp_status);
    //    }
    //    for (i = 0; i < 8; i++) {
    //        int j = i / 4;
    //        env->vfp.reg[vrz + j].f32[i % 4] = res_f32s[i];
    //    }
    //    break;
    case 0x4: /* u32 to f16 */
        for (i = 0; i < 4; i++) {
            src_u32 = env->vfp.reg[vrx].udspi[i];
            res_f16 = uint32_to_float16(src_u32, &env->vfp.fp_status);
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
        env->vfp.reg[vrz].f64[1] = 0;
        break;
    case 0x5: /* s32 to f16 */
        for (i = 0; i < 4; i++) {
            src_s32 = env->vfp.reg[vrx].dspi[i];
            res_f16 = int32_to_float16(src_s32, &env->vfp.fp_status);
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
        env->vfp.reg[vrz].f64[1] = 0;
        break;
    case 0x6: /* u32 to f32 */
        for (i = 0; i < 4; i++) {
            src_u32 = env->vfp.reg[vrx].udspi[i];
            res_f32 = uint32_to_float32(src_u32, &env->vfp.fp_status);
            env->vfp.reg[vrz].f32[i] = res_f32;
        }
        break;
    case 0x7: /* s32 to f32 */
        for (i = 0; i < 4; i++) {
            src_s32 = env->vfp.reg[vrx].dspi[i];
            res_f32 = int32_to_float32(src_s32, &env->vfp.fp_status);
            env->vfp.reg[vrz].f32[i] = res_f32;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}

void FPUV3_HELPER(vftoirm)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, mode_bits, i;
    float16 src_f16;
    float32 src_f32;
    uint16_t res_u16;
    int16_t res_s16;
    uint32_t res_u32s[8];
    int32_t res_s32s[8];
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    mode_bits = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & 0x3;

    switch (mode_bits) {
    case 0x0:
        env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
        break;
    case 0x1:
        env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
        break;
    case 0x2:
        env->vfp.fp_status.float_rounding_mode = float_round_up;
        break;
    case 0x3:
        env->vfp.fp_status.float_rounding_mode = float_round_down;
        break;
    default:
        break;
    }

    /*
     * | T1 | T2 | S |
     * +----+----+---+
     * | 7  | 6  | 5 |
     */
    result_type = (insn >> 5) & 0x7;

    switch (result_type) {
    case 0x0: /* f16 to u16 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            res_u16 = float16_to_uint16(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].udsps[i] = res_u16;
        }
        break;
    case 0x1: /* f16 to s16 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            res_s16 = float16_to_int16(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].dsps[i] = res_s16;
        }
        break;
    case 0x2: /* f16 to u32 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            res_u32s[i] = float16_to_uint32(src_f16, &env->vfp.fp_status);
        }
        for (i = 0; i < 8; i++) {
            int j = i / 4;
            env->vfp.reg[vrz + j].udspi[i % 4] = res_u32s[i];
        }
        break;
    case 0x3: /* f16 to s32 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            res_s32s[i] = float16_to_int32(src_f16, &env->vfp.fp_status);
        }
        for (i = 0; i < 8; i++) {
            int j = i / 4;
            env->vfp.reg[vrz + j].dspi[i % 4] = res_s32s[i];
        }
        break;
    case 0x4: /* f32 to u16 */
        for (i = 0; i < 4; i++) {
            src_f32 = env->vfp.reg[vrx].f32[i];
            res_u16 = float32_to_uint16(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].udsps[i] = res_u16;
        }
        env->vfp.reg[vrz].f64[1] = 0;
        break;
    case 0x5: /* f32 to s16 */
        for (i = 0; i < 4; i++) {
            src_f32 = env->vfp.reg[vrx].f32[i];
            res_s16 = float32_to_int16(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].dsps[i] = res_s16;
        }
        env->vfp.reg[vrz].f64[1] = 0;
        break;
    case 0x6: /* f32 to u32 */
        for (i = 0; i < 4; i++) {
            src_f32 = env->vfp.reg[vrx].f32[i];
            res_u32s[0] = float32_to_uint32(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].udspi[i] = res_u32s[0];
        }
        break;
    case 0x7: /* f32 to s32 */
        for (i = 0; i < 4; i++) {
            src_f32 = env->vfp.reg[vrx].f32[i];
            res_s32s[0] = float32_to_int32(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].dspi[i] = res_s32s[0];
        }
        break;
    default:
        env->vfp.fp_status.float_rounding_mode = round_mode;
        helper_exception(env, EXCP_CSKY_UDEF);
    }
    env->vfp.fp_status.float_rounding_mode = round_mode;
}

void FPUV3_HELPER(vftofirm)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t vrx, vrz, mode_bits, i;
    float16 src_f16;
    float32 src_f32;
    float16 res_f16;
    float32 res_f32;
    int round_mode = env->vfp.fp_status.float_rounding_mode;

    uint32_t result_type;

    vrx = (insn >> CSKY_FPUV3_VREG_SHI_VRX) & CSKY_FPUV3_VREG_MASK;
    vrz = (insn >> CSKY_FPUV3_VREG_SHI_VRZ) & CSKY_FPUV3_VREG_MASK;
    mode_bits = (insn >> CSKY_FPUV3_VREG_SHI_VRY) & 0x3;

    switch (mode_bits) {
    case 0x0:
        env->vfp.fp_status.float_rounding_mode = float_round_nearest_even;
        break;
    case 0x1:
        env->vfp.fp_status.float_rounding_mode = float_round_to_zero;
        break;
    case 0x2:
        env->vfp.fp_status.float_rounding_mode = float_round_up;
        break;
    case 0x3:
        env->vfp.fp_status.float_rounding_mode = float_round_down;
        break;
    default:
        break;
    }

    /*
     * | T1 | T2 |
     * +----+----+
     * | 7  | 6  |
     */
    result_type = (insn >> 6) & 0x3;

    switch (result_type) {
    case 0x0: /* f16 */
        for (i = 0; i < 8; i++) {
            src_f16 = env->vfp.reg[vrx].f16[i];
            res_f16 = float16_round_to_int(src_f16, &env->vfp.fp_status);
            env->vfp.reg[vrz].f16[i] = res_f16;
        }
        break;
    case 0x3: /* f32 */
        for (i = 0; i < 4; i++) {
            src_f32 = env->vfp.reg[vrx].f32[i];
            res_f32 = float32_round_to_int(src_f32, &env->vfp.fp_status);
            env->vfp.reg[vrz].f32[i] = res_f32;
        }
        break;
    default:
        env->vfp.fp_status.float_rounding_mode = round_mode;
        helper_exception(env, EXCP_CSKY_UDEF);
    }
    env->vfp.fp_status.float_rounding_mode = round_mode;
}

