/*
 * Special Function Unit Cpp header, get from target/riscv/sfu/sfu.h
 *
 * Copyright (c) 2025 Alibaba Group. All rights reserved.
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
#ifdef __cplusplus
extern "C" {
#endif
    #define SFU_NV 16
    #define SFU_DZ  8
    #define SFU_OF  4
    #define SFU_UF  2
    #define SFU_NX  1
    typedef struct sfu_output{
        float sfu_data_output;
        float sfu_err_output;
        int sfu_exception_output;
        long int sfu_booth_output;
    } sfu_output;
    sfu_output sfu_exp2(uint32_t a);
    sfu_output sfu_rcp(uint32_t a);
    sfu_output sfu_sigmoid(uint32_t a);
    sfu_output sfu_tanh(uint32_t a);
    sfu_output sfu_sin(uint32_t a);
    sfu_output sfu_cos(uint32_t a);
    sfu_output sfu_log2(uint32_t a);
    sfu_output sfu_sqrt(uint32_t a);
    sfu_output sfu_rsqrt(uint32_t a);
    float32 sfu_to_f32(sfu_output *a);
    void sfu_set_flags(float_status *s, sfu_output *a);

#ifdef __cplusplus
}
#endif
