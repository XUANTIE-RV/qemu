/*
 * CSKY simulation
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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#include <libxml/xmlstring.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "../include/hw/csky/dynsoc.h"
#include "compile_gen.h"


#define CSKYSIM_VERSION "cskysim v5.0.4 (QEMU V9.0.0)"
#define XML_MINVER      0.0
#define XML_MAXVER      2.0

static enum {
    USER_DEFINE_CPU_PROP = 1 << 0,
} cskysim_user_options;

char *cpu_610_mpu_list[] = {
    "ck610", "ck610ef", "ck610e", "ck610f"
};

char *cpu_610_mmu_list[] = {
    "ck610m", "ck610mef", "ck610me", "ck610mf"
};

struct cpu_info {
    const char *name;
    const char *arch;
    const char *abi;
};

static const struct cpu_info cpu_arr[] = {
    { .name = "ck510",       .arch = "csky",    .abi = "abiv1" },
    { .name = "ck520",       .arch = "csky",    .abi = "abiv1" },
    { .name = "ck610",       .arch = "csky",    .abi = "abiv1" },
    { .name = "ck610e",      .arch = "csky",    .abi = "abiv1" },
    { .name = "ck610f",      .arch = "csky",    .abi = "abiv1" },
    { .name = "ck610ef",     .arch = "csky",    .abi = "abiv1" },
    { .name = "ck801",       .arch = "csky",    .abi = "abiv2" },
    { .name = "ck801t",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck802",       .arch = "csky",    .abi = "abiv2" },
    { .name = "ck802h",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck802j",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck802t",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck802ht",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803",       .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803h",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803t",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ht",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803f",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fh",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803e",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803eh",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803et",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803eht",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ef",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efh",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ft",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803eft",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efht",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803r1",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803hr1",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803tr1",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803htr1",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fr1",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fhr1",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803er1",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ehr1",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803etr1",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ehtr1",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efr1",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efhr1",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ftr1",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fhtr1",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803eftr1",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efhtr1", .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803s",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803sf",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803sef",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803st",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803r2",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803hr2",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803tr2",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803htr2",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fr2",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fhr2",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803er2",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ehr2",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803etr2",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ehtr2",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efr2",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efhr2",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ftr2",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fhtr2",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803eftr2",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efhtr2", .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803r3",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803hr3",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803tr3",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803htr3",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fr3",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fvr3",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fhr3",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803er3",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ehr3",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803etr3",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ehtr3",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efr3",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efvr3",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efhr3",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803ftr3",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803fhtr3",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803eftr3",  .arch = "csky",    .abi = "abiv2" },
    { .name = "ck803efhtr3", .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804",       .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804e",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804ef",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804efh",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804efht",   .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804eft",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804eh",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804eht",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804et",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804f",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804fh",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804ft",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804h",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804ht",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck804t",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck805",       .arch = "csky",    .abi = "abiv2" },
    { .name = "ck805e",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck805f",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck805t",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck805ef",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck805et",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck805ft",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck805eft",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck807",       .arch = "csky",    .abi = "abiv2" },
    { .name = "ck807e",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck807f",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck807ef",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810",       .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810v",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810f",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810t",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810fv",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810tv",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810ft",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810ftv",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810e",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810et",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810ef",     .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810efm",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck810eft",    .arch = "csky",    .abi = "abiv2" },
    { .name = "ck860",       .arch = "csky",    .abi = "abiv2" },
    { .name = "ck860v",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck860f",      .arch = "csky",    .abi = "abiv2" },
    { .name = "ck860fv",     .arch = "csky",    .abi = "abiv2" },
    { .name = "e801",        .arch = "csky",    .abi = "abiv2" },
    { .name = "e802",        .arch = "csky",    .abi = "abiv2" },
    { .name = "e802t",       .arch = "csky",    .abi = "abiv2" },
    { .name = "e803",        .arch = "csky",    .abi = "abiv2" },
    { .name = "e803t",       .arch = "csky",    .abi = "abiv2" },
    { .name = "e804d",       .arch = "csky",    .abi = "abiv2" },
    { .name = "e804dt",      .arch = "csky",    .abi = "abiv2" },
    { .name = "e804f",       .arch = "csky",    .abi = "abiv2" },
    { .name = "e804ft",      .arch = "csky",    .abi = "abiv2" },
    { .name = "e804df",      .arch = "csky",    .abi = "abiv2" },
    { .name = "e804dft",     .arch = "csky",    .abi = "abiv2" },
    { .name = "c807",        .arch = "csky",    .abi = "abiv2" },
    { .name = "c807f",       .arch = "csky",    .abi = "abiv2" },
    { .name = "c807fv",      .arch = "csky",    .abi = "abiv2" },
    { .name = "c810",        .arch = "csky",    .abi = "abiv2" },
    { .name = "c810v",       .arch = "csky",    .abi = "abiv2" },
    { .name = "c810t",       .arch = "csky",    .abi = "abiv2" },
    { .name = "c810tv",      .arch = "csky",    .abi = "abiv2" },
    { .name = "c860",        .arch = "csky",    .abi = "abiv2" },
    { .name = "c860v",       .arch = "csky",    .abi = "abiv2" },
    { .name = "s802",        .arch = "csky",    .abi = "abiv2" },
    { .name = "s802t",       .arch = "csky",    .abi = "abiv2" },
    { .name = "s803",        .arch = "csky",    .abi = "abiv2" },
    { .name = "s803t",       .arch = "csky",    .abi = "abiv2" },
    { .name = "r807",        .arch = "csky",    .abi = "abiv2" },
    { .name = "r807f",       .arch = "csky",    .abi = "abiv2" },
    { .name = "i805",        .arch = "csky",    .abi = "abiv2" },
    { .name = "i805f",       .arch = "csky",    .abi = "abiv2" },
    { .name = "e902",        .arch = "riscv32", .abi = "riscv32" },
    { .name = "e902t",       .arch = "riscv32", .abi = "riscv32" },
    { .name = "e902m",       .arch = "riscv32", .abi = "riscv32" },
    { .name = "e902mt",      .arch = "riscv32", .abi = "riscv32" },
    { .name = "e906",        .arch = "riscv32", .abi = "riscv32" },
    { .name = "e906f",       .arch = "riscv32", .abi = "riscv32" },
    { .name = "e906fd",      .arch = "riscv32", .abi = "riscv32" },
    { .name = "e906p",       .arch = "riscv32", .abi = "riscv32" },
    { .name = "e906fp",      .arch = "riscv32", .abi = "riscv32" },
    { .name = "e906fdp",     .arch = "riscv32", .abi = "riscv32" },
    { .name = "e907",        .arch = "riscv32", .abi = "riscv32" },
    { .name = "e907f",       .arch = "riscv32", .abi = "riscv32" },
    { .name = "e907fd",      .arch = "riscv32", .abi = "riscv32" },
    { .name = "e907p",       .arch = "riscv32", .abi = "riscv32" },
    { .name = "e907fp",      .arch = "riscv32", .abi = "riscv32" },
    { .name = "e907fdp",     .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv32i",       .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv32ic",      .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv32im",      .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv32imac",    .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv32imc",     .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv32ec",      .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv32emc",     .arch = "riscv32", .abi = "riscv32" },
    { .name = "sifive-e31",  .arch = "riscv32", .abi = "riscv32" },
    { .name = "sifive-u34",  .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv32gcsu-v1.10.0",  .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv32gcsu-v1.9.1",  .arch = "riscv32", .abi = "riscv32" },
    { .name = "c960",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "c910",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "c910v",       .arch = "riscv64", .abi = "riscv64" },
    { .name = "c920",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "c910v2",      .arch = "riscv64", .abi = "riscv64" },
    { .name = "c920v2",      .arch = "riscv64", .abi = "riscv64" },
    { .name = "r910",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "r920",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "c908",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "c908i",       .arch = "riscv64", .abi = "riscv64" },
    { .name = "c908v",       .arch = "riscv64", .abi = "riscv64" },
    { .name = "c906",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "c906fd",      .arch = "riscv64", .abi = "riscv64" },
    { .name = "c906fdv",     .arch = "riscv64", .abi = "riscv64" },
    { .name = "c907",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "c907fd",      .arch = "riscv64", .abi = "riscv64" },
    { .name = "c907fdv",     .arch = "riscv64", .abi = "riscv64" },
    { .name = "c907fdvm",    .arch = "riscv64", .abi = "riscv64" },
    { .name = "c907-rv64",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "c907fd-rv64",      .arch = "riscv64", .abi = "riscv64" },
    { .name = "c907fdv-rv64",     .arch = "riscv64", .abi = "riscv64" },
    { .name = "c907fdvm-rv64",    .arch = "riscv64", .abi = "riscv64" },
    { .name = "c907-rv32",        .arch = "riscv32", .abi = "riscv32" },
    { .name = "c907fd-rv32",      .arch = "riscv32", .abi = "riscv32" },
    { .name = "c907fdv-rv32",     .arch = "riscv32", .abi = "riscv32" },
    { .name = "c907fdvm-rv32",    .arch = "riscv32", .abi = "riscv32" },
    { .name = "rv64gc",      .arch = "riscv64", .abi = "riscv64" },
    { .name = "rv64i",       .arch = "riscv64", .abi = "riscv64" },
    { .name = "rv64imac",    .arch = "riscv64", .abi = "riscv64" },
    { .name = "rv64imafd",   .arch = "riscv64", .abi = "riscv64" },
    { .name = "rv64imafdc",  .arch = "riscv64", .abi = "riscv64" },
    { .name = "sifive-e51",  .arch = "riscv64", .abi = "riscv64" },
    { .name = "sifive-u54",  .arch = "riscv64", .abi = "riscv64" },
    { .name = "rv64gcsu-v1.10.0",  .arch = "riscv64", .abi = "riscv64" },
    { .name = "rv64gcsu-v1.9.1",  .arch = "riscv64", .abi = "riscv64" },
    { .name = "r908",          .arch = "riscv64", .abi = "riscv64" },
    { .name = "r908fd",        .arch = "riscv64", .abi = "riscv64" },
    { .name = "r908fdv",       .arch = "riscv64", .abi = "riscv64" },
    { .name = "r908-cp",       .arch = "riscv64", .abi = "riscv64" },
    { .name = "r908fd-cp",     .arch = "riscv64", .abi = "riscv64" },
    { .name = "r908fdv-cp",    .arch = "riscv64", .abi = "riscv64" },
    { .name = "r908-rv32",          .arch = "riscv32", .abi = "riscv32" },
    { .name = "r908fd-rv32",        .arch = "riscv32", .abi = "riscv32" },
    { .name = "r908fdv-rv32",       .arch = "riscv32", .abi = "riscv32" },
    { .name = "r908-cp-rv32",       .arch = "riscv32", .abi = "riscv32" },
    { .name = "r908fd-cp-rv32",     .arch = "riscv32", .abi = "riscv32" },
    { .name = "r908fdv-cp-rv32",    .arch = "riscv32", .abi = "riscv32" },
    { .name = "c910v3",         .arch = "riscv64", .abi = "riscv64" },
    { .name = "c910v3-cp",      .arch = "riscv64", .abi = "riscv64" },
    { .name = "c920v3",         .arch = "riscv64", .abi = "riscv64" },
    { .name = "c920v3-cp",      .arch = "riscv64", .abi = "riscv64" },
    { .name = NULL }
};

static bool rv_set_machine = false;
static char *riscv_boards[] = {
    "sifive_e",
    "sifive_ex",
    "sifive_u",
    "sifive_ux",
    "virt",
    "spike_v1.10",
    "spike_v1.9.1",
};

static inline void sa_value(char *s, xmlChar *p)
{
    if (p == NULL) {
        printf("lack a prop\n");
        assert(0);
    }
    strcpy(s, (char *)p);
    xmlFree(p);
}

static inline void space_strcat(char *dst, char *src)
{
    strcat(dst, src);
    strcat(dst, " ");
}

static int mpu_in_cpu(char *name)
{
    int i;
    for (i = 0; i < sizeof(cpu_610_mpu_list) / sizeof(char *); i++) {
        if (!strcmp(name, cpu_610_mpu_list[i])) {
            return 1;
        }
    }

    return 0;
}

static char *mmu_cpu_name(char *name)
{
    int i;
    for (i = 0; i < sizeof(cpu_610_mmu_list) / sizeof(char *); i++) {
        if (!strcmp(name, cpu_610_mmu_list[i])) {
            return cpu_610_mpu_list[i];
        }
    }
    return NULL;
}

static int xml_parse_old(xmlNodePtr cur, struct dynsoc_board_info *b_info)
{
    xmlNodePtr cur1;
    xmlChar *xc;
    char *tmp;
    char *name;
    int i;

    strcpy(b_info->name, "dummyh");

    for (cur = cur->xmlChildrenNode; cur != NULL; cur = cur->next) {
        if (!xmlStrcmp(cur->name, BAD_CAST"cpu_info")) {
            tmp = (char *)xmlGetProp(cur, BAD_CAST"cpu");

            if (!memcmp(tmp, "ck6", 3) || !memcmp(tmp, "ck5", 3)) {
                strcpy(b_info->cpu.abi, "abiv1");
            } else {
                strcpy(b_info->cpu.abi, "abiv2");
            }

            if (mpu_in_cpu(tmp)) {
                strcpy(b_info->cpu_prop.mem_prot, "mpu");
                sa_value(b_info->cpu.cpu_name, tmp);
            } else {
                name = mmu_cpu_name(tmp);
                if (name != NULL) {
                    strcpy(b_info->cpu.cpu_name, name);
                } else {
                    sa_value(b_info->cpu.cpu_name, tmp);
                }
            }

            xc = xmlGetProp(cur, BAD_CAST"endian");
            sa_value(b_info->cpu.endian, xc);
            continue;
        }

        if (!xmlStrcmp(cur->name, BAD_CAST"dyndev")) {
            b_info->shm = 1;

            for (cur1 = cur->xmlChildrenNode, i = 0; cur1 != NULL;
                 cur1 = cur1->next, i++) {
                if (cur1->type == XML_TEXT_NODE) {
                    continue;
                }
                if (i >= 32) {
                    /* do not support more than 32. */
                    break;
                }

                tmp = (char *)xmlGetProp(cur1, BAD_CAST"name");
                if (!strcmp(tmp, "csky_uart")) {
                    b_info->dev[i].type = DYNSOC_UART;
                    strcpy(b_info->dev[i].name, "csky_uart");
                } else if (!strcmp(tmp, "csky_timers")) {
                    b_info->dev[i].type = DYNSOC_TIMER;
                    strcpy(b_info->dev[i].name, "csky_timer");
                } else if (!strcmp(tmp, "csky_lcdc")) {
                    b_info->dev[i].type = DYNSOC_LCDC;
                    strcpy(b_info->dev[i].name, "csky_lcdc");
                } else if (!strcmp(tmp, "csky_mac")) {
                    b_info->dev[i].type = DYNSOC_MAC;
                    strcpy(b_info->dev[i].name, "csky_mac");
                } else if (!strcmp(tmp, "csky_mac_v2")) {
                    b_info->dev[i].type = DYNSOC_MAC;
                    strcpy(b_info->dev[i].name, "csky_mac_v2");
                } else if (!strcmp(tmp, "csky_exit")) {
                    b_info->dev[i].type = DYNSOC_EXIT;
                    strcpy(b_info->dev[i].name, "csky_exit");
                } else if (!strcmp(tmp, "csky_memlog")) {
                    b_info->dev[i].type = DYNSOC_MEMLOG;
                    strcpy(b_info->dev[i].name, "csky_memlog");
                } else if (!strcmp(tmp, "csky_dma")) {
                    b_info->dev[i].type = DYNSOC_DMA;
                    strcpy(b_info->dev[i].name, "csky_dma");
                } else if (!strcmp(tmp, "csky_iis")) {
                    b_info->dev[i].type = DYNSOC_IIS;
                    strcpy(b_info->dev[i].name, "csky_iis");
                } else if (!strcmp(tmp, "csky_nand")) {
                    b_info->dev[i].type = DYNSOC_NAND;
                    strcpy(b_info->dev[i].name, "csky_nand");
                } else if (!strcmp(tmp, "csky_sdhc")) {
                    b_info->dev[i].type = DYNSOC_SDHC;
                    strcpy(b_info->dev[i].name, "csky_sdhc");
                } else if (!strcmp(tmp, "csky_mca")) {
                    b_info->dev[i].type = DYNSOC_MCA;
                    strcpy(b_info->dev[i].name, "csky_mca");
                } else if (!strcmp(tmp, "csky_fft")) {
                    b_info->dev[i].type = DYNSOC_FFT;
                    strcpy(b_info->dev[i].name, "csky_fft");
                } else if (!strcmp(tmp, "csky_intc")) {
                    b_info->dev[i].type = DYNSOC_INTC;
                    strcpy(b_info->dev[i].name, "csky_intc");
                } else if (!strcmp(tmp, "csky_tcip_v1")) {
                    b_info->dev[i].type = DYNSOC_INTC;
                    strcpy(b_info->dev[i].name, "csky_tcip_v1");
                } else if (!strcmp(tmp, "sysbus-ohci")) {
                    b_info->dev[i].type = DYNSOC_USB;
                    strcpy(b_info->dev[i].name, "sysbus-ohci");
                } else {
                    printf("unrecognizable type!\n");
                    goto parse_fail;
                }

                xc = xmlGetProp(cur1, BAD_CAST"filename");
                sa_value(b_info->dev[i].filename, xc);
                xc = xmlGetProp(cur1, BAD_CAST"addr");
                b_info->dev[i].addr = strtoull((char *)xc, NULL, 16);
                xc = xmlGetProp(cur1, BAD_CAST"irq");
                b_info->dev[i].irq = strtoul((char *)xc, NULL, 10);

                xc = xmlGetProp(cur1, BAD_CAST"extra");
                if (!memcmp(xc, "csky_vic_v1_init_cpu ", 21)) {
                    i++;
                    b_info->dev[i].type = DYNSOC_TIMER;
                    strcpy(b_info->dev[i].name, "csky_coret");
                    strcpy(b_info->dev[i].filename, "builtin");
                    b_info->dev[i].addr = b_info->dev[i - 1].addr + 0x10;
                    b_info->dev[i].irq = strtoul(((char *)xc + 21), NULL, 10);
                }

            }
            continue;
        }

        if (!xmlStrcmp(cur->name, BAD_CAST"memory")) {
            for (cur1 = cur->xmlChildrenNode, i = 0; cur1 != NULL;
                 cur1 = cur1->next, i++) {
                if (cur1->type == XML_TEXT_NODE) {
                    continue;
                }
                if (i >= 8) {
                    /* do not support more than 4. */
                    break;
                }
                xc = xmlGetProp(cur1, BAD_CAST"name");
                sa_value(b_info->mem[i].name, xc);
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"addr");
                b_info->mem[i].addr = strtoull(tmp, NULL, 16);
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"size");
                b_info->mem[i].size = strtoull(tmp, NULL, 16);
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"attr");
                if (!strcmp(tmp, "MEM_RAM")) {
                    b_info->mem[i].writeable = 1;
                } else if (!strcmp(tmp, "MEM_ROM")) {
                    b_info->mem[i].writeable = 0;
                } else {
                    printf("unrecognizable attribute!\n");
                    goto parse_fail;
                }
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"boot");
                if (tmp != NULL) {
                    b_info->mem[i].boot = strtoull(tmp, NULL, 16);
                    if (b_info->mem[i].boot != 0) {
                        b_info->loader_start = b_info->mem[i].addr;
                    }
                }
            }
            continue;
        }

    }

    strcpy(b_info->cpu_prop.pctrace, "off");
    strcpy(b_info->cpu_prop.elrw, "off");

    return 0;

