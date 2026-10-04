# profiler

Ultra-low-overhead, hardware-independent GCC function execution profiler module written in ANSI/ISO C99.

## Overview
The `profiler` module implements GCC's function instrumentation hooks (`__cyg_profile_func_enter` and `__cyg_profile_func_exit`) using an in-memory statistical aggregation model driven by `hashmap`. Rather than streaming raw chronological event traces that rapidly overflow circular buffers, `profiler` accumulates invocation counts, total execution cycles, minimum cycles, and maximum cycles per unique function directly in RAM.

### Key Architectural Highlights:
- **Zero OS / Hardware Coupling**: Completely hardware-independent core. No CMSIS, vendor, or RTOS dependencies.
- **Zero Dynamic Allocation**: Freestanding operation over caller-allocated `HashMapEntry`, `ProfilerMetric`, and `ProfilerStackFrame` buffers (MISRA C:2012 Rule 21.3).
- **In-Memory Cycle Accounting**: Tracks `call_count`, `total_cycles`, `min_cycles`, and `max_cycles` per unique function code address.
- **Sub-Microsecond In-Place Accumulation**: Uses Knuth's multiplicative golden ratio hash and in-place reference lookup (`hashmap_get_ref`) to locate and update accumulators in single-digit CPU cycles.
- **Single Shared Shadow Stack**: Measures exact elapsed function cycles across in-flight calls while safely dropping overflows when stack depth limits are reached.
- **Packed Binary Protocol (`PROF-BIN v2`)**: Emits compact binary frames directly over `Stream`, sealed with an IEEE 802.3 CRC-32 trailer.
- **Anti-Recursion Protection**: All module symbols decorated with `NO_INST` (`__attribute__((no_instrument_function))`).

---

## Build System Integration

Add the module to a CMake project and link the exported `profiler` target. The transport is selected
at configure time. The default `null` backend discards output; `memory` supports host tests;
`uart_stm32_hal_dma` is for DMA-capable STM32 integrations; and `uart_stm32_hal_it` uses UART
interrupt reception and blocking transmission for environments where DMA is unavailable, such as
Renode's current STM32G0 model.

```cmake
set(PROFILER_TRANSPORT "uart_stm32_hal_dma" CACHE STRING "")
add_subdirectory(path/to/profiler profiler_build)
target_link_libraries(firmware PRIVATE profiler)
```

The target exports `profiler.h`, `port/profiler_port.h`, and `port/profiler_transport.h`, and
resolves the `hashmap` and `stream` dependencies from `modules/` or sibling repositories. Set
`HASHMAP_DIR` or `STREAM_DIR` to override those locations. The library disables function
instrumentation for its own sources; instrument only application sources.

When using the STM32 backend, link it to the project's HAL interface target so CubeMX headers and
HAL symbols are available:

```cmake
target_link_libraries(profiler PRIVATE stm32cubemx)
```

The UART backend defines the three global HAL UART callbacks it uses. Do not define
`HAL_UART_TxCpltCallback`, `HAL_UART_ErrorCallback`, or `HAL_UARTEx_RxEventCallback` in the
application while that backend is selected.

---

## API Summary

```c
#include "profiler.h"
#include "hashmap.h"
#include "stream.h"

/* 1. Allocate static buffers with power-of-two map capacity */
#define MAP_CAPACITY     (64U)
#define METRICS_CAPACITY (48U) /* 75% load limit */
#define STACK_DEPTH      (16U)

static HashMapEntry       s_map_entries[MAP_CAPACITY];
static ProfilerMetric     s_metrics[METRICS_CAPACITY];
static ProfilerStackFrame s_stack_frames[STACK_DEPTH];

/* 2. Configure and initialize */
ProfilerConfig config = {
    .frequency        = 168000000U, /* e.g., 168 MHz CPU ticks */
    .map_entries      = s_map_entries,
    .map_capacity     = MAP_CAPACITY,
    .metrics          = s_metrics,
    .metrics_capacity = METRICS_CAPACITY,
    .stack_frames     = s_stack_frames,
    .stack_depth      = STACK_DEPTH
};

profiler_init(&config);

/* 3. Start profiling */
profiler_start();

/* ... run instrumented functions ... */

/* 4. Stream binary dump over abstract Stream transport (e.g. UART DMA) */
profiler_dump(&my_stream);

/* 5. Stop or reset if needed */
profiler_stop();
profiler_reset();
```

