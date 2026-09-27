#ifndef PROFILER_H
#define PROFILER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declaration of Bitmap to avoid strict header dependency if unused */
struct Bitmap;

#ifndef NO_INST
#define NO_INST __attribute__((no_instrument_function))
#endif

#define PROFILER_EVENT_ENTER (0U)
#define PROFILER_EVENT_EXIT  (1U)

/**
 * @brief Represents a single function enter/exit profiling event.
 */
typedef struct {
    void*     this;      /**< Pointer to the current function address. */
    void*     call;      /**< Pointer to the call site address. */
    uint32_t  timestamp; /**< Raw timestamp counter snapshot from profiler_port_ticks(). */
    uint8_t   event;     /**< PROFILER_EVENT_ENTER or PROFILER_EVENT_EXIT. */
} profiler_event_t;

/**
 * @brief Profiler configuration passed by the application at startup.
 */
typedef struct {
    uint32_t          frequency; /**< Ticks per second, unit documented by app. */
    profiler_event_t* buffer;    /**< Caller-allocated buffer, static lifetime. */
    uint32_t          capacity;  /**< Must be a power of two (>= 2). */
} profiler_config_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the profiler with caller-supplied buffer and configuration.
 *
 * Validates that config is non-NULL, buffer is non-NULL, and capacity is a power of 2 >= 2.
 * If validation fails, the profiler remains uninitialized and disabled.
 * Does not start event capture automatically.
 *
 * @param config Pointer to the configuration structure.
 */
NO_INST void profiler_init(const profiler_config_t* config);

/**
 * @brief Returns the configured tick frequency (stored value for offline analysis).
 *
 * @return Frequency value supplied in profiler_init().
 */
NO_INST uint32_t profiler_frequency(void);

/**
 * @brief Returns the configured event buffer capacity.
 *
 * @return Capacity value supplied in profiler_init().
 */
NO_INST uint32_t profiler_capacity(void);

/**
 * @brief Enables function event capture.
 */
NO_INST void profiler_start(void);

/**
 * @brief Disables function event capture.
 */
NO_INST void profiler_stop(void);

/**
 * @brief Resets event write index and overflow flag without clearing configuration.
 */
NO_INST void profiler_reset(void);

/**
 * @brief Queries whether profiling is currently enabled.
 *
 * @return true if enabled, false otherwise.
 */
NO_INST bool profiler_enabled(void);

/**
 * @brief Queries whether the ring buffer wrapped and overwrote older events.
 *
 * @return true if overflow/wrap occurred since last init/reset.
 */
NO_INST bool profiler_overflowed(void);

/**
 * @brief Reads a single recorded event in chronological order.
 *
 * Index 0 maps to the oldest surviving recorded event.
 *
 * @param index Logical event index (0 to count - 1).
 * @param out_event Destination buffer to copy event data.
 * @return true if event was successfully read, false if index is out of bounds or parameter invalid.
 */
NO_INST bool profiler_read_event(uint32_t index, profiler_event_t* out_event);

/**
 * @brief Sets an optional bitmap filter for O(1) function filtering.
 *
 * When set, only functions whose address hash matches a set bit in the bitmap will be recorded.
 * Pass NULL to disable filtering and record all instrumented functions.
 *
 * @param filter Pointer to an initialized Bitmap structure, or NULL.
 */
NO_INST void profiler_set_filter_bitmap(const struct Bitmap* filter);

#ifdef __cplusplus
}
#endif

#endif /* PROFILER_H */
