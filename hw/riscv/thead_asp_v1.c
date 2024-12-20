/*
 * RISCV ASP V1 System emulation.
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

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "target/riscv/cpu.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "elf.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/thead_asp_v1.h"
#include "hw/intc/riscv_clic.h"
#include "hw/intc/thead_clint.h"
#include "hw/char/csky_uart.h"
#include "hw/timer/csky_timer.h"

static const MemMapEntry asp_memmap[] = {
    [ASP_SRAM0] =            {  0x00000000, 16 * MiB },
    [ASP_EXIT] =             {  0x10002000, 16 * KiB },
    [ASP_SRAM1] =            {  0x20000000, 16 * MiB },
    [ASP_TIMER] =            {  0x40011000, 16 * KiB },
    [ASP_UART] =             {  0x40015000, 4  * MiB },
    [ASP_NNE] =              {  0x4001d000,  1 * KiB },
    [ASP_SRAM2] =            {  0x50000000, 16 * MiB },
    [ASP_SRAM3] =            {  0x60000000, 16 * MiB },
    [ASP_SRAM_A] =           {  0x80000000, 32 * KiB },
    [ASP_SRAM_B] =           {  0x80008000, 32 * KiB },
    [ASP_SRAM_C] =           {  0x80010000, 32 * KiB },
    [ASP_SRAM_D] =           {  0x80018000, 32 * KiB },
    [ASP_CLINT] =            {  0xe0000000, 64 * KiB },
    [ASP_CLIC] =             {  0xe0800000, 20 * KiB },
    [ASP_SYSTEMMAP] =        {  0xeffff000, 0x40 },

};

static uint64_t load_kernel(CPURISCVState *env, const char *kernel_filename)
{
    uint64_t kernel_entry = 0, kernel_low = 0, kernel_high = 0;

    if (load_elf(kernel_filename, NULL, NULL, NULL,
                 &kernel_entry, &kernel_low, &kernel_high,
                 0, 0, EM_RISCV, 1, 0) < 0) {
        error_report("qemu: could not load kernel '%s'", kernel_filename);
        exit(1);
    }
    env->pc = (uint32_t)kernel_entry;
    return kernel_entry;
}

static void
asp_add_memory_subregion(MemoryRegion *sysmem, hwaddr base, hwaddr size,
                            const char *name)
{
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    memory_region_init_ram(ram, NULL, name, size, &error_fatal);
    memory_region_add_subregion(sysmem, base, ram);
}

static void thead_asp_v1_init(MachineState *machine)
{
    CPURISCVState *env;
    DeviceState *dev;
    SysBusDevice *s;
    MemoryRegion *sysmem = get_system_memory();
    qemu_irq *irqs = g_malloc0(sizeof(qemu_irq) * ASP_CLIC_IRQ_NUMS);
    qemu_irq *pirq = g_malloc0(sizeof(qemu_irq) * 2);
    int i;

    /* Create cpu */
    Object *cpuobj = object_new(machine->cpu_type);
    object_property_set_bool(cpuobj, "realized", true, &error_fatal);
    env = &RISCV_CPU(cpuobj)->env;

    /* Add memory region */
    for (i = 0; i < 9; i++) {
        MemMapEntry map = asp_memmap[ASP_SRAM0 + i];
        char *name = g_strdup_printf("riscv.csky.smartl.ram.%d", i);
        asp_add_memory_subregion(sysmem, map.base, map.size, name);
        g_free(name);
    }

    /* Create CLIC */
    dev = riscv_clic_create(asp_memmap[ASP_CLIC].base,
                            false, false, true,
                            ASP_CLIC_HARTS,
                            ASP_CLIC_IRQ_NUMS,
                            ASP_CLIC_INTCTLBITS,
                            ASP_CLIC_VERSION);
    for (i = 0; i < ASP_CLIC_IRQ_NUMS; i++) {
        irqs[i] = qdev_get_gpio_in(dev, i);
    }

    /* Create CLINT */
    pirq[0] = irqs[3];
    pirq[1] = irqs[7];
    thead_clint_create(asp_memmap[ASP_CLINT].base, pirq, 1);

    /* Create CSKY UART */
    csky_uart_create(asp_memmap[ASP_UART].base, irqs[0x10], NULL, serial_hd(0));

    /* Create CSKY timer */
    csky_timer_set_freq(1000000000ll);
    sysbus_create_varargs("csky_timer",
                          asp_memmap[ASP_TIMER].base, irqs[0x12], irqs[0x13],
                          irqs[0x14], irqs[0x15], NULL);

    /* Create CSKY ASP-NNE V1 */
    dev = qdev_new("thead_asp_nne_v1");
    qdev_prop_set_uint32(dev, "sram_a_addr", asp_memmap[ASP_SRAM_A].base);
    qdev_prop_set_uint32(dev, "sram_b_addr", asp_memmap[ASP_SRAM_B].base);
    qdev_prop_set_uint32(dev, "sram_c_addr", asp_memmap[ASP_SRAM_C].base);
    qdev_prop_set_uint32(dev, "cmd_addr", asp_memmap[ASP_SRAM_D].base);

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, asp_memmap[ASP_NNE].base);
    sysbus_connect_irq(s, 0, irqs[0x16]);
    sysbus_connect_irq(s, 1, irqs[0x17]);


    /* Create CSKY exit */
    sysbus_create_simple("csky_exit", asp_memmap[ASP_EXIT].base, NULL);

    if (machine->kernel_filename) {
        load_kernel(env, machine->kernel_filename);
    }
}

static void thead_asp_v1_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V asp_v1";
    mc->init = thead_asp_v1_init;
    mc->default_cpu_type = RISCV_CPU_TYPE_NAME("e906fdp");
}

static const TypeInfo thead_asp_v1_type = {
    .name = MACHINE_TYPE_NAME("thead_asp_v1"),
    .parent = TYPE_MACHINE,
    .class_init = thead_asp_v1_class_init,
};

static void thead_asp_v1_machine_init(void)
{
    type_register_static(&thead_asp_v1_type);
}

type_init(thead_asp_v1_machine_init)
