#include "baffle_internal.h"
struct bf_agc {
    uint32_t magic;
    bf_agc_config config;
    float gain, limiter, target, min_gain, max_gain, attack, release, gate, limit;
};
struct bf_vad {
    uint32_t magic;
    bf_vad_config config;
    float noise, floor, previous[BF_MAX_CHANNELS];
    uint64_t onset, hang;
};
static int geometry(uint32_t rate, uint32_t channels, uint32_t frames)
{
    return rate >= 8000 && rate <= 192000 && channels && channels <= BF_MAX_CHANNELS && frames &&
           frames <= BF_MAX_FRAMES;
}
static int probability(float p)
{
    return isfinite(p) && (p == -1 || (p >= 0 && p <= 1));
}
static bf_result samples_valid(const float *p, uint32_t frames, uint32_t channels)
{
    if (frames && !p)
        return BF_ERROR_INVALID;
    for (size_t i = 0; i < (size_t)frames * channels; ++i) {
        if (!isfinite(p[i]))
            return BF_ERROR_NONFINITE;
        if (fabsf(p[i]) > 16)
            return BF_ERROR_INVALID;
    }
    return BF_OK;
}
size_t bf_agc_size(void)
{
    return baffle_align(sizeof(bf_agc));
}
bf_result bf_agc_init(void *p, size_t bytes, const bf_agc_config *c, bf_agc **out)
{
    if (!out)
        return BF_ERROR_INVALID;
    *out = NULL;
    if (!BAFFLE_HEADER(c) || !geometry(c->sample_rate, c->channels, c->max_frames) || c->reserved ||
        !isfinite(c->target_dbfs) || c->target_dbfs < -60 || c->target_dbfs > -1 ||
        !isfinite(c->min_gain_db) || c->min_gain_db < -60 || c->min_gain_db > 0 ||
        !isfinite(c->max_gain_db) || c->max_gain_db < 0 || c->max_gain_db > 40 ||
        !isfinite(c->attack_ms) || c->attack_ms < .1f || c->attack_ms > 1000 ||
        !isfinite(c->release_ms) || c->release_ms < 1 || c->release_ms > 5000 ||
        !isfinite(c->gate_dbfs) || c->gate_dbfs < -120 || c->gate_dbfs > -10 ||
        !isfinite(c->limit_dbfs) || c->limit_dbfs < -20 || c->limit_dbfs > 0)
        return BF_ERROR_INVALID;
    if (!baffle_storage(p, bytes, bf_agc_size()))
        return BF_ERROR_CAPACITY;
    memset(p, 0, bf_agc_size());
    bf_agc *s = p;
    s->magic = 0x42464147u;
    s->config = *c;
    s->gain = s->limiter = 1;
    s->target = powf(10, c->target_dbfs / 20);
    s->min_gain = powf(10, c->min_gain_db / 20);
    s->max_gain = powf(10, c->max_gain_db / 20);
    s->gate = powf(10, c->gate_dbfs / 20);
    s->limit = powf(10, c->limit_dbfs / 20);
    s->attack = expf(-1000 / ((float)c->sample_rate * c->attack_ms));
    s->release = expf(-1000 / ((float)c->sample_rate * c->release_ms));
    *out = s;
    return BF_OK;
}
bf_result bf_agc_reset(bf_agc *s)
{
    if (!s || s->magic != 0x42464147u)
        return BF_ERROR_INVALID;
    s->gain = s->limiter = 1;
    return BF_OK;
}
bf_result bf_agc_process(bf_agc *s, const float *in, float *out, uint32_t frames, float speech,
                         bf_level_info *info)
{
    bf_level_info v = BF_INIT(bf_level_info);
    bf_result r;
    if (!s || s->magic != 0x42464147u || !BAFFLE_HEADER(info) || frames > s->config.max_frames ||
        (frames && !out) || !probability(speech))
        return BF_ERROR_INVALID;
    r = samples_valid(in, frames, s->config.channels);
    if (r != BF_OK)
        return r;
    double power = 0, opower = 0;
    uint32_t channels = s->config.channels;
    for (size_t i = 0; i < (size_t)frames * channels; ++i)
        power += (double)in[i] * in[i];
    v.input_rms = frames ? (float)sqrt(power / ((double)frames * channels)) : 0;
    float desired = s->gain;
    if (v.input_rms >= s->gate) {
        float target =
            fmaxf(s->min_gain, fminf(s->max_gain, s->target / fmaxf(v.input_rms, 1e-10f)));
        if (target < s->gain || speech < 0 || speech >= .5f)
            desired = target;
    }
    for (uint32_t i = 0; i < frames; ++i) {
        float alpha = desired < s->gain ? s->attack : s->release;
        s->gain = alpha * s->gain + (1 - alpha) * desired;
        float peak = 0;
        for (uint32_t c = 0; c < channels; ++c)
            peak = fmaxf(peak, fabsf(in[(size_t)i * channels + c] * s->gain));
        float need = peak > s->limit ? s->limit / peak : 1;
        s->limiter = need < s->limiter ? need : s->release * s->limiter + (1 - s->release) * need;
        for (uint32_t c = 0; c < channels; ++c) {
            float x = in[(size_t)i * channels + c] * s->gain * s->limiter;
            x = fmaxf(-s->limit, fminf(s->limit, baffle_clean(x)));
            if (s->limiter < .99999f)
                ++v.limited_samples;
            out[(size_t)i * channels + c] = x;
            opower += (double)x * x;
            v.output_peak = fmaxf(v.output_peak, fabsf(x));
        }
    }
    v.gain = s->gain * s->limiter;
    v.output_rms = frames ? (float)sqrt(opower / ((double)frames * channels)) : 0;
    baffle_record(info, &v, sizeof(v));
    return BF_OK;
}
size_t bf_vad_size(void)
{
    return baffle_align(sizeof(bf_vad));
}
bf_result bf_vad_init(void *p, size_t bytes, const bf_vad_config *c, bf_vad **out)
{
    if (!out)
        return BF_ERROR_INVALID;
    *out = NULL;
    if (!BAFFLE_HEADER(c) || !geometry(c->sample_rate, c->channels, c->max_frames) ||
        c->onset_ms > 1000 || c->hangover_ms > 5000 || !isfinite(c->threshold) ||
        c->threshold <= 0 || c->threshold >= 1 || !isfinite(c->noise_floor_dbfs) ||
        c->noise_floor_dbfs < -120 || c->noise_floor_dbfs > -20)
        return BF_ERROR_INVALID;
    if (!baffle_storage(p, bytes, bf_vad_size()))
        return BF_ERROR_CAPACITY;
    memset(p, 0, bf_vad_size());
    bf_vad *s = p;
    s->magic = 0x42465641u;
    s->config = *c;
    s->floor = powf(10, c->noise_floor_dbfs / 10);
    s->noise = s->floor;
    *out = s;
    return BF_OK;
}
bf_result bf_vad_reset(bf_vad *s)
{
    if (!s || s->magic != 0x42465641u)
        return BF_ERROR_INVALID;
    s->noise = s->floor;
    s->onset = s->hang = 0;
    memset(s->previous, 0, sizeof(s->previous));
    return BF_OK;
}
bf_result bf_vad_process(bf_vad *s, const float *in, uint32_t frames, bf_vad_info *out)
{
    bf_vad_info v = BF_INIT(bf_vad_info);
    bf_result r;
    if (!s || s->magic != 0x42465641u || !BAFFLE_HEADER(out) || frames > s->config.max_frames)
        return BF_ERROR_INVALID;
    r = samples_valid(in, frames, s->config.channels);
    if (r != BF_OK)
        return r;
    double power = 0, difference = 0;
    for (uint32_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < s->config.channels; ++c) {
            float x = baffle_clean(in[(size_t)i * s->config.channels + c]);
            double d = (double)x - s->previous[c];
            power += (double)x * x;
            difference += d * d;
            s->previous[c] = x;
        }
    if (frames) {
        float mean = (float)(power / ((double)frames * s->config.channels));
        float snr = mean / fmaxf(s->noise, 1e-12f);
        float voicing =
            .25f + .75f * (1 - fminf(1, (float)(difference / fmax(power, 1e-15)) / 1.5f));
        v.probability = fmaxf(0, fminf(1, (snr - 1) / (snr + 3))) * voicing;
        float alpha = (float)frames / ((float)s->config.sample_rate * .25f + (float)frames);
        if (v.probability >= s->config.threshold * .7f)
            alpha *= .01f;
        s->noise = fmaxf(s->floor * .1f, s->noise + alpha * (mean - s->noise));
        if (v.probability >= s->config.threshold) {
            uint64_t needed = (uint64_t)s->config.onset_ms * s->config.sample_rate / 1000u;
            s->onset = s->onset + frames < needed ? s->onset + frames : needed;
            if (s->onset >= needed)
                s->hang = (uint64_t)s->config.hangover_ms * s->config.sample_rate / 1000u + frames;
        } else
            s->onset = 0;
        v.active = s->hang != 0;
        s->hang = s->hang > frames ? s->hang - frames : 0;
    } else
        v.active = s->hang != 0;
    v.noise_rms = sqrtf(s->noise);
    baffle_record(out, &v, sizeof(v));
    return BF_OK;
}
