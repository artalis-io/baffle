/* C11 offline/paced public-API qualification. No file I/O or allocations in
 * measured DSP calls; no physical timestamps or acoustic silence are inferred. */
#include "../common/demo.h"
#include <baffle.h>
#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#include <dlfcn.h>
#endif
#ifdef BF_COMPARE_WEBRTC
#include "webrtc_bridge.h"
#endif
#ifdef BF_EXPERIMENTAL_RESIDUAL
#include "residual_experiment_c.h"
#endif
typedef struct Receipt {
    int64_t release, start, end, allocations;
    uint32_t flags, priming, discarded, vad_active;
    float vad_probability, gain;
    uint64_t aec_resets;
} Receipt;
typedef struct Transport {
    uint64_t frame, capture_epoch, reference_epoch, generation;
    uint32_t missing;
} Transport;
typedef struct Runner {
    float near_pcm[480], far_pcm[480], output[480];
    bf_audio_buffer n, r, o;
    bf_aec *aec;
    bf_pipeline *pipeline;
    Transport transport[128];
    size_t event_count, event_index;
    int missing;
    uint32_t frames, stages;
    Receipt observed;
#ifdef BF_COMPARE_WEBRTC
    BaffleQaWebrtc *webrtc;
#endif
#ifdef BF_EXPERIMENTAL_RESIDUAL
    ResidualExperiment *residual;
#endif
} Runner;
static bf_audio_buffer view(float *data, uint32_t rate, uint32_t frames, uint64_t domain)
{
    bf_audio_buffer b = BF_INIT(bf_audio_buffer);
    b.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    b.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    b.format.sample_rate = rate;
    b.format.channels = 1;
    b.format.sample_format = BF_PCM_F32;
    b.format.storage = BF_INTERLEAVED;
    b.format.channel_layout = BF_LAYOUT_MONO;
    b.data = data;
    b.frames = b.capacity_frames = frames;
    b.timing.stream_id = b.timing.clock_domain = domain;
    b.timing.epoch = 1;
    b.timing.uncertainty_ns = UINT64_MAX;
    return b;
}
static int64_t rss_kib(void)
{
#ifdef __linux__
    FILE *f = demo_fopen("/proc/self/statm", "r");
    char line[256], *end = NULL;
    if (!f)
        return -1;
    int ok = fgets(line, sizeof(line), f) != NULL;
    if (fclose(f))
        ok = 0;
    if (!ok)
        return -1;
    errno = 0;
    (void)strtoull(line, &end, 10);
    if (errno || end == line)
        return -1;
    const char *p = end;
    unsigned long long resident = strtoull(p, &end, 10);
    long page = sysconf(_SC_PAGESIZE);
    if (errno || end == p || page <= 0 ||
        resident > (unsigned long long)INT64_MAX / (unsigned long long)page)
        return -1;
    return (int64_t)(resident * (unsigned long long)page / 1024);
#else
    return -1;
#endif
}
static int load_events(Runner *s, const char *path, size_t frames)
{
    FILE *f = demo_fopen(path, "r");
    if (!f)
        return 0;
    char line[512];
    int ok = 1;
    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) {
            ok = 0;
            break;
        }
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            ++p;
        if (!*p)
            continue;
        uint64_t values[5] = {0};
        for (unsigned i = 0; i < 5; ++i) {
            char *end = NULL;
            while (*p == ' ' || *p == '\t')
                ++p;
            if (*p < '0' || *p > '9') {
                ok = 0;
                break;
            }
            errno = 0;
            unsigned long long v = strtoull(p, &end, 10);
            if (errno || p == end) {
                ok = 0;
                break;
            }
            values[i] = (uint64_t)v;
            p = end;
        }
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            ++p;
        if (!ok || *p || s->event_count == 128 || values[0] >= frames || values[0] % s->frames ||
            values[4] > 1 ||
            (s->event_count && values[0] <= s->transport[s->event_count - 1].frame)) {
            ok = 0;
            break;
        }
        s->transport[s->event_count++] =
            (Transport){values[0], values[1], values[2], values[3], (uint32_t)values[4]};
    }
    if (ferror(f))
        ok = 0;
    if (fclose(f))
        ok = 0;
    return ok;
}
static bf_result process(Runner *s, uint64_t position, uint32_t *flags)
{
    memset(&s->observed, 0, sizeof(s->observed));
    s->observed.gain = 1;
    s->observed.vad_probability = -1;
    s->n.timing.frame_position = s->r.timing.frame_position = position;
    s->n.timing.flags = s->r.timing.flags = 0;
    if (s->event_index < s->event_count && s->transport[s->event_index].frame == position) {
        Transport e = s->transport[s->event_index++];
        if (e.capture_epoch != s->n.timing.epoch)
            s->n.timing.flags |= BF_AUDIO_DISCONTINUITY;
        if (e.reference_epoch != s->r.timing.epoch)
            s->r.timing.flags |= BF_AUDIO_DISCONTINUITY;
        s->n.timing.epoch = e.capture_epoch;
        s->r.timing.epoch = e.reference_epoch;
        s->r.timing.generation = e.generation;
        s->missing = e.missing != 0;
    }
    if (s->aec) {
        bf_aec_info info = BF_INIT(bf_aec_info);
        bf_result r =
            bf_aec_process(s->aec, s->near_pcm, &s->n.timing, s->missing ? NULL : s->far_pcm,
                           s->missing ? NULL : &s->r.timing, s->output, &info);
        s->observed.aec_resets = info.resets;
        *flags = info.flags;
        return r;
    }
    if (s->pipeline) {
        bf_process_info info = BF_INIT(bf_process_info);
#ifdef BF_EXPERIMENTAL_RESIDUAL
        bf_aec_info ai = BF_INIT(bf_aec_info);
        bf_result r =
            residual_process(s->residual, s->near_pcm, &s->n.timing, s->missing ? NULL : s->far_pcm,
                             s->missing ? NULL : &s->r.timing, &ai);
        if (r != BF_OK)
            return r;
#endif
        bf_result status =
            bf_pipeline_process(s->pipeline, &s->n, s->missing ? NULL : &s->r, &s->o, &info);
        if (status != BF_OK)
            return status;
#ifdef BF_EXPERIMENTAL_RESIDUAL
        info.aec = ai;
        info.flags |= ai.flags;
#endif
        s->observed.priming = info.priming;
        s->observed.discarded = info.discarded_frames;
        s->observed.vad_active = info.vad.active;
        if (s->stages & BF_STAGE_VAD)
            s->observed.vad_probability = info.vad.probability;
        if (s->stages & BF_STAGE_AGC)
            s->observed.gain = info.level.gain;
        s->observed.aec_resets = info.aec.resets;
        if (!s->o.frames)
            memset(s->output, 0, s->frames * sizeof(float));
        *flags = info.flags;
        return BF_OK;
    }
#ifdef BF_COMPARE_WEBRTC
    if (!baffle_qa_webrtc_process(s->webrtc, s->near_pcm, s->far_pcm, s->output))
        return BF_ERROR_STATE;
#endif
    *flags = 0;
    return BF_OK;
}
static int store_pipeline(Runner *s, float *result, size_t length, uint32_t delay)
{
    if (!s->o.frames || s->o.timing.frame_position >= length)
        return 1;
    size_t source = (size_t)s->o.timing.frame_position, count = length - source;
    if (count > s->o.frames)
        count = s->o.frames;
    if (source > SIZE_MAX - delay || count > length + delay - (source + delay))
        return 0;
    memcpy(result + source + delay, s->output, count * sizeof(float));
    return 1;
}
#define REQUIRE(x, m)                                                                              \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            error = (m);                                                                           \
            goto done;                                                                             \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    int result_code = 1;
    const char *error = NULL, *backend = NULL;
    Runner *s = NULL;
    float *mic = NULL, *ref = NULL, *result = NULL;
    size_t length = 0, ref_length = 0, bytes = 0, calls = 0;
    Receipt *receipts = NULL;
    demo_arena arena = {0};
    demo_waiter waiter = {0};
    int waiter_open = 0;
    FILE *raw = NULL, *summary = NULL;
    char *environment = NULL;
    REQUIRE(argc >= 8 && argc <= 10, "runner backend rate mic.f32 ref.f32 out.f32 receipts.csv "
                                     "summary.json [seconds [default|highres]]");
    uint16_t endian = 1;
    REQUIRE(*(unsigned char *)&endian == 1, "little-endian host required");
    backend = argv[1];
