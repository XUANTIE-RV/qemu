/*
 * C-SKY mmu
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
#include "cpu.h"
#include "translate.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#if !defined(CONFIG_USER_ONLY)
/**************************** MMU ***************************/
static int get_page_bits(CPUCSKYState *env)
{
    switch ((env->mmu.mpr >> 13) & 0xfff) {
    case 0x0:
        return 12;
    case 0x3:
        return 14;
    case 0xf:
        return 16;
    case 0x3f:
        return 18;
    case 0xff:
        return 20;
    case 0x3ff:
        return 22;
    case 0xfff:
        return 24;
    default:
        qemu_log_mask(CPU_LOG_MMU, "CSKY CPU does not support "
                      "PageMask 0x%x!\n", env->mmu.mpr);
        return 0;
    }
}

void helper_ttlbinv_all(CPUCSKYState *env)
{
    CPUState *cs = env_cpu(env);
    if (env->full_mmu) {
        memset(env->tlb_context->t_tlb, 0,
               sizeof(struct csky_tlb_t) * CSKY_TLB_MAX);
        memset(env->tlb_context->nt_tlb, 0,
               sizeof(struct csky_tlb_t) * CSKY_TLB_MAX);
    }
    tlb_flush(cs);
}

void helper_tlbinv_idx(CPUCSKYState *env)
{
    CPUState *cs = env_cpu(env);
    csky_tlb_t *ptlb;
    int j;

    if (env->full_mmu) {
        ptlb = &env->tlb_context->tlb[env->mmu.mir & 0x7f];

        for (j = ptlb->VPN;
             j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
            tlb_flush_page(cs, j);
        }
        memset(ptlb, 0, sizeof(struct csky_tlb_t));
    } else {
        tlb_flush(cs);
    }
}

void helper_tlbinv_all(CPUCSKYState *env)
{
    CPUState *cs = env_cpu(env);
    if (env->full_mmu) {
        memset(env->tlb_context->tlb, 0,
               sizeof(struct csky_tlb_t) * CSKY_TLB_MAX);
    }
    tb_flush(cs);
    tlb_flush(cs);
}

void helper_tlbinv_all_s(CPUCSKYState *env)
{
    CPUState *cs = env_cpu(env);
    CPU_FOREACH(cs) {
        env = csky_cpu_get_env(cs);
        helper_tlbinv_all(env);
    }
}

static inline uint32_t csky_get_asid(CPUCSKYState *env, uint32_t rx)
{
    if (env->features & CPU_C860) {
        return rx & CSKY_MP_ASID_MASK;
    } else {
        return rx & CSKY_ASID_MASK;
    }
}

static inline uint32_t csky_get_vpn(CPUCSKYState *env, uint32_t rx)
{
    return rx & CSKY_VPN_MASK & ~(env->mmu.mpr | 0x1fff);
}

void helper_tlbinv_asid(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = env_cpu(env);

    if (env->full_mmu) {
        uint32_t asid = csky_get_asid(env, rx);
        csky_tlb_t *ptlb = env->tlb_context->tlb;
        int i;

        for (i = 0; i < CSKY_TLB_MAX; ++i) {
            if (ptlb->ASID == asid) {
                ptlb->V0 = 0;
                ptlb->V1 = 0;
                int j;
                for (j = ptlb->VPN;
                     j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
                    tlb_flush_page(cs, j);
                }
                memset(ptlb, 0, sizeof(struct csky_tlb_t));
            }
            ptlb++;
        }
    } else {
        tlb_flush(cs);
    }
}

void helper_tlbinv_asid_s(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = env_cpu(env);
    CPU_FOREACH(cs) {
        env = csky_cpu_get_env(cs);
        helper_tlbinv_asid(env, rx);
    }
}

void helper_tlbinv_vaa(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = env_cpu(env);

    if (env->full_mmu) {
        uint32_t vpn = csky_get_vpn(env, rx);
        csky_tlb_t *ptlb = env->tlb_context->tlb;
        int i;

        for (i = 0; i < CSKY_TLB_MAX; ++i) {
            if (ptlb->VPN == vpn) {
                ptlb->V0 = 0;
                ptlb->V1 = 0;
                int j;
                for (j = ptlb->VPN;
                     j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
                    tlb_flush_page(cs, j);
                }
                memset(ptlb, 0, sizeof(struct csky_tlb_t));
            }
            ptlb++;
        }
    } else {
        tlb_flush(cs);
    }
}

