/*
 * QEMU RISC-V CPU QOM header (target agnostic)
 *
 * Copyright (c) 2023 Ventana Micro Systems Inc.
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

#ifndef RISCV_CPU_QOM_H
#define RISCV_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_RISCV_CPU "riscv-cpu"
#define TYPE_RISCV_DYNAMIC_CPU "riscv-dynamic-cpu"
#define TYPE_RISCV_VENDOR_CPU "riscv-vendor-cpu"
#define TYPE_RISCV_BARE_CPU "riscv-bare-cpu"

#define RISCV_CPU_TYPE_SUFFIX "-" TYPE_RISCV_CPU
#define RISCV_CPU_TYPE_NAME(name) (name RISCV_CPU_TYPE_SUFFIX)

#define TYPE_RISCV_CPU_ANY              RISCV_CPU_TYPE_NAME("any")
#define TYPE_RISCV_CPU_MAX              RISCV_CPU_TYPE_NAME("max")
#define TYPE_RISCV_CPU_BASE32           RISCV_CPU_TYPE_NAME("rv32")
#define TYPE_RISCV_CPU_BASE64           RISCV_CPU_TYPE_NAME("rv64")
#define TYPE_RISCV_CPU_BASE128          RISCV_CPU_TYPE_NAME("x-rv128")
#define TYPE_RISCV_CPU_RV32I            RISCV_CPU_TYPE_NAME("rv32i")
#define TYPE_RISCV_CPU_RV32E            RISCV_CPU_TYPE_NAME("rv32e")
#define TYPE_RISCV_CPU_RV64I            RISCV_CPU_TYPE_NAME("rv64i")
#define TYPE_RISCV_CPU_RV64E            RISCV_CPU_TYPE_NAME("rv64e")
#define TYPE_RISCV_CPU_RVA22U64         RISCV_CPU_TYPE_NAME("rva22u64")
#define TYPE_RISCV_CPU_RVA22S64         RISCV_CPU_TYPE_NAME("rva22s64")
#define TYPE_RISCV_CPU_RVB23U64         RISCV_CPU_TYPE_NAME("rvb23u64")
#define TYPE_RISCV_CPU_RVB23S64         RISCV_CPU_TYPE_NAME("rvb23s64")
#define TYPE_RISCV_CPU_IBEX             RISCV_CPU_TYPE_NAME("lowrisc-ibex")
#define TYPE_RISCV_CPU_SHAKTI_C         RISCV_CPU_TYPE_NAME("shakti-c")
#define TYPE_RISCV_CPU_SIFIVE_E31       RISCV_CPU_TYPE_NAME("sifive-e31")
#define TYPE_RISCV_CPU_SIFIVE_E34       RISCV_CPU_TYPE_NAME("sifive-e34")
#define TYPE_RISCV_CPU_SIFIVE_E51       RISCV_CPU_TYPE_NAME("sifive-e51")
#define TYPE_RISCV_CPU_SIFIVE_U34       RISCV_CPU_TYPE_NAME("sifive-u34")
#define TYPE_RISCV_CPU_SIFIVE_U54       RISCV_CPU_TYPE_NAME("sifive-u54")
#define TYPE_RISCV_CPU_THEAD_C906       RISCV_CPU_TYPE_NAME("thead-c906")
#define TYPE_RISCV_CPU_VEYRON_V1        RISCV_CPU_TYPE_NAME("veyron-v1")
#define TYPE_RISCV_CPU_HOST             RISCV_CPU_TYPE_NAME("host")
#define TYPE_RISCV_CPU_E902             RISCV_CPU_TYPE_NAME("e902")
#define TYPE_RISCV_CPU_E902T            RISCV_CPU_TYPE_NAME("e902t")
#define TYPE_RISCV_CPU_E902M            RISCV_CPU_TYPE_NAME("e902m")
#define TYPE_RISCV_CPU_E902MT           RISCV_CPU_TYPE_NAME("e902mt")
#define TYPE_RISCV_CPU_E906             RISCV_CPU_TYPE_NAME("e906")
#define TYPE_RISCV_CPU_E906F            RISCV_CPU_TYPE_NAME("e906f")
#define TYPE_RISCV_CPU_E906FD           RISCV_CPU_TYPE_NAME("e906fd")
#define TYPE_RISCV_CPU_E906FDP          RISCV_CPU_TYPE_NAME("e906fdp")
#define TYPE_RISCV_CPU_E906P            RISCV_CPU_TYPE_NAME("e906p")
#define TYPE_RISCV_CPU_E906FP           RISCV_CPU_TYPE_NAME("e906fp")
#define TYPE_RISCV_CPU_E907             RISCV_CPU_TYPE_NAME("e907")
#define TYPE_RISCV_CPU_E907F            RISCV_CPU_TYPE_NAME("e907f")
#define TYPE_RISCV_CPU_E907FD           RISCV_CPU_TYPE_NAME("e907fd")
#define TYPE_RISCV_CPU_E907FDP          RISCV_CPU_TYPE_NAME("e907fdp")
#define TYPE_RISCV_CPU_E907P            RISCV_CPU_TYPE_NAME("e907p")
#define TYPE_RISCV_CPU_E907FP           RISCV_CPU_TYPE_NAME("e907fp")
#define TYPE_RISCV_CPU_C910             RISCV_CPU_TYPE_NAME("c910")
#define TYPE_RISCV_CPU_C910V            RISCV_CPU_TYPE_NAME("c910v")
#define TYPE_RISCV_CPU_C920             RISCV_CPU_TYPE_NAME("c920")
#define TYPE_RISCV_CPU_C906             RISCV_CPU_TYPE_NAME("c906")
#define TYPE_RISCV_CPU_C906FD           RISCV_CPU_TYPE_NAME("c906fd")
#define TYPE_RISCV_CPU_C906FDV          RISCV_CPU_TYPE_NAME("c906fdv")
#define TYPE_RISCV_CPU_C908I            RISCV_CPU_TYPE_NAME("c908i")
#define TYPE_RISCV_CPU_C908             RISCV_CPU_TYPE_NAME("c908")
#define TYPE_RISCV_CPU_C908V            RISCV_CPU_TYPE_NAME("c908v")
#define TYPE_RISCV_CPU_C960             RISCV_CPU_TYPE_NAME("c960")
#define TYPE_RISCV_CPU_R910             RISCV_CPU_TYPE_NAME("r910")
#define TYPE_RISCV_CPU_R920             RISCV_CPU_TYPE_NAME("r920")
#define TYPE_RISCV_CPU_C907             RISCV_CPU_TYPE_NAME("c907")
#define TYPE_RISCV_CPU_C907FD           RISCV_CPU_TYPE_NAME("c907fd")
#define TYPE_RISCV_CPU_C907FDV          RISCV_CPU_TYPE_NAME("c907fdv")
#define TYPE_RISCV_CPU_C907FDVM         RISCV_CPU_TYPE_NAME("c907fdvm")
#define TYPE_RISCV_CPU_C907RV64         RISCV_CPU_TYPE_NAME("c907-rv64")
#define TYPE_RISCV_CPU_C907FDRV64       RISCV_CPU_TYPE_NAME("c907fd-rv64")
#define TYPE_RISCV_CPU_C907FDVRV64      RISCV_CPU_TYPE_NAME("c907fdv-rv64")
#define TYPE_RISCV_CPU_C907FDVMRV64     RISCV_CPU_TYPE_NAME("c907fdvm-rv64")
#define TYPE_RISCV_CPU_C907RV32         RISCV_CPU_TYPE_NAME("c907-rv32")
#define TYPE_RISCV_CPU_C907FDRV32       RISCV_CPU_TYPE_NAME("c907fd-rv32")
#define TYPE_RISCV_CPU_C907FDVRV32      RISCV_CPU_TYPE_NAME("c907fdv-rv32")
#define TYPE_RISCV_CPU_C907FDVMRV32     RISCV_CPU_TYPE_NAME("c907fdvm-rv32")
#define TYPE_RISCV_CPU_C910V2           RISCV_CPU_TYPE_NAME("c910v2")
#define TYPE_RISCV_CPU_C920V2           RISCV_CPU_TYPE_NAME("c920v2")
#define TYPE_RISCV_CPU_C910V3           RISCV_CPU_TYPE_NAME("c910v3")
#define TYPE_RISCV_CPU_C910V3_CP        RISCV_CPU_TYPE_NAME("c910v3-cp")
#define TYPE_RISCV_CPU_C920V3           RISCV_CPU_TYPE_NAME("c920v3")
#define TYPE_RISCV_CPU_C920V3_CP        RISCV_CPU_TYPE_NAME("c920v3-cp")
#define TYPE_RISCV_CPU_C908IRV32        RISCV_CPU_TYPE_NAME("c908i-rv32")
#define TYPE_RISCV_CPU_C908RV32         RISCV_CPU_TYPE_NAME("c908-rv32")
#define TYPE_RISCV_CPU_C908VRV32        RISCV_CPU_TYPE_NAME("c908v-rv32")
#define TYPE_RISCV_CPU_R908             RISCV_CPU_TYPE_NAME("r908")
#define TYPE_RISCV_CPU_R908FD           RISCV_CPU_TYPE_NAME("r908fd")
#define TYPE_RISCV_CPU_R908FDV          RISCV_CPU_TYPE_NAME("r908fdv")
#define TYPE_RISCV_CPU_R908_CP          RISCV_CPU_TYPE_NAME("r908-cp")
#define TYPE_RISCV_CPU_R908FD_CP        RISCV_CPU_TYPE_NAME("r908fd-cp")
#define TYPE_RISCV_CPU_R908FDV_CP       RISCV_CPU_TYPE_NAME("r908fdv-cp")
#define TYPE_RISCV_CPU_R908RV32         RISCV_CPU_TYPE_NAME("r908-rv32")
#define TYPE_RISCV_CPU_R908FDRV32       RISCV_CPU_TYPE_NAME("r908fd-rv32")
#define TYPE_RISCV_CPU_R908FDVRV32      RISCV_CPU_TYPE_NAME("r908fdv-rv32")
#define TYPE_RISCV_CPU_R908_CPRV32      RISCV_CPU_TYPE_NAME("r908-cp-rv32")
#define TYPE_RISCV_CPU_R908FD_CPRV32    RISCV_CPU_TYPE_NAME("r908fd-cp-rv32")
#define TYPE_RISCV_CPU_R908FDV_CPRV32   RISCV_CPU_TYPE_NAME("r908fdv-cp-rv32")


OBJECT_DECLARE_CPU_TYPE(RISCVCPU, RISCVCPUClass, RISCV_CPU)

#endif /* RISCV_CPU_QOM_H */
