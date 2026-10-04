#include "profiler_port.h"

#if defined(__riscv)

NO_INST void profiler_port_init(void)
{
}

NO_INST uint32_t profiler_port_ticks(void)
{
    uint32_t cycle;
    __asm__ volatile ("csrr %0, mcycle" : "=r" (cycle));
    return cycle;
}

NO_INST uint32_t profiler_port_enter_critical(void)
{
    uint32_t prev_mstatus;
    /* Clear MIE (Machine Interrupt Enable, bit 3) atomically */
    __asm__ volatile ("csrci mstatus, 8\n"
                      "csrr %0, mstatus"
                      : "=r" (prev_mstatus) :: "memory");
    return prev_mstatus;
}

NO_INST void profiler_port_exit_critical(uint32_t state)
{
    /* Restore MIE bit if it was set in previous state */
    if ((state & (1U << 3U)) != 0U) {
        __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
    }
}

#else

NO_INST void profiler_port_init(void) {}
NO_INST uint32_t profiler_port_ticks(void) { return 0U; }
NO_INST uint32_t profiler_port_enter_critical(void) { return 0U; }
NO_INST void profiler_port_exit_critical(uint32_t state) { (void)state; }

#endif