parse_fail:
    return -1;
}

static int xml_parse(char *filename, struct dynsoc_board_info *b_info)
{
    xmlDocPtr doc;
    xmlNodePtr cur, cur1;
    xmlChar *xc;
    char *tmp;
    int i;
    int is_riscv = 0;

    xmlKeepBlanksDefault(0);

    doc = xmlParseFile(filename);
    if (!xmlFree) {
        xmlMemGet(&xmlFree, &xmlMalloc, &xmlRealloc, NULL);
    }

    if (doc == NULL) {
        printf("parse failed\n");
        goto parse_fail;
    }

    /* getting root element */
    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
        printf("empty document\n");
        goto parse_fail;
    }

    /* Judge whether the file is the right one */
    if (xmlStrcmp(cur->name, BAD_CAST"Board")) {
        printf("Root node is not Board.\n");
        goto parse_fail;
    }

    xc = (char *)xmlGetProp(cur, BAD_CAST"arch");
    sa_value(b_info->arch, xc);
    if ((strcmp(b_info->arch, "riscv32") == 0) ||
        (strcmp(b_info->arch, "riscv64") == 0)) {
        is_riscv = 1;
    }
    /* support xml version between XML_MINVER to XML_MAXVER */
    xc = (char *)xmlGetProp(cur, BAD_CAST"cskysimv");
    if (xc == NULL) {
        /* not have "cskysimv", treate as version 0.0,
         * try to compatible old xml */
        if (xml_parse_old(cur, b_info) == 0) {
            goto finish;
        } else {
            goto parse_fail;
        }
    } else {
        double version = strtod(xc, NULL);
        if (version < XML_MINVER || version > XML_MAXVER) {
            printf("xml version should be between %.1lf and %.1lf!\n",
                   XML_MINVER, XML_MAXVER);
            goto parse_fail;
        }
    }
    if (is_riscv) {
        int idx = 0;
        xc = (char *)xmlGetProp(cur, BAD_CAST"name");
        sa_value(b_info->name, xc);
        /* check not exist board */
        while (idx < (sizeof(riscv_boards) / sizeof(char *))) {
            if (strcasecmp(riscv_boards[idx], b_info->name) == 0) {
                rv_set_machine = true;
                break;
            }
            idx++;
        }
        if (!rv_set_machine) {
            strcpy(b_info->xml_name, b_info->name);
            strcpy(b_info->name, "dummyh");
        }
    } else {
        strcpy(b_info->name, "dummyh");
    }
    b_info->shm = 1;

    b_info->set_intc = 0;

    for (cur = cur->xmlChildrenNode; cur != NULL; cur = cur->next) {
        if (!xmlStrcmp(cur->name, BAD_CAST"cpu_info")) {
            xc = xmlGetProp(cur, BAD_CAST"cpu");
            sa_value(b_info->cpu.cpu_name, xc);
            xc = xmlGetProp(cur, BAD_CAST"abi");
            sa_value(b_info->cpu.abi, xc);
            xc = xmlGetProp(cur, BAD_CAST"endian");
            sa_value(b_info->cpu.endian, xc);
            continue;
        }

        if (!xmlStrcmp(cur->name, BAD_CAST"dyndev")) {
            for (cur1 = cur->xmlChildrenNode, i = 0; cur1 != NULL;
                 cur1 = cur1->next, i++) {
                if (cur1->type == XML_TEXT_NODE) {
                    continue;
                }
                if (i >= 32) {
                    /* do not support more than 32. */
                    break;
                }

                tmp = (char *)xmlGetProp(cur1, BAD_CAST"type");
                if (!strcmp(tmp, "UART")) {
                    b_info->dev[i].type = DYNSOC_UART;
                } else if (!strcmp(tmp, "TIMER")) {
                    b_info->dev[i].type = DYNSOC_TIMER;
                } else if (!strcmp(tmp, "LCDC")) {
                    b_info->dev[i].type = DYNSOC_LCDC;
                } else if (!strcmp(tmp, "MAC")) {
                    b_info->dev[i].type = DYNSOC_MAC;
                } else if (!strcmp(tmp, "EXIT")) {
                    b_info->dev[i].type = DYNSOC_EXIT;
                } else if (!strcmp(tmp, "MEMLOG")) {
                    b_info->dev[i].type = DYNSOC_MEMLOG;
                } else if (!strcmp(tmp, "DMA")) {
                    b_info->dev[i].type = DYNSOC_DMA;
                } else if (!strcmp(tmp, "IIS")) {
                    b_info->dev[i].type = DYNSOC_IIS;
                } else if (!strcmp(tmp, "NAND")) {
                    b_info->dev[i].type = DYNSOC_NAND;
                } else if (!strcmp(tmp, "SDHC")) {
                    b_info->dev[i].type = DYNSOC_SDHC;
                } else if (!strcmp(tmp, "MCA")) {
                    b_info->dev[i].type = DYNSOC_MCA;
                } else if (!strcmp(tmp, "FFT")) {
                    b_info->dev[i].type = DYNSOC_FFT;
                } else if (!strcmp(tmp, "INTC")) {
                    b_info->dev[i].type = DYNSOC_INTC;
                    b_info->set_intc = 1;
                } else {
                    printf("unrecognizable type!\n");
                    goto parse_fail;
                }

                xc = xmlGetProp(cur1, BAD_CAST"name");
                sa_value(b_info->dev[i].name, xc);
                xc = xmlGetProp(cur1, BAD_CAST"filename");
                sa_value(b_info->dev[i].filename, xc);
                xc = xmlGetProp(cur1, BAD_CAST"addr");
                b_info->dev[i].addr = strtoull((char *)xc, NULL, 16);
                xc = xmlGetProp(cur1, BAD_CAST"irq");
                if (*((char *)xc) == '\0') {
                    b_info->dev[i].irq = 0xdeadbeef;
                } else {
                    b_info->dev[i].irq = strtoul((char *)xc, NULL, 10);
                }
            }

            if (b_info->set_intc == 0) {
                printf("warning: no interrupt controller\n");
            }

            continue;
        }

        if (!xmlStrcmp(cur->name, BAD_CAST"memory")) {
            for (cur1 = cur->xmlChildrenNode, i = 0; cur1 != NULL;
                 cur1 = cur1->next, i++) {
                if (cur1->type == XML_TEXT_NODE) {
                    continue;
                }
                if (i >= 8) {
                    /* do not support more than 4. */
                    break;
                }
                xc = xmlGetProp(cur1, BAD_CAST"name");
                sa_value(b_info->mem[i].name, xc);
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"addr");
                b_info->mem[i].addr = strtoull(tmp, NULL, 16);
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"size");
                b_info->mem[i].size = strtoull(tmp, NULL, 16);
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"attr");
                if (!strcmp(tmp, "MEM_RAM")) {
                    b_info->mem[i].writeable = 1;
                } else if (!strcmp(tmp, "MEM_ROM")) {
                    b_info->mem[i].writeable = 0;
                } else {
                    printf("unrecognizable attribute!\n");
                    goto parse_fail;
                }
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"boot");
                if (tmp != NULL) {
                    b_info->mem[i].boot = strtoull(tmp, NULL, 16);
                    if (b_info->mem[i].boot != 0) {
                        b_info->loader_start = b_info->mem[i].addr;
                    }
                }
            }
            continue;
        }

        if (!xmlStrcmp(cur->name, BAD_CAST"cpu_prop")) {
            for (cur1 = cur->xmlChildrenNode; cur1 != NULL;
                 cur1 = cur1->next) {
                if (cur1->type == XML_TEXT_NODE) {
                    continue;
                }
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"name");

                if (!strcmp(tmp, "full_mmu")) {
                    xc = xmlGetProp(cur1, BAD_CAST"value");
                    sa_value(b_info->cpu_prop.full_mmu, xc);
                    continue;
                }

                if (!strcmp(tmp, "pctrace")) {
                    xc = xmlGetProp(cur1, BAD_CAST"value");
                    sa_value(b_info->cpu_prop.pctrace, xc);
                    continue;
                }

                if (!strcmp(tmp, "elrw")) {
                    xc = xmlGetProp(cur1, BAD_CAST"value");
                    sa_value(b_info->cpu_prop.elrw, xc);
                    continue;
                }

                if (!strcmp(tmp, "mem_prot")) {
                    xc = xmlGetProp(cur1, BAD_CAST"value");
                    sa_value(b_info->cpu_prop.mem_prot, xc);
                    continue;
                }

                if (!strcmp(tmp, "unaligned_access")) {
                    xc = xmlGetProp(cur1, BAD_CAST"value");
                    sa_value(b_info->cpu_prop.unaligned_access, xc);
                    continue;
                }

           }
           continue;
        }

        if (!xmlStrcmp(cur->name, BAD_CAST"misc_options")) {
            for (cur1 = cur->xmlChildrenNode; cur1 != NULL;
                 cur1 = cur1->next) {

                if (cur1->type == XML_TEXT_NODE) {
                    continue;
                }
                tmp = (char *)xmlGetProp(cur1, BAD_CAST"onoff");
                if (!strcmp(tmp, "on")) {
                    tmp = (char *)xmlGetProp(cur1, BAD_CAST"cmd");
                    space_strcat(b_info->misc, tmp);
                }
                continue;

           }
           continue;
        }
    }

