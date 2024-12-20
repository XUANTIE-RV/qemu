/*
 * CSKY MCA emulation.
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
#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "trace.h"
#include "exec/tracestub.h"
#include <math.h>
#include "migration/vmstate.h"
#include "exec/memory.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

typedef int16_t fxp16_q15_t;
typedef int32_t fxp32_q16_t;

uint32_t coeff[80][8] = {
    [0] = {25, 18, 7,},      /* csky_mca_vec_add_vec_fxp8 */
    [1] = {26, 17, 6,},      /* csky_mca_vec_add_vec_fxp8 */
    [2] = {27, 16, 6,},      /* csky_mca_vec_add_scalar_fxp8 */
    [3] = {23, 20, 7,},      /* csky_mca_vec_add_vec_fxp16 */
    [4] = {25, 18, 6,},      /* csky_mca_vec_add_vec_fxp16 */
    [5] = {25, 17, 6,},      /* csky_mca_vec_add_scalar_fxp16 */
    [6] = {23, 21, 7,},      /* csky_mca_vec_add_vec_fxp32 */
    [7] = {22, 22, 6,},      /* csky_mca_vec_add_vec_fxp32 */
    [8] = {22, 20, 6,},      /* csky_mca_vec_add_scalar_fxp32 */
    [9] = {27, 19, 7,},      /* csky_mca_vec_mul_vec_fxp8 */
    [10] = {28, 18, 6,},     /* csky_mca_vec_mul_vec_fxp8 */
    [11] = {28, 16, 6,},     /* csky_mca_vec_mul_scalar_fxp8 */
    [12] = {25, 21, 7,},     /* csky_mca_vec_mul_vec_fxp16 */
    [13] = {27, 19, 6,},     /* csky_mca_vec_mul_vec_fxp16 */
    [14] = {29, 17, 6,},     /* csky_mca_vec_mul_scalar_fxp16 */
    [15] = {27, 20, 7,},     /* csky_mca_vec_mul_vec_fxp32 */
    [16] = {26, 21, 6,},     /* csky_mca_vec_mul_vec_fxp32 */
    [17] = {27, 19, 6,},     /* csky_mca_vec_mul_scalar_fxp32 */
    [18] = {45, 9, 2,},      /* csky_mca_mat_mul_vec_fxp8 */
    [19] = {45, 8, 1,},      /* csky_mca_mat_mul_vec_fxp8 */
    [20] = {49, 9, 2,},      /* csky_mca_mat_mul_vec_fxp16 */
    [21] = {49, 8, 1,},      /* csky_mca_mat_mul_vec_fxp16 */
    [22] = {49, 9, 2,},      /* csky_mca_mat_mul_vec_fxp32 */
    [23] = {49, 8, 1,},      /* csky_mca_mat_mul_vec_fxp32 */
    [24] = {70, 10, 2,},     /* csky_mca_fully_connected_fxp8 */
    [25] = {70, 9, 1,},      /* csky_mca_fully_connected_fxp8 */
    [26] = {68, 10, 2,},     /* csky_mca_fully_connected_with_relu_fxp8 */
    [27] = {68, 9, 1,},      /* csky_mca_fully_connected_with_relu_fxp8 */
    [28] = {66, 10, 2,},     /* csky_mca_fully_connected_fxp16 */
    [29] = {66, 9, 1,},      /* csky_mca_fully_connected_fxp16 */
    [30] = {65, 10, 2,},     /* csky_mca_fully_connected_with_relu_fxp16 */
    [31] = {65, 9, 1,},      /* csky_mca_fully_connected_with_relu_fxp16 */
    /* csky_mca_mat_mul_vec_v8_m8_o32 */
    [32] = {56, 9, 2, 116, 18,  26 , 12,},
    /* csky_mca_mat_mul_vec_v8_m8_o32 */
    [33] = {56, 8, 1, 116, 16,  26 , 11,},
    /* csky_mca_fully_connected_v8_m8_b8_o32 */
    [34] = {75, 10, 2, 121, 19,  24 , 11,},
    /* csky_mca_fully_connected_v8_m8_b8_o32 */
    [35] = {75, 9, 1, 121, 17,  24 , 10,},
    /* csky_mca_fully_connected_with_relu_v8_m8_b8_o32 */
    [36] = {77, 10, 2, 123, 19,  24 , 11,},
    /* csky_mca_fully_connected_with_relu_v8_m8_b8_o32 */
    [37] = {77, 9, 1, 123, 17,  24 , 10,},
    /* csky_mca_fully_connected_v8_m8_b16_o32 */
    [38] = {74, 10, 2, 120, 19,  25 , 11,},
    /* csky_mca_fully_connected_v8_m8_b16_o32 */
    [39] = {74, 9, 1, 120, 17,  25 , 10,},
    /* csky_mca_fully_connected_with_relu_v8_m8_b16_o32 */
    [40] = {74, 10, 2, 120, 19,  25 , 11,},
    /* csky_mca_fully_connected_with_relu_v8_m8_b16_o32 */
    [41] = {74, 9, 1, 120, 17,  25 , 10,},
    [42] = {30, 28,},        /* csky_mca_softmax_fxp16 */
    [43] = {22,},            /* csky_mca_asrc_mono_fxp16_config */
    [44] = {22,},            /* csky_mca_asrc_mono_fxp24_config */
    [45] = {22,},            /* csky_mca_asrc_mono_fxp32_config */
    [46] = {20,},            /* csky_mca_asrc_stereo_fxp16_config */
    [47] = {20,},            /* csky_mca_asrc_stereo_fxp24_config */
    [48] = {18,},            /* csky_mca_asrc_stereo_fxp32_config */
    [49] = {35, 6, 2,},      /* csky_mca_asrc_mono_fxp16 */
    [50] = {44, 6, 2,},      /* csky_mca_asrc_mono_fxp24 */
    [51] = {35, 6, 2,},      /* csky_mca_asrc_mono_fxp32 */
    [52] = {39, 9, 4,},      /* csky_mca_asrc_stereo_fxp16 */
    [53] = {49, 9, 4,},      /* csky_mca_asrc_stereo_fxp24 */
    [54] = {39, 9, 4,},      /* csky_mca_asrc_stereo_fxp32 */
    [55] = {15,},            /* csky_mca_fir_fxp16_coeff32_config */
    [56] = {15,},            /* csky_mca_fir_fxp24_coeff32_config */
    [57] = {15,},            /* csky_mca_fir_fxp32_coeff32_config */
    [58] = {20,},            /* csky_mca_fir_fxp16_coeff64_config */
    [59] = {19,},            /* csky_mca_fir_fxp24_coeff64_config */
    [60] = {20,},            /* csky_mca_fir_fxp32_coeff64_config */
    [61] = {33, 3, 2,},      /* csky_mca_fir_fxp16 */
    [62] = {44, 3, 2,},      /* csky_mca_fir_fxp24 */
    [63] = {35, 3, 2,},      /* csky_mca_fir_fxp32 */
    [64] = {33, 3, 3,},      /* csky_mca_fir_fxp16 */
    [65] = {44, 3, 3,},      /* csky_mca_fir_fxp24 */
    [66] = {35, 3, 3,},      /* csky_mca_fir_fxp32 */
    [67] = {15,},            /* csky_mca_iir_fxp16_coeff32_config */
    [68] = {14,},            /* csky_mca_iir_fxp24_coeff32_config */
    [69] = {15,},            /* csky_mca_iir_fxp32_coeff32_config */
    [70] = {16,},            /* csky_mca_iir_fxp16_coeff64_config */
    [71] = {16,},            /* csky_mca_iir_fxp24_coeff64_config */
    [72] = {17,},            /* csky_mca_iir_fxp32_coeff64_config */
    [73] = {59, 13,},        /* csky_mca_iir_fxp16 */
    [74] = {76, 13,},        /* csky_mca_iir_fxp24 */
    [75] = {61, 13,},        /* csky_mca_iir_fxp32 */
    [76] = {59, 18,},        /* csky_mca_iir_fxp16 */
    [77] = {76, 18,},        /* csky_mca_iir_fxp24 */
    [78] = {61, 18,},        /* csky_mca_iir_fxp32 */
};

#define Q16 (16)

typedef struct {
    uint64_t lo;  // Low part
    union {
        uint64_t u; // High part as unsigned 64-bit int
        int64_t s;  // High part as signed 64-bit int
    } hi;         // High part
} int128_t;

// Bit-width of input & output data
typedef enum {
    DRV_ACC_DATA_BITS_8  = 0,
    DRV_ACC_DATA_BITS_16 = 1,
    DRV_ACC_DATA_BITS_32 = 2,
} drv_acc_data_bits_t;

// Shift dir of input & output data
typedef enum {
    DRV_ACC_SHIFT_LEFT  = 0,
    DRV_ACC_SHIFT_RIGHT = 1,
} drv_acc_shift_dir_t;

// Mode selection of ACC
typedef enum {
    DRV_ACC_MODE_SEL_MAC      = 0,
    DRV_ACC_MODE_SEL_VEC_MUL  = 2,
    DRV_ACC_MODE_SEL_VEC_ADD  = 3,
    DRV_ACC_MODE_SEL_SOFTMAX  = 9,
} drv_acc_mode_sel_t;

typedef enum {
  THEAD_MCA_SIM_VEC_OP_MUL,
  THEAD_MCA_SIM_VEC_OP_ADD,
} csky_mca_sim_vec_op_t;

// ASRC work mode
typedef enum {
    DRV_ASRC_MODE_ASRC = 0,
    DRV_ASRC_MODE_FIR  = 2,
    DRV_ASRC_MODE_IIR  = 3,
} drv_asrc_mode_t;

// ASRC source data mode
typedef enum {
    DRV_ASRC_DATA_MODE_32_BITS                   = 0,
    DRV_ASRC_DATA_MODE_24_BITS                   = 1,
    DRV_ASRC_DATA_MODE_16_BITS_STORED_AS_32_BITS = 2,
    DRV_ASRC_DATA_MODE_16_BITS                   = 3,
} drv_asrc_data_mode_t;

// ASRC source data channel number selection
typedef enum {
    DRV_ASRC_CH_NUM_SEL_STEREO = 0,
    DRV_ASRC_CH_NUM_SEL_MONO   = 1,
} drv_asrc_ch_num_sel_t;

// FIR & IIR coefficient data mode selection
typedef enum {
    DRV_FIR_IIR_COEFF_SEL_32_BITS = 0, // Q=1.7.24
    DRV_FIR_IIR_COEFF_SEL_64_BITS = 1, // Q=1.15.48
} drv_fir_iir_coeff_sel_t;

