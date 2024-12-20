#ifndef PCTRACE_H
#define PCTRACE_H

#define TB_TRACE_NUM 4096
struct csky_trace_info {
    uint64_t tb_pc;
    bool notjmp;
};

#endif
