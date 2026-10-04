#include "profiler_port.h"

#if defined(__arm__) || defined(__thumb__)

#define PROFILER_DEMCR            (*(volatile uint32_t*)0xE000EDFCU)
#define PROFILER_DEMCR_TRCENA     (1U << 24U)

#define PROFILER_DWT_CTRL         (*(volatile uint32_t*)0xE0001000U)
#define PROFILER_DWT_CTRL_CYCCNTENA (1U << 0U)

#define PROFILER_DWT_CYCCNT       (*(volatile uint32_t*)0xE0001004U)

NO_INST void profiler_port_init(void)
{
    PROFILER_DEMCR |= PROFILER_DEMCR_TRCENA;
    PROFILER_DWT_CYCCNT = 0U;
    PROFILER_DWT_CTRL |= PROFILER_DWT_CTRL_CYCCNTENA;
}

NO_INST uint32_t profiler_port_ticks(void)
{
    return PROFILER_DWT_CYCCNT;
}

NO_INST uint32_t profiler_port_enter_critical(void)
{
    uint32_t prev_primask = 0U;
    __asm__ volatile (
        "mrs %0, primask\n"
        "cpsid i\n"
        : "=r" (prev_primask) :: "memory"
    );
    return prev_primask;
}

NO_INST void profiler_port_exit_critical(uint32_t state)
{
    __asm__ volatile (
        "msr primask, %0\n"
        :: "r" (state) : "memory"
    );
}

#else

NO_INST void profiler_port_init(void) {}
NO_INST uint32_t profiler_port_ticks(void) { return 0U; }
NO_INST uint32_t profiler_port_enter_critical(void) { return 0U; }
NO_INST void profiler_port_exit_critical(uint32_t state) { (void)state; }

#endif
