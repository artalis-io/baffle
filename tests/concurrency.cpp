#include <baffle.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdlib>
#include <cmath>
static std::atomic<unsigned> failures{0};
static void session(unsigned id)
{
    bf_pipeline_config c = BF_INIT(bf_pipeline_config);
    c.sample_rate = 16000;
    c.channels = 1;
    c.reference_channels = 1;
    c.frame_frames = 160;
    c.stages = BF_STAGE_NOISE | BF_STAGE_AGC | BF_STAGE_VAD;
    c.noise_strength_db = 18;
    c.agc_target_dbfs = -18;
    size_t bytes = 0;
    if (bf_pipeline_size(&c, &bytes) != BF_OK) {
        ++failures;
        return;
    }
    std::vector<unsigned char> storage(bytes + BF_ALIGNMENT);
    void *p =
        reinterpret_cast<void *>((reinterpret_cast<uintptr_t>(storage.data()) + BF_ALIGNMENT - 1u) &
                                 ~static_cast<uintptr_t>(BF_ALIGNMENT - 1u));
    bf_pipeline *s = nullptr;
    if (bf_pipeline_init(p, bytes, &c, &s) != BF_OK) {
        ++failures;
        return;
    }
    float input[160], output[160];
    for (unsigned i = 0; i < 160; ++i)
        input[i] = .01f * static_cast<float>(id + 1);
    bf_audio_buffer a = BF_INIT(bf_audio_buffer), b = BF_INIT(bf_audio_buffer);
    a.format = BF_INIT(bf_pcm_format);
    a.timing = BF_INIT(bf_audio_timing);
    a.format.sample_rate = 16000;
    a.format.channels = 1;
    a.format.sample_format = BF_PCM_F32;
    a.format.storage = BF_INTERLEAVED;
    a.format.channel_layout = BF_LAYOUT_MONO;
    a.frames = a.capacity_frames = 160;
    a.data = input;
    a.timing.stream_id = id + 1;
    a.timing.clock_domain = id + 1;
    a.timing.epoch = 1;
    b = a;
    b.data = output;
    for (unsigned i = 0; i < 2000; ++i) {
        bf_process_info info = BF_INIT(bf_process_info);
        if (bf_pipeline_process(s, &a, nullptr, &b, &info) != BF_OK)
            ++failures;
        for (unsigned j = 0; j < b.frames; ++j)
            if (!std::isfinite(output[j]))
                ++failures;
        a.timing.frame_position += 160;
        if (i % 173 == 0 && bf_pipeline_reset(s, BF_STAGE_ALL) != BF_OK)
            ++failures;
    }
    if (bf_pipeline_deinit(s) != BF_OK)
        ++failures;
}
int main()
{
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 8; ++i)
        threads.emplace_back(session, i);
    for (auto &t : threads)
        t.join();
    return failures.load() ? 1 : 0;
}
