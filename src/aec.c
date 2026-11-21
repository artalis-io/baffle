#include "baffle_internal.h"
#ifndef BF_HAVE_SPEEXDSP
#define BF_HAVE_SPEEXDSP 0
#endif
#if BF_HAVE_SPEEXDSP
#include <speex/speex_echo.h>
void baffle_speex_configure_capture(SpeexEchoState *s);
void baffle_speex_reset_complete(SpeexEchoState *s);
int baffle_speex_recovered(SpeexEchoState *s);
typedef struct baffle_speex_arena {
    unsigned char *data;
    size_t used, capacity;
} baffle_speex_arena;
#if defined(_MSC_VER) && !defined(__clang__)
static __declspec(thread) baffle_speex_arena *baffle_initializing_arena;
#else
static _Thread_local baffle_speex_arena *baffle_initializing_arena;
#endif
void *baffle_speex_allocate(int bytes)
{
    baffle_speex_arena *a = baffle_initializing_arena;
    if (!a || bytes < 0)
        return NULL; /* No allocation is admitted during processing. */
    size_t count = baffle_align((size_t)bytes);
    if (count > a->capacity - a->used)
        return NULL;
    void *p = a->data + a->used;
    a->used += count;
    memset(p, 0, count);
    return p;
}
#endif
struct bf_aec {
    uint32_t magic, seen, reference_missing;
    bf_aec_config config;
    bf_audio_timing capture, reference;
    uint64_t resets;
#if BF_HAVE_SPEEXDSP
    SpeexEchoState *echo;
    baffle_speex_arena arena;
    int16_t *near_pcm, *far_pcm, *out_pcm;
    int32_t *impulse;
    uint32_t impulse_frames;
#endif
};
static bf_result aec_plan(const bf_aec_config *c, size_t *bytes, size_t *pool)
{
    if (!BAFFLE_HEADER(c) || c->backend != BF_AEC_SPEEXDSP || c->sample_rate < 8000 ||
        c->sample_rate > 48000 ||
        !(c->frame_frames == c->sample_rate / 100u || c->frame_frames == c->sample_rate / 50u) ||
        c->sample_rate % 100u || !c->capture_channels || c->capture_channels > 8 ||
        !c->reference_channels || c->reference_channels > 2 || c->tail_ms < 40 || c->tail_ms > 500)
        return BF_ERROR_INVALID;
    uint32_t f = c->frame_frames, n = 2 * f,
             segments = (c->sample_rate * c->tail_ms / 1000u + f - 1u) / f;
    size_t channels = c->capture_channels, refs = c->reference_channels;
    /* Pinned MDF allocation sum, plus conservative state/alignment/FFT overhead.
     * Bound includes both TWO_PATH coefficient banks and smallft caches. */
    *pool = 8192u +
            (size_t)n * (4u * (5u * channels + refs + refs * (segments + 1u) +
                               2u * channels * refs * segments + 3u) +
                         12u) +
            channels * f * 4u + 7u * (f + 1u) * 4u + segments * 4u + refs * 4u + 16u * channels +
            6u * refs * f;
    *pool = baffle_align(*pool);
    *bytes = baffle_align(sizeof(bf_aec));
    if (!baffle_add(bytes, baffle_align((2u * channels + refs) * f * sizeof(int16_t))) ||
        !baffle_add(bytes, (size_t)segments * f * sizeof(int32_t)) ||
        !baffle_add(bytes, BF_ALIGNMENT + *pool))
        return BF_ERROR_OVERFLOW;
    return BF_HAVE_SPEEXDSP ? BF_OK : BF_ERROR_UNSUPPORTED;
}
bf_result bf_aec_size(const bf_aec_config *c, size_t *bytes)
{
    size_t pool;
    if (!bytes)
        return BF_ERROR_INVALID;
    return aec_plan(c, bytes, &pool);
}
bf_result bf_aec_init(void *p, size_t bytes, const bf_aec_config *c, bf_aec **out)
{
    size_t need, pool;
    bf_result r;
    if (!out)
        return BF_ERROR_INVALID;
    *out = NULL;
    r = aec_plan(c, &need, &pool);
    if (r != BF_OK)
        return r;
    if (!baffle_storage(p, bytes, need))
        return BF_ERROR_CAPACITY;
#if BF_HAVE_SPEEXDSP
    if (baffle_initializing_arena)
        return BF_ERROR_STATE;
    memset(p, 0, need);
    bf_aec *s = p;
    s->config = *c;
    s->near_pcm = (int16_t *)((unsigned char *)p + baffle_align(sizeof(*s)));
    s->far_pcm = s->near_pcm + (size_t)c->frame_frames * c->capture_channels;
    s->out_pcm = s->far_pcm + (size_t)c->frame_frames * c->reference_channels;
    s->impulse = (int32_t *)((unsigned char *)s->near_pcm +
                             baffle_align((size_t)c->frame_frames *
                                          (2u * c->capture_channels + c->reference_channels) *
                                          sizeof(int16_t)));
    s->impulse_frames =
        ((c->sample_rate * c->tail_ms / 1000u + c->frame_frames - 1u) / c->frame_frames) *
        c->frame_frames;
    s->arena.data =
        (unsigned char *)(((uintptr_t)(s->impulse + s->impulse_frames) + BF_ALIGNMENT - 1u) &
                          ~(uintptr_t)(BF_ALIGNMENT - 1u));
    s->arena.capacity = pool;
    baffle_initializing_arena = &s->arena;
    s->echo = speex_echo_state_init_mc((int)c->frame_frames, (int)s->impulse_frames,
                                       (int)c->capture_channels, (int)c->reference_channels);
    baffle_initializing_arena = NULL;
    if (!s->echo)
        return BF_ERROR_CAPACITY;
    int rate = (int)c->sample_rate;
    if (speex_echo_ctl(s->echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate))
        return BF_ERROR_STATE;
    baffle_speex_configure_capture(s->echo);
    s->magic = 0x42464145u;
    *out = s;
    return BF_OK;
#else
    (void)p;
    (void)pool;
    return BF_ERROR_UNSUPPORTED;
#endif
}
bf_result bf_aec_reset(bf_aec *s)
{
    if (!s || s->magic != 0x42464145u)
        return BF_ERROR_INVALID;
#if BF_HAVE_SPEEXDSP
    if (s->resets == UINT64_MAX)
        return BF_ERROR_OVERFLOW;
    baffle_speex_reset_complete(s->echo);
    ++s->resets;
    s->seen = s->reference_missing = 0;
    return BF_OK;
#else
    return BF_ERROR_UNSUPPORTED;
#endif
}
#if BF_HAVE_SPEEXDSP
static int changed(const bf_audio_timing *a, const bf_audio_timing *b, uint32_t frames)
{
    return a->stream_id != b->stream_id || a->clock_domain != b->clock_domain ||
           a->epoch != b->epoch || b->frame_position > UINT64_MAX - frames ||
           a->frame_position != b->frame_position + frames ||
           (a->flags & (BF_AUDIO_GAP | BF_AUDIO_DISCONTINUITY));
}
static int16_t pcm16(float x)
{
    return x >= 32767.0f / 32768 ? INT16_MAX : x <= -1 ? INT16_MIN : (int16_t)lrintf(x * 32768);
}
#endif
bf_result bf_aec_process(bf_aec *s, const float *near, const bf_audio_timing *nt, const float *far,
                         const bf_audio_timing *ft, float *out, bf_aec_info *info)
{
    bf_audio_timing unknown = BF_INIT(bf_audio_timing);
    unknown.uncertainty_ns = UINT64_MAX;
    if (!far && !ft)
        ft = &unknown;
    if (!s || s->magic != 0x42464145u || !near || !out || !BAFFLE_HEADER(nt) ||
        !BAFFLE_HEADER(ft) || !BAFFLE_HEADER(info))
        return BF_ERROR_INVALID;
#if BF_HAVE_SPEEXDSP
    uint32_t f = s->config.frame_frames, c = s->config.capture_channels,
             k = s->config.reference_channels;
    for (size_t i = 0; i < (size_t)f * c; ++i)
        if (!isfinite(near[i]))
            return BF_ERROR_NONFINITE;
        else if (fabsf(near[i]) > 16)
            return BF_ERROR_INVALID;
    if (far)
        for (size_t i = 0; i < (size_t)f * k; ++i) {
            if (!isfinite(far[i]))
                return BF_ERROR_NONFINITE;
            if (fabsf(far[i]) > 16)
                return BF_ERROR_INVALID;
        }
    if (nt->frame_position > UINT64_MAX - f || (far && ft->frame_position > UINT64_MAX - f))
        return BF_ERROR_OVERFLOW;
    bf_aec_info v = BF_INIT(bf_aec_info);
    /* Reset clears seen/reference bookkeeping. Preserve the independently
     * observable generation transition when an epoch change also resets AEC. */
    int generation_changed = far && s->seen && !s->reference_missing &&
                             ft->generation != s->reference.generation;
    if (s->seen && (changed(nt, &s->capture, f) ||
                    (far && (s->reference_missing || changed(ft, &s->reference, f))))) {
        bf_result r = bf_aec_reset(s);
        if (r != BF_OK)
            return r;
        v.flags |= BF_AUDIO_DISCONTINUITY;
    }
    if (generation_changed)
        v.flags |= BF_REFERENCE_GENERATION_CHANGED;
    double ip = 0, op = 0;
    for (size_t i = 0; i < (size_t)f * c; ++i) {
        ip += (double)near[i] * near[i];
        if (far) {
            s->near_pcm[i] = pcm16(near[i]);
            if (near[i] > 32767.0f / 32768 || near[i] < -1)
                v.flags |= BF_AUDIO_CLIPPED;
        }
    }
    if (far) {
        for (size_t i = 0; i < (size_t)f * k; ++i) {
            s->far_pcm[i] = pcm16(far[i]);
            if (far[i] > 32767.0f / 32768 || far[i] < -1)
                v.flags |= BF_AUDIO_CLIPPED;
        }
        speex_echo_cancellation(s->echo, s->near_pcm, s->far_pcm, s->out_pcm);
        if (baffle_speex_recovered(s->echo)) {
            bf_result r = bf_aec_reset(s);
            if (r != BF_OK)
                return r;
            v.flags |= BF_AUDIO_DISCONTINUITY;
        }
        for (size_t i = 0; i < (size_t)f * c; ++i)
            out[i] = (float)s->out_pcm[i] / 32768;
    } else {
        v.flags |= BF_REFERENCE_MISSING;
        memmove(out, near, (size_t)f * c * sizeof(float));
    }
    for (size_t i = 0; i < (size_t)f * c; ++i)
        op += (double)out[i] * out[i];
    s->capture = *nt;
    s->reference = *ft;
    s->seen = 1;
    s->reference_missing = far == NULL;
    v.resets = s->resets;
    v.reference_generation = far ? ft->generation : 0;
    v.backend_arena_used = s->arena.used;
    v.backend_arena_capacity = s->arena.capacity;
    v.input_rms = (float)sqrt(ip / ((double)f * c));
    v.output_rms = (float)sqrt(op / ((double)f * c));
    baffle_record(info, &v, sizeof(v));
    return BF_OK;
#else
    (void)far;
    return BF_ERROR_UNSUPPORTED;
#endif
}
bf_result bf_aec_estimate_delay(bf_aec *s, uint32_t *delay)
{
    if (!s || s->magic != 0x42464145u || !delay)
        return BF_ERROR_INVALID;
#if BF_HAVE_SPEEXDSP
    if (s->config.capture_channels != 1 || s->config.reference_channels != 1)
        return BF_ERROR_UNSUPPORTED;
    if (speex_echo_ctl(s->echo, SPEEX_ECHO_GET_IMPULSE_RESPONSE, s->impulse))
        return BF_ERROR_STATE;
    uint32_t peak = 0;
    int64_t maximum = 0;
    for (uint32_t i = 0; i < s->impulse_frames; ++i) {
        int64_t x = s->impulse[i];
        if (x < 0)
            x = -x;
        if (x > maximum) {
            maximum = x;
            peak = i;
        }
    }
    if (!maximum)
        return BF_ERROR_STATE;
    *delay = peak;
    return BF_OK;
#else
    return BF_ERROR_UNSUPPORTED;
#endif
}
bf_result bf_aec_deinit(bf_aec *s)
{
    if (!s || s->magic != 0x42464145u)
        return BF_ERROR_INVALID;
#if BF_HAVE_SPEEXDSP
    speex_echo_state_destroy(s->echo);
    s->magic = 0;
    return BF_OK;
#else
    return BF_ERROR_UNSUPPORTED;
#endif
}
