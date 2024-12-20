/*
 * RISCV SMARTL System emulation.
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

#ifndef HW_THEAD_ASP_V1_H
#define HW_THEAD_ASP_V1_H

#define ASP_CLIC_IRQ_NUMS 256
#define ASP_CLIC_VERSION "v0.8"
#define ASP_CLIC_HARTS 1
#define ASP_CLIC_INTCTLBITS 3

enum {
    ASP_SRAM0,
    ASP_SRAM1,
    ASP_SRAM2,
    ASP_SRAM3,
    ASP_SRAM_A,
    ASP_SRAM_B,
    ASP_SRAM_C,
    ASP_SRAM_D,
    ASP_SYSTEMMAP,
    ASP_TIMER,
    ASP_UART,
    ASP_CLINT,
    ASP_CLIC,
    ASP_EXIT,
    ASP_NNE,
};

#endif
