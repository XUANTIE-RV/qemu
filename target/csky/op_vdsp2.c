/*
 * C-SKY helper routines
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
#include "exec/exec-all.h"
#include "csky_ldst.h"
#include <math.h>

#define SIGNBIT8    (1 << 7)
#define MAX_U8      ((uint8_t)0xff)
#define MIN_U8      ((uint8_t)0x0)
#define MAX_S8      ((int8_t)0x7f)
#define MIN_S8      ((int8_t)0x80)
#define SIGNBIT16   (1 << 15)
#define MAX_U16     ((uint16_t)0xffff)
#define MIN_U16     ((uint16_t)0x0)
#define MAX_S16     ((int16_t)0x7fff)
#define MIN_S16     ((int16_t)0x8000)
#define SIGNBIT32   (1 << 31)
#define MAX_U32     ((uint32_t)0xffffffff)
#define MIN_U32     ((uint32_t)0x0)
#define MAX_S32     ((int32_t)0x7fffffff)
#define MIN_S32     ((int32_t)0x80000000)
#define SIGNBIT64   ((uint64_t)1 << 63)
#define MAX_U64     ((uint64_t)0xffffffffffffffff)
#define MIN_U64     ((uint64_t)0x0)
#define MAX_S64     ((int64_t)0x7fffffffffffffff)
#define MIN_S64     ((int64_t)0x8000000000000000)

/* ADD/SUB/COMPARE instructions. */
static inline uint8_t sat_add_u8(uint8_t a, uint8_t b)
{
    uint8_t res = a + b;
    if (res < a) {
        res = MAX_U8;
    }
    return res;
}

static inline uint16_t sat_add_u16(uint16_t a, uint16_t b)
{
    uint16_t res = a + b;
    if (res < a) {
        res = MAX_U16;
    }
    return res;
}

static inline uint32_t sat_add_u32(uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (res < a) {
        res = MAX_U32;
    }
    return res;
}

static inline uint64_t sat_add_u64(uint64_t a, uint64_t b)
{
    uint64_t res = a + b;
    if (res < a) {
        res = MAX_U64;
    }
    return res;
}

static inline uint8_t sat_add_s8(uint8_t a, uint8_t b)
{
    uint8_t res = a + b;
    if (((res ^ a) & SIGNBIT8) && !((a ^ b) & SIGNBIT8)) {
        res = ~(((int8_t)a >> 7) ^ SIGNBIT8);
    }
    return res;
}

static inline uint16_t sat_add_s16(uint16_t a, uint16_t b)
{
    uint16_t res = a + b;
    if (((res ^ a) & SIGNBIT16) && !((a ^ b) & SIGNBIT16)) {
        res = ~(((int16_t)a >> 15) ^ SIGNBIT16);
    }
    return res;
}

static inline uint32_t sat_add_s32(uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (((res ^ a) & SIGNBIT32) && !((a ^ b) & SIGNBIT32)) {
        res = ~(((int32_t)a >> 31) ^ SIGNBIT32);
    }
    return res;
}

static inline uint64_t sat_add_s64(uint64_t a, uint64_t b)
{
    uint64_t res = a + b;
    if (((res ^ a) & SIGNBIT64) && !((a ^ b) & SIGNBIT64)) {
        res = ~(((int64_t)a >> 63) ^ SIGNBIT64);
    }
    return res;
}

static inline uint8_t sat_sub_u8(uint8_t a, uint8_t b)
{
    uint8_t res = a - b;
    if (res > a) {
        res = 0;
    }
    return res;
}

static inline uint16_t sat_sub_u16(uint16_t a, uint16_t b)
{
    uint16_t res = a - b;
    if (res > a) {
        res = 0;
    }
    return res;
}

static inline uint32_t sat_sub_u32(uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (res > a) {
        res = 0;
    }
    return res;
}

static inline uint64_t sat_sub_u64(uint64_t a, uint64_t b)
{
    uint64_t res = a - b;
    if (res > a) {
        res = 0;
    }
    return res;
}

static inline uint8_t sat_sub_s8(uint8_t a, uint8_t b)
{
    uint8_t res = a - b;
    if (((res ^ a) & SIGNBIT8) && ((a ^ b) & SIGNBIT8)) {
        res = ~(((int8_t)a >> 7) ^ SIGNBIT8);
    }
    return res;
}

static inline uint16_t sat_sub_s16(uint16_t a, uint16_t b)
{
    uint16_t res = a - b;
    if (((res ^ a) & SIGNBIT16) && ((a ^ b) & SIGNBIT16)) {
        res = ~(((int16_t)a >> 15) ^ SIGNBIT16);
    }
    return res;
}

static inline uint32_t sat_sub_s32(uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (((res ^ a) & SIGNBIT32) && ((a ^ b) & SIGNBIT32)) {
        res = ~(((int32_t)a >> 31) ^ SIGNBIT32);
    }
    return res;
}

static inline uint64_t sat_sub_s64(uint64_t a, uint64_t b)
{
    uint64_t res = a - b;
    if (((res ^ a) & SIGNBIT64) && ((a ^ b) & SIGNBIT64)) {
        res = ~(((int64_t)a >> 63) ^ SIGNBIT64);
    }
    return res;
}

static inline void decode_imm4(CPUCSKYState *env, uint32_t imm,
                               uint32_t *size, uint32_t *index) {
    if ((imm >> 2) == 1) {
        *size = 16;
        *index = imm & 0x3;
    } else if ((imm >> 2) == 2) {
        if (imm & 0x2) {
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
        *size = 32;
        *index = imm & 0x1;
    } else if ((imm >> 2) == 0) {
        *size = 8;
        *index = imm & 0x3;
    } else {
        *size = 8;
        *index = (imm & 0x3) + 4;
    }
}

static inline void decode_imm5(CPUCSKYState *env, uint32_t imm,
                               uint32_t *size, uint32_t *index) {
    if ((imm >> 3) == 1) {
        *size = 16;
        *index = imm & 0x7;
    } else if ((imm >> 3) == 2) {
        if (imm & 0x4) {
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
        *size = 32;
        *index = imm & 0x3;
    } else if ((imm >> 3) == 0) {
        *size = 8;
        *index = imm & 0x7;
    } else {
        *size = 8;
        *index = (imm & 0x7) + 8;
    }
}

static inline void decode_imm7(CPUCSKYState *env, uint32_t *imm,
                               uint32_t *width) {
    switch (*imm & 0x70) {
    case 0x20:
        if (*imm & 0x8) {
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
        *width = 8;
        *imm = *imm & 0xf;
        break;
    case 0x30:
        *width = 16;
        *imm = *imm & 0xf;
        break;
    case 0x10:
        *width = 32;
        *imm = *imm & 0xf;
        break;
    case 0x40:
        *width = 32;
        *imm = (*imm & 0xf) + 16;
        break;
    case 0x00:
        *width = 64;
        *imm = *imm & 0xf;
        break;
    case 0x70:
        *width = 64;
        *imm = (*imm & 0xf) + 16;
        break;
    case 0x60:
        *width = 64;
        *imm = (*imm & 0xf) + 32;
        break;
    case 0x50:
        *width = 64;
        *imm = (*imm & 0xf) + 48;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

static inline uint32_t get_size_from_imm(CPUCSKYState *env, uint32_t *imm)
{
    uint32_t res;
    if (*imm & 0x40) {
        res = 64;
        *imm &= 0x3f;
    } else if (*imm & 0x20) {
        res = 32;
        *imm &= 0x1f;
    } else if (*imm & 0x10) {
        res = 16;
        *imm &= 0xf;
    } else if (*imm & 0x8) {
        res = 8;
        *imm &= 0x7;
    } else {
        helper_exception(env, EXCP_CSKY_UDEF);
        return 0;
    }
    return res;
}

void VDSP2_HELPER(vadd)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] +
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] +
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] +
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspl[i] = (env->vfp.reg[rx].dspl[i] +
                                        env->vfp.reg[ry].dspl[i]);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vsub)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] -
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] -
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] -
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspl[i] = (env->vfp.reg[rx].dspl[i] -
                                        env->vfp.reg[ry].dspl[i]);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vadd_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = sat_add_s8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = sat_add_s16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = sat_add_s32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].udspl[i];
                env->vfp.reg[rz].udspl[i] = sat_add_s64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = sat_add_u8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = sat_add_u16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = sat_add_u32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].udspl[i];
                env->vfp.reg[rz].udspl[i] = sat_add_u64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vsub_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = sat_sub_s8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = sat_sub_s16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = sat_sub_s32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].udspl[i];
                env->vfp.reg[rz].udspl[i] = sat_sub_s64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = sat_sub_u8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = sat_sub_u16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = sat_sub_u32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].udspl[i];
                env->vfp.reg[rz].udspl[i] = sat_sub_u64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vadd_h)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udsps[i] +
                                         env->vfp.reg[ry].udsps[i]) >> 8;
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspi[i] +
                                         env->vfp.reg[ry].udspi[i]) >> 16;
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspl[i] +
                                         env->vfp.reg[ry].udspl[i]) >> 32;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    env->vfp.reg[rz].udspl[1] = 0;
}

void VDSP2_HELPER(vsub_h)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udsps[i] -
                                         env->vfp.reg[ry].udsps[i]) >> 8;
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspi[i] -
                                         env->vfp.reg[ry].udspi[i]) >> 16;
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspl[i] -
                                         env->vfp.reg[ry].udspl[i]) >> 32;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    env->vfp.reg[rz].udspl[1] = 0;
}

void VDSP2_HELPER(vadd_rh)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udsps[i] +
                                         env->vfp.reg[ry].udsps[i] +
                                         (1 << 7)) >> 8;
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspi[i] +
                                         env->vfp.reg[ry].udspi[i] +
                                         (1 << 15)) >> 16;
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspl[i] +
                                         env->vfp.reg[ry].udspl[i] +
                                         ((uint64_t)1 << 31)) >> 32;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    env->vfp.reg[rz].udspl[1] = 0;
}

void VDSP2_HELPER(vsub_rh)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udsps[i] -
                                         env->vfp.reg[ry].udsps[i] +
                                         (1 << 7)) >> 8;
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspi[i] -
                                         env->vfp.reg[ry].udspi[i] +
                                         (1 << 15)) >> 16;
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspl[i] -
                                         env->vfp.reg[ry].udspl[i] +
                                         ((uint64_t)1 << 31)) >> 32;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    env->vfp.reg[rz].udspl[1] = 0;
}

void VDSP2_HELPER(vadd_e)(CPUCSKYState *env, uint32_t insn)
{
    int i, number, half_n;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp1, tmp2;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;
    half_n = number / 2;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < half_n; i++) {
                tmp1.dsps[i] = (env->vfp.reg[rx].dspc[i] +
                                env->vfp.reg[ry].dspc[i]);
                tmp2.dsps[i] = (env->vfp.reg[rx].dspc[i + half_n] +
                                env->vfp.reg[ry].dspc[i + half_n]);
            }
            break;
        case 16:
            for (i = 0; i < half_n; i++) {
                tmp1.dspi[i] = (env->vfp.reg[rx].dsps[i] +
                                env->vfp.reg[ry].dsps[i]);
                tmp2.dspi[i] = (env->vfp.reg[rx].dsps[i + half_n] +
                                env->vfp.reg[ry].dsps[i + half_n]);
            }
            break;
        case 32:
            for (i = 0; i < half_n; i++) {
                tmp1.dspl[i] = ((int64_t)env->vfp.reg[rx].dspi[i] +
                                (int64_t)env->vfp.reg[ry].dspi[i]);
                tmp2.dspl[i] = ((int64_t)env->vfp.reg[rx].dspi[i + half_n] +
                                (int64_t)env->vfp.reg[ry].dspi[i + half_n]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < half_n; i++) {
                tmp1.udsps[i] = (env->vfp.reg[rx].udspc[i] +
                                 env->vfp.reg[ry].udspc[i]);
                tmp2.udsps[i] = (env->vfp.reg[rx].udspc[i + half_n] +
                                 env->vfp.reg[ry].udspc[i + half_n]);
            }
            break;
        case 16:
            for (i = 0; i < half_n; i++) {
                tmp1.udspi[i] = (env->vfp.reg[rx].udsps[i] +
                                 env->vfp.reg[ry].udsps[i]);
                tmp2.udspi[i] = (env->vfp.reg[rx].udsps[i + half_n] +
                                 env->vfp.reg[ry].udsps[i + half_n]);
            }
            break;
        case 32:
            for (i = 0; i < half_n; i++) {
                tmp1.udspl[i] = ((uint64_t)env->vfp.reg[rx].udspi[i] +
                                 (uint64_t)env->vfp.reg[ry].udspi[i]);
                tmp2.udspl[i] = ((uint64_t)env->vfp.reg[rx].udspi[i + half_n] +
                                 (uint64_t)env->vfp.reg[ry].udspi[i + half_n]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vsub_e)(CPUCSKYState *env, uint32_t insn)
{
    int i, number, half_n;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp1, tmp2;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;
    half_n = number / 2;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < half_n; i++) {
                tmp1.dsps[i] = (env->vfp.reg[rx].dspc[i] -
                                env->vfp.reg[ry].dspc[i]);
                tmp2.dsps[i] = (env->vfp.reg[rx].dspc[i + half_n] -
                                env->vfp.reg[ry].dspc[i + half_n]);
            }
            break;
        case 16:
            for (i = 0; i < half_n; i++) {
                tmp1.dspi[i] = (env->vfp.reg[rx].dsps[i] -
                                env->vfp.reg[ry].dsps[i]);
                tmp2.dspi[i] = (env->vfp.reg[rx].dsps[i + half_n] -
                                env->vfp.reg[ry].dsps[i + half_n]);
            }
            break;
        case 32:
            for (i = 0; i < half_n; i++) {
                tmp1.dspl[i] = ((int64_t)env->vfp.reg[rx].dspi[i] -
                                (int64_t)env->vfp.reg[ry].dspi[i]);
                tmp2.dspl[i] = ((int64_t)env->vfp.reg[rx].dspi[i + half_n] -
                                (int64_t)env->vfp.reg[ry].dspi[i + half_n]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < half_n; i++) {
                tmp1.udsps[i] = (env->vfp.reg[rx].udspc[i] -
                                 env->vfp.reg[ry].udspc[i]);
                tmp2.udsps[i] = (env->vfp.reg[rx].udspc[i + half_n] -
                                 env->vfp.reg[ry].udspc[i + half_n]);
            }
            break;
        case 16:
            for (i = 0; i < half_n; i++) {
                tmp1.udspi[i] = (env->vfp.reg[rx].udsps[i] -
                                 env->vfp.reg[ry].udsps[i]);
                tmp2.udspi[i] = (env->vfp.reg[rx].udsps[i + half_n] -
                                 env->vfp.reg[ry].udsps[i + half_n]);
            }
            break;
        case 32:
            for (i = 0; i < half_n; i++) {
                tmp1.udspl[i] = ((uint64_t)env->vfp.reg[rx].udspi[i] -
                                 (uint64_t)env->vfp.reg[ry].udspi[i]);
                tmp2.udspl[i] = ((uint64_t)env->vfp.reg[rx].udspi[i + half_n] -
                                 (uint64_t)env->vfp.reg[ry].udspi[i + half_n]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vadd_x)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp0, tmp1;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                tmp0.dsps[i] = (env->vfp.reg[rx].dsps[i] +
                                env->vfp.reg[ry].dspc[i]);
                tmp1.dsps[i] = (env->vfp.reg[rx + 1].dsps[i] +
                                env->vfp.reg[ry].dspc[i + number / 2]);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                tmp0.dspi[i] = (env->vfp.reg[rx].dspi[i] +
                                env->vfp.reg[ry].dsps[i]);
                tmp1.dspi[i] = (env->vfp.reg[rx + 1].dspi[i] +
                                env->vfp.reg[ry].dsps[i + number / 2]);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                tmp0.dspl[i] = (env->vfp.reg[rx].dspl[i] +
                                env->vfp.reg[ry].dspi[i]);
                tmp1.dspl[i] = (env->vfp.reg[rx + 1].dspl[i] +
                                env->vfp.reg[ry].dspi[i + number / 2]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                tmp0.udsps[i] = (env->vfp.reg[rx].udsps[i] +
                                 env->vfp.reg[ry].udspc[i]);
                tmp1.udsps[i] = (env->vfp.reg[rx + 1].udsps[i] +
                                 env->vfp.reg[ry].udspc[i + number / 2]);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                tmp0.udspi[i] = (env->vfp.reg[rx].udspi[i] +
                                 env->vfp.reg[ry].udsps[i]);
                tmp1.udspi[i] = (env->vfp.reg[rx + 1].udspi[i] +
                                 env->vfp.reg[ry].udsps[i + number / 2]);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                tmp0.udspl[i] = (env->vfp.reg[rx].udspl[i] +
                                 env->vfp.reg[ry].udspi[i]);
                tmp1.udspl[i] = (env->vfp.reg[rx + 1].udspl[i] +
                                 env->vfp.reg[ry].udspi[i + number / 2]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp0, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp1, sizeof(union VDSP));
}

void VDSP2_HELPER(vsub_x)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp0, tmp1;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                tmp0.dsps[i] = (env->vfp.reg[rx].dsps[i] -
                                env->vfp.reg[ry].dspc[i]);
                tmp1.dsps[i] = (env->vfp.reg[rx + 1].dsps[i] -
                                env->vfp.reg[ry].dspc[i + number / 2]);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                tmp0.dspi[i] = (env->vfp.reg[rx].dspi[i] -
                                env->vfp.reg[ry].dsps[i]);
                tmp1.dspi[i] = (env->vfp.reg[rx + 1].dspi[i] -
                                env->vfp.reg[ry].dsps[i + number / 2]);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                tmp0.dspl[i] = (env->vfp.reg[rx].dspl[i] -
                                env->vfp.reg[ry].dspi[i]);
                tmp1.dspl[i] = (env->vfp.reg[rx + 1].dspl[i] -
                                env->vfp.reg[ry].dspi[i + number / 2]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                tmp0.udsps[i] = (env->vfp.reg[rx].udsps[i] -
                                 env->vfp.reg[ry].udspc[i]);
                tmp1.udsps[i] = (env->vfp.reg[rx + 1].udsps[i] -
                                 env->vfp.reg[ry].udspc[i + number / 2]);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                tmp0.udspi[i] = (env->vfp.reg[rx].udspi[i] -
                                 env->vfp.reg[ry].udsps[i]);
                tmp1.udspi[i] = (env->vfp.reg[rx + 1].udspi[i] -
                                 env->vfp.reg[ry].udsps[i + number / 2]);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                tmp0.udspl[i] = (env->vfp.reg[rx].udspl[i] -
                                 env->vfp.reg[ry].udspi[i]);
                tmp1.udspl[i] = (env->vfp.reg[rx + 1].udspl[i] -
                                 env->vfp.reg[ry].udspi[i + number / 2]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp0, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp1, sizeof(union VDSP));
}

void VDSP2_HELPER(vaddh)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int16_t a, b;
                a = (int16_t)env->vfp.reg[rx].dspc[i];
                b = (int16_t)env->vfp.reg[ry].dspc[i];
                env->vfp.reg[rz].dspc[i] = (a + b) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int32_t a, b;
                a = (int32_t)env->vfp.reg[rx].dsps[i];
                b = (int32_t)env->vfp.reg[ry].dsps[i];
                env->vfp.reg[rz].dsps[i] = (a + b) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int64_t a, b;
                a = (int64_t)env->vfp.reg[rx].dspi[i];
                b = (int64_t)env->vfp.reg[ry].dspi[i];
                env->vfp.reg[rz].dspi[i] = (a + b) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint16_t a, b;
                a = (uint16_t)env->vfp.reg[rx].udspc[i];
                b = (uint16_t)env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = (a + b) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint32_t a, b;
                a = (uint32_t)env->vfp.reg[rx].udsps[i];
                b = (uint32_t)env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = (a + b) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint64_t a, b;
                a = (uint64_t)env->vfp.reg[rx].udspi[i];
                b = (uint64_t)env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = (a + b) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vsubh)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int16_t a, b;
                a = (int16_t)env->vfp.reg[rx].dspc[i];
                b = (int16_t)env->vfp.reg[ry].dspc[i];
                env->vfp.reg[rz].dspc[i] = (a - b) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int32_t a, b;
                a = (int32_t)env->vfp.reg[rx].dsps[i];
                b = (int32_t)env->vfp.reg[ry].dsps[i];
                env->vfp.reg[rz].dsps[i] = (a - b) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int64_t a, b;
                a = (int64_t)env->vfp.reg[rx].dspi[i];
                b = (int64_t)env->vfp.reg[ry].dspi[i];
                env->vfp.reg[rz].dspi[i] = (a - b) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint16_t a, b;
                a = (uint16_t)env->vfp.reg[rx].udspc[i];
                b = (uint16_t)env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = (a - b) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint32_t a, b;
                a = (uint32_t)env->vfp.reg[rx].udsps[i];
                b = (uint32_t)env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = (a - b) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint64_t a, b;
                a = (uint64_t)env->vfp.reg[rx].udspi[i];
                b = (uint64_t)env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = (a - b) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vaddh_r)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int16_t a, b;
                a = (int16_t)env->vfp.reg[rx].dspc[i];
                b = (int16_t)env->vfp.reg[ry].dspc[i];
                env->vfp.reg[rz].dspc[i] = (a + b + 1) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int32_t a, b;
                a = (int32_t)env->vfp.reg[rx].dsps[i];
                b = (int32_t)env->vfp.reg[ry].dsps[i];
                env->vfp.reg[rz].dsps[i] = (a + b + 1) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int64_t a, b;
                a = (int64_t)env->vfp.reg[rx].dspi[i];
                b = (int64_t)env->vfp.reg[ry].dspi[i];
                env->vfp.reg[rz].dspi[i] = (a + b + 1) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint16_t a, b;
                a = (uint16_t)env->vfp.reg[rx].udspc[i];
                b = (uint16_t)env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = (a + b + 1) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint32_t a, b;
                a = (uint32_t)env->vfp.reg[rx].udsps[i];
                b = (uint32_t)env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = (a + b + 1) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint64_t a, b;
                a = (uint64_t)env->vfp.reg[rx].udspi[i];
                b = (uint64_t)env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = (a + b + 1) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vsubh_r)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int16_t a, b;
                a = (int16_t)env->vfp.reg[rx].dspc[i];
                b = (int16_t)env->vfp.reg[ry].dspc[i];
                env->vfp.reg[rz].dspc[i] = (a - b + 1) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int32_t a, b;
                a = (int32_t)env->vfp.reg[rx].dsps[i];
                b = (int32_t)env->vfp.reg[ry].dsps[i];
                env->vfp.reg[rz].dsps[i] = (a - b + 1) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int64_t a, b;
                a = (int64_t)env->vfp.reg[rx].dspi[i];
                b = (int64_t)env->vfp.reg[ry].dspi[i];
                env->vfp.reg[rz].dspi[i] = (a - b + 1) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint16_t a, b;
                a = (uint16_t)env->vfp.reg[rx].udspc[i];
                b = (uint16_t)env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = (a - b + 1) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint32_t a, b;
                a = (uint32_t)env->vfp.reg[rx].udsps[i];
                b = (uint32_t)env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = (a - b + 1) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint64_t a, b;
                a = (uint64_t)env->vfp.reg[rx].udspi[i];
                b = (uint64_t)env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = (a - b + 1) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vpadd)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;
    union VDSP tmp;
    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number / 2; i++) {
            tmp.dspc[i] = (env->vfp.reg[rx].dspc[2 * i] +
                           env->vfp.reg[rx].dspc[2 * i + 1]);
            tmp.dspc[i + number / 2] = (env->vfp.reg[ry].dspc[2 * i] +
                                        env->vfp.reg[ry].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < number / 2; i++) {
            tmp.dsps[i] = (env->vfp.reg[rx].dsps[2 * i] +
                           env->vfp.reg[rx].dsps[2 * i + 1]);
            tmp.dsps[i + number / 2] = (env->vfp.reg[ry].dsps[2 * i] +
                                        env->vfp.reg[ry].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < number / 2; i++) {
            tmp.dspi[i] = (env->vfp.reg[rx].dspi[2 * i] +
                           env->vfp.reg[rx].dspi[2 * i + 1]);
            tmp.dspi[i + number / 2] = (env->vfp.reg[ry].dspi[2 * i] +
                                        env->vfp.reg[ry].dspi[2 * i + 1]);
        }
        break;
    case 64:
        for (i = 0; i < number / 2; i++) {
            tmp.dspl[i] = (env->vfp.reg[rx].dspl[2 * i] +
                           env->vfp.reg[rx].dspl[2 * i + 1]);
            tmp.dspl[i + number / 2] = (env->vfp.reg[ry].dspl[2 * i] +
                                        env->vfp.reg[ry].dspl[2 * i + 1]);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vpadd_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz, sign;
    union VDSP tmp;
    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                int8_t a, b;
                a = env->vfp.reg[rx].dspc[2 * i];
                b = env->vfp.reg[rx].dspc[2 * i + 1];
                tmp.dspc[i] = sat_add_s8(a, b);

                a = env->vfp.reg[ry].dspc[2 * i];
                b = env->vfp.reg[ry].dspc[2 * i + 1];
                tmp.dspc[i + number / 2] = sat_add_s8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[rx].dsps[2 * i + 1];
                tmp.dsps[i] = sat_add_s16(a, b);

                a = env->vfp.reg[ry].dsps[2 * i];
                b = env->vfp.reg[ry].dsps[2 * i + 1];
                tmp.dsps[i + number / 2] = sat_add_s16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[rx].dspi[2 * i + 1];
                tmp.dspi[i] = sat_add_s32(a, b);

                a = env->vfp.reg[ry].dspi[2 * i];
                b = env->vfp.reg[ry].dspi[2 * i + 1];
                tmp.dspi[i + number / 2] = sat_add_s32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspl[2 * i];
                b = env->vfp.reg[rx].dspl[2 * i + 1];
                tmp.dspl[i] = sat_add_s64(a, b);

                a = env->vfp.reg[ry].dspl[2 * i];
                b = env->vfp.reg[ry].dspl[2 * i + 1];
                tmp.dspl[i + number / 2] = sat_add_s64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].udspc[2 * i];
                b = env->vfp.reg[rx].udspc[2 * i + 1];
                tmp.udspc[i] = sat_add_u8(a, b);

                a = env->vfp.reg[ry].udspc[2 * i];
                b = env->vfp.reg[ry].udspc[2 * i + 1];
                tmp.udspc[i + number / 2] = sat_add_u8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[rx].udsps[2 * i + 1];
                tmp.udsps[i] = sat_add_u16(a, b);

                a = env->vfp.reg[ry].udsps[2 * i];
                b = env->vfp.reg[ry].udsps[2 * i + 1];
                tmp.udsps[i + number / 2] = sat_add_u16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[rx].udspi[2 * i + 1];
                tmp.udspi[i] = sat_add_u32(a, b);

                a = env->vfp.reg[ry].udspi[2 * i];
                b = env->vfp.reg[ry].udspi[2 * i + 1];
                tmp.udspi[i + number / 2] = sat_add_u32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspl[2 * i];
                b = env->vfp.reg[rx].udspl[2 * i + 1];
                tmp.udspl[i] = sat_add_u64(a, b);

                a = env->vfp.reg[ry].udspl[2 * i];
                b = env->vfp.reg[ry].udspl[2 * i + 1];
                tmp.udspl[i + number / 2] = sat_add_u64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vpadd_e)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, rz;
    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[2 * i] +
                                            env->vfp.reg[rx].dspc[2 * i + 1]);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[2 * i] +
                                            env->vfp.reg[rx].dsps[2 * i + 1]);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int64_t a = (int64_t)env->vfp.reg[rx].dspi[2 * i];
                int64_t b = (int64_t)env->vfp.reg[rx].dspi[2 * i + 1];
                env->vfp.reg[rz].dspl[i] = a + b;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[2 * i] +
                                             env->vfp.reg[rx].udspc[2 * i + 1]);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[2 * i] +
                                             env->vfp.reg[rx].udsps[2 * i + 1]);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint64_t a = (uint64_t)env->vfp.reg[rx].udspi[2 * i];
                uint64_t b = (uint64_t)env->vfp.reg[rx].udspi[2 * i + 1];
                env->vfp.reg[rz].udspl[i] = a + b;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vpadda_e)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, rz;
    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].dsps[i] += (env->vfp.reg[rx].dspc[2 * i] +
                                             env->vfp.reg[rx].dspc[2 * i + 1]);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].dspi[i] += (env->vfp.reg[rx].dsps[2 * i] +
                                             env->vfp.reg[rx].dsps[2 * i + 1]);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].dspl[i] += ((int64_t)env->vfp.reg[rx].dspi[2 * i] +
                                             (int64_t)env->vfp.reg[rx].dspi[2 * i + 1]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rz].udsps[i] +
                                             env->vfp.reg[rx].udspc[2 * i] +
                                             env->vfp.reg[rx].udspc[2 * i + 1]);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rz].udspi[i] +
                                             env->vfp.reg[rx].udsps[2 * i] +
                                             env->vfp.reg[rx].udsps[2 * i + 1]);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                env->vfp.reg[rz].udspl[i] = (env->vfp.reg[rz].udspl[i] +
                                             env->vfp.reg[rx].udspi[2 * i] +
                                             env->vfp.reg[rx].udspi[2 * i + 1]);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vasx_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].dspc[2 * i + 1];
                b = env->vfp.reg[ry].dspc[2 * i];
                tmp.dspc[2 * i + 1] = sat_add_s8(a, b);

                a = env->vfp.reg[rx].dspc[2 * i];
                b = env->vfp.reg[ry].dspc[2 * i + 1];
                tmp.dspc[2 * i] = sat_sub_s8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i + 1];
                b = env->vfp.reg[ry].dsps[2 * i];
                tmp.dsps[2 * i + 1] = sat_add_s16(a, b);

                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[ry].dsps[2 * i + 1];
                tmp.dsps[2 * i] = sat_sub_s16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i + 1];
                b = env->vfp.reg[ry].dspi[2 * i];
                tmp.dspi[2 * i + 1] = sat_add_s32(a, b);

                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[ry].dspi[2 * i + 1];
                tmp.dspi[2 * i] = sat_sub_s32(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].udspc[2 * i + 1];
                b = env->vfp.reg[ry].udspc[2 * i];
                tmp.udspc[2 * i + 1] = sat_add_u8(a, b);

                a = env->vfp.reg[rx].udspc[2 * i];
                b = env->vfp.reg[ry].udspc[2 * i + 1];
                tmp.udspc[2 * i] = sat_sub_u8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i + 1];
                b = env->vfp.reg[ry].udsps[2 * i];
                tmp.udsps[2 * i + 1] = sat_add_u16(a, b);

                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[ry].udsps[2 * i + 1];
                tmp.udsps[2 * i] = sat_sub_u16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i + 1];
                b = env->vfp.reg[ry].udspi[2 * i];
                tmp.udspi[2 * i + 1] = sat_add_u32(a, b);

                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[ry].udspi[2 * i + 1];
                tmp.udspi[2 * i] = sat_sub_u32(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vsax_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].dspc[2 * i + 1];
                b = env->vfp.reg[ry].dspc[2 * i];
                tmp.dspc[2 * i + 1] = sat_sub_s8(a, b);

                a = env->vfp.reg[rx].dspc[2 * i];
                b = env->vfp.reg[ry].dspc[2 * i + 1];
                tmp.dspc[2 * i] = sat_add_s8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i + 1];
                b = env->vfp.reg[ry].dsps[2 * i];
                tmp.dsps[2 * i + 1] = sat_sub_s16(a, b);

                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[ry].dsps[2 * i + 1];
                tmp.dsps[2 * i] = sat_add_s16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i + 1];
                b = env->vfp.reg[ry].dspi[2 * i];
                tmp.dspi[2 * i + 1] = sat_sub_s32(a, b);

                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[ry].dspi[2 * i + 1];
                tmp.dspi[2 * i] = sat_add_s32(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].udspc[2 * i + 1];
                b = env->vfp.reg[ry].udspc[2 * i];
                tmp.udspc[2 * i + 1] = sat_sub_u8(a, b);

                a = env->vfp.reg[rx].udspc[2 * i];
                b = env->vfp.reg[ry].udspc[2 * i + 1];
                tmp.udspc[2 * i] = sat_add_u8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i + 1];
                b = env->vfp.reg[ry].udsps[2 * i];
                tmp.udsps[2 * i + 1] = sat_sub_u16(a, b);

                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[ry].udsps[2 * i + 1];
                tmp.udsps[2 * i] = sat_add_u16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i + 1];
                b = env->vfp.reg[ry].udspi[2 * i];
                tmp.udspi[2 * i + 1] = sat_sub_u32(a, b);

                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[ry].udspi[2 * i + 1];
                tmp.udspi[2 * i] = sat_add_u32(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vasxh)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dspc[2 * i + 1];
                b = env->vfp.reg[ry].dspc[2 * i];
                tmp.dspc[2 * i + 1] = (a + b) >> 1;

                a = env->vfp.reg[rx].dspc[2 * i];
                b = env->vfp.reg[ry].dspc[2 * i + 1];
                tmp.dspc[2 * i] = (a - b) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i + 1];
                b = env->vfp.reg[ry].dsps[2 * i];
                tmp.dsps[2 * i + 1] = (a + b) >> 1;

                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[ry].dsps[2 * i + 1];
                tmp.dsps[2 * i] = (a - b) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i + 1];
                b = env->vfp.reg[ry].dspi[2 * i];
                tmp.dspi[2 * i + 1] = (a + b) >> 1;

                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[ry].dspi[2 * i + 1];
                tmp.dspi[2 * i] = (a - b) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udspc[2 * i + 1];
                b = env->vfp.reg[ry].udspc[2 * i];
                tmp.udspc[2 * i + 1] = (a + b) >> 1;

                a = env->vfp.reg[rx].udspc[2 * i];
                b = env->vfp.reg[ry].udspc[2 * i + 1];
                tmp.udspc[2 * i] = (a - b) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i + 1];
                b = env->vfp.reg[ry].udsps[2 * i];
                tmp.udsps[2 * i + 1] = (a + b) >> 1;

                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[ry].udsps[2 * i + 1];
                tmp.udsps[2 * i] = (a - b) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i + 1];
                b = env->vfp.reg[ry].udspi[2 * i];
                tmp.udspi[2 * i + 1] = (a + b) >> 1;

                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[ry].udspi[2 * i + 1];
                tmp.udspi[2 * i] = (a - b) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vsaxh)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspc[2 * i + 1];
                b = env->vfp.reg[ry].dspc[2 * i];
                tmp.dspc[2 * i + 1] = (a - b) >> 1;

                a = env->vfp.reg[rx].dspc[2 * i];
                b = env->vfp.reg[ry].dspc[2 * i + 1];
                tmp.dspc[2 * i] = (a + b) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i + 1];
                b = env->vfp.reg[ry].dsps[2 * i];
                tmp.dsps[2 * i + 1] = (a - b) >> 1;

                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[ry].dsps[2 * i + 1];
                tmp.dsps[2 * i] = (a + b) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i + 1];
                b = env->vfp.reg[ry].dspi[2 * i];
                tmp.dspi[2 * i + 1] = (a - b) >> 1;

                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[ry].dspi[2 * i + 1];
                tmp.dspi[2 * i] = (a + b) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspc[2 * i + 1];
                b = env->vfp.reg[ry].udspc[2 * i];
                tmp.udspc[2 * i + 1] = (a - b) >> 1;

                a = env->vfp.reg[rx].udspc[2 * i];
                b = env->vfp.reg[ry].udspc[2 * i + 1];
                tmp.udspc[2 * i] = (a + b) >> 1;
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i + 1];
                b = env->vfp.reg[ry].udsps[2 * i];
                tmp.udsps[2 * i + 1] = (a - b) >> 1;

                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[ry].udsps[2 * i + 1];
                tmp.udsps[2 * i] = (a + b) >> 1;
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i + 1];
                b = env->vfp.reg[ry].udspi[2 * i];
                tmp.udspi[2 * i + 1] = (a - b) >> 1;

                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[ry].udspi[2 * i + 1];
                tmp.udspi[2 * i] = (a + b) >> 1;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

