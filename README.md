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
- **Packed Binary Protocol (`PROF-BIN v1`)**: Emits compact binary frames directly over `stream` with zero string formatting and zero-copy DMA bursts, sealed with an IEEE 802.3 CRC-32 trailer.
- **Anti-Recursion Protection**: All module symbols decorated with `NO_INST` (`__attribute__((no_instrument_function))`).

---

## Build System Integration

The module is distributed as pure C source files (`profiler.h`, `profiler.c`, `profiler_port.h`) and must **not** be compiled into a static library archive (`.a`). Compile `profiler.c` directly into your application or firmware image along with `hashmap.c`. Implement target-specific timestamp and critical section functions declared in `profiler_port.h`.

Crucially, **`profiler.c` and `hashmap.c` must NOT be compiled with `-finstrument-functions`**.

Selectively enable function instrumentation on your application files in CMake:

```cmake
# Instrument target application files selectively
set_source_files_properties(
    <file>.c
    PROPERTIES COMPILE_OPTIONS "-finstrument-functions"
)
```

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

## Porting

Implement the timestamp setup, tick reading, and critical section functions in `profiler_port.c`:

```c
#include "profiler_port.h"
#include "stm32h533xx.h"

void profiler_port_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t profiler_port_ticks(void)
{
    return DWT->CYCCNT;
}

uint32_t profiler_port_enter_critical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

void profiler_port_exit_critical(uint32_t state)
{
    __set_PRIMASK(state);
}
```

---

## Analyzing a Dump

`parse_prof_dump.py` decodes the `PROF-BIN v1` binary format, verifies payload CRC-32 integrity, and resolves function addresses using `arm-none-eabi-addr2line`:

```bash
python parse_prof_dump.py dump.bin --elf firmware.elf --output report.csv --summary
```