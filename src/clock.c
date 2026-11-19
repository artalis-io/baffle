#include "baffle_internal.h"
struct bf_clock_tracker {
    uint32_t magic, rate, seen;
    bf_audio_timing first, last;
    bf_clock_estimate estimate;
};
size_t bf_clock_tracker_size(void)
{
    return baffle_align(sizeof(bf_clock_tracker));
}
bf_result bf_clock_tracker_init(void *p, size_t bytes, bf_clock_tracker **out)
{
    if (!out)
        return BF_ERROR_INVALID;
    *out = NULL;
    if (!baffle_storage(p, bytes, bf_clock_tracker_size()))
        return BF_ERROR_CAPACITY;
    memset(p, 0, bf_clock_tracker_size());
    *out = p;
    (*out)->magic = 0x4246434cu;
    return BF_OK;
}
void bf_clock_tracker_reset(bf_clock_tracker *s)
{
    if (s && s->magic == 0x4246434cu)
        s->seen = 0;
}
bf_result bf_clock_tracker_observe(bf_clock_tracker *s, const bf_audio_timing *t, uint32_t rate,
                                   bf_clock_estimate *out)
{
    bf_clock_estimate v = BF_INIT(bf_clock_estimate);
    if (!s || s->magic != 0x4246434cu || !BAFFLE_HEADER(t) || !BAFFLE_HEADER(out) || rate < 8000 ||
        rate > 192000)
        return BF_ERROR_INVALID;
    v.uncertainty_ns = UINT64_MAX;
    v.uncertain = 1;
    if (!(t->flags & BF_TIME_HOST_VALID)) {
        s->seen = 0;
        baffle_record(out, &v, sizeof(v));
        return BF_OK;
    }
    if (!t->clock_domain || !t->epoch)
        return BF_ERROR_INVALID;
    if (!s->seen || rate != s->rate || t->stream_id != s->last.stream_id ||
        t->clock_domain != s->last.clock_domain || t->epoch != s->last.epoch ||
        (t->flags & (BF_AUDIO_GAP | BF_AUDIO_DISCONTINUITY)) ||
        t->host_time_ns <= s->last.host_time_ns || t->frame_position <= s->last.frame_position) {
        s->first = *t;
        s->rate = rate;
        s->seen = 1;
        s->estimate = v;
    } else {
        uint64_t span = t->host_time_ns - s->first.host_time_ns;
        uint64_t uncertainty = UINT64_MAX;
        if (t->uncertainty_ns != UINT64_MAX &&
            s->first.uncertainty_ns <= UINT64_MAX - t->uncertainty_ns)
            uncertainty = t->uncertainty_ns + s->first.uncertainty_ns;
        v = s->estimate;
        if (span >= UINT64_C(100000000)) {
            v.observation_ns = span;
            v.uncertainty_ns = uncertainty;
            v.uncertain = uncertainty == UINT64_MAX || uncertainty > span / 100u;
            double ratio = (double)(t->frame_position - s->first.frame_position) * 1e9 /
                           (double)span / (double)rate;
            v.observed_ppm = (ratio - 1.0) * 1e6;
            v.valid = (uint32_t)isfinite(v.observed_ppm);
            s->estimate = v;
            if (span >= UINT64_C(1000000000))
                s->first = *t;
        } else if (uncertainty == UINT64_MAX) {
            v.uncertain = 1;
            v.uncertainty_ns = UINT64_MAX;
        }
    }
    s->last = *t;
    baffle_record(out, &v, sizeof(v));
    return BF_OK;
}