static inline int8_t abs_8(int8_t a)
{
    if (a < 0) {
        a = -a;
    }
    return a;
}

static inline int16_t abs_16(int16_t a)
{
    if (a < 0) {
        a = -a;
    }
    return a;
}

static inline int32_t abs_32(int32_t a)
{
    return abs(a);
}

static inline int64_t abs_64(int64_t a)
{
    if (a < 0) {
        a = -a;
    }
    return a;
}

void VDSP2_HELPER(vabs)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspc[i] = abs_8(env->vfp.reg[rx].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dsps[i] = abs_16(env->vfp.reg[rx].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspi[i] = abs_32(env->vfp.reg[rx].dspi[i]);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vabs_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspc[i] == MIN_S8) {
                env->vfp.reg[rz].dspc[i] = MAX_S8;
            } else {
                env->vfp.reg[rz].dspc[i] = abs_8(env->vfp.reg[rx].dspc[i]);
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dsps[i] == MIN_S16) {
                env->vfp.reg[rz].dsps[i] = MAX_S16;
            } else {
                env->vfp.reg[rz].dsps[i] = abs_16(env->vfp.reg[rx].dsps[i]);
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspi[i] == MIN_S32) {
                env->vfp.reg[rz].dspi[i] = MAX_S32;
            } else {
                env->vfp.reg[rz].dspi[i] = abs_32(env->vfp.reg[rx].dspi[i]);
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vsabs_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz, sign;
    int64_t max_abs, min_abs;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        max_abs = ((int64_t)1 << (width - 1)) - 1;
        min_abs = -max_abs;
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int16_t res;
                res = ((int16_t)env->vfp.reg[rx].dspc[i]
                       - (int16_t)env->vfp.reg[ry].dspc[i]);
                if (res >= (int16_t)max_abs || res <= (int16_t)min_abs) {
                    env->vfp.reg[rz].dspc[i] = max_abs;
                } else {
                    env->vfp.reg[rz].dspc[i] = (res >= 0) ? res : -res;
                }
             }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int32_t res;
                res = ((int32_t)env->vfp.reg[rx].dsps[i]
                       - (int32_t)env->vfp.reg[ry].dsps[i]);
                if (res >= (int32_t)max_abs || res <= (int32_t)min_abs) {
                    env->vfp.reg[rz].dsps[i] = max_abs;
                } else {
                    env->vfp.reg[rz].dsps[i] = (res >= 0) ? res : -res;
                }
             }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int64_t res;
                res = ((int64_t)env->vfp.reg[rx].dspi[i]
                       - (int64_t)env->vfp.reg[ry].dspi[i]);
                if (res >= (int64_t)max_abs || res <= (int64_t)min_abs) {
                    env->vfp.reg[rz].dspi[i] = max_abs;
                } else {
                    env->vfp.reg[rz].dspi[i] = (res >= 0) ? res : -res;
                }
             }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        max_abs = ((int64_t)1 << width) - 1;
        min_abs = -max_abs;
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int16_t res;
                res = ((uint16_t)env->vfp.reg[rx].udspc[i]
                       - (uint16_t)env->vfp.reg[ry].udspc[i]);
                if (res >= (int16_t)max_abs || res <= (int16_t)min_abs) {
                    env->vfp.reg[rz].dspc[i] = max_abs;
                } else {
                    env->vfp.reg[rz].dspc[i] = (res >= 0) ? res : -res;
                }
             }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int32_t res;
                res = ((uint32_t)env->vfp.reg[rx].udsps[i]
                       - (uint32_t)env->vfp.reg[ry].udsps[i]);
                if (res >= (int32_t)max_abs || res <= (int32_t)min_abs) {
                    env->vfp.reg[rz].dsps[i] = max_abs;
                } else {
                    env->vfp.reg[rz].dsps[i] = (res >= 0) ? res : -res;
                }
             }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int64_t res;
                res = ((uint64_t)env->vfp.reg[rx].udspi[i]
                       - (uint64_t)env->vfp.reg[ry].udspi[i]);
                if (res >= (int64_t)max_abs || res <= (int64_t)min_abs) {
                    env->vfp.reg[rz].dspi[i] = max_abs;
                } else {
                    env->vfp.reg[rz].dspi[i] = (res >= 0) ? res : -res;
                }
             }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vsabs_e)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp1, tmp2;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dspc[i];
                b = env->vfp.reg[ry].dspc[i];
                tmp1.dsps[i] = abs_16(a - b);

                a = env->vfp.reg[rx].dspc[i + number / 2];
                b = env->vfp.reg[ry].dspc[i + number / 2];
                tmp2.dsps[i] = abs_16(a - b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dsps[i];
                b = env->vfp.reg[ry].dsps[i];
                tmp1.dspi[i] = abs_32(a - b);

                a = env->vfp.reg[rx].dsps[i + number / 2];
                b = env->vfp.reg[ry].dsps[i + number / 2];
                tmp2.dspi[i] = abs_32(a - b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspi[i];
                b = env->vfp.reg[ry].dspi[i];
                tmp1.dspl[i] = abs_64(a - b);

                a = env->vfp.reg[rx].dspi[i + number / 2];
                b = env->vfp.reg[ry].dspi[i + number / 2];
                tmp2.dspl[i] = abs_64(a - b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].udspc[i];
                tmp1.udsps[i] = abs_16(a - b);

                a = env->vfp.reg[rx].udspc[i + number / 2];
                b = env->vfp.reg[ry].udspc[i + number / 2];
                tmp2.udsps[i] = abs_16(a - b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].udsps[i];
                tmp1.udspi[i] = abs_32(a - b);

                a = env->vfp.reg[rx].udsps[i + number / 2];
                b = env->vfp.reg[ry].udsps[i + number / 2];
                tmp2.udspi[i] = abs_32(a - b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].udspi[i];
                tmp1.udspl[i] = abs_64(a - b);

                a = env->vfp.reg[rx].udspi[i + number / 2];
                b = env->vfp.reg[ry].udspi[i + number / 2];
                tmp2.udspl[i] = abs_64(a - b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vsabsa)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                env->vfp.reg[rz].dspc[i] += abs_32(env->vfp.reg[rx].dspc[i]
                                                  - env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                env->vfp.reg[rz].dsps[i] += abs_32(env->vfp.reg[rx].dsps[i]
                                                   - env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int64_t a = env->vfp.reg[rx].dspi[i];
                int64_t b = env->vfp.reg[ry].dspi[i];
                env->vfp.reg[rz].dspi[i] += abs_64(a - b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                env->vfp.reg[rz].udspc[i] += abs_32(env->vfp.reg[rx].udspc[i]
                                                  - env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                env->vfp.reg[rz].udsps[i] += abs_32(env->vfp.reg[rx].udsps[i]
                                                   - env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint64_t a = env->vfp.reg[rx].udspi[i];
                uint64_t b = env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] += abs_64(a - b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vsabsa_e)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp1, tmp2;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dspc[i];
                b = env->vfp.reg[ry].dspc[i];
                tmp1.dsps[i] = env->vfp.reg[rz].dsps[i] + abs_16(a - b);

                a = env->vfp.reg[rx].dspc[i + number / 2];
                b = env->vfp.reg[ry].dspc[i + number / 2];
                tmp2.dsps[i] = env->vfp.reg[rz + 1].dsps[i] + abs_16(a - b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dsps[i];
                b = env->vfp.reg[ry].dsps[i];
                tmp1.dspi[i] = env->vfp.reg[rz].dspi[i] + abs_32(a - b);

                a = env->vfp.reg[rx].dsps[i + number / 2];
                b = env->vfp.reg[ry].dsps[i + number / 2];
                tmp2.dspi[i] = env->vfp.reg[rz + 1].dspi[i] + abs_32(a - b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspi[i];
                b = env->vfp.reg[ry].dspi[i];
                tmp1.dspl[i] = env->vfp.reg[rz].dspl[i] + abs_64(a - b);

                a = env->vfp.reg[rx].dspi[i + number / 2];
                b = env->vfp.reg[ry].dspi[i + number / 2];
                tmp2.dspl[i] = env->vfp.reg[rz + 1].dspl[i] + abs_64(a - b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].udspc[i];
                tmp1.udsps[i] = env->vfp.reg[rz].udsps[i] + abs_16(a - b);

                a = env->vfp.reg[rx].udspc[i + number / 2];
                b = env->vfp.reg[ry].udspc[i + number / 2];
                tmp2.udsps[i] = env->vfp.reg[rz + 1].udsps[i] + abs_16(a - b);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].udsps[i];
                tmp1.udspi[i] = env->vfp.reg[rz].udspi[i] + abs_32(a - b);

                a = env->vfp.reg[rx].udsps[i + number / 2];
                b = env->vfp.reg[ry].udsps[i + number / 2];
                tmp2.udspi[i] = env->vfp.reg[rz + 1].udspi[i] + abs_32(a - b);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].udspi[i];
                tmp1.udspl[i] = env->vfp.reg[rz].udspl[i] + abs_64(a - b);

                a = env->vfp.reg[rx].udspi[i + number / 2];
                b = env->vfp.reg[ry].udspi[i + number / 2];
                tmp2.udspl[i] = env->vfp.reg[rz + 1].udspl[i] + abs_64(a - b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vneg)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspc[i] = -(env->vfp.reg[rx].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dsps[i] = -(env->vfp.reg[rx].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspi[i] = -(env->vfp.reg[rx].dspi[i]);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vneg_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspc[i] == MIN_S8) {
                env->vfp.reg[rx].dspc[i] = MAX_S8;
            } else {
                env->vfp.reg[rz].dspc[i] = -(env->vfp.reg[rx].dspc[i]);
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dsps[i] == MIN_S16) {
                env->vfp.reg[rx].dsps[i] = MAX_S16;
            } else {
                env->vfp.reg[rz].dsps[i] = -(env->vfp.reg[rx].dsps[i]);
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspi[i] == MIN_S32) {
                env->vfp.reg[rx].dspi[i] = MAX_S32;
            } else {
                env->vfp.reg[rz].dspi[i] = -(env->vfp.reg[rx].dspi[i]);
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vmax)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dspc[i] >= env->vfp.reg[ry].dspc[i]) {
                    env->vfp.reg[rz].dspc[i] = env->vfp.reg[rx].dspc[i];
                } else {
                    env->vfp.reg[rz].dspc[i] = env->vfp.reg[ry].dspc[i];
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dsps[i] >= env->vfp.reg[ry].dsps[i]) {
                    env->vfp.reg[rz].dsps[i] = env->vfp.reg[rx].dsps[i];
                } else {
                    env->vfp.reg[rz].dsps[i] = env->vfp.reg[ry].dsps[i];
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dspi[i] >= env->vfp.reg[ry].dspi[i]) {
                    env->vfp.reg[rz].dspi[i] = env->vfp.reg[rx].dspi[i];
                } else {
                    env->vfp.reg[rz].dspi[i] = env->vfp.reg[ry].dspi[i];
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udspc[i] >= env->vfp.reg[ry].udspc[i]) {
                    env->vfp.reg[rz].udspc[i] = env->vfp.reg[rx].udspc[i];
                } else {
                    env->vfp.reg[rz].udspc[i] = env->vfp.reg[ry].udspc[i];
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udsps[i] >= env->vfp.reg[ry].udsps[i]) {
                    env->vfp.reg[rz].udsps[i] = env->vfp.reg[rx].udsps[i];
                } else {
                    env->vfp.reg[rz].udsps[i] = env->vfp.reg[ry].udsps[i];
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udspi[i] >= env->vfp.reg[ry].udspi[i]) {
                    env->vfp.reg[rz].udspi[i] = env->vfp.reg[rx].udspi[i];
                } else {
                    env->vfp.reg[rz].udspi[i] = env->vfp.reg[ry].udspi[i];
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vmin)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dspc[i] <= env->vfp.reg[ry].dspc[i]) {
                    env->vfp.reg[rz].dspc[i] = env->vfp.reg[rx].dspc[i];
                } else {
                    env->vfp.reg[rz].dspc[i] = env->vfp.reg[ry].dspc[i];
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dsps[i] <= env->vfp.reg[ry].dsps[i]) {
                    env->vfp.reg[rz].dsps[i] = env->vfp.reg[rx].dsps[i];
                } else {
                    env->vfp.reg[rz].dsps[i] = env->vfp.reg[ry].dsps[i];
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dspi[i] <= env->vfp.reg[ry].dspi[i]) {
                    env->vfp.reg[rz].dspi[i] = env->vfp.reg[rx].dspi[i];
                } else {
                    env->vfp.reg[rz].dspi[i] = env->vfp.reg[ry].dspi[i];
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udspc[i] <= env->vfp.reg[ry].udspc[i]) {
                    env->vfp.reg[rz].udspc[i] = env->vfp.reg[rx].udspc[i];
                } else {
                    env->vfp.reg[rz].udspc[i] = env->vfp.reg[ry].udspc[i];
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udsps[i] <= env->vfp.reg[ry].udsps[i]) {
                    env->vfp.reg[rz].udsps[i] = env->vfp.reg[rx].udsps[i];
                } else {
                    env->vfp.reg[rz].udsps[i] = env->vfp.reg[ry].udsps[i];
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udspi[i] <= env->vfp.reg[ry].udspi[i]) {
                    env->vfp.reg[rz].udspi[i] = env->vfp.reg[rx].udspi[i];
                } else {
                    env->vfp.reg[rz].udspi[i] = env->vfp.reg[ry].udspi[i];
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vpmax)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                int8_t a, b;
                a = env->vfp.reg[rx].dspc[2 * i];
                b = env->vfp.reg[rx].dspc[2 * i + 1];
                if (a >= b) {
                    tmp.dspc[i] = a;
                } else {
                    tmp.dspc[i] = b;
                }

                a = env->vfp.reg[ry].dspc[2 * i];
                b = env->vfp.reg[ry].dspc[2 * i + 1];
                if (a >= b) {
                    tmp.dspc[i + number / 2] = a;
                } else {
                    tmp.dspc[i + number / 2] = b;
                }
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[rx].dsps[2 * i + 1];
                if (a >= b) {
                    tmp.dsps[i] = a;
                } else {
                    tmp.dsps[i] = b;
                }

                a = env->vfp.reg[ry].dsps[2 * i];
                b = env->vfp.reg[ry].dsps[2 * i + 1];
                if (a >= b) {
                    tmp.dsps[i + number / 2] = a;
                } else {
                    tmp.dsps[i + number / 2] = b;
                }
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[rx].dspi[2 * i + 1];
                if (a >= b) {
                    tmp.dspi[i] = a;
                } else {
                    tmp.dspi[i] = b;
                }

                a = env->vfp.reg[ry].dspi[2 * i];
                b = env->vfp.reg[ry].dspi[2 * i + 1];
                if (a >= b) {
                    tmp.dspi[i + number / 2] = a;
                } else {
                    tmp.dspi[i + number / 2] = b;
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].udspc[2 * i];
                b = env->vfp.reg[rx].udspc[2 * i + 1];
                if (a >= b) {
                    tmp.udspc[i] = a;
                } else {
                    tmp.udspc[i] = b;
                }

                a = env->vfp.reg[ry].udspc[2 * i];
                b = env->vfp.reg[ry].udspc[2 * i + 1];
                if (a >= b) {
                    tmp.udspc[i + number / 2] = a;
                } else {
                    tmp.udspc[i + number / 2] = b;
                }
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[rx].udsps[2 * i + 1];
                if (a >= b) {
                    tmp.udsps[i] = a;
                } else {
                    tmp.udsps[i] = b;
                }

                a = env->vfp.reg[ry].udsps[2 * i];
                b = env->vfp.reg[ry].udsps[2 * i + 1];
                if (a >= b) {
                    tmp.udsps[i + number / 2] = a;
                } else {
                    tmp.udsps[i + number / 2] = b;
                }
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[rx].udspi[2 * i + 1];
                if (a >= b) {
                    tmp.udspi[i] = a;
                } else {
                    tmp.udspi[i] = b;
                }

                a = env->vfp.reg[ry].udspi[2 * i];
                b = env->vfp.reg[ry].udspi[2 * i + 1];
                if (a >= b) {
                    tmp.udspi[i + number / 2] = a;
                } else {
                    tmp.udspi[i + number / 2] = b;
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vpmin)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                int8_t a, b;
                a = env->vfp.reg[rx].dspc[2 * i];
                b = env->vfp.reg[rx].dspc[2 * i + 1];
                if (a <= b) {
                    tmp.dspc[i] = a;
                } else {
                    tmp.dspc[i] = b;
                }

                a = env->vfp.reg[ry].dspc[2 * i];
                b = env->vfp.reg[ry].dspc[2 * i + 1];
                if (a <= b) {
                    tmp.dspc[i + number / 2] = a;
                } else {
                    tmp.dspc[i + number / 2] = b;
                }
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[rx].dsps[2 * i + 1];
                if (a <= b) {
                    tmp.dsps[i] = a;
                } else {
                    tmp.dsps[i] = b;
                }

                a = env->vfp.reg[ry].dsps[2 * i];
                b = env->vfp.reg[ry].dsps[2 * i + 1];
                if (a <= b) {
                    tmp.dsps[i + number / 2] = a;
                } else {
                    tmp.dsps[i + number / 2] = b;
                }
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[rx].dspi[2 * i + 1];
                if (a <= b) {
                    tmp.dspi[i] = a;
                } else {
                    tmp.dspi[i] = b;
                }

                a = env->vfp.reg[ry].dspi[2 * i];
                b = env->vfp.reg[ry].dspi[2 * i + 1];
                if (a <= b) {
                    tmp.dspi[i + number / 2] = a;
                } else {
                    tmp.dspi[i + number / 2] = b;
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint8_t a, b;
                a = env->vfp.reg[rx].udspc[2 * i];
                b = env->vfp.reg[rx].udspc[2 * i + 1];
                if (a <= b) {
                    tmp.udspc[i] = a;
                } else {
                    tmp.udspc[i] = b;
                }

                a = env->vfp.reg[ry].udspc[2 * i];
                b = env->vfp.reg[ry].udspc[2 * i + 1];
                if (a <= b) {
                    tmp.udspc[i + number / 2] = a;
                } else {
                    tmp.udspc[i + number / 2] = b;
                }
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[rx].udsps[2 * i + 1];
                if (a <= b) {
                    tmp.udsps[i] = a;
                } else {
                    tmp.udsps[i] = b;
                }

                a = env->vfp.reg[ry].udsps[2 * i];
                b = env->vfp.reg[ry].udsps[2 * i + 1];
                if (a <= b) {
                    tmp.udsps[i + number / 2] = a;
                } else {
                    tmp.udsps[i + number / 2] = b;
                }
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[rx].udspi[2 * i + 1];
                if (a <= b) {
                    tmp.udspi[i] = a;
                } else {
                    tmp.udspi[i] = b;
                }

                a = env->vfp.reg[ry].udspi[2 * i];
                b = env->vfp.reg[ry].udspi[2 * i + 1];
                if (a <= b) {
                    tmp.udspi[i + number / 2] = a;
                } else {
                    tmp.udspi[i + number / 2] = b;
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vcmpnez)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspc[i] != 0) {
                env->vfp.reg[rz].dspc[i] = 0xff;
            } else {
                env->vfp.reg[rz].dspc[i] = 0;
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dsps[i] != 0) {
                env->vfp.reg[rz].dsps[i] = 0xffff;
            } else {
                env->vfp.reg[rz].dsps[i] = 0;
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspi[i] != 0) {
                env->vfp.reg[rz].dspi[i] = 0xffffffff;
            } else {
                env->vfp.reg[rz].dspi[i] = 0;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vcmpne)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspc[i] != env->vfp.reg[ry].dspc[i]) {
                env->vfp.reg[rz].dspc[i] = 0xff;
            } else {
                env->vfp.reg[rz].dspc[i] = 0;
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dsps[i] != env->vfp.reg[ry].dsps[i]) {
                env->vfp.reg[rz].dsps[i] = 0xffff;
            } else {
                env->vfp.reg[rz].dsps[i] = 0;
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspi[i] != env->vfp.reg[ry].dspi[i]) {
                env->vfp.reg[rz].dspi[i] = 0xffffffff;
            } else {
                env->vfp.reg[rz].dspi[i] = 0;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vcmphsz)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspc[i] >= 0) {
                env->vfp.reg[rz].dspc[i] = 0xff;
            } else {
                env->vfp.reg[rz].dspc[i] = 0;
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dsps[i] >= 0) {
                env->vfp.reg[rz].dsps[i] = 0xffff;
            } else {
                env->vfp.reg[rz].dsps[i] = 0;
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspi[i] >= 0) {
                env->vfp.reg[rz].dspi[i] = 0xffffffff;
            } else {
                env->vfp.reg[rz].dspi[i] = 0;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vcmphs)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dspc[i] >= env->vfp.reg[ry].dspc[i]) {
                    env->vfp.reg[rz].dspc[i] = 0xff;
                } else {
                    env->vfp.reg[rz].dspc[i] = 0;
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dsps[i] >= env->vfp.reg[ry].dsps[i]) {
                    env->vfp.reg[rz].dsps[i] = 0xffff;
                } else {
                    env->vfp.reg[rz].dsps[i] = 0;
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dspi[i] >= env->vfp.reg[ry].dspi[i]) {
                    env->vfp.reg[rz].dspi[i] = 0xffffffff;
                } else {
                    env->vfp.reg[rz].dspi[i] = 0;
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udspc[i] >= env->vfp.reg[ry].udspc[i]) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                } else {
                    env->vfp.reg[rz].udspc[i] = 0;
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udsps[i] >= env->vfp.reg[ry].udsps[i]) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                } else {
                    env->vfp.reg[rz].udsps[i] = 0;
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udspi[i] >= env->vfp.reg[ry].udspi[i]) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                } else {
                    env->vfp.reg[rz].udspi[i] = 0;
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vcmpltz)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspc[i] < 0) {
                env->vfp.reg[rz].dspc[i] = 0xff;
            } else {
                env->vfp.reg[rz].dspc[i] = 0;
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dsps[i] < 0) {
                env->vfp.reg[rz].dsps[i] = 0xffff;
            } else {
                env->vfp.reg[rz].dsps[i] = 0;
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspi[i] < 0) {
                env->vfp.reg[rz].dspi[i] = 0xffffffff;
            } else {
                env->vfp.reg[rz].dspi[i] = 0;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vcmplt)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dspc[i] < env->vfp.reg[ry].dspc[i]) {
                    env->vfp.reg[rz].dspc[i] = 0xff;
                } else {
                    env->vfp.reg[rz].dspc[i] = 0;
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dsps[i] < env->vfp.reg[ry].dsps[i]) {
                    env->vfp.reg[rz].dsps[i] = 0xffff;
                } else {
                    env->vfp.reg[rz].dsps[i] = 0;
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].dspi[i] < env->vfp.reg[ry].dspi[i]) {
                    env->vfp.reg[rz].dspi[i] = 0xffffffff;
                } else {
                    env->vfp.reg[rz].dspi[i] = 0;
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udspc[i] < env->vfp.reg[ry].udspc[i]) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                } else {
                    env->vfp.reg[rz].udspc[i] = 0;
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udsps[i] < env->vfp.reg[ry].udsps[i]) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                } else {
                    env->vfp.reg[rz].udsps[i] = 0;
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                if (env->vfp.reg[rx].udspi[i] < env->vfp.reg[ry].udspi[i]) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                } else {
                    env->vfp.reg[rz].udspi[i] = 0;
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vcmphz)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspc[i] > 0) {
                env->vfp.reg[rz].dspc[i] = 0xff;
            } else {
                env->vfp.reg[rz].dspc[i] = 0;
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dsps[i] > 0) {
                env->vfp.reg[rz].dsps[i] = 0xffff;
            } else {
                env->vfp.reg[rz].dsps[i] = 0;
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspi[i] > 0) {
                env->vfp.reg[rz].dspi[i] = 0xffffffff;
            } else {
                env->vfp.reg[rz].dspi[i] = 0;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vcmplsz)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspc[i] <= 0) {
                env->vfp.reg[rz].dspc[i] = 0xff;
            } else {
                env->vfp.reg[rz].dspc[i] = 0;
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dsps[i] <= 0) {
                env->vfp.reg[rz].dsps[i] = 0xffff;
            } else {
                env->vfp.reg[rz].dsps[i] = 0;
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspi[i] <= 0) {
                env->vfp.reg[rz].dspi[i] = 0xffffffff;
            } else {
                env->vfp.reg[rz].dspi[i] = 0;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vclip)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, imm, rx, rz;
    int64_t max_imm, min_imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    number = 128 / width;

    if (sign) {
        imm++;      /* oimm */
        max_imm = ((uint64_t)1 << (imm - 1)) - 1;
        min_imm = -((uint64_t)1 << (imm - 1));
    } else {
        max_imm = ((uint64_t)1 << imm) - 1;
        min_imm = 0;
    }

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspc[i] > (int8_t)max_imm) {
                env->vfp.reg[rz].dspc[i] = (int8_t)max_imm;
            } else if (env->vfp.reg[rx].dspc[i] < (int8_t)min_imm) {
                env->vfp.reg[rz].dspc[i] = (int8_t)min_imm;
            } else {
                env->vfp.reg[rz].dspc[i] = env->vfp.reg[rx].dspc[i];
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dsps[i] > (int16_t)max_imm) {
                env->vfp.reg[rz].dsps[i] = (int16_t)max_imm;
            } else if (env->vfp.reg[rx].dsps[i] < (int16_t)min_imm) {
                env->vfp.reg[rz].dsps[i] = (int16_t)min_imm;
            } else {
                env->vfp.reg[rz].dsps[i] = env->vfp.reg[rx].dsps[i];
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspi[i] > (int32_t)max_imm) {
                env->vfp.reg[rz].dspi[i] = (int32_t)max_imm;
            } else if (env->vfp.reg[rx].dspi[i] < (int32_t)min_imm) {
                env->vfp.reg[rz].dspi[i] = (int32_t)min_imm;
            } else {
                env->vfp.reg[rz].dspi[i] = env->vfp.reg[rx].dspi[i];
            }
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[rx].dspl[i] > (int64_t)max_imm) {
                env->vfp.reg[rz].dspl[i] = (int64_t)max_imm;
            } else if (env->vfp.reg[rx].dspl[i] < (int64_t)min_imm) {
                env->vfp.reg[rz].dspl[i] = (int64_t)min_imm;
            } else {
                env->vfp.reg[rz].dspl[i] = env->vfp.reg[rx].dspl[i];
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

/* MUL/DIV instructions. */
static int8_t sat_round_addh_s8(int8_t x, int8_t y, int8_t z)
{
    int16_t round;
    int8_t tmp;

    round = (int16_t)(1 << 7);
    if (x == (int8_t)(1 << 7) && y == (int8_t)(1 << 7)) {
        if (z & SIGNBIT8) {
            tmp = ((((int16_t)z << 8) + (((int16_t)x * (int16_t)y) << 1) +
                round) >> 8) & 0xff;
        } else {
            tmp = 0x7f;
        }
    } else {
        tmp = (sat_add_s16(((int16_t)z << 8) + round,
                ((int16_t)x * (int16_t)y) << 1) >> 8) & 0xff;
    }
    return tmp;
}
static int16_t sat_round_addh_s16(int16_t x, int16_t y, int16_t z)
{
    int32_t round;
    int16_t tmp;

    round = (int32_t)(1 << 15);
    if (x == (int16_t)(1 << 15) && y == (int16_t)(1 << 15)) {
        if (z & SIGNBIT16) {
            tmp = ((((int32_t)z << 16) + (((int32_t)x * (int32_t)y) << 1) +
                round) >> 16) & 0xffff;
        } else {
            tmp = 0x7fff;
        }
    } else {
        tmp = (sat_add_s32(((int32_t)z << 16) + round,
                ((int32_t)x * (int32_t)y) << 1) >> 16) & 0xffff;
    }
    return tmp;
}
static int32_t sat_round_addh_s32(int32_t x, int32_t y, int32_t z)
{
    int64_t round;
    int32_t tmp;

    round = ((int64_t)1 << 31);
    if (x == (int32_t)(1 << 31) && y == (int32_t)(1 << 31)) {
        if (z & SIGNBIT32) {
            tmp = ((((int64_t)z << 32) + (((int64_t)x * (int64_t)y) << 1) +
                round) >> 32) & 0xffffffff;
        } else {
            tmp = 0x7fffffff;
        }
    } else {
        tmp = (sat_add_s64(((int64_t)z << 32) + round,
                ((int64_t)x * (int64_t)y) << 1) >> 32) & 0xffffffff;
    }
    return tmp;
}
static int8_t sat_round_subh_s8(int8_t x, int8_t y, int8_t z)
{
    int16_t round;
    int8_t tmp;

    round = (int16_t)(1 << 7);
    if (x == (int8_t)(1 << 7) && y == (int8_t)(1 << 7)) {
        if (z & SIGNBIT8) {
            tmp = 0x80;
        } else {
            tmp = ((((int16_t)z << 8) - (((int16_t)x * (int16_t)y) << 1) +
                round) >> 8) & 0xff;
        }
    } else {
        tmp = (sat_sub_s16(((int16_t)z << 8) + round,
                ((int16_t)x * (int16_t)y) << 1) >> 8) & 0xff;
    }
    return tmp;
}
static int16_t sat_round_subh_s16(int16_t x, int16_t y, int16_t z)
{
    int32_t round;
    int16_t tmp;

    round = (int32_t)(1 << 15);
    if (x == (int16_t)(1 << 15) && y == (int16_t)(1 << 15)) {
        if (z & SIGNBIT16) {
            tmp = 0x8000;
        } else {
            tmp = ((((int32_t)z << 16) - (((int32_t)x * (int32_t)y) << 1) +
                round) >> 16) & 0xffff;
        }
    } else {
        tmp = (sat_sub_s32(((int32_t)z << 16) + round,
                ((int32_t)x * (int32_t)y) << 1) >> 16) & 0xffff;
    }
    return tmp;
}
static int32_t sat_round_subh_s32(int32_t x, int32_t y, int32_t z)
{
    int64_t round;
    int32_t tmp;

    round = (int64_t)1 << 31;
    if (x == (int32_t)(1 << 31) && y == (int32_t)(1 << 31)) {
        if (z & SIGNBIT32) {
            tmp = 0x80000000;
        } else {
            tmp = ((((int64_t)z << 32) - (((int64_t)x * (int64_t)y) << 1) +
                round) >> 32) & 0xffffffff;
        }
    } else {
        tmp = (sat_sub_s64(((int64_t)z << 32) + round,
                ((int64_t)x * (int64_t)y) << 1) >> 32) & 0xffffffff;
    }
    return tmp;
}
static int8_t sat_round_aah_s8(int8_t x1, int8_t y1,  int8_t x2,
        int8_t y2)
{
    int16_t round, result;
    int8_t tmp;
    bool sat = false;

    round = (int16_t)1 << 7;
    if (!(x1 == (int8_t)(1 << 7) && y1 == (int8_t)(1 << 7)) &&
        !(x2 == (int8_t)(1 << 7) && y2 == (int8_t)(1 << 7))) {
        result = sat_add_s16((int16_t)x1 * (int16_t)y1 << 1,
                    (int16_t)x2 * (int16_t)y2 << 1);
        if (result == MAX_S16 || result == MIN_S16) {
            sat = true;
        }
        if (sat) {
            if (!(result & SIGNBIT16)) {
                tmp = (result >> 8) & 0xff;
            } else {
                tmp = (sat_add_s16(round +
                        (((int16_t)x1 * (int16_t)y1) << 1),
                        ((int16_t)x2 * (int16_t)y2) << 1) >> 8) & 0xff;
            }
        } else {
            tmp = (sat_add_s16(round,
                    (((int16_t)x1 * (int16_t)y1) << 1) +
                    (((int16_t)x2 * (int16_t)y2) << 1)) >> 8) & 0xff;
        }
    } else if ((x1 == (int8_t)(1 << 7) && y1 == (int8_t)(1 << 7)) &&
        (x2 == (int8_t)(1 << 7) && y2 == (int8_t)(1 << 7))) {
                tmp = 0x7f;
    } else if (x1 == (int8_t)(1 << 7) && y1 == (int8_t)(1 << 7)) {
        if (x2 == 0 || y2 == 0) {
            tmp = 0x7f;
        } else if ((x2 ^ y2) & SIGNBIT8) {
            tmp = (sat_add_s16(round,
                    (((int16_t)x1 * (int16_t)y1) << 1) +
                    (((int16_t)x2 * (int16_t)y2) << 1)) >> 8) & 0xff;
        } else {
            tmp = 0x7f;
        }
    } else {
        if (x1 == 0 || y1 == 0) {
            tmp = 0x7f;
        } else if ((x1 ^ y1) & SIGNBIT8) {
            tmp = (sat_add_s16(round,
                    (((int16_t)x1 * (int16_t)y1) << 1) +
                    (((int16_t)x2 * (int16_t)y2) << 1)) >> 8) & 0xff;
        } else {
            tmp = 0x7f;
        }
    }
    return tmp;
}
static int8_t sat_round_ash_s8(int8_t x1, int8_t y1,  int8_t x2,
        int8_t y2)
{
    int16_t round, result;
    int8_t tmp;
    bool sat = false;

    round = (int16_t)1 << 7;
    if (!(x1 == (int8_t)(1 << 7) && y1 == (int8_t)(1 << 7)) &&
        !(x2 == (int8_t)(1 << 7) && y2 == (int8_t)(1 << 7))) {
        result = sat_sub_s16((int16_t)x1 * (int16_t)y1 << 1,
                    (int16_t)x2 * (int16_t)y2 << 1);
        if (result == MAX_S16 || result == MIN_S16) {
            sat = true;
        }
        if (sat) {
            if (!(result & SIGNBIT16)) {
                tmp = (result >> 8) & 0xff;
            } else {
                tmp = (sat_sub_s16(round +
                        (((int16_t)x1 * (int16_t)y1) << 1),
                        ((int16_t)x2 * (int16_t)y2) << 1) >> 8) & 0xff;
            }
        } else {
            tmp = (sat_add_s16(round,
                    (((int16_t)x1 * (int16_t)y1) << 1) -
                    (((int16_t)x2 * (int16_t)y2) << 1)) >> 8) & 0xff;
        }
    } else if ((x1 == (int8_t)(1 << 7) && y1 == (int8_t)(1 << 7)) &&
        (x2 == (int8_t)(1 << 7) && y2 == (int8_t)(1 << 7))) {
        tmp = 0;
    } else if (x1 == (int8_t)(1 << 7) && y1 == (int8_t)(1 << 7)) {
        if (x2 == 0 || y2 == 0) {
            tmp = 0x7f;
        } else if (!((x2 ^ y2) & SIGNBIT8)) {
            tmp = (sat_add_s16(round,
                    ((int16_t)x1 * (int16_t)y1 << 1) -
                    ((int16_t)x2 * (int16_t)y2 << 1)
                    ) >> 8) & 0xff;
        } else {
            tmp = 0x7f;
        }
    } else {
        tmp = (sat_add_s16(round -
                (((int16_t)x2 * (int16_t)y2) << 1),
                (((int16_t)x1 * (int16_t)y1) << 1)) >> 8) & 0xff;
    }
    return tmp;
}
static int8_t sat_round_ssh_s8(int8_t x1, int8_t y1,  int8_t x2,
        int8_t y2)
{
    int16_t round, result;
    int8_t tmp;
    bool sat = false;

    round = (int16_t)1 << 7;
    if (!(x1 == (int8_t)(1 << 7) && y1 == (int8_t)(1 << 7)) &&
        !(x2 == (int8_t)(1 << 7) && y2 == (int8_t)(1 << 7))) {
        result = sat_add_s16((int16_t)x1 * (int16_t)y1 << 1,
                    (int16_t)x2 * (int16_t)y2 << 1);
        if (result == MAX_S16 || result == MIN_S16) {
            sat = true;
        }
        if (sat) {
            if (result & SIGNBIT16) {
                tmp = 0x7f;
            } else {
                tmp = (sat_sub_s16(round -
                        (((int16_t)x1 * (int16_t)y1) << 1),
                        ((int16_t)x2 * (int16_t)y2) << 1) >> 8) & 0xff;
            }
        } else {
            tmp = (sat_sub_s16(round,
                    (((int16_t)x1 * (int16_t)y1) << 1) +
                    (((int16_t)x2 * (int16_t)y2) << 1)) >> 8) & 0xff;
        }
    } else if ((x1 == (int8_t)(1 << 7) && y1 == (int8_t)(1 << 7)) &&
        (x2 == (int8_t)(1 << 7) && y2 == (int8_t)(1 << 7))) {
                tmp = 0x80;
    } else if (x1 == (int8_t)(1 << 7) && y1 == (int8_t)(1 << 7)) {
        tmp = (sat_sub_s16(round -
                (((int16_t)x1 * (int16_t)y1) << 1),
                (((int16_t)x2 * (int16_t)y2) << 1)) >> 8) & 0xff;
    } else {
        tmp = (sat_sub_s16(round -
                (((int16_t)x2 * (int16_t)y2) << 1),
                (((int16_t)x1 * (int16_t)y1) << 1)) >> 8) & 0xff;
    }
    return tmp;
}
static int16_t sat_round_aah_s16(int16_t x1, int16_t y1,  int16_t x2,
        int16_t y2)
{
    int32_t round, result;
    int16_t tmp;
    bool sat = false;

    round = (int32_t)1 << 15;
    if (!(x1 == (int16_t)(1 << 15) && y1 == (int16_t)(1 << 15)) &&
        !(x2 == (int16_t)(1 << 15) && y2 == (int16_t)(1 << 15))) {
        result = sat_add_s32((int32_t)x1 * (int32_t)y1 << 1,
                    (int32_t)x2 * (int32_t)y2 << 1);
        if (result == MAX_S32 || result == MIN_S32) {
            sat = true;
        }
        if (sat) {
            if (!(result & SIGNBIT32)) {
                tmp = (result >> 16) & 0xffff;
            } else {
                tmp = (sat_add_s32(round +
                        (((int32_t)x1 * (int32_t)y1) << 1),
                        ((int32_t)x2 * (int32_t)y2) << 1) >> 16) & 0xffff;
            }

        } else {
            tmp = (sat_add_s32(round,
                    (((int32_t)x1 * (int32_t)y1) << 1) +
                    (((int32_t)x2 * (int32_t)y2) << 1)) >> 16) & 0xffff;
        }
    } else if ((x1 == (int16_t)(1 << 15) && y1 == (int16_t)(1 << 15)) &&
        (x2 == (int16_t)(1 << 15) && y2 == (int16_t)(1 << 15))) {
                tmp = 0x7fff;
    } else if (x1 == (int16_t)(1 << 15) && y1 == (int16_t)(1 << 15)) {
        if (x2 == 0 || y2 == 0) {
            tmp = 0x7fff;
        } else if ((x2 ^ y2) & SIGNBIT16) {
            tmp = (sat_add_s32(round,
                    (((int32_t)x1 * (int32_t)y1) << 1) +
                    (((int32_t)x2 * (int32_t)y2) << 1)) >> 16) & 0xffff;
        } else {
            tmp = 0x7fff;
        }
    } else {
        if (x1 == 0 || y1 == 0) {
            tmp = 0x7fff;
        } else if ((x1 ^ y1) & SIGNBIT16) {
            tmp = (sat_add_s32(round,
                    (((int32_t)x1 * (int32_t)y1) << 1) +
                    (((int32_t)x2 * (int32_t)y2) << 1)) >> 16) & 0xffff;
        } else {
            tmp = 0x7fff;
        }
    }
    return tmp;
}
static int16_t sat_round_ash_s16(int16_t x1, int16_t y1,  int16_t x2,
        int16_t y2)
{
    int32_t round, result;
    int16_t tmp;
    bool sat = false;

    round = (int32_t)1 << 15;
    if (!(x1 == (int16_t)(1 << 15) && y1 == (int16_t)(1 << 15)) &&
        !(x2 == (int16_t)(1 << 15) && y2 == (int16_t)(1 << 15))) {
        result = sat_sub_s32((int32_t)x1 * (int32_t)y1 << 1,
                    (int32_t)x2 * (int32_t)y2 << 1);
        if (result == MAX_S32 || result == MIN_S32) {
            sat = true;
        }
        if (sat) {
            if (!(result & SIGNBIT32)) {
                tmp = (result >> 16) & 0xffff;
            } else {
                tmp = (sat_sub_s32(round +
                        (((int32_t)x1 * (int32_t)y1) << 1),
                        ((int32_t)x2 * (int32_t)y2) << 1) >> 16) & 0xffff;
            }

        } else {
            tmp = (sat_add_s32(round,
                    (((int32_t)x1 * (int32_t)y1) << 1) -
                    (((int32_t)x2 * (int32_t)y2) << 1)) >> 16) & 0xffff;
        }
    } else if ((x1 == (int16_t)(1 << 15) && y1 == (int16_t)(1 << 15)) &&
        (x2 == (int16_t)(1 << 15) && y2 == (int16_t)(1 << 15))) {
                tmp = 0;
    } else if (x1 == (int16_t)(1 << 15) && y1 == (int16_t)(1 << 15)) {
        if (x2 == 0 || y2 == 0) {
            tmp = 0x7fff;
        } else if (!((x2 ^ y2) & SIGNBIT16)) {
            tmp = (sat_add_s32(round,
                    (((int32_t)x1 * (int32_t)y1) << 1) -
                    (((int32_t)x2 * (int32_t)y2) << 1)) >> 16) & 0xffff;
        } else {
            tmp = 0x7fff;
        }
    } else {
        tmp = (sat_add_s32(round -
                (((int32_t)x2 * (int32_t)y2) << 1),
                (((int32_t)x1 * (int32_t)y1) << 1)) >> 16) & 0xffff;
    }
    return tmp;
}
static int16_t sat_round_ssh_s16(int16_t x1, int16_t y1,  int16_t x2,
        int16_t y2)
{
    int32_t round, result;
    int16_t tmp;
    bool sat = false;

    round = (int32_t)1 << 15;
    if (!(x1 == (int16_t)(1 << 15) && y1 == (int16_t)(1 << 15)) &&
        !(x2 == (int16_t)(1 << 15) && y2 == (int16_t)(1 << 15))) {
        result = sat_add_s32((int32_t)x1 * (int32_t)y1 << 1,
                    (int32_t)x2 * (int32_t)y2 << 1);
        if (result == MAX_S32 || result == MIN_S32) {
            sat = true;
        }
        if (sat) {
            if (result & SIGNBIT32) {
                tmp = 0x7fff;
            } else {
                tmp = (sat_sub_s32(round -
                        (((int32_t)x1 * (int32_t)y1) << 1),
                        ((int32_t)x2 * (int32_t)y2) << 1) >> 16) & 0xffff;
            }

        } else {
            tmp = (sat_sub_s32(round,
                    (((int32_t)x1 * (int32_t)y1) << 1) +
                    (((int32_t)x2 * (int32_t)y2) << 1)) >> 16) & 0xffff;
        }
    } else if ((x1 == (int16_t)(1 << 15) && y1 == (int16_t)(1 << 15)) &&
        (x2 == (int16_t)(1 << 15) && y2 == (int16_t)(1 << 15))) {
                tmp = 0x8000;
    } else if (x1 == (int16_t)(1 << 15) && y1 == (int16_t)(1 << 15)) {
        tmp = (sat_sub_s32(round -
                (((int32_t)x1 * (int32_t)y1) << 1),
                (((int32_t)x2 * (int32_t)y2) << 1)) >> 16) & 0xffff;
    } else {
        tmp = (sat_sub_s32(round -
                (((int32_t)x2 * (int32_t)y2) << 1),
                (((int32_t)x1 * (int32_t)y1) << 1)) >> 16) & 0xffff;
    }
    return tmp;
}
static int32_t sat_round_aah_s32(int32_t x1, int32_t y1,  int32_t x2,
        int32_t y2)
{
    int64_t round, result;
    int32_t tmp;
    bool sat = false;

    round = (int64_t)1 << 31;
    if (!(x1 == (int32_t)(1 << 31) && y1 == (int32_t)(1 << 31)) &&
        !(x2 == (int32_t)(1 << 31) && y2 == (int32_t)(1 << 31))) {
        result = sat_add_s64((int64_t)x1 * (int64_t)y1 << 1,
                    (int64_t)x2 * (int64_t)y2 << 1);
        if (result == MAX_S64 || result == MIN_S64) {
            sat = true;
        }
        if (sat) {
            if (!(result & SIGNBIT64)) {
                tmp = (result >> 32) & 0xffffffff;
            } else {
                tmp = (sat_add_s64(round +
                        (((int64_t)x1 * (int64_t)y1) << 1),
                        ((int64_t)x2 * (int64_t)y2) << 1) >> 32) & 0xffffffff;
            }
        } else {
            tmp = (sat_add_s64(round,
                    (((int64_t)x1 * (int64_t)y1) << 1) +
                    (((int64_t)x2 * (int64_t)y2) << 1)) >> 32) & 0xffffffff;
        }
    } else if ((x1 == (int32_t)(1 << 31) && y1 == (int32_t)(1 << 31)) &&
        (x2 == (int32_t)(1 << 31) && y2 == (int32_t)(1 << 31))) {
                tmp = 0x7fffffff;
    } else if (x1 == (int32_t)(1 << 31) && y1 == (int32_t)(1 << 31)) {
        if (x2 == 0 || y2 == 0) {
            tmp = 0x7fffffff;
        } else if ((x2 ^ y2) & SIGNBIT32) {
            tmp = (sat_add_s64(round,
                    (((int64_t)x1 * (int64_t)y1) << 1) +
                    (((int64_t)x2 * (int64_t)y2) << 1)) >> 32) & 0xffffffff;
        } else {
            tmp = 0x7fffffff;
        }
    } else {
        if (x1 == 0 || y1 == 0) {
            tmp = 0x7fffffff;
        } else if ((x1 ^ y1) & SIGNBIT32) {
            tmp = (sat_add_s64(round,
                    (((int64_t)x1 * (int64_t)y1) << 1) +
                    (((int64_t)x2 * (int64_t)y2) << 1)) >> 32) & 0xffffffff;
        } else {
            tmp = 0x7fffffff;
        }
    }
    return tmp;
}
static int32_t sat_round_ash_s32(int32_t x1, int32_t y1,  int32_t x2,
        int32_t y2)
{
    int64_t round, result;
    int32_t tmp;
    bool sat = false;

    round = (int64_t)1 << 31;
    if (!(x1 == (int32_t)(1 << 31) && y1 == (int32_t)(1 << 31)) &&
        !(x2 == (int32_t)(1 << 31) && y2 == (int32_t)(1 << 31))) {
        result = sat_sub_s64((int64_t)x1 * (int64_t)y1 << 1,
                    (int64_t)x2 * (int64_t)y2 << 1);
        if (result == MAX_S64 || result == MIN_S64) {
            sat = true;
        }
        if (sat) {
            if (!(result & SIGNBIT64)) {
                tmp = (result >> 32) & 0xffffffff;
            } else {
                tmp = (sat_sub_s64(round +
                        (((int64_t)x1 * (int64_t)y1) << 1),
                        ((int64_t)x2 * (int64_t)y2) << 1) >> 32) & 0xffffffff;
            }

        } else {
            tmp = (sat_add_s64(round,
                    (((int64_t)x1 * (int64_t)y1) << 1) -
                    (((int64_t)x2 * (int64_t)y2) << 1)) >> 32) & 0xffffffff;
        }
    } else if ((x1 == (int32_t)(1 << 31) && y1 == (int32_t)(1 << 31)) &&
        (x2 == (int32_t)(1 << 31) && y2 == (int32_t)(1 << 31))) {
                tmp = 0;
    } else if (x1 == (int32_t)(1 << 31) && y1 == (int32_t)(1 << 31)) {
        if (x2 == 0 || y2 == 0) {
            tmp = 0x7fffffff;
        } else if (!((x2 ^ y2) & SIGNBIT32)) {
            tmp = (sat_add_s64(round,
                    (((int64_t)x1 * (int64_t)y1) << 1) -
                    (((int64_t)x2 * (int64_t)y2) << 1)) >> 32) & 0xffffffff;
        } else {
            tmp = 0x7fffffff;
        }
    } else {
        tmp = (sat_add_s64(round -
                (((int64_t)x2 * (int64_t)y2) << 1),
                (((int64_t)x1 * (int64_t)y1) << 1)) >> 32) & 0xffffffff;
    }
    return tmp;
}
static int32_t sat_round_ssh_s32(int32_t x1, int32_t y1,  int32_t x2,
        int32_t y2)
{
    int64_t round, result;
    int32_t tmp;
    bool sat = false;

    round = (int64_t)1 << 31;
    if (!(x1 == (int32_t)(1 << 31) && y1 == (int32_t)(1 << 31)) &&
        !(x2 == (int32_t)(1 << 31) && y2 == (int32_t)(1 << 31))) {
        result = sat_add_s64((int64_t)x1 * (int64_t)y1 << 1,
                    (int64_t)x2 * (int64_t)y2 << 1);
        if (result == MAX_S64 || result == MIN_S64) {
            sat = true;
        }
        if (sat) {
            if (result & SIGNBIT64) {
                tmp = 0x7fffffff;
            } else {
                tmp = (sat_sub_s64(round -
                        (((int64_t)x1 * (int64_t)y1) << 1),
                        ((int64_t)x2 * (int64_t)y2) << 1) >> 32) & 0xffffffff;
            }

        } else {
            tmp = (sat_sub_s64(round,
                    (((int64_t)x1 * (int64_t)y1) << 1) +
                    (((int64_t)x2 * (int64_t)y2) << 1)) >> 32) & 0xffffffff;
        }
    } else if ((x1 == (int32_t)(1 << 31) && y1 == (int32_t)(1 << 31)) &&
        (x2 == (int32_t)(1 << 31) && y2 == (int32_t)(1 << 31))) {
                tmp = 0x80000000;
    } else if (x1 == (int32_t)(1 << 31) && y1 == (int32_t)(1 << 31)) {
        tmp = (sat_sub_s64(round -
                (((int64_t)x1 * (int64_t)y1) << 1),
                (((int64_t)x2 * (int64_t)y2) << 1)) >> 32) & 0xffffffff;
    } else {
        tmp = (sat_sub_s64(round -
                (((int64_t)x2 * (int64_t)y2) << 1),
                (((int64_t)x1 * (int64_t)y1) << 1)) >> 32) & 0xffffffff;
    }
    return tmp;
}
void VDSP2_HELPER(vmul)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] *
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] *
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] *
                                            env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] *
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] *
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] *
                                             env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP2_HELPER(vmuli)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, sign, rx, ry, rz, index = 0, imm5;
    union VDSP tmp;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dspc[i] = (env->vfp.reg[rx].dspc[i] *
                                            env->vfp.reg[ry].dspc[index]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] = (env->vfp.reg[rx].dsps[i] *
                                            env->vfp.reg[ry].dsps[index]);
            }

            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] = (env->vfp.reg[rx].dspi[i] *
                                            env->vfp.reg[ry].dspi[index]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udspc[i] = (env->vfp.reg[rx].udspc[i] *
                                             env->vfp.reg[ry].udspc[index]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] = (env->vfp.reg[rx].udsps[i] *
                                             env->vfp.reg[ry].udsps[index]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] = (env->vfp.reg[rx].udspi[i] *
                                             env->vfp.reg[ry].udspi[index]);
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vmulh)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = ((
                        (int16_t)env->vfp.reg[rx].dspc[i] *
                        (int16_t)env->vfp.reg[ry].dspc[i]
                    ) >> 8) & 0xff;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = ((
                        (int32_t)env->vfp.reg[rx].dsps[i] *
                        (int32_t)env->vfp.reg[ry].dsps[i]
                    ) >> 16) & 0xffff;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = ((
                        (int64_t)env->vfp.reg[rx].dspi[i] *
                        (int64_t)env->vfp.reg[ry].dspi[i]
                    ) >> 32) & 0xffffffff;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = ((
                        (uint16_t)env->vfp.reg[rx].udspc[i] *
                        (uint16_t)env->vfp.reg[ry].udspc[i]
                    ) >> 8) & 0xff;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = ((
                        (uint32_t)env->vfp.reg[rx].udsps[i] *
                        (uint32_t)env->vfp.reg[ry].udsps[i]
                    ) >> 16) & 0xffff;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = ((
                        (uint64_t)env->vfp.reg[rx].udspi[i] *
                        (uint64_t)env->vfp.reg[ry].udspi[i]
                    ) >> 32) & 0xffffffff;
            }
            break;
        }
    }
}

