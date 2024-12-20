/*
 * RISC-V P Extension Helpers for QEMU.
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "fpu/softfloat.h"
#include <math.h>
#include "internals.h"
#include "vector_internals.h"

/*
 *** SIMD Data Processing Instructions
 */

/* 16-bit Addition & Subtraction Instructions */
typedef void PackedFn3i(CPURISCVState *, void *, void *, void *, uint8_t);

/* Define a common function to loop elements in packed register */
static inline target_ulong
rvpr(CPURISCVState *env, target_ulong a, target_ulong b,
     uint8_t step, uint8_t size, PackedFn3i *fn)
{
    int i, passes = sizeof(target_ulong) / size;
    target_ulong result = 0;

    for (i = 0; i < passes; i += step) {
        fn(env, &result, &a, &b, i);
    }
    return result;
}

#define RVPR(NAME, STEP, SIZE)                                  \
target_ulong HELPER(NAME)(CPURISCVState *env, target_ulong a,   \
                          target_ulong b)                       \
{                                                               \
    return rvpr(env, a, b, STEP, SIZE, (PackedFn3i *)do_##NAME);\
}

static inline int32_t hadd32(int32_t a, int32_t b)
{
    return ((int64_t)a + b) >> 1;
}

static inline void do_radd16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = hadd32(a[i], b[i]);
}

RVPR(radd16, 1, 2);

static inline uint32_t haddu32(uint32_t a, uint32_t b)
{
    return ((uint64_t)a + b) >> 1;
}

static inline void do_uradd16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = haddu32(a[i], b[i]);
}

RVPR(uradd16, 1, 2);

static inline void do_kadd16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = sadd16(env, 0, a[i], b[i]);
}

RVPR(kadd16, 1, 2);

static inline void do_ukadd16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = saddu16(env, 0, a[i], b[i]);
}

RVPR(ukadd16, 1, 2);

static inline int32_t hsub32(int32_t a, int32_t b)
{
    return ((int64_t)a - b) >> 1;
}

static inline int64_t hsub64(int64_t a, int64_t b)
{
    int64_t res = a - b;
    int64_t over = (res ^ a) & (a ^ b) & INT64_MIN;

    /* With signed overflow, bit 64 is inverse of bit 63. */
    return (res >> 1) ^ over;
}

static inline void do_rsub16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = hsub32(a[i], b[i]);
}

RVPR(rsub16, 1, 2);

static inline uint64_t hsubu64(uint64_t a, uint64_t b)
{
    return (a - b) >> 1;
}

static inline void do_ursub16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = hsubu64(a[i], b[i]);
}

RVPR(ursub16, 1, 2);

static inline void do_ksub16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = ssub16(env, 0, a[i], b[i]);
}

RVPR(ksub16, 1, 2);

static inline void do_uksub16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = ssubu16(env, 0, a[i], b[i]);
}

RVPR(uksub16, 1, 2);

static inline void do_cras16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = a[H2(i)] - b[H2(i + 1)];
    d[H2(i + 1)] = a[H2(i + 1)] + b[H2(i)];
}

RVPR(cras16, 2, 2);

static inline void do_rcras16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hsub32(a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = hadd32(a[H2(i + 1)], b[H2(i)]);
}

RVPR(rcras16, 2, 2);

static inline void do_urcras16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hsubu64(a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = haddu32(a[H2(i + 1)], b[H2(i)]);
}

RVPR(urcras16, 2, 2);

static inline void do_kcras16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = ssub16(env, 0, a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = sadd16(env, 0, a[H2(i + 1)], b[H2(i)]);
}

RVPR(kcras16, 2, 2);

static inline void do_ukcras16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = ssubu16(env, 0, a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = saddu16(env, 0, a[H2(i + 1)], b[H2(i)]);
}

RVPR(ukcras16, 2, 2);

static inline void do_crsa16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = a[H2(i)] + b[H2(i + 1)];
    d[H2(i + 1)] = a[H2(i + 1)] - b[H2(i)];
}

RVPR(crsa16, 2, 2);

static inline void do_rcrsa16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hadd32(a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = hsub32(a[H2(i + 1)], b[H2(i)]);
}

RVPR(rcrsa16, 2, 2);

static inline void do_urcrsa16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = haddu32(a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = hsubu64(a[H2(i + 1)], b[H2(i)]);
}

RVPR(urcrsa16, 2, 2);

static inline void do_kcrsa16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = sadd16(env, 0, a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = ssub16(env, 0, a[H2(i + 1)], b[H2(i)]);
}

RVPR(kcrsa16, 2, 2);

static inline void do_ukcrsa16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = saddu16(env, 0, a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = ssubu16(env, 0, a[H2(i + 1)], b[H2(i)]);
}

RVPR(ukcrsa16, 2, 2);

static inline void do_stas16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = a[H2(i)] - b[H2(i)];
    d[H2(i + 1)] = a[H2(i + 1)] + b[H2(i + 1)];
}

RVPR(stas16, 2, 2);

static inline void do_rstas16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hsub32(a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = hadd32(a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(rstas16, 2, 2);

static inline void do_urstas16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hsubu64(a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = haddu32(a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(urstas16, 2, 2);

static inline void do_kstas16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = ssub16(env, 0, a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = sadd16(env, 0, a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(kstas16, 2, 2);

static inline void do_ukstas16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = ssubu16(env, 0, a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = saddu16(env, 0, a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(ukstas16, 2, 2);

static inline void do_stsa16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = a[H2(i)] + b[H2(i)];
    d[H2(i + 1)] = a[H2(i + 1)] - b[H2(i + 1)];
}

RVPR(stsa16, 2, 2);

static inline void do_rstsa16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hadd32(a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = hsub32(a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(rstsa16, 2, 2);

static inline void do_urstsa16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = haddu32(a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = hsubu64(a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(urstsa16, 2, 2);

static inline void do_kstsa16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = sadd16(env, 0, a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = ssub16(env, 0, a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(kstsa16, 2, 2);

static inline void do_ukstsa16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = saddu16(env, 0, a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = ssubu16(env, 0, a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(ukstsa16, 2, 2);

/* 8-bit Addition & Subtraction Instructions */
static inline void do_radd8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;
    d[i] = hadd32(a[i], b[i]);
}

RVPR(radd8, 1, 1);

static inline void do_uradd8(CPURISCVState *env, void *vd, void *va,
                                  void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va, *b = vb;
    d[i] = haddu32(a[i], b[i]);
}

RVPR(uradd8, 1, 1);

static inline void do_kadd8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;
    d[i] = sadd8(env, 0, a[i], b[i]);
}

RVPR(kadd8, 1, 1);

static inline void do_ukadd8(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va, *b = vb;
    d[i] = saddu8(env, 0, a[i], b[i]);
}

RVPR(ukadd8, 1, 1);

static inline void do_rsub8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;
    d[i] = hsub32(a[i], b[i]);
}

RVPR(rsub8, 1, 1);

static inline void do_ursub8(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va, *b = vb;
    d[i] = hsubu64(a[i], b[i]);
}

RVPR(ursub8, 1, 1);

static inline void do_ksub8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;
    d[i] = ssub8(env, 0, a[i], b[i]);
}

RVPR(ksub8, 1, 1);

static inline void do_uksub8(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va, *b = vb;
    d[i] = ssubu8(env, 0, a[i], b[i]);
}

RVPR(uksub8, 1, 1);

/* 16-bit Shift Instructions */
static inline void do_sra16(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0xf;
    d[i] = a[i] >> shift;
}

RVPR(sra16, 1, 2);

static inline void do_srl16(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0xf;
    d[i] = a[i] >> shift;
}

RVPR(srl16, 1, 2);

static inline void do_sll16(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0xf;
    d[i] = a[i] << shift;
}

RVPR(sll16, 1, 2);

static inline void do_sra16_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0xf;

    d[i] = vssra16(env, 0, a[i], shift);
}

RVPR(sra16_u, 1, 2);

static inline void do_srl16_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0xf;

    d[i] = vssrl16(env, 0, a[i], shift);
}

RVPR(srl16_u, 1, 2);

static inline void do_ksll16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, result;
    uint8_t shift = *(uint8_t *)vb & 0xf;

    result = a[i] << shift;
    if (shift > (clrsb32(a[i]) - 16)) {
        env->vxsat = 0x1;
        d[i] = (a[i] & INT16_MIN) ? INT16_MIN : INT16_MAX;
    } else {
        d[i] = result;
    }
}

RVPR(ksll16, 1, 2);

static inline void do_kslra16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va;
    int32_t shift = sextract32((*(target_ulong *)vb), 0, 5);

    if (shift >= 0) {
        do_ksll16(env, vd, va, vb, i);
    } else {
        shift = -shift;
        shift = (shift == 16) ? 15 : shift;
        d[i] = a[i] >> shift;
    }
}

RVPR(kslra16, 1, 2);

static inline void do_kslra16_u(CPURISCVState *env, void *vd, void *va,
                                void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va;
    int32_t shift = sextract32((*(uint32_t *)vb), 0, 5);

    if (shift >= 0) {
        do_ksll16(env, vd, va, vb, i);
    } else {
        shift = -shift;
        shift = (shift == 16) ? 15 : shift;
        d[i] = vssra16(env, 0, a[i], shift);
    }
}

RVPR(kslra16_u, 1, 2);

/* SIMD 8-bit Shift Instructions */
static inline void do_sra8(CPURISCVState *env, void *vd, void *va,
                           void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x7;
    d[i] = a[i] >> shift;
}

RVPR(sra8, 1, 1);

static inline void do_srl8(CPURISCVState *env, void *vd, void *va,
                           void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x7;
    d[i] = a[i] >> shift;
}

RVPR(srl8, 1, 1);

static inline void do_sll8(CPURISCVState *env, void *vd, void *va,
                           void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x7;
    d[i] = a[i] << shift;
}

RVPR(sll8, 1, 1);

static inline void do_sra8_u(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x7;
    d[i] =  vssra8(env, 0, a[i], shift);
}

RVPR(sra8_u, 1, 1);

static inline void do_srl8_u(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x7;
    d[i] =  vssrl8(env, 0, a[i], shift);
}

RVPR(srl8_u, 1, 1);

static inline void do_ksll8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, result;
    uint8_t shift = *(uint8_t *)vb & 0x7;

    result = a[i] << shift;
    if (shift > (clrsb32(a[i]) - 24)) {
        env->vxsat = 0x1;
        d[i] = (a[i] & INT8_MIN) ? INT8_MIN : INT8_MAX;
    } else {
        d[i] = result;
    }
}

RVPR(ksll8, 1, 1);

static inline void do_kslra8(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va;
    int32_t shift = sextract32((*(uint32_t *)vb), 0, 4);

    if (shift >= 0) {
        do_ksll8(env, vd, va, vb, i);
    } else {
        shift = -shift;
        shift = (shift == 8) ? 7 : shift;
        d[i] = a[i] >> shift;
    }
}

RVPR(kslra8, 1, 1);

static inline void do_kslra8_u(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va;
    int32_t shift = sextract32((*(uint32_t *)vb), 0, 4);

    if (shift >= 0) {
        do_ksll8(env, vd, va, vb, i);
    } else {
        shift = -shift;
        shift = (shift == 8) ? 7 : shift;
        d[i] =  vssra8(env, 0, a[i], shift);
    }
}

RVPR(kslra8_u, 1, 1);

/* SIMD 16-bit Compare Instructions */
static inline void do_cmpeq16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] == b[i]) ? 0xffff : 0x0;
}

RVPR(cmpeq16, 1, 2);

static inline void do_scmplt16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] < b[i]) ? 0xffff : 0x0;
}

