/*
 *  Emulation of Linux signals
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "qemu.h"
#include "user-internals.h"
#include "signal-common.h"
#include "linux-user/trace.h"

#if defined(TARGET_CSKYV1)
#define CSKY_NGREG 19
#elif defined(TARGET_CSKYV2)
#define CSKY_NGREG 35
#endif

#ifdef CONFIG_CSKY_KERNEL_4X
struct target_sigcontext {
    abi_ulong  sc_mask;     /* old sigmask */
    abi_ulong  sc_usp;      /* old user stack pointer */
    abi_ulong  sc_a0;
    abi_ulong  sc_a1;
    abi_ulong  sc_a2;
    abi_ulong  sc_a3;
    abi_ulong  sc_regs[10];
    abi_ulong  sc_r15;
#ifdef TARGET_CSKYV2
    abi_ulong  sc_exregs[16];
    abi_ulong  sc_rhi;
    abi_ulong  sc_rlo;
#endif
    abi_ulong  sc_sr;       /* psr */
    abi_ulong  sc_pc;

    abi_ulong  sc_fcr;
    abi_ulong  sc_fsr;
    abi_ulong  sc_fesr;
    abi_ulong  sc_feinst1;
    abi_ulong  sc_feinst2;
    abi_ulong  sc_fpregs[32];

};

struct target_ucontext {
    unsigned long           tuc_flags;
    struct target_ucontext  *tuc_link;
    target_stack_t          tuc_stack;
    struct sigcontext        tuc_mcontext;
    target_sigset_t         tuc_sigmask;    /* mask last for extensibility */
};

struct rt_sigframe {
    abi_long sig;
    abi_ulong pinfo;
    abi_ulong puc;
    struct target_siginfo info;
    struct target_ucontext uc;
    uint16_t retcode[4];                /* include <stdint> ?? */
};
#else
typedef struct fpregset {
    int f_fsr;
    int f_fesr;
    int f_feinst1;
    int f_feinst2;
    int f_fpregs[32];
} csky_fpregset_t;


struct ckcontext {
    int version;
    greg_t gregs[CSKY_NGREG];
    csky_fpregset_t fpregs;
};

#define MCONTEXT_VERSION 2

struct target_ucontext {
    unsigned long           tuc_flags;
    struct target_ucontext  *tuc_link;
    target_stack_t          tuc_stack;
    struct ckcontext        tuc_mcontext;
    target_sigset_t         tuc_sigmask;    /* mask last for extensibility */
};


struct target_sigcontext {
    abi_ulong  sc_mask;     /* old sigmask */
    abi_ulong  sc_usp;      /* old user stack pointer */
    abi_ulong  sc_r1;
    abi_ulong  sc_r2;
    abi_ulong  sc_r3;
    abi_ulong  sc_r4;
    abi_ulong  sc_r5;
    abi_ulong  sc_r6;
    abi_ulong  sc_r7;
    abi_ulong  sc_r8;
    abi_ulong  sc_r9;
    abi_ulong  sc_r10;
    abi_ulong  sc_r11;
    abi_ulong  sc_r12;
    abi_ulong  sc_r13;
    abi_ulong  sc_r14;
    abi_ulong  sc_r15;
#ifdef TARGET_CSKYV2
    abi_ulong  sc_r16;
    abi_ulong  sc_r17;
    abi_ulong  sc_r18;
    abi_ulong  sc_r19;
    abi_ulong  sc_r20;
    abi_ulong  sc_r21;
    abi_ulong  sc_r22;
    abi_ulong  sc_r23;
    abi_ulong  sc_r24;
    abi_ulong  sc_r25;
    abi_ulong  sc_r26;
    abi_ulong  sc_r27;
    abi_ulong  sc_r28;
    abi_ulong  sc_r29;
    abi_ulong  sc_r30;
    abi_ulong  sc_r31;
    abi_ulong  sc_r32;
#endif
    abi_ulong  sc_sr;       /* psr */
    abi_ulong  sc_pc;

    abi_ulong  sc_fsr;
    abi_ulong  sc_fesr;
    abi_ulong  sc_feinst1;
    abi_ulong  sc_feinst2;
    abi_ulong  sc_fpregs[32];

};