void VDSP2_HELPER(vmulih)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, sign, rx, ry, rz, index = 0, imm5;
    union VDSP tmp;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dspc[i] = ((
                        (int16_t)env->vfp.reg[rx].dspc[i] *
                        (int16_t)env->vfp.reg[ry].dspc[index]
                    ) >> 8) & 0xff;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] = ((
                        (int32_t)env->vfp.reg[rx].dsps[i] *
                        (int32_t)env->vfp.reg[ry].dsps[index]
                    ) >> 16) & 0xffff;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] = ((
                        (int64_t)env->vfp.reg[rx].dspi[i] *
                        (int64_t)env->vfp.reg[ry].dspi[index]
                    ) >> 32) & 0xffffffff;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udspc[i] = ((
                        (uint16_t)env->vfp.reg[rx].udspc[i] *
                        (uint16_t)env->vfp.reg[ry].udspc[index]
                    ) >> 8) & 0xff;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] = ((
                        (uint32_t)env->vfp.reg[rx].udsps[i] *
                        (uint32_t)env->vfp.reg[ry].udsps[index]
                    ) >> 16) & 0xffff;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] = ((
                        (uint64_t)env->vfp.reg[rx].udspi[i] *
                        (uint64_t)env->vfp.reg[ry].udspi[index]
                    ) >> 32) & 0xffffffff;
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vmule)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;
    union VDSP tmp, tmp1;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] = (
                    (int16_t)env->vfp.reg[rx].dspc[i] *
                    (int16_t)env->vfp.reg[ry].dspc[i]
                );
                tmp1.dsps[i] = (
                    (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                    (int16_t)env->vfp.reg[ry].dspc[cnt + i]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] = (
                    (int32_t)env->vfp.reg[rx].dsps[i] *
                    (int32_t)env->vfp.reg[ry].dsps[i]
                );
                tmp1.dspi[i] = (
                    (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                    (int32_t)env->vfp.reg[ry].dsps[cnt + i]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspl[i] = (
                    (int64_t)env->vfp.reg[rx].dspi[i] *
                    (int64_t)env->vfp.reg[ry].dspi[i]
                );
                tmp1.dspl[i] = (
                    (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                    (int64_t)env->vfp.reg[ry].dspi[cnt + i]
                );
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] = (
                    (uint16_t)env->vfp.reg[rx].udspc[i] *
                    (uint16_t)env->vfp.reg[ry].udspc[i]
                );
                tmp1.udsps[i] = (
                    (uint16_t)env->vfp.reg[rx].udspc[cnt + i] *
                    (uint16_t)env->vfp.reg[ry].udspc[cnt + i]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] = (
                    (uint32_t)env->vfp.reg[rx].udsps[i] *
                    (uint32_t)env->vfp.reg[ry].udsps[i]
                );
                tmp1.udspi[i] = (
                    (uint32_t)env->vfp.reg[rx].udsps[cnt + i] *
                    (uint32_t)env->vfp.reg[ry].udsps[cnt + i]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspl[i] = (
                    (uint64_t)env->vfp.reg[rx].udspi[i] *
                    (uint64_t)env->vfp.reg[ry].udspi[i]
                );
                tmp1.udspl[i] = (
                    (uint64_t)env->vfp.reg[rx].udspi[cnt + i] *
                    (uint64_t)env->vfp.reg[ry].udspi[cnt + i]
                );
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}

void VDSP2_HELPER(vmulie)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, sign, rx, ry, rz, index = 0, imm5;
    union VDSP tmp, tmp1;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] = (
                    (int16_t)env->vfp.reg[rx].dspc[i] *
                    (int16_t)env->vfp.reg[ry].dspc[index]
                );
                tmp1.dsps[i] = (
                    (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                    (int16_t)env->vfp.reg[ry].dspc[index]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] = (
                    (int32_t)env->vfp.reg[rx].dsps[i] *
                    (int32_t)env->vfp.reg[ry].dsps[index]
                );
                tmp1.dspi[i] = (
                    (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                    (int32_t)env->vfp.reg[ry].dsps[index]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspl[i] = (
                    (int64_t)env->vfp.reg[rx].dspi[i] *
                    (int64_t)env->vfp.reg[ry].dspi[index]
                );
                tmp1.dspl[i] = (
                    (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                    (int64_t)env->vfp.reg[ry].dspi[index]
                );
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] = (
                    (uint16_t)env->vfp.reg[rx].udspc[i] *
                    (uint16_t)env->vfp.reg[ry].udspc[index]
                );
                tmp1.udsps[i] = (
                    (uint16_t)env->vfp.reg[rx].udspc[cnt + i] *
                    (uint16_t)env->vfp.reg[ry].udspc[index]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] = (
                    (uint32_t)env->vfp.reg[rx].udsps[i] *
                    (uint32_t)env->vfp.reg[ry].udsps[index]
                );
                tmp1.udspi[i] = (
                    (uint32_t)env->vfp.reg[rx].udsps[cnt + i] *
                    (uint32_t)env->vfp.reg[ry].udsps[index]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspl[i] = (
                    (uint64_t)env->vfp.reg[rx].udspi[i] *
                    (uint64_t)env->vfp.reg[ry].udspi[index]
                );
                tmp1.udspl[i] = (
                    (uint64_t)env->vfp.reg[rx].udspi[cnt + i] *
                    (uint64_t)env->vfp.reg[ry].udspi[index]
                );
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}
void VDSP2_HELPER(vmula)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] += (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] += (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += (env->vfp.reg[rx].dspi[i] *
                                             env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] += (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] += (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += (env->vfp.reg[rx].udspi[i] *
                                              env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP2_HELPER(vmulai)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, sign, rx, ry, rz, index = 0, imm5;
    union VDSP tmp;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    memcpy(&tmp, &env->vfp.reg[rz], sizeof(union VDSP));
    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dspc[i] += (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[index]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] += (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[index]);
            }

            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] += (env->vfp.reg[rx].dspi[i] *
                                             env->vfp.reg[ry].dspi[index]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udspc[i] += (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[index]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] += (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[index]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] += (env->vfp.reg[rx].udspi[i] *
                                              env->vfp.reg[ry].udspi[index]);
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vmulae)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;
    union VDSP tmp, tmp1;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] = (
                    env->vfp.reg[rz].dsps[i] +
                    (int16_t)env->vfp.reg[rx].dspc[i] *
                    (int16_t)env->vfp.reg[ry].dspc[i]
                );
                tmp1.dsps[i] = (
                    env->vfp.reg[rz].dsps[cnt + i] +
                    (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                    (int16_t)env->vfp.reg[ry].dspc[cnt + i]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] = (
                    env->vfp.reg[rz].dspi[i] +
                    (int32_t)env->vfp.reg[rx].dsps[i] *
                    (int32_t)env->vfp.reg[ry].dsps[i]
                );
                tmp1.dspi[i] = (
                    env->vfp.reg[rz].dspi[cnt + i] +
                    (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                    (int32_t)env->vfp.reg[ry].dsps[cnt + i]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspl[i] = (
                    env->vfp.reg[rz].dspl[i] +
                    (int64_t)env->vfp.reg[rx].dspi[i] *
                    (int64_t)env->vfp.reg[ry].dspi[i]
                );
                tmp1.dspl[i] = (
                    env->vfp.reg[rz].dspl[cnt + i] +
                    (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                    (int64_t)env->vfp.reg[ry].dspi[cnt + i]
                );
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] = (
                    env->vfp.reg[rz].udsps[i] +
                    (uint16_t)env->vfp.reg[rx].udspc[i] *
                    (uint16_t)env->vfp.reg[ry].udspc[i]
                );
                tmp1.udsps[i] = (
                    env->vfp.reg[rz].udsps[cnt + i] +
                    (uint16_t)env->vfp.reg[rx].udspc[cnt + i] *
                    (uint16_t)env->vfp.reg[ry].udspc[cnt + i]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] = (
                    env->vfp.reg[rz].udspi[i] +
                    (uint32_t)env->vfp.reg[rx].udsps[i] *
                    (uint32_t)env->vfp.reg[ry].udsps[i]
                );
                tmp1.udspi[i] = (
                    env->vfp.reg[rz].udspi[cnt + i] +
                    (uint32_t)env->vfp.reg[rx].udsps[cnt + i] *
                    (uint32_t)env->vfp.reg[ry].udsps[cnt + i]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspl[i] = (
                    env->vfp.reg[rz].udspl[i] +
                    (uint64_t)env->vfp.reg[rx].udspi[i] *
                    (uint64_t)env->vfp.reg[ry].udspi[i]
                );
                tmp1.udspl[i] = (
                    env->vfp.reg[rz].udspl[cnt + i] +
                    (uint64_t)env->vfp.reg[rx].udspi[cnt + i] *
                    (uint64_t)env->vfp.reg[ry].udspi[cnt + i]
                );
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}
void VDSP2_HELPER(vmulaie)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, sign, rx, ry, rz, index = 0, imm5;
    union VDSP tmp, tmp1;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    memcpy(&tmp, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp1, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] += (
                    (int16_t)env->vfp.reg[rx].dspc[i] *
                    (int16_t)env->vfp.reg[ry].dspc[index]
                );
                tmp1.dsps[i] += (
                    (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                    (int16_t)env->vfp.reg[ry].dspc[index]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] += (
                    (int32_t)env->vfp.reg[rx].dsps[i] *
                    (int32_t)env->vfp.reg[ry].dsps[index]
                );
                tmp1.dspi[i] += (
                    (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                    (int32_t)env->vfp.reg[ry].dsps[index]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspl[i] += (
                    (int64_t)env->vfp.reg[rx].dspi[i] *
                    (int64_t)env->vfp.reg[ry].dspi[index]
                );
                tmp1.dspl[i] += (
                    (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                    (int64_t)env->vfp.reg[ry].dspi[index]
                );
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] += (
                    (uint16_t)env->vfp.reg[rx].udspc[i] *
                    (uint16_t)env->vfp.reg[ry].udspc[index]
                );
                tmp1.udsps[i] += (
                    (uint16_t)env->vfp.reg[rx].udspc[cnt + i] *
                    (uint16_t)env->vfp.reg[ry].udspc[index]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] += (
                    (uint32_t)env->vfp.reg[rx].udsps[i] *
                    (uint32_t)env->vfp.reg[ry].udsps[index]
                );
                tmp1.udspi[i] += (
                    (uint32_t)env->vfp.reg[rx].udsps[cnt + i] *
                    (uint32_t)env->vfp.reg[ry].udsps[index]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspl[i] += (
                    (uint64_t)env->vfp.reg[rx].udspi[i] *
                    (uint64_t)env->vfp.reg[ry].udspi[index]
                );
                tmp1.udspl[i] += (
                    (uint64_t)env->vfp.reg[rx].udspi[cnt + i] *
                    (uint64_t)env->vfp.reg[ry].udspi[index]
                );
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}
void VDSP2_HELPER(vmuls)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] -= (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] -= (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] -= (env->vfp.reg[rx].dspi[i] *
                                             env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] -= (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] -= (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] -= (env->vfp.reg[rx].udspi[i] *
                                              env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}
void VDSP2_HELPER(vmulsi)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, sign, rx, ry, rz, index = 0, imm5;
    union VDSP tmp;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    memcpy(&tmp, &env->vfp.reg[rz], sizeof(union VDSP));

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dspc[i] -= (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[index]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] -= (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[index]);
            }

            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] -= (env->vfp.reg[rx].dspi[i] *
                                             env->vfp.reg[ry].dspi[index]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udspc[i] -= (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[index]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] -= (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[index]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] -= (env->vfp.reg[rx].udspi[i] *
                                              env->vfp.reg[ry].udspi[index]);
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vmulse)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;
    union VDSP tmp, tmp1;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    memcpy(&tmp, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp1, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] -= (
                    (int16_t)env->vfp.reg[rx].dspc[i] *
                    (int16_t)env->vfp.reg[ry].dspc[i]
                );
                tmp1.dsps[i] -= (
                    (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                    (int16_t)env->vfp.reg[ry].dspc[cnt + i]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] -= (
                    (int32_t)env->vfp.reg[rx].dsps[i] *
                    (int32_t)env->vfp.reg[ry].dsps[i]
                );
                tmp1.dspi[i] -= (
                    (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                    (int32_t)env->vfp.reg[ry].dsps[cnt + i]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspl[i] -= (
                    (int64_t)env->vfp.reg[rx].dspi[i] *
                    (int64_t)env->vfp.reg[ry].dspi[i]
                );
                tmp1.dspl[i] -= (
                    (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                    (int64_t)env->vfp.reg[ry].dspi[cnt + i]
                );
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] -= (
                    (uint16_t)env->vfp.reg[rx].udspc[i] *
                    (uint16_t)env->vfp.reg[ry].udspc[i]
                );
                tmp1.udsps[i] -= (
                    (uint16_t)env->vfp.reg[rx].udspc[cnt + i] *
                    (uint16_t)env->vfp.reg[ry].udspc[cnt + i]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] -= (
                    (uint32_t)env->vfp.reg[rx].udsps[i] *
                    (uint32_t)env->vfp.reg[ry].udsps[i]
                );
                tmp1.udspi[i] -= (
                    (uint32_t)env->vfp.reg[rx].udsps[cnt + i] *
                    (uint32_t)env->vfp.reg[ry].udsps[cnt + i]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspl[i] -= (
                    (uint64_t)env->vfp.reg[rx].udspi[i] *
                    (uint64_t)env->vfp.reg[ry].udspi[i]
                );
                tmp1.udspl[i] -= (
                    (uint64_t)env->vfp.reg[rx].udspi[cnt + i] *
                    (uint64_t)env->vfp.reg[ry].udspi[cnt + i]
                );
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}
void VDSP2_HELPER(vmulsie)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, sign, rx, ry, rz, index = 0, imm5;
    union VDSP tmp, tmp1;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    memcpy(&tmp, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp1, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dsps[i] -= (
                    (int16_t)env->vfp.reg[rx].dspc[i] *
                    (int16_t)env->vfp.reg[ry].dspc[index]
                );
                tmp1.dsps[i] -= (
                    (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                    (int16_t)env->vfp.reg[ry].dspc[index]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] -= (
                    (int32_t)env->vfp.reg[rx].dsps[i] *
                    (int32_t)env->vfp.reg[ry].dsps[index]
                );
                tmp1.dspi[i] -= (
                    (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                    (int32_t)env->vfp.reg[ry].dsps[index]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.dspl[i] -= (
                    (int64_t)env->vfp.reg[rx].dspi[i] *
                    (int64_t)env->vfp.reg[ry].dspi[index]
                );
                tmp1.dspl[i] -= (
                    (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                    (int64_t)env->vfp.reg[ry].dspi[index]
                );
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udsps[i] -= (
                    (uint16_t)env->vfp.reg[rx].udspc[i] *
                    (uint16_t)env->vfp.reg[ry].udspc[index]
                );
                tmp1.udsps[i] -= (
                    (uint16_t)env->vfp.reg[rx].udspc[cnt + i] *
                    (uint16_t)env->vfp.reg[ry].udspc[index]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] -= (
                    (uint32_t)env->vfp.reg[rx].udsps[i] *
                    (uint32_t)env->vfp.reg[ry].udsps[index]
                );
                tmp1.udspi[i] -= (
                    (uint32_t)env->vfp.reg[rx].udsps[cnt + i] *
                    (uint32_t)env->vfp.reg[ry].udsps[index]
                );
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp.udspl[i] -= (
                    (uint64_t)env->vfp.reg[rx].udspi[i] *
                    (uint64_t)env->vfp.reg[ry].udspi[index]
                );
                tmp1.udspl[i] -= (
                    (uint64_t)env->vfp.reg[rx].udspi[cnt + i] *
                    (uint64_t)env->vfp.reg[ry].udspi[index]
                );
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}

void VDSP2_HELPER(vmulaca)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 4;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (
                    (int32_t)env->vfp.reg[rx].dspc[4 * i] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * i] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 1] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * i + 1] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 2] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * i + 2] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 3] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * i + 3]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspl[i] = (
                    (int64_t)env->vfp.reg[rx].dsps[4 * i] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * i] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 1] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * i + 1] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 2] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * i + 2] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 3] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * i + 3]
                );
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * i] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 1] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * i + 1] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 2] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * i + 2] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 3] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * i + 3]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspl[i] = (
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * i] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 1] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * i + 1] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 2] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * i + 2] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 3] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * i + 3]
                );
            }
            break;
        }
    }
}

