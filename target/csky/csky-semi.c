/*
 * C-SKY semihosting operations
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
#include "gdbstub/syscalls.h"
#include "semihosting/semihost.h"
#include "semihosting/syscalls.h"
#include "semihosting/softmmu-uaccess.h"
#include "exec/gdbstub.h"
#include "cpu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"

#define CSKY_SEMIHOST_EXIT            0x00
#define CSKY_SEMIHOST_INIT            0x01
#define CSKY_SEMIHOST_OPEN            0x02
#define CSKY_SEMIHOST_CLOSE           0x03
#define CSKY_SEMIHOST_READ            0x04
#define CSKY_SEMIHOST_WRITE           0x05
#define CSKY_SEMIHOST_SEEK            0x06
#define CSKY_SEMIHOST_RENAME          0x07
#define CSKY_SEMIHOST_UNLINK          0x08
#define CSKY_SEMIHOST_STAT            0x09
#define CSKY_SEMIHOST_FSTAT           0x0a
#define CSKY_SEMIHOST_TIME            0x0b
#define CSKY_SEMIHOST_ISATTY          0x0c
#define CSKY_SEMIHOST_SYSTEM          0x0d

static target_ulong sarg0, sarg1, sarg2;



typedef uint32_t gdb_mode_t;
typedef uint32_t gdb_time_t;

struct csky_gdb_stat {
  uint32_t    gdb_st_dev;     /* device */
  uint32_t    gdb_st_ino;     /* inode */
  gdb_mode_t  gdb_st_mode;    /* protection */
  uint32_t    gdb_st_nlink;   /* number of hard links */
  uint32_t    gdb_st_uid;     /* user ID of owner */
  uint32_t    gdb_st_gid;     /* group ID of owner */
  uint32_t    gdb_st_rdev;    /* device type (if inode device) */
  uint64_t    gdb_st_size;    /* total size, in bytes */
  uint64_t    gdb_st_blksize; /* blocksize for filesystem I/O */
  uint64_t    gdb_st_blocks;  /* number of blocks allocated */
  gdb_time_t  gdb_st_atime;   /* time of last access */
  gdb_time_t  gdb_st_mtime;   /* time of last modification */
  gdb_time_t  gdb_st_ctime;   /* time of last change */
} QEMU_PACKED;

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int translate_openflags(int flags)
{
    int hf;

    if (flags & GDB_O_WRONLY) {
        hf = O_WRONLY;
    } else if (flags & GDB_O_RDWR) {
        hf = O_RDWR;
    } else {
        hf = O_RDONLY;
    }

    if (flags & GDB_O_APPEND) {
        hf |= O_APPEND;
    }
    if (flags & GDB_O_CREAT) {
        hf |= O_CREAT;
    }
    if (flags & GDB_O_TRUNC) {
        hf |= O_TRUNC;
    }
    if (flags & GDB_O_EXCL) {
        hf |= O_EXCL;
    }
    hf |= O_BINARY;
    return hf;
}

static void translate_stat(CPUCSKYState *env, target_ulong addr, struct stat *s)
{
    struct csky_gdb_stat *p;
    p = lock_user(VERIFY_WRITE, addr, sizeof(struct csky_gdb_stat), 0);
    if (!p)
        /* FIXME - should this return an error code? */
        return;
    p->gdb_st_dev = cpu_to_be32(s->st_dev);
    p->gdb_st_ino = cpu_to_be32(s->st_ino);
    p->gdb_st_mode = cpu_to_be32(s->st_mode);
    p->gdb_st_nlink = cpu_to_be32(s->st_nlink);
    p->gdb_st_uid = cpu_to_be32(s->st_uid);
    p->gdb_st_gid = cpu_to_be32(s->st_gid);
    p->gdb_st_rdev = cpu_to_be32(s->st_rdev);
    p->gdb_st_size = cpu_to_be64(s->st_size);
#ifdef _WIN32
    /* Windows stat is missing some fields.  */
    p->gdb_st_blksize = 0;
    p->gdb_st_blocks = 0;
#else
    p->gdb_st_blksize = cpu_to_be64(s->st_blksize);
    p->gdb_st_blocks = cpu_to_be64(s->st_blocks);
#endif
    p->gdb_st_atime = cpu_to_be32(s->st_atime);
    p->gdb_st_mtime = cpu_to_be32(s->st_mtime);
    p->gdb_st_ctime = cpu_to_be32(s->st_ctime);
    unlock_user(p, addr, sizeof(struct csky_gdb_stat));
}


