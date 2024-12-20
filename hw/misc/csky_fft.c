/*
 * CSKY FFT emulation.
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
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "exec/tracestub.h"
#include "migration/vmstate.h"
#include "exec/memory.h"
#include "hw/irq.h"

#define TYPE_THEAD_FFT  "csky_fft"
#define THEAD_FFT(obj)  OBJECT_CHECK(struct csky_fft_state, (obj), TYPE_THEAD_FFT)

struct csky_fft_state {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t start;              /* FFT start register */
    uint32_t in_addr;            /* where store the input data */
    uint32_t out_addr;           /* where store the output data */
    uint32_t mode;               /* FFT mode select register */
    uint32_t in_num;             /* FFT input number register */
    uint32_t intr;               /* FFT interrupt & error flag register */
    uint32_t mask;               /* FFT interrupt & error mask register */
};

/* fft types */
enum {
    RFFT  = 0x1,
    CFFT  = 0x2,
    IRFFT = 0x4,
    ICFFT = 0x8,
    PSD   = 0x10,
};

/* fft hardware configure, may be move to Property */
#define ENABLE_ROUNDING 0
#define TWIDDLE_COEFF_Q 30
#define DEFAULT_INPUT_NUM 512
/* interpret reg bits */
#define MODE_RESERVED_MASK 0xFFFFC0E0
#define MODE_FUNC_SEL 0x1F
#define MODE_POINTS_SEL_SHIFT 8
#define MODE_POINTS_SEL_MASK 0x3F
#define IN_NUM_RESERVED_MASK 0xFFFFFC00
#define INTR_FLAG_MASK 0xF
#define INTR_FLAG_RESERVED_MASK 0xFFFFFFF0
#define INTR_MASK_RESERVED_MASK 0xFFFFFFF0

#define MEM_OUT_ADDR_ERROR_POS 3
#define MEM_OUT_ADDR_ERROR_MASK 0x1
#define MEM_IN_ADDR_ERROR_POS 2
#define MEM_IN_ADDR_ERROR_MASK 0x1
#define ERROR_POS 1
#define ERROR_MASK 0x1
#define INTERRUPT_POS 0
#define INTERRUPT_MASK 0x1

/*
 * Twiddle coefficients for 512-point FFT. 256, 128, 64, 32, 16-point FFT can
 * reuse this table with step 2, 4, 8, 16, 32.
 */