typedef struct {
    void *result;
    uint32_t result_shift_bits;
    drv_acc_shift_dir_t result_shift_dir;
    drv_acc_data_bits_t result_bits;
} output_handler_params_t;

static inline uint32_t reg_field_extract(uint32_t reg, uint32_t width,
        uint32_t pos)
{
    return ((reg >> pos) & ((1U << width) - 1));
}

static inline void reg_field_set(uint32_t *reg, uint32_t pos,
        uint32_t value, uint32_t width)
{
    *reg &= 0xffffffff ^ (((1U << width) - 1) << pos);
    *reg |= (value << pos);
}

#define ACC_COMP_EN_POS           (0)
#define ACC_COMP_DONE_POS         (1)
#define ACC_INIT_DATA_SD_POS      (23)
#define ACC_INIT_DATA_SB_POS      (18)
#define ACC_INIT_DATA_BYPASSN_POS (10)
#define ACC_ACTIVE_BYPASSN_POS    (11)
#define ACC_DATA_A_LOOP_EN_POS    (17)
#define ACC_DATA_B_LOOP_EN_POS    (16)
#define ACC_INIT_DATA_BITS_POS    (25)
#define ACC_INPUT_DATA_BITS_POS   (6)
#define ACC_RESULT_BITS_POS       (7)
#define VEC_SCALAR_EN_POS         (27)
#define ACC_DATA_DEPTH_POS        (0)
#define ACC_DATA_WAY_POS          (16)
#define ACC_ACCESS_ES_POS         (3)
#define ACC_COMP_MODE_POS         (2)
#define ASRC_COMP_EN_POS          (0)
#define ASRC_COMP_START_POS       (0)
#define ASRC_COMP_MODE_POS        (0)
#define ASRC_DATA_MODE_POS        (4)
#define ASRC_ACCESS_ES_POS        (11)
#define MAC_RESULT_SB_POS         (0)
#define MAC_RESULT_SD_POS         (6)
#define MAC_RESULT_BITS_POS       (7)
#define SOFTMAX_RESULT_SB_POS     (17)
#define SOFTMAX_RESULT_SD_POS     (24)
#define SOFTMAX_RESULT_BITS_POS   (25)
#define IIR_COEF_SEL_POS          (0)
#define IIR_OUT_SEL_L_SEL_POS     (4)
#define FIR_COEF_SEL_POS          (0)
#define FIR_ORDER_POS             (4)
#define FIR_OUT_SEL_L_SEL_POS     (16)
#define ASRC_CH_NUM_SEL_POS       (0)
#define ASRC_LIST_SIZE_POS        (8)
#define ASRC_ORDER_POS            (4)

#define TYPE_THEAD_MCA  "csky_mca"
#define THEAD_MCA(obj)  OBJECT_CHECK(csky_mca_state, (obj), TYPE_THEAD_MCA)

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

// IIR order, i.e. history length of input
#define DRV_IIR_ORDER 2

// IIR history length of output
#define DRV_IIR_OUTPUT_HISTORY_LENGTH 2

void int128_mac(int128_t *i128, int64_t a, int64_t b);
bool int128_is_negative(const int128_t *i128);
void int128_negate(int128_t *i128);
void int128_init(int128_t *i128);
void int128_add_mca(int128_t *i128, int64_t v);
static void mul_64_64_keep_128(int64_t a, int64_t b, int128_t *product);
void int128_shift_left(int128_t *i128, unsigned int shift_bits);
void int128_shift_right(int128_t *i128, unsigned int shift_bits);
void int128_round(int128_t *i128, unsigned int frac_bits);
int64_t int128_sat_to_int64(const int128_t *i128);
uint32_t csky_mca_sim_asrc(drv_asrc_data_mode_t data_mode,
                           drv_asrc_ch_num_sel_t ch_num_sel, uint32_t order,
                           uint32_t list_size, const void *ch1_data_start,
                           const void *ch1_data_end, const void *ch2_data_start,
                           const void *ch2_data_end, const int32_t *coeff,
                           void *result, uint32_t *asrc_pointer);

uint32_t csky_mca_sim_fir(drv_asrc_data_mode_t data_mode,
                          drv_fir_iir_coeff_sel_t coeff_sel, uint32_t order,
                          uint32_t out_loc_sel, const void *data_start,
                          const void *data_end, const void *coeff,
                          void *result);

uint32_t csky_mca_sim_iir(drv_asrc_data_mode_t data_mode,
                          drv_fir_iir_coeff_sel_t coeff_sel,
                          uint32_t out_loc_sel, const void *data_start,
                          const void *data_end, const void *coeff, uint32_t yn1,
                          uint32_t yn2, void *result);

void csky_mca_sim_mac(
    const void *data_a, const void *data_b, const void *init_data, void *result,
    uint32_t data_depth, uint32_t data_way, drv_acc_data_bits_t input_data_bits,
    bool enable_init_data, bool enable_active, bool enable_data_b_loop,
    bool enable_data_a_loop, uint32_t init_data_shift_bits,
    drv_acc_shift_dir_t init_data_shift_dir, drv_acc_data_bits_t init_data_bits,
    uint32_t result_shift_bits, drv_acc_shift_dir_t result_shift_dir,
    drv_acc_data_bits_t result_bits, uint32_t step);

void csky_mca_sim_softmax(const void *data_a, void *data_b, void *result,
                          uint32_t data_depth, uint32_t result_shift_bits,
                          drv_acc_shift_dir_t result_shift_dir,
                          drv_acc_data_bits_t result_bits);

void csky_mca_sim_vec(csky_mca_sim_vec_op_t vec_op, const void *data_a,
                      const void *data_b, void *result, uint32_t data_depth,
                      drv_acc_data_bits_t input_data_bits,
                      bool enable_vec_scalar, uint32_t result_shift_bits,
                      drv_acc_shift_dir_t result_shift_dir,
                      drv_acc_data_bits_t result_bits, uint32_t scalar);

static inline int64_t csky_mca_sim_acc_read_data(
    const void **addr, drv_acc_data_bits_t data_bits)
{
    int64_t value = 0;
    switch (data_bits) {
    case DRV_ACC_DATA_BITS_8:
        value = *(const int8_t *)*addr;
        *addr = (const char *)*addr + sizeof(int8_t);
        break;
    case DRV_ACC_DATA_BITS_16:
        value = *(const int16_t *)*addr;
        *addr = (const char *)*addr + sizeof(int16_t);
        break;
    case DRV_ACC_DATA_BITS_32:
        value = *(const int32_t *)*addr;
        *addr = (const char *)*addr + sizeof(int32_t);
        break;
    }
    return value;
}

static inline
uint32_t drv_asrc_size_of_data_mode(drv_asrc_data_mode_t data_mode) {
    switch (data_mode) {
    case DRV_ASRC_DATA_MODE_32_BITS:
        return 4;
    case DRV_ASRC_DATA_MODE_24_BITS:
        return 4;
    case DRV_ASRC_DATA_MODE_16_BITS_STORED_AS_32_BITS:
        return 4;
    case DRV_ASRC_DATA_MODE_16_BITS:
        return 2;
    default:
        assert(0 && "Invalid data mode.");
        return 0;
    }
}

static uint32_t n_power_of_2(fxp32_q16_t value)
{
    uint32_t n = 0;

    while (value >>= 1) {
        n++;
    }

    return n;
}

// The input range should be [1, 1024].
#define assert_input_range(input) \
    assert((input) >= (1 << Q16) && (input) <= (1024 << Q16))

static fxp32_q16_t round_shift64(int64_t in, uint32_t shift)
{
    in += 1 << (shift - 1);

    return (fxp32_q16_t)(in >> shift);
}

#define INT128_BITS (CHAR_BIT * sizeof(int128_t))
#define INT128_BITS_HALF (INT128_BITS >> 1)

void int128_init(int128_t *i128)
{
    i128->lo = 0;
    i128->hi.u = 0;
}

void int128_add_mca(int128_t *i128, int64_t v)
{
    uint64_t uv = v;
    if ((i128->lo += uv) < uv) {
        ++i128->hi.u;
    }
    if (v < 0) {
        i128->hi.u += ~(uint64_t)0;
    }
}

static void mul_64_64_keep_128(int64_t a, int64_t b, int128_t *product) {
    int sign = (a < 0 ? 1 : 0) ^ (b < 0 ? 1 : 0);

    uint64_t ua = a < 0 ? -a : a;
    uint64_t ub = b < 0 ? -b : b;

    // BIT: [0, 64)
    uint64_t prod_0 = (ua & UINT32_MAX) * (ub & UINT32_MAX);
    // BIT: [32, 96)
    uint64_t prod_1 = (ua & UINT32_MAX) * (ub >> 32);
    // BIT: [32, 96)
    uint64_t prod_2 = (ua >> 32) * (ub & UINT32_MAX);
    // BIT: [64, 128)
    uint64_t prod_3 = (ua >> 32) * (ub >> 32);

    uint64_t prod_00 = prod_0 >> 32;
    if ((prod_1 += prod_00) < prod_00) {
        prod_3 += (uint64_t)1 << 32;
    }
    if ((prod_1 += prod_2) < prod_2) {
        prod_3 += (uint64_t)1 << 32;
    }
    prod_3 += (prod_1 >> 32);

    product->lo = (prod_1 << 32) | (prod_0 & UINT32_MAX);
    product->hi.u = prod_3;

    if (sign) {
        int128_negate(product);
    }
}

void int128_mac(int128_t *i128, int64_t a, int64_t b) {
    int128_t product;
    mul_64_64_keep_128(a, b, &product);

    if ((i128->lo += product.lo) < product.lo) {
        ++i128->hi.u;
    }
    i128->hi.u += product.hi.u;
}

bool int128_is_negative(const int128_t *i128) {
    return i128->hi.s < 0;
}

void int128_negate(int128_t *i128) {
    i128->lo = ~i128->lo;
    i128->hi.u = ~i128->hi.u;
    int128_add_mca(i128, 1);
}

void int128_shift_left(int128_t *i128, unsigned int shift_bits)
{
    if (shift_bits >= INT128_BITS) {
        i128->lo = 0;
        i128->hi.u = 0;
    } else if (shift_bits >= INT128_BITS_HALF) {
        i128->hi.u = i128->lo << (shift_bits - INT128_BITS_HALF);
        i128->lo = 0;
    } else if (shift_bits > 0) {
        i128->hi.u <<= shift_bits;
        i128->hi.u |= (i128->lo >> (INT128_BITS_HALF - shift_bits));
        i128->lo <<= shift_bits;
    }
}

