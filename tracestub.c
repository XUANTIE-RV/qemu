/*
 *  CSKY trace server
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
#include "exec/tracestub.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/config-file.h"
#include "sysemu/runstate.h"
struct csky_trace_server_state traceserver;
#ifdef CONFIG_USER_ONLY
static int traceserver_fd = -1;
#endif
long long csky_trace_icount;
struct csky_trace_filter tfilter;
int waddr_num, raddr_num;

QemuOptsList qemu_csky_trace_opts = {
    .name = "csky-trace",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_csky_trace_opts.head),
    .desc = {
        {
            .name = "port",
            .type = QEMU_OPT_STRING,
            .help = "communicate port",
        },{
            .name = "tb_trace",
            .type = QEMU_OPT_BOOL,
            .help = "trace basic blocks or not",
        },{
            .name = "mem_trace",
            .type = QEMU_OPT_BOOL,
            .help = "trace ld/st or not",
        },{
            .name = "x_vf_trace",
            .type = QEMU_OPT_BOOL,
            .help = "trace experiment vector factor",
        },{
            .name = "x_lmul_trace",
            .type = QEMU_OPT_BOOL,
            .help = "trace experiment vector lmul",
        },{
            .name = "auto_trace",
            .type = QEMU_OPT_BOOL,
            .help = "auto gen trace or not",
        },{
            .name = "start",
            .type = QEMU_OPT_STRING,
            .help = "trace start addr",
        },{
            .name = "exit",
            .type = QEMU_OPT_STRING,
            .help = "trace exit addr",
        },{
            .name = "proxy_trace",
            .type = QEMU_OPT_BOOL,
            .help = "add inst addr for memory trace or not",
        },{ /* end of list */ }
    },
};

/* return the length of cpuname without the suffix "-csky-cpu" in cpu_type*/
void csky_trace_set_cpu(const char *cpu_type)
{
    char *type;
    memset(tfilter.cpu, 0, 20);
    type = strstr(cpu_type, "-riscv-cpu");
    if (type == NULL) {
        type = strstr(cpu_type, "-csky-cpu");
    }
    /* save one char space for TRACE_CPUNAME */
    if (type != NULL) {
        memcpy(tfilter.cpu + 1, cpu_type, MIN(type - cpu_type, 16));
    } else if (cpu_type != NULL) {
        strncpy(tfilter.cpu + 1, cpu_type, 16);
    }
}

void csky_trace_handle_opts(CPUState *cs, uint32_t cpuid)
{
    QemuOptsList *ret;
    QemuOpts *opts;
    bool b;

    ret = qemu_find_opts("csky-trace");
    if (ret) {
        opts = qemu_opts_find(ret, NULL);
        if (opts) {
            cs->csky_trace_features |= CSKY_TRACE;
            cs->csky_trace_features |= TB_TRACE;
            cs->csky_trace_features |= MEM_TRACE;
            cs->csky_trace_features |= X_VF_TRACE;
            cs->csky_trace_features |= X_LMUL_TRACE;

            tfilter.enable = true;
            tfilter.cpuid = cpuid;
            tfilter.event |= TRACE_EVENT_INSN;
            tfilter.event |= TRACE_EVENT_DATA;
            tfilter.event |= TRACE_EVENT_X_VF;
            tfilter.event |= TRACE_EVENT_X_LMUL;
            b = qemu_opt_get_bool(opts, "tb_trace", true);
            if (!b) {
                tfilter.event &= ~TRACE_EVENT_INSN;
            }
            b = qemu_opt_get_bool(opts, "mem_trace", true);
            if (!b) {
                tfilter.event &= ~TRACE_EVENT_DATA;
            }
            b = qemu_opt_get_bool(opts, "x_vf_trace", false);
            if (!b) {
                tfilter.event &= ~TRACE_EVENT_X_VF;
            }
            b = qemu_opt_get_bool(opts, "x_lmul_trace", false);
            if (!b) {
                tfilter.event &= ~TRACE_EVENT_X_LMUL;
            }
            b = qemu_opt_get_bool(opts, "auto_trace", true);
            if (!b) {
                tfilter.event &= ~TRACE_EVENT_DATA;
                tfilter.event &= ~TRACE_EVENT_INSN;
                tfilter.enable = false;
            }
        }
    }
}

