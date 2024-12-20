/*
 * CSKY hw header.
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

#ifndef HW_CSKY_BOOT_H
#define HW_CSKY_BOOT_H

#include "exec/memory.h"
#include "hw/irq.h"
#include "qemu/notify.h"

#ifdef TARGET_CSKY
#include "target/csky/cpu.h"
#endif

/*
 * struct used as a parameter of the csky_load_kernel machine init
 * done notifier
 */
#ifdef TARGET_CSKY
typedef struct {
    Notifier notifier; /* actual notifier */
    CSKYCPU *cpu; /* handle to the first cpu object */
} CSKYLoadKernelNotifier;
#endif

struct csky_boot_info {
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    const char *dtb_filename;
    const char *bios;
    hwaddr loader_start;
    hwaddr initrd_size;
    uint32_t kernel_flags;
    hwaddr entry;
    hwaddr dtb_addr;
    uint32_t magic;
    uint32_t freq;
#ifdef TARGET_CSKY
    CSKYLoadKernelNotifier load_kernel_notifier;
#endif
};

/* kernel flags */
#define KERNEL_ELF                  1 /* elf */
#define KERNEL_UIMAGE               2 /* uimage */
#define KERNEL_BIN_NO_BIOS          3 /* bin without bios */
#define KERNEL_BIN_AND_BIOS         4 /* bin with bios */
#define KERNEL_KBIN_NO_CMDLINE      5 /* Kernel bin whithout cmdline and bios */

#ifdef TARGET_CSKY
/**
 * csky_load_kernel - Loads memory with everything needed to boot
 *
 * @cpu: handle to the first CPU object
 * @info: handle to the boot info struct
 * Registers a machine init done notifier that copies to memory
 * everything needed to boot, depending on machine and user options:
 * kernel image, boot loaders, initrd, dtb. Also registers the CPU
 * reset handler.
 */
void csky_load_kernel(CSKYCPU *cpu, struct csky_boot_info *info);
#endif

#endif
