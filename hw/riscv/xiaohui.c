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
#include "qemu/guest-random.h"
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
#include "hw/intc/riscv_imsic.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/misc/sifive_test.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/xt_clic.h"
#include "hw/intc/xt_clic_v0p10.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/dma/dw_dma_sw.h"
#include "hw/misc/riscv_iopmp.h"
#include "hw/misc/riscv_iopmp_dispatcher.h"
#include "hw/stream.h"
#include "hw/char/csky_uart.h"
#include "hw/misc/xiaohui_pcu.h"
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
    XiaohuiAIAType aia_type;
    bool have_iopmp;
    XiaohuiAclintType aclint_type;
    bool have_pcu;
    bool clic_v0p10;
    bool sync_monchipba;

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
     /* APLIC and IOPMP addresses (compatible with xiaohui_v2) */
     [XIAOHUI_APLIC_M]      = { 0x0008000000,    32 * KiB },
     [XIAOHUI_APLIC_S]      = { 0x0008020000,    32 * KiB },
     [XIAOHUI_ACLINT]       = { 0x0008040000,   64 * KiB },
     [XIAOHUI_DMA]          = { 0x0018000000,   64 * KiB },
     [XIAOHUI_IOPMP]        = { 0x0026f00000,    1 * MiB },
     [XIAOHUI_IMSIC_M]      = { 0xf00000000000,  64 * KiB },
     [XIAOHUI_IMSIC_S]      = { 0xf00010000000,   4 * MiB },
     [XIAOHUI_PCU]          = { 0x26808000,       4 * KiB },
};

const uint64_t xiaohui_per_hart_aclint_base[16] = {
    0x2680c000, 0x2682c000,
    0x2688c000, 0x268ac000,
    0x2690c000, 0x2692c000,
    0x2698c000, 0x269ac000,
    0x26a0c000, 0x26a2c000,
    0x26a8c000, 0x26aac000,
    0x26b0c000, 0x26b2c000,
    0x26b8c000, 0x26bac000
};

static void create_fdt_one_per_hart_aclint(RISCVXiaohuiState *s,
                                           unsigned long base,
                                           uint32_t hartid,
                                           uint32_t *intc_phandles)
{
    char *name;
    unsigned long addr;
    uint32_t aclint_cells_size;
    g_autofree uint32_t *aclint_mtimer_cells = NULL;
    MachineState *ms = MACHINE(s);

    aclint_mtimer_cells = g_new0(uint32_t, 2);
    aclint_mtimer_cells[0] = cpu_to_be32(intc_phandles[hartid]);
    aclint_mtimer_cells[1] = cpu_to_be32(IRQ_M_TIMER);
    aclint_cells_size = sizeof(uint32_t) * 2;

    addr = base;
    name = g_strdup_printf("/soc/mtimer@%lx", addr);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "riscv,aclint-mtimer");
    qemu_fdt_setprop_cells(ms->fdt, name, "reg",
        HI_CELL(addr), LOW_CELL(addr),
        0x0, 0x100,
        HI_CELL((addr + 0x100)), LOW_CELL((addr + 0x100)),
        0x0, 0x100);
    qemu_fdt_setprop(ms->fdt, name, "interrupts-extended",
        aclint_mtimer_cells, aclint_cells_size);
    riscv_socket_fdt_write_id(ms, name, 0);
    g_free(name);
}

static void create_fdt_socket_per_hart_aclint(RISCVXiaohuiState *s,
                                              uint32_t hart_count,
                                              uint32_t *intc_phandles)
{
    int i;

    for (i = 0; i < (int)hart_count; i++) {
        create_fdt_one_per_hart_aclint(s, xiaohui_per_hart_aclint_base[i],
                                       i, intc_phandles);
    }
}

static void create_fdt_one_imsic(RISCVXiaohuiState *s, hwaddr base_addr,
                                 uint32_t *intc_phandles, uint32_t msi_phandle,
                                 bool m_mode, uint32_t imsic_guest_bits)
{
    int cpu;
    g_autofree char *imsic_name = NULL;
    MachineState *ms = MACHINE(s);
    uint64_t imsic_addr, imsic_size;
    g_autofree uint32_t *imsic_cells = NULL;
    g_autofree uint32_t *imsic_regs = NULL;

    imsic_cells = g_new0(uint32_t, ms->smp.cpus * 2);
    imsic_regs = g_new0(uint32_t, 4);

    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(m_mode ? IRQ_M_EXT
                                                       : IRQ_S_EXT);
    }

    imsic_addr = base_addr;
    imsic_size = IMSIC_HART_SIZE(imsic_guest_bits) * s->soc.num_harts;
    imsic_regs[0] = cpu_to_be32(imsic_addr >> 32);
    imsic_regs[1] = cpu_to_be32(imsic_addr);
    imsic_regs[2] = 0;
    imsic_regs[3] = cpu_to_be32(imsic_size);

    imsic_name = g_strdup_printf("/soc/imsics@%lx", (unsigned long)base_addr);
    qemu_fdt_add_subnode(ms->fdt, imsic_name);
    qemu_fdt_setprop_string(ms->fdt, imsic_name, "compatible", "riscv,imsics");
    qemu_fdt_setprop_cell(ms->fdt, imsic_name, "#interrupt-cells",
                          FDT_IMSIC_INT_CELLS);
    qemu_fdt_setprop(ms->fdt, imsic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(ms->fdt, imsic_name, "msi-controller", NULL, 0);
    qemu_fdt_setprop(ms->fdt, imsic_name, "interrupts-extended",
                     imsic_cells, ms->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop(ms->fdt, imsic_name, "reg", imsic_regs,
                     sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,num-ids",
                          XIAOHUI_IRQCHIP_NUM_MSIS);

    if (imsic_guest_bits) {
        qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,guest-index-bits",
                              imsic_guest_bits);
    }
    qemu_fdt_setprop_cell(ms->fdt, imsic_name, "phandle", msi_phandle);
}

