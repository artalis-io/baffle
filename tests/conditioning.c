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
static void *memory(size_t n, void **base)
{
    *base = malloc(n + BF_ALIGNMENT);
    CHECK(*base);
    return (void *)(((uintptr_t)*base + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
}
static uint32_t random_state = 1;
static float noise(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return (float)(random_state >> 8) / 8388608.0f - 1;
}
int main(void)
{
    float in[160], out[160], previous[160] = {0};
    void *base;
    size_t bytes;
    bf_noise_config nc = BF_INIT(bf_noise_config);
    bf_noise *ns;
    nc.sample_rate = 16000;
    nc.channels = 1;
    nc.frame_frames = 160;
    nc.strength_db = 0;
    CHECK(bf_noise_size(&nc, &bytes) == BF_OK);
    void *p = memory(bytes, &base);
    CHECK(bf_noise_init(p, bytes, &nc, &ns) == BF_OK);
    double error = 0, signal = 0;
    for (unsigned frame = 0; frame < 100; ++frame) {
        for (unsigned i = 0; i < 160; ++i)
            in[i] = .2f * noise();
        CHECK(bf_noise_process(ns, in, out, -1) == BF_OK);
        for (unsigned i = 0; i < 160; ++i) {
            double d = out[i] - previous[i];
            error += d * d;
            signal += (double)previous[i] * previous[i];
        }
        memcpy(previous, in, sizeof(in));
    }
    printf("NS disabled overlap-add relative_error_db=%.2f\n", 10 * log10(error / signal));
    CHECK(error / signal < 1e-10);
    /* A protected foreground frame must not seed a noise estimate after init
     * or reset. With no previously learned noise, frozen NS is unity WOLA. */
    nc.strength_db = 18;
    CHECK(bf_noise_init(p, bytes, &nc, &ns) == BF_OK);
    uint32_t saved_random_state = random_state;
    for (unsigned pass = 0; pass < 2; ++pass) {
        if (pass) CHECK(bf_noise_reset(ns) == BF_OK);
        memset(previous, 0, sizeof(previous)); error = signal = 0;
        for (unsigned frame = 0; frame < 40; ++frame) {
            for (unsigned i = 0; i < 160; ++i) in[i] = .02f * noise();
            CHECK(bf_noise_process(ns, in, out, 1) == BF_OK);
            for (unsigned i = 0; i < 160; ++i) {
                double d = out[i] - previous[i]; error += d * d;
                signal += (double)previous[i] * previous[i];
            }
            memcpy(previous, in, sizeof(in));
        }
        CHECK(error / signal < 1e-10);
    }
    random_state = saved_random_state; /* Preserve the original quality fixtures. */
    nc.strength_db = 20;
    CHECK(bf_noise_init(p, bytes, &nc, &ns) == BF_OK);
    double input_energy = 0, output_energy = 0;
    for (unsigned frame = 0; frame < 500; ++frame) {
        for (unsigned i = 0; i < 160; ++i)
            in[i] = .03f * noise();
        CHECK(bf_noise_process(ns, in, out, 0) == BF_OK);
        if (frame > 100)
            for (unsigned i = 0; i < 160; ++i) {
                input_energy += (double)in[i] * in[i];
                output_energy += (double)out[i] * out[i];
            }
    }
    printf("NS known stationary noise reduction_db=%.3f\n",
           10 * log10(input_energy / output_energy));
    CHECK(output_energy / input_energy < .5);
    double correlation = 0, reference = 0;
    for (unsigned frame = 0; frame < 100; ++frame) {
        for (unsigned i = 0; i < 160; ++i)
            in[i] = .2f * (float)sin(2 * 3.141592653589793 * 300 * i / 16000) + .03f * noise();
        CHECK(bf_noise_process(ns, in, out, 1) == BF_OK);
        if (frame > 5)
            for (unsigned i = 0; i < 160; ++i) {
                double clean = .2 * sin(2 * 3.141592653589793 * 300 * i / 16000);
                correlation += out[i] * clean;
                reference += clean * clean;
            }
    }
    printf("NS voiced tone projection=%.5f\n", correlation / reference);
    CHECK(correlation / reference > .85 && correlation / reference < 1.1);
    in[10] = NAN;
    CHECK(bf_noise_process(ns, in, out, 0) == BF_ERROR_NONFINITE);
    CHECK(bf_noise_reset(ns) == BF_OK);
    free(base);
    bf_agc_config ac = BF_INIT(bf_agc_config);
    bf_agc *agc;
    bf_level_info li = BF_INIT(bf_level_info);
    ac.sample_rate = 16000;
    ac.channels = 1;
    ac.max_frames = 160;
    ac.target_dbfs = -18;
    ac.min_gain_db = -20;
    ac.max_gain_db = 20;
    ac.attack_ms = 5;
    ac.release_ms = 100;
    ac.gate_dbfs = -60;
    ac.limit_dbfs = -1;
    p = memory(bf_agc_size(), &base);
    CHECK(bf_agc_init(p, bf_agc_size(), &ac, &agc) == BF_OK);
    for (unsigned f = 0; f < 300; ++f) {
        for (unsigned i = 0; i < 160; ++i)
            in[i] = .02f * (float)sin(2 * 3.141592653589793 * 300 * i / 16000);
        CHECK(bf_agc_process(agc, in, out, 160, 1, &li) == BF_OK);
    }
    CHECK(fabs(li.output_rms - pow(10, -18.0 / 20)) < .005);
    for (unsigned i = 0; i < 160; ++i)
        in[i] = i & 1 ? 1 : -1;
    CHECK(bf_agc_process(agc, in, out, 160, 1, &li) == BF_OK &&
          li.output_peak <= pow(10, -1.0 / 20) + 1e-6 && li.limited_samples);
    memset(in, 0, sizeof(in));
    CHECK(bf_agc_process(agc, in, out, 160, 0, &li) == BF_OK && li.output_rms == 0);
    free(base);
    bf_vad_config vc = BF_INIT(bf_vad_config);
    bf_vad *vad;
    bf_vad_info vi = BF_INIT(bf_vad_info);
    vc.sample_rate = 16000;
    vc.channels = 1;
    vc.max_frames = 160;
    vc.onset_ms = 20;
    vc.hangover_ms = 100;
    vc.threshold = .55f;
    vc.noise_floor_dbfs = -60;
    p = memory(bf_vad_size(), &base);
    CHECK(bf_vad_init(p, bf_vad_size(), &vc, &vad) == BF_OK);
    unsigned false_positives = 0;
    for (unsigned f = 0; f < 100; ++f) {
        for (unsigned i = 0; i < 160; ++i)
            in[i] = .03f * noise();
        CHECK(bf_vad_process(vad, in, 160, &vi) == BF_OK);
        false_positives += vi.active;
    }
    CHECK(false_positives == 0);
    for (unsigned f = 0; f < 3; ++f) {
        for (unsigned i = 0; i < 160; ++i)
            in[i] = .15f * (float)sin(2 * 3.141592653589793 * 300 * i / 16000);
        CHECK(bf_vad_process(vad, in, 160, &vi) == BF_OK);
    }
    CHECK(vi.active && vi.probability > .55f);
    memset(in, 0, sizeof(in));
    for (unsigned f = 0; f < 12; ++f)
        CHECK(bf_vad_process(vad, in, 160, &vi) == BF_OK);
    CHECK(!vi.active);
    CHECK(bf_vad_reset(vad) == BF_OK);
    free(base);
    puts("NS numerical/quality fixtures, AGC limiting/target and classical VAD onset/hangover "
         "passed");
    return 0;
}