static const uint32_t twiddle_coeff_512[] = {
    /* W512[000] and W512[128] */0x40000000, 0x00000000, 0x00000000, 0xc0000000,
    /* W512[001] and W512[129] */0x3ffec42d, 0xff36f170, 0xff36f170, 0xc0013bd3,
    /* W512[002] and W512[130] */0x3ffb10c1, 0xfe6deaa1, 0xfe6deaa1, 0xc004ef3f,
    /* W512[003] and W512[131] */0x3ff4e5e0, 0xfda4f351, 0xfda4f351, 0xc00b1a20,
    /* W512[004] and W512[132] */0x3fec43c7, 0xfcdc1342, 0xfcdc1342, 0xc013bc39,
    /* W512[005] and W512[133] */0x3fe12acb, 0xfc135231, 0xfc135231, 0xc01ed535,
    /* W512[006] and W512[134] */0x3fd39b5a, 0xfb4ab7db, 0xfb4ab7db, 0xc02c64a6,
    /* W512[007] and W512[135] */0x3fc395f9, 0xfa824bfd, 0xfa824bfd, 0xc03c6a07,
    /* W512[008] and W512[136] */0x3fb11b48, 0xf9ba1651, 0xf9ba1651, 0xc04ee4b8,
    /* W512[009] and W512[137] */0x3f9c2bfb, 0xf8f21e8e, 0xf8f21e8e, 0xc063d405,
    /* W512[010] and W512[138] */0x3f84c8e2, 0xf82a6c6a, 0xf82a6c6a, 0xc07b371e,
    /* W512[011] and W512[139] */0x3f6af2e3, 0xf7630799, 0xf7630799, 0xc0950d1d,
    /* W512[012] and W512[140] */0x3f4eaafe, 0xf69bf7c9, 0xf69bf7c9, 0xc0b15502,
    /* W512[013] and W512[141] */0x3f2ff24a, 0xf5d544a7, 0xf5d544a7, 0xc0d00db6,
    /* W512[014] and W512[142] */0x3f0ec9f5, 0xf50ef5de, 0xf50ef5de, 0xc0f1360b,
    /* W512[015] and W512[143] */0x3eeb3347, 0xf4491311, 0xf4491311, 0xc114ccb9,
    /* W512[016] and W512[144] */0x3ec52fa0, 0xf383a3e2, 0xf383a3e2, 0xc13ad060,
    /* W512[017] and W512[145] */0x3e9cc076, 0xf2beafed, 0xf2beafed, 0xc1633f8a,
    /* W512[018] and W512[146] */0x3e71e759, 0xf1fa3ecb, 0xf1fa3ecb, 0xc18e18a7,
    /* W512[019] and W512[147] */0x3e44a5ef, 0xf136580d, 0xf136580d, 0xc1bb5a11,
    /* W512[020] and W512[148] */0x3e14fdf7, 0xf0730342, 0xf0730342, 0xc1eb0209,
    /* W512[021] and W512[149] */0x3de2f148, 0xefb047f2, 0xefb047f2, 0xc21d0eb8,
    /* W512[022] and W512[150] */0x3dae81cf, 0xeeee2d9d, 0xeeee2d9d, 0xc2517e31,
    /* W512[023] and W512[151] */0x3d77b192, 0xee2cbbc1, 0xee2cbbc1, 0xc2884e6e,
    /* W512[024] and W512[152] */0x3d3e82ae, 0xed6bf9d1, 0xed6bf9d1, 0xc2c17d52,
    /* W512[025] and W512[153] */0x3d02f757, 0xecabef3d, 0xecabef3d, 0xc2fd08a9,
    /* W512[026] and W512[154] */0x3cc511d9, 0xebeca36c, 0xebeca36c, 0xc33aee27,
    /* W512[027] and W512[155] */0x3c84d496, 0xeb2e1dbe, 0xeb2e1dbe, 0xc37b2b6a,
    /* W512[028] and W512[156] */0x3c42420a, 0xea70658a, 0xea70658a, 0xc3bdbdf6,
    /* W512[029] and W512[157] */0x3bfd5cc4, 0xe9b38223, 0xe9b38223, 0xc402a33c,
    /* W512[030] and W512[158] */0x3bb6276e, 0xe8f77acf, 0xe8f77acf, 0xc449d892,
    /* W512[031] and W512[159] */0x3b6ca4c4, 0xe83c56cf, 0xe83c56cf, 0xc4935b3c,
    /* W512[032] and W512[160] */0x3b20d79e, 0xe7821d59, 0xe7821d59, 0xc4df2862,
    /* W512[033] and W512[161] */0x3ad2c2e8, 0xe6c8d59c, 0xe6c8d59c, 0xc52d3d18,
    /* W512[034] and W512[162] */0x3a8269a3, 0xe61086bc, 0xe61086bc, 0xc57d965d,
    /* W512[035] and W512[163] */0x3a2fcee8, 0xe55937d5, 0xe55937d5, 0xc5d03118,
    /* W512[036] and W512[164] */0x39daf5e8, 0xe4a2eff6, 0xe4a2eff6, 0xc6250a18,
    /* W512[037] and W512[165] */0x3983e1e8, 0xe3edb628, 0xe3edb628, 0xc67c1e18,
    /* W512[038] and W512[166] */0x392a9642, 0xe3399167, 0xe3399167, 0xc6d569be,
    /* W512[039] and W512[167] */0x38cf1669, 0xe28688a4, 0xe28688a4, 0xc730e997,
    /* W512[040] and W512[168] */0x387165e3, 0xe1d4a2c8, 0xe1d4a2c8, 0xc78e9a1d,
    /* W512[041] and W512[169] */0x3811884d, 0xe123e6ad, 0xe123e6ad, 0xc7ee77b3,
    /* W512[042] and W512[170] */0x37af8159, 0xe0745b24, 0xe0745b24, 0xc8507ea7,
    /* W512[043] and W512[171] */0x374b54ce, 0xdfc606f1, 0xdfc606f1, 0xc8b4ab32,
    /* W512[044] and W512[172] */0x36e5068a, 0xdf18f0ce, 0xdf18f0ce, 0xc91af976,
    /* W512[045] and W512[173] */0x367c9a7e, 0xde6d1f65, 0xde6d1f65, 0xc9836582,
    /* W512[046] and W512[174] */0x361214b0, 0xddc29958, 0xddc29958, 0xc9edeb50,
    /* W512[047] and W512[175] */0x35a5793c, 0xdd196538, 0xdd196538, 0xca5a86c4,
    /* W512[048] and W512[176] */0x3536cc52, 0xdc71898d, 0xdc71898d, 0xcac933ae,
    /* W512[049] and W512[177] */0x34c61236, 0xdbcb0cce, 0xdbcb0cce, 0xcb39edca,
    /* W512[050] and W512[178] */0x34534f41, 0xdb25f566, 0xdb25f566, 0xcbacb0bf,
    /* W512[051] and W512[179] */0x33de87de, 0xda8249b4, 0xda8249b4, 0xcc217822,
    /* W512[052] and W512[180] */0x3367c090, 0xd9e01006, 0xd9e01006, 0xcc983f70,
    /* W512[053] and W512[181] */0x32eefdea, 0xd93f4e9e, 0xd93f4e9e, 0xcd110216,
    /* W512[054] and W512[182] */0x32744493, 0xd8a00bae, 0xd8a00bae, 0xcd8bbb6d,
    /* W512[055] and W512[183] */0x31f79948, 0xd8024d59, 0xd8024d59, 0xce0866b8,
    /* W512[056] and W512[184] */0x317900d6, 0xd76619b6, 0xd76619b6, 0xce86ff2a,
    /* W512[057] and W512[185] */0x30f8801f, 0xd6cb76c9, 0xd6cb76c9, 0xcf077fe1,
    /* W512[058] and W512[186] */0x30761c18, 0xd6326a88, 0xd6326a88, 0xcf89e3e8,
    /* W512[059] and W512[187] */0x2ff1d9c7, 0xd59afadb, 0xd59afadb, 0xd00e2639,
    /* W512[060] and W512[188] */0x2f6bbe45, 0xd5052d97, 0xd5052d97, 0xd09441bb,
    /* W512[061] and W512[189] */0x2ee3cebe, 0xd4710883, 0xd4710883, 0xd11c3142,
    /* W512[062] and W512[190] */0x2e5a1070, 0xd3de9156, 0xd3de9156, 0xd1a5ef90,
    /* W512[063] and W512[191] */0x2dce88aa, 0xd34dcdb4, 0xd34dcdb4, 0xd2317756,
    /* W512[064] and W512[192] */0x2d413ccd, 0xd2bec333, 0xd2bec333, 0xd2bec333,
    /* W512[065] and W512[193] */0x2cb2324c, 0xd2317756, 0xd2317756, 0xd34dcdb4,
    /* W512[066] and W512[194] */0x2c216eaa, 0xd1a5ef90, 0xd1a5ef90, 0xd3de9156,
    /* W512[067] and W512[195] */0x2b8ef77d, 0xd11c3142, 0xd11c3142, 0xd4710883,
    /* W512[068] and W512[196] */0x2afad269, 0xd09441bb, 0xd09441bb, 0xd5052d97,
    /* W512[069] and W512[197] */0x2a650525, 0xd00e2639, 0xd00e2639, 0xd59afadb,
    /* W512[070] and W512[198] */0x29cd9578, 0xcf89e3e8, 0xcf89e3e8, 0xd6326a88,
    /* W512[071] and W512[199] */0x29348937, 0xcf077fe1, 0xcf077fe1, 0xd6cb76c9,
    /* W512[072] and W512[200] */0x2899e64a, 0xce86ff2a, 0xce86ff2a, 0xd76619b6,
    /* W512[073] and W512[201] */0x27fdb2a7, 0xce0866b8, 0xce0866b8, 0xd8024d59,
    /* W512[074] and W512[202] */0x275ff452, 0xcd8bbb6d, 0xcd8bbb6d, 0xd8a00bae,
    /* W512[075] and W512[203] */0x26c0b162, 0xcd110216, 0xcd110216, 0xd93f4e9e,
    /* W512[076] and W512[204] */0x261feffa, 0xcc983f70, 0xcc983f70, 0xd9e01006,
    /* W512[077] and W512[205] */0x257db64c, 0xcc217822, 0xcc217822, 0xda8249b4,
    /* W512[078] and W512[206] */0x24da0a9a, 0xcbacb0bf, 0xcbacb0bf, 0xdb25f566,
    /* W512[079] and W512[207] */0x2434f332, 0xcb39edca, 0xcb39edca, 0xdbcb0cce,
    /* W512[080] and W512[208] */0x238e7673, 0xcac933ae, 0xcac933ae, 0xdc71898d,
    /* W512[081] and W512[209] */0x22e69ac8, 0xca5a86c4, 0xca5a86c4, 0xdd196538,
    /* W512[082] and W512[210] */0x223d66a8, 0xc9edeb50, 0xc9edeb50, 0xddc29958,
    /* W512[083] and W512[211] */0x2192e09b, 0xc9836582, 0xc9836582, 0xde6d1f65,
    /* W512[084] and W512[212] */0x20e70f32, 0xc91af976, 0xc91af976, 0xdf18f0ce,
    /* W512[085] and W512[213] */0x2039f90f, 0xc8b4ab32, 0xc8b4ab32, 0xdfc606f1,
    /* W512[086] and W512[214] */0x1f8ba4dc, 0xc8507ea7, 0xc8507ea7, 0xe0745b24,
    /* W512[087] and W512[215] */0x1edc1953, 0xc7ee77b3, 0xc7ee77b3, 0xe123e6ad,
    /* W512[088] and W512[216] */0x1e2b5d38, 0xc78e9a1d, 0xc78e9a1d, 0xe1d4a2c8,
    /* W512[089] and W512[217] */0x1d79775c, 0xc730e997, 0xc730e997, 0xe28688a4,
    /* W512[090] and W512[218] */0x1cc66e99, 0xc6d569be, 0xc6d569be, 0xe3399167,
    /* W512[091] and W512[219] */0x1c1249d8, 0xc67c1e18, 0xc67c1e18, 0xe3edb628,
    /* W512[092] and W512[220] */0x1b5d100a, 0xc6250a18, 0xc6250a18, 0xe4a2eff6,
    /* W512[093] and W512[221] */0x1aa6c82b, 0xc5d03118, 0xc5d03118, 0xe55937d5,
    /* W512[094] and W512[222] */0x19ef7944, 0xc57d965d, 0xc57d965d, 0xe61086bc,
    /* W512[095] and W512[223] */0x19372a64, 0xc52d3d18, 0xc52d3d18, 0xe6c8d59c,
    /* W512[096] and W512[224] */0x187de2a7, 0xc4df2862, 0xc4df2862, 0xe7821d59,
    /* W512[097] and W512[225] */0x17c3a931, 0xc4935b3c, 0xc4935b3c, 0xe83c56cf,
    /* W512[098] and W512[226] */0x17088531, 0xc449d892, 0xc449d892, 0xe8f77acf,
    /* W512[099] and W512[227] */0x164c7ddd, 0xc402a33c, 0xc402a33c, 0xe9b38223,
    /* W512[100] and W512[228] */0x158f9a76, 0xc3bdbdf6, 0xc3bdbdf6, 0xea70658a,
    /* W512[101] and W512[229] */0x14d1e242, 0xc37b2b6a, 0xc37b2b6a, 0xeb2e1dbe,
    /* W512[102] and W512[230] */0x14135c94, 0xc33aee27, 0xc33aee27, 0xebeca36c,
    /* W512[103] and W512[231] */0x135410c3, 0xc2fd08a9, 0xc2fd08a9, 0xecabef3d,
    /* W512[104] and W512[232] */0x1294062f, 0xc2c17d52, 0xc2c17d52, 0xed6bf9d1,
    /* W512[105] and W512[233] */0x11d3443f, 0xc2884e6e, 0xc2884e6e, 0xee2cbbc1,
    /* W512[106] and W512[234] */0x1111d263, 0xc2517e31, 0xc2517e31, 0xeeee2d9d,
    /* W512[107] and W512[235] */0x104fb80e, 0xc21d0eb8, 0xc21d0eb8, 0xefb047f2,
    /* W512[108] and W512[236] */0x0f8cfcbe, 0xc1eb0209, 0xc1eb0209, 0xf0730342,
    /* W512[109] and W512[237] */0x0ec9a7f3, 0xc1bb5a11, 0xc1bb5a11, 0xf136580d,
    /* W512[110] and W512[238] */0x0e05c135, 0xc18e18a7, 0xc18e18a7, 0xf1fa3ecb,
    /* W512[111] and W512[239] */0x0d415013, 0xc1633f8a, 0xc1633f8a, 0xf2beafed,
    /* W512[112] and W512[240] */0x0c7c5c1e, 0xc13ad060, 0xc13ad060, 0xf383a3e2,
    /* W512[113] and W512[241] */0x0bb6ecef, 0xc114ccb9, 0xc114ccb9, 0xf4491311,
    /* W512[114] and W512[242] */0x0af10a22, 0xc0f1360b, 0xc0f1360b, 0xf50ef5de,
    /* W512[115] and W512[243] */0x0a2abb59, 0xc0d00db6, 0xc0d00db6, 0xf5d544a7,
    /* W512[116] and W512[244] */0x09640837, 0xc0b15502, 0xc0b15502, 0xf69bf7c9,
    /* W512[117] and W512[245] */0x089cf867, 0xc0950d1d, 0xc0950d1d, 0xf7630799,
    /* W512[118] and W512[246] */0x07d59396, 0xc07b371e, 0xc07b371e, 0xf82a6c6a,
    /* W512[119] and W512[247] */0x070de172, 0xc063d405, 0xc063d405, 0xf8f21e8e,
    /* W512[120] and W512[248] */0x0645e9af, 0xc04ee4b8, 0xc04ee4b8, 0xf9ba1651,
    /* W512[121] and W512[249] */0x057db403, 0xc03c6a07, 0xc03c6a07, 0xfa824bfd,
    /* W512[122] and W512[250] */0x04b54825, 0xc02c64a6, 0xc02c64a6, 0xfb4ab7db,
    /* W512[123] and W512[251] */0x03ecadcf, 0xc01ed535, 0xc01ed535, 0xfc135231,
    /* W512[124] and W512[252] */0x0323ecbe, 0xc013bc39, 0xc013bc39, 0xfcdc1342,
    /* W512[125] and W512[253] */0x025b0caf, 0xc00b1a20, 0xc00b1a20, 0xfda4f351,
    /* W512[126] and W512[254] */0x0192155f, 0xc004ef3f, 0xc004ef3f, 0xfe6deaa1,
    /* W512[127] and W512[255] */0x00c90e90, 0xc0013bd3, 0xc0013bd3, 0xff36f170,
};

