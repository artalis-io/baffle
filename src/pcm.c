#include "baffle_internal.h"
bf_result bf_pcm_validate(const bf_audio_buffer *b)
{
    const bf_pcm_format *f;
    if (!BAFFLE_HEADER(b))
        return BF_ERROR_INVALID;
    f = &b->format;
    if (!BAFFLE_HEADER(f) || !BAFFLE_HEADER(&b->timing) || f->sample_rate < 8000 ||
        f->sample_rate > 192000 || !f->channels || f->channels > BF_MAX_CHANNELS || f->reserved ||
        b->timing.reserved || b->frames > b->capacity_frames ||
        b->capacity_frames > BF_MAX_FRAMES ||
        (f->sample_format != BF_PCM_F32 && f->sample_format != BF_PCM_S16) ||
        (f->storage != BF_INTERLEAVED && f->storage != BF_PLANAR) ||
        !((f->channel_layout == BF_LAYOUT_MONO && f->channels == 1) ||
          (f->channel_layout == BF_LAYOUT_STEREO && f->channels == 2) ||
          f->channel_layout == BF_LAYOUT_DISCRETE))
        return BF_ERROR_INVALID;
    if ((b->timing.frame_time_numerator == 0) != (b->timing.frame_time_denominator == 0) ||
        (b->timing.frame_time_denominator &&
         b->timing.time_remainder >= b->timing.frame_time_denominator) ||
        (!b->timing.frame_time_denominator && b->timing.time_remainder))
        return BF_ERROR_INVALID;
    if (b->capacity_frames) {
        if (f->storage == BF_INTERLEAVED && !b->data)
            return BF_ERROR_INVALID;
        if (f->storage == BF_PLANAR) {
            if (!b->planes)
                return BF_ERROR_INVALID;
            for (uint32_t c = 0; c < f->channels; ++c)
                if (!b->planes[c])
                    return BF_ERROR_INVALID;
        }
    }
    if (b->timing.frame_position > UINT64_MAX - b->frames ||
        ((b->timing.flags & BF_TIME_DEVICE_VALID) &&
         b->timing.device_position > UINT64_MAX - b->frames))
        return BF_ERROR_OVERFLOW;
    return BF_OK;
}
float baffle_sample(const bf_audio_buffer *b, uint32_t frame, uint32_t channel)
{
    const bf_pcm_format *f = &b->format;
    size_t i = f->storage == BF_PLANAR ? frame : (size_t)frame * f->channels + channel;
    const unsigned char *p = f->storage == BF_PLANAR ? b->planes[channel] : b->data;
    if (f->sample_format == BF_PCM_F32) {
        float x;
        memcpy(&x, p + i * sizeof(x), sizeof(x));
        return x;
    } else {
        int16_t x;
        memcpy(&x, p + i * sizeof(x), sizeof(x));
        return (float)x / 32768.0f;
    }
}
void baffle_store(bf_audio_buffer *b, uint32_t frame, uint32_t channel, float x)
{
    const bf_pcm_format *f = &b->format;
    size_t i = f->storage == BF_PLANAR ? frame : (size_t)frame * f->channels + channel;
    unsigned char *p = f->storage == BF_PLANAR ? b->planes[channel] : b->data;
    if (f->sample_format == BF_PCM_F32)
        memcpy(p + i * sizeof(x), &x, sizeof(x));
    else {
        int16_t v = x >= 32767.0f / 32768.0f ? INT16_MAX
                    : x <= -1.0f             ? INT16_MIN
                                             : (int16_t)lrintf(x * 32768.0f);
        memcpy(p + i * sizeof(v), &v, sizeof(v));
    }
}
bf_result baffle_finite(const bf_audio_buffer *b, uint32_t policy)
{
    if (policy > BF_NONFINITE_ZERO)
        return BF_ERROR_INVALID;
    if (policy == BF_NONFINITE_REJECT)
        for (uint32_t i = 0; i < b->frames; ++i)
            for (uint32_t c = 0; c < b->format.channels; ++c)
                if (!isfinite(baffle_sample(b, i, c)))
                    return BF_ERROR_NONFINITE;
    return BF_OK;
}
bf_result bf_pcm_convert(const bf_audio_buffer *in, bf_audio_buffer *out, uint32_t policy)
{
    bf_result r = bf_pcm_validate(in);
    if (r != BF_OK)
        return r;
    r = bf_pcm_validate(out);
    if (r != BF_OK)
        return r;
    if (in->format.channels != out->format.channels ||
        in->format.sample_rate != out->format.sample_rate)
        return BF_ERROR_UNSUPPORTED;
    if (out->capacity_frames < in->frames)
        return BF_ERROR_CAPACITY;
    r = baffle_finite(in, policy);
    if (r != BF_OK)
        return r;
    uint32_t sanitized = 0, clipped = 0;
    for (uint32_t i = 0; i < in->frames; ++i)
        for (uint32_t c = 0; c < in->format.channels; ++c) {
            float x = baffle_sample(in, i, c), clean = baffle_clean(x);
            if (!isfinite(x) || (x != 0 && fabsf(x) < FLT_MIN))
                sanitized = 1;
            if (out->format.sample_format == BF_PCM_S16 &&
                (clean > 32767.0f / 32768.0f || clean < -1.0f))
                clipped = 1;
            baffle_store(out, i, c, clean);
        }
    out->frames = in->frames;
    baffle_record(&out->timing, &in->timing, sizeof(in->timing));
    if (sanitized)
        out->timing.flags |= BF_AUDIO_SANITIZED;
    if (clipped)
        out->timing.flags |= BF_AUDIO_CLIPPED;
    return BF_OK;
}
bf_result bf_timing_advance(const bf_audio_timing *in, uint32_t frames, uint32_t rate,
                            bf_audio_timing *out)
{
    bf_audio_timing value;
    uint64_t delta;
    if (!BAFFLE_HEADER(in) || !BAFFLE_HEADER(out) || frames > BF_MAX_FRAMES || rate < 8000 ||
        rate > 192000)
        return BF_ERROR_INVALID;
    value = *in;
    delta = 0;
    if (value.frame_position > UINT64_MAX - frames ||
        ((value.flags & BF_TIME_DEVICE_VALID) && value.device_position > UINT64_MAX - frames))
        return BF_ERROR_OVERFLOW;
    value.frame_position += frames;
    if (frames)
        value.flags &= ~(BF_AUDIO_GAP | BF_AUDIO_DISCONTINUITY);
    if (value.flags & BF_TIME_DEVICE_VALID)
        value.device_position += frames;
    if (value.flags & BF_TIME_HOST_VALID) {
        uint64_t numerator = value.frame_time_numerator, denominator = value.frame_time_denominator;
        if (!numerator && !denominator) {
            numerator = UINT64_C(1000000000);
            denominator = rate;
        }
        if (!numerator || !denominator || value.time_remainder >= denominator)
            return BF_ERROR_INVALID;
        if (frames && numerator > UINT64_MAX / frames)
            return BF_ERROR_OVERFLOW;
        uint64_t total = (uint64_t)frames * numerator;
        if (value.time_remainder > UINT64_MAX - total)
            return BF_ERROR_OVERFLOW;
        total += value.time_remainder;
        delta = total / denominator;
        value.time_remainder = total % denominator;
        value.frame_time_numerator = numerator;
        value.frame_time_denominator = denominator;
        if (value.host_time_ns > UINT64_MAX - delta)
            return BF_ERROR_OVERFLOW;
        value.host_time_ns += delta;
        if (frames)
            value.flags |= BF_TIME_INTERPOLATED;
    }
    baffle_record(out, &value, sizeof(value));
    return BF_OK;
}
