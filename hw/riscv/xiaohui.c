/*
 * RISCV Xiaohui System emulation.
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
#include "qemu/units.h"
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
#include "hw/intc/sifive_plic.h"
#include "hw/misc/sifive_test.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/xt_clic.h"
#include "hw/char/csky_uart.h"
#include "hw/timer/csky_timer.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/riscv/xiaohui.h"
#include "sysemu/device_tree.h"
#include <libfdt.h>

#define HI_CELL(cell)  ((uint64_t)cell >> 32)
#define LOW_CELL(cell) ((uint64_t)cell & UINT32_MAX)

enum {
    XIAOHUI_PLIC_NUM_SOURCES = 1023,
    XIAOHUI_PLIC_NUM_PRIORITIES = 32,
    XIAOHUI_PLIC_PRIORITY_BASE = 0x0,
    XIAOHUI_PLIC_PENDING_BASE = 0x1000,
    XIAOHUI_PLIC_ENABLE_BASE = 0x2000,
    XIAOHUI_PLIC_ENABLE_STRIDE = 0x80,
    XIAOHUI_PLIC_CONTEXT_BASE = 0x200000,
    XIAOHUI_PLIC_CONTEXT_STRIDE = 0x1000,
};

typedef struct RISCVXiaohuiState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    RISCVHartArrayState soc;
    bool boot_linux;

    int fdt_size;
} RISCVXiaohuiState;

#define TYPE_XIAOHUI_MACHINE MACHINE_TYPE_NAME("xiaohui")
DECLARE_INSTANCE_CHECKER(RISCVXiaohuiState, RISCV_XIAOHUI_MACHINE,
                         TYPE_XIAOHUI_MACHINE)
const MemMapEntry xiaohui_memmap[] = {
     [XIAOHUI_SRAM]         = { 0x0000000000,    1 * MiB },
     [XIAOHUI_PLIC]         = { 0x0008000000,   64 * MiB },
     [XIAOHUI_CLINT]        = { 0x000c000000,   64 * KiB },
     [XIAOHUI_CLIC]         = { 0x000c010000,   20 * KiB },
     [XIAOHUI_AHB_CPR]      = { 0x0018030000,   64 * KiB },
     [XIAOHUI_TIMER]        = { 0x0019001000,    4 * KiB },
     [XIAOHUI_UART0]        = { 0x001900d000,    4 * KiB },
     /* Using the simulation2 for exit qemu */
     [XIAOHUI_TEST]         = { 0x004c000000,    4 * KiB },
     [XIAOHUI_DRAM]         = { 0x0050000000,    0 },
};