struct sigframe {
    abi_long sig;
    struct target_sigcontext sc;
    uint16_t retcode[4];                /* include <stdint.h> ?? */
    abi_ulong extramask[TARGET_NSIG_WORDS - 1];
};

struct rt_sigframe {
    abi_long sig;
    abi_ulong pinfo;
    abi_ulong puc;
    struct target_siginfo info;
    struct target_ucontext uc;
    uint16_t retcode[4];                /* include <stdint> ?? */
};
#endif

static void
rt_save_fpu_state(struct target_ucontext *uc,
                  CPUCSKYState *env)
{
#ifdef CONFIG_CPU_HAS_FPU
#error "not implemented yet"
#endif
}

static void
rt_restore_fpu_state(struct target_ucontext *uc,
                     CPUCSKYState *env)
{
#ifdef CONFIG_CPU_HAS_FPU
#error "not implemented yet"
#endif
}

static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUCSKYState *env, size_t frame_size)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;
#ifdef TARGET_CSKYV1
    unsigned long sp = env->regs[0];
#elif defined TARGET_CSKYV2
    unsigned long sp = env->regs[14];
#endif
    /* This is the X/Open sanctioned signal stack switching.  */
    if ((ka->sa_flags & TARGET_SA_ONSTACK) && !sas_ss_flags(sp)) {
        sp = ts->sigaltstack_used.ss_sp + ts->sigaltstack_used.ss_size;
    }
    /* force 8-byte align */
    return (sp - frame_size) & ~7;
}

#ifdef CONFIG_CSKY_KERNEL_4X
static void
setup_return(CPUCSKYState *env, struct target_sigaction *ka,
             uint16_t rc[], int usig, abi_ulong rc_addr, int real_time)
{
    /* abi_ulong handler = ka->_sa_handler; */
    abi_ulong retcode;
    /* int err = 0; */

    if (ka->sa_flags & TARGET_SA_RESTORER) {
        retcode = ka->sa_restorer;
    } else {
#if defined(TARGET_CSKYV1)
        /* movi r1, 127; addi r1, 32; addi r1, (_NR_rt_sigreturn - 127 - 32); trap0 */
        __put_user(0x6000 + (127 << 4) + 1, rc);
        __put_user(0x2000 + (31 << 4) + 1, rc + 1);
        __put_user(0x2000 + ((TARGET_NR_rt_sigreturn - 127 - 32 - 1) << 4) + 1, rc + 2);
        __put_user(0x0008, rc + 3);
#elif defined(TARGET_CSKYV2)
        __put_user(0xea07, rc);/* movi r7, xxx */
        __put_user(TARGET_NR_rt_sigreturn, rc + 1);
        __put_user(0xC000, rc + 2);/* trap32 0 */
        __put_user(0x2020, rc + 3);
#endif
        retcode = rc_addr;
    }

    env->pc = ka->_sa_handler;
#if defined(TARGET_CSKYV1)
    env->regs[2] = usig;
#elif defined(TARGET_CSKYV2)
    env->regs[0] = usig;
#endif
    env->regs[15] = retcode;
}

static int rt_setup_ucontext(struct target_ucontext *uc, CPUCSKYState *env)
{
    struct target_sigcontext *sc =
        (struct target_sigcontext *)&uc->tuc_mcontext;
    int i = 0;
    __put_user(env->pc, &sc->sc_pc);
    __put_user(env->regs[14], &sc->sc_usp);
    __put_user(env->regs[0], &sc->sc_a0);
    __put_user(env->regs[1], &sc->sc_a1);
    __put_user(env->regs[2], &sc->sc_a2);
    __put_user(env->regs[3], &sc->sc_a3);
    for (i = 0; i < 10; i++) {
        __put_user(env->regs[i + 4], &sc->sc_regs[i]);
    }
    __put_user(env->regs[15], &sc->sc_r15);
#ifdef TARGET_CSKYV2
    for (i = 0; i < 16; i++) {
        __put_user(env->regs[i + 16], &sc->sc_exregs[i]);
    }
    __put_user(env->hi, &sc->sc_rhi);
    __put_user(env->lo, &sc->sc_rlo);
#endif
    __put_user(env->cp0.psr | env->psr_c, &sc->sc_sr);
    rt_save_fpu_state(uc, env);

    return 0;
}

