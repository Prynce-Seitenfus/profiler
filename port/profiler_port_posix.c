#include "profiler_port.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <time.h>
#include <pthread.h>

static pthread_mutex_t s_profiler_mutex = PTHREAD_MUTEX_INITIALIZER;

NO_INST void profiler_port_init(void)
{
}

NO_INST uint32_t profiler_port_ticks(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        uint64_t ns = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
        return (uint32_t)(ns & 0xFFFFFFFFULL);
    }
    return 0U;
}

NO_INST uint32_t profiler_port_enter_critical(void)
{
    (void)pthread_mutex_lock(&s_profiler_mutex);
    return 0U;
}

NO_INST void profiler_port_exit_critical(uint32_t state)
{
    (void)state;
    (void)pthread_mutex_unlock(&s_profiler_mutex);
}

#else

NO_INST void profiler_port_init(void) {}
NO_INST uint32_t profiler_port_ticks(void) { return 0U; }
NO_INST uint32_t profiler_port_enter_critical(void) { return 0U; }
NO_INST void profiler_port_exit_critical(uint32_t state) { (void)state; }

#endif
