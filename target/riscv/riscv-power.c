#include "qemu/osdep.h"
#include "cpu-qom.h"
#include "riscv-power.h"
#include "hw/core/cpu.h"
#include "cpu.h"

void riscv_cpu_set_power_on(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    cpu->power_state = XT_POWER_ON;
}

void riscv_cpu_set_power_off(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    cpu->power_state = XT_POWER_OFF;
}

static void riscv_set_cpu_on_async_work(CPUState *cs,
                                        run_on_cpu_data data)
{
    cpu_set_pc(cs, *(uint64_t *)data.host_ptr);
}

uint64_t release_address;

void riscv_cpu_release(CPUState *cs, uint64_t entry)
{
    cs->halted = false;
    riscv_cpu_set_power_on(cs);
    release_address = entry;
    async_run_on_cpu(cs, riscv_set_cpu_on_async_work,
                     RUN_ON_CPU_HOST_PTR(&release_address));
}
