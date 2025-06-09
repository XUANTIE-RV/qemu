/*
 * QEMU RISC-V MTT(Memory tracking table)
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

#ifndef RISCV_MTT_H
#define RISCV_MTT_H

#include "cpu.h"

typedef enum {
    SMMTTBARE = 0,
    SMMTT34   = 1,
    SMMTT46   = 2,
    SMMTT56   = 3,
    SMMTTMAX
} mtt_mode_t;

typedef enum {
    ACCESS_DISALLOW = 0,
    ACCESS_ALLOW_RX,
    ACCESS_ALLOW_RW,
    ACCESS_ALLOW_RWX,
} mtt_access_t;


int mtt_access_to_page_prot(mtt_access_t mtt_access);
bool mtt_check_access(CPURISCVState *env, hwaddr addr,
                      mtt_access_t *allowed_access, MMUAccessType access_type);

#endif
