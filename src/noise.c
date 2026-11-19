#include "baffle_internal.h"
struct bf_noise {
    uint32_t magic, hop, nfft, channels, bins, seen, window_frames;
    float floor_gain;
    float *previous, *overlap, *window, *real, *imag, *twiddle_r, *twiddle_i, *noise, *smooth,
        *gain, *minimum;
};
static bf_result noise_plan(const bf_noise_config *c, uint32_t *nfft, size_t *bytes)
{
    if (!BAFFLE_HEADER(c) || c->sample_rate < 8000 || c->sample_rate > 192000 || !c->channels ||
        c->channels > BF_MAX_CHANNELS || !c->frame_frames || c->frame_frames > 960 ||
        !isfinite(c->strength_db) || c->strength_db < 0 || c->strength_db > 40)
        return BF_ERROR_INVALID;
    *nfft = 1;
    while (*nfft < 2 * c->frame_frames)
        *nfft *= 2;
    size_t count = (size_t)2 * c->frame_frames * c->channels + 2 * c->frame_frames + 3u * (*nfft) +
                   (size_t)4 * (*nfft / 2u + 1u) * c->channels;
    *bytes = baffle_align(sizeof(bf_noise));
    return baffle_add(bytes, count * sizeof(float)) ? BF_OK : BF_ERROR_OVERFLOW;
}
bf_result bf_noise_size(const bf_noise_config *c, size_t *bytes)
{
    uint32_t n;
    if (!bytes)
        return BF_ERROR_INVALID;
    return noise_plan(c, &n, bytes);
}
bf_result bf_noise_init(void *p, size_t bytes, const bf_noise_config *c, bf_noise **out)
{
    uint32_t n;
    size_t need;
    bf_result r;
    if (!out)
        return BF_ERROR_INVALID;
    *out = NULL;
    r = noise_plan(c, &n, &need);
    if (r != BF_OK)
        return r;
    if (!baffle_storage(p, bytes, need))
        return BF_ERROR_CAPACITY;
    memset(p, 0, need);
    bf_noise *s = p;
    s->magic = 0x42464e53u;
    s->hop = c->frame_frames;
    s->nfft = n;
    s->bins = n / 2 + 1;
    s->channels = c->channels;
    s->floor_gain = powf(10, -c->strength_db / 20);
    s->window_frames = c->sample_rate / (5u * c->frame_frames);
    if (!s->window_frames)
        s->window_frames = 1;
    s->previous = (float *)((unsigned char *)p + baffle_align(sizeof(*s)));
    s->overlap = s->previous + (size_t)s->hop * s->channels;
    s->window = s->overlap + (size_t)s->hop * s->channels;
    s->real = s->window + 2 * s->hop;
    s->imag = s->real + n;
    s->twiddle_r = s->imag + n;
    s->twiddle_i = s->twiddle_r + n / 2;
    s->noise = s->twiddle_i + n / 2;
    s->smooth = s->noise + (size_t)s->bins * s->channels;
    s->gain = s->smooth + (size_t)s->bins * s->channels;
    s->minimum = s->gain + (size_t)s->bins * s->channels;
    for (uint32_t i = 0; i < 2 * s->hop; ++i)
        s->window[i] = (float)sqrt(.5 - .5 * cos(2 * BAFFLE_PI * ((double)i + .5) / (2 * s->hop)));
    for (uint32_t i = 0; i < n / 2; ++i) {
        s->twiddle_r[i] = (float)cos(2 * BAFFLE_PI * i / n);
        s->twiddle_i[i] = (float)sin(2 * BAFFLE_PI * i / n);
    }
    for (size_t i = 0; i < (size_t)s->bins * s->channels; ++i)
        s->gain[i] = 1;
    *out = s;
    return BF_OK;
}
bf_result bf_noise_reset(bf_noise *s)
{
    if (!s || s->magic != 0x42464e53u)
        return BF_ERROR_INVALID;
    memset(s->previous, 0, (size_t)2 * s->hop * s->channels * sizeof(float));
    memset(s->noise, 0, (size_t)4 * s->bins * s->channels * sizeof(float));
    for (size_t i = 0; i < (size_t)s->bins * s->channels; ++i)
        s->gain[i] = 1;
    s->seen = 0;
    return BF_OK;
}
static void fft(bf_noise *s, int inverse)
{
    uint32_t n = s->nfft;
    for (uint32_t i = 1, j = 0; i < n; ++i) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = s->real[i];
            s->real[i] = s->real[j];
            s->real[j] = t;
            t = s->imag[i];
            s->imag[i] = s->imag[j];
            s->imag[j] = t;
        }
    }
    for (uint32_t len = 2; len <= n; len *= 2)
        for (uint32_t base = 0; base < n; base += len)
            for (uint32_t j = 0; j < len / 2; ++j) {
                uint32_t k = j * (n / len), a = base + j, b = a + len / 2;
                float wr = s->twiddle_r[k], wi = inverse ? s->twiddle_i[k] : -s->twiddle_i[k];
                float tr = wr * s->real[b] - wi * s->imag[b],
                      ti = wr * s->imag[b] + wi * s->real[b];
                s->real[b] = s->real[a] - tr;
                s->imag[b] = s->imag[a] - ti;
                s->real[a] += tr;
                s->imag[a] += ti;
            }
    if (inverse)
        for (uint32_t i = 0; i < n; ++i) {
            s->real[i] /= (float)n;
            s->imag[i] /= (float)n;
        }
}
bf_result bf_noise_process(bf_noise *s, const float *in, float *out, float speech)
{
    if (!s || s->magic != 0x42464e53u || !in || !out || !isfinite(speech) ||
        !(speech == -1 || (speech >= 0 && speech <= 1)))
        return BF_ERROR_INVALID;
    for (size_t i = 0; i < (size_t)s->hop * s->channels; ++i) {
        if (!isfinite(in[i]))
            return BF_ERROR_NONFINITE;
        if (fabsf(in[i]) > 16)
            return BF_ERROR_INVALID;
    }
    for (uint32_t c = 0; c < s->channels; ++c) {
        for (uint32_t i = 0; i < s->hop; ++i) {
            size_t j = (size_t)c * s->hop + i;
            s->real[i] = s->previous[j] * s->window[i];
            s->previous[j] = baffle_clean(in[(size_t)i * s->channels + c]);
            s->real[i + s->hop] = s->previous[j] * s->window[i + s->hop];
        }
        memset(s->real + 2 * s->hop, 0, (s->nfft - 2 * s->hop) * sizeof(float));
        memset(s->imag, 0, s->nfft * sizeof(float));
        fft(s, 0);
        for (uint32_t k = 0; k < s->bins; ++k) {
            size_t j = (size_t)c * s->bins + k;
            float power = s->real[k] * s->real[k] + s->imag[k] * s->imag[k];
            if (!s->seen) {
                s->noise[j] = power * .01f;
                s->smooth[j] = s->minimum[j] = power;
            }
            s->smooth[j] = .8f * s->smooth[j] + .2f * power;
            s->minimum[j] = fminf(s->minimum[j], s->smooth[j]);
            if (speech >= 0 && speech < .2f)
                s->noise[j] += .1f * (power - s->noise[j]);
            else if (speech < 0 && s->seen % s->window_frames == s->window_frames - 1u) {
                s->noise[j] = .8f * s->noise[j] + .2f * 1.5f * s->minimum[j];
                s->minimum[j] = s->smooth[j];
            }
            float desired =
                sqrtf(fmaxf(s->floor_gain * s->floor_gain, 1 - s->noise[j] / fmaxf(power, 1e-15f)));
            s->gain[j] = .8f * s->gain[j] + .2f * desired;
            float g = s->gain[j];
            s->real[k] *= g;
            s->imag[k] *= g;
            if (k && k < s->nfft / 2) {
                s->real[s->nfft - k] *= g;
                s->imag[s->nfft - k] *= g;
            }
        }
        fft(s, 1);
        for (uint32_t i = 0; i < s->hop; ++i) {
            size_t j = (size_t)c * s->hop + i;
            out[(size_t)i * s->channels + c] =
                s->seen ? baffle_clean(s->real[i] * s->window[i] + s->overlap[j]) : 0;
            s->overlap[j] = s->real[i + s->hop] * s->window[i + s->hop];
        }
    }
    if (s->seen == UINT32_MAX)
        s->seen = s->window_frames;
    else
        ++s->seen;
    return BF_OK;
}