void VDSP2_HELPER(vmulacaa)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 4;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += (
                    (int32_t)env->vfp.reg[rx].dspc[4 * i] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * i] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 1] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * i + 1] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 2] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * i + 2] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 3] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * i + 3]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspl[i] += (
                    (int64_t)env->vfp.reg[rx].dsps[4 * i] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * i] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 1] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * i + 1] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 2] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * i + 2] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 3] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * i + 3]
                );
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += (
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * i] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 1] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * i + 1] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 2] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * i + 2] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 3] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * i + 3]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspl[i] += (
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * i] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 1] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * i + 1] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 2] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * i + 2] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 3] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * i + 3]
                );
            }
            break;
        }
    }
}

void VDSP2_HELPER(vmulacai)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng, sign, rx, ry, rz, index;
    union VDSP tmp;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (((insn >> 25) & 0x1) == 0 && ((insn >> 20) & 0x1) == 1) {
        lng = 16;
        index = (insn >> 7) & 0x1;
    } else if (((insn >> 25) & 0x1) == 0 && ((insn >> 20) & 0x1) == 0) {
        lng = 8;
        index = (insn >> 7) & 0x1;
    } else if (((insn >> 25) & 0x1) == 1 && ((insn >> 20) & 0x1) == 1) {
        lng = 8;
        index = ((insn >> 7) & 0x1) + 2;
    } else {
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    cnt = 128 / lng;
    cnt = cnt / 4;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] = (
                    (int32_t)env->vfp.reg[rx].dspc[4 * i] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * index] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 1] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * index + 1] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 2] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * index + 2] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 3] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * index + 3]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dspl[i] = (
                    (int64_t)env->vfp.reg[rx].dsps[4 * i] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * index] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 1] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * index + 1] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 2] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * index + 2] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 3] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * index + 3]
                );
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] = (
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * index] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 1] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * index + 1] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 2] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * index + 2] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 3] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * index + 3]
                );
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udspl[i] = (
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * index] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 1] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * index + 1] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 2] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * index + 2] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 3] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * index + 3]
                );
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vmulacaai)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng, sign, rx, ry, rz, index;
    union VDSP tmp;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (((insn >> 25) & 0x1) == 0 && ((insn >> 20) & 0x1) == 1) {
        lng = 16;
        index = (insn >> 7) & 0x1;
    } else if (((insn >> 25) & 0x1) == 0 && ((insn >> 20) & 0x1) == 0) {
        lng = 8;
        index = (insn >> 7) & 0x1;
    } else if (((insn >> 25) & 0x1) == 1 && ((insn >> 20) & 0x1) == 1) {
        lng = 8;
        index = ((insn >> 7) & 0x1) + 2;
    } else {
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    cnt = 128 / lng;
    cnt = cnt / 4;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.dspi[i] = (
                    (int32_t)env->vfp.reg[rx].dspc[4 * i] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * index] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 1] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * index + 1] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 2] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * index + 2] +
                    (int32_t)env->vfp.reg[rx].dspc[4 * i + 3] *
                    (int32_t)env->vfp.reg[ry].dspc[4 * index + 3]
                );
                tmp.dspi[i] += env->vfp.reg[rz].dspi[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.dspl[i] = (
                    (int64_t)env->vfp.reg[rx].dsps[4 * i] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * index] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 1] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * index + 1] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 2] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * index + 2] +
                    (int64_t)env->vfp.reg[rx].dsps[4 * i + 3] *
                    (int64_t)env->vfp.reg[ry].dsps[4 * index + 3]
                );
                tmp.dspl[i] += env->vfp.reg[rz].dspl[i];
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp.udspi[i] = (
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * index] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 1] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * index + 1] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 2] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * index + 2] +
                    (uint32_t)env->vfp.reg[rx].udspc[4 * i + 3] *
                    (uint32_t)env->vfp.reg[ry].udspc[4 * index + 3]
                );
                tmp.udspi[i] += env->vfp.reg[rz].udspi[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp.udspl[i] = (
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * index] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 1] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * index + 1] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 2] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * index + 2] +
                    (uint64_t)env->vfp.reg[rx].udsps[4 * i + 3] *
                    (uint64_t)env->vfp.reg[ry].udsps[4 * index + 3]
                );
                tmp.udspl[i] += env->vfp.reg[rz].udspl[i];
            }
            break;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vrmulse)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;
    union VDSP tmp, tmp1;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspc[i] == (int8_t)(1 << 7) &&
                env->vfp.reg[ry].dspc[i] == (int8_t)(1 << 7)) {
                    tmp.dsps[i] = MAX_S16;
            } else {
                tmp.dsps[i] = ((
                    (int16_t)env->vfp.reg[rx].dspc[i] *
                    (int16_t)env->vfp.reg[ry].dspc[i]
                ) << 1);
            }
            if (env->vfp.reg[rx].dspc[cnt + i] == (int8_t)(1 << 7) &&
                env->vfp.reg[ry].dspc[cnt + i] == (int8_t)(1 << 7)) {
                    tmp1.dsps[i] = MAX_S16;
            } else {
                tmp1.dsps[i] = ((
                    (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                    (int16_t)env->vfp.reg[ry].dspc[cnt + i]
                ) << 1);
            }
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dsps[i] == (int16_t)(1 << 15) &&
                env->vfp.reg[ry].dsps[i] == (int16_t)(1 << 15)) {
                    tmp.dspi[i] = MAX_S32;
            } else {
                tmp.dspi[i] = ((
                    (int32_t)env->vfp.reg[rx].dsps[i] *
                    (int32_t)env->vfp.reg[ry].dsps[i]
                ) << 1);
            }
            if (env->vfp.reg[rx].dsps[cnt + i] == (int16_t)(1 << 15) &&
                env->vfp.reg[ry].dsps[cnt + i] == (int16_t)(1 << 15)) {
                    tmp1.dspi[i] = MAX_S32;
            } else {
                tmp1.dspi[i] = ((
                    (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                    (int32_t)env->vfp.reg[ry].dsps[cnt + i]
                ) << 1);
            }
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspi[i] == (int32_t)(1 << 31) &&
                env->vfp.reg[ry].dspi[i] == (int32_t)(1 << 31)) {
                    tmp.dspl[i] = MAX_S64;
            } else {
                tmp.dspl[i] = ((
                    (int64_t)env->vfp.reg[rx].dspi[i] *
                    (int64_t)env->vfp.reg[ry].dspi[i]
                ) << 1);
            }
            if (env->vfp.reg[rx].dspi[cnt + i] == (int32_t)(1 << 31) &&
                env->vfp.reg[ry].dspi[cnt + i] == (int32_t)(1 << 31)) {
                    tmp1.dspl[i] = MAX_S64;
            } else {
                tmp1.dspl[i] = ((
                    (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                    (int64_t)env->vfp.reg[ry].dspi[cnt + i]
                ) << 1);
            }
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulise)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, ry, rz, index = 0, imm5;
    union VDSP tmp, tmp1;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspc[i] == (int8_t)(1 << 7) &&
                env->vfp.reg[ry].dspc[index] == (int8_t)(1 << 7)) {
                    tmp.dsps[i] = MAX_S16;
            } else {
                tmp.dsps[i] = ((
                    (int16_t)env->vfp.reg[rx].dspc[i] *
                    (int16_t)env->vfp.reg[ry].dspc[index]
                ) << 1);
            }
            if (env->vfp.reg[rx].dspc[cnt + i] == (int8_t)(1 << 7) &&
                env->vfp.reg[ry].dspc[index] == (int8_t)(1 << 7)) {
                    tmp1.dsps[i] = MAX_S16;
            } else {
                tmp1.dsps[i] = ((
                    (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                    (int16_t)env->vfp.reg[ry].dspc[index]
                ) << 1);
            }
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dsps[i] == (int16_t)(1 << 15) &&
                env->vfp.reg[ry].dsps[index] == (int16_t)(1 << 15)) {
                    tmp.dspi[i] = MAX_S32;
            } else {
                tmp.dspi[i] = ((
                    (int32_t)env->vfp.reg[rx].dsps[i] *
                    (int32_t)env->vfp.reg[ry].dsps[index]
                ) << 1);
            }
            if (env->vfp.reg[rx].dsps[cnt + i] == (int16_t)(1 << 15) &&
                env->vfp.reg[ry].dsps[index] == (int16_t)(1 << 15)) {
                    tmp1.dspi[i] = MAX_S32;
            } else {
                tmp1.dspi[i] = ((
                    (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                    (int32_t)env->vfp.reg[ry].dsps[index]
                ) << 1);
            }
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspi[i] == (int32_t)(1 << 31) &&
                env->vfp.reg[ry].dspi[index] == (int32_t)(1 << 31)) {
                    tmp.dspl[i] = MAX_S64;
            } else {
                tmp.dspl[i] = ((
                    (int64_t)env->vfp.reg[rx].dspi[i] *
                    (int64_t)env->vfp.reg[ry].dspi[index]
                ) << 1);
            }
            if (env->vfp.reg[rx].dspi[cnt + i] == (int32_t)(1 << 31) &&
                env->vfp.reg[ry].dspi[index] == (int32_t)(1 << 31)) {
                    tmp1.dspl[i] = MAX_S64;
            } else {
                tmp1.dspl[i] = ((
                    (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                    (int64_t)env->vfp.reg[ry].dspi[index]
                ) << 1);
            }
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulhs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspc[i] == (int8_t)(1 << 7) &&
                env->vfp.reg[ry].dspc[i] == (int8_t)(1 << 7)) {
                    env->vfp.reg[rz].dspc[i] = MAX_S8;
            } else {
                env->vfp.reg[rz].dspc[i] = ((
                        (int16_t)env->vfp.reg[rx].dspc[i] *
                        (int16_t)env->vfp.reg[ry].dspc[i]
                    ) >> 7) & 0xff;
            }
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dsps[i] == (int16_t)(1 << 15) &&
                env->vfp.reg[ry].dsps[i] == (int16_t)(1 << 15)) {
                    env->vfp.reg[rx].dsps[i] = MAX_S16;
            } else {
                env->vfp.reg[rz].dsps[i] = ((
                        (int32_t)env->vfp.reg[rx].dsps[i] *
                        (int32_t)env->vfp.reg[ry].dsps[i]
                    ) >> 15) & 0xffff;
            }
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspi[i] == (int32_t)(1 << 31) &&
                env->vfp.reg[ry].dspi[i] == (int32_t)(1 << 31)) {
                    env->vfp.reg[rz].dspi[i] = MAX_S32;
            } else {
                    env->vfp.reg[rz].dspi[i] = ((
                            (int64_t)env->vfp.reg[rx].dspi[i] *
                            (int64_t)env->vfp.reg[ry].dspi[i]
                        ) >> 31) & 0xffffffff;
            }
        }
        break;
    }
}
void VDSP2_HELPER(vrmulhis)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, ry, rz, index = 0, imm5;
    union VDSP tmp;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspc[i] == (int8_t)(1 << 7) &&
                env->vfp.reg[ry].dspc[index] == (int8_t)(1 << 7)) {
                    tmp.dspc[i] = MAX_S8;
            } else {
                tmp.dspc[i] = ((
                        (int16_t)env->vfp.reg[rx].dspc[i] *
                        (int16_t)env->vfp.reg[ry].dspc[index]
                    ) >> 7) & 0xff;
            }
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dsps[i] == (int16_t)(1 << 15) &&
                env->vfp.reg[ry].dsps[index] == (int16_t)(1 << 15)) {
                    tmp.dsps[i] = MAX_S16;
            } else {
                tmp.dsps[i] = ((
                        (int32_t)env->vfp.reg[rx].dsps[i] *
                        (int32_t)env->vfp.reg[ry].dsps[index]
                    ) >> 15) & 0xffff;
            }
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspi[i] == (int32_t)(1 << 31) &&
                env->vfp.reg[ry].dspi[index] == (int32_t)(1 << 31)) {
                    tmp.dspi[i] = MAX_S32;
            } else {
                tmp.dspi[i] = ((
                        (int64_t)env->vfp.reg[rx].dspi[i] *
                        (int64_t)env->vfp.reg[ry].dspi[index]
                    ) >> 31) & 0xffffffff;
            }
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulhrs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspc[i] == (int8_t)(1 << 7) &&
                env->vfp.reg[ry].dspc[i] == (int8_t)(1 << 7)) {
                    env->vfp.reg[rz].dspc[i] = MAX_S8;
            } else {
                env->vfp.reg[rz].dspc[i] = ((((
                        (int16_t)env->vfp.reg[rx].dspc[i] *
                        (int16_t)env->vfp.reg[ry].dspc[i]
                    ) >> 6) + 1) >> 1) & 0xff;
            }
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dsps[i] == (int16_t)(1 << 15) &&
                env->vfp.reg[ry].dsps[i] == (int16_t)(1 << 15)) {
                    env->vfp.reg[rz].dsps[i] = MAX_S16;
            } else {
                env->vfp.reg[rz].dsps[i] = ((((
                        (int32_t)env->vfp.reg[rx].dsps[i] *
                        (int32_t)env->vfp.reg[ry].dsps[i]
                    ) >> 14) + 1) >> 1) & 0xffff;
            }
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspi[i] == (int32_t)(1 << 31) &&
                env->vfp.reg[ry].dspi[i] == (int32_t)(1 << 31)) {
                    env->vfp.reg[rz].dspi[i] = MAX_S32;
            } else {
                    env->vfp.reg[rz].dspi[i] = ((((
                            (int64_t)env->vfp.reg[rx].dspi[i] *
                            (int64_t)env->vfp.reg[ry].dspi[i]
                        ) >> 30) + 1) >> 1) & 0xffffffff;
            }
        }
        break;
    }
}
void VDSP2_HELPER(vrmulhirs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, ry, rz, index = 0, imm5;
    union VDSP tmp;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspc[i] == (int8_t)(1 << 7) &&
                env->vfp.reg[ry].dspc[index] == (int8_t)(1 << 7)) {
                tmp.dspc[i] = MAX_S8;
            } else {
                tmp.dspc[i] = ((((
                        (int16_t)env->vfp.reg[rx].dspc[i] *
                        (int16_t)env->vfp.reg[ry].dspc[index]
                    ) >> 6) + 1) >> 1) & 0xff;
            }
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dsps[i] == (int16_t)(1 << 15) &&
                env->vfp.reg[ry].dsps[index] == (int16_t)(1 << 15)) {
                tmp.dsps[i] = MAX_S16;
            } else {
                tmp.dsps[i] = ((((
                        (int32_t)env->vfp.reg[rx].dsps[i] *
                        (int32_t)env->vfp.reg[ry].dsps[index]
                    ) >> 14) + 1) >> 1) & 0xffff;
            }
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            if (env->vfp.reg[rx].dspi[i] == (int32_t)(1 << 31) &&
                env->vfp.reg[ry].dspi[index] == (int32_t)(1 << 31)) {
                tmp.dspi[i] = MAX_S32;
            } else {
                tmp.dspi[i] = ((((
                        (int64_t)env->vfp.reg[rx].dspi[i] *
                        (int64_t)env->vfp.reg[ry].dspi[index]
                    ) >> 30) + 1) >> 1) & 0xffffffff;
            }
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulhars)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[i],
                env->vfp.reg[ry].dspc[i],
                env->vfp.reg[rz].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[i],
                env->vfp.reg[ry].dsps[i],
                env->vfp.reg[rz].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[i],
                env->vfp.reg[ry].dspi[i],
                env->vfp.reg[rz].dspi[i]);
        }
        break;
    }
}
void VDSP2_HELPER(vrmulhairs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, ry, rz, index = 0, imm5;
    union VDSP tmp;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[i] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[i],
                env->vfp.reg[ry].dspc[index],
                env->vfp.reg[rz].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[i] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[i],
                env->vfp.reg[ry].dsps[index],
                env->vfp.reg[rz].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[i] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[i],
                env->vfp.reg[ry].dspi[index],
                env->vfp.reg[rz].dspi[i]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulhsrs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (sat_sub_s16(
                    ((int16_t)env->vfp.reg[rz].dspc[i] << 8),
                    ((((int16_t)env->vfp.reg[rx].dspc[i] *
                    (int16_t)env->vfp.reg[ry].dspc[i]) << 1) -
                    (int16_t)(1 << 7))
                ) >> 8) & 0xff;
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (sat_sub_s32(
                ((int32_t)env->vfp.reg[rz].dsps[i] << 16),
                ((((int32_t)env->vfp.reg[rx].dsps[i] *
                (int32_t)env->vfp.reg[ry].dsps[i]) << 1) -
                (int32_t)(1 << 15))
            ) >> 16) & 0xffff;
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (sat_sub_s64(
                ((int64_t)env->vfp.reg[rz].dspi[i] << 32),
                ((((int64_t)env->vfp.reg[rx].dspi[i] *
                (int64_t)env->vfp.reg[ry].dspi[i]) << 1) -
                ((int64_t)1 << 31))
            ) >> 32) & 0xffffffff;
        }
        break;
    }
}
void VDSP2_HELPER(vrmulhsirs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, ry, rz, index = 0, imm5;
    union VDSP tmp;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[i] = (sat_sub_s16(
                ((int16_t)env->vfp.reg[rz].dspc[i] << 8),
                ((((int16_t)env->vfp.reg[rx].dspc[i] *
                (int16_t)env->vfp.reg[ry].dspc[index]) << 1) -
                (int16_t)(1 << 7))
            ) >> 8) & 0xff;
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[i] = (sat_sub_s32(
                ((int32_t)env->vfp.reg[rz].dsps[i] << 16),
                ((((int32_t)env->vfp.reg[rx].dsps[i] *
                (int32_t)env->vfp.reg[ry].dsps[index]) << 1) -
                (int32_t)(1 << 15))
            ) >> 16) & 0xffff;
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[i] = (sat_sub_s64(
                ((int64_t)env->vfp.reg[rz].dspi[i] << 32),
                ((((int64_t)env->vfp.reg[rx].dspi[i] *
                (int64_t)env->vfp.reg[ry].dspi[index]) << 1) -
                ((int64_t)1 << 31))
            ) >> 32) & 0xffffffff;
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulsae)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz, imm;
    union VDSP tmp, tmp1;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (insn >> 5) & 0xf;
    cnt = cnt / 2;
    memcpy(&tmp, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp1, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[i] += ((
                (int16_t)env->vfp.reg[rx].dspc[i] *
                (int16_t)env->vfp.reg[ry].dspc[i]
            ) >> imm);
            tmp1.dsps[i] += ((
                (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                (int16_t)env->vfp.reg[ry].dspc[cnt + i]
            ) >> imm);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[i] += ((
                (int32_t)env->vfp.reg[rx].dsps[i] *
                (int32_t)env->vfp.reg[ry].dsps[i]
            ) >> imm);
            tmp1.dspi[i] += ((
                (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                (int32_t)env->vfp.reg[ry].dsps[cnt + i]
            ) >> imm);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspl[i] += ((
                (int64_t)env->vfp.reg[rx].dspi[i] *
                (int64_t)env->vfp.reg[ry].dspi[i]
            ) >> imm);
            tmp1.dspl[i] += ((
                (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                (int64_t)env->vfp.reg[ry].dspi[cnt + i]
            ) >> imm);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulsaie)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, rz, index = 0, imm, imm5;
    union VDSP tmp, tmp1;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    memcpy(&tmp, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp1, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[i] += ((
                (int16_t)env->vfp.reg[rx].dspc[i] *
                (int16_t)env->vfp.reg[rx + 1].dspc[index]
            ) >> imm);
            tmp1.dsps[i] += ((
                (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                (int16_t)env->vfp.reg[rx + 1].dspc[index]
            ) >> imm);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[i] += ((
                (int32_t)env->vfp.reg[rx].dsps[i] *
                (int32_t)env->vfp.reg[rx + 1].dsps[index]
            ) >> imm);
            tmp1.dspi[i] += ((
                (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                (int32_t)env->vfp.reg[rx + 1].dsps[index]
            ) >> imm);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspl[i] += ((
                (int64_t)env->vfp.reg[rx].dspi[i] *
                (int64_t)env->vfp.reg[rx + 1].dspi[index]
            ) >> imm);
            tmp1.dspl[i] += ((
                (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                (int64_t)env->vfp.reg[rx + 1].dspi[index]
            ) >> imm);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}

void VDSP2_HELPER(vrmulsse)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz, imm;
    union VDSP tmp, tmp1;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (insn >> 5) & 0xf;
    cnt = cnt / 2;
    memcpy(&tmp, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp1, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[i] += (-(
                (int16_t)env->vfp.reg[rx].dspc[i] *
                (int16_t)env->vfp.reg[ry].dspc[i]
            ) >> imm);
            tmp1.dsps[i] += (-(
                (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                (int16_t)env->vfp.reg[ry].dspc[cnt + i]
            ) >> imm);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[i] += (-(
                (int32_t)env->vfp.reg[rx].dsps[i] *
                (int32_t)env->vfp.reg[ry].dsps[i]
            ) >> imm);
            tmp1.dspi[i] += (-(
                (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                (int32_t)env->vfp.reg[ry].dsps[cnt + i]
            ) >> imm);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspl[i] += (-(
                (int64_t)env->vfp.reg[rx].dspi[i] *
                (int64_t)env->vfp.reg[ry].dspi[i]
            ) >> imm);
            tmp1.dspl[i] += (-(
                (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                (int64_t)env->vfp.reg[ry].dspi[cnt + i]
            ) >> imm);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}

void VDSP2_HELPER(vrmulssie)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, rz, index = 0, imm, imm5;
    union VDSP tmp, tmp1;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;
    memcpy(&tmp, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp1, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[i] += (-(
                (int16_t)env->vfp.reg[rx].dspc[i] *
                (int16_t)env->vfp.reg[rx + 1].dspc[index]
            ) >> imm);
            tmp1.dsps[i] += (-(
                (int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                (int16_t)env->vfp.reg[rx + 1].dspc[index]
            ) >> imm);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[i] += (-(
                (int32_t)env->vfp.reg[rx].dsps[i] *
                (int32_t)env->vfp.reg[rx + 1].dsps[index]
            ) >> imm);
            tmp1.dspi[i] += (-(
                (int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                (int32_t)env->vfp.reg[rx + 1].dsps[index]
            ) >> imm);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspl[i] += (-(
                (int64_t)env->vfp.reg[rx].dspi[i] *
                (int64_t)env->vfp.reg[rx + 1].dspi[index]
            ) >> imm);
            tmp1.dspl[i] += (-(
                (int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                (int64_t)env->vfp.reg[rx + 1].dspi[index]
            ) >> imm);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}

void VDSP2_HELPER(vrmulshre)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz, imm;
    union VDSP tmp, tmp1;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (insn >> 5) & 0xf;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[i] = (((int16_t)env->vfp.reg[rx].dspc[i] *
                            (int16_t)env->vfp.reg[ry].dspc[i]) >> imm);
            tmp1.dsps[i] = (((int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                             (int16_t)env->vfp.reg[ry].dspc[cnt + i]) >> imm);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[i] = (((int32_t)env->vfp.reg[rx].dsps[i] *
                            (int32_t)env->vfp.reg[ry].dsps[i]) >> imm);
            tmp1.dspi[i] = (((int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                             (int32_t)env->vfp.reg[ry].dsps[cnt + i]) >> imm);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspl[i] = (((int64_t)env->vfp.reg[rx].dspi[i] *
                           (int64_t)env->vfp.reg[ry].dspi[i]) >> imm);
            tmp1.dspl[i] = (((int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                             (int64_t)env->vfp.reg[ry].dspi[cnt + i]) >> imm);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}

void VDSP2_HELPER(vrmulshrie)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, rz, index = 0, imm, imm5;
    union VDSP tmp, tmp1;

    imm5 = ((insn >> 21) & 0x10) | ((insn >> 17) & 0x8) | ((insn >> 5) & 0x7);
    decode_imm5(env, imm5, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[i] = (((int16_t)env->vfp.reg[rx].dspc[i] *
                            (int16_t)env->vfp.reg[rx + 1].dspc[index]) >> imm);
            tmp1.dsps[i] = (((int16_t)env->vfp.reg[rx].dspc[cnt + i] *
                             (int16_t)env->vfp.reg[rx + 1].dspc[index]) >> imm);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[i] = (((int32_t)env->vfp.reg[rx].dsps[i] *
                            (int32_t)env->vfp.reg[rx + 1].dsps[index]) >> imm);
            tmp1.dspi[i] = (((int32_t)env->vfp.reg[rx].dsps[cnt + i] *
                             (int32_t)env->vfp.reg[rx + 1].dsps[index]) >> imm);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspl[i] = (((int64_t)env->vfp.reg[rx].dspi[i] *
                            (int64_t)env->vfp.reg[rx + 1].dspi[index]) >> imm);
            tmp1.dspl[i] = (((int64_t)env->vfp.reg[rx].dspi[cnt + i] *
                             (int64_t)env->vfp.reg[rx + 1].dspi[index]) >> imm);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp1, sizeof(union VDSP));
}

void VDSP2_HELPER(vrmulxaars)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;
    union VDSP tmp;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i],
                env->vfp.reg[rz].dspc[2 * i]);
            tmp.dspc[2 * i + 1] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i + 1],
                env->vfp.reg[rz].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i],
                env->vfp.reg[rz].dsps[2 * i]);
            tmp.dsps[2 * i + 1] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i + 1],
                env->vfp.reg[rz].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i],
                env->vfp.reg[rz].dspi[2 * i]);
            tmp.dspi[2 * i + 1] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i + 1],
                env->vfp.reg[rz].dspi[2 * i + 1]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vrmulxaairs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, ry, rz, index = 0, imm4;
    union VDSP tmp;

    imm4 = ((insn >> 22) & 0x8) | ((insn >> 18) & 0x4) | ((insn >> 6) & 0x3);
    decode_imm4(env, imm4, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * index],
                env->vfp.reg[rz].dspc[2 * i]);
            tmp.dspc[2 * i + 1] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * index + 1],
                env->vfp.reg[rz].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * index],
                env->vfp.reg[rz].dsps[2 * i]);
            tmp.dsps[2 * i + 1] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * index + 1],
                env->vfp.reg[rz].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * index],
                env->vfp.reg[rz].dspi[2 * i]);
            tmp.dspi[2 * i + 1] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * index + 1],
                env->vfp.reg[rz].dspi[2 * i + 1]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulxasrs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;
    union VDSP tmp;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_subh_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i + 1],
                env->vfp.reg[rz].dspc[2 * i]);
            tmp.dspc[2 * i + 1] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i],
                env->vfp.reg[rz].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_subh_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i + 1],
                env->vfp.reg[rz].dsps[2 * i]);
            tmp.dsps[2 * i + 1] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i],
                env->vfp.reg[rz].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_subh_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i + 1],
                env->vfp.reg[rz].dspi[2 * i]);
            tmp.dspi[2 * i + 1] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i],
                env->vfp.reg[rz].dspi[2 * i + 1]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulxasirs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, ry, rz, index = 0, imm4;
    union VDSP tmp;

    imm4 = ((insn >> 22) & 0x8) | ((insn >> 18) & 0x4) | ((insn >> 6) & 0x3);
    decode_imm4(env, imm4, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_subh_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * index + 1],
                env->vfp.reg[rz].dspc[2 * i]);
            tmp.dspc[2 * i + 1] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * index],
                env->vfp.reg[rz].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_subh_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * index + 1],
                env->vfp.reg[rz].dsps[2 * i]);
            tmp.dsps[2 * i + 1] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * index],
                env->vfp.reg[rz].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_subh_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * index + 1],
                env->vfp.reg[rz].dspi[2 * i]);
            tmp.dspi[2 * i + 1] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * index],
                env->vfp.reg[rz].dspi[2 * i + 1]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulxssrs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;
    union VDSP tmp;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_subh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i],
                env->vfp.reg[rz].dspc[2 * i]);
            tmp.dspc[2 * i + 1] = sat_round_subh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i + 1],
                env->vfp.reg[rz].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_subh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i],
                env->vfp.reg[rz].dsps[2 * i]);
            tmp.dsps[2 * i + 1] = sat_round_subh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i + 1],
                env->vfp.reg[rz].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_subh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i],
                env->vfp.reg[rz].dspi[2 * i]);
            tmp.dspi[2 * i + 1] = sat_round_subh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i + 1],
                env->vfp.reg[rz].dspi[2 * i + 1]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulxssirs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, ry, rz, index = 0, imm4;
    union VDSP tmp;

    imm4 = ((insn >> 22) & 0x8) | ((insn >> 18) & 0x4) | ((insn >> 6) & 0x3);
    decode_imm4(env, imm4, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_subh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * index],
                env->vfp.reg[rz].dspc[2 * i]);
            tmp.dspc[2 * i + 1] = sat_round_subh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * index + 1],
                env->vfp.reg[rz].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_subh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * index],
                env->vfp.reg[rz].dsps[2 * i]);
            tmp.dsps[2 * i + 1] = sat_round_subh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * index + 1],
                env->vfp.reg[rz].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_subh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * index],
                env->vfp.reg[rz].dspi[2 * i]);
            tmp.dspi[2 * i + 1] = sat_round_subh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * index + 1],
                env->vfp.reg[rz].dspi[2 * i + 1]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulxsars)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;
    union VDSP tmp;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i + 1],
                env->vfp.reg[rz].dspc[2 * i]);
            tmp.dspc[2 * i + 1] = sat_round_subh_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i],
                env->vfp.reg[rz].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i + 1],
                env->vfp.reg[rz].dsps[2 * i]);
            tmp.dsps[2 * i + 1] = sat_round_subh_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i],
                env->vfp.reg[rz].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i + 1],
                env->vfp.reg[rz].dspi[2 * i]);
            tmp.dspi[2 * i + 1] = sat_round_subh_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i],
                env->vfp.reg[rz].dspi[2 * i + 1]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrmulxsairs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t lng = 0, rx, ry, rz, index = 0, imm4;
    union VDSP tmp;

    imm4 = ((insn >> 22) & 0x8) | ((insn >> 18) & 0x4) | ((insn >> 6) & 0x3);
    decode_imm4(env, imm4, &lng, &index);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_addh_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * index + 1],
                env->vfp.reg[rz].dspc[2 * i]);
            tmp.dspc[2 * i + 1] = sat_round_subh_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * index],
                env->vfp.reg[rz].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_addh_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * index + 1],
                env->vfp.reg[rz].dsps[2 * i]);
            tmp.dsps[2 * i + 1] = sat_round_subh_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * index],
                env->vfp.reg[rz].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_addh_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * index + 1],
                env->vfp.reg[rz].dspi[2 * i]);
            tmp.dspi[2 * i + 1] = sat_round_subh_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * index],
                env->vfp.reg[rz].dspi[2 * i + 1]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}
