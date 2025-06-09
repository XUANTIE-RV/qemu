riscv_ss = ss.source_set()
riscv_ss.add(files('boot.c'), fdt)
riscv_ss.add(when: 'CONFIG_RISCV_NUMA', if_true: files('numa.c'))
riscv_ss.add(files('riscv_hart.c'))
riscv_ss.add(when: 'CONFIG_OPENTITAN', if_true: files('opentitan.c'))
riscv_ss.add(when: 'CONFIG_RISCV_VIRT', if_true: files('virt.c'))
riscv_ss.add(when: 'CONFIG_SERVER_PLATFORM_REF', if_true: files('server_platform_ref.c'))
riscv_ss.add(when: 'CONFIG_SHAKTI_C', if_true: files('shakti_c.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_E', if_true: files('sifive_e.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_U', if_true: files('sifive_u.c'))
riscv_ss.add(when: 'CONFIG_SPIKE', if_true: files('spike.c'))
riscv_ss.add(when: 'CONFIG_MICROCHIP_PFSOC', if_true: files('microchip_pfsoc.c'))
riscv_ss.add(when: 'CONFIG_ACPI', if_true: files('virt-acpi-build.c'))
riscv_ss.add(when: 'CONFIG_RISCV_SMARTL', if_true: files('smartl.c'))
riscv_ss.add(when: 'CONFIG_RISCV_SMARTH', if_true: files('smarth.c'))
riscv_ss.add(when: 'CONFIG_RISCV_XIAOHUI', if_true: files('xiaohui.c'))
riscv_ss.add(when: 'CONFIG_THEAD_PMU', if_true: files('thead_pmu.c'))
if csky_dynsoc
    riscv_ss.add(files('dynsoc.c'))
    riscv_ss.add(when: ['CONFIG_RISCV_DUMMYH'], if_true: files('dummyh.c'))
endif
riscv_ss.add(when: 'CONFIG_RISCV_IOMMU', if_true: files('riscv-iommu.c', 'riscv-iommu-pci.c'))
riscv_ss.add(when: 'CONFIG_RISCV_CBQRI',
              if_true: files('cbqri_capacity.c', 'cbqri_bandwidth.c'))
riscv_ss.add(when: 'CONFIG_CBQRI_EXAMPLE_SOC', if_true: files('cbqri_example_soc.c'))

hw_arch += {'riscv': riscv_ss}