void int128_shift_right(int128_t *i128, unsigned int shift_bits) {
    if (shift_bits >= INT128_BITS) {
        i128->hi.s >>= shift_bits;
        i128->lo = i128->hi.u;
    } else if (shift_bits >= INT128_BITS_HALF) {
        i128->lo = i128->hi.s >> (shift_bits - INT128_BITS_HALF);
        i128->hi.s >>= shift_bits;
    } else if (shift_bits > 0) {
        i128->lo >>= shift_bits;
        i128->lo |= (i128->hi.s << (INT128_BITS_HALF - shift_bits));
        i128->hi.s >>= shift_bits;
    }
}

void int128_round(int128_t *i128, unsigned int frac_bits) {
    assert(frac_bits < INT128_BITS);

    if (frac_bits == 0) {
        return;
    }

    bool is_negative = int128_is_negative(i128);
    if (is_negative) {
        int128_negate(i128);
    }

    if (frac_bits <= INT128_BITS_HALF) {
        uint64_t mask = (uint64_t)1 << (frac_bits - 1);
        if (i128->lo & mask) {
            if ((i128->lo += mask) < mask) {
                ++i128->hi.u;
            }
        }
        i128->lo &= (~((mask << 1) - 1));
    } else {
        uint64_t mask = (uint64_t)1 << (frac_bits - 1 - INT128_BITS_HALF);
        if (i128->hi.u & mask) {
            i128->hi.u += mask;
        }
        i128->hi.u &= (~((mask << 1) - 1));
        i128->lo = 0;
    }

    if (is_negative) {
        int128_negate(i128);
    }
}

int64_t int128_sat_to_int64(const int128_t *i128) {
    if (i128->hi.s > 0) {
        return INT64_MAX;
    }
    if (i128->hi.s < -1) {
        return INT64_MIN;
    }
    uint64_t mask = (uint64_t)1 << (INT128_BITS_HALF - 1);
    if (i128->hi.s == 0) {
        return (i128->lo & mask) ? INT64_MAX : i128->lo;
    }
    return (i128->lo & mask) ? i128->lo : INT64_MIN;
}

typedef struct csky_mca_state {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t data_a_addr;
    uint32_t data_b_addr;
    uint32_t init_data_addr;
    uint32_t result_addr;
    uint32_t acc_comp_length;
    uint32_t acc_ctrl;
    uint32_t acc_result_ctrl;
    uint32_t acc_step;
    uint32_t scalar;
    uint32_t asrc_en;
    uint32_t asrc_start;
    uint32_t asrc_mode;
    uint32_t asrc_ctrl;
    uint32_t fir_ctrl;
    uint32_t iir_ctrl;
    uint32_t point_clr;
    uint32_t ch1d_start_addr;
    uint32_t ch2d_start_addr;
    uint32_t ch1d_end_addr;
    uint32_t ch2d_end_addr;
    uint32_t asrc_coef_addr;
    uint32_t fir_start_addr;
    uint32_t fir_end_addr;
    uint32_t fir_coef_addr;
    uint32_t iir_start_addr;
    uint32_t iir_end_addr;
    uint32_t iir_coef_addr;
    uint32_t iir_yn1d;
    uint32_t iir_yn2d;
    uint32_t asrc_result_addr;
    uint32_t result_size;
    uint32_t asrc_sr;
    uint32_t asrc_im;
    uint32_t asrc_is;
    uint32_t asrc_ris;
    uint32_t asrc_ic;
    uint32_t asrc_dmaen;
    uint32_t intr_sta;
    uint32_t raw_intr_sta;
    uint32_t intr_unmask;
    uint32_t intr_clr;

} csky_mca_state;

uint32_t g_drv_sim_asrc_pointer;

static inline
uint32_t drv_acc_size_of_data_bits(drv_acc_data_bits_t data_bits) {
    switch (data_bits) {
    case DRV_ACC_DATA_BITS_8:
        return 1;
    case DRV_ACC_DATA_BITS_16:
        return 2;
    case DRV_ACC_DATA_BITS_32:
        return 4;
    default:
        assert(0 && "Invalid data bits.");
        return 0;
    }
}

static inline int64_t csky_mca_sim_asrc_read_data(
    const void **data, drv_asrc_data_mode_t data_mode) {
    int64_t value = 0;
    switch (data_mode) {
    case DRV_ASRC_DATA_MODE_32_BITS:
        value = *(const int32_t *)*data;
        *data = (const char *)*data + sizeof(int32_t);
        break;
    case DRV_ASRC_DATA_MODE_24_BITS:
        value = (*(const int32_t *)*data << 8) >> 8;
        *data = (const char *)*data + sizeof(int32_t);
        break;
    case DRV_ASRC_DATA_MODE_16_BITS_STORED_AS_32_BITS:
        value = (*(const int32_t *)*data << 16) >> 16;
        *data = (const char *)*data + sizeof(int32_t);
        break;
    case DRV_ASRC_DATA_MODE_16_BITS:
        value = *(const int16_t *)*data;
        *data = (const char *)*data + sizeof(int16_t);
        break;
    }
    return value;
}

static inline int64_t csky_mca_sim_asrc_read_filter_coeff(
    const void **coeff, drv_fir_iir_coeff_sel_t coeff_sel) {
    int64_t value = 0;
    switch (coeff_sel) {
    case DRV_FIR_IIR_COEFF_SEL_32_BITS:
        value = *(const int32_t *)*coeff;
        *coeff = (const char *)*coeff + sizeof(int32_t);
        break;
    case DRV_FIR_IIR_COEFF_SEL_64_BITS:
        value = *(const int64_t *)*coeff;
        *coeff = (const char *)*coeff + sizeof(int64_t);
        break;
    }
    return value;
}

static inline uint32_t csky_mca_sim_asrc_write_data(
    void **addr, const int128_t *value, drv_asrc_data_mode_t data_mode) {
    int32_t value_i32 = 0;
    int64_t value_i64 = int128_sat_to_int64(value);
    switch (data_mode) {
    case DRV_ASRC_DATA_MODE_32_BITS:
        value_i32 = *(int32_t *)*addr =
            (int32_t)min((int64_t)INT32_MAX, max((int64_t)INT32_MIN, value_i64));
        *addr = (char *)*addr + sizeof(int32_t);
        break;
    case DRV_ASRC_DATA_MODE_24_BITS:
        value_i32 = *(int32_t *)*addr =
            0x00FFFFFF & (int32_t)min((int64_t)(INT32_MAX >> 8),
                                      max((int64_t)(INT32_MIN >> 8), value_i64));
        *addr = (char *)*addr + sizeof(int32_t);
        break;
    case DRV_ASRC_DATA_MODE_16_BITS_STORED_AS_32_BITS:
        value_i32 = *(int32_t *)*addr =
            0x0000FFFF &
            (int32_t)min((int64_t)INT16_MAX, max((int64_t)INT16_MIN, value_i64));
        *addr = (char *)*addr + sizeof(int32_t);
        break;
    case DRV_ASRC_DATA_MODE_16_BITS:
        value_i32 = *(int16_t *)*addr =
            (int16_t)min((int64_t)INT16_MAX, max((int64_t)INT16_MIN, value_i64));
        *addr = (char *)*addr + sizeof(int16_t);
        break;
    }
    return value_i32;
}

uint32_t csky_mca_sim_asrc(drv_asrc_data_mode_t data_mode,
                           drv_asrc_ch_num_sel_t ch_num_sel, uint32_t order,
                           uint32_t list_size, const void *ch1_data_start,
                           const void *ch1_data_end, const void *ch2_data_start,
                           const void *ch2_data_end, const int32_t *coeff,
                           void *result, uint32_t *asrc_pointer) {
    uint32_t result_size = 0;

    uint32_t data_elem_size = drv_asrc_size_of_data_mode(data_mode);
    const char *ch1_data_start_iter = (const char *)ch1_data_start;
    const char *ch1_data_asrc_iter = ch1_data_start_iter + data_elem_size * order;
    const char *ch2_data_start_iter = (const char *)ch2_data_start;
    while (ch1_data_asrc_iter <= (const char *)ch1_data_end) {
        int128_t sum1, sum2;
        int128_init(&sum1);

        if (ch_num_sel == DRV_ASRC_CH_NUM_SEL_STEREO) {
            int128_init(&sum2);
        }

        assert(*asrc_pointer < list_size);
        const int32_t *coeff_iter = coeff + *asrc_pointer * (order + 2);
        const void *ch1_data_iter = ch1_data_start_iter;
        const void *ch2_data_iter = ch2_data_start_iter;

        for (uint32_t i = 0; i <= order; ++i) {
            int32_t coeff_value = *coeff_iter++;
            int64_t data_value;
            data_value = csky_mca_sim_asrc_read_data(&ch1_data_iter, data_mode);
            int128_add_mca(&sum1, coeff_value * data_value);

            if (ch_num_sel == DRV_ASRC_CH_NUM_SEL_STEREO) {
                data_value = csky_mca_sim_asrc_read_data(&ch2_data_iter, data_mode);
                int128_add_mca(&sum2, coeff_value * data_value);
            }
      }

        const uint32_t q_output = 31;  // Coeff is Q1.0.31
        int128_round(&sum1, q_output);
        int128_shift_right(&sum1, q_output);
        csky_mca_sim_asrc_write_data(&result, &sum1, data_mode);
        result_size += data_elem_size;

        if (ch_num_sel == DRV_ASRC_CH_NUM_SEL_STEREO) {
            int128_round(&sum2, q_output);
            int128_shift_right(&sum2, q_output);
            csky_mca_sim_asrc_write_data(&result, &sum2, data_mode);
            result_size += data_elem_size;
        }

        ch1_data_start_iter += data_elem_size * (uint32_t)*coeff_iter;
        ch1_data_asrc_iter += data_elem_size * (uint32_t)*coeff_iter;
        ch2_data_start_iter += data_elem_size * (uint32_t)*coeff_iter;

        *asrc_pointer += 1;
        if (*asrc_pointer == list_size) {
            *asrc_pointer = 0;
        }
    }

    return result_size;
}

