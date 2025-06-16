/*
 * RISC-V DSA Helpers for QEMU.
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

#ifndef RISCV_DSA_H
#define RISCV_DSA_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define DSA_VER_0_1 0x1

/*
 * It is the same as RISCVException in cpu_bits.h.
 * Rename it to avoid compile error.
 */
typedef enum RISCV_DSA_Exception {
    RISCV_DSA_EXCP_NONE = -1, /* sentinel value */
    RISCV_DSA_EXCP_INST_ADDR_MIS = 0x0,
    RISCV_DSA_EXCP_INST_ACCESS_FAULT = 0x1,
    RISCV_DSA_EXCP_ILLEGAL_INST = 0x2,
    RISCV_DSA_EXCP_BREAKPOINT = 0x3,
    RISCV_DSA_EXCP_LOAD_ADDR_MIS = 0x4,
    RISCV_DSA_EXCP_LOAD_ACCESS_FAULT = 0x5,
    RISCV_DSA_EXCP_STORE_AMO_ADDR_MIS = 0x6,
    RISCV_DSA_EXCP_STORE_AMO_ACCESS_FAULT = 0x7,
    RISCV_DSA_EXCP_U_ECALL = 0x8,
    RISCV_DSA_EXCP_S_ECALL = 0x9,
    RISCV_DSA_EXCP_VS_ECALL = 0xa,
    RISCV_DSA_EXCP_M_ECALL = 0xb,
    RISCV_DSA_EXCP_INST_PAGE_FAULT = 0xc, /* since: priv-1.10.0 */
    RISCV_DSA_EXCP_LOAD_PAGE_FAULT = 0xd, /* since: priv-1.10.0 */
    RISCV_DSA_EXCP_STORE_PAGE_FAULT = 0xf, /* since: priv-1.10.0 */
    RISCV_DSA_EXCP_SW_CHECK = 0x12, /* since: priv-1.13.0 */
    RISCV_DSA_EXCP_HW_ERR = 0x13, /* since: priv-1.13.0 */
    RISCV_DSA_EXCP_DOUBLE_TRAP = 0x10, /* Ssdbltrp extension */
    RISCV_DSA_EXCP_INST_GUEST_PAGE_FAULT = 0x14,
    RISCV_DSA_EXCP_LOAD_GUEST_ACCESS_FAULT = 0x15,
    RISCV_DSA_EXCP_VIRT_INSTRUCTION_FAULT = 0x16,
    RISCV_DSA_EXCP_STORE_GUEST_AMO_ACCESS_FAULT = 0x17,
    RISCV_DSA_EXCP_SEMIHOST = 0x3f,
} RISCV_DSA_Exception;

/* The api that qemu supply for dsa */
typedef struct qemu_dsa_ops {
    /*
     * @val: the value of gpr to set/get
     * @gprno: the number of gpr to set/get
     * @return: true if success
     */
    bool (*get_gpr)(uint64_t *val, uint32_t gprno);
    bool (*set_gpr)(uint64_t val, uint32_t gprno);
    bool (*get_fpr)(uint64_t *val, uint32_t fprno);
    bool (*set_fpr)(uint64_t val, uint32_t fprno);
    bool (*get_csr)(uint64_t *val, uint32_t csrno);
    bool (*set_csr)(uint64_t val, uint32_t csrno);
    bool (*get_vector_element)(void *val, uint32_t vregno,
                               uint32_t ele_size, uint32_t ele_no);
    bool (*set_vector_element)(void *val, uint32_t vregno,
                               uint32_t ele_size, uint32_t ele_no);

    /* Special case : ele_size == 0 represents balf a byte */
    bool (*get_matrix_element)(void *val, uint32_t mregno,
                               uint32_t ele_size, uint32_t rowno,
                               uint32_t colno);
    bool (*set_matrix_element)(void *val, uint32_t mregno,
                               uint32_t ele_size, uint32_t rowno,
                               uint32_t colno);

    /* vaddr is visual address, size only support 1, 2, 4, 8 bytes */
    bool (*load_data)(void *val, uint64_t vaddr, uint32_t size);
    bool (*store_data)(void *val, uint64_t vaddr, uint32_t size);
    bool (*get_reg_address)(void *env_base, uint64_t *offset, char *regname);
} qemu_dsa_ops;

