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
static void multichannel_reset_case(uint32_t rate, uint32_t channels, uint32_t refs, unsigned mode)
{
    bf_aec_config c = BF_INIT(bf_aec_config);
    c.sample_rate = rate; c.frame_frames = rate/100;
    c.capture_channels = channels; c.reference_channels = refs; c.tail_ms = 40;
    c.backend = BF_AEC_SPEEXDSP;
    size_t bytes; CHECK(bf_aec_size(&c, &bytes) == BF_OK);
    void *raw = malloc(bytes + BF_ALIGNMENT); CHECK(raw);
    void *p = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    bf_aec *s; CHECK(bf_aec_init(p, bytes, &c, &s) == BF_OK);
    float near[480*8], far[480*2], out[480*8]; uint32_t rng = 917u;
    bf_audio_timing nt = BF_INIT(bf_audio_timing), ft = BF_INIT(bf_audio_timing);
    bf_aec_info info = BF_INIT(bf_aec_info);
    for (unsigned frame = 0; frame < 400; ++frame) {
        for (unsigned i = 0; i < c.frame_frames; ++i) {
            for (unsigned ch = 0; ch < refs; ++ch) {
                rng = rng * 1664525u + 1013904223u;
                far[refs*i+ch] = ((float)(rng >> 8) / 16777216.0f - .5f) * .4f;
            }
            for (unsigned ch=0;ch<channels;++ch)
                near[channels*i+ch] = .6f*far[refs*i+ch%refs] + .2f*far[refs*i+(ch+1)%refs];
        }
        nt.frame_position = ft.frame_position = frame * c.frame_frames;
        CHECK(bf_aec_process(s, near, &nt, far, &ft, out, &info) == BF_OK);
    }
    uint64_t resets=info.resets, position=400u*c.frame_frames;
    memset(near, 0, sizeof(near));
    if (mode==0) CHECK(bf_aec_reset(s) == BF_OK);
    else if(mode==1) ++nt.epoch;
    else {
        nt.frame_position=position;
        CHECK(bf_aec_process(s,near,&nt,NULL,NULL,out,&info)==BF_OK);
        position+=c.frame_frames;
    }
    double ghost = 0;
    for (unsigned frame = 0; frame < 8; ++frame) {
        nt.frame_position = ft.frame_position = position+frame*c.frame_frames;
        CHECK(bf_aec_process(s, near, &nt, far, &ft, out, &info) == BF_OK);
        CHECK(info.resets==resets+1);
        for (unsigned i = 0; i < c.frame_frames*channels; ++i) ghost += (double)out[i]*out[i];
    }
    printf("AEC reset rate=%u shape=%ux%u mode=%u old-path ghost energy=%.12g\n", rate,channels,refs,mode,ghost); fflush(stdout);
    CHECK(ghost == 0); /* A reset filter must not synthesize old-path echo. */
    CHECK(bf_aec_deinit(s) == BF_OK); free(raw);
}
static void near_only_response(void)
{
    /* No echo: require near-end fidelity, independently of ERLE. A 20 Hz
     * pole-radius model predicts >13.9 dB error SNR at 200 Hz at these rates.
     * Leave 0.9 dB for S16 quantization and finite-precision filter state. */
    const uint32_t rates[] = {8000, 16000, 48000};
    float in[960], zero[960] = {0}, out[960];
    for (unsigned ri = 0; ri < 3; ++ri)
        for (unsigned hop = 1; hop <= 2; ++hop) {
            bf_aec_config c = BF_INIT(bf_aec_config);
            c.sample_rate = rates[ri];
            c.frame_frames = rates[ri] / 100 * hop;
            c.capture_channels = c.reference_channels = 1;
            c.tail_ms = 200;
            c.backend = BF_AEC_SPEEXDSP;
            size_t bytes;
            CHECK(bf_aec_size(&c, &bytes) == BF_OK);
            void *raw = malloc(bytes + BF_ALIGNMENT);
            CHECK(raw);
            void *memory = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) &
                                   ~(uintptr_t)(BF_ALIGNMENT - 1u));
            bf_aec *s;
            CHECK(bf_aec_init(memory, bytes, &c, &s) == BF_OK);
            bf_audio_timing nt = BF_INIT(bf_audio_timing), ft = BF_INIT(bf_audio_timing);
            double power = 0, error = 0, mean = 0;
            for (unsigned frame = 0; frame < 300 / hop; ++frame) {
                uint32_t start = frame * c.frame_frames;
                for (uint32_t i = 0; i < c.frame_frames; ++i)
                    in[i] = .02f + .1f * (float)sin(2 * 3.141592653589793 * 200 *
                                                     (start + i) / c.sample_rate);
                nt.frame_position = ft.frame_position = start;
                bf_aec_info info = BF_INIT(bf_aec_info);
                CHECK(bf_aec_process(s, in, &nt, zero, &ft, out, &info) == BF_OK);
                if (frame >= 100 / hop)
                    for (uint32_t i = 0; i < c.frame_frames; ++i) {
                        double voice = (double)in[i] - .02;
                        power += voice * voice;
                        error += (out[i] - voice) * (out[i] - voice);
                        mean += out[i];
                    }
            }
            double snr = 10 * log10(power / error);
            printf("AEC near-only rate=%u hop_ms=%u raw_error_snr_db=%.3f dc_mean=%.8f\n",
                   c.sample_rate, hop * 10, snr, mean / (2 * c.sample_rate));
            fflush(stdout);
            CHECK(snr > 13 && fabs(mean / (2 * c.sample_rate)) < .0001);
            CHECK(bf_aec_deinit(s) == BF_OK);
            free(raw);
        }
}
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
    near_only_response();
    for(unsigned ri=0;ri<2;++ri)
        for(unsigned shape=0;shape<4;++shape)
            for(unsigned mode=0;mode<3;++mode)
                multichannel_reset_case(ri ? 48000u:16000u,shape==0 ? 1u:shape==3 ? 8u:2u,shape==1 ? 1u:2u,mode);
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
    near_time.frame_position += 160;
    far_time.frame_position += 160;
    ++far_time.epoch;
    ++far_time.generation;
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) == BF_OK &&
          info.resets == 2 && (info.flags & BF_AUDIO_DISCONTINUITY) &&
          (info.flags & BF_REFERENCE_GENERATION_CHANGED));
    bf_audio_timing missing = BF_INIT(bf_audio_timing);
    missing.uncertainty_ns = UINT64_MAX;
    near_time.frame_position += 160;
    near[0] = 1.25f;
    CHECK(bf_aec_process(s, near, &near_time, NULL, &missing, out, &info) == BF_OK);
    CHECK((info.flags & BF_REFERENCE_MISSING) &&
          !(info.flags & (BF_REFERENCE_GENERATION_CHANGED | BF_AUDIO_CLIPPED)) &&
          !info.reference_generation && !memcmp(near, out, sizeof(out)));
    near_time.frame_position += 160;
    CHECK(bf_aec_process(s, near, &near_time, NULL, NULL, out, &info) == BF_OK);
    CHECK((info.flags & BF_REFERENCE_MISSING) &&
          !(info.flags & (BF_REFERENCE_GENERATION_CHANGED | BF_AUDIO_CLIPPED)) &&
          !info.reference_generation && !memcmp(near, out, sizeof(out)));
    CHECK(bf_aec_process(s, near, &near_time, far_signal, NULL, out, &info) == BF_ERROR_INVALID);
    CHECK(bf_aec_reset(s) == BF_OK);
    memset(near, 0, sizeof(near));
    memset(far_signal, 0, 160 * sizeof(float));
    near[0] = far_signal[0] = -1;
    near[1] = far_signal[1] = 32767.0f / 32768;
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) == BF_OK &&
          !(info.flags & BF_AUDIO_CLIPPED));
    near_time.frame_position += 160;
    far_time.frame_position += 160;
    near[0] = far_signal[0] = nextafterf(1.0f, 0.0f); /* saturates below +1 */
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) == BF_OK &&
          (info.flags & BF_AUDIO_CLIPPED));
    near[0] = NAN;
    unsigned char *snapshot = malloc(bytes);
    CHECK(snapshot);
    memcpy(snapshot, memory, bytes);
    float previous[160];
    memcpy(previous, out, sizeof(previous));
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) ==
          BF_ERROR_NONFINITE);
    CHECK(!memcmp(snapshot, memory, bytes) && !memcmp(previous, out, sizeof(previous)));
    near[0] = 0;
    far_signal[0] = INFINITY;
    CHECK(bf_aec_process(s, near, &near_time, far_signal, &far_time, out, &info) ==
          BF_ERROR_NONFINITE);
    CHECK(!memcmp(snapshot, memory, bytes) && !memcmp(previous, out, sizeof(previous)));
    free(snapshot);
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
