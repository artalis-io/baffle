/* Public APIs only. The support header owns test threads, not DSP/device state. */
#include <baffle/hail.h>
#include <hail/virtual.h>
#include "../../tools/common/threads.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static void poll_context(ha_context *c)
{
    uint32_t n;
    CHECK(ha_context_poll(c, NULL, 0, &n) == HA_OK);
}
typedef struct Driver {
    ha_context *context;
    ha_stream *stream;
    _Atomic int ready, done;
} Driver;
static int callbacks(void *arg)
{
    Driver *d = (Driver *)arg;
    float capture[160], reference[960] = {0}, rendered[960];
    for (unsigned j = 0; j < 160; ++j)
        capture[j] = .02f;
    while (!atomic_load_explicit(&d->ready, memory_order_acquire))
        demo_thread_yield();
    uint64_t generation = 1;
    for (unsigned i = 0; i < 10000; ++i) {
        if (i && i % 127 == 0) {
            CHECK(ha_stream_flush_playback(d->stream, HA_FLUSH_NATIVE_REQUIRED, &generation) ==
                  HA_PENDING);
            poll_context(d->context);
            poll_context(d->context);
            poll_context(d->context);
            ha_stream_status status = HA_INIT(ha_stream_status);
            CHECK(ha_stream_get_status(d->stream, &status) == HA_OK &&
                  status.flush_result == HA_OK);
        }
        uint32_t written = 0;
        CHECK(ha_stream_write(d->stream, reference, 480, generation, &written) == HA_OK &&
              written == 480);
        ha_virtual_step_info step = HA_INIT(ha_virtual_step_info);
        step.capture_data = capture;
        step.capture_frames = 160;
        step.playback_frames = 480;
        step.advance_ns = 10000000;
        ha_block_info receipt = HA_INIT(ha_block_info);
        CHECK(ha_virtual_step(d->stream, &step, rendered, 480, &receipt) == HA_OK);
        demo_thread_yield();
    }
    atomic_store_explicit(&d->done, 1, memory_order_release);
    return 0;
}
int main(void)
{
    ha_context_config cc = HA_INIT(ha_context_config);
    cc.backend = HA_BACKEND_VIRTUAL;
    ha_context *context = NULL;
    ha_stream *stream = NULL;
    CHECK(ha_context_create(&cc, &context) == HA_OK);
    ha_stream_config sc = HA_INIT(ha_stream_config);
    sc.directions = HA_CAPTURE | HA_PLAYBACK;
    sc.capture_format = (ha_format)HA_FORMAT_INIT(16000, 1, HA_PCM_F32);
    sc.playback_format = (ha_format)HA_FORMAT_INIT(48000, 2, HA_PCM_F32);
    sc.capture_device = (ha_device_selector)HA_INIT(ha_device_selector);
    sc.playback_device = (ha_device_selector)HA_INIT(ha_device_selector);
    sc.quantum_frames = 512;
    sc.capture_slots = sc.playback_slots = sc.reference_slots = 16;
    CHECK(ha_stream_open(context, &sc, &stream) == HA_OK);
    ha_virtual_config vc = HA_INIT(ha_virtual_config);
    vc.host_time_ns = 1000000000;
    vc.flush_delay_polls = 2;
    CHECK(ha_virtual_configure(stream, &vc) == HA_OK);
    bf_library_info library = BF_INIT(bf_library_info);
    CHECK(bf_library_query(&library) == BF_OK);
    bf_hail_config c = BF_INIT(bf_hail_config);
    c.pipeline = (bf_pipeline_config)BF_INIT(bf_pipeline_config);
    c.pipeline.sample_rate = 16000;
    c.pipeline.channels = 1;
    c.pipeline.reference_channels = 2;
    c.pipeline.frame_frames = 160;
    c.pipeline.stages = BF_STAGE_NOISE | BF_STAGE_AGC | BF_STAGE_VAD;
    if (library.features & BF_FEATURE_SPEEXDSP)
        c.pipeline.stages |= BF_STAGE_AEC;
    c.pipeline.aec_tail_ms = 200;
    c.pipeline.noise_strength_db = 18;
    c.pipeline.agc_target_dbfs = -18;
    c.reference_history_frames = 4000;
    c.max_alignment_uncertainty_ns = 1000;
    size_t bytes = 0;
    CHECK(bf_hail_adapter_size(stream, &c, &bytes) == BF_OK && bytes <= SIZE_MAX - BF_ALIGNMENT);
    void *storage = malloc(bytes + BF_ALIGNMENT);
    CHECK(storage);
    void *memory =
        (void *)(((uintptr_t)storage + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    bf_hail_adapter *adapter = NULL;
    CHECK(bf_hail_adapter_init(memory, bytes, stream, &c, &adapter) == BF_OK);
    CHECK(ha_stream_start(stream) == HA_PENDING);
    poll_context(context);
    Driver driver = {.context = context, .stream = stream};
    atomic_init(&driver.ready, 0);
    atomic_init(&driver.done, 0);
    demo_thread thread;
    CHECK(demo_thread_start(&thread, callbacks, &driver) == 0);
    float pcm[160];
    bf_audio_buffer out = BF_INIT(bf_audio_buffer);
    out.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    out.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    out.format.sample_rate = 16000;
    out.format.channels = 1;
    out.format.sample_format = BF_PCM_F32;
    out.format.storage = BF_INTERLEAVED;
    out.format.channel_layout = BF_LAYOUT_MONO;
    out.data = pcm;
    out.capacity_frames = 160;
    atomic_store_explicit(&driver.ready, 1, memory_order_release);
    uint64_t emitted = 0, overlap = 0, epoch = 0, last = 0;
    unsigned drained = 0;
    for (unsigned calls = 0; calls < 1000000; ++calls) {
        int finished = atomic_load_explicit(&driver.done, memory_order_acquire);
        bf_process_info info = BF_INIT(bf_process_info);
        CHECK(bf_hail_adapter_pump(adapter, &out, &info) == BF_OK);
        if (!finished)
            ++overlap;
        if (out.frames) {
            CHECK(out.timing.stream_id && out.timing.clock_domain && out.timing.epoch);
            if (out.timing.epoch == epoch)
                CHECK(out.timing.frame_position >= last);
            epoch = out.timing.epoch;
            last = out.timing.frame_position + out.frames;
            emitted += out.frames;
            for (unsigned i = 0; i < out.frames; ++i)
                CHECK(isfinite(pcm[i]));
        }
        if (finished && ++drained == 256)
            break;
        demo_thread_yield();
    }
    CHECK(demo_thread_join(&thread, NULL) == 0);
    CHECK(overlap && emitted);
    bf_hail_status status = BF_INIT(bf_hail_status);
    CHECK(bf_hail_adapter_status(adapter, &status) == BF_OK);
    CHECK(status.capture_pending_frames < 192 && status.reference_pending_frames <= 4000);
    CHECK(ha_stream_stop(stream) == HA_PENDING);
    poll_context(context);
    bf_process_info info = BF_INIT(bf_process_info);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(bf_hail_adapter_flush(adapter, &out, &info) == BF_OK);
        if (!out.frames)
            break;
    }
    CHECK(!out.frames);
    CHECK(bf_hail_adapter_deinit(adapter) == BF_OK);
    free(storage);
    CHECK(ha_stream_close_begin(stream) == HA_PENDING);
    poll_context(context);
    CHECK(ha_stream_close_finish(&stream) == HA_OK);
    CHECK(ha_context_destroy(&context) == HA_OK);
    printf("PASS concurrent installed Hail callbacks/DSP: overlap_pumps=%llu emitted=%llu "
           "bounded_bytes=%zu\n",
           (unsigned long long)overlap, (unsigned long long)emitted, bytes);
    return 0;
}
