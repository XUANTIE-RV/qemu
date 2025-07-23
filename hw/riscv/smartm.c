/*
 * RISCV SMARTM System emulation.
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
#include "hw/riscv/smartm.h"
#include "hw/intc/xt_clic.h"
#include "hw/intc/thead_clint.h"
#include "hw/char/csky_uart.h"
#include "hw/timer/csky_timer.h"

static const MemMapEntry smartm_memmap[] = {
    [SMARTM_SRAM0] =            {  0x00000000, 1 * MiB },
    [SMARTM_EXIT] =             {  0x10002000, 16 * KiB },
    [SMARTM_SRAM1] =            {  0x00100000, 512 * KiB },
    [SMARTM_TIMER] =            {  0x00181000, 4 * KiB },
    [SMARTM_UART] =             {  0x00180000, 4 * KiB },
    [SMARTM_TIMER2] =           {  0x00184000, 4 * KiB },
    [SMARTM_SRAM2] =            {  0x001d0000, 192 * KiB },
    [SMARTM_PMU] =              {  0x00182000, 4 * KiB },
    [SMARTM_CLINT] =            {  0xe0000000, 64 * KiB },
    [SMARTM_CLIC] =             {  0xe0800000, 20 * KiB },
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
smartm_add_memory_subregion(MemoryRegion *sysmem, hwaddr base, hwaddr size,
                            const char *name)
{
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    memory_region_init_ram(ram, NULL, name, size, &error_fatal);
    memory_region_add_subregion(sysmem, base, ram);
}

static void smartm_machine_instance_init(Object *obj)
{
}

static void smartm_init(MachineState *machine)
{
    CPURISCVState *env = NULL;
    DeviceState *dev;
    RISCVSmartlState *s = RISCV_SMARTM_MACHINE(machine);
    MemoryRegion *sysmem = get_system_memory();
    qemu_irq *irqs = g_malloc0(sizeof(qemu_irq) *
                               machine->smp.cpus *
                               SMARTM_CLIC_IRQ_NUMS);
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
    for (i = 0; i < 4; i++) {
        MemMapEntry map = smartm_memmap[SMARTM_SRAM0 + i];
        char *name = g_strdup_printf("riscv.csky.smartm.ram.%d", i);
        smartm_add_memory_subregion(sysmem, map.base, map.size, name);
        g_free(name);
    }

    /* Create CLIC */
    dev = xt_clic_create(smartm_memmap[SMARTM_CLIC].base, true,
                         machine->smp.cpus,
                         SMARTM_CLIC_IRQ_NUMS,
                         SMARTM_CLIC_INTCTLBITS);
    for (i = 0; i < machine->smp.cpus * SMARTM_CLIC_IRQ_NUMS; i++) {
        irqs[i] = qdev_get_gpio_in(dev, i);
    }

    /* Create CLINT */
    pirq[0] = irqs[3];
    pirq[1] = irqs[7];
    if (machine->smp.cpus > 1) {
        pirq[2] = irqs[SMARTM_CLIC_IRQ_NUMS + 3];
        pirq[3] = irqs[SMARTM_CLIC_IRQ_NUMS + 7];
    }
    thead_clint_create(smartm_memmap[SMARTM_CLINT].base, pirq,
                       machine->smp.cpus);

    /* Create CSKY UART */
    csky_uart_create(smartm_memmap[SMARTM_UART].base, irqs[0x10], NULL,
                     serial_hd(0));

    /* Create CSKY timer */
    csky_timer_set_freq(1000000000ll);
    csky_timer_create(smartm_memmap[SMARTM_TIMER].base, &irqs[0x12], NULL,
                      machine->smp.cpus, 0);
    if (machine->smp.cpus > 1) {
        csky_timer_create(smartm_memmap[SMARTM_TIMER2].base,
                          &irqs[SMARTM_CLIC_IRQ_NUMS + 0x12], NULL,
                          machine->smp.cpus, 0);
    }

    /* Create CSKY exit */
    sysbus_create_simple("csky_exit", smartm_memmap[SMARTM_EXIT].base, NULL);
    /* Create CSKY pmu */
    sysbus_create_simple("thead_pmu", smartm_memmap[SMARTM_PMU].base, NULL);

    if (machine->kernel_filename) {
        load_kernel(env, machine->kernel_filename);
    }
    g_free(irqs);
    g_free(pirq);
}

static void smartm_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V smartm";
    mc->init = smartm_init;
    mc->default_cpu_type = RISCV_CPU_TYPE_NAME("e906fdp");
    mc->max_cpus = 2;
}

static const TypeInfo smartm_type = {
    .name = MACHINE_TYPE_NAME("smartm"),
    .parent = TYPE_MACHINE,
    .class_init = smartm_class_init,
    .instance_init = smartm_machine_instance_init,
    .instance_size = sizeof(RISCVSmartlState),
};

static void smartm_machine_init(void)
{
    type_register_static(&smartm_type);
}

type_init(smartm_machine_init)