static void csky_semi_return_u32(CPUCSKYState *env, uint32_t ret, uint32_t err)
{
    target_ulong reg1 = env->regs[1];
    env->regs[0] = ret;
    if (put_user_u32(err, reg1)) {
        qemu_log_mask(LOG_GUEST_ERROR, "csky-semihosting: return value "
                      "discarded because argument block not writable\n");
    }
}
static void csky_semi_cb(CPUState *cs, uint64_t ret, int err)
{
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;

    target_ulong reg1 = env->regs[1];

    if (ret == (uint64_t)-1) {
        put_user_u32(err, reg1);
    }
    env->regs[0] = ret;
}

static target_ulong csky_gdb_syscall(CSKYCPU *cpu, gdb_syscall_complete_cb cb,
                                    const char *fmt, ...)
{
    CPUCSKYState *env = &cpu->env;

    gdb_do_syscall(cb, fmt);

    return env->regs[0];
}

#define GET_ARG(n) do {                                 \
    if (get_user_u32(arg ## n, args + (n) * 4)) {       \
        return -1;                                      \
    }                                                   \
} while (0)

#define SET_ARG(n, val)                                 \
     put_user_u32(val, args + (n) * 4)

target_ulong csky_do_semihosting(CPUCSKYState *env)
{
    CSKYCPU *cpu = env_archcpu(env);
    CPUState *cs = CPU(cpu);
    target_ulong args;
    target_ulong arg0, arg1, arg2, arg3;
    char *s;
    int nr, fd;
    uint32_t ret;
    uint32_t len;

    nr = env->regs[0];
    args = env->regs[1];

    switch (nr) {
    case CSKY_SEMIHOST_EXIT:
        gdb_exit(env->regs[1]);
        exit(env->regs[1]);
        break;
    case CSKY_SEMIHOST_OPEN:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        s = lock_user_string(arg0);
        if (!s) {
            /* FIXME - should this error code be -TARGET_EFAULT ? */
            return (uint32_t)-1;
        }
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "open,%s,%x,1a4", arg0,
                                  (int)arg1 + 1, arg2);
            return ret;
        } else {
            ret = open(s, translate_openflags(arg2), 0644);
        }
        unlock_user(s, arg0, 0);
        break;
    case CSKY_SEMIHOST_CLOSE:
        /* fixme: stdin/out/err */
        GET_ARG(0);
        fd = arg0;
        if (arg0 > 2) {
            if (use_gdb_syscalls()) {
                ret = csky_gdb_syscall(cpu, csky_semi_cb, "close,%x", arg0);
                return ret;
            } else {
                ret = close(fd);
            }
        } else {
            ret = 0;
        }
        break;
    case CSKY_SEMIHOST_WRITE:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "write,%x,%x,%x",
                                   arg0, arg1, len);
            return ret;
        } else {
            if (len == 0) {
                ret = 0;
                break;
            }
            s = lock_user(VERIFY_READ, arg1, len, 1);
            if (!s) {
                return (uint32_t)-1;
            }
            ret = write(arg0, s, len);
            unlock_user(s, arg1, 0);
        }
        break;
    case CSKY_SEMIHOST_READ:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "read,%x,%x,%x",
                                   arg0, arg1, len);
            return ret;
        } else {
            s = lock_user(VERIFY_WRITE, arg1, len, 0);
            if (!s) {
                return (uint32_t)-1;
            }
            do {/*fixme: return if ret == -a asap */
                ret = read(arg0, s, len);
            } while (ret == -1 && errno == EINTR);
            if (ret == -1) {
                unlock_user(s, arg1, 0);
            } else {
                unlock_user(s, arg1, ret);
            }
        }
        break;
    case CSKY_SEMIHOST_SEEK:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "lseek,%x,%lx,%x",
                                   arg0, (int64_t)((int32_t)arg1), arg2);
            return ret;
        } else {
            ret = lseek(arg0, (int32_t)arg1, arg2);
        }
        break;
    case CSKY_SEMIHOST_RENAME:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "rename,%s,%s",
                                   arg0, (int)arg1 + 1, arg2, (int)arg3 + 1);
            return ret;
        } else {
            char *s2;
            s = lock_user_string(arg0);
            s2 = lock_user_string(arg2);
            if (!s || !s2) {
                ret = (uint32_t)-1;
            } else {
                ret = rename(s, s2);
            }
            if (s2) {
                unlock_user(s2, arg2, 0);
            }
            if (s) {
                unlock_user(s, arg0, 0);
            }
        }
        break;
    case CSKY_SEMIHOST_UNLINK:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "unlink,%s",
                                  arg0, (int)arg1 + 1);
            return ret;
        } else {
            s = lock_user_string(arg0);
            if (!s) {
                return (uint32_t)-1;
            }
            ret =  remove(s);
            unlock_user(s, arg0, 0);
        }
        break;
    case CSKY_SEMIHOST_STAT:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        sarg2 = arg2;
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "stat,%s,%x",
                           arg0, (int)arg1 + 1, arg2);
            return ret;
        } else {
            struct stat st;
            s = lock_user_string(arg0);
            if (!s) {
                /* FIXME - check error code? */
                ret = -1;
            } else {
                ret = stat(s, &st);
                unlock_user(s, arg0, 0);
            }
            if (ret == 0) {
                translate_stat(env, arg2, &st);
            }
        }
        break;
    case CSKY_SEMIHOST_FSTAT:
        GET_ARG(0);
        GET_ARG(1);
        sarg1 = arg1;
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "fstat,%x,%x",
                           arg0, arg1);
            return ret;
        } else {
            struct stat st;
            ret = fstat(arg0, &st);
            if (ret == 0) {
                translate_stat(env, arg1, &st);
            }
        }
        break;
    case CSKY_SEMIHOST_TIME:
        GET_ARG(0);
        GET_ARG(1);
        sarg0 = arg0;
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "gettimeofday,%x,%x",
                           arg0, arg1);
            return ret;
        } else {
            qemu_timeval tv;
            int64_t rt = g_get_real_time();
            struct gdb_timeval *p;
            ret = qemu_gettimeofday(&tv);
            if (ret == 0) {
                p = lock_user(VERIFY_WRITE, arg0,
                              sizeof(struct gdb_timeval), 0);
                if (!p) {
                    ret = -1;
                } else {
                    p->tv_sec = cpu_to_be32(tv.tv_sec);
                    p->tv_usec = cpu_to_be64(tv.tv_usec);
                    unlock_user(p, arg0, sizeof(struct gdb_timeval));
                }
            }
        }
        break;
    case CSKY_SEMIHOST_ISATTY:
        GET_ARG(0);
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "isatty,%x", arg0);
            return ret;
        } else {
            ret = isatty(arg0);
        }
        break;
    case CSKY_SEMIHOST_SYSTEM:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            ret = csky_gdb_syscall(cpu, csky_semi_cb, "system,%s",
                                   arg0, (int)arg1 + 1);
            return ret;
        } else {
            s = lock_user_string(arg0);
            if (!s) {
                ret = -1;
            } else {
                ret = system(s);
                unlock_user(s, arg0, 0);
            }

        }
        break;
    default:
        fprintf(stderr, "qemu: Unsupported SemiHosting 0x%02x\n", nr);
        cpu_dump_state(cs, stderr, 0);
        abort();
    }
    csky_semi_return_u32(env, ret, errno);
    return ret;
}