#define IOPMP_IRQ 18

static void create_fdt_iopmp(RISCVXiaohuiState *s, const MemMapEntry *memmap,
                             uint32_t aplic_m_phandle) {
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);

    name = g_strdup_printf("/soc/iopmp@%lx", (long)memmap[XIAOHUI_IOPMP].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "riscv_iopmp");
    qemu_fdt_setprop_cells(ms->fdt, name, "reg", 0x0,
        memmap[XIAOHUI_IOPMP].base,
        0x0, memmap[XIAOHUI_IOPMP].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent", aplic_m_phandle);
    qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", IOPMP_IRQ);
}

/*
 * Create an APLIC FDT node.
 * When msi_phandle != 0, use MSI mode (APLIC-IMSIC); otherwise use direct
 * delivery mode and aplic_cells must be provided.
 */
static void create_fdt_one_aplic(RISCVXiaohuiState *s,
                                 unsigned long aplic_addr,
                                 uint32_t aplic_size,
                                 uint32_t *aplic_cells,
                                 uint32_t msi_phandle,
                                 uint32_t aplic_phandle,
                                 uint32_t aplic_child_phandle)
{
    MachineState *ms = MACHINE(s);
    g_autofree char *aplic_name = NULL;

    aplic_name = g_strdup_printf("/soc/aplic@%lx", aplic_addr);
    qemu_fdt_add_subnode(ms->fdt, aplic_name);
    qemu_fdt_setprop_string(ms->fdt, aplic_name, "compatible", "riscv,aplic");
    qemu_fdt_setprop_cell(ms->fdt, aplic_name,
                          "#interrupt-cells", FDT_APLIC_INT_CELLS);
    qemu_fdt_setprop(ms->fdt, aplic_name, "interrupt-controller", NULL, 0);

    if (msi_phandle) {
        qemu_fdt_setprop_cell(ms->fdt, aplic_name, "msi-parent", msi_phandle);
    } else {
        qemu_fdt_setprop(ms->fdt, aplic_name, "interrupts-extended",
                         aplic_cells,
                         s->soc.num_harts * sizeof(uint32_t) * 2);
    }

    qemu_fdt_setprop_cells(ms->fdt, aplic_name, "reg",
                           HI_CELL(aplic_addr), LOW_CELL(aplic_addr), 0x0,
                           aplic_size);
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "riscv,num-sources", 96);

    if (aplic_child_phandle) {
        qemu_fdt_setprop_cell(ms->fdt, aplic_name, "riscv,children",
                              aplic_child_phandle);
        qemu_fdt_setprop_cells(ms->fdt, aplic_name, "riscv,delegate",
                               aplic_child_phandle, 0x1,
                               96);
    }

    riscv_socket_fdt_write_id(ms, aplic_name, 0);
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "phandle", aplic_phandle);
}

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
    bool is_32_bit = riscv_is_32bit(&s->soc);
    uint32_t *intc_phandles = g_new0(uint32_t, s->soc.num_harts);
    uint8_t rng_seed[32];

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
        RISCVCPU *cpu_ptr = &s->soc.harts[cpu];
        int intc_phandle = phandle++;
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa = riscv_isa_string(&s->soc.harts[cpu]);
        intc_phandles[cpu] = intc_phandle;

        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        qemu_fdt_add_subnode(fdt, nodename);

        /* Auto-detect MMU type from CPU configuration */
        if (cpu_ptr->cfg.satp_mode.supported != 0) {
            uint8_t satp_mode_max =
                satp_mode_max_from_map(cpu_ptr->cfg.satp_mode.map);
            g_autofree char *sv_name = g_strdup_printf("riscv,%s",
                satp_mode_str(satp_mode_max, is_32_bit));
            qemu_fdt_setprop_string(fdt, nodename, "mmu-type", sv_name);
        } else {
            qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv39");
        }

        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);

        if (cpu_ptr->cfg.ext_zicbom) {
            qemu_fdt_setprop_cell(fdt, nodename, "riscv,cbom-block-size",
                                  cpu_ptr->cfg.cbom_blocksize);
        }
        if (cpu_ptr->cfg.ext_zicboz) {
            qemu_fdt_setprop_cell(fdt, nodename, "riscv,cboz-block-size",
                                  cpu_ptr->cfg.cboz_blocksize);
        }
        if (cpu_ptr->cfg.ext_zicbop) {
            qemu_fdt_setprop_cell(fdt, nodename, "riscv,cbop-block-size",
                                  cpu_ptr->cfg.cbop_blocksize);
        }

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

    /* Create interrupt controller based on aia_type */
    uint32_t aplic_m_phandle = 0;
    uint32_t aplic_s_phandle = 0;
    plic_phandle = 0;

    if (s->aia_type == XIAOHUI_AIA_TYPE_APLIC_IMSIC) {
        uint32_t msi_m_phandle = phandle++;
        create_fdt_one_imsic(s, mmap[XIAOHUI_IMSIC_M].base,
                             intc_phandles, msi_m_phandle, true, 0);
        uint32_t msi_s_phandle = phandle++;
        create_fdt_one_imsic(s, mmap[XIAOHUI_IMSIC_S].base,
                             intc_phandles, msi_s_phandle, false, 6);
        /*
         * In aplic-imsic mode, override the default APLIC layout: APLIC M/S
         * each occupy 16 KiB starting from 0x0008000000, i.e.
         *   APLIC_M: 0x0008000000 (16 KiB)
         *   APLIC_S: 0x0008004000 (16 KiB)
         * The aplic mode keeps the original 32 KiB layout from xiaohui_memmap.
         */
        const hwaddr aplic_m_base = XIAOHUI_APLIC_IMSIC_APLIC_M_BASE;
        const hwaddr aplic_m_size = XIAOHUI_APLIC_IMSIC_APLIC_SIZE;
        const hwaddr aplic_s_base = XIAOHUI_APLIC_IMSIC_APLIC_S_BASE;
        const hwaddr aplic_s_size = XIAOHUI_APLIC_IMSIC_APLIC_SIZE;

        aplic_m_phandle = phandle++;
        aplic_s_phandle = phandle++;
        create_fdt_one_aplic(s, aplic_m_base, aplic_m_size,
                             NULL, msi_m_phandle,
                             aplic_m_phandle, aplic_s_phandle);
        create_fdt_one_aplic(s, aplic_s_base, aplic_s_size,
                             NULL, msi_s_phandle,
                             aplic_s_phandle, 0);
        plic_phandle = aplic_m_phandle;
    } else if (s->aia_type == XIAOHUI_AIA_TYPE_APLIC) {
        uint32_t *aplic_cells = g_new0(uint32_t, s->soc.num_harts * 2);
        aplic_m_phandle = phandle++;
        aplic_s_phandle = phandle++;
        for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
            aplic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
            aplic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_EXT);
        }
        create_fdt_one_aplic(s, mmap[XIAOHUI_APLIC_M].base,
                             mmap[XIAOHUI_APLIC_M].size,
                             aplic_cells, 0,
                             aplic_m_phandle, aplic_s_phandle);
        for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
            aplic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
            aplic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
        }
        create_fdt_one_aplic(s, mmap[XIAOHUI_APLIC_S].base,
                             mmap[XIAOHUI_APLIC_S].size,
                             aplic_cells, 0,
                             aplic_s_phandle, 0);
        plic_phandle = aplic_m_phandle;
        g_free(aplic_cells);
    } else {
        plic_phandle = ++phandle;
        cells = g_new0(uint32_t, s->soc.num_harts * 4);
        for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
            cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
            cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
            cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
            cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
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
    }


    /* ACLINT / CLINT FDT nodes */
    if (s->aclint_type == XIAOHUI_ACLINT_TYPE_PER_HART) {
        create_fdt_socket_per_hart_aclint(s, s->soc.num_harts,
                                         intc_phandles);
    } else {
        unsigned long addr, size;
        aclint_mtimer_cells = g_new0(uint32_t, s->soc.num_harts * 2);
        aclint_mswi_cells = g_new0(uint32_t, s->soc.num_harts * 2);
        aclint_sswi_cells = g_new0(uint32_t, s->soc.num_harts * 2);
        for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
            aclint_mtimer_cells[cpu * 2 + 0] =
                cpu_to_be32(intc_phandles[cpu]);
            aclint_mtimer_cells[cpu * 2 + 1] =
                cpu_to_be32(IRQ_M_TIMER);
            aclint_mswi_cells[cpu * 2 + 0] =
                cpu_to_be32(intc_phandles[cpu]);
            aclint_mswi_cells[cpu * 2 + 1] =
                cpu_to_be32(IRQ_M_SOFT);
            aclint_sswi_cells[cpu * 2 + 0] =
                cpu_to_be32(intc_phandles[cpu]);
            aclint_sswi_cells[cpu * 2 + 1] =
                cpu_to_be32(IRQ_S_SOFT);
        }
        aclint_cells_size = s->soc.num_harts * sizeof(uint32_t) * 2;

        if (s->aclint_type == XIAOHUI_ACLINT_TYPE_ACLINT) {
            /* Shared-base ACLINT */
            addr = mmap[XIAOHUI_ACLINT].base;
            nodename = g_strdup_printf("/soc/mswi@%lx", addr);
            qemu_fdt_add_subnode(mc->fdt, nodename);
            qemu_fdt_setprop_string(mc->fdt, nodename,
                "compatible", "riscv,aclint-mswi");
            qemu_fdt_setprop_cells(mc->fdt, nodename, "reg",
                0x0, addr, 0x0, RISCV_ACLINT_SWI_SIZE);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupts-extended",
                aclint_mswi_cells, aclint_cells_size);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(mc->fdt, nodename,
                "#interrupt-cells", 0);
            riscv_socket_fdt_write_id(mc, nodename, 0);
            g_free(nodename);

            addr = mmap[XIAOHUI_ACLINT].base + 0x4000;
            size = RISCV_ACLINT_DEFAULT_MTIMER_SIZE;
            nodename = g_strdup_printf("/soc/mtimer@%lx", addr);
            qemu_fdt_add_subnode(mc->fdt, nodename);
            qemu_fdt_setprop_string(mc->fdt, nodename,
                "compatible", "riscv,aclint-mtimer");
            qemu_fdt_setprop_cells(mc->fdt, nodename, "reg",
                0x0, addr + RISCV_ACLINT_DEFAULT_MTIME,
                0x0, size - RISCV_ACLINT_DEFAULT_MTIME,
                0x0, addr + RISCV_ACLINT_DEFAULT_MTIMECMP,
                0x0, RISCV_ACLINT_DEFAULT_MTIME);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupts-extended",
                aclint_mtimer_cells, aclint_cells_size);
            riscv_socket_fdt_write_id(mc, nodename, 0);
            g_free(nodename);

            addr = mmap[XIAOHUI_ACLINT].base + 0xC000;
            nodename = g_strdup_printf("/soc/sswi@%lx", addr);
            qemu_fdt_add_subnode(mc->fdt, nodename);
            qemu_fdt_setprop_string(mc->fdt, nodename,
                "compatible", "riscv,aclint-sswi");
            qemu_fdt_setprop_cells(mc->fdt, nodename, "reg",
                0x0, addr, 0x0, RISCV_ACLINT_SWI_SIZE);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupts-extended",
                aclint_sswi_cells, aclint_cells_size);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(mc->fdt, nodename,
                "#interrupt-cells", 0);
            riscv_socket_fdt_write_id(mc, nodename, 0);
            g_free(nodename);
        } else {
            /* CLINT mode (default) */
            addr = mmap[XIAOHUI_CLINT].base;
            nodename = g_strdup_printf("/soc/mswi@%lx", addr);
            qemu_fdt_add_subnode(mc->fdt, nodename);
            qemu_fdt_setprop_string(mc->fdt, nodename,
                "compatible", "riscv,aclint-mswi");
            qemu_fdt_setprop_cells(mc->fdt, nodename, "reg",
                0x0, addr, 0x0, RISCV_ACLINT_SWI_SIZE);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupts-extended",
                aclint_mswi_cells, aclint_cells_size);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(mc->fdt, nodename,
                "#interrupt-cells", 0);
            riscv_socket_fdt_write_id(mc, nodename, 0);
            g_free(nodename);

            addr = mmap[XIAOHUI_CLINT].base + 0x4000;
            size = RISCV_ACLINT_DEFAULT_MTIMER_SIZE;
            nodename = g_strdup_printf("/soc/mtimer@%lx", addr);
            qemu_fdt_add_subnode(mc->fdt, nodename);
            qemu_fdt_setprop_string(mc->fdt, nodename,
                "compatible", "riscv,aclint-mtimer");
            qemu_fdt_setprop_cells(mc->fdt, nodename, "reg",
                0x0, addr + RISCV_ACLINT_DEFAULT_MTIME,
                0x0, size - RISCV_ACLINT_DEFAULT_MTIME,
                0x0, addr + RISCV_ACLINT_DEFAULT_MTIMECMP,
                0x0, RISCV_ACLINT_DEFAULT_MTIME);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupts-extended",
                aclint_mtimer_cells, aclint_cells_size);
            riscv_socket_fdt_write_id(mc, nodename, 0);
            g_free(nodename);

            addr = mmap[XIAOHUI_CLINT].base + 0xC000;
            nodename = g_strdup_printf("/soc/sswi@%lx", addr);
            qemu_fdt_add_subnode(mc->fdt, nodename);
            qemu_fdt_setprop_string(mc->fdt, nodename,
                "compatible", "riscv,aclint-sswi");
            qemu_fdt_setprop_cells(mc->fdt, nodename, "reg",
                0x0, addr, 0x0, RISCV_ACLINT_SWI_SIZE);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupts-extended",
                aclint_sswi_cells, aclint_cells_size);
            qemu_fdt_setprop(mc->fdt, nodename,
                "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(mc->fdt, nodename,
                "#interrupt-cells", 0);
            riscv_socket_fdt_write_id(mc, nodename, 0);
            g_free(nodename);
        }
        g_free(aclint_mswi_cells);
        g_free(aclint_mtimer_cells);
        g_free(aclint_sswi_cells);
    }

    nodename = g_strdup_printf("/soc/serial@%lx",
                               (long)mmap[XIAOHUI_UART0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible",
                            "snps,dw-apb-uart");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           HI_CELL(mmap[XIAOHUI_UART0].base),
                           LOW_CELL(mmap[XIAOHUI_UART0].base),
                           HI_CELL(mmap[XIAOHUI_UART0].size),
                           LOW_CELL(mmap[XIAOHUI_UART0].size));
    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", nodename);
    if (s->aia_type != XIAOHUI_AIA_TYPE_NONE) {
        qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent",
                               aplic_s_phandle);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0x14, 0x4);
    } else {
        qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0x14);
    }
    qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency", 0x2255100);
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", 0x3);
    qemu_fdt_setprop_string(fdt, nodename, "clock-names", "baudclk");
    qemu_fdt_setprop_cells(fdt, nodename, "reg-shift", 0x2);
    qemu_fdt_setprop_cells(fdt, nodename, "reg-io-width", 0x4);
    g_free(nodename);

    /* Create DMA device tree node */
    nodename = g_strdup_printf("/soc/dma@%lx",
                               (long)mmap[XIAOHUI_DMA].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "snps,dma-spear1340");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           HI_CELL(mmap[XIAOHUI_DMA].base),
                           LOW_CELL(mmap[XIAOHUI_DMA].base),
                           HI_CELL(mmap[XIAOHUI_DMA].size),
                           LOW_CELL(mmap[XIAOHUI_DMA].size));
    if (s->aia_type != XIAOHUI_AIA_TYPE_NONE) {
        qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent",
                               aplic_s_phandle);
    } else {
        qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    }
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0x11);
    g_free(nodename);

    /* Create IOPMP device tree node if enabled */
    if (s->have_iopmp) {
        create_fdt_iopmp(s, mmap, plic_phandle);
    }

    /* Add rng-seed to /chosen */
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));

    g_free(intc_phandles);
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
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    char *plic_hart_config;
    int i, j;
    unsigned int smp_cpus = machine->smp.cpus;
    qemu_irq *clic_irqs = g_new0(qemu_irq, smp_cpus * XIAOHUI_CLIC_IRQ_NUMS);
    uint64_t kernel_entry = 0;
    uint64_t fdt_load_addr;
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
        object_property_set_bool(OBJECT(&s->soc),  "cpu-off", true, &error_abort);
    } else if (s->boot_linux) {
        object_property_set_uint(OBJECT(&s->soc), "resetvec",
                                 xiaohui_memmap[XIAOHUI_SRAM].base,
                                 &error_abort);
    }
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_abort);
    s->soc.harts[0].env.elf_start = kernel_entry;

    /* Set xt_monchipba for all CPUs when sync_monchipba is enabled */
    if (s->sync_monchipba) {
        /*
         * Bits [31:20]: Base Address (RW)
         * Bits [19:4]: Reserved (RO, all zeros)
         * Bits [3:1]: Size (RW)
         * Bit [0]: EN (RW)
         */
        for (i = 0; i < smp_cpus; i++) {
            s->soc.harts[i].env.xt_monchipba =
                xiaohui_memmap[XIAOHUI_APLIC_M].base | 1;
        }
    }

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
    DeviceState *clic;
    if (s->clic_v0p10) {
        clic = xt_clic_v0p10_create(true,
                     machine->smp.cpus,
                     XIAOHUI_CLIC_IRQ_NUMS,
                     XIAOHUI_CLIC_INTCTLBITS);
    } else {
        clic = xt_clic_create(xiaohui_memmap[XIAOHUI_CLIC].base, true,
                     machine->smp.cpus,
                     XIAOHUI_CLIC_IRQ_NUMS,
                     XIAOHUI_CLIC_INTCTLBITS);
    }

    /* Create interrupt controller based on aia_type */
    DeviceState *plic;
    if (s->aia_type == XIAOHUI_AIA_TYPE_APLIC_IMSIC) {
        uint32_t imsic_num_ids = XIAOHUI_IRQCHIP_NUM_MSIS;
        /*
         * In aplic-imsic mode, override the default APLIC layout: APLIC M/S
         * each occupy 16 KiB starting from 0x0008000000, i.e.
         *   APLIC_M: 0x0008000000 (16 KiB)
         *   APLIC_S: 0x0008004000 (16 KiB)
         * The aplic mode keeps the original 32 KiB layout from xiaohui_memmap.
         */
        const hwaddr aplic_m_base = XIAOHUI_APLIC_IMSIC_APLIC_M_BASE;
        const hwaddr aplic_m_size = XIAOHUI_APLIC_IMSIC_APLIC_SIZE;
        const hwaddr aplic_s_base = XIAOHUI_APLIC_IMSIC_APLIC_S_BASE;
        const hwaddr aplic_s_size = XIAOHUI_APLIC_IMSIC_APLIC_SIZE;

        for (i = 0; i < smp_cpus; i++) {
            riscv_imsic_create(xiaohui_memmap[XIAOHUI_IMSIC_M].base
                               + i * IMSIC_HART_SIZE(0),
                               i, true, 1, imsic_num_ids);
        }
        for (i = 0; i < smp_cpus; i++) {
            riscv_imsic_create(xiaohui_memmap[XIAOHUI_IMSIC_S].base
                               + i * IMSIC_HART_SIZE(6),
                               i, false, 1 + 6, imsic_num_ids);
        }
        DeviceState *aplic_m = riscv_aplic_create(
                                     aplic_m_base, aplic_m_size,
                                     0, smp_cpus, 96, 3,
                                     true, true, NULL, false);
        riscv_aplic_create(aplic_s_base, aplic_s_size,
                           0, smp_cpus, 96, 3,
                           true, false, aplic_m, false);
        plic = aplic_m;
        for (i = 0; i < 96; i++) {
            irqs[i] = qdev_get_gpio_in(DEVICE(plic), i);
        }
    } else if (s->aia_type == XIAOHUI_AIA_TYPE_APLIC) {
        DeviceState *aplic_m = riscv_aplic_create(
                                     xiaohui_memmap[XIAOHUI_APLIC_M].base,
                                     xiaohui_memmap[XIAOHUI_APLIC_M].size,
                                     0, smp_cpus, 96, 3,
                                     false, true, NULL, false);
        riscv_aplic_create(xiaohui_memmap[XIAOHUI_APLIC_S].base,
                           xiaohui_memmap[XIAOHUI_APLIC_S].size,
                           0, smp_cpus, 96, 3,
                           false, false, aplic_m, false);
        plic = aplic_m;
        for (i = 0; i < 96; i++) {
            irqs[i] = qdev_get_gpio_in(DEVICE(plic), i);
        }
    } else {
        plic_hart_config = riscv_plic_hart_config_string(smp_cpus);
        plic = sifive_plic_create(xiaohui_memmap[XIAOHUI_PLIC].base,
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
        }
    }
    for (i = 0; i < smp_cpus; i++) {
        for (j = 0; j < XIAOHUI_CLIC_IRQ_NUMS; j++) {
            int index = i * XIAOHUI_CLIC_IRQ_NUMS + j;
            clic_irqs[index] = qdev_get_gpio_in(DEVICE(clic), index);
        }
    }
    /* Create ACLINT / CLINT based on aclint_type */
    if (s->aclint_type == XIAOHUI_ACLINT_TYPE_PER_HART) {
        for (i = 0; i < smp_cpus; i++) {
            riscv_aclint_mtimer_create(
                xiaohui_per_hart_aclint_base[i],
                0x200, i, 1, 0x100, 0x0,
                XIAOHUI_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
        }
    } else if (s->aclint_type == XIAOHUI_ACLINT_TYPE_ACLINT) {
        riscv_aclint_swi_create(
            xiaohui_memmap[XIAOHUI_ACLINT].base, 0,
            smp_cpus, false, 0);
        riscv_aclint_swi_create(
            xiaohui_memmap[XIAOHUI_ACLINT].base + 0xC000,
            0, smp_cpus, true, 0x1000);
        riscv_aclint_mtimer_create(
            xiaohui_memmap[XIAOHUI_ACLINT].base + 0x4000,
            RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
            0, smp_cpus, RISCV_ACLINT_DEFAULT_MTIMECMP,
            RISCV_ACLINT_DEFAULT_MTIME,
            XIAOHUI_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
    } else {
        riscv_aclint_swi_create(
            xiaohui_memmap[XIAOHUI_CLINT].base, 0,
            smp_cpus, false, 0);
        riscv_aclint_swi_create(
            xiaohui_memmap[XIAOHUI_CLINT].base + 0xC000,
            0, smp_cpus, true, 0x1000);
        riscv_aclint_mtimer_create(
            xiaohui_memmap[XIAOHUI_CLINT].base + 0x4000,
            RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
            0, smp_cpus, RISCV_ACLINT_DEFAULT_MTIMECMP,
            RISCV_ACLINT_DEFAULT_MTIME,
            XIAOHUI_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
        riscv_aclint_mtimer_create(
            xiaohui_memmap[XIAOHUI_CLINT].base + 0xD000,
            0x3000, 0, smp_cpus, RISCV_ACLINT_DEFAULT_MTIMECMP,
            UINT32_MAX, XIAOHUI_ACLINT_DEFAULT_TIMEBASE_FREQ,
            false);
    }
    csky_uart_create(xiaohui_memmap[XIAOHUI_UART0].base, irqs[20],
                     clic_irqs[20], serial_hd(0));

    /* Create DMA device */
    DeviceState *dma_dev = sysbus_create_varargs("dw_dma_sw_axi",
                             xiaohui_memmap[XIAOHUI_DMA].base,
                             irqs[17], clic_irqs[17], NULL);

    /* Create IOPMP device if enabled */
    if (s->have_iopmp) {
        MemMapEntry iopmp_protect_memmap = { xiaohui_memmap[XIAOHUI_DRAM].base,
                                             /* Protect DRAM region */
                                             machine->ram_size };
        /* Create IOPMP device */
        DeviceState *iopmp_dev, *iopmp_disp_dev;
        StreamSink *iopmp_ss, *iopmp_disp_ss;

        iopmp_dev = iopmp_create(xiaohui_memmap[XIAOHUI_IOPMP].base,
                                irqs[IOPMP_IRQ], clic_irqs[IOPMP_IRQ], 256);

        iopmp_setup_system_memory(iopmp_dev, &iopmp_protect_memmap, 1, 0);

        iopmp_disp_dev = qdev_new(TYPE_RISCV_IOPMP_DISP);
        qdev_prop_set_uint32(DEVICE(iopmp_disp_dev), "target-num", 1);
        qdev_prop_set_uint32(DEVICE(iopmp_disp_dev), "stage-num", 1);
        qdev_realize(DEVICE(iopmp_disp_dev), NULL, &error_fatal);

        /* Add memmap information to dispatcher */
        iopmp_ss = (StreamSink *)&(RISCV_IOPMP(iopmp_dev)->txn_info_sink);
        iopmp_dispatcher_add_target(DEVICE(iopmp_disp_dev), iopmp_ss,
                                    iopmp_protect_memmap.base,
                                    iopmp_protect_memmap.size,
                                    0, 0);

        iopmp_disp_ss =
            (StreamSink *)&(RISCV_IOPMP_DISP(iopmp_disp_dev)->txn_info_sink);
        iopmp_setup_sink(iopmp_dev, iopmp_disp_ss);
        dw_dma_setup_sink(dma_dev, iopmp_disp_ss);
    }

    if (s->boot_linux) {
        sifive_test_create(xiaohui_memmap[XIAOHUI_TEST].base);

        /* load/create device tree */
        if (machine->dtb) {
            machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
            if (!machine->fdt) {
                error_report("load_device_tree() failed");
                exit(1);
            }
        } else {
            create_fdt(s, xiaohui_memmap, machine->ram_size,
                       machine->kernel_cmdline);
        }

        const char *firmware_name = riscv_default_firmware_name(&s->soc);
        firmware_end_addr =
        riscv_find_and_load_firmware(machine, firmware_name,
                                     xiaohui_memmap[XIAOHUI_DRAM].base, NULL);
        if (machine->kernel_filename) {
            kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc,
                                                            firmware_end_addr);
            kernel_entry = riscv_load_kernel(machine, &s->soc,
                                             kernel_start_addr, true, NULL);
            s->soc.harts[0].env.elf_start = kernel_entry;
        }
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
        csky_timer_create(xiaohui_memmap[XIAOHUI_TIMER].base,
                          &irqs[25], &clic_irqs[25], smp_cpus,
                          XIAOHUI_CLIC_IRQ_NUMS);
        /* Create CPR with PCU-based RVBA controlled by has_pcu */
        DeviceState *cpr_dev = qdev_new("xiaohui_cpr");
        qdev_prop_set_bit(cpr_dev, "use-pcu-rvba", s->have_pcu);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(cpr_dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(cpr_dev), 0,
                        xiaohui_memmap[XIAOHUI_AHB_CPR].base);

        /* Create per-core PCU instances when PCU is enabled */
        if (s->have_pcu) {
            for (i = 0; i < smp_cpus; i++) {
                hwaddr pcu_addr = xiaohui_memmap[XIAOHUI_PCU].base +
                                  (i / 2) * 0x80000 + (i % 2) * 0x20000;
                DeviceState *pcu_dev = qdev_new(TYPE_XIAOHUI_PCU);
                qdev_prop_set_uint32(pcu_dev, "hart-id", i);
                sysbus_realize_and_unref(SYS_BUS_DEVICE(pcu_dev),
                                         &error_fatal);
                sysbus_mmio_map(SYS_BUS_DEVICE(pcu_dev), 0, pcu_addr);
            }
        }
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

static char *xiaohui_get_aia(Object *obj, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);
    const char *val;
    switch (s->aia_type) {
    case XIAOHUI_AIA_TYPE_APLIC:
        val = "aplic";
        break;
    case XIAOHUI_AIA_TYPE_APLIC_IMSIC:
        val = "aplic-imsic";
        break;
    default:
        val = "none";
        break;
    }
    return g_strdup(val);
}

static void xiaohui_set_aia(Object *obj, const char *val, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);
    if (!strcmp(val, "none")) {
        s->aia_type = XIAOHUI_AIA_TYPE_NONE;
    } else if (!strcmp(val, "aplic")) {
        s->aia_type = XIAOHUI_AIA_TYPE_APLIC;
    } else if (!strcmp(val, "aplic-imsic")) {
        s->aia_type = XIAOHUI_AIA_TYPE_APLIC_IMSIC;
    } else {
        error_setg(errp, "Invalid AIA type: %s", val);
        error_append_hint(errp, "Valid values are none, aplic, aplic-imsic.\n");
    }
}

static bool xiaohui_get_iopmp(Object *obj, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);
    return s->have_iopmp;
}