static void rt_restore_ucontext(struct target_ucontext *uc, CPUCSKYState *env)
{
    struct target_sigcontext *sc =
        (struct target_sigcontext *)&uc->tuc_mcontext;
    int i = 0;
    __get_user(env->pc, &sc->sc_pc);
    __get_user(env->regs[14], &sc->sc_usp);
    __get_user(env->regs[0], &sc->sc_a0);
    __get_user(env->regs[1], &sc->sc_a1);
    __get_user(env->regs[2], &sc->sc_a2);
    __get_user(env->regs[3], &sc->sc_a3);
    for (i = 0; i < 10; i++) {
        __get_user(env->regs[i + 4], &sc->sc_regs[i]);
    }
    __get_user(env->regs[15], &sc->sc_r15);
#ifdef TARGET_CSKYV2
    for (i = 0; i < 16; i++) {
        __get_user(env->regs[i + 16], &sc->sc_exregs[i]);
    }
    __get_user(env->hi, &sc->sc_rhi);
    __get_user(env->lo, &sc->sc_rlo);
#endif
    __get_user(env->cp0.psr, &sc->sc_sr);
    __get_user(env->psr_c, &sc->sc_sr);
    env->cp0.psr &= 0xfffe;
    env->psr_c &= 0x0001;
    rt_restore_fpu_state(uc, env);
}

#else

static void
save_fpu_state(struct target_sigcontext *sc,
               CPUCSKYState *env)
{
#ifdef CONFIG_CPU_HAS_FPU
#error "not implemented yet"
#endif
}

static void
restore_fpu_state(struct target_sigcontext *sc,
                  CPUCSKYState *env)
{
#ifdef CONFIG_CPU_HAS_FPU
#error "not implemented yet"
#endif
}

static void
setup_sigcontext(struct target_sigcontext *sc,
                 CPUCSKYState *env, abi_ulong mask)
{
    __put_user(env->regs[0], &sc->sc_usp);
    __put_user(env->regs[1], &sc->sc_r1);
    __put_user(env->regs[2], &sc->sc_r2);
    __put_user(env->regs[3], &sc->sc_r3);
    __put_user(env->regs[4], &sc->sc_r4);
    __put_user(env->regs[5], &sc->sc_r5);
    __put_user(env->regs[6], &sc->sc_r6);
    __put_user(env->regs[7], &sc->sc_r7);
    __put_user(env->regs[8], &sc->sc_r8);
    __put_user(env->regs[9], &sc->sc_r9);
    __put_user(env->regs[10], &sc->sc_r10);
    __put_user(env->regs[11], &sc->sc_r11);
    __put_user(env->regs[12], &sc->sc_r12);
    __put_user(env->regs[13], &sc->sc_r13);
    __put_user(env->regs[14], &sc->sc_r14);
    __put_user(env->regs[15], &sc->sc_r15);
#ifdef TARGET_CSKYV2
    __put_user(env->regs[16], &sc->sc_r16);
    __put_user(env->regs[17], &sc->sc_r17);
    __put_user(env->regs[18], &sc->sc_r18);
    __put_user(env->regs[19], &sc->sc_r19);
    __put_user(env->regs[20], &sc->sc_r20);
    __put_user(env->regs[21], &sc->sc_r21);
    __put_user(env->regs[22], &sc->sc_r22);
    __put_user(env->regs[23], &sc->sc_r23);
    __put_user(env->regs[24], &sc->sc_r24);
    __put_user(env->regs[25], &sc->sc_r25);
    __put_user(env->regs[26], &sc->sc_r26);
    __put_user(env->regs[27], &sc->sc_r27);
    __put_user(env->regs[28], &sc->sc_r28);
    __put_user(env->regs[29], &sc->sc_r29);
    __put_user(env->regs[30], &sc->sc_r30);
    __put_user(env->regs[31], &sc->sc_r31);
#endif

    /*  FIXME shangyh */
    /* __put_user(mask, &sc->sc_mask); */
    __put_user(env->pc, &sc->sc_pc);  /* pc??? */
    /*  FIXME shangyh */
    __put_user((env->cp0.psr | env->psr_c), &sc->sc_sr);   /* what's sr??? */

    save_fpu_state(sc, env);
}