#ifdef BF_EXPERIMENTAL_RESIDUAL
    int split_control = !strcmp(backend, "residual-control");
    if (split_control)
        backend = "pipeline";
    REQUIRE(!strcmp(backend, "pipeline"),
            "Residual experiment accepts pipeline or residual-control only");
#endif
    uint32_t rate = 0, seconds = 0;
    REQUIRE(demo_number(argv[2], 48000, &rate) &&
                (rate == 8000 || rate == 16000 || rate == 32000 || rate == 48000),
            "unsupported rate");
    REQUIRE(argc < 9 || (demo_number(argv[8], 600, &seconds) && seconds), "unsupported duration");
    int highres = argc == 10 && !strcmp(argv[9], "highres");
    REQUIRE(argc < 10 || highres || !strcmp(argv[9], "default"), "unknown wait policy");
    uint32_t frames = !strcmp(backend, "ns5") ? rate / 200 : rate / 100;
    REQUIRE(strcmp(backend, "ns5") || !seconds, "ns5 is offline-only");
    int pipeline_backend = !strcmp(backend, "pipeline") || !strcmp(backend, "aec-ns") ||
                           !strcmp(backend, "aec-ns-agc") || !strcmp(backend, "aec-vad") ||
                           !strcmp(backend, "ns-only") || !strcmp(backend, "ns-zero") ||
                           !strcmp(backend, "ns5") || !strcmp(backend, "agc-only") ||
                           !strcmp(backend, "aec-agc") || !strcmp(backend, "ns-agc");
    uint32_t stages = BF_STAGE_ALL;
    if (!strcmp(backend, "aec-ns"))
        stages = BF_STAGE_AEC | BF_STAGE_NOISE;
    if (!strcmp(backend, "aec-ns-agc"))
        stages = BF_STAGE_AEC | BF_STAGE_NOISE | BF_STAGE_AGC;
    if (!strcmp(backend, "aec-vad"))
        stages = BF_STAGE_AEC | BF_STAGE_VAD;
    if (!strcmp(backend, "agc-only"))
        stages = BF_STAGE_AGC;
    if (!strcmp(backend, "aec-agc"))
        stages = BF_STAGE_AEC | BF_STAGE_AGC;
    if (!strcmp(backend, "ns-agc"))
        stages = BF_STAGE_NOISE | BF_STAGE_AGC;
    if (!strcmp(backend, "ns-only") || !strcmp(backend, "ns-zero") || !strcmp(backend, "ns5"))
        stages = BF_STAGE_NOISE;
    uint32_t delay = pipeline_backend && (stages & BF_STAGE_NOISE) ? frames : 0;
    uint32_t tail_ms = !strcmp(backend, "baffle-40ms")    ? 40u
                       : !strcmp(backend, "baffle-500ms") ? 500u
                                                          : 200u;
    REQUIRE(demo_wait_init(&waiter, highres), "wait policy unavailable");
    waiter_open = 1;
    REQUIRE(demo_load_f32(argv[3], 256u * 1024u * 1024u, &mic, &length) &&
                demo_load_f32(argv[4], 256u * 1024u * 1024u, &ref, &ref_length),
            "invalid input size/read");
    REQUIRE(length == ref_length, "input duration mismatch");
    for (size_t i = 0; i < length; ++i)
        REQUIRE(isfinite(mic[i]) && isfinite(ref[i]) && fabsf(mic[i]) <= 16 && fabsf(ref[i]) <= 16,
                "invalid PCM");
    calls = seconds ? (size_t)seconds * 100 : (length + frames - 1) / frames;
    s = (Runner *)calloc(1, sizeof(*s));
    REQUIRE(s, "runner allocation");
    s->frames = frames;
    s->stages = stages;
