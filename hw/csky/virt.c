/*
 * CSKY virt System emulation.
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
#include "qemu/option.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "target/csky/cpu.h"
#include "hw/csky/csky_boot.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "qemu/datadir.h"
#include "hw/char/csky_uart.h"
#include "hw/timer/csky_mptimer.h"
#include "hw/timer/csky_timer.h"
#include "hw/intc/csky_intc.h"
#include "hw/intc/csky_mic.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"

#include <libfdt.h>
static struct csky_boot_info virt_binfo = {
    .loader_start   = 0,
#ifdef TARGET_CSKYV2
    .dtb_addr       = 0x8f000000,
#else
    .dtb_addr       = 0x9f000000,
#endif
    .magic          = 0x20150401,
    .freq           = 50000000ll,
};

enum {
    VIRT_PCIE,
    VIRT_PCIE_MMIO,
    VIRT_PCIE_PIO,
    VIRT_PCIE_ECAM,
};

static const struct MemMapEntry virt_memmap[] = {
    [VIRT_PCIE_MMIO] =  {0xd0000000, 0x10000000},
    [VIRT_PCIE_ECAM] =  {0xe0000000, 0x01000000},
    [VIRT_PCIE_PIO] =   {0xe1000000, 0x00010000},
};

static DeviceState *micdev;

#define FDT_PCI_ADDR_CELLS    3
#define FDT_PCI_INT_CELLS     1

static void create_pcie_irq_map(void *fdt, char *nodename, uint32_t phandle)
{
    uint32_t full_irq_map[4 * 4 * 6] = {0};
    uint32_t *irq_map = full_irq_map;
    int devfn, pin;

    for (devfn = 0; devfn <= 0x18; devfn += 0x8) {
        for (pin = 0; pin < 4; pin++) {
            int irq_nr = 60 + ((pin + PCI_SLOT(devfn)) % PCI_NUM_PINS);
            int i;

            uint32_t map[] = {
                devfn << 8, 0, 0,     /* devfn */
                pin + 1,              /* PCI pin */
                phandle, irq_nr};     /* GIC irq */

            /* Convert map to big endian */
            for (i = 0; i < 6; i++) {
                irq_map[i] = cpu_to_be32(map[i]);
            }
            irq_map += 6;
        }
    }

    qemu_fdt_setprop(fdt, nodename, "interrupt-map", full_irq_map,
                     sizeof(full_irq_map));

    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-map-mask", 0x1800, 0, 0,
                           0x7);
}

