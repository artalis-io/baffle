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
static float input[48000], whole[150000], split[150000];
static unsigned run(uint32_t ir, uint32_t orate, int fragmented, int ppm, float *output)
{
    bf_resampler_config c = BF_INIT(bf_resampler_config);
    size_t bytes;
    bf_resampler *s;
    unsigned n = 48000, used = 0, made = 0;
    c.input_rate = ir;
    c.output_rate = orate;
    c.channels = 1;
    c.max_input_frames = 1024;
    CHECK(bf_resampler_size(&c, &bytes) == BF_OK);
    void *base = malloc(bytes + BF_ALIGNMENT);
    CHECK(base);
    void *p = (void *)(((uintptr_t)base + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    CHECK(bf_resampler_init(p, bytes, &c, &s) == BF_OK);
    CHECK(bf_resampler_set_drift(s, ppm) == BF_OK);
    for (unsigned calls = 0; calls < 200000 && used < n; ++calls) {
        bf_resample_info info = BF_INIT(bf_resample_info);
        unsigned frames = fragmented ? 1u + calls % 173u : 1024u,
                 cap = fragmented ? 1u + calls % 79u : 4096u;
        if (frames > n - used)
            frames = n - used;
        bf_result r = bf_resampler_process(s, input + used, frames, output + made, cap, &info);
        CHECK(r == BF_OK || r == BF_NEED_OUTPUT);
        CHECK(info.consumed || info.produced);
        used += info.consumed;
        made += info.produced;
    }
    CHECK(used == n);
    /* Drain pending interpolation before synthetic FIR-tail flush. */
    for (unsigned calls = 0; calls < 100; ++calls) {
        bf_resample_info info = BF_INIT(bf_resample_info);
        bf_result r = bf_resampler_process(s, NULL, 0, output + made, 4096, &info);
        made += info.produced;
        CHECK(r == BF_OK || r == BF_NEED_OUTPUT);
        if (r == BF_OK)
            break;
    }
    {
        float nan = NAN;
        bf_resample_info info = BF_INIT(bf_resample_info);
        CHECK(bf_resampler_process(s, &nan, 1, output, 1, &info) == BF_ERROR_NONFINITE &&
              !info.consumed && !info.produced);
    }
    {
        bf_resample_info info = BF_INIT(bf_resample_info);
        float temp[4096];
        unsigned tail = 0;
        bf_result r;
        do {
            r = bf_resampler_flush(s, temp, 7, &info);
            CHECK(r == BF_OK || r == BF_NEED_OUTPUT);
            tail += info.produced;
            CHECK(tail < 10000);
        } while (r == BF_NEED_OUTPUT);
        CHECK(bf_resampler_process(s, NULL, 0, temp, 1, &info) == BF_ERROR_STATE);
        CHECK(bf_resampler_reset(s) == BF_OK);
        CHECK(bf_resampler_flush(s, temp, 1, &info) == BF_OK && !info.produced);
    }
    free(base);
    return made;
}
int main(void)
{
    const double pi = 3.14159265358979323846;
    for (unsigned i = 0; i < 48000; ++i)
        input[i] = (float)(.5 * sin(2 * pi * 1000 * i / 48000));
    unsigned n = run(48000, 16000, 0, 0, whole), m = run(48000, 16000, 1, 0, split);
    CHECK(n == 16000 && m == n && !memcmp(whole, split, n * sizeof(float)));
    double error = 0, energy = 0;
    for (unsigned i = 1000; i < n; ++i) {
        double expected = .5 * sin(2 * pi * 1000 * ((double)i * 3 - 95.5) / 48000);
        error += (whole[i] - expected) * (whole[i] - expected);
        energy += expected * expected;
    }
    printf("SRC 48->16k 1kHz relative_error_db=%.3f\n", 10 * log10(error / energy));
    CHECK(error / energy < 1e-8);
    for (unsigned i = 0; i < 48000; ++i)
        input[i] = (float)(.5 * sin(2 * pi * 12000 * i / 48000));
    n = run(48000, 16000, 1, 0, whole);
    energy = 0;
    for (unsigned i = 1000; i < n; ++i)
        energy += (double)whole[i] * whole[i];
    printf("SRC 48->16k 12kHz stopband_db=%.3f\n", 10 * log10(energy / (n - 1000) / .125));
    CHECK(energy / (n - 1000) / .125 < 1e-8);
    {
        const double tones[] = {100, 1000, 3000, 6000, 9000, 12000, 18000};
        const uint32_t inputs[] = {44100, 48000};
        for (unsigned rate = 0; rate < 2; ++rate)
            for (unsigned tone = 0; tone < 7; ++tone) {
                double frequency = tones[tone];
                for (unsigned i = 0; i < 48000; ++i)
                    input[i] = (float)(.5 * sin(2 * pi * frequency * i / inputs[rate]));
                n = run(inputs[rate], 16000, 0, 0, whole);
                double measured = 0, expected = 0;
                for (unsigned i = 1000; i < n; ++i) {
                    measured += (double)whole[i] * whole[i];
                    double oracle =
                        .5 * sin(2 * pi * frequency * ((double)i * inputs[rate] / 16000 - 95.5) /
                                 inputs[rate]);
                    expected += oracle * oracle;
                }
                double ratio = measured / expected;
                printf("SRC %u->16000 tone %.0f gain_db=%.3f\n", inputs[rate], frequency,
                       10 * log10(ratio));
                if (frequency <= 6000)
                    CHECK(ratio > .98 && ratio < 1.02);
                else
                    CHECK(ratio < 1e-6);
            }
    }
    for (unsigned i = 0; i < 48000; ++i)
        input[i] = (float)(.25 * sin(2 * pi * 700 * i / 16000));
    n = run(16000, 48000, 0, 1000, whole);
    m = run(16000, 48000, 1, 1000, split);
    CHECK(m == n && n < 144000 && n > 143800 && !memcmp(whole, split, n * sizeof(float)));
    n = run(16000, 48000, 1, -1000, whole);
    CHECK(n > 144000 && n < 144200);
    /* Additional nominal rates and extreme downsampling retain exact accounting
       and agree bit-for-bit across arbitrary input/output boundaries. */
    const uint32_t rates[][2] = {
        {44100, 16000}, {48000, 44100}, {96000, 16000}, {192000, 8000}, {8000, 16000}};
    for (size_t pair = 0; pair < sizeof(rates) / sizeof(rates[0]); ++pair) {
        uint32_t ir = rates[pair][0], out_rate = rates[pair][1];
        for (unsigned i = 0; i < 48000; ++i)
            input[i] = (float)(.25 * sin(2 * pi * 700 * i / ir));
        n = run(ir, out_rate, 0, 0, whole);
        m = run(ir, out_rate, 1, 0, split);
        CHECK(n == (uint32_t)(((uint64_t)48000 * out_rate + ir - 1) / ir));
        CHECK(n == m && !memcmp(whole, split, n * sizeof(float)));
        for (unsigned i = 0; i < n; ++i)
            CHECK(isfinite(whole[i]) && fabsf(whole[i]) < .3f);
    }
    puts("SRC accounting, chunk invariance, drift, nonfinite rejection, flush and reset passed");
    {
        bf_resampler_config c = BF_INIT(bf_resampler_config);
        c.input_rate = 16000;
        c.output_rate = 48000;
        c.channels = 32;
        c.max_input_frames = 160;
        size_t bytes;
        CHECK(bf_resampler_size(&c, &bytes) == BF_OK);
        unsigned char *raw = malloc(bytes + BF_ALIGNMENT);
        CHECK(raw);
        void *memory =
            (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
        bf_resampler *s;
        CHECK(bf_resampler_init(memory, bytes, &c, &s) == BF_OK);
        float source[160 * 32] = {0}, result[512 * 32];
        for (unsigned ch = 0; ch < 32; ++ch)
            source[ch] = 1.0f / (float)(ch + 1);
        bf_resample_info info = BF_INIT(bf_resample_info);
        CHECK(bf_resampler_process(s, source, 160, result, 512, &info) == BF_OK);
        CHECK(info.consumed == 160 && info.produced == 480);
        for (unsigned i = 0; i < info.produced; ++i)
            for (unsigned ch = 0; ch < 32; ++ch)
                CHECK(fabsf(result[i * 32 + ch] * (float)(ch + 1) - result[i * 32]) < 2e-7f);
        free(raw);
    }
    return 0;
}