void VDSP2_HELPER(vrcmulrs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;
    union VDSP tmp;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_ash_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i],
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i + 1]);
            tmp.dspc[2 * i + 1] = sat_round_aah_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i + 1],
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_ash_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i],
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i + 1]);
            tmp.dsps[2 * i + 1] = sat_round_aah_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i + 1],
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_ash_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i],
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i + 1]);
            tmp.dspi[2 * i + 1] = sat_round_aah_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i + 1],
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vrcmulae)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz, index;
    union VDSP tmp1, tmp2;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    index = (insn >> 5) & 0xf;
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 4;
    memcpy(&tmp1, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp2, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dsps[2 * i] += (((int16_t)vrx.dspc[2 * i] *
                (int16_t)vry.dspc[2 * i]) >> index) +
                (-((int16_t)vrx.dspc[2 * i + 1] *
                (int16_t)vry.dspc[2 * i + 1]) >> index);
            tmp1.dsps[2 * i + 1] += (((int16_t)vrx.dspc[2 * i] *
                (int16_t)vry.dspc[2 * i + 1]) >> index) +
                (((int16_t)vrx.dspc[2 * i + 1] *
                (int16_t)vry.dspc[2 * i]) >> index);
            tmp2.dsps[2 * i] += (((int16_t)vrx.dspc[2 * i + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 2 * cnt]) >> index) +
                (-((int16_t)vrx.dspc[2 * i + 1 + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dsps[2 * i + 1] += (((int16_t)vrx.dspc[2 * i + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 1 + 2 * cnt]) >> index) +
                (((int16_t)vrx.dspc[2 * i + 1 + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 2 * cnt]) >> index);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dspi[2 * i] += (((int32_t)vrx.dsps[2 * i] *
                (int32_t)vry.dsps[2 * i]) >> index) +
                (-((int32_t)vrx.dsps[2 * i + 1] *
                (int32_t)vry.dsps[2 * i + 1]) >> index);
            tmp1.dspi[2 * i + 1] += (((int32_t)vrx.dsps[2 * i] *
                (int32_t)vry.dsps[2 * i + 1]) >> index) +
                (((int32_t)vrx.dsps[2 * i + 1] *
                (int32_t)vry.dsps[2 * i]) >> index);
            tmp2.dspi[2 * i] += (((int32_t)vrx.dsps[2 * i + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 2 * cnt]) >> index) +
                (-((int32_t)vrx.dsps[2 * i + 1 + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dspi[2 * i + 1] += (((int32_t)vrx.dsps[2 * i + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 1 + 2 * cnt]) >> index) +
                (((int32_t)vrx.dsps[2 * i + 1 + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 2 * cnt]) >> index);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dspl[2 * i] += (((int64_t)vrx.dspi[2 * i] *
                (int64_t)vry.dspi[2 * i]) >> index) +
                (-((int64_t)vrx.dspi[2 * i + 1] *
                (int64_t)vry.dspi[2 * i + 1]) >> index);
            tmp1.dspl[2 * i + 1] += (((int64_t)vrx.dspi[2 * i] *
                (int64_t)vry.dspi[2 * i + 1]) >> index) +
                (((int64_t)vrx.dspi[2 * i + 1] *
                (int64_t)vry.dspi[2 * i]) >> index);
            tmp2.dspl[2 * i] += (((int64_t)vrx.dspi[2 * i + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 2 * cnt]) >> index) +
                (-((int64_t)vrx.dspi[2 * i + 1 + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dspl[2 * i + 1] += (((int64_t)vrx.dspi[2 * i + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 1 + 2 * cnt]) >> index) +
                (((int64_t)vrx.dspi[2 * i + 1 + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 2 * cnt]) >> index);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp1, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vrcmulcrs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;
    union VDSP tmp;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_aah_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i],
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i + 1]);
            tmp.dspc[2 * i + 1] = sat_round_ash_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i + 1],
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_aah_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i],
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i + 1]);
            tmp.dsps[2 * i + 1] = sat_round_ash_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i + 1],
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_aah_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i],
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i + 1]);
            tmp.dspi[2 * i + 1] = sat_round_ash_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i + 1],
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vrcmulcae)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz, index;
    union VDSP tmp1, tmp2;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    index = (insn >> 5) & 0xf;
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 4;
    memcpy(&tmp1, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp2, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dsps[2 * i] += (((int16_t)vrx.dspc[2 * i] *
                (int16_t)vry.dspc[2 * i]) >> index) +
                (((int16_t)vrx.dspc[2 * i + 1] *
                (int16_t)vry.dspc[2 * i + 1]) >> index);
            tmp1.dsps[2 * i + 1] += (((int16_t)vrx.dspc[2 * i] *
                (int16_t)vry.dspc[2 * i + 1]) >> index) +
                (-((int16_t)vrx.dspc[2 * i + 1] *
                (int16_t)vry.dspc[2 * i]) >> index);
            tmp2.dsps[2 * i] += (((int16_t)vrx.dspc[2 * i + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 2 * cnt]) >> index) +
                (((int16_t)vrx.dspc[2 * i + 1 + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dsps[2 * i + 1] += (((int16_t)vrx.dspc[2 * i + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 1 + 2 * cnt]) >> index) +
                (-((int16_t)vrx.dspc[2 * i + 1 + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 2 * cnt]) >> index);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dspi[2 * i] += (((int32_t)vrx.dsps[2 * i] *
                (int32_t)vry.dsps[2 * i]) >> index) +
                (((int32_t)vrx.dsps[2 * i + 1] *
                (int32_t)vry.dsps[2 * i + 1]) >> index);
            tmp1.dspi[2 * i + 1] += (((int32_t)vrx.dsps[2 * i] *
                (int32_t)vry.dsps[2 * i + 1]) >> index) +
                (-((int32_t)vrx.dsps[2 * i + 1] *
                (int32_t)vry.dsps[2 * i]) >> index);
            tmp2.dspi[2 * i] += (((int32_t)vrx.dsps[2 * i + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 2 * cnt]) >> index) +
                (((int32_t)vrx.dsps[2 * i + 1 + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dspi[2 * i + 1] += (((int32_t)vrx.dsps[2 * i + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 1 + 2 * cnt]) >> index) +
                (-((int32_t)vrx.dsps[2 * i + 1 + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 2 * cnt]) >> index);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dspl[2 * i] += (((int64_t)vrx.dspi[2 * i] *
                (int64_t)vry.dspi[2 * i]) >> index) +
                (((int64_t)vrx.dspi[2 * i + 1] *
                (int64_t)vry.dspi[2 * i + 1]) >> index);
            tmp1.dspl[2 * i + 1] += (((int64_t)vrx.dspi[2 * i] *
                (int64_t)vry.dspi[2 * i + 1]) >> index) +
                (-((int64_t)vrx.dspi[2 * i + 1] *
                (int64_t)vry.dspi[2 * i]) >> index);
            tmp2.dspl[2 * i] += (((int64_t)vrx.dspi[2 * i + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 2 * cnt]) >> index) +
                (((int64_t)vrx.dspi[2 * i + 1 + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dspl[2 * i + 1] += (((int64_t)vrx.dspi[2 * i + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 1 + 2 * cnt]) >> index) +
                (-((int64_t)vrx.dspi[2 * i + 1 + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 2 * cnt]) >> index);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp1, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vrcmulnrs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;
    union VDSP tmp;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_ash_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i + 1],
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i]);
            tmp.dspc[2 * i + 1] = sat_round_ssh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i + 1],
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_ash_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i + 1],
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i]);
            tmp.dsps[2 * i + 1] = sat_round_ssh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i + 1],
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_ash_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i + 1],
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i]);
            tmp.dspi[2 * i + 1] = sat_round_ssh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i + 1],
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vrcmulnae)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz, index;
    union VDSP tmp1, tmp2;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    index = (insn >> 5) & 0xf;
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 4;
    memcpy(&tmp1, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp2, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dsps[2 * i] += (-((int16_t)vrx.dspc[2 * i] *
                (int16_t)vry.dspc[2 * i]) >> index) +
                (((int16_t)vrx.dspc[2 * i + 1] *
                (int16_t)vry.dspc[2 * i + 1]) >> index);
            tmp1.dsps[2 * i + 1] += (-((int16_t)vrx.dspc[2 * i] *
                (int16_t)vry.dspc[2 * i + 1]) >> index) +
                (-((int16_t)vrx.dspc[2 * i + 1] *
                (int16_t)vry.dspc[2 * i]) >> index);
            tmp2.dsps[2 * i] += (-((int16_t)vrx.dspc[2 * i + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 2 * cnt]) >> index) +
                (((int16_t)vrx.dspc[2 * i + 1 + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dsps[2 * i + 1] += (-((int16_t)vrx.dspc[2 * i + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 1 + 2 * cnt]) >> index) +
                (-((int16_t)vrx.dspc[2 * i + 1 + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 2 * cnt]) >> index);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dspi[2 * i] += (-((int32_t)vrx.dsps[2 * i] *
                (int32_t)vry.dsps[2 * i]) >> index) +
                (((int32_t)vrx.dsps[2 * i + 1] *
                (int32_t)vry.dsps[2 * i + 1]) >> index);
            tmp1.dspi[2 * i + 1] += (-((int32_t)vrx.dsps[2 * i] *
                (int32_t)vry.dsps[2 * i + 1]) >> index) +
                (-((int32_t)vrx.dsps[2 * i + 1] *
                (int32_t)vry.dsps[2 * i]) >> index);
            tmp2.dspi[2 * i] += (-((int32_t)vrx.dsps[2 * i + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 2 * cnt]) >> index) +
                (((int32_t)vrx.dsps[2 * i + 1 + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dspi[2 * i + 1] += (-((int32_t)vrx.dsps[2 * i + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 1 + 2 * cnt]) >> index) +
                (-((int32_t)vrx.dsps[2 * i + 1 + 2 * cnt] *
                (int32_t)vry.dsps[2 * i  + 2 * cnt]) >> index);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dspl[2 * i] += (-((int64_t)vrx.dspi[2 * i] *
                (int64_t)vry.dspi[2 * i]) >> index) +
                (((int64_t)vrx.dspi[2 * i + 1] *
                (int64_t)vry.dspi[2 * i + 1]) >> index);
            tmp1.dspl[2 * i + 1] += (-((int64_t)vrx.dspi[2 * i] *
                (int64_t)vry.dspi[2 * i + 1]) >> index) +
                (-((int64_t)vrx.dspi[2 * i + 1] *
                (int64_t)vry.dspi[2 * i]) >> index);
            tmp2.dspl[2 * i] += (-((int64_t)vrx.dspi[2 * i + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 2 * cnt]) >> index) +
                (((int64_t)vrx.dspi[2 * i + 1 + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dspl[2 * i + 1] += (-((int64_t)vrx.dspi[2 * i + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 1 + 2 * cnt]) >> index) +
                (-((int64_t)vrx.dspi[2 * i + 1 + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 2 * cnt]) >> index);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp1, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vrcmulcnrs)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;
    union VDSP tmp;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 2;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp.dspc[2 * i] = sat_round_ssh_s8(
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i],
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i + 1]);
            tmp.dspc[2 * i + 1] = sat_round_ash_s8(
                env->vfp.reg[rx].dspc[2 * i + 1],
                env->vfp.reg[ry].dspc[2 * i],
                env->vfp.reg[rx].dspc[2 * i],
                env->vfp.reg[ry].dspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp.dsps[2 * i] = sat_round_ssh_s16(
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i],
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i + 1]);
            tmp.dsps[2 * i + 1] = sat_round_ash_s16(
                env->vfp.reg[rx].dsps[2 * i + 1],
                env->vfp.reg[ry].dsps[2 * i],
                env->vfp.reg[rx].dsps[2 * i],
                env->vfp.reg[ry].dsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp.dspi[2 * i] = sat_round_ssh_s32(
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i],
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i + 1]);
            tmp.dspi[2 * i + 1] = sat_round_ash_s32(
                env->vfp.reg[rx].dspi[2 * i + 1],
                env->vfp.reg[ry].dspi[2 * i],
                env->vfp.reg[rx].dspi[2 * i],
                env->vfp.reg[ry].dspi[2 * i + 1]);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vrcmulcnae)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz, index;
    union VDSP tmp1, tmp2;

    wid = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    index = (insn >> 5) & 0xf;
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    cnt = cnt / 4;
    memcpy(&tmp1, &env->vfp.reg[rz], sizeof(union VDSP));
    memcpy(&tmp2, &env->vfp.reg[rz + 1], sizeof(union VDSP));

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dsps[2 * i] += (-((int16_t)vrx.dspc[2 * i] *
                (int16_t)vry.dspc[2 * i]) >> index) +
                (-((int16_t)vrx.dspc[2 * i + 1] *
                (int16_t)vry.dspc[2 * i + 1]) >> index);
            tmp1.dsps[2 * i + 1] += (-((int16_t)vrx.dspc[2 * i] *
                (int16_t)vry.dspc[2 * i + 1]) >> index) +
                (((int16_t)vrx.dspc[2 * i + 1] *
                (int16_t)vry.dspc[2 * i]) >> index);
            tmp2.dsps[2 * i] += (-((int16_t)vrx.dspc[2 * i + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 2 * cnt]) >> index) +
                (-((int16_t)vrx.dspc[2 * i + 1 + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dsps[2 * i + 1] += (-((int16_t)vrx.dspc[2 * i + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 1 + 2 * cnt]) >> index) +
                (((int16_t)vrx.dspc[2 * i + 1 + 2 * cnt] *
                (int16_t)vry.dspc[2 * i + 2 * cnt]) >> index);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dspi[2 * i] += (-((int32_t)vrx.dsps[2 * i] *
                (int32_t)vry.dsps[2 * i]) >> index) +
                (-((int32_t)vrx.dsps[2 * i + 1] *
                (int32_t)vry.dsps[2 * i + 1]) >> index);
            tmp1.dspi[2 * i + 1] += (-((int32_t)vrx.dsps[2 * i] *
                (int32_t)vry.dsps[2 * i + 1]) >> index) +
                (((int32_t)vrx.dsps[2 * i + 1] *
                (int32_t)vry.dsps[2 * i]) >> index);
            tmp2.dspi[2 * i] += (-((int32_t)vrx.dsps[2 * i + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 2 * cnt]) >> index) +
                (-((int32_t)vrx.dsps[2 * i + 1 + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dspi[2 * i + 1] += (-((int32_t)vrx.dsps[2 * i + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 1 + 2 * cnt]) >> index) +
                (((int32_t)vrx.dsps[2 * i + 1 + 2 * cnt] *
                (int32_t)vry.dsps[2 * i + 2 * cnt]) >> index);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            union VDSP vrx, vry;
            vrx = env->vfp.reg[rx];
            vry = env->vfp.reg[ry];
            tmp1.dspl[2 * i] += (-((int64_t)vrx.dspi[2 * i] *
                (int64_t)vry.dspi[2 * i]) >> index) +
                (-((int64_t)vrx.dspi[2 * i + 1] *
                (int64_t)vry.dspi[2 * i + 1]) >> index);
            tmp1.dspl[2 * i + 1] += (-((int64_t)vrx.dspi[2 * i] *
                (int64_t)vry.dspi[2 * i + 1]) >> index) +
                (((int64_t)vrx.dspi[2 * i + 1] *
                (int64_t)vry.dspi[2 * i]) >> index);
            tmp2.dspl[2 * i] += (-((int64_t)vrx.dspi[2 * i + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 2 * cnt]) >> index) +
                (-((int64_t)vrx.dspi[2 * i + 1 + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 1 + 2 * cnt]) >> index);
            tmp2.dspl[2 * i + 1] += (-((int64_t)vrx.dspi[2 * i + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 1 + 2 * cnt]) >> index) +
                (((int64_t)vrx.dspi[2 * i + 1 + 2 * cnt] *
                (int64_t)vry.dspi[2 * i + 2 * cnt]) >> index);
        }
        break;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp1, sizeof(union VDSP));
    memcpy(&(env->vfp.reg[rz + 1]), &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vrecpe)(CPUCSKYState *env, uint32_t insn)
{
    int i, cnt;
    uint32_t size, wid, sign, rx, rz;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    wid = 8 << size;
    cnt = 128 / wid;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int8_t a = 0x10, b = 0x20;
                int8_t c = env->vfp.reg[rx].dspc[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].dspc[i] = 0x5a +
                    ((int16_t)((int16_t)c * (int16_t)0xffc4) >> 5);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int16_t a = 0x1000, b = 0x2000;
                int16_t c = env->vfp.reg[rx].dsps[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].dsps[i] = 0x5a56 +
                    ((int32_t)((int32_t)c * (int32_t)0xffffc3c5) >> 13);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int32_t a = 0x10000000, b = 0x20000000;
                int32_t c = env->vfp.reg[rx].dspi[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].dspi[i] = 0x5a560418 +
                    ((int64_t)((int64_t)c * (int64_t)0xffffffffc3c432cb) >> 29);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                uint8_t a = 0x20, b = 0x40;
                uint8_t c = env->vfp.reg[rx].udspc[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].udspc[i] = 0xb4 +
                    ((int16_t)((int16_t)c * (int16_t)0xff88) >> 6);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                uint16_t a = 0x2000, b = 0x4000;
                uint16_t c = env->vfp.reg[rx].udsps[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].udsps[i] = 0xb4ac +
                    ((int32_t)((int32_t)c * (int32_t)0xffff8789) >> 14);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                uint32_t a = 0x20000000, b = 0x40000000;
                uint32_t c = env->vfp.reg[rx].udspi[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].udspi[i] = 0xb4ac0831 +
                    ((int64_t)((int64_t)c * (int64_t)0xffffffff87886595) >> 30);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}
void VDSP2_HELPER(vrecps)(CPUCSKYState *env, uint32_t insn)
{
    int i, cnt;
    uint32_t size, wid, sign, rx, ry, rz;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    wid = 8 << size;
    cnt = 128 / wid;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int8_t a = env->vfp.reg[rx].dspc[i];
                int8_t b = env->vfp.reg[ry].dspc[i];
                env->vfp.reg[rz].dspc[i] = 0x40 +
                    ((int16_t)(-(int16_t)a * (int16_t)b) >> 5);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int16_t a = env->vfp.reg[rx].dsps[i];
                int16_t b = env->vfp.reg[ry].dsps[i];
                env->vfp.reg[rz].dsps[i] = 0x4000 +
                    ((int32_t)(-(int32_t)a * (int32_t)b) >> 13);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int32_t a = env->vfp.reg[rx].dspi[i];
                int32_t b = env->vfp.reg[ry].dspi[i];
                env->vfp.reg[rz].dspi[i] = 0x40000000 +
                    ((int64_t)(-(int64_t)a * (int64_t)b) >> 29);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                uint8_t a = env->vfp.reg[rx].udspc[i];
                uint8_t b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = 0x80 +
                    ((int16_t)(-(int16_t)a * (int16_t)b) >> 6);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                uint16_t a = env->vfp.reg[rx].udsps[i];
                uint16_t b = env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = 0x8000 +
                    ((int32_t)(-(int32_t)a * (int32_t)b) >> 14);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                uint32_t a = env->vfp.reg[rx].udspi[i];
                uint32_t b = env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = 0x80000000 +
                    ((int64_t)(-(int64_t)a * (int64_t)b) >> 30);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}
void VDSP2_HELPER(vrsqrte)(CPUCSKYState *env, uint32_t insn)
{
    int i, cnt;
    uint32_t size, wid, sign, rx, rz;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    wid = 8 << size;
    cnt = 128 / wid;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int8_t a = 0x10, b = 0x20;
                int8_t c = env->vfp.reg[rx].dspc[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].dspc[i] = 0x3c - c;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int16_t a = 0x1000, b = 0x2000;
                int16_t c = env->vfp.reg[rx].dsps[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].dsps[i] = 0x3c28 - c;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int32_t a = 0x10000000, b = 0x20000000;
                int32_t c = env->vfp.reg[rx].dspi[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].dspi[i] = 0x3c28f5c2 - c;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                uint8_t a = 0x20, b = 0x40;
                uint8_t c = env->vfp.reg[rx].udspc[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].udspc[i] = 0x78 - c;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                uint16_t a = 0x2000, b = 0x4000;
                uint16_t c = env->vfp.reg[rx].udsps[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].udsps[i] = 0x7851 - c;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                uint32_t a = 0x20000000, b = 0x40000000;
                uint32_t c = env->vfp.reg[rx].udspi[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].udspi[i] = 0x7851eb85 - c;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vrsqrts)(CPUCSKYState *env, uint32_t insn)
{
    int i, cnt;
    uint32_t size, wid, sign, rx, ry, rz;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    wid = 8 << size;
    cnt = 128 / wid;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int8_t a = env->vfp.reg[rx].dspc[i];
                int8_t b = env->vfp.reg[ry].dspc[i];
                env->vfp.reg[rz].dspc[i] = 0x30 +
                    ((int16_t)(-(int16_t)a * (int16_t)b) >> 6);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int16_t a = env->vfp.reg[rx].dsps[i];
                int16_t b = env->vfp.reg[ry].dsps[i];
                env->vfp.reg[rz].dsps[i] = 0x3000 +
                    ((int32_t)(-(int32_t)a * (int32_t)b) >> 14);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int32_t a = env->vfp.reg[rx].dspi[i];
                int32_t b = env->vfp.reg[ry].dspi[i];
                env->vfp.reg[rz].dspi[i] = 0x30000000 +
                    ((int64_t)(-(int64_t)a * (int64_t)b) >> 30);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                uint8_t a = env->vfp.reg[rx].udspc[i];
                uint8_t b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = 0x60 +
                    ((int16_t)(-(int16_t)a * (int16_t)b) >> 7);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                uint16_t a = env->vfp.reg[rx].udsps[i];
                uint16_t b = env->vfp.reg[ry].udsps[i];
                env->vfp.reg[rz].udsps[i] = 0x6000 +
                    ((int32_t)(-(int32_t)a * (int32_t)b) >> 15);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                uint32_t a = env->vfp.reg[rx].udspi[i];
                uint32_t b = env->vfp.reg[ry].udspi[i];
                env->vfp.reg[rz].udspi[i] = 0x60000000 +
                    ((int64_t)(-(int64_t)a * (int64_t)b) >> 31);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vexpe)(CPUCSKYState *env, uint32_t insn)
{
    int i, cnt;
    uint32_t size, wid, sign, rx, rz;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    wid = 8 << size;
    cnt = 128 / wid;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int8_t a = 0x10, b = 0x20;
                int8_t c = env->vfp.reg[rx].dspc[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].dspc[i] = ((int16_t)(0x5c * (int16_t)c)) >> 5;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int16_t a = 0x1000, b = 0x2000;
                int16_t c = env->vfp.reg[rx].dsps[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].dsps[i] = ((int32_t)(0x5c55 * (int32_t)c))
                    >> 13;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int32_t a = 0x10000000, b = 0x20000000;
                int32_t c = env->vfp.reg[rx].dspi[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].dspi[i] = ((int64_t)(0x5c551d68 * (int64_t)c))
                    >> 29;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (wid) {
        case 8:
            for (i = 0; i < cnt; i++) {
                uint8_t a = 0x20, b = 0x40;
                uint8_t c = env->vfp.reg[rx].udspc[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].udspc[i] = ((uint16_t)(0xb8 * (uint16_t)c))
                    >> 6;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                uint16_t a = 0x2000, b = 0x4000;
                uint16_t c = env->vfp.reg[rx].udsps[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].udsps[i] = ((uint32_t)(0xb8aa * (uint32_t)c))
                    >> 14;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                uint32_t a = 0x20000000, b = 0x40000000;
                uint32_t c = env->vfp.reg[rx].udspi[i];
                if (c < a) {
                    c = a;
                } else if (c >= b) {
                    c = b;
                }
                env->vfp.reg[rz].udspi[i] = ((uint64_t)(0xb8aa3ad1 *
                                                        (uint64_t)c)) >> 30;
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

/* SHIFT instructions. */
static inline uint8_t shl_8(uint8_t x, uint8_t y)
{
    if (y >= 8) {
        return 0;
    } else {
        return x << y;
    }
}

static inline uint16_t shl_16(uint16_t x, uint16_t y)
{
    if (y >= 16) {
        return 0;
    } else {
        return x << y;
    }
}

static inline uint32_t shl_32(uint32_t x, uint32_t y)
{
    if (y >= 32) {
        return 0;
    } else {
        return x << y;
    }
}

static inline uint64_t shl_64(uint64_t x, uint64_t y)
{
    if (y >= 64) {
        return 0;
    } else {
        return x << y;
    }
}

static inline uint8_t shr_u8(uint8_t x, uint8_t y)
{
    if (y >= 8) {
        return 0;
    } else {
        return x >> y;
    }
}

static inline int8_t shr_s8(int8_t x, uint8_t y)
{
    if (y >= 8) {
        return x >> 7;
    } else {
        return x >> y;
    }
}

static inline uint16_t shr_u16(uint16_t x, uint16_t y)
{
    if (y >= 16) {
        return 0;
    } else {
        return x >> y;
    }
}

static inline int16_t shr_s16(int16_t x, uint16_t y)
{
    if (y >= 16) {
        return x >> 15;
    } else {
        return x >> y;
    }
}

static inline uint32_t shr_u32(uint32_t x, uint32_t y)
{
    if (y >= 32) {
        return 0;
    } else {
        return x >> y;
    }
}

static inline int32_t shr_s32(int32_t x, uint32_t y)
{
    if (y >= 32) {
        return x >> 31;
    } else {
        return x >> y;
    }
}

static inline uint64_t shr_u64(uint64_t x, uint64_t y)
{
    if (y >= 64) {
        return 0;
    } else {
        return x >> y;
    }
}

static inline int64_t shr_s64(int64_t x, uint64_t y)
{
    if (y >= 64) {
        return x >> 63;
    } else {
        return x >> y;
    }
}

static inline uint8_t sat_shl_u8(uint8_t x, uint8_t y)
{
    uint8_t res;
    if (y >= 8) {
        res = (x == 0) ? 0 : MAX_U8;
    } else {
        res = x << y;
        if ((res >> y) != x) {
            res = MAX_U8;
        }
    }
    return res;
}

static inline int8_t sat_shl_s8(int8_t x, uint8_t y)
{
    int8_t res;
    if (y >= 8) {
        res = (x == 0) ? 0 : (x >> 7) ^ ~SIGNBIT8;
    } else {
        res = x << y;
        if ((res >> y) != x) {
            res = (x >> 7) ^ ~SIGNBIT8;
        }
    }
    return res;
}

static inline uint16_t sat_shl_u16(uint16_t x, uint16_t y)
{
    uint16_t res;
    if (y >= 16) {
        res = (x == 0) ? 0 : MAX_U16;
    } else {
        res = x << y;
        if ((res >> y) != x) {
            res = MAX_U16;
        }
    }
    return res;
}

static inline int16_t sat_shl_s16(int16_t x, uint16_t y)
{
    int16_t res;
    if (y >= 16) {
        res = (x == 0) ? 0 : (x >> 15) ^ ~SIGNBIT16;
    } else {
        res = x << y;
        if ((res >> y) != x) {
            res = (x >> 15) ^ ~SIGNBIT16;
        }
    }
    return res;
}

static inline uint32_t sat_shl_u32(uint32_t x, uint32_t y)
{
    uint32_t res;
    if (y >= 32) {
        res = (x == 0) ? 0 : MAX_U32;
    } else {
        res = x << y;
        if ((res >> y) != x) {
            res = MAX_U32;
        }
    }
    return res;
}

static inline int32_t sat_shl_s32(int32_t x, uint32_t y)
{
    int32_t res;
    if (y >= 32) {
        res = (x == 0) ? 0 : (x >> 31) ^ ~SIGNBIT32;
    } else {
        res = x << y;
        if ((res >> y) != x) {
            res = (x >> 31) ^ ~SIGNBIT32;
        }
    }
    return res;
}

static inline uint64_t sat_shl_u64(uint64_t x, uint64_t y)
{
    uint64_t res;
    if (y >= 64) {
        res = (x == 0) ? 0 : MAX_U64;
    } else {
        res = x << y;
        if ((res >> y) != x) {
            res = MAX_U64;
        }
    }
    return res;
}

static inline int64_t sat_shl_s64(int64_t x, uint64_t y)
{
    int64_t res;
    if (y >= 64) {
        res = (x == 0) ? 0 : (x >> 63) ^ ~SIGNBIT64;
    } else {
        res = x << y;
        if ((res >> y) != x) {
            res = (x >> 63) ^ ~SIGNBIT64;
        }
    }
    return res;
}

static inline uint8_t round_shr_u8(uint8_t x, uint8_t y)
{
    uint8_t res;
    if (y > 8) {
        res = 0;
    } else if (y == 0) {
        res = x;
    } else {
        uint16_t big_res = (uint16_t)x + (1 << (y - 1));
        res = big_res >> y;
    }
    return res;
}

static inline int8_t round_shr_s8(int8_t x, uint8_t y)
{
    int8_t res;
    if (y >= 8) {
        res = 0;
    } else if (y == 0) {
        res = x;
    } else {
        int16_t big_res = (int16_t)x + (1 << (y - 1));
        res = big_res >> y;
    }
    return res;
}

static inline uint16_t round_shr_u16(uint16_t x, uint16_t y)
{
    uint16_t res;
    if (y > 16) {
        res = 0;
    } else if (y == 0) {
        res = x;
    } else {
        uint32_t big_res = (uint32_t)x + (1 << (y - 1));
        res = big_res >> y;
    }
    return res;
}

static inline int16_t round_shr_s16(int16_t x, uint16_t y)
{
    int16_t res;
    if (y >= 16) {
        res = 0;
    } else if (y == 0) {
        res = x;
    } else {
        int32_t big_res = (int32_t)x + (1 << (y - 1));
        res = big_res >> y;
    }
    return res;
}

static inline uint32_t round_shr_u32(uint32_t x, uint32_t y)
{
    uint32_t res;
    if (y > 32) {
        res = 0;
    } else if (y == 0) {
        res = x;
    } else {
        uint64_t big_res = (uint64_t)x + ((uint64_t)1 << (y - 1));
        res = big_res >> y;
    }
    return res;
}

static inline int32_t round_shr_s32(int32_t x, uint32_t y)
{
    int32_t res;
    if (y >= 32) {
        res = 0;
    } else if (y == 0) {
        res = x;
    } else {
        int64_t big_res = (int64_t)x + ((int64_t)1 << (y - 1));
        res = big_res >> y;
    }
    return res;
}

static inline uint64_t round_shr_u64(uint64_t x, uint64_t y)
{
    uint64_t res;
    if (y > 64) {
        res = 0;
    } else if (y == 64) {
        res = x >> 63;
    } else if (y == 0) {
        res = x;
    } else {
        res = x >> (y - 1);
        if (res == MAX_U64) {
            res = 0x8000000000000000ULL;
        } else {
            res++;
            res >>= 1;
        }
    }
    return res;
}

static inline int64_t round_shr_s64(int64_t x, uint64_t y)
{
    int64_t res;
    if (y >= 64) {
        res = 0;
    } else if (y == 0) {
        res = x;
    } else {
        res = x >> (y - 1);
        if (res == MAX_S64) {
            res = 0x4000000000000000LL;
        } else {
            res++;
            res >>= 1;
        }
    }
    return res;
}

static inline uint16_t low_sat_shr_u16(uint16_t x, uint16_t y)
{
    uint16_t res;
    res = x >> y;
    if (res > (uint16_t)MAX_U8) {
        res = MAX_U8;
    }
    return res;
}

static inline int16_t low_sat_shr_s16(int16_t x, uint16_t y)
{
    int16_t res;
    res = x >> y;
    if (res > (int16_t)MAX_S8) {
        res = MAX_S8;
    } else if (res < (int16_t)MIN_S8) {
        res = MIN_S8;
    }
    return res;
}

static inline uint32_t low_sat_shr_u32(uint32_t x, uint32_t y)
{
    uint32_t res;
    res = x >> y;
    if (res > (uint32_t)MAX_U16) {
        res = MAX_U16;
    }
    return res;
}

static inline int32_t low_sat_shr_s32(int32_t x, uint32_t y)
{
    int32_t res;
    res = x >> y;
    if (res > (int32_t)MAX_S16) {
        res = MAX_S16;
    } else if (res < (int32_t)MIN_S16) {
        res = MIN_S16;
    }
    return res;
}

static inline uint64_t low_sat_shr_u64(uint64_t x, uint64_t y)
{
    uint64_t res;
    res = x >> y;
    if (res > (uint64_t)MAX_U32) {
        res = MAX_U32;
    }
    return res;
}

static inline int64_t low_sat_shr_s64(int64_t x, uint64_t y)
{
    int64_t res;
    res = x >> y;
    if (res > (int64_t)MAX_S32) {
        res = MAX_S32;
    } else if (res < (int64_t)MIN_S32) {
        res = MIN_S32;
    }
    return res;
}

static inline uint16_t low_round_sat_shr_u16(uint16_t x, uint16_t y)
{
    uint16_t res;
    if (y > 16) {
        res = 0;
    } else if (y == 16) {
        res = x >> 15;
    } else {
        uint32_t big_res = (uint32_t)x + (1 << (y - 1));
        big_res >>= y;
        if (big_res > (uint32_t)MAX_U8) {
            res = MAX_U8;
        } else {
            res = big_res;
        }
    }
    return res;
}

static inline int16_t low_round_sat_shr_s16(int16_t x, uint16_t y)
{
    int16_t res;
    if (y >= 16) {
        res = 0;
    } else {
        int32_t big_res = (int32_t)x + (1 << (y - 1));
        big_res >>= y;
        if (big_res > (int32_t)MAX_S8) {
            res = MAX_S8;
        } else if (big_res < (int32_t)MIN_S8) {
            res = MIN_S8;
        } else {
            res = big_res;
        }
    }
    return res;
}

static inline uint32_t low_round_sat_shr_u32(uint32_t x, uint32_t y)
{
    uint32_t res;
    if (y > 32) {
        res = 0;
    } else if (y == 32) {
        res = x >> 31;
    } else {
        uint64_t big_res = (uint64_t)x + (1 << (y - 1));
        big_res >>= y;
        if (big_res > (uint64_t)MAX_U16) {
            res = MAX_U16;
        } else {
            res = big_res;
        }
    }
    return res;
}

static inline int32_t low_round_sat_shr_s32(int32_t x, uint32_t y)
{
    int32_t res;
    if (y >= 32) {
        res = 0;
    } else {
        int64_t big_res = (int64_t)x + (1 << (y - 1));
        big_res >>= y;
        if (big_res > (int64_t)MAX_S16) {
            res = MAX_S16;
        } else if (big_res < (int64_t)MIN_S16) {
            res = MIN_S16;
        } else {
            res = big_res;
        }
    }
    return res;
}

static inline uint64_t low_round_sat_shr_u64(uint64_t x, uint64_t y)
{
    uint64_t res;
    if (y > 64) {
        res = 0;
    } else if (y == 64) {
        res = x >> 63;
    } else {
        res = x >> (y - 1);
        if (res == MAX_U64) {
            res = 0x8000000000000000ULL;
        } else {
            res++;
            res >>= 1;
        }
        if (res > (uint64_t)MAX_U32) {
            res = MAX_U32;
        }
    }
    return res;
}

static inline int64_t low_round_sat_shr_s64(int64_t x, uint64_t y)
{
    int64_t res;
    if (y >= 64) {
        res = 0;
    } else {
        res = x >> (y - 1);
        if (res == MAX_S64) {
            res = 0x4000000000000000LL;
        } else {
            res++;
            res >>= 1;
        }
        if (res > (int64_t)MAX_S32) {
            res = MAX_S32;
        } else if (res < (int64_t)MIN_S32) {
            res = MIN_S32;
        }
    }
    return res;
}

void VDSP2_HELPER(vsht)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a, b;
                a = env->vfp.reg[rx].dspc[i];
                b = env->vfp.reg[ry].dspc[i];
                if (b > 0) {
                    env->vfp.reg[rz].dspc[i] = shl_8(a, b);
                } else {
                    env->vfp.reg[rz].dspc[i] = shr_s8(a, -b);
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[i];
                b = env->vfp.reg[ry].dspc[2 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dsps[i] = shl_16(a, b);
                } else {
                    env->vfp.reg[rz].dsps[i] = shr_s16(a, -b);
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[i];
                b = env->vfp.reg[ry].dspc[4 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dspi[i] = shl_32(a, b);
                } else {
                    env->vfp.reg[rz].dspi[i] = shr_s32(a, -b);
                }
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspl[i];
                b = env->vfp.reg[ry].dspc[8 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dspl[i] = shl_64(a, b);
                } else {
                    env->vfp.reg[rz].dspl[i] = shr_s64(a, -b);
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                int8_t b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].dspc[i];
                if (b > 0) {
                    env->vfp.reg[rz].udspc[i] = shl_8(a, b);
                } else {
                    env->vfp.reg[rz].udspc[i] = shr_u8(a, -b);
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                int16_t b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].dspc[2 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udsps[i] = shl_16(a, b);
                } else {
                    env->vfp.reg[rz].udsps[i] = shr_u16(a, -b);
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                int32_t b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].dspc[4 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udspi[i] = shl_32(a, b);
                } else {
                    env->vfp.reg[rz].udspi[i] = shr_u32(a, -b);
                }
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                int64_t b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].dspc[8 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udspl[i] = shl_64(a, b);
                } else {
                    env->vfp.reg[rz].udspl[i] = shr_u64(a, -b);
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vsht_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a, b;
                a = env->vfp.reg[rx].dspc[i];
                b = env->vfp.reg[ry].dspc[i];
                if (b > 0) {
                    env->vfp.reg[rz].dspc[i] = sat_shl_s8(a, b);
                } else {
                    env->vfp.reg[rz].dspc[i] = shr_s8(a, -b);
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[i];
                b = env->vfp.reg[ry].dspc[2 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dsps[i] = sat_shl_s16(a, b);
                } else {
                    env->vfp.reg[rz].dsps[i] = shr_s16(a, -b);
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[i];
                b = env->vfp.reg[ry].dspc[4 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dspi[i] = sat_shl_s32(a, b);
                } else {
                    env->vfp.reg[rz].dspi[i] = shr_s32(a, -b);
                }
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspl[i];
                b = env->vfp.reg[ry].dspc[8 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dspl[i] = sat_shl_s64(a, b);
                } else {
                    env->vfp.reg[rz].dspl[i] = shr_s64(a, -b);
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                int8_t b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].dspc[i];
                if (b > 0) {
                    env->vfp.reg[rz].udspc[i] = sat_shl_u8(a, b);
                } else {
                    env->vfp.reg[rz].udspc[i] = shr_u8(a, -b);
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                int16_t b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].dspc[2 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udsps[i] = sat_shl_u16(a, b);
                } else {
                    env->vfp.reg[rz].udsps[i] = shr_u16(a, -b);
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                int32_t b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].dspc[4 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udspi[i] = sat_shl_u32(a, b);
                } else {
                    env->vfp.reg[rz].udspi[i] = shr_u32(a, -b);
                }
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                int64_t b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].dspc[8 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udspl[i] = sat_shl_u64(a, b);
                } else {
                    env->vfp.reg[rz].udspl[i] = shr_u64(a, -b);
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vsht_r)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a, b;
                a = env->vfp.reg[rx].dspc[i];
                b = env->vfp.reg[ry].dspc[i];
                if (b > 0) {
                    env->vfp.reg[rz].dspc[i] = shl_8(a, b);
                } else {
                    env->vfp.reg[rz].dspc[i] = round_shr_s8(a, -b);
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[i];
                b = env->vfp.reg[ry].dspc[2 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dsps[i] = shl_16(a, b);
                } else {
                    env->vfp.reg[rz].dsps[i] = round_shr_s16(a, -b);
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[i];
                b = env->vfp.reg[ry].dspc[4 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dspi[i] = shl_32(a, b);
                } else {
                    env->vfp.reg[rz].dspi[i] = round_shr_s32(a, -b);
                }
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspl[i];
                b = env->vfp.reg[ry].dspc[8 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dspl[i] = shl_64(a, b);
                } else {
                    env->vfp.reg[rz].dspl[i] = round_shr_s64(a, -b);
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                int8_t b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].dspc[i];
                if (b > 0) {
                    env->vfp.reg[rz].udspc[i] = shl_8(a, b);
                } else {
                    env->vfp.reg[rz].udspc[i] = round_shr_u8(a, -b);
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                int16_t b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].dspc[2 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udsps[i] = shl_16(a, b);
                } else {
                    env->vfp.reg[rz].udsps[i] = round_shr_u16(a, -b);
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                int32_t b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].dspc[4 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udspi[i] = shl_32(a, b);
                } else {
                    env->vfp.reg[rz].udspi[i] = round_shr_u32(a, -b);
                }
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                int64_t b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].dspc[8 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udspl[i] = shl_64(a, b);
                } else {
                    env->vfp.reg[rz].udspl[i] = round_shr_u64(a, -b);
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vsht_rs)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a, b;
                a = env->vfp.reg[rx].dspc[i];
                b = env->vfp.reg[ry].dspc[i];
                if (b > 0) {
                    env->vfp.reg[rz].dspc[i] = sat_shl_s8(a, b);
                } else {
                    env->vfp.reg[rz].dspc[i] = round_shr_s8(a, -b);
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[i];
                b = env->vfp.reg[ry].dspc[2 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dsps[i] = sat_shl_s16(a, b);
                } else {
                    env->vfp.reg[rz].dsps[i] = round_shr_s16(a, -b);
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[i];
                b = env->vfp.reg[ry].dspc[4 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dspi[i] = sat_shl_s32(a, b);
                } else {
                    env->vfp.reg[rz].dspi[i] = round_shr_s32(a, -b);
                }
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspl[i];
                b = env->vfp.reg[ry].dspc[8 * i];
                if (b > 0) {
                    env->vfp.reg[rz].dspl[i] = sat_shl_s64(a, b);
                } else {
                    env->vfp.reg[rz].dspl[i] = round_shr_s64(a, -b);
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                int8_t b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].dspc[i];
                if (b > 0) {
                    env->vfp.reg[rz].udspc[i] = sat_shl_u8(a, b);
                } else {
                    env->vfp.reg[rz].udspc[i] = round_shr_u8(a, -b);
                }
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                int16_t b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].dspc[2 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udsps[i] = sat_shl_u16(a, b);
                } else {
                    env->vfp.reg[rz].udsps[i] = round_shr_u16(a, -b);
                }
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                int32_t b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].dspc[4 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udspi[i] = sat_shl_u32(a, b);
                } else {
                    env->vfp.reg[rz].udspi[i] = round_shr_u32(a, -b);
                }
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                int64_t b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].dspc[8 * i];
                if (b > 0) {
                    env->vfp.reg[rz].udspl[i] = sat_shl_u64(a, b);
                } else {
                    env->vfp.reg[rz].udspl[i] = round_shr_u64(a, -b);
                }
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vshl)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            uint8_t a, b;
            a = env->vfp.reg[rx].udspc[i];
            b = env->vfp.reg[ry].udspc[i];
            env->vfp.reg[rz].udspc[i] = shl_8(a, b);
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            uint16_t a, b;
            a = env->vfp.reg[rx].udsps[i];
            b = env->vfp.reg[ry].udspc[2 * i];
            env->vfp.reg[rz].udsps[i] = shl_16(a, b);
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            uint32_t a, b;
            a = env->vfp.reg[rx].udspi[i];
            b = env->vfp.reg[ry].udspc[4 * i];
            env->vfp.reg[rz].udspi[i] = shl_32(a, b);
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            uint64_t a, b;
            a = env->vfp.reg[rx].udspl[i];
            b = env->vfp.reg[ry].udspc[8 * i];
            env->vfp.reg[rz].udspl[i] = shl_64(a, b);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vshl_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a;
                uint8_t b;
                a = env->vfp.reg[rx].dspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].dspc[i] = sat_shl_s8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a;
                uint16_t b;
                a = env->vfp.reg[rx].dsps[i];
                b = env->vfp.reg[ry].udspc[2 * i];
                env->vfp.reg[rz].dsps[i] = sat_shl_s16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a;
                uint32_t b;
                a = env->vfp.reg[rx].dspi[i];
                b = env->vfp.reg[ry].udspc[4 * i];
                env->vfp.reg[rz].dspi[i] = sat_shl_s32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a;
                uint64_t b;
                a = env->vfp.reg[rx].dspl[i];
                b = env->vfp.reg[ry].udspc[8 * i];
                env->vfp.reg[rz].dspl[i] = sat_shl_s64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                uint8_t b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = sat_shl_u8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                uint16_t b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].udspc[2 * i];
                env->vfp.reg[rz].udsps[i] = sat_shl_u16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                uint32_t b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].udspc[4 * i];
                env->vfp.reg[rz].udspi[i] = sat_shl_u32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                uint64_t b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].udspc[8 * i];
                env->vfp.reg[rz].udspl[i] = sat_shl_u64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vshli)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, rx, rz, imm;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    number = 128 / width;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            uint8_t a;
            a = env->vfp.reg[rx].udspc[i];
            env->vfp.reg[rz].udspc[i] = shl_8(a, imm);
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            uint16_t a;
            a = env->vfp.reg[rx].udsps[i];
            env->vfp.reg[rz].udsps[i] = shl_16(a, imm);
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            uint32_t a;
            a = env->vfp.reg[rx].udspi[i];
            env->vfp.reg[rz].udspi[i] = shl_32(a, imm);
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            uint64_t a;
            a = env->vfp.reg[rx].udspl[i];
            env->vfp.reg[rz].udspl[i] = shl_64(a, imm);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vshli_s)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, rx, rz, imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    number = 128 / width;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a;
                a = env->vfp.reg[rx].dspc[i];
                env->vfp.reg[rz].dspc[i] = sat_shl_s8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a;
                a = env->vfp.reg[rx].dsps[i];
                env->vfp.reg[rz].dsps[i] = sat_shl_s16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a;
                a = env->vfp.reg[rx].dspi[i];
                env->vfp.reg[rz].dspi[i] = sat_shl_s32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a;
                a = env->vfp.reg[rx].dspl[i];
                env->vfp.reg[rz].dspl[i] = sat_shl_s64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                a = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] = sat_shl_u8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                a = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] = sat_shl_u16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                a = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] = sat_shl_u32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                a = env->vfp.reg[rx].udspl[i];
                env->vfp.reg[rz].udspl[i] = sat_shl_u64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vshli_e)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width, sign, rx, rz, imm;
    union VDSP tmp1, tmp2;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    switch (imm & 0x70) {
    case 0x30:
        width = 8;
        imm = imm & 0xf;
        break;
    case 0x10:
        width = 16;
        imm = imm & 0xf;
        break;
    case 0x40:
        width = 16;
        imm = (imm & 0xf) + 16;
        break;
    case 0x00:
        width = 32;
        imm = imm & 0xf;
        break;
    case 0x70:
        width = 32;
        imm = (imm & 0xf) + 16;
        break;
    case 0x60:
        width = 32;
        imm = (imm & 0xf) + 32;
        break;
    case 0x50:
        width = 32;
        imm = (imm & 0xf) + 48;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    number = 128 / width;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                int16_t a;
                a = env->vfp.reg[rx].dspc[i];
                tmp1.dsps[i] = shl_16(a, imm);

                a = env->vfp.reg[rx].dspc[i + number / 2];
                tmp2.dsps[i] = shl_16(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                int32_t a;
                a = env->vfp.reg[rx].dsps[i];
                tmp1.dspi[i] = shl_32(a, imm);

                a = env->vfp.reg[rx].dsps[i + number / 2];
                tmp2.dspi[i] = shl_32(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int64_t a;
                a = env->vfp.reg[rx].dspi[i];
                tmp1.dspl[i] = shl_64(a, imm);

                a = env->vfp.reg[rx].dspi[i + number / 2];
                tmp2.dspl[i] = shl_64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                uint16_t a;
                a = env->vfp.reg[rx].udspc[i];
                tmp1.udsps[i] = shl_16(a, imm);

                a = env->vfp.reg[rx].udspc[i + number / 2];
                tmp2.udsps[i] = shl_16(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint32_t a;
                a = env->vfp.reg[rx].udsps[i];
                tmp1.udspi[i] = shl_32(a, imm);

                a = env->vfp.reg[rx].udsps[i + number / 2];
                tmp2.udspi[i] = shl_32(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint64_t a;
                a = env->vfp.reg[rx].udspi[i];
                tmp1.udspl[i] = shl_64(a, imm);

                a = env->vfp.reg[rx].udspi[i + number / 2];
                tmp2.udspl[i] = shl_64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vshr)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a;
                uint8_t b;
                a = env->vfp.reg[rx].dspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].dspc[i] = shr_s8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a;
                uint16_t b;
                a = env->vfp.reg[rx].dsps[i];
                b = env->vfp.reg[ry].udspc[2 * i];
                env->vfp.reg[rz].dsps[i] = shr_s16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a;
                uint32_t b;
                a = env->vfp.reg[rx].dspi[i];
                b = env->vfp.reg[ry].udspc[4 * i];
                env->vfp.reg[rz].dspi[i] = shr_s32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a;
                uint64_t b;
                a = env->vfp.reg[rx].dspl[i];
                b = env->vfp.reg[ry].udspc[8 * i];
                env->vfp.reg[rz].dspl[i] = shr_s64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                uint8_t b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = shr_u8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                uint16_t b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].udspc[2 * i];
                env->vfp.reg[rz].udsps[i] = shr_u16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                uint32_t b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].udspc[4 * i];
                env->vfp.reg[rz].udspi[i] = shr_u32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                uint64_t b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].udspc[8 * i];
                env->vfp.reg[rz].udspl[i] = shr_u64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vshr_r)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, sign, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a;
                uint8_t b;
                a = env->vfp.reg[rx].dspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].dspc[i] = round_shr_s8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a;
                uint16_t b;
                a = env->vfp.reg[rx].dsps[i];
                b = env->vfp.reg[ry].udspc[2 * i];
                env->vfp.reg[rz].dsps[i] = round_shr_s16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a;
                uint32_t b;
                a = env->vfp.reg[rx].dspi[i];
                b = env->vfp.reg[ry].udspc[4 * i];
                env->vfp.reg[rz].dspi[i] = round_shr_s32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a;
                uint64_t b;
                a = env->vfp.reg[rx].dspl[i];
                b = env->vfp.reg[ry].udspc[8 * i];
                env->vfp.reg[rz].dspl[i] = round_shr_s64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                uint8_t b;
                a = env->vfp.reg[rx].udspc[i];
                b = env->vfp.reg[ry].udspc[i];
                env->vfp.reg[rz].udspc[i] = round_shr_u8(a, b);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                uint16_t b;
                a = env->vfp.reg[rx].udsps[i];
                b = env->vfp.reg[ry].udspc[2 * i];
                env->vfp.reg[rz].udsps[i] = round_shr_u16(a, b);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                uint32_t b;
                a = env->vfp.reg[rx].udspi[i];
                b = env->vfp.reg[ry].udspc[4 * i];
                env->vfp.reg[rz].udspi[i] = round_shr_u32(a, b);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                uint64_t b;
                a = env->vfp.reg[rx].udspl[i];
                b = env->vfp.reg[ry].udspc[8 * i];
                env->vfp.reg[rz].udspl[i] = round_shr_u64(a, b);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vshri)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, rx, rz, imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    imm++;  /* oimm */
    number = 128 / width;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a;
                a = env->vfp.reg[rx].dspc[i];
                env->vfp.reg[rz].dspc[i] = shr_s8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a;
                a = env->vfp.reg[rx].dsps[i];
                env->vfp.reg[rz].dsps[i] = shr_s16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a;
                a = env->vfp.reg[rx].dspi[i];
                env->vfp.reg[rz].dspi[i] = shr_s32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a;
                a = env->vfp.reg[rx].dspl[i];
                env->vfp.reg[rz].dspl[i] = shr_s64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                a = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] = shr_u8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                a = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] = shr_u16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                a = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] = shr_u32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                a = env->vfp.reg[rx].udspl[i];
                env->vfp.reg[rz].udspl[i] = shr_u64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vshri_r)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, rx, rz, imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    imm++;  /* oimm */
    number = 128 / width;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a;
                a = env->vfp.reg[rx].dspc[i];
                env->vfp.reg[rz].dspc[i] = round_shr_s8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a;
                a = env->vfp.reg[rx].dsps[i];
                env->vfp.reg[rz].dsps[i] = round_shr_s16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a;
                a = env->vfp.reg[rx].dspi[i];
                env->vfp.reg[rz].dspi[i] = round_shr_s32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a;
                a = env->vfp.reg[rx].dspl[i];
                env->vfp.reg[rz].dspl[i] = round_shr_s64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                a = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] = round_shr_u8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                a = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] = round_shr_u16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                a = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] = round_shr_u32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                a = env->vfp.reg[rx].udspl[i];
                env->vfp.reg[rz].udspl[i] = round_shr_u64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vshri_l)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, rx, rz, imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    imm++;  /* oimm */
    number = 128 / width;

    if (sign) {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[rx].dsps[2 * i + 1];
                a = shr_s16(a, imm);
                b = shr_s16(b, imm);
                env->vfp.reg[rz].dsps[i] = (a & 0xff) | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[rx].dspi[2 * i + 1];
                a = shr_s32(a, imm);
                b = shr_s32(b, imm);
                env->vfp.reg[rz].dspi[i] = (a & 0xffff) | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspl[2 * i];
                b = env->vfp.reg[rx].dspl[2 * i + 1];
                a = shr_s64(a, imm);
                b = shr_s64(b, imm);
                env->vfp.reg[rz].dspl[i] = (a & 0xffffffff) | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[rx].udsps[2 * i + 1];
                a = shr_u16(a, imm);
                b = shr_u16(b, imm);
                env->vfp.reg[rz].udsps[i] = (a & 0xff) | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[rx].udspi[2 * i + 1];
                a = shr_u32(a, imm);
                b = shr_u32(b, imm);
                env->vfp.reg[rz].udspi[i] = (a & 0xffff) | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspl[2 * i];
                b = env->vfp.reg[rx].udspl[2 * i + 1];
                a = shr_u64(a, imm);
                b = shr_u64(b, imm);
                env->vfp.reg[rz].udspl[i] = (a & 0xffffffff) | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    env->vfp.reg[rz].dspl[1] = 0;
}

