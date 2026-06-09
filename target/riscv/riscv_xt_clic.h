#ifndef RISCV_XT_CLIC_H
#define RISCV_XT_CLIC_H
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "cpu.h"
#include "hw/intc/xt_clic.h"
#include "hw/intc/xt_clic_v0p10.h"

void riscv_clic_get_next_interrupt(CPURISCVState *env);
bool riscv_clic_is_clic_mode(CPURISCVState *env);
bool riscv_clic_shv_interrupt(CPURISCVState *env, int clic_irq);
bool riscv_clic_edge_triggered(CPURISCVState *env, int clic_irq);
void riscv_clic_clean_pending(CPURISCVState *env, int clic_irq);
void riscv_clic_set_irq(CPURISCVState *env, int clic_irq, uint8_t val);
void riscv_clic_decode_exccode(uint32_t exccode, target_ulong *mode,
                               target_ulong *il, target_ulong *irq);
target_ulong riscv_clic_find_suitable_interrupt(CPURISCVState *env,
                                                uint8_t write_mode);
#endif