uint32_t csky_mca_sim_fir(drv_asrc_data_mode_t data_mode,
                          drv_fir_iir_coeff_sel_t coeff_sel, uint32_t order,
                          uint32_t out_loc_sel, const void *data_start,
                          const void *data_end, const void *coeff,
                          void *result) {
    uint32_t result_size = 0;

    uint32_t data_elem_size = drv_asrc_size_of_data_mode(data_mode);
    const char *data_start_iter = (const char *)data_start;
    const char *data_filter_iter = data_start_iter + data_elem_size * order;

    while (data_filter_iter <= (const char *)data_end) {
        int128_t sum;
        int128_init(&sum);

        const void *coeff_iter = coeff;
        const void *data_iter = data_start_iter;
        for (uint32_t i = 0; i <= order; ++i) {
            int64_t coeff_value =
                csky_mca_sim_asrc_read_filter_coeff(&coeff_iter, coeff_sel);
            int64_t data_value = csky_mca_sim_asrc_read_data(&data_iter, data_mode);
            if (coeff_sel == DRV_FIR_IIR_COEFF_SEL_32_BITS) {
                int128_add_mca(&sum, coeff_value * data_value);
            } else {
                int128_mac(&sum, coeff_value, data_value);
            }
        }

        uint32_t q_output =
          coeff_sel == DRV_FIR_IIR_COEFF_SEL_32_BITS ? 24 : (out_loc_sel * 4);
        int128_round(&sum, q_output);
        int128_shift_right(&sum, q_output);
        csky_mca_sim_asrc_write_data(&result, &sum, data_mode);
        result_size += data_elem_size;

        data_start_iter += data_elem_size;
        data_filter_iter += data_elem_size;
    }

    return result_size;
}

uint32_t csky_mca_sim_iir(drv_asrc_data_mode_t data_mode,
                          drv_fir_iir_coeff_sel_t coeff_sel,
                          uint32_t out_loc_sel, const void *data_start,
                          const void *data_end, const void *coeff, uint32_t yn1,
                          uint32_t yn2, void *result) {
    uint32_t result_size = 0;

    uint32_t data_elem_size = drv_asrc_size_of_data_mode(data_mode);
    const char *data_start_iter = (const char *)data_start;
    const char *data_filter_iter =
        data_start_iter + data_elem_size * DRV_IIR_ORDER;
    while (data_filter_iter <= (const char *)data_end) {
        int128_t sum;
        int128_init(&sum);

        const void *coeff_iter = coeff;
        const void *data_iter = data_start_iter;
        for (uint32_t i = 0; i <= (DRV_IIR_ORDER + DRV_IIR_OUTPUT_HISTORY_LENGTH);
             ++i) {
            int64_t coeff_value =
                csky_mca_sim_asrc_read_filter_coeff(&coeff_iter, coeff_sel);
            int64_t data_value;
            if (i <= DRV_IIR_ORDER) {
                data_value = csky_mca_sim_asrc_read_data(&data_iter, data_mode);
            } else {
                const void *temp = (i == DRV_IIR_ORDER + 1) ? &yn1 : &yn2;
                data_value = csky_mca_sim_asrc_read_data(&temp, data_mode);
            }
            if (coeff_sel == DRV_FIR_IIR_COEFF_SEL_32_BITS) {
                int128_add_mca(&sum, coeff_value * data_value);
            } else {
                int128_mac(&sum, coeff_value, data_value);
            }
        }

        uint32_t q_output =
            coeff_sel == DRV_FIR_IIR_COEFF_SEL_32_BITS ? 24 : (out_loc_sel * 4);
        int128_round(&sum, q_output);
        int128_shift_right(&sum, q_output);
        yn2 = yn1;
        yn1 = csky_mca_sim_asrc_write_data(&result, &sum, data_mode);
        result_size += data_elem_size;

        data_start_iter += data_elem_size;
        data_filter_iter += data_elem_size;
    }

    return result_size;
}

static inline void csky_mca_sim_acc_write_data_from_i128(
    void **addr, int128_t *value, uint32_t shift_bits,
    drv_acc_shift_dir_t shift_dir, drv_acc_data_bits_t data_bits)
{
    switch (shift_dir) {
    case DRV_ACC_SHIFT_LEFT:
        int128_shift_left(value, shift_bits);
        break;
    case DRV_ACC_SHIFT_RIGHT:
        int128_shift_right(value, shift_bits);
        break;
    }

    int64_t value_i64 = int128_sat_to_int64(value);

    switch (data_bits) {
    case DRV_ACC_DATA_BITS_8:
        *(int8_t *)*addr =
            (int8_t)min((int64_t)INT8_MAX, max((int64_t)INT8_MIN, value_i64));
        *addr = (char *)*addr + sizeof(int8_t);
        break;
    case DRV_ACC_DATA_BITS_16:
        *(int16_t *)*addr =
            (int16_t)min((int64_t)INT16_MAX, max((int64_t)INT16_MIN, value_i64));
        *addr = (char *)*addr + sizeof(int16_t);
        break;
    case DRV_ACC_DATA_BITS_32:
        *(int32_t *)*addr =
            (int32_t)min((int64_t)INT32_MAX, max((int64_t)INT32_MIN, value_i64));
        *addr = (char *)*addr + sizeof(int32_t);
        break;
    }
}

void csky_mca_sim_mac(
    const void *data_a, const void *data_b, const void *init_data, void *result,
    uint32_t data_depth, uint32_t data_way, drv_acc_data_bits_t input_data_bits,
    bool enable_init_data, bool enable_active, bool enable_data_b_loop,
    bool enable_data_a_loop, uint32_t init_data_shift_bits,
    drv_acc_shift_dir_t init_data_shift_dir, drv_acc_data_bits_t init_data_bits,
    uint32_t result_shift_bits, drv_acc_shift_dir_t result_shift_dir,
    drv_acc_data_bits_t result_bits, uint32_t step)
{
    for (uint32_t i_depth = 0; i_depth < data_depth; ++i_depth) {
        int128_t sum;
        int128_init(&sum);

        // Adds with initial data.
        if (enable_init_data) {
            int64_t init_data_value =
              csky_mca_sim_acc_read_data(&init_data, init_data_bits);
            switch (init_data_shift_dir) {
            case DRV_ACC_SHIFT_LEFT:
                init_data_value <<= init_data_shift_bits;
                break;
            case DRV_ACC_SHIFT_RIGHT:
                init_data_value >>= init_data_shift_bits;
                break;
            }
            int128_add_mca(&sum, init_data_value);
        }

        const void *data_a_iter = data_a;
        const void *data_b_iter = data_b;

        // Multiplication & accumulation.
        for (uint32_t i_way = 0; i_way < data_way; ++i_way) {
            int64_t a = csky_mca_sim_acc_read_data(&data_a_iter, input_data_bits);
            int64_t b = csky_mca_sim_acc_read_data(&data_b_iter, input_data_bits);
            int128_add_mca(&sum, a * b);
        }

        // Performs activation function (ReLU is the only one).
        if (enable_active) {
            if (int128_is_negative(&sum)) {
                int128_init(&sum);
            }
        }

        // Shifts, saturates, and writes to the output.
        csky_mca_sim_acc_write_data_from_i128(&result, &sum, result_shift_bits,
                                              result_shift_dir, result_bits);

        // Updates input iterators.
        if (!enable_data_a_loop) {
            data_a = data_a_iter + step;
        }
        if (!enable_data_b_loop) {
            data_b = data_b_iter + step;
        }
    }
}

// -----------------------------------------------------------------
// HOW IT WORKS?
// Calculating "1/a" is same with solving y(x) = 0, where y(x) is:
// y(x) = 1/x - a......(1)
// And according to Newtown-Raphson method, we need to iteratively
// calculating the following function until converging:
// x(n+1) = x(n) * (2 - a * x[n])........(2);
// In case we get lost, remember that the goal is calculating "1/a",
// which is the same with the root of (1). And to solve (1), we need
// to iteratively calculating (2). Finally, if x converging to some
// value, we will have a approximated "1/a".
// BUT...here is the difficult part:
// 1. how to make sure that x can converge?
// 2. how to pick the initial value of x (x0)?
// The answer of the first question is that the converging range of
// x is (0, 2/a). And for the second question, the x0 must be very
// close to "1/a" to get faster convergence.
// It's really hard to pick x0, but we can use some trick to do that.
// Suppose we can limit the range of x to [0.5, 1], in which case the
// range of a will be [1, 2], we can pick 0.75 as the initial value
// and get a good convergence. So all we need to do is putting a into
// [1, 2], which can be done by dividing a with 2^n (n is the nearest
// n power of two that smaller than a).
// -----------------------------------------------------------------
// Here is the fake code of the entire solution:
//     let n = floor(log2(a));
//     a = a * 2^-n;
//     x = 0.75;
//     loop 5 times:
//         x = x * (2 - a * x);
//     loop end;
//     x = x * 2^-n;
//     return x;
// -----------------------------------------------------------------
// References:
// 1. How it works: https://www.dsprelated.com/showcode/201.php;
// 2. Newton-Raphson: https://en.wikipedia.org/wiki/Newton%27s_method;
// 3. Pick initial value: https://hal.inria.fr/inria-00071899/document;
static fxp32_q16_t reciprocal_q16(fxp32_q16_t input)
{
    // By now, it's only for softmax, which means the input
    // is in range [1, 1024].
    assert_input_range(input);

    // Get the nearest n power of 2 that is smaller than
    // the integer part of the input.
    uint32_t n = n_power_of_2(input >> Q16);

    // Move input to [1, 2], so the 1/input will be within
    // [0.5, 1], which is easy for determining the initial
    // value.
    input >>= n;

    // See reference 3, section 2.2.1 for picking initial value.
    // Since we moved input into [1, 2], we can further divide it
    // into two sections [1, 1.33333) and [1.33333, 2] for faster
    // convergence.
    fxp32_q16_t iterator =
        (input - 87381) < 0  // 1.3 q16.
            ? 56246          // 0.858245067268760, beta 4 of [0, 1.33333)
            : 39421;         // 0.601524275335661, beta 4 of [1.33333, 2]

    const fxp32_q16_t two = 2 << Q16;

    int64_t temp_64;
    int32_t temp_32;

    // Why 3 iterations?
    // Under following conditions:
    //     1. input in range [1, 1024];
    //     2. output has at least q15 precision;
    // iterates 3 rounds can have expected convergence.
    for (size_t i = 0; i < 3; i++) {
        temp_64 = (int64_t)iterator * (int64_t)input;  // q32

        // Round shift.
        temp_32 = round_shift64(temp_64, Q16);

        temp_32 = two - temp_32;

        temp_64 = (int64_t)temp_32 * (int64_t)iterator;

        iterator = round_shift64(temp_64, Q16);
    }

    iterator = iterator >> n;

    return iterator;
}

static inline fxp32_q16_t round_shift(fxp32_q16_t in, uint8_t shift)
{
    in += 1 << (shift - 1);
    return in >> shift;
}

