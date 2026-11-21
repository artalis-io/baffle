/* Public APIs only: callback driver and application DSP owner are distinct. */
#include <baffle/hail.h>
#include <hail/virtual.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
static void poll(ha_context *c)
{
    uint32_t n;
    CHECK(ha_context_poll(c, nullptr, 0, &n) == HA_OK);
}
int main()
{
    ha_context_config cc = HA_INIT(ha_context_config);
    cc.backend = HA_BACKEND_VIRTUAL;
    ha_context *context = nullptr;
    ha_stream *stream = nullptr;
    CHECK(ha_context_create(&cc, &context) == HA_OK);
    ha_stream_config sc = HA_INIT(ha_stream_config);
    sc.directions = HA_CAPTURE | HA_PLAYBACK;
    sc.capture_format = HA_FORMAT_INIT(16000, 1, HA_PCM_F32);
    sc.playback_format = HA_FORMAT_INIT(48000, 2, HA_PCM_F32);
    sc.capture_device = HA_INIT(ha_device_selector);
    sc.playback_device = HA_INIT(ha_device_selector);
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
    c.pipeline = BF_INIT(bf_pipeline_config);
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
    size_t bytes;
    CHECK(bf_hail_adapter_size(stream, &c, &bytes) == BF_OK);
    std::vector<unsigned char> storage(bytes + BF_ALIGNMENT);
    void *memory =
        reinterpret_cast<void *>((reinterpret_cast<uintptr_t>(storage.data()) + BF_ALIGNMENT - 1u) &
                                 ~static_cast<uintptr_t>(BF_ALIGNMENT - 1u));
    bf_hail_adapter *adapter = nullptr;
    CHECK(bf_hail_adapter_init(memory, bytes, stream, &c, &adapter) == BF_OK);
    CHECK(ha_stream_start(stream) == HA_PENDING);
    poll(context);
    std::atomic<bool> ready{false}, done{false};
    std::thread callbacks([&] {
        float capture[160], reference[960] = {0}, rendered[960];
        for (auto &x : capture)
            x = .02f;
        while (!ready.load(std::memory_order_acquire))
            std::this_thread::yield();
        uint64_t generation = 1;
        for (unsigned i = 0; i < 10000; ++i) {
            if (i && i % 127 == 0) {
                CHECK(ha_stream_flush_playback(stream, HA_FLUSH_NATIVE_REQUIRED, &generation) ==
                      HA_PENDING);
                poll(context);
                poll(context);
                poll(context);
                ha_stream_status status = HA_INIT(ha_stream_status);
                CHECK(ha_stream_get_status(stream, &status) == HA_OK &&
                      status.flush_result == HA_OK);
            }
            uint32_t written = 0;
            CHECK(ha_stream_write(stream, reference, 480, generation, &written) == HA_OK &&
                  written == 480);
            ha_virtual_step_info step = HA_INIT(ha_virtual_step_info);
            step.capture_data = capture;
            step.capture_frames = 160;
            step.playback_frames = 480;
            step.advance_ns = 10000000;
            ha_block_info receipt = HA_INIT(ha_block_info);
            CHECK(ha_virtual_step(stream, &step, rendered, 480, &receipt) == HA_OK);
            std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });
    float pcm[160];
    bf_audio_buffer out = BF_INIT(bf_audio_buffer);
    out.format = BF_INIT(bf_pcm_format);
    out.timing = BF_INIT(bf_audio_timing);
    out.format.sample_rate = 16000;
    out.format.channels = 1;
    out.format.sample_format = BF_PCM_F32;
    out.format.storage = BF_INTERLEAVED;
    out.format.channel_layout = BF_LAYOUT_MONO;
    out.data = pcm;
    out.capacity_frames = 160;
    ready.store(true, std::memory_order_release);
    uint64_t emitted = 0, overlap = 0, epoch = 0, last = 0;
    unsigned drained = 0;
    for (unsigned calls = 0; calls < 1000000; ++calls) {
        bool finished = done.load(std::memory_order_acquire);
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
                CHECK(std::isfinite(pcm[i]));
        }
        if (finished && ++drained == 256)
            break;
        std::this_thread::yield();
    }
    callbacks.join();
    CHECK(overlap && emitted);
    bf_hail_status status = BF_INIT(bf_hail_status);
    CHECK(bf_hail_adapter_status(adapter, &status) == BF_OK);
    CHECK(status.capture_pending_frames < 192 && status.reference_pending_frames <= 4000);
    CHECK(ha_stream_stop(stream) == HA_PENDING);
    poll(context);
    bf_process_info info = BF_INIT(bf_process_info);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(bf_hail_adapter_flush(adapter, &out, &info) == BF_OK);
        if (!out.frames)
            break;
    }
    CHECK(!out.frames);
    CHECK(bf_hail_adapter_deinit(adapter) == BF_OK);
    CHECK(ha_stream_close_begin(stream) == HA_PENDING);
    poll(context);
    CHECK(ha_stream_close_finish(&stream) == HA_OK);
    CHECK(ha_context_destroy(&context) == HA_OK);
    std::printf("PASS concurrent installed Hail callbacks/DSP: overlap_pumps=%llu emitted=%llu "
                "bounded_bytes=%zu\n",
                static_cast<unsigned long long>(overlap), static_cast<unsigned long long>(emitted),
                bytes);
    return 0;
}