#ifdef _MSC_VER
    size_t environment_size = 0;
    REQUIRE(!_dupenv_s(&environment, &environment_size, "BF_QUALIFICATION_TRANSPORT_EVENTS"),
            "transport environment read");
#else
    const char *value = getenv("BF_QUALIFICATION_TRANSPORT_EVENTS");
    if (value) {
        size_t n = strlen(value);
        environment = (char *)malloc(n + 1);
        REQUIRE(environment, "environment storage");
        memcpy(environment, value, n + 1);
    }
#endif
    if (environment && *environment) {
        REQUIRE(!seconds && strncmp(backend, "aec3", 4),
                "transport injection supports offline public Baffle only");
        REQUIRE(load_events(s, environment, length),
                "bounded ordered frame-aligned transport events required");
    }
    REQUIRE(calls <= SIZE_MAX / sizeof(Receipt) && length <= SIZE_MAX / sizeof(float) - delay,
            "output plan overflow");
    receipts = (Receipt *)calloc(calls, sizeof(*receipts));
    REQUIRE(receipts, "receipt allocation");
    if (!seconds) {
        result = (float *)calloc(length + delay, sizeof(float));
        REQUIRE(result, "output allocation");
    }
    int64_t before = rss_kib();
    if (!strcmp(backend, "baffle") || !strcmp(backend, "baffle-40ms") ||
        !strcmp(backend, "baffle-500ms")) {
        bf_aec_config c = BF_INIT(bf_aec_config);
        c.sample_rate = rate;
        c.frame_frames = frames;
        c.capture_channels = c.reference_channels = 1;
        c.tail_ms = tail_ms;
        c.backend = BF_AEC_SPEEXDSP;
        REQUIRE(bf_aec_size(&c, &bytes) == BF_OK && demo_arena_init(&arena, bytes, BF_ALIGNMENT) &&
                    bf_aec_init(arena.aligned, bytes, &c, &s->aec) == BF_OK,
                "AEC init");
    } else if (pipeline_backend) {
        bf_pipeline_config c = BF_INIT(bf_pipeline_config);
        c.sample_rate = rate;
        c.frame_frames = frames;
        c.channels = c.reference_channels = 1;
        c.stages = stages;
        c.aec_tail_ms = 200;
        c.noise_strength_db = !strcmp(backend, "ns-zero") ? 0.0f : 18.0f;
        c.agc_target_dbfs = -18;
#ifdef BF_EXPERIMENTAL_RESIDUAL
        REQUIRE(residual_open(rate, !split_control, &s->residual) == BF_OK,
                "residual experiment init");
        c.stages &= ~BF_STAGE_AEC;
#endif
        REQUIRE(bf_pipeline_size(&c, &bytes) == BF_OK &&
                    demo_arena_init(&arena, bytes, BF_ALIGNMENT) &&
                    bf_pipeline_init(arena.aligned, bytes, &c, &s->pipeline) == BF_OK,
                "pipeline init");
#ifdef BF_EXPERIMENTAL_RESIDUAL
        REQUIRE(residual_bytes(s->residual) <= SIZE_MAX - bytes, "residual workspace overflow");
        bytes += residual_bytes(s->residual);
#endif
    } else if (!strcmp(backend, "aec3") || !strcmp(backend, "aec3-linear")) {
#ifdef BF_COMPARE_WEBRTC
        REQUIRE(baffle_qa_webrtc_open(rate, !strcmp(backend, "aec3-linear"), &s->webrtc),
                "AEC3 unavailable/init failed");
#else
        REQUIRE(0, "AEC3 unavailable: configure BF_COMPARE_WEBRTC with external package");
#endif
    } else
        REQUIRE(0, "unknown backend");
    int64_t initialized = rss_kib();
    void (*alloc_begin)(void) = NULL;
    unsigned long long (*alloc_end)(void) = NULL;
