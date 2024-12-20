/*
 * CSKY Trilobite System emulation.
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
#include "hw/timer/csky_timer.h"
#include "hw/intc/csky_intc.h"
#include "hw/csky/dynsoc.h"
#include "hw/net/csky_mac.h"

#define CORET_IRQ_NUM   0

static struct csky_boot_info trilobite_binfo = {
    .loader_start = 0x0,
    .dtb_addr = 0x8f000000,
    .magic = 0x20150401,
    .freq = 50000000ll,
};

static void trilobite_init(MachineState *machine)
{
    Object *cpuobj;
    CSKYCPU *cpu;
    CPUCSKYState *env;
    qemu_irq *cpu_intc;
    qemu_irq intc[32];
    DeviceState *dev;
    int i;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);

    cpuobj = object_new(machine->cpu_type);

    object_property_set_bool(cpuobj, "realized", true, &error_fatal);

    cpu = CSKY_CPU(cpuobj);
    env = &cpu->env;

    memory_region_init_ram(ram, NULL, "trilobite.sdram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(sysmem, 0x8000000, ram);

    cpu_intc = csky_intc_init_cpu(env);

    dev = sysbus_create_simple("csky_intc", 0x10010000, cpu_intc[0]);

    for (i = 0; i < 32; i++) {
        intc[i] = qdev_get_gpio_in(dev, i);
    }

    /* create uart */
    csky_uart_create(0x10015000,
                      intc[16], NULL,
                      serial_hd(0));

    csky_timer_set_freq(trilobite_binfo.freq);
    sysbus_create_varargs("csky_timer", 0x10011000, intc[12], intc[13],
                            intc[14], intc[15], NULL);

    csky_mac_create(0x10006000, intc[26]);

    sysbus_create_simple("csky_lcdc", 0x10004000, intc[28]);

    trilobite_binfo.ram_size = machine->ram_size;
    trilobite_binfo.kernel_filename = machine->kernel_filename;
    trilobite_binfo.kernel_cmdline = machine->kernel_cmdline;
    trilobite_binfo.initrd_filename = machine->initrd_filename;
    csky_load_kernel(cpu, &trilobite_binfo);
}

static void trilobite_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CSKY trilobite";
    mc->init = trilobite_init;
    mc->default_cpu_type = CSKY_CPU_TYPE_NAME("ck810f");
}

static const TypeInfo trilobite_type = {
    .name = MACHINE_TYPE_NAME("trilobite"),
    .parent = TYPE_MACHINE,
    .class_init = trilobite_class_init,
};

static void trilobite_machine_init(void)
{
    type_register_static(&trilobite_type);
}

type_init(trilobite_machine_init)
