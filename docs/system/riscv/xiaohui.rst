Xiaohui RISC-V Platform (``xiaohui``)
=====================================

The ``xiaohui`` machine emulates a Xuantie-based RISC-V SoC platform developed
by Alibaba T-Head. It supports both **Linux boot** (``linux=on``) and
**bare-metal / RTOS boot** (``linux=off``) modes, making it suitable for full
OS development as well as firmware and RTOS validation.

- **Default CPU**: ``c907fdvm`` (Xuantie C907)
- **Max CPUs**: 16
- **Default RAM**: 4 GiB
- **Timebase Frequency**: 25 MHz

Memory map layout
-----------------

The table below shows the physical address map. Some regions are conditional
on machine options.

=============================  ======  =====================  =========================
Address Range                  Size    Device                 Condition
=============================  ======  =====================  =========================
0x00_0000_0000-0x00_000F_FFFF  1 MiB   SRAM (Boot ROM)        Always
0x00_0800_0000-0x00_0BFF_FFFF  64 MiB  PLIC                   aia=none
0x00_0800_0000-0x00_0800_7FFF  32 KiB  APLIC M-mode           aia=aplic
0x00_0802_0000-0x00_0802_7FFF  32 KiB  APLIC S-mode           aia=aplic
0x00_0800_0000-0x00_0800_3FFF  16 KiB  APLIC M-mode           aia=aplic-imsic
0x00_0800_4000-0x00_0800_7FFF  16 KiB  APLIC S-mode           aia=aplic-imsic
0x00_0804_0000-0x00_0804_FFFF  64 KiB  ACLINT                 aclint=aclint
0x00_0C00_0000-0x00_0C00_FFFF  64 KiB  CLINT                  aclint=clint (default)
0x00_0C01_0000-0x00_0C01_4FFF  20 KiB  CLIC                   Always
0x00_1800_0000-0x00_1800_FFFF  64 KiB  DW DMA                 Always
0x00_1803_0000-0x00_1803_FFFF  64 KiB  AHB CPR                linux=off only
0x00_1900_1000-0x00_1900_1FFF  4 KiB   Timer                  linux=off only
0x00_1900_D000-0x00_1900_DFFF  4 KiB   UART0 (DW APB UART)    Always
0x00_2680_8000+                4 KiB   PCU (per core)         pcu=on
0x00_26F0_0000-0x00_26FF_FFFF  1 MiB   IOPMP                  iopmp=on
0x00_4C00_0000-0x00_4C00_0FFF  4 KiB   Test / Exit device     Always
0x00_5000_0000+                conf.   DRAM                   Always
0xF000_0000_0000               64 KiB  IMSIC M-mode           aia=aplic-imsic
0xF000_1000_0000               4 MiB   IMSIC S-mode           aia=aplic-imsic
=============================  ======  =====================  =========================

.. note::

   **APLIC layout differs by aia mode**:

   - ``aia=aplic`` (direct delivery) keeps the 32 KiB-each layout:
     APLIC M @ ``0x00_0800_0000``, APLIC S @ ``0x00_0802_0000``.
   - ``aia=aplic-imsic`` uses a compact 16 KiB-each layout starting from
     ``0x00_0800_0000``: APLIC M @ ``0x00_0800_0000``,
     APLIC S @ ``0x00_0800_4000``. 16 KiB equals ``APLIC_MIN_SIZE``
     (see ``include/hw/intc/riscv_aplic.h``), which is sufficient because
     interrupt delivery is offloaded to IMSIC via MSI in this mode.

   **Per-hart ACLINT** (``aclint=per-hart``): Each hart gets a dedicated
   ACLINT MTIMER at a platform-defined address (see source for the full table).

   **PCU addresses** follow a cluster-based layout:
   ``base + (hart_id / 2) * 0x80000 + (hart_id % 2) * 0x20000``

System architecture diagram
---------------------------

