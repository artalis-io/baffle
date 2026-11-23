#ifndef BAFFLE_QA_HAIL_OWNER_H
#define BAFFLE_QA_HAIL_OWNER_H
#include "../common/demo.h"
#include <hail.h>
/* Only SDK retirement success permits reclamation. Timeout preserves handles.
 * timeout_ms==0 permits one bounded poll, useful for deterministic virtual tests. */
static inline int demo_hail_retire(ha_context **context, ha_stream **stream, uint32_t timeout_ms,
                                   ha_result *last)
{
    ha_result r = HA_OK;
    uint64_t start = 0, now = 0;
    if (!context || !stream) {
        if (last)
            *last = HA_INVALID_ARGUMENT;
        return 0;
    }
    if (!*context && !*stream) {
        if (last)
            *last = HA_OK;
        return 1;
    }
    if (!demo_now(&start)) {
        if (last)
            *last = HA_PENDING;
        return 0;
    }
    if (*stream) {
        r = ha_stream_close_begin(*stream);
        if (r != HA_PENDING && r != HA_OK)
            goto failed;
        for (unsigned calls = 0; calls < 5001; ++calls) {
            uint32_t n = 0;
            r = ha_context_poll(*context, NULL, 0, &n);
            if (r != HA_OK)
                goto failed;
            r = ha_stream_close_finish(stream);
            if (r == HA_OK && !*stream)
                break;
            if (r != HA_PENDING)
                goto failed;
            if (!demo_now(&now) || now < start || (now - start) / 1000000u >= timeout_ms)
                goto failed;
            if (!demo_sleep_ms(1))
                goto failed;
        }
        if (*stream) {
            r = HA_PENDING;
            goto failed;
        }
    }
    if (*context) {
        for (unsigned calls = 0; calls < 5001; ++calls) {
            r = ha_context_destroy(context);
            if (r == HA_OK && !*context)
                break;
            if (r != HA_PENDING)
                goto failed;
            if (!demo_now(&now) || now < start || (now - start) / 1000000u >= timeout_ms)
                goto failed;
            if (!demo_sleep_ms(1))
                goto failed;
        }
        if (*context) {
            r = HA_PENDING;
            goto failed;
        }
    }
    if (last)
        *last = HA_OK;
    return 1;
failed:
    if (last)
        *last = r;
    return 0;
}
#endif
