/*
 * C-SKY fcntl header
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

#ifndef CSKY_TARGET_FCNTL_H
#define CSKY_TARGET_FCNTL_H
#include "../generic/fcntl.h"
#ifndef CONFIG_CSKY_KERNEL_4X
#undef TARGET_O_ACCMODE
#undef TARGET_O_RDONLY
#undef TARGET_O_WRONLY
#undef TARGET_O_RDWR
#undef TARGET_O_CREAT
#undef TARGET_O_EXCL
#undef TARGET_O_NOCTTY
#undef TARGET_O_TRUNC
#undef TARGET_O_APPEND
#undef TARGET_O_NONBLOCK
#undef TARGET_O_NDELAY
#undef TARGET_O_SYNC
#undef TARGET_FASYNC
#undef TARGET_O_DIRECTORY
#undef TARGET_O_NOFOLLOW
#undef TARGET_O_DIRECT
#undef TARGET_O_LARGEFILE
#define TARGET_O_ACCMODE        0003
#define TARGET_O_RDONLY         00
#define TARGET_O_WRONLY         01
#define TARGET_O_RDWR           02
#define TARGET_O_CREAT          0100 /* not fcntl */
#define TARGET_O_EXCL           0200 /* not fcntl */
#define TARGET_O_NOCTTY         0400 /* not fcntl */
#define TARGET_O_TRUNC          01000 /* not fcntl */
#define TARGET_O_APPEND         02000
#define TARGET_O_NONBLOCK       04000
#define TARGET_O_NDELAY         TARGET_O_NONBLOCK
#define TARGET_O_SYNC           010000
#define TARGET_FASYNC           020000 /* fcntl, for BSD compatibility */
/* these 4 macros are csky-specific */
#define TARGET_O_DIRECTORY      040000 /* must be a directory */
#define TARGET_O_NOFOLLOW      0100000 /* don't follow links */
#define TARGET_O_DIRECT        0200000 /* direct disk access hint */
#define TARGET_O_LARGEFILE     0400000
#endif
#endif