// The basic idea and code is from:
// https://www.quinapalus.com/efunc.html
// Another reference and to solve the negative part:
// https://github.com/Rockbox/rockbox/blob/master/lib/fixedpoint/fixedpoint.c
static fxp32_q16_t exp_q16(fxp32_q16_t x) {
    // Make sure the input is non-positive.
    assert(x <= 0);

    const fxp32_q16_t one_q16 = 0x00010000;

    // If x == 0x0, just return 1.0f;
    if (x == 0x0) {
        return one_q16;
    }

    // Try move x to the positive side.
    x = x + 0xb1721;  // log(65536) in q16.

    // If x is still negative, we can directly return
    // 0x0 since the output will be too small for q16.
    if (x < 0) {
        return 0x0;
    }

    fxp32_q16_t temp;

    // Start from exp(-log(65536)) in q16.
    fxp32_q16_t out = 0x1;

    temp = x - 0x58b91;  // log(256) in q16.
    if (temp >= 0) {
        x = temp;
        out <<= 8;
    }

    temp = x - 0x2c5c8;  // log(16) in q16.
    if (temp >= 0) {
        x = temp;
        out <<= 4;
    };

    temp = x - 0x162e4;  // log(4) in q16.
    if (temp >= 0) {
        x = temp;
        out <<= 2;
    }

    temp = x - 0x0b172;  // log(2) in q16.
    if (temp >= 0) {
        x = temp;
        out <<= 1;
    }

    temp = x - 0x067cd;  // log(3/2) in q16.
    if (temp >= 0) {
        x = temp;
        out += round_shift(out, 1);
    }

    temp = x - 0x03920;  // log(5/4) in q16.
    if (temp >= 0) {
        x = temp;
        out += round_shift(out, 2);
    }

    temp = x - 0x01e27;  // log(9/8) in q16.
    if (temp >= 0) {
        x = temp;
        out += round_shift(out, 3);
    }

    temp = x - 0x00f85;  // log(17/16) in q16.
    if (temp >= 0) {
        x = temp;
        out += round_shift(out, 4);
    }

    temp = x - 0x007e1;  // log(33/32) in q16.
    if (temp >= 0) {
        x = temp;
        out += round_shift(out, 5);
    }

    temp = x - 0x003f8;  // log(65/64) in q16.
    if (temp >= 0) {
        x = temp;
        out += round_shift(out, 6);
    }

    temp = x - 0x001fe;  // log(129/128) in q16.
    if (temp >= 0) {
        x = temp;
        out += round_shift(out, 7);
    }

    // The following lines handles the residual error. From
    // my understanding, the logic is similar with:
    //     out = out * (1 + x); // floating point.
    // which is for reducing the error at near zero, only
    // this solution can approach bit by bit.
    if (x & 0x100) {
        out += round_shift(out, 8);
    }
    if (x & 0x080) {
        out += round_shift(out, 9);
    }
    if (x & 0x040) {
        out += round_shift(out, 10);
    }
    if (x & 0x020) {
        out += round_shift(out, 11);
    }
    if (x & 0x010) {
        out += round_shift(out, 12);
    }
    if (x & 0x008) {
        out += round_shift(out, 13);
    }
    if (x & 0x004) {
        out += round_shift(out, 14);
    }
    if (x & 0x002) {
        out += round_shift(out, 15);
    }
    if (x & 0x001) {
        out += round_shift(out, 16);
    }

    return out;
}

static fxp32_q16_t get_max(const fxp32_q16_t *input, size_t size)
{
    fxp32_q16_t max = input[0];

    for (size_t i = 1; i < size; i++) {
        fxp32_q16_t value = input[i];

        int64_t temp = (int64_t)value - (int64_t)max;

        // sign bit is 0.
        if (temp >= 0) {
            max = value;
        }
    }

    return max;
}

static fxp16_q15_t round_saturate_shift(int64_t in, uint32_t shift)
{
     // Round.
     in += (1 << (shift - 1));

     // Shift and take the low 32 bit.
     // Since this method is only for softmax, so 32 bit
     // would be enough.
     int32_t temp = (int32_t)(in >> shift);

     // Saturate.
     // Since this method is only for softmax, so the
     // input must be positive and only deal with the
     // positive part would be enough.
     if (temp > INT16_MAX) temp = INT16_MAX;

     return (fxp16_q15_t)temp;
}

typedef void (*output_handler_t)(fxp16_q15_t output, size_t index,
                                 void *params);

// For the math explain, check this wiki page:
// https://zh.wikipedia.org/wiki/Softmax%E5%87%BD%E6%95%B0
static void softmax_q16_in_q15_out(const fxp32_q16_t *input, fxp32_q16_t *temp,
                                   size_t size, output_handler_t output_handler,
                                   void *params)
{
    assert(size <= 1024);

    // WHY?
    // Making sure every element in the input array to be
    // non-positive can prevent overflow in exp.
    fxp32_q16_t max = get_max(input, size);

    // The worest case would be 1024 * 2^16, which is
    // 0x4000000, and takes less than 32 bits.
    fxp32_q16_t sum_exp = 0;  // q16;

    for (size_t i = 0; i < size; i++) {
       // Expands to prevent overflow.
       int64_t diff = (int64_t)input[i] - (int64_t)max;

       fxp32_q16_t exp_out = 0x0;

       // Exp will return zero if its input is less than
       // -0xb1721.
       if (diff > -0xb1721) {
           fxp32_q16_t low_half = (fxp32_q16_t)diff;

           exp_out = exp_q16(low_half);  // q16.
       }

       temp[i] = exp_out;  // q16.

       sum_exp += exp_out;
    }

    // 1 / sum, q16.
    fxp32_q16_t inv_sum = reciprocal_q16(sum_exp);

    for (size_t i = 0; i < size; i++) {
        int64_t raw = (int64_t)temp[i] * (int64_t)inv_sum;  // q32.

        fxp16_q15_t out = round_saturate_shift(raw, Q16 + 1);  // q15.

        output_handler(out, i, params);  // q15.
    }
}


static void output_handler(fxp16_q15_t output, size_t index, void *params)
{
    output_handler_params_t *tp = (output_handler_params_t *)params;
    int64_t value = output;
    switch (tp->result_shift_dir) {
    case DRV_ACC_SHIFT_LEFT:
        value <<= tp->result_shift_bits;
        break;
    case DRV_ACC_SHIFT_RIGHT:
        value >>= tp->result_shift_bits;
        break;
    }
    switch (tp->result_bits) {
    case DRV_ACC_DATA_BITS_8:
        break;
    case DRV_ACC_DATA_BITS_16:
        *(int16_t *)tp->result =
            (int16_t)min((int64_t)INT16_MAX, max((int64_t)INT16_MIN, value));
        tp->result = (char *)tp->result + sizeof(int16_t);
        break;
    case DRV_ACC_DATA_BITS_32:
        *(int32_t *)tp->result =
            (int32_t)min((int64_t)INT32_MAX, max((int64_t)INT32_MIN, value));
        tp->result = (char *)tp->result + sizeof(int32_t);
        break;
    }
}

void csky_mca_sim_softmax(const void *data_a, void *data_b, void *result,
                          uint32_t data_depth, uint32_t result_shift_bits,
                          drv_acc_shift_dir_t result_shift_dir,
                          drv_acc_data_bits_t result_bits)
{
    output_handler_params_t params = {
        .result = result,
        .result_shift_bits = result_shift_bits,
        .result_shift_dir = result_shift_dir,
        .result_bits = result_bits,
    };

    softmax_q16_in_q15_out(data_a, data_b, data_depth, output_handler, &params);
}

static inline void csky_mca_sim_acc_write_data_from_i64(
    void **addr, int64_t value, uint32_t shift_bits,
    drv_acc_shift_dir_t shift_dir, drv_acc_data_bits_t data_bits)
{
    switch (shift_dir) {
    case DRV_ACC_SHIFT_LEFT:
        value <<= shift_bits;
        break;
    case DRV_ACC_SHIFT_RIGHT:
        value >>= shift_bits;
        break;
    }
    switch (data_bits) {
    case DRV_ACC_DATA_BITS_8:
        *(int8_t *)*addr =
            (int8_t)min((int64_t)INT8_MAX, max((int64_t)INT8_MIN, value));
        *addr = (char *)*addr + sizeof(int8_t);
        break;
    case DRV_ACC_DATA_BITS_16:
        *(int16_t *)*addr =
            (int16_t)min((int64_t)INT16_MAX, max((int64_t)INT16_MIN, value));
        *addr = (char *)*addr + sizeof(int16_t);
        break;
    case DRV_ACC_DATA_BITS_32:
        *(int32_t *)*addr =
            (int32_t)min((int64_t)INT32_MAX, max((int64_t)INT32_MIN, value));
        *addr = (char *)*addr + sizeof(int32_t);
        break;
    }
}

void csky_mca_sim_vec(csky_mca_sim_vec_op_t vec_op, const void *data_a,
                      const void *data_b, void *result, uint32_t data_depth,
                      drv_acc_data_bits_t input_data_bits,
                      bool enable_vec_scalar, uint32_t result_shift_bits,
                      drv_acc_shift_dir_t result_shift_dir,
                      drv_acc_data_bits_t result_bits, uint32_t scalar)
{
    int64_t a, b, c = 0;

    for (uint32_t i_depth = 0; i_depth < data_depth; ++i_depth) {
        a = csky_mca_sim_acc_read_data(&data_a, input_data_bits);
        if (enable_vec_scalar) {
            b = (int32_t)scalar;
        } else {
            b = csky_mca_sim_acc_read_data(&data_b, input_data_bits);
        }
        switch (vec_op) {
        case THEAD_MCA_SIM_VEC_OP_MUL:
            c = a * b;
            break;
        case THEAD_MCA_SIM_VEC_OP_ADD:
            c = a + b;
            break;
        }
        csky_mca_sim_acc_write_data_from_i64(&result, c, result_shift_bits,
                                         result_shift_dir, result_bits);
    }
}
/* fixme: handle cross bank */
static bool diff_bank(uint32_t addr_a, uint32_t addr_b)
{
    if ((addr_a ^ addr_b) & 0xffff8000) {
        return true;
    } else {
        return false;
    }
}

