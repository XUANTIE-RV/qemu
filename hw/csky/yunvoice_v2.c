/*
 * CSKY YunVoice V2 System emulation.
 *
 * Copyright (c) 2019 C-SKY Limited. All rights reserved.
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

#undef NEED_CPU_H
#define NEED_CPU_H

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "target/csky/cpu.h"
#include "hw/csky/csky_boot.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/char/csky_uart.h"
#include "hw/timer/csky_coret.h"
#include "hw/intc/csky_tcip_v1.h"

#define UART_IRQ_NUM  0
#define CORET_IRQ_NUM 1
#define MCA_IRQ_NUM   2
#define FFT_IRQ_NUM   3

#define YUNVOICE_V2_E804_DTCM 0
#define YUNVOICE_V2_EXIT      1
#define YUNVOICE_V2_SDRAM     2
#define YUNVOICE_V2_FFT       3
#define YUNVOICE_V2_I805_DTCM 4
#define YUNVOICE_V2_MCA       5
#define YUNVOICE_V2_UART3     6
#define YUNVOICE_V2_CORET     7
#define YUNVOICE_V2_TCIP      8

static struct csky_boot_info yunvoice_v2_binfo = {
    .loader_start = 0x0,
    .freq         = 1000000000ll,
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} yunvoice_v2_memmap[] = {
    [YUNVOICE_V2_E804_DTCM] =  {0x10000000,     0x2000},
    [YUNVOICE_V2_EXIT] =       {0x10002000,     0x1000}, /* fake device */
    [YUNVOICE_V2_SDRAM] =      {0x18000000,  0x2000000},
    [YUNVOICE_V2_FFT] =        {0x2A000000,  0x1000000},
    [YUNVOICE_V2_I805_DTCM] =  {0x80010000,    0x30000},
    [YUNVOICE_V2_MCA] =        {0x89000000,  0x1000000},
    [YUNVOICE_V2_UART3] =      {0x96000000,     0x1000}, /* fixme: wrong size */
    [YUNVOICE_V2_CORET] =      {0xE000E000,      0x100},
    [YUNVOICE_V2_TCIP] =       {0xE000E100,      0xe00},
};

#define UART_IRQ_NUM  0
#define CORET_IRQ_NUM 1
#define MCA_IRQ_NUM   2
#define FFT_IRQ_NUM   3

static void yunvoice_v2_init(MachineState *machine)
{
    CPUState * cs;
    CPUCSKYState *env;
    qemu_irq *cpu_intc;
    qemu_irq intc[32];
    DeviceState *dev;
    int i;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram0 = g_new(MemoryRegion, 1);
    MemoryRegion *ram1 = g_new(MemoryRegion, 1);
    MemoryRegion *ram2 = g_new(MemoryRegion, 1);

    cs = cpu_create(machine->cpu_type);
    env = csky_cpu_get_env(cs);

    memory_region_init_ram(ram0, NULL, "e804.dtcm",
        yunvoice_v2_memmap[YUNVOICE_V2_E804_DTCM].size,
        &error_fatal);
    memory_region_add_subregion(sysmem,
        yunvoice_v2_memmap[YUNVOICE_V2_E804_DTCM].base, ram0);

    memory_region_init_ram(ram1, NULL, "sdram",
        yunvoice_v2_memmap[YUNVOICE_V2_SDRAM].size,
        &error_fatal);
    memory_region_add_subregion(sysmem,
        yunvoice_v2_memmap[YUNVOICE_V2_SDRAM].base, ram1);

    memory_region_init_ram(ram2, NULL, "i805.dtcm",
        yunvoice_v2_memmap[YUNVOICE_V2_I805_DTCM].size,
        &error_fatal);
    memory_region_add_subregion(sysmem,
        yunvoice_v2_memmap[YUNVOICE_V2_I805_DTCM].base, ram2);

    cpu_intc = csky_vic_v1_init_cpu(env);

    if (env->cpu_freq != 0) {
        yunvoice_v2_binfo.freq = env->cpu_freq;
    }

    dev = sysbus_create_simple("csky_tcip_v1",
        yunvoice_v2_memmap[YUNVOICE_V2_TCIP].base, cpu_intc[0]);
    for (i = 0; i < 32; i++) {
        intc[i] = qdev_get_gpio_in(dev, i);
    }

    sysbus_create_simple("csky_coret",
        yunvoice_v2_memmap[YUNVOICE_V2_CORET].base, intc[CORET_IRQ_NUM]);
    csky_coret_set_freq(yunvoice_v2_binfo.freq);

    sysbus_create_simple("csky_mca",
        yunvoice_v2_memmap[YUNVOICE_V2_MCA].base, intc[MCA_IRQ_NUM]);
    sysbus_create_simple("csky_fft",
        yunvoice_v2_memmap[YUNVOICE_V2_FFT].base, intc[FFT_IRQ_NUM]);

    csky_uart_create(yunvoice_v2_memmap[YUNVOICE_V2_UART3].base,
        intc[UART_IRQ_NUM], NULL, serial_hd(0));
    sysbus_create_simple("csky_exit",
        yunvoice_v2_memmap[YUNVOICE_V2_EXIT].base, NULL);

    yunvoice_v2_binfo.ram_size = machine->ram_size;
    yunvoice_v2_binfo.kernel_filename = machine->kernel_filename;
    yunvoice_v2_binfo.kernel_cmdline = machine->kernel_cmdline;
    yunvoice_v2_binfo.initrd_filename = machine->initrd_filename;
    csky_load_kernel((CSKYCPU *)cs, &yunvoice_v2_binfo);
}

static void yunvoice_v2_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CSKY yunvoice_v2";
    mc->init = yunvoice_v2_init;
    mc->default_cpu_type = CSKY_CPU_TYPE_NAME("i805f");
}

static const TypeInfo yunvoice_v2_type = {
    .name = MACHINE_TYPE_NAME("yunvoice_v2"),
    .parent = TYPE_MACHINE,
    .class_init = yunvoice_v2_class_init,
};

static void yunvoice_v2_machine_init(void)
{
    type_register_static(&yunvoice_v2_type);
}

type_init(yunvoice_v2_machine_init)
