/* Gate J rejected experiment, preserved as C11 QA code, not production DSP. */
#include "../common/demo.h"
#include "residual_experiment_c.h"
typedef struct Z {
    float r, i;
} Z;
static Z add(Z a, Z b)
{
    return (Z){a.r + b.r, a.i + b.i};
}
static Z sub(Z a, Z b)
{
    return (Z){a.r - b.r, a.i - b.i};
}
static Z mul(Z a, Z b)
{
    return (Z){a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r};
}
static Z scale(Z a, float x)
{
    return (Z){a.r * x, a.i * x};
}
static Z conjugate(Z a)
{
    return (Z){a.r, -a.i};
}
static float norm(Z a)
{
    return a.r * a.r + a.i * a.i;
}
struct ResidualExperiment {
    uint32_t frames, n, seen;
    int enabled;
    uint64_t resets;
    bf_aec *aec;
    demo_arena arena;
    Z x[512], y[512], cross[512], twiddle[256];
    float px[512], py[512], gain[512], residual[480];
};
static void fft(ResidualExperiment *s, Z *x, int inverse)
{
    uint32_t n = s->n;
    for (uint32_t i = 1, j = 0; i < n; ++i) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            Z temp = x[i];
            x[i] = x[j];
            x[j] = temp;
        }
    }
    for (uint32_t len = 2; len <= n; len *= 2)
        for (uint32_t first = 0; first < n; first += len)
            for (uint32_t k = 0; k < len / 2; ++k) {
                Z w = s->twiddle[k * (n / len)];
                if (inverse)
                    w = conjugate(w);
                Z u = x[first + k], v = mul(x[first + k + len / 2], w);
                x[first + k] = add(u, v);
                x[first + k + len / 2] = sub(u, v);
            }
    if (inverse)
        for (uint32_t i = 0; i < n; ++i) {
            x[i].r /= (float)n;
            x[i].i /= (float)n;
        }
}
static void reset(ResidualExperiment *s)
{
    s->seen = 0;
    memset(s->cross, 0, sizeof(s->cross));
    memset(s->px, 0, sizeof(s->px));
    memset(s->py, 0, sizeof(s->py));
    for (uint32_t i = 0; i < s->n; ++i)
        s->gain[i] = 1;
}
bf_result residual_open(uint32_t rate, int enabled, ResidualExperiment **out)
{
    if (!out || (rate != 8000 && rate != 16000 && rate != 32000 && rate != 48000))
        return BF_ERROR_INVALID;
    *out = NULL;
    ResidualExperiment *s = (ResidualExperiment *)calloc(1, sizeof(*s));
    if (!s)
        return BF_ERROR_CAPACITY;
    s->frames = rate / 100;
    s->n = 1;
    s->enabled = enabled;
    while (s->n < s->frames)
        s->n *= 2;
    for (uint32_t k = 0; k < s->n / 2; ++k) {
        double angle = -2 * 3.14159265358979323846 * k / s->n;
        s->twiddle[k] = (Z){(float)cos(angle), (float)sin(angle)};
    }
    reset(s);
    bf_aec_config c = BF_INIT(bf_aec_config);
    c.sample_rate = rate;
    c.frame_frames = s->frames;
    c.capture_channels = c.reference_channels = 1;
    c.tail_ms = 200;
    c.backend = BF_AEC_SPEEXDSP;
    size_t bytes = 0;
    bf_result r = bf_aec_size(&c, &bytes);
    if (r != BF_OK) {
        residual_close(s);
        return r;
    }
    if (!demo_arena_init(&s->arena, bytes, BF_ALIGNMENT)) {
        residual_close(s);
        return BF_ERROR_CAPACITY;
    }
    r = bf_aec_init(s->arena.aligned, bytes, &c, &s->aec);
    if (r != BF_OK) {
        residual_close(s);
        return r;
    }
    *out = s;
    return BF_OK;
}
void residual_close(ResidualExperiment *s)
{
    if (s) {
        if (s->aec)
            (void)bf_aec_deinit(s->aec);
        demo_arena_free(&s->arena);
        free(s);
    }
}
size_t residual_bytes(const ResidualExperiment *s)
{
    return s ? sizeof(*s) + s->arena.bytes + BF_ALIGNMENT - 1 : 0;
}
bf_result residual_process(ResidualExperiment *s, float *mic, const bf_audio_timing *nt,
                           const float *ref, const bf_audio_timing *rt, bf_aec_info *info)
{
    if (!s || !mic || !nt || !info)
        return BF_ERROR_INVALID;
    bf_result r = bf_aec_process(s->aec, mic, nt, ref, rt, s->residual, info);
    if (r != BF_OK)
        return r;
    if (info->resets != s->resets || (info->flags & BF_REFERENCE_GENERATION_CHANGED) || !ref)
        reset(s);
    s->resets = info->resets;
    double reference_energy = 0;
    if (ref)
        for (uint32_t i = 0; i < s->frames; ++i)
            reference_energy += (double)ref[i] * ref[i];
    if (!s->enabled || reference_energy <= 1e-20) {
        reset(s);
        memcpy(mic, s->residual, s->frames * sizeof(float));
        return BF_OK;
    }
    memset(s->x, 0, sizeof(s->x));
    memset(s->y, 0, sizeof(s->y));
    double echo = 0;
    for (uint32_t i = 0; i < s->frames; ++i) {
        float e = mic[i] - s->residual[i];
        s->x[i].r = e;
        s->y[i].r = s->residual[i];
        echo += (double)e * e;
    }
    if (echo <= 1e-20) {
        reset(s);
        memcpy(mic, s->residual, s->frames * sizeof(float));
        return BF_OK;
    }
    fft(s, s->x, 0);
    fft(s, s->y, 0);
    const float alpha = .904837418f;
    if (s->seen < 20)
        ++s->seen;
    for (uint32_t k = 0; k < s->n; ++k) {
        s->px[k] = alpha * s->px[k] + (1 - alpha) * norm(s->x[k]);
        s->py[k] = alpha * s->py[k] + (1 - alpha) * norm(s->y[k]);
        s->cross[k] =
            add(scale(s->cross[k], alpha), mul(scale(conjugate(s->x[k]), 1 - alpha), s->y[k]));
        double den = (double)s->px[k] * s->py[k];
        float coherence = den > 1e-30 ? (float)fmin(1.0, (double)norm(s->cross[k]) / den) : 0;
        float target = s->seen >= 20 ? sqrtf(fmaxf(0, 1 - coherence)) : 1,
              smooth = target < s->gain[k] ? .135335283f : .818730753f;
        s->gain[k] = smooth * s->gain[k] + (1 - smooth) * target;
        s->y[k] = scale(s->y[k], s->gain[k]);
    }
    fft(s, s->y, 1);
    for (uint32_t i = 0; i < s->frames; ++i)
        mic[i] = s->y[i].r;
    return BF_OK;
}
