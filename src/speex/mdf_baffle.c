/* Baffle's narrow integration of the hash-pinned, unmodified Speex MDF source.
 * The upstream notch defaults are too destructive for near-end low speech at
 * several rates. Keep the same stable two-pole DC rejector, but set a consistent
 * 20 Hz pole-radius parameter. This is not an exact -3 dB cutoff specification.
 * No adaptation, double-talk or output-suppression parameters are changed.
 * Including the source keeps its private state out of Baffle's portable API. */
#include "mdf.c"

void baffle_speex_configure_capture(SpeexEchoState *s)
{
    s->notch_radius = expf(-6.2831853071795864769f * 20.0f / (float)s->sampling_rate);
}
