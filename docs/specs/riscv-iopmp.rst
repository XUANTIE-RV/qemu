.. _riscv-iopmp:

RISC-V IOPMP support for RISC-V machines
========================================

IOPMP support is based on `IOPMP specification version 0.7`_. The device is
available on the RISC-V virt machine but is disabled by default. To enable
iopmp device, use the 'iopmp' machine option

.. code-block:: bash

  $ qemu-system-riscv64 -M virt,iopmp=on

On the virt board, the number of IOPMP device is fixed at 1, and its protect
region is fixed to 0x0~0xFFFFFFFF.

To configure IOPMP device, modify gloal driver property

.. code-block:: bash

  -global driver=riscv_iopmp, property=<property>, value=<value>

Below are the IOPMP device properties and their default values:

- mdcfg_fmt: 1 (Options: 0/1/2)
- srcmd_fmt: 0 (Options: 0/1/2)
- tor_en: true (Options: true/false)
- sps_en: false (Options: true/false)
- prient_prog: true (Options: true/false)
- rrid_transl_en: false (Options: true/false)
- rrid_transl_prog: false (Options: true/false)
- chk_x: true (Options: true/false)
- no_x: false (Options: true/false)
- no_w: false (Options: true/false)
- stall_en: false (Options: true/false)
- peis: true (Options: true/false)
- pees: true (Options: true/false)
- mfr_en: true (Options: true/false)
- md_entry_num: 5 (IMP: Valid only for mdcfg_fmt 1/2)
- md_num: 8 (Range: 0-63)
- rrid_num: 16 (Range: srcmd_fmt ≠ 2: 0-65535, srcmd_fmt = 2: 0-32)
- entry_num: 48 (Range: 0-IMP. For mdcfg_fmt = 1,
  it is fixed as md_num * (md_entry_num + 1).
  Entry registers must not overlap with other registers.)
- prio_entry: 65535 (Range: 0-IMP. If prio_entry > entry_num,
  it will be set to entry_num.)
- rrid_transl: 0x0 (Range: 0-65535)
- entry_offset: 0x4000 (IMP: Entry registers must not overlap
  with other registers.)
- err_rdata: 0x0 (uint32. Specifies the value used in responses to
  read transactions when errors are suppressed)
- msi_en: false (Options: true/false)
- msidata: 12 (Range: 1-1023)
- stall_violation_en: true (Options: true/false)
- err_msiaddr: 0x24000000 (lower-part 32-bit address)
- err_msiaddrh: 0x0 (higher-part 32-bit address)
- msi_rrid: 0 (Range: 0-65535. Specifies the rrid used by the IOPMP to send
  the MSI.)

.. _IOPMP specification version 0.7: https://github.com/riscv-non-isa/iopmp-spec/releases/download/v0.7/iopmp-v0.7.pdf
