/*
 * CSKY trace
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
#include "exec/tracestub.h"
#include "exec/helper-proto.h"

static uint32_t csky_trace_insn_seg;
static uint32_t csky_trace_data_seg;
#ifdef TARGET_RISCV
bool csky_trace_elf_start;
#endif
inline bool gen_mem_trace(void)
{
    if (tfilter.enable) {
#ifdef TARGET_RISCV
        if (!csky_trace_elf_start) {
            return false;
        }
#endif

        if (tfilter.event & TRACE_EVENT_DATA) {
            return true;
        }
    }
    return false;
}

inline bool gen_tb_trace(void)
{
    if (tfilter.enable) {
        if (tfilter.event & TRACE_EVENT_INSN) {
            return true;
        }
    }
    return false;
}

inline bool gen_x_vf_trace(void)
{
    if (tfilter.enable) {
        if (tfilter.event & TRACE_EVENT_X_VF) {
            return true;
        }
    }
    return false;
}

inline bool gen_x_lmul_trace(void)
{
    if (tfilter.enable) {
        if (tfilter.event & TRACE_EVENT_X_LMUL) {
            return true;
        }
    }
    return false;
}

static inline uint32_t csky_trace_get_addr_base(target_ulong addr)
{
    if (sizeof(target_ulong) == 8) {
        return (uint64_t)addr >> 32;
    }
    return 0;
}

static inline uint32_t csky_trace_get_addr_offset(target_ulong addr)
{
    if (sizeof(target_ulong) == 8) {
        return extract64(addr, 0, 32);
    }
    return addr;
}


static inline void csky_trace_send_base(uint32_t *base, uint8_t type,
                                        target_ulong addr)
{
    int packlen = 0;
    uint32_t addr_base;
    packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
    addr_base = csky_trace_get_addr_base(addr);
    if (addr_base != *base) {
        *base = addr_base;
        write_trace_8_8(type, packlen, sizeof(uint8_t), addr_base);
    }
    return;
}

static bool stsp_range_match(target_ulong pc, target_ulong smask)
{
    struct trace_range *tr = NULL;
    int i;
    if (tfilter.stsp_num == 0) {
        if (smask == 0) {
            return true;
        } else {
            return false;
        }
    }
    for (i = 0; i < tfilter.stsp_num; i++) {
        tr = &tfilter.stsp_range[i];
        if (smask != 0) {
            if ((tr->start & smask ) == pc) {
                tr->start = pc;
                return true;
            } else if ((tr->end & smask) ==pc) {
                tr->end = pc;
                return true;
            } else {
                return false;
            }
        }
        if (tfilter.sstsp == STSP_EXIT) {
            if (tr->start == pc) {
                tfilter.sstsp = STSP_START;
                return true;
            }
            return false;
        } else if (tfilter.sstsp == STSP_START) {
            if (tr->end == pc) {
                tfilter.sstsp = STSP_EXIT;
                return false;
            }
            return true;
        } else {
            return false;
        }
    }
    return false;
}

static bool addr_range_match(target_ulong pc, target_ulong smask)
{
    struct trace_range *tr = NULL;
    int i;
    if (tfilter.addr_num == 0) {
        if (smask == 0) {
            return true;
        } else {
            return false;
        }
    }
    for (i = 0; i < tfilter.addr_num; i++) {
        tr = &tfilter.addr_range[i];
        if (smask != 0) {
            if ((tr->start & smask ) == pc) {
                tr->start = pc;
                return true;
            } else if ((tr->end & smask) == pc) {
                tr->end = pc;
                return true;
            } else {
                return false;
            }
        }
        if ((tr->start <= pc) && (tr->end > pc)) {
            return true;
        }
    }
    return false;
}

static bool data_range_match(target_ulong pc, target_ulong value)
{
    if (tfilter.data_num == 0) {
        return true;
    }
#ifdef TARGET_CSKY
    for (int i = 0; i < tfilter.data_num; i++) {
        struct trace_data *td = &tfilter.data_range[i];
        struct trace_range *tr = &td->data_addr;
        if ((tr->start <= pc) && (tr->end > pc)) {
            if (td->mode == TRACE_VALUE_SINGLE) {
                if (td->min == value) {
                    return true;
                }
            } else if (td->mode == TRACE_VALUE_RANGE) {
                if ((td->min <= value) && (td->max > value)) {
                    return true;
                }
            }
        }
    }
#endif
    return false;
}

bool trace_range_test(void *cpu, uint32_t pc, uint32_t smask)
{
    bool result = false;
    result = stsp_range_match(pc, smask);
    result |= addr_range_match(pc, smask);
    return result;
}

static int addr_trace_filter(CPUArchState *env, target_ulong pc)
{
    int result = 0;
#ifdef TARGET_CSKY
    if (tfilter.enable) {
        if (tfilter.asid & TRACE_ASID_ENABLE_MASK) {
            if (ENV_GET_MMU(env)) {
                if (tfilter.asid != ENV_GET_ASID(env)) {
                    return result;
                }
            }
        }
        if (stsp_range_match(pc, 0)) {
            result |= STSP_RANGE_MATCH;
            if (addr_range_match(pc, 0)) {
                result |= ADDR_RANGE_MATCH;
            }
        }
    }
#elif TARGET_RISCV
    if (tfilter.enable) {
        if (stsp_range_match(pc, 0)) {
            result |= STSP_RANGE_MATCH;
        }
        result |= ADDR_RANGE_MATCH;
    }
#endif
    return result;
}

static int data_trace_filter(CPUArchState *env,
    target_ulong pc, target_ulong addr, target_ulong value)
{
    int result = 0;
    if (tfilter.enable) {
#ifdef TARGET_CSKY
        if (tfilter.asid & TRACE_ASID_ENABLE_MASK) {
            if (ENV_GET_MMU(env)) {
                if (tfilter.asid != ENV_GET_ASID(env)) {
                    return result;
                }
            }
        }
#endif
        if (stsp_range_match(pc, 0)) {
            result |= STSP_RANGE_MATCH;
            if (addr_range_match(addr, 0)) {
                result |= ADDR_RANGE_MATCH;
            }
            if (data_range_match(addr, value)) {
                    result |= DATA_RANGE_MATCH;
            }
        }
    }
    return result;
}
//#define CSKY_TRACE_DEBUG
#ifdef CSKY_TRACE_DEBUG
static int sync_trace_count;
#endif
void helper_csky_trace_icount(CPUArchState *env, target_ulong tb_pc, uint32_t icount)
{
    static long long total_csky_trace_icount;
    static long long last_csky_trace_icount;
    total_csky_trace_icount += icount;
    if ((total_csky_trace_icount - last_csky_trace_icount) > INSN_PER_PACKET) {
#ifdef CSKY_TRACE_DEBUG
        if (sync_trace_count % 100 == 0) {
            fprintf(stderr, ".");
            fflush(stderr);
        }
        sync_trace_count++;
#endif
        trace_send();
        last_csky_trace_icount = total_csky_trace_icount;
    }
    if (tfilter.enable) {
        if (tfilter.event & TRACE_EVENT_INSN) {
            int result = addr_trace_filter(env, tb_pc);
            if (result & STSP_RANGE_MATCH) {
                csky_trace_icount += icount;
            }
        }
    }
}

void helper_trace_tb_start(CPUArchState *env, target_ulong tb_pc)
{
    static int8_t lastbase;
    int8_t base = (tb_pc >> 24) & 0xff;
    int32_t offset = tb_pc & 0xffffff;
    int result = addr_trace_filter(env, tb_pc);
#ifdef TARGET_RISCV
    if (!csky_trace_elf_start) {
        if (tb_pc == env->elf_start) {
            csky_trace_elf_start = true;
        }
    }
    if (!csky_trace_elf_start) {
        return;
    }
#endif
    if (result & STSP_RANGE_MATCH) {
        csky_trace_send_base(&csky_trace_insn_seg, INST_SEG, tb_pc);
        env->last_pc = tb_pc;
        if (base != lastbase) {
            write_trace_8(INST_BASE, 2 * sizeof(uint8_t), base);
            lastbase = base;
        }
        write_trace_8(INST_OFFSET, sizeof(uint32_t), offset);
        if (result & ADDR_RANGE_MATCH) {
            //write_trace_8(ADDR_CMPR_MATCH, sizeof(uint32_t), offset);
        }
    }
}

void helper_trace_tb_exit(uint32_t subtype, uint32_t offset)
{
#ifdef TARGET_RISCV
    if (!csky_trace_elf_start) {
        return;
    }
#endif
    write_trace_8_8(INST_EXIT, sizeof(uint32_t), subtype, offset);
    if (is_gdbserver_start) {
        trace_send_immediately();
    }
}

void extern_helper_trace_tb_exit(uint32_t subtype, uint32_t offset)
{
    helper_trace_tb_exit(subtype, offset);
}

static void helper_trace_ldst(CPUArchState *env, target_ulong pc,
    target_ulong rz, target_ulong addr, int type)
{
    int packlen = 0;
    int result = data_trace_filter(env, pc, addr, rz);

    packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
    if (result & STSP_RANGE_MATCH) {
        if (tfilter.proxy) {
            write_trace_8(DATA_INST_OFFSET, sizeof(uint32_t), pc - env->last_pc);
        }
        csky_trace_send_base(&csky_trace_data_seg, DATA_SEG, addr);
        addr = csky_trace_get_addr_offset(addr);
        switch (type) {
        case LD8U: case LD8S:
            write_trace_8_8(DATA_RADDR, packlen, sizeof(uint8_t), addr);
            write_trace_8_8(DATA_VALUE, packlen, 0, (uint32_t)rz);
            raddr_num++;
            break;
        case LD16U: case LD16S:
            write_trace_8_8(DATA_RADDR, packlen, sizeof(uint16_t), addr);
            write_trace_8_8(DATA_VALUE, packlen, 0, (uint32_t)rz);
            raddr_num++;
            break;
        case LD32U: case LD32S:
            write_trace_8_8(DATA_RADDR, packlen, sizeof(uint32_t), addr);
            write_trace_8_8(DATA_VALUE, packlen, 0, (uint32_t)rz);
            raddr_num++;
            break;
        case LD64U: case LD64S:
            write_trace_8_8(DATA_RADDR, packlen, sizeof(uint64_t), addr);
            //write_trace_8_seq(DATA_VALUE, sizeof(uint64_t) + 1, (uint8_t *)&rz);
            write_trace_8_8(DATA_VALUE, packlen, 0, extract64(rz, 0, 32));
            write_trace_8_8(DATA_VALUE, packlen, 32, extract64(rz, 32, 32));
            raddr_num++;
            break;
        case ST8:
            write_trace_8_8(DATA_WADDR, packlen, sizeof(uint8_t), addr);
            write_trace_8_8(DATA_VALUE, packlen, 0, (uint32_t)rz);
            waddr_num++;
            break;
        case ST16:
            write_trace_8_8(DATA_WADDR, packlen, sizeof(uint16_t), addr);
            write_trace_8_8(DATA_VALUE, packlen, 0, (uint32_t)rz);
            waddr_num++;
            break;
        case ST32:
            write_trace_8_8(DATA_WADDR, packlen, sizeof(uint32_t), addr);
            write_trace_8_8(DATA_VALUE, packlen, 0, (uint32_t)rz);
            waddr_num++;
            break;
        case ST64:
            write_trace_8_8(DATA_WADDR, packlen, sizeof(uint64_t), addr);
            //write_trace_8_seq(DATA_VALUE, sizeof(uint64_t) + 1, (uint8_t*)&rz);
            write_trace_8_8(DATA_VALUE, packlen, 0, extract64(rz, 0, 32));
            write_trace_8_8(DATA_VALUE, packlen, 32, extract64(rz, 32, 32));
            waddr_num++;
            break;
        default:
            break;
        }
        if (result & ADDR_RANGE_MATCH) {
        }
        if (result & DATA_RANGE_MATCH) {
        }
    }
}

void helper_trace_ld8u(CPUArchState *env, target_ulong pc,
                       target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, LD8U);
}

void helper_trace_ld16u(CPUArchState *env, target_ulong pc,
                        target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, LD16U);
}

void helper_trace_ld32u(CPUArchState *env, target_ulong pc,
                        target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, LD32U);
}

void helper_trace_ld64u(CPUArchState *env, target_ulong pc,
                        target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, LD64U);
}

void helper_trace_ld8s(CPUArchState *env, target_ulong pc,
                       target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, LD8S);
}

void helper_trace_ld16s(CPUArchState *env, target_ulong pc,
                        target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, LD16S);
}

void helper_trace_ld32s(CPUArchState *env, target_ulong pc,
                        target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, LD32S);
}

void helper_trace_ld64s(CPUArchState *env, target_ulong pc,
                        target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, LD64S);
}

void helper_trace_st8(CPUArchState *env, target_ulong pc,
                      target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, ST8);
}

void helper_trace_st16(CPUArchState *env, target_ulong pc,
                       target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, ST16);
}

void helper_trace_st32(CPUArchState *env, target_ulong pc,
                       target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, ST32);
}

void helper_trace_st64(CPUArchState *env, target_ulong pc,
                       target_ulong rz, target_ulong addr)
{
    helper_trace_ldst(env, pc, rz, addr, ST64);
}

void helper_trace_m_addr(CPUArchState *env, target_ulong pc,
                         target_ulong addr, target_ulong num, uint32_t type)
{
    int packlen = 0;
    int result = addr_trace_filter(env, pc);

    packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
    if (result & STSP_RANGE_MATCH) {
        csky_trace_send_base(&csky_trace_data_seg, DATA_SEG, addr);
        addr = csky_trace_get_addr_offset(addr);
        write_trace_8_8(type, packlen, num * sizeof(uint32_t), addr);
        if (type == 0x41) {
            waddr_num++;
        } else if (type == 0x40) {
            raddr_num++;
        }
        if (result & ADDR_RANGE_MATCH) {
        }
    }
}

void helper_trace_m_value(CPUArchState *env, target_ulong pc,
                          target_ulong addr, target_ulong value)
{
    int packlen = 0;
    int result = data_trace_filter(env, pc, addr, value);

    packlen = 2 * sizeof(uint8_t) + sizeof(uint32_t);
    if (result & STSP_RANGE_MATCH) {
        //write_trace_8_seq(DATA_VALUE, packlen, &value);
        if (sizeof(target_ulong) == 64) {
            write_trace_8_8(DATA_VALUE, packlen, 0, extract64(value, 0, 32));
            write_trace_8_8(DATA_VALUE, packlen, 0, extract64(value, 32, 32));
        } else {
            write_trace_8_8(DATA_VALUE, packlen, 0, value);
        }
        if (result & ADDR_RANGE_MATCH) {
        }
        if (result & DATA_RANGE_MATCH) {
        }
    }
}

void helper_trace_update_tcr(CPUArchState *env)
{
    int mode = -1;
    int enable = 0;
    uint32_t *addr_index = &tfilter.addr_num;
    uint32_t *data_index = &tfilter.data_num;
    uint32_t *stsp_index = &tfilter.stsp_num;
    int value_index, value_mode, i;
    CPUState *cpu = env_cpu(env);

    /* TRCEn enable */
    if ((env->cp13.tcr & 0x01) && (cpu->csky_trace_features & CSKY_TRACE)) {

        tfilter.enable = true;
        tfilter.event = env->cp13.ter; /* get all trace event */
        /* first send trace header */
        write_trace_header(env->cp13.ter);

        if (!(cpu->csky_trace_features & MEM_TRACE)) {
            tfilter.event &= ~(0x02);
        }
        if (!(cpu->csky_trace_features & TB_TRACE)) {
            tfilter.event &= ~(0x01);
        }
        tfilter.asid = env->cp13.asid;

        /* fill all range */
        for (i = 0; i < MAX_ADDR_CMPR_NUM - 1; i++) {
            mode = (env->cp13.addr_cmpr_config[i] & ADDR_CMPR_MODE_MASK)
                    >> ADDR_CMPR_MODE_START;
            enable = env->cp13.addr_cmpr_config[i] & ADDR_CMPR_ENABLE_MASK;
            if (enable) {
                switch (mode) {
                case INSN_ADDR_RANGE_CMPR:
                case DATA_ADDR_RANGE_CMPR: /* addr range match */
                    tfilter.addr_range[*addr_index].start
                                    = env->cp13.addr_cmpr[i];
                    tfilter.addr_range[*addr_index].end
                                    = env->cp13.addr_cmpr[++i];
                    (*addr_index)++;
                    break;
                case ASSOCIATE_VALUE_CMPR: /* associate value match */
                    value_index = env->cp13.addr_cmpr_config[i]
                                & ADDR_CMPR_DATA_MASK;
                    value_mode = env->cp13.data_cmpr_config[value_index]
                                & DATA_CMPR_MODE_MASK;
                    if (TRACE_VALUE_SINGLE == value_mode) {
                        tfilter.data_range[*data_index].mode
                                = value_mode;
                        tfilter.data_range[*data_index].min
                                = env->cp13.data_cmpr[value_index];
                    } else if (TRACE_VALUE_RANGE == value_mode) {
                        tfilter.data_range[*data_index].mode
                                = value_mode;
                        tfilter.data_range[*data_index].min
                                = env->cp13.data_cmpr[value_index];
                        tfilter.data_range[*data_index].max
                                = env->cp13.data_cmpr[value_index + 1];
                    } else {
                        return;
                    }
                    tfilter.data_range[*data_index].data_addr.start
                            =  env->cp13.addr_cmpr[i];
                    tfilter.data_range[*data_index].data_addr.end
                            =  env->cp13.addr_cmpr[++i];
                    (*data_index)++;
                    break;
                case STSP_RANGE_CMPR: /* stsp range match */
                    tfilter.stsp_range[*stsp_index].start
                            = env->cp13.addr_cmpr[i];
                    tfilter.stsp_range[*stsp_index].end
                            = env->cp13.addr_cmpr[++i];
                    (*stsp_index)++;
                    break;
                default:
                    break;
                }
            }
        }
    } else {
        memset(&tfilter, 0, sizeof(tfilter));
    }
}