static void xiaohui_set_iopmp(Object *obj, bool value, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);

    s->have_iopmp = value;
}

static char *xiaohui_get_aclint(Object *obj, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);
    const char *val;

    switch (s->aclint_type) {
    case XIAOHUI_ACLINT_TYPE_ACLINT:
        val = "aclint";
        break;
    case XIAOHUI_ACLINT_TYPE_PER_HART:
        val = "per-hart";
        break;
    default:
        val = "clint";
        break;
    }
    return g_strdup(val);
}

static void xiaohui_set_aclint(Object *obj, const char *val, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);

    if (!strcmp(val, "clint")) {
        s->aclint_type = XIAOHUI_ACLINT_TYPE_CLINT;
    } else if (!strcmp(val, "aclint")) {
        s->aclint_type = XIAOHUI_ACLINT_TYPE_ACLINT;
    } else if (!strcmp(val, "per-hart")) {
        s->aclint_type = XIAOHUI_ACLINT_TYPE_PER_HART;
    } else {
        error_setg(errp, "Invalid ACLINT type: %s", val);
        error_append_hint(errp,
            "Valid values are clint, aclint, per-hart.\n");
    }
}

static bool xiaohui_get_pcu(Object *obj, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);
    return s->have_pcu;
}

static void xiaohui_set_pcu(Object *obj, bool value, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);

    s->have_pcu = value;
}

