/*
 * a dummy similar with CSKY Trilobite V2 System emulation.
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
#include "target/riscv/cpu.h"
#include "hw/csky/csky_boot.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/char/csky_uart.h"
#include "hw/timer/csky_coret.h"
#include "hw/csky/dynsoc.h"
#include "elf.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/intc/xt_clic.h"
#include "hw/intc/thead_clint.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/boot.h"

static struct csky_boot_info dummyh_binfo = {
    .loader_start = 0x0,
    .dtb_addr = 0x8f000000,
    .magic = 0x20150401,
};

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

#define RAM_NUM     8
#define DEV_NUM     32
static void dummyh_init(MachineState *machine)
{
    DeviceState *dev = NULL;
    int i;
    struct dynsoc_board_info *b_info = dynsoc_b_info;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram[RAM_NUM];
    int dev_type;
    qemu_irq *plic_irqs = NULL;
    qemu_irq *clic_irqs = NULL;
    char *plic_hart_config;
    bool has_clic = false, has_plic = false;
    uint64_t kernel_entry = 0;
    unsigned int smp_cpus = machine->smp.cpus;
    RISCVVirtState *s = g_new0(RISCVVirtState, 1);

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
    if (strstr(b_info->xml_name, "xiaohui")) {
        object_property_set_bool(OBJECT(&s->soc), "cpu-off", true,
                                 &error_abort);
    }
    sysbus_realize(SYS_BUS_DEVICE(&s->soc[0]), &error_abort);
    s->soc[0].harts[0].env.elf_start = kernel_entry;

    /* We don't want to expose this device currently */
    if (strstr(b_info->xml_name, "xiaohui")) {
        sysbus_create_simple("xiaohui_cpr", 0x18030000, NULL);
    }

    for (i = 0; i < RAM_NUM; i++) {
        ram[i] = g_new(MemoryRegion, 1);
        if (b_info->mem[i].size != 0) {
            if (b_info->mem[i].boot == 1) {
                dummyh_binfo.ram_size = b_info->mem[i].size;
            }
            if (b_info->mem[i].writeable == 1) {
                memory_region_init_ram(ram[i], NULL,
                                       b_info->mem[i].name,
                                       b_info->mem[i].size,
                                       &error_fatal);
                memory_region_add_subregion(sysmem, b_info->mem[i].addr,
                                            ram[i]);
            } else if (b_info->mem[i].writeable == 0) {
                /* !writeable means this ram is alias of a rom/flash */
                memory_region_init_ram(ram[i], NULL,
                                       b_info->mem[i].name,
                                       b_info->mem[i].size,
                                       &error_fatal);
                memory_region_set_readonly(ram[i], true);
                memory_region_add_subregion(sysmem, b_info->mem[i].addr,
                                            ram[i]);
            }
        }

        if (b_info->mem[i].size == 0) {
            continue;
        }
    }

    if (b_info->set_intc == 0) {
        goto finish_device;
    }
    for (i = 0; i < DEV_NUM; i++) {
        if (strstr(b_info->dev[i].name, "csky_clic")) {
            has_clic = true;
        }
        if (strstr(b_info->dev[i].name, "csky_plic")) {
            has_plic = true;
        }
    }

    clic_irqs = g_new0(qemu_irq, 256 * smp_cpus);
    plic_irqs = g_new0(qemu_irq, 128);
    for (i = 0; i < DEV_NUM; i++) {
        dev_type = b_info->dev[i].type;
        switch (dev_type) {
        case DYNSOC_EMPTY:
            /* do nothing */
            break;
        case DYNSOC_INTC:
            if (strstr(b_info->dev[i].name, "csky_clic")) {
                dev = xt_clic_create(b_info->dev[i].addr, true, smp_cpus,
                                     256, 3);
                for (int j = 0; j < 256 * smp_cpus; j++) {
                    clic_irqs[j] = qdev_get_gpio_in(dev, j);
                }
            }
            if (!strcmp(b_info->dev[i].name, "csky_plic")) {
                int j;
                /* create PLIC hart topology configuration string */
                plic_hart_config = riscv_plic_hart_config_string(smp_cpus);
                /* create PLIC */
                DeviceState *plic = sifive_plic_create(b_info->dev[i].addr,
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
                for (j = 0; j < 127; j++) {
                    plic_irqs[j] = qdev_get_gpio_in(DEVICE(plic), j);
                }
            }
            if (!strcmp(b_info->dev[i].name, "csky_clint")) {
                if (has_plic) {
                    uint64_t addr = b_info->dev[i].addr;
                    riscv_aclint_swi_create(addr, 0, smp_cpus, false, 0);
                    riscv_aclint_swi_create(addr + 0xC000, 0, smp_cpus, true, 0x1000);
                    riscv_aclint_mtimer_create(addr + 0x4000,
                                               RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                                               0, smp_cpus,
                                               RISCV_ACLINT_DEFAULT_MTIMECMP,
                                               RISCV_ACLINT_DEFAULT_MTIME,
                                               25000000,
                                               true);
                    riscv_aclint_mtimer_create(addr + 0xD000,
                                               0x3000,
                                               0, smp_cpus,
                                               RISCV_ACLINT_DEFAULT_MTIMECMP,
                                               UINT32_MAX,
                                               25000000,
                                               false);
                } else if (has_clic) {
                    qemu_irq *pirq = g_new0(qemu_irq, 2 * smp_cpus);

                    for (int j = 0; j < smp_cpus; j++) {
                        pirq[2 * j] = clic_irqs[3 + j * 256];
                        pirq[2 * j + 1] = clic_irqs[7 + j * 256];
                    }
                    thead_clint_create(b_info->dev[i].addr, pirq, smp_cpus);
                }
            }
            break;
        case DYNSOC_UART:
            csky_uart_create(b_info->dev[i].addr,
                             plic_irqs[b_info->dev[i].irq],
                             clic_irqs[b_info->dev[i].irq],
                             serial_hd(0));
            break;
        case DYNSOC_TIMER:
            if (!strcmp(b_info->dev[i].name, "csky_coret")) {
                continue;
            }
            if (plic_irqs[b_info->dev[i].irq]) {
                sysbus_create_varargs(b_info->dev[i].name, b_info->dev[i].addr,
                                      plic_irqs[b_info->dev[i].irq],
                                      plic_irqs[b_info->dev[i].irq + 1],
                                      plic_irqs[b_info->dev[i].irq + 2],
                                      plic_irqs[b_info->dev[i].irq + 3],
                                      clic_irqs[b_info->dev[i].irq],
                                      clic_irqs[b_info->dev[i].irq + 1],
                                      clic_irqs[b_info->dev[i].irq + 2],
                                      clic_irqs[b_info->dev[i].irq + 3],
                                      NULL);
            } else {
                sysbus_create_varargs(b_info->dev[i].name, b_info->dev[i].addr,
                                      clic_irqs[b_info->dev[i].irq],
                                      clic_irqs[b_info->dev[i].irq + 1],
                                      clic_irqs[b_info->dev[i].irq + 2],
                                      clic_irqs[b_info->dev[i].irq + 3],
                                      NULL);
            }
            break;
        case DYNSOC_LCDC:
            /* todo */
            break;
        case DYNSOC_MAC:
            /* todo */
            break;
        case DYNSOC_EXIT:
        case DYNSOC_MEMLOG:
            sysbus_create_simple(b_info->dev[i].name, b_info->dev[i].addr,
                                 NULL);
            break;
        case DYNSOC_DMA:
            /* todo */
            break;
        case DYNSOC_IIS:
            /* todo */
            break;
        case DYNSOC_NAND:
        case DYNSOC_SDHC:
            /* todo */
            break;
        case DYNSOC_USB:
        case DYNSOC_CUSTOM:
            /* todo */
            break;
        default:
            fprintf(stderr, "not support device type\n");
            exit(1);
        }

    }

finish_device:
    if (dummyh_binfo.ram_size == 0) {
        dummyh_binfo.ram_size = machine->ram_size;
    }
    dummyh_binfo.kernel_filename = machine->kernel_filename;
    dummyh_binfo.kernel_cmdline = machine->kernel_cmdline;
    dummyh_binfo.initrd_filename = machine->initrd_filename;
    dummyh_binfo.bios = machine->firmware;
    dummyh_binfo.loader_start = b_info->loader_start;
}

static void dummyh_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CSKY dummyh";
    mc->init = dummyh_init;
    mc->default_cpu_type = RISCV_CPU_TYPE_NAME("e902");
    mc->max_cpus = 8;
}

static const TypeInfo dummyh_type = {
    .name = MACHINE_TYPE_NAME("dummyh"),
    .parent = TYPE_MACHINE,
    .class_init = dummyh_class_init,
    .instance_size = sizeof(RISCVVirtState)
};

static void dummyh_machine_init(void)
{
    type_register_static(&dummyh_type);
}

type_init(dummyh_machine_init)
