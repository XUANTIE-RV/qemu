/*
 * C-SKY gdb server stub
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
#include "exec/gdbstub.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "gdbstub/helpers.h"

#define NUM_CORE_REGS 144

static int cskyv1cpu_gdb_read_register(CPUCSKYState *env,
                                       GByteArray *mem_buf, int n)
{
    int ret = 0;
    switch (n) {
    case 0 ... 15:
        /* Current general registers */
        ret = gdb_get_reg32(mem_buf, env->regs[n]);
        break;
    case 20:
        /* hi register */
        ret = gdb_get_reg32(mem_buf, env->hi);
        break;
    case 21:
        /* lo register */
        ret = gdb_get_reg32(mem_buf, env->lo);
        break;
    case 24 ... 55:
        /* cp1gr0 ~ cp1gr31 */
        ret = gdb_get_reg32(mem_buf, env->cp1.fr[n - 24]);
        break;
    case 72:
        /* pc register */
        ret = gdb_get_reg32(mem_buf, env->pc);
        break;
    case 89:
        ret = gdb_get_reg32(mem_buf, env->cp0.psr | (env->psr_s << 31) |
                            env->psr_c);
        break;
#if !defined(CONFIG_USER_ONLY)
    case 73 ... 88:
        /* Banked registers */
        ret = gdb_get_reg32(mem_buf, env->banked_regs[n - 73]);
        break;
    case 90 ... 101:
        /* Cp0 CPU control registers */
        ret = gdb_get_reg32(mem_buf, ((uint32_t *)&env->cp0.vbr)[n - 90]);
        break;
    case 102:
        ret = gdb_get_reg32(mem_buf, env->cp0.cpidr_counter);
        break;
    case 103:
        ret = gdb_get_reg32(mem_buf, env->cp0.dcsr | env->dcsr_v);
        break;
    case 104 ... 108:
        ret = gdb_get_reg32(mem_buf, ((uint32_t *)&env->cp0.cpwr)[n - 104]);
        break;
    case 109:
        /* pacr */
        ret = gdb_get_reg32(mem_buf, env->cp0.pacr[env->cp0.prsr & 0x7]);
        break;
    case 110:
        /* prsr */
        ret = gdb_get_reg32(mem_buf, env->cp0.prsr);
        break;
    case 111 ... 119:
#endif
    case 121 ... 127:
        /* cp1cr0 ~ cp1cr6 */
        ret = gdb_get_reg32(mem_buf, 0);
        break;
    case 128 ... 147:
        /* cp15cr0 ~ cp15cr31 */
         ret = gdb_get_reg32(mem_buf, 0);
         break;
     default:
        ret = 0;
    }
    return ret;
}

static int cskyv2cpu_gdb_read_register(CPUCSKYState *env,
                                       GByteArray *mem_buf, int n)
{
    int ret = 0;
#if !defined(CONFIG_USER_ONLY)
    CSKYCPUClass *mcc = CSKY_CPU_GET_CLASS(env_cpu(env));
#endif
    switch (n) {
    case 0 ... 31:
        /* Current general registers */
        ret = gdb_get_reg32(mem_buf, env->regs[n]);
        break;
    case 36:
        /* hi register */
        ret = gdb_get_reg32(mem_buf, env->hi);
        break;
    case 37:
        /* lo register */
        ret = gdb_get_reg32(mem_buf, env->lo);
        break;
    case 40 ... 55:
        /* fr0 ~ fr15 */
        ret = gdb_get_reg64(mem_buf, env->vfp.reg[n - 40].fpu[0]);
        break;
    case 56 ... 71:
        /* vr0 ~ vr15 */
        gdb_get_reg32(mem_buf, env->vfp.reg[n - 56].udspi[0]);
        gdb_get_reg32(mem_buf, env->vfp.reg[n - 56].udspi[1]);
        gdb_get_reg32(mem_buf, env->vfp.reg[n - 56].udspi[2]);
        gdb_get_reg32(mem_buf, env->vfp.reg[n - 56].udspi[3]);
        ret = 16;
        break;
    case 72:
        /* pc register */
        ret = gdb_get_reg32(mem_buf, env->pc);
        break;
    case 89:
        ret = gdb_get_reg32(mem_buf, env->cp0.psr | (env->psr_s << 31) |
                      (env->psr_bm << 2) | env->psr_c);
        break;
#if !defined(CONFIG_USER_ONLY)
    case 73 ... 88:
        /* Banked registers */
        ret = gdb_get_reg32(mem_buf, env->banked_regs[n - 73]);
        break;
    case 90 ... 101:
        ret = gdb_get_reg32(mem_buf, ((uint32_t *)&env->cp0.vbr)[n - 90]);
        break;
    case 102:
        {
            uint32_t counter;

            counter = env->cp0.cpidr_counter;
            env->cp0.cpidr_counter = (counter + 1) % 4;
            ret = gdb_get_reg32(mem_buf, env->cp0.cpidr[counter]);
            break;
        }
    case  103 ... 108:
        ret = gdb_get_reg32(mem_buf, ((uint32_t *)&env->cp0.dcsr)[n - 103]);
        break;
    case 109:
        ret = gdb_get_reg32(mem_buf, env->cp0.pacr[env->cp0.prsr & 0x7]);
        break;
    case 110:
        ret = gdb_get_reg32(mem_buf, env->cp0.prsr);
        break;
    case 111 ... 117:
        ret = gdb_get_reg32(mem_buf, 0);
        break;
    case 118: /* rmr */
        ret = gdb_get_reg32(mem_buf, mcc->rmr);
        break;
    case 119:
        ret = gdb_get_reg32(mem_buf, env->cp0.mpid);
        break;
    case 120:
        ret = gdb_get_reg32(mem_buf, 0);
        break;
#endif
    case 121 ... 123:
        /* fid fcr fesr */
        ret = gdb_get_reg32(mem_buf, ((uint32_t *)&env->vfp.fid)[n - 121]);
        break;
    case 127:
        /* usp */
        ret = gdb_get_reg32(mem_buf, env->stackpoint.nt_usp);
        break;
    case 128 ... 136:
        /* cp15cr0 ~ cp15cr8 */
        ret = gdb_get_reg32(mem_buf, 0);
        break;
    default:
        ret = 0;
    }
    return ret;
}

