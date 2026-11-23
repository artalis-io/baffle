/* Installed-public-API live source. Device callbacks never invoke application
 * code; this consumer thread owns DSP. Sink pointers are borrowed for one call. */
#include "hail_owner.h"
#include "live_source.h"
#ifndef BF_LIVE_SOURCE_ONLY
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <poll.h>
#include <unistd.h>
#endif
#endif
static bf_audio_buffer buffer(float *data)
{
    bf_audio_buffer b = BF_INIT(bf_audio_buffer);
    b.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    b.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    b.format.sample_rate = 16000;
    b.format.channels = 1;
    b.format.sample_format = BF_PCM_F32;
    b.format.storage = BF_INTERLEAVED;
    b.format.channel_layout = BF_LAYOUT_MONO;
    b.data = data;
    b.frames = b.capacity_frames = 160;
    return b;
}
static bf_audio_timing timing(const ha_block_info *h, uint64_t stream)
{
    bf_audio_timing t = BF_INIT(bf_audio_timing);
    t.stream_id = stream;
    t.clock_domain = h->clock_domain;
    t.epoch = h->clock_epoch;
    t.generation = h->generation;
    t.frame_position = h->first_frame;
    t.device_position = h->device_position;
    t.host_time_ns = h->host_time_ns;
    t.uncertainty_ns = h->uncertainty_ns;
    t.lost_frames = h->lost_frames;
    t.frame_time_numerator = h->frame_time_numerator;
    t.frame_time_denominator = h->frame_time_denominator;
    t.time_remainder = h->time_remainder;
    if (h->flags & HA_TIME_HOST_VALID)
        t.flags |= BF_TIME_HOST_VALID;
    if (h->flags & HA_TIME_DEVICE_VALID)
        t.flags |= BF_TIME_DEVICE_VALID;
    if (h->flags & HA_TIME_ESTIMATED)
        t.flags |= BF_TIME_ESTIMATED;
    if (h->flags & HA_BLOCK_DISCONTINUITY)
        t.flags |= BF_AUDIO_DISCONTINUITY;
    if (h->flags & (HA_BLOCK_GAP | HA_BLOCK_UNKNOWN_GAP))
        t.flags |= BF_AUDIO_GAP;
    if (h->flags & HA_BLOCK_SILENCE)
        t.flags |= BF_AUDIO_SILENCE;
    return t;
}
#define REQUIRE(x, m)                                                                              \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            message = (m);                                                                         \
            goto done;                                                                             \
        }                                                                                          \
    } while (0)
