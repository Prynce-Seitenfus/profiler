#ifndef PROFILER_PORT_H
#define PROFILER_PORT_H

#include <stdint.h>
#include "profiler.h"

/**
 * @file profiler_port.h
 * @brief Target port contract implemented by the application platform.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the hardware timestamp counter peripheral.
 */
NO_INST void profiler_port_init(void);

/**
 * @brief Reads the free-running hardware timestamp counter.
 *
 * @return Current timestamp tick value.
 */
NO_INST uint32_t profiler_port_ticks(void);

/**
 * @brief Enters a critical section, disabling interrupts.
 *
 * @return Target-specific previous interrupt/state mask to restore on exit.
 */
NO_INST uint32_t profiler_port_enter_critical(void);

/**
 * @brief Exits a critical section, restoring interrupt state.
 *
 * @param[in] state Previous interrupt state returned by profiler_port_enter_critical().
 */
NO_INST void profiler_port_exit_critical(uint32_t state);

#ifdef __cplusplus
}
#endif

#endif /* PROFILER_PORT_H */