.. code-block:: none

                                 Xiaohui SoC
   ════════════════════════════════════════════════════════════════════

                       ┌────────┐  ┌────────┐          ┌────────┐
                       │ Hart 0 │  │ Hart 1 │  . . .   │ Hart N │ (<=16)
                       └┬──┬──┬─┘  └┬──┬──┬─┘          └┬──┬──┬─┘
                        │  │  │     │  │  │              │  │  │
                  msip/ │  │  │     │  │  │              │  │  │
                  mtip  │  │  │     │  │  │              │  │  │
                        │ meip│     │ meip│              │ meip│
                        │  │ clic   │  │ clic            │  │ clic
                        │  │  │     │  │  │              │  │  │
     ┌──────────────────┘  │  │     │  │  │              │  │  │
     │  ┌──────────────────┘  │     │  │  │              │  │  │
     │  │  ┌──────────────────┘     │  │  │              │  │  │
     │  │  │     (same 3 IRQ lines to each hart)         │  │  │
     │  │  │                                             │  │  │
   ┌─┴──┴──┴─────────────────────────────────────────────┴──┴──┴──┐
   │                     Interrupt Controllers                     │
   │                                                               │
   │  ┌─────────────┐   ┌────────────────┐   ┌──────────────────┐ │
   │  │ CLINT/ACLINT│   │ PLIC / APLIC / │   │ CLIC             │ │
   │  │ /Per-hart   │   │ APLIC+IMSIC    │   │ (4096 IRQs/hart) │ │
   │  │             │   │                │   │                  │ │
   │  │ >>msip/mtip │   │ >>meip(ExtIRQ) │   │ >>clic_irq      │ │
   │  └─────────────┘   └───────┬────────┘   └────────┬─────────┘ │
   │                            │                     │            │
   └────────────────────────────┼─────────────────────┼────────────┘
                                │                     │
                     ┌──────────┴─────────────────────┴──────────┐
                     │          Peripherals (dual-wired)          │
                     │     Each IRQ connects to BOTH paths:       │
                     │         PLIC/APLIC  and  CLIC              │
                     │                                            │
                     │  ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ │
                     │  │ UART0 │ │  DMA  │ │ IOPMP │ │ Timer │ │
                     │  │IRQ 20 │ │IRQ 17 │ │IRQ 18 │ │IRQ 25 │ │
                     │  └───────┘ └───────┘ └───────┘ └───────┘ │
                     └───────────────────────────────────────────┘

     ┌──────────┐  ┌──────────┐  ┌─────────┐  ┌──────────┐
     │   SRAM   │  │   DRAM   │  │ CPR/PCU │  │   Test   │
     │ (Boot ROM│  │ (4 GiB+) │  │(linux=  │  │  / Exit  │
     │  1 MiB)  │  │          │  │  off)   │  │          │
     └──────────┘  └──────────┘  └─────────┘  └──────────┘

Interrupt wiring diagram
-------------------------

Each peripheral IRQ is **dual-wired** to both PLIC/APLIC and CLIC.

