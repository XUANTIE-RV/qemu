/*
 * RISCV SMARTH System emulation.
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "target/riscv/cpu.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "elf.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/virt.h"
#include "hw/intc/sifive_plic.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/char/csky_uart.h"
#include "hw/timer/csky_coret.h"
#include "hw/timer/csky_timer.h"
#include "hw/riscv/boot.h"

static uint64_t load_kernel(const char *kernel_filename)
{
    uint64_t kernel_entry = 0, kernel_low = 0, kernel_high = 0;

    if (load_elf(kernel_filename, NULL, NULL, NULL,
                 &kernel_entry, &kernel_low, &kernel_high,
                 0, 0, EM_RISCV, 1, 0) < 0) {
        error_report("qemu: could not load kernel '%s'", kernel_filename);
        exit(1);
    }
    return kernel_entry;
}

static void smarth_init(MachineState *machine)
{
    qemu_irq irqs[128];
    RISCVVirtState *s = g_new0(RISCVVirtState, 1);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    char *plic_hart_config;
    int i;
    unsigned int smp_cpus = machine->smp.cpus;
    uint64_t kernel_entry = 0;

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc[0],
                            TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->soc[0]), "cpu-type", machine->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc[0]),  "num-harts", smp_cpus,
                            &error_abort);
    if (machine->kernel_filename) {
        kernel_entry = load_kernel(machine->kernel_filename);
        object_property_set_uint(OBJECT(&s->soc[0]),  "resetvec",
                                 kernel_entry, &error_abort);

    }
    sysbus_realize(SYS_BUS_DEVICE(&s->soc[0]), &error_abort);
    s->soc[0].harts[0].env.elf_start = kernel_entry;

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "smarth.sdram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, 0, main_mem);

    /* create PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(smp_cpus);
    /* create PLIC */
    DeviceState *plic = sifive_plic_create(0x4000000000,
        plic_hart_config, smp_cpus, 0,
        127, /* NUM_SOURCES */
        7, /* NUM_PRIORITIES */
        0x0, /* PRIORITY_BASE */
        0x1000, /* PENDING_BASE */
        0x2000, /* ENABLE_BASE */
        0x80, /* ENABLE_STRIDE */
        0x200000, /* CONTEXT_BASE */
        0x1000, /* CONTEXT_STRIDE */
        0x4000000);
    g_free(plic_hart_config);
    for (i = 0; i < 127; i++) {
        irqs[i] = qdev_get_gpio_in(DEVICE(plic), i);
    }
    riscv_aclint_swi_create(0x4004000000, 0, smp_cpus, false, 0);
    riscv_aclint_swi_create(0x400400C000, 0, smp_cpus, true, 0x1000);
    riscv_aclint_mtimer_create(0x4004004000, RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                               0, smp_cpus, RISCV_ACLINT_DEFAULT_MTIMECMP,
                               RISCV_ACLINT_DEFAULT_MTIME,
                               RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
    riscv_aclint_mtimer_create(0x400400D000, 0x3000,
                               0, smp_cpus, RISCV_ACLINT_DEFAULT_MTIMECMP,
                               UINT32_MAX, RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ,
                               false);
    csky_uart_create(0x10015000, irqs[0x20], NULL, serial_hd(0));

    csky_timer_set_freq(50000000ll);
    sysbus_create_varargs("csky_timer", 0x10011000, irqs[0x22], irqs[0x23],
                            irqs[0x24], irqs[0x25], NULL);

    sysbus_create_simple("csky_exit", 0x10002000, NULL);

    g_free(plic_hart_config);
}

static void smarth_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V smarth";
    mc->init = smarth_init;
    mc->max_cpus = 8; /* hardcoded limit in BBL */
    mc->default_cpu_type = RISCV_CPU_TYPE_NAME("c910v");
}

static const TypeInfo smarth_type = {
    .name = MACHINE_TYPE_NAME("smarth"),
    .parent = TYPE_MACHINE,
    .class_init = smarth_class_init,
    .instance_size = sizeof(RISCVVirtState)
};

static void smarth_machine_init(void)
{
    type_register_static(&smarth_type);
}

type_init(smarth_machine_init)