int baffle_live_run(uint32_t backend, const char *key, uint32_t seconds, BaffleLiveSink sink,
                    BaffleLiveStop stop, void *user, char *error, size_t error_capacity, int gain,
                    int feedback)
{
    int result_code = 1;
    const char *message = NULL;
    ha_context *context = NULL;
    ha_stream *stream = NULL;
    bf_pipeline *pipeline = NULL;
    demo_arena arena = {0};
    uint64_t start = 0, now = 0;
    size_t bytes = 0;
    REQUIRE(key && sink && stop && seconds >= 1 && seconds <= 86400 && backend >= 1 && backend <= 5,
            "live source arguments");
    if (error && error_capacity)
        error[0] = 0;
    if (stop(user))
        return 0;
    uint16_t endian = 1;
    REQUIRE(*(unsigned char *)&endian == 1 && demo_now(&now),
            "little-endian monotonic host required");
    ha_context_config cc = HA_INIT(ha_context_config);
    cc.backend = backend;
    REQUIRE(ha_context_create(&cc, &context) == HA_OK, "context create");
    ha_stream_config sc = HA_INIT(ha_stream_config);
    sc.directions = HA_CAPTURE;
    sc.negotiation = HA_EXACT_APPLICATION;
    sc.capture_format = (ha_format)HA_FORMAT_INIT(16000, 1, HA_PCM_F32);
    sc.capture_device = (ha_device_selector)HA_INIT(ha_device_selector);
    if (backend != HA_BACKEND_VIRTUAL) {
        size_t n = strlen(key);
        REQUIRE(n && n < HA_DEVICE_KEY_CAPACITY, "capture key");
        sc.capture_device.kind = HA_SELECT_PERSISTENT_KEY;
        sc.capture_device.key_bytes = (uint32_t)n;
        memcpy(sc.capture_device.key, key, n + 1);
    }
    sc.quantum_frames = 160;
    sc.capture_slots = 32;
    ha_diagnostic diagnostic = HA_INIT(ha_diagnostic);
    sc.diagnostic = &diagnostic;
    ha_result opened = ha_stream_open(context, &sc, &stream);
    if (opened != HA_OK) {
        if (error && error_capacity)
            snprintf(error, error_capacity, "open: %s native=%lld", ha_result_string(opened),
                     (long long)diagnostic.native_code);
        goto done;
    }
    ha_stream_info identity = HA_INIT(ha_stream_info);
    REQUIRE(ha_stream_get_info(stream, &identity) == HA_OK, "stream identity");
    bf_pipeline_config pc = BF_INIT(bf_pipeline_config);
    pc.sample_rate = 16000;
    pc.frame_frames = 160;
    pc.channels = pc.reference_channels = 1;
    pc.stages = BF_STAGE_NOISE | (feedback ? BF_STAGE_VAD : 0) | (gain ? BF_STAGE_AGC : 0);
    pc.noise_strength_db = 18;
    pc.agc_target_dbfs = -18;
    REQUIRE(bf_pipeline_size(&pc, &bytes) == BF_OK &&
                demo_arena_init(&arena, bytes, BF_ALIGNMENT) &&
                bf_pipeline_init(arena.aligned, bytes, &pc, &pipeline) == BF_OK,
            "pipeline init");
    REQUIRE(ha_stream_start(stream) == HA_PENDING && demo_now(&start), "start");
    ha_stream_status status = HA_INIT(ha_stream_status);
    uint32_t events = 0;
    do {
        REQUIRE(ha_context_poll(context, NULL, 0, &events) == HA_OK &&
                    ha_stream_get_status(stream, &status) == HA_OK,
                "start poll/status");
        REQUIRE(status.state != HA_FAILED && demo_now(&now) && now >= start &&
                    now - start < 5000000000u,
                "start failed/timed out");
        if (stop(user)) {
            result_code = 0;
            goto done;
        }
        REQUIRE(demo_sleep_ms(1), "start sleep");
    } while (status.state != HA_RUNNING);
    float input[160] = {0}, output[160] = {0}, part[160] = {0}, previous[160] = {0};
    uint32_t filled = 0;
    int previous_valid = 0;
    bf_audio_buffer in = buffer(input), out = buffer(output);
    REQUIRE(demo_now(&start), "capture clock");
    while (!stop(user)) {
        REQUIRE(demo_now(&now) && now >= start, "capture clock");
        if (now - start >= (uint64_t)seconds * 1000000000u)
            break;
        REQUIRE(ha_context_poll(context, NULL, 0, &events) == HA_OK &&
                    ha_stream_get_status(stream, &status) == HA_OK && status.state == HA_RUNNING,
                "capture poll/state");
        uint32_t n = 0;
        ha_block_info b = HA_INIT(ha_block_info);
        ha_result r = ha_stream_read(stream, part, 160 - filled, &n, &b);
        if (r == HA_WOULD_BLOCK || r == HA_BUSY) {
            REQUIRE(demo_sleep_ms(1), "capture sleep");
            continue;
        }
        REQUIRE(r == HA_OK, "capture read");
        if (!n)
            continue;
        if (b.flags & (HA_BLOCK_GAP | HA_BLOCK_UNKNOWN_GAP | HA_BLOCK_DISCONTINUITY)) {
            filled = 0;
            previous_valid = 0;
            REQUIRE(bf_pipeline_reset(pipeline, BF_STAGE_ALL) == BF_OK, "reset");
        }
        if (!filled)
            in.timing = timing(&b, identity.stream_id);
        REQUIRE(n <= 160 - filled, "capture bounds");
        memcpy(input + filled, part, n * sizeof(float));
        filled += n;
        if (filled != 160)
            continue;
        bf_process_info info = BF_INIT(bf_process_info);
        REQUIRE(bf_pipeline_process(pipeline, &in, NULL, &out, &info) == BF_OK,
                "Baffle processing");
        if (out.frames == 160 && previous_valid) {
            float power = 0, peak = 0;
            for (unsigned i = 0; i < 160; ++i) {
                power += previous[i] * previous[i];
                peak = fmaxf(peak, fabsf(previous[i]));
            }
            BaffleLiveBlock block = {
                previous,           output, out.timing, status.capture_lost_frames, info.vad.active,
                sqrtf(power / 160), peak};
            if (!sink(&block, user))
                break;
        }
        memcpy(previous, input, sizeof(input));
        previous_valid = 1;
        filled = 0;
    }
    result_code = 0;
done:
    if (pipeline && bf_pipeline_deinit(pipeline) != BF_OK) {
        result_code = 1;
        message = "pipeline deinit";
    }
    demo_arena_free(&arena);
    ha_result retired = HA_OK;
    if (!demo_hail_retire(&context, &stream, 5000, &retired)) {
        result_code = 2;
        message = "SDK retirement incomplete; storage retained. Restart application before another "
                  "session.";
    }
    if (message && error && error_capacity)
        snprintf(error, error_capacity, "%s", message);
    return result_code;
}
#ifndef BF_LIVE_SOURCE_ONLY
static int stopped(void *unused)
{
    (void)unused;
#ifdef _WIN32
    DWORD available = 0;
    if (!PeekNamedPipe(GetStdHandle(STD_INPUT_HANDLE), NULL, 0, NULL, &available, NULL))
        return GetLastError() == ERROR_BROKEN_PIPE;
    return available != 0;
#else
    struct pollfd p = {STDIN_FILENO, POLLIN, 0};
    return poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP));
#endif
}
static int pipe_sink(const BaffleLiveBlock *b, void *unused)
{
    (void)unused;
    uint32_t header[4] = {0x564c4642u, 160u, b->timing.flags, b->vad_active};
    uint64_t counters[3] = {b->timing.frame_position, b->timing.epoch, b->capture_lost};
    float levels[2] = {b->rms, b->peak};
    if (fwrite(header, sizeof(header), 1, stdout) != 1 ||
        fwrite(counters, sizeof(counters), 1, stdout) != 1 ||
        fwrite(levels, sizeof(levels), 1, stdout) != 1 ||
        fwrite(b->raw, 160 * sizeof(float), 1, stdout) != 1 ||
        fwrite(b->processed, 160 * sizeof(float), 1, stdout) != 1 || fflush(stdout))
        return 0;
    return 1;
}
int main(int argc, char **argv)
{
    uint32_t backend = 0, seconds = 0;
    char error[512] = {0};
    if (argc != 4 || !demo_number(argv[1], 5, &backend) || !backend ||
        !demo_number(argv[3], 1800, &seconds) || !seconds) {
        fprintf(stderr, "live_capture backend key seconds(1..1800)\n");
        return 1;
    }
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        return 1;
#endif
    int result = baffle_live_run(backend, argv[2], seconds, pipe_sink, stopped, NULL, error,
                                 sizeof(error), 1, 1);
    if (ferror(stdout) && !result)
        result = 1;
    if (result)
        fprintf(stderr, "%s\n", *error ? error : "transcript pipe closed");
    return result;
}
#endif