void helper_tlbinv_vaa_s(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = env_cpu(env);
    CPU_FOREACH(cs) {
        env = csky_cpu_get_env(cs);
        helper_tlbinv_vaa(env, rx);
    }
}

void helper_tlbinv_va(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = env_cpu(env);

    if (env->full_mmu) {
        uint32_t vpn = csky_get_vpn(env, rx);
        uint32_t asid = csky_get_asid(env, rx);
        csky_tlb_t *ptlb = env->tlb_context->tlb;
        int i;

        for (i = 0; i < CSKY_TLB_MAX; ++i) {
            if (ptlb->VPN == vpn && ptlb->ASID == asid) {
                ptlb->V0 = 0;
                ptlb->V1 = 0;
                int j;
                for (j = ptlb->VPN;
                     j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
                    tlb_flush_page(cs, j);
                }
                memset(ptlb, 0, sizeof(struct csky_tlb_t));
            }
            ptlb++;
        }
    } else {
        tlb_flush(cs);
    }
}

void helper_tlbinv_va_s(CPUCSKYState *env, uint32_t rx)
{
    CPUState *cs = env_cpu(env);
    CPU_FOREACH(cs) {
        env = csky_cpu_get_env(cs);
        helper_tlbinv_va(env, rx);
    }
}

void helper_tlbinv(CPUCSKYState *env)
{
    CPUState *cs = env_cpu(env);

    if (env->full_mmu) {
        csky_tlb_t *ptlb;
        uint8_t asid;
        int i;

        asid = ENV_GET_ASID(env);
        ptlb = &env->tlb_context->tlb[0];

        for (i = 0; i < CSKY_TLB_MAX; ++i) {
            if (ptlb->ASID == asid) {
                ptlb->V0 = 0;
                ptlb->V1 = 0;
                int j;
                for (j = ptlb->VPN;
                     j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
                    tlb_flush_page(cs, j);
                }
                memset(ptlb, 0, sizeof(struct csky_tlb_t));
            }
            ptlb++;
        }
    } else {
        tlb_flush(cs);
    }
}

void csky_tlbwi(CPUCSKYState *env)
{
    csky_tlb_t *ptlb;
    CPUState *cs = env_cpu(env);

    if (!env->full_mmu) {
        tlb_flush(cs);
        return;
    }

    ptlb = &env->tlb_context->tlb[env->mmu.mir & 0x7f];
    int j;
    for (j = ptlb->VPN; j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
        tlb_flush_page(cs, j);
    }

    ptlb->VPN   = env->mmu.meh & ~(env->mmu.mpr | 0x1fff);
    ptlb->ASID  = ENV_GET_ASID(env);
    ptlb->G     = env->mmu.mel0 & env->mmu.mel1 & 0x1;
    ptlb->C0    = (env->mmu.mel0 >> 3) & 0x7;
    ptlb->C1    = (env->mmu.mel1 >> 3) & 0x7;
    ptlb->V0    = (env->mmu.mel0 >> 1) & 0x1;
    ptlb->V1    = (env->mmu.mel1 >> 1) & 0x1;
    ptlb->D0    = (env->mmu.mel0 >> 2) & 0x1;
    ptlb->D1    = (env->mmu.mel1 >> 2) & 0x1;
#if !defined(TARGET_CSKYV2)
    ptlb->PFN[0] = (env->mmu.mel0 << 6) & ~((env->mmu.mpr >> 1) | 0xfff);
    ptlb->PFN[1] = (env->mmu.mel1 << 6) & ~((env->mmu.mpr >> 1) | 0xfff);
#else
    ptlb->PFN[0] = env->mmu.mel0 & ~((env->mmu.mpr >> 1) | 0xfff);
    ptlb->PFN[1] = env->mmu.mel1 & ~((env->mmu.mpr >> 1) | 0xfff);
#endif
#if !defined(TARGET_CSKYV2)
    ptlb->PageMask = env->mmu.mpr;
#endif

    for (j = ptlb->VPN; j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
        tlb_flush_page(cs, j);
    }
}

