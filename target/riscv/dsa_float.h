/*
 * RISC-V DSA float Helpers for QEMU.
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

#ifndef RISCV_DSA_FLOAT_H
#define RISCV_DSA_FLOAT_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "fpu/softfloat.h"


/* The softfloat api that qemu supply for dsa */
typedef struct qemu_float_ops {
    float_status *fp_status;
    float_status *mfp_status;
    void (*float_raise)(uint16_t flags, float_status *status);

/*----------------------------------------------------------------------------
| If `a' is denormal and we are in flush-to-zero mode then set the
| input-denormal exception and return zero. Otherwise just return the value.
*----------------------------------------------------------------------------*/
    float16 (*float16_squash_input_denormal)(float16 a, float_status *status);
    float32 (*float32_squash_input_denormal)(float32 a, float_status *status);
    float64 (*float64_squash_input_denormal)(float64 a, float_status *status);
    bfloat16 (*bfloat16_squash_input_denormal)(bfloat16 a,
                                               float_status *status);
    float8e4 (*float8e4_squash_input_denormal)(float8e4 a,
                                               float_status *status);
    float8e5 (*float8e5_squash_input_denormal)(float8e5 a,
                                               float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE integer-to-floating-point conversion routines.
*----------------------------------------------------------------------------*/
    /*
     * conversion use the settings defined in fcsr
     * (*int16_to_float16)(int8_t a, status) =
     *              (*int16_to_float16_scalbn)(int16_t a, 0, status)
     * "int" refers to "int scale"
     */
    float16 (*int16_to_float16_scalbn)(int16_t a, int, float_status *status);
    float16 (*int32_to_float16_scalbn)(int32_t a, int, float_status *status);
    float16 (*int64_to_float16_scalbn)(int64_t a, int, float_status *status);
    float16 (*uint16_to_float16_scalbn)(uint16_t a, int, float_status *status);
    float16 (*uint32_to_float16_scalbn)(uint32_t a, int, float_status *status);
    float16 (*uint64_to_float16_scalbn)(uint64_t a, int, float_status *status);
    float16 (*int8_to_float16)(int8_t a, float_status *status);
    float16 (*int16_to_float16)(int16_t a, float_status *status);
    float16 (*int32_to_float16)(int32_t a, float_status *status);
    float16 (*int64_to_float16)(int64_t a, float_status *status);
    float16 (*uint8_to_float16)(uint8_t a, float_status *status);
    float16 (*uint16_to_float16)(uint16_t a, float_status *status);
    float16 (*uint32_to_float16)(uint32_t a, float_status *status);
    float16 (*uint64_to_float16)(uint64_t a, float_status *status);

    float32 (*int16_to_float32_scalbn)(int16_t, int, float_status *status);
    float32 (*int32_to_float32_scalbn)(int32_t, int, float_status *status);
    float32 (*int64_to_float32_scalbn)(int64_t, int, float_status *status);
    float32 (*uint16_to_float32_scalbn)(uint16_t, int, float_status *status);
    float32 (*uint32_to_float32_scalbn)(uint32_t, int, float_status *status);
    float32 (*uint64_to_float32_scalbn)(uint64_t, int, float_status *status);
    float32 (*int16_to_float32)(int16_t, float_status *status);
    float32 (*int32_to_float32)(int32_t, float_status *status);
    float32 (*int64_to_float32)(int64_t, float_status *status);
    float32 (*uint16_to_float32)(uint16_t, float_status *status);
    float32 (*uint32_to_float32)(uint32_t, float_status *status);
    float32 (*uint64_to_float32)(uint64_t, float_status *status);

    float64 (*int16_to_float64_scalbn)(int16_t, int, float_status *status);
    float64 (*int32_to_float64_scalbn)(int32_t, int, float_status *status);
    float64 (*int64_to_float64_scalbn)(int64_t, int, float_status *status);
    float64 (*uint16_to_float64_scalbn)(uint16_t, int, float_status *status);
    float64 (*uint32_to_float64_scalbn)(uint32_t, int, float_status *status);
    float64 (*uint64_to_float64_scalbn)(uint64_t, int, float_status *status);
    float64 (*int16_to_float64)(int16_t, float_status *status);
    float64 (*int32_to_float64)(int32_t, float_status *status);
    float64 (*int64_to_float64)(int64_t, float_status *status);
    float64 (*uint16_to_float64)(uint16_t, float_status *status);
    float64 (*uint32_to_float64)(uint32_t, float_status *status);
    float64 (*uint64_to_float64)(uint64_t, float_status *status);

/*----------------------------------------------------------------------------
| Software half-precision conversion routines.
*----------------------------------------------------------------------------*/

    float16 (*float32_to_float16)(float32, float_status *status);
    float32 (*float16_to_float32)(float16, float_status *status);
    float16 (*float64_to_float16)(float64, float_status *status);
    float64 (*float16_to_float64)(float16, float_status *status);
    int8_t  (*float16_to_int8_scalbn)(float16, FloatRoundMode, int,
                                      float_status *);
    int16_t (*float16_to_int16_scalbn)(float16, FloatRoundMode, int,
                                       float_status *);
    int32_t (*float16_to_int32_scalbn)(float16, FloatRoundMode, int,
                                       float_status *);
    int64_t (*float16_to_int64_scalbn)(float16, FloatRoundMode, int,
                                       float_status *);
    int8_t  (*float16_to_int8)(float16, float_status *status);
    int16_t (*float16_to_int16)(float16, float_status *status);
    int32_t (*float16_to_int32)(float16, float_status *status);
    int64_t (*float16_to_int64)(float16, float_status *status);
    int16_t (*float16_to_int16_round_to_zero)(float16, float_status *status);
    int32_t (*float16_to_int32_round_to_zero)(float16, float_status *status);
    int64_t (*float16_to_int64_round_to_zero)(float16, float_status *status);
    uint8_t  (*float16_to_uint8_scalbn)(float16, FloatRoundMode, int,
                                        float_status *status);
    uint16_t (*float16_to_uint16_scalbn)(float16, FloatRoundMode, int,
                                         float_status *status);
    uint32_t (*float16_to_uint32_scalbn)(float16, FloatRoundMode, int,
                                         float_status *status);
    uint64_t (*float16_to_uint64_scalbn)(float16, FloatRoundMode, int,
                                         float_status *status);
    uint8_t  (*float16_to_uint8)(float16, float_status *status);
    uint16_t (*float16_to_uint16)(float16, float_status *status);
    uint32_t (*float16_to_uint32)(float16, float_status *status);
    uint64_t (*float16_to_uint64)(float16, float_status *status);
    uint16_t (*float16_to_uint16_round_to_zero)(float16, float_status *status);
    uint32_t (*float16_to_uint32_round_to_zero)(float16, float_status *status);
    uint64_t (*float16_to_uint64_round_to_zero)(float16, float_status *status);

/*----------------------------------------------------------------------------
| Software half-precision operations.
*----------------------------------------------------------------------------*/
    float16 (*float16_round_to_int)(float16, float_status *status);
    float16 (*float16_add)(float16, float16, float_status *status);
    float16 (*float16_sub)(float16, float16, float_status *status);
    float16 (*float16_mul)(float16, float16, float_status *status);
    float16 (*float16_muladd)(float16, float16, float16, int,
                              float_status *status);
    float16 (*float16_div)(float16, float16, float_status *status);
    float16 (*float16_scalbn)(float16, int, float_status *status);
    float16 (*float16_min)(float16, float16, float_status *status);
    float16 (*float16_max)(float16, float16, float_status *status);
    float16 (*float16_minnum)(float16, float16, float_status *status);
    float16 (*float16_maxnum)(float16, float16, float_status *status);
    float16 (*float16_minnummag)(float16, float16, float_status *status);
    float16 (*float16_maxnummag)(float16, float16, float_status *status);
    float16 (*float16_minimum_number)(float16, float16,
                                      float_status *status);
    float16 (*float16_maximum_number)(float16, float16,
                                      float_status *status);
    float16 (*float16_sqrt)(float16, float_status *status);
    FloatRelation (*float16_compare)(float16, float16,
                                     float_status *status);
    FloatRelation (*float16_compare_quiet)(float16, float16,
                                           float_status *status);
    bool (*float16_is_quiet_nan)(float16, float_status *status);
    bool (*float16_is_signaling_nan)(float16, float_status *status);
    float16 (*float16_silence_nan)(float16, float_status *status);

    bool (*float16_is_any_nan)(float16 a);
    bool (*float16_is_neg)(float16 a);
    bool (*float16_is_infinity)(float16 a);
    bool (*float16_is_zero)(float16 a);
    bool (*float16_is_zero_or_denormal)(float16 a);
    bool (*float16_is_normal)(float16 a);
    float16 (*float16_abs)(float16 a);
    float16 (*float16_chs)(float16 a);
    float16 (*float16_set_sign)(float16 a, int sign);
    bool (*float16_eq)(float16 a, float16 b, float_status *s);
    bool (*float16_le)(float16 a, float16 b, float_status *s);
    bool (*float16_lt)(float16 a, float16 b, float_status *s);
    bool (*float16_unordered)(float16 a, float16 b,
                              float_status *s);
    bool (*float16_eq_quiet)(float16 a, float16 b, float_status *s);
    bool (*float16_le_quiet)(float16 a, float16 b, float_status *s);
    bool (*float16_lt_quiet)(float16 a, float16 b, float_status *s);
    bool (*float16_unordered_quiet)(float16 a, float16 b,
                                    float_status *s);

/*----------------------------------------------------------------------------
| The pattern for a default generated half-precision NaN.
*----------------------------------------------------------------------------*/
    float16 (*float16_default_nan)(float_status *status);

/*----------------------------------------------------------------------------
| Software bfloat16 conversion routines.
*----------------------------------------------------------------------------*/
    bfloat16 (*bfloat16_round_to_int)(bfloat16, float_status *status);
    bfloat16 (*float32_to_bfloat16)(float32, float_status *status);
    float32 (*bfloat16_to_float32)(bfloat16, float_status *status);
    bfloat16 (*float64_to_bfloat16)(float64 a, float_status *status);
    float64 (*bfloat16_to_float64)(bfloat16 a, float_status *status);

    int8_t (*bfloat16_to_int8_scalbn)(bfloat16, FloatRoundMode,
                                      int, float_status *status);
    int16_t (*bfloat16_to_int16_scalbn)(bfloat16, FloatRoundMode,
                                        int, float_status *status);
    int32_t (*bfloat16_to_int32_scalbn)(bfloat16, FloatRoundMode,
                                        int, float_status *status);
    int64_t (*bfloat16_to_int64_scalbn)(bfloat16, FloatRoundMode,
                                        int, float_status *status);

    int8_t (*bfloat16_to_int8)(bfloat16, float_status *status);
    int16_t (*bfloat16_to_int16)(bfloat16, float_status *status);
    int32_t (*bfloat16_to_int32)(bfloat16, float_status *status);
    int64_t (*bfloat16_to_int64)(bfloat16, float_status *status);

    int8_t (*bfloat16_to_int8_round_to_zero)(bfloat16,
                                             float_status *status);
    int16_t (*bfloat16_to_int16_round_to_zero)(bfloat16,
                                               float_status *status);
    int32_t (*bfloat16_to_int32_round_to_zero)(bfloat16,
                                               float_status *status);
    int64_t (*bfloat16_to_int64_round_to_zero)(bfloat16,
                                               float_status *status);

    uint8_t (*bfloat16_to_uint8_scalbn)(bfloat16 a, FloatRoundMode,
                                        int, float_status *status);
    uint16_t (*bfloat16_to_uint16_scalbn)(bfloat16 a, FloatRoundMode,
                                          int, float_status *status);
    uint32_t (*bfloat16_to_uint32_scalbn)(bfloat16 a, FloatRoundMode,
                                          int, float_status *status);
    uint64_t (*bfloat16_to_uint64_scalbn)(bfloat16 a, FloatRoundMode,
                                          int, float_status *status);

    uint8_t  (*bfloat16_to_uint8)(bfloat16 a, float_status *status);
    uint16_t (*bfloat16_to_uint16)(bfloat16 a, float_status *status);
    uint32_t (*bfloat16_to_uint32)(bfloat16 a, float_status *status);
    uint64_t (*bfloat16_to_uint64)(bfloat16 a, float_status *status);

    uint8_t  (*bfloat16_to_uint8_round_to_zero)(bfloat16 a,
                                                float_status *status);
    uint16_t (*bfloat16_to_uint16_round_to_zero)(bfloat16 a,
                                                 float_status *status);
    uint32_t (*bfloat16_to_uint32_round_to_zero)(bfloat16 a,
                                                 float_status *status);
    uint64_t (*bfloat16_to_uint64_round_to_zero)(bfloat16 a,
                                                 float_status *status);

    bfloat16 (*int8_to_bfloat16_scalbn)(int8_t a, int, float_status *status);
    bfloat16 (*int16_to_bfloat16_scalbn)(int16_t a, int, float_status *status);
    bfloat16 (*int32_to_bfloat16_scalbn)(int32_t a, int, float_status *status);
    bfloat16 (*int64_to_bfloat16_scalbn)(int64_t a, int, float_status *status);
    bfloat16 (*uint8_to_bfloat16_scalbn)(uint8_t a, int, float_status *status);
    bfloat16 (*uint16_to_bfloat16_scalbn)(uint16_t a, int,
                                          float_status *status);
    bfloat16 (*uint32_to_bfloat16_scalbn)(uint32_t a, int,
                                          float_status *status);
    bfloat16 (*uint64_to_bfloat16_scalbn)(uint64_t a, int,
                                          float_status *status);

    bfloat16 (*int8_to_bfloat16)(int8_t a, float_status *status);
    bfloat16 (*int16_to_bfloat16)(int16_t a, float_status *status);
    bfloat16 (*int32_to_bfloat16)(int32_t a, float_status *status);
    bfloat16 (*int64_to_bfloat16)(int64_t a, float_status *status);
    bfloat16 (*uint8_to_bfloat16)(uint8_t a, float_status *status);
    bfloat16 (*uint16_to_bfloat16)(uint16_t a, float_status *status);
    bfloat16 (*uint32_to_bfloat16)(uint32_t a, float_status *status);
    bfloat16 (*uint64_to_bfloat16)(uint64_t a, float_status *status);

/*----------------------------------------------------------------------------
| Software bfloat16 operations.
*----------------------------------------------------------------------------*/
    bfloat16 (*bfloat16_add)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_sub)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_mul)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_div)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_muladd)(bfloat16, bfloat16, bfloat16, int,
                                float_status *status);
    float16 (*bfloat16_scalbn)(bfloat16, int, float_status *status);
    bfloat16 (*bfloat16_min)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_max)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_minnum)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_maxnum)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_minnummag)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_maxnummag)(bfloat16, bfloat16, float_status *status);
    bfloat16 (*bfloat16_minimum_number)(bfloat16, bfloat16,
                                        float_status *status);
    bfloat16 (*bfloat16_maximum_number)(bfloat16, bfloat16,
                                        float_status *status);
    bfloat16 (*bfloat16_sqrt)(bfloat16, float_status *status);
    FloatRelation (*bfloat16_compare)(bfloat16, bfloat16,
                                      float_status *status);
    FloatRelation (*bfloat16_compare_quiet)(bfloat16, bfloat16,
                                            float_status *status);

    bool (*bfloat16_is_quiet_nan)(bfloat16, float_status *status);
    bool (*bfloat16_is_signaling_nan)(bfloat16, float_status *status);
    bfloat16 (*bfloat16_silence_nan)(bfloat16, float_status *status);
    bfloat16 (*bfloat16_default_nan)(float_status *status);

    bool (*bfloat16_is_any_nan)(bfloat16 a);
    bool (*bfloat16_is_neg)(bfloat16 a);
    bool (*bfloat16_is_infinity)(bfloat16 a);
    bool (*bfloat16_is_zero)(bfloat16 a);
    bool (*bfloat16_is_zero_or_denormal)(bfloat16 a);
    bool (*bfloat16_is_normal)(bfloat16 a);
    bfloat16 (*bfloat16_abs)(bfloat16 a);
    bfloat16 (*bfloat16_chs)(bfloat16 a);
    bfloat16 (*bfloat16_set_sign)(bfloat16 a, int sign);
    bool (*bfloat16_eq)(bfloat16 a, bfloat16 b, float_status *s);
    bool (*bfloat16_le)(bfloat16 a, bfloat16 b, float_status *s);
    bool (*bfloat16_lt)(bfloat16 a, bfloat16 b, float_status *s);
    bool (*bfloat16_unordered)(bfloat16 a, bfloat16 b, float_status *s);
    bool (*bfloat16_eq_quiet)(bfloat16 a, bfloat16 b, float_status *s);
    bool (*bfloat16_le_quiet)(bfloat16 a, bfloat16 b, float_status *s);
    bool (*bfloat16_lt_quiet)(bfloat16 a, bfloat16 b, float_status *s);
    bool (*bfloat16_unordered_quiet)(bfloat16 a, bfloat16 b, float_status *s);