RVPR(scmplt16, 1, 2);

static inline void do_scmple16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] <= b[i]) ? 0xffff : 0x0;
}

RVPR(scmple16, 1, 2);

static inline void do_ucmplt16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] < b[i]) ? 0xffff : 0x0;
}

RVPR(ucmplt16, 1, 2);

static inline void do_ucmple16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] <= b[i]) ? 0xffff : 0x0;
}

RVPR(ucmple16, 1, 2);

/* SIMD 8-bit Compare Instructions */
static inline void do_cmpeq8(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] == b[i]) ? 0xff : 0x0;
}

RVPR(cmpeq8, 1, 1);

static inline void do_scmplt8(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] < b[i]) ? 0xff : 0x0;
}

RVPR(scmplt8, 1, 1);

static inline void do_scmple8(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] <= b[i]) ? 0xff : 0x0;
}

RVPR(scmple8, 1, 1);

static inline void do_ucmplt8(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] < b[i]) ? 0xff : 0x0;
}

RVPR(ucmplt8, 1, 1);

static inline void do_ucmple8(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va, *b = vb;
    d[i] = (a[i] <= b[i]) ? 0xff : 0x0;
}

RVPR(ucmple8, 1, 1);

/* SIMD 16-bit Multiply Instructions */
typedef void PackedFn3(CPURISCVState *, void *, void *, void *);
static inline uint64_t rvpr64(CPURISCVState *env, target_ulong a,
                              target_ulong b, PackedFn3 *fn)
{
    uint64_t result;

    fn(env, &result, &a, &b);
    return result;
}

#define RVPR64(NAME)                                            \
uint64_t HELPER(NAME)(CPURISCVState *env, target_ulong a,       \
                      target_ulong b)                           \
{                                                               \
    return rvpr64(env, a, b, (PackedFn3 *)do_##NAME);           \
}

static inline void do_smul16(CPURISCVState *env, void *vd, void *va, void *vb)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    d[H4(0)] = (int32_t)a[H2(0)] * b[H2(0)];
    d[H4(1)] = (int32_t)a[H2(1)] * b[H2(1)];
}

RVPR64(smul16);

static inline void do_smulx16(CPURISCVState *env, void *vd, void *va, void *vb)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    d[H4(0)] = (int32_t)a[H2(0)] * b[H2(1)];
    d[H4(1)] = (int32_t)a[H2(1)] * b[H2(0)];
}

RVPR64(smulx16);

static inline void do_umul16(CPURISCVState *env, void *vd, void *va, void *vb,
                             uint8_t i)
{
    uint32_t *d = vd;
    uint16_t *a = va, *b = vb;
    d[H4(0)] = (uint32_t)a[H2(0)] * b[H2(0)];
    d[H4(1)] = (uint32_t)a[H2(1)] * b[H2(1)];
}

RVPR64(umul16);

static inline void do_umulx16(CPURISCVState *env, void *vd, void *va, void *vb,
                              uint8_t i)
{
    uint32_t *d = vd;
    uint16_t *a = va, *b = vb;
    d[H4(0)] = (uint32_t)a[H2(0)] * b[H2(1)];
    d[H4(1)] = (uint32_t)a[H2(1)] * b[H2(0)];
}

RVPR64(umulx16);

static inline void do_khm16(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;

    if (a[i] == INT16_MIN && b[i] == INT16_MIN) {
        env->vxsat = 1;
        d[i] = INT16_MAX;
    } else {
        d[i] = (int32_t)a[i] * b[i] >> 15;
    }
}

RVPR(khm16, 1, 2);

static inline void do_khmx16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;

    /*
     * t[x] = ra.H[x] s* rb.H[y];
     * rt.H[x] = SAT.Q15(t[x] s>> 15);
     *
     * (RV32: (x,y)=(1,0),(0,1),
     *  RV64: (x,y)=(3,2),(2,3),
     *              (1,0),(0,1)
     */
    if (a[H2(i)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        env->vxsat = 1;
        d[H2(i)] = INT16_MAX;
    } else {
        d[H2(i)] = (int32_t)a[H2(i)] * b[H2(i + 1)] >> 15;
    }
    if (a[H2(i + 1)] == INT16_MIN && b[H2(i)] == INT16_MIN) {
        env->vxsat = 1;
        d[H2(i + 1)] = INT16_MAX;
    } else {
        d[H2(i + 1)] = (int32_t)a[H2(i + 1)] * b[H2(i)] >> 15;
    }
}

RVPR(khmx16, 2, 2);

/* SIMD 8-bit Multiply Instructions */
static inline void do_smul8(CPURISCVState *env, void *vd, void *va, void *vb)
{
    int16_t *d = vd;
    int8_t *a = va, *b = vb;
    d[H2(0)] = (int16_t)a[H1(0)] * b[H1(0)];
    d[H2(1)] = (int16_t)a[H1(1)] * b[H1(1)];
    d[H2(2)] = (int16_t)a[H1(2)] * b[H1(2)];
    d[H2(3)] = (int16_t)a[H1(3)] * b[H1(3)];
}

RVPR64(smul8);

static inline void do_smulx8(CPURISCVState *env, void *vd, void *va, void *vb)
{
    int16_t *d = vd;
    int8_t *a = va, *b = vb;
    d[H2(0)] = (int16_t)a[H1(0)] * b[H1(1)];
    d[H2(1)] = (int16_t)a[H1(1)] * b[H1(0)];
    d[H2(2)] = (int16_t)a[H1(2)] * b[H1(3)];
    d[H2(3)] = (int16_t)a[H1(3)] * b[H1(2)];
}

RVPR64(smulx8);

static inline void do_umul8(CPURISCVState *env, void *vd, void *va, void *vb)
{
    uint16_t *d = vd;
    uint8_t *a = va, *b = vb;
    d[H2(0)] = (uint16_t)a[H1(0)] * b[H1(0)];
    d[H2(1)] = (uint16_t)a[H1(1)] * b[H1(1)];
    d[H2(2)] = (uint16_t)a[H1(2)] * b[H1(2)];
    d[H2(3)] = (uint16_t)a[H1(3)] * b[H1(3)];
}

RVPR64(umul8);

static inline void do_umulx8(CPURISCVState *env, void *vd, void *va, void *vb)
{
    uint16_t *d = vd;
    uint8_t *a = va, *b = vb;
    d[H2(0)] = (uint16_t)a[H1(0)] * b[H1(1)];
    d[H2(1)] = (uint16_t)a[H1(1)] * b[H1(0)];
    d[H2(2)] = (uint16_t)a[H1(2)] * b[H1(3)];
    d[H2(3)] = (uint16_t)a[H1(3)] * b[H1(2)];
}

RVPR64(umulx8);

static inline void do_khm8(CPURISCVState *env, void *vd, void *va,
                           void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;

    if (a[i] == INT8_MIN && b[i] == INT8_MIN) {
        env->vxsat = 1;
        d[i] = INT8_MAX;
    } else {
        d[i] = (int16_t)a[i] * b[i] >> 7;
    }
}

RVPR(khm8, 1, 1);

static inline void do_khmx8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;
    /*
     * t[x] = ra.B[x] s* rb.B[y];
     * rt.B[x] = SAT.Q7(t[x] s>> 7);
     *
     * (RV32: (x,y)=(3,2),(2,3),
     *              (1,0),(0,1),
     * (RV64: (x,y)=(7,6),(6,7),(5,4),(4,5),
     *              (3,2),(2,3),(1,0),(0,1))
     */
    if (a[H1(i)] == INT8_MIN && b[H1(i + 1)] == INT8_MIN) {
        env->vxsat = 1;
        d[H1(i)] = INT8_MAX;
    } else {
        d[H1(i)] = (int16_t)a[H1(i)] * b[H1(i + 1)] >> 7;
    }
    if (a[H1(i + 1)] == INT8_MIN && b[H1(i)] == INT8_MIN) {
        env->vxsat = 1;
        d[H1(i + 1)] = INT8_MAX;
    } else {
        d[H1(i + 1)] = (int16_t)a[H1(i + 1)] * b[H1(i)] >> 7;
    }
}

RVPR(khmx8, 2, 1);

/* SIMD 16-bit Miscellaneous Instructions */
static inline void do_smin16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] < b[i]) ? a[i] : b[i];
}

RVPR(smin16, 1, 2);

static inline void do_umin16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] < b[i]) ? a[i] : b[i];
}

RVPR(umin16, 1, 2);

static inline void do_smax16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] > b[i]) ? a[i] : b[i];
}

RVPR(smax16, 1, 2);

static inline void do_umax16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] > b[i]) ? a[i] : b[i];
}

RVPR(umax16, 1, 2);

static int64_t sat64(CPURISCVState *env, int64_t a, uint8_t shift)
{
    int64_t max = shift >= 64 ? INT64_MAX : (1ull << shift) - 1;
    int64_t min = shift >= 64 ? INT64_MIN : -(1ull << shift);
    int64_t result;

    if (a > max) {
        result = max;
        env->vxsat = 0x1;
    } else if (a < min) {
        result = min;
        env->vxsat = 0x1;
    } else {
        result = a;
    }
    return result;
}

static inline void do_sclip16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0xf;

    d[i] = sat64(env, a[i], shift);
}

RVPR(sclip16, 1, 2);

static uint64_t satu64(CPURISCVState *env, uint64_t a, uint8_t shift)
{
    uint64_t max = shift >= 64 ? UINT64_MAX : (1ull << shift) - 1;
    uint64_t result;

    if (a > max) {
        result = max;
        env->vxsat = 0x1;
    } else {
        result = a;
    }
    return result;
}

static inline void do_uclip16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0xf;

    if (a[i] < 0) {
        d[i] = 0;
        env->vxsat = 0x1;
    } else {
        d[i] = satu64(env, a[i], shift);
    }
}

RVPR(uclip16, 1, 2);

typedef void PackedFn2i(CPURISCVState *, void *, void *, uint8_t);

static inline target_ulong rvpr2(CPURISCVState *env, target_ulong a,
                                 uint8_t step, uint8_t size, PackedFn2i *fn)
{
    int i, passes = sizeof(target_ulong) / size;
    target_ulong result;

    for (i = 0; i < passes; i += step) {
        fn(env, &result, &a, i);
    }
    return result;
}

