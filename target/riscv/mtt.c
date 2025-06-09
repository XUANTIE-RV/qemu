/*
 * QEMU RISC-V MTT(Memory tracking table)
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
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

#include "qemu/osdep.h"
#include "cpu.h"

#define PN_3_64  0xFFC00000000000ULL
#define PN_2_64  0x3FFFFE000000ULL
#define PN_1_64  0x1FF0000ULL
#define PN_0_64  0xF000ULL
#define PN_2_32  0x3FE000000ULL
#define PN_1_32  0x1FF8000ULL
#define PN_0_32  0x7000ULL
#define PA_2M_L2_OFFSET 0x1E00000ULL
#define PA_4M_L2_OFFSET 0x1C00000ULL

typedef uint64_t load_entry_fn(AddressSpace *, hwaddr,
                               MemTxAttrs, MemTxResult *);

static uint64_t load_entry_32(AddressSpace *as, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    return address_space_ldl(as, addr, attrs, result);
}

static uint64_t load_entry_64(AddressSpace *as, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    return address_space_ldq(as, addr, attrs, result);
}

static bool mtt_lookup(CPURISCVState *env, hwaddr addr, mtt_mode_t mode,
                       mtt_access_t *allowed_access,
                       MMUAccessType access_type)
{
    MemTxResult res;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    hwaddr base;
    hwaddr L3_addr, L2_addr, L1_addr;
    uint64_t L3_entry, L2_entry, L1_entry;
    int access, index, pmp_prot, pmp_ret;
    int pte_size, xlen;
    uint64_t pn[4];
    uint64_t l2_type_mask, l2_type_shift, l2_info_mask, l2_reserved_mask;
    uint64_t l1_reserved_mask;
    load_entry_fn *load_entry;
    RISCVMXL mxl = riscv_cpu_mxl(env);

    CPUState *cs = env_cpu(env);
    base = (hwaddr)env->mttppn << PGSHIFT;

    switch (mxl) {
    case MXL_RV32:
        l2_type_mask = 0x1C00000ULL;
        l2_type_shift = 22;
        l2_info_mask = 0x3FFFFFULL;
        l2_reserved_mask = MPTE_L2_RESERVED_32;
        l1_reserved_mask = MPTE_L1_RESERVED_32;
        load_entry = &load_entry_32;
        pte_size = 2;
        xlen = 32;
        break;
    case MXL_RV64:
        l2_type_mask = 0x700000000000ULL;
        l2_type_shift = 44;
        l2_info_mask = 0xFFFFFFFFFFFULL;
        l2_reserved_mask = MPTE_L2_RESERVED_64;
        l1_reserved_mask = MPTE_L1_RESERVED_64;
        load_entry = &load_entry_64;
        pte_size = 3;
        xlen = 64;
        break;
    default:
        g_assert_not_reached();
        break;
    }

    switch (mode) {
    case SMMTT34:
    case SMMTT46:
        pn[2] = (addr & PN_2_32) >> 25;
        pn[1] = (addr & PN_1_32) >> 15;
        pn[0] = (addr & PN_0_32) >> 12;
        break;
    case SMMTT56:
        pn[3] = (addr & PN_3_64) >> 46;
        pn[2] = (addr & PN_2_64) >> 25;
        pn[1] = (addr & PN_1_64) >> 16;
        pn[0] = (addr & PN_0_64) >> 12;
        break;
    default:
        g_assert_not_reached();
        break;
    }
    /* PAW = 56, lookup MTTL3 */
    if (mode == SMMTT56) {
        L3_addr = base + (pn[3] << pte_size);
        /*
         * MTT structure accesses are to be treated as implicit M-mode accesses
         * and are subject to PMP/Smepmp and IOPMP checks.
         */
        pmp_ret = get_physical_address_pmp(env, &pmp_prot, L3_addr,
                                           sizeof(uint64_t),
                                           MMU_DATA_LOAD, PRV_M);
        if (pmp_ret != TRANSLATE_SUCCESS) {
            return false;
        }
        L3_entry = load_entry(cs->as, L3_addr, attrs, &res);
        base = (hwaddr)(L3_entry & MTTP_PPN_MASK_64) << PGSHIFT;
        if ((L3_entry & MPTE_L3_VALID) == 0) {
            return false;
        }
        g_assert((L3_entry & MPTE_L3_RESERVED) == 0);
    }

    /* lookup MTTL2 */
    L2_addr = base + (pn[2] << pte_size);
    pmp_ret = get_physical_address_pmp(env, &pmp_prot, L2_addr,
                                       xlen / 8,
                                       MMU_DATA_LOAD, PRV_M);
    if (pmp_ret != TRANSLATE_SUCCESS) {
        return false;
    }
    L2_entry = load_entry(cs->as, L2_addr, attrs, &res);
    g_assert((L2_entry & l2_reserved_mask) == 0);

    int L2_type = (L2_entry & l2_type_mask) >> l2_type_shift;
    switch (L2_type) {
    case 0b000:
        /* 1G_disallow */
        *allowed_access = ACCESS_DISALLOW;
        return false;
    case 0b001:
        /* 1G_allow_rx */
        *allowed_access = ACCESS_ALLOW_RX;
        return (access_type == MMU_DATA_LOAD ||
                access_type == MMU_INST_FETCH);
    case 0b010:
        /* 1G_allow_rw */
        *allowed_access = ACCESS_ALLOW_RW;
        return (access_type == MMU_DATA_LOAD ||
                access_type == MMU_DATA_STORE);
    case 0b011:
        /* 1G_allow_rwx */
        *allowed_access = ACCESS_ALLOW_RWX;
        return true;
    case 0b100:
        /* MTT_L1_DIR */
        break;
    case 0b0101:
        if (mxl == MXL_RV32) {
            /* 4M_PAGES */
            index = (addr & PA_4M_L2_OFFSET) >> 22;
         } else {
            /* 2M_PAGES */
            index = (addr & PA_2M_L2_OFFSET) >> 21;
         }
        access = (L2_entry & (0b11ULL << (index * 2))) >> (index * 2);
        switch (access) {
        case 0b00:
            *allowed_access = ACCESS_DISALLOW;
            return false;
        case 0b01:
            *allowed_access = ACCESS_ALLOW_RX;
            return (access_type == MMU_DATA_LOAD ||
                    access_type == MMU_INST_FETCH);
        case 0b10:
            *allowed_access = ACCESS_ALLOW_RW;
            return (access_type == MMU_DATA_LOAD ||
                    access_type == MMU_DATA_STORE);
        case 0b11:
            *allowed_access = ACCESS_ALLOW_RWX;
            return true;
        default:
            g_assert_not_reached();
            break;
        }
        break;
    default:
        /* Reserved for future use and causes an access violation if used. */
        return false;
    }

    /* Lookup MTTL1 */
    base = (hwaddr)(L2_entry & l2_info_mask) << PGSHIFT;
    L1_addr = base + (pn[1] << pte_size);
    pmp_ret = get_physical_address_pmp(env, &pmp_prot, L1_addr,
                                       xlen / 8,
                                       MMU_DATA_LOAD, PRV_M);
    if (pmp_ret != TRANSLATE_SUCCESS) {
        return false;
    }
    L1_entry = load_entry(cs->as, L1_addr, attrs, &res);
    g_assert((L1_entry & l1_reserved_mask) == 0);
    index = pn[0];
    access = (L1_entry & (0b11ULL << (index * 2))) >> (index * 2);
    switch (access & 0b11ULL) {
    case 0b00:
        *allowed_access = ACCESS_DISALLOW;
        return false;
    case 0b01:
        *allowed_access = ACCESS_ALLOW_RX;
        return (access_type == MMU_DATA_LOAD ||
                access_type == MMU_INST_FETCH);
    case 0b10:
        *allowed_access = ACCESS_ALLOW_RW;
        return (access_type == MMU_DATA_LOAD ||
                access_type == MMU_DATA_STORE);
    case 0b11:
        *allowed_access = ACCESS_ALLOW_RWX;
        return true;
    default:
        g_assert_not_reached();
        break;
    }
    return false;
}

bool mtt_check_access(CPURISCVState *env, hwaddr addr,
                      mtt_access_t *allowed_access, MMUAccessType access_type)
{
    bool mtt_has_access;
    mtt_mode_t mode = env->mttmode;

    mtt_has_access = mtt_lookup(env, addr, mode,
                                allowed_access, access_type);
    return mtt_has_access;
}

/*
 * Convert MTT access to TLB page privilege.
 */
int mtt_access_to_page_prot(mtt_access_t mtt_access)
{
    int prot;
    switch (mtt_access) {
    case ACCESS_ALLOW_RX:
        prot = PAGE_READ | PAGE_EXEC;
        break;
    case ACCESS_ALLOW_RW:
        prot = PAGE_READ | PAGE_WRITE;
        break;
    case ACCESS_ALLOW_RWX:
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        break;
    default:
        prot = 0;
        break;
    }

    return prot;
}
