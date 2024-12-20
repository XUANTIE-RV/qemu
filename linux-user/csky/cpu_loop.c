/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *  Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu.h"
#include "user-internals.h"
#include "cpu_loop-common.h"
#include "signal-common.h"
#include "elf.h"

void cpu_loop(CPUCSKYState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    int *host_ptr;
    target_siginfo_t info;
    abi_ulong pc;
    target_ulong ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
            break;
        case EXCP_CSKY_TRAP0:
            /*FIXME implement tls*/
#if defined(TARGET_CSKYV1)
            env->pc += 2;
            /*CLONE_SETTLS=0x8000 */
#if defined(CONFIG_CSKY_KERNEL_4X)
           if ((env->regs[1] == 220) && (env->regs[2] & 0x8000)) {
                cpu_set_tls(env, env->regs[6]);
            }
            if (env->regs[1] == 244) {
                cpu_set_tls(env, env->regs[2]);
                break;
            }
#else
            if ((env->regs[1] == 120) && (env->regs[2] & 0x8000)) {
                cpu_set_tls(env, env->regs[6]);
            }
            if (env->regs[1] == 218) {
                cpu_set_tls(env, env->regs[2]);
                break;
            }
#endif
            ret = do_syscall(env, env->regs[1],
                             env->regs[2], env->regs[3], env->regs[4],
                             env->regs[5], env->regs[6], env->regs[7],
                             0, 0);
            if (ret == -QEMU_ERESTARTSYS) {
                env->pc -= 2;
            } else if (ret != -QEMU_ESIGRETURN) {
                env->regs[2] = ret;
            }
#else
            env->pc += 4;
            /*two ways to set tls*/
#if defined(CONFIG_CSKY_KERNEL_4X)
            if ((env->regs[7] == 220) && (env->regs[0] & 0x8000)) {
                cpu_set_tls(env, env->regs[4]);
            }
            if (env->regs[7] == 244) {
                cpu_set_tls(env, env->regs[0]);
                break;
            }
#else
            if ((env->regs[7] == 120) && (env->regs[0] & 0x8000)) {
                cpu_set_tls(env, env->regs[4]);
            }
            if (env->regs[7] == 218) {
                cpu_set_tls(env, env->regs[0]);
                break;
            }
#endif
            ret = do_syscall(env, env->regs[7],
                             env->regs[0], env->regs[1], env->regs[2],
                             env->regs[3], env->regs[4], env->regs[5],
                             0, 0);
            if (ret == -QEMU_ERESTARTSYS) {
                env->pc -= 4;
            } else if (ret != -QEMU_ESIGRETURN) {
                env->regs[0] = ret;
            }
#endif
            break;
        case EXCP_CSKY_TRAP2:
#if defined(TARGET_CSKYV1)
            env->pc += 2;
            host_ptr = g2h(cs, env->regs[4]);
            if (tswap32(env->regs[2]) != *host_ptr)
                env->regs[2] = 1;
            else{
                *host_ptr = tswap32(env->regs[3]);
                env->regs[2] = 0;
            }
#elif defined(TARGET_CSKYV2)
            env->pc += 4;
            host_ptr = g2h(cs, env->regs[2]);
            if (tswap32(env->regs[0]) != *host_ptr)
                env->regs[0] = 1;
            else {
                *host_ptr = tswap32(env->regs[1]);
                env->regs[0] = 0;
            }
#endif
            break;
        case EXCP_CSKY_TRAP3:
#if defined(TARGET_CSKYV1)
            env->pc += 2;
            env->regs[2] = env->tls_value;
            break;
#elif defined(TARGET_CSKYV2)
            fprintf(stderr, "Dont need to implemente trap3 in cskyv2\n");
            break;
#endif
        case EXCP_CSKY_DIV:
            {
                info.si_signo = SIGFPE;
                info.si_errno = 0;
                info.si_code = TARGET_FPE_INTDIV;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP_CSKY_IN_RESET:
            break;
        case EXCP_CSKY_EXIT:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "qemu: pc equals exit address 0x%08x, force exit\n",
                          env->exit_addr);
            exit(0);
        case EXCP_CSKY_DATA_ABORT:      /* fall through */
        case EXCP_CSKY_UDEF:            /* fall through */
        case EXCP_CSKY_PRIVILEGE:       /* fall through */
        case EXCP_CSKY_BKPT:            /* fall through */
        default:
            pc = env->pc;
            fprintf(stderr,
                    "qemu: 0x%08x: unhandled CPU exception 0x%x - aborting\n",
                    pc, trapnr);
            /* TODO: call cpu_dump_state */
            abort();
        }
        process_pending_signals(env);
    }
}


void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    env->regs[0] = regs->r0;
    env->regs[1] = regs->r1;
    env->regs[2] = regs->r2;
    env->regs[3] = regs->r3;
    env->regs[4] = regs->r4;
    env->regs[5] = regs->r5;
    env->regs[6] = regs->r6;
    env->regs[7] = regs->r7;
    env->regs[8] = regs->r8;
    env->regs[9] = regs->r9;
    env->regs[10] = regs->r10;
    env->regs[11] = regs->r11;
    env->regs[12] = regs->r12;
    env->regs[13] = regs->r13;
    env->regs[14] = regs->r14;
    env->regs[15] = regs->r15;
#if defined(TARGET_CSKYV2)
    env->regs[16] = regs->r16;
    env->regs[17] = regs->r17;
    env->regs[18] = regs->r18;
    env->regs[19] = regs->r19;
    env->regs[20] = regs->r20;
    env->regs[21] = regs->r21;
    env->regs[22] = regs->r22;
    env->regs[23] = regs->r23;
    env->regs[24] = regs->r24;
    env->regs[25] = regs->r25;
    env->regs[26] = regs->r26;
    env->regs[27] = regs->r27;
    env->regs[28] = regs->r28;
    env->regs[29] = regs->r29;
    env->regs[30] = regs->r30;
    env->regs[31] = regs->r31;
#endif
    env->pc = regs->pc;
    env->cp0.psr = regs->sr;
    env->psr_c = 0;
    env->psr_s = 0;
    env->dcsr_v = 0;
}