#define RVPR2(NAME, STEP, SIZE)                                  \
target_ulong HELPER(NAME)(CPURISCVState *env, target_ulong a)    \
{                                                                \
    return rvpr2(env, a, STEP, SIZE, (PackedFn2i *)do_##NAME);   \
}

static inline void do_kabs16(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int16_t *d = vd, *a = va;

    if (a[i] == INT16_MIN) {
        d[i] = INT16_MAX;
        env->vxsat = 0x1;
    } else {
        d[i] = abs(a[i]);
    }
}

RVPR2(kabs16, 1, 2);

static inline void do_clrs16(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int16_t *d = vd, *a = va;
    d[i] = clrsb32(a[i]) - 16;
}

RVPR2(clrs16, 1, 2);

static inline void do_clz16(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int16_t *d = vd, *a = va;
    d[i] = (a[i] < 0) ? 0 : (clz32(a[i]) - 16);
}

RVPR2(clz16, 1, 2);

static inline void do_clo16(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int16_t *d = vd, *a = va;
    d[i] = (a[i] >= 0) ? 0 : (clo32(a[i]) - 16);
}

RVPR2(clo16, 1, 2);

/* SIMD 8-bit Miscellaneous Instructions */
static inline void do_smin8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] < b[i]) ? a[i] : b[i];
}

RVPR(smin8, 1, 1);

static inline void do_umin8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] < b[i]) ? a[i] : b[i];
}

RVPR(umin8, 1, 1);

static inline void do_smax8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] > b[i]) ? a[i] : b[i];
}

RVPR(smax8, 1, 1);

static inline void do_umax8(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    uint8_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] > b[i]) ? a[i] : b[i];
}

RVPR(umax8, 1, 1);

static inline void do_sclip8(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x7;

    d[i] = sat64(env, a[i], shift);
}

RVPR(sclip8, 1, 1);

static inline void do_uclip8(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int8_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x7;

    if (a[i] < 0) {
        d[i] = 0;
        env->vxsat = 0x1;
    } else {
        d[i] = satu64(env, a[i], shift);
    }
}

RVPR(uclip8, 1, 1);

static inline void do_kabs8(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *d = vd, *a = va;

    if (a[i] == INT8_MIN) {
        d[i] = INT8_MAX;
        env->vxsat = 0x1;
    } else {
        d[i] = abs(a[i]);
    }
}

RVPR2(kabs8, 1, 1);

static inline void do_clrs8(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *d = vd, *a = va;
    d[i] = clrsb32(a[i]) - 24;
}

RVPR2(clrs8, 1, 1);

static inline void do_clz8(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *d = vd, *a = va;
    d[i] = (a[i] < 0) ? 0 : (clz32(a[i]) - 24);
}

RVPR2(clz8, 1, 1);

static inline void do_clo8(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *d = vd, *a = va;
    d[i] = (a[i] >= 0) ? 0 : (clo32(a[i]) - 24);
}

RVPR2(clo8, 1, 1);

static inline void do_swap8(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *d = vd, *a = va;
    d[H1(i)] = a[H1(i + 1)];
    d[H1(i + 1)] = a[H1(i)];
}

RVPR2(swap8, 2, 1);

/* 8-bit Unpacking Instructions */
static inline void
do_sunpkd810(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *a = va;
    int16_t *d = vd;

    d[H2(i / 2)] = a[H1(i)];
    d[H2(i / 2 + 1)] = a[H1(i + 1)];
}

RVPR2(sunpkd810, 4, 1);

static inline void
do_sunpkd820(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *a = va;
    int16_t *d = vd;

    d[H2(i / 2)] = a[H1(i)];
    d[H2(i / 2 + 1)] = a[H1(i + 2)];
}

RVPR2(sunpkd820, 4, 1);

static inline void
do_sunpkd830(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *a = va;
    int16_t *d = vd;

    d[H2(i / 2)] = a[H1(i)];
    d[H2(i / 2 + 1)] = a[H1(i + 3)];
}

RVPR2(sunpkd830, 4, 1);

static inline void
do_sunpkd831(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *a = va;
    int16_t *d = vd;

    d[H2(i / 2)] = a[H1(i) + 1];
    d[H2(i / 2 + 1)] = a[H1(i + 3)];
}

RVPR2(sunpkd831, 4, 1);

static inline void
do_sunpkd832(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int8_t *a = va;
    int16_t *d = vd;

    d[H2(i / 2)] = a[H1(i) + 2];
    d[H2(i / 2 + 1)] = a[H1(i + 3)];
}

RVPR2(sunpkd832, 4, 1);

static inline void
do_zunpkd810(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    uint8_t *a = va;
    uint16_t *d = vd;

    d[H2(i / 2)] = a[H1(i)];
    d[H2(i / 2 + 1)] = a[H1(i + 1)];
}

RVPR2(zunpkd810, 4, 1);

static inline void
do_zunpkd820(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    uint8_t *a = va;
    uint16_t *d = vd;

    d[H2(i / 2)] = a[H1(i)];
    d[H2(i / 2 + 1)] = a[H1(i + 2)];
}

RVPR2(zunpkd820, 4, 1);

static inline void
do_zunpkd830(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    uint8_t *a = va;
    uint16_t *d = vd;

    d[H2(i / 2)] = a[H1(i)];
    d[H2(i / 2 + 1)] = a[H1(i + 3)];
}

RVPR2(zunpkd830, 4, 1);

static inline void
do_zunpkd831(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    uint8_t *a = va;
    uint16_t *d = vd;

    d[H2(i / 2)] = a[H1(i) + 1];
    d[H2(i / 2 + 1)] = a[H1(i + 3)];
}

RVPR2(zunpkd831, 4, 1);

static inline void
do_zunpkd832(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    uint8_t *a = va;
    uint16_t *d = vd;

    d[H2(i / 2)] = a[H1(i) + 2];
    d[H2(i / 2 + 1)] = a[H1(i + 3)];
}

RVPR2(zunpkd832, 4, 1);

/*
 *** Partial-SIMD Data Processing Instructions
 */

/* 16-bit Packing Instructions */
static inline void do_pkbb16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i + 1)] = a[H2(i)];
    d[H2(i)] = b[H2(i)];
}

RVPR(pkbb16, 2, 2);

static inline void do_pkbt16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i + 1)] = a[H2(i)];
    d[H2(i)] = b[H2(i + 1)];
}

RVPR(pkbt16, 2, 2);

static inline void do_pktt16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i + 1)] = a[H2(i + 1)];
    d[H2(i)] = b[H2(i + 1)];
}

RVPR(pktt16, 2, 2);

static inline void do_pktb16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i + 1)] = a[H2(i + 1)];
    d[H2(i)] = b[H2(i)];
}

RVPR(pktb16, 2, 2);

/* Most Significant Word “32x32” Multiply & Add Instructions */
static inline void do_smmul(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[i] = (int64_t)a[i] * b[i] >> 32;
}

RVPR(smmul, 1, 4);

static inline void do_smmul_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[i] = ((int64_t)a[i] * b[i] + (uint32_t)INT32_MIN) >> 32;
}

RVPR(smmul_u, 1, 4);

typedef void PackedFn4i(CPURISCVState *, void *, void *,
                        void *, void *, uint8_t);

static inline target_ulong
rvpr_acc(CPURISCVState *env, target_ulong a,
         target_ulong b, target_ulong c,
         uint8_t step, uint8_t size, PackedFn4i *fn)
{
    int i, passes = sizeof(target_ulong) / size;
    target_ulong result = 0;

    for (i = 0; i < passes; i += step) {
        fn(env, &result, &a, &b, &c, i);
    }
    return result;
}

#define RVPR_ACC(NAME, STEP, SIZE)                                     \
target_ulong HELPER(NAME)(CPURISCVState *env, target_ulong a,          \
                          target_ulong b, target_ulong c)              \
{                                                                      \
    return rvpr_acc(env, a, b, c, STEP, SIZE, (PackedFn4i *)do_##NAME);\
}

static inline void do_kmmac(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb, *c = vc;
    d[i] = sadd32(env, 0, ((int64_t)a[i] * b[i]) >> 32, c[i]);
}

RVPR_ACC(kmmac, 1, 4);

static inline void do_kmmac_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb, *c = vc;
    d[i] = sadd32(env, 0, ((int64_t)a[i] * b[i] +
                           (uint32_t)INT32_MIN) >> 32, c[i]);
}

RVPR_ACC(kmmac_u, 1, 4);

static inline void do_kmmsb(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb, *c = vc;
    d[i] = ssub32(env, 0, c[i], (int64_t)a[i] * b[i] >> 32);
}

RVPR_ACC(kmmsb, 1, 4);

static inline void do_kmmsb_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb, *c = vc;
    d[i] = ssub32(env, 0, c[i], ((int64_t)a[i] * b[i] +
                                 (uint32_t)INT32_MIN) >> 32);
}

RVPR_ACC(kmmsb_u, 1, 4);

static inline void do_kwmmul(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    if (a[i] == INT32_MIN && b[i] == INT32_MIN) {
        env->vxsat = 0x1;
        d[i] = INT32_MAX;
    } else {
        d[i] = (int64_t)a[i] * b[i] >> 31;
    }
}

RVPR(kwmmul, 1, 4);

static inline void do_kwmmul_u(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    if (a[i] == INT32_MIN && b[i] == INT32_MIN) {
        env->vxsat = 0x1;
        d[i] = INT32_MAX;
    } else {
        d[i] = ((int64_t)a[i] * b[i] + (1ull << 30)) >> 31;
    }
}

RVPR(kwmmul_u, 1, 4);

/* Most Significant Word “32x16” Multiply & Add Instructions */
static inline void do_smmwb(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    int16_t *b = vb;
    d[H4(i)] = (int64_t)a[H4(i)] * b[H2(2 * i)] >> 16;
}

RVPR(smmwb, 1, 4);

static inline void do_smmwb_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    int16_t *b = vb;
    d[H4(i)] = ((int64_t)a[H4(i)] * b[H2(2 * i)] + (1ull << 15)) >> 16;
}

RVPR(smmwb_u, 1, 4);

static inline void do_smmwt(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    int16_t *b = vb;
    d[H4(i)] = (int64_t)a[H4(i)] * b[H2(2 * i + 1)] >> 16;
}

RVPR(smmwt, 1, 4);

static inline void do_smmwt_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    int16_t *b = vb;
    d[H4(i)] = ((int64_t)a[H4(i)] * b[H2(2 * i + 1)] + (1ull << 15)) >> 16;
}

RVPR(smmwt_u, 1, 4);

static inline void do_kmmawb(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *c = vc;
    int16_t *b = vb;
    d[H4(i)] = sadd32(env, 0, (int64_t)a[H4(i)] * b[H2(2 * i)] >> 16, c[H4(i)]);
}

RVPR_ACC(kmmawb, 1, 4);

static inline void do_kmmawb_u(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *c = vc;
    int16_t *b = vb;
    d[H4(i)] = sadd32(env, 0, ((int64_t)a[H4(i)] * b[H2(2 * i)] +
                               (1ull << 15)) >> 16, c[H4(i)]);
}

RVPR_ACC(kmmawb_u, 1, 4);

static inline void do_kmmawt(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *c = vc;
    int16_t *b = vb;
    d[H4(i)] = sadd32(env, 0, (int64_t)a[H4(i)] * b[H2(2 * i + 1)] >> 16,
                      c[H4(i)]);
}

RVPR_ACC(kmmawt, 1, 4);

static inline void do_kmmawt_u(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *c = vc;
    int16_t *b = vb;
    d[H4(i)] = sadd32(env, 0, ((int64_t)a[H4(i)] * b[H2(2 * i + 1)] +
                               (1ull << 15)) >> 16, c[H4(i)]);
}

RVPR_ACC(kmmawt_u, 1, 4);

