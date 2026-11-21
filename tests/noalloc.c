#include <baffle.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned guarded, violations;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
void *__wrap_malloc(size_t n)
{
    if (guarded)
        ++violations;
    return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t s)
{
    if (guarded)
        ++violations;
    return __real_calloc(n, s);
}
void *__wrap_realloc(void *p, size_t n)
{
    if (guarded)
        ++violations;
    return __real_realloc(p, n);
}
void __wrap_free(void *p)
{
    if (guarded)
        ++violations;
    __real_free(p);
}
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    bf_library_info l = BF_INIT(bf_library_info);
    CHECK(bf_library_query(&l) == BF_OK);
    bf_pipeline_config c = BF_INIT(bf_pipeline_config);
    c.sample_rate = 16000;
    c.channels = 2;
    c.reference_channels = 2;
    c.frame_frames = 160;
    c.stages = BF_STAGE_ALL;
    if (!(l.features & BF_FEATURE_SPEEXDSP))
        c.stages &= ~BF_STAGE_AEC;
    c.aec_tail_ms = 200;
    c.noise_strength_db = 18;
    c.agc_target_dbfs = -18;
    size_t bytes;
    CHECK(bf_pipeline_size(&c, &bytes) == BF_OK);
    unsigned char *raw = malloc(bytes + BF_ALIGNMENT);
    CHECK(raw);
    void *p = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    bf_pipeline *s;
    CHECK(bf_pipeline_init(p, bytes, &c, &s) == BF_OK);
    float pcm[320] = {0}, reference[320] = {0}, out[320];
    bf_audio_buffer a = BF_INIT(bf_audio_buffer);
    a.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    a.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    a.format.sample_rate = 16000;
    a.format.channels = 2;
    a.format.sample_format = BF_PCM_F32;
    a.format.storage = BF_INTERLEAVED;
    a.format.channel_layout = BF_LAYOUT_STEREO;
    a.data = pcm;
    a.frames = a.capacity_frames = 160;
    a.timing.epoch = 1;
    a.timing.clock_domain = 1;
    bf_audio_buffer b = a, r = a;
    b.data = out;
    r.data = reference;
    r.timing.clock_domain = 2;
    guarded = 1;
    for (unsigned i = 0; i < 1000; ++i) {
        bf_process_info info = BF_INIT(bf_process_info);
        CHECK(bf_pipeline_process(s, &a, &r, &b, &info) == BF_OK);
        a.timing.frame_position += 160;
        r.timing.frame_position += 160;
        if (i % 97 == 0)
            CHECK(bf_pipeline_reset(s, BF_STAGE_ALL) == BF_OK);
    }
    guarded = 0;
    CHECK(!violations);
    CHECK(bf_pipeline_deinit(s) == BF_OK);
    free(raw);
    puts("No wrapped heap calls during processing/reset (including optional AEC)");
    return 0;
}