#ifdef __linux__
    _Static_assert(sizeof(void *) == sizeof(alloc_begin) && sizeof(void *) == sizeof(alloc_end),
                   "POSIX function pointer size");
    void *begin_symbol = dlsym(RTLD_DEFAULT, "bf_qa_alloc_begin"),
         *end_symbol = dlsym(RTLD_DEFAULT, "bf_qa_alloc_end");
    memcpy(&alloc_begin, &begin_symbol, sizeof(alloc_begin));
    memcpy(&alloc_end, &end_symbol, sizeof(alloc_end));
#endif
    s->n = view(s->near_pcm, rate, frames, 1);
    s->r = view(s->far_pcm, rate, frames, 2);
    s->o = view(s->output, rate, frames, 1);
    uint64_t position = 0;
    uint32_t flags = 0;
    if (seconds)
        for (unsigned i = 0; i < 100; ++i) {
            REQUIRE(process(s, position, &flags) == BF_OK, "warm-up DSP");
            position += frames;
        }
    uint64_t origin;
    REQUIRE(demo_now(&origin), "monotonic clock");
    for (size_t call = 0; call < calls; ++call) {
        size_t offset = call * frames, count = seconds ? frames : length - offset;
        if (count > frames)
            count = frames;
        memset(s->near_pcm, 0, frames * sizeof(float));
        memset(s->far_pcm, 0, frames * sizeof(float));
        for (size_t i = 0; i < count; ++i) {
            s->near_pcm[i] = mic[(offset + i) % length];
            s->far_pcm[i] = ref[(offset + i) % length];
        }
        uint64_t release, start, end;
        REQUIRE(demo_now(&release), "release clock");
        if (seconds) {
            release = origin + call * 10000000u;
            REQUIRE(demo_wait_until(&waiter, release), "release wait");
        }
        REQUIRE(demo_now(&start), "start clock");
        if (alloc_begin && alloc_end)
            alloc_begin();
        bf_result status = process(s, position, &flags);
        int64_t allocations = -1;
        if (alloc_begin && alloc_end) {
            unsigned long long n = alloc_end();
            REQUIRE(n <= INT64_MAX, "allocation counter overflow");
            allocations = (int64_t)n;
        }
        REQUIRE(demo_now(&end) && start >= origin && end >= start && end - origin <= INT64_MAX &&
                    release >= origin,
                "processing clock interval");
        REQUIRE(status == BF_OK, "DSP process");
        s->observed.release = (int64_t)(release - origin);
        s->observed.start = (int64_t)(start - origin);
        s->observed.end = (int64_t)(end - origin);
        s->observed.allocations = allocations;
        s->observed.flags = flags;
        receipts[call] = s->observed;
        for (uint32_t i = 0; i < frames; ++i)
            REQUIRE(isfinite(s->output[i]), "nonfinite output");
        if (!seconds) {
            if (s->pipeline)
                REQUIRE(store_pipeline(s, result, length, delay), "pipeline output bounds");
            else
                memcpy(result + offset, s->output, count * sizeof(float));
        }
        position += frames;
    }
    if (s->pipeline && !seconds) {
        int drained = 0;
        for (unsigned i = 0; i < 3; ++i) {
            bf_process_info info = BF_INIT(bf_process_info);
            REQUIRE(bf_pipeline_flush(s->pipeline, &s->o, &info) == BF_OK, "pipeline flush");
            if (!s->o.frames) {
                drained = 1;
                break;
            }
            REQUIRE(store_pipeline(s, result, length, delay), "flush output bounds");
        }
        REQUIRE(drained, "bounded drain failed");
    }
    int64_t final_rss = rss_kib(), peak = -1;