static inline void do_kmmwb2(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    int16_t *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H2(2 * i)] == INT16_MIN) {
        env->vxsat = 0x1;
        d[H4(i)] = INT32_MAX;
    } else {
        d[H4(i)] = (int64_t)a[H4(i)] * b[H2(2 * i)] >> 15;
    }
}

RVPR(kmmwb2, 1, 4);

static inline void do_kmmwb2_u(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    int16_t *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H2(2 * i)] == INT16_MIN) {
        env->vxsat = 0x1;
        d[H4(i)] = INT32_MAX;
    } else {
        d[H4(i)] = ((int64_t)a[H4(i)] * b[H2(2 * i)] + (1ull << 14)) >> 15;
    }
}

RVPR(kmmwb2_u, 1, 4);

static inline void do_kmmwt2(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    int16_t *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H2(2 * i + 1)] == INT16_MIN) {
        env->vxsat = 0x1;
        d[H4(i)] = INT32_MAX;
    } else {
        d[H4(i)] = (int64_t)a[H4(i)] * b[H2(2 * i + 1)] >> 15;
    }
}

RVPR(kmmwt2, 1, 4);

static inline void do_kmmwt2_u(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    int16_t *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H2(2 * i + 1)] == INT16_MIN) {
        env->vxsat = 0x1;
        d[H4(i)] = INT32_MAX;
    } else {
        d[H4(i)] = ((int64_t)a[H4(i)] * b[H2(2 * i + 1)] + (1ull << 14)) >> 15;
    }
}

RVPR(kmmwt2_u, 1, 4);

static inline void do_kmmawb2(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *c = vc, result;
    int16_t *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H2(2 * i)] == INT16_MIN) {
        env->vxsat = 0x1;
        result = INT32_MAX;
    } else {
        result = (int64_t)a[H4(i)] * b[H2(2 * i)] >> 15;
    }
    d[H4(i)] = sadd32(env, 0, result, c[H4(i)]);
}

RVPR_ACC(kmmawb2, 1, 4);

static inline void do_kmmawb2_u(CPURISCVState *env, void *vd, void *va,
                                void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *c = vc, result;
    int16_t *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H2(2 * i)] == INT16_MIN) {
        env->vxsat = 0x1;
        result = INT32_MAX;
    } else {
        result = ((int64_t)a[H4(i)] * b[H2(2 * i)] + (1ull << 14)) >> 15;
    }
    d[H4(i)] = sadd32(env, 0, result, c[H4(i)]);
}

RVPR_ACC(kmmawb2_u, 1, 4);

static inline void do_kmmawt2(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *c = vc, result;
    int16_t *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H2(2 * i + 1)] == INT16_MIN) {
        env->vxsat = 0x1;
        result = INT32_MAX;
    } else {
        result = (int64_t)a[H4(i)] * b[H2(2 * i + 1)] >> 15;
    }
    d[H4(i)] = sadd32(env, 0, result, c[H4(i)]);
}

RVPR_ACC(kmmawt2, 1, 4);

static inline void do_kmmawt2_u(CPURISCVState *env, void *vd, void *va,
                                void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *a = va, *c = vc, result;
    int16_t *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H2(2 * i + 1)] == INT16_MIN) {
        env->vxsat = 0x1;
        result = INT32_MAX;
    } else {
        result = ((int64_t)a[H4(i)] * b[H2(2 * i + 1)] + (1ull << 14)) >> 15;
    }
    d[H4(i)] = sadd32(env, 0, result, c[H4(i)]);
}

RVPR_ACC(kmmawt2_u, 1, 4);

/* Signed 16-bit Multiply with 32-bit Add/Subtract Instruction */
static inline void do_smbb16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    d[H4(i)] = (int32_t)a[H2(2 * i)] * b[H2(2 * i)];
}

RVPR(smbb16, 1, 4);

static inline void do_smbt16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    d[H4(i)] = (int32_t)a[H2(2 * i)] * b[H2(2 * i + 1)];
}

RVPR(smbt16, 1, 4);

static inline void do_smtt16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    d[H4(i)] = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i + 1)];
}

RVPR(smtt16, 1, 4);

static inline void do_kmda(CPURISCVState *env, void *vd, void *va,
                           void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    if (a[H2(2 * i)] == INT16_MIN && a[H2(2 * i + 1)] == INT16_MIN &&
        b[H2(2 * i)] == INT16_MIN && b[H2(2 * i + 1)] == INT16_MIN) {
        d[H4(i)] = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        d[H4(i)] = (int32_t)a[H2(2 * i)] * b[H2(2 * i)] +
                   (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i + 1)];
    }
}

RVPR(kmda, 1, 4);

static inline void do_kmxda(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    if (a[H2(2 * i)] == INT16_MIN && a[H2(2 * i + 1)] == INT16_MIN &&
        b[H2(2 * i)] == INT16_MIN && b[H2(2 * i + 1)] == INT16_MIN) {
        d[H4(i)] = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        d[H4(i)] = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i)] +
                   (int32_t)a[H2(2 * i)] * b[H2(2 * i + 1)];
    }
}

RVPR(kmxda, 1, 4);

static inline void do_smds(CPURISCVState *env, void *vd, void *va,
                           void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    d[H4(i)] = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i + 1)] -
               (int32_t)a[H2(2 * i)] * b[H2(2 * i)];
}

RVPR(smds, 1, 4);

static inline void do_smdrs(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    d[H4(i)] = (int32_t)a[H2(2 * i)] * b[H2(2 * i)] -
               (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i + 1)];
}

RVPR(smdrs, 1, 4);

static inline void do_smxds(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    d[H4(i)] = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i)] -
               (int32_t)a[H2(2 * i)] * b[H2(2 * i + 1)];
}

RVPR(smxds, 1, 4);

static inline void do_kmabb(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    d[H4(i)] = sadd32(env, 0, (int32_t)a[H2(2 * i)] * b[H2(2 * i)], c[H4(i)]);
}

RVPR_ACC(kmabb, 1, 4);

static inline void do_kmabt(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    d[H4(i)] = sadd32(env, 0, (int32_t)a[H2(2 * i)] * b[H2(2 * i + 1)],
                      c[H4(i)]);
}

RVPR_ACC(kmabt, 1, 4);

static inline void do_kmatt(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    d[H4(i)] = sadd32(env, 0, (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i + 1)],
                      c[H4(i)]);
}

RVPR_ACC(kmatt, 1, 4);

static inline void do_kmada(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    int32_t p1, p2;
    p1 = (int32_t)a[H2(2 * i)] * b[H2(2 * i)];
    p2 = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i + 1)];

    if (a[H2(i)] == INT16_MIN && a[H2(i + 1)] == INT16_MIN &&
        b[H2(i)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        if (c[H4(i)] < 0) {
            d[H4(i)] = INT32_MAX + c[H4(i)] + 1ll;
        } else {
            env->vxsat = 0x1;
            d[H4(i)] = INT32_MAX;
        }
    } else {
        d[H4(i)] = sadd32(env, 0, p1 + p2, c[H4(i)]);
    }
}

RVPR_ACC(kmada, 1, 4);

static inline void do_kmaxda(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    int32_t p1, p2;
    p1 = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i)];
    p2 = (int32_t)a[H2(2 * i)] * b[H2(2 * i + 1)];

    if (a[H2(2 * i)] == INT16_MIN && a[H2(2 * i + 1)] == INT16_MIN &&
        b[H2(2 * i)] == INT16_MIN && b[H2(2 * i + 1)] == INT16_MIN) {
        if (c[H4(i)] < 0) {
            d[H4(i)] = INT32_MAX + c[H4(i)] + 1ll;
        } else {
            env->vxsat = 0x1;
            d[H4(i)] = INT32_MAX;
        }
    } else {
        d[H4(i)] = sadd32(env, 0, p1 + p2, c[H4(i)]);
    }
}

RVPR_ACC(kmaxda, 1, 4);

static inline void do_kmads(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    int32_t p1, p2;
    p1 =   (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i + 1)];
    p2 =   (int32_t)a[H2(2 * i)] * b[H2(2 * i)];

    d[H4(i)] = sadd32(env, 0, p1 - p2, c[H4(i)]);
}

RVPR_ACC(kmads, 1, 4);

static inline void do_kmadrs(CPURISCVState *env, void *vd, void *va,
                             void *vb, void * vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    int32_t p1, p2;
    p1 = (int32_t)a[H2(2 * i)] * b[H2(2 * i)];
    p2 = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i + 1)];

    d[H4(i)] = sadd32(env, 0, p1 - p2, c[H4(i)]);
}

RVPR_ACC(kmadrs, 1, 4);

static inline void do_kmaxds(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    int32_t p1, p2;
    p1 = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i)];
    p2 = (int32_t)a[H2(2 * i)] * b[H2(2 * i + 1)];

    d[H4(i)] = sadd32(env, 0, p1 - p2, c[H4(i)]);
}

RVPR_ACC(kmaxds, 1, 4);

static inline void do_kmsda(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    int32_t p1, p2;
    p1 = (int32_t)a[H2(2 * i)] * b[H2(2 * i)];
    p2 = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i + 1)];

    if (a[H2(i)] == INT16_MIN && a[H2(i + 1)] == INT16_MIN &&
        b[H2(i)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        if (c[H4(i)] < 0) {
            env->vxsat = 0x1;
            d[H4(i)] = INT32_MIN;
        } else {
            d[H4(i)] = c[H4(i)] - 1ll - INT32_MAX;
        }
    } else {
        d[H4(i)] = ssub32(env, 0, c[H4(i)], p1 + p2);
    }
}

RVPR_ACC(kmsda, 1, 4);

static inline void do_kmsxda(CPURISCVState *env, void *vd, void *va,
                             void *vb, void * vc, uint8_t i)
{
    int32_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;
    int32_t p1, p2;
    p1 = (int32_t)a[H2(2 * i)] * b[H2(2 * i + 1)];
    p2 = (int32_t)a[H2(2 * i + 1)] * b[H2(2 * i)];

    if (a[H2(i)] == INT16_MIN && a[H2(i + 1)] == INT16_MIN &&
        b[H2(i)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        if (c[H4(i)] < 0) {
            env->vxsat = 0x1;
            d[H4(i)] = INT32_MIN;
        } else {
            d[H4(i)] = c[H4(i)] - 1ll - INT32_MAX;
        }
    } else {
        d[H4(i)] = ssub32(env, 0, c[H4(i)], p1 + p2);
    }
}

RVPR_ACC(kmsxda, 1, 4);

/* Signed 16-bit Multiply with 64-bit Add/Subtract Instructions */
static inline void do_smal(CPURISCVState *env, void *vd, void *va,
                           void *vb, uint8_t i)
{
    int64_t *d = vd, *a = va;
    int16_t *b = vb;

    if (i == 0) {
        *d = *a;
    }

    *d += b[H2(i)] * b[H2(i + 1)];
}

uint64_t helper_smal(CPURISCVState *env, uint64_t a, target_ulong b)
{
    int i;
    int64_t result = 0;

    for (i = 0; i < sizeof(target_ulong) / 2; i += 2) {
        do_smal(env, &result, &a, &b, i);
    }
    return result;
}

/* Partial-SIMD Miscellaneous Instructions */
static inline void do_sclip32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x1f;

    d[i] = sat64(env, a[i], shift);
}