typedef struct qemu_float_ops qemu_float_ops;

/* The api that libdsa supply for qemu */
typedef struct riscv_dsa_ops {
    uint32_t version;
    bool (*is_dsa_insn_16)(uint16_t insn);
    bool (*is_dsa_insn_32)(uint32_t insn);
    RISCV_DSA_Exception (*exec_dsa_insn)(void *env_base, qemu_dsa_ops *ops,
                    qemu_float_ops *fops,  uint32_t insn, uint32_t length);
} riscv_dsa_ops;

/* In dsa lib, a function named dsa_init should exist */
typedef uint32_t (*dsa_init_fn)(void *, riscv_dsa_ops *, qemu_dsa_ops *);

/*
 * The disassemble api that dsa should supply for qemu.
 * If not, dsa insns will be executed but not disassembled
 */
typedef enum {
    rv_dsa32,
    rv_dsa64,
    rv_dsa128
} rv_dsa_isa;

/* The disassemable result */
typedef struct dsa_disasm_info {
    rv_dsa_isa isa;     /* The isa, is filled before the function call */
    uint64_t pc;        /* The pc of the instruction */
    char *buf_name;
    uint32_t nbuflen;   /* The max length of buf_name, is filled before the function call */
    char *buf_operand;
    uint32_t obuflen;   /* The max length of buf_operand, is filled before the function call */
} dsa_disasm_info;

/*
 * The definitions below is the instruction info that
 * can be analysed by cpf.
 * The insn info is not necessary depending on whether
 * user want to use cpf.
 */
#define DSA_MAX_OPRND           8

enum DSA_OPERAND_TYPE {
    DSA_OP_NONE = 0,
    DSA_REG = 1,
    DSA_CREG = 2,       /* Control register, psr/vbr...  */
    DSA_IMM = 7,
    DSA_FREG = 8,
    DSA_LABEL = 9,
};

enum DSA_OPRND_PROP {
    DSA_DEF_OPRND       = 0x0,          /* default */
    DSA_SRC_OPRND       = 0x1,          /* source operand, if split, 1st splist inst */
    DSA_DST_OPRND       = 0x2,          /* dest operand, if split, 1st splist inst */
};

enum DSA_INST_TYPE {
    DSA_TY_NONE     = 0x0,      /* default type */
    DSA_TY_ALU      = 0x1,
    DSA_TY_JUMP     = 0x2,
    DSA_TY_BRANCH   = 0x4,      /* branch inst will be handled in branch model. */
    DSA_TY_LOAD     = 0x8,      /* amo insn should be DSA_TY_LOAD|DSA_TY_STORE */
    DSA_TY_STORE    = 0x10,
    DSA_TY_DSP      = 0x400,    /* P ext */
    DSA_TY_VDSP     = 0x800,    /* vector */
    DSA_TY_FLOAT    = 0x1000,
    DSA_TY_MATRIX   = 0x80000,  /* rv matrix extension */
};

struct dsa_insn_table {
    char        *name;          /* instruction name. */
    uint32_t    type;
    int         pipe_cycle;     /* cycles in excute, default is 1*/
};

typedef struct dsa_insn_info {
    uint32_t            id;             /* instruction id in dsa_insn_table */
    uint32_t            oprnd_num;      /* the number of operand. */
    struct dsa_insn_op {                /* instruction operands. */
        uint32_t        type;           /* operand type, such as reg, imm, freg. */
        uint32_t        prop;           /* operand property, such as source operand, dest operand. */
        uint32_t        value;          /* if type=REG, value=regno, if type=IMM, value=imm value*/
    } op[DSA_MAX_OPRND];
} dsa_insn_info;

/*
 * In dsa lib, a function named dsa_disasm_inst should exist
 * insn_info can be NULL, must check it
 */
typedef bool (*dsa_disasm_fn)(uint64_t inst, uint32_t instlen,
                              dsa_disasm_info *dis_info, dsa_insn_info *insn_info);
typedef uint32_t (*get_dsa_insn_table_fn)(struct dsa_insn_table **insn_table);
#endif
