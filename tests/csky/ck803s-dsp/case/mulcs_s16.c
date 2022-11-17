/*
 * Copyright (c) 2021 T-Head Semiconductor Co., Ltd. All rights reserved.
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
#include "test_device.h"
#include "dsp_insn.h"
#define TEST_TIMES      12
int main(void)
{
    int i = 0;

    init_testsuite("Testing insn MULCS.S16 \n");

    /*
     * MULCS.S16
     * rz = rx[15:0] * ry[15:0] - rx[31:16] * ry[31:16], signed
     */
    struct binary_calculation bin_sample[TEST_TIMES] = {
        {0x00010001, 0x00000001, 0x00000001},
        {0x00010001, 0x7FFF7FFF, 0x00000000},
        {0x00010010, 0x80007FFF, 0x00087FF0},
        {0x00010001, 0x0000FFFF, 0xFFFFFFFF},
        {0x00010002, 0x7FFF8000, 0xFFFE8001},
        {0x00017FFF, 0x8000FFFF, 0x00000001},
        {0x0001FFFF, 0x0000FFFF, 0x00000001},
        {0x0001FFFF, 0x7FFF8000, 0x00000001},
        {0x00018000, 0x80008000, 0x40008000},
        {0x00010000, 0x00000000, 0x00000000},
        {0x00010000, 0x7FFF7FFF, 0xFFFF8001},
        {0x00010000, 0x80008000, 0x00008000},
    };

    for (i = 0; i < TEST_TIMES; i++) {
        TEST(test_mulcs_s16(bin_sample[i].op1, bin_sample[i].op2)
                     == bin_sample[i].result);
    }

    return done_testing();
}

