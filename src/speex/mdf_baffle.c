/* Baffle's narrow integration of the hash-pinned, unmodified Speex MDF source.
 * The upstream notch defaults are too destructive for near-end low speech at
 * several rates. Keep the same stable two-pole DC rejector, but set a consistent
 * 20 Hz pole-radius parameter. This is not an exact -3 dB cutoff specification.
 * No adaptation, double-talk or output-suppression parameters are changed.
 * Including the source keeps its private state out of Baffle's portable API. */
#include "mdf.c"
#include <limits.h>

void baffle_speex_configure_capture(SpeexEchoState *s)
{
    s->notch_radius = expf(-6.2831853071795864769f * 20.0f / (float)s->sampling_rate);
}

void baffle_speex_reset_complete(SpeexEchoState *s)
{
    speex_echo_state_reset(s);
    /* Upstream 1.2.1 reset uses mono ranges for these allocations. Complete
     * every configured capture/reference bank before a new epoch can use it.
     * Preserve the existing mono path and its adaptive scheduling exactly. */
    if (s->C > 1 || s->K > 1) {
        size_t n=(size_t)s->window_size, m=(size_t)s->M;
        size_t channels=(size_t)s->C, refs=(size_t)s->K;
        memset(s->W,0,n*m*channels*refs*sizeof(*s->W));
#ifdef TWO_PATH
        memset(s->foreground,0,n*m*channels*refs*sizeof(*s->foreground));
#endif
        memset(s->X,0,n*(m+1)*refs*sizeof(*s->X));
        memset(s->last_y,0,n*channels*sizeof(*s->last_y));
        memset(s->play_buf,0,(size_t)(PLAYBACK_DELAY+1)*(size_t)s->frame_size*refs*sizeof(*s->play_buf));
    }
}

int baffle_speex_recovered(SpeexEchoState *s)
{
    /* Cancellation increments this before processing. A zero afterward means
     * the backend's divergence guard reset itself, not ordinary convergence. */
    return s->cancel_count == 0;
}

void baffle_speex_prepare(SpeexEchoState *s)
{
    /* This signed counter controls only the AUMDF modulo schedule. Preserve
     * its phase without signed overflow on months-long continuous streams. */
    if (s->cancel_count >= INT_MAX-1) {
        int period=s->M-1;
        s->cancel_count=period > 0 ? 1+(s->cancel_count-1)%period:1;
    }
}

#ifdef BF_AEC_DIAGNOSTICS
/* Offline static qualification only; never queried on a callback path. */
void baffle_speex_diagnostics(SpeexEchoState *s, float *v)
{
    v[0]=(float)s->adapted;v[1]=(float)s->cancel_count;
    v[2]=s->leak_estimate;v[3]=s->sum_adapt;
    v[4]=(float)s->saturated;v[5]=(float)s->screwed_up;
}
void baffle_speex_counter_limit(SpeexEchoState *s)
{
    s->cancel_count=INT_MAX-1;
}
#endif