/*----------------------------------------------------------------------------
| Software float8e4 conversion routines.
*----------------------------------------------------------------------------*/
    float8e4 (*float8e4_round_to_int)(float8e4, float_status *status);
    float8e4 (*bfloat16_to_float8e4)(bfloat16, float_status *status);
    bfloat16 (*float8e4_to_bfloat16)(float8e4, float_status *status);
    float8e4 (*float16_to_float8e4)(float16, float_status *status);
    float16 (*float8e4_to_float16)(float8e4, float_status *status);
    float8e4 (*float32_to_float8e4)(float32, float_status *status);
    float32 (*float8e4_to_float32)(float8e4, float_status *status);
    float8e4 (*float64_to_float8e4)(float64 a, float_status *status);
    float64 (*float8e4_to_float64)(float8e4 a, float_status *status);
    int8_t (*float8e4_to_int8_scalbn)(float8e4, FloatRoundMode,
                                    int, float_status *status);
    int16_t (*float8e4_to_int16_scalbn)(float8e4, FloatRoundMode,
                                        int, float_status *status);
    int32_t (*float8e4_to_int32_scalbn)(float8e4, FloatRoundMode,
                                        int, float_status *status);
    int64_t (*float8e4_to_int64_scalbn)(float8e4, FloatRoundMode,
                                        int, float_status *status);
    int8_t (*float8e4_to_int8)(float8e4, float_status *status);
    int16_t (*float8e4_to_int16)(float8e4, float_status *status);
    int32_t (*float8e4_to_int32)(float8e4, float_status *status);
    int64_t (*float8e4_to_int64)(float8e4, float_status *status);
    int8_t (*float8e4_to_int8_round_to_zero)(float8e4, float_status *status);
    int16_t (*float8e4_to_int16_round_to_zero)(float8e4, float_status *status);
    int32_t (*float8e4_to_int32_round_to_zero)(float8e4, float_status *status);
    int64_t (*float8e4_to_int64_round_to_zero)(float8e4, float_status *status);
    uint8_t (*float8e4_to_uint8_scalbn)(float8e4 a, FloatRoundMode,
                                        int, float_status *status);
    uint16_t (*float8e4_to_uint16_scalbn)(float8e4 a, FloatRoundMode,
                                        int, float_status *status);
    uint32_t (*float8e4_to_uint32_scalbn)(float8e4 a, FloatRoundMode,
                                        int, float_status *status);
    uint64_t (*float8e4_to_uint64_scalbn)(float8e4 a, FloatRoundMode,
                                        int, float_status *status);
    uint8_t (*float8e4_to_uint8)(float8e4 a, float_status *status);
    uint16_t (*float8e4_to_uint16)(float8e4 a, float_status *status);
    uint32_t (*float8e4_to_uint32)(float8e4 a, float_status *status);
    uint64_t (*float8e4_to_uint64)(float8e4 a, float_status *status);
    uint8_t (*float8e4_to_uint8_round_to_zero)(float8e4 a,
                                               float_status *status);
    uint16_t (*float8e4_to_uint16_round_to_zero)(float8e4 a,
                                                 float_status *status);
    uint32_t (*float8e4_to_uint32_round_to_zero)(float8e4 a,
                                                 float_status *status);
    uint64_t (*float8e4_to_uint64_round_to_zero)(float8e4 a,
                                                 float_status *status);
    float8e4 (*int8_to_float8e4_scalbn)(int8_t a, int, float_status *status);
    float8e4 (*int16_to_float8e4_scalbn)(int16_t a, int, float_status *status);
    float8e4 (*int32_to_float8e4_scalbn)(int32_t a, int, float_status *status);
    float8e4 (*int64_to_float8e4_scalbn)(int64_t a, int, float_status *status);
    float8e4 (*uint8_to_float8e4_scalbn)(uint8_t a, int, float_status *status);
    float8e4 (*uint16_to_float8e4_scalbn)(uint16_t a, int,
                                          float_status *status);
    float8e4 (*uint32_to_float8e4_scalbn)(uint32_t a, int,
                                          float_status *status);
    float8e4 (*uint64_to_float8e4_scalbn)(uint64_t a, int,
                                          float_status *status);
    float8e4 (*int8_to_float8e4)(int8_t a, float_status *status);
    float8e4 (*int16_to_float8e4)(int16_t a, float_status *status);
    float8e4 (*int32_to_float8e4)(int32_t a, float_status *status);
    float8e4 (*int64_to_float8e4)(int64_t a, float_status *status);
    float8e4 (*uint8_to_float8e4)(uint8_t a, float_status *status);
    float8e4 (*uint16_to_float8e4)(uint16_t a, float_status *status);
    float8e4 (*uint32_to_float8e4)(uint32_t a, float_status *status);
    float8e4 (*uint64_to_float8e4)(uint64_t a, float_status *status);

