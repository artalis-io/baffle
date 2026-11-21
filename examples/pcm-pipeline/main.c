/* Installed-public-header consumer. Generated PCM, not device qualification. */
#include <baffle.h>
#include <stdio.h>
#include <stdlib.h>
static void check(bf_result r)
{
    if (r != BF_OK) {
        fprintf(stderr, "%s\n", bf_result_description(r));
        exit(1);
    }
}
int main(void)
{
    bf_pipeline_config c = BF_INIT(bf_pipeline_config);
    c.sample_rate = 16000;
    c.channels = 1;
    c.reference_channels = 1;
    c.frame_frames = 160;
    c.stages = BF_STAGE_NOISE | BF_STAGE_AGC | BF_STAGE_VAD;
    c.noise_strength_db = 18;
    c.agc_target_dbfs = -18;
    size_t bytes;
    check(bf_pipeline_size(&c, &bytes));
    unsigned char *raw = malloc(bytes + BF_ALIGNMENT);
    if (!raw)
        return 1;
    void *memory = (void *)(((uintptr_t)raw + BF_ALIGNMENT - 1u) & ~(uintptr_t)(BF_ALIGNMENT - 1u));
    bf_pipeline *s;
    check(bf_pipeline_init(memory, bytes, &c, &s));
    float microphone[160] = {0}, processed[160];
    bf_audio_buffer input = BF_INIT(bf_audio_buffer);
    input.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    input.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    input.format.sample_rate = 16000;
    input.format.channels = 1;
    input.format.sample_format = BF_PCM_F32;
    input.format.storage = BF_INTERLEAVED;
    input.format.channel_layout = BF_LAYOUT_MONO;
    input.frames = input.capacity_frames = 160;
    input.data = microphone;
    input.timing.stream_id = 1;
    input.timing.clock_domain = 17;
    input.timing.epoch = 1;
    input.timing.uncertainty_ns = UINT64_MAX; /* No observed host/device timestamps. */
    bf_audio_buffer output = input;
    output.data = processed;
    uint32_t emitted = 0;
    bf_process_info info = BF_INIT(bf_process_info);
    for (unsigned i = 0; i < 100; ++i) {
        check(bf_pipeline_process(s, &input, NULL, &output, &info));
        emitted += output.frames; /* Deliver output.frames to your inference transport here; zero
                                     while priming. */
        check(bf_timing_advance(&input.timing, 160, 16000, &input.timing));
    }
    check(bf_pipeline_flush(s, &output, &info));
    emitted += output.frames;
    check(bf_pipeline_deinit(s));
    free(raw);
    printf("generated silence: %u frames, delay %u frames, caller storage %zu bytes\n", emitted,
           info.algorithmic_delay_frames, bytes);
    return emitted == 16000 ? 0 : 1;
}
