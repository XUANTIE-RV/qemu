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

#define PA_RW_L3_INDEX  0xFFC00000000000ULL
#define PA_RW_L2_INDEX  0x3FFFFE000000ULL
#define PA_RW_L1_INDEX  0x1FF0000ULL
#define PA_RW_L2_OFFSET 0x1E00000ULL
#define PA_RW_L1_OFFSET 0xF000ULL
#define L2_RW_TYPE_MASK 0xF00000000000ULL

static bool mtt_rw_lookup(CPURISCVState *env, hwaddr addr, mtt_mode_t mode,
                          mtt_access_t *allowed_access,
                          MMUAccessType access_type)
{
    MemTxResult res;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    hwaddr base;
    hwaddr L3_addr, L2_addr, L1_addr;
    uint64_t L3_entry, L2_entry, L1_entry;
    int access, index, pmp_prot, pmp_ret;

    CPUState *cs = env_cpu(env);
    base = (hwaddr)env->mttppn << PGSHIFT;

    /* PAW = 56, lookup MTTL3 */
    if (mode == SMMTT56RW) {
        L3_addr = base + (((addr & PA_RW_L3_INDEX) >> 46) << 3);
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
        L3_entry = address_space_ldq(cs->as, L3_addr, attrs, &res);
        base = (hwaddr)(L3_entry & MTTP_PPN_MASK_64) << PGSHIFT;
    }
    /* lookup MTTL2 */
    L2_addr = base + (((addr & PA_RW_L2_INDEX) >> 25) << 3);
    pmp_ret = get_physical_address_pmp(env, &pmp_prot, L2_addr,
                                       sizeof(uint64_t),
                                       MMU_DATA_LOAD, PRV_M);
    if (pmp_ret != TRANSLATE_SUCCESS) {
        return false;
    }
    L2_entry = address_space_ldq(cs->as, L2_addr, attrs, &res);

    int L2_type = (L2_entry & L2_RW_TYPE_MASK) >> 44;
    switch (L2_type) {
    case 0b0000:
        /* 1G_disallow */
        *allowed_access = ACCESS_DISALLOW;
        return false;
        break;
    case 0b0001:
        /* 1G_allow_r */
        *allowed_access = ACCESS_ALLOW_R;
        return (access_type == MMU_DATA_LOAD ||
                access_type == MMU_INST_FETCH);
        break;
    case 0b0011:
        /* 1G_allow_rw */
        *allowed_access = ACCESS_ALLOW_RW;
        return true;
        break;
    case 0b0100:
        /* MTT_L1_DIR */
        break;
    case 0b0111:
        /* 2M_PAGES */
        index = (addr & PA_RW_L2_OFFSET) >> 21;
        access = (L2_entry & (0b11ULL << (index * 2))) >> (index * 2);
        switch (access) {
        case 0b00:
            *allowed_access = ACCESS_DISALLOW;
            return false;
            break;
        case 0b01:
            *allowed_access = ACCESS_ALLOW_R;
            return (access_type == MMU_DATA_LOAD ||
                    access_type == MMU_INST_FETCH);
            break;
        case 0b11:
            *allowed_access = ACCESS_ALLOW_RW;
            return true;
            break;
        default:
            g_assert_not_reached();
            break;
        }
        break;
    default:
        g_assert_not_reached();
        break;
    }

    /* Lookup MTTL1 */
    base = (hwaddr)(L2_entry & MTTP_PPN_MASK_64) << PGSHIFT;
    L1_addr = base + (((addr & PA_RW_L1_INDEX) >> 16) << 3);
    pmp_ret = get_physical_address_pmp(env, &pmp_prot, L1_addr,
                                       sizeof(uint64_t),
                                       MMU_DATA_LOAD, PRV_M);
    if (pmp_ret != TRANSLATE_SUCCESS) {
        return false;
    }
    L1_entry = address_space_ldq(cs->as, L1_addr, attrs, &res);
    index = (addr & PA_RW_L1_OFFSET) >> 12;
    access = (L1_entry & (0b1111ULL << (index * 4))) >> (index * 4);
    switch (access) {
    case 0b0000:
        *allowed_access = ACCESS_DISALLOW;
        return false;
        break;
    case 0b0001:
        *allowed_access = ACCESS_ALLOW_R;
        return (access_type == MMU_DATA_LOAD ||
                access_type == MMU_INST_FETCH);
        break;
    case 0b0011:
        *allowed_access = ACCESS_ALLOW_RW;
        return true;
        break;
    default:
        g_assert_not_reached();
        break;
    }
    return false;
}