static int test = 0;
static void trace_add_syn(void)
{
    uint16_t syn_start = SYN_START;
    uint16_t syn_end = SYN_END;
    uint16_t syn_icount = SYN_ICOUNT;

    traceserver.insn_num = csky_trace_icount - traceserver.last_icount;
    memcpy(traceserver.buf + traceserver.pos, &syn_start, sizeof(uint16_t));
    memcpy(traceserver.buf + traceserver.pos + 2, &syn_icount,
        sizeof(uint16_t));
    memcpy(traceserver.buf + traceserver.pos + 4, &(traceserver.insn_num),
        sizeof(uint32_t));
    memcpy(traceserver.buf + traceserver.pos + 8, &syn_end,
        sizeof(uint16_t));

    traceserver.pos += 5 * sizeof(uint16_t);
    test += traceserver.insn_num;
}
void trace_buf_alloc(bool add_sync)
{
    traceserver.buf = (char *)malloc(DEFAULT_BUFFER_LEN);
    memset(traceserver.buf, 0, DEFAULT_BUFFER_LEN);
    traceserver.len = DEFAULT_BUFFER_LEN;
    traceserver.pos = 0;
    if (add_sync) {
        trace_add_syn();
    }
}

void trace_buf_clear(void)
{
    traceserver.pos = 0;
    trace_add_syn();
}
#ifdef CONFIG_USER_ONLY

static void send_packet(void)
{
    int ret;
    uint32_t last = traceserver.pos;
    uint32_t start = 0;
    while (last > 0) {
        ret = send(traceserver.fd, (const uint8_t *)traceserver.buf + start,
                last, 0);
        if (ret < 0) {
            if (errno != EINTR) {
                return;
            }
        } else {
            start += ret;
            last -= ret;
        }
    }
}
#endif


void trace_termsig_handler(void)
{
    if (traceserver.initok) {
        trace_add_syn();
#ifdef CONFIG_USER_ONLY
        send_packet();
        close(traceserver.fd);
#else
        qemu_chr_fe_write_all(&traceserver.chr,
            (const uint8_t *)traceserver.buf, traceserver.pos);
        qemu_chr_fe_disconnect(&traceserver.chr);
#endif
    }
    trace_buf_clear();
}

/* Trace_send is triggered by the target insn counter.
 * It can only be called after trace device enabled
 */
void trace_send(void)
{
    if (traceserver.initok) {
        if (traceserver.buf == NULL) {
            trace_buf_alloc(true);
        }
#ifdef CONFIG_USER_ONLY
        send_packet();
#else
        qemu_chr_fe_write_all(&traceserver.chr,
            (const uint8_t *)traceserver.buf, traceserver.pos);
#endif
    }
    trace_buf_clear();
    traceserver.last_icount += traceserver.insn_num;
}

void trace_send_immediately(void)
{
    if ((traceserver.buf != NULL) && (traceserver.initok != false)) {
        if (traceserver.pos > 5 * sizeof(uint16_t)) {
            traceserver.last_icount += traceserver.insn_num;
            trace_add_syn();
        }
#ifdef CONFIG_USER_ONLY
        send_packet();
#else
        qemu_chr_fe_write_all(&traceserver.chr,
            (const uint8_t *)traceserver.buf, traceserver.pos);
#endif
        trace_buf_clear();
    }
}

void write_trace_before(uint32_t packlen, bool header)
{

    if (traceserver.buf == 0) {
        trace_buf_alloc(!header);
    }
    if ((traceserver.pos + packlen) > traceserver.len) {
        traceserver.buf = (char *)realloc(traceserver.buf,
            traceserver.len + 2 * 1024);
        traceserver.len += 2 * 1024;
    }
}

