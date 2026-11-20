#include <baffle.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 8)
        return 0;
    bf_pipeline_config c = BF_INIT(bf_pipeline_config);
    c.sample_rate = 16000;
    c.channels = 1u + data[0] % 8u;
    c.reference_channels = 1;
    c.frame_frames = 1u + data[1] % 160u;
    c.stages = data[2] & (BF_STAGE_NOISE | BF_STAGE_AGC | BF_STAGE_VAD);
    c.nonfinite_policy = data[3] % 2u;
    c.noise_strength_db = data[4] % 41u;
    c.agc_target_dbfs = -18;
    size_t bytes;
    if (bf_pipeline_size(&c, &bytes) != BF_OK)
        return 0;
    unsigned char *raw = malloc(bytes + BF_ALIGNMENT);
    if (!raw)
        abort();
    void *p = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    bf_pipeline *s;
    if (bf_pipeline_init(p, bytes, &c, &s) != BF_OK)
        abort();
    float input[160 * 8] = {0}, output[160 * 8];
    size_t n = (size_t)c.frame_frames * c.channels * sizeof(float);
    if (n > size - 8)
        n = size - 8;
    memcpy(input, data + 8, n);
    bf_audio_buffer a = BF_INIT(bf_audio_buffer);
    a.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    a.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    a.format.sample_rate = 16000;
    a.format.channels = c.channels;
    a.format.storage = BF_INTERLEAVED;
    a.format.sample_format = BF_PCM_F32;
    a.format.channel_layout = BF_LAYOUT_DISCRETE;
    a.data = input;
    a.frames = a.capacity_frames = c.frame_frames;
    a.timing.epoch = 1;
    a.timing.clock_domain = 1;
    bf_audio_buffer b = a;
    b.data = output;
    bf_result expected = BF_OK;
    for (size_t i = 0; i < (size_t)c.frame_frames * c.channels; ++i)
        if (isfinite(input[i]) && fabsf(input[i]) > 16)
            expected = BF_ERROR_INVALID;
    if (c.nonfinite_policy == BF_NONFINITE_REJECT)
        for (size_t i = 0; i < (size_t)c.frame_frames * c.channels; ++i)
            if (!isfinite(input[i]))
                expected = BF_ERROR_NONFINITE;
    for (unsigned i = 0; i < 4; ++i) {
        bf_process_info info = BF_INIT(bf_process_info);
        bf_result r = bf_pipeline_process(s, &a, NULL, &b, &info);
        if (r != expected)
            abort();
        if (r == BF_OK)
            for (size_t j = 0; j < (size_t)b.frames * c.channels; ++j)
                if (!isfinite(output[j]))
                    abort();
        a.timing.frame_position += c.frame_frames;
        if (data[5] & (1u << i))
            ++a.timing.epoch;
        if (data[6] & (1u << i))
            (void)bf_pipeline_reset(s, (data[7] & 0x80u) ? BF_STAGE_NOISE : BF_STAGE_ALL);
    }
    bf_process_info info = BF_INIT(bf_process_info);
    (void)bf_pipeline_flush(s, &b, &info);
    if (bf_pipeline_deinit(s) != BF_OK)
        abort();
    free(raw);
    return 0;
}
