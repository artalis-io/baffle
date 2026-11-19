/* This adapter uses installed public headers ONLY, including Baffle. */
#include <baffle/hail.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
struct bf_hail_adapter {
    uint32_t magic, capture_count, reference_count, reference_offset, history_read, history_count,
        reference_seen;
    uint64_t reference_origin_frame, reference_output_index;
    bf_hail_config config;
    ha_stream *stream;
    ha_stream_info hail;
    bf_pipeline *pipeline;
    bf_resampler *resampler;
    bf_clock_tracker *capture_clock, *reference_clock;
    bf_audio_timing capture_origin, reference_anchor;
    bf_hail_status status;
    unsigned char *raw_capture, *raw_reference;
    float *capture, *reference_input, *history, *aligned;
    bf_audio_timing *points;
    bf_audio_timing *capture_points;
    uint32_t capture_delay;
    uint32_t flushing, delayed_valid_frames;
};
static uint32_t capture_delay(const ha_stream_info *h, const bf_hail_config *c)
{
    if (!(c->pipeline.stages & BF_STAGE_AEC))
        return 0;
    bf_resampler_config rc = BF_INIT(bf_resampler_config);
    uint32_t twice = 0;
    rc.input_rate = h->playback_format.sample_rate;
    rc.output_rate = h->capture_format.sample_rate;
    rc.channels = h->playback_format.channels;
    rc.max_input_frames = h->quantum_frames;
    if (bf_resampler_latency(&rc, &twice) != BF_OK)
        return 0;
    uint64_t denominator = 2u * (uint64_t)rc.input_rate;
    return (uint32_t)(((uint64_t)twice * rc.output_rate + denominator - 1u) / denominator);
}
static size_t aligned(size_t n)
{
    return (n + BF_ALIGNMENT - 1u) & ~(size_t)(BF_ALIGNMENT - 1u);
}
static int header(const void *p, size_t n)
{
    uint32_t h[2];
    if (!p)
        return 0;
    memcpy(h, p, 8);
    return h[0] >= n && h[1] == BF_STRUCT_VERSION;
}
static void record(void *out, const void *in, size_t n)
{
    memcpy((unsigned char *)out + 8, (const unsigned char *)in + 8, n - 8);
}
static void count(uint64_t *v, uint64_t n)
{
    *v = n > UINT64_MAX - *v ? UINT64_MAX : *v + n;
}
bf_result bf_hail_translate_timing(uint64_t id, const ha_block_info *h, bf_audio_timing *out)
{
    if (!h || h->struct_size < sizeof(*h) || h->struct_version != HA_STRUCT_VERSION ||
        !header(out, sizeof(*out)))
        return BF_ERROR_INVALID;
    bf_audio_timing t = BF_INIT(bf_audio_timing);
    t.stream_id = id;
    t.clock_domain = h->clock_domain;
    t.epoch = h->clock_epoch;
    t.generation = h->generation;
    t.frame_position = h->first_frame;
    t.device_position = h->device_position;
    t.host_time_ns = h->host_time_ns;
    t.uncertainty_ns = h->uncertainty_ns;
    t.lost_frames = h->lost_frames;
    t.frame_time_numerator = h->frame_time_numerator;
    t.frame_time_denominator = h->frame_time_denominator;
    t.time_remainder = h->time_remainder;
    if (h->flags & HA_TIME_HOST_VALID)
        t.flags |= BF_TIME_HOST_VALID;
    if (h->flags & HA_TIME_DEVICE_VALID)
        t.flags |= BF_TIME_DEVICE_VALID;
    if (h->flags & HA_TIME_ESTIMATED)
        t.flags |= BF_TIME_ESTIMATED;
    if (h->flags & HA_BLOCK_GAP)
        t.flags |= BF_AUDIO_GAP;
    if (h->flags & HA_BLOCK_DISCONTINUITY)
        t.flags |= BF_AUDIO_DISCONTINUITY;
    if (h->flags & HA_BLOCK_SILENCE)
        t.flags |= BF_AUDIO_SILENCE;
    if (h->flags & HA_BLOCK_UNKNOWN_GAP)
        t.flags |= BF_AUDIO_GAP | BF_AUDIO_DISCONTINUITY;
    record(out, &t, sizeof(t));
    return BF_OK;
}
static bf_pcm_format format(const ha_format *h)
{
    bf_pcm_format f = BF_INIT(bf_pcm_format);
    f.sample_rate = h->sample_rate;
    f.channels = h->channels;
    f.sample_format = h->sample_format == HA_PCM_F32 ? BF_PCM_F32 : BF_PCM_S16;
    f.storage = BF_INTERLEAVED;
    f.channel_layout = h->channel_layout == HA_LAYOUT_MONO     ? BF_LAYOUT_MONO
                       : h->channel_layout == HA_LAYOUT_STEREO ? BF_LAYOUT_STEREO
                                                               : BF_LAYOUT_DISCRETE;
    return f;
}
static bf_result setup(ha_stream *stream, const bf_hail_config *c, ha_stream_info *h,
                       size_t sizes[3], size_t *bytes)
{
    if (!stream || !header(c, sizeof(*c)) || c->allow_uncertain_reference > 1 ||
        !c->reference_history_frames || c->reference_history_frames > 65536)
        return BF_ERROR_INVALID;
    if (ha_stream_get_info(stream, h) != HA_OK)
        return BF_ERROR_STATE;
    if (!h->stream_id || h->directions != (HA_CAPTURE | HA_PLAYBACK) || !h->reference_slots ||
        h->capture_format.sample_rate != c->pipeline.sample_rate ||
        h->capture_format.channels != c->pipeline.channels ||
        h->playback_format.channels != c->pipeline.reference_channels ||
        c->reference_history_frames < 4 * c->pipeline.frame_frames)
        return BF_ERROR_UNSUPPORTED;
    bf_result r = bf_pipeline_size(&c->pipeline, &sizes[0]);
    if (r != BF_OK)
        return r;
    bf_resampler_config rc = BF_INIT(bf_resampler_config);
    rc.input_rate = h->playback_format.sample_rate;
    rc.output_rate = h->capture_format.sample_rate;
    rc.channels = h->playback_format.channels;
    rc.max_input_frames = h->quantum_frames;
    sizes[1] = 0;
    if (c->pipeline.stages & BF_STAGE_AEC) {
        r = bf_resampler_size(&rc, &sizes[1]);
        if (r != BF_OK)
            return r;
    }
    sizes[2] = bf_clock_tracker_size();
    *bytes = aligned(sizeof(bf_hail_adapter)) + aligned(sizes[0]) + aligned(sizes[1]) +
             2 * aligned(sizes[2]);
    *bytes += aligned((size_t)h->quantum_frames *
                      (h->capture_format.channels + h->playback_format.channels) * sizeof(float));
    uint32_t reference_channels = sizes[1] ? c->pipeline.reference_channels : 0;
    *bytes += aligned((size_t)c->pipeline.frame_frames *
                      (c->pipeline.channels + reference_channels) * sizeof(float));
    uint32_t delay = capture_delay(h, c);
    *bytes += aligned((size_t)delay * c->pipeline.channels * sizeof(float));
    *bytes += aligned((size_t)(c->pipeline.frame_frames + delay) * sizeof(bf_audio_timing));
    if (sizes[1]) {
        *bytes += aligned((size_t)h->quantum_frames * h->playback_format.channels * sizeof(float));
        *bytes += (size_t)c->reference_history_frames *
                  (sizeof(bf_audio_timing) + h->playback_format.channels * sizeof(float));
    }
    return BF_OK;
}
bf_result bf_hail_adapter_size(ha_stream *stream, const bf_hail_config *c, size_t *bytes)
{
    ha_stream_info h = HA_INIT(ha_stream_info);
    size_t sizes[3];
    if (!bytes)
        return BF_ERROR_INVALID;
    return setup(stream, c, &h, sizes, bytes);
}
bf_result bf_hail_adapter_init(void *p, size_t bytes, ha_stream *stream, const bf_hail_config *c,
                               bf_hail_adapter **out)
{
    size_t sizes[3], need;
    ha_stream_info h = HA_INIT(ha_stream_info);
    bf_result r;
    if (!out)
        return BF_ERROR_INVALID;
    *out = NULL;
    r = setup(stream, c, &h, sizes, &need);
    if (r != BF_OK)
        return r;
    if (!p || (uintptr_t)p % BF_ALIGNMENT || bytes < need)
        return BF_ERROR_CAPACITY;
    memset(p, 0, need);
    bf_hail_adapter *s = p;
    s->config = *c;
    s->stream = stream;
    s->hail = h;
    s->status = (bf_hail_status)BF_INIT(bf_hail_status);
    s->status.last_capture_source = (bf_audio_timing)BF_INIT(bf_audio_timing);
    s->status.last_reference_source = (bf_audio_timing)BF_INIT(bf_audio_timing);
    s->status.last_aligned_reference = (bf_audio_timing)BF_INIT(bf_audio_timing);
    s->status.capture_clock = (bf_clock_estimate)BF_INIT(bf_clock_estimate);
    s->status.reference_clock = (bf_clock_estimate)BF_INIT(bf_clock_estimate);
    unsigned char *cursor = (unsigned char *)p + aligned(sizeof(*s));
    r = bf_pipeline_init(cursor, sizes[0], &c->pipeline, &s->pipeline);
    if (r != BF_OK)
        return r;
    cursor += aligned(sizes[0]);
    bf_resampler_config rc = BF_INIT(bf_resampler_config);
    rc.input_rate = h.playback_format.sample_rate;
    rc.output_rate = h.capture_format.sample_rate;
    rc.channels = h.playback_format.channels;
    rc.max_input_frames = h.quantum_frames;
    if (sizes[1]) {
        r = bf_resampler_init(cursor, sizes[1], &rc, &s->resampler);
        if (r != BF_OK)
            return r;
    }
    cursor += aligned(sizes[1]);
    r = bf_clock_tracker_init(cursor, sizes[2], &s->capture_clock);
    if (r != BF_OK)
        return r;
    cursor += aligned(sizes[2]);
    r = bf_clock_tracker_init(cursor, sizes[2], &s->reference_clock);
    if (r != BF_OK)
        return r;
    cursor += aligned(sizes[2]);
    s->raw_capture = cursor;
    s->raw_reference =
        cursor + (size_t)h.quantum_frames * h.capture_format.channels * sizeof(float);
    cursor += aligned((size_t)h.quantum_frames *
                      (h.capture_format.channels + h.playback_format.channels) * sizeof(float));
    s->capture_delay = capture_delay(&h, c);
    s->capture = (float *)cursor;
    s->aligned =
        s->capture + (size_t)(c->pipeline.frame_frames + s->capture_delay) * c->pipeline.channels;
    cursor += aligned((size_t)c->pipeline.frame_frames *
                      (c->pipeline.channels + (sizes[1] ? c->pipeline.reference_channels : 0)) *
                      sizeof(float));
    cursor += aligned((size_t)s->capture_delay * c->pipeline.channels * sizeof(float));
    s->capture_points = (bf_audio_timing *)cursor;
    cursor +=
        aligned((size_t)(c->pipeline.frame_frames + s->capture_delay) * sizeof(bf_audio_timing));
    s->reference_input = (float *)cursor;
    if (sizes[1])
        cursor += aligned((size_t)h.quantum_frames * h.playback_format.channels * sizeof(float));
    s->points = (bf_audio_timing *)cursor;
    if (sizes[1])
        cursor += (size_t)c->reference_history_frames * sizeof(bf_audio_timing);
    s->history = (float *)cursor;
    s->magic = 0x42464841u;
    *out = s;
    return BF_OK;
}
static bf_audio_buffer view(void *data, bf_pcm_format f, uint32_t frames, bf_audio_timing t)
{
    bf_audio_buffer b = BF_INIT(bf_audio_buffer);
    b.data = data;
    b.format = f;
    b.frames = b.capacity_frames = frames;
    b.timing = t;
    return b;
}
static bf_result convert(void *raw, float *out, uint32_t frames, const ha_format *f,
                         bf_audio_timing *t, uint32_t policy)
{
    bf_pcm_format source = format(f), dest = source;
    dest.sample_format = BF_PCM_F32;
    bf_audio_buffer a = view(raw, source, frames, *t), b = view(out, dest, frames, *t);
    bf_result r = bf_pcm_convert(&a, &b, policy);
    if (r == BF_OK)
        *t = b.timing;
    return r;
}
static bf_audio_timing point_time(bf_hail_adapter *s, const bf_resample_info *ri)
{
    bf_audio_timing t = s->reference_anchor;
    t.frame_position = s->reference_output_index;
    if (s->reference_output_index != UINT64_MAX)
        ++s->reference_output_index;
    t.flags &= ~(BF_TIME_DEVICE_VALID | BF_AUDIO_GAP | BF_AUDIO_DISCONTINUITY);
    t.flags |= BF_TIME_ESTIMATED | BF_TIME_INTERPOLATED;
    t.device_position = 0;
    t.frame_time_numerator = 1000000000u;
    t.frame_time_denominator = s->config.pipeline.sample_rate;
    t.time_remainder = 0;
    if (ri->priming || t.frame_position == UINT64_MAX ||
        !(s->reference_anchor.flags & BF_TIME_HOST_VALID) ||
        ri->source_frame_position > UINT64_MAX - s->reference_origin_frame) {
        t.flags &= ~BF_TIME_HOST_VALID;
        return t;
    }
    uint64_t absolute = s->reference_origin_frame + ri->source_frame_position;
    /* Subtract integer origins before floating conversion: absolute frame
     * positions may exceed the mantissa of MSVC's double-width long double. */
    long double relative = absolute >= s->reference_anchor.frame_position
                               ? (long double)(absolute - s->reference_anchor.frame_position)
                               : -(long double)(s->reference_anchor.frame_position - absolute);
    long double source = relative +
                         (long double)ri->source_phase / (long double)ri->source_phase_denominator -
                         (long double)ri->delay_twice_input_frames / 2;
    long double per = s->reference_anchor.frame_time_denominator
                          ? (long double)s->reference_anchor.frame_time_numerator /
                                (long double)s->reference_anchor.frame_time_denominator
                          : 1e9L / (long double)s->hail.playback_format.sample_rate;
    long double offset = source * per;
    if (s->reference_anchor.frame_time_denominator)
        offset += (long double)s->reference_anchor.time_remainder /
                  (long double)s->reference_anchor.frame_time_denominator;
    if (offset < 0) {
        if (-offset > (long double)t.host_time_ns)
            t.flags &= ~BF_TIME_HOST_VALID;
        else
            t.host_time_ns -= (uint64_t)(-offset);
    } else if (offset > (long double)(UINT64_MAX - t.host_time_ns))
        t.flags &= ~BF_TIME_HOST_VALID;
    else
        t.host_time_ns += (uint64_t)offset;
    if (t.uncertainty_ns != UINT64_MAX)
        t.uncertainty_ns = t.uncertainty_ns > UINT64_MAX - 2 ? UINT64_MAX : t.uncertainty_ns + 2;
    return t;
}
static void push(bf_hail_adapter *s, const float *pcm, bf_audio_timing t)
{
    uint32_t cap = s->config.reference_history_frames, k = s->config.pipeline.reference_channels;
    if (s->history_count == cap) {
        s->history_read = (s->history_read + 1u) % cap;
        --s->history_count;
        count(&s->status.reference_overrun_frames, 1);
    }
    uint32_t i = (s->history_read + s->history_count) % cap;
    s->points[i] = t;
    memcpy(s->history + (size_t)i * k, pcm, k * sizeof(float));
    ++s->history_count;
}
static bf_result references(bf_hail_adapter *s)
{
    uint32_t rate = s->config.pipeline.sample_rate, k = s->config.pipeline.reference_channels;
    uint32_t budget = 2 * s->config.pipeline.frame_frames,
             reads = (s->hail.playback_format.sample_rate + rate - 1u) / rate + 1u;
    for (uint32_t read = 0; read < reads && budget; ++read) {
        if (s->reference_offset == s->reference_count) {
            ha_block_info hb = HA_INIT(ha_block_info);
            uint32_t n = 0;
            ha_result hr = ha_stream_read_reference(s->stream, s->raw_reference,
                                                    s->hail.quantum_frames, &n, &hb);
            if (hr == HA_BUSY || hr == HA_WOULD_BLOCK)
                break;
            if (hr != HA_OK)
                return BF_ERROR_STATE;
            if (!n)
                break;
            bf_audio_timing t = BF_INIT(bf_audio_timing);
            (void)bf_hail_translate_timing(s->hail.stream_id, &hb, &t);
            if (!s->reference_seen || t.epoch != s->status.last_reference_source.epoch ||
                t.clock_domain != s->status.last_reference_source.clock_domain ||
                t.frame_position !=
                    s->status.last_reference_source.frame_position + s->reference_count ||
                (t.flags & (BF_AUDIO_GAP | BF_AUDIO_DISCONTINUITY))) {
                (void)bf_resampler_reset(s->resampler);
                count(&s->status.reference_invalidated_frames, s->history_count);
                s->history_count = s->history_read = 0;
                s->reference_origin_frame = t.frame_position;
                s->reference_output_index = 0;
            }
            s->reference_anchor = t;
            s->status.last_reference_source = t;
            s->reference_seen = 1;
            (void)bf_clock_tracker_observe(s->reference_clock, &t,
                                           s->hail.playback_format.sample_rate,
                                           &s->status.reference_clock);
            if (!s->resampler) {
                s->reference_count = s->reference_offset = n;
                continue;
            }
            bf_result r = convert(s->raw_reference, s->reference_input, n, &s->hail.playback_format,
                                  &t, s->config.pipeline.nonfinite_policy);
            if (r != BF_OK)
                return r;
            s->reference_anchor = t;
            s->reference_count = n;
            s->reference_offset = 0;
        }
        uint32_t max_steps = s->hail.quantum_frames + budget;
        for (uint32_t step = 0; step < max_steps && budget; ++step) {
            bf_resample_info ri = BF_INIT(bf_resample_info);
            float pcm[BF_MAX_CHANNELS];
            uint32_t n = s->reference_offset < s->reference_count ? 1u : 0u;
            bf_result r = bf_resampler_process(
                s->resampler, n ? s->reference_input + (size_t)s->reference_offset * k : NULL, n,
                pcm, 1, &ri);
            if (r != BF_OK && r != BF_NEED_OUTPUT)
                return r;
            s->reference_offset += ri.consumed;
            if (ri.produced) {
                push(s, pcm, point_time(s, &ri));
                --budget;
            }
            if (!ri.consumed && !ri.produced)
                break;
        }
    }
    if (s->resampler && s->status.capture_clock.valid && s->status.reference_clock.valid &&
        (s->config.allow_uncertain_reference ||
         (!s->status.capture_clock.uncertain && !s->status.reference_clock.uncertain))) {
        double relative = ((1 + s->status.reference_clock.observed_ppm / 1e6) /
                               (1 + s->status.capture_clock.observed_ppm / 1e6) -
                           1) *
                          1e6;
        if (isfinite(relative))
            (void)bf_resampler_set_drift(s->resampler, (int32_t)fmax(-2000, fmin(2000, relative)));
    } else if (s->resampler)
        (void)bf_resampler_set_drift(s->resampler, 0);
    return BF_OK;
}
static int uncertainty_ok(bf_hail_adapter *s, const bf_audio_timing *a, const bf_audio_timing *b,
                          uint32_t *flags)
{
    if (a->uncertainty_ns == UINT64_MAX || b->uncertainty_ns == UINT64_MAX ||
        a->uncertainty_ns > s->config.max_alignment_uncertainty_ns ||
        b->uncertainty_ns > s->config.max_alignment_uncertainty_ns) {
        *flags |= BF_REFERENCE_UNALIGNED;
        return (int)s->config.allow_uncertain_reference;
    }
    return 1;
}
static int select_reference(bf_hail_adapter *s, bf_audio_timing *timing, uint32_t *flags)
{
    uint32_t f = s->config.pipeline.frame_frames, k = s->config.pipeline.reference_channels,
             cap = s->config.reference_history_frames;
    if (!(s->capture_origin.flags & BF_TIME_HOST_VALID) ||
        s->capture_origin.host_time_ns < s->config.reference_delay_ns) {
        *flags |= BF_REFERENCE_UNALIGNED;
        return 0;
    }
    uint64_t target = s->capture_origin.host_time_ns - s->config.reference_delay_ns,
             period = 1000000000u / s->config.pipeline.sample_rate;
    for (uint32_t i = 0; i < cap && s->history_count; ++i) {
        bf_audio_timing *p = &s->points[s->history_read];
        if ((p->flags & BF_TIME_HOST_VALID) &&
            (p->host_time_ns >= target || target - p->host_time_ns <= period / 2 + 2u))
            break;
        s->history_read = (s->history_read + 1u) % cap;
        --s->history_count;
        count(&s->status.alignment_adjustments, 1);
    }
    if (s->history_count < f)
        return 0;
    bf_audio_timing first = s->points[s->history_read];
    for (uint32_t i = 0; i < f; ++i) {
        uint32_t index = (s->history_read + i) % cap;
        bf_audio_timing *p = &s->points[index];
        bf_audio_timing expected = BF_INIT(bf_audio_timing);
        if (bf_timing_advance(&s->capture_origin, i, s->config.pipeline.sample_rate, &expected) !=
            BF_OK)
            return 0;
        if (expected.host_time_ns < s->config.reference_delay_ns)
            return 0;
        uint64_t wanted = expected.host_time_ns - s->config.reference_delay_ns;
        uint64_t error =
            p->host_time_ns > wanted ? p->host_time_ns - wanted : wanted - p->host_time_ns;
        if (!(p->flags & BF_TIME_HOST_VALID) || p->epoch != first.epoch ||
            p->generation != first.generation || p->clock_domain != first.clock_domain ||
            p->frame_position != first.frame_position + i || error > period / 2 + 4u ||
            !uncertainty_ok(s, &s->capture_origin, p, flags))
            return 0;
    }
    for (uint32_t i = 0; i < f; ++i)
        memcpy(s->aligned + (size_t)i * k, s->history + (size_t)((s->history_read + i) % cap) * k,
               k * sizeof(float));
    *timing = first;
    s->history_read = (s->history_read + f) % cap;
    s->history_count -= f;
    s->status.last_aligned_reference = first;
    return 1;
}
bf_result bf_hail_adapter_pump(bf_hail_adapter *s, bf_audio_buffer *out, bf_process_info *info)
{
    if (!s || s->magic != 0x42464841u || !header(info, sizeof(*info)))
        return BF_ERROR_INVALID;
    if (s->flushing)
        return BF_ERROR_STATE;
    bf_result r = bf_pcm_validate(out);
    if (r != BF_OK)
        return r;
    if (out->capacity_frames < s->config.pipeline.frame_frames)
        return BF_ERROR_CAPACITY;
    if (out->format.sample_rate != s->config.pipeline.sample_rate ||
        out->format.channels != s->config.pipeline.channels)
        return BF_ERROR_UNSUPPORTED;
    bf_process_info empty = BF_INIT(bf_process_info);
    empty.vad = (bf_vad_info)BF_INIT(bf_vad_info);
    empty.level = (bf_level_info)BF_INIT(bf_level_info);
    empty.aec = (bf_aec_info)BF_INIT(bf_aec_info);
    record(info, &empty, sizeof(empty));
    out->frames = 0;
    {
        bf_audio_timing unknown = BF_INIT(bf_audio_timing);
        unknown.uncertainty_ns = UINT64_MAX;
        record(&out->timing, &unknown, sizeof(unknown));
    }
    r = references(s);
    if (r != BF_OK)
        return r;
    uint32_t f = s->config.pipeline.frame_frames, c = s->config.pipeline.channels, n = 0,
             need = f + s->capture_delay - s->capture_count;
    if (need > s->hail.quantum_frames)
        need = s->hail.quantum_frames;
    ha_block_info hb = HA_INIT(ha_block_info);
    ha_result hr = ha_stream_read(s->stream, s->raw_capture, need, &n, &hb);
    if (hr == HA_BUSY || hr == HA_WOULD_BLOCK)
        return BF_OK;
    if (hr != HA_OK)
        return BF_ERROR_STATE;
    if (!n)
        return BF_OK;
    bf_audio_timing t = BF_INIT(bf_audio_timing);
    (void)bf_hail_translate_timing(s->hail.stream_id, &hb, &t);
    s->status.last_capture_source = t;
    (void)bf_clock_tracker_observe(s->capture_clock, &t, s->config.pipeline.sample_rate,
                                   &s->status.capture_clock);
    if (s->capture_count &&
        (t.epoch != s->capture_origin.epoch || t.clock_domain != s->capture_origin.clock_domain ||
         t.frame_position != s->capture_origin.frame_position + s->capture_count ||
         (t.flags & (BF_AUDIO_GAP | BF_AUDIO_DISCONTINUITY)))) {
        count(&s->status.discarded_capture_frames, s->capture_count);
        s->capture_count = 0;
        t.flags |= BF_AUDIO_GAP | BF_AUDIO_DISCONTINUITY;
    }
    if (!s->capture_count)
        s->capture_origin = t;
    r = convert(s->raw_capture, s->capture + (size_t)s->capture_count * c, n,
                &s->hail.capture_format, &t, s->config.pipeline.nonfinite_policy);
    if (r != BF_OK)
        return r;
    s->capture_origin.flags |= t.flags & BF_AUDIO_SANITIZED;
    for (uint32_t i = 0; i < n; ++i) {
        s->capture_points[s->capture_count + i] = (bf_audio_timing)BF_INIT(bf_audio_timing);
        r = bf_timing_advance(&t, i, s->config.pipeline.sample_rate,
                              &s->capture_points[s->capture_count + i]);
        if (r != BF_OK)
            return r;
    }
    s->capture_count += n;
    if (s->capture_count < f + s->capture_delay)
        return BF_OK;
    bf_audio_timing rt = BF_INIT(bf_audio_timing);
    uint32_t flags = 0;
    int have = s->resampler ? select_reference(s, &rt, &flags) : 0;
    bf_pcm_format cf = format(&s->hail.capture_format);
    cf.sample_format = BF_PCM_F32;
    bf_audio_buffer capture = view(s->capture, cf, f, s->capture_origin);
    bf_pcm_format rf = format(&s->hail.playback_format);
    rf.sample_format = BF_PCM_F32;
    rf.sample_rate = cf.sample_rate;
    bf_audio_buffer reference = view(s->aligned, rf, f, rt);
    r = bf_pipeline_process(s->pipeline, &capture, have ? &reference : NULL, out, info);
    if (r != BF_OK)
        return r;
    s->delayed_valid_frames = f;
    s->capture_count -= f;
    if (s->capture_count) {
        memmove(s->capture, s->capture + (size_t)f * c,
                (size_t)s->capture_count * c * sizeof(float));
        memmove(s->capture_points, s->capture_points + f,
                (size_t)s->capture_count * sizeof(bf_audio_timing));
        s->capture_origin = s->capture_points[0];
    }
    info->algorithmic_delay_frames += s->capture_delay;
    info->flags |= flags;
    if (!have && s->resampler) {
        info->flags |= BF_REFERENCE_MISSING;
        count(&s->status.missing_reference_frames, f);
    }
    count(&s->status.processed_frames, out->frames);
    count(&s->status.discarded_capture_frames, info->discarded_frames);
    return BF_OK;
}
bf_result bf_hail_adapter_flush(bf_hail_adapter *s, bf_audio_buffer *out, bf_process_info *info)
{
    if (!s || s->magic != 0x42464841u || !header(info, sizeof(*info)))
        return BF_ERROR_INVALID;
    bf_result r = bf_pcm_validate(out);
    if (r != BF_OK)
        return r;
    uint32_t f = s->config.pipeline.frame_frames, c = s->config.pipeline.channels;
    if (out->capacity_frames < f)
        return BF_ERROR_CAPACITY;
    if (out->format.channels != c || out->format.sample_rate != s->config.pipeline.sample_rate)
        return BF_ERROR_UNSUPPORTED;
    s->flushing = 1;
    uint32_t consumed = 0, flags = 0, discarded = 0;
    /* At most two pending capture blocks plus the noise overlap tail. A
       priming result is internal: zero frames means fully drained to callers. */
    for (uint32_t step = 0; step < 3; ++step) {
        if (s->capture_count) {
            uint32_t real = s->capture_count > f ? f : s->capture_count;
            if (real < f)
                memset(s->capture + (size_t)real * c, 0, (size_t)(f - real) * c * sizeof(float));
            bf_pcm_format cf = format(&s->hail.capture_format);
            cf.sample_format = BF_PCM_F32;
            bf_audio_buffer input = view(s->capture, cf, f, s->capture_origin);
            if (real < f)
                input.timing.flags |= BF_AUDIO_PADDING;
            r = bf_pipeline_process(s->pipeline, &input, NULL, out, info);
            if (r != BF_OK)
                return r;
            uint32_t valid =
                (s->config.pipeline.stages & BF_STAGE_NOISE) ? s->delayed_valid_frames : real;
            if (out->frames > valid)
                out->frames = valid;
            s->delayed_valid_frames = real;
            s->capture_count -= real;
            if (s->capture_count) {
                memmove(s->capture, s->capture + (size_t)real * c,
                        (size_t)s->capture_count * c * sizeof(float));
                memmove(s->capture_points, s->capture_points + real,
                        (size_t)s->capture_count * sizeof(bf_audio_timing));
                s->capture_origin = s->capture_points[0];
            }
            info->consumed_frames = real;
            info->flags |= BF_AUDIO_PADDING;
        } else {
            r = bf_pipeline_flush(s->pipeline, out, info);
            if (r != BF_OK)
                return r;
            if (out->frames > s->delayed_valid_frames)
                out->frames = s->delayed_valid_frames;
            s->delayed_valid_frames = 0;
        }
        consumed += info->consumed_frames;
        flags |= info->flags;
        discarded += info->discarded_frames;
        if (out->frames || (!s->capture_count && !s->delayed_valid_frames))
            break;
    }
    info->consumed_frames = consumed;
    info->flags |= flags;
    info->discarded_frames = discarded;
    info->algorithmic_delay_frames += s->capture_delay;
    count(&s->status.processed_frames, out->frames);
    count(&s->status.discarded_capture_frames, discarded);
    return BF_OK;
}
bf_result bf_hail_adapter_status(const bf_hail_adapter *s, bf_hail_status *out)
{
    if (!s || s->magic != 0x42464841u || !header(out, sizeof(*out)))
        return BF_ERROR_INVALID;
    bf_hail_status value = s->status;
    value.capture_pending_frames = s->capture_count;
    value.reference_pending_frames = s->history_count;
    value.reference_history_frames = s->config.reference_history_frames;
    value.capture_alignment_delay_frames = s->capture_delay;
    record(out, &value, sizeof(value));
    return BF_OK;
}
bf_result bf_hail_adapter_deinit(bf_hail_adapter *s)
{
    if (!s || s->magic != 0x42464841u)
        return BF_ERROR_INVALID;
    bf_result r = bf_pipeline_deinit(s->pipeline);
    if (r != BF_OK)
        return r;
    s->magic = 0;
    return BF_OK;
}
