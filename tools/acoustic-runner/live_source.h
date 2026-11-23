#ifndef BAFFLE_DEMO_LIVE_SOURCE_H
#define BAFFLE_DEMO_LIVE_SOURCE_H
#include <baffle.h>
#include <stdint.h>
#include <stddef.h>
// Private application interface, not an installed Baffle API. Borrowed only
// during sink(). Calls run on the application capture consumer, never the
// device callback. The sink must copy before returning.
typedef struct BaffleLiveBlock {
    const float *raw;
    const float *processed;
    bf_audio_timing timing;
    uint64_t capture_lost;
    uint32_t vad_active;
    float rms, peak;
} BaffleLiveBlock;
typedef int (*BaffleLiveSink)(const BaffleLiveBlock *, void *);
typedef int (*BaffleLiveStop)(void *);
/* Return 2 means SDK retirement remains pending/failed: storage was retained.
 * An interactive caller must not reopen another session after that result. */
int baffle_live_run(uint32_t backend, const char *key, uint32_t seconds, BaffleLiveSink sink,
                    BaffleLiveStop stop, void *user, char *error, size_t error_capacity, int gain,
                    int feedback);
#endif