static void *create_smp_fdt(MachineState *machine)
{
    void *fdt;
    int i, cpu;
    char *nodename;
    int fdt_size;
    uint32_t phandle;

    fdt = create_device_tree(&fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "csky ck860_platform");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "csky,ck860_platform");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x1);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x1);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x1);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x1);

    qemu_fdt_add_subnode(fdt, "/aliases");
    qemu_fdt_setprop_string(fdt, "/aliases", "serial0", "/soc/serial@ffffe000");

    nodename = g_strdup_printf("/memory");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0x0, machine->ram_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = machine->smp.cpus - 1; cpu >= 0; cpu--) {
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "status", "ok");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        g_free(nodename);
    }

    nodename = g_strdup_printf("/soc/interrupt-controller");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "csky,mpintc");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cells(fdt, nodename, "phandle", 0x2);
    phandle = qemu_fdt_get_phandle(fdt, nodename);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/timer");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "csky,mptimer");
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", 0x1);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0x10);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", 0x2);
    g_free(nodename);


    nodename = g_strdup_printf("/soc/serial@ffffe000");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0xffffe000, 0x1000);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", 0x2);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0x2b);
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", 0x1);
    qemu_fdt_setprop_string(fdt, nodename, "clock-names", "baudclk");
    qemu_fdt_setprop_cells(fdt, nodename, "reg-shift", 0x2);
    qemu_fdt_setprop_cells(fdt, nodename, "reg-io-width", 0x4);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/apb-clock");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "fixed-clock");
    qemu_fdt_setprop_cells(fdt, nodename, "clock-frequency", 0x2faf080);
    qemu_fdt_setprop_string(fdt, nodename, "clock-output-names", "dummy_apb");
    qemu_fdt_setprop_cells(fdt, nodename, "#clock-cells", 0x0);
    qemu_fdt_setprop_cells(fdt, nodename, "phandle", 0x1);
    g_free(nodename);

   for (i = 0; i < 8; i++) {
        nodename = g_strdup_printf("/soc/virtio_mmio@%x",
            0xffff0000 + i * 0x1000);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "virtio,mmio");
        qemu_fdt_setprop_cells(fdt, nodename, "reg",
            0xffff0000 + i * 0x1000, 0x1000);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", 0x2);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 50 + i);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/pci@%lx",
                               (long) virt_memmap[VIRT_PCIE_ECAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "#address-cells",
                           FDT_PCI_ADDR_CELLS);
    qemu_fdt_setprop_cells(fdt, nodename, "#interrupt-cells",
                           FDT_PCI_INT_CELLS);
    qemu_fdt_setprop_cells(fdt, nodename, "#size-cells", 0x2);
    qemu_fdt_setprop_string(fdt, nodename, "compatible",
                            "pci-host-ecam-generic");
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "pci");
    qemu_fdt_setprop_cell(fdt, nodename, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(fdt, nodename, "bus-range", 0,
                           virt_memmap[VIRT_PCIE_ECAM].base /
                           PCIE_MMCFG_SIZE_MIN - 1);
    qemu_fdt_setprop(fdt, nodename, "dma-coherent", NULL, 0);
    qemu_fdt_setprop_cells(fdt, nodename, "reg", virt_memmap[VIRT_PCIE_ECAM].base,
                           virt_memmap[VIRT_PCIE_ECAM].size);
    qemu_fdt_setprop_sized_cells(fdt, nodename, "ranges",
        1, FDT_PCI_RANGE_IOPORT, 2, 0,
        1, virt_memmap[VIRT_PCIE_PIO].base, 2, virt_memmap[VIRT_PCIE_PIO].size,
        1, FDT_PCI_RANGE_MMIO,
        2, virt_memmap[VIRT_PCIE_MMIO].base,
        1, virt_memmap[VIRT_PCIE_MMIO].base, 2, virt_memmap[VIRT_PCIE_MMIO].size);
    create_pcie_irq_map(fdt, nodename, phandle);
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/chosen");
    if (strcmp(machine->kernel_cmdline, "")) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
            machine->kernel_cmdline);
    } else {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
        "console=ttyS0,115200 \
         rdinit=/sbin/init root=/dev/ram0 \
         clk_ignore_unused loglevel=7");
    }
    return fdt;
}

static void *create_fdt(MachineState *machine)
{
    void *fdt;
    int i, cpu;
    char *nodename;
    int fdt_size;

    fdt = create_device_tree(&fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "csky");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "csky");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x1);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x1);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x1);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x1);

    nodename = g_strdup_printf("/memory");
    qemu_fdt_add_subnode(fdt, nodename);
#ifdef TARGET_CSKYV2
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0x0, machine->ram_size);
#else
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0x10000000, machine->ram_size - 0x10000000);
    virt_binfo.loader_start = 0x10000000;
#endif
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x0);

    for (cpu = machine->smp.cpus - 1; cpu >= 0; cpu--) {
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "csky");
        qemu_fdt_setprop_cell(fdt, nodename, "ccr", 0x417d);
        qemu_fdt_setprop_cell(fdt, nodename, "hint", 0xe);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        g_free(nodename);
    }

    nodename = g_strdup_printf("/soc/interrupt-controller");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "csky,apb-intc");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0xfffff000, 0x1000);
    qemu_fdt_setprop_cells(fdt, nodename, "phandle", 0x2);
    qemu_fdt_setprop_cells(fdt, nodename, "linux,phandle", 0x2);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/timer0");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "snps,dw-apb-timer");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0xffffd000, 0x1000);
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", 0x1);
    qemu_fdt_setprop_string(fdt, nodename, "clock-names", "timer");
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0x1);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", 0x2);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/timer1");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "snps,dw-apb-timer");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0xffffd014, 0x800);
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", 0x1);
    qemu_fdt_setprop_string(fdt, nodename, "clock-names", "timer");
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0x2);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", 0x2);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/serial0");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0xffffe000, 0x1000);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", 0x2);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0x3);
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", 0x1);
    qemu_fdt_setprop_cells(fdt, nodename, "baud", 0x1c200);
    qemu_fdt_setprop_cells(fdt, nodename, "reg-shift", 0x2);
    qemu_fdt_setprop_cells(fdt, nodename, "reg-io-width", 0x1);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/apb-clock");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "fixed-clock");
    qemu_fdt_setprop_cells(fdt, nodename, "clock-frequency", 0x2625a00);
    qemu_fdt_setprop_string(fdt, nodename, "clock-output-names", "dummy_apb");
    qemu_fdt_setprop_cells(fdt, nodename, "#clock-cells", 0x0);
    qemu_fdt_setprop_cells(fdt, nodename, "linux,phandle", 0x1);
    qemu_fdt_setprop_cells(fdt, nodename, "phandle", 0x1);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/qemu-exit");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "csky,qemu-exit");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0xffffc000, 0x1000);
    g_free(nodename);

    for (i = 0; i < 8; i++) {
        nodename = g_strdup_printf("/soc/virtio_mmio%x",
            0xffff0000 + i * 0x1000);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "virtio,mmio");
        qemu_fdt_setprop_cells(fdt, nodename, "reg",
            0xffff0000 + i * 0x1000, 0x1000);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", 0x2);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 10 + i);
        g_free(nodename);
    }

    qemu_fdt_add_subnode(fdt, "/chosen");
    if (strcmp(machine->kernel_cmdline, "")) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
            machine->kernel_cmdline);
    } else {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
            "console=ttyS0,115200 rdinit=/sbin/init root=/dev/ram0");
    }
    return fdt;
}

