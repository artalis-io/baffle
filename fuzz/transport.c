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
    uint32_t channels = 1u + data[0] % 32u, frames = data[1];
    float input[256 * 32] = {0}, output[256 * 32];
    size_t n = (size_t)frames * channels * sizeof(float);
    if (n > size - 8)
        n = size - 8;
    memcpy(input, data + 8, n);
    bf_audio_buffer a = BF_INIT(bf_audio_buffer), b = BF_INIT(bf_audio_buffer);
    a.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    a.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    a.format.sample_rate = 8000u + (uint32_t)data[2] * 700u;
    a.format.channels = channels;
    a.format.storage = BF_INTERLEAVED;
    a.format.sample_format = BF_PCM_F32;
    a.format.channel_layout = BF_LAYOUT_DISCRETE;
    a.data = input;
    a.frames = a.capacity_frames = frames;
    b = a;
    b.data = output;
    bf_result r = bf_pcm_convert(&a, &b, data[3] % 3u);
    if (r == BF_OK)
        for (size_t i = 0; i < (size_t)frames * channels; ++i)
            if (!isfinite(output[i]))
                abort();
    bf_resampler_config c = BF_INIT(bf_resampler_config);
    c.input_rate = a.format.sample_rate;
    c.output_rate = 8000u + (uint32_t)data[4] * 700u;
    c.channels = channels;
    c.max_input_frames = 256;
    size_t bytes;
    if (bf_resampler_size(&c, &bytes) != BF_OK)
        return 0;
    unsigned char *raw = malloc(bytes + BF_ALIGNMENT);
    if (!raw)
        abort();
    void *p = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    bf_resampler *s;
    if (bf_resampler_init(p, bytes, &c, &s) != BF_OK)
        abort();
    bf_resample_info info = BF_INIT(bf_resample_info);
    (void)bf_resampler_set_drift(s, (int32_t)data[5] * 20 - 2500);
    r = bf_resampler_process(s, input, frames, output, data[6], &info);
    if (r == BF_OK || r == BF_NEED_OUTPUT)
        for (size_t i = 0; i < (size_t)info.produced * channels; ++i)
            if (!isfinite(output[i]))
                abort();
    (void)bf_resampler_reset(s);
    (void)bf_resampler_flush(s, output, data[7], &info);
    free(raw);
    return 0;
}