#ifdef __linux__
    struct rusage usage;
    if (!getrusage(RUSAGE_SELF, &usage) && usage.ru_maxrss >= 0)
        peak = (int64_t)usage.ru_maxrss;
#endif
    if (s->aec) {
        REQUIRE(bf_aec_deinit(s->aec) == BF_OK, "AEC deinit");
        s->aec = NULL;
    }
    if (s->pipeline) {
        REQUIRE(bf_pipeline_deinit(s->pipeline) == BF_OK, "pipeline deinit");
        s->pipeline = NULL;
    }
    REQUIRE(demo_write_f32(argv[5], result, seconds ? 0 : length + delay), "output write");
    raw = demo_fopen(argv[6], "wb");
    summary = demo_fopen(argv[7], "wb");
    REQUIRE(raw && summary, "receipt open");
    fprintf(raw, "frame,release_ns,start_ns,end_ns,processing_ns,start_lateness_ns,completion_"
                 "deadline_miss,allocation_calls,flags,priming,discarded_frames,vad_active,vad_"
                 "probability,gain,aec_resets\n");
    uint32_t misses = 0, execution_misses = 0;
    for (size_t i = 0; i < calls; ++i) {
        Receipt t = receipts[i];
        int missed = seconds && t.end > t.release + 10000000;
        misses += (uint32_t)missed;
        execution_misses += (uint32_t)(t.end - t.start > 10000000);
        int64_t lateness = seconds && t.start > t.release ? t.start - t.release : 0;
        fprintf(raw, "%zu,%lld,%lld,%lld,%lld,%lld,%d,%lld,%u,%u,%u,%u,%.9g,%.9g,%llu\n", i,
                (long long)t.release, (long long)t.start, (long long)t.end,
                (long long)(t.end - t.start), (long long)lateness, missed, (long long)t.allocations,
                t.flags, t.priming, t.discarded, t.vad_active, (double)t.vad_probability,
                (double)t.gain, (unsigned long long)t.aec_resets);
    }
    fprintf(
        summary,
        "{\"rate\":%u,\"frames\":%u,\"calls\":%zu,\"wait_policy\":\"%s\",\"paced_seconds\":%u,"
        "\"workspace_bytes\":%zu,\"baffle_tail_ms\":%u,"
        "\"rss_before_init_kib\":%lld,\"rss_after_init_kib\":%lld,\"rss_after_process_kib\":%lld,"
        "\"process_peak_rss_kib\":%lld,\"completion_deadline_misses\":%u,"
        "\"execution_deadline_misses\":%u,\"added_pipeline_delay_frames\":%u,\"stage_mask\":%u}\n",
        rate, frames, calls, highres ? "private-windows-highres" : "standard-sleep", seconds, bytes,
        !strncmp(backend, "baffle", 6) ? tail_ms : 0, (long long)before, (long long)initialized,
        (long long)final_rss, (long long)peak, misses, execution_misses, delay,
        pipeline_backend ? stages : BF_STAGE_AEC);
    REQUIRE(!ferror(raw) && !ferror(summary), "receipt write");
    result_code = 0;
done:
    if (raw && fclose(raw))
        result_code = 1;
    if (summary && fclose(summary))
        result_code = 1;
    if (s) {
        if (s->aec && (bf_aec_deinit(s->aec) != BF_OK))
            result_code = 1;
        if (s->pipeline && (bf_pipeline_deinit(s->pipeline) != BF_OK))
            result_code = 1;
#ifdef BF_EXPERIMENTAL_RESIDUAL
        residual_close(s->residual);
#endif
#ifdef BF_COMPARE_WEBRTC
        baffle_qa_webrtc_close(s->webrtc);
#endif
    }
    if (waiter_open && !demo_wait_close(&waiter))
        result_code = 1;
    demo_arena_free(&arena);
    free(s);
    free(mic);
    free(ref);
    free(result);
    free(receipts);
    free(environment);
    if (error)
        fprintf(stderr, "%s\n", error);
    return result_code;
}