RVPR(sclip32, 1, 4);

static inline void do_uclip32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x1f;

    if (a[i] < 0) {
        d[i] = 0;
        env->vxsat = 0x1;
    } else {
        d[i] = satu64(env, a[i], shift);
    }
}

RVPR(uclip32, 1, 4);

static inline void do_clrs32(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int32_t *d = vd, *a = va;
    d[i] = clrsb32(a[i]);
}

RVPR2(clrs32, 1, 4);

static inline void do_clz32(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int32_t *d = vd, *a = va;
    d[i] = clz32(a[i]);
}

RVPR2(clz32, 1, 4);

static inline void do_clo32(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int32_t *d = vd, *a = va;
    d[i] = clo32(a[i]);
}

RVPR2(clo32, 1, 4);

static inline void do_pbsad(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_ulong *d = vd;
    uint8_t *a = va, *b = vb;
    *d += abs(a[i] - b[i]);
}

RVPR(pbsad, 1, 1);

static inline void do_pbsada(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    target_ulong *d = vd, *c = vc;
    uint8_t *a = va, *b = vb;
    if (i == 0) {
        *d += *c;
    }
    *d += abs(a[i] - b[i]);
}

RVPR_ACC(pbsada, 1, 1);

/* 8-bit Multiply with 32-bit Add Instructions */
static inline void do_smaqa(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    int8_t *a = va, *b = vb;
    int32_t *d = vd, *c = vc;

    d[H4(i)] = c[H4(i)] + a[H1(i * 4)] * b[H1(i * 4)] +
               a[H1(i * 4 + 1)] * b[H1(i * 4 + 1)] +
               a[H1(i * 4 + 2)] * b[H1(i * 4 + 2)] +
               a[H1(i * 4 + 3)] * b[H1(i * 4 + 3)];
}

RVPR_ACC(smaqa, 1, 4);

static inline void do_umaqa(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    uint8_t *a = va, *b = vb;
    uint32_t *d = vd, *c = vc;

    d[H4(i)] = c[H4(i)] + a[H1(i * 4)] * b[H1(i * 4)] +
               a[H1(i * 4 + 1)] * b[H1(i * 4 + 1)] +
               a[H1(i * 4 + 2)] * b[H1(i * 4 + 2)] +
               a[H1(i * 4 + 3)] * b[H1(i * 4 + 3)];
}

RVPR_ACC(umaqa, 1, 4);

static inline void do_smaqa_su(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)
{
    int8_t *a = va;
    uint8_t *b = vb;
    int32_t *d = vd, *c = vc;

    d[H4(i)] = c[H4(i)] + a[H1(i * 4)] * b[H1(i * 4)] +
               a[H1(i * 4 + 1)] * b[H1(i * 4 + 1)] +
               a[H1(i * 4 + 2)] * b[H1(i * 4 + 2)] +
               a[H1(i * 4 + 3)] * b[H1(i * 4 + 3)];
}

RVPR_ACC(smaqa_su, 1, 4);

/*
 *** 64-bit Profile Instructions
 */
/* 64-bit Addition & Subtraction Instructions */

/* Define a common function to loop elements in packed register */
static inline uint64_t
rvpr64_64_64(CPURISCVState *env, uint64_t a, uint64_t b,
             uint8_t step, uint8_t size, PackedFn3i *fn)
{
    int i, passes = sizeof(uint64_t) / size;
    uint64_t result = 0;

    for (i = 0; i < passes; i += step) {
        fn(env, &result, &a, &b, i);
    }
    return result;
}

