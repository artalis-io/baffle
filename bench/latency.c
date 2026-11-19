/* Native, unpaced processing-cost benchmark. No I/O inside measured calls. */
#define _POSIX_C_SOURCE 200809L
#include <baffle.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double now(void)
{
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return (double)t.QuadPart / (double)f.QuadPart;
}
static double cpu(void)
{
    FILETIME a, b, k, u;
    ULARGE_INTEGER x, y;
    GetThreadTimes(GetCurrentThread(), &a, &b, &k, &u);
    x.LowPart = k.dwLowDateTime;
    x.HighPart = k.dwHighDateTime;
    y.LowPart = u.dwLowDateTime;
    y.HighPart = u.dwHighDateTime;
    return (double)(x.QuadPart + y.QuadPart) * 1e-7;
}
#else
#include <time.h>
static double stamp(clockid_t id)
{
    struct timespec t;
    if (clock_gettime(id, &t))
        abort();
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static double now(void)
{
    return stamp(CLOCK_MONOTONIC);
}
static double cpu(void)
{
    return stamp(CLOCK_THREAD_CPUTIME_ID);
}
#endif
static void check(bf_result r)
{
    if (r != BF_OK) {
        fprintf(stderr, "DSP error %d\n", r);
        exit(1);
    }
}
static void *arena(size_t bytes, void **raw)
{
    *raw = malloc(bytes + BF_ALIGNMENT);
    if (!*raw)
        exit(1);
    return (void *)(((uintptr_t)*raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
}
static int compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static bf_audio_buffer buffer(float *p, uint32_t rate, uint32_t ch, uint32_t frames)
{
    bf_audio_buffer b = BF_INIT(bf_audio_buffer);
    b.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    b.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    b.format.sample_rate = rate;
    b.format.channels = ch;
    b.format.sample_format = BF_PCM_F32;
    b.format.storage = BF_INTERLEAVED;
    b.format.channel_layout = BF_LAYOUT_DISCRETE;
    b.data = p;
    b.frames = b.capacity_frames = frames;
    b.timing.stream_id = 1;
    b.timing.clock_domain = 1;
    b.timing.epoch = 1;
    b.timing.uncertainty_ns = UINT64_MAX;
    return b;
}
static void report(const char *name, uint32_t rate, uint32_t channels, uint32_t sessions,
                   size_t bytes, double *times, uint32_t n, double elapsed, double used,
                   double delay_ms)
{
#ifdef _WIN32
    /* GetThreadTimes accounting is coarse. Short trials cannot resolve CPU use. */
    if (elapsed < .25)
        used = NAN;
#endif
    double total = 0, jitter = 0;
    uint32_t misses = 0;
    for (uint32_t i = 0; i < n; ++i) {
        total += times[i];
        if (times[i] > .010)
            ++misses;
        if (i)
            jitter += fabs(times[i] - times[i - 1]);
    }
    qsort(times, n, sizeof(*times), compare);
    printf("%s,%u,%u,%u,%u,%zu,%.4f,%.4f,%.4f,%.4f,%.4f,%u,%.6f,%.3f,%.3f,%.4f\n", name, rate,
           channels, sessions, n, bytes, times[n / 2] * 1e3, times[(n - 1) * 95 / 100] * 1e3,
           times[(n - 1) * 99 / 100] * 1e3, times[n - 1] * 1e3, jitter / (n - 1) * 1e3, misses,
           total / ((double)n * .01), used / ((double)n * .01) * 100,
           elapsed > 0 ? used / elapsed * 100 : 0, delay_ms);
}
static void pipeline(uint32_t rate, uint32_t ch, uint32_t mask, uint32_t sessions, uint32_t n)
{
    bf_pipeline_config c = BF_INIT(bf_pipeline_config);
    c.sample_rate = rate;
    c.channels = ch;
    c.reference_channels = 2;
    c.frame_frames = rate / 100;
    c.stages = mask;
    c.aec_tail_ms = 200;
    c.noise_strength_db = 18;
    c.agc_target_dbfs = -18;
    size_t bytes;
    check(bf_pipeline_size(&c, &bytes));
    void *raw[8];
    bf_pipeline *p[8];
    for (uint32_t s = 0; s < sessions; ++s)
        check(bf_pipeline_init(arena(bytes, &raw[s]), bytes, &c, &p[s]));
    size_t samples = (size_t)c.frame_frames * ch;
    float *in = malloc(samples * sizeof(float)), *out = malloc(samples * sizeof(float)),
          *ref = malloc((size_t)c.frame_frames * 2 * sizeof(float));
    double *t = malloc(n * sizeof(double));
    if (!in || !out || !ref || !t)
        exit(1);
    uint32_t rng = 42;
    for (size_t i = 0; i < samples; ++i) {
        rng = rng * 1664525u + 1013904223u;
        in[i] = .04f * ((float)(rng >> 8) / 8388608.0f - 1);
    }
    for (uint32_t i = 0; i < c.frame_frames * 2; ++i)
        ref[i] = .03f * sinf((float)i * .17f);
    bf_audio_buffer a = buffer(in, rate, ch, c.frame_frames),
                    b = buffer(out, rate, ch, c.frame_frames),
                    r = buffer(ref, rate, 2, c.frame_frames);
    r.timing.clock_domain = 2;
    double start = 0, thread = 0;
    for (uint32_t i = 0; i < n + 100; ++i) {
        if (i == 100) {
            start = now();
            thread = cpu();
        }
        double begin = now();
        for (uint32_t s = 0; s < sessions; ++s) {
            bf_process_info info = BF_INIT(bf_process_info);
            check(bf_pipeline_process(p[s], &a, &r, &b, &info));
        }
        double end = now();
        if (i >= 100)
            t[i - 100] = end - begin;
        a.timing.frame_position += c.frame_frames;
        r.timing.frame_position += c.frame_frames;
    }
    double used = cpu() - thread, elapsed = now() - start;
    char name[40];
    snprintf(name, sizeof(name), "pipeline-mask-%u", mask);
    report(name, rate, ch, sessions, bytes * sessions, t, n, elapsed, used,
           (mask & BF_STAGE_NOISE) ? 10 : 0);
    for (uint32_t s = 0; s < sessions; ++s) {
        check(bf_pipeline_deinit(p[s]));
        free(raw[s]);
    }
    free(t);
    free(in);
    free(out);
    free(ref);
}
static void primitives(uint32_t n)
{
    float input[480 * 8], output[480 * 8], matrix[8];
    for (uint32_t i = 0; i < 480 * 8; ++i)
        input[i] = .05f * sinf((float)i * .13f);
    for (uint32_t i = 0; i < 8; ++i)
        matrix[i] = .125f;
    double *t = malloc(n * sizeof(double));
    if (!t)
        exit(1);
    bf_resampler_config c = BF_INIT(bf_resampler_config);
    c.input_rate = 48000;
    c.output_rate = 16000;
    c.channels = 1;
    c.max_input_frames = 480;
    size_t bytes;
    check(bf_resampler_size(&c, &bytes));
    void *raw;
    bf_resampler *s;
    check(bf_resampler_init(arena(bytes, &raw), bytes, &c, &s));
    double start = now(), thread = cpu();
    for (uint32_t i = 0; i < n; ++i) {
        bf_resample_info info = BF_INIT(bf_resample_info);
        double begin = now();
        check(bf_resampler_process(s, input, 480, output, 480, &info));
        t[i] = now() - begin;
        if (info.consumed != 480)
            exit(1);
    }
    double used = cpu() - thread;
    report("resampler-48-to-16", 48000, 1, 1, bytes, t, n, now() - start, used, 95.5 / 48.0);
    free(raw);
    bf_audio_buffer a = buffer(input, 48000, 8, 480), b = buffer(output, 48000, 1, 480);
    start = now();
    thread = cpu();
    for (uint32_t i = 0; i < n; ++i) {
        double begin = now();
        check(bf_channel_matrix(&a, &b, matrix, 1));
        t[i] = now() - begin;
    }
    used = cpu() - thread;
    report("matrix-8-to-1", 48000, 8, 1, 0, t, n, now() - start, used, 0);
    free(t);
}
int main(int argc, char **argv)
{
    uint32_t n = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 3000;
    if (n < 100 || n > 1000000)
        return 2;
    bf_library_info lib = BF_INIT(bf_library_info);
    check(bf_library_query(&lib));
    puts("case,rate,channels,sessions,iterations,workspace_bytes,p50_ms,p95_ms,p99_ms,max_ms,mean_"
         "adjacent_cost_jitter_ms,deadline_misses,wall_rtf,thread_cpu_percent_audio,thread_cpu_"
         "percent_unpaced_run,algorithmic_delay_ms");
    primitives(n);
    pipeline(16000, 1, BF_STAGE_AGC, 1, n);
    pipeline(16000, 1, BF_STAGE_VAD, 1, n);
    pipeline(16000, 1, BF_STAGE_NOISE, 1, n);
    if (lib.features & BF_FEATURE_SPEEXDSP)
        pipeline(16000, 1, BF_STAGE_AEC, 1, n);
    uint32_t mask =
        (lib.features & BF_FEATURE_SPEEXDSP) ? BF_STAGE_ALL : BF_STAGE_ALL & ~BF_STAGE_AEC;
    pipeline(16000, 1, mask, 1, n);
    for (uint32_t ch = 1; ch <= 8; ch *= 2)
        pipeline(48000, ch, mask, 1, n);
    pipeline(16000, 1, mask, 8, n);
    return 0;
}
