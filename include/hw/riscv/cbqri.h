/*
 * RISC-V Capacity and Bandwidth QoS Register Interface
 * URL: https://github.com/riscv-non-isa/riscv-cbqri
 *
 * Copyright (c) 2023 BayLibre SAS
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

#ifndef HW_RISCV_CBQRI_H
#define HW_RISCV_CBQRI_H

#include "qemu/typedefs.h"

#define RISCV_CBQRI_VERSION_MAJOR   0
#define RISCV_CBQRI_VERSION_MINOR   1

#define TYPE_RISCV_CBQRI_CC         "riscv.cbqri.capacity"
#define TYPE_RISCV_CBQRI_BC         "riscv.cbqri.bandwidth"

/* Capacity Controller hardware capabilities */
typedef struct RiscvCbqriCapacityCaps {
    uint16_t nb_mcids;
    uint16_t nb_rcids;

    uint16_t ncblks;

    bool supports_at_data:1;
    bool supports_at_code:1;

    bool supports_alloc_op_config_limit:1;
    bool supports_alloc_op_read_limit:1;
    bool supports_alloc_op_flush_rcid:1;

    bool supports_mon_op_config_event:1;
    bool supports_mon_op_read_counter:1;

    bool supports_mon_evt_id_none:1;
    bool supports_mon_evt_id_occupancy:1;
} RiscvCbqriCapacityCaps;

/* Bandwidth Controller hardware capabilities */
typedef struct RiscvCbqriBandwidthCaps {
    uint16_t nb_mcids;
    uint16_t nb_rcids;

    uint16_t nbwblks;
    uint16_t mrbwb;

    bool supports_at_data:1;
    bool supports_at_code:1;

    bool supports_alloc_op_config_limit:1;
    bool supports_alloc_op_read_limit:1;

    bool supports_mon_op_config_event:1;
    bool supports_mon_op_read_counter:1;

    bool supports_mon_evt_id_none:1;
    bool supports_mon_evt_id_rdwr_count:1;
    bool supports_mon_evt_id_rdonly_count:1;
    bool supports_mon_evt_id_wronly_count:1;
} RiscvCbqriBandwidthCaps;

DeviceState *riscv_cbqri_cc_create(hwaddr addr,
                                   const RiscvCbqriCapacityCaps *caps,
                                   const char *target_name);
DeviceState *riscv_cbqri_bc_create(hwaddr addr,
                                   const RiscvCbqriBandwidthCaps *caps,
                                   const char *target_name);

void example_soc_cbqri_init(void);
#endif
