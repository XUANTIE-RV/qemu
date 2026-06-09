#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/cpu.h"
#include "qapi/error.h"
#include "trace.h"
#include "exec/exec-all.h"
#include "cpu.h"
#include "pmp.h"
#include "internals.h"
/*
 * Check if address is within PMP entry range
 */
static bool spmp_in_range(CPURISCVState *env, int idx,
                          hwaddr addr, target_ulong size)
{
    hwaddr start = pmp_is_in_range(env, idx, addr);
    hwaddr end   = pmp_is_in_range(env, idx, addr + size - 1);
    return (start + end) == 2;
}

/*
 * Determine if an access is permitted based on the given SPMP rule
 * configuration and the current CPU state.
 *
 * @cfg_perms:      R/W/X permissions from the SPMP config register.
 * @u_bit:          The U-bit of the SPMP rule.
 * @shared_bit:     The SHARED-bit of the SPMP rule.
 * @mode:           The current CPU privilege mode (PRV_S or PRV_U).
 * @sum_bit:        The value of the sstatus.SUM bit.
 * @access_type:    The requested access type (read, write, or exec).
 * @sstack_inst:    Indicates if access is for a CFI shadow stack op.
 * @allowed_privs:  Output pointer for the resulting allowed permissions.
 * @return:         A translation status code (SUCCESS, FAIL, PMP_FAIL).
 */
static int spmp_check_permissions(pmp_priv_t cfg_perms,
                                  bool u_bit, bool shared_bit,
                                  target_ulong mode, bool sum_bit,
                                  MMUAccessType access_type,
                                  bool sstack_inst,
                                  pmp_priv_t *allowed_privs)
{
    pmp_priv_t effective_perms = 0; /* Default to deny all access */
    pmp_priv_t privs = 1 << access_type;

    /* First, check for invalid permission combinations in the rule itself. */
    if ((cfg_perms == (PMP_WRITE | PMP_EXEC)) ||
        ((cfg_perms == PMP_WRITE) && shared_bit)) {
        *allowed_privs = 0;
        return TRANSLATE_FAIL;
    }

    /*
     * Calculate effective permissions by rule type, which is determined
     * by the SHARED and U bits. This structure directly maps to the
     * SPMP encoding table in the RISC-V Privileged Specification.
     */
    int rule_type = (shared_bit << 1) | u_bit;
    switch (rule_type) {
    case 0b00: /* SHARED=0, U=0: S-mode Exclusive Region */
        if (mode == PRV_S) {
            effective_perms = cfg_perms;
        }
        break;
    case 0b01: /* SHARED=0, U=1: U-mode Exclusive Region */
        if (mode == PRV_U) {
            effective_perms = cfg_perms;
        } else if (mode == PRV_S && sum_bit) {
            /* If SUM=1, S-mode can access but not execute. */
            effective_perms = cfg_perms & ~PMP_EXEC;
        }
        break;
    case 0b11: /* SHARED=1, U=1: Shared Region */
        effective_perms = cfg_perms;
        /*
         * In U-mode, the W bit and X bit are mutually exclusive per spec
         * Figure 5 (encoding table):
         *   R=1,W=1,X=0 -> U-mode: Read-only (W stripped)
         *   R=1,W=1,X=1 -> U-mode: Exec-only (W and R stripped)
         */
        if (mode == PRV_U &&
            ((cfg_perms & (PMP_READ | PMP_WRITE)) == (PMP_READ | PMP_WRITE))) {
            effective_perms &= ~PMP_WRITE;
            if (cfg_perms & PMP_EXEC) {
                effective_perms &= ~PMP_READ;
            }
        }
        break;
    case 0b10: /* SHARED=1, U=0: Reserved */
        /*
         * Per spec, this combination is reserved. This implementation
         * defers to the permissions set in cfg_perms.
         */
        effective_perms = cfg_perms;
        break;
    }

    /* Check if the calculated effective permissions grant the request. */
    if (!(privs & effective_perms)) {
        *allowed_privs = 0;
        return TRANSLATE_FAIL;
    }

    /*
     * Basic permissions are met. Now handle the special case for CFI
     * (Control Flow Integrity) shadow stack regions.
     */
    bool cfi_rule_active = !shared_bit && (cfg_perms == PMP_WRITE);
    bool cfi_access_invalid = (access_type != MMU_DATA_LOAD) && !sstack_inst;
    if (cfi_rule_active && cfi_access_invalid) {
        *allowed_privs = 0;
        /* Fail with a specific code for PMP/CFI violations. */
        return TRANSLATE_PMP_FAIL;
    }

    /* All checks passed; the access is permitted. */
    *allowed_privs = effective_perms & privs;
    return TRANSLATE_SUCCESS;
}

/*
 * Callback to check if an SPMP rule is active.
 * A rule is active if its 'A' field is not OFF AND its corresponding
 * bit in the spmpswitch register is set.
 */
