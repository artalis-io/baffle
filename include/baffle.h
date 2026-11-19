#ifndef BF_BAFFLE_H
#define BF_BAFFLE_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32) && !defined(BF_STATIC)
#if defined(BF_BUILDING_LIBRARY)
#define BF_API __declspec(dllexport)
#else
#define BF_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define BF_API __attribute__((visibility("default")))
#else
#define BF_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define BF_VERSION_MAJOR 0u
#define BF_VERSION_MINOR 1u
#define BF_VERSION_PATCH 0u
#define BF_STRUCT_VERSION 1u
#ifdef __cplusplus
#define BF_INIT(type) []() { type bf_value = {}; bf_value.struct_size=(uint32_t)sizeof(type); bf_value.struct_version=BF_STRUCT_VERSION; return bf_value; }()
#else
#define BF_INIT(type) { .struct_size=(uint32_t)sizeof(type), .struct_version=BF_STRUCT_VERSION }
#endif
typedef int32_t bf_result;
#define BF_OK ((bf_result)0)
#define BF_NEED_OUTPUT ((bf_result)1)
#define BF_ERROR_INVALID ((bf_result)-1)
#define BF_ERROR_CAPACITY ((bf_result)-2)
#define BF_ERROR_UNSUPPORTED ((bf_result)-3)
#define BF_ERROR_STATE ((bf_result)-4)
#define BF_ERROR_NONFINITE ((bf_result)-5)
#define BF_ERROR_OVERFLOW ((bf_result)-6)
typedef struct bf_library_info {
    uint32_t struct_size, struct_version;
    uint32_t major, minor, patch, features;
} bf_library_info;
/* Borrowed pointers last only for a call. Records may have larger unknown tails;
 * version 1 required prefixes may not be shortened. ABI zero is provisional. */
BF_API bf_result bf_library_query(bf_library_info *info);
BF_API const char *bf_result_description(bf_result result);
#define BF_MAX_CHANNELS 32u
#define BF_MAX_FRAMES 8192u
#define BF_PCM_F32 1u
#define BF_PCM_S16 2u
#define BF_INTERLEAVED 1u
#define BF_PLANAR 2u
#define BF_LAYOUT_MONO 1u
#define BF_LAYOUT_STEREO 2u
#define BF_LAYOUT_DISCRETE 3u
#define BF_TIME_HOST_VALID 1u
#define BF_TIME_DEVICE_VALID 2u
#define BF_TIME_ESTIMATED 4u
#define BF_TIME_INTERPOLATED 8u
#define BF_AUDIO_DISCONTINUITY 16u
#define BF_AUDIO_GAP 32u
#define BF_AUDIO_SILENCE 64u
#define BF_AUDIO_SANITIZED 128u
#define BF_REFERENCE_MISSING 256u
#define BF_REFERENCE_UNALIGNED 512u
#define BF_NONFINITE_REJECT 0u
#define BF_NONFINITE_ZERO 1u
#define BF_ALIGNMENT 16u
typedef struct bf_pcm_format {
    uint32_t struct_size, struct_version;
    uint32_t sample_rate, channels, sample_format, storage, channel_layout, reserved;
} bf_pcm_format;
typedef struct bf_audio_timing {
    uint32_t struct_size, struct_version;
    uint64_t stream_id, clock_domain, epoch, generation, frame_position;
    uint64_t device_position, host_time_ns, uncertainty_ns, lost_frames;
    uint32_t flags, reserved;
} bf_audio_timing;
typedef struct bf_audio_buffer {
    uint32_t struct_size, struct_version;
    bf_pcm_format format;
    void *data; /* interleaved; borrowed, never retained */
    void **planes; /* planar: channels pointers, each capacity_frames samples */
    uint32_t frames, capacity_frames;
    bf_audio_timing timing;
} bf_audio_buffer;
typedef struct bf_clock_estimate {
    uint32_t struct_size, struct_version;
    double observed_ppm;
    uint64_t observation_ns, uncertainty_ns;
    uint32_t valid, uncertain;
} bf_clock_estimate;
typedef struct bf_clock_tracker bf_clock_tracker;
/* Validates geometry, not accessibility of arbitrary caller pointers. */
BF_API bf_result bf_pcm_validate(const bf_audio_buffer *buffer);
/* Formats must have equal rate/channel count. Exact F32 in-place is supported;
 * all other source/destination storage must be disjoint. Nonfinite rejection
 * is transactional; ZERO sanitizes nonfinite and subnormal values explicitly. */
BF_API bf_result bf_pcm_convert(const bf_audio_buffer *input, bf_audio_buffer *output, uint32_t nonfinite_policy);
BF_API bf_result bf_timing_advance(const bf_audio_timing *input, uint32_t frames, uint32_t rate, bf_audio_timing *output);
/* No allocator is used: caller storage must be BF_ALIGNMENT aligned and last
 * until all users have quiesced. One owner per context; no concurrent calls. */
BF_API size_t bf_clock_tracker_size(void);
BF_API bf_result bf_clock_tracker_init(void *storage, size_t bytes, bf_clock_tracker **out);
BF_API void bf_clock_tracker_reset(bf_clock_tracker *tracker);
BF_API bf_result bf_clock_tracker_observe(bf_clock_tracker *tracker, const bf_audio_timing *timing,
                                          uint32_t rate, bf_clock_estimate *output);
#ifdef __cplusplus
}
#endif
#endif