finish:
    return 0;

parse_fail:
    xmlFreeDoc(doc);
    return 1;
}

static int check_cpu_info(struct dynsoc_board_info *b_info)
{
    int i = 0,
        is_find = 0,
        ret = 0;

    while (cpu_arr[i].name != NULL) {
        if (strcmp(b_info->cpu.cpu_name, cpu_arr[i].name) == 0) {
            is_find = 1;
            break;
        }
        i++;
    }
    if (is_find == 0) {
        printf("Error: cpu %s not support!\n", b_info->cpu.cpu_name);
        ret = 1;
    } else if (strcmp(b_info->arch, cpu_arr[i].arch) != 0) {
        printf("Error: cpu arch %s not match!\n", b_info->arch);
        ret = 1;
    } else if (strcmp(b_info->cpu.abi, cpu_arr[i].abi) != 0) {
        printf("Error: cpu abi %s not match!\n", b_info->cpu.abi);
        ret = 1;
    }
    return ret;
}

#define DEV_ALIGN_SIZE 0x100
static int check_device(struct dynsoc_board_info *b_info)
{
    int i = 0,
        j = 0,
        ret = 0,
        is_riscv = 0,
        has_device = 0,
        has_intc = 0,
        is_rv_intc = 0;
    bool has_coret = false,
         has_legacy_clic = false;

    if ((strcmp(b_info->arch, "riscv32") == 0) ||
        (strcmp(b_info->arch, "riscv64") == 0)) {
        is_riscv = 1;
    }

    while (b_info->dev[i].type != DYNSOC_EMPTY) {
        has_device = 1;

        /* get interrupt controller info */
        if (b_info->dev[i].type == DYNSOC_INTC) {
            has_intc = 1;
            if ((strstr(b_info->dev[i].name, "csky_clic")) ||
                (strstr(b_info->dev[i].name, "csky_plic")) ||
                (strstr(b_info->dev[i].name, "csky_clint"))) {
                is_rv_intc = 1;
            }
            if (strstr(b_info->dev[i].name, "csky_clic_v0.7.0")) {
                has_legacy_clic = true;
            }
        }
        if (b_info->dev[i].type == DYNSOC_TIMER) {
            if (strstr(b_info->dev[i].name, "csky_coret")) {
                has_coret = true;
            }
        }
        /* check address align */
        //if ((b_info->dev[i].addr & (DEV_ALIGN_SIZE - 1)) != 0) {
        //    printf("Error: Device %s address is not 0x100 aligned.\n",
        //           b_info->dev[i].name);
        //    ret = 1;
        //}
        if (b_info->dev[i].irq != 0xdeadbeef) {
            if (is_riscv && b_info->dev[i].irq < 16) {
                printf("Error: Device %s irq=%d is internally "
                       "used for RISCV.\n",
                       b_info->dev[i].name, b_info->dev[i].irq);
                ret = 1;
            }
            if (b_info->dev[i].irq < 0 || b_info->dev[i].irq >= 1024) {
                printf("Error: Device %s irq=%d is out of range[0-1023].\n",
                       b_info->dev[i].name, b_info->dev[i].irq);
                ret = 1;
            }
        }

        /* check address/irq conflict */
        j = i + 1;
        while (b_info->dev[j].type != DYNSOC_EMPTY) {
            if (b_info->dev[i].addr == b_info->dev[j].addr) {
                printf("Error: Address conflict between %s and %s\n",
                       b_info->dev[i].name, b_info->dev[j].name);
                ret = 1;
            }

            if (b_info->dev[i].type == DYNSOC_INTC
                || b_info->dev[j].type == DYNSOC_INTC) {
                j++;
                continue;
            }
            if (b_info->dev[i].irq == b_info->dev[j].irq) {
                printf("Error: IRQ conflict between %s and %s\n",
                       b_info->dev[i].name, b_info->dev[j].name);
                ret = 1;
            }
            j++;
        }
        i++;
    }

    if (has_device && !has_intc) {
        printf("Error: Interrupt controller is not configured.\n");
        ret = 1;
    }

    if (has_device && is_riscv && !is_rv_intc) {
        printf("Error: RISCV Series can only use CLIC or CLINT or PLIC.\n");
        ret = 1;
    }

    if (has_device && !is_riscv && is_rv_intc) {
        printf("Error: CLIC or CLINT or PLIC can only be used by "
               "RISCV Series.\n");
        ret = 1;
    }
    if (has_device && is_riscv && has_coret) {
        printf("Error: csky_coret can not be used by "
               "RISCV Series.\n");
        ret = 1;
    }
    if (has_device && is_riscv && has_legacy_clic) {
        printf("Error: csky_clic version is too old\n");
        ret = 1;
    }
    return ret;
}