/* Number of butterfly stages of 512-point FFT. */
static const uint32_t fft_order_512 = 9;

/* Complex number in format of 32-bit integer. */
typedef struct {
    int32_t re;
    int32_t im;
} ci32_t;

static ci32_t g_fft_temp_buffer[512];

static int32_t i64_round_to_i32(int64_t x, uint8_t shift_bits) {
#if ENABLE_ROUNDING
    bool is_neg = x < 0;
    uint64_t y = is_neg ? -x : x;
    y += ((uint64_t)1 << (shift_bits - 1));
    y >>= shift_bits;
    x = y;
    return (int32_t)(is_neg ? -x : x);
#else
    return (int32_t)(x >> shift_bits);
#endif
}

static void ci32_add(const ci32_t *a, const ci32_t *b, ci32_t *c) {
    c->re = i64_round_to_i32((int64_t)a->re + b->re, 1);
    c->im = i64_round_to_i32((int64_t)a->im + b->im, 1);
}

static void ci32_sub(const ci32_t *a, const ci32_t *b, ci32_t *c) {
    c->re = i64_round_to_i32((int64_t)a->re - b->re, 1);
    c->im = i64_round_to_i32((int64_t)a->im - b->im, 1);
}

static void ci32_mul(const ci32_t *a, const ci32_t *b, ci32_t *c) {
    c->re = i64_round_to_i32((int64_t)a->re * b->re, TWIDDLE_COEFF_Q) -
            i64_round_to_i32((int64_t)a->im * b->im, TWIDDLE_COEFF_Q);
    c->im = i64_round_to_i32((int64_t)a->re * b->im, TWIDDLE_COEFF_Q) +
            i64_round_to_i32((int64_t)a->im * b->re, TWIDDLE_COEFF_Q);
}

