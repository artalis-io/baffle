/* Installed public Hail only. Explicit endpoints, bounded private recording.
 * PCM/file I/O never runs in device callbacks; no system levels are changed. */
#include "hail_owner.h"
#include <hail/virtual.h>
typedef struct Block {
    ha_block_info info;
    uint64_t file_frame;
    uint32_t direction;
} Block;
typedef struct Event {
    int64_t elapsed_ns;
    uint32_t state;
    uint64_t generation;
    int32_t result;
} Event;
typedef struct Acquisition {
    ha_context *context;
    ha_stream *stream;
    float *microphone, *reference;
    Block *blocks;
    Event *events;
    size_t capacity, block_capacity, mc, rc, bc, ec;
    uint64_t origin, generation;
} Acquisition;
static int poll_context(ha_context *c)
{
    uint32_t n = 0;
    return ha_context_poll(c, NULL, 0, &n) == HA_OK;
}
static int status(ha_stream *s, ha_stream_status *out)
{
    *out = (ha_stream_status)HA_INIT(ha_stream_status);
    return ha_stream_get_status(s, out) == HA_OK;
}
static int event(Acquisition *a, uint32_t state, ha_result result)
{
    uint64_t now;
    if (a->ec >= 4096 || !demo_now(&now) || now < a->origin || now - a->origin > INT64_MAX)
        return 0;
    a->events[a->ec++] = (Event){(int64_t)(now - a->origin), state, a->generation, result};
    return 1;
}
static int drain(Acquisition *a)
{
    ha_stream_status s;
    if (!status(a->stream, &s))
        return 0;
    if (s.state != HA_RUNNING && s.state != HA_STOPPING)
        return 1;
    for (uint32_t leg = 0; leg < 2; ++leg)
        for (unsigned i = 0; i < 64; ++i) {
            size_t *count = leg ? &a->rc : &a->mc;
            if (*count > a->capacity || a->capacity - *count < 480)
                return 0;
            uint32_t n = 0;
            ha_block_info info = HA_INIT(ha_block_info);
            float *dst = leg ? a->reference + *count * 2 : a->microphone + *count;
            ha_result r = leg ? ha_stream_read_reference(a->stream, dst, 480, &n, &info)
                              : ha_stream_read(a->stream, dst, 480, &n, &info);
            if (r == HA_WOULD_BLOCK || r == HA_BUSY)
                break;
            if (r != HA_OK)
                return 0;
            if (!n)
                break;
            if (n > 480 || info.frames != n || a->bc >= a->block_capacity)
                return 0;
            a->blocks[a->bc++] = (Block){info, (uint64_t)*count, leg};
            *count += n;
        }
    return 1;
}
static int select_device(ha_device_selector *d, uint32_t backend, const char *key)
{
    *d = (ha_device_selector)HA_INIT(ha_device_selector);
    if (backend == HA_BACKEND_VIRTUAL && !strcmp(key, "-"))
        return 1;
    size_t n = strlen(key);
    if (!n || n >= HA_DEVICE_KEY_CAPACITY)
        return 0;
    d->kind = HA_SELECT_PERSISTENT_KEY;
    d->key_bytes = (uint32_t)n;
    memcpy(d->key, key, n + 1);
    return 1;
}
static int write_metadata(FILE *f, const Acquisition *a, const ha_stream_info *info,
                          const ha_context_status *context, const ha_stream_status *final,
                          uint32_t backend, ha_result flush, int requested, int acknowledged,
                          const char *behavior)
{
    fprintf(f,
            "{\"type\":\"session\",\"backend\":%u,\"stream_id\":%llu,\"clock_relationship\":%u,"
            "\"conversion\":%u,\"virtual\":%s,"
            "\"capture_rate\":48000,\"capture_channels\":1,\"render_rate\":48000,\"render_"
            "channels\":2,\"capture_frames\":%zu,\"reference_frames\":%zu,"
            "\"capture_lost_frames\":%llu,\"playback_underrun_frames\":%llu,\"reference_lost_"
            "frames\":%llu,\"flush_result\":%d,"
            "\"flush_requested\":%s,\"flush_acknowledged\":%s,\"cancel_behavior\":",
            context->backend, (unsigned long long)info->stream_id, info->clock_relationship,
            info->conversion, backend == HA_BACKEND_VIRTUAL ? "true" : "false", a->mc, a->rc,
            (unsigned long long) final->capture_lost_frames,
            (unsigned long long) final->playback_underrun_frames,
            (unsigned long long) final->reference_lost_frames, flush, requested ? "true" : "false",
            acknowledged ? "true" : "false");
    if (!demo_json_string(f, behavior))
        return 0;
    fprintf(f, ",\"acoustic_calibration\":null}\n");
    for (size_t i = 0; i < a->bc; ++i) {
        Block b = a->blocks[i];
        ha_block_info t = b.info;
        fprintf(f,
                "{\"type\":\"block\",\"leg\":%u,\"file_frame\":%llu,\"first_frame\":%llu,"
                "\"frames\":%u,\"clock_domain\":%llu,\"epoch\":%llu,\"generation\":%llu,"
                "\"host_time_ns\":%llu,\"device_position\":%llu,\"uncertainty_ns\":%llu,\"flags\":%"
                "u,\"lost_frames\":%llu,\"frame_time_numerator\":%llu,\"frame_time_denominator\":%"
                "llu,\"time_remainder\":%llu}\n",
                b.direction, (unsigned long long)b.file_frame, (unsigned long long)t.first_frame,
                t.frames, (unsigned long long)t.clock_domain, (unsigned long long)t.clock_epoch,
                (unsigned long long)t.generation, (unsigned long long)t.host_time_ns,
                (unsigned long long)t.device_position, (unsigned long long)t.uncertainty_ns,
                t.flags, (unsigned long long)t.lost_frames,
                (unsigned long long)t.frame_time_numerator,
                (unsigned long long)t.frame_time_denominator, (unsigned long long)t.time_remainder);
    }
    for (size_t i = 0; i < a->ec; ++i) {
        Event e = a->events[i];
        fprintf(f,
                "{\"type\":\"event\",\"state\":%u,\"elapsed_ns\":%lld,\"generation\":%llu,"
                "\"result\":%d}\n",
                e.state, (long long)e.elapsed_ns, (unsigned long long)e.generation, e.result);
    }
    return !ferror(f);
}
#define REQUIRE(x, m)                                                                              \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            error = (m);                                                                           \
            goto done;                                                                             \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    Acquisition a = {0};
    int result = 1;
    const char *error = NULL;
    float *stimulus = NULL;
    size_t stimulus_size = 0;
    FILE *metadata = NULL;
    uint32_t backend = 0, seconds = 0;
    char capture_path[8192], reference_path[8192], metadata_path[8192];
    REQUIRE(argc == 3 || (argc >= 7 && argc <= 9),
            "acquire backend --list | backend capture-key playback-key stereo48k.f32 seconds "
            "prefix [cancel_ms [stop|restart]]");
    REQUIRE(demo_number(argv[1], HA_BACKEND_ALSA, &backend), "backend integer");
    ha_context_config cc = HA_INIT(ha_context_config);
    cc.backend = backend;
    REQUIRE(ha_context_create(&cc, &a.context) == HA_OK, "context create");
    if (argc == 3) {
        REQUIRE(!strcmp(argv[2], "--list"), "expected --list");
        ha_device_info devices[HA_MAX_DEVICES];
        uint32_t n = 0;
        for (uint32_t i = 0; i < HA_MAX_DEVICES; ++i)
            devices[i] = (ha_device_info)HA_INIT(ha_device_info);
        REQUIRE(ha_context_enumerate(a.context, devices, HA_MAX_DEVICES, &n) == HA_OK,
                "enumeration");
        for (uint32_t i = 0; i < n; ++i) {
            printf("{\"direction\":%u,\"name\":", devices[i].direction);
            REQUIRE(demo_json_string(stdout, devices[i].name), "device name output");
            printf(",\"key\":");
            REQUIRE(demo_json_string(stdout, devices[i].persistent_key), "device key output");
            printf("}\n");
        }
        REQUIRE(!ferror(stdout), "enumeration output");
        result = 0;
        goto done;
    }
    REQUIRE(demo_number(argv[5], 120, &seconds) && seconds >= 1, "duration 1..120 seconds");
    int64_t cancel_ms = -1;
    if (argc >= 8 && strcmp(argv[7], "-1")) {
        uint32_t value = 0;
        REQUIRE(demo_number(argv[7], 120000, &value), "cancel integer");
        cancel_ms = value;
    }
    const char *behavior = argc == 9 ? argv[8] : "restart";
    REQUIRE(!strcmp(behavior, "stop") || !strcmp(behavior, "restart"), "cancel behavior");
    REQUIRE(cancel_ms < (int64_t)seconds * 1000, "cancel must be within run");
    int n = snprintf(capture_path, sizeof(capture_path), "%s.capture.f32", argv[6]);
    REQUIRE(n > 0 && (size_t)n < sizeof(capture_path), "prefix length");
    n = snprintf(reference_path, sizeof(reference_path), "%s.reference.f32", argv[6]);
    REQUIRE(n > 0 && (size_t)n < sizeof(reference_path), "prefix length");
    n = snprintf(metadata_path, sizeof(metadata_path), "%s.jsonl", argv[6]);
    REQUIRE(n > 0 && (size_t)n < sizeof(metadata_path), "prefix length");
    REQUIRE(demo_load_f32(argv[4], 256u * 1024u * 1024u, &stimulus, &stimulus_size) &&
                stimulus_size % 2 == 0,
            "stereo F32 input size/read");
    for (size_t i = 0; i < stimulus_size; ++i)
        REQUIRE(isfinite(stimulus[i]) && fabsf(stimulus[i]) <= 1,
                "stimulus must be finite within full scale");
    ha_stream_config sc = HA_INIT(ha_stream_config);
    sc.directions = HA_CAPTURE | HA_PLAYBACK;
    sc.negotiation = HA_EXACT_APPLICATION;
    sc.capture_format = (ha_format)HA_FORMAT_INIT(48000, 1, HA_PCM_F32);
    sc.playback_format = (ha_format)HA_FORMAT_INIT(48000, 2, HA_PCM_F32);
    REQUIRE(select_device(&sc.capture_device, backend, argv[2]) &&
                select_device(&sc.playback_device, backend, argv[3]),
            "exact endpoint key required");
    sc.quantum_frames = 480;
    sc.capture_slots = sc.playback_slots = sc.reference_slots = 32;
    ha_diagnostic diagnostic = HA_INIT(ha_diagnostic);
    sc.diagnostic = &diagnostic;
    ha_result opened = ha_stream_open(a.context, &sc, &a.stream);
    if (opened != HA_OK) {
        fprintf(stderr, "stream open: %s operation=%u native=%lld\n", ha_result_string(opened),
                diagnostic.operation, (long long)diagnostic.native_code);
        goto done;
    }
    ha_stream_info stream_info = HA_INIT(ha_stream_info);
    ha_context_status context_info = HA_INIT(ha_context_status);
    REQUIRE(ha_stream_get_info(a.stream, &stream_info) == HA_OK &&
                ha_context_get_status(a.context, &context_info) == HA_OK,
            "identity/backend query");
    a.capacity = (size_t)(seconds + 2) * 48000;
    a.block_capacity = (size_t)(seconds + 2) * 4000;
    a.microphone = (float *)calloc(a.capacity, sizeof(float));
    a.reference = (float *)calloc(a.capacity * 2, sizeof(float));
    a.blocks = (Block *)calloc(a.block_capacity, sizeof(Block));
    a.events = (Event *)calloc(4096, sizeof(Event));
    REQUIRE(a.microphone && a.reference && a.blocks && a.events, "bounded recording allocation");
    float playback[960], virtual_capture[480] = {0}, played[960];
    uint64_t submitted = 0, tick = 0;
    size_t offset = 480;
    int canceled = 0, pending = 0, acknowledged = 0;
    ha_result last_flush = cancel_ms < 0 ? HA_OK : HA_PENDING;
    REQUIRE(ha_stream_start(a.stream) == HA_PENDING, "start");
    uint64_t startup, now;
    ha_stream_status st;
    REQUIRE(demo_now(&startup), "startup clock");
    for (;;) {
        REQUIRE(status(a.stream, &st), "start status");
        if (st.state == HA_RUNNING)
            break;
        REQUIRE(st.state != HA_FAILED && poll_context(a.context) && demo_now(&now) &&
                    now >= startup && now - startup < 5000000000u,
                "start failed/timed out");
        REQUIRE(demo_sleep_ms(1), "start sleep");
    }
    REQUIRE(demo_now(&a.origin), "origin clock");
    a.generation = 1;
    REQUIRE(event(&a, 1, HA_OK), "start event");
    demo_waiter waiter = {0};
    for (;;) {
        REQUIRE(demo_now(&now) && now >= a.origin, "recording clock");
        uint64_t elapsed = now - a.origin;
        if (elapsed >= (uint64_t)seconds * 1000000000u)
            break;
        REQUIRE(poll_context(a.context) && status(a.stream, &st) && st.state == HA_RUNNING,
                "device/stream failed");
        if (cancel_ms >= 0 && !canceled && elapsed >= (uint64_t)cancel_ms * 1000000u) {
            REQUIRE(event(&a, 2, HA_PENDING), "flush request event");
            REQUIRE(ha_stream_flush_playback(a.stream, HA_FLUSH_NATIVE_REQUIRED, &a.generation) ==
                        HA_PENDING,
                    "flush request");
            REQUIRE(event(&a, 3, HA_PENDING), "generation event");
            canceled = pending = 1;
            offset = 480;
        }
        if (pending && st.flush_completed_generation == a.generation &&
            st.flush_result != HA_PENDING) {
            REQUIRE(event(&a, 4, st.flush_result), "flush ack event");
            pending = 0;
            acknowledged = 1;
            last_flush = st.flush_result;
            REQUIRE(last_flush == HA_OK, "native flush unsupported/failed");
        }
        if (!pending && (!canceled || !strcmp(behavior, "restart")))
            for (unsigned writes = 0; writes < 4; ++writes) {
                if (offset == 480) {
                    for (size_t i = 0; i < 960; ++i)
                        playback[i] = stimulus[(submitted * 2 + i) % stimulus_size];
                    offset = 0;
                }
                uint32_t written = 0;
                ha_result r = ha_stream_write(a.stream, playback + offset * 2,
                                              (uint32_t)(480 - offset), a.generation, &written);
                if (r == HA_BUSY || r == HA_WOULD_BLOCK)
                    break;
                REQUIRE(r == HA_OK && written <= 480 - offset, "playback submit");
                submitted += written;
                offset += written;
                if (offset != 480)
                    break;
            }
        if (backend == HA_BACKEND_VIRTUAL) {
            ha_virtual_step_info step = HA_INIT(ha_virtual_step_info);
            step.capture_data = virtual_capture;
            step.capture_frames = step.playback_frames = 480;
            step.advance_ns = 10000000;
            ha_block_info receipt = HA_INIT(ha_block_info);
            REQUIRE(ha_virtual_step(a.stream, &step, played, 480, &receipt) == HA_OK,
                    "virtual self-test step");
        }
        REQUIRE(drain(&a), "recording read/bounds");
        if (backend == HA_BACKEND_VIRTUAL)
            REQUIRE(demo_wait_until(&waiter, a.origin + (++tick) * 10000000u), "virtual pacing");
        else
            REQUIRE(demo_sleep_ms(1), "native control sleep");
    }
    REQUIRE(drain(&a) && status(a.stream, &st), "final read/status");
    ha_stream_status final = st;
    REQUIRE(ha_stream_stop(a.stream) == HA_PENDING && demo_now(&startup), "stop");
    for (;;) {
        REQUIRE(status(a.stream, &st), "stop status");
        if (st.state == HA_STOPPED)
            break;
        REQUIRE(st.state != HA_FAILED && poll_context(a.context) && drain(&a) && demo_now(&now) &&
                    now >= startup && now - startup < 5000000000u,
                "stop failed/timed out");
        REQUIRE(demo_sleep_ms(1), "stop sleep");
    }
    REQUIRE(drain(&a) && event(&a, 5, HA_OK), "stopped event");
    ha_result retired = HA_OK;
    REQUIRE(demo_hail_retire(&a.context, &a.stream, 5000, &retired),
            "native retirement incomplete; storage retained");
    REQUIRE(demo_write_f32(capture_path, a.microphone, a.mc) &&
                demo_write_f32(reference_path, a.reference, a.rc * 2),
            "recording write");
    metadata = demo_fopen(metadata_path, "wb");
    REQUIRE(metadata && write_metadata(metadata, &a, &stream_info, &context_info, &final, backend,
                                       last_flush, canceled, acknowledged, behavior),
            "metadata write");
    printf("Captured %zu frames; reference %zu frames. No acoustic silence or timing calibration "
           "inferred.\n",
           a.mc, a.rc);
    result = 0;
done:
    if (metadata && fclose(metadata))
        result = 1;
    ha_result cleanup = HA_OK;
    if (!demo_hail_retire(&a.context, &a.stream, 5000, &cleanup)) {
        result = 2;
        fprintf(stderr, "Native retirement incomplete; Hail storage retained, result=%d\n",
                cleanup);
    }
    free(stimulus);
    free(a.microphone);
    free(a.reference);
    free(a.blocks);
    free(a.events);
    if (error)
        fprintf(stderr, "%s\n", error);
    return result;
}