static void
restore_sigcontext(CPUCSKYState *env, struct target_sigcontext *sc)
{
    __get_user(env->regs[0], &sc->sc_usp);
    __get_user(env->regs[1], &sc->sc_r1);
    __get_user(env->regs[2], &sc->sc_r2);
    __get_user(env->regs[3], &sc->sc_r3);
    __get_user(env->regs[4], &sc->sc_r4);
    __get_user(env->regs[5], &sc->sc_r5);
    __get_user(env->regs[6], &sc->sc_r6);
    __get_user(env->regs[7], &sc->sc_r7);
    __get_user(env->regs[8], &sc->sc_r8);
    __get_user(env->regs[9], &sc->sc_r9);
    __get_user(env->regs[10], &sc->sc_r10);
    __get_user(env->regs[11], &sc->sc_r11);
    __get_user(env->regs[12], &sc->sc_r12);
    __get_user(env->regs[13], &sc->sc_r13);
    __get_user(env->regs[14], &sc->sc_r14);
    __get_user(env->regs[15], &sc->sc_r15);
#ifdef TARGET_CSKYV2
    __get_user(env->regs[16], &sc->sc_r16);
    __get_user(env->regs[17], &sc->sc_r17);
    __get_user(env->regs[18], &sc->sc_r18);
    __get_user(env->regs[19], &sc->sc_r19);
    __get_user(env->regs[20], &sc->sc_r20);
    __get_user(env->regs[21], &sc->sc_r21);
    __get_user(env->regs[22], &sc->sc_r22);
    __get_user(env->regs[23], &sc->sc_r23);
    __get_user(env->regs[24], &sc->sc_r24);
    __get_user(env->regs[25], &sc->sc_r25);
    __get_user(env->regs[26], &sc->sc_r26);
    __get_user(env->regs[27], &sc->sc_r27);
    __get_user(env->regs[28], &sc->sc_r28);
    __get_user(env->regs[29], &sc->sc_r29);
    __get_user(env->regs[30], &sc->sc_r30);
    __get_user(env->regs[31], &sc->sc_r31);
#endif

    /* FIXME  add by shangyh */
    /* __get_user(mask, &sc->sc_mask); */
    __get_user(env->pc, &sc->sc_pc);  /* pc??? */
    __get_user(env->cp0.psr, &sc->sc_sr);   /* what's sr??? */
    __get_user(env->psr_c, &sc->sc_sr);
    env->cp0.psr &= 0xfffe;
    env->psr_c &= 0x0001;

    restore_fpu_state(sc, env);
}

static void
rt_setup_ucontext(struct target_ucontext *uc, CPUCSKYState *env)
{
    greg_t *gregs = uc->tuc_mcontext.gregs;
    __put_user(MCONTEXT_VERSION, &uc->tuc_mcontext.version);
    __put_user(env->pc, &gregs[0]);
    __put_user(env->regs[1], &gregs[1]);
    __put_user(-1, &gregs[2]);
    __put_user(env->cp0.psr | env->psr_c, &gregs[3]);
    __put_user(env->regs[2], &gregs[4]);
    __put_user(env->regs[3], &gregs[5]);
    __put_user(env->regs[4], &gregs[6]);
    __put_user(env->regs[5], &gregs[7]);
    __put_user(env->regs[6], &gregs[8]);
    __put_user(env->regs[7], &gregs[9]);
    __put_user(env->regs[8], &gregs[10]);
    __put_user(env->regs[9], &gregs[11]);
    __put_user(env->regs[10], &gregs[12]);
    __put_user(env->regs[11], &gregs[13]);
    __put_user(env->regs[12], &gregs[14]);
    __put_user(env->regs[13], &gregs[15]);
    __put_user(env->regs[14], &gregs[16]);
    __put_user(env->regs[15], &gregs[17]);
#ifdef TARGET_CSKYV2
    __put_user(env->regs[16], &gregs[18]);
    __put_user(env->regs[17], &gregs[19]);
    __put_user(env->regs[18], &gregs[20]);
    __put_user(env->regs[19], &gregs[21]);
    __put_user(env->regs[20], &gregs[22]);
    __put_user(env->regs[21], &gregs[23]);
    __put_user(env->regs[22], &gregs[24]);
    __put_user(env->regs[23], &gregs[25]);
    __put_user(env->regs[24], &gregs[26]);
    __put_user(env->regs[25], &gregs[27]);
    __put_user(env->regs[26], &gregs[28]);
    __put_user(env->regs[27], &gregs[29]);
    __put_user(env->regs[28], &gregs[30]);
    __put_user(env->regs[29], &gregs[31]);
    __put_user(env->regs[30], &gregs[32]);
    __put_user(env->regs[31], &gregs[33]);
#endif
    __put_user(env->regs[0], &gregs[34]);

    rt_save_fpu_state(uc, env);
}