void VDSP2_HELPER(vshri_lr)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, rx, rz, imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    imm++;  /* oimm */
    number = 128 / width;

    if (sign) {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[rx].dsps[2 * i + 1];
                a = round_shr_s16(a, imm);
                b = round_shr_s16(b, imm);
                env->vfp.reg[rz].dsps[i] = (a & 0xff) | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[rx].dspi[2 * i + 1];
                a = round_shr_s32(a, imm);
                b = round_shr_s32(b, imm);
                env->vfp.reg[rz].dspi[i] = (a & 0xffff) | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspl[2 * i];
                b = env->vfp.reg[rx].dspl[2 * i + 1];
                a = round_shr_s64(a, imm);
                b = round_shr_s64(b, imm);
                env->vfp.reg[rz].dspl[i] = (a & 0xffffffff) | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[rx].udsps[2 * i + 1];
                a = round_shr_u16(a, imm);
                b = round_shr_u16(b, imm);
                env->vfp.reg[rz].udsps[i] = (a & 0xff) | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[rx].udspi[2 * i + 1];
                a = round_shr_u32(a, imm);
                b = round_shr_u32(b, imm);
                env->vfp.reg[rz].udspi[i] = (a & 0xffff) | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspl[2 * i];
                b = env->vfp.reg[rx].udspl[2 * i + 1];
                a = round_shr_u64(a, imm);
                b = round_shr_u64(b, imm);
                env->vfp.reg[rz].udspl[i] = (a & 0xffffffff) | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    env->vfp.reg[rz].dspl[1] = 0;
}

void VDSP2_HELPER(vshri_ls)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, rx, rz, imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    imm++;  /* oimm */
    number = 128 / width;

    if (sign) {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[rx].dsps[2 * i + 1];
                a = low_sat_shr_s16(a, imm);
                b = low_sat_shr_s16(b, imm);
                env->vfp.reg[rz].dsps[i] = (a & 0xff) | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[rx].dspi[2 * i + 1];
                a = low_sat_shr_s32(a, imm);
                b = low_sat_shr_s32(b, imm);
                env->vfp.reg[rz].dspi[i] = (a & 0xffff) | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspl[2 * i];
                b = env->vfp.reg[rx].dspl[2 * i + 1];
                a = low_sat_shr_s64(a, imm);
                b = low_sat_shr_s64(b, imm);
                env->vfp.reg[rz].dspl[i] = (a & 0xffffffff) | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[rx].udsps[2 * i + 1];
                a = low_sat_shr_u16(a, imm);
                b = low_sat_shr_u16(b, imm);
                env->vfp.reg[rz].udsps[i] = (a & 0xff) | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[rx].udspi[2 * i + 1];
                a = low_sat_shr_u32(a, imm);
                b = low_sat_shr_u32(b, imm);
                env->vfp.reg[rz].udspi[i] = (a & 0xffff) | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspl[2 * i];
                b = env->vfp.reg[rx].udspl[2 * i + 1];
                a = low_sat_shr_u64(a, imm);
                b = low_sat_shr_u64(b, imm);
                env->vfp.reg[rz].udspl[i] = (a & 0xffffffff) | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    env->vfp.reg[rz].dspl[1] = 0;
}

void VDSP2_HELPER(vshri_lrs)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, rx, rz, imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    imm++;  /* oimm */
    number = 128 / width;

    if (sign) {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                int16_t a, b;
                a = env->vfp.reg[rx].dsps[2 * i];
                b = env->vfp.reg[rx].dsps[2 * i + 1];
                a = low_round_sat_shr_s16(a, imm);
                b = low_round_sat_shr_s16(b, imm);
                env->vfp.reg[rz].dsps[i] = (a & 0xff) | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                int32_t a, b;
                a = env->vfp.reg[rx].dspi[2 * i];
                b = env->vfp.reg[rx].dspi[2 * i + 1];
                a = low_round_sat_shr_s32(a, imm);
                b = low_round_sat_shr_s32(b, imm);
                env->vfp.reg[rz].dspi[i] = (a & 0xffff) | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                int64_t a, b;
                a = env->vfp.reg[rx].dspl[2 * i];
                b = env->vfp.reg[rx].dspl[2 * i + 1];
                a = low_round_sat_shr_s64(a, imm);
                b = low_round_sat_shr_s64(b, imm);
                env->vfp.reg[rz].dspl[i] = (a & 0xffffffff) | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = env->vfp.reg[rx].udsps[2 * i];
                b = env->vfp.reg[rx].udsps[2 * i + 1];
                a = low_round_sat_shr_u16(a, imm);
                b = low_round_sat_shr_u16(b, imm);
                env->vfp.reg[rz].udsps[i] = (a & 0xff) | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = env->vfp.reg[rx].udspi[2 * i];
                b = env->vfp.reg[rx].udspi[2 * i + 1];
                a = low_round_sat_shr_u32(a, imm);
                b = low_round_sat_shr_u32(b, imm);
                env->vfp.reg[rz].udspi[i] = (a & 0xffff) | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = env->vfp.reg[rx].udspl[2 * i];
                b = env->vfp.reg[rx].udspl[2 * i + 1];
                a = low_round_sat_shr_u64(a, imm);
                b = low_round_sat_shr_u64(b, imm);
                env->vfp.reg[rz].udspl[i] = (a & 0xffffffff) | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    env->vfp.reg[rz].dspl[1] = 0;
}

void VDSP2_HELPER(vshria)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, rx, rz, imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    imm++;  /* oimm */
    number = 128 / width;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a;
                a = env->vfp.reg[rx].dspc[i];
                env->vfp.reg[rz].dspc[i] += shr_s8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a;
                a = env->vfp.reg[rx].dsps[i];
                env->vfp.reg[rz].dsps[i] += shr_s16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a;
                a = env->vfp.reg[rx].dspi[i];
                env->vfp.reg[rz].dspi[i] += shr_s32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a;
                a = env->vfp.reg[rx].dspl[i];
                env->vfp.reg[rz].dspl[i] += shr_s64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                a = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] += shr_u8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                a = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] += shr_u16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                a = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] += shr_u32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                a = env->vfp.reg[rx].udspl[i];
                env->vfp.reg[rz].udspl[i] += shr_u64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vshria_r)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t width = 0, sign, rx, rz, imm;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = ((insn >> 19) & 0x7e) + ((insn >> 5) & 0x1);

    decode_imm7(env, &imm, &width);
    imm++;  /* oimm */
    number = 128 / width;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                int8_t a;
                a = env->vfp.reg[rx].dspc[i];
                env->vfp.reg[rz].dspc[i] += round_shr_s8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                int16_t a;
                a = env->vfp.reg[rx].dsps[i];
                env->vfp.reg[rz].dsps[i] += round_shr_s16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                int32_t a;
                a = env->vfp.reg[rx].dspi[i];
                env->vfp.reg[rz].dspi[i] += round_shr_s32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                int64_t a;
                a = env->vfp.reg[rx].dspl[i];
                env->vfp.reg[rz].dspl[i] += round_shr_s64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number; i++) {
                uint8_t a;
                a = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] += round_shr_u8(a, imm);
            }
            break;
        case 16:
            for (i = 0; i < number; i++) {
                uint16_t a;
                a = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] += round_shr_u16(a, imm);
            }
            break;
        case 32:
            for (i = 0; i < number; i++) {
                uint32_t a;
                a = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] += round_shr_u32(a, imm);
            }
            break;
        case 64:
            for (i = 0; i < number; i++) {
                uint64_t a;
                a = env->vfp.reg[rx].udspl[i];
                env->vfp.reg[rz].udspl[i] += round_shr_u64(a, imm);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vexh)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz, ry, imm1, imm2;

    size = ((insn >> 4) & 0x1) + ((insn >> 20) & 0x1) * 2;
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_REG_SHI_RY) & CSKY_VDSP2_REG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    imm1 = env->regs[ry] & 0x3f;
    imm2 = (env->regs[ry] >> 6) & 0x3f;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            uint32_t a, b;
            a = env->vfp.reg[rx].udspc[i];
            b = env->vfp.reg[rz].udspc[i];
            a = (a >> imm1) << (8 - imm2 + imm1 - 1);
            b = (b << (24 + imm2 - imm1 + 1)) >> (24 + imm2 - imm1 + 1);
            env->vfp.reg[rz].udspc[i] = a | b;
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            uint32_t a, b;
            a = env->vfp.reg[rx].udsps[i];
            b = env->vfp.reg[rz].udsps[i];
            a = (a >> imm1) << (16 - imm2 + imm1 - 1);
            b = (b << (16 + imm2 - imm1 + 1)) >> (16 + imm2 - imm1 + 1);
            env->vfp.reg[rz].udsps[i] = a | b;
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            uint32_t a, b;
            a = env->vfp.reg[rx].udspi[i];
            b = env->vfp.reg[rz].udspi[i];
            a = (a >> imm1) << (32 - imm2 + imm1 - 1);
            b = (b << (imm2 - imm1 + 1)) >> (imm2 - imm1 + 1);
            env->vfp.reg[rz].udspi[i] = a | b;
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            uint64_t a, b;
            a = env->vfp.reg[rx].udspl[i];
            b = env->vfp.reg[rz].udspl[i];
            a = (a >> imm1) << (64 - imm2 + imm1 - 1);
            b = (b << (imm2 - imm1 + 1)) >> (imm2 - imm1 + 1);
            env->vfp.reg[rz].udspl[i] = a | b;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vexl)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz, ry, imm1, imm2;

    size = ((insn >> 4) & 0x1) + ((insn >> 20) & 0x1) * 2;
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_REG_SHI_RY) & CSKY_VDSP2_REG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    imm1 = env->regs[ry] & 0x3f;
    imm2 = (env->regs[ry] >> 6) & 0x3f;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            uint32_t a, b;
            a = env->vfp.reg[rx].udspc[i];
            b = env->vfp.reg[rz].udspc[i];
            a = (a << (32 - imm2 - 1)) >> (32 - imm2 + imm1 - 1);
            b = (b >> (imm2 - imm1 + 1)) << (imm2 - imm1 + 1);
            env->vfp.reg[rz].udspc[i] = a | b;
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            uint32_t a, b;
            a = env->vfp.reg[rx].udsps[i];
            b = env->vfp.reg[rz].udsps[i];
            a = (a << (32 - imm2 - 1)) >> (32 - imm2 + imm1 - 1);
            b = (b >> (imm2 - imm1 + 1)) << (imm2 - imm1 + 1);
            env->vfp.reg[rz].udsps[i] = a | b;
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            uint32_t a, b;
            a = env->vfp.reg[rx].udspi[i];
            b = env->vfp.reg[rz].udspi[i];
            a = (a << (32 - imm2 - 1)) >> (32 - imm2 + imm1 - 1);
            b = (b >> (imm2 - imm1 + 1)) << (imm2 - imm1 + 1);
            env->vfp.reg[rz].udspi[i] = a | b;
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            uint64_t a, b;
            a = env->vfp.reg[rx].udspl[i];
            b = env->vfp.reg[rz].udspl[i];
            a = (a << (64 - imm2 - 1)) >> (64 - imm2 + imm1 - 1);
            b = (b >> (imm2 - imm1 + 1)) << (imm2 - imm1 + 1);
            env->vfp.reg[rz].udspl[i] = a | b;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

