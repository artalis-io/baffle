/* An independent installed-public-API consumer, never a private Hail harness. */
#include <baffle/hail.h>
#include <hail/virtual.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static float capture[512 * BF_MAX_CHANNELS], render[512 * BF_MAX_CHANNELS],
    played[512 * BF_MAX_CHANNELS], processed[960 * BF_MAX_CHANNELS];
static void poll(ha_context *c)
{
    uint32_t n;
    CHECK(ha_context_poll(c, NULL, 0, &n) == HA_OK);
}
static ha_stream_status status(ha_stream *s)
{
    ha_stream_status v = HA_INIT(ha_stream_status);
    CHECK(ha_stream_get_status(s, &v) == HA_OK);
    return v;
}
static float sample(uint64_t frame, uint32_t channel)
{
    return .05f + (float)channel * .001f + (float)(frame % 256u) / 4096;
}
static bf_audio_buffer output(uint32_t rate, uint32_t channels)
{
    bf_audio_buffer out = BF_INIT(bf_audio_buffer);
    out.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    out.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    out.format.sample_rate = rate;
    out.format.channels = channels;
    out.format.channel_layout = BF_LAYOUT_DISCRETE;
    out.format.sample_format = BF_PCM_F32;
    out.format.storage = BF_INTERLEAVED;
    out.data = processed;
    out.capacity_frames = rate / 100u;
    return out;
}
static void pump(bf_hail_adapter *a, uint32_t rate, uint32_t channels, int exact)
{
    bf_audio_buffer out = output(rate, channels);
    bf_process_info pi = BF_INIT(bf_process_info);
    CHECK(bf_hail_adapter_pump(a, &out, &pi) == BF_OK);
    for (uint32_t i = 0; i < out.frames; ++i)
        for (uint32_t c = 0; c < channels; ++c) {
            float x = processed[(size_t)i * channels + c];
            CHECK(isfinite(x));
            if (exact)
                CHECK(fabsf(x - sample(out.timing.frame_position + i, c)) < 2e-6f);
        }
    if (out.frames)
        CHECK(out.timing.clock_domain && out.timing.epoch && out.timing.stream_id);
}
typedef struct fixture_clock {
    uint32_t capture_rate, playback_rate;
    int32_t ppm;
    uint64_t host_remainder, playback_remainder;
} fixture_clock;
static uint32_t demand(fixture_clock *d, uint32_t capture_frames)
{
    uint64_t denominator = (uint64_t)d->capture_rate * (uint32_t)(1000000 + d->ppm);
    d->playback_remainder +=
        (uint64_t)capture_frames * d->playback_rate * (uint32_t)(1000000 - d->ppm);
    uint32_t n = (uint32_t)(d->playback_remainder / denominator);
    d->playback_remainder %= denominator;
    return n;
}
static void step(ha_stream *s, fixture_clock *driver, uint64_t *position, uint32_t cap,
                 uint32_t render_frames, uint32_t channels, uint32_t ref_channels,
                 uint64_t generation, int write, uint32_t flags)
{
    for (uint32_t i = 0; i < cap; ++i)
        for (uint32_t c = 0; c < channels; ++c)
            capture[(size_t)i * channels + c] = sample(*position + i, c);
    for (uint32_t i = 0; i < render_frames; ++i)
        for (uint32_t c = 0; c < ref_channels; ++c)
            render[(size_t)i * ref_channels + c] =
                .01f * (float)(generation % 16u + 1u) + (float)c * .001f;
    if (write && render_frames) {
        uint32_t n;
        CHECK(ha_stream_write(s, render, render_frames, generation, &n) == HA_OK &&
              n == render_frames);
    }
    ha_virtual_step_info v = HA_INIT(ha_virtual_step_info);
    ha_block_info b = HA_INIT(ha_block_info);
    v.capture_frames = cap;
    v.playback_frames = render_frames;
    v.capture_data = capture;
    uint64_t denominator = (uint64_t)driver->capture_rate * (uint32_t)(1000000 + driver->ppm);
    driver->host_remainder += (uint64_t)cap * UINT64_C(1000000000000000);
    v.advance_ns = driver->host_remainder / denominator;
    driver->host_remainder %= denominator;
    v.flags = flags;
    CHECK(ha_virtual_step(s, &v, played, render_frames, &b) == HA_OK);
    *position += cap;
    if (write)
        CHECK(!memcmp(render, played, (size_t)render_frames * ref_channels * sizeof(float)));
}
static void scenario(uint32_t rate, uint32_t playback_rate, uint32_t channels,
                     uint32_t ref_channels, int32_t ppm, uint32_t steps, int aec)
{
    ha_context_config cc = HA_INIT(ha_context_config);
    ha_stream_config sc = HA_INIT(ha_stream_config);
    ha_device_selector select = HA_INIT(ha_device_selector);
    ha_context *c;
    ha_stream *s;
    ha_format cf = HA_FORMAT_INIT(rate, channels, HA_PCM_F32),
              pf = HA_FORMAT_INIT(playback_rate, ref_channels, HA_PCM_F32);
    cc.backend = HA_BACKEND_VIRTUAL;
    CHECK(ha_context_create(&cc, &c) == HA_OK);
    sc.directions = HA_CAPTURE | HA_PLAYBACK;
    sc.capture_format = cf;
    sc.playback_format = pf;
    sc.capture_device = sc.playback_device = select;
    sc.quantum_frames = 512;
    sc.capture_slots = sc.playback_slots = sc.reference_slots = 16;
    CHECK(ha_stream_open(c, &sc, &s) == HA_OK);
    ha_stream_info hi = HA_INIT(ha_stream_info);
    CHECK(ha_stream_get_info(s, &hi) == HA_OK);
    ha_virtual_config clocks = HA_INIT(ha_virtual_config);
    clocks.capture_drift_ppm = ppm;
    clocks.playback_drift_ppm = -ppm;
    clocks.host_time_ns = 1000000000;
    clocks.flush_delay_polls = 2;
    CHECK(ha_virtual_configure(s, &clocks) == HA_OK);
    bf_hail_config config = BF_INIT(bf_hail_config);
    config.pipeline = (bf_pipeline_config)BF_INIT(bf_pipeline_config);
    config.pipeline.sample_rate = rate;
    config.pipeline.channels = channels;
    config.pipeline.reference_channels = ref_channels;
    config.pipeline.frame_frames = rate / 100u;
    config.pipeline.stages = BF_STAGE_NOISE | BF_STAGE_VAD | (aec ? BF_STAGE_AEC : 0u);
    if (aec)
        config.pipeline.stages |= BF_STAGE_AGC;
    config.pipeline.noise_strength_db = aec ? 18.0f : 0.0f;
    config.pipeline.aec_tail_ms = 200;
    config.pipeline.agc_target_dbfs = -18;
    config.reference_history_frames = rate / 4u;
    config.max_alignment_uncertainty_ns = 1000;
    size_t bytes;
    bf_hail_adapter *a;
    CHECK(bf_hail_adapter_size(s, &config, &bytes) == BF_OK);
    unsigned char *base = malloc(bytes + BF_ALIGNMENT);
    CHECK(base);
    void *memory =
        (void *)(((uintptr_t)base + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    CHECK(bf_hail_adapter_init(memory, bytes, s, &config, &a) == BF_OK);
    CHECK(ha_stream_start(s) == HA_PENDING);
    poll(c);
    uint64_t position = 0, generation = 1;
    fixture_clock driver = {rate, playback_rate, ppm, 0, 0};
    for (uint32_t i = 0; i < steps; ++i) {
        uint32_t frames = 16u + i % 4u * 16u;
        uint32_t render_frames = demand(&driver, frames);
        step(s, &driver, &position, frames, render_frames, channels, ref_channels, generation, 1,
             0);
        for (unsigned j = 0; j < 4; ++j)
            pump(a, rate, channels, !aec);
    }
    bf_hail_status bs = BF_INIT(bf_hail_status);
    CHECK(bf_hail_adapter_status(a, &bs) == BF_OK);
    CHECK(bs.processed_frames > 0 &&
          bs.last_capture_source.clock_domain != bs.last_reference_source.clock_domain);
    CHECK(bs.last_capture_source.stream_id == hi.stream_id &&
          bs.last_reference_source.stream_id == hi.stream_id);
    CHECK(bs.capture_clock.valid && bs.reference_clock.valid);
    if (aec)
        CHECK(bs.missing_reference_frames < bs.processed_frames / 5u);
    CHECK(fabs(bs.capture_clock.observed_ppm - ppm) < .1 &&
          fabs(bs.reference_clock.observed_ppm + ppm) < .1);
    CHECK(!status(s).capture_lost_frames && !status(s).playback_underrun_frames &&
          !status(s).reference_lost_frames);
    uint64_t epoch = bs.last_capture_source.epoch;
    for (unsigned cancel = 0; cancel < 8; ++cancel) {
        uint64_t old = generation;
        CHECK(ha_stream_flush_playback(s, HA_FLUSH_NATIVE_REQUIRED, &generation) == HA_PENDING);
        uint32_t n = 99;
        CHECK(ha_stream_write(s, render, 1, old, &n) == HA_BUSY && !n);
        for (unsigned j = 0; j < 3; ++j) {
            step(s, &driver, &position, 48, 0, channels, ref_channels, generation, 0, 0);
            pump(a, rate, channels, !aec);
            poll(c);
        }
        CHECK(status(s).flush_result == HA_OK);
        CHECK(ha_stream_write(s, render, 1, old, &n) == HA_STALE_GENERATION && !n);
        for (unsigned j = 0; j < 8; ++j) {
            step(s, &driver, &position, 48, demand(&driver, 48), channels, ref_channels, generation,
                 1, 0);
            pump(a, rate, channels, !aec);
        }
        CHECK(bf_hail_adapter_status(a, &bs) == BF_OK && bs.last_capture_source.epoch == epoch);
    }
    step(s, &driver, &position, 48, demand(&driver, 48), channels, ref_channels, generation, 0, 0);
    for (unsigned j = 0; j < 4; ++j)
        pump(a, rate, channels, !aec);
    CHECK(status(s).playback_underrun_frames > 0);
    for (unsigned j = 0; j < 30; ++j)
        step(s, &driver, &position, 512, 0, channels, ref_channels, generation, 0, 0);
    CHECK(status(s).capture_lost_frames > 0);
    for (unsigned j = 0; j < 50; ++j)
        pump(a, rate, channels, !aec);
    step(s, &driver, &position, 48, demand(&driver, 48), channels, ref_channels, generation, 1,
         HA_VIRTUAL_UNKNOWN_CAPTURE_GAP);
    for (unsigned j = 0; j < 4; ++j)
        pump(a, rate, channels, !aec);
    CHECK(bf_hail_adapter_status(a, &bs) == BF_OK && bs.last_capture_source.epoch > epoch);
    CHECK(ha_stream_stop(s) == HA_PENDING);
    poll(c);
    generation = status(s).playback_generation;
    position = 0;
    CHECK(ha_stream_start(s) == HA_PENDING);
    poll(c);
    for (unsigned j = 0; j < 20; ++j) {
        step(s, &driver, &position, 48, demand(&driver, 48), channels, ref_channels, generation, 1,
             0);
        pump(a, rate, channels, !aec);
    }
    /* EOS does not invent real frames for partial packets or lose the NS tail. */
    bf_audio_buffer tail = output(rate, channels);
    bf_process_info tail_info = BF_INIT(bf_process_info);
    CHECK(ha_stream_stop(s) == HA_PENDING);
    poll(c);
    for (unsigned j = 0; j < 4; ++j) {
        CHECK(bf_hail_adapter_flush(a, &tail, &tail_info) == BF_OK);
        if (!tail.frames)
            break;
    }
    CHECK(!tail.frames && bf_hail_adapter_pump(a, &tail, &tail_info) == BF_ERROR_STATE);
    uint64_t removed, replacement;
    CHECK(ha_virtual_device_set(c, HA_CAPTURE, 0, 1, &removed) == HA_OK);
    CHECK(status(s).state == HA_FAILED);
    CHECK(bf_hail_adapter_deinit(a) == BF_OK);
    free(base);
    CHECK(ha_stream_close_begin(s) == HA_PENDING);
    poll(c);
    CHECK(ha_stream_close_finish(&s) == HA_OK && !s);
    CHECK(ha_virtual_device_set(c, HA_CAPTURE, 1, 0, &replacement) == HA_OK);
    CHECK(ha_stream_open(c, &sc, &s) == HA_PERMISSION_DENIED && !s);
    CHECK(ha_virtual_device_set(c, HA_CAPTURE, 1, 1, &replacement) == HA_OK);
    CHECK(ha_stream_open(c, &sc, &s) == HA_OK);
    ha_stream_info reopened = HA_INIT(ha_stream_info);
    CHECK(ha_stream_get_info(s, &reopened) == HA_OK);
    CHECK(reopened.stream_id != hi.stream_id &&
          reopened.capture_clock_domain != hi.capture_clock_domain);
    /* A session shorter than one DSP hop must not confuse NS priming with EOS. */
    CHECK(bf_hail_adapter_size(s, &config, &bytes) == BF_OK);
    base = malloc(bytes + BF_ALIGNMENT);
    CHECK(base);
    memory = (void *)(((uintptr_t)base + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    CHECK(bf_hail_adapter_init(memory, bytes, s, &config, &a) == BF_OK);
    CHECK(ha_stream_start(s) == HA_PENDING);
    poll(c);
    driver = (fixture_clock){rate, playback_rate, 0, 0, 0};
    position = 0;
    generation = status(s).playback_generation;
    step(s, &driver, &position, 17, demand(&driver, 17), channels, ref_channels, generation, 1, 0);
    for (unsigned j = 0; j < 4; ++j)
        pump(a, rate, channels, !aec);
    tail = output(rate, channels);
    CHECK(bf_hail_adapter_flush(a, &tail, &tail_info) == BF_OK && tail.frames == 17);
    CHECK(tail_info.consumed_frames == 17 && (tail_info.flags & BF_AUDIO_PADDING));
    CHECK(tail.timing.stream_id == reopened.stream_id && tail.timing.frame_position == 0);
    CHECK(tail.timing.flags & BF_AUDIO_PADDING);
    if (!aec)
        for (unsigned i = 0; i < 17; ++i)
            for (unsigned ch = 0; ch < channels; ++ch)
                CHECK(fabsf(processed[(size_t)i * channels + ch] - sample(i, ch)) < 2e-6f);
    CHECK(bf_hail_adapter_flush(a, &tail, &tail_info) == BF_OK && !tail.frames);
    CHECK(bf_hail_adapter_deinit(a) == BF_OK);
    free(base);
    CHECK(ha_stream_close_begin(s) == HA_PENDING);
    poll(c);
    CHECK(ha_stream_close_finish(&s) == HA_OK);
    CHECK(ha_context_destroy(&c) == HA_OK);
    printf("PASS installed Hail duplex rate=%u playback=%u channels=%u/%u drift=%d steps=%u aec=%d "
           "caller_bytes=%zu missing_reference_frames=%llu\n",
           rate, playback_rate, channels, ref_channels, ppm, steps, aec, bytes,
           (unsigned long long)bs.missing_reference_frames);
}
int main(void)
{
    scenario(16000, 48000, 1, 2, 0, 8000, 0);
    scenario(48000, 48000, 4, 2, 1000, 8000, 0);
    scenario(44100, 48000, 8, 2, -1000, 8000, 0);
    scenario(16000, 48000, 32, 32, 100, 2000, 0);
    scenario(16000, 48000, 1, 2, 100, 280000, 0); /* 700 modeled seconds. */
    bf_library_info library = BF_INIT(bf_library_info);
    CHECK(bf_library_query(&library) == BF_OK);
    if (library.features & BF_FEATURE_SPEEXDSP) {
        scenario(16000, 48000, 1, 2, 0, 8000, 1);
        scenario(16000, 48000, 1, 2, 1000, 16000, 1);
        scenario(48000, 48000, 2, 2, -1000, 8000, 1);
        scenario(44100, 48000, 8, 2, 100, 4000, 1);
        scenario(16000, 48000, 1, 2, 100, 280000, 1);
    }
    return 0;
}