static bool get_ramsize_from_dtb(unsigned int *ram_size, unsigned int *base)
{
    void *fdt = NULL;
    int size;
    int lenp = 0;
    unsigned int *res;
    if (virt_binfo.dtb_filename) {
        char *filename;
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, virt_binfo.dtb_filename);
        if (!filename) {
            fprintf(stderr, "Couldn't open dtb file %s\n",
                    virt_binfo.dtb_filename);
            return false;
        }

        fdt = load_device_tree(filename, &size);
        if (!fdt) {
            fprintf(stderr, "Couldn't open dtb file %s\n", filename);
            g_free(filename);
            return false;
        }
        g_free(filename);
    } else {
        fprintf(stderr, "Board was unable to create a dtb blob\n");
        return false;
    }

    qemu_fdt_dumpdtb(fdt, size);

    /* get reg<base size> to point "res" */
    res = (unsigned int *)
        qemu_fdt_getprop(fdt, "/memory", "reg", &lenp, &error_fatal);

    if (lenp == 8) {
        *base = __builtin_bswap32(*res);
        *ram_size = __builtin_bswap32(*(res + 1));
        return true;
    } else {
        return false;
    }
}

static void gpex_pcie_init(void)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *mmio_reg;
    hwaddr ecam_base, ecam_size;
    hwaddr mmio_base, mmio_size;
    hwaddr pio_base;
    qemu_irq irq;
    int i;

    dev = qdev_new(TYPE_GPEX_HOST);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* ECAM space */
    ecam_base = virt_memmap[VIRT_PCIE_ECAM].base;
    ecam_size = virt_memmap[VIRT_PCIE_ECAM].size;
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);

    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam", ecam_reg,
                             0, ecam_size);
    memory_region_add_subregion(get_system_memory(), ecam_base, ecam_alias);

    /* MMIO space */
    mmio_base = virt_memmap[VIRT_PCIE_MMIO].base;
    mmio_size = virt_memmap[VIRT_PCIE_MMIO].size;
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio", mmio_reg,
                             mmio_base, mmio_size);
    memory_region_add_subregion(get_system_memory(), mmio_base, mmio_alias);

    /* IO port space */
    pio_base = virt_memmap[VIRT_PCIE_PIO].base;
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, pio_base);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        irq = qdev_get_gpio_in(micdev, 60 + i);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, 60 + i);
    }
}