static void csky_compute_asrc(csky_mca_state *s)
{
    uint32_t mode, byte, index, cycles;
    drv_asrc_data_mode_t data_mode;
    uint32_t c1, c2, type; /* for trace device event */

    mode          = reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS);
    data_mode     = reg_field_extract(s->asrc_mode, 2, ASRC_DATA_MODE_POS);

    switch (data_mode) {
    case 0:
    case 1:
    case 2:
        byte = 4;
        break;
    case 3:
        byte = 2;
        break;
    default:
        break;
    }

    switch (mode) {
    case DRV_ASRC_MODE_ASRC: {
        uint32_t num_ch1 = ((s->ch1d_end_addr - s->ch1d_start_addr) / byte) + 1;
        uint32_t num_ch2 = ((s->ch2d_end_addr - s->ch2d_start_addr) / byte) + 1;
        static int32_t ch1_data[1024];
        static int32_t ch2_data[1024];
        static int32_t coef[256 * 16];
        static int32_t result[1024];

        drv_asrc_ch_num_sel_t ch_num = reg_field_extract(s->asrc_ctrl, 1,
                                        ASRC_CH_NUM_SEL_POS);
        uint32_t list_size   = reg_field_extract(s->asrc_ctrl, 8,
                                        ASRC_LIST_SIZE_POS);
        uint32_t asrc_order  = reg_field_extract(s->asrc_ctrl, 4,
                                        ASRC_ORDER_POS);
        uint32_t *pasrc_pointer = &g_drv_sim_asrc_pointer;

        cpu_physical_memory_read(s->ch1d_start_addr, ch1_data, num_ch1 * byte);
        cpu_physical_memory_read(s->ch2d_start_addr, ch2_data, num_ch2 * byte);
        cpu_physical_memory_read(s->asrc_coef_addr, coef, 4096 * byte);

        s->result_size = csky_mca_sim_asrc(data_mode, ch_num, asrc_order,
            list_size + 1, ch1_data, (char *)ch1_data + (num_ch1 - 1) * byte,
            ch2_data, (char *)ch2_data + (num_ch2 - 1) * byte, coef, result,
            pasrc_pointer);

        index = 49;
        if (ch_num == DRV_ASRC_CH_NUM_SEL_STEREO) {
            index = 52;
        }
        c1 = coeff[index][1];
        c2 = coeff[index][2];
        type = MCA_ASRC;
        cycles = (s->result_size / 4) * c1 +
                 (s->result_size / 4) * (asrc_order + 1) * c2;
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (type << 8), cycles);

        cpu_physical_memory_write(s->asrc_result_addr, result, s->result_size);
        } break;
    case DRV_ASRC_MODE_FIR: {
        uint32_t num = (s->fir_end_addr - s->fir_start_addr) / byte + 1;
        static int32_t data[1024];
        static int32_t coef[4 * 1024];
        static int32_t result[1024];

        drv_fir_iir_coeff_sel_t coeff_sel_fir = reg_field_extract(s->fir_ctrl, 1,
                                                FIR_COEF_SEL_POS);
        uint32_t fir_order         = reg_field_extract(s->fir_ctrl, 12,
                                        FIR_ORDER_POS);
        uint32_t out_loc_sel       = reg_field_extract(s->fir_ctrl, 5,
                                        FIR_OUT_SEL_L_SEL_POS);

        cpu_physical_memory_read(s->fir_start_addr, data, num * byte);
        cpu_physical_memory_read(s->fir_coef_addr, coef,
                (fir_order + 1) * (4 << coeff_sel_fir));

        s->result_size = csky_mca_sim_fir(data_mode, coeff_sel_fir, fir_order,
            out_loc_sel, data, (char *)data + (num - 1) * byte, coef, result);

        index = 61;
        if (coeff_sel_fir) {
            index = 64;
        }
        c1 = coeff[index][1];
        c2 = coeff[index][2];
        type = MCA_FIR;
        cycles = (num - fir_order) * c1 +
                 (num - fir_order) * (fir_order + 1) * c2;
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (type << 8), cycles);

        cpu_physical_memory_write(s->asrc_result_addr, result,
                s->result_size);
        } break;
    case DRV_ASRC_MODE_IIR: {
        uint32_t num = ((s->iir_end_addr - s->iir_start_addr) / byte) + 1;
        static int32_t data[1024];
        static int32_t coef[1024];
        static int32_t result[1024];

        drv_fir_iir_coeff_sel_t coeff_sel_iir = reg_field_extract(s->iir_ctrl, 1,
                                                IIR_COEF_SEL_POS);
        uint32_t out_loc_sel = reg_field_extract(s->iir_ctrl, 5,
                                                IIR_OUT_SEL_L_SEL_POS);
        uint32_t yn1 = s->iir_yn1d;
        uint32_t yn2 = s->iir_yn2d;

        cpu_physical_memory_read(s->iir_start_addr, data, num * byte);
        cpu_physical_memory_read(s->iir_coef_addr, coef,
                num * (4 << coeff_sel_iir));

        s->result_size = csky_mca_sim_iir(data_mode, coeff_sel_iir, out_loc_sel,
            data, (char *)data + (num - 1) * byte, coef, yn1, yn2, result);

        index = 73;
        if (coeff_sel_iir) {
            index = 76;
        }
        c1 = coeff[index][1];
        type = MCA_IIR;

        cycles = (num - 2) * c1;
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (type << 8), cycles);

        cpu_physical_memory_write(s->asrc_result_addr, result, s->result_size);

        } break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_compute_asrc: mode not available %x\n", (int)mode);
        break;
    }
}


static void csky_compute_acc(csky_mca_state *s)
{
    uint32_t mode;
    uint32_t index, cycles = 0;
    uint32_t c1, c2, unit, type; /* for trace device event */

    mode = reg_field_extract(s->acc_ctrl, 4, ACC_COMP_MODE_POS);

    switch (mode) {
    case DRV_ACC_MODE_SEL_MAC: {
        static int32_t data_a[1024 * 1024];
        static int32_t data_b[1024 * 1024];
        static int32_t result[1024];
        static int32_t init_data[1024];

        drv_acc_shift_dir_t init_data_shift_dir = reg_field_extract(s->acc_ctrl, 1,
                ACC_INIT_DATA_SD_POS);
        drv_acc_data_bits_t init_data_bits = reg_field_extract(s->acc_ctrl, 2,
                ACC_INIT_DATA_BITS_POS);
        drv_acc_shift_dir_t result_shift_dir = reg_field_extract(s->acc_result_ctrl, 1,
                MAC_RESULT_SD_POS);
        drv_acc_data_bits_t result_bits = reg_field_extract(s->acc_result_ctrl, 2,
                ACC_RESULT_BITS_POS);
        drv_acc_data_bits_t input_data_bits = reg_field_extract(s->acc_ctrl, 2,
                ACC_INPUT_DATA_BITS_POS);

        uint32_t data_depth    = reg_field_extract(s->acc_comp_length, 10,
                                    ACC_DATA_DEPTH_POS);
        uint32_t data_way      = reg_field_extract(s->acc_comp_length, 10,
                                    ACC_DATA_WAY_POS);
        bool enable_init_data  = reg_field_extract(s->acc_ctrl, 1,
                                    ACC_INIT_DATA_BYPASSN_POS);
        bool enable_active     = reg_field_extract(s->acc_ctrl, 1,
                                    ACC_ACTIVE_BYPASSN_POS);
        bool data_b_loop_en    = reg_field_extract(s->acc_ctrl, 1,
                                    ACC_DATA_B_LOOP_EN_POS);
        bool data_a_loop_en    = reg_field_extract(s->acc_ctrl, 1,
                                    ACC_DATA_A_LOOP_EN_POS);
        uint32_t result_sb     = reg_field_extract(s->acc_result_ctrl, 6,
                                    MAC_RESULT_SB_POS);
        uint32_t init_data_sb  = reg_field_extract(s->acc_ctrl, 5,
                                    ACC_INIT_DATA_SB_POS);
        uint32_t step          = s->acc_step;

        if (data_b_loop_en) {
            cpu_physical_memory_read(s->data_a_addr, data_a,
            (1 << input_data_bits) * (data_depth + 1) * (data_way + 1 + step));
            cpu_physical_memory_read(s->data_b_addr, data_b,
                (1 << input_data_bits) * (data_way + 1));
        } else if (data_a_loop_en) {
            cpu_physical_memory_read(s->data_a_addr, data_a,
                (1 << input_data_bits) * (data_way + 1));
            cpu_physical_memory_read(s->data_b_addr, data_b,
            (1 << input_data_bits) * (data_depth + 1) * (data_way + 1 + step));
        } else if (data_a_loop_en && data_b_loop_en) {
            cpu_physical_memory_read(s->data_b_addr, data_b,
                (1 << input_data_bits) * (data_way + 1));
            cpu_physical_memory_read(s->data_a_addr, data_a,
                (1 << input_data_bits) * (data_way + 1));
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_compute_acc: 0x%x wrong loop set.\n",
                      (int)mode);
        }

        cpu_physical_memory_read(s->init_data_addr, init_data,
                (1 << init_data_bits) * (data_depth + 1));

        index = 19;
        c1 = coeff[index][1];
        c2 = coeff[index][2];
        if (enable_init_data) {
            c1++;
            c2++;
        }
        if (diff_bank(s->data_a_addr, s->data_b_addr)) {
            c1--;
            c2--;
        }
        unit = 16 / (1 << input_data_bits); /* 128bits needs 1 cycle */
        type = MCA_MAC;
        cycles = c1 * (data_depth + 1) +
                 (data_depth + 1) * ceil((data_way + 1) / unit) * c2;
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (type << 8), cycles);

        csky_mca_sim_mac(data_a, data_b, init_data, result, data_depth + 1,
                data_way + 1, input_data_bits, enable_init_data, enable_active,
                data_b_loop_en, data_a_loop_en, init_data_sb,
                init_data_shift_dir, init_data_bits, result_sb,
                result_shift_dir, result_bits, step);

        cpu_physical_memory_write(s->result_addr, result,
                (1 << result_bits) * (data_depth + 1));

        uint32_t input_data_elem_size =
            drv_acc_size_of_data_bits(input_data_bits);
        if (!data_a_loop_en) {
            s->data_a_addr += input_data_elem_size * (data_way + 1 + step)
                                                        * (data_depth + 1);
        }
        if (!data_b_loop_en) {
            s->data_b_addr += input_data_elem_size * (data_way + 1 + step)
                                                        * (data_depth + 1);
        }

        if (enable_init_data) {
            uint32_t init_data_elem_size =
                drv_acc_size_of_data_bits(init_data_bits);
            s->init_data_addr += init_data_elem_size * (data_depth + 1);
        }

        uint32_t result_elem_size = drv_acc_size_of_data_bits(result_bits);
        s->result_addr += result_elem_size * (data_depth + 1);

        } break;

    case DRV_ACC_MODE_SEL_VEC_MUL:
    case DRV_ACC_MODE_SEL_VEC_ADD: {
        static int32_t data_a[1024] = {0};
        static int32_t data_b[1024] = {0};
        static int32_t result[1024] = {0};

        drv_acc_shift_dir_t result_shift_dir = reg_field_extract(s->acc_result_ctrl, 1,
                MAC_RESULT_SD_POS);
        drv_acc_data_bits_t result_bits = reg_field_extract(s->acc_result_ctrl, 2,
                ACC_RESULT_BITS_POS);
        drv_acc_data_bits_t input_data_bits = reg_field_extract(s->acc_ctrl, 2,
                ACC_INPUT_DATA_BITS_POS);

        uint32_t data_depth    = reg_field_extract(s->acc_comp_length, 10,
                                    ACC_DATA_DEPTH_POS);
        uint32_t result_sb     = reg_field_extract(s->acc_result_ctrl, 6,
                                    MAC_RESULT_SB_POS);
        bool enable_vec_scalar = reg_field_extract(s->acc_ctrl, 1,
                                    VEC_SCALAR_EN_POS);
        uint32_t scalar        = s->scalar;
        csky_mca_sim_vec_op_t vec_op =
            mode == DRV_ACC_MODE_SEL_VEC_MUL
            ? THEAD_MCA_SIM_VEC_OP_MUL : THEAD_MCA_SIM_VEC_OP_ADD;

        cpu_physical_memory_read(s->data_a_addr, data_a,
                (1 << input_data_bits) * (data_depth + 1));
        cpu_physical_memory_read(s->data_b_addr, data_b,
                (1 << input_data_bits) * (data_depth + 1));

        index = 0;
        c1 = coeff[index][2];
        if (enable_vec_scalar) {
            c1--;
        }
        if (diff_bank(s->data_a_addr, s->data_b_addr)) {
            c1--;
        }
        unit = 16 / (1 << input_data_bits); /* 128bits needs 1 cycle */
        cycles = ceil((data_depth + 1) / unit) * c1;
        if (mode == DRV_ACC_MODE_SEL_VEC_MUL) {
            type = MCA_VEC_MUL;
        } else {
            type = MCA_VEC_ADD;
        }
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (type << 8), cycles);

        csky_mca_sim_vec(vec_op, data_a, data_b, result, data_depth + 1,
                       input_data_bits, enable_vec_scalar, result_sb,
                       result_shift_dir, result_bits, scalar);

        cpu_physical_memory_write(s->result_addr, result,
                (1 << result_bits) * (data_depth + 1));

        uint32_t input_data_elem_size =
            drv_acc_size_of_data_bits(input_data_bits);
        s->data_a_addr += input_data_elem_size * (data_depth + 1);
        if (!enable_vec_scalar) {
            s->data_b_addr += input_data_elem_size * (data_depth + 1);
        }

        uint32_t result_elem_size = drv_acc_size_of_data_bits(result_bits);
        s->result_addr += result_elem_size * (data_depth + 1);
        } break;
    case DRV_ACC_MODE_SEL_SOFTMAX: {
        static int32_t data_a[1024];
        static int32_t data_b[1024];
        static int32_t result[1024];

        drv_acc_shift_dir_t result_shift_dir = reg_field_extract(
                s->acc_result_ctrl, 1, SOFTMAX_RESULT_SD_POS);
        drv_acc_data_bits_t result_bits = reg_field_extract(s->acc_result_ctrl, 2,
                SOFTMAX_RESULT_BITS_POS);
        uint32_t data_depth    = reg_field_extract(s->acc_comp_length, 10,
                                    ACC_DATA_DEPTH_POS);
        uint32_t result_sb     = reg_field_extract(s->acc_result_ctrl, 5,
                                    SOFTMAX_RESULT_SB_POS);
        cpu_physical_memory_read(s->data_a_addr, data_a,
                4 * (data_depth + 1));
        cpu_physical_memory_read(s->data_b_addr, data_b,
                4 * (data_depth + 1));
        index = 42;
        cycles = (data_depth + 1) * coeff[index][1];
        type = MCA_SOFTMAX;
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (type << 8), cycles);

        csky_mca_sim_softmax(data_a, data_b, result, data_depth + 1,
                           result_sb, result_shift_dir, result_bits);


        s->data_a_addr += sizeof(int32_t) * (data_depth + 1);

        uint32_t result_elem_size = drv_acc_size_of_data_bits(result_bits);

        cpu_physical_memory_write(s->result_addr, result,
                result_elem_size * (data_depth + 1));

        s->result_addr += result_elem_size * (data_depth + 1);
        } break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_compute_acc: 0x%x chose the right mode\n",
                      (int)mode);
        break;
    }
}