/* MOVE/SWAP/BIT instructions. */
static inline uint16_t mov_sat_low_u16(uint16_t x)
{
    uint16_t res;
    if (x > MAX_U8) {
        res = MAX_U8;
    } else {
        res = x;
    }
    return res;
}

static inline uint16_t mov_sat_low_s16(int16_t x)
{
    uint16_t res;
    if (x > (int16_t)MAX_S8) {
        res = (uint8_t)MAX_S8;
    } else if (x < (int16_t)MIN_S8) {
        res = (uint8_t)MIN_S8;
    } else {
        res = (uint8_t)x;
    }
    return res;
}

static inline uint32_t mov_sat_low_u32(uint32_t x)
{
    uint32_t res;
    if (x > MAX_U16) {
        res = MAX_U16;
    } else {
        res = x;
    }
    return res;
}

static inline uint32_t mov_sat_low_s32(int32_t x)
{
    uint32_t res;
    if (x > (int32_t)MAX_S16) {
        res = (uint16_t)MAX_S16;
    } else if (x < (int32_t)MIN_S16) {
        res = (uint16_t)MIN_S16;
    } else {
        res = (uint16_t)x;
    }
    return res;
}

static inline uint64_t mov_sat_low_u64(uint64_t x)
{
    uint64_t res;
    if (x > MAX_U32) {
        res = MAX_U32;
    } else {
        res = x;
    }
    return res;
}

static inline uint64_t mov_sat_low_s64(int64_t x)
{
    uint64_t res;
    if (x > (int64_t)MAX_S32) {
        res = (uint32_t)MAX_S32;
    } else if (x < (int64_t)MIN_S32) {
        res = (uint32_t)MIN_S32;
    } else {
        res = (uint32_t)x;
    }
    return res;
}

void VDSP2_HELPER(vmtvr_1)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t size, width, rx, vz, idx;

    size = ((insn >> 4) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;

    switch (width) {
    case 8:
        env->vfp.reg[vz].udspc[idx] = (uint8_t)env->regs[rx];
        break;
    case 16:
        env->vfp.reg[vz].udsps[idx] = (uint16_t)env->regs[rx];
        break;
    case 32:
        env->vfp.reg[vz].udspi[idx] = (uint32_t)env->regs[rx];
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vmtvr_2)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t size, width, rx, vz, idx;

    size = ((insn >> 4) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;

    switch (width) {
    case 8:
        env->vfp.reg[vz].udspc[idx] = (uint8_t)env->regs[rx];
        env->vfp.reg[vz].udspc[idx + 1] = (uint8_t)env->regs[rx + 1];
        break;
    case 16:
        env->vfp.reg[vz].udsps[idx] = (uint16_t)env->regs[rx];
        env->vfp.reg[vz].udsps[idx + 1] = (uint16_t)env->regs[rx + 1];
        break;
    case 32:
        env->vfp.reg[vz].udspi[idx] = (uint32_t)env->regs[rx];
        env->vfp.reg[vz].udspi[idx + 1] = (uint32_t)env->regs[rx + 1];
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vmfvr)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t size, width, sign, vx, rz, idx;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;

    sign = (insn >> 5) & 0x1;
    vx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_REG_SHI_RZ) & CSKY_VDSP2_REG_MASK;
    idx = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;

    if (sign) {
        switch (width) {
        case 8:
            env->regs[rz] = (int32_t)env->vfp.reg[vx].dspc[idx];
            break;
        case 16:
            env->regs[rz] = (int32_t)env->vfp.reg[vx].dsps[idx];
            break;
        case 32:
            env->regs[rz] = (int32_t)env->vfp.reg[vx].dspi[idx];
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            env->regs[rz] = (uint32_t)env->vfp.reg[vx].udspc[idx];
            break;
        case 16:
            env->regs[rz] = (uint32_t)env->vfp.reg[vx].udsps[idx];
            break;
        case 32:
            env->regs[rz] = (uint32_t)env->vfp.reg[vx].udspi[idx];
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
}

void VDSP2_HELPER(vsext)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, number;
    uint32_t size, width, vrx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    vrx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_REG_SHI_RZ) & CSKY_VDSP2_REG_MASK;
    env->regs[rz] = 0;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[vrx].dspc[i] < 0) {
                env->regs[rz] |= 1 << i;
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[vrx].dsps[i] < 0) {
                env->regs[rz] |= 1 << i;
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            if (env->vfp.reg[vrx].dspi[i] < 0) {
                env->regs[rz] |= 1 << i;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vmov_e)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, number;
    uint32_t size, width, sign, rx, rz;
    union VDSP tmp1, tmp2;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                tmp1.dsps[i] = env->vfp.reg[rx].dspc[i];
                tmp2.dsps[i] = env->vfp.reg[rx].dspc[i + number / 2];
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                tmp1.dspi[i] = env->vfp.reg[rx].dsps[i];
                tmp2.dspi[i] = env->vfp.reg[rx].dsps[i + number / 2];
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                tmp1.dspl[i] = env->vfp.reg[rx].dspi[i];
                tmp2.dspl[i] = env->vfp.reg[rx].dspi[i + number / 2];
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 8:
            for (i = 0; i < number / 2; i++) {
                tmp1.udsps[i] = env->vfp.reg[rx].udspc[i];
                tmp2.udsps[i] = env->vfp.reg[rx].udspc[i + number / 2];
            }
            break;
        case 16:
            for (i = 0; i < number / 2; i++) {
                tmp1.udspi[i] = env->vfp.reg[rx].udsps[i];
                tmp2.udspi[i] = env->vfp.reg[rx].udsps[i + number / 2];
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                tmp1.udspl[i] = env->vfp.reg[rx].udspi[i];
                tmp2.udspl[i] = env->vfp.reg[rx].udspi[i + number / 2];
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vmov_l)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, number;
    uint32_t size, width, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 16:
        for (i = 0; i < number / 2; i++) {
            uint16_t a, b;
            a = env->vfp.reg[rx].udspc[4 * i];
            b = env->vfp.reg[rx].udspc[4 * i + 2];
            tmp.udsps[i] = a | (b << 8);
            a = env->vfp.reg[ry].udspc[4 * i];
            b = env->vfp.reg[ry].udspc[4 * i + 2];
            tmp.udsps[i + number / 2] = a | (b << 8);
        }
        break;
    case 32:
        for (i = 0; i < number / 2; i++) {
            uint32_t a, b;
            a = env->vfp.reg[rx].udsps[4 * i];
            b = env->vfp.reg[rx].udsps[4 * i + 2];
            tmp.udspi[i] = a | (b << 16);
            a = env->vfp.reg[ry].udsps[4 * i];
            b = env->vfp.reg[ry].udsps[4 * i + 2];
            tmp.udspi[i + number / 2] = a | (b << 16);
        }
        break;
    case 64:
        for (i = 0; i < number / 2; i++) {
            uint64_t a, b;
            a = env->vfp.reg[rx].udspi[4 * i];
            b = env->vfp.reg[rx].udspi[4 * i + 2];
            tmp.udspl[i] = a | (b << 32);
            a = env->vfp.reg[ry].udspi[4 * i];
            b = env->vfp.reg[ry].udspi[4 * i + 2];
            tmp.udspl[i + number / 2] = a | (b << 32);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vmov_h)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, number;
    uint32_t size, width, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 16:
        for (i = 0; i < number / 2; i++) {
            uint16_t a, b;
            a = env->vfp.reg[rx].udspc[4 * i + 1];
            b = env->vfp.reg[rx].udspc[4 * i + 3];
            tmp.udsps[i] = a | (b << 8);
            a = env->vfp.reg[ry].udspc[4 * i + 1];
            b = env->vfp.reg[ry].udspc[4 * i + 3];
            tmp.udsps[i + number / 2] = a | (b << 8);
        }
        break;
    case 32:
        for (i = 0; i < number / 2; i++) {
            uint32_t a, b;
            a = env->vfp.reg[rx].udsps[4 * i + 1];
            b = env->vfp.reg[rx].udsps[4 * i + 3];
            tmp.udspi[i] = a | (b << 16);
            a = env->vfp.reg[ry].udsps[4 * i + 1];
            b = env->vfp.reg[ry].udsps[4 * i + 3];
            tmp.udspi[i + number / 2] = a | (b << 16);
        }
        break;
    case 64:
        for (i = 0; i < number / 2; i++) {
            uint64_t a, b;
            a = env->vfp.reg[rx].udspi[4 * i + 1];
            b = env->vfp.reg[rx].udspi[4 * i + 3];
            tmp.udspl[i] = a | (b << 32);
            a = env->vfp.reg[ry].udspi[4 * i + 1];
            b = env->vfp.reg[ry].udspi[4 * i + 3];
            tmp.udspl[i + number / 2] = a | (b << 32);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vmov_sl)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, number;
    uint32_t size, width, sign, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    if (sign) {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = mov_sat_low_s16(env->vfp.reg[rx].dsps[2 * i]);
                b = mov_sat_low_s16(env->vfp.reg[rx].dsps[2 * i + 1]);
                tmp.udsps[i] = a | (b << 8);
                a = mov_sat_low_s16(env->vfp.reg[ry].dsps[2 * i]);
                b = mov_sat_low_s16(env->vfp.reg[ry].dsps[2 * i + 1]);
                tmp.udsps[i + number / 2] = a | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = mov_sat_low_s32(env->vfp.reg[rx].dspi[2 * i]);
                b = mov_sat_low_s32(env->vfp.reg[rx].dspi[2 * i + 1]);
                tmp.udspi[i] = a | (b << 16);
                a = mov_sat_low_s32(env->vfp.reg[ry].dspi[2 * i]);
                b = mov_sat_low_s32(env->vfp.reg[ry].dspi[2 * i + 1]);
                tmp.udspi[i + number / 2] = a | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = mov_sat_low_s64(env->vfp.reg[rx].dspl[2 * i]);
                b = mov_sat_low_s64(env->vfp.reg[rx].dspl[2 * i + 1]);
                tmp.udspl[i] = a | (b << 32);
                a = mov_sat_low_s64(env->vfp.reg[ry].dspl[2 * i]);
                b = mov_sat_low_s64(env->vfp.reg[ry].dspl[2 * i + 1]);
                tmp.udspl[i + number / 2] = a | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        switch (width) {
        case 16:
            for (i = 0; i < number / 2; i++) {
                uint16_t a, b;
                a = mov_sat_low_u16(env->vfp.reg[rx].udsps[2 * i]);
                b = mov_sat_low_u16(env->vfp.reg[rx].udsps[2 * i + 1]);
                tmp.udsps[i] = a | (b << 8);
                a = mov_sat_low_u16(env->vfp.reg[ry].udsps[2 * i]);
                b = mov_sat_low_u16(env->vfp.reg[ry].udsps[2 * i + 1]);
                tmp.udsps[i + number / 2] = a | (b << 8);
            }
            break;
        case 32:
            for (i = 0; i < number / 2; i++) {
                uint32_t a, b;
                a = mov_sat_low_u32(env->vfp.reg[rx].udspi[2 * i]);
                b = mov_sat_low_u32(env->vfp.reg[rx].udspi[2 * i + 1]);
                tmp.udspi[i] = a | (b << 16);
                a = mov_sat_low_u32(env->vfp.reg[ry].udspi[2 * i]);
                b = mov_sat_low_u32(env->vfp.reg[ry].udspi[2 * i + 1]);
                tmp.udspi[i + number / 2] = a | (b << 16);
            }
            break;
        case 64:
            for (i = 0; i < number / 2; i++) {
                uint64_t a, b;
                a = mov_sat_low_u64(env->vfp.reg[rx].udspl[2 * i]);
                b = mov_sat_low_u64(env->vfp.reg[rx].udspl[2 * i + 1]);
                tmp.udspl[i] = a | (b << 32);
                a = mov_sat_low_u64(env->vfp.reg[ry].udspl[2 * i]);
                b = mov_sat_low_u64(env->vfp.reg[ry].udspl[2 * i + 1]);
                tmp.udspl[i + number / 2] = a | (b << 32);
            }
            break;
        default:
            helper_exception(env, EXCP_CSKY_UDEF);
            return;
        }
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vmov_rh)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, number;
    uint32_t size, width, rx, ry, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 16:
        for (i = 0; i < number / 2; i++) {
            uint16_t a, b;
            a = (env->vfp.reg[rx].udsps[2 * i] + (1 << 7)) >> 8;
            b = (env->vfp.reg[rx].udsps[2 * i + 1] + (1 << 7)) >> 8;
            tmp.udsps[i] = a | (b << 8);
            a = (env->vfp.reg[ry].udsps[2 * i] + (1 << 7)) >> 8;
            b = (env->vfp.reg[ry].udsps[2 * i + 1] + (1 << 7)) >> 8;
            tmp.udsps[i + number / 2] = a | (b << 8);
        }
        break;
    case 32:
        for (i = 0; i < number / 2; i++) {
            uint32_t a, b;
            a = (env->vfp.reg[rx].udspi[2 * i] + (1 << 15)) >> 16;
            b = (env->vfp.reg[rx].udspi[2 * i + 1] + (1 << 15)) >> 16;
            tmp.udspi[i] = a | (b << 16);
            a = (env->vfp.reg[ry].udspi[2 * i] + (1 << 15)) >> 16;
            b = (env->vfp.reg[ry].udspi[2 * i + 1] + (1 << 15)) >> 16;
            tmp.udspi[i + number / 2] = a | (b << 16);
        }
        break;
    case 64:
        for (i = 0; i < number / 2; i++) {
            uint64_t a, b;
            a = (env->vfp.reg[rx].udspl[2 * i] + ((uint64_t)1 << 31)) >> 32;
            b = (env->vfp.reg[rx].udspl[2 * i + 1] + ((uint64_t)1 << 31)) >> 32;
            tmp.udspl[i] = a | (b << 32);
            a = (env->vfp.reg[ry].udspl[2 * i] + ((uint64_t)1 << 31)) >> 32;
            b = (env->vfp.reg[ry].udspl[2 * i + 1] + ((uint64_t)1 << 31)) >> 32;
            tmp.udspl[i + number / 2] = a | (b << 32);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vswp)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            tmp.dspc[i] = env->vfp.reg[rz].dspc[i];
            env->vfp.reg[rz].dspc[i] = env->vfp.reg[rx].dspc[i];
            env->vfp.reg[rx].dspc[i] = tmp.dspc[i];
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            tmp.dsps[i] = env->vfp.reg[rz].dsps[i];
            env->vfp.reg[rz].dsps[i] = env->vfp.reg[rx].dsps[i];
            env->vfp.reg[rx].dsps[i] = tmp.dsps[i];
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            tmp.dspi[i] = env->vfp.reg[rz].dspi[i];
            env->vfp.reg[rz].dspi[i] = env->vfp.reg[rx].dspi[i];
            env->vfp.reg[rx].dspi[i] = tmp.dspi[i];
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            tmp.dspl[i] = env->vfp.reg[rz].dspl[i];
            env->vfp.reg[rz].dspl[i] = env->vfp.reg[rx].dspl[i];
            env->vfp.reg[rx].dspl[i] = tmp.dspl[i];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vtrn)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;
    union VDSP tmp1, tmp2;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    number = number / 2;
    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            tmp1.dspc[2 * i + 1] = env->vfp.reg[ry].dspc[2 * i];
            tmp1.dspc[2 * i] = env->vfp.reg[rx].dspc[2 * i];

            tmp2.dspc[2 * i + 1] = env->vfp.reg[ry].dspc[2 * i + 1];
            tmp2.dspc[2 * i] = env->vfp.reg[rx].dspc[2 * i + 1];
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            tmp1.dsps[2 * i + 1] = env->vfp.reg[ry].dsps[2 * i];
            tmp1.dsps[2 * i] = env->vfp.reg[rx].dsps[2 * i];

            tmp2.dsps[2 * i + 1] = env->vfp.reg[ry].dsps[2 * i + 1];
            tmp2.dsps[2 * i] = env->vfp.reg[rx].dsps[2 * i + 1];
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            tmp1.dspi[2 * i + 1] = env->vfp.reg[ry].dspi[2 * i];
            tmp1.dspi[2 * i] = env->vfp.reg[rx].dspi[2 * i];

            tmp2.dspi[2 * i + 1] = env->vfp.reg[ry].dspi[2 * i + 1];
            tmp2.dspi[2 * i] = env->vfp.reg[rx].dspi[2 * i + 1];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
}

/* vrevq vrevh vrevw vrevd */
void VDSP2_HELPER(vrevx)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz;
    union VDSP tmp;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            tmp.dspc[number - i - 1] = env->vfp.reg[rx].dspc[i];
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            tmp.dsps[number - i - 1] = env->vfp.reg[rx].dsps[i];
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            tmp.dspi[number - i - 1] = env->vfp.reg[rx].dspi[i];
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            tmp.dspl[number - i - 1] = env->vfp.reg[rx].dspl[i];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vexti)(CPUCSKYState *env, uint32_t insn)
{
    int i;
    uint32_t rx, ry, rz, imm;
    union VDSP tmp;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    imm = ((insn >> 4) & 0xf);
    if (imm == 0xf) {
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    if (((insn >> 25) & 0x1) == 0) {
        for (i = 0; i <= imm; i++) {
            tmp.dspc[i] = env->vfp.reg[rx].dspc[i];
        }
    } else {
        for (i = 0; i <= imm; i++) {
            tmp.dspc[i] = env->vfp.reg[rx].dspc[i + 15 - imm];
        }
    }
    if (((insn >> 20) & 0x1) == 0) {
        for (i = imm + 1; i <= 15; i++) {
            tmp.dspc[i] = env->vfp.reg[ry].dspc[i - imm - 1];
        }
    } else {
        for (i = imm + 1; i <= 15; i++) {
            tmp.dspc[i] = env->vfp.reg[ry].dspc[i];
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vext)(CPUCSKYState *env, uint32_t insn)
{
    int i;
    uint32_t rx, ry, rz, rk, imm4, bit6, bit5;
    union VDSP tmp;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    rk = ((insn >> 4) & 0x7) + ((insn >> 20) & 0x1) * 8
        + ((insn >> 25) & 0x1) * 16;

    imm4 = env->regs[rk] & 0xf;
    bit6 = env->regs[rk] & 0x20;
    bit5 = env->regs[rk] & 0x10;

    if (imm4 == 0xf) {
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    if (bit6 == 0) {
        for (i = 0; i <= imm4; i++) {
            tmp.dspc[i] = env->vfp.reg[rx].dspc[i];
        }
    } else {
        for (i = 0; i <= imm4; i++) {
            tmp.dspc[i] = env->vfp.reg[rx].dspc[i + 15 - imm4];
        }
    }
    if (bit5 == 0) {
        for (i = imm4 + 1; i <= 15; i++) {
            tmp.dspc[i] = env->vfp.reg[ry].dspc[i - imm4 - 1];
        }
    } else {
        for (i = imm4 + 1; i <= 15; i++) {
            tmp.dspc[i] = env->vfp.reg[ry].dspc[i];
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vtbl)(CPUCSKYState *env, uint32_t insn)
{
    int i;
    uint32_t rx, ry, rz;
    union VDSP tmp;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    for (i = 0; i < 16; i++) {
        if (env->vfp.reg[ry].udspc[i] < 16) {
            tmp.udspc[i] = env->vfp.reg[rx].udspc[
                            env->vfp.reg[ry].udspc[i]];
        } else {
            tmp.udspc[i] = 0;
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vtbx)(CPUCSKYState *env, uint32_t insn)
{
    int i;
    uint32_t rx, ry, rz, sign;
    union VDSP tmp;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    if (sign) {
        for (i = 0; i < 16; i++) {
            if (env->vfp.reg[ry].dspc[i] < 16) {
                tmp.dspc[i] = env->vfp.reg[rx].dspc[env->vfp.reg[ry].dspc[i]];
            } else {
                tmp.dspc[i] = env->vfp.reg[rz].dspc[i];
            }
        }
    } else {
        for (i = 0; i < 16; i++) {
            if (env->vfp.reg[ry].udspc[i] < 16) {
                tmp.udspc[i] = env->vfp.reg[rx].udspc[
                                env->vfp.reg[ry].udspc[i]];
            } else {
                tmp.udspc[i] = env->vfp.reg[rz].udspc[i];
            }
        }
    }
    memcpy(&(env->vfp.reg[rz]), &tmp, sizeof(union VDSP));
}

void VDSP2_HELPER(vmovi)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t rz, imm, idx, sign;
    uint32_t width = 0;

    sign = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (((insn >> 21) & 0xf) << 4) + ((insn >> 16) & 0xf);
    idx = (((insn >> 25) & 0x1) << 2) + (((insn >> 20) & 0x1) << 1)
        + ((insn >> 6) & 0x1);

    if ((idx & 0x6) == 4) {
        width = 8;
    } else if ((idx & 0x6) == 6) {
        width = 16;
        idx &= 0x1;
    } else if ((idx & 0x6) == 2) {
        width = 32;
        idx &= 0x1;
    } else {
        width = 32;
        idx = (idx & 0x1) + 2;
    }
    number = 128 / width;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspc[i] = imm;
        }
        break;
    case 16:
        imm <<= (idx * 8);
        for (i = 0; i < number; i++) {
            if (sign) {
                env->vfp.reg[rz].dsps[i] = ~imm;
            } else {
                env->vfp.reg[rz].dsps[i] = imm;
            }
        }
        break;
    case 32:
        imm <<= (idx * 8);
        for (i = 0; i < number; i++) {
            if (sign) {
                env->vfp.reg[rz].dspi[i] = ~imm;
            } else {
                env->vfp.reg[rz].dspi[i] = imm;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vmaski)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rz, imm, high;

    high = (insn >> CSKY_VDSP2_SIGN_SHI) & CSKY_VDSP2_SIGN_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (((insn >> 21) & 0xf) << 4) + ((insn >> 16) & 0xf);

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    if (width == 8 && high == 0) {
        /* vmaski.8.l */
        env->vfp.reg[rz].dspl[1] = 0;
        for (i = 0; i < number / 2; i++) {
            if (imm & (1 << i)) {
                env->vfp.reg[rz].dspc[i] = 0xff;
            } else {
                env->vfp.reg[rz].dspc[i] = 0;
            }
        }
    } else if (width == 8 && high == 1) {
        /* vmaski.8.h */
        for (i = 0; i < number / 2; i++) {
            if (imm & (1 << i)) {
                env->vfp.reg[rz].dspc[i + 8] = 0xff;
            } else {
                env->vfp.reg[rz].dspc[i + 8] = 0;
            }
        }
    } else if (width == 16) {
        /* vmaski.16 */
        for (i = 0; i < number; i++) {
            if (imm & (1 << i)) {
                env->vfp.reg[rz].dsps[i] = 0xffff;
            } else {
                env->vfp.reg[rz].dsps[i] = 0;
            }
        }
    } else {
        helper_exception(env, EXCP_CSKY_UDEF);
    }
}

void VDSP2_HELPER(vand)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, rz;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    env->vfp.reg[rz].dspl[0] = (env->vfp.reg[rx].dspl[0]
                                & env->vfp.reg[ry].dspl[0]);
    env->vfp.reg[rz].dspl[1] = (env->vfp.reg[rx].dspl[1]
                                & env->vfp.reg[ry].dspl[1]);
}

void VDSP2_HELPER(vandn)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, rz;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    env->vfp.reg[rz].dspl[0] = (env->vfp.reg[rx].dspl[0]
                                & ~(env->vfp.reg[ry].dspl[0]));
    env->vfp.reg[rz].dspl[1] = (env->vfp.reg[rx].dspl[1]
                                & ~(env->vfp.reg[ry].dspl[1]));
}

void VDSP2_HELPER(vxor)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, rz;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    env->vfp.reg[rz].dspl[0] = (env->vfp.reg[rx].dspl[0]
                                ^ env->vfp.reg[ry].dspl[0]);
    env->vfp.reg[rz].dspl[1] = (env->vfp.reg[rx].dspl[1]
                                ^ env->vfp.reg[ry].dspl[1]);
}

void VDSP2_HELPER(vnot)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, rz;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    env->vfp.reg[rz].dspl[0] = ~(env->vfp.reg[rx].dspl[0]);
    env->vfp.reg[rz].dspl[1] = ~(env->vfp.reg[rx].dspl[1]);
}

void VDSP2_HELPER(vor)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, rz;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    env->vfp.reg[rz].dspl[0] = (env->vfp.reg[rx].dspl[0]
                                | env->vfp.reg[ry].dspl[0]);
    env->vfp.reg[rz].dspl[1] = (env->vfp.reg[rx].dspl[1]
                                | env->vfp.reg[ry].dspl[1]);
}

void VDSP2_HELPER(vorn)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, rz;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    env->vfp.reg[rz].dspl[0] = (env->vfp.reg[rx].dspl[0]
                                | ~(env->vfp.reg[ry].dspl[0]));
    env->vfp.reg[rz].dspl[1] = (env->vfp.reg[rx].dspl[1]
                                | ~(env->vfp.reg[ry].dspl[1]));
}

void VDSP2_HELPER(vsel)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, ry, rz, rk;
    uint64_t tmp1, tmp2;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    rk = ((insn >> 4) & 0x3) | (((insn >> 20) & 0x1) << 2)
        | (((insn >> 25) & 0x1) << 3);

    tmp1 = env->vfp.reg[rx].dspl[0] & env->vfp.reg[rk].dspl[0];
    tmp2 = env->vfp.reg[ry].dspl[0] & ~(env->vfp.reg[rk].dspl[0]);
    env->vfp.reg[rz].dspl[0] = tmp1 | tmp2;

    tmp1 = env->vfp.reg[rx].dspl[1] & env->vfp.reg[rk].dspl[1];
    tmp2 = env->vfp.reg[ry].dspl[1] & ~(env->vfp.reg[rk].dspl[1]);
    env->vfp.reg[rz].dspl[1] = tmp1 | tmp2;
}

void VDSP2_HELPER(vcls)(CPUCSKYState *env, uint32_t insn)
{
    int i, number, val;
    uint32_t size, width, rx, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            val = env->vfp.reg[rx].dspc[i];
            env->vfp.reg[rz].dspc[i] = clrsb32(val) - 24;
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            val = env->vfp.reg[rx].dsps[i];
            env->vfp.reg[rz].dsps[i] = clrsb32(val) - 16;
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            val = env->vfp.reg[rx].dspi[i];
            env->vfp.reg[rz].dspi[i] = clrsb32(val);
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            int64_t val_ll = env->vfp.reg[rx].dspl[i];
            env->vfp.reg[rz].dspl[i] = clrsb64(val_ll);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vclz)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz, val;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            val = env->vfp.reg[rx].udspc[i];
            if (val == 0) {
                env->vfp.reg[rz].udspc[i] = 8;
            } else {
                env->vfp.reg[rz].udspc[i] = __builtin_clz(val) - 24;
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            val = env->vfp.reg[rx].udsps[i];
            if (val == 0) {
                env->vfp.reg[rz].udsps[i] = 16;
            } else {
                env->vfp.reg[rz].udsps[i] = __builtin_clz(val) - 16;
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            val = env->vfp.reg[rx].udspi[i];
            if (val == 0) {
                env->vfp.reg[rz].udspi[i] = 32;
            } else {
                env->vfp.reg[rz].udspi[i] = __builtin_clz(val);
            }
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            uint64_t val_ll = env->vfp.reg[rx].udspl[i];
            if (val_ll == 0) {
                env->vfp.reg[rz].udspl[i] = 64;
            } else {
                env->vfp.reg[rz].udspl[i] = __builtin_clzll(val_ll);
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vcnt1)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz, val;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            val = env->vfp.reg[rx].udspc[i];
            env->vfp.reg[rz].udspc[i] = __builtin_popcount(val);
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            val = env->vfp.reg[rx].udsps[i];
            env->vfp.reg[rz].udsps[i] = __builtin_popcount(val);
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            val = env->vfp.reg[rx].udspi[i];
            env->vfp.reg[rz].udspi[i] = __builtin_popcount(val);
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            uint64_t val_ll = env->vfp.reg[rx].udspl[i];
            env->vfp.reg[rz].udspl[i] = __builtin_popcountll(val_ll);
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vtst)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, ry, rz;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    ry = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] &
                                        env->vfp.reg[ry].dspc[i]) ?
                                        0xff : 0x0;
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] &
                                        env->vfp.reg[ry].dsps[i]) ?
                                        0xffff : 0x0;
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] &
                                        env->vfp.reg[ry].dspi[i]) ?
                                        0xffffffff : 0x0;
        }
        break;
    case 64:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspl[i] = (env->vfp.reg[rx].dspl[i] &
                                        env->vfp.reg[ry].dspl[i]) ?
                                        0xffffffffffffffff : 0x0;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vdupg)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, vz;

    size = ((insn >> 4) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            env->vfp.reg[vz].udspc[i] = (uint8_t)env->regs[rx];
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[vz].udsps[i] = (uint16_t)env->regs[rx];
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[vz].udspi[i] = env->regs[rx];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vdup_1)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz, idx;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspc[i] = env->vfp.reg[rx].dspc[idx];
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dsps[i] = env->vfp.reg[rx].dsps[idx];
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[rz].dspi[i] = env->vfp.reg[rx].dspi[idx];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vdup_2)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz, idx;
    uint32_t j0, j1, k0, k1;
    union VDSP tmp1, tmp2;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;

    j0 = idx / number;
    k0 = idx % number;
    j1 = (idx + 1) / number;
    k1 = (idx + 1) % number;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            tmp1.dspc[i] = env->vfp.reg[rx + j0].dspc[k0];
            tmp2.dspc[i] = env->vfp.reg[rx + j1].dspc[k1];
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            tmp1.dsps[i] = env->vfp.reg[rx + j0].dsps[k0];
            tmp2.dsps[i] = env->vfp.reg[rx + j1].dsps[k1];
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            tmp1.dspi[i] = env->vfp.reg[rx + j0].dspi[k0];
            tmp2.dspi[i] = env->vfp.reg[rx + j1].dspi[k1];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
}

void VDSP2_HELPER(vdup_3)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz, idx;
    uint32_t j0, j1, j2, k0, k1, k2;
    union VDSP tmp1, tmp2, tmp3;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;

    j0 = idx / number;
    k0 = idx % number;
    j1 = (idx + 1) / number;
    k1 = (idx + 1) % number;
    j2 = (idx + 2) / number;
    k2 = (idx + 2) % number;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            tmp1.dspc[i] = env->vfp.reg[rx + j0].dspc[k0];
            tmp2.dspc[i] = env->vfp.reg[rx + j1].dspc[k1];
            tmp3.dspc[i] = env->vfp.reg[rx + j2].dspc[k2];
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            tmp1.dsps[i] = env->vfp.reg[rx + j0].dsps[k0];
            tmp2.dsps[i] = env->vfp.reg[rx + j1].dsps[k1];
            tmp3.dsps[i] = env->vfp.reg[rx + j2].dsps[k2];
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            tmp1.dspi[i] = env->vfp.reg[rx + j0].dspi[k0];
            tmp2.dspi[i] = env->vfp.reg[rx + j1].dspi[k1];
            tmp3.dspi[i] = env->vfp.reg[rx + j2].dspi[k2];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 2], &tmp3, sizeof(union VDSP));
}

void VDSP2_HELPER(vdup_4)(CPUCSKYState *env, uint32_t insn)
{
    int i, number;
    uint32_t size, width, rx, rz, idx;
    uint32_t j0, j1, j2, j3, k0, k1, k2, k3;
    union VDSP tmp0, tmp1, tmp2, tmp3;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;

    j0 = idx / number;
    k0 = idx % number;
    j1 = (idx + 1) / number;
    k1 = (idx + 1) % number;
    j2 = (idx + 2) / number;
    k2 = (idx + 2) % number;
    j3 = (idx + 3) / number;
    k3 = (idx + 3) % number;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            tmp0.dspc[i] = env->vfp.reg[rx + j0].dspc[k0];
            tmp1.dspc[i] = env->vfp.reg[rx + j1].dspc[k1];
            tmp2.dspc[i] = env->vfp.reg[rx + j2].dspc[k2];
            tmp3.dspc[i] = env->vfp.reg[rx + j3].dspc[k3];
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            tmp0.dsps[i] = env->vfp.reg[rx + j0].dsps[k0];
            tmp1.dsps[i] = env->vfp.reg[rx + j1].dsps[k1];
            tmp2.dsps[i] = env->vfp.reg[rx + j2].dsps[k2];
            tmp3.dsps[i] = env->vfp.reg[rx + j3].dsps[k3];
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            tmp0.dspi[i] = env->vfp.reg[rx + j0].dspi[k0];
            tmp1.dspi[i] = env->vfp.reg[rx + j1].dspi[k1];
            tmp2.dspi[i] = env->vfp.reg[rx + j2].dspi[k2];
            tmp3.dspi[i] = env->vfp.reg[rx + j3].dspi[k3];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp0, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 2], &tmp2, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 3], &tmp3, sizeof(union VDSP));
}

static inline uint32_t get_size_from_idx(uint32_t *idx)
{
    uint32_t res;
    if (*idx & 0x10) {
        res = 8;
        *idx &= 0xf;
    } else if (*idx & 0x8) {
        res = 16;
        *idx &= 0x7;
    } else if (*idx & 0x4) {
        res = 32;
        *idx &= 0x3;
    } else {
        res = 64;
        *idx &= 0x1;
    }
    return res;
}