static void mp860_init(MachineState *machine)
{
    CPUState        *cs;
    CSKYCPU         *cpu = NULL, *last_cpu;

    DeviceState     *mptimerdev;
    SysBusDevice    *micbusdev, *mptimerbusdev;
    unsigned int ram_size = machine->ram_size;
    int n, i;
    void *fdt;
    /* load dtb or create fdt */
    virt_binfo.dtb_filename = machine->dtb;
    if (!virt_binfo.dtb_filename) {
        fdt = create_smp_fdt(machine);
        ram_size = machine->ram_size;
        qemu_fdt_dumpdtb(fdt, fdt_totalsize(fdt));
        rom_add_blob_fixed_as("mrom.fdt", fdt, fdt_totalsize(fdt),
                virt_binfo.dtb_addr & 0x1fffffff, &address_space_memory);
    }
    /*
     * Prepare RAM.
     */
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);

    memory_region_init_ram(ram, NULL, "ram", ram_size, &error_fatal);
    memory_region_add_subregion(sysmem, 0, ram);

    /*
     * Prepare CPU
     */
    if (!strcmp(machine->cpu_type, CSKY_CPU_TYPE_NAME("c810"))) {
        machine->cpu_type = CSKY_CPU_TYPE_NAME("c860v");
    }
    for (n = 0; n < machine->smp.cpus; n++) {
        cs = cpu_create(machine->cpu_type);
        cs->cpu_index = n;
        CSKY_CPU(cs)->env.features |= UNALIGNED_ACCESS;
        CSKY_CPU(cs)->env.mmu_default = 1;
        if (n == 0) {
            cpu = CSKY_CPU(cs);
        } else {
            last_cpu->env.next_cpu = &(CSKY_CPU(cs)->env);
        }
        if (n == machine->smp.cpus - 1) {
            CSKY_CPU(cs)->env.next_cpu = &(cpu->env);
        }
        last_cpu = CSKY_CPU(cs);
        csky_mic_init_cpu(&(CSKY_CPU(cs)->env));
    }

    /*
     * use C-SKY MultiCore interrupt controller
     */

    micdev = qdev_new("csky_mic");
    micbusdev = SYS_BUS_DEVICE(micdev);
    sysbus_realize_and_unref(micbusdev, &error_fatal);
    sysbus_mmio_map(micbusdev, 0, 0xfffe0000);

    uint32_t mic_cpu_base = 0xfffe8000;
    for (i = 0; i < machine->smp.cpus; i++) {
        sysbus_mmio_map(micbusdev, i + 1, mic_cpu_base);
        mic_cpu_base += 0x1000;

    }
    DeviceState *cpudev = DEVICE(cpu);
    last_cpu = cpu;
    for (i = 0; i < machine->smp.cpus; i++) {
        sysbus_connect_irq(micbusdev, i, qdev_get_gpio_in(cpudev, 0));
        sysbus_connect_irq(micbusdev, i + machine->smp.cpus,
            qdev_get_gpio_in(cpudev, 1));
        last_cpu = env_archcpu(last_cpu->env.next_cpu);
        cpudev = DEVICE(last_cpu);
    }
    /*
     * create GPEX PCIe
     */
    gpex_pcie_init();

    /*
     * use C-SKY MultiCore timer
     */

    mptimerdev = qdev_new("csky_mptimer");
    mptimerbusdev = SYS_BUS_DEVICE(mptimerdev);
    sysbus_realize_and_unref(mptimerbusdev, &error_fatal);
    sysbus_mmio_map(mptimerbusdev, 0, 0xffffd000);

    last_cpu = cpu;
    for (n = 0; n < machine->smp.cpus; n++) {
        sysbus_connect_irq(mptimerbusdev, n,
          qdev_get_gpio_in(micdev, n == 0 ? 16 : 16 * (n - 1) + 224));
        last_cpu->env.mptimerdev = mptimerdev;
        last_cpu = env_archcpu(last_cpu->env.next_cpu);
    }
    /*
     * use virtio-mmio
     */
    for (i = 0; i < 8; i++) {
        sysbus_create_simple("virtio-mmio", 0xffff0000 + i * 0x1000,
            qdev_get_gpio_in(micdev, 50 + i));
    }
    csky_uart_create(0xffffe000, qdev_get_gpio_in(micdev, 43), NULL,
        serial_hd(0));

    sysbus_create_simple("csky_exit", 0xffffc000, NULL);

    virt_binfo.kernel_filename = machine->kernel_filename;
    virt_binfo.ram_size = ram_size;
    csky_load_kernel(cpu, &virt_binfo);
}

