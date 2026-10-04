#ifndef PROFILER_TRANSPORT_H
#define PROFILER_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "profiler.h"
#include "stream.h"

/**
 * @file profiler_transport.h
 * @brief Hardware-independent transport contract for profiler I/O.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of a transport operation. */
typedef enum {
    PROFILER_TRANSPORT_STATUS_OK = 0,
    PROFILER_TRANSPORT_STATUS_INVALID_PARAM,
    PROFILER_TRANSPORT_STATUS_NOT_READY,
    PROFILER_TRANSPORT_STATUS_IO_ERROR,
    PROFILER_TRANSPORT_STATUS_TIMEOUT,
    PROFILER_TRANSPORT_STATUS_UNSUPPORTED
} ProfilerTransportStatus;

/** @brief Bit flags describing a latched receive event. */
#define PROFILER_TRANSPORT_EVENT_NONE    (0x00U)
#define PROFILER_TRANSPORT_EVENT_FRAME   (0x01U)
#define PROFILER_TRANSPORT_EVENT_OVERRUN (0x02U)
#define PROFILER_TRANSPORT_EVENT_ERROR   (0x04U)

/**
 * @brief Monotonic millisecond clock supplied by the application.
 *
 * @return Current free-running millisecond counter value.
 */
typedef uint32_t (*ProfilerTransportTickFn)(void);

/**
 * @brief Cooperative wait hook supplied by the application.
 *
 * @param milliseconds Requested wait duration.
 */
typedef void (*ProfilerTransportYieldFn)(uint32_t milliseconds);

/**
 * @brief Caller-owned, statically allocated transport configuration.
 */
typedef struct {
    void* device;                        /**< Backend handle, such as a UART handle. */
    uint8_t* tx_buffer;                  /**< Caller-owned transmit buffer. */
    size_t tx_buffer_size;               /**< Transmit buffer capacity in bytes. */
    uint8_t* rx_buffer;                  /**< Caller-owned receive frame buffer. */
    size_t rx_buffer_size;               /**< Receive buffer capacity in bytes. */
    uint32_t tx_timeout_ms;              /**< Per-chunk timeout; zero disables it where supported. */
    ProfilerTransportTickFn tick_ms;     /**< Monotonic millisecond clock, required for timeouts. */
    ProfilerTransportYieldFn yield;      /**< Cooperative wait hook; NULL permits busy-waiting. */
} ProfilerTransportConfig;

/**
 * @brief Initializes the selected backend and binds its Stream callbacks.
 *
 * @param[in] config Caller-owned configuration with static buffer storage.
 * @return `PROFILER_TRANSPORT_STATUS_OK` on success, otherwise an error.
 */
NO_INST ProfilerTransportStatus profiler_transport_init(const ProfilerTransportConfig* config);

/**
 * @brief Returns the initialized bidirectional Stream.
 *
 * @return Stream pointer, or NULL when the backend is not initialized.
 */
NO_INST const Stream* profiler_transport_stream(void);

/**
 * @brief Arms reception of the next frame.
 *
 * Re-arming while already listening is idempotent.
 *
 * @return `PROFILER_TRANSPORT_STATUS_OK` on success, otherwise an error.
 */
NO_INST ProfilerTransportStatus profiler_transport_listen(void);

/**
 * @brief Consumes pending receive event flags.
 *
 * @param[out] out_length Optional destination for the pending frame length.
 * @return Bitwise OR of `PROFILER_TRANSPORT_EVENT_*` flags.
 */
NO_INST uint8_t profiler_transport_poll(size_t* out_length);

/**
 * @brief Reads and clears the sticky transport error.
 *
 * Use this to observe failures from Stream write/flush callbacks; StreamFlushFn cannot return
 * a status.
 *
 * @return `PROFILER_TRANSPORT_STATUS_OK` when no error was latched.
 */
NO_INST ProfilerTransportStatus profiler_transport_status(void);

#ifdef __cplusplus
}
#endif

#endif /* PROFILER_TRANSPORT_H */
