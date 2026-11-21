#include <baffle.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 8)
        return 0;
    const uint32_t rates[] = {8000, 16000, 48000};
    bf_aec_config c = BF_INIT(bf_aec_config);
    c.sample_rate = rates[data[1] % 3u];
    c.frame_frames = c.sample_rate / 100u * (1u + data[2] % 2u);
    c.capture_channels = 1u + data[0] % 2u;
    c.reference_channels = 1;
    c.tail_ms = data[3] & 1u ? 200u : 40u;
    c.backend = BF_AEC_SPEEXDSP;
    size_t bytes;
    bf_result r = bf_aec_size(&c, &bytes);
    if (r == BF_ERROR_UNSUPPORTED)
        return 0; /* Meaningful campaign explicitly enables the backend. */
    if (r != BF_OK)
        abort();
    unsigned char *raw = malloc(bytes + BF_ALIGNMENT), *snapshot = malloc(bytes);
    if (!raw || !snapshot)
        abort();
    void *memory = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) &
                           ~(uintptr_t)(BF_ALIGNMENT - 1u));
    bf_aec *s;
    if (bf_aec_init(memory, bytes, &c, &s) != BF_OK)
        abort();
    float input[960 * 2] = {0}, reference[960] = {0}, output[960 * 2];
    size_t n = (size_t)c.frame_frames * c.capture_channels, count = n * sizeof(float);
    if (count > size - 8)
        count = size - 8;
    memcpy(input, data + 8, count);
    memcpy(reference, input, c.frame_frames * sizeof(float));
    int valid = 1;
    for (size_t i = 0; i < n; ++i)
        if (!isfinite(input[i]) || fabsf(input[i]) > 16)
            valid = 0;
    bf_audio_timing nt = BF_INIT(bf_audio_timing), ft = BF_INIT(bf_audio_timing);
    nt.stream_id = 1;
    ft.stream_id = 2;
    nt.clock_domain = 10;
    ft.clock_domain = 20;
    nt.epoch = ft.epoch = 1;
    for (unsigned step = 0; step < 8; ++step) {
        if (step == 2)
            ft.generation = data[4];
        if (step == 3)
            nt.epoch += data[5] & 1u;
        nt.flags = step == 4 ? BF_AUDIO_GAP : 0;
        if (step == 6 && bf_aec_reset(s) != BF_OK)
            abort();
        memcpy(snapshot, memory, bytes);
        memset(output, 0x55, n * sizeof(float));
        bf_aec_info info = BF_INIT(bf_aec_info);
        const float *ref = step == 5 && (data[6] & 1u) ? NULL : reference;
        r = bf_aec_process(s, input, &nt, ref, ref ? &ft : NULL, output, &info);
        if (valid) {
            if (r != BF_OK || info.backend_arena_used > info.backend_arena_capacity)
                abort();
            for (size_t i = 0; i < n; ++i)
                if (!isfinite(output[i]))
                    abort();
            if (!ref && (!(info.flags & BF_REFERENCE_MISSING) ||
                         (info.flags & BF_REFERENCE_GENERATION_CHANGED) ||
                         memcmp(input, output, n * sizeof(float))))
                abort();
        } else {
            if ((r != BF_ERROR_NONFINITE && r != BF_ERROR_INVALID) ||
                memcmp(snapshot, memory, bytes))
                abort();
            const unsigned char *p = (const unsigned char *)output;
            for (size_t i = 0; i < n * sizeof(float); ++i)
                if (p[i] != 0x55)
                    abort();
        }
        nt.frame_position += c.frame_frames;
        ft.frame_position += c.frame_frames;
    }
    if (bf_aec_deinit(s) != BF_OK)
        abort();
    free(snapshot);
    free(raw);
    return 0;
}