static void
rt_restore_ucontext(struct target_ucontext *uc, CPUCSKYState *env)
{
    greg_t *gregs = uc->tuc_mcontext.gregs;
    __get_user(env->pc, &gregs[0]);
    __get_user(env->regs[1], &gregs[1]);
    __get_user(env->cp0.psr , &gregs[3]);
    __get_user(env->psr_c, &gregs[3]);
    env->cp0.psr &= 0xfffe;
    env->psr_c &= 0x0001;

     __get_user(env->regs[2], &gregs[4]);
    __get_user(env->regs[3], &gregs[5]);
    __get_user(env->regs[4], &gregs[6]);
    __get_user(env->regs[5], &gregs[7]);
    __get_user(env->regs[6], &gregs[8]);
    __get_user(env->regs[7], &gregs[9]);
    __get_user(env->regs[8], &gregs[10]);
    __get_user(env->regs[9], &gregs[11]);
    __get_user(env->regs[10], &gregs[12]);
    __get_user(env->regs[11], &gregs[13]);
    __get_user(env->regs[12], &gregs[14]);
    __get_user(env->regs[13], &gregs[15]);
    __get_user(env->regs[14], &gregs[16]);
    __get_user(env->regs[15], &gregs[17]);
#ifdef TARGET_CSKYV2
    __get_user(env->regs[16], &gregs[18]);
    __get_user(env->regs[17], &gregs[19]);
    __get_user(env->regs[18], &gregs[20]);
    __get_user(env->regs[19], &gregs[21]);
    __get_user(env->regs[20], &gregs[22]);
    __get_user(env->regs[21], &gregs[23]);
    __get_user(env->regs[22], &gregs[24]);
    __get_user(env->regs[23], &gregs[25]);
    __get_user(env->regs[24], &gregs[26]);
    __get_user(env->regs[25], &gregs[27]);
    __get_user(env->regs[26], &gregs[28]);
    __get_user(env->regs[27], &gregs[29]);
    __get_user(env->regs[28], &gregs[30]);
    __get_user(env->regs[29], &gregs[31]);
    __get_user(env->regs[30], &gregs[32]);
    __get_user(env->regs[31], &gregs[33]);
#endif
    __get_user(env->regs[0], &gregs[34]);

    rt_restore_fpu_state(uc, env);
}
static void
setup_return(CPUCSKYState *env, struct target_sigaction *ka,
             uint16_t rc[], int usig, abi_ulong rc_addr, int real_time)
{
    /* abi_ulong handler = ka->_sa_handler; */
    abi_ulong retcode;
    /* int err = 0; */

    if (ka->sa_flags & TARGET_SA_RESTORER) {
        retcode = ka->sa_restorer;
    } else {
#if defined(TARGET_CSKYV1)
        if (real_time) {
            __put_user(0x6000 + (TARGET_NR_rt_sigreturn << 4) + 1, rc);
        } else {
            __put_user(0x6000 + (TARGET_NR_sigreturn << 4) + 1, rc);
        }
        __put_user(0x08, rc + 1);
#elif defined(TARGET_CSKYV2)
        __put_user(0xea07, rc);/* movih r1, xxx */
#else
        if (real_time) {
            __put_user(TARGET_NR_rt_sigreturn, rc + 1);
        } else {
            __put_user(TARGET_NR_sigreturn, rc + 1);
        }
        __put_user(0xC000, rc + 2);/* trap32 0 */
        __put_user(0x2020, rc + 3);
#endif
        retcode = rc_addr;
    }

    env->pc = ka->_sa_handler;
#if defined(TARGET_CSKYV1)
    env->regs[2] = usig;
#elif defined(TARGET_CSKYV2)
    env->regs[0] = usig;
#endif
    env->regs[15] = retcode;
}

