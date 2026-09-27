# profiler

Ultra-low-overhead, hardware-independent GCC function instrumentation profiler module written in ANSI/ISO C99.

## Overview
The `profiler` module implements GCC's function instrumentation hooks (`__cyg_profile_func_enter` and `__cyg_profile_func_exit`) with minimal runtime overhead.

### Key Architectural Highlights:
- **Zero OS / Hardware Coupling**: Completely hardware-independent core. No CMSIS, vendor, or RTOS dependencies.
- **Zero Dynamic Allocation**: All event buffers are caller-allocated and statically managed.
- **Small Runtime Port Contract**: Target timestamp setup and reads are provided by two functions declared in `profiler_port.h`.
- **Lock-Free Preemption Safety**: Reentrant slot reservation across tasks and ISRs via atomic fetch-and-add using `atomic.h`.
- **Fast Power-of-Two Masking**: Bitwise ring buffer addressing (`index & (capacity - 1)`) without modulo or division operations.
- **Anti-Recursion Protection**: All module symbols decorated with `NO_INST` (`__attribute__((no_instrument_function))`).
- **Optional O(1) Function Filtering**: Integrated with `bitmap.h` to skip recording high-frequency functions.

---

## Build System Integration

The module is distributed as pure C source files (`profiler.h`, `profiler.c`, `profiler_port.h`) and must **not** be compiled into a static library archive (`.a`). Compile `profiler.c` directly into your application or firmware image. Add one target-specific `profiler_port.c` that implements the two functions declared in `profiler_port.h`.

Crucially, **`profiler.c` itself must NOT be compiled with `-finstrument-functions`**.

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

/* 1. Allocate static buffer with power-of-two capacity */
#define EVENT_CAPACITY 1024U
static profiler_event_t s_event_buffer[EVENT_CAPACITY];

/* 2. Configure and initialize */
profiler_config_t config = {
    .frequency = 168000000U, /* e.g., 168 MHz CPU ticks */
    .buffer    = s_event_buffer,
    .capacity  = EVENT_CAPACITY
};

profiler_init(&config);

/* 3. Start capturing */
profiler_start();

/* ... run instrumented functions ... */

/* 4. Stop capturing */
profiler_stop();

/* 5. Read back recorded events chronologically */
profiler_event_t event;
for (uint32_t i = 0U; profiler_read_event(i, &event); ++i) {
    /* Process or dump event.this, event.call, event.timestamp, event.event */
}
```

---

## Porting

Implement the timestamp setup and read functions in `profiler_port.c`. The
profiler calls `profiler_port_init()` once for each valid `profiler_init()`
configuration and calls `profiler_port_ticks()` when recording each event.
Keep the hardware-specific headers and counter access in this file; the shared
`profiler_port.h` does not need target-specific edits.

An STM32 DWT port can be as small as:

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
```

A host port can initialize a monotonic clock and return its current count.
Set `config.frequency` to the timestamp source's frequency so recorded values
can be interpreted correctly.

## Analyzing a dump

`scripts/parse_prof_dump.py` reads the `PROF-DUMP v1` text format and pairs each
function's `ENTER` and `EXIT` events. It reports each matched call as CSV with
the cycle count and elapsed time in microseconds. The script uses the frequency
in the dump header by default; `--frequency` overrides it. Supply the firmware
ELF to resolve function and call-site addresses:

```text
python scripts/parse_prof_dump.py dump.txt --elf firmware.elf
```

The `arm-none-eabi-addr2line` executable must be on `PATH` (or specified with
`--addr2line`). Use `--summary` for aggregate per-function statistics. If the
dump wrapped its event buffer, calls whose matching event was overwritten
cannot be timed; `--summary` reports unmatched entry and exit counts. Without
`--elf`, addresses are retained instead of being resolved to function names.
Pass `--frequency 32M` to override the dump's frequency.