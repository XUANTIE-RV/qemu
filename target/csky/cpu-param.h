/*
 * C-SKY cpu parameters for qemu.
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
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

#ifndef CSKY_CPU_PARAM_H
#define CSKY_CPU_PARAM_H 1

#define TARGET_LONG_BITS    32
#define TARGET_PAGE_BITS    12
#define TARGET_PHYS_ADDR_SPACE_BITS 32

#ifdef CONFIG_USER_ONLY
#define TARGET_VIRT_ADDR_SPACE_BITS 30
#else
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif
#define TCG_GUEST_DEFAULT_MO      (0)

#endif