static int check_memory(struct dynsoc_board_info *b_info)
{
    int i, j, cnt, ret = 0;

    for (cnt = 0; cnt < 8; cnt++) {
        /* count memory by name/address/size */
        if (b_info->mem[cnt].name[0] == 0
            && b_info->mem[cnt].addr == 0
            && b_info->mem[cnt].size == 0) {
            break;
        }
    }

    if (cnt == 0) {
        printf("Error: Must have one memory at last.\n");
        ret = 1;
    }

    for (i = 0; i < cnt; i++) {
        int name_len = strlen(b_info->mem[i].name);
        if (name_len == 0) {
            printf("Error: Memory Name can't be empty!\n");
            ret = 1;
        }
        if (name_len > 31) {
            printf("Error: Memory Name is too long and should be less than 31!\n");
            ret = 1;
        }
        if ((b_info->mem[i].addr & 0x3ff) != 0) {
            printf("Error: Start address is not aligned with 1k.\n");
            ret = 1;
        }
        //if ((b_info->mem[i].size & 0x3ff) != 0) {
        //    printf("Error: Length is not aligned with 1k.\n");
        //    ret = 1;
        //}
        if (b_info->mem[i].size == 0) {
            printf("Error: Length should not be zero.\n");
            ret = 1;
        }
        if (b_info->mem[i].size > 0x10000000000) {
            printf("Error: Length should not be larger than 1024 GB.\n");
            ret = 1;
        }
    }

    if (ret != 0) {
        return ret;
    }

    /* check memory conflict */
    for (i = 0; i < cnt - 1; i++) {
        uint64_t start_i, end_i;
        start_i = b_info->mem[i].addr;
        end_i = start_i + b_info->mem[i].size;
        for (j = i + 1; j < cnt; j++) {
            uint64_t start_j, end_j;
            start_j = b_info->mem[j].addr;
            end_j = start_j + b_info->mem[j].size;

            if (strcmp(b_info->mem[i].name, b_info->mem[j].name) == 0) {
                printf("Error: Memory Name conflict!\n");
                ret = 1;
            }

            if ((start_i >= start_j && start_i < end_j)
                || (end_i > start_j && end_i <= end_j)
                || (start_j >= start_i && start_j < end_i)
                || (end_j > start_i && end_j <= end_i)) {
                printf("Error: Mem \"%s\" and \"%s\" Address range conflict!\n",
                       b_info->mem[i].name, b_info->mem[j].name);
                ret = 1;
            }
        }
    }

    /* check memory-dev conflict */
    for (i = 0; i < cnt - 1; i++) {
        uint64_t start_i, end_i;
        start_i = b_info->mem[i].addr;
        end_i = start_i + b_info->mem[i].size;

        j = 0;
        while(b_info->dev[j].type != DYNSOC_EMPTY) {
            uint64_t start_j, end_j;
            start_j = b_info->dev[j].addr;
            end_j = start_j + DEV_ALIGN_SIZE;

            if ((start_i >= start_j && start_i < end_j)
                || (end_i > start_j && end_i <= end_j)
                || (start_j >= start_i && start_j < end_i)
                || (end_j > start_i && end_j <= end_i)) {
                printf("Error: Mem \"%s\" and Dev \"%s\" Address range conflict!\n",
                       b_info->mem[i].name, b_info->dev[j].name);
                ret = 1;
            }
            j++;
        }
    }
    return ret;
}