void csky_tlbwr(CPUCSKYState *env)
{
    csky_tlb_t *ptlb;
    uint32_t index;
    CPUState *cs = env_cpu(env);
    int j;

    if (!env->full_mmu) {
        tlb_flush(cs);
        return;
    }

    /* index = robin,VPN[18:13]   page size: 4KB*/
    /* index = robin,VPN[20:15]   page size: 16KB*/
    /* index = robin,VPN[22:17]   page size: 64KB*/
    /* ... */
    index = (env->mmu.meh >> (get_page_bits(env) + 1)) & 0x3f;
    if (env->tlb_context->round_robin[index]) {
        env->tlb_context->round_robin[index] = 0;
        index = index + 64;
    } else {
        env->tlb_context->round_robin[index] = 1;
    }
    ptlb =  &env->tlb_context->tlb[index];

    for (j = ptlb->VPN; j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
        tlb_flush_page(cs, j);
    }

    ptlb->VPN   = env->mmu.meh & ~(env->mmu.mpr | 0x1fff);
    ptlb->ASID  = ENV_GET_ASID(env);
    ptlb->G     = env->mmu.mel0 & env->mmu.mel1 & 0x1;
    ptlb->C0    = (env->mmu.mel0 >> 3) & 0x7;
    ptlb->C1    = (env->mmu.mel1 >> 3) & 0x7;
    ptlb->V0    = (env->mmu.mel0 >> 1) & 0x1;
    ptlb->V1    = (env->mmu.mel1 >> 1) & 0x1;
    ptlb->D0    = (env->mmu.mel0 >> 2) & 0x1;
    ptlb->D1    = (env->mmu.mel1 >> 2) & 0x1;
#if !defined(TARGET_CSKYV2)
    ptlb->PFN[0] = (env->mmu.mel0 << 6) & ~((env->mmu.mpr >> 1) | 0xfff);
    ptlb->PFN[1] = (env->mmu.mel1 << 6) & ~((env->mmu.mpr >> 1) | 0xfff);
#else
    ptlb->PFN[0] = env->mmu.mel0 & ~((env->mmu.mpr >> 1) | 0xfff);
    ptlb->PFN[1] = env->mmu.mel1 & ~((env->mmu.mpr >> 1) | 0xfff);
#endif
#if !defined(TARGET_CSKYV2)
    ptlb->PageMask = env->mmu.mpr;
#endif

    for (j = ptlb->VPN; j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
        tlb_flush_page(cs, j);
    }
}

void csky_tlbp(CPUCSKYState *env)
{
    csky_tlb_t *ptlb;
    uint32_t index;

    if (!env->full_mmu) {
        env->mmu.mir = 2;
        return;
    }


    /* index = robin,VPN[18:13]*/
    index = (env->mmu.meh >> (get_page_bits(env) + 1)) & 0x3f;
    ptlb =  &env->tlb_context->tlb[index];

    if (ptlb->VPN == (env->mmu.meh & ~(env->mmu.mpr | 0x1fff))
            && (ptlb->G == 1 || ptlb->ASID == ENV_GET_ASID(env))) {
        env->mmu.mir = index;
        return;
    }

    index += 64;
    ptlb =  &env->tlb_context->tlb[index];
    if (ptlb->VPN == (env->mmu.meh & ~(env->mmu.mpr | 0x1fff))
            && (ptlb->G == 1 || ptlb->ASID == ENV_GET_ASID(env))) {
        env->mmu.mir = index;
        return;
    }

    /* if not found, set P bit */
    env->mmu.mir |= (1 << 31);
}

