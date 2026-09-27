#include "profiler.h"
#include "profiler_port.h"
#include "atomic.h"
#include "bitmap.h"

/* Static internal state */
static profiler_event_t* s_buffer = NULL;
static uint32_t s_capacity = 0U;
static uint32_t s_mask = 0U;
static uint32_t s_frequency = 0U;
static volatile bool s_enabled = false;
static volatile bool s_overflowed = false;
static atomic_size_t s_write_index = { 0U };
static const Bitmap* s_filter_bitmap = NULL;

/* Helper to check if a value is a non-zero power of 2 (>= 2) */
NO_INST static bool is_valid_power_of_two(uint32_t value)
{
    return ((value >= 2U) && ((value & (value - 1U)) == 0U));
}

NO_INST void profiler_init(const profiler_config_t* config)
{
    s_enabled = false;
    s_buffer = NULL;
    s_capacity = 0U;
    s_mask = 0U;
    s_frequency = 0U;
    s_overflowed = false;
    atomic_init_size_t(&s_write_index, 0U);
    s_filter_bitmap = NULL;

    if (config == NULL) {
        return;
    }

    if (config->buffer == NULL) {
        return;
    }

    if (!is_valid_power_of_two(config->capacity)) {
        return;
    }

    s_buffer = config->buffer;
    s_capacity = config->capacity;
    s_mask = config->capacity - 1U;
    s_frequency = config->frequency;
    profiler_port_init();
}

NO_INST uint32_t profiler_frequency(void)
{
    return s_frequency;
}

NO_INST uint32_t profiler_capacity(void)
{
    return s_capacity;
}

NO_INST void profiler_start(void)
{
    if (s_buffer != NULL) {
        s_enabled = true;
    }
}

NO_INST void profiler_stop(void)
{
    s_enabled = false;
}

NO_INST void profiler_reset(void)
{
    atomic_store_release(&s_write_index, 0U);
    s_overflowed = false;
}

NO_INST bool profiler_enabled(void)
{
    return s_enabled;
}

NO_INST bool profiler_overflowed(void)
{
    return s_overflowed;
}

NO_INST void profiler_set_filter_bitmap(const struct Bitmap* filter)
{
    s_filter_bitmap = filter;
}

NO_INST bool profiler_read_event(uint32_t index, profiler_event_t* out_event)
{
    if ((out_event == NULL) || (s_buffer == NULL)) {
        return false;
    }

    size_t total_written = atomic_load_acquire(&s_write_index);
    uint32_t total_events;

    if (s_overflowed) {
        total_events = s_capacity;
    } else {
        total_events = (uint32_t)total_written;
    }

    if (index >= total_events) {
        return false;
    }

    uint32_t slot;
    if (s_overflowed) {
        slot = (uint32_t)(((total_written - (size_t)s_capacity) + (size_t)index) & (size_t)s_mask);
    } else {
        slot = index;
    }

    *out_event = s_buffer[slot];
    return true;
}

NO_INST static bool check_filter(const void* fn_addr)
{
    if (s_filter_bitmap == NULL) {
        return true;
    }

    if (s_filter_bitmap->bit_count == 0U) {
        return false;
    }

    size_t hash = ((size_t)fn_addr >> 2U) % s_filter_bitmap->bit_count;
    return bitmap_test_bit(s_filter_bitmap, hash);
}

NO_INST void __cyg_profile_func_enter(void* this_fn, void* call_site)
{
    if (!s_enabled) {
        return;
    }

    if (!check_filter(this_fn)) {
        return;
    }

    uint32_t ts = profiler_port_ticks();
#if defined(__GNUC__) && (__GNUC__ >= 4)
    size_t idx = (size_t)__atomic_fetch_add(&s_write_index.value, 1U, __ATOMIC_RELAXED);
#else
    size_t idx = atomic_load_relaxed(&s_write_index);
    atomic_store_relaxed(&s_write_index, idx + 1U);
#endif

    uint32_t slot = (uint32_t)(idx & (size_t)s_mask);
    if (idx >= (size_t)s_capacity) {
        s_overflowed = true;
    }

    s_buffer[slot].this = this_fn;
    s_buffer[slot].call = call_site;
    s_buffer[slot].timestamp = ts;
    s_buffer[slot].event = PROFILER_EVENT_ENTER;
}

NO_INST void __cyg_profile_func_exit(void* this_fn, void* call_site)
{
    if (!s_enabled) {
        return;
    }

    if (!check_filter(this_fn)) {
        return;
    }

    uint32_t ts = profiler_port_ticks();
#if defined(__GNUC__) && (__GNUC__ >= 4)
    size_t idx = (size_t)__atomic_fetch_add(&s_write_index.value, 1U, __ATOMIC_RELAXED);
#else
    size_t idx = atomic_load_relaxed(&s_write_index);
    atomic_store_relaxed(&s_write_index, idx + 1U);
#endif

    uint32_t slot = (uint32_t)(idx & (size_t)s_mask);
    if (idx >= (size_t)s_capacity) {
        s_overflowed = true;
    }

    s_buffer[slot].this = this_fn;
    s_buffer[slot].call = call_site;
    s_buffer[slot].timestamp = ts;
    s_buffer[slot].event = PROFILER_EVENT_EXIT;
}
