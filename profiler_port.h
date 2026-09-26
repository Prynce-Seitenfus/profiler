#ifndef PROFILER_PORT_H
#define PROFILER_PORT_H

#include <stdint.h>

/**
 * @file profiler_port.h
 * @brief Target-specific timestamp source for the generic profiler.
 *
 * This header provides the compile-time macro PROFILER_TICKS().
 * On embedded hardware (e.g. ARM Cortex-M), define this macro to sample
 * high-resolution hardware counters such as:
 *   #define PROFILER_TICKS() (DWT->CYCCNT)
 *   #define PROFILER_TICKS() (TIM2->CNT)
 *
 * For host-based unit testing (e.g., test_bench on Windows/MinGW), a fallback
 * implementation is provided if not externally defined.
 */

#ifndef PROFILER_TICKS
#if defined(_MSC_VER)
#include <intrin.h>
#define PROFILER_TICKS() ((uint32_t)__rdtsc())
#elif defined(__x86_64__) || defined(__i386__)
static inline uint32_t profiler_port_default_ticks(void)
{
    uint32_t lo;
    uint32_t hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return lo;
}
#define PROFILER_TICKS() (profiler_port_default_ticks())
#else
#define PROFILER_TICKS() (0U)
#endif
#endif /* PROFILER_TICKS */

#endif /* PROFILER_PORT_H */