void csky_tlbr(CPUCSKYState *env)
{
    /* FIXME */
    CPUState *cs = env_cpu(env);
    csky_tlb_t *ptlb;

    if (!env->full_mmu) {
        tlb_flush(cs);
        return;
    }


    ptlb = &env->tlb_context->tlb[env->mmu.mir & 0x7f];

    env->mmu.meh = ptlb->VPN | ptlb->ASID;
#if !defined(TARGET_CSKYV2)
    env->mmu.mel0 = ptlb->PFN[0] >> 6 | ptlb->C0 << 3
                    | ptlb->D0 << 2 | ptlb->V0 << 1 | ptlb->G;

    env->mmu.mel1 = ptlb->PFN[1] >> 6 | ptlb->C1 << 3
                    | ptlb->D1 << 2 | ptlb->V1 << 1 | ptlb->G;
#else
    env->mmu.mel0 = ptlb->PFN[0]  | ptlb->C0 << 3
                    | ptlb->D0 << 2 | ptlb->V0 << 1 | ptlb->G;

    env->mmu.mel1 = ptlb->PFN[1]  | ptlb->C1 << 3
                    | ptlb->D1 << 2 | ptlb->V1 << 1 | ptlb->G;
#endif
#if !defined(TARGET_CSKYV2)
    env->mmu.mpr = ptlb->PageMask;
#endif
}

/* ----------------------------- */

enum {
    TLBRET_ABORT = -5,
    TLBRET_DIRTY = -4,
    TLBRET_INVALID = -3,
    TLBRET_NOMATCH = -2,
    TLBRET_BADADDR = -1,
    TLBRET_MATCH = 0
};

static int get_physical_address(CPUCSKYState *env, hwaddr *physical,
                                int *prot, target_ulong addr,
                                int access_type, int mmu_idx)
{
    if (env->cp0.ccr & 1) {
        if (env->features & CSKY_MMU) {
            if (env->full_mmu) {                                             
                return mmu_get_physical_address(env, physical, prot, addr,
                    access_type & 1);
            } else {
                return thin_mmu_get_physical_address(env, physical, prot, addr,
                    access_type & 1);
            }
        } else if (env->features & CSKY_MPU) {
            return mpu_get_physical_address(env, physical, prot, addr,
                        access_type & 1);
        }
    }

    return nommu_get_physical_address(env, physical, prot, addr,
                access_type & 1);
}