/*----------------------------------------------------------------------------
| Software float8e4 operations.
*----------------------------------------------------------------------------*/
    float8e4 (*float8e4_add)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_sub)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_mul)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_div)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_muladd)(float8e4, float8e4, float8e4, int,
                                float_status *status);
    float8e4 (*float8e4_scalbn)(float8e4, int, float_status *status);
    float8e4 (*float8e4_min)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_max)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_minnum)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_maxnum)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_minnummag)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_maxnummag)(float8e4, float8e4, float_status *status);
    float8e4 (*float8e4_minimum_number)(float8e4, float8e4,
                                        float_status *status);
    float8e4 (*float8e4_maximum_number)(float8e4, float8e4,
                                        float_status *status);
    float8e4 (*float8e4_sqrt)(float8e4, float_status *status);
    FloatRelation (*float8e4_compare)(float8e4, float8e4,
                                      float_status *status);
    FloatRelation (*float8e4_compare_quiet)(float8e4, float8e4,
                                            float_status *status);
    bool (*float8e4_is_quiet_nan)(float8e4, float_status *status);
    bool (*float8e4_is_signaling_nan)(float8e4, float_status *status);
    float8e4 (*float8e4_silence_nan)(float8e4, float_status *status);
    float8e4 (*float8e4_default_nan)(float_status *status);
    bool (*float8e4_is_any_nan)(float8e4 a);
    bool (*float8e4_is_neg)(float8e4 a);
    bool (*float8e4_is_infinity)(float8e4 a);
    bool (*float8e4_is_zero)(float8e4 a);
    bool (*float8e4_is_zero_or_denormal)(float8e4 a);
    bool (*float8e4_is_normal)(float8e4 a);
    float8e4 (*float8e4_abs)(float8e4 a);
    float8e4 (*float8e4_chs)(float8e4 a);
    float8e4 (*float8e4_set_sign)(float8e4 a, int sign);
    bool (*float8e4_eq)(float8e4 a, float8e4 b, float_status *s);
    bool (*float8e4_le)(float8e4 a, float8e4 b, float_status *s);
    bool (*float8e4_lt)(float8e4 a, float8e4 b, float_status *s);
    bool (*float8e4_unordered)(float8e4 a, float8e4 b, float_status *s);
    bool (*float8e4_eq_quiet)(float8e4 a, float8e4 b, float_status *s);
    bool (*float8e4_le_quiet)(float8e4 a, float8e4 b, float_status *s);
    bool (*float8e4_lt_quiet)(float8e4 a, float8e4 b, float_status *s);
    bool (*float8e4_unordered_quiet)(float8e4 a, float8e4 b,
                                     float_status *s);
