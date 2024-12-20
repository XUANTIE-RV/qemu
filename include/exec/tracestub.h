#ifndef TRACE_STUB_H
#define TRACE_STUB_H

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/option.h"
#include "qemu/timer.h"
#ifdef CONFIG_USER_ONLY
#include "qemu/sockets.h"
#else
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#endif

#define DEFAULT_TRACESTUB_PORT  8810

#define TRACE_HEADER_LENGTH     4096
#define CURRENT_TRACE_VERSION   1
#define QEMU_TRACE_ID           0xf8

#define DEFAULT_BUFFER_LEN      (16 * 1024)
#define DEFAULT_PACKET_NUM      4096
#define DEFAULT_DATA_MAXLEN     4
#define TRACE_HEADER_LENGTH     4096
#define INSN_PER_PACKET         (20 * 512)

#define TRACE_VERSION           0x1
#define TRACE_END               0x3
#define INST_BASE               0x4
#define INST_OFFSET             0x5
#define INST_ASID               0x10
#define INST_EXCP               0x11
#define INST_TEE                0x12
#define INST_EXIT               0x14
#define TRACE_CPUID             0x30
#define TRACE_ID                0x31
#define TRACE_CONFIG            0x32
#define CPU_NAME                0x33
#define INST_SEG                0x34
#define TRACE_EXCP_PUSH         0x35
#define TRACE_EXCP_POP          0x36
#define DATA_INST_OFFSET        0x37
#define INST_VECTOR_FACTOR      0x38
#define INST_VECTOR_LMUL        0x39
#define DATA_RADDR              0x40
#define DATA_WADDR              0x41
#define DATA_VALUE              0x42
#define DEVICE_EVENT            0x49
#define DATA_SRADDR             0x53
#define DATA_SWADDR             0x54
#define DATA_SEG                0x60
#define TRACE_COMPRESS          0x82
#define SYN_START               ((0x00 << 8) | 0x02)
#define SYN_END                 ((0x07 << 8) | 0x02)
#define SYN_ICOUNT              ((0x01 << 8) | 0x02)

/* device types */
#define DEVICE_MCA 1

/* device event types */
#define MCA_RFFT      1
#define MCA_CFFT      2
#define MCA_IRFFT     3
#define MCA_ICFFT     4
#define MCA_PSD       5
#define MCA_MAC       6
#define MCA_VEC_MUL   7
#define MCA_VEC_ADD   8
#define MCA_SOFTMAX   9
#define MCA_ASRC      10
#define MCA_FIR       11
#define MCA_IIR       12

//#define ADDR_CMPR
//#define DATA_CMPR

/* trace module define */

#define STSP_RANGE_MATCH        (0x1 << 0)
#define ADDR_RANGE_MATCH        (0x1 << 1)
#define DATA_RANGE_MATCH        (0x1 << 2)

#define TRACE_VALUE_SINGLE      1
#define TRACE_VALUE_RANGE       2

#define TRACE_ASID_ENABLE_MASK  1

#define TRACE_EVENT_INSN        (0x1 << 0)
#define TRACE_EVENT_DATA        (0x1 << 1)
#define TRACE_EVENT_GDB         (0x1 << 22)
#define TRACE_EVENT_X_VF        (0x1 << 23)
#define TRACE_EVENT_X_LMUL      (0x1 << 24)

#define MAX_ADDR_CMPR_NUM       2
#define MAX_DATA_CMPR_NUM       2

#define ADDR_CMPR_MODE_START    12

#define ADDR_CMPR_MODE_MASK     (0xf << ADDR_CMPR_MODE_START)
#define ADDR_CMPR_ENABLE_MASK   0x1
#define ADDR_CMPR_DATA_MASK     (0xf << 4)
#define DATA_CMPR_MODE_MASK     (0xf << ADDR_CMPR_MODE_START)

#define INSN_ADDR_RANGE_CMPR    0
#define DATA_ADDR_RANGE_CMPR    1
#define ASSOCIATE_VALUE_CMPR    2
#define STSP_RANGE_CMPR         3

/* trace type define */

#define CSKY_TRACE              (0x1 << 0)
#define TB_TRACE                (0x1 << 1)
#define MEM_TRACE               (0x1 << 2)
#define X_VF_TRACE              (0x1 << 3)
#define X_LMUL_TRACE            (0x1 << 4)


enum mem_ldst_type {
    LD8U,
    LD16U,
    LD32U,
    LD64U,
    LD8S,
    LD16S,
    LD32S,
    LD64S,
    ST8,
    ST16,
    ST32,
    ST64
};

struct csky_trace_compress {
    char *lastpacket;
    uint32_t len;
    uint32_t count;
};

struct csky_trace_server_state {
#ifdef CONFIG_USER_ONLY
    int fd;
#else
    CharBackend chr;
    Notifier exit;
#endif
    bool initok;
    uint32_t count;
    uint32_t total;
    uint32_t pos;
    char *buf;
    uint32_t len;
    uint64_t insn_num;
    uint64_t last_icount;
    struct csky_trace_compress compress;
};

extern struct csky_trace_server_state traceserver;
struct trace_range {
    uint64_t start;
    uint64_t end;
};

struct trace_data {
    struct trace_range data_addr;
    uint32_t mode;
    uint32_t min;
    uint32_t max;
};

enum stsp_status {
    STSP_START = 1,
    STSP_EXIT = 2
};

struct csky_trace_filter {
    bool enable;
    bool proxy;
    uint32_t event;
    uint32_t asid;
    struct trace_range stsp_range[MAX_ADDR_CMPR_NUM];
    enum stsp_status sstsp;
    uint32_t stsp_num;
    struct trace_range addr_range[MAX_ADDR_CMPR_NUM];
    uint32_t addr_num;
    struct trace_data data_range[MAX_DATA_CMPR_NUM];
    uint32_t data_num;
    uint32_t cpuid;
    char cpu[20];
};


extern struct csky_trace_filter tfilter;
extern long long csky_trace_icount;
extern int waddr_num;
extern int raddr_num;
extern QemuOptsList qemu_csky_trace_opts;
#ifdef CONFIG_USER_ONLY
int traceserver_start(int port, int debug_mode);
#else
int traceserver_start(const char *device);
#endif
void trace_exit_notify(void);
bool gen_mem_trace(void);
bool gen_tb_trace(void);
bool gen_x_vf_trace(void);
bool gen_x_lmul_trace(void);
void trace_buf_clear(void);
void trace_buf_alloc(bool sync);
void write_trace_header(uint32_t config);
void write_trace_before(uint32_t packlen, bool sync);
void write_trace_8(uint8_t type, uint32_t packlen, uint32_t value);
void write_trace_8_8(uint8_t type, uint32_t packlen, uint8_t value,
                     uint32_t value2);
void write_trace_8_24(uint8_t type, uint32_t packlen, uint32_t value,
                     uint32_t value2);
void trace_termsig_handler(void);
void trace_send(void);
void trace_send_immediately(void);
bool trace_range_test(void *cpu, uint32_t pc, uint32_t smask);
void csky_trace_handle_opts(CPUState *cs, uint32_t cpuid);
void csky_trace_set_cpu(const char *cpu_type);

void extern_helper_trace_tb_exit(uint32_t subtype, uint32_t offset);
#endif
