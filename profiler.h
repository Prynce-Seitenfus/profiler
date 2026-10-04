#ifndef PROFILER_H
#define PROFILER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "hashmap.h"

/* Forward declaration of Stream */
struct Stream;

#ifndef NO_INST
#define NO_INST __attribute__((no_instrument_function))
#endif

#define PROFILER_BIN_MAGIC    (0x464F5250U)
#define PROFILER_BIN_VERSION  (0x0002U)

/**
 * @brief Aggregated runtime performance metrics for a unique instrumented function.
 * Total size: 24 bytes (naturally 8-byte aligned on 32-bit and 64-bit architectures).
 */
typedef struct ProfilerMetric {
    uint32_t fn_address;   /**< Unique function code address (key). */
    uint32_t call_count;   /**< Total number of completed invocations. */
    uint64_t total_cycles; /**< Accumulated execution time in CPU cycles. */
    uint32_t min_cycles;   /**< Minimum observed execution duration in cycles. */
    uint32_t max_cycles;   /**< Maximum observed execution duration in cycles. */
} ProfilerMetric;

/**
 * @brief Shadow call stack activation frame for active in-flight invocations.
 */
typedef struct ProfilerStackFrame {
    const void* fn_address;      /**< Address of the executing function. */
    uint32_t    enter_timestamp; /**< Timestamp snapshot taken on entry. */
} ProfilerStackFrame;

/**
 * @brief Binary protocol header for streaming dumps.
 */
typedef struct ProfilerBinHeader {
    uint32_t magic;             /**< "PROF" magic identifier. */
    uint16_t version;           /**< Protocol version (0x0002U). */
    uint16_t record_count;      /**< Number of ProfilerMetric records following header. */
    uint32_t frequency;         /**< Timestamp clock frequency in Hz. */
    uint16_t dropped_functions; /**< Unique functions dropped due to capacity exhaustion. */
    uint16_t stack_overflows;   /**< Calls dropped due to shadow call stack overflow. */
} ProfilerBinHeader;

/**
 * @brief Application configuration provided to profiler_init().
 */
typedef struct ProfilerConfig {
    uint32_t            frequency;        /**< Timestamp clock frequency in Hz. */
    HashMapEntry*       map_entries;      /**< Static array of HashMapEntry (power of 2). */
    size_t              map_capacity;     /**< Total slot capacity of map_entries (>= 2). */
    ProfilerMetric*     metrics;          /**< Caller-allocated array of ProfilerMetric. */
    size_t              metrics_capacity; /**< Capacity of metrics array (must be >= 75% of map_capacity). */
    ProfilerStackFrame* stack_frames;     /**< Caller-allocated shadow call stack buffer. */
    size_t              stack_depth;      /**< Maximum nesting depth of stack_frames. */
} ProfilerConfig;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the profiler with caller-supplied buffers and configuration.
 *
 * Validates parameters, initializes the internal HashMap container over map_entries,
 * and resets all internal metric accumulators and shadow stack state.
 *
 * @param[in] config Pointer to the configuration structure.
 * @return true if initialized successfully, false on invalid parameters.
 */
NO_INST bool profiler_init(const ProfilerConfig* config);

/**
 * @brief Starts profiling by enabling instrumentation hooks.
 */
NO_INST void profiler_start(void);

/**
 * @brief Stops profiling by disabling instrumentation hooks.
 */
NO_INST void profiler_stop(void);

/**
 * @brief Resets all accumulated metrics, shadow call stack, diagnostics, and table entries.
 */
NO_INST void profiler_reset(void);

/**
 * @brief Queries whether profiling is currently enabled.
 *
 * @return true if enabled, false otherwise.
 */
NO_INST bool profiler_enabled(void);

/**
 * @brief Returns the configured timestamp frequency in Hz.
 *
 * @return Frequency value supplied in profiler_init().
 */
NO_INST uint32_t profiler_frequency(void);

/**
 * @brief Returns the number of unique functions currently tracked in the hash map.
 *
 * @return Count of occupied metric entries.
 */
NO_INST size_t profiler_tracked_count(void);

/**
 * @brief Returns the number of unique functions dropped due to full table capacity.
 *
 * @return Count of dropped unique functions.
 */
NO_INST uint16_t profiler_dropped_functions_count(void);

/**
 * @brief Returns the count of calls dropped due to shadow stack overflow.
 *
 * @return Count of dropped shadow stack entries.
 */
NO_INST uint16_t profiler_stack_overflow_count(void);

/**
 * @brief Retrieves a copy of the metrics associated with a function address.
 *
 * @param[in]  fn_addr    Function address to look up.
 * @param[out] out_metric Destination pointer to copy the metrics into.
 * @return true if function was found, false otherwise.
 */
NO_INST bool profiler_get_metric(const void* fn_addr, ProfilerMetric* out_metric);

/**
 * @brief Streams packed binary profiler statistics over an abstract Stream.
 *
 * Emits the 16-byte ProfilerBinHeader, streams all ProfilerMetric records directly
 * using zero-copy transfers, and finishes with a 32-bit CRC checksum.
 * Does NOT reset accumulated statistics (cumulative telemetry across calls).
 *
 * @param[in] stream Pointer to the target Stream instance.
 * @return true on successful transmission, false on stream error or invalid argument.
 */
NO_INST bool profiler_dump(const struct Stream* stream);

#ifdef __cplusplus
}
#endif

#endif /* PROFILER_H */
