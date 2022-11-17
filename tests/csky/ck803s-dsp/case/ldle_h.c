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
#define TEST_TIMES      2
int main(void)
{
    init_testsuite("Testing insn LDLE.H \n");

    /*
     * LDLE.H
     * rz = {rz[31:16], mem(rx+offset<<1)[15:0]}
     */
    int32_t sample[TEST_TIMES] = {0x0000ffff, 0x00007fff};

    TEST(test_ldle_h_1() == sample[0]);
    TEST(test_ldle_h_2() == sample[1]);

    return done_testing();
}