static void virt_init(MachineState *machine)
{
        Object          *cpuobj;
        CSKYCPU         *cpu;

        DeviceState     *intc;
        int i;
        unsigned int ram_size = 0, base = 0;
        void *fdt;

        if (machine->cpu_type) {
            if (strstr(machine->cpu_type, "860")) {
                mp860_init(machine);
                return;
            }
        }
        if (machine->smp.cpus > 1) {
            mp860_init(machine);
            return;
        }

        /* load dtb or create fdt */
        virt_binfo.dtb_filename = machine->dtb;
        if (virt_binfo.dtb_filename) {
            /* read ram base and size from dtb file. */
            if (!get_ramsize_from_dtb(&ram_size, &base)) {
                fprintf(stderr,
                    "qemu: fail to read RAM base/size in dtb '%s'\n",
                    virt_binfo.dtb_filename);
                exit(1);
            }
        } else {
            fdt = create_fdt(machine);
            ram_size = machine->ram_size;
            qemu_fdt_dumpdtb(fdt, fdt_totalsize(fdt));
            rom_add_blob_fixed_as("mrom.fdt", fdt, fdt_totalsize(fdt),
                virt_binfo.dtb_addr & 0x1fffffff, &address_space_memory);
        }
        /*
         * Prepare RAM.
         */
        MemoryRegion *sysmem = get_system_memory();
        MemoryRegion *ram = g_new(MemoryRegion, 1);

        memory_region_init_ram(ram, NULL, "ram", ram_size, &error_fatal);
        memory_region_add_subregion(sysmem, base, ram);

        cpuobj = object_new(machine->cpu_type);

        object_property_set_bool(cpuobj, "realized", true, &error_fatal);

        cpu = CSKY_CPU(cpuobj);

        /*
         * use C-SKY interrupt controller
         */
        intc = sysbus_create_simple(
                        "csky_intc",
                        0xfffff000,
                        *csky_intc_init_cpu(&cpu->env));

        /*
         * use dw-apb-timer
         */
        csky_timer_set_freq(virt_binfo.freq);
        sysbus_create_varargs(
                        "csky_timer",
                        0xffffd000,
                        qdev_get_gpio_in(intc, 1),
                        qdev_get_gpio_in(intc, 2),
                        NULL);

        /*
         * use 16650a uart.
         */
        csky_uart_create(
                        0xffffe000,
                        qdev_get_gpio_in(intc, 3), NULL,
                        serial_hd(0));

        /*
         * for qemu exit, use cmd poweroff.
         */
        sysbus_create_simple("csky_exit", 0xffffc000, NULL);

        if (virt_binfo.dtb_filename) {
            /*
             * add net, io-len is 2K.
             */
            //csky_mac_v2_create(0xffffa000, qdev_get_gpio_in(intc, 4));
        }
        /*
         * use virtio-mmio
         */
        for (i = 0; i < 8; i++) {
            sysbus_create_simple("virtio-mmio", 0xffff0000 + i * 0x1000,
                qdev_get_gpio_in(intc, 10 + i));
        }
        /*
         * boot up kernel with unaligned_access and mmu on.
         */
#ifdef TARGET_CSKYV2
        cpu->env.features |= UNALIGNED_ACCESS;
#endif
        cpu->env.mmu_default = 1;

        virt_binfo.kernel_filename = machine->kernel_filename;
        virt_binfo.ram_size = ram_size;
        csky_load_kernel(cpu, &virt_binfo);
}

static void virt_class_init(ObjectClass *oc, void *data)
{

        MACHINE_CLASS(oc)->is_default = true;
        MACHINE_CLASS(oc)->desc = "C-SKY QEMU virt machine";
        MACHINE_CLASS(oc)->init = virt_init;
        MACHINE_CLASS(oc)->max_cpus = 128;
#ifdef TARGET_CSKYV2
        MACHINE_CLASS(oc)->default_cpu_type = CSKY_CPU_TYPE_NAME("c810");
        MACHINE_CLASS(oc)->default_ram_size = 0x40000000;
#else
        MACHINE_CLASS(oc)->default_cpu_type = CSKY_CPU_TYPE_NAME("ck610ef");
#endif
        MACHINE_CLASS(oc)->default_ram_size = 0x50000000;
        machine_class_allow_dynamic_sysbus_dev(MACHINE_CLASS(oc),
                                               TYPE_RAMFB_DEVICE);
}

static const TypeInfo virt_type = {
        .name           = MACHINE_TYPE_NAME("virt"),
        .parent         = TYPE_MACHINE,
        .class_init     = virt_class_init,
};

static void virt_machine_init(void)
{
        type_register_static(&virt_type);
}

type_init(virt_machine_init)