static void cskg_mca_irq(csky_mca_state *s, uint32_t raw_state, uint32_t *state,
        uint32_t mask, uint32_t pos)
{
    if (reg_field_extract(mask, 1, pos)
            && reg_field_extract(raw_state, 1, pos)) {
        reg_field_set(state, pos, 1, 1);
        qemu_set_irq(s->irq, 1);
    }
}

static uint64_t csky_mca_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_mca_state *s = (csky_mca_state *)opaque;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mca_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset >> 2) {
    case 0x0: /* acc_data_a_start_reg */
        ret = s->data_a_addr;
        break;
    case 0x1: /* acc_data_b_start_reg */
        ret = s->data_b_addr;
        break;
    case 0x2: /* acc_init_data_addr_reg */
        ret = s->init_data_addr;
        break;
    case 0x3: /* acc_result_data_addr_reg */
        ret = s->result_addr;
        break;
    case 0x4: /* acc_comp_length */
        ret = s->acc_comp_length;
        break;
    case 0x5: /* acc_ctrl_reg */
        ret = s->acc_ctrl;
        break;
    case 0x6: /* acc_result_ctrl */
        ret = s->acc_result_ctrl;
        break;
    case 0x7: /* acc_step */
        ret = s->acc_step;
        break;
    case 0x8: /* scalar */
        ret = s->scalar;
        break;
    case 0x40: /* asrc_en_reg */
        ret = s->asrc_en;
        break;
    case 0x41: /* asrc_start_reg */
        ret = s->asrc_start;
        break;
    case 0x42: /* asrc_mode_reg */
        ret = s->asrc_mode;
        break;
    case 0x43: /* asrc_ctrl_reg */
        ret = s->asrc_ctrl;
        break;
    case 0x44: /* fir_ctrl_reg */
        ret = s->fir_ctrl;
        break;
    case 0x45: /* iir_ctrl_reg */
        ret = s->iir_ctrl;
        break;
    case 0x46: /* asrc_point_clear_reg */
        ret = s->point_clr;
        break;
    case 0x47: /* asrc_sh1d_st_addr_reg */
        ret = s->ch1d_start_addr;
        break;
    case 0x48: /* asrc_sh1d_end_addr_reg */
        ret = s->ch1d_end_addr;
        break;
    case 0x49: /* asrc_sh2d_start_addr_reg */
        ret = s->ch2d_start_addr;
        break;
    case 0x4a: /* asrc_sh2d_end_addr_reg */
        ret = s->ch2d_end_addr;
        break;
    case 0x4b: /* asrc_coeff_addr_reg */
        ret = s->asrc_coef_addr;
        break;
    case 0x4c: /* fir_d_start_addr_reg */
        ret = s->fir_start_addr;
        break;
    case 0x4d: /* fir_d_end_addr_reg */
        ret = s->fir_end_addr;
        break;
    case 0x4e: /* fir_coeff_addr_reg */
        ret = s->fir_coef_addr;
        break;
    case 0x4f: /* iir_d_start_addr_reg */
        ret = s->iir_start_addr;
        break;
    case 0x50: /* iir_d_end_addr_reg */
        ret = s->iir_end_addr;
        break;
    case 0x51: /* iir_coeff_addr_reg */
        ret = s->iir_coef_addr;
        break;
    case 0x52: /* iir_yn1d_reg */
        ret = s->iir_yn1d;
        break;
    case 0x53: /* iir_yn2d_reg */
        ret = s->iir_yn2d;
        break;
    case 0x54: /* result_st_addr_reg */
        ret = s->asrc_result_addr;
        break;
    case 0x55: /* result_size_reg */
        ret = s->result_size;
        break;
    case 0x56: /* asrc_sr_reg */
        ret = s->asrc_sr;
        break;
    case 0x57: /* asrc_im_reg */
        ret = s->asrc_im;
        break;
    case 0x58: /* asrc_is_reg */
        ret = s->asrc_is;
        break;
    case 0x59: /* asrc_ris_reg */
        ret = s->asrc_ris;
        break;
    case 0x5a: /* asrc_ic_reg */
        ret = s->asrc_ic;
        break;
    case 0x5b: /* asrc_dmaen_reg */
        ret = s->asrc_dmaen;
        break;
    case 0x80: /* intr_sta */
        ret = s->intr_sta;
        break;
    case 0x81: /* raw_intr_sta */
        ret = s->raw_intr_sta;
        break;
    case 0x82: /* intr_unmask */
        ret = s->intr_unmask;
        break;
    case 0x83: /* intr_clr */
        ret = s->intr_clr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mca_read: Bad offset %x\n", (int)offset);
    }

    return ret;
}