static void butterfly(ci32_t *a, ci32_t *b, const ci32_t *twiddle_coeff,
                      bool iflag) {
    ci32_t wn = *twiddle_coeff;

    if (iflag) {
        wn.im = -wn.im;
    }

    /*
     * a' = (a + b)
     * b' = (a - b) * wn
     */
    ci32_t temp = *a;
    ci32_add(a, b, a);
    ci32_sub(&temp, b, &temp);
    ci32_mul(&temp, &wn, b);
}

static void bit_reversal(ci32_t *x, uint32_t fft_len) {
    for (uint32_t m = 1, j = fft_len >> 1; m + 1 < fft_len; ++m) {
        if (m < j) {
            ci32_t temp = x[m];
            x[m] = x[j];
            x[j] = temp;
        }
        uint32_t k = fft_len >> 1;
        while (j >= k) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
}

/**
 * Complex FFT - optimized for hardware.
 *
 * Take 512-point FFT as example:
 *   Input is divided into 4 blocks:
 *     Block 0: 000 - 127
 *     Block 1: 128 - 255
 *     Block 2: 256 - 383
 *     Block 3: 384 - 511
 *
 *   Stage 0:
 *     x000 - x256 W000 | x128 - x384 | W128
 *     x001 - x257 W001 | x129 - x385 | W129
 *     ...
 *     x126 - x382 W126 | x254 - x510 | W254
 *     x127 - x383 W127 | x255 - x511 | W255
 *   Stage 1:
 *     x000 - x128 | x256 - x384 | W000
 *     x001 - x129 | x257 - x385 | W002
 *     ...
 *     x126 - x254 | x383 - x510 | W252
 *     x127 - x255 | x383 - x511 | W254
 *   Stage 2:
 *     x000 - x064 | x128 - x192 | x256 - 320 | x384 - x448 | W000
 *     x001 - x065 | x129 - x193 | x257 - 321 | x385 - x449 | W004
 *     ...
 *     x062 - x126 | x190 - x254 | x318 - 320 | x446 - x510 | W248
 *     x063 - x127 | x191 - x255 | x319 - 321 | x447 - x511 | W252
 *   Stage 3...6:
 *     ...
 *   Stage 7:
 *     x000 - x002 | x004 - x006 | ... | x504 - x506 | x508 - x510 | W000
 *     x001 - x003 | x005 - x007 | ... | x505 - x507 | x509 - x511 | W128
 *   Stage 8:
 *     x000 - x001 | x002 - x003 | ... | x508 - x509 | x510 - x511 | W000
 */
static void cfft(ci32_t *x, const uint32_t order, bool iflag)
{
    uint32_t fft_len = (1U << order);
    const ci32_t *twiddle_coeff;
    uint32_t step;

    for (uint32_t m = 0; m < order; ++m) {
        uint32_t group_num = 1 << m;
        uint32_t group_interval = fft_len >> m;
        uint32_t unit_interval = group_interval >> 1;
        uint32_t unit_inverval_half = unit_interval >> 1;

        if (m == 0) {  /*  The first stage */
            twiddle_coeff = (const ci32_t *)twiddle_coeff_512;
            step = 1U << (fft_order_512 - order + 1);

            ci32_t *xa, *xb, *ya, *yb;
            xa = x;
            xb = xa + unit_interval;
            ya = x + unit_interval / 2;
            yb = ya + unit_interval;

            for (uint32_t r = 0; r < unit_inverval_half; ++r) {
                /*  Parallel butterfly operations */
                butterfly(xa, xb, twiddle_coeff + 0, iflag);
                butterfly(ya, yb, twiddle_coeff + 1, iflag);

                /*  Next 4 input samples from 4 blocks */
                ++xa, ++xb, ++ya, ++yb;

                /*  Next 128-bit twiddle coefficients */
                twiddle_coeff += step;
            }
        } else {  /*  Other stages, pipelined */
            step = 1U << (fft_order_512 - order + m + 1);

            ci32_t *xa, *xb;
            for (uint32_t r = 0; r < unit_interval; ++r) {
                uint32_t index = r * step;
                index = (index & ((1U << (fft_order_512 - 1)) - 1)) +
                  (index >> (fft_order_512 - 1));
                twiddle_coeff = (const ci32_t *)twiddle_coeff_512 + index;

                xa = x + r;
                xb = xa + unit_interval;

                for (uint32_t group = 0; group < group_num; ++group) {

                    butterfly(xa, xb, twiddle_coeff, iflag);
                    xa += group_interval;
                    xb += group_interval;
                }
            }
        }
    }

    bit_reversal(x, fft_len);
}

static void csky_fft_real(const int32_t *in_addr, int32_t *out_addr,
    uint32_t order, uint32_t in_num)
{
    uint32_t fft_len = (uint32_t)1 << order;
    assert(in_addr != NULL && out_addr != NULL);
    assert(in_num > 0 && in_num <= fft_len);

    ci32_t *temp = g_fft_temp_buffer;

    for (uint32_t i = 0; i < in_num; ++i) {
        temp[i].re = in_addr[i];
        temp[i].im = 0;
    }

    for (uint32_t i = in_num; i < fft_len; ++i) {
        temp[i].re = 0;
        temp[i].im = 0;
    }

    cfft((ci32_t *)temp, order, false);

    out_addr[0] = temp[0].re;
    out_addr[1] = temp[fft_len >> 1].re;

    for (uint32_t i = 2, j = 1; i < fft_len; i += 2, ++j) {
        out_addr[i] = temp[j].re;
        out_addr[i + 1] = temp[j].im;
    }

}

static void csky_fft_complex(const int32_t *in_addr, int32_t *out_addr,
        uint32_t order, bool iflag)
{
    uint32_t fft_len = (uint32_t)1 << order;
    assert(in_addr != NULL && out_addr != NULL);

    if (in_addr != out_addr) {
        memcpy(out_addr, in_addr, sizeof(int32_t) * fft_len * 2);
    }

    /*  Right shift 1-bit to avoid overflow. */
    for (uint32_t i = 0; i < fft_len; ++i) {
        out_addr[2 * i + 0] >>= 1;
        out_addr[2 * i + 1] >>= 1;
    }

    cfft((ci32_t *)out_addr, order, iflag);

}

static void csky_ifft_real(const int32_t *in_addr, int32_t *out_addr,
        uint32_t order)
{
    uint32_t fft_len = (uint32_t)1 << order;
    assert(in_addr != NULL && out_addr != NULL);

    ci32_t *temp = g_fft_temp_buffer;

    temp[0].re = in_addr[0];
    temp[0].im = 0;
    temp[fft_len >> 1].re = in_addr[1];
    temp[fft_len >> 1].im = 0;
    for (uint32_t i = 2, j = 1; i < fft_len; i += 2, ++j) {
      temp[j].re = in_addr[i];
      temp[j].im = in_addr[i + 1];
      temp[fft_len - j].re = in_addr[i];
      temp[fft_len - j].im = -in_addr[i + 1];
    }

    /*  Right shift 1-bit to avoid overflow. */
    for (uint32_t i = 0; i < fft_len; ++i) {
      temp[i].re >>= 1;
      temp[i].im >>= 1;
    }

    cfft((ci32_t *)temp, order, true);

    for (uint32_t i = 0; i < fft_len; ++i) {
      out_addr[i] = temp[i].re;
    }
}

static void csky_power_spectrums(const int32_t *in_addr, int64_t *out_addr,
        uint32_t order, uint32_t in_num)
{
    uint32_t fft_len = (uint32_t)1 << order;
    uint32_t fft_len_half = fft_len >> 1;

	int32_t *y = (int32_t *)out_addr;
    csky_fft_real(in_addr, y, order, in_num);

    out_addr[fft_len_half] = (int64_t)y[1] * y[1];
    out_addr[0] = (int64_t)y[0] * y[0];

    for (uint32_t i = 1; i < fft_len_half; ++i) {
        int64_t re = y[2 * i];
        int64_t im = y[2 * i + 1];
        out_addr[i] = re * re + im * im;
    }
}

static int csky_fft_get_order(int mode)
{
    uint32_t order = 9;
    switch (mode) {
    case 0b000001:
        order = 9;
        break;
    case 0b000010:
        order = 8;
        break;
    case 0b000100:
        order = 7;
        break;
    case 0b001000:
        order = 6;
        break;
    case 0b010000:
        order = 5;
        break;
    case 0b100000:
        order = 4;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_fft: mode value error %x\n", mode);
    }
    return order;
}

uint32_t cycles[5][3][6] = {
    { /* RFFT */
        {109, 169, 301, 553, 1105, 2257}, /* 804_dtcm */
        {121, 181, 325, 613, 1213, 2485}, /* 805_dtcm */
        {133, 229, 397, 961, 2089, 4405}, /* sdram */
    },
    { /* CFFT */
        {144, 240, 420, 816, 1620, 3288},
        {156, 264, 480, 924, 1836, 3732},
        {192, 348, 804, 1788, 3768, 7824},
    },
    { /* RIFFT */
        {108, 168, 300, 552, 1104, 2256},
        {120, 180, 324, 612, 1212, 2484},
        {132, 228, 396, 948, 2088, 4428},
    },
    { /* CIFFT */
        {144, 240, 420, 816, 1620, 3288},
        {156, 264, 480, 924, 1836, 3732},
        {192, 348, 828, 1776, 3768, 7812},
    },
    { /* PSD */
        {121, 181, 313, 589, 1177, 2389},
        {133, 205, 349, 649, 1285, 2616},
        {145, 241, 421, 997, 2173, 4585},
    },
};

static int csky_fft_get_region(uint32_t in_addr)
{
    if (in_addr >= 0x10000000 && in_addr < 0x10002000) {
        return 0;
    } else if (in_addr >= 0x80010000 && in_addr < 0x80040000) {
        return 1;
    } else if (in_addr >= 0x18000000 && in_addr < 0x1a000000) {
        return 2;
    }
    return 1;
}

static void csky_fft_compute(struct csky_fft_state *s)
{
    /* fixme: maybe different memory size for different fft mode */
    int32_t *in_addr, *out_addr, region;
    uint32_t func, order, in_num, output_size, input_size;
    func     = s->mode & MODE_FUNC_SEL ;
    in_num   = s->in_num;

    /* default input output size */
    input_size = DEFAULT_INPUT_NUM * 4;
    order = csky_fft_get_order((s->mode >> MODE_POINTS_SEL_SHIFT) &
        MODE_POINTS_SEL_MASK);
    output_size = ((uint32_t)1 << order) * 4;
    /* adjust according to fft func mode */
    if (func == RFFT || func == PSD) {
        input_size = s->in_num * 4;
    }
    if (func == PSD) {
        output_size = (output_size / 8 + 1) * 8;
    } else if (func == CFFT ||
            func == ICFFT) {
        input_size *= 2;
        output_size *= 2;
    }

    /* copy physical memory to a local buffer */
    in_addr = g_malloc(input_size);
    out_addr = g_malloc(output_size);
    region = csky_fft_get_region(s->in_addr);
    cpu_physical_memory_read(s->in_addr, in_addr, input_size);

    switch (func) {
    case RFFT:
        csky_fft_real(in_addr, out_addr, order, in_num);
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (MCA_RFFT << 8),
            cycles[0][region][order - 4]);
        break;
    case CFFT:
        csky_fft_complex(in_addr, out_addr, order, false);
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (MCA_RFFT << 8),
            cycles[1][region][order - 4]);
        break;
    case IRFFT:
        csky_ifft_real(in_addr, out_addr, order);
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (MCA_RFFT << 8),
            cycles[2][region][order - 4]);
        break;
    case ICFFT:
        csky_fft_complex(in_addr, out_addr, order, true);
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (MCA_RFFT << 8),
            cycles[3][region][order - 4]);
        break;
    case PSD:
        csky_power_spectrums(in_addr, (int64_t *)out_addr, order, in_num);
        write_trace_8_24(DEVICE_EVENT, 8, DEVICE_MCA | (MCA_RFFT << 8),
            cycles[4][region][order - 4]);
        break;
    default:
        break;
    }
    /* write back local buffer to physical memory */
    cpu_physical_memory_write(s->out_addr, out_addr, output_size);
    g_free(in_addr);
    g_free(out_addr);
}