void write_trace_header(uint32_t config)
{
    uint32_t version = ((CURRENT_TRACE_VERSION) << 8) | TRACE_VERSION;
    uint32_t traceid = ((QEMU_TRACE_ID) << 8) | TRACE_ID;
    uint32_t cpuid[6] = {0};
    char *cpu = tfilter.cpu;
    uint32_t packlen = 0;
    uint16_t config_type = TRACE_CONFIG;
    static bool header = true; /* a header or just trace config */
    cpuid[0] = TRACE_CPUID;
    cpuid[1] = tfilter.cpuid;
    cpu[0] = CPU_NAME;

    if (header) {
        packlen = 9 * sizeof(uint32_t) + 1 * sizeof(uint16_t) + 20 * sizeof(char);
        write_trace_before(packlen, true);
        /* add version */
        memcpy(traceserver.buf + traceserver.pos, &version, sizeof(uint32_t));
        traceserver.pos += sizeof(uint32_t);
        /* add traceid */
        memcpy(traceserver.buf + traceserver.pos, &traceid, sizeof(uint32_t));
        traceserver.pos += sizeof(uint32_t);
        /* add cpuid */
        memcpy(traceserver.buf + traceserver.pos, cpuid, sizeof(uint32_t) * 6);
        traceserver.pos += sizeof(uint32_t) * 6;
        /* add cpu */
        memcpy(traceserver.buf + traceserver.pos, cpu, sizeof(char) * 20);
        traceserver.pos += sizeof(char) * 20;
        /* add trace control registers */
        memcpy(traceserver.buf + traceserver.pos, &config_type,
            sizeof(uint16_t));
        traceserver.pos += sizeof(uint16_t);
        memcpy(traceserver.buf + traceserver.pos, &config, sizeof(uint32_t));
        traceserver.pos += sizeof(uint32_t);
        trace_send_immediately();
        header = false;
    } else {
        write_trace_8_8(TRACE_CONFIG, 6, 0, config);
    }
}
//#define CSKY_TRACE_COMPRESS
/* compress and send the coming element   */
static void csky_trace_compress(uint32_t packetlen, char *start)
{
#ifdef CSKY_TRACE_COMPRESS
    uint64_t compress_element;
    struct csky_trace_compress *pcompress = &traceserver.compress;
    if (pcompress->lastpacket == NULL) {
        pcompress->lastpacket = (char *)malloc(packetlen);
        memcpy(pcompress->lastpacket, start, packetlen);
        pcompress->len = packetlen;
        pcompress->count = 1;
    } else {
        if (packetlen == pcompress->len && memcmp(pcompress->lastpacket, start,
                packetlen) == 0) {
            pcompress->count++;
        } else {
            memcpy(traceserver.buf + traceserver.pos, pcompress->lastpacket,
                pcompress->len);
            traceserver.pos += pcompress->len / sizeof(uint8_t);
            if (pcompress->count > 1) {
                write_trace_before(sizeof(uint64_t), false);
                compress_element = ((uint64_t)pcompress->count << 32)
                    | ((1 << 24) | TRACE_COMPRESS);
                memcpy(traceserver.buf + traceserver.pos, &compress_element,
                    sizeof(uint64_t));
                traceserver.pos += sizeof(uint64_t);
            }
            pcompress->lastpacket = (char *)realloc(pcompress->lastpacket,
                packetlen);
            memcpy(pcompress->lastpacket, start, packetlen);
            pcompress->len = packetlen;
            pcompress->count = 1;
        }
    }
#else
    memcpy(traceserver.buf + traceserver.pos, start, packetlen);
    traceserver.pos += packetlen / sizeof(uint8_t);
#endif
}

void write_trace_8(uint8_t type, uint32_t  packlen, uint32_t value)
{
    value =  (value << 8) | type;

    write_trace_before(packlen, false);

    assert((traceserver.pos + packlen) <= traceserver.len);
    csky_trace_compress(packlen, (char *)&value);
}

void write_trace_8_8(uint8_t type, uint32_t packlen, uint8_t value1
    , uint32_t value2)
{
    uint64_t value = type;
    value = ((uint64_t)value1 << 8) | value;
    value = ((uint64_t)value2 << 16 * sizeof(uint8_t)) | value;

    write_trace_before(packlen, false);
    assert((traceserver.pos + packlen) <= traceserver.len);
    csky_trace_compress(packlen, (char *)&value);
}

void write_trace_8_24(uint8_t type, uint32_t packlen, uint32_t value1,
    uint32_t value2)
{
    uint64_t value = type;
    value = ((uint64_t)value1 << 8) | value;
    value = ((uint64_t)value2 << 32 * sizeof(uint8_t)) | value;

    write_trace_before(packlen, false);
    assert((traceserver.pos + packlen) <= traceserver.len);
    csky_trace_compress(packlen, (char *)&value);
}
/*
void write_trace_8_seq(uint8_t type, uint32_t packlen, uint8_t *value)
{
    write_trace_before(packlen, false);
    assert((traceserver.pos + packlen) <= traceserver.len);
    memcpy(traceserver.buf + traceserver.pos, &type, 1);
    memcpy(traceserver.buf + traceserver.pos + 1 , value, packlen - 1);
    traceserver.pos += packlen / sizeof(uint8_t);
}*/
#ifdef CONFIG_USER_ONLY

