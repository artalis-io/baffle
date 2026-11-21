#include <baffle.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
static float capture[160 * BF_MAX_CHANNELS], reference[160 * 2], output[160 * BF_MAX_CHANNELS];
static bf_audio_buffer buffer(float *data, uint32_t channels)
{
    bf_audio_buffer b = BF_INIT(bf_audio_buffer);
    b.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    b.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    b.timing.uncertainty_ns = UINT64_MAX;
    b.format.sample_rate = 16000;
    b.format.channels = channels;
    b.format.sample_format = BF_PCM_F32;
    b.format.storage = BF_INTERLEAVED;
    b.format.channel_layout = BF_LAYOUT_DISCRETE;
    b.frames = b.capacity_frames = 160;
    b.data = data;
    return b;
}
static void reference_isolation(void)
{
    /* Render-only transitions cannot reset near-leg NS/AGC/VAD state. Disable
     * AEC to isolate that contract from legitimate changes in its residual. */
    bf_pipeline_config config = BF_INIT(bf_pipeline_config);
    config.sample_rate = 16000; config.channels = config.reference_channels = 1;
    config.frame_frames = 160; config.stages = BF_STAGE_NOISE | BF_STAGE_AGC | BF_STAGE_VAD;
    config.noise_strength_db = 18; config.agc_target_dbfs = -18;
    size_t bytes;
    CHECK(bf_pipeline_size(&config, &bytes) == BF_OK);
    void *raw[2]; bf_pipeline *states[2]; float result[2][160];
    for (unsigned k = 0; k < 2; ++k) {
        raw[k] = malloc(bytes + BF_ALIGNMENT); CHECK(raw[k]);
        void *aligned = (void *)(((uintptr_t)raw[k] + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
        CHECK(bf_pipeline_init(aligned, bytes, &config, &states[k]) == BF_OK);
    }
    bf_audio_buffer in = buffer(capture, 1), ref = buffer(reference, 1);
    in.timing.stream_id = 1; in.timing.clock_domain = 11; in.timing.epoch = 1;
    ref.timing.stream_id = 2; ref.timing.clock_domain = 22; ref.timing.epoch = 1;
    for (unsigned frame = 0; frame < 30; ++frame) {
        in.timing.frame_position = ref.timing.frame_position = (uint64_t)frame * 160;
        for (unsigned i = 0; i < 160; ++i) capture[i] = .03f * (float)sin((frame * 160u + i) * .13);
        bf_process_info info[2];
        for (unsigned k = 0; k < 2; ++k) {
            bf_audio_buffer out = buffer(result[k], 1);
            bf_audio_buffer far = ref;
            if (k && frame >= 10) { far.timing.epoch = 2; far.timing.generation = 2; }
            if (k && frame == 10) far.timing.flags = BF_AUDIO_DISCONTINUITY;
            info[k] = (bf_process_info)BF_INIT(bf_process_info);
            CHECK(bf_pipeline_process(states[k], &in, k && frame >= 11 && frame <= 13 ? NULL : &far,
                                      &out, &info[k]) == BF_OK);
            CHECK(info[k].priming == (frame == 0) && info[k].discarded_frames == 0);
        }
        CHECK(info[0].vad.active == info[1].vad.active && info[0].level.gain == info[1].level.gain);
        if (frame) CHECK(!memcmp(result[0], result[1], sizeof(result[0])));
    }
    in.timing.frame_position += 160; in.timing.epoch = 2;
    bf_audio_buffer out = buffer(result[1], 1);
    bf_process_info info = BF_INIT(bf_process_info);
    CHECK(bf_pipeline_process(states[1], &in, NULL, &out, &info) == BF_OK);
    CHECK(info.priming && info.discarded_frames == 160 && !out.frames);
    CHECK(info.flags & BF_AUDIO_DISCONTINUITY);
    for (unsigned k = 0; k < 2; ++k) { CHECK(bf_pipeline_deinit(states[k]) == BF_OK); free(raw[k]); }
}
int main(void)
{
    reference_isolation();
    bf_library_info library = BF_INIT(bf_library_info);
    CHECK(bf_library_query(&library) == BF_OK);
    for (uint32_t channels = 1; channels <= BF_MAX_CHANNELS; channels *= 2)
        for (uint32_t mask = 0; mask <= BF_STAGE_ALL; ++mask) {
            if (mask & BF_STAGE_AEC && (!(library.features & BF_FEATURE_SPEEXDSP) || channels > 8))
                continue;
            bf_pipeline_config c = BF_INIT(bf_pipeline_config);
            bf_pipeline *s;
            size_t bytes;
            c.sample_rate = 16000;
            c.channels = channels;
            c.reference_channels = 2;
            c.frame_frames = 160;
            c.stages = mask;
            c.aec_tail_ms = 200;
            c.noise_strength_db = 0;
            c.agc_target_dbfs = -18;
            CHECK(bf_pipeline_size(&c, &bytes) == BF_OK);
            unsigned char *base = malloc(bytes + BF_ALIGNMENT);
            CHECK(base);
            void *memory =
                (void *)(((uintptr_t)base + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
            CHECK(bf_pipeline_init(memory, bytes, &c, &s) == BF_OK);
            bf_audio_buffer in = buffer(capture, channels), ref = buffer(reference, 2),
                            out = buffer(output, channels);
            in.timing.stream_id = 4;
            in.timing.clock_domain = 11;
            in.timing.epoch = 1;
            in.timing.flags = BF_TIME_HOST_VALID;
            ref.timing = in.timing;
            ref.timing.clock_domain = 12;
            ref.timing.generation = 1;
            uint64_t emitted = 0;
            for (unsigned frame = 0; frame < 20; ++frame) {
                bf_process_info info = BF_INIT(bf_process_info);
                for (unsigned i = 0; i < 160; ++i)
                    for (unsigned ch = 0; ch < channels; ++ch)
                        capture[(size_t)i * channels + ch] = .01f * (float)(ch + 1);
                in.timing.frame_position = ref.timing.frame_position = (uint64_t)frame * 160;
                in.timing.host_time_ns = ref.timing.host_time_ns =
                    1000000000u + (uint64_t)frame * 10000000u;
                CHECK(bf_pipeline_process(s, &in, &ref, &out, &info) == BF_OK);
                CHECK(info.consumed_frames == 160 &&
                      info.algorithmic_delay_frames == ((mask & BF_STAGE_NOISE) ? 160u : 0u));
                if (out.frames) {
                    CHECK(out.timing.epoch == 1 && out.timing.clock_domain == 11 &&
                          out.timing.uncertainty_ns == UINT64_MAX);
                    CHECK(out.timing.frame_position == emitted);
                    emitted += out.frames;
                } else
                    CHECK(mask & BF_STAGE_NOISE && frame == 0 &&
                          !(out.timing.flags & BF_TIME_HOST_VALID));
                for (size_t i = 0; i < (size_t)out.frames * channels; ++i)
                    CHECK(isfinite(output[i]));
            }
            bf_process_info info = BF_INIT(bf_process_info);
            CHECK(bf_pipeline_flush(s, &out, &info) == BF_OK);
            emitted += out.frames;
            CHECK(emitted == 3200);
            CHECK(bf_pipeline_flush(s, &out, &info) == BF_OK && !out.frames);
            CHECK(bf_pipeline_process(s, &in, &ref, &out, &info) == BF_ERROR_STATE);
            CHECK(bf_pipeline_reset(s, BF_STAGE_ALL) == BF_OK);
            capture[10] = NAN;
            CHECK(bf_pipeline_process(s, &in, &ref, &out, &info) == BF_ERROR_NONFINITE);
            capture[10] = 0;
            ++in.timing.epoch;
            in.timing.flags |= BF_AUDIO_DISCONTINUITY;
            CHECK(bf_pipeline_process(s, &in, NULL, &out, &info) == BF_OK);
            if (mask & BF_STAGE_AEC)
                CHECK(info.flags & BF_REFERENCE_MISSING);
            /* Independent capture epoch reset discards exactly the NS overlap. */
            ++in.timing.epoch;
            CHECK(bf_pipeline_process(s, &in, NULL, &out, &info) == BF_OK);
            CHECK(info.discarded_frames == ((mask & BF_STAGE_NOISE) ? 160u : 0u));
            if (!mask) {
                capture[0] = 2;
                out.format.sample_format = BF_PCM_S16;
                CHECK(bf_pipeline_process(s, &in, NULL, &out, &info) == BF_OK);
                CHECK((info.flags & BF_AUDIO_CLIPPED) && (out.timing.flags & BF_AUDIO_CLIPPED));
                capture[0] = 0;
            }
            CHECK(bf_pipeline_deinit(s) == BF_OK);
            CHECK(bf_pipeline_process(s, &in, &ref, &out, &info) == BF_ERROR_INVALID);
            c.nonfinite_policy = BF_NONFINITE_ZERO;
            CHECK(bf_pipeline_init(memory, bytes, &c, &s) == BF_OK);
            out.format.sample_format = BF_PCM_F32;
            capture[0] = INFINITY;
            reference[0] = -INFINITY;
            CHECK(bf_pipeline_process(s, &in, &ref, &out, &info) == BF_OK);
            CHECK(info.flags & BF_AUDIO_SANITIZED);
            if (!out.frames)
                CHECK(bf_pipeline_flush(s, &out, &info) == BF_OK);
            CHECK(out.frames == 160 && (out.timing.flags & BF_AUDIO_SANITIZED));
            for (unsigned i = 0; i < out.frames * channels; ++i)
                CHECK(isfinite(output[i]));
            capture[0] = reference[0] = 0;
            CHECK(bf_pipeline_deinit(s) == BF_OK);
            free(base);
        }
    puts("Pipeline stage combinations, channels, timing, exact EOS drain, reset and missing "
         "references passed");
    return 0;
}
