#ifndef BF_HAIL_ADAPTER_H
#define BF_HAIL_ADAPTER_H
#include <baffle.h>
#include <hail.h>
#if defined(_WIN32) && !defined(BF_HAIL_STATIC)
#if defined(BF_BUILDING_HAIL_ADAPTER)
#define BF_HAIL_API __declspec(dllexport)
#else
#define BF_HAIL_API __declspec(dllimport)
#endif
#else
#define BF_HAIL_API BF_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bf_hail_adapter bf_hail_adapter;
typedef struct bf_hail_config {
    uint32_t struct_size, struct_version;
    bf_pipeline_config pipeline;
    uint32_t reference_history_frames, allow_uncertain_reference;
    uint64_t reference_delay_ns, max_alignment_uncertainty_ns;
} bf_hail_config;
typedef struct bf_hail_status {
    uint32_t struct_size, struct_version;
    bf_audio_timing last_capture_source, last_reference_source, last_aligned_reference;
    bf_clock_estimate capture_clock, reference_clock;
    uint64_t processed_frames, missing_reference_frames, reference_overrun_frames,
        discarded_capture_frames, alignment_adjustments;
    uint64_t reference_invalidated_frames;
    uint32_t capture_pending_frames, reference_pending_frames, reference_history_frames,
        capture_alignment_delay_frames;
} bf_hail_status;
BF_HAIL_API bf_result bf_hail_translate_timing(uint64_t stream_id, const ha_block_info *source,
                                               bf_audio_timing *output);
/* Setup/control only. Stream is borrowed, must be duplex and have references.
 * Caller must deinit adapter before ha_stream_close_finish frees the stream. */
BF_HAIL_API bf_result bf_hail_adapter_size(ha_stream *stream, const bf_hail_config *config,
                                           size_t *bytes);
BF_HAIL_API bf_result bf_hail_adapter_init(void *storage, size_t bytes, ha_stream *stream,
                                           const bf_hail_config *config, bf_hail_adapter **out);
/* One serialized application I/O owner. At most one capture read and one DSP
 * frame; a rate-dependent bounded reference-read/SRC budget. Never polls Hail
 * control or writes playback. Output backpressure leaves Hail queues bounded.
 * Missing/uncertain alignment bypasses AEC, never blocks microphone delivery. */
BF_HAIL_API bf_result bf_hail_adapter_pump(bf_hail_adapter *adapter, bf_audio_buffer *output,
                                           bf_process_info *info);
BF_HAIL_API bf_result bf_hail_adapter_status(const bf_hail_adapter *adapter,
                                             bf_hail_status *status);
/* After capture is quiescent: no Hail I/O. Drain partial framing/NS output;
 * terminal zero padding is explicit and output is trimmed to real input frames.
 * Repeat until output.frames==0, then deinit. Pump is invalid after flush. */
BF_HAIL_API bf_result bf_hail_adapter_flush(bf_hail_adapter *adapter, bf_audio_buffer *output,
                                            bf_process_info *info);
BF_HAIL_API bf_result bf_hail_adapter_deinit(bf_hail_adapter *adapter);
#ifdef __cplusplus
}
#endif
#endif