/*----------------------------------------------------------------------------
| Software float8e5 conversion routines.
*----------------------------------------------------------------------------*/
    float8e5 (*float8e5_round_to_int)(float8e5, float_status *status);
    float8e5 (*bfloat16_to_float8e5)(bfloat16, float_status *status);
    bfloat16 (*float8e5_to_bfloat16)(float8e5, float_status *status);
    float8e5 (*float16_to_float8e5)(float16, float_status *status);
    float16 (*float8e5_to_float16)(float8e5, float_status *status);
    float8e5 (*float32_to_float8e5)(float32, float_status *status);
    float32 (*float8e5_to_float32)(float8e5, float_status *status);
    float8e5 (*float64_to_float8e5)(float64 a, float_status *status);
    float64 (*float8e5_to_float64)(float8e5 a, float_status *status);
    int8_t (*float8e5_to_int8_scalbn)(float8e5, FloatRoundMode,
                                      int, float_status *status);
    int16_t (*float8e5_to_int16_scalbn)(float8e5, FloatRoundMode,
                                        int, float_status *status);
    int32_t (*float8e5_to_int32_scalbn)(float8e5, FloatRoundMode,
                                        int, float_status *status);
    int64_t (*float8e5_to_int64_scalbn)(float8e5, FloatRoundMode,
                                        int, float_status *status);
    int8_t (*float8e5_to_int8)(float8e5, float_status *status);
    int16_t (*float8e5_to_int16)(float8e5, float_status *status);
    int32_t (*float8e5_to_int32)(float8e5, float_status *status);
    int64_t (*float8e5_to_int64)(float8e5, float_status *status);
    int8_t (*float8e5_to_int8_round_to_zero)(float8e5, float_status *status);
    int16_t (*float8e5_to_int16_round_to_zero)(float8e5, float_status *status);
    int32_t (*float8e5_to_int32_round_to_zero)(float8e5, float_status *status);
    int64_t (*float8e5_to_int64_round_to_zero)(float8e5, float_status *status);
    uint8_t (*float8e5_to_uint8_scalbn)(float8e5 a, FloatRoundMode,
                                        int, float_status *status);
    uint16_t (*float8e5_to_uint16_scalbn)(float8e5 a, FloatRoundMode,
                                          int, float_status *status);
    uint32_t (*float8e5_to_uint32_scalbn)(float8e5 a, FloatRoundMode,
                                          int, float_status *status);
    uint64_t (*float8e5_to_uint64_scalbn)(float8e5 a, FloatRoundMode,
                                          int, float_status *status);
    uint8_t (*float8e5_to_uint8)(float8e5 a, float_status *status);
    uint16_t (*float8e5_to_uint16)(float8e5 a, float_status *status);
    uint32_t (*float8e5_to_uint32)(float8e5 a, float_status *status);
    uint64_t (*float8e5_to_uint64)(float8e5 a, float_status *status);
    uint8_t (*float8e5_to_uint8_round_to_zero)(float8e5 a, float_status *status);
    uint16_t (*float8e5_to_uint16_round_to_zero)(float8e5 a, float_status *status);
    uint32_t (*float8e5_to_uint32_round_to_zero)(float8e5 a, float_status *status);
    uint64_t (*float8e5_to_uint64_round_to_zero)(float8e5 a, float_status *status);
    float8e5 (*int8_to_float8e5_scalbn)(int8_t a, int, float_status *status);
    float8e5 (*int16_to_float8e5_scalbn)(int16_t a, int, float_status *status);
    float8e5 (*int32_to_float8e5_scalbn)(int32_t a, int, float_status *status);
    float8e5 (*int64_to_float8e5_scalbn)(int64_t a, int, float_status *status);
    float8e5 (*uint8_to_float8e5_scalbn)(uint8_t a, int, float_status *status);
    float8e5 (*uint16_to_float8e5_scalbn)(uint16_t a, int, float_status *status);
    float8e5 (*uint32_to_float8e5_scalbn)(uint32_t a, int, float_status *status);
    float8e5 (*uint64_to_float8e5_scalbn)(uint64_t a, int, float_status *status);
    float8e5 (*int8_to_float8e5)(int8_t a, float_status *status);
    float8e5 (*int16_to_float8e5)(int16_t a, float_status *status);
    float8e5 (*int32_to_float8e5)(int32_t a, float_status *status);
    float8e5 (*int64_to_float8e5)(int64_t a, float_status *status);
    float8e5 (*uint8_to_float8e5)(uint8_t a, float_status *status);
    float8e5 (*uint16_to_float8e5)(uint16_t a, float_status *status);
    float8e5 (*uint32_to_float8e5)(uint32_t a, float_status *status);
    float8e5 (*uint64_to_float8e5)(uint64_t a, float_status *status);

