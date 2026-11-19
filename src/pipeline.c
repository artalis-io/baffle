#include "baffle_internal.h"
struct bf_pipeline {
    uint32_t magic, seen, metadata_valid, flushing;
    bf_pipeline_config config;
    bf_audio_timing last, delayed;
    bf_aec *aec;
    bf_noise *noise;
    bf_agc *agc;
    bf_vad *vad;
    float *work, *reference;
    float activity;
};
static void configs(const bf_pipeline_config *c, bf_aec_config *a, bf_noise_config *n,
                    bf_agc_config *g, bf_vad_config *v)
{
    *a = (bf_aec_config)BF_INIT(bf_aec_config);
    a->sample_rate = c->sample_rate;
    a->frame_frames = c->frame_frames;
    a->capture_channels = c->channels;
    a->reference_channels = c->reference_channels;
    a->tail_ms = c->aec_tail_ms;
    a->backend = BF_AEC_SPEEXDSP;
    *n = (bf_noise_config)BF_INIT(bf_noise_config);
    n->sample_rate = c->sample_rate;
    n->channels = c->channels;
    n->frame_frames = c->frame_frames;
    n->strength_db = c->noise_strength_db;
    *g = (bf_agc_config)BF_INIT(bf_agc_config);
    g->sample_rate = c->sample_rate;
    g->channels = c->channels;
    g->max_frames = c->frame_frames;
    g->target_dbfs = c->agc_target_dbfs;
    g->min_gain_db = -20;
    g->max_gain_db = 20;
    g->attack_ms = 5;
    g->release_ms = 250;
    g->gate_dbfs = -60;
    g->limit_dbfs = -1;
    *v = (bf_vad_config)BF_INIT(bf_vad_config);
    v->sample_rate = c->sample_rate;
    v->channels = c->channels;
    v->max_frames = c->frame_frames;
    v->onset_ms = 20;
    v->hangover_ms = 180;
    v->threshold = .55f;
    v->noise_floor_dbfs = -60;
}
static bf_result plan(const bf_pipeline_config *c, size_t sizes[4], size_t *bytes)
{
    bf_aec_config a;
    bf_noise_config n;
    bf_agc_config g;
    bf_vad_config v;
    bf_result r;
    if (!BAFFLE_HEADER(c) || c->sample_rate < 8000 || c->sample_rate > 192000 || !c->channels ||
        c->channels > BF_MAX_CHANNELS || !c->reference_channels ||
        c->reference_channels > BF_MAX_CHANNELS || !c->frame_frames || c->frame_frames > 960 ||
        c->stages & ~BF_STAGE_ALL || c->nonfinite_policy > BF_NONFINITE_ZERO ||
        !isfinite(c->noise_strength_db) || c->noise_strength_db < 0 || c->noise_strength_db > 40 ||
        !isfinite(c->agc_target_dbfs) || c->agc_target_dbfs < -60 || c->agc_target_dbfs > -1)
        return BF_ERROR_INVALID;
    configs(c, &a, &n, &g, &v);
    memset(sizes, 0, 4 * sizeof(size_t));
    if (c->stages & BF_STAGE_AEC) {
        r = bf_aec_size(&a, &sizes[0]);
        if (r != BF_OK)
            return r;
    }
    if (c->stages & BF_STAGE_NOISE) {
        r = bf_noise_size(&n, &sizes[1]);
        if (r != BF_OK)
            return r;
    }
    if (c->stages & BF_STAGE_AGC)
        sizes[2] = bf_agc_size();
    if (c->stages & BF_STAGE_VAD)
        sizes[3] = bf_vad_size();
    *bytes = baffle_align(sizeof(bf_pipeline));
    for (uint32_t i = 0; i < 4; ++i)
        if (!baffle_add(bytes, baffle_align(sizes[i])))
            return BF_ERROR_OVERFLOW;
    return baffle_add(bytes, (size_t)c->frame_frames * (c->channels + c->reference_channels) *
                                 sizeof(float))
               ? BF_OK
               : BF_ERROR_OVERFLOW;
}
bf_result bf_pipeline_size(const bf_pipeline_config *c, size_t *bytes)
{
    size_t sizes[4];
    if (!bytes)
        return BF_ERROR_INVALID;
    return plan(c, sizes, bytes);
}
bf_result bf_pipeline_init(void *p, size_t bytes, const bf_pipeline_config *c, bf_pipeline **out)
{
    size_t sizes[4], need;
    bf_result r;
    if (!out)
        return BF_ERROR_INVALID;
    *out = NULL;
    r = plan(c, sizes, &need);
    if (r != BF_OK)
        return r;
    if (!baffle_storage(p, bytes, need))
        return BF_ERROR_CAPACITY;
    memset(p, 0, need);
    bf_pipeline *s = p;
    s->config = *c;
    s->activity = -1;
    unsigned char *cursor = (unsigned char *)p + baffle_align(sizeof(*s));
    bf_aec_config a;
    bf_noise_config n;
    bf_agc_config g;
    bf_vad_config v;
    configs(c, &a, &n, &g, &v);
    if (sizes[0]) {
        r = bf_aec_init(cursor, sizes[0], &a, &s->aec);
        if (r != BF_OK)
            return r;
    }
    cursor += baffle_align(sizes[0]);
    if (sizes[1]) {
        r = bf_noise_init(cursor, sizes[1], &n, &s->noise);
        if (r != BF_OK)
            return r;
    }
    cursor += baffle_align(sizes[1]);
    if (sizes[2]) {
        r = bf_agc_init(cursor, sizes[2], &g, &s->agc);
        if (r != BF_OK)
            return r;
    }
    cursor += baffle_align(sizes[2]);
    if (sizes[3]) {
        r = bf_vad_init(cursor, sizes[3], &v, &s->vad);
        if (r != BF_OK)
            return r;
    }
    cursor += baffle_align(sizes[3]);
    s->work = (float *)cursor;
    s->reference = s->work + (size_t)c->frame_frames * c->channels;
    s->magic = 0x42465049u;
    *out = s;
    return BF_OK;
}
bf_result bf_pipeline_reset(bf_pipeline *s, uint32_t mask)
{
    if (!s || s->magic != 0x42465049u || mask & ~BF_STAGE_ALL)
        return BF_ERROR_INVALID;
    bf_result r;
    if (s->aec && (mask & BF_STAGE_AEC)) {
        r = bf_aec_reset(s->aec);
        if (r != BF_OK)
            return r;
    }
    if (s->noise && (mask & BF_STAGE_NOISE)) {
        (void)bf_noise_reset(s->noise);
        s->metadata_valid = 0;
    }
    if (s->agc && (mask & BF_STAGE_AGC))
        (void)bf_agc_reset(s->agc);
    if (s->vad && (mask & BF_STAGE_VAD)) {
        (void)bf_vad_reset(s->vad);
        s->activity = -1;
    }
    if (mask == BF_STAGE_ALL)
        s->seen = 0;
    s->flushing = 0;
    return BF_OK;
}
static bf_result output_valid(bf_pipeline *s, bf_audio_buffer *out)
{
    bf_result r = bf_pcm_validate(out);
    if (r != BF_OK)
        return r;
    if (out->format.sample_rate != s->config.sample_rate ||
        out->format.channels != s->config.channels)
        return BF_ERROR_UNSUPPORTED;
    return out->capacity_frames < s->config.frame_frames ? BF_ERROR_CAPACITY : BF_OK;
}
static bf_result finish(bf_pipeline *s, bf_audio_buffer *out, bf_process_info *info,
                        const bf_audio_timing *timing)
{
    bf_result r;
    uint32_t f = s->config.frame_frames, c = s->config.channels;
    for (size_t i = 0; i < (size_t)f * c; ++i) {
        if (fabsf(s->work[i]) > 16) {
            s->work[i] = fmaxf(-16, fminf(16, s->work[i]));
            info->flags |= BF_AUDIO_CLIPPED;
        }
    }
    if (s->agc) {
        r = bf_agc_process(s->agc, s->work, s->work, f, s->activity, &info->level);
        if (r != BF_OK)
            return r;
    }
    if (s->vad) {
        r = bf_vad_process(s->vad, s->work, f, &info->vad);
        if (r != BF_OK)
            return r;
        s->activity = info->vad.probability;
    }
    for (uint32_t i = 0; i < f; ++i)
        for (uint32_t j = 0; j < c; ++j)
            baffle_store(out, i, j, s->work[(size_t)i * c + j]);
    out->frames = f;
    baffle_record(&out->timing, timing, sizeof(*timing));
    return BF_OK;
}
bf_result bf_pipeline_process(bf_pipeline *s, const bf_audio_buffer *in, const bf_audio_buffer *ref,
                              bf_audio_buffer *out, bf_process_info *info)
{
    if (!s || s->magic != 0x42465049u || !BAFFLE_HEADER(info))
        return BF_ERROR_INVALID;
    if (s->flushing)
        return BF_ERROR_STATE;
    bf_result r = bf_pcm_validate(in);
    if (r != BF_OK)
        return r;
    r = output_valid(s, out);
    if (r != BF_OK)
        return r;
    uint32_t f = s->config.frame_frames, c = s->config.channels, k = s->config.reference_channels;
    if (in->frames != f || in->format.sample_rate != s->config.sample_rate ||
        in->format.channels != c)
        return BF_ERROR_UNSUPPORTED;
    r = baffle_finite(in, s->config.nonfinite_policy);
    if (r != BF_OK)
        return r;
    if (ref) {
        r = bf_pcm_validate(ref);
        if (r != BF_OK)
            return r;
        if (ref->frames != f || ref->format.sample_rate != s->config.sample_rate ||
            ref->format.channels != k)
            return BF_ERROR_UNSUPPORTED;
        r = baffle_finite(ref, s->config.nonfinite_policy);
        if (r != BF_OK)
            return r;
    }
    for (uint32_t i = 0; i < f; ++i) {
        for (uint32_t j = 0; j < c; ++j)
            if (fabsf(baffle_sample(in, i, j)) > 16)
                return BF_ERROR_INVALID;
        if (ref)
            for (uint32_t j = 0; j < k; ++j)
                if (fabsf(baffle_sample(ref, i, j)) > 16)
                    return BF_ERROR_INVALID;
    }
    bf_process_info value = BF_INIT(bf_process_info);
    value.vad = (bf_vad_info)BF_INIT(bf_vad_info);
    value.level = (bf_level_info)BF_INIT(bf_level_info);
    value.aec = (bf_aec_info)BF_INIT(bf_aec_info);
    value.algorithmic_delay_frames = s->noise ? f : 0;
    value.consumed_frames = f;
    if (s->seen &&
        (in->timing.stream_id != s->last.stream_id ||
         in->timing.clock_domain != s->last.clock_domain || in->timing.epoch != s->last.epoch ||
         in->timing.frame_position != s->last.frame_position + f ||
         (in->timing.flags & (BF_AUDIO_GAP | BF_AUDIO_DISCONTINUITY)))) {
        uint32_t mask = BF_STAGE_NOISE | BF_STAGE_VAD;
        if (in->timing.stream_id != s->last.stream_id)
            mask |= BF_STAGE_AGC;
        r = bf_pipeline_reset(s, mask);
        if (r != BF_OK)
            return r;
        value.flags |= BF_AUDIO_DISCONTINUITY;
    }
    for (uint32_t i = 0; i < f; ++i) {
        for (uint32_t j = 0; j < c; ++j) {
            float x = baffle_sample(in, i, j);
            if (!isfinite(x))
                value.flags |= BF_AUDIO_SANITIZED;
            s->work[(size_t)i * c + j] = baffle_clean(x);
        }
        if (ref)
            for (uint32_t j = 0; j < k; ++j) {
                float x = baffle_sample(ref, i, j);
                if (!isfinite(x))
                    value.flags |= BF_AUDIO_SANITIZED;
                s->reference[(size_t)i * k + j] = baffle_clean(x);
            }
    }
    if (s->aec) {
        bf_audio_timing unknown = BF_INIT(bf_audio_timing);
        unknown.uncertainty_ns = UINT64_MAX;
        r = bf_aec_process(s->aec, s->work, &in->timing, ref ? s->reference : NULL,
                           ref ? &ref->timing : &unknown, s->work, &value.aec);
        if (r != BF_OK)
            return r;
        value.flags |= value.aec.flags;
    }
    bf_audio_timing timing = in->timing;
    if (s->noise) {
        r = bf_noise_process(s->noise, s->work, s->work, s->activity);
        if (r != BF_OK)
            return r;
        value.priming = !s->metadata_valid;
        timing = s->delayed;
        s->delayed = in->timing;
        s->metadata_valid = 1;
    }
    s->last = in->timing;
    s->seen = 1;
    if (value.priming) {
        bf_audio_timing unknown = BF_INIT(bf_audio_timing);
        unknown.uncertainty_ns = UINT64_MAX;
        out->frames = 0;
        baffle_record(&out->timing, &unknown, sizeof(unknown));
    } else {
        r = finish(s, out, &value, &timing);
        if (r != BF_OK)
            return r;
    }
    baffle_record(info, &value, sizeof(value));
    return BF_OK;
}
bf_result bf_pipeline_flush(bf_pipeline *s, bf_audio_buffer *out, bf_process_info *info)
{
    if (!s || s->magic != 0x42465049u || !BAFFLE_HEADER(info))
        return BF_ERROR_INVALID;
    bf_result r = output_valid(s, out);
    if (r != BF_OK)
        return r;
    bf_process_info value = BF_INIT(bf_process_info);
    value.vad = (bf_vad_info)BF_INIT(bf_vad_info);
    value.level = (bf_level_info)BF_INIT(bf_level_info);
    value.aec = (bf_aec_info)BF_INIT(bf_aec_info);
    value.algorithmic_delay_frames = s->noise ? s->config.frame_frames : 0;
    s->flushing = 1;
    out->frames = 0;
    {
        bf_audio_timing unknown = BF_INIT(bf_audio_timing);
        unknown.uncertainty_ns = UINT64_MAX;
        baffle_record(&out->timing, &unknown, sizeof(unknown));
    }
    if (s->noise && s->metadata_valid) {
        memset(s->work, 0, (size_t)s->config.frame_frames * s->config.channels * sizeof(float));
        r = bf_noise_process(s->noise, s->work, s->work, 1);
        if (r != BF_OK)
            return r;
        r = finish(s, out, &value, &s->delayed);
        if (r != BF_OK)
            return r;
        s->metadata_valid = 0;
    }
    baffle_record(info, &value, sizeof(value));
    return BF_OK;
}
bf_result bf_pipeline_deinit(bf_pipeline *s)
{
    if (!s || s->magic != 0x42465049u)
        return BF_ERROR_INVALID;
    if (s->aec) {
        bf_result r = bf_aec_deinit(s->aec);
        if (r != BF_OK)
            return r;
    }
    s->magic = 0;
    return BF_OK;
}