int mmu_get_physical_address(CPUCSKYState *env,
                             hwaddr *physical,
                             int *prot, target_ulong address, int rw)
{
    uint32_t index;
    uint32_t super_mode;
    uint32_t pgd_addr;
    csky_tlb_t *ptlb;
    uint8_t odd;
    uint8_t ASID;
    int page_bits;
    CPUState *cs = env_cpu(env);

    page_bits = get_page_bits(env);
    super_mode = env->psr_s;

    if (address >= 0x80000000 && address < 0xa0000000) {
        if (super_mode) {
            if (!(env->mmu.msa0 & 0x2)) {
                return TLBRET_INVALID;
            }
            if (rw == 0 || env->mmu.msa0 & 0x4) {
                *physical = address - 0x80000000 + (env->mmu.msa0 & 0xe0000000);
                *prot = PAGE_READ;
                if (env->mmu.msa0 & 0x4) {
                    *prot |= PAGE_WRITE;
                }
                return TLBRET_MATCH;
            }
            return TLBRET_DIRTY;
        }
        return TLBRET_BADADDR;
    } else if (address >= 0xa0000000 && address < 0xc0000000) {
        if (super_mode) {
            if (!(env->mmu.msa1 & 0x2)) {
                return TLBRET_INVALID;
            }
            if (rw == 0 || env->mmu.msa1 & 0x4) {
                *physical = address - 0xa0000000 + (env->mmu.msa1 & 0xe0000000);
                *prot = PAGE_READ;
                if (env->mmu.msa1 & 0x4) {
                    *prot |= PAGE_WRITE;
                }
                return TLBRET_MATCH;
            }
            return TLBRET_DIRTY;
        }
        return TLBRET_BADADDR;
    } else if (address >= 0xc0000000) {
        if (super_mode) {
           if (env->features & CPU_C860) {
               pgd_addr = env->mmu.mpgd0 & (~0xfff);
           } else {
               pgd_addr = env->mmu.mpgd1 & (~0xfff);
           }
           goto do_map;
        }
        return TLBRET_BADADDR;
    } else {        /* address in 0x0-0x80000000*/
        pgd_addr = env->mmu.mpgd1 & (~0xfff);
    }

do_map:
    /* MMU is enable */
    ASID = ENV_GET_ASID(env);
    odd = (address >> page_bits) & 0x1;
    /* */
    index = (address >> (page_bits + 1)) & 0x3f;
    ptlb =  &env->tlb_context->tlb[index];

    if (((address & ~(env->mmu.mpr | 0x1fff)) == 0) &&
        !(odd ? ptlb->V1 : ptlb->V0)) {
        goto hard_refill;
    }

    if ((ptlb->G == 1 || ptlb->ASID == ASID) &&
       ptlb->VPN == (address & ~(env->mmu.mpr | 0x1fff))) {
        if (!(odd ? ptlb->V1 : ptlb->V0)) {
            return TLBRET_INVALID;
        }
        if (rw == 0 || (odd ? ptlb->D1 : ptlb->D0)) {
            *physical = ptlb->PFN[odd] | (address & ((1 << page_bits) - 1));
            *prot = PAGE_READ;
            if ((odd ? ptlb->D1 : ptlb->D0)) {
                *prot |= PAGE_WRITE;
            }
            return TLBRET_MATCH;
        }
        return TLBRET_DIRTY;
    }

    index += 64;
    ptlb = &env->tlb_context->tlb[index];

    if ((ptlb->G == 1 || ptlb->ASID == ASID) &&
        ptlb->VPN == (address & ~(env->mmu.mpr | 0x1fff))) {
        if (!(odd ? ptlb->V1 : ptlb->V0)) {
            return TLBRET_INVALID;
        }
        if (rw == 0 || (odd ? ptlb->D1 : ptlb->D0)) {
            *physical = ptlb->PFN[odd] | (address & ((1 << page_bits) - 1));
            *prot = PAGE_READ;
            if ((odd ? ptlb->D1 : ptlb->D0)) {
                *prot |= PAGE_WRITE;
            }
            return TLBRET_MATCH;
        }
        return TLBRET_DIRTY;
    }
    /* FIXME add cskyv2 hard_tlb_refill*/
hard_refill:
    if (((env->mmu.mpr >> 13) & 0xfff) == 0x0) {
        uint32_t pte_addr, pte0, pte1, j;

#define PGDIR_SHIFT    22
#define PTE_INDX_SHIFT  10

        /* Get current pgd table base */
        pgd_addr += (address >> PGDIR_SHIFT) << 2;
        /* Get pte table base */
        pte_addr = ldl_phys(cs->as, pgd_addr);
        pte_addr += (address >> PTE_INDX_SHIFT) & 0xff8;

        pte0 = ldl_phys(cs->as, pte_addr);
        pte1 = ldl_phys(cs->as, pte_addr + 4);

        odd = (address >> 12) & 0x1;
        index = (address >> 13) & 0x3f;

        if (env->tlb_context->round_robin[index]) {
            env->tlb_context->round_robin[index] = 0;
            index = index + 64;
        } else {
            env->tlb_context->round_robin[index] = 1;
        }
        ptlb = &env->tlb_context->tlb[index];

        /* csky_tlb has two 2 pfn, but tlb_set_page only set 1 softmmu page,
         * so flush both 2 softmmu pages before refill it. */
        for (j = ptlb->VPN; j <= (ptlb->VPN | env->mmu.mpr | 0x1000); j += 0x1000) {
            tlb_flush_page(cs, j);
        }

        ptlb->VPN   = address & ~0x1fff;
        ptlb->ASID  = ENV_GET_ASID(env);
#if !defined(TARGET_CSKYV2)
        ptlb->G     = (pte0 >> 6) & (pte1 >> 6) & 0X1;
        ptlb->C0    = (pte0 >> 9) & 0x7;
        ptlb->C1    = (pte1 >> 9) & 0x7;
        ptlb->V0    = (pte0 >> 7) & 0x1;
        ptlb->V1    = (pte1 >> 7) & 0x1;
        ptlb->D0    = (pte0 >> 8) & 0x1;
        ptlb->D1    = (pte1 >> 8) & 0x1;
#else
        ptlb->G     = pte0 & pte1 & 0X1;
        ptlb->C0    = (pte0 >> 3) & 0x7;
        ptlb->C1    = (pte1 >> 3) & 0x7;
        ptlb->V0    = (pte0 >> 1) & 0x1;
        ptlb->V1    = (pte1 >> 1) & 0x1;
        ptlb->D0    = (pte0 >> 2) & 0x1;
        ptlb->D1    = (pte1 >> 2) & 0x1;
#endif

        ptlb->PFN[0] = pte0 & ~0xfff;
        ptlb->PFN[1] = pte1 & ~0xfff;

        ptlb->PageMask = env->mmu.mpr;

        if (!(odd ? ptlb->V1 : ptlb->V0)) {
            return TLBRET_INVALID;
        }
        if (rw == 0 || (odd ? ptlb->D1 : ptlb->D0)) {
            *physical = ptlb->PFN[odd] | (address & 0xfff);
            *prot = PAGE_READ;
            if ((odd ? ptlb->D1 : ptlb->D0)) {
                *prot |= PAGE_WRITE;
            }
            return TLBRET_MATCH;
        }
        return TLBRET_DIRTY;
    }

    return TLBRET_NOMATCH;
}