#define RVPR64_64_64(NAME, STEP, SIZE)                                    \
uint64_t HELPER(NAME)(CPURISCVState *env, uint64_t a, uint64_t b)         \
{                                                                         \
    return rvpr64_64_64(env, a, b, STEP, SIZE, (PackedFn3i *)do_##NAME);  \
}

static inline void do_add64(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int64_t *d = vd, *a = va, *b = vb;
    *d = *a + *b;
}

RVPR64_64_64(add64, 1, 8);

static inline int64_t hadd64(int64_t a, int64_t b)
{
    int64_t res = a + b;
    int64_t over = (res ^ a) & (res ^ b) & INT64_MIN;

    /* With signed overflow, bit 64 is inverse of bit 63. */
    return (res >> 1) ^ over;
}

static inline void do_radd64(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int64_t *d = vd, *a = va, *b = vb;
    *d = hadd64(*a, *b);
}

RVPR64_64_64(radd64, 1, 8);

static inline uint64_t haddu64(uint64_t a, uint64_t b)
{
    uint64_t res = a + b;
    bool over = res < a;

    return over ? ((res >> 1) | INT64_MIN) : (res >> 1);
}

static inline void do_uradd64(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint64_t *d = vd, *a = va, *b = vb;
    *d = haddu64(*a, *b);
}

RVPR64_64_64(uradd64, 1, 8);

static inline void do_kadd64(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int64_t *d = vd, *a = va, *b = vb;
    *d = sadd64(env, 0, *a, *b);
}

RVPR64_64_64(kadd64, 1, 8);

static inline void do_ukadd64(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint64_t *d = vd, *a = va, *b = vb;
    *d = saddu64(env, 0, *a, *b);
}

RVPR64_64_64(ukadd64, 1, 8);

static inline void do_sub64(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int64_t *d = vd, *a = va, *b = vb;
    *d = *a - *b;
}

RVPR64_64_64(sub64, 1, 8);

static inline void do_rsub64(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int64_t *d = vd, *a = va, *b = vb;
    *d = hsub64(*a, *b);
}

RVPR64_64_64(rsub64, 1, 8);

static inline void do_ursub64(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint64_t *d = vd, *a = va, *b = vb;
    if (*a >= *b) {
        *d = hsubu64(*a, *b);
    } else {
        *d = hsubu64(*a, *b) | INT64_MIN;
    }
}

RVPR64_64_64(ursub64, 1, 8);

static inline void do_ksub64(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int64_t *d = vd, *a = va, *b = vb;
    *d = ssub64(env, 0, *a, *b);
}

RVPR64_64_64(ksub64, 1, 8);

static inline void do_uksub64(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint64_t *d = vd, *a = va, *b = vb;
    *d = ssubu64(env, 0, *a, *b);
}

RVPR64_64_64(uksub64, 1, 8);

/* 32-bit Multiply with 64-bit Add/Subtract Instructions */
static inline uint64_t
rvpr64_acc(CPURISCVState *env, target_ulong a,
           target_ulong b, uint64_t c,
           uint8_t step, uint8_t size, PackedFn4i *fn)
{
    int i, passes = (env->xl == MXL_RV32) ? 4 / size :
                                            sizeof(target_ulong) / size;
    uint64_t result = 0;
    uint64_t exp_a = a, exp_b = b;

    for (i = 0; i < passes; i += step) {
        fn(env, &result, (env->xl == MXL_RV32) ? (void *)&a : (void *)&exp_a,
           (env->xl == MXL_RV32) ? (void *)&b : (void *)&exp_b, &c, i);
    }
    return result;
}

#define RVPR64_ACC(NAME, STEP, SIZE)                                     \
uint64_t HELPER(NAME)(CPURISCVState *env, target_ulong a,                \
                      target_ulong b, uint64_t c)                        \
{                                                                        \
    return rvpr64_acc(env, a, b, c, STEP, SIZE, (PackedFn4i *)do_##NAME);\
}

static inline void do_smar64(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int32_t *a = va, *b = vb;
    int64_t *d = vd, *c = vc;
    if (i == 0) {
        *d = *c;
    }
    *d += (int64_t)a[H4(i)] * b[H4(i)];
}

RVPR64_ACC(smar64, 1, 4);

static inline void do_smsr64(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int32_t *a = va, *b = vb;
    int64_t *d = vd, *c = vc;
    if (i == 0) {
        *d = *c;
    }
    *d -= (int64_t)a[H4(i)] * b[H4(i)];
}

RVPR64_ACC(smsr64, 1, 4);

static inline void do_umar64(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    uint32_t *a = va, *b = vb;
    uint64_t *d = vd, *c = vc;
    if (i == 0) {
        *d = *c;
    }
    *d += (uint64_t)a[H4(i)] * b[H4(i)];
}

RVPR64_ACC(umar64, 1, 4);

static inline void do_umsr64(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    uint32_t *a = va, *b = vb;
    uint64_t *d = vd, *c = vc;
    if (i == 0) {
        *d = *c;
    }
    *d -= (uint64_t)a[H4(i)] * b[H4(i)];
}

RVPR64_ACC(umsr64, 1, 4);

static inline void do_kmar64(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int32_t *a = va, *b = vb;
    int64_t *d = vd, *c = vc;
    int64_t m0 =  (int64_t)a[H4(i)] * b[H4(i)];
    if (env->xl != MXL_RV32) {
        int64_t m1 =  (int64_t)a[H4(i + 1)] * b[H4(i + 1)];
        if (a[H4(i)] == INT32_MIN && b[H4(i)] == INT32_MIN &&
            a[H4(i + 1)] == INT32_MIN && b[H4(i + 1)] == INT32_MIN) {
            if (*c >= 0) {
                *d = INT64_MAX;
                env->vxsat = 1;
            } else {
                *d = sadd64(env, 0, *c + m0, m1);
            }
        } else {
            *d = sadd64(env, 0, *c, m0 + m1);
        }
    } else {
        *d = sadd64(env, 0, *c, m0);
    }
}

RVPR64_ACC(kmar64, 1, sizeof(target_ulong));

static inline void do_kmsr64(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int32_t *a = va, *b = vb;
    int64_t *d = vd, *c = vc;

    int64_t m0 =  (int64_t)a[H4(i)] * b[H4(i)];
    if (env->xl != MXL_RV32) {
        int64_t m1 =  (int64_t)a[H4(i + 1)] * b[H4(i + 1)];
        if (a[H4(i)] == INT32_MIN && b[H4(i)] == INT32_MIN &&
            a[H4(i + 1)] == INT32_MIN && b[H4(i + 1)] == INT32_MIN) {
            if (*c <= 0) {
                *d = INT64_MIN;
                env->vxsat = 1;
            } else {
                *d = ssub64(env, 0, *c - m0, m1);
            }
        } else {
            *d = ssub64(env, 0, *c, m0 + m1);
        }
    } else {
        *d = ssub64(env, 0, *c, m0);
    }
}

RVPR64_ACC(kmsr64, 1, sizeof(target_ulong));

static inline void do_ukmar64(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    uint32_t *a = va, *b = vb;
    uint64_t *d = vd, *c = vc;

    if (i == 0) {
        *d = *c;
    }
    *d = saddu64(env, 0, *d, (uint64_t)a[H4(i)] * b[H4(i)]);
}

RVPR64_ACC(ukmar64, 1, 4);

static inline void do_ukmsr64(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    uint32_t *a = va, *b = vb;
    uint64_t *d = vd, *c = vc;

    if (i == 0) {
        *d = *c;
    }
    *d = ssubu64(env, 0, *d, (uint64_t)a[i] * b[i]);
}

RVPR64_ACC(ukmsr64, 1, 4);

/* Signed 16-bit Multiply with 64-bit Add/Subtract Instructions */
static inline void do_smalbb(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d += (int64_t)a[H2(i)] * b[H2(i)];
}

RVPR64_ACC(smalbb, 2, 2);

static inline void do_smalbt(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d += (int64_t)a[H2(i)] * b[H2(i + 1)];
}

RVPR64_ACC(smalbt, 2, 2);

static inline void do_smaltt(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d += (int64_t)a[H2(i + 1)] * b[H2(i + 1)];
}

RVPR64_ACC(smaltt, 2, 2);

static inline void do_smalda(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d += (int64_t)a[H2(i)] * b[H2(i)] + (int64_t)a[H2(i + 1)] * b[H2(i + 1)];
}

RVPR64_ACC(smalda, 2, 2);

static inline void do_smalxda(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d += (int64_t)a[H2(i)] * b[H2(i + 1)] + (int64_t)a[H2(i + 1)] * b[H2(i)];
}

RVPR64_ACC(smalxda, 2, 2);

static inline void do_smalds(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d += (int64_t)a[H2(i + 1)] * b[H2(i + 1)] - (int64_t)a[H2(i)] * b[H2(i)];
}

RVPR64_ACC(smalds, 2, 2);

static inline void do_smaldrs(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d += (int64_t)a[H2(i)] * b[H2(i)] - (int64_t)a[H2(i + 1)] * b[H2(i + 1)];
}

RVPR64_ACC(smaldrs, 2, 2);

static inline void do_smalxds(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d += (int64_t)a[H2(i + 1)] * b[H2(i)] - (int64_t)a[H2(i)] * b[H2(i + 1)];
}

RVPR64_ACC(smalxds, 2, 2);

static inline void do_smslda(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d -= (int64_t)a[H2(i)] * b[H2(i)] + (int64_t)a[H2(i + 1)] * b[H2(i + 1)];
}

RVPR64_ACC(smslda, 2, 2);

static inline void do_smslxda(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int16_t *a = va, *b = vb;

    if (i == 0) {
        *d = *c;
    }

    *d -= (int64_t)a[H2(i + 1)] * b[H2(i)] + (int64_t)a[H2(i)] * b[H2(i + 1)];
}

RVPR64_ACC(smslxda, 2, 2);

/* Q15 saturation instructions */
static inline void do_kaddh(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int32_t *a = va, *b = vb;

    *d = sat64(env, (int64_t)a[H4(i)] + b[H4(i)], 15);
}

RVPR(kaddh, 2, 4);

static inline void do_ksubh(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int32_t *a = va, *b = vb;

    *d = sat64(env, (int64_t)a[H4(i)] - b[H4(i)], 15);
}

RVPR(ksubh, 2, 4);

static inline void do_khmbb(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int16_t *a = va, *b = vb;

    *d = sat64(env, (int64_t)a[H2(i)] * b[H2(i)] >> 15, 15);
}

RVPR(khmbb, 4, 2);

static inline void do_khmbt(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int16_t *a = va, *b = vb;

    *d = sat64(env, (int64_t)a[H2(i)] * b[H2(i + 1)] >> 15, 15);
}

RVPR(khmbt, 4, 2);

static inline void do_khmtt(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int16_t *a = va, *b = vb;

    *d = sat64(env, (int64_t)a[H2(i + 1)] * b[H2(i + 1)] >> 15, 15);
}

RVPR(khmtt, 4, 2);

static inline void do_ukaddh(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    target_long *d = vd;
    uint32_t *a = va, *b = vb;

    *d = (int16_t)satu64(env, saddu32(env, 0, a[H4(i)], b[H4(i)]), 16);
}

RVPR(ukaddh, 2, 4);

static inline void do_uksubh(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    target_long *d = vd;
    uint32_t *a = va, *b = vb;

    *d = (int16_t)satu64(env, ssubu32(env, 0, a[H4(i)], b[H4(i)]), 16);
}

RVPR(uksubh, 2, 4);

/* Q31 saturation Instructions */
static inline void do_kaddw(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int32_t *a = va, *b = vb;

    *d = sadd32(env, 0, a[H4(i)], b[H4(i)]);
}

RVPR(kaddw, 2, 4);

static inline void do_ukaddw(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    target_long *d = vd;
    uint32_t *a = va, *b = vb;

    *d = (int32_t)saddu32(env, 0, a[H4(i)], b[H4(i)]);
}

RVPR(ukaddw, 2, 4);

static inline void do_ksubw(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int32_t *a = va, *b = vb;

    *d = ssub32(env, 0, a[H4(i)], b[H4(i)]);
}

RVPR(ksubw, 2, 4);

static inline void do_uksubw(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    uint32_t *a = va, *b = vb;

    *d = (int32_t)ssubu32(env, 0, a[H4(i)], b[H4(i)]);
}

RVPR(uksubw, 2, 4);

static inline void do_kdmbb(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int16_t *a = va, *b = vb;

    if (a[H2(i)] == INT16_MIN && b[H2(i)] == INT16_MIN) {
        *d = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        *d = (int64_t)a[H2(i)] * b[H2(i)] << 1;
    }
}

RVPR(kdmbb, 4, 2);

static inline void do_kdmbt(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int16_t *a = va, *b = vb;

    if (a[H2(i)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        *d = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        *d = (int64_t)a[H2(i)] * b[H2(i + 1)] << 1;
    }
}

RVPR(kdmbt, 4, 2);

static inline void do_kdmtt(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int16_t *a = va, *b = vb;

    if (a[H2(i + 1)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        *d = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        *d = (int64_t)a[H2(i + 1)] * b[H2(i + 1)] << 1;
    }
}

RVPR(kdmtt, 4, 2);

static inline void do_kslraw(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    target_long *d = vd;
    int32_t *a = va;
    int32_t shift = sextract32((*(uint32_t *)vb), 0, 6);

    if (shift >= 0) {
        *d = (int32_t)sat64(env, (int64_t)a[H4(i)] << shift, 31);
    } else {
        shift = -shift;
        shift = (shift == 32) ? 31 : shift;
        *d = a[H4(i)] >> shift;
    }
}

RVPR(kslraw, 2, 4);

static inline void do_kslraw_u(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    target_long *d = vd;
    int32_t *a = va;
    int32_t shift = sextract32((*(uint32_t *)vb), 0, 6);

    if (shift >= 0) {
        *d = (int32_t)sat64(env, (int64_t)a[H4(i)] << shift, 31);
    } else {
        shift = -shift;
        shift = (shift == 32) ? 31 : shift;
        *d = vssra32(env, 0, a[H4(i)], shift);
    }
}

RVPR(kslraw_u, 2, 4);

static inline void do_ksllw(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int32_t *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x1f;

    *d = (int32_t)sat64(env, (int64_t)a[H4(i)] << shift, 31);
}

RVPR(ksllw, 2, 4);

static inline void do_kdmabb(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)

{
    target_long *d = vd;
    int16_t *a = va, *b = vb;
    int32_t *c = vc, m0;

    if (a[H2(i)] == INT16_MIN && b[H2(i)] == INT16_MIN) {
        m0 = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        m0 = (int32_t)a[H2(i)] * b[H2(i)] << 1;
    }
    *d = sadd32(env, 0, c[H4(i)], m0);
}

RVPR_ACC(kdmabb, 4, 2);

static inline void do_kdmabt(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)

{
    target_long *d = vd;
    int16_t *a = va, *b = vb;
    int32_t *c = vc, m0;

    if (a[H2(i)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        m0 = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        m0 = (int32_t)a[H2(i)] * b[H2(i + 1)] << 1;
    }
    *d = sadd32(env, 0, c[H4(i)], m0);
}

RVPR_ACC(kdmabt, 4, 2);

static inline void do_kdmatt(CPURISCVState *env, void *vd, void *va,
                             void *vb, void *vc, uint8_t i)

{
    target_long *d = vd;
    int16_t *a = va, *b = vb;
    int32_t *c = vc, m0;

    if (a[H2(i + 1)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        m0 = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        m0 = (int32_t)a[H2(i + 1)] * b[H2(i + 1)] << 1;
    }
    *d = sadd32(env, 0, c[H4(i)], m0);
}

RVPR_ACC(kdmatt, 4, 2);

static inline void do_kabsw(CPURISCVState *env, void *vd, void *va, uint8_t i)

{
    target_long *d = vd;
    int32_t *a = va;

    if (a[H4(i)] == INT32_MIN) {
        *d = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        *d = (int32_t)abs(a[H4(i)]);
    }
}

RVPR2(kabsw, 2, 4);

/* 32-bit Computation Instructions */
static inline void do_raddw(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int32_t *a = va, *b = vb;
    target_long *d = vd;

    *d = hadd32(a[H4(i)], b[H4(i)]);
}

RVPR(raddw, 2, 4);

static inline void do_uraddw(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *a = va, *b = vb;
    target_long *d = vd;

    *d = (int32_t)haddu32(a[H4(i)], b[H4(i)]);
}

RVPR(uraddw, 2, 4);

static inline void do_rsubw(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int32_t *a = va, *b = vb;
    target_long *d = vd;

    *d = hsub32(a[H4(i)], b[H4(i)]);
}

RVPR(rsubw, 2, 4);

static inline void do_ursubw(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *a = va, *b = vb;
    target_long *d = vd;

    *d = (int32_t)hsubu64(a[H4(i)], b[H4(i)]);
}

RVPR(ursubw, 2, 4);

static inline void do_maxw(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd;
    int32_t *a = va, *b = vb;

    *d = (a[H4(i)] > b[H4(i)]) ? a[H4(i)] : b[H4(i)];
}

RVPR(maxw, 2, 4);

static inline void do_minw(CPURISCVState *env, void *vd, void *va,
                           void *vb, uint8_t i)
{
    target_long *d = vd;
    int32_t *a = va, *b = vb;

    *d = (a[H4(i)] < b[H4(i)]) ? a[H4(i)] : b[H4(i)];
}

RVPR(minw, 2, 4);

static inline void do_mulr64(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint64_t *d = vd;
    uint32_t *a = va, *b = vb;

    *d = (uint64_t)a[H4(0)] * b[H4(0)];
}

RVPR64(mulr64);

static inline void do_mulsr64(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd;
    int64_t result;
    int32_t *a = va, *b = vb;

    result = (int64_t)a[H4(0)] * b[H4(0)];
    d[H4(1)] = result >> 32;
    d[H4(0)] = result & UINT32_MAX;
}

RVPR64(mulsr64);

/* Miscellaneous Instructions */
static inline void do_ave(CPURISCVState *env, void *vd, void *va,
                          void *vb, uint8_t i)
{
    target_long *d = vd, *a = va, *b = vb, half;

    half = hadd64(*a, *b);
    if ((*a ^ *b) & 0x1) {
        half++;
    }
    *d = half;
}

RVPR(ave, 1, sizeof(target_ulong));

static inline void do_sra_u(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    target_long *d = vd, *a = va;
    uint8_t *b = vb;
    uint8_t shift = env->xl == MXL_RV32 ? (*b & 0x1f) : (*b & 0x3f);

    *d = vssra64(env, 0, *a, shift);
}

RVPR(sra_u, 1, sizeof(target_ulong));

static inline void do_bitrev(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    target_ulong *d = vd, *a = va;
    uint8_t *b = vb;
    uint8_t shift = env->xl == MXL_RV32 ? (*b & 0x1f) : (*b & 0x3f);

    *d = revbit64(*a) >> (64 - shift - 1);
}

RVPR(bitrev, 1, sizeof(target_ulong));

static inline target_ulong
rvpr_64(CPURISCVState *env, uint64_t a, target_ulong b, PackedFn3 *fn)
{
    target_ulong result = 0;

    fn(env, &result, &a, &b);
    return result;
}

#define RVPR_64(NAME)                                       \
target_ulong HELPER(NAME)(CPURISCVState *env, uint64_t a,   \
                          target_ulong b)                   \
{                                                           \
    return rvpr_64(env, a, b, (PackedFn3 *)do_##NAME);      \
}

static inline void do_wext(CPURISCVState *env, void *vd, void *va,
                           void *vb, uint8_t i)
{
    target_long *d = vd;
    int64_t *a = va;
    uint8_t b = *(uint8_t *)vb & 0x1f;

    *d = sextract64(*a, b, 32);
}

RVPR_64(wext);

static inline void do_bpick(CPURISCVState *env, void *vd, void *va,
                            void *vb, void *vc, uint8_t i)
{
    target_long *d = vd, *a = va, *b = vb, *c = vc;

    *d = (*c & *a) | (~*c & *b);
}

RVPR_ACC(bpick, 1, sizeof(target_ulong));

/*
 *** RV64 Only Instructions
 */
/* (RV64 Only) SIMD 32-bit Add/Subtract Instructions */
static inline void do_radd32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint16_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[i] = hadd32(a[i], b[i]);
}

RVPR64_64_64(radd32, 1, 4);

static inline void do_uradd32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint16_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[i] = haddu32(a[i], b[i]);
}

RVPR64_64_64(uradd32, 1, 4);

static inline void do_kadd32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint16_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[i] = sadd32(env, 0, a[i], b[i]);
}

RVPR64_64_64(kadd32, 1, 4);

static inline void do_ukadd32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint16_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[i] = saddu32(env, 0, a[i], b[i]);
}

RVPR64_64_64(ukadd32, 1, 4);

static inline void do_rsub32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint16_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[i] = hsub32(a[i], b[i]);
}

RVPR64_64_64(rsub32, 1, 4);

static inline void do_ursub32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint16_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[i] = hsubu64(a[i], b[i]);
}

RVPR64_64_64(ursub32, 1, 4);

static inline void do_ksub32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint16_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[i] = ssub32(env, 0, a[i], b[i]);
}