static bool xiaohui_get_clic_v0p10(Object *obj, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);
    return s->clic_v0p10;
}

static void xiaohui_set_clic_v0p10(Object *obj, bool value, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);

    s->clic_v0p10 = value;
}

static bool xiaohui_get_sync_monchipba(Object *obj, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);
    return s->sync_monchipba;
}

static void xiaohui_set_sync_monchipba(Object *obj, bool value, Error **errp)
{
    RISCVXiaohuiState *s = RISCV_XIAOHUI_MACHINE(obj);

    s->sync_monchipba = value;
}

static void xiaohui_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V xiaohui";
    mc->default_ram_id = "riscv.xiaohui.dram";
    mc->init = xiaohui_init;
    mc->max_cpus = 16; /* hardcoded limit in BBL */
    mc->default_cpu_type = RISCV_CPU_TYPE_NAME("c907fdvm");
    mc->default_ram_size = 4 * GiB;

    object_class_property_add_bool(oc, "linux", xiaohui_get_linux,
                                   xiaohui_set_linux);
    object_class_property_set_description(oc, "linux",
                                          "Set on/off to show whether to "
                                          "boot linux or not");

    object_class_property_add_str(oc, "aia", xiaohui_get_aia,
                                 xiaohui_set_aia);
    object_class_property_set_description(oc, "aia",
        "Set AIA type: none, aplic, or aplic-imsic (default: none)");

    object_class_property_add_bool(oc, "iopmp", xiaohui_get_iopmp,
                                   xiaohui_set_iopmp);
    object_class_property_set_description(oc, "iopmp",
                                          "Set on/off to enable IOPMP support");

    object_class_property_add_str(oc, "aclint", xiaohui_get_aclint,
                                  xiaohui_set_aclint);
    object_class_property_set_description(oc, "aclint",
        "Set ACLINT type: clint, aclint, or per-hart (default: clint)");

    object_class_property_add_bool(oc, "pcu", xiaohui_get_pcu,
                                   xiaohui_set_pcu);
    object_class_property_set_description(oc, "pcu",
        "Set on/off to enable PCU support (default: off)");

    object_class_property_add_bool(oc, "clic-v0p10", xiaohui_get_clic_v0p10,
                                   xiaohui_set_clic_v0p10);
    object_class_property_set_description(oc, "clic-v0p10",
        "Set on/off to enable CLIC v0.10 support");

    object_class_property_add_bool(oc, "sync-monchipba",
        xiaohui_get_sync_monchipba,
        xiaohui_set_sync_monchipba);
    object_class_property_set_description(oc, "sync-monchipba",
        "Set on/off to enable xt_monchipba synchronization");
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
