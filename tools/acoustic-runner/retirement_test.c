#include "hail_owner.h"
#include <hail/virtual.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void)
{
    ha_context_config c = HA_INIT(ha_context_config);
    c.backend = HA_BACKEND_VIRTUAL;
    ha_context *context = NULL;
    ha_stream *stream = NULL;
    CHECK(ha_context_create(&c, &context) == HA_OK);
    ha_stream_config sc = HA_INIT(ha_stream_config);
    sc.directions = HA_CAPTURE;
    sc.capture_format = (ha_format)HA_FORMAT_INIT(16000, 1, HA_PCM_F32);
    sc.capture_device = (ha_device_selector)HA_INIT(ha_device_selector);
    sc.quantum_frames = 160;
    sc.capture_slots = 2;
    CHECK(ha_stream_open(context, &sc, &stream) == HA_OK);
    ha_virtual_config v = HA_INIT(ha_virtual_config);
    v.host_time_ns = 1000000000;
    v.faults = HA_VIRTUAL_HOLD_RETIREMENT;
    CHECK(ha_virtual_configure(stream, &v) == HA_OK);
    ha_result last = HA_OK;
    CHECK(!demo_hail_retire(&context, &stream, 0, &last));
    CHECK(context && stream && last == HA_PENDING);
    v.faults = 0;
    CHECK(ha_virtual_configure(stream, &v) == HA_OK);
    CHECK(demo_hail_retire(&context, &stream, 100, &last));
    CHECK(!context && !stream && last == HA_OK);
    CHECK(demo_hail_retire(&context, &stream, 0, &last));
    puts("PASS pending retirement retains ownership; retry releases exactly once");
    return 0;
}