static int traceserver_open(int port)
{
    struct sockaddr_in sockaddr;
    int fd, ret;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
#ifndef _WIN32
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif

    socket_set_fast_reuse(fd);

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = 0;
    ret = bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (ret < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    ret = listen(fd, 1);
    if (ret < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;

}

static void traceserver_accept(void)
{
    struct sockaddr_in sockaddr;
    socklen_t len;
    int fd;

    for (;;) {
        len = sizeof(sockaddr);
        fd = accept(traceserver_fd, (struct sockaddr *)&sockaddr, &len);
        if (fd < 0 && errno != EINTR) {
            perror("accept");
            return;
        } else if (fd >= 0) {
#ifndef _WIN32
            fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
            break;
        }
    }

    /* set short latency */
    socket_set_nodelay(fd);

    traceserver.fd = fd;
    traceserver.initok = true;
}

int traceserver_start(int port, int debug_mode)
{
    traceserver_fd = traceserver_open(port);
    if (traceserver_fd < 0) {
        return -1;
    }
    traceserver_accept();
    if (debug_mode) {
        tfilter.event |= TRACE_EVENT_GDB;
    }
    if (tfilter.enable) {
        write_trace_header(tfilter.event);
    }
    traceserver.initok = true;
    return 0;
}

void trace_exit_notify(void)
{
    if ((traceserver.buf != NULL) && (traceserver.initok != false)) {
        if (traceserver.pos > 5 * sizeof(uint16_t)) {
            traceserver.last_icount += traceserver.insn_num;
            trace_add_syn();
        }
        send_packet();
        close(traceserver.fd);
        trace_buf_clear();
        if (traceserver.buf != NULL) {
            free(traceserver.buf);
            traceserver.buf = NULL;
        }
        traceserver.initok = false;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "WADDR_NUM: %d\n", waddr_num);
    qemu_log_mask(LOG_GUEST_ERROR, "RADDR_NUM: %d\n", raddr_num);
}

#else
static void trace_chr_event(void *opaque, QEMUChrEvent event)
{
    switch (event) {
    case CHR_EVENT_OPENED:
        traceserver.initok = true;
        if (is_gdbserver_start) {
            tfilter.event |= TRACE_EVENT_GDB;
        }
        if (tfilter.enable) {
            write_trace_header(tfilter.event);
        }
        if (!runstate_needs_reset() && !is_gdbserver_start) {
            vm_start();
        }
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_CLOSED:
        traceserver.initok = false;
        break;

    default:
        break;
    }
}

void trace_exit_notify(void)
{
     if ((traceserver.buf != NULL) && (traceserver.initok != false)) {
        if (traceserver.pos > 5 * sizeof(uint16_t)) {
            traceserver.last_icount += traceserver.insn_num;
            trace_add_syn();
        }
        traceserver.initok = false;
        qemu_chr_fe_write_all(&traceserver.chr,
                (const uint8_t *)traceserver.buf, traceserver.pos);
        qemu_chr_fe_disconnect(&traceserver.chr);
        trace_buf_clear();
        if (traceserver.buf != NULL) {
            free(traceserver.buf);
            traceserver.buf = NULL;
        }
    }
}

int traceserver_start(const char *device)
{
    char tracestub_device_name[128];
    Chardev *chr = NULL;

    if (!device) {
        return -1;
    }
    if (strcmp(device, "none") != 0) {
        /* enforce required TCP attributes */
        snprintf(tracestub_device_name, sizeof(tracestub_device_name),
                 "tcp::%s,nowait,nodelay,server", device);
        device = tracestub_device_name;
        chr = qemu_chr_new_noreplay("trace", device, true, NULL);
        if (!chr) {
            return -1;
        }
    } else {
        return -1;
    }
    if (chr) {
        qemu_chr_fe_init(&traceserver.chr, chr, &error_abort);
        qemu_chr_fe_set_handlers(&traceserver.chr, NULL, NULL,
                                 trace_chr_event, NULL, NULL, NULL, true);
    }
    atexit(trace_exit_notify);
    return 0;
}


#endif