/*----------------------------------------------------------------------------
| Software float8e5 operations.
*----------------------------------------------------------------------------*/
    float8e5 (*float8e5_add)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_sub)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_mul)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_div)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_muladd)(float8e5, float8e5, float8e5, int,
                                float_status *status);
    float8e5 (*float8e5_scalbn)(float8e5, int, float_status *status);
    float8e5 (*float8e5_min)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_max)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_minnum)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_maxnum)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_minnummag)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_maxnummag)(float8e5, float8e5, float_status *status);
    float8e5 (*float8e5_minimum_number)(float8e5, float8e5,
                                        float_status *status);
    float8e5 (*float8e5_maximum_number)(float8e5, float8e5,
                                        float_status *status);
    float8e5 (*float8e5_sqrt)(float8e5, float_status *status);
    FloatRelation (*float8e5_compare)(float8e5, float8e5,
                                      float_status *status);
    FloatRelation (*float8e5_compare_quiet)(float8e5, float8e5,
                                            float_status *status);
    bool (*float8e5_is_quiet_nan)(float8e5, float_status *status);
    bool (*float8e5_is_signaling_nan)(float8e5, float_status *status);
    float8e5 (*float8e5_silence_nan)(float8e5, float_status *status);
    float8e5 (*float8e5_default_nan)(float_status *status);
    bool (*float8e5_is_any_nan)(float8e5 a);
    bool (*float8e5_is_neg)(float8e5 a);
    bool (*float8e5_is_infinity)(float8e5 a);
    bool (*float8e5_is_zero)(float8e5 a);
    bool (*float8e5_is_zero_or_denormal)(float8e5 a);
    bool (*float8e5_is_normal)(float8e5 a);
    float8e5 (*float8e5_abs)(float8e5 a);
    float8e5 (*float8e5_chs)(float8e5 a);
    float8e5 (*float8e5_set_sign)(float8e5 a, int sign);
    bool (*float8e5_eq)(float8e5 a, float8e5 b, float_status *s);
    bool (*float8e5_le)(float8e5 a, float8e5 b, float_status *s);
    bool (*float8e5_lt)(float8e5 a, float8e5 b, float_status *s);
    bool (*float8e5_unordered)(float8e5 a, float8e5 b, float_status *s);
    bool (*float8e5_eq_quiet)(float8e5 a, float8e5 b, float_status *s);
    bool (*float8e5_le_quiet)(float8e5 a, float8e5 b, float_status *s);
    bool (*float8e5_lt_quiet)(float8e5 a, float8e5 b, float_status *s);
    bool (*float8e5_unordered_quiet)(float8e5 a, float8e5 b,
                                     float_status *s);

