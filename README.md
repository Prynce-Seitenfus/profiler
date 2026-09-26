# profiler

Ultra-low-overhead, hardware-independent GCC function instrumentation profiler module written in ANSI/ISO C99.

## Overview
The `profiler` module implements GCC's function instrumentation hooks (`__cyg_profile_func_enter` and `__cyg_profile_func_exit`) with minimal runtime overhead.

### Key Architectural Highlights:
- **Zero OS / Hardware Coupling**: Completely hardware-independent core. No CMSIS, vendor, or RTOS dependencies.
- **Zero Dynamic Allocation**: All event buffers are caller-allocated and statically managed.
- **Compile-Time Inline Timestamps**: Sourced via `#define PROFILER_TICKS()` defined in `profiler_port.h` without runtime function pointer indirection.
- **Lock-Free Preemption Safety**: Reentrant slot reservation across tasks and ISRs via atomic fetch-and-add using `atomic.h`.
- **Fast Power-of-Two Masking**: Bitwise ring buffer addressing (`index & (capacity - 1)`) without modulo or division operations.
- **Anti-Recursion Protection**: All module symbols decorated with `NO_INST` (`__attribute__((no_instrument_function))`).
- **Optional O(1) Function Filtering**: Integrated with `bitmap.h` to skip recording high-frequency functions.

---

## Build System Integration

The module is distributed as pure C source files (`profiler.h`, `profiler.c`, `profiler_port.h`) and must **not** be compiled into a static library archive (`.a`). Compile `profiler.c` directly into your application or firmware image.

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

Configure `profiler_port.h` for your target architecture:

- **ARM Cortex-M (DWT Cycle Counter)**:
  ```c
  #define PROFILER_TICKS() (DWT->CYCCNT)
  ```
- **ARM Cortex-M (Timer Counter)**:
  ```c
  #define PROFILER_TICKS() (TIM2->CNT)
  ```
- **POSIX / Host**:
  ```c
  #define PROFILER_TICKS() (platform_get_ticks())
  ```