static inline int riscv_warning(struct dynsoc_board_info *b_info, char *stub)
{
    if (b_info == NULL || stub == NULL) {
        return -1;
    }
    if (strcmp(b_info->arch, "riscv32") == 0) {
        return -2;
    }
    if (strcmp(b_info->arch, "riscv64") == 0) {
        return -2;
    }
    return 0;
}

static int check_cpu_prop(struct dynsoc_board_info *b_info)
{
    int ret = 0;

    if (strlen(b_info->cpu_prop.pctrace) != 0) {
        if ((strcmp(b_info->cpu_prop.pctrace, "off") != 0)
            && (strcmp(b_info->cpu_prop.pctrace, "on") != 0)) {
            int val;
            char *endptr = NULL;
            val = strtol(b_info->cpu_prop.pctrace, &endptr, 10);
            if (*endptr != '\0') {
                printf("Error: pctrace value=\"%s\" is invalid!\n",
                       b_info->cpu_prop.pctrace);
                ret = 1;
            } else {
                printf("Warning: pctrace=n is not support, "
                       "should be pctrace=[on|off]!\n");
                ret = 0;
            }
        }
    }

    if (strlen(b_info->cpu_prop.elrw) != 0) {
        if ((strcmp(b_info->cpu_prop.elrw, "off") != 0)
            && (strcmp(b_info->cpu_prop.elrw, "on") != 0)) {
            printf("Error: elrw value=\"%s\" is invalid!\n",
                   b_info->cpu_prop.elrw);
            ret = 1;
        }
    }

    if (strlen(b_info->cpu_prop.mem_prot) != 0) {
        if ((strcmp(b_info->cpu_prop.mem_prot, "mmu") != 0)
            && (strcmp(b_info->cpu_prop.mem_prot, "mpu") != 0)
            && (strcmp(b_info->cpu_prop.mem_prot, "mgu") != 0)
            && (strcmp(b_info->cpu_prop.mem_prot, "no") != 0)) {
            printf("Error: mem_prot value=\"%s\" is invalid!\n",
                   b_info->cpu_prop.mem_prot);
            ret = 1;
        }
    }

    if (strlen(b_info->cpu_prop.full_mmu) != 0) {
        if ((strcmp(b_info->cpu_prop.full_mmu, "off") != 0)
            && (strcmp(b_info->cpu_prop.full_mmu, "on") != 0)) {
            printf("Error: full_mmu value=\"%s\" is invalid!\n",
                   b_info->cpu_prop.full_mmu);
            ret = 1;
        }
    }

    if (strlen(b_info->cpu_prop.unaligned_access) != 0) {
        if ((strcmp(b_info->cpu_prop.unaligned_access, "off") != 0)
            && (strcmp(b_info->cpu_prop.unaligned_access, "on") != 0)) {
            printf("Error: unaligned_access value=\"%s\" is invalid!\n",
                   b_info->cpu_prop.unaligned_access);
            ret = 1;
        }
    }

    return ret;
}