void VDSP2_HELPER(vins_1)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t width = 0, rx, rz, idx1, idx2;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx1 = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;
    idx2 = (((insn >> 25) & 0x1) << 4) + (((insn >> 20) & 0x1) << 3)
        + ((insn >> 4) & 0x7);

    decode_imm5(env, idx2, &width, &idx2);

    switch (width) {
    case 8:
        env->vfp.reg[rz].udspc[idx2] = env->vfp.reg[rx].dspc[idx1];
        break;
    case 16:
        env->vfp.reg[rz].udsps[idx2] = env->vfp.reg[rx].dsps[idx1];
        break;
    case 32:
        env->vfp.reg[rz].udspi[idx2] = env->vfp.reg[rx].dspi[idx1];
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vins_2)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t number, width = 0, rx, rz, idx1, idx2;
    uint32_t j0, j1, k0, k1;
    uint32_t a, b;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx1 = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;
    idx2 = (((insn >> 25) & 0x1) << 4) + (((insn >> 20) & 0x1) << 3)
        + ((insn >> 4) & 0x7);

    decode_imm5(env, idx2, &width, &idx2);

    number = 128 / width;

    j0 = idx1 / number;
    k0 = idx1 % number;
    j1 = (idx1 + 1) / number;
    k1 = (idx1 + 1) % number;

    switch (width) {
    case 8:
        a = env->vfp.reg[rx + j0].udspc[k0];
        b = env->vfp.reg[rx + j1].udspc[k1];
        env->vfp.reg[rz].udspc[idx2] = a;
        env->vfp.reg[rz + 1].udspc[idx2] = b;
        break;
    case 16:
        a = env->vfp.reg[rx + j0].udsps[k0];
        b = env->vfp.reg[rx + j1].udsps[k1];
        env->vfp.reg[rz].udsps[idx2] = a;
        env->vfp.reg[rz + 1].udsps[idx2] = b;
        break;
    case 32:
        a = env->vfp.reg[rx + j0].udspi[k0];
        b = env->vfp.reg[rx + j1].udspi[k1];
        env->vfp.reg[rz].udspi[idx2] = a;
        env->vfp.reg[rz + 1].udspi[idx2] = b;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vins_3)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t number, width = 0, rx, rz, idx1, idx2;
    uint32_t j0, j1, j2, k0, k1, k2;
    uint32_t a, b, c;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx1 = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;
    idx2 = (((insn >> 25) & 0x1) << 4) + (((insn >> 20) & 0x1) << 3)
        + ((insn >> 4) & 0x7);

    decode_imm5(env, idx2, &width, &idx2);
    number = 128 / width;

    j0 = idx1 / number;
    k0 = idx1 % number;
    j1 = (idx1 + 1) / number;
    k1 = (idx1 + 1) % number;
    j2 = (idx1 + 2) / number;
    k2 = (idx1 + 2) % number;

    switch (width) {
    case 8:
        a = env->vfp.reg[rx + j0].udspc[k0];
        b = env->vfp.reg[rx + j1].udspc[k1];
        c = env->vfp.reg[rx + j2].udspc[k2];
        env->vfp.reg[rz].udspc[idx2] = a;
        env->vfp.reg[rz + 1].udspc[idx2] = b;
        env->vfp.reg[rz + 2].udspc[idx2] = c;
        break;
    case 16:
        a = env->vfp.reg[rx + j0].udsps[k0];
        b = env->vfp.reg[rx + j1].udsps[k1];
        c = env->vfp.reg[rx + j2].udsps[k2];
        env->vfp.reg[rz].udsps[idx2] = a;
        env->vfp.reg[rz + 1].udsps[idx2] = b;
        env->vfp.reg[rz + 2].udsps[idx2] = c;
        break;
    case 32:
        a = env->vfp.reg[rx + j0].udspi[k0];
        b = env->vfp.reg[rx + j1].udspi[k1];
        c = env->vfp.reg[rx + j2].udspi[k2];
        env->vfp.reg[rz].udspi[idx2] = a;
        env->vfp.reg[rz + 1].udspi[idx2] = b;
        env->vfp.reg[rz + 2].udspi[idx2] = c;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vins_4)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t number, width = 0, rx, rz, idx1, idx2;
    uint32_t j0, j1, j2, j3, k0, k1, k2, k3;
    uint32_t a, b, c, d;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx1 = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;
    idx2 = (((insn >> 25) & 0x1) << 4) + (((insn >> 20) & 0x1) << 3)
        + ((insn >> 4) & 0x7);

    decode_imm5(env, idx2, &width, &idx2);
    number = 128 / width;

    j0 = idx1 / number;
    k0 = idx1 % number;
    j1 = (idx1 + 1) / number;
    k1 = (idx1 + 1) % number;
    j2 = (idx1 + 2) / number;
    k2 = (idx1 + 2) % number;
    j3 = (idx1 + 3) / number;
    k3 = (idx1 + 3) % number;

    switch (width) {
    case 8:
        a = env->vfp.reg[rx + j0].udspc[k0];
        b = env->vfp.reg[rx + j1].udspc[k1];
        c = env->vfp.reg[rx + j2].udspc[k2];
        d = env->vfp.reg[rx + j3].udspc[k3];
        env->vfp.reg[rz].udspc[idx2] = a;
        env->vfp.reg[rz + 1].udspc[idx2] = b;
        env->vfp.reg[rz + 2].udspc[idx2] = c;
        env->vfp.reg[rz + 3].udspc[idx2] = d;
        break;
    case 16:
        a = env->vfp.reg[rx + j0].udsps[k0];
        b = env->vfp.reg[rx + j1].udsps[k1];
        c = env->vfp.reg[rx + j2].udsps[k2];
        d = env->vfp.reg[rx + j3].udsps[k3];
        env->vfp.reg[rz].udsps[idx2] = a;
        env->vfp.reg[rz + 1].udsps[idx2] = b;
        env->vfp.reg[rz + 2].udsps[idx2] = c;
        env->vfp.reg[rz + 3].udsps[idx2] = d;
        break;
    case 32:
        a = env->vfp.reg[rx + j0].udspi[k0];
        b = env->vfp.reg[rx + j1].udspi[k1];
        c = env->vfp.reg[rx + j2].udspi[k2];
        d = env->vfp.reg[rx + j3].udspi[k3];
        env->vfp.reg[rz].udspi[idx2] = a;
        env->vfp.reg[rz + 1].udspi[idx2] = b;
        env->vfp.reg[rz + 2].udspi[idx2] = c;
        env->vfp.reg[rz + 3].udspi[idx2] = d;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vpkg_2)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t number, width = 0, rx, rz, idx1, idx2;
    uint32_t j0, j1, k1;
    uint32_t a, b;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx1 = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;
    idx2 = (((insn >> 25) & 0x1) << 4) + (((insn >> 20) & 0x1) << 3)
        + ((insn >> 4) & 0x7);

    decode_imm5(env, idx2, &width, &idx2);
    number = 128 / width;

    j0 = idx2;
    j1 = (idx2 + 1) % number;
    k1 = (idx2 + 1) / number;

    switch (width) {
    case 8:
        a = env->vfp.reg[rx].udspc[idx1];
        b = env->vfp.reg[rx + 1].udspc[idx1];
        env->vfp.reg[rz].udspc[j0] = a;
        env->vfp.reg[rz + k1].udspc[j1] = b;
        break;
    case 16:
        a = env->vfp.reg[rx].udsps[idx1];
        b = env->vfp.reg[rx + 1].udsps[idx1];
        env->vfp.reg[rz].udsps[j0] = a;
        env->vfp.reg[rz + k1].udsps[j1] = b;
        break;
    case 32:
        a = env->vfp.reg[rx].udspi[idx1];
        b = env->vfp.reg[rx + 1].udspi[idx1];
        env->vfp.reg[rz].udspi[j0] = a;
        env->vfp.reg[rz + k1].udspi[j1] = b;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vpkg_3)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t number, width = 0, rx, rz, idx1, idx2;
    uint32_t j0, j1, j2, k1, k2;
    uint32_t a, b, c;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx1 = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;
    idx2 = (((insn >> 25) & 0x1) << 4) + (((insn >> 20) & 0x1) << 3)
        + ((insn >> 4) & 0x7);

    decode_imm5(env, idx2, &width, &idx2);
    number = 128 / width;

    j0 = idx2;
    j1 = (idx2 + 1) % number;
    j2 = (idx2 + 2) % number;
    k1 = (idx2 + 1) / number;
    k2 = (idx2 + 2) / number;

    switch (width) {
    case 8:
        a = env->vfp.reg[rx].udspc[idx1];
        b = env->vfp.reg[rx + 1].udspc[idx1];
        c = env->vfp.reg[rx + 2].udspc[idx1];
        env->vfp.reg[rz].udspc[j0] = a;
        env->vfp.reg[rz + k1].udspc[j1] = b;
        env->vfp.reg[rz + k2].udspc[j2] = c;
        break;
    case 16:
        a = env->vfp.reg[rx].udsps[idx1];
        b = env->vfp.reg[rx + 1].udsps[idx1];
        c = env->vfp.reg[rx + 2].udsps[idx1];
        env->vfp.reg[rz].udsps[j0] = a;
        env->vfp.reg[rz + k1].udsps[j1] = b;
        env->vfp.reg[rz + k2].udsps[j2] = c;
        break;
    case 32:
        a = env->vfp.reg[rx].udspi[idx1];
        b = env->vfp.reg[rx + 1].udspi[idx1];
        c = env->vfp.reg[rx + 2].udspi[idx1];
        env->vfp.reg[rz].udspi[j0] = a;
        env->vfp.reg[rz + k1].udspi[j1] = b;
        env->vfp.reg[rz + k2].udspi[j2] = c;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vpkg_4)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t number, width = 0, rx, rz, idx1, idx2;
    uint32_t j0, j1, j2, j3, k1, k2, k3;
    uint32_t a, b, c, d;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    idx1 = (insn >> CSKY_VDSP2_INDEX_SHI) & CSKY_VDSP2_INDEX_MASK;
    idx2 = (((insn >> 25) & 0x1) << 4) + (((insn >> 20) & 0x1) << 3)
        + ((insn >> 4) & 0x7);

    decode_imm5(env, idx2, &width, &idx2);
    number = 128 / width;

    j0 = idx2;
    j1 = (idx2 + 1) % number;
    j2 = (idx2 + 2) % number;
    j3 = (idx2 + 3) % number;
    k1 = (idx2 + 1) / number;
    k2 = (idx2 + 2) / number;
    k3 = (idx2 + 3) / number;

    switch (width) {
    case 8:
        a = env->vfp.reg[rx].udspc[idx1];
        b = env->vfp.reg[rx + 1].udspc[idx1];
        c = env->vfp.reg[rx + 2].udspc[idx1];
        d = env->vfp.reg[rx + 3].udspc[idx1];
        env->vfp.reg[rz].udspc[j0] = a;
        env->vfp.reg[rz + k1].udspc[j1] = b;
        env->vfp.reg[rz + k2].udspc[j2] = c;
        env->vfp.reg[rz + k3].udspc[j3] = d;
        break;
    case 16:
        a = env->vfp.reg[rx].udsps[idx1];
        b = env->vfp.reg[rx + 1].udsps[idx1];
        c = env->vfp.reg[rx + 2].udsps[idx1];
        d = env->vfp.reg[rx + 3].udsps[idx1];
        env->vfp.reg[rz].udsps[j0] = a;
        env->vfp.reg[rz + k1].udsps[j1] = b;
        env->vfp.reg[rz + k2].udsps[j2] = c;
        env->vfp.reg[rz + k3].udsps[j3] = d;
        break;
    case 32:
        a = env->vfp.reg[rx].udspi[idx1];
        b = env->vfp.reg[rx + 1].udspi[idx1];
        c = env->vfp.reg[rx + 2].udspi[idx1];
        d = env->vfp.reg[rx + 3].udspi[idx1];
        env->vfp.reg[rz].udspi[j0] = a;
        env->vfp.reg[rz + k1].udspi[j1] = b;
        env->vfp.reg[rz + k2].udspi[j2] = c;
        env->vfp.reg[rz + k3].udspi[j3] = d;
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vitl_2)(CPUCSKYState *env, uint32_t insn)
{
    int i, j, number;
    int pos, index;
    uint32_t size, width, rx, rz;
    union VDSP tmp[3];

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            for (j = 0; j < 2; j++) {
                pos = (2 * i + j) / number;
                index = (2 * i + j) % number;
                tmp[pos].dspc[index] = env->vfp.reg[rx + j].dspc[i];
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            for (j = 0; j < 2; j++) {
                pos = (2 * i + j) / number;
                index = (2 * i + j) % number;
                tmp[pos].dsps[index] = env->vfp.reg[rx + j].dsps[i];
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            for (j = 0; j < 2; j++) {
                pos = (2 * i + j) / number;
                index = (2 * i + j) % number;
                tmp[pos].dspi[index] = env->vfp.reg[rx + j].dspi[i];
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp[0], sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp[1], sizeof(union VDSP));
}

void VDSP2_HELPER(vdtl_2)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t i, number, size, width, rx, rz;
    uint32_t j0, j1, k0, k1;
    union VDSP tmp1, tmp2;

    size = ((insn >> 20) & 0x1) | ((insn >> 24) & 0x2);
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_VREG_SHI_VRX) & CSKY_VDSP2_VREG_MASK;
    rz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            j0 = (2 * i) / number;
            k0 = (2 * i) % number;
            j1 = (2 * i + 1) / number;
            k1 = (2 * i + 1) % number;
            tmp1.dspc[i] = env->vfp.reg[rx + j0].dspc[k0];
            tmp2.dspc[i] = env->vfp.reg[rx + j1].dspc[k1];
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            j0 = (2 * i) / number;
            k0 = (2 * i) % number;
            j1 = (2 * i + 1) / number;
            k1 = (2 * i + 1) % number;
            tmp1.dsps[i] = env->vfp.reg[rx + j0].dsps[k0];
            tmp2.dsps[i] = env->vfp.reg[rx + j1].dsps[k1];
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            j0 = (2 * i) / number;
            k0 = (2 * i) % number;
            j1 = (2 * i + 1) / number;
            k1 = (2 * i + 1) % number;
            tmp1.dspi[i] = env->vfp.reg[rx + j0].dspi[k0];
            tmp2.dspi[i] = env->vfp.reg[rx + j1].dspi[k1];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    memcpy(&env->vfp.reg[rz], &tmp1, sizeof(union VDSP));
    memcpy(&env->vfp.reg[rz + 1], &tmp2, sizeof(union VDSP));
}

/* LOAD/STORE instructions. */
void VDSP2_HELPER(vldru_n)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, n;
    uint32_t size, width, rx, ry, vz, addr, bytes, packnum, packlen, mask;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    ry = (insn >> CSKY_VDSP2_REG_SHI_RY) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }
    memset(&env->vfp.reg[vz], 0, sizeof(union VDSP));

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            env->vfp.reg[vz].udspc[i] = csky_cpu_ldub_data_ra(env, addr, GETPC());
            addr += 1;
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            env->vfp.reg[vz].udsps[i] = csky_cpu_lduw_data_ra(env, addr, GETPC());
            addr += 2;
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            env->vfp.reg[vz].udspi[i] = csky_cpu_ldl_data_ra(env, addr, GETPC());
            addr += 4;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    if (gen_mem_trace()) {
        bytes = width / 8 * n;
        packnum = bytes / 4;
        mask =  (1 << (bytes % 4) * 8) - 1;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < packnum; i++) {
            write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vz].udspi[i]);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspi[i] & mask);
        }
    }
    env->regs[rx] += env->regs[ry];
}

void VDSP2_HELPER(vldu_n)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, n;
    uint32_t size, width, rx, vz, addr, bytes, packnum, packlen, mask;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }
    memset(&env->vfp.reg[vz], 0, sizeof(union VDSP));

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            env->vfp.reg[vz].udspc[i] = csky_cpu_ldub_data_ra(env, addr, GETPC());
            addr += 1;
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            env->vfp.reg[vz].udsps[i] = csky_cpu_lduw_data_ra(env, addr, GETPC());
            addr += 2;
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            env->vfp.reg[vz].udspi[i] = csky_cpu_ldl_data_ra(env, addr, GETPC());
            addr += 4;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    if (gen_mem_trace()) {
        bytes = width / 8 * n;
        packnum = bytes / 4;
        mask =  (1 << (bytes % 4) * 8) - 1;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < packnum; i++) {
            write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vz].udspi[i]);
        }
        if (bytes % 4) {
            mask =  (1 << (bytes % 4) * 8) - 1;
            write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspi[i] & mask);
        }
    }
    env->regs[rx] = addr;
}

void VDSP2_HELPER(vlde)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, number;
    uint32_t size, width, rx, vy, vz, addr, packlen;

    size = (insn >> 4) & 0x3;
    width = 8 << size;
    number = 128 / width;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vy = (insn >> CSKY_VDSP2_VREG_SHI_VRY) & CSKY_VDSP2_VREG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    addr = env->regs[rx];

    packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            addr = env->regs[rx] + env->vfp.reg[vy].udspc[i];
            if (addr & ((1 << size) - 1)) {
                helper_exception(env, EXCP_CSKY_ALIGN);
                return;
            }
            env->vfp.reg[vz].udspc[i] = csky_cpu_ldub_data_ra(env, addr, GETPC());
            if (gen_mem_trace()) {
                write_trace_8_8(DATA_RADDR, packlen, 1, addr);
                write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspc[i]);
            }
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            addr = env->regs[rx] + env->vfp.reg[vy].udsps[i];
            if (addr & ((1 << size) - 1)) {
                helper_exception(env, EXCP_CSKY_ALIGN);
                return;
            }
            env->vfp.reg[vz].udsps[i] = csky_cpu_lduw_data_ra(env, addr, GETPC());
            if (gen_mem_trace()) {
                write_trace_8_8(DATA_RADDR, packlen, 2, addr);
                write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udsps[i]);
            }
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            addr = env->regs[rx] + env->vfp.reg[vy].udspi[i];
            if (addr & ((1 << size) - 1)) {
                helper_exception(env, EXCP_CSKY_ALIGN);
                return;
            }
            env->vfp.reg[vz].udspi[i] = csky_cpu_ldl_data_ra(env, addr, GETPC());
            if (gen_mem_trace()) {
                write_trace_8_8(DATA_RADDR, packlen, 4, addr);
                write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspi[i]);
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
}

void VDSP2_HELPER(vld_n)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, number;
    uint32_t size, width, rx, vz, imm, addr, bytes, packnum, packlen, mask;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (((insn >> 21) & 0x1f) << 2) + ((insn >> 8) & 0x3);
    number = ((insn >> 6) & 0x3) + 1;
    addr = env->regs[rx] + (imm << size);
    env->vfp.reg[vz].dspl[0] = 0;
    env->vfp.reg[vz].dspl[1] = 0;

    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            env->vfp.reg[vz].udspc[i] = csky_cpu_ldub_data_ra(env, addr + i, GETPC());
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            env->vfp.reg[vz].udsps[i] = csky_cpu_lduw_data_ra(env, addr + 2 * i, GETPC());
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            env->vfp.reg[vz].udspi[i] = csky_cpu_ldl_data_ra(env, addr + 4 * i, GETPC());
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    if (gen_mem_trace()) {
        bytes = width / 8 * number;
        packnum = bytes / 4;
        mask =  (1 << (bytes % 4) * 8) - 1;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, addr);
        for (i = 0; i < packnum; i++) {
            write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vz].udspi[i]);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspi[i] & mask);
        }
    }
}
void VDSP2_HELPER(vldx)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, n;
    uint32_t size, width, rx, ry, vz, addr, bytes, packnum, packlen, mask;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    ry = (insn >> CSKY_VDSP2_REG_SHI_RY) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    addr = env->regs[rx];
    n = env->regs[ry] & (128 / width - 1);
    if (n == 0) {
        n = 128 / width;
    }

    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }
    memset(&env->vfp.reg[vz], 0, sizeof(union VDSP));

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            env->vfp.reg[vz].udspc[i] = csky_cpu_ldub_data_ra(env, addr, GETPC());
            addr += 1;
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            env->vfp.reg[vz].udsps[i] = csky_cpu_lduw_data_ra(env, addr, GETPC());
            addr += 2;
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            env->vfp.reg[vz].udspi[i] = csky_cpu_ldl_data_ra(env, addr, GETPC());
            addr += 4;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    if (gen_mem_trace()) {
        bytes = width / 8 * n;
        packnum = bytes / 4;
        mask =  (1 << (bytes % 4) * 8) - 1;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < packnum; i++) {
            write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vz].udspi[i]);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspi[i] & mask);
        }
    }
}

void VDSP2_HELPER(vlrw_n)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, n;
    uint32_t size, vz, addr, offset, bytes, packlen;

    size = (insn >> 4) & 0x3;
    if (size != 0x2) {
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    offset = (((insn >> 16) & 0x3ff) << 2) + ((insn >> 8) & 0x3);
    addr = (env->pc + (offset << size)) & (0xffffffff << size);

    memset(&env->vfp.reg[vz], 0, sizeof(union VDSP));

    for (i = 0; i < n; i++) {
        env->vfp.reg[vz].udspi[i] = csky_cpu_ldl_code_ra(env, addr, GETPC());
        addr += 4;
    }
    if (gen_mem_trace()) {
        bytes = 4 * n;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, addr - 4 * n);
        for (i = 0; i < n; i++) {
            write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vz].udspi[i]);
        }
    }
}
void VDSP2_HELPER(vldmru)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i;
    uint32_t size, width, rx, ry, vn, n, addr, tmp, packlen;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    ry = (insn >> CSKY_VDSP2_REG_SHI_RY) & CSKY_VDSP2_REG_MASK;
    vn = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if ((addr & ((1 << size) - 1))
        || (env->regs[ry] & ((1 << size) - 1))) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            int j, k;
            tmp = addr;
            for (j = 0; j < 16; j++) {
                env->vfp.reg[vn + i].udspc[j] = csky_cpu_ldub_data_ra(env, tmp, GETPC());
                tmp++;
            }
            if (gen_mem_trace()) {
                write_trace_8_8(DATA_RADDR, packlen, 16, addr);
                for (k = 0; k < 4; k++) {
                    write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
                }
            }
            addr += env->regs[ry];
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            int j, k;
            tmp = addr;
            for (j = 0; j < 8; j++) {
                env->vfp.reg[vn + i].udsps[j] = csky_cpu_lduw_data_ra(env, tmp, GETPC());
                tmp += 2;
            }
            if (gen_mem_trace()) {
                write_trace_8_8(DATA_RADDR, packlen, 16, addr);
                for (k = 0; k < 4; k++) {
                    write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
                }
            }
            addr += env->regs[ry];
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            int j, k;
            tmp = addr;
            for (j = 0; j < 4; j++) {
                env->vfp.reg[vn + i].udspi[j] = csky_cpu_ldl_data_ra(env, tmp, GETPC());
                tmp += 4;
            }
            if (gen_mem_trace()) {
                write_trace_8_8(DATA_RADDR, packlen, 16, addr);
                for (k = 0; k < 4; k++) {
                    write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
                }
            }
            addr += env->regs[ry];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    env->regs[rx] = addr;
}

void VDSP2_HELPER(vldmu)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i;
    uint32_t size, width, rx, vn, n, addr, bytes, packlen;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vn = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 16; j++) {
                env->vfp.reg[vn + i].udspc[j] = csky_cpu_ldub_data_ra(env, addr, GETPC());
                addr++;
            }
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 8; j++) {
                env->vfp.reg[vn + i].udsps[j] = csky_cpu_lduw_data_ra(env, addr, GETPC());
                addr += 2;
            }
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 4; j++) {
                env->vfp.reg[vn + i].udspi[j] = csky_cpu_ldl_data_ra(env, addr, GETPC());
                addr += 4;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    if (gen_mem_trace()) {
        int k;
        bytes = n * 16;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < n; i++) {
            for (k = 0; k < 4; k++) {
                write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
            }
        }
    }
    env->regs[rx] = addr;
}

void VDSP2_HELPER(vldm)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i;
    uint32_t size, width, rx, vn, n, addr, bytes, packnum, packlen;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vn = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 16; j++) {
                env->vfp.reg[vn + i].udspc[j] = csky_cpu_ldub_data_ra(env, addr, GETPC());
                addr++;
            }
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 8; j++) {
                env->vfp.reg[vn + i].udsps[j] = csky_cpu_lduw_data_ra(env, addr, GETPC());
                addr += 2;
            }
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 4; j++) {
                env->vfp.reg[vn + i].udspi[j] = csky_cpu_ldl_data_ra(env, addr, GETPC());
                addr += 4;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    if (gen_mem_trace()) {
        int k;
        bytes = n * 16;
        packnum = bytes / 4;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < n; i++) {
            for (k = 0; k < packnum; k++) {
                write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
            }
        }
    }
}
void VDSP2_HELPER(vstru_n)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, n;
    uint32_t size, width, rx, ry, vz, addr, bytes, packnum, packlen, mask;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    ry = (insn >> CSKY_VDSP2_REG_SHI_RY) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            csky_cpu_stb_data_ra(env, addr, env->vfp.reg[vz].udspc[i], GETPC());
            addr += 1;
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            csky_cpu_stw_data_ra(env, addr, env->vfp.reg[vz].udsps[i], GETPC());
            addr += 2;
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            csky_cpu_stl_data_ra(env, addr, env->vfp.reg[vz].udspi[i], GETPC());
            addr += 4;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    if (gen_mem_trace()) {
        bytes = width / 8 * n;
        packnum = bytes / 4;
        mask =  (1 << (bytes % 4) * 8) - 1;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < packnum; i++) {
            write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vz].udspi[i]);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspi[i] & mask);
        }
    }

    env->regs[rx] += env->regs[ry];
}

void VDSP2_HELPER(vstu_n)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, n;
    uint32_t size, width, rx, vz, addr, bytes, packnum, packlen, mask;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            csky_cpu_stb_data_ra(env, addr, env->vfp.reg[vz].udspc[i], GETPC());
            addr += 1;
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            csky_cpu_stw_data_ra(env, addr, env->vfp.reg[vz].udsps[i], GETPC());
            addr += 2;
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            csky_cpu_stl_data_ra(env, addr, env->vfp.reg[vz].udspi[i], GETPC());
            addr += 4;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    if (gen_mem_trace()) {
        bytes = width / 8 * n;
        packnum = bytes / 4;
        mask =  (1 << (bytes % 4) * 8) - 1;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < packnum; i++) {
            write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vz].udspi[i]);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspi[i] & mask);
        }
    }
    env->regs[rx] = addr;
}

void VDSP2_HELPER(vstx)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, n;
    uint32_t size, width, rx, ry, vz, addr, bytes, packnum, packlen, mask;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    ry = (insn >> CSKY_VDSP2_REG_SHI_RY) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;

    addr = env->regs[rx];
    n = env->regs[ry] & (128 / width - 1);
    if (n == 0) {
        n = 128 / width;
    }

    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            csky_cpu_stb_data_ra(env, addr, env->vfp.reg[vz].udspc[i], GETPC());
            addr += 1;
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            csky_cpu_stw_data_ra(env, addr, env->vfp.reg[vz].udsps[i], GETPC());
            addr += 2;
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            csky_cpu_stl_data_ra(env, addr, env->vfp.reg[vz].udspi[i], GETPC());
            addr += 4;
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    if (gen_mem_trace()) {
        bytes = width / 8 * n;
        packnum = bytes / 4;
        mask =  (1 << (bytes % 4) * 8) - 1;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < packnum; i++) {
            write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vz].udspi[i]);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspi[i] & mask);
        }
    }
}



void VDSP2_HELPER(vst_n)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i, number;
    uint32_t size, width, rx, vz, imm, addr, bytes, packnum, packlen, mask;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vz = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    imm = (((insn >> 21) & 0x1f) << 2) + ((insn >> 8) & 0x3);
    number = ((insn >> 6) & 0x3) + 1;

    addr = env->regs[rx] + (imm << size);
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    switch (width) {
    case 8:
        for (i = 0; i < number; i++) {
            csky_cpu_stb_data_ra(env, addr + i, env->vfp.reg[vz].udspc[i], GETPC());
        }
        break;
    case 16:
        for (i = 0; i < number; i++) {
            csky_cpu_stw_data_ra(env, addr + 2 * i, env->vfp.reg[vz].udsps[i], GETPC());
        }
        break;
    case 32:
        for (i = 0; i < number; i++) {
            csky_cpu_stl_data_ra(env, addr + 4 * i, env->vfp.reg[vz].udspi[i], GETPC());
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    if (gen_mem_trace()) {
        bytes = width / 8 * number;
        packnum = bytes / 4;
        mask =  (1 << (bytes % 4) * 8) - 1;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_RADDR, packlen, bytes, addr);
        for (i = 0; i < packnum; i++) {
            write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vz].udspi[i]);
        }
        if (bytes % 4) {
            write_trace_8_8(DATA_VALUE, packlen, 0, env->vfp.reg[vz].udspi[i] & mask);
        }
    }
}

void VDSP2_HELPER(vstmu)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i;
    uint32_t size, width, rx, vn, n, addr, bytes, packlen;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vn = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 16; j++) {
                csky_cpu_stb_data_ra(env, addr, env->vfp.reg[vn + i].udspc[j], GETPC());
                addr += 1;
            }
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 8; j++) {
                csky_cpu_stw_data_ra(env, addr, env->vfp.reg[vn + i].udsps[j], GETPC());
                addr += 2;
            }
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 4; j++) {
                csky_cpu_stl_data_ra(env, addr, env->vfp.reg[vn + i].udspi[j], GETPC());
                addr += 4;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    if (gen_mem_trace()) {
        int k;
        bytes = n * 16;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < n; i++) {
            for (k = 0; k < n; k++) {
                write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
            }
        }
    }
    env->regs[rx] = addr;
}

void VDSP2_HELPER(vstm)(CPUCSKYState *env, uint32_t insn)
{
    int32_t i;
    uint32_t size, width, rx, vn, n, addr, bytes, packlen;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    vn = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if (addr & ((1 << size) - 1)) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 16; j++) {
                csky_cpu_stb_data_ra(env, addr, env->vfp.reg[vn + i].udspc[j], GETPC());
                addr += 1;
            }
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 8; j++) {
                csky_cpu_stw_data_ra(env, addr, env->vfp.reg[vn + i].udsps[j], GETPC());
                addr += 2;
            }
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 4; j++) {
                csky_cpu_stl_data_ra(env, addr, env->vfp.reg[vn + i].udspi[j], GETPC());
                addr += 4;
            }
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }
    if (gen_mem_trace()) {
        int k;
        bytes = n * 16;
        packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
        write_trace_8_8(DATA_WADDR, packlen, bytes, env->regs[rx]);
        for (i = 0; i < n; i++) {
            for (k = 0; k < 4; k++) {
                write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
            }
        }
    }
}
void VDSP2_HELPER(vstmru)(CPUCSKYState *env, uint32_t insn) {
    int32_t i;
    uint32_t size, width, rx, ry, vn, n, addr, tmp, packlen;

    size = (insn >> 4) & 0x3;
    width = 8 << size;

    rx = (insn >> CSKY_VDSP2_REG_SHI_RX) & CSKY_VDSP2_REG_MASK;
    ry = (insn >> CSKY_VDSP2_REG_SHI_RY) & CSKY_VDSP2_REG_MASK;
    vn = (insn >> CSKY_VDSP2_VREG_SHI_VRZ) & CSKY_VDSP2_VREG_MASK;
    n = ((insn >> CSKY_VDSP2_N_SHI) & CSKY_VDSP2_N_MASK) + 1;

    addr = env->regs[rx];
    /* check align */
    if ((addr & ((1 << size) - 1))
        || (env->regs[ry] & ((1 << size) - 1))) {
        helper_exception(env, EXCP_CSKY_ALIGN);
        return;
    }

    packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
    switch (width) {
    case 8:
        for (i = 0; i < n; i++) {
            int j, k;
            tmp = addr;
            for (j = 0; j < 16; j++) {
                csky_cpu_stb_data_ra(env, tmp, env->vfp.reg[vn + i].udspc[j], GETPC());
                tmp += 1;
            }
            if (gen_mem_trace()) {
                write_trace_8_8(DATA_WADDR, packlen, 16, addr);
                for (k = 0; k < 4; k++) {
                    write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
                }
            }
            addr += env->regs[ry];
        }
        break;
    case 16:
        for (i = 0; i < n; i++) {
            int j, k;
            tmp = addr;
            for (j = 0; j < 8; j++) {
                csky_cpu_stw_data_ra(env, tmp, env->vfp.reg[vn + i].udsps[j], GETPC());
                tmp += 2;
            }
            if (gen_mem_trace()) {
                write_trace_8_8(DATA_WADDR, packlen, 16, addr);
                for (k = 0; k < 4; k++) {
                    write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
                }
            }
            addr += env->regs[ry];
        }
        break;
    case 32:
        for (i = 0; i < n; i++) {
            int j, k;
            tmp = addr;
            for (j = 0; j < 4; j++) {
                csky_cpu_stl_data_ra(env, tmp, env->vfp.reg[vn + i].udspi[j], GETPC());
                tmp += 4;
            }
            if (gen_mem_trace()) {
                write_trace_8_8(DATA_WADDR, packlen, 16, addr);
                for (k = 0; k < 4; k++) {
                    write_trace_8_8(DATA_VALUE,  packlen, 0, env->vfp.reg[vn + i].udspi[k]);
                }
            }
            addr += env->regs[ry];
        }
        break;
    default:
        helper_exception(env, EXCP_CSKY_UDEF);
        return;
    }

    env->regs[rx] = addr;
}