int csky_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;

    if (env->features & CPU_ABIV1) {
        return cskyv1cpu_gdb_read_register(env, mem_buf, n);
    } else if (env->features & CPU_ABIV2) {
        return cskyv2cpu_gdb_read_register(env, mem_buf, n);
    } else {
        g_assert(0);
    }

    return 0;
}

static int cskyv1cpu_gdb_write_register(CPUCSKYState *env,
                                        uint8_t *mem_buf, int n)
{
#if !defined(CONFIG_USER_ONLY)
    CPUState *cs = env_cpu(env);
#endif
    uint32_t tmp;

    tmp = ldl_p(mem_buf);
    switch (n) {
    case 0 ... 15:
        /* Current general registers */
        env->regs[n] = tmp;
        return 4;
    case 20:
        /* hi register */
        env->hi = tmp;
        return 4;
    case 21:
        /* lo register */
        env->lo = tmp;
        return 4;
    case 24 ... 55:
        /* cp1gr0 ~ cp1gr31 */
        env->cp1.fr[n - 24] = tmp;
        break;
    case 72:
        /* pc register */
        env->pc = tmp;
        return 4;
#if !defined(CONFIG_USER_ONLY)
    case 73 ... 88:
        /* Banked registers */
        env->banked_regs[n - 73] = tmp;
        return 4;
    case 89:
        if ((env->cp0.psr & 0x2) != (tmp & 0x2)) {
            helper_switch_regs(env);
        }
        env->cp0.psr = tmp;
        env->psr_c = tmp & 0x1;
        env->psr_s = (tmp & (1 << 31)) >> 31;
        return 4;
    case 90 ... 100:
        /* Cp0 CPU control registers */
        ((uint32_t *)&env->cp0.vbr)[n - 90] = tmp;
        return 4;
    case 103 ... 106:
        ((uint32_t *)&env->cp0.dcsr)[n - 103] = tmp;
        return 4;
    case 107:
         /* cr18 */
        if ((env->cp0.ccr & 0x1) != (tmp & 0x1)) {
            /* flush global QEMU TLB and tb_jmp_cache */
            tlb_flush(cs);

            if (tmp & 0x1) { /* for mmu/mpu */
                if (env->features & CSKY_MMU) {
                    if (env->full_mmu) {
                        env->tlb_context->get_physical_address =
                            mmu_get_physical_address;
                    } else {
                        env->tlb_context->get_physical_address =
                            thin_mmu_get_physical_address;
                    }
                } else if (env->features & CSKY_MPU) {
                    env->tlb_context->get_physical_address =
                                     mpu_get_physical_address;
                }
            } else {
                env->tlb_context->get_physical_address =
                              nommu_get_physical_address;
            }
        }

        env->cp0.ccr = tmp;
        return 4;
    case 108:
         /* cr19 */
         env->cp0.capr = tmp;
         return 4;
    case 109:
        env->cp0.pacr[env->cp0.prsr & 0x7] = tmp;
        return 4;
    case 110:
        env->cp0.prsr = tmp;
        return 4;
    case 111 ... 119:
        return 4;
#endif
    case 121 ... 127:
        /* cp1cr0 ~ cp1cr6 */
        break;
    case 128 ... 147:
        /* cp15cr0 ~ cp15cr8 */
        break;
    default:
        return 0;
    }
    return 4;
}