static uint64_t csky_fft_read(void *opaque, hwaddr offset, unsigned size)
{
    struct csky_fft_state *s = (struct csky_fft_state *)opaque;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_fft_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset >> 2) {
    case 0x0: /* fft_start_reg */
        ret = s->start;
        break;
    case 0x1: /* fft_men_in_addr_reg */
        ret = s->in_addr;
        break;
    case 0x2: /* fft_men_out_addr_reg */
        ret = s->out_addr;
        break;
    case 0x3: /* fft_mode_sel_reg */
        ret = s->mode;
        break;
    case 0x4: /* fft_in_num_reg */
        ret = s->in_num;
        break;
    case 0x5: /* fft_intr_flag_reg */
        ret = s->intr;
        break;
    case 0x6: /* fft_intr_mask_reg*/
        ret = s->mask;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_fft_read: Bad offset %x\n", (int)offset);
    }

    return ret;
}

static void csky_fft_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    struct csky_fft_state *s = (struct csky_fft_state *)opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_fft_write: 0x%x must word align read\n",
                      (int)offset);
    }
    switch (offset >> 2) {
    case 0x0: /* fft_start_reg */
        if ((s->start & 0x1) != 1) { /* not busy */
            s->start |= 0x1;
            csky_fft_compute(s);
            s->intr |= INTERRUPT_MASK << INTERRUPT_POS;
            qemu_set_irq(s->irq, 1);
            s->start &= (UINT32_MAX - 1);
        } else { /* device is busy */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_fft_write: device is too busy\n");
        }
        break;
    case 0x1: /* fft_in_addr_reg */
        if ((value % 4) != 0) { /* starting address with 00,04,08,0c */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_fft_write: input address must align with 4\n");
            s->intr |= MEM_IN_ADDR_ERROR_MASK << MEM_IN_ADDR_ERROR_POS;
            if (s->mask & MEM_IN_ADDR_ERROR_MASK) {
                qemu_set_irq(s->irq, 1);
            }
        } else {
            s->in_addr = value;
        }
        break;
    case 0x2: /* fft_out_addr_reg */
        if ((value % 4) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_fft_write: output address must align with 4\n");
            s->intr |= MEM_OUT_ADDR_ERROR_MASK << MEM_OUT_ADDR_ERROR_POS;
            if (s->mask & MEM_OUT_ADDR_ERROR_MASK) {
                qemu_set_irq(s->irq, 1);
            }
        } else {
            s->out_addr = value;
        }
        break;
    case 0x3: /* fft_mode_sel_reg */
        s->mode = (value & (~(MODE_RESERVED_MASK))) |
            (s->mode & MODE_RESERVED_MASK);
        break;
    case 0x4: /* fft_in_num_reg */
        if (((s->mode & MODE_FUNC_SEL) != CFFT) &&
            ((s->mode & MODE_FUNC_SEL) != ICFFT)) {
            s->in_num =  (value & (~(IN_NUM_RESERVED_MASK))) |
                (s->in_num & IN_NUM_RESERVED_MASK);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                "csky_fft_write: in_num is ignored in complex fft or ifft\n");
        }
        break;
    case 0x5: /* fft_intr_flag_reg*/
        if (value & INTR_FLAG_MASK) {
            s->intr &= ~(value & INTR_FLAG_MASK) | INTR_FLAG_RESERVED_MASK;
            qemu_set_irq(s->irq, 0);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                "csky_fft_write: intr_flag is cleared by write 1\n");
        }
        break;
    case 0x6: /* fft_intr_mask_reg*/
        s->mask = (value & (~(INTR_MASK_RESERVED_MASK))) |
            (s->mode & INTR_MASK_RESERVED_MASK);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_fft_write: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps csky_fft_ops = {
    .read = csky_fft_read,
    .write = csky_fft_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_csky_fft = {
    .name = TYPE_THEAD_FFT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(start, struct csky_fft_state),
        VMSTATE_UINT32(in_addr, struct csky_fft_state),
        VMSTATE_UINT32(out_addr, struct csky_fft_state),
        VMSTATE_UINT32(mode, struct csky_fft_state),
        VMSTATE_UINT32(in_num, struct csky_fft_state),
        VMSTATE_UINT32(intr, struct csky_fft_state),
        VMSTATE_UINT32(mask, struct csky_fft_state),
        VMSTATE_END_OF_LIST()
    }
};

static void csky_fft_init(Object *obj)
{
    struct csky_fft_state *s = THEAD_FFT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_fft_ops, s,
                          TYPE_THEAD_FFT, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void csky_fft_realize(DeviceState *dev, Error **errp)
{
    struct csky_fft_state *s = THEAD_FFT(dev);
    s->start        = 0x00000000;
    s->in_addr  = 0x00000000;
    s->out_addr = 0x00000000;
    s->mode     = 0x00000101;
    s->in_num       = 0x00000200;
    s->intr   = 0x00000000;
    s->mask    = 0x00000001;
}

static void csky_fft_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_CSKY, dc->categories);
    dc->realize = csky_fft_realize;
    dc->vmsd = &vmstate_csky_fft;
    dc->desc = "cskysim type: FFT";
    dc->user_creatable = true;
}

static const TypeInfo csky_fft_info = {
    .name          = TYPE_THEAD_FFT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct csky_fft_state),
    .instance_init = csky_fft_init,
    .class_init    = csky_fft_class_init,
};


static void csky_fft_register_types(void)
{
    type_register_static(&csky_fft_info);
}

type_init(csky_fft_register_types)

