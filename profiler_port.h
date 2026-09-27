#ifndef PROFILER_PORT_H
#define PROFILER_PORT_H

#include "profiler.h"

/**
 * @file profiler_port.h
 * @brief Fixed target-port contract implemented by the application.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the target's timestamp source.
 */
NO_INST void profiler_port_init(void);

/**
 * @brief Reads the target's free-running timestamp counter.
 *
 * @return Current timestamp counter value.
 */
NO_INST uint32_t profiler_port_ticks(void);

#ifdef __cplusplus
}
#endif

#endif /* PROFILER_PORT_H */
