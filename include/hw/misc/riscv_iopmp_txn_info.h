/*
 * QEMU RISC-V IOPMP transaction information
 *
 * The transaction information structure provides the complete transaction
 * length to the IOPMP device
 *
 * Copyright (c) 2023-2025 Andes Tech. Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#ifndef RISCV_IOPMP_TXN_INFO_H
#define RISCV_IOPMP_TXN_INFO_H

typedef struct {
    /* The id of requestor */
    uint32_t rrid:16;
    /* The start address of transaction */
    uint64_t start_addr;
    /* The end address of transaction */
    uint64_t end_addr;
    /* The stage of cascading IOPMP */
    uint32_t stage;
} RISCVIOPMPTxnInfo;

#endif
