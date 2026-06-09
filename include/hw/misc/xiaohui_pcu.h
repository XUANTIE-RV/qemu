/*
 * Xiaohui PCU (Power Control Unit) - public interface.
 *
 * Copyright (c) 2026 Alibaba Group. All rights reserved.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XIAOHUI_PCU_H
#define HW_XIAOHUI_PCU_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"

#define TYPE_XIAOHUI_PCU  "xiaohui_pcu"

/*
 * Query the PCU instance that manages the given hart.
 * Returns the PCU device, or NULL if not found.
 */
DeviceState *xiaohui_pcu_find_by_hart(uint32_t hart_id);

/*
 * Check whether the PCU for the given hart has both RVBA and power-on mode
 * configured (i.e. sw_pwr_mode == ON).
 * If ready, fills *rvba with the configured reset vector and returns true.
 * Otherwise returns false.
 */
bool xiaohui_pcu_get_release_info(DeviceState *pcu_dev, uint64_t *rvba);

#endif /* HW_XIAOHUI_PCU_H */