#define PA_L3_INDEX  0xFFC00000000000ULL
#define PA_L2_INDEX  0x3FFFFC000000ULL
#define PA_L1_INDEX  0x3FE0000ULL
#define PA_L2_OFFSET 0x3E00000ULL
#define PA_L1_OFFSET 0x1F000ULL
#define L2_TYPE_MASK 0x300000000000ULL

static bool mtt_lookup(CPURISCVState *env, hwaddr addr, mtt_mode_t mode,
                       mtt_access_t *allowed_access,
                       MMUAccessType access_type)
{
    MemTxResult res;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    hwaddr base;
    hwaddr L3_addr, L2_addr, L1_addr;
    uint64_t L3_entry, L2_entry, L1_entry;
    int access, index, pmp_ret, pmp_prot;

    CPUState *cs = env_cpu(env);
    base = (hwaddr)env->mttppn << PGSHIFT;

    /* PAW = 56, lookup MTTL3 */
    if (mode == SMMTT56) {
        L3_addr = base + (((addr & PA_L3_INDEX) >> 46) << 3);
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
        L3_entry = address_space_ldq(cs->as, L3_addr, attrs, &res);
        base = (hwaddr)(L3_entry & MTTP_PPN_MASK_64) << PGSHIFT;
    }

    /* lookup MTTL2 */
    L2_addr = base + (((addr & PA_L2_INDEX) >> 26) << 3);
    pmp_ret = get_physical_address_pmp(env, &pmp_prot, L2_addr,
                                       sizeof(uint64_t),
                                       MMU_DATA_LOAD, PRV_M);
    if (pmp_ret != TRANSLATE_SUCCESS) {
        return false;
    }
    L2_entry = address_space_ldq(cs->as, L2_addr, attrs, &res);

    int L2_type = (L2_entry & L2_TYPE_MASK) >> 44;
    switch (L2_type) {
    case 0b00:
        /* 1G_disallow */
        *allowed_access = ACCESS_DISALLOW;
        return false;
        break;
    case 0b01:
        /* 1G_allow */
        *allowed_access = ACCESS_ALLOW;
        return true;
        break;
    case 0b10:
        /* MTT_L1_DIR */
        break;
    case 0b11:
        /* 2M_PAGES */
        index = (addr & PA_L2_OFFSET) >> 21;
        access = ((L2_entry & (0b1ULL << index)) >> index);
        switch (access) {
        case 0b0:
            *allowed_access = ACCESS_DISALLOW;
            return false;
            break;
        case 0b1:
            *allowed_access = ACCESS_ALLOW;
            return true;
            break;
        default:
            g_assert_not_reached();
            break;
        }
        break;
    default:
        g_assert_not_reached();
        break;
    }

    /* Lookup MTTL1 */
    base = (hwaddr)(L2_entry & MTTP_PPN_MASK_64) << PGSHIFT;
    L1_addr = base + (((addr & PA_L1_INDEX) >> 17) << 3);
    pmp_ret = get_physical_address_pmp(env, &pmp_prot, L1_addr,
                                       sizeof(uint64_t),
                                       MMU_DATA_LOAD, PRV_M);
    if (pmp_ret != TRANSLATE_SUCCESS) {
        return false;
    }
    L1_entry = address_space_ldq(cs->as, L1_addr, attrs, &res);
    index = (addr & PA_L1_OFFSET) >> 12;
    access = (L1_entry & (0b11ULL << (index * 2))) >> (index * 2);
    switch (access) {
    case 0b00:
        *allowed_access = ACCESS_DISALLOW;
        return false;
        break;
    case 0b01:
        *allowed_access = ACCESS_ALLOW;
        return true;
        break;
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

    /* rw mode is always a multiple of 2.*/
    if (mode % 1 == 0) {
        mtt_has_access = mtt_rw_lookup(env, addr, mode,
                                       allowed_access, access_type);
    } else {
        mtt_has_access = mtt_lookup(env, addr, mode,
                                    allowed_access, access_type);
    }
    return mtt_has_access;
}

/*
 * Convert MTT access to TLB page privilege.
 */
int mtt_access_to_page_prot(mtt_access_t mtt_access)
{
    int prot;
    switch (mtt_access) {
    case ACCESS_ALLOW:
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        break;
    case ACCESS_ALLOW_R:
        prot = PAGE_READ | PAGE_EXEC;
        break;
    case ACCESS_ALLOW_RW:
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        break;
    default:
        prot = 0;
        break;
    }

    return prot;
}
