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
 * Lesser General Public License  *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "testsuite.h"
#include "rvv_insn.h"

struct rvv_reg src0[] = {
    {
        .fixs8 = {
            {0x01, 0x10, 0x11, 0x10, 0x10, 0x01, 0x10, 0x10, 0x01, 0x11, 0x00, 0x11, 0x00, 0x11, 0x01, 0x01, },
        },
        .fixs16 = {
            {0x0001, 0x0100, 0x0101, 0x0100, 0x0001, 0x0101, 0x0100, 0x0001, },
        },
        .fixs32 = {
            {0x00010000, 0x00010001, 0x00010001, 0x00000001, },
        },
        .fixs64 = {
            {0x0000000100000000, 0x0000000000000001, },
        },
    },
};

uint32_t dst[12] = {17, 9, 9, 9, 5, 5, 5, 3, 3, 1, 1, 1};
uint32_t res[12];

int main(void)
{
    int i = 0;
    init_testsuite("Testing insn vmpopc.m\n");

    res[0] = test_vmpopc_m_8(src0[i].fixs8[0], vlmax_8 - 2);
    res[1] = test_vmpopc_m_8_vm(src0[i].fixs8[0], vmo.fixs8);
    res[2] = test_vmpopc_m_8_vm(src0[i].fixs8[0], vme.fixs8);

    res[3] = test_vmpopc_m_16(src0[i].fixs16[0], vlmax_16 - 2);
    res[4] = test_vmpopc_m_16_vm(src0[i].fixs16[0], vmo.fixs16);
    res[5] = test_vmpopc_m_16_vm(src0[i].fixs16[0], vme.fixs16);


    res[6] = test_vmpopc_m_32(src0[i].fixs32[0], vlmax_32 - 2);
    res[7] = test_vmpopc_m_32_vm(src0[i].fixs32[0], vmo.fixs32);
    res[8] = test_vmpopc_m_32_vm(src0[i].fixs32[0], vme.fixs32);

    res[9] = test_vmpopc_m_64(src0[i].fixs64[0], vlmax_64 - 2);
    res[10] = test_vmpopc_m_64_vm(src0[i].fixs64[0], vmo.fixs64);
    res[11] = test_vmpopc_m_64_vm(src0[i].fixs64[0], vme.fixs64);

    for (i = 0; i < 12; i++) {
        TEST(dst[i] == res[i]);
#ifdef DEBUG
        printf("%d, %d,\n", dst[i], res[i]);
#endif
    }

    return done_testing();
}