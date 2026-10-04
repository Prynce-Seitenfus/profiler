#include "profiler.h"
#include "profiler_port.h"
#include "stream.h"
#include <string.h>

/* Internal static profiler state */
static HashMap              s_map;
static ProfilerMetric*      s_metrics = NULL;
static size_t               s_metrics_capacity = 0U;
static size_t               s_metrics_count = 0U;

static ProfilerStackFrame*  s_stack_frames = NULL;
static size_t               s_stack_depth = 0U;
static size_t               s_stack_index = 0U;

static uint32_t             s_frequency = 0U;
static volatile bool        s_enabled = false;
static uint16_t             s_dropped_functions = 0U;
static uint16_t             s_stack_overflows = 0U;

/* Internal polynomial calculation for IEEE 802.3 CRC-32 (0xEDB88320) */
NO_INST static uint32_t calculate_crc32(uint32_t seed, const uint8_t* data, size_t length)
{
    uint32_t crc = seed;

    if (data == NULL) {
        return crc;
    }

    for (size_t i = 0U; i < length; ++i) {
        crc ^= (uint32_t)data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            } else {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

/* Helper to check if a value is a non-zero power of 2 (>= 2) */
NO_INST static bool is_valid_power_of_two(size_t value)
{
    return ((value >= 2U) && ((value & (value - 1U)) == 0U));
}

NO_INST bool profiler_init(const ProfilerConfig* config)
{
    s_enabled = false;
    s_metrics = NULL;
    s_metrics_capacity = 0U;
    s_metrics_count = 0U;
    s_stack_frames = NULL;
    s_stack_depth = 0U;
    s_stack_index = 0U;
    s_frequency = 0U;
    s_dropped_functions = 0U;
    s_stack_overflows = 0U;

    if (config == NULL) {
        return false;
    }

    if ((config->map_entries == NULL) || (!is_valid_power_of_two(config->map_capacity))) {
        return false;
    }

    if ((config->metrics == NULL) || (config->metrics_capacity == 0U)) {
        return false;
    }

    if ((config->stack_frames == NULL) || (config->stack_depth == 0U)) {
        return false;
    }

    if (!hashmap_init(&s_map, config->map_entries, config->map_capacity)) {
        return false;
    }

    s_metrics = config->metrics;
    s_metrics_capacity = config->metrics_capacity;
    s_stack_frames = config->stack_frames;
    s_stack_depth = config->stack_depth;
    s_frequency = config->frequency;

    profiler_port_init();
    return true;
}

NO_INST void profiler_start(void)
{
    if ((s_metrics != NULL) && (s_stack_frames != NULL)) {
        uint32_t state = profiler_port_enter_critical();
        s_enabled = true;
        profiler_port_exit_critical(state);
    }
}

NO_INST void profiler_stop(void)
{
    uint32_t state = profiler_port_enter_critical();
    s_enabled = false;
    profiler_port_exit_critical(state);
}

NO_INST void profiler_reset(void)
{
    uint32_t state = profiler_port_enter_critical();
    hashmap_clear(&s_map);
    s_metrics_count = 0U;
    s_stack_index = 0U;
    s_dropped_functions = 0U;
    s_stack_overflows = 0U;
    profiler_port_exit_critical(state);
}

NO_INST bool profiler_enabled(void)
{
    return s_enabled;
}

NO_INST uint32_t profiler_frequency(void)
{
    return s_frequency;
}

NO_INST size_t profiler_tracked_count(void)
{
    return s_metrics_count;
}

NO_INST uint16_t profiler_dropped_functions_count(void)
{
    return s_dropped_functions;
}

NO_INST uint16_t profiler_stack_overflow_count(void)
{
    return s_stack_overflows;
}

NO_INST bool profiler_get_metric(const void* fn_addr, ProfilerMetric* out_metric)
{
    if ((fn_addr == NULL) || (out_metric == NULL)) {
        return false;
    }

    uint32_t state = profiler_port_enter_critical();
    void* val = NULL;
    bool found = hashmap_get(&s_map, fn_addr, &val);
    if (found && (val != NULL)) {
        *out_metric = *(const ProfilerMetric*)val;
    }
    profiler_port_exit_critical(state);

    return found;
}

NO_INST bool profiler_dump(const struct Stream* stream)
{
    if ((stream == NULL) || (s_metrics == NULL)) {
        return false;
    }

    uint32_t state = profiler_port_enter_critical();
    bool was_enabled = s_enabled;
    s_enabled = false;
    profiler_port_exit_critical(state);

    ProfilerBinHeader header;
    header.magic = PROFILER_BIN_MAGIC;
    header.version = PROFILER_BIN_VERSION;
    header.record_count = (uint16_t)s_metrics_count;
    header.frequency = s_frequency;
    header.dropped_functions = s_dropped_functions;
    header.stack_overflows = s_stack_overflows;

    /* Compute CRC-32 over header and metric payload */
    uint32_t crc = calculate_crc32(0xFFFFFFFFU, (const uint8_t*)&header, sizeof(header));
    if (s_metrics_count > 0U) {
        size_t records_bytes = s_metrics_count * sizeof(ProfilerMetric);
        crc = calculate_crc32(crc, (const uint8_t*)s_metrics, records_bytes);
    }
    crc ^= 0xFFFFFFFFU;

    /* Stream 16-byte header */
    size_t written = stream_write(stream, (const uint8_t*)&header, sizeof(header));
    bool success = (written == sizeof(header));

    /* Stream contiguous metrics payload */
    if (success && (s_metrics_count > 0U)) {
        size_t records_bytes = s_metrics_count * sizeof(ProfilerMetric);
        written = stream_write(stream, (const uint8_t*)s_metrics, records_bytes);
        success = (written == records_bytes);
    }

    /* Stream 4-byte CRC-32 trailer */
    if (success) {
        written = stream_write(stream, (const uint8_t*)&crc, sizeof(crc));
        success = (written == sizeof(crc));
    }

    stream_flush(stream);

    /* Preserve cumulative metric statistics */
    state = profiler_port_enter_critical();
    s_enabled = was_enabled;
    profiler_port_exit_critical(state);

    return success;
}

NO_INST static void update_metric(const void* fn_addr, uint32_t cycles)
{
    void** slot = hashmap_get_ref(&s_map, fn_addr);

    if (slot != NULL) {
        ProfilerMetric* metric = (ProfilerMetric*)(*slot);
        metric->call_count++;
        metric->total_cycles += (uint64_t)cycles;
        if (cycles < metric->min_cycles) {
            metric->min_cycles = cycles;
        }
        if (cycles > metric->max_cycles) {
            metric->max_cycles = cycles;
        }
    } else if (s_metrics_count < s_metrics_capacity) {
        ProfilerMetric* metric = &s_metrics[s_metrics_count];
        metric->fn_address = (uint32_t)(uintptr_t)fn_addr;
        metric->call_count = 1U;
        metric->total_cycles = (uint64_t)cycles;
        metric->min_cycles = cycles;
        metric->max_cycles = cycles;

        if (hashmap_insert(&s_map, fn_addr, (void*)metric)) {
            s_metrics_count++;
        } else {
            if (s_dropped_functions < 0xFFFFU) {
                s_dropped_functions++;
            }
        }
    } else {
        if (s_dropped_functions < 0xFFFFU) {
            s_dropped_functions++;
        }
    }
}

NO_INST void __cyg_profile_func_enter(void* this_fn, void* call_site)
{
    (void)call_site;

    if (!s_enabled || (this_fn == NULL)) {
        return;
    }

    uint32_t ts = profiler_port_ticks();
    uint32_t state = profiler_port_enter_critical();

    if (s_stack_index < s_stack_depth) {
        s_stack_frames[s_stack_index].fn_address = this_fn;
        s_stack_frames[s_stack_index].enter_timestamp = ts;
        s_stack_index++;
    } else {
        if (s_stack_overflows < 0xFFFFU) {
            s_stack_overflows++;
        }
    }

    profiler_port_exit_critical(state);
}

NO_INST void __cyg_profile_func_exit(void* this_fn, void* call_site)
{
    (void)call_site;

    if (!s_enabled || (this_fn == NULL)) {
        return;
    }

    uint32_t ts = profiler_port_ticks();
    uint32_t state = profiler_port_enter_critical();

    if (s_stack_index > 0U) {
        const ProfilerStackFrame* frame = &s_stack_frames[s_stack_index - 1U];
        if (frame->fn_address == this_fn) {
            s_stack_index--;
            uint32_t cycles = ts - frame->enter_timestamp;
            update_metric(this_fn, cycles);
        }
    }

    profiler_port_exit_critical(state);
}
