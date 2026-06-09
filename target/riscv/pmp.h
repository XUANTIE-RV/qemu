/*
 * QEMU RISC-V PMP (Physical Memory Protection)
 *
 * Author: Daire McNamara, daire.mcnamara@emdalo.com
 *         Ivan Griffin, ivan.griffin@emdalo.com
 *
 * This provides a RISC-V Physical Memory Protection interface
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

#ifndef RISCV_PMP_H
#define RISCV_PMP_H

#include "cpu.h"

typedef enum {
    PMP_READ  = 1 << 0,
    PMP_WRITE = 1 << 1,
    PMP_EXEC  = 1 << 2,
    PMP_AMATCH = (3 << 3),
    PMP_LOCK  = 1 << 7,
    PMP_U = 1 << 8,
    PMP_SHARED = 1 << 9
} pmp_priv_t;

typedef enum {
    PMP_AMATCH_OFF,  /* Null (off)                            */
    PMP_AMATCH_TOR,  /* Top of Range                          */
    PMP_AMATCH_NA4,  /* Naturally aligned four-byte region    */
    PMP_AMATCH_NAPOT /* Naturally aligned power-of-two region */
} pmp_am_t;

typedef enum {
    MSECCFG_MML       = 1 << 0,
    MSECCFG_MMWP      = 1 << 1,
    MSECCFG_RLB       = 1 << 2,
    MSECCFG_USEED     = 1 << 8,
    MSECCFG_SSEED     = 1 << 9,
    MSECCFG_MLPE      = 1 << 10,
    MSECCFG_PMM       = 3ULL << 32,
} mseccfg_field_t;

typedef struct {
    target_ulong addr_reg;
    uint16_t  cfg_reg;
} pmp_entry_t;

typedef struct {
    hwaddr sa;
    hwaddr ea;
} pmp_addr_t;

typedef struct {
    pmp_entry_t pmp[MAX_RISCV_PMPS];
    pmp_addr_t  addr[MAX_RISCV_PMPS];
    uint32_t num_rules;
    uint8_t  spmp_start;
} pmp_table_t;

void pmpcfg_csr_write(CPURISCVState *env, uint32_t reg_index,
                      target_ulong val);
target_ulong pmpcfg_csr_read(CPURISCVState *env, uint32_t reg_index);

void mseccfg_csr_write(CPURISCVState *env, uint64_t val);
uint64_t mseccfg_csr_read(CPURISCVState *env);

void pmpaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
                       target_ulong val);
target_ulong pmpaddr_csr_read(CPURISCVState *env, uint32_t addr_index);
bool pmp_hart_has_privs(CPURISCVState *env, hwaddr addr,
                        target_ulong size, pmp_priv_t privs,
                        pmp_priv_t *allowed_privs,
                        target_ulong mode);
target_ulong pmp_get_tlb_size(CPURISCVState *env, hwaddr addr);
void pmp_update_rule_addr(CPURISCVState *env, uint32_t pmp_index);
void pmp_update_rule_nums(CPURISCVState *env);
uint32_t pmp_get_num_rules(CPURISCVState *env);
int pmp_priv_to_page_prot(pmp_priv_t pmp_priv);
void pmp_unlock_entries(CPURISCVState *env);

#define MSECCFG_MML_ISSET(env) get_field(env->mseccfg, MSECCFG_MML)
#define MSECCFG_MMWP_ISSET(env) get_field(env->mseccfg, MSECCFG_MMWP)
#define MSECCFG_RLB_ISSET(env) get_field(env->mseccfg, MSECCFG_RLB)

int get_physical_address_pmp(CPURISCVState *env, int *prot, hwaddr addr,
                             int size, MMUAccessType access_type,
                             int mode);
uint8_t pmp_get_a_field(uint16_t cfg_reg);
int pmp_is_in_range(CPURISCVState *env, int pmp_index, hwaddr addr);
int spmp_lookup(CPURISCVState *env, hwaddr addr,
                target_ulong size, MMUAccessType access_type,
                pmp_priv_t *allowed_privs, int mmu_idx);
target_ulong spmp_get_tlb_size(CPURISCVState *env, hwaddr addr);
/*
 * A function pointer type for callbacks that check if a PMP/SPMP rule
 * at a given index is currently active.
 */
typedef bool (*is_pmp_rule_active_fn)(CPURISCVState *env, int index);
target_ulong get_pmp_tlb_size_core(CPURISCVState *env, hwaddr addr,
                                   int start_idx, int end_idx,
                                   is_pmp_rule_active_fn is_active_cb);
                                   
target_ulong spmpaddr_csr_read(CPURISCVState *env, uint32_t addr_index);
void spmpaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
                        target_ulong val);

target_ulong spmpcfg_csr_read(CPURISCVState *env, uint32_t global_index);
void spmpcfg_csr_write(CPURISCVState *env, uint32_t global_index,
                       target_ulong val);
#endif