static int cskyv2cpu_gdb_write_register(CPUCSKYState *env,
                                        uint8_t *mem_buf, int n)
{
#if !defined(CONFIG_USER_ONLY)
    CPUState *cs = env_cpu(env);
    CSKYCPUClass *mcc = CSKY_CPU_GET_CLASS(env_cpu(env));
#endif
    uint32_t tmp;

    tmp = ldl_p(mem_buf);
    switch (n) {
    case 0 ... 31:
        /* Current general registers */
        env->regs[n] = tmp;
        return 4;
    case 36:
        /* hi register */
        env->hi = tmp;
        return 4;
    case 37:
        /* lo register */
        env->lo = tmp;
        return 4;
    case 40 ... 55:
        /* fr0 ~ fr15 */
        env->vfp.reg[n - 40].fpu[0] = ldq_p(mem_buf);
        return 8;
    case 56 ... 71:
        /* vr0 ~ vr15 */
        env->vfp.reg[n - 56].udspi[0] = ldl_p(mem_buf);
        env->vfp.reg[n - 56].udspi[1] = ldl_p(mem_buf + 4);
        env->vfp.reg[n - 56].udspi[2] = ldl_p(mem_buf + 8);
        env->vfp.reg[n - 56].udspi[3] = ldl_p(mem_buf + 0xC);
        return 16;
        break;
    case 72:
        /* pc register */
        env->pc = tmp;
        return 4;
#if !defined(CONFIG_USER_ONLY)
    case 73 ... 88:
        /* Banked registers */
        env->banked_regs[n - 73] = tmp;
        return 4;
    case 89:
        if ((env->cp0.psr & 0x2) != (tmp & 0x2)) {
            helper_switch_regs(env);
        }
        if (env->features & ABIV2_JAVA) {
            env->cp0.psr = tmp;
            env->psr_s = tmp >> 31;
            env->psr_bm = (tmp >> 2) & 0x1;
            env->psr_c = tmp & 0x1;
        } else {
            env->cp0.psr = tmp & (~0x400);
            env->psr_s = tmp >> 31;
            env->psr_bm = 0;
            env->psr_c = tmp & 0x1;
        }
        return 4;
    case 90 ... 100:
        ((uint32_t *)&env->cp0.psr)[n - 89] = tmp;
        return 4;
    case 101:
         /* GSR */
    case 102:
         /* CPUIDRR */
        return 4;
    case 103 ... 106:
        ((uint32_t *)&env->cp0.dcsr)[n - 103] = tmp;
        return 4;
    case 107:
         /* CR18 */
        if ((env->cp0.ccr & 0x1) != (tmp & 0x1)) {
            /* flush global QEMU TLB and tb_jmp_cache */
            tlb_flush(cs);

            if (tmp & 0x1) {  /* enable mmu/mpu */
                if (env->features & CSKY_MMU) {
                    if (env->full_mmu) {
                        env->tlb_context->get_physical_address =
                            mmu_get_physical_address;
                    } else {
                        env->tlb_context->get_physical_address =
                            thin_mmu_get_physical_address;
                    }
                } else if (env->features & CSKY_MPU) {
                    env->tlb_context->get_physical_address =
                                    mpu_get_physical_address;
                }
            } else {
                env->tlb_context->get_physical_address =
                              nommu_get_physical_address;
            }

        }

        env->cp0.ccr = tmp;
        return 4;
    case 108:
         env->cp0.capr = tmp;
         return 4;
    case 109:
         env->cp0.pacr[env->cp0.prsr & 0x7] = tmp;
         return 4;
    case 110:
         env->cp0.prsr = tmp;
         return 4;
    case 111 ... 117:
        break;
    case 118: /* rmr */
        mcc->rmr = tmp;
        return 4;
    case 119:
        env->cp0.mpid = tmp;
        return 4;
    case 120:
        break;
#endif
    case 121 ... 123:
        /* fid fcr fesr */
        ((uint32_t *)&env->vfp.fid)[n - 121] = tmp;
        return 4;
    case 127:
        /* usp */
        env->stackpoint.nt_usp = tmp;
        return 4;
    case 128 ... 136:
        /* cp15cr0 ~ cp15cr8 */
        break;
    default:
        return 0;
    }
    return 4;
}

int csky_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;

    if (env->features & CPU_ABIV1) {
        return cskyv1cpu_gdb_write_register(env, mem_buf, n);
    } else if (env->features & CPU_ABIV2) {
        return cskyv2cpu_gdb_write_register(env, mem_buf, n);
    } else {
        g_assert(0);
    }

    return 0;
}
