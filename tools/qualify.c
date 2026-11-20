/* Offline WAV qualification. File access and allocation occur outside DSP calls.
 * Input files must be independently aligned and have documented provenance. */
#include <baffle.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <errno.h>
typedef struct wav {
    uint32_t rate, channels, frames;
    float *pcm;
} wav;
static FILE *open_file(const char *path, const char *mode)
{
#ifdef _MSC_VER
    FILE *f = NULL;
    if (fopen_s(&f, path, mode))
        return NULL;
    return f;
#else
    return fopen(path, mode);
#endif
}
static void fail(const char *s)
{
    fprintf(stderr, "%s\n", s);
    exit(1);
}
static uint64_t unsigned_argument(const char *text, uint64_t maximum)
{
    char *end;
    errno = 0;
    unsigned long long v = strtoull(text, &end, 10);
    if (!*text || *text < '0' || *text > '9' || *end || errno || v > maximum)
        fail("invalid unsigned argument");
    return (uint64_t)v;
}
static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t u16(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}
static wav load(const char *path)
{
    FILE *f = open_file(path, "rb");
    if (!f)
        fail("cannot open WAV");
    if (fseek(f, 0, SEEK_END))
        fail("seek");
    long length = ftell(f);
    if (length < 44 || length > 256 * 1024 * 1024)
        fail("WAV size limit 256 MiB");
    rewind(f);
    unsigned char *p = malloc((size_t)length);
    if (!p || fread(p, 1, (size_t)length, f) != (size_t)length)
        fail("read");
    fclose(f);
    if (memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4) || u32(p + 4) != (uint32_t)length - 8)
        fail("expected RIFF WAVE");
    uint32_t rate = 0, ch = 0;
    const unsigned char *pcm = NULL;
    size_t bytes = 0;
    for (size_t i = 12; i + 8 <= (size_t)length;) {
        uint32_t n = u32(p + i + 4);
        if (n > (size_t)length - i - 8)
            fail("invalid WAV chunk");
        if (!memcmp(p + i, "fmt ", 4)) {
            if (n < 16 || u16(p + i + 8) != 1 || u16(p + i + 22) != 16)
                fail("only PCM16 WAV supported");
            ch = u16(p + i + 10);
            rate = u32(p + i + 12);
            if (ch < 1 || ch > 2 || u16(p + i + 20) != ch * 2)
                fail("mono/stereo only");
        }
        if (!memcmp(p + i, "data", 4)) {
            pcm = p + i + 8;
            bytes = n;
        }
        i += 8 + (size_t)n + (n & 1u);
    }
    if (!rate || !ch || !pcm || bytes % (ch * 2u) || rate < 8000 || rate > 48000 || rate % 100)
        fail("unsupported WAV format");
    wav w = {rate, ch, (uint32_t)(bytes / (ch * 2u)), NULL};
    w.pcm = malloc((size_t)w.frames * ch * sizeof(float));
    if (!w.pcm)
        fail("allocation");
    for (size_t i = 0; i < (size_t)w.frames * ch; ++i) {
        uint32_t v = u16(pcm + 2 * i);
        int32_t s = v >= 32768 ? (int32_t)v - 65536 : (int32_t)v;
        w.pcm[i] = (float)s / 32768;
    }
    free(p);
    return w;
}
static void put(FILE *f, uint32_t v, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; ++i)
        if (fputc((int)((v >> (8 * i)) & 255u), f) == EOF)
            fail("write");
}
static void resample(wav *w, uint32_t rate)
{
    if (w->rate == rate)
        return;
    bf_resampler_config c = BF_INIT(bf_resampler_config);
    c.input_rate = w->rate;
    c.output_rate = rate;
    c.channels = w->channels;
    c.max_input_frames = BF_MAX_FRAMES;
    size_t bytes;
    if (bf_resampler_size(&c, &bytes) != BF_OK)
        fail("SRC configuration");
    uint64_t frames = ((uint64_t)w->frames * rate + w->rate - 1u) / w->rate;
    if (frames > UINT32_MAX || frames > SIZE_MAX / w->channels / sizeof(float))
        fail("SRC output size");
    unsigned char *raw = malloc(bytes + BF_ALIGNMENT);
    float *output = malloc((size_t)frames * w->channels * sizeof(float));
    if (!raw || !output)
        fail("SRC allocation");
    void *memory = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    bf_resampler *s;
    if (bf_resampler_init(memory, bytes, &c, &s) != BF_OK)
        fail("SRC init");
    uint32_t used = 0, made = 0;
    float terminal[BF_MAX_CHANNELS];
    for (uint64_t calls = 0; calls <= (uint64_t)w->frames + frames; ++calls) {
        if (used == w->frames && made == frames)
            break;
        uint32_t n = w->frames - used, capacity = (uint32_t)frames - made;
        if (n > BF_MAX_FRAMES)
            n = BF_MAX_FRAMES;
        if (capacity > BF_MAX_FRAMES)
            capacity = BF_MAX_FRAMES;
        float *destination = output + (size_t)made * w->channels;
        if (!capacity) {
            capacity = 1;
            destination = terminal;
        }
        bf_resample_info info = BF_INIT(bf_resample_info);
        bf_result result = bf_resampler_process(s, w->pcm + (size_t)used * w->channels, n,
                                                destination, capacity, &info);
        if ((result != BF_OK && result != BF_NEED_OUTPUT) || (!info.consumed && !info.produced))
            fail("SRC progress");
        used += info.consumed;
        made += info.produced;
        if (made > frames)
            fail("SRC produced beyond nominal duration");
    }
    if (used != w->frames || made != frames)
        fail("SRC accounting");
    uint32_t twice;
    if (bf_resampler_latency(&c, &twice) != BF_OK)
        fail("SRC delay");
    printf("offline_src=%u->%u group_delay_input_frames=%.1f\n", w->rate, rate, (double)twice / 2);
    free(raw);
    free(w->pcm);
    w->pcm = output;
    w->frames = (uint32_t)frames;
    w->rate = rate;
}
int main(int argc, char **argv)
{
    if (argc != 4 && (argc < 6 || argc > 9)) {
        fprintf(stderr, "usage: baffle_qualify capture.wav aligned-reference.wav output.wav "
                        "[far_only_first_frame far_only_frames [processing_rate [tail_ms [hop_ms]]]]\n");
        return 2;
    }
    wav a = load(argv[1]), r = load(argv[2]);
    if (a.rate != r.rate || a.frames != r.frames || !a.frames)
        fail("files require identical rate, length and independently established alignment");
    uint64_t first = 0, count = 0;
    if (argc >= 6) {
        first = unsigned_argument(argv[4], UINT64_MAX);
        count = unsigned_argument(argv[5], UINT64_MAX);
        if (!count || first > a.frames || count > a.frames - first)
            fail("invalid annotated far-only interval");
    }
    if (argc >= 7) {
        uint32_t rate = (uint32_t)unsigned_argument(argv[6], UINT32_MAX);
        if (rate < 8000 || rate > 48000 || rate % 100)
            fail("unsupported processing rate");
        first = first * rate / a.rate;
        count = count * rate / a.rate;
        resample(&a, rate);
        resample(&r, rate);
    }
    bf_aec_config c = BF_INIT(bf_aec_config);
    c.sample_rate = a.rate;
    c.frame_frames = a.rate / 100;
    c.capture_channels = a.channels;
    c.reference_channels = r.channels;
    c.tail_ms = 200;
    if (argc >= 8)
        c.tail_ms = (uint32_t)unsigned_argument(argv[7], UINT32_MAX);
    if (argc >= 9) {
        uint32_t hop = (uint32_t)unsigned_argument(argv[8], UINT32_MAX);
        if (hop != 10 && hop != 20)
            fail("hop_ms must be 10 or 20");
        c.frame_frames = a.rate / 100 * (hop / 10);
    }
    c.backend = BF_AEC_SPEEXDSP;
    size_t bytes;
    if (bf_aec_size(&c, &bytes) != BF_OK)
        fail("enable BF_WITH_SPEEXDSP for qualification");
    unsigned char *raw = malloc(bytes + BF_ALIGNMENT);
    if (!raw)
        fail("allocation");
    void *p = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    bf_aec *s;
    if (bf_aec_init(p, bytes, &c, &s) != BF_OK)
        fail("AEC init");
    float near[960 * 2], far[960 * 2], out[960 * 2];
    bf_audio_timing nt = BF_INIT(bf_audio_timing), ft = BF_INIT(bf_audio_timing);
    nt.stream_id = 1;
    ft.stream_id = 2;
    nt.clock_domain = 1;
    ft.clock_domain = 2;
    nt.epoch = ft.epoch = 1;
    nt.uncertainty_ns = ft.uncertainty_ns = UINT64_MAX;
    FILE *f = open_file(argv[3], "wb");
    if (!f)
        fail("output open");
    uint32_t data_bytes = a.frames * a.channels * 2u;
    fwrite("RIFF", 1, 4, f);
    put(f, 36 + data_bytes, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put(f, 16, 4);
    put(f, 1, 2);
    put(f, a.channels, 2);
    put(f, a.rate, 4);
    put(f, a.rate * a.channels * 2, 4);
    put(f, a.channels * 2, 2);
    put(f, 16, 2);
    fwrite("data", 1, 4, f);
    put(f, data_bytes, 4);
    double input_energy = 0, output_energy = 0;
    for (uint32_t pos = 0; pos < a.frames; pos += c.frame_frames) {
        uint32_t n = a.frames - pos;
        if (n > c.frame_frames)
            n = c.frame_frames;
        memset(near, 0, sizeof(near));
        memset(far, 0, sizeof(far));
        memcpy(near, a.pcm + (size_t)pos * a.channels, (size_t)n * a.channels * sizeof(float));
        memcpy(far, r.pcm + (size_t)pos * r.channels, (size_t)n * r.channels * sizeof(float));
        nt.frame_position = ft.frame_position = pos;
        bf_aec_info info = BF_INIT(bf_aec_info);
        if (bf_aec_process(s, near, &nt, far, &ft, out, &info) != BF_OK)
            fail("AEC process");
        for (uint32_t i = 0; i < n; ++i)
            for (uint32_t ch = 0; ch < a.channels; ++ch) {
                size_t j = (size_t)i * a.channels + ch;
                float x = out[j];
                if (!isfinite(x))
                    fail("nonfinite output");
                int32_t v = x >= 32767.0f / 32768 ? 32767
                            : x <= -1             ? -32768
                                                  : (int32_t)lrintf(x * 32768);
                put(f, (uint32_t)(uint16_t)v, 2);
                if (pos + i >= first && (uint64_t)(pos + i) - first < count) {
                    input_energy += (double)near[j] * near[j];
                    output_energy += (double)x * x;
                }
            }
    }
    if (fclose(f))
        fail("output close");
    uint32_t delay;
    if (bf_aec_estimate_delay(s, &delay) == BF_OK)
        printf("dominant_adapted_tap=%u (not hardware latency)\n", delay);
    if (bf_aec_deinit(s) != BF_OK)
        fail("deinit");
    free(raw);
    free(a.pcm);
    free(r.pcm);
    printf("rate=%u capture_channels=%u reference_channels=%u real_frames=%u workspace_bytes=%zu "
           "frame_frames=%u tail_ms=%u\n",
           a.rate, a.channels, r.channels, a.frames, bytes, c.frame_frames, c.tail_ms);
    if (count && input_energy > 0 && output_energy > 0)
        printf("annotated_far_only_erle_db=%.4f interval_first=%llu interval_frames=%llu\n",
               10 * log10(input_energy / output_energy), (unsigned long long)first,
               (unsigned long long)count);
    else
        puts("ERLE unavailable: supply an independently annotated far-only interval with nonzero "
             "energy");
    return 0;
}
