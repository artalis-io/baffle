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
int main(void)
{
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
            free(base);
        }
    puts("Pipeline stage combinations, channels, timing, exact EOS drain, reset and missing "
         "references passed");
    return 0;
}
