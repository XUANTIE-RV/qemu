/*
 * Copyright (c) 2011-2019 C-SKY Limited. All rights reserved.
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


#include "testsuite.h"
#include "matrix_insn.h"


struct matrix_mn4clip_s src0[] = {
    {
        .matrix_uint32_4x4 = {{0x0000FFFF, 0x00012345, 0x11111111, 0xFFFFDDDD},
                              {0x0000FFFF, 0x00012345, 0x11111111, 0xFFFFDDDD},
                              {0x0000FFFF, 0x00012345, 0x11111111, 0xFFFFDDDD},
                              {0x0000FFFF, 0x00012345, 0x11111111, 0xFFFFDDDD},}
    }
};

struct matrix_mn4clip_s src1[] = {
    {
        .matrix_uint32_4x4 = {{0x0000000C, 0x0000000C, 0x00000012, 0x0000001B},
                              {0x0000000E, 0x0000000E, 0x00000014, 0x00000014},
                              {0x00000009, 0x00000008, 0x00000015, 0x0000001D},
                              {0x00000008, 0x00000009, 0x00000015, 0x0000001B},}
    }
};

struct matrix_mn4clip_s src2[] = {
    {
        .matrix_uint8_4x16 = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                              {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                              {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                              {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06},}
    }
};

struct matrix_mn4clip_s dst0[] = {
    {
        .matrix_uint8_4x16 = {{0x10, 0x12, 0xFF, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                              {0x04, 0x05, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                              {0x80, 0xFF, 0x89, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                              {0xFF, 0x92, 0x89, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},}
    },
    {
        .matrix_uint8_4x16 = {{0x10, 0x12, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                              {0x04, 0x05, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                              {0x80, 0xFF, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                              {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},}
    }
};


struct matrix_mn4clip_s res;

#include <stdio.h>
int main(void)
{
    init_testsuite("Testing insn mn4clipu.s.mm\n");

    test_mn4clipu_s_mm(src0[0].matrix_uint32_4x4,
                       src1[0].matrix_uint32_4x4,
                       src2[0].matrix_uint32_4x4,
                       res.matrix_uint8_4x16);
    result_compare_mn4clipu_s(dst0[0].matrix_uint8_4x16, res.matrix_uint8_4x16);

    memset(&res, 0, sizeof(dst0[0]));

    test_mn4clipu_s_mm_3x3(src0[0].matrix_uint32_4x4,
                           src1[0].matrix_uint32_4x4,
                           src2[0].matrix_uint32_4x4,
                           res.matrix_uint8_4x16);
    result_compare_mn4clipu_s(dst0[1].matrix_uint8_4x16, res.matrix_uint8_4x16);

    return done_testing();
}