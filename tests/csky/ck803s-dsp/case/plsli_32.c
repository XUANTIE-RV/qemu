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
#define TEST_TIMES      4
int main(void)
{
    init_testsuite("Testing insn PLSLI.32 \n");

    /*
     * PLSLI.32
     * rz = rx << imm5, r(z+1) = r(x+1) << imm5
     */
    struct binary64_calculation sample[TEST_TIMES] = {
        {0Xffffffff, 0X00000000, 0x00000000ffffffff}, /* imm = 0 */
        {0X12345678, 0X00000001, 0x000000022468ACF0}, /* imm = 1 */
        {0Xffffffff, 0X7fffffff, 0xfff00000fff00000}, /* imm = 20 */
        {0X80000000, 0X7fffffff, 0x8000000000000000}, /* imm = 31 */
    };

    TEST(test_plsli_32_0(sample[0].op1, sample[0].op2) == sample[0].result);
    TEST(test_plsli_32_1(sample[1].op1, sample[1].op2) == sample[1].result);
    TEST(test_plsli_32_20(sample[2].op1, sample[2].op2) == sample[2].result);
    TEST(test_plsli_32_31(sample[3].op1, sample[3].op2) == sample[3].result);
    return done_testing();
}
