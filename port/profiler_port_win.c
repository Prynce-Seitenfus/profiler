#include "profiler_port.h"

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static CRITICAL_SECTION s_profiler_cs;
static bool             s_profiler_cs_initialized = false;

NO_INST void profiler_port_init(void)
{
    if (!s_profiler_cs_initialized) {
        InitializeCriticalSection(&s_profiler_cs);
        s_profiler_cs_initialized = true;
    }
}

NO_INST uint32_t profiler_port_ticks(void)
{
    LARGE_INTEGER counter;
    if (QueryPerformanceCounter(&counter)) {
        return (uint32_t)(counter.QuadPart & 0xFFFFFFFFLL);
    }
    return (uint32_t)GetTickCount();
}

NO_INST uint32_t profiler_port_enter_critical(void)
{
    if (!s_profiler_cs_initialized) {
        InitializeCriticalSection(&s_profiler_cs);
        s_profiler_cs_initialized = true;
    }
    EnterCriticalSection(&s_profiler_cs);
    return 0U;
}

NO_INST void profiler_port_exit_critical(uint32_t state)
{
    (void)state;
    if (s_profiler_cs_initialized) {
        LeaveCriticalSection(&s_profiler_cs);
    }
}

#else

/* Fallback dummy implementation if compiled on non-Windows */
NO_INST void profiler_port_init(void) {}
NO_INST uint32_t profiler_port_ticks(void) { return 0U; }
NO_INST uint32_t profiler_port_enter_critical(void) { return 0U; }
NO_INST void profiler_port_exit_critical(uint32_t state) { (void)state; }

#endif