static void csky_mca_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    csky_mca_state *s = (csky_mca_state *)opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mca_write: 0x%x must word align read\n",
                      (int)offset);
    }
    switch (offset >> 2) {
    case 0x0: /* acc_data_a_start_reg */
        if (!reg_field_extract(s->acc_ctrl, 1, ACC_COMP_EN_POS)) {
            s->data_a_addr = value;
        } else {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ACC_ACCESS_ES_POS);
        }
        break;
    case 0x1: /* acc_data_b_start_reg */
        if (!reg_field_extract(s->acc_ctrl, 1, ACC_COMP_EN_POS)) {
            s->data_b_addr = value;
        } else {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ACC_ACCESS_ES_POS);
        }
        break;
    case 0x2: /* acc_init_data_addr_reg */
        if (!reg_field_extract(s->acc_ctrl, 1, ACC_COMP_EN_POS)) {
            s->init_data_addr = value;
        } else {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ACC_ACCESS_ES_POS);
        }
        break;
    case 0x3: /* acc_result_data_addr_reg */
        if (!reg_field_extract(s->acc_ctrl, 1, ACC_COMP_EN_POS)) {
            s->result_addr = value;
        } else {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ACC_ACCESS_ES_POS);
        }
        break;
    case 0x4: /* acc_comp_length */
        if (!reg_field_extract(s->acc_ctrl, 1, ACC_COMP_EN_POS)) {
            s->acc_comp_length = value;
        } else {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ACC_ACCESS_ES_POS);
        }
        break;
    case 0x5: /* acc_ctrl_reg */
        if (!reg_field_extract(s->acc_ctrl, 1, ACC_COMP_EN_POS)) {
            s->acc_ctrl = value;
            if (reg_field_extract(s->acc_ctrl, 1, ACC_COMP_EN_POS)) {
                csky_compute_acc(s);
                reg_field_set(&s->acc_ctrl, ACC_COMP_DONE_POS, 1, 1);
                reg_field_set(&s->acc_ctrl, ACC_COMP_EN_POS, 0, 1);
            }
        } else {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ACC_ACCESS_ES_POS);
        }
        break;
    case 0x6: /* acc_result_ctrl */
        if (!reg_field_extract(s->acc_ctrl, 1, ACC_COMP_EN_POS)) {
            s->acc_result_ctrl = value;
        } else {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ACC_ACCESS_ES_POS);
        }
        break;
    case 0x7: /* acc_step */
        s->acc_step = value;
        break;
    case 0x8: /* scalar */
        s->scalar = value;
        break;
    case 0x40: /* asrc_en_reg */
        s->asrc_en = value;
        break;
    case 0x41: /* asrc_start_reg */
        if (reg_field_extract(s->asrc_en, 1, ASRC_COMP_EN_POS)) {
            s->asrc_start = value;
            if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)) {
                csky_compute_asrc(s);
                reg_field_set(&s->asrc_start, ASRC_COMP_START_POS, 0, 1);
                //reg_field_set(&s->asrc_en, ASRC_COMP_EN_POS, 0, 1);
            }
        }
        break;
    case 0x42: /* asrc_mode_reg */
        if (!reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)) {
            s->asrc_mode = value;
        } else {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        }
        break;
    case 0x43: /* asrc_ctrl_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) << 1)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->asrc_ctrl = value;
        }
        break;
    case 0x44: /* fir_ctrl_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 2)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->fir_ctrl = value;
        }
        break;
    case 0x45: /* iir_ctrl_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 3)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->iir_ctrl = value;
        }
        break;
    case 0x46: /* asrc_point_clear_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) << 1)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->point_clr = value;
            if (s->point_clr == 1) {
                g_drv_sim_asrc_pointer = 0x00000000;
            }
        }
        break;
    case 0x47: /* asrc_sh1d_st_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) << 1)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->ch1d_start_addr = value;
        }
        break;
    case 0x48: /* asrc_sh1d_end_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) << 1)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->ch1d_end_addr = value;
        }
        break;
    case 0x49: /* asrc_sh2d_start_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) << 1)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->ch2d_start_addr = value;
        }
        break;
    case 0x4a: /* asrc_sh2d_end_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) << 1)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->ch2d_end_addr = value;
        }
        break;
    case 0x4b: /* asrc_coeff_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) << 1)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->asrc_coef_addr = value;
        }
        break;
    case 0x4c: /* fir_d_start_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 2)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->fir_start_addr = value;
        }
        break;
    case 0x4d: /* fir_d_end_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 2)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->fir_end_addr = value;
        }
        break;
    case 0x4e: /* fir_coeff_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 2)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->fir_coef_addr = value;
        }
        break;
    case 0x4f: /* iir_d_start_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 3)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->iir_start_addr = value;
        }
        break;
    case 0x50: /* iir_d_end_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 3)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->iir_end_addr = value;
        }
        break;
    case 0x51: /* iir_coeff_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 3)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->iir_coef_addr = value;
        }
        break;
    case 0x52: /* iir_yn1d_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 3)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->iir_yn1d = value;
        }
        break;
    case 0x53: /* iir_yn2d_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)
            && (reg_field_extract(s->asrc_mode, 2, ASRC_COMP_MODE_POS) == 3)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->iir_yn2d = value;
        }
        break;
    case 0x54: /* result_st_addr_reg */
        if (reg_field_extract(s->asrc_start, 1, ASRC_COMP_START_POS)) {
            cskg_mca_irq(s, s->raw_intr_sta, &s->intr_sta, s->intr_unmask,
                    ASRC_ACCESS_ES_POS);
        } else {
            s->asrc_result_addr = value;
        }
        break;
    case 0x55: /* result_size_reg */
        s->result_size = value;
        break;
    case 0x56: /* asrc_sr_reg */
        s->asrc_sr = value;
        break;
    case 0x57: /* asrc_im_reg */
        s->asrc_im = value;
        break;
    case 0x58: /* asrc_is_reg */
        s->asrc_is = value;
        break;
    case 0x59: /* asrc_ris_reg */
        s->asrc_ris = value;
        break;
    case 0x5a: /* asrc_ic_reg */
        s->asrc_ic = value; /* TODO: clean the intrrupt state. */
        break;
    case 0x5b: /* asrc_dmaen_reg */
        s->asrc_dmaen  = value;
        break;
    case 0x80: /* intr_sta */
        s->intr_sta = value;
        break;
    case 0x81: /* raw_intr_sta */
        s->raw_intr_sta = value;
        break;
    case 0x82: /* intr_unmask */
        s->intr_unmask = value;
        break;
    case 0x83: /* intr_clr */
        s->intr_clr = value;    /* TODO: clean the intrrupt state. */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mca_write: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps csky_mca_ops = {
    .read = csky_mca_read,
    .write = csky_mca_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_csky_mca = {
    .name = TYPE_THEAD_MCA,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(data_a_addr, csky_mca_state),
        VMSTATE_UINT32(data_b_addr, csky_mca_state),
        VMSTATE_UINT32(init_data_addr, csky_mca_state),
        VMSTATE_UINT32(result_addr, csky_mca_state),
        VMSTATE_UINT32(acc_comp_length, csky_mca_state),
        VMSTATE_UINT32(acc_ctrl, csky_mca_state),
        VMSTATE_UINT32(acc_result_ctrl, csky_mca_state),
        VMSTATE_UINT32(acc_step, csky_mca_state),
        VMSTATE_UINT32(scalar, csky_mca_state),
        VMSTATE_UINT32(asrc_en, csky_mca_state),
        VMSTATE_UINT32(asrc_start, csky_mca_state),
        VMSTATE_UINT32(asrc_mode, csky_mca_state),
        VMSTATE_UINT32(asrc_ctrl, csky_mca_state),
        VMSTATE_UINT32(fir_ctrl, csky_mca_state),
        VMSTATE_UINT32(iir_ctrl, csky_mca_state),
        VMSTATE_UINT32(point_clr, csky_mca_state),
        VMSTATE_UINT32(ch1d_start_addr, csky_mca_state),
        VMSTATE_UINT32(ch2d_start_addr, csky_mca_state),
        VMSTATE_UINT32(ch1d_end_addr, csky_mca_state),
        VMSTATE_UINT32(ch2d_end_addr, csky_mca_state),
        VMSTATE_UINT32(asrc_coef_addr, csky_mca_state),
        VMSTATE_UINT32(fir_start_addr, csky_mca_state),
        VMSTATE_UINT32(fir_end_addr, csky_mca_state),
        VMSTATE_UINT32(fir_coef_addr, csky_mca_state),
        VMSTATE_UINT32(iir_start_addr, csky_mca_state),
        VMSTATE_UINT32(iir_end_addr, csky_mca_state),
        VMSTATE_UINT32(iir_coef_addr, csky_mca_state),
        VMSTATE_UINT32(iir_yn1d, csky_mca_state),
        VMSTATE_UINT32(iir_yn2d, csky_mca_state),
        VMSTATE_UINT32(asrc_result_addr, csky_mca_state),
        VMSTATE_UINT32(result_size, csky_mca_state),
        VMSTATE_UINT32(asrc_sr, csky_mca_state),
        VMSTATE_UINT32(asrc_im, csky_mca_state),
        VMSTATE_UINT32(asrc_is, csky_mca_state),
        VMSTATE_UINT32(asrc_ris, csky_mca_state),
        VMSTATE_UINT32(asrc_ic, csky_mca_state),
        VMSTATE_UINT32(asrc_dmaen, csky_mca_state),
        VMSTATE_UINT32(intr_sta, csky_mca_state),
        VMSTATE_UINT32(raw_intr_sta, csky_mca_state),
        VMSTATE_UINT32(intr_unmask, csky_mca_state),
        VMSTATE_UINT32(intr_clr, csky_mca_state),
        VMSTATE_END_OF_LIST()
    }
};

static Property csky_mca_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void csky_mca_init(Object *obj)
{
    csky_mca_state *s = THEAD_MCA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_mca_ops, s,
                          TYPE_THEAD_MCA, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->data_a_addr      = 0x00000000;
    s->data_b_addr      = 0x00000000;
    s->init_data_addr   = 0x00000000;
    s->result_addr      = 0x00000000;
    s->acc_comp_length  = 0x00000000;
    s->acc_ctrl         = 0x00000000;
    s->acc_result_ctrl  = 0x00000000;
    s->acc_step         = 0x00000000;
    s->scalar           = 0x00000000;
    s->asrc_en          = 0x00000000;
    s->asrc_ctrl        = 0x000027b0;
    s->fir_ctrl         = 0x000c00f0;
    s->iir_ctrl         = 0x000000c0;
    s->point_clr        = 0x00000000;
    s->ch1d_start_addr  = 0x00000000;
    s->ch2d_start_addr  = 0x00000000;
    s->ch1d_end_addr    = 0x00000000;
    s->ch2d_end_addr    = 0x00000000;
    s->asrc_coef_addr   = 0x00000000;
    s->fir_start_addr   = 0x00000000;
    s->fir_end_addr     = 0x00000000;
    s->fir_coef_addr    = 0x00000000;
    s->iir_start_addr   = 0x00000000;
    s->iir_end_addr     = 0x00000000;
    s->iir_coef_addr    = 0x00000000;
    s->iir_yn1d         = 0x00000000;
    s->iir_yn2d         = 0x00000000;
    s->asrc_result_addr = 0x00000000;
    s->result_size      = 0x00000000;
    s->asrc_sr          = 0x00000000;
    s->asrc_im          = 0x00000011;
    s->asrc_is          = 0x00000000;
    s->asrc_ris         = 0x00000000;
    s->asrc_ic          = 0x00000000;
    s->asrc_dmaen       = 0x00000000;
    s->intr_sta         = 0x00000000;
    s->raw_intr_sta     = 0x00000000;
    s->intr_unmask      = 0x00000007;
    s->intr_clr         = 0x00000000;
}

static void csky_mca_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);
    dc->vmsd = &vmstate_csky_mca;
    dc->props_ = csky_mca_properties;
    dc->desc = "cskysim type: MCA";
    dc->user_creatable = true;
}

static const TypeInfo csky_mca_info = {
    .name          = TYPE_THEAD_MCA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_mca_state),
    .instance_init = csky_mca_init,
    .class_init    = csky_mca_class_init,
};


static void csky_mca_register_types(void)
{
    type_register_static(&csky_mca_info);
}

type_init(csky_mca_register_types)

