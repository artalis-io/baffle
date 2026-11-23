#include <baffle.h>
#include "../tools/common/threads.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <math.h>
static _Atomic unsigned failures;
static int session(void *arg)
{
    unsigned id = *(unsigned *)arg;
    bf_pipeline_config c = BF_INIT(bf_pipeline_config);
    c.sample_rate = 16000;
    c.channels = c.reference_channels = 1;
    c.frame_frames = 160;
    c.stages = BF_STAGE_NOISE | BF_STAGE_AGC | BF_STAGE_VAD;
    bf_library_info library = BF_INIT(bf_library_info);
    if (bf_library_query(&library) != BF_OK) {
        atomic_fetch_add(&failures, 1);
        return 1;
    }
    if (library.features & BF_FEATURE_SPEEXDSP)
        c.stages |= BF_STAGE_AEC;
    c.aec_tail_ms = 200;
    c.noise_strength_db = 18;
    c.agc_target_dbfs = -18;
    size_t bytes = 0;
    void *storage = NULL;
    bf_pipeline *s = NULL;
    if (bf_pipeline_size(&c, &bytes) != BF_OK || bytes > SIZE_MAX - BF_ALIGNMENT) {
        atomic_fetch_add(&failures, 1);
        return 1;
    }
    storage = malloc(bytes + BF_ALIGNMENT);
    if (!storage) {
        atomic_fetch_add(&failures, 1);
        return 1;
    }
    void *p = (void *)(((uintptr_t)storage + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    if (bf_pipeline_init(p, bytes, &c, &s) != BF_OK) {
        free(storage);
        atomic_fetch_add(&failures, 1);
        return 1;
    }
    float input[160], output[160], reference[160];
    for (unsigned i = 0; i < 160; ++i) {
        input[i] = .01f * (float)(id + 1);
        reference[i] = .005f;
    }
    bf_audio_buffer a = BF_INIT(bf_audio_buffer), b = BF_INIT(bf_audio_buffer);
    a.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    a.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
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
    bf_audio_buffer ref = a;
    ref.data = reference;
    ref.timing.clock_domain = 100 + id;
    for (unsigned i = 0; i < 2000; ++i) {
        bf_process_info info = BF_INIT(bf_process_info);
        if (bf_pipeline_process(s, &a, &ref, &b, &info) != BF_OK)
            atomic_fetch_add(&failures, 1);
        for (unsigned j = 0; j < b.frames; ++j)
            if (!isfinite(output[j]))
                atomic_fetch_add(&failures, 1);
        a.timing.frame_position += 160;
        ref.timing.frame_position += 160;
        if (i % 173 == 0 && bf_pipeline_reset(s, BF_STAGE_ALL) != BF_OK)
            atomic_fetch_add(&failures, 1);
    }
    if (bf_pipeline_deinit(s) != BF_OK)
        atomic_fetch_add(&failures, 1);
    free(storage);
    return 0;
}
int main(void)
{
    demo_thread threads[8];
    unsigned ids[8];
    unsigned started = 0;
    for (unsigned i = 0; i < 8; ++i) {
        ids[i] = i;
        if (demo_thread_start(&threads[i], session, &ids[i])) {
            atomic_fetch_add(&failures, 1);
            break;
        }
        ++started;
    }
    for (unsigned i = 0; i < started; ++i)
        if (demo_thread_join(&threads[i], NULL))
            return 1;
    return atomic_load(&failures) ? 1 : 0;
}