static int xml_check(struct dynsoc_board_info *b_info)
{
    int ret;

    /* Step 1: check cpu name and abi */
    ret = check_cpu_info(b_info);
    if (ret != 0) {
        return 1;
    }
    /* check cpu prop */
    ret = check_cpu_prop(b_info);
    if (ret != 0) {
        return 1;
    }

    /* Step 2: check memory */
    ret = check_memory(b_info);
    if (ret != 0) {
        return 2;
    }

    /* Step 3: check device */
    ret = check_device(b_info);
    if (ret != 0) {
        return 3;
    }

    return 0;
}

static inline void print_help(void)
{
    fprintf(stderr, CSKYSIM_VERSION"\n"
           "usage: qemu -soc cfg.xml -kernel Image [options]\n"
           "\n"
           "Options:\n"
           "-h or -help     display this help and exit\n"
           "-version        display version information and exit\n"
           "-soc xml        use 'xml' as board description\n"
           "-check xml      check 'xml' validity\n"
           "-nographic      disable graphical output and redirect serial I/Os to console\n"
           "-kernel bzImage use 'bzImage' as kernel image\n"
           "-append cmdline use 'cmdline' as kernel command line\n"
           "-initrd file    use 'file' as initial ram disk\n"
           "-bios file      use 'file' as bootloader\n"
           "\n"
           "If you want to see the full version of help\n"
           "Please using the option --full-help to get QEMU help\n");
}

static inline void print_version(void)
{
    int date = COMPILE_DATE;
    fprintf(stderr, CSKYSIM_VERSION);
    fprintf(stderr, " B%d\n", date);
}

#ifdef _WIN32
#define CSKYSIM_KEY     ("0x2333")
struct dynsoc_board_info *create_shm(int shmkey)
{
    HANDLE shmid;
    char *ret;
    char shmkey_str[10];
    struct dynsoc_board_info *shm;
    sprintf(shmkey_str, "%d", shmkey);
    shmid = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                              sizeof(struct dynsoc_board_info), shmkey_str);

    if (shmid == NULL) {
        printf("CreateFileMapping failed\n");
        return (void *)-1;
    }

    ret = (char *)MapViewOfFile(shmid, FILE_MAP_ALL_ACCESS, 0, 0,
                                sizeof(struct dynsoc_board_info));

    if (ret == NULL) {
        printf("MapViewOfFile failed\n");
        return (void *)-1;
    }

    memset(ret, 0, sizeof(struct dynsoc_board_info));

    shm = (struct dynsoc_board_info *)ret;
    shm->shmid = (int)shmid;

    return shm;
}