int thin_mmu_get_physical_address(CPUCSKYState *env,
                                  hwaddr *physical,
                                  int *prot, target_ulong address, int rw)
{
    uint32_t super_mode;
    uint32_t pgd_addr;
    CPUState *cs = env_cpu(env);

    super_mode = env->psr_s;

    if (address >= 0x80000000 && address < 0xa0000000) {
        if (super_mode) {
            if (!(env->mmu.msa0 & 0x2)) {
                return TLBRET_INVALID;
            }
            if (rw == 0 || env->mmu.msa0 & 0x4) {
                *physical = address - 0x80000000 + (env->mmu.msa0 & 0xe0000000);
                *prot = PAGE_READ;
                if (env->mmu.msa0 & 0x4) {
                    *prot |= PAGE_WRITE;
                }
                return TLBRET_MATCH;
            }
            return TLBRET_DIRTY;
        }
        return TLBRET_BADADDR;
    } else if (address >= 0xa0000000 && address < 0xc0000000) {
        if (super_mode) {
            if (!(env->mmu.msa1 & 0x2)) {
                return TLBRET_INVALID;
            }
            if (rw == 0 || env->mmu.msa1 & 0x4) {
                *physical = address - 0xa0000000 + (env->mmu.msa1 & 0xe0000000);
                *prot = PAGE_READ;
                if (env->mmu.msa1 & 0x4) {
                    *prot |= PAGE_WRITE;
                }
                return TLBRET_MATCH;
            }
            return TLBRET_DIRTY;
        }
        return TLBRET_BADADDR;
    } else if (address >= 0xc0000000) {
        if (super_mode) {
            if (env->features & CPU_C860) {
                pgd_addr = env->mmu.mpgd0 & (~0xfff);
            } else {
                pgd_addr = env->mmu.mpgd1 & (~0xfff);
            }
                goto do_map;
        }
        return TLBRET_BADADDR;
    } else {        /* address in 0x0-0x80000000*/
        pgd_addr = env->mmu.mpgd1 & (~0xfff);
    }

do_map:
#define PGDIR_SHIFT    22
#define PTE_INDX_SHIFT  10
#define PTE_PFN 0xfffff000

#ifdef TARGET_CSKYV2
#define PTE_V   0x00000002
#define PTE_D   0x00000004
#define PTE_G   0x00000001
#else
#define PTE_V   0x00000080
#define PTE_D   0x00000100
#define PTE_G   0x00000040
#endif

    if (((env->mmu.mpr >> 13) & 0xfff) == 0x0) {
        uint32_t pte_addr, pte;

        /* Get current pgd table base */
        pgd_addr += (address >> PGDIR_SHIFT) << 2;
        /* Get pte table base */
        pte_addr = ldl_phys(cs->as, pgd_addr);

        pte_addr += (address >> PTE_INDX_SHIFT) & 0xffc;

        pte = ldl_phys(cs->as, pte_addr);

        if (!(pte & PTE_V)) {
            return TLBRET_INVALID;
        }
        if (rw == 0 || (pte & PTE_D)) {
            *physical = (pte & PTE_PFN) | (address & 0xfff);
            *prot = PAGE_READ;
            if (pte & PTE_D) {
                *prot |= PAGE_WRITE;
            }
            return TLBRET_MATCH;
        }
        return TLBRET_DIRTY;
    }

    return TLBRET_NOMATCH;
}