static void *create_fdt(RISCVXiaohuiState *s, const struct MemMapEntry *mmap,
                        uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int cpu;
    uint32_t *cells;
    uint32_t aclint_cells_size;
    uint32_t *aclint_mswi_cells;
    uint32_t *aclint_sswi_cells;
    uint32_t *aclint_mtimer_cells;
    char *nodename;
    uint32_t plic_phandle, phandle = 1;
    MachineState *mc = MACHINE(s);

    fdt = mc->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_string(fdt, "/", "compatible", "csky,xiaohui");

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    nodename = g_strdup_printf("/memory@%lx", (long)mmap[XIAOHUI_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           mmap[XIAOHUI_DRAM].base >> 32,
                           mmap[XIAOHUI_DRAM].base,
                           mem_size >> 32,
                           mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    /* cpus node */
    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        int intc_phandle = phandle;
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa = riscv_isa_string(&s->soc.harts[cpu]);
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv39");
        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", intc_phandle);
        g_free(intc);
        g_free(nodename);
    }

    plic_phandle = ++phandle;
    cells = g_new0(uint32_t, s->soc.num_harts * 4);
    for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
                               (long)mmap[XIAOHUI_PLIC].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended", cells,
                     s->soc.num_harts * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           HI_CELL(mmap[XIAOHUI_PLIC].base),
                           LOW_CELL(mmap[XIAOHUI_PLIC].base),
                           HI_CELL(mmap[XIAOHUI_PLIC].size),
                           LOW_CELL(mmap[XIAOHUI_PLIC].size));
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,max-priority",
                  XIAOHUI_PLIC_NUM_PRIORITIES);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev",
                  XIAOHUI_PLIC_NUM_SOURCES);
    qemu_fdt_setprop_cells(fdt, nodename, "phandle", plic_phandle);
    plic_phandle = qemu_fdt_get_phandle(fdt, nodename);
    g_free(nodename);


    /* aclint node, mtimer, mswi and sswi node */
    unsigned long addr, size;
    aclint_mtimer_cells = g_new0(uint32_t, s->soc.num_harts * 2);
    aclint_mswi_cells = g_new0(uint32_t, s->soc.num_harts * 2);
    aclint_sswi_cells = g_new0(uint32_t, s->soc.num_harts * 2);

    for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        aclint_mtimer_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandle);
        aclint_mtimer_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_TIMER);
        aclint_mswi_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandle);
        aclint_mswi_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_SOFT);
        aclint_sswi_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandle);
        aclint_sswi_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_SOFT);
        g_free(nodename);
    }
    aclint_cells_size = s->soc.num_harts * sizeof(uint32_t) * 2;

    addr = mmap[XIAOHUI_CLINT].base;
    nodename = g_strdup_printf("/soc/mswi@%lx", addr);
    qemu_fdt_add_subnode(mc->fdt, nodename);
    qemu_fdt_setprop_string(mc->fdt, nodename, "compatible",
        "riscv,aclint-mswi");
    qemu_fdt_setprop_cells(mc->fdt, nodename, "reg",
        0x0, addr, 0x0, RISCV_ACLINT_SWI_SIZE);
    qemu_fdt_setprop(mc->fdt, nodename, "interrupts-extended",
        aclint_mswi_cells, aclint_cells_size);
    qemu_fdt_setprop(mc->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(mc->fdt, nodename, "#interrupt-cells", 0);
    riscv_socket_fdt_write_id(mc, nodename, 0);
    g_free(nodename);

    addr = mmap[XIAOHUI_CLINT].base + 0x4000;
    size = RISCV_ACLINT_DEFAULT_MTIMER_SIZE;
    nodename = g_strdup_printf("/soc/mtimer@%lx", addr);
    qemu_fdt_add_subnode(mc->fdt, nodename);
    qemu_fdt_setprop_string(mc->fdt, nodename, "compatible",
        "riscv,aclint-mtimer");
    qemu_fdt_setprop_cells(mc->fdt, nodename, "reg",
        0x0, addr + RISCV_ACLINT_DEFAULT_MTIME,
        0x0, size - RISCV_ACLINT_DEFAULT_MTIME,
        0x0, addr + RISCV_ACLINT_DEFAULT_MTIMECMP,
        0x0, RISCV_ACLINT_DEFAULT_MTIME);
    qemu_fdt_setprop(mc->fdt, nodename, "interrupts-extended",
        aclint_mtimer_cells, aclint_cells_size);
    riscv_socket_fdt_write_id(mc, nodename, 0);
    g_free(nodename);

    addr = mmap[XIAOHUI_CLINT].base + 0xC000;
    nodename = g_strdup_printf("/soc/sswi@%lx", addr);
    qemu_fdt_add_subnode(mc->fdt, nodename);
    qemu_fdt_setprop_string(mc->fdt, nodename, "compatible",
        "riscv,aclint-sswi");
    qemu_fdt_setprop_cells(mc->fdt, nodename, "reg",
        0x0, addr, 0x0, RISCV_ACLINT_SWI_SIZE);
    qemu_fdt_setprop(mc->fdt, nodename, "interrupts-extended",
        aclint_sswi_cells, aclint_cells_size);
    qemu_fdt_setprop(mc->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(mc->fdt, nodename, "#interrupt-cells", 0);
    riscv_socket_fdt_write_id(mc, nodename, 0);
    g_free(nodename);
    g_free(aclint_mswi_cells);
    g_free(aclint_mtimer_cells);
    g_free(aclint_sswi_cells);

    nodename = g_strdup_printf("/soc/serial@%lx",
                               (long)mmap[XIAOHUI_UART0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           HI_CELL(mmap[XIAOHUI_UART0].base),
                           LOW_CELL(mmap[XIAOHUI_UART0].base),
                           HI_CELL(mmap[XIAOHUI_UART0].size),
                           LOW_CELL(mmap[XIAOHUI_UART0].size));
    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", nodename);

    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0x14);
    qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency", 0x2255100);
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", 0x3);
    qemu_fdt_setprop_string(fdt, nodename, "clock-names", "baudclk");
    qemu_fdt_setprop_cells(fdt, nodename, "reg-shift", 0x2);
    qemu_fdt_setprop_cells(fdt, nodename, "reg-io-width", 0x4);
    g_free(nodename);


    return fdt;
}

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

static void xiaohui_init(MachineState *machine)
{
    qemu_irq irqs[128];
    qemu_irq clic_irqs[128];
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    char *plic_hart_config;
    int i;
    unsigned int smp_cpus = machine->smp.cpus;
    uint64_t kernel_entry = 0;
    uint32_t fdt_load_addr;
    target_ulong firmware_end_addr, kernel_start_addr;

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->soc), "cpu-type", machine->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc),  "num-harts", smp_cpus,
                            &error_abort);

    if (!s->boot_linux && machine->kernel_filename) {
        kernel_entry = load_kernel(machine->kernel_filename);
        object_property_set_uint(OBJECT(&s->soc),  "resetvec",
                                 kernel_entry, &error_abort);
    }
    object_property_set_bool(OBJECT(&s->soc),  "cpu-off", true, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_abort);
    s->soc.harts[0].env.elf_start = kernel_entry;

    /* Register SRAM */
    memory_region_init_ram(sram, NULL, "xiaohui.sram",
                           xiaohui_memmap[XIAOHUI_SRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                xiaohui_memmap[XIAOHUI_SRAM].base, sram);

    /* Register DRAM */
    memory_region_add_subregion(system_memory,
                                xiaohui_memmap[XIAOHUI_DRAM].base,
                                machine->ram);

    /* Create CLIC */
    DeviceState *clic = xt_clic_create(xiaohui_memmap[XIAOHUI_CLIC].base, true,
                         machine->smp.cpus,
                         XIAOHUI_CLIC_IRQ_NUMS,
                         XIAOHUI_CLIC_INTCTLBITS);


    /* create PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(smp_cpus);
    /* create PLIC */
    DeviceState *plic = sifive_plic_create(xiaohui_memmap[XIAOHUI_PLIC].base,
        plic_hart_config, smp_cpus, 0,
        XIAOHUI_PLIC_NUM_SOURCES,
        XIAOHUI_PLIC_NUM_PRIORITIES,
        XIAOHUI_PLIC_PRIORITY_BASE,
        XIAOHUI_PLIC_PENDING_BASE,
        XIAOHUI_PLIC_ENABLE_BASE,
        XIAOHUI_PLIC_ENABLE_STRIDE,
        XIAOHUI_PLIC_CONTEXT_BASE,
        XIAOHUI_PLIC_CONTEXT_STRIDE,
        xiaohui_memmap[XIAOHUI_PLIC].size);
    g_free(plic_hart_config);
    for (i = 0; i < 127; i++) {
        irqs[i] = qdev_get_gpio_in(DEVICE(plic), i);
        clic_irqs[i] = qdev_get_gpio_in(DEVICE(clic), i);
    }
    riscv_aclint_swi_create(xiaohui_memmap[XIAOHUI_CLINT].base, 0, smp_cpus,
                            false, 0);
    riscv_aclint_swi_create(xiaohui_memmap[XIAOHUI_CLINT].base + 0xC000, 0,
                            smp_cpus, true, 0x1000);
    riscv_aclint_mtimer_create(xiaohui_memmap[XIAOHUI_CLINT].base + 0x4000,
                               RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                               0, smp_cpus, RISCV_ACLINT_DEFAULT_MTIMECMP,
                               RISCV_ACLINT_DEFAULT_MTIME,
                               XIAOHUI_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
    riscv_aclint_mtimer_create(xiaohui_memmap[XIAOHUI_CLINT].base + 0xD000,
                               0x3000,
                               0, smp_cpus, RISCV_ACLINT_DEFAULT_MTIMECMP,
                               UINT32_MAX, XIAOHUI_ACLINT_DEFAULT_TIMEBASE_FREQ,
                               false);
    csky_uart_create(xiaohui_memmap[XIAOHUI_UART0].base, irqs[20],
                     clic_irqs[20], serial_hd(0));
    if (s->boot_linux) {
        sifive_test_create(xiaohui_memmap[XIAOHUI_TEST].base);
        create_fdt(s, xiaohui_memmap, machine->ram_size,
                   machine->kernel_cmdline);
        const char *firmware_name = riscv_default_firmware_name(&s->soc);
        firmware_end_addr =
        riscv_find_and_load_firmware(machine, firmware_name,
                                     xiaohui_memmap[XIAOHUI_DRAM].base, NULL);
        kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc,
                                                        firmware_end_addr);
        kernel_entry = riscv_load_kernel(machine, &s->soc,
                                         kernel_start_addr, true, NULL);
        s->soc.harts[0].env.elf_start = kernel_entry;
        fdt_load_addr = riscv_compute_fdt_addr(
                        xiaohui_memmap[XIAOHUI_DRAM].base,
                        xiaohui_memmap[XIAOHUI_DRAM].size,
                        machine);
        riscv_load_fdt(fdt_load_addr, machine->fdt);
        riscv_setup_rom_reset_vec(machine, &s->soc,
                                  xiaohui_memmap[XIAOHUI_DRAM].base,
                                  xiaohui_memmap[XIAOHUI_SRAM].base,
                                  xiaohui_memmap[XIAOHUI_SRAM].size,
                                  kernel_entry, fdt_load_addr);
    } else {
        sysbus_create_simple("csky_exit", xiaohui_memmap[XIAOHUI_TEST].base,
                             NULL);
        csky_timer_set_freq(XIAOHUI_TIMER_DEFAULT_TIMEBASE_FREQ);
        sysbus_create_varargs("csky_timer", xiaohui_memmap[XIAOHUI_TIMER].base,
                              irqs[25], irqs[26], irqs[27], irqs[28],
                              clic_irqs[25], clic_irqs[26], clic_irqs[27],
                              clic_irqs[28], NULL);
        sysbus_create_simple("xiaohui_cpr",
                             xiaohui_memmap[XIAOHUI_AHB_CPR].base, NULL);
    }
}

static bool xiaohui_get_linux(Object *obj, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);
    return s->boot_linux;
}

static void xiaohui_set_linux(Object *obj, bool value, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);

    s->boot_linux = value;
}

static void xiaohui_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V xiaohui";
    mc->default_ram_id = "riscv.xiaohui.dram";
    mc->init = xiaohui_init;
    mc->max_cpus = 8; /* hardcoded limit in BBL */
    mc->default_cpu_type = RISCV_CPU_TYPE_NAME("c907fdvm");

    object_class_property_add_bool(oc, "linux", xiaohui_get_linux,
                                   xiaohui_set_linux);
    object_class_property_set_description(oc, "linux",
                                          "Set on/off to show whether to "
                                          "boot linux or not");
}

static const TypeInfo xiaohui_type = {
    .name = MACHINE_TYPE_NAME("xiaohui"),
    .parent = TYPE_MACHINE,
    .class_init = xiaohui_class_init,
    .instance_size = sizeof(RISCVXiaohuiState)
};

static void xiaohui_machine_init(void)
{
    type_register_static(&xiaohui_type);
}

type_init(xiaohui_machine_init)