.. code-block:: none

   ┌────────────────────────────────────────────────────────────────┐
   │ 1. Timer / IPI path (CLINT/ACLINT ──► Harts)                  │
   │                                                                │
   │    CLINT/ACLINT ──msip (M-mode SW IRQ)──────► Hart 0..N       │
   │                 ──ssip (S-mode SW IRQ)──────► Hart 0..N       │
   │                 ──mtip (M-mode Timer IRQ)───► Hart 0..N       │
   └────────────────────────────────────────────────────────────────┘

   ┌────────────────────────────────────────────────────────────────┐
   │ 2. External IRQ path (Peripherals ──► PLIC/APLIC ──► Harts)   │
   │                                                                │
   │    UART0 ──IRQ 20──┐                                           │
   │    DMA   ──IRQ 17──┤   PLIC/APLIC   ──meip (Ext IRQ)──► Hart 0│
   │    IOPMP ──IRQ 18──┤   (S-mode)                        ► Hart 1│
   │    Timer ──IRQ 25──┘                                   ► ...   │
   │        (linux=off)                                     ► Hart N│
   └────────────────────────────────────────────────────────────────┘

   ┌────────────────────────────────────────────────────────────────┐
   │ 3. CLIC path (Peripherals ──► CLIC ──► Harts)                 │
   │                                                                │
   │    UART0 ──IRQ 20──┐                                           │
   │    DMA   ──IRQ 17──┤   CLIC          ──clic_irq──────► Hart 0 │
   │    IOPMP ──IRQ 18──┤   (4096/hart)                    ► Hart 1 │
   │    Timer ──IRQ 25──┘                                  ► ...    │
   │        (linux=off)                                    ► Hart N │
   └────────────────────────────────────────────────────────────────┘

   ┌────────────────────────────────────────────────────────────────┐
   │ 4. AIA MSI path (aia=aplic-imsic only)                        │
   │                                                                │
   │    Device ──wired──► APLIC_S ──MSI──► IMSIC_S ──meip──► Hart  │
   │                         │                         (S-mode)     │
   │              APLIC_M (parent) ──MSI──► IMSIC_M ──meip──► Hart  │
   │                                                   (M-mode)    │
   └────────────────────────────────────────────────────────────────┘

Machine-specific options
------------------------

- linux=[on|off]

  Boot mode selection. When ``on``, the machine boots Linux via OpenSBI +
  kernel. When ``off`` (default), it boots bare-metal or RTOS firmware
  directly.

- aia=[none|aplic|aplic-imsic]

  Interrupt controller selection. ``none`` (default) uses SiFive PLIC.
  ``aplic`` uses APLIC in direct delivery mode. ``aplic-imsic`` uses the
  full AIA stack with APLIC forwarding wired interrupts as MSIs to IMSIC.

- aclint=[clint|aclint|per-hart]

  Timer/IPI controller selection. ``clint`` (default) uses SiFive CLINT.
  ``aclint`` uses RISC-V ACLINT. ``per-hart`` assigns a dedicated ACLINT
  MTIMER to each hart at platform-specific addresses.

- iopmp=[on|off]

  When ``on``, an IOPMP device is added to protect DRAM from unauthorized
  DMA transactions. Default is ``off``.

- pcu=[on|off]

  When ``on``, per-core PCU (Power Control Unit) instances are created with
  cluster-based addressing, and the CPR device has ``use-pcu-rvba`` enabled.
  Default is ``off``.

- clic-v0p10=[on|off]

  When ``on``, the CLIC uses the v0.10 specification (older revision).
  Default is ``off``.

- sync-monchipba=[on|off]

  When ``on``, the ``xt_monchipba`` CSR is synchronized across all harts
  to the APLIC M-mode base address. Default is ``off``.

Linux boot mode (``linux=on``)
------------------------------

When ``linux=on`` is specified, the machine operates in a standard Linux boot
flow similar to the QEMU ``virt`` machine:

1. **Firmware**: OpenSBI (``fw_dynamic``) is loaded as the M-mode firmware
   via ``-bios``.
2. **Kernel**: A Linux kernel Image is loaded via ``-kernel``.
3. **Device Tree**: An FDT is auto-generated describing all devices,
   interrupt controllers, and memory layout.
4. **Boot sequence**: CPU starts from SRAM (reset vector at ``0x0``), which
   contains a small trampoline that jumps to OpenSBI in DRAM. OpenSBI
   initializes M-mode, then hands off to Linux in S-mode.
5. **Test device**: ``sifive_test`` is used for poweroff/reboot.

Devices available in Linux mode:

- UART0 (serial console, ``stdout-path`` in FDT)
- DW DMA controller
- IOPMP (if ``iopmp=on``)
- PLIC / APLIC+IMSIC (depending on ``aia`` option)
- CLINT / ACLINT (depending on ``aclint`` option)

Example: booting Linux:

.. code-block:: bash

  $ qemu-system-riscv64 \
      -M xiaohui,linux=on,aia=aplic-imsic,aclint=per-hart \
      -cpu xt-c930v-cp \
      -smp 4 -m 4G \
      -bios fw_dynamic.bin \
      -kernel Image \
      -nographic

RTOS / bare-metal boot mode (``linux=off``, default)
----------------------------------------------------

When ``linux=off`` (the default), the machine targets bare-metal firmware or
RTOS workloads:

1. **Kernel loading**: The ELF binary specified via ``-kernel`` is loaded
   directly, and the CPU reset vector is set to its entry point.
2. **No FDT**: No device tree is generated; firmware discovers devices via
   hardcoded addresses.
3. **CPU off by default**: Non-boot harts start in a powered-off state
   (``cpu-off=true``), to be brought up by firmware.
4. **Exit device**: ``csky_exit`` device (at TEST address) is used for
   simulation termination.

Additional devices in RTOS mode:

- **CLIC Timer** (``csky_timer``): Connected to IRQ 25 on both PLIC/APLIC
  and CLIC, with a 25 MHz timebase.
- **CPR** (Clock/Power/Reset controller): Mapped at ``AHB_CPR`` address.
  When ``pcu=on``, the ``use-pcu-rvba`` property is enabled.
- **PCU** (Power Control Unit, ``pcu=on``): Per-core power management
  instances using cluster-based addressing.

Example: running bare-metal firmware:

.. code-block:: bash

  $ qemu-system-riscv64 \
      -M xiaohui \
      -cpu c907fdvm \
      -kernel firmware.elf \
      -nographic

Example: RTOS with PCU and AIA:

.. code-block:: bash

  $ qemu-system-riscv64 \
      -M xiaohui,aia=aplic,pcu=on,aclint=per-hart \
      -cpu xt-c930v-cp \
      -smp 4 -m 4G \
      -kernel rtos.elf \
      -nographic

Interrupt controller configurations
------------------------------------

``aia=none`` (default) — SiFive PLIC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Standard SiFive PLIC at ``0x00_0800_0000``
- 1023 interrupt sources, 32 priority levels
- Supports M-mode and S-mode contexts

``aia=aplic`` — APLIC direct delivery
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- APLIC M-mode domain at ``0x00_0800_0000`` (parent)
- APLIC S-mode domain at ``0x00_0802_0000`` (child)
- 96 wired interrupt sources
- Direct interrupt delivery to hart contexts

``aia=aplic-imsic`` — Full AIA (APLIC + IMSIC)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- APLIC M-mode domain at ``0x00_0800_0000``, **16 KiB** (parent)
- APLIC S-mode domain at ``0x00_0800_4000``, **16 KiB** (child)
- IMSIC M-mode at ``0xF000_0000_0000`` (1 interrupt file per hart)
- IMSIC S-mode at ``0xF000_1000_0000`` (7 interrupt files per hart: 1 S + 6 VS)
- 2047 MSI identities per hart
- APLIC forwards wired interrupts as MSIs to IMSIC

.. note::

   APLIC M/S use a different (more compact) layout in this mode than in
   ``aia=aplic``. Each domain occupies the minimum legal aperture
   (``APLIC_MIN_SIZE`` = 16 KiB) because wired interrupts are converted
   to MSIs and routed via IMSIC, so the APLIC itself does not need the
   full 32 KiB region used by the direct-delivery mode.

Timer/IPI controller configurations
------------------------------------

``aclint=clint`` (default) — SiFive CLINT
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- CLINT at ``0x00_0C00_0000``
- MSWI + SSWI + MTIMER + Supervisor timer
- 25 MHz timebase

``aclint=aclint`` — RISC-V ACLINT
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- ACLINT at ``0x00_0804_0000``
- MSWI + SSWI + MTIMER
- 25 MHz timebase

``aclint=per-hart`` — Per-hart ACLINT
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Each hart gets a dedicated ACLINT MTIMER at a platform-specific address
- Enables independent per-hart timer control
- 25 MHz timebase
