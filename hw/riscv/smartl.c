/*
 * RISCV SMARTL System emulation.
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
#include "hw/riscv/smartl.h"
#include "hw/intc/xt_clic.h"
#include "hw/intc/thead_clint.h"
#include "hw/char/csky_uart.h"
#include "hw/timer/csky_timer.h"

static const MemMapEntry smartl_memmap[] = {
    [SMARTL_SRAM0] =            {  0x00000000, 16 * MiB },
    [SMARTL_EXIT] =             {  0x10002000, 16 * KiB },
    [SMARTL_SRAM1] =            {  0x20000000, 16 * MiB },
    [SMARTL_TIMER] =            {  0x40011000, 16 * KiB },
    [SMARTL_UART] =             {  0x40015000, 4  * MiB },
    [SMARTL_TIMER2] =           {  0x40810000, 16 * KiB },
    [SMARTL_UART2] =            {  0x40850000, 4  * MiB },
    [SMARTL_SRAM2] =            {  0x50000000, 16 * MiB },
    [SMARTL_SRAM3] =            {  0x60000000, 16 * MiB },
    [SMARTL_PMU] =              {  0x6104FFF8, 1 * KiB },
    [SMARTL_CLINT] =            {  0xe0000000, 64 * KiB },
    [SMARTL_CLIC] =             {  0xe0800000, 20 * KiB },
    [SMARTL_SYSTEMMAP] =        {  0xeffff000, 0x40 },

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
    env->elf_start = kernel_entry;
    return kernel_entry;
}

static void
smartl_add_memory_subregion(MemoryRegion *sysmem, hwaddr base, hwaddr size,
                            const char *name)
{
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    memory_region_init_ram(ram, NULL, name, size, &error_fatal);
    memory_region_add_subregion(sysmem, base, ram);
}

static void smartl_machine_instance_init(Object *obj)
{
}

static void smartl_init(MachineState *machine)
{
    CPURISCVState *env = NULL;
    DeviceState *dev;
    RISCVSmartlState *s = RISCV_SMARTL_MACHINE(machine);
    MemoryRegion *sysmem = get_system_memory();
    qemu_irq *irqs = g_malloc0(sizeof(qemu_irq) *
                               machine->smp.cpus *
                               SMARTL_CLIC_IRQ_NUMS);
    qemu_irq *pirq = g_malloc0(sizeof(qemu_irq) * 2 * machine->smp.cpus);
    int i;

    /* Create cpu */
    for (i = 0; i < machine->smp.cpus; i++) {
        Object *cpuobj = object_new(machine->cpu_type);
        object_property_set_bool(cpuobj, "realized", true, &error_fatal);
        if (i == 0) {
            env = &RISCV_CPU(cpuobj)->env;
        } else {
            CPU(cpuobj)->halted = true;
        }
        s->harts[i] = RISCV_CPU(cpuobj);
        s->harts[i]->env.mhartid = i;
    }

    /* Add memory region */
    for (i = 0; i < 5; i++) {
        MemMapEntry map = smartl_memmap[SMARTL_SRAM0 + i];
        char *name = g_strdup_printf("riscv.csky.smartl.ram.%d", i);
        smartl_add_memory_subregion(sysmem, map.base, map.size, name);
        g_free(name);
    }

    /* Create CLIC */
    dev = xt_clic_create(smartl_memmap[SMARTL_CLIC].base, true,
                         machine->smp.cpus,
                         SMARTL_CLIC_IRQ_NUMS,
                         SMARTL_CLIC_INTCTLBITS);
    for (i = 0; i < machine->smp.cpus * SMARTL_CLIC_IRQ_NUMS; i++) {
        irqs[i] = qdev_get_gpio_in(dev, i);
    }

    /* Create CLINT */
    pirq[0] = irqs[3];
    pirq[1] = irqs[7];
    if (machine->smp.cpus > 1) {
        pirq[2] = irqs[SMARTL_CLIC_IRQ_NUMS + 3];
        pirq[3] = irqs[SMARTL_CLIC_IRQ_NUMS + 7];
    }
    thead_clint_create(smartl_memmap[SMARTL_CLINT].base, pirq,
                       machine->smp.cpus);

    /* Create CSKY UART */
    csky_uart_create(smartl_memmap[SMARTL_UART].base, irqs[0x10], NULL,
                     serial_hd(0));

    /* Create CSKY timer */
    csky_timer_set_freq(1000000000ll);
    sysbus_create_varargs("csky_timer",
                          smartl_memmap[SMARTL_TIMER].base, irqs[0x12], irqs[0x13],
                          irqs[0x14], irqs[0x15], NULL);
    if (machine->smp.cpus > 1) {
        sysbus_create_varargs("csky_timer",
                              smartl_memmap[SMARTL_TIMER2].base,
                              irqs[SMARTL_CLIC_IRQ_NUMS + 0x12],
                              irqs[SMARTL_CLIC_IRQ_NUMS + 0x13],
                              irqs[SMARTL_CLIC_IRQ_NUMS + 0x14],
                              irqs[SMARTL_CLIC_IRQ_NUMS + 0x15],
                              NULL);
        csky_uart_create(smartl_memmap[SMARTL_UART2].base,
                         irqs[SMARTL_CLIC_IRQ_NUMS + 0x10],
                         NULL,
                         serial_hd(0));
    }

    /* Create CSKY exit */
    sysbus_create_simple("csky_exit", smartl_memmap[SMARTL_EXIT].base, NULL);
    /* Create CSKY pmu */
    sysbus_create_simple("thead_pmu", smartl_memmap[SMARTL_PMU].base, NULL);

    if (machine->kernel_filename) {
        load_kernel(env, machine->kernel_filename);
    }
}

static void smartl_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V smartl";
    mc->init = smartl_init;
    mc->default_cpu_type = RISCV_CPU_TYPE_NAME("e906fdp");
    mc->max_cpus = 2;
}

static const TypeInfo smartl_type = {
    .name = MACHINE_TYPE_NAME("smartl"),
    .parent = TYPE_MACHINE,
    .class_init = smartl_class_init,
    .instance_init = smartl_machine_instance_init,
    .instance_size = sizeof(RISCVSmartlState),
};

static void smartl_machine_init(void)
{
    type_register_static(&smartl_type);
}

type_init(smartl_machine_init)
