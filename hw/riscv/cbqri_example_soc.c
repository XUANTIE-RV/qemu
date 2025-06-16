/*
 * RISC-V Capacity and Bandwidth QoS Register Interface
 * URL: https://github.com/riscv-non-isa/riscv-cbqri
 *
 * Copyright (c) 2023 BayLibre SAS
 *
 * This file contains an hypothetical CBQRI configuration instantiation
 * for testing purposes. This may also be configured from the command
 * line.
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/cbqri.h"

/*
 * Example hardware:
 *
 * - Global
 *   - Number of RCIDs - 64
 *   - Number of MCIDs - 256
 * - L2 cache
 *   - NCBLKS - 12
 *   - Number of access types - 2 (code and data)
 *   - Usage monitoring not supported
 *   - Capacity allocation operations - CONFIG_LIMIT, READ_LIMIT
 * - LLC
 *   - NCBLKS - 16
 *   - Number of access types - 2 (code and data)
 *   - Usage monitoring operations - CONFIG_EVENT, READ_COUNTER
 *   - Event IDs supported - None, Occupancy
 *   - Capacity allocation operations - CONFIG_LIMIT, READ_LIMIT, FLUSH_RCID
 * - Memory controllers
 *   - NBWBLKS - 1024
 *   - MRBWB - 80 (80%)
 *   - Usage monitoring operations - CONFIG_EVENT, READ_COUNTER
 *   - Event IDs supported - None, Total read/write byte count,
 *   - total read byte count, total write byte count
 *   - Bandwidth allocation operations - CONFIG_LIMIT, READ_LIMIT
 *   - Number of access types - 1 (no code/data differentiation)
 *
 * 0x04820000  Cluster 0 L2 cache controller
 * 0x04821000  Cluster 1 L2 cache controller
 * 0x0482B000  Shared LLC controller
 * 0x04828000  Memory controller 0
 * 0x04829000  Memory controller 1
 * 0x0482A000  Memory controller 2
 */

#define CBQRI_NB_MCIDS  256
#define CBQRI_NB_RCIDS  64

static const RiscvCbqriCapacityCaps example_soc_L2_cluster = {
    .nb_mcids = CBQRI_NB_MCIDS,
    .nb_rcids = CBQRI_NB_RCIDS,
    .ncblks = 12,
    .supports_at_data = true,
    .supports_at_code = true,
    .supports_alloc_op_config_limit = true,
    .supports_alloc_op_read_limit = true,
};

static const RiscvCbqriCapacityCaps example_soc_LLC = {
    .nb_mcids = CBQRI_NB_MCIDS,
    .nb_rcids = CBQRI_NB_RCIDS,
    .ncblks = 16,
    .supports_at_data = true,
    .supports_at_code = true,
    .supports_alloc_op_config_limit = true,
    .supports_alloc_op_read_limit = true,
    .supports_alloc_op_flush_rcid = true,
    .supports_mon_op_config_event = true,
    .supports_mon_op_read_counter = true,
    .supports_mon_evt_id_none = true,
    .supports_mon_evt_id_occupancy = true,
};

static const RiscvCbqriBandwidthCaps example_soc_memory = {
    .nb_mcids = CBQRI_NB_MCIDS,
    .nb_rcids = CBQRI_NB_RCIDS,
    .nbwblks = 1024,
    .mrbwb = 1024 * 80 / 100,
    .supports_alloc_op_config_limit = true,
    .supports_alloc_op_read_limit = true,
    .supports_mon_op_config_event = true,
    .supports_mon_op_read_counter = true,
    .supports_mon_evt_id_none = true,
    .supports_mon_evt_id_rdwr_count = true,
    .supports_mon_evt_id_rdonly_count = true,
    .supports_mon_evt_id_wronly_count = true,
};

void example_soc_cbqri_init(void)
{
    riscv_cbqri_cc_create(0x04820000, &example_soc_L2_cluster,
                          "cluster 0 L2 cache controller");
    riscv_cbqri_cc_create(0x04821000, &example_soc_L2_cluster,
                          "cluster 1 L2 cache controller");
    riscv_cbqri_cc_create(0x0482B000, &example_soc_LLC,
                          "shared LLC controller");
    riscv_cbqri_bc_create(0x04828000, &example_soc_memory,
                          "memory controller 0");
    riscv_cbqri_bc_create(0x04829000, &example_soc_memory,
                          "memory controller 1");
    riscv_cbqri_bc_create(0x0482a000, &example_soc_memory,
                          "memory controller 2");
}