int nommu_get_physical_address(CPUCSKYState *env,
                               hwaddr *physical,
                               int *prot, target_ulong address, int rw)
{
    *physical = address;
    *prot = PAGE_READ | PAGE_WRITE;
    return TLBRET_MATCH;
}

int mpu_get_physical_address(CPUCSKYState *env,
                             hwaddr *physical,
                             int *prot, target_ulong address, int rw)
{
    int i, t, base, size;
    int j = 0;

    for (i = 7; i >= 0; i--) {
        if (env->cp0.pacr[i] & 0x1) {
            if (((env->cp0.pacr[i] >> 1) & 0x1f) == 0x1f) {
                size = 0xffffffff;
            } else {
                size = (1 << (((env->cp0.pacr[i] >> 1) & 0x1f) + 1)) - 1;
            }
            base = env->cp0.pacr[i] & ~size; /* page align */
            if ((base <= address) && (address <= (base + size))) {
                t = (env->cp0.capr >> (8 + i * 2)) & 0x3;
                if (env->features & ABIV2_TEE) {
                    /* if the area config Trusted, but now world is Non-Trust
                     * can not access the area. */
                    if (!env->psr_t && (env->cp0.capr & (1 << (i + 24)))) {
                        j = 2;
                        break;
                    }
                }
                if (env->cp0.psr & 0x80000000) {
                    if (t == 0) {
                        j = 2;
                        break;
                    } else {
                        j = 0;
                        break;
                    }
                } else {
                    if (t < 2) {
                        j = 2;
                        break;
                    } else if (t == 2) {
                        if (rw == 0) {
                            j = 1;
                            break;
                        } else {
                            j = 2;
                            break;
                        }
                    } else {
                        j = 0;
                        break;
                    }
                }
            } else {
                j = 2;
                continue;
            }
        }
    }
    if (j == 0) {
        *physical = address;
        *prot = PAGE_READ | PAGE_WRITE;
        return TLBRET_MATCH;
    } else if (j == 1) {
        *physical = address;
        *prot = PAGE_READ;
        return TLBRET_MATCH;
    } else {
        return TLBRET_ABORT;
    }
}

#endif

bool csky_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr)
{
#ifndef CONFIG_USER_ONLY
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;
    int ret;
    int prot;
    hwaddr physical;

    ret = get_physical_address(env, &physical, &prot, address, access_type,
        mmu_idx);

    if (ret == TLBRET_MATCH) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                physical & TARGET_PAGE_MASK, prot | PAGE_EXEC,
                mmu_idx, TARGET_PAGE_SIZE);
        return true;
    } else if (probe) {
        return false;
    } else if (ret < 0) {
        /* Set the BADVPN Register */
        env->mmu.meh &= ~(0xfffff << 12);
        env->mmu.meh |= address & ~((1 << get_page_bits(env)) - 1);

        switch (ret) {
        case TLBRET_ABORT:
            cs->exception_index = EXCP_CSKY_DATA_ABORT;
            break;
        case TLBRET_DIRTY:
            cs->exception_index = EXCP_CSKY_TLB_MODIFY;
            break;
        case TLBRET_INVALID:
            if (access_type & 0x1) {
                cs->exception_index = EXCP_CSKY_TLB_WRITE_INVALID;
            } else {
                cs->exception_index = EXCP_CSKY_TLB_READ_INVALID;
            }
            break;
        case TLBRET_NOMATCH:
            cs->exception_index = EXCP_CSKY_TLB_UNMATCH;
            break;
        case TLBRET_BADADDR:
            cs->exception_index = EXCP_CSKY_DATA_ABORT;
            break;
        default:
            cs->exception_index = EXCP_CSKY_DATA_ABORT;
            break;
        }
        if (retaddr) {
            cpu_restore_state(cs, retaddr);
        }
        helper_exception(env, cs->exception_index);
    }
    return true;
#else
    cs->exception_index = EXCP_CSKY_DATA_ABORT;
    cpu_loop_exit_restore(cs, retaddr);
    return true;
#endif
}


