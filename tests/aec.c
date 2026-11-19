#include <baffle.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
static float far_signal[400000];
int main(void)
{
    bf_aec_config c = BF_INIT(bf_aec_config);
    bf_aec *s;
    size_t bytes;
    bf_audio_timing near_time = BF_INIT(bf_audio_timing), far_time = BF_INIT(bf_audio_timing);
    bf_aec_info info = BF_INIT(bf_aec_info);
    c.sample_rate = 16000;
    c.frame_frames = 160;
    c.capture_channels = c.reference_channels = 1;
    c.tail_ms = 200;
    c.backend = BF_AEC_SPEEXDSP;
    bf_library_info library = BF_INIT(bf_library_info);
    CHECK(bf_library_query(&library) == BF_OK);
    if (!(library.features & BF_FEATURE_SPEEXDSP)) {
        CHECK(bf_aec_size(&c, &bytes) == BF_ERROR_UNSUPPORTED);
        puts("AEC backend explicitly unavailable in CPU-core-only build");
        return 0;
    }
    CHECK(bf_aec_size(&c, &bytes) == BF_OK);
    void *base = malloc(bytes + BF_ALIGNMENT);
    CHECK(base);
    void *memory =
        (void *)(((uintptr_t)base + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    CHECK(bf_aec_init(memory, bytes, &c, &s) == BF_OK);
    near_time.stream_id = 1;
    near_time.clock_domain = 11;
    near_time.epoch = 1;
    far_time = near_time;
    far_time.clock_domain = 12;
    far_time.generation = 1;
    uint32_t seed = 7;
    for (unsigned i = 0; i < 400000; ++i) {
        seed = 1664525u * seed + 1013904223u;
        far_signal[i] = .2f * ((float)(seed >> 8) / 8388608.0f - 1);
    }
    float near[160], out[160];
    double echo_power = 0, residual = 0, projection = 0, near_power = 0, change_echo = 0,
           change_residual = 0;
    uint32_t first_delay = UINT32_MAX;
    for (unsigned frame = 0; frame < 2200; ++frame) {
        unsigned begin = frame * 160;
        for (unsigned i = 0; i < 160; ++i) {
            unsigned at = begin + i, delay = frame < 1200 ? 32 : 83;
            float echo = at >= delay ? .6f * far_signal[at - delay] : 0;
            if (at >= delay + 61)
                echo -= .15f * far_signal[at - delay - 61];
            float voice = frame >= 800 && frame < 1200
                              ? .12f * (float)sin(2 * 3.141592653589793 * 230 * at / 16000)
                              : 0;
            near[i] = echo + voice;
            if (frame >= 500 && frame < 800)
                echo_power += (double)echo * echo;
            if (frame >= 1800)
                change_echo += (double)echo * echo;
        }
        near_time.frame_position = far_time.frame_position = begin;
        CHECK(bf_aec_process(s, near, &near_time, far_signal + begin, &far_time, out, &info) ==
              BF_OK);
        CHECK(info.backend_arena_used <= info.backend_arena_capacity && !info.resets);
        if (frame == 799)
            CHECK(bf_aec_estimate_delay(s, &first_delay) == BF_OK && first_delay >= 30 &&
                  first_delay <= 34);
        for (unsigned i = 0; i < 160; ++i) {
            CHECK(isfinite(out[i]));
            if (frame >= 500 && frame < 800)
                residual += (double)out[i] * out[i];
            if (frame >= 800 && frame < 1200) {
                double voice = .12 * sin(2 * 3.141592653589793 * 230 * (begin + i) / 16000);
                projection += out[i] * voice;
                near_power += voice * voice;
            }
            if (frame >= 1800)
                change_residual += (double)out[i] * out[i];
        }
    }
    double erle = 10 * log10(echo_power / fmax(residual, 1e-20));
    double recovery = 10 * log10(change_echo / fmax(change_residual, 1e-20));
    printf("AEC synthetic ERLE_db=%.3f double_talk_voice_projection=%.5f path_change_ERLE_db=%.3f "
           "dominant_delay=%u caller_bytes=%zu backend_used=%llu\n",
           erle, projection / near_power, recovery, first_delay, bytes,
           (unsigned long long)info.backend_arena_used);
    CHECK(erle > 20 && projection / near_power > .8 && projection / near_power < 1.2 &&
          recovery > 15);
    uint32_t changed_delay;
    CHECK(bf_aec_estimate_delay(s, &changed_delay) == BF_OK && changed_delay >= 81 &&
          changed_delay <= 85);
    near_time.frame_position += 160;
    far_time.frame_position += 160;
    ++far_time.generation;
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) == BF_OK &&
          (info.flags & BF_REFERENCE_GENERATION_CHANGED) && !info.resets);
    near_time.frame_position += 160;
    far_time.frame_position += 160;
    CHECK(bf_aec_process(s, near, &near_time, NULL, &far_time, out, &info) == BF_OK &&
          (info.flags & BF_REFERENCE_MISSING) && !memcmp(out, near, sizeof(out)));
    near_time.frame_position += 160;
    far_time.frame_position += 160;
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) == BF_OK &&
          info.resets == 1);
    CHECK(bf_aec_reset(s) == BF_OK);
    memset(near, 0, sizeof(near));
    memset(far_signal, 0, 160 * sizeof(float));
    near[0] = far_signal[0] = -1;
    near[1] = far_signal[1] = 32767.0f / 32768;
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) == BF_OK &&
          !(info.flags & BF_AUDIO_CLIPPED));
    near_time.frame_position += 160;
    far_time.frame_position += 160;
    near[0] = far_signal[0] = 1;
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) == BF_OK &&
          (info.flags & BF_AUDIO_CLIPPED));
    near[0] = NAN;
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) ==
          BF_ERROR_NONFINITE);
    CHECK(bf_aec_deinit(s) == BF_OK);
    free(base);
    /* Exercise worst supported channel/tail pool bound, not acoustic quality. */
    c.sample_rate = 48000;
    c.frame_frames = 480;
    c.capture_channels = 8;
    c.reference_channels = 2;
    c.tail_ms = 500;
    CHECK(bf_aec_size(&c, &bytes) == BF_OK);
    base = malloc(bytes + BF_ALIGNMENT);
    CHECK(base);
    memory = (void *)(((uintptr_t)base + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    CHECK(bf_aec_init(memory, bytes, &c, &s) == BF_OK);
    CHECK(bf_aec_reset(s) == BF_OK && bf_aec_deinit(s) == BF_OK);
    free(base);
    /* Common rates, both hop durations, tail extremes and channel extremes:
       verify the pinned allocator plan and finite real backend execution. */
    const uint32_t rates[] = {8000, 16000, 44100, 48000};
    float quiet[960 * 8] = {0}, far_quiet[960 * 2] = {0}, processed[960 * 8];
    for (unsigned ri = 0; ri < 4; ++ri)
        for (unsigned hop = 1; hop <= 2; ++hop)
            for (unsigned shape = 0; shape < 4; ++shape) {
                c.sample_rate = rates[ri];
                c.frame_frames = rates[ri] / 100 * hop;
                c.capture_channels = shape & 1u ? 8u : 1u;
                c.reference_channels = shape & 2u ? 2u : 1u;
                c.tail_ms = shape & 1u ? 500u : 40u;
                CHECK(bf_aec_size(&c, &bytes) == BF_OK);
                base = malloc(bytes + BF_ALIGNMENT);
                CHECK(base);
                memory = (void *)(((uintptr_t)base + BF_ALIGNMENT - 1u) &
                                  ~(uintptr_t)(BF_ALIGNMENT - 1u));
                CHECK(bf_aec_init(memory, bytes, &c, &s) == BF_OK);
                near_time.frame_position = far_time.frame_position = 0;
                CHECK(bf_aec_process(s, quiet, &near_time, far_quiet, &far_time, processed,
                                     &info) == BF_OK);
                CHECK(info.backend_arena_used <= info.backend_arena_capacity);
                for (unsigned sample = 0; sample < c.frame_frames * c.capture_channels; ++sample)
                    CHECK(isfinite(processed[sample]));
                CHECK(bf_aec_reset(s) == BF_OK && bf_aec_deinit(s) == BF_OK);
                free(base);
            }
    puts("Mature MDF backend: bounded arena, double-talk, path recovery, missing references and "
         "generations passed");
    return 0;
}