RVPR64_64_64(ksub32, 1, 4);

static inline void do_uksub32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint16_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[i] = ssubu32(env, 0, a[i], b[i]);
}

RVPR64_64_64(uksub32, 1, 4);

static inline void do_cras32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = a[H4(i)] - b[H4(i + 1)];
    d[H4(i + 1)] = a[H4(i + 1)] + b[H4(i)];
}

RVPR64_64_64(cras32, 2, 4);

static inline void do_rcras32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = hsub32(a[H4(i)], b[H4(i + 1)]);
    d[H4(i + 1)] = hadd32(a[H4(i + 1)], b[H4(i)]);
}

RVPR64_64_64(rcras32, 2, 4);

static inline void do_urcras32(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = hsubu64(a[H4(i)], b[H4(i + 1)]);
    d[H4(i + 1)] = haddu32(a[H4(i + 1)], b[H4(i)]);
}

RVPR64_64_64(urcras32, 2, 4);

static inline void do_kcras32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = ssub32(env, 0, a[H4(i)], b[H4(i + 1)]);
    d[H4(i + 1)] = sadd32(env, 0, a[H4(i + 1)], b[H4(i)]);
}

RVPR64_64_64(kcras32, 2, 4);

static inline void do_ukcras32(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = ssubu32(env, 0, a[H4(i)], b[H4(i + 1)]);
    d[H4(i + 1)] = saddu32(env, 0, a[H4(i + 1)], b[H4(i)]);
}

RVPR64_64_64(ukcras32, 2, 4);

static inline void do_crsa32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = a[H4(i)] + b[H4(i + 1)];
    d[H4(i + 1)] = a[H4(i + 1)] - b[H4(i)];
}

RVPR64_64_64(crsa32, 2, 4);

static inline void do_rcrsa32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = hadd32(a[H4(i)], b[H4(i + 1)]);
    d[H4(i + 1)] = hsub32(a[H4(i + 1)], b[H4(i)]);
}

RVPR64_64_64(rcrsa32, 2, 4);

static inline void do_urcrsa32(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = haddu32(a[H4(i)], b[H4(i + 1)]);
    d[H4(i + 1)] = hsubu64(a[H4(i + 1)], b[H4(i)]);
}

RVPR64_64_64(urcrsa32, 2, 4);

static inline void do_kcrsa32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = sadd32(env, 0, a[H4(i)], b[H4(i + 1)]);
    d[H4(i + 1)] = ssub32(env, 0, a[H4(i + 1)], b[H4(i)]);
}

RVPR64_64_64(kcrsa32, 2, 4);

static inline void do_ukcrsa32(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = saddu32(env, 0, a[H4(i)], b[H4(i + 1)]);
    d[H4(i + 1)] = ssubu32(env, 0, a[H4(i + 1)], b[H4(i)]);
}

RVPR64_64_64(ukcrsa32, 2, 4);

static inline void do_stas32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = a[H4(i)] - b[H4(i)];
    d[H4(i + 1)] = a[H4(i + 1)] + b[H4(i + 1)];
}

RVPR64_64_64(stas32, 2, 4);

static inline void do_rstas32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = hsub32(a[H4(i)], b[H4(i)]);
    d[H4(i + 1)] = hadd32(a[H4(i + 1)], b[H4(i + 1)]);
}

RVPR64_64_64(rstas32, 2, 4);

static inline void do_urstas32(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = hsubu64(a[H4(i)], b[H4(i)]);
    d[H4(i + 1)] = haddu32(a[H4(i + 1)], b[H4(i + 1)]);
}

RVPR64_64_64(urstas32, 2, 4);

static inline void do_kstas32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = ssub32(env, 0, a[H4(i)], b[H4(i)]);
    d[H4(i + 1)] = sadd32(env, 0, a[H4(i + 1)], b[H4(i + 1)]);
}

RVPR64_64_64(kstas32, 2, 4);

static inline void do_ukstas32(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = ssubu32(env, 0, a[H4(i)], b[H4(i)]);
    d[H4(i + 1)] = saddu32(env, 0, a[H4(i + 1)], b[H4(i + 1)]);
}

RVPR64_64_64(ukstas32, 2, 4);

static inline void do_stsa32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = a[H4(i)] + b[H4(i)];
    d[H4(i + 1)] = a[H4(i + 1)] - b[H4(i + 1)];
}

RVPR64_64_64(stsa32, 2, 4);

static inline void do_rstsa32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = hadd32(a[H4(i)], b[H4(i)]);
    d[H4(i + 1)] = hsub32(a[H4(i + 1)], b[H4(i + 1)]);
}

RVPR64_64_64(rstsa32, 2, 4);

static inline void do_urstsa32(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = haddu32(a[H4(i)], b[H4(i)]);
    d[H4(i + 1)] = hsubu64(a[H4(i + 1)], b[H4(i + 1)]);
}

RVPR64_64_64(urstsa32, 2, 4);

static inline void do_kstsa32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = sadd32(env, 0, a[H4(i)], b[H4(i)]);
    d[H4(i + 1)] = ssub32(env, 0, a[H4(i + 1)], b[H4(i + 1)]);
}

RVPR64_64_64(kstsa32, 2, 4);

static inline void do_ukstsa32(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = saddu32(env, 0, a[H4(i)], b[H4(i)]);
    d[H4(i + 1)] = ssubu32(env, 0, a[H4(i + 1)], b[H4(i + 1)]);
}

RVPR64_64_64(ukstsa32, 2, 4);

/* (RV64 Only) SIMD 32-bit Shift Instructions */
static inline void do_sra32(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x1f;
    d[i] = a[i] >> shift;
}

RVPR64_64_64(sra32, 1, 4);

static inline void do_srl32(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x1f;
    d[i] = a[i] >> shift;
}

RVPR64_64_64(srl32, 1, 4);

static inline void do_sll32(CPURISCVState *env, void *vd, void *va,
                            void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x1f;
    d[i] = a[i] << shift;
}

RVPR64_64_64(sll32, 1, 4);

static inline void do_sra32_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x1f;

    d[i] = vssra32(env, 0, a[i], shift);
}

RVPR64_64_64(sra32_u, 1, 4);

static inline void do_srl32_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va;
    uint8_t shift = *(uint8_t *)vb & 0x1f;

    d[i] = vssrl32(env, 0, a[i], shift);
}

RVPR64_64_64(srl32_u, 1, 4);

static inline void do_ksll32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, result;
    uint8_t shift = *(uint64_t *)vb & 0x1f;

    result = a[i] << shift;
    if (shift > clrsb32(a[i])) {
        env->vxsat = 0x1;
        d[i] = (a[i] & INT32_MIN) ? INT32_MIN : INT32_MAX;
    } else {
        d[i] = result;
    }
}

RVPR64_64_64(ksll32, 1, 4);

static inline void do_kslra32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va;
    int64_t shift = sextract64(*(uint64_t *)vb, 0, 6);

    if (shift >= 0) {
        do_ksll32(env, vd, va, vb, i);
    } else {
        shift = -shift;
        shift = (shift == 32) ? 31 : shift;
        d[i] = a[i] >> shift;
    }
}

RVPR64_64_64(kslra32, 1, 4);

static inline void do_kslra32_u(CPURISCVState *env, void *vd, void *va,
                                void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va;
    int32_t shift = sextract32((*(uint32_t *)vb), 0, 6);

    if (shift >= 0) {
        do_ksll32(env, vd, va, vb, i);
    } else {
        shift = -shift;
        shift = (shift == 32) ? 31 : shift;
        d[i] = vssra32(env, 0, a[i], shift);
    }
}

RVPR64_64_64(kslra32_u, 1, 4);

/* (RV64 Only) SIMD 32-bit Miscellaneous Instructions */
static inline void do_smin32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] < b[i]) ? a[i] : b[i];
}

RVPR64_64_64(smin32, 1, 4);

static inline void do_umin32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] < b[i]) ? a[i] : b[i];
}

RVPR64_64_64(umin32, 1, 4);

static inline void do_smax32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int32_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] > b[i]) ? a[i] : b[i];
}

RVPR64_64_64(smax32, 1, 4);

static inline void do_umax32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;

    d[i] = (a[i] > b[i]) ? a[i] : b[i];
}

RVPR64_64_64(umax32, 1, 4);

static inline void do_kabs32(CPURISCVState *env, void *vd, void *va, uint8_t i)
{
    int32_t *d = vd, *a = va;

    if (a[i] == INT32_MIN) {
        d[i] = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        d[i] = abs(a[i]);
    }
}

RVPR2(kabs32, 1, 4);

/* (RV64 Only) SIMD Q15 saturating Multiply Instructions */
static inline void do_khmbb16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;

    d[H4(i / 2)] = sat64(env, (int64_t)a[H2(i)] * b[H2(i)] >> 15, 15);
}

RVPR64_64_64(khmbb16, 2, 2);

static inline void do_khmbt16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;

    d[H4(i / 2)] = sat64(env, (int64_t)a[H2(i)] * b[H2(i + 1)] >> 15, 15);
}

RVPR64_64_64(khmbt16, 2, 2);

static inline void do_khmtt16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;

    d[H4(i / 2)] = sat64(env, (int64_t)a[H2(i + 1)] * b[H2(i + 1)] >> 15, 15);
}

RVPR64_64_64(khmtt16, 2, 2);

static inline void do_kdmbb16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;

    if (a[H2(i)] == INT16_MIN && b[H2(i)] == INT16_MIN) {
        d[H4(i / 2)] = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        d[H4(i / 2)] = (int64_t)a[H2(i)] * b[H2(i)] << 1;
    }
}

RVPR64_64_64(kdmbb16, 2, 2);

static inline void do_kdmbt16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;

    if (a[H2(i)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        d[H4(i / 2)] = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        d[H4(i / 2)] = (int64_t)a[H2(i)] * b[H2(i + 1)] << 1;
    }
}

RVPR64_64_64(kdmbt16, 2, 2);

static inline void do_kdmtt16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;

    if (a[H2(i + 1)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        d[H4(i / 2)] = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        d[H4(i / 2)] = (int64_t)a[H2(i + 1)] * b[H2(i + 1)] << 1;
    }
}

RVPR64_64_64(kdmtt16, 2, 2);

static inline void do_kdmabb16(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)

{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    int32_t *c = vc, m0;

    if (a[H2(i)] == INT16_MIN && b[H2(i)] == INT16_MIN) {
        m0 = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        m0 = (int32_t)a[H2(i)] * b[H2(i)] << 1;
    }
    d[H4(i / 2)] = sadd32(env, 0, c[H4(i / 2)], m0);
}

RVPR_ACC(kdmabb16, 2, 2);

static inline void do_kdmabt16(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)

{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    int32_t *c = vc, m0;

    if (a[H2(i)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        m0 = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        m0 = (int32_t)a[H2(i)] * b[H2(i + 1)] << 1;
    }
    d[H4(i / 2)] = sadd32(env, 0, c[H4(i / 2)], m0);
}

RVPR_ACC(kdmabt16, 2, 2);

static inline void do_kdmatt16(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)

{
    int32_t *d = vd;
    int16_t *a = va, *b = vb;
    int32_t *c = vc, m0;

    if (a[H2(i + 1)] == INT16_MIN && b[H2(i + 1)] == INT16_MIN) {
        m0 = INT32_MAX;
        env->vxsat = 0x1;
    } else {
        m0 = (int32_t)a[H2(i + 1)] * b[H2(i + 1)] << 1;
    }
    d[H4(i / 2)] = sadd32(env, 0, c[H4(i / 2)], m0);
}

RVPR_ACC(kdmatt16, 2, 2);

/* (RV64 Only) 32-bit Multiply Instructions */
static inline void do_smbt32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int64_t *d = vd;
    int32_t *a = va, *b = vb;
    *d = (int64_t)a[H4(2 * i)] * b[H4(2 * i + 1)];
}

RVPR64_64_64(smbt32, 1, 8);

static inline void do_smtt32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int64_t *d = vd;
    int32_t *a = va, *b = vb;
    *d = (int64_t)a[H4(2 * i + 1)] * b[H4(2 * i + 1)];
}

RVPR64_64_64(smtt32, 1, 8);

/* (RV64 Only) 32-bit Multiply & Add Instructions */
static inline void do_kmabb32(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int32_t *a = va, *b = vb;
    *d = sadd64(env, 0, (int64_t)a[H4(2 * i)] * b[H4(2 * i)], *c);
}

RVPR_ACC(kmabb32, 1, 8);

static inline void do_kmabt32(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int32_t *a = va, *b = vb;
    *d = sadd64(env, 0, (int64_t)a[H4(2 * i)] * b[H4(2 * i + 1)], *c);
}

RVPR_ACC(kmabt32, 1, 8);

static inline void do_kmatt32(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int32_t *a = va, *b = vb;
    *d = sadd64(env, 0, (int64_t)a[H4(2 * i + 1)] * b[H4(2 * i + 1)], *c);
}

RVPR_ACC(kmatt32, 1, 8);

/* (RV64 Only) 32-bit Parallel Multiply & Add Instructions */
static inline void do_kmda32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int64_t *d = vd;
    int32_t *a = va, *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H4(i)] == INT32_MIN &&
        a[H4(i + 1)] == INT32_MIN && b[H4(i + 1)] == INT32_MIN) {
        *d = INT64_MAX;
        env->vxsat = 0x1;
    } else {
        *d = (int64_t)a[H4(i)] * b[H4(i)] +
             (int64_t)a[H4(i + 1)] * b[H4(i + 1)];
    }
}

RVPR64_64_64(kmda32, 1, 8);

static inline void do_kmxda32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int64_t *d = vd;
    int32_t *a = va, *b = vb;
    if (a[H4(i)] == INT32_MIN && b[H4(i)] == INT32_MIN &&
        a[H4(i + 1)] == INT32_MIN && b[H4(i + 1)] == INT32_MIN) {
        *d = INT64_MAX;
        env->vxsat = 0x1;
    } else {
        *d = (int64_t)a[H4(i)] * b[H4(i + 1)] +
             (int64_t)a[H4(i + 1)] * b[H4(i)];
    }
}

RVPR64_64_64(kmxda32, 1, 8);

static inline void do_kmaxda32(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int32_t *a = va, *b = vb;
    int64_t p1, p2;
    p1 = (int64_t)a[H4(i)] * b[H4(i + 1)];
    p2 = (int64_t)a[H4(i + 1)] * b[H4(i)];

    if (a[H4(i)] == INT32_MIN && a[H4(i + 1)] == INT32_MIN &&
        b[H4(i)] == INT32_MIN && b[H4(i + 1)] == INT32_MIN) {
        if (*d < 0) {
            *d = (INT64_MAX + *c) + 1ll;
        } else {
            env->vxsat = 0x1;
            *d = INT64_MAX;
        }
    } else {
        *d = sadd64(env, 0, p1 + p2, *c);
    }
}

RVPR_ACC(kmaxda32, 1, 8);

static inline void do_kmads32(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int32_t *a = va, *b = vb;
    int64_t t0, t1;
    t1 = (int64_t)a[H4(i + 1)] * b[H4(i + 1)];
    t0 = (int64_t)a[H4(i)] * b[H4(i)];

    *d = sadd64(env, 0, t1 - t0, *c);
}

RVPR_ACC(kmads32, 1, 8);

static inline void do_kmadrs32(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int32_t *a = va, *b = vb;
    int64_t t0, t1;
    t1 = (int64_t)a[H4(i + 1)] * b[H4(i + 1)];
    t0 = (int64_t)a[H4(i)] * b[H4(i)];

    *d = sadd64(env, 0, t0 - t1, *c);
}

RVPR_ACC(kmadrs32, 1, 8);

static inline void do_kmaxds32(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int32_t *a = va, *b = vb;
    int64_t t01, t10;
    t01 = (int64_t)a[H4(i)] * b[H4(i + 1)];
    t10 = (int64_t)a[H4(i + 1)] * b[H4(i)];

    *d = sadd64(env, 0, t10 - t01, *c);
}

RVPR_ACC(kmaxds32, 1, 8);

static inline void do_kmsda32(CPURISCVState *env, void *vd, void *va,
                              void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int32_t *a = va, *b = vb;
    int64_t t0, t1;
    t0 = (int64_t)a[H4(i)] * b[H4(i)];
    t1 = (int64_t)a[H4(i + 1)] * b[H4(i + 1)];

    if (a[H4(i)] == INT32_MIN && a[H4(i + 1)] == INT32_MIN &&
        b[H4(i)] == INT32_MIN && b[H4(i + 1)] == INT32_MIN) {
        if (*c < 0) {
            env->vxsat = 0x1;
            *d = INT64_MIN;
        } else {
            *d = *c - 1ll - INT64_MAX;
        }
    } else {
        *d = ssub64(env, 0, *c, t0 + t1);
    }
}

RVPR_ACC(kmsda32, 1, 8);

static inline void do_kmsxda32(CPURISCVState *env, void *vd, void *va,
                               void *vb, void *vc, uint8_t i)
{
    int64_t *d = vd, *c = vc;
    int32_t *a = va, *b = vb;
    int64_t t01, t10;
    t10 = (int64_t)a[H4(i + 1)] * b[H4(i)];
    t01 = (int64_t)a[H4(i)] * b[H4(i + 1)];

    if (a[H4(i)] == INT32_MIN && a[H4(i + 1)] == INT32_MIN &&
        b[H4(i)] == INT32_MIN && b[H4(i + 1)] == INT32_MIN) {
        if (*c < 0) {
            env->vxsat = 0x1;
            *d = INT64_MIN;
        } else {
            *d = *c - 1ll - INT64_MAX;
        }
    } else {
        *d = ssub64(env, 0, *c, t10 + t01);
    }
}

RVPR_ACC(kmsxda32, 1, 8);

static inline void do_smds32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int64_t *d = vd;
    int32_t *a = va, *b = vb;
    *d = (int64_t)a[H4(i + 1)] * b[H4(i + 1)] -
         (int64_t)a[H4(i)] * b[H4(i)];
}

RVPR64_64_64(smds32, 1, 8);

static inline void do_smdrs32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int64_t *d = vd;
    int32_t *a = va, *b = vb;
    *d = (int64_t)a[H4(i)] * b[H4(i)] -
         (int64_t)a[H4(i + 1)] * b[H4(i + 1)];
}

RVPR64_64_64(smdrs32, 1, 8);

static inline void do_smxds32(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int64_t *d = vd;
    int32_t *a = va, *b = vb;
    *d = (int64_t)a[H4(i + 1)] * b[H4(i)] -
         (int64_t)a[H4(i)] * b[H4(i + 1)];
}

RVPR64_64_64(smxds32, 1, 8);

/* (RV64 Only) Non-SIMD 32-bit Shift Instructions */
static inline void do_sraiw_u(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int64_t *d = vd;
    int32_t *a = va;
    uint8_t shift = *(uint8_t *)vb;

    *d = vssra32(env, 0, a[H4(i)], shift);
}

RVPR64_64_64(sraiw_u, 1, 8);

/* (RV64 Only)  32-bit packing instructions here */
static inline void do_pkbb32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = b[H4(i)];
    d[H4(i + 1)] = a[H4(i)];
}

RVPR64_64_64(pkbb32, 2, 4);

static inline void do_pkbt32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = b[H4(i + 1)];
    d[H4(i + 1)] = a[H4(i)];
}

RVPR64_64_64(pkbt32, 2, 4);

static inline void do_pktb32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = b[H4(i)];
    d[H4(i + 1)] = a[H4(i + 1)];
}

RVPR64_64_64(pktb32, 2, 4);

static inline void do_pktt32(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint32_t *d = vd, *a = va, *b = vb;
    d[H4(i)] = b[H4(i + 1)];
    d[H4(i + 1)] = a[H4(i + 1)];
}

RVPR64_64_64(pktt32, 2, 4);