void free_shm(struct dynsoc_board_info *b_info)
{

}

void start_qemu(char *cmd)
{
    system(cmd);
}

static char *prefix_args(struct dynsoc_board_info *b_info, char *cmd)
{
    char *ret;
    if ((strcmp(b_info->arch, "riscv32") == 0)) {
        strcpy(cmd, "qemu-system-riscv32 ");
    } else if ((strcmp(b_info->arch, "riscv64") == 0)) {
        strcpy(cmd, "qemu-system-riscv64 ");
    } else {
        if ((strcmp(b_info->cpu.abi, "abiv1") == 0) &&
            (strcmp(b_info->cpu.endian, "little") == 0)) {
            strcpy(cmd, "qemu-system-cskyv1 ");
        } else if ((strcmp(b_info->cpu.abi, "abiv1") == 0) &&
                   (strcmp(b_info->cpu.endian, "big") == 0)) {
            strcpy(cmd, "qemu-system-cskyv1eb ");
        } else if ((strcmp(b_info->cpu.abi, "abiv2") == 0) &&
                   (strcmp(b_info->cpu.endian, "little") == 0)) {
            strcpy(cmd, "qemu-system-cskyv2 ");
        } else if ((strcmp(b_info->cpu.abi, "abiv2") == 0) &&
                   (strcmp(b_info->cpu.endian, "big") == 0)) {
            strcpy(cmd, "qemu-system-cskyv2eb ");
        } else {
            printf("bad abi or endian\n");
            return (char *)-1;
        }
    }

    ret = cmd + strlen(cmd);
    return ret;
}

char *cmd_copy(struct dynsoc_board_info *b_info, int argc, char *argv[],
    char *index)
{
    int i;
    int add_quotes = 0;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-cpu-prop") == 0) {
            cskysim_user_options |= USER_DEFINE_CPU_PROP;
            if (riscv_warning(b_info, "cpu-prop") < 0) {
                assert(i != argc - 1);
                /* Only use pctrace for riscv */
                char *pctrace = strstr(argv[i + 1], "pctrace");
                if (pctrace) {
                    bool status_on = !strcmp(pctrace, "pctrace=on");
                    bool status_off = !strcmp(pctrace, "pctrace=off");
                    if (status_on || status_off) {
                        assert(status_on ^ status_off);
                        strcpy(index, argv[i]);
                        index += strlen(argv[i]);
                        *index = ' ';
                        index += 1;

                        strcpy(index, status_on ? "pctrace=on" : "pctrace=off");
                        index += strlen(argv[i + 1]);
                        *index = ' ';
                        index += 1;
                        i += 2;
                        continue;
                    }
                    printf("pctrace legal value is on or off\n");
                }
                /* Otherwise, just skip -cpu-prop for risc-v */
                printf("only pctrace is legal for -cpu-prop in riscv\n");
                i++;
                continue;
            }
        } else if (strcmp(argv[i], "-csky-extend") == 0) {
            if (riscv_warning(b_info, "csky-extend") < 0) {
                i++;
                continue;
            }
        }
        strcpy(index, argv[i]);
        index += strlen(argv[i]);

        if (add_quotes == 1) {
            *index = '"';
            index += 1;
            add_quotes = 0;
        }

        *index = ' ';
        index += 1;

        if (strcmp(argv[i], "-kernel") == 0) {
            *index = '"';
            index += 1;
            add_quotes = 1;
        }
        if ((strcmp(argv[i], "-cpu") == 0) && rv_set_machine) {
            memset(b_info->cpu.cpu_name, 0, 32);
            strncpy(b_info->cpu.cpu_name, argv[i], 32);
        }
    }
    return index;
}

#else
#define CSKYSIM_KEY     ((key_t)0x2333)
struct dynsoc_board_info *create_shm(int shmkey)
{
    int shmid;
    struct dynsoc_board_info *shm;
    shmid = shmget(shmkey, sizeof(struct dynsoc_board_info),
                   0666 | IPC_CREAT);

    if (shmid == -1) {
        printf("shmget failed\n");
        return (void *)-1;
    }

    shm = shmat(shmid, (void *)0, 0);
    if ((void *)shm == (void *)-1) {
        printf("shmat failed\n");
        return (void *)-1;
    }

    memset(shm, 0, sizeof(struct dynsoc_board_info));
    shm->shmid = shmid;

    return shm;
}

void free_shm(struct dynsoc_board_info *b_info)
{
    int shmid = b_info->shmid;

    if (shmdt(b_info) == -1) {
        printf("shmdt failed\n");
    }

    if (shmctl(shmid, IPC_RMID, 0) == -1) {
        printf("shmctl failed\n");
    }
}

void start_qemu(char *cmd)
{
    system(cmd);
}

static char *prefix_args(struct dynsoc_board_info *b_info, char *cmd)
{
    char *ret;

    if ((strcmp(b_info->arch, "riscv32") == 0)) {
        strcpy(cmd, "qemu-system-riscv32 ");
    } else if ((strcmp(b_info->arch, "riscv64") == 0)) {
        strcpy(cmd, "qemu-system-riscv64 ");
    } else {
        if ((strcmp(b_info->cpu.abi, "abiv1") == 0) &&
            (strcmp(b_info->cpu.endian, "little") == 0)) {
            strcpy(cmd, "qemu-system-cskyv1 ");
        } else if ((strcmp(b_info->cpu.abi, "abiv1") == 0) &&
                   (strcmp(b_info->cpu.endian, "big") == 0)) {
            strcpy(cmd, "qemu-system-cskyv1eb ");
        } else if ((strcmp(b_info->cpu.abi, "abiv2") == 0) &&
                   (strcmp(b_info->cpu.endian, "little") == 0)) {
            strcpy(cmd, "qemu-system-cskyv2 ");
        } else if ((strcmp(b_info->cpu.abi, "abiv2") == 0) &&
                   (strcmp(b_info->cpu.endian, "big") == 0)) {
            strcpy(cmd, "qemu-system-cskyv2eb ");
        } else {
            printf("bad abi or endian\n");
            return (char *)-1;
        }
    }
    ret = cmd + strlen(cmd);
    return ret;
}