/*----------------------------------------------------------------------------
| Software IEC/IEEE single-precision conversion routines.
*----------------------------------------------------------------------------*/
    int16_t (*float32_to_int16_scalbn)(float32, FloatRoundMode, int,
                                       float_status *);
    int32_t (*float32_to_int32_scalbn)(float32, FloatRoundMode, int,
                                       float_status *);
    int64_t (*float32_to_int64_scalbn)(float32, FloatRoundMode, int,
                                       float_status *);
    int16_t (*float32_to_int16)(float32, float_status *status);
    int32_t (*float32_to_int32)(float32, float_status *status);
    int64_t (*float32_to_int64)(float32, float_status *status);
    int16_t (*float32_to_int16_round_to_zero)(float32, float_status *status);
    int32_t (*float32_to_int32_round_to_zero)(float32, float_status *status);
    int64_t (*float32_to_int64_round_to_zero)(float32, float_status *status);
    uint16_t (*float32_to_uint16_scalbn)(float32, FloatRoundMode, int,
                                         float_status *);
    uint32_t (*float32_to_uint32_scalbn)(float32, FloatRoundMode, int,
                                         float_status *);
    uint64_t (*float32_to_uint64_scalbn)(float32, FloatRoundMode, int,
                                         float_status *);
    uint16_t (*float32_to_uint16)(float32, float_status *status);
    uint32_t (*float32_to_uint32)(float32, float_status *status);
    uint64_t (*float32_to_uint64)(float32, float_status *status);
    uint16_t (*float32_to_uint16_round_to_zero)(float32, float_status *status);
    uint32_t (*float32_to_uint32_round_to_zero)(float32, float_status *status);
    uint64_t (*float32_to_uint64_round_to_zero)(float32, float_status *status);
    float64 (*float32_to_float64)(float32, float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE single-precision operations.
*----------------------------------------------------------------------------*/
    float32 (*float32_round_to_int)(float32, float_status *status);
    float32 (*float32_add)(float32, float32, float_status *status);
    float32 (*float32_sub)(float32, float32, float_status *status);
    float32 (*float32_mul)(float32, float32, float_status *status);
    float32 (*float32_div)(float32, float32, float_status *status);
    float32 (*float32_rem)(float32, float32, float_status *status);
    float32 (*float32_muladd)(float32, float32, float32, int,
                              float_status *status);
    float32 (*float32_sqrt)(float32, float_status *status);
    float32 (*float32_exp2)(float32, float_status *status);
    float32 (*float32_log2)(float32, float_status *status);
    FloatRelation (*float32_compare)(float32, float32, float_status *status);
    FloatRelation (*float32_compare_quiet)(float32, float32,
                                           float_status *status);
    float32 (*float32_min)(float32, float32, float_status *status);
    float32 (*float32_max)(float32, float32, float_status *status);
    float32 (*float32_minnum)(float32, float32, float_status *status);
    float32 (*float32_maxnum)(float32, float32, float_status *status);
    float32 (*float32_minnummag)(float32, float32, float_status *status);
    float32 (*float32_maxnummag)(float32, float32, float_status *status);
    float32 (*float32_minimum_number)(float32, float32, float_status *status);
    float32 (*float32_maximum_number)(float32, float32, float_status *status);
    bool (*float32_is_quiet_nan)(float32, float_status *status);
    bool (*float32_is_signaling_nan)(float32, float_status *status);
    float32 (*float32_silence_nan)(float32, float_status *status);
    float32 (*float32_scalbn)(float32, int, float_status *status);

    float32 (*float32_abs)(float32 a);
    float32 (*float32_chs)(float32 a);
    bool (*float32_is_infinity)(float32 a);
    bool (*float32_is_neg)(float32 a);
    bool (*float32_is_zero)(float32 a);
    bool (*float32_is_any_nan)(float32 a);
    bool (*float32_is_zero_or_denormal)(float32 a);
    bool (*float32_is_normal)(float32 a);
    bool (*float32_is_denormal)(float32 a);
    bool (*float32_is_zero_or_normal)(float32 a);
    float32 (*float32_set_sign)(float32 a, int sign);
    bool (*float32_eq)(float32 a, float32 b, float_status *s);
    bool (*float32_le)(float32 a, float32 b, float_status *s);
    bool (*float32_lt)(float32 a, float32 b, float_status *s);
    bool (*float32_unordered)(float32 a, float32 b, float_status *s);
    bool (*float32_eq_quiet)(float32 a, float32 b, float_status *s);
    bool (*float32_le_quiet)(float32 a, float32 b, float_status *s);
    bool (*float32_lt_quiet)(float32 a, float32 b, float_status *s);
    bool (*float32_unordered_quiet)(float32 a, float32 b,
                                    float_status *s);

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| single-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/
    float32 (*packFloat32)(bool zSign, int zExp, uint32_t zSig);

/*----------------------------------------------------------------------------
| The pattern for a default generated single-precision NaN.
*----------------------------------------------------------------------------*/
    float32 (*float32_default_nan)(float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision conversion routines.
*----------------------------------------------------------------------------*/
    int16_t (*float64_to_int16_scalbn)(float64, FloatRoundMode, int,
                                       float_status *);
    int32_t (*float64_to_int32_scalbn)(float64, FloatRoundMode, int,
                                       float_status *);
    int64_t (*float64_to_int64_scalbn)(float64, FloatRoundMode, int,
                                       float_status *);
    int16_t (*float64_to_int16)(float64, float_status *status);
    int32_t (*float64_to_int32)(float64, float_status *status);
    int64_t (*float64_to_int64)(float64, float_status *status);
    int16_t (*float64_to_int16_round_to_zero)(float64, float_status *status);
    int32_t (*float64_to_int32_round_to_zero)(float64, float_status *status);
    int64_t (*float64_to_int64_round_to_zero)(float64, float_status *status);
    int32_t (*float64_to_int32_modulo)(float64, FloatRoundMode,
                                       float_status *status);
    int64_t (*float64_to_int64_modulo)(float64, FloatRoundMode,
                                       float_status *status);
    uint16_t (*float64_to_uint16_scalbn)(float64, FloatRoundMode, int,
                                         float_status *);
    uint32_t (*float64_to_uint32_scalbn)(float64, FloatRoundMode, int,
                                         float_status *);
    uint64_t (*float64_to_uint64_scalbn)(float64, FloatRoundMode, int,
                                         float_status *);
    uint16_t (*float64_to_uint16)(float64, float_status *status);
    uint32_t (*float64_to_uint32)(float64, float_status *status);
    uint64_t (*float64_to_uint64)(float64, float_status *status);
    uint16_t (*float64_to_uint16_round_to_zero)(float64, float_status *status);
    uint32_t (*float64_to_uint32_round_to_zero)(float64, float_status *status);
    uint64_t (*float64_to_uint64_round_to_zero)(float64, float_status *status);
    float32 (*float64_to_float32)(float64, float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision operations.
*----------------------------------------------------------------------------*/
    float64 (*float64_round_to_int)(float64, float_status *status);
    float64 (*float64_add)(float64, float64, float_status *status);
    float64 (*float64_sub)(float64, float64, float_status *status);
    float64 (*float64_mul)(float64, float64, float_status *status);
    float64 (*float64_div)(float64, float64, float_status *status);
    float64 (*float64_rem)(float64, float64, float_status *status);
    float64 (*float64_muladd)(float64, float64, float64, int,
                              float_status *status);
    float64 (*float64_sqrt)(float64, float_status *status);
    float64 (*float64_log2)(float64, float_status *status);
    FloatRelation (*float64_compare)(float64, float64, float_status *status);
    FloatRelation (*float64_compare_quiet)(float64, float64,
                                           float_status *status);
    float64 (*float64_min)(float64, float64, float_status *status);
    float64 (*float64_max)(float64, float64, float_status *status);
    float64 (*float64_minnum)(float64, float64, float_status *status);
    float64 (*float64_maxnum)(float64, float64, float_status *status);
    float64 (*float64_minnummag)(float64, float64, float_status *status);
    float64 (*float64_maxnummag)(float64, float64, float_status *status);
    float64 (*float64_minimum_number)(float64, float64, float_status *status);
    float64 (*float64_maximum_number)(float64, float64, float_status *status);
    bool (*float64_is_quiet_nan)(float64 a, float_status *status);
    bool (*float64_is_signaling_nan)(float64, float_status *status);
    float64 (*float64_silence_nan)(float64, float_status *status);
    float64 (*float64_scalbn)(float64, int, float_status *status);

    float64 (*float64_abs)(float64 a);
    float64 (*float64_chs)(float64 a);
    bool (*float64_is_infinity)(float64 a);
    bool (*float64_is_neg)(float64 a);
    bool (*float64_is_zero)(float64 a);
    bool (*float64_is_any_nan)(float64 a);
    bool (*float64_is_zero_or_denormal)(float64 a);
    bool (*float64_is_normal)(float64 a);
    bool (*float64_is_denormal)(float64 a);
    bool (*float64_is_zero_or_normal)(float64 a);
    float64 (*float64_set_sign)(float64 a, int sign);
    bool (*float64_eq)(float64 a, float64 b, float_status *s);
    bool (*float64_le)(float64 a, float64 b, float_status *s);
    bool (*float64_lt)(float64 a, float64 b, float_status *s);
    bool (*float64_unordered)(float64 a, float64 b, float_status *s);
    bool (*float64_eq_quiet)(float64 a, float64 b, float_status *s);
    bool (*float64_le_quiet)(float64 a, float64 b, float_status *s);
    bool (*float64_lt_quiet)(float64 a, float64 b, float_status *s);
    bool (*float64_unordered_quiet)(float64 a, float64 b,
                                    float_status *s);

/*----------------------------------------------------------------------------
| The pattern for a default generated double-precision NaN.
*----------------------------------------------------------------------------*/
    float64 (*float64_default_nan)(float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision operations, rounding to single precision,
| returning a result in double precision, with only one rounding step.
*----------------------------------------------------------------------------*/
    float64 (*float64r32_add)(float64, float64, float_status *status);
    float64 (*float64r32_sub)(float64, float64, float_status *status);
    float64 (*float64r32_mul)(float64, float64, float_status *status);
    float64 (*float64r32_div)(float64, float64, float_status *status);
    float64 (*float64r32_muladd)(float64, float64, float64, int,
                                 float_status *status);
    float64 (*float64r32_sqrt)(float64, float_status *status);

} qemu_float_ops;

#endif