---

## Transport Integration

The application owns the command protocol and profiler start/stop lifecycle. The selected
transport backend supplies a `Stream` for `profiler_dump()` and exposes received frames through
`profiler_transport_poll()` and `Stream.read`.

Allocate transport buffers with static lifetime. DMA backends may require alignment and placement in
DMA-reachable memory:

```c
static uint8_t s_tx_buffer[256U] __attribute__((aligned(32)));
static uint8_t s_rx_buffer[32U] __attribute__((aligned(32)));

ProfilerTransportConfig transport_config = {
    .device = &huart2,
    .tx_buffer = s_tx_buffer,
    .tx_buffer_size = sizeof(s_tx_buffer),
    .rx_buffer = s_rx_buffer,
    .rx_buffer_size = sizeof(s_rx_buffer),
    .tx_timeout_ms = 1000U,
    .tick_ms = profiler_tick_ms,
    .yield = profiler_yield_ms
};

if (profiler_transport_init(&transport_config) != PROFILER_TRANSPORT_STATUS_OK) {
    Error_Handler();
}
if (profiler_transport_listen() != PROFILER_TRANSPORT_STATUS_OK) {
    Error_Handler();
}
```

In task context, poll the received frame, handle the command, call `profiler_dump()` with
`profiler_transport_stream()`, check `profiler_transport_status()` after the dump to detect a
flush failure, and re-arm reception. Keep HAL callbacks limited to latching transport events.

---

## Porting & Platform Backends

The module provides modular platform port backends selected via `PROFILER_PORT` in CMake:
- `dwt`: Native ARM Cortex-M DWT `CYCCNT` (M3/M4/M7/M33/M55) with zero interrupt overhead.
- `16bit_it`: Single 16-bit hardware timer extended with software overflow ISR (`profiler_port_16bit_it_overflow_isr`). Ideal for Cortex-M0/M0+, MSP430, AVR, and Renode simulation.
- `2x16bit`: Cascaded Master/Slave 16-bit timers with rollover double-read glitch protection (e.g. STM32 TIM1+TIM3 hardware TRGO).
- `win`: Native Windows host port using `QueryPerformanceCounter` and `CRITICAL_SECTION`.
- `posix`: POSIX host port using `clock_gettime(CLOCK_MONOTONIC)` and `pthread_mutex`.
- `riscv`: RISC-V hardware port reading CSR `mcycle` and atomic `mstatus` MIE masking.
- `custom` (default): Application provides its own `profiler_port.c` implementing `port/profiler_port.h`.

```cmake
set(PROFILER_PORT "dwt" CACHE STRING "")
set(PROFILER_TRANSPORT "uart_stm32_hal_dma" CACHE STRING "")
add_subdirectory(path/to/profiler profiler_build)
target_link_libraries(firmware PRIVATE profiler)
```

---

## Analyzing a Dump

`parse_prof_dump.py` decodes the `PROF-BIN v2` binary format, verifies payload CRC-32 integrity, and resolves function addresses using `arm-none-eabi-addr2line`:

```bash
python parse_prof_dump.py dump.bin --elf firmware.elf --output report.csv --summary
```

For interactive visualization, see [Perfetto UI](https://ui.perfetto.dev/) for traces or
[Speedscope](https://www.speedscope.app/) for profiles. The profiler's binary dump and the parser's
CSV output are not directly documented as compatible with either viewer; conversion to a supported
trace or profile format may be needed.