char *cmd_copy(struct dynsoc_board_info *b_info, int argc, char *argv[],
    char *index)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-cpu-prop") == 0) {
            cskysim_user_options |= USER_DEFINE_CPU_PROP;
            if (riscv_warning(b_info, "cpu-prop") < 0) {
                assert(i != argc - 1);
                /* Only use pctrace for riscv */
                char *pctrace = strstr(argv[i + 1], "pctrace");
                if (pctrace) {
                    bool status_on = !strcmp(pctrace, "pctrace=on");
                    bool status_off = !strcmp(pctrace, "pctrace=off");
                    if (status_on || status_off) {
                        assert(status_on ^ status_off);
                        strcpy(index, argv[i]);
                        index += strlen(argv[i]);
                        *index = ' ';
                        index += 1;

                        strcpy(index, status_on ? "pctrace=on" : "pctrace=off");
                        index += strlen(argv[i + 1]);
                        *index = ' ';
                        index += 1;
                        i += 2;
                        continue;
                    }
                    printf("pctrace legal value is on or off\n");
                }
                /* Otherwise, just skip -cpu-prop for risc-v */
                printf("only pctrace is legal for -cpu-prop in riscv\n");
                i++;
                continue;
            }
        } else if (strcmp(argv[i], "-csky-extend") == 0) {
            if (riscv_warning(b_info, "csky-extend") < 0) {
                i++;
                continue;
            }
        }
        strcpy(index, argv[i]);
        index += strlen(argv[i]);
        *index = ' ';
        index += 1;
    }

    return index;
}

#endif

static struct dynsoc_board_info *share_board_info(int shmkey)
{
    struct dynsoc_board_info *b_info;

    b_info = create_shm(shmkey);
    if (b_info == (struct dynsoc_board_info *)-1) {
        printf("create shm failed\n");
        return b_info;
    }

    b_info->write_enable = 1;
    b_info->read_enable = 0;
    return b_info;
}

static int postfix_args(struct dynsoc_board_info *b_info, char *cmd)
{
    space_strcat(cmd, "-machine");
    space_strcat(cmd, b_info->name);
    space_strcat(cmd, "-cpu");
    space_strcat(cmd, b_info->cpu.cpu_name);
    if ((cskysim_user_options & USER_DEFINE_CPU_PROP) == 0) {
        space_strcat(cmd, "-cpu-prop");
        strcat(cmd, "pctrace=");
        if (strcmp(b_info->cpu_prop.pctrace, "off")) {
            strcat(cmd, "on");
        } else {
            strcat(cmd, "off");
        }

        if (strcmp(b_info->arch, "riscv32") != 0 &&
            strcmp(b_info->arch, "riscv64") != 0) {
            if (strcmp(b_info->cpu_prop.elrw, "off")) {
                strcat(cmd, ",elrw=on");
            }

            if (!strcmp(b_info->cpu_prop.mem_prot, "mmu")) {
                strcat(cmd, ",mem_prot=mmu");
                strcat(cmd, ",full_mmu=on");
            } else if (!strcmp(b_info->cpu_prop.mem_prot, "mpu") ||
                    !strcmp(b_info->cpu_prop.mem_prot, "mgu")) {
                strcat(cmd, ",mem_prot=mpu");
            } else if (!strcmp(b_info->cpu_prop.mem_prot, "no")) {
                strcat(cmd, ",mem_prot=no");
            }

            if (strcmp(b_info->cpu_prop.unaligned_access, "off")) {
                strcat(cmd, ",unaligned_access=on");
            }
        }
        strcat(cmd, " ");
    }
    space_strcat(cmd, b_info->misc);

    /* QEMU could read struct now */
    b_info->write_enable = 0;
    b_info->read_enable = 1;
    return 0;
}

int main(int argc, char *argv[])
{
    struct dynsoc_board_info *b_info;
    char cmd[2048];
    char *index = cmd;
    int i, ret;
    int single_cskysim = 0;
    int sleep_time = 10;
    int shmkey;

    for (i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "--h") == 0) ||
            (strcmp(argv[i], "-help") == 0) || (strcmp(argv[i], "-h") == 0)) {
            print_help();
            goto cskysim_end;
        }

        if ((strcmp(argv[i], "--version") == 0) ||
            (strcmp(argv[i], "-version") == 0) ||
            (strcmp(argv[i], "-v") == 0)) {
            print_version();
            goto cskysim_end;
        }

        if ((strcmp(argv[i], "----sc") == 0)) {
            single_cskysim = 1;
            strcpy(argv[i], "");
            sleep_time = atoi(argv[i + 1]);
            strcpy(argv[i + 1], "");
        }

        if (strcmp(argv[i], "--full-help") == 0) {
            /* replace as QEMU's --help */
            strcpy(argv[i], "--help");
        }
    }

    /* generate dynsoc_board_info randomly. */
    srand((unsigned)time(NULL));
    i = 20;
    do {
        shmkey = rand() % 10000;
        if (shmkey) {
            b_info = share_board_info(shmkey);
        }
    } while ((b_info == (struct dynsoc_board_info *)-1) && i--);

    if (b_info == (struct dynsoc_board_info *)-1) {
        printf("can not share the board info\n");
        goto cskysim_fail;
    }

    /* find the option -check, parse and check the xml file */
    for (i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--check") == 0) ||
            (strcmp(argv[i], "-check") == 0)) {
            ret = xml_parse(argv[i + 1], b_info);
            if (ret != 0) {
                printf("Error: xml is not a legal board.\n");
                goto cskysim_fail;
            }
            if (!rv_set_machine) {
                ret = xml_check(b_info);
            }
            return ret;
        }
    }

    /* find the option -soc and parse the xml file */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-soc") == 0) {
            ret = xml_parse(argv[i + 1], b_info);
            if (ret != 0) {
                printf("Error: xml is not a legal board.\n");
                goto cskysim_fail;
            }
            /* check rv and set machine */
            if (!rv_set_machine) {
                if (b_info->shm == 1) {
                    char shmkey_str[20] = {0};
                    sprintf(shmkey_str, "shm=on,shmkey=%d", shmkey);
                    argv[i + 1] = (char *)malloc(strlen(shmkey_str) + 1);
                    memset(argv[i + 1], 0, strlen(shmkey_str) + 1);
                    strcpy(argv[i + 1], shmkey_str);
                } else {
                    argv[i + 1] = (char *)malloc(strlen("shm=off") + 1);
                    memset(argv[i + 1], 0, strlen("shm=off") + 1);
                    strcpy(argv[i + 1], "shm=off");
                }
            } else {
                int j = i + 2 ;
                for (; j < argc; j++) {
                    argv[i] = argv[j];
                    i++;
                }
                while (i < argc) {
                    argv[i] = NULL;
                    i++;
                }
                argc = argc - 2;
            }
            break;
        }
    }

    memset(cmd, 0, sizeof(cmd));
    /* determined call which qemu */
    index = prefix_args(b_info, cmd);
    if (index == (char *)-1) {
        printf("prefix args failed\n");
        goto cskysim_fail;
    }

    index = cmd_copy(b_info, argc, argv, index);

    if ((int)(index - cmd) >= 1024) {
        printf("args too long\n");
        goto cskysim_fail;
    }

    /* postfix args from xml can override the same argv[] */
    ret = postfix_args(b_info, index);
    if (ret != 0) {
        printf("postfix args failed\n");
        goto cskysim_fail;
    }
    printf("QEMU cmdline: %s\n", cmd);
    fflush(stdout);

    if (single_cskysim) {
        /* do nothing */
        sleep(sleep_time);
    } else {
        /* QEMU start */
        start_qemu(cmd);
    }

    if (b_info->shm == 1) {
        free_shm(b_info);
    }

cskysim_end:
    return 0;

cskysim_fail:
    return 1;
}
