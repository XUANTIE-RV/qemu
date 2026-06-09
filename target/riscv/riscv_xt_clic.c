#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "riscv_xt_clic.h"

void riscv_clic_get_next_interrupt(CPURISCVState *env)
{
    if (env->xt_clic_v0p10) {
        xt_clic_v0p10_get_next_interrupt(env->xt_clic_v0p10);
    } else if (env->xt_clic_v0p8) {
        xt_clic_get_next_interrupt(env->xt_clic_v0p8);
    }
}

bool riscv_clic_is_clic_mode(CPURISCVState *env)
{
    if (env->xt_clic_v0p10) {
        return xt_clic_v0p10_is_clic_mode(env);
    } else if (env->xt_clic_v0p8) {
        return xt_clic_is_clic_mode(env);
    } else {
        return false;
    }
}

bool riscv_clic_shv_interrupt(CPURISCVState *env, int clic_irq)
{
    if (env->xt_clic_v0p10) {
        CPUState *cs = env_cpu(env);
        return xt_clic_v0p10_shv_interrupt(env->xt_clic_v0p10, cs->cpu_index, clic_irq);
    } else if (env->xt_clic_v0p8) {
        return xt_clic_shv_interrupt(env->xt_clic_v0p8, clic_irq);
    } else {
        return false;
    }
}

bool riscv_clic_edge_triggered(CPURISCVState *env, int clic_irq)
{
    if (env->xt_clic_v0p10) {
        CPUState *cs = env_cpu(env);
        return xt_clic_v0p10_edge_triggered(env->xt_clic_v0p10, cs->cpu_index, clic_irq);
    } else if (env->xt_clic_v0p8) {
        return xt_clic_edge_triggered(env->xt_clic_v0p8, clic_irq);
    } else {
        return false;
    }
}

void riscv_clic_clean_pending(CPURISCVState *env, int clic_irq)
{
    if (env->xt_clic_v0p10) {
        CPUState *cs = env_cpu(env);
        xt_clic_v0p10_clean_pending(env->xt_clic_v0p10, cs->cpu_index, clic_irq);
    } else if (env->xt_clic_v0p8) {
        xt_clic_clean_pending(env->xt_clic_v0p8, clic_irq);
    }
}

void riscv_clic_set_irq(CPURISCVState *env, int irq, uint8_t val)
{
    bool lock = bql_locked();
    int clic_irq = 0;
    if (!lock) {
        bql_lock();
    }
    if (env->xt_clic_v0p10) {
        XTCLICV0P10State *xt_clic_v0p10 = env->xt_clic_v0p10;

        clic_irq = env->mhartid * xt_clic_v0p10->num_sources + irq;
        xt_clic_v0p10_set_irq(xt_clic_v0p10, clic_irq, val);
    } else if (env->xt_clic_v0p8) {
        XTCLICState *clic = env->xt_clic_v0p8;

        clic_irq = env->mhartid * clic->num_sources + irq;
        xt_clic_set_irq(clic, clic_irq, val);
    }
    if (!lock) {
        bql_unlock();
    }
}

target_ulong riscv_clic_find_suitable_interrupt(CPURISCVState *env,
                                                uint8_t write_mode)
{
    if (env->xt_clic_v0p10) {
        return xt_clic_v0p10_find_suitable_interrupt(env, write_mode);
    } else if (env->xt_clic_v0p8) {
        return xt_clic_find_suitable_interrupt(env, write_mode);
    }
    return 0;
}

void riscv_clic_decode_exccode(uint32_t exccode, target_ulong *mode,
                               target_ulong *il, target_ulong *irq)
{
    if (irq != NULL) {
        *irq = extract32(exccode, 0, 12);
    }
    if (mode != NULL) {
        *mode = extract32(exccode, 12, 2);
    }
    if (il != NULL) {
        *il = extract32(exccode, 14, 8);
    }
}
