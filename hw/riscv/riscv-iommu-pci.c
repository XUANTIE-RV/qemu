/*
 * QEMU emulation of an RISC-V IOMMU
 *
 * Copyright (C) 2022-2023 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/riscv_hart.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "qom/object.h"

#include "cpu_bits.h"
#include "riscv-iommu.h"
#include "riscv-iommu-bits.h"

/* RISC-V IOMMU PCI Device Emulation */

typedef struct RISCVIOMMUStatePci {
    PCIDevice        pci;     /* Parent PCIe device state */
    uint16_t         vendor_id;
    uint16_t         device_id;
    uint8_t          revision;
    MemoryRegion     bar0;    /* PCI BAR (including MSI-x config) */
    RISCVIOMMUState  iommu;   /* common IOMMU state */
} RISCVIOMMUStatePci;

/* interrupt delivery callback */
static void riscv_iommu_pci_notify(RISCVIOMMUState *iommu, unsigned vector)
{
    RISCVIOMMUStatePci *s = container_of(iommu, RISCVIOMMUStatePci, iommu);

    if (msix_enabled(&(s->pci))) {
        msix_notify(&(s->pci), vector);
    }
}

static void riscv_iommu_pci_realize(PCIDevice *dev, Error **errp)
{
    RISCVIOMMUStatePci *s = DO_UPCAST(RISCVIOMMUStatePci, pci, dev);
    RISCVIOMMUState *iommu = &s->iommu;
    uint8_t *pci_conf = dev->config;
    Error *err = NULL;

    pci_set_word(pci_conf + PCI_VENDOR_ID, s->vendor_id);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, s->vendor_id);
    pci_set_word(pci_conf + PCI_DEVICE_ID, s->device_id);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, s->device_id);
    pci_set_byte(pci_conf + PCI_REVISION_ID, s->revision);

    /* Set device id for trace / debug */
    DEVICE(iommu)->id = g_strdup_printf("%02x:%02x.%01x",
        pci_dev_bus_num(dev), PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
    qdev_realize(DEVICE(iommu), NULL, errp);

    memory_region_init(&s->bar0, OBJECT(s), "riscv-iommu-bar0",
        QEMU_ALIGN_UP(memory_region_size(&iommu->regs_mr), TARGET_PAGE_SIZE));
    memory_region_add_subregion(&s->bar0, 0, &iommu->regs_mr);

    pcie_endpoint_cap_init(dev, 0);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64, &s->bar0);

    int ret = msix_init(dev, RISCV_IOMMU_INTR_COUNT + 1,
                        &s->bar0, 0, RISCV_IOMMU_REG_MSI_CONFIG,
                        &s->bar0, 0, RISCV_IOMMU_REG_MSI_CONFIG + 256, 0, &err);

    if (ret == -ENOTSUP) {
        /*
         * MSI-x is not supported by the platform.
         * Driver should use timer/polling based notification handlers.
         */
        warn_report_err(err);
    } else if (ret < 0) {
        error_propagate(errp, err);
        return;
    } else {
        /* mark all allocated MSIx vectors as used. */
        msix_vector_use(dev, RISCV_IOMMU_INTR_CQ);
        msix_vector_use(dev, RISCV_IOMMU_INTR_FQ);
        msix_vector_use(dev, RISCV_IOMMU_INTR_PM);
        msix_vector_use(dev, RISCV_IOMMU_INTR_PQ);
        iommu->notify = riscv_iommu_pci_notify;
    }

    PCIBus *bus = pci_device_root_bus(dev);
    if (!bus) {
        error_setg(errp, "can't find PCIe root port for %02x:%02x.%x",
            pci_bus_num(pci_get_bus(dev)), PCI_SLOT(dev->devfn),
            PCI_FUNC(dev->devfn));
        return;
    }

    riscv_iommu_pci_setup_iommu(iommu, bus, errp);
}

static void riscv_iommu_pci_exit(PCIDevice *pci_dev)
{
    pci_setup_iommu(pci_device_root_bus(pci_dev), NULL, NULL);
}

static const VMStateDescription riscv_iommu_vmstate = {
    .name = "riscv-iommu",
    .unmigratable = 1
};

static void riscv_iommu_pci_init(Object *obj)
{
    RISCVIOMMUStatePci *s = RISCV_IOMMU_PCI(obj);
    RISCVIOMMUState *iommu = &s->iommu;

    object_initialize_child(obj, "iommu", iommu, TYPE_RISCV_IOMMU);
    qdev_alias_all_properties(DEVICE(iommu), obj);
}

static Property riscv_iommu_pci_properties[] = {
    DEFINE_PROP_UINT16("vendor-id", RISCVIOMMUStatePci, vendor_id,
                       PCI_VENDOR_ID_REDHAT),
    DEFINE_PROP_UINT16("device-id", RISCVIOMMUStatePci, device_id,
                       PCI_DEVICE_ID_REDHAT_RISCV_IOMMU),
    DEFINE_PROP_UINT8("revision", RISCVIOMMUStatePci, revision, 0x01),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_iommu_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = riscv_iommu_pci_realize;
    k->exit = riscv_iommu_pci_exit;
    k->class_id = 0x0806;
    dc->desc = "RISCV-IOMMU DMA Remapping device";
    dc->vmsd = &riscv_iommu_vmstate;
    dc->hotpluggable = false;
    dc->user_creatable = true;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, riscv_iommu_pci_properties);
}

static const TypeInfo riscv_iommu_pci = {
    .name = TYPE_RISCV_IOMMU_PCI,
    .parent = TYPE_PCI_DEVICE,
    .class_init = riscv_iommu_pci_class_init,
    .instance_init = riscv_iommu_pci_init,
    .instance_size = sizeof(RISCVIOMMUStatePci),
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};

static void riscv_iommu_register_pci_types(void)
{
    type_register_static(&riscv_iommu_pci);
}

type_init(riscv_iommu_register_pci_types);
