/*
 * CSKY smartl System emulation.
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
#include "hw/timer/csky_timer.h"
#include "hw/intc/csky_tcip_v1.h"

#define CORET_IRQ_NUM   1
#define SMARTL_SRAM0    (16 * 1024 * 1024)
#define SMARTL_SRAM1    (16 * 1024 * 1024)
#define SMARTL_SRAM2    (16 * 1024 * 1024)
#define SMARTL_SRAM3    (16 * 1024 * 1024)

static struct csky_boot_info smartl_binfo = {
    .loader_start = 0x0,
    .freq         = 1000000000ll,
};

static void smartl_init(MachineState *machine)
{
    Object *cpuobj;
    CSKYCPU *cpu;
    CPUCSKYState *env;
    qemu_irq *cpu_intc;
    qemu_irq intc[32];
    DeviceState *dev;
    int i;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram0 = g_new(MemoryRegion, 1);
    MemoryRegion *ram1 = g_new(MemoryRegion, 1);
    MemoryRegion *ram2 = g_new(MemoryRegion, 1);
    MemoryRegion *ram3 = g_new(MemoryRegion, 1);

    cpuobj = object_new(machine->cpu_type);

    object_property_set_bool(cpuobj, "realized", true, &error_fatal);

    cpu = CSKY_CPU(cpuobj);
    env = &cpu->env;

    memory_region_init_ram(ram0, NULL, "smartl.sdram0",
                           SMARTL_SRAM0, &error_fatal);
    memory_region_add_subregion(sysmem, 0x0, ram0);
    memory_region_init_ram(ram1, NULL, "smartl.sdram1",
                           SMARTL_SRAM1, &error_fatal);
    memory_region_add_subregion(sysmem, 0x20000000, ram1);
    memory_region_init_ram(ram2, NULL, "smartl.sdram2",
                           SMARTL_SRAM2, &error_fatal);
    memory_region_add_subregion(sysmem, 0x50000000, ram2);
    memory_region_init_ram(ram3, NULL, "smartl.sdram3",
                           SMARTL_SRAM3, &error_fatal);
    memory_region_add_subregion(sysmem, 0x60000000, ram3);

    cpu_intc = csky_vic_v1_init_cpu(env);

    if (env->cpu_freq != 0) {
        smartl_binfo.freq = env->cpu_freq;
    }

    dev = sysbus_create_simple("csky_tcip_v1", 0xE000E100, cpu_intc[0]);

    for (i = 0; i < 32; i++) {
        intc[i] = qdev_get_gpio_in(dev, i);
    }

    sysbus_create_simple("csky_coret", 0xE000E000, intc[CORET_IRQ_NUM]);
    csky_coret_set_freq(smartl_binfo.freq);

    csky_uart_create(0x40015000, intc[0], NULL, serial_hd(0));

    csky_timer_set_freq(smartl_binfo.freq);
    sysbus_create_varargs("csky_timer", 0x40011000, intc[2], intc[3],
                            intc[4], intc[5], NULL);

    sysbus_create_simple("csky_exit", 0x10002000, NULL);
    sysbus_create_simple("csky_memlog", 0x10003000, NULL);

    smartl_binfo.ram_size = machine->ram_size;
    smartl_binfo.kernel_filename = machine->kernel_filename;
    smartl_binfo.kernel_cmdline = machine->kernel_cmdline;
    smartl_binfo.initrd_filename = machine->initrd_filename;
    csky_load_kernel(cpu, &smartl_binfo);
}

static void smartl_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CSKY smartl";
    mc->init = smartl_init;
    mc->default_cpu_type = CSKY_CPU_TYPE_NAME("e804df");
}

static const TypeInfo smartl_type = {
    .name = MACHINE_TYPE_NAME("smartl"),
    .parent = TYPE_MACHINE,
    .class_init = smartl_class_init,
};

static void smartl_machine_init(void)
{
    type_register_static(&smartl_type);
}

type_init(smartl_machine_init)