/* compare linux/arch/csky/kernel/signal.c:setup_frame() */
void setup_frame(int usig, struct target_sigaction *ka,
                        target_sigset_t *set, CPUCSKYState *env)
{
    struct sigframe *frame;
    abi_ulong frame_addr = get_sigframe(ka, env, sizeof(*frame));
    int i;

    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    setup_sigcontext(&frame->sc, env, set->sig[0]);

    for (i = 1; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->extramask[i - 1]);
    }

    setup_return(env, ka, frame->retcode, usig,
                 frame_addr + offsetof(struct sigframe, retcode), 0);

#if defined(TARGET_CSKYV1)
    env->regs[0] = frame_addr;
#elif defined(TARGET_CSKYV2)
    env->regs[14] = frame_addr;
#endif
    return;

give_sigsegv:
    force_sigsegv(usig);
}

long do_sigreturn(CPUCSKYState *env)
{
    abi_ulong frame_addr;
    struct sigframe *frame;
    target_sigset_t set;
    sigset_t host_set;
    int i;
#ifdef TARGET_CSKYV1
    if (env->regs[0] & 7) {
        goto badframe;
    }

    frame_addr = env->regs[0];
#elif defined TARGET_CSKYV2
    if (env->regs[14] & 7) {
        goto badframe;
    }

    frame_addr = env->regs[14];
#endif

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    __get_user(set.sig[0], &frame->sc.sc_mask);

    for (i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(set.sig[i], &frame->extramask[i - 1]);
    }

    target_to_host_sigset_internal(&host_set, &set);
    set_sigmask(&host_set);

    restore_sigcontext(env, &frame->sc);

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV /* , current */);
    return 0; /* unreachable */
}
#endif

void setup_rt_frame(int usig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUCSKYState *env)
{
    struct rt_sigframe *frame;
    abi_ulong frame_addr = get_sigframe(ka, env, sizeof(*frame));
    int i;
    abi_ulong info_addr, uc_addr;
    TaskState *ts = (TaskState *)thread_cpu->opaque;

    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    /* save pinfo */
    info_addr = frame_addr + offsetof(struct rt_sigframe, info);
    __put_user(info_addr, &frame->pinfo);

    /* save puc  */
    uc_addr = frame_addr + offsetof(struct rt_sigframe, uc);
    __put_user(uc_addr, &frame->puc);

    /* save info */
    frame->info = *info;

    /* Now, we begin to create the ucontext */

    /* Clear all the bits of the ucontext we don't use.  */
    memset(&frame->uc, 0, offsetof(struct target_ucontext, tuc_mcontext));

    /*
     * __put_user(0, &(frame->uc).tuc_flags);
     * __put_user(0x0, &(frame->uc).tuc_link);
     */

    /* save information of the stack */
    __put_user(ts->sigaltstack_used.ss_sp, &frame->uc.tuc_stack.ss_sp);
    __put_user(ts->sigaltstack_used.ss_size, &frame->uc.tuc_stack.ss_size);
    __put_user(sas_ss_flags(get_sp_from_cpustate(env)),
               &frame->uc.tuc_stack.ss_flags);


    /* save ucontext */
    rt_setup_ucontext(&frame->uc, env);

    /* save sigmask */
    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    setup_return(env, ka, frame->retcode, usig,
                 frame_addr + offsetof(struct rt_sigframe, retcode), 1);
#if defined(TARGET_CSKYV1)
    env->regs[3] = info_addr;
    env->regs[4] = uc_addr;
    env->regs[0] = frame_addr;
#elif defined(TARGET_CSKYV2)
    env->regs[1] = info_addr;
    env->regs[2] = uc_addr;
    env->regs[14] = frame_addr;
#endif
    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    force_sigsegv(usig);
}

long do_rt_sigreturn(CPUCSKYState *env)
{
    abi_ulong frame_addr;
    struct rt_sigframe *frame;
    sigset_t host_set;
#if defined(TARGET_CSKYV1)
    if (env->regs[0] & 7) {
        goto badframe;
    }

    frame_addr = env->regs[0];
#elif defined(TARGET_CSKYV2)
    if (env->regs[14] & 7) {
        goto badframe;
    }

    frame_addr = env->regs[14];
#endif
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    target_to_host_sigset(&host_set, &frame->uc.tuc_sigmask);
    set_sigmask(&host_set);

    rt_restore_ucontext(&frame->uc, env);
    target_restore_altstack(&frame->uc.tuc_stack, env);

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}