static bool spmp_is_rule_active(CPURISCVState *env, int i)
{
    int spmp_idx = i - env->pmp_state.spmp_start;
    assert((i >= env->pmp_state.spmp_start) && (i < MAX_RISCV_PMPS));
    uint8_t a_field = pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg);
    bool switch_enabled = (env->spmpswitch >> spmp_idx) & 1;
    return switch_enabled && (a_field != PMP_AMATCH_OFF);
}

/*
 * Main SPMP lookup function: check access rights under SPMP rules
 */
int spmp_lookup(CPURISCVState *env, hwaddr addr,
                target_ulong size, MMUAccessType access_type,
                pmp_priv_t *allowed_privs, int mmu_idx)
{
    pmp_table_t *pmp_state = &env->pmp_state;
    int mode = mmuidx_priv(mmu_idx);
    int pmp_size;
    int i;

    /* M-mode bypasses SPMP checks */
    if (mode == PRV_M) {
        *allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
        return TRANSLATE_SUCCESS;
    }

    /* If no SPMP entries are delegated, treat as inactive */
    if (pmp_state->spmp_start >= MAX_RISCV_PMPS) {
        *allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
        return TRANSLATE_SUCCESS;
    }

    /* Handle unknown size: assume full page access */
    if (size == 0) {
        pmp_size = -(addr | TARGET_PAGE_MASK);
    } else {
        pmp_size = size;
    }

    /* Search for highest-priority matching active rule */
    for (i = pmp_state->spmp_start; i < MAX_RISCV_PMPS; i++) {
        if (!spmp_is_rule_active(env, i)) {
            continue;
        }
        if (spmp_in_range(env, i, addr, pmp_size)) {
            uint16_t cfg = pmp_state->pmp[i].cfg_reg;
            pmp_priv_t perms = cfg & (PMP_READ | PMP_WRITE | PMP_EXEC);
            bool sum_bit = mmuidx_sum(mmu_idx);
            bool sstack_inst = get_field(mmu_idx, MMU_IDX_SS_ACCESS);

            return spmp_check_permissions(perms,
                                          (cfg & PMP_U) != 0,
                                          (cfg & PMP_SHARED) != 0,
                                          mode, sum_bit,
                                          access_type, sstack_inst,
                                          allowed_privs);
        }
    }

    /* No match found: default-deny if any SPMP is active */
    *allowed_privs = 0;
    return TRANSLATE_FAIL;
}

target_ulong spmp_get_tlb_size(CPURISCVState *env, hwaddr addr)
{
    /*
     * If the SPMP delegation extension is disabled or no rules are
     * delegated, the page is not split by SPMP.
     */
    if (!riscv_cpu_cfg(env)->ext_smpmpdeleg ||
        (env->pmp_state.spmp_start >= MAX_RISCV_PMPS)) {
        return TARGET_PAGE_SIZE;
    }

    return get_pmp_tlb_size_core(env, addr, env->pmp_state.spmp_start,
                                 MAX_RISCV_PMPS, spmp_is_rule_active);
}

target_ulong spmpaddr_csr_read(CPURISCVState *env, uint32_t addr_index)
{
    target_ulong val = env->pmp_state.pmp[addr_index].addr_reg;
    trace_pmpaddr_csr_read(env->mhartid, addr_index, val);
    return val;
}

void spmpaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
                        target_ulong val)
{
    /* No need to do anything if the value is not changing. */
    if (env->pmp_state.pmp[addr_index].addr_reg == val) {
        return;
    }
    trace_pmpaddr_csr_write(env->mhartid, addr_index, val);
    env->pmp_state.pmp[addr_index].addr_reg = val;
    pmp_update_rule_addr(env, addr_index);

    /*
     * If the next entry is TOR, its range depends on the address we just
     * wrote, so its derived address also needs to be updated.
     */
    if (addr_index + 1 < MAX_RISCV_PMPS) {
        uint8_t next_cfg = env->pmp_state.pmp[addr_index + 1].cfg_reg;
        if (PMP_AMATCH_TOR == pmp_get_a_field(next_cfg)) {
            pmp_update_rule_addr(env, addr_index + 1);
        }
    }

    tlb_flush(env_cpu(env));
}

target_ulong spmpcfg_csr_read(CPURISCVState *env, uint32_t global_index)
{
    target_ulong cfg_val = env->pmp_state.pmp[global_index].cfg_reg;
    trace_pmpcfg_csr_read(env->mhartid, global_index, cfg_val);
    return cfg_val;
}

void spmpcfg_csr_write(CPURISCVState *env, uint32_t global_index,
                       target_ulong val)
{
    trace_pmpcfg_csr_write(env->mhartid, global_index, val);
    if (val != env->pmp_state.pmp[global_index].cfg_reg) {
        env->pmp_state.pmp[global_index].cfg_reg = val;
        pmp_update_rule_addr(env, global_index);
        tlb_flush(env_cpu(env));
    }
    return;
}
