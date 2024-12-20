#ifndef RISCV_CPU_POWER_H
#define RISCV_CPU_POWER_H

#include "hw/core/cpu.h"

void riscv_cpu_set_power_on(CPUState *);
void riscv_cpu_set_power_off(CPUState *);
void riscv_cpu_release(CPUState *, uint64_t);

#endif
