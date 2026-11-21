/* Deliberately calls no core bf_ function: shared dependencies must still load. */
#include <baffle/hail.h>
#include <hail/virtual.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void)
{
    ha_context_config cc = HA_INIT(ha_context_config);
    ha_context *context = NULL;
    ha_stream *stream = NULL;
    cc.backend = HA_BACKEND_VIRTUAL;
    CHECK(ha_context_create(&cc, &context) == HA_OK);
    ha_stream_config sc = HA_INIT(ha_stream_config);
    sc.directions = HA_CAPTURE | HA_PLAYBACK;
    sc.capture_format = sc.playback_format = (ha_format)HA_FORMAT_INIT(16000, 1, HA_PCM_F32);
    sc.capture_device = sc.playback_device = (ha_device_selector)HA_INIT(ha_device_selector);
    sc.quantum_frames = 160;
    sc.capture_slots = sc.playback_slots = sc.reference_slots = 2;
    CHECK(ha_stream_open(context, &sc, &stream) == HA_OK);
    bf_hail_config c = BF_INIT(bf_hail_config);
    c.pipeline = (bf_pipeline_config)BF_INIT(bf_pipeline_config);
    c.pipeline.sample_rate = 16000;
    c.pipeline.channels = c.pipeline.reference_channels = 1;
    c.pipeline.frame_frames = 160;
    c.pipeline.agc_target_dbfs = -18;
    c.reference_history_frames = 640;
    size_t bytes;
    bf_hail_adapter *adapter = NULL;
    CHECK(bf_hail_adapter_size(stream, &c, &bytes) == BF_OK);
    unsigned char *raw = malloc(bytes + BF_ALIGNMENT);
    CHECK(raw);
    void *memory = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    CHECK(bf_hail_adapter_init(memory, bytes, stream, &c, &adapter) == BF_OK);
    CHECK(ha_stream_start(stream) == HA_PENDING);
    uint32_t events = 0;
    CHECK(ha_context_poll(context, NULL, 0, &events) == HA_OK);
    float input[160], output[160], playback[160] = {0}, rendered[160];
    for (unsigned i = 0; i < 160; ++i)
        input[i] = .1f;
    uint32_t written = 0;
    CHECK(ha_stream_write(stream, playback, 160, 1, &written) == HA_OK && written == 160);
    ha_virtual_step_info step = HA_INIT(ha_virtual_step_info);
    step.capture_frames = step.playback_frames = 160;
    step.capture_data = input;
    step.advance_ns = 10000000;
    ha_block_info receipt = HA_INIT(ha_block_info);
    CHECK(ha_virtual_step(stream, &step, rendered, 160, &receipt) == HA_OK);
    bf_audio_buffer out = BF_INIT(bf_audio_buffer);
    out.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    out.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    out.format.sample_rate = 16000;
    out.format.channels = 1;
    out.format.sample_format = BF_PCM_F32;
    out.format.storage = BF_INTERLEAVED;
    out.format.channel_layout = BF_LAYOUT_MONO;
    out.capacity_frames = 160;
    out.data = output;
    bf_process_info info = BF_INIT(bf_process_info);
    CHECK(bf_hail_adapter_pump(adapter, &out, &info) == BF_OK && out.frames == 160);
    CHECK(!memcmp(input, output, sizeof(input)));
    CHECK(out.timing.stream_id && out.timing.clock_domain && out.timing.epoch);
    CHECK(bf_hail_adapter_deinit(adapter) == BF_OK);
    free(raw);
    CHECK(ha_stream_close_begin(stream) == HA_PENDING);
    CHECK(ha_context_poll(context, NULL, 0, &events) == HA_OK);
    CHECK(ha_stream_close_finish(&stream) == HA_OK);
    CHECK(ha_context_destroy(&context) == HA_OK);
    CHECK(!memcmp(input, output, sizeof(input)));
    puts("PASS adapter-only installed consumer; copied PCM survives deinit");
    return 0;
}
