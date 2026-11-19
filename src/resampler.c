#include "baffle_internal.h"
#define BAFFLE_PHASES 256u
#define BAFFLE_RESAMPLE_MAGIC 0x42465253u
struct bf_resampler {
    uint32_t magic, taps, channels, max_input, input_rate, output_rate, write_index, flushing,
        padding;
    int32_t current_ppm, target_ppm;
    uint64_t accepted, next_index, phase, denominator, output_count;
    float *history, *table;
};
static bf_result resample_plan(const bf_resampler_config *c, uint32_t *taps, size_t *bytes)
{
    size_t n;
    if (!BAFFLE_HEADER(c) || c->input_rate < 8000 || c->input_rate > 192000 ||
        c->output_rate < 8000 || c->output_rate > 192000 || !c->channels ||
        c->channels > BF_MAX_CHANNELS || !c->max_input_frames ||
        c->max_input_frames > BF_MAX_FRAMES)
        return BF_ERROR_INVALID;
    *taps = 64u * ((c->input_rate + c->output_rate - 1u) / c->output_rate);
    *bytes = baffle_align(sizeof(bf_resampler));
    if (!baffle_mul(*taps, c->channels, &n) || !baffle_mul(n, sizeof(float), &n) ||
        !baffle_add(bytes, n) ||
        !baffle_mul((size_t)*taps * (BAFFLE_PHASES + 1u), sizeof(float), &n) ||
        !baffle_add(bytes, n))
        return BF_ERROR_OVERFLOW;
    return BF_OK;
}
bf_result bf_resampler_size(const bf_resampler_config *c, size_t *bytes)
{
    uint32_t taps;
    if (!bytes)
        return BF_ERROR_INVALID;
    return resample_plan(c, &taps, bytes);
}
bf_result bf_resampler_latency(const bf_resampler_config *c, uint32_t *delay)
{
    uint32_t taps;
    size_t bytes;
    if (!delay)
        return BF_ERROR_INVALID;
    bf_result r = resample_plan(c, &taps, &bytes);
    if (r != BF_OK)
        return r;
    *delay = taps - 1u;
    return BF_OK;
}
bf_result bf_resampler_init(void *p, size_t bytes, const bf_resampler_config *c, bf_resampler **out)
{
    size_t need;
    uint32_t taps;
    bf_result r;
    if (!out)
        return BF_ERROR_INVALID;
    *out = NULL;
    r = resample_plan(c, &taps, &need);
    if (r != BF_OK)
        return r;
    if (!baffle_storage(p, bytes, need))
        return BF_ERROR_CAPACITY;
    memset(p, 0, need);
    bf_resampler *s = p;
    s->magic = BAFFLE_RESAMPLE_MAGIC;
    s->taps = taps;
    s->channels = c->channels;
    s->max_input = c->max_input_frames;
    s->input_rate = c->input_rate;
    s->output_rate = c->output_rate;
    s->denominator = (uint64_t)c->output_rate * 1000000u;
    s->history = (float *)((unsigned char *)p + baffle_align(sizeof(*s)));
    s->table = s->history + (size_t)taps * c->channels;
    double cutoff = .9 * fmin(1.0, (double)c->output_rate / (double)c->input_rate),
           delay = (double)(taps - 1u) / 2.0;
    for (uint32_t phase = 0; phase <= BAFFLE_PHASES; ++phase) {
        double sum = 0;
        for (uint32_t k = 0; k < taps; ++k) {
            double x = (double)k + (double)phase / BAFFLE_PHASES - delay, a = x / delay;
            double window = fabs(a) > 1 ? 0
                                        : .35875 + .48829 * cos(BAFFLE_PI * a) +
                                              .14128 * cos(2 * BAFFLE_PI * a) +
                                              .01168 * cos(3 * BAFFLE_PI * a);
            double h = fabs(x) < 1e-12 ? cutoff : sin(BAFFLE_PI * cutoff * x) / (BAFFLE_PI * x);
            s->table[(size_t)phase * taps + k] = (float)(h * window);
            sum += h * window;
        }
        for (uint32_t k = 0; k < taps; ++k)
            s->table[(size_t)phase * taps + k] =
                (float)((double)s->table[(size_t)phase * taps + k] / sum);
    }
    *out = s;
    return BF_OK;
}
bf_result bf_resampler_reset(bf_resampler *s)
{
    if (!s || s->magic != BAFFLE_RESAMPLE_MAGIC)
        return BF_ERROR_INVALID;
    memset(s->history, 0, (size_t)s->taps * s->channels * sizeof(float));
    s->accepted = s->next_index = s->phase = s->output_count = 0;
    s->write_index = s->flushing = s->padding = 0;
    s->current_ppm = s->target_ppm = 0;
    return BF_OK;
}
bf_result bf_resampler_set_drift(bf_resampler *s, int32_t ppm)
{
    if (!s || s->magic != BAFFLE_RESAMPLE_MAGIC || ppm < -2000 || ppm > 2000)
        return BF_ERROR_INVALID;
    if (s->flushing)
        return BF_ERROR_STATE;
    s->target_ppm = ppm;
    return BF_OK;
}
static bf_result resample_run(bf_resampler *s, const float *input, uint32_t frames, float *out,
                              uint32_t capacity, bf_resample_info *info, int flush)
{
    bf_resample_info v = BF_INIT(bf_resample_info);
    if (!BAFFLE_HEADER(info))
        return BF_ERROR_INVALID;
    baffle_record(info, &v, sizeof(v));
    if (!s || s->magic != BAFFLE_RESAMPLE_MAGIC || frames > s->max_input ||
        capacity > BF_MAX_FRAMES || (frames && !input && !flush) || (capacity && !out) ||
        (frames && input == out))
        return BF_ERROR_INVALID;
    if (!flush && s->flushing)
        return BF_ERROR_STATE;
    if (s->accepted > UINT64_MAX - BF_MAX_FRAMES * 32u ||
        s->next_index > UINT64_MAX - BF_MAX_FRAMES * 32u || s->output_count > UINT64_MAX - capacity)
        return BF_ERROR_OVERFLOW;
    if (!flush)
        for (size_t i = 0; i < (size_t)frames * s->channels; ++i) {
            if (!isfinite(input[i]))
                return BF_ERROR_NONFINITE;
            if (fabsf(input[i]) > 16)
                return BF_ERROR_INVALID;
        }
    if (flush && !s->flushing) {
        s->flushing = 1;
        s->padding = s->accepted ? s->taps - 1u : 0;
    }
    v.delay_twice_input_frames = s->taps - 1u;
    v.output_position = s->output_count;
    v.source_frame_position = s->next_index;
    v.source_phase = s->phase;
    v.source_phase_denominator = s->denominator;
    v.priming = s->next_index < s->taps / 2u;
    while (v.produced < capacity) {
        if (s->next_index >= s->accepted) {
            if (flush ? !s->padding : v.consumed == frames)
                break;
            for (uint32_t c = 0; c < s->channels; ++c)
                s->history[(size_t)s->write_index * s->channels + c] =
                    flush ? 0 : baffle_clean(input[(size_t)v.consumed * s->channels + c]);
            s->write_index = (s->write_index + 1u) % s->taps;
            ++s->accepted;
            if (flush)
                --s->padding;
            else
                ++v.consumed;
            continue;
        }
        /* Pending output is always emitted before accepting another input,
         * so this history contains exactly the latest next_index sample. */
        double phase = (double)s->phase / (double)s->denominator * BAFFLE_PHASES;
        uint32_t bin = (uint32_t)phase;
        double fraction = phase - bin;
        const float *a = s->table + (size_t)bin * s->taps, *b = a + s->taps;
        for (uint32_t c = 0; c < s->channels; ++c) {
            double sum = 0;
            for (uint32_t k = 0; k < s->taps; ++k) {
                uint32_t index = (s->write_index + s->taps - 1u - k) % s->taps;
                sum += (double)s->history[(size_t)index * s->channels + c] *
                       ((double)a[k] + ((double)b[k] - a[k]) * fraction);
            }
            out[(size_t)v.produced * s->channels + c] = (float)sum;
        }
        ++v.produced;
        ++s->output_count;
        if (s->current_ppm < s->target_ppm)
            ++s->current_ppm;
        else if (s->current_ppm > s->target_ppm)
            --s->current_ppm;
        s->phase += (uint64_t)s->input_rate * (uint32_t)(1000000 + s->current_ppm);
        s->next_index += s->phase / s->denominator;
        s->phase %= s->denominator;
    }
    baffle_record(info, &v, sizeof(v));
    return s->next_index < s->accepted || (flush ? s->padding != 0 : v.consumed < frames)
               ? BF_NEED_OUTPUT
               : BF_OK;
}
bf_result bf_resampler_process(bf_resampler *s, const float *in, uint32_t frames, float *out,
                               uint32_t capacity, bf_resample_info *info)
{
    return resample_run(s, in, frames, out, capacity, info, 0);
}
bf_result bf_resampler_flush(bf_resampler *s, float *out, uint32_t capacity, bf_resample_info *info)
{
    return resample_run(s, NULL, 0, out, capacity, info, 1);
}
