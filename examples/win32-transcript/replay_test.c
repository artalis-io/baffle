#include "speech.h"
#include <bcrypt.h>
#include <math.h>
static FILE *trace;
static float *raw_audio, *processed_audio;
static size_t audio_frames;
static double decode_ms[256], completion_ms[256];
static size_t measurements;
static uint32_t peak_queued;
static LARGE_INTEGER replay_origin, replay_frequency;
static int traced_decode(Speech *, const float *, size_t, SpeechResult *, char *, size_t);
#define speech_decode traced_decode
#ifndef BF_REPLAY_APP_SOURCE
#define BF_REPLAY_APP_SOURCE "app.c"
#endif
/* Test-only source injection: exercise the actual application worker/state,
 * replacing only microphone acquisition with paced immutable PCM. */
#include BF_REPLAY_APP_SOURCE
#undef speech_decode
static int traced_decode(Speech *s, const float *x, size_t n, SpeechResult *r, char *e, size_t c)
{
    LARGE_INTEGER begin, end;
    QueryPerformanceCounter(&begin);
    int ok = speech_decode(s, x, n, r, e, c);
    QueryPerformanceCounter(&end);
    if (!ok)
        return 0;
    if (measurements >= 256)
        return 0;
    decode_ms[measurements] = (double)(end.QuadPart - begin.QuadPart) * 1000 /
                              (double)replay_frequency.QuadPart;
    completion_ms[measurements++] = (double)(end.QuadPart - replay_origin.QuadPart) * 1000 /
                                    (double)replay_frequency.QuadPart;
    char digest[65] = {0};
    unsigned char bytes[32];
    if (n > ULONG_MAX / sizeof(float) ||
        BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)x, (ULONG)(n * sizeof(float)), bytes,
                   sizeof(bytes)) < 0)
        return 0;
    for (unsigned i = 0; i < 32; ++i) {
        digest[i * 2] = "0123456789abcdef"[bytes[i] >> 4];
        digest[i * 2 + 1] = "0123456789abcdef"[bytes[i] & 15];
    }
    char *utf = malloc(CAP_CHARS * 4);
    if (!utf)
        return 0;
    if (!app_wide_to_utf8(r->text.text, utf, CAP_CHARS * 4)) {
        free(utf);
        return 0;
    }
    fprintf(trace, "DECODE frames=%zu sha256=%s last_two=%.9g text=%s\n", n, digest,
            r->last_two_start, utf);
    fflush(trace);
    free(utf);
    return ok;
}
int baffle_live_run(uint32_t backend, const char *key, uint32_t seconds, BaffleLiveSink sink,
                    BaffleLiveStop stop, void *user, char *err, size_t cap, int gain, int feedback)
{
    (void)backend;
    (void)key;
    (void)seconds;
    (void)err;
    (void)cap;
    (void)gain;
    (void)feedback;
    LARGE_INTEGER origin, frequency;
    QueryPerformanceCounter(&origin);
    QueryPerformanceFrequency(&frequency);
    replay_origin = origin;
    replay_frequency = frequency;
    size_t count = (audio_frames + 159) / 160 + 200;
    for (size_t block = 0; block < count && !stop(user); ++block) {
        float raw[160] = {0}, processed[160] = {0};
        float power = 0, peak = 0;
        for (size_t i = 0; i < 160; ++i) {
            size_t pos = block * 160 + i;
            if (pos >= 16000 && pos - 16000 < audio_frames) {
                raw[i] = raw_audio[pos - 16000];
                processed[i] = processed_audio[pos - 16000];
            }
            power += raw[i] * raw[i];
            peak = fmaxf(peak, fabsf(raw[i]));
        }
        BaffleLiveBlock b = {0};
        b.raw = raw;
        b.processed = processed;
        b.rms = (float)sqrt(power / 160);
        b.peak = peak;
        b.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
        b.timing.frame_position = block * 160;
        b.timing.epoch = 1;
        if (!sink(&b, user))
            break;
        NativeApp *app = user;
        uint32_t written = atomic_load_explicit(&app->ring.write, memory_order_acquire);
        uint32_t read = atomic_load_explicit(&app->ring.read, memory_order_acquire);
        uint32_t queued = written - read;
        if (queued > peak_queued)
            peak_queued = queued;
        for (;;) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            if ((double)(now.QuadPart - origin.QuadPart) / (double)frequency.QuadPart >=
                (double)(block + 1) * .01)
                break;
            Sleep(1);
        }
    }
    return 0;
}
int wmain(int argc, wchar_t **argv)
{
    if (argc != 5 && argc != 6)
        return 1;
    if (argc == 6 && wcscmp(argv[5], L"raw") && wcscmp(argv[5], L"ns"))
        return 1;
    int processing = argc == 6 && !wcscmp(argv[5], L"raw") ? 0 : 1;
    int result = 1, initialized = 0;
    char error[APP_ERROR] = {0};
    size_t n = 0;
    NativeApp *a = NULL;
    char *text = NULL;
    if (!app_read_wave(argv[1], &raw_audio, &audio_frames, error, sizeof(error)) ||
        !app_read_wave(argv[2], &processed_audio, &n, error, sizeof(error)) || n != audio_frames)
        goto done;
    trace = app_file(argv[4], L"wbx");
    if (!trace)
        goto done;
    fprintf(trace, "REPLAY queue_capacity=%u input_frames=%zu caption_profile=%s\n",
            (unsigned)(sizeof(((AppRing *)0)->frames) / sizeof(AppFrame)), audio_frames,
            processing ? "ns" : "raw");
    a = calloc(1, sizeof(*a));
    text = malloc(CAP_CHARS * 4);
    if (!a || !text || wcslen(argv[3]) >= APP_PATH)
        goto done;
    app_init(a);
    initialized = 1;
    wcscpy_s(a->models, APP_PATH, argv[3]);
    a->processing = processing;
    if (!app_start(a))
        goto done;
    if (WaitForSingleObject(a->worker, 90000) != WAIT_OBJECT_0) {
        /* An unproven worker/capture lifetime permits no reclamation. */
        ExitProcess(8);
    }
    DWORD worker_result = 1;
    if (!GetExitCodeThread(a->worker, &worker_result))
        goto done;
    if (!app_wide_to_utf8(a->history.text, text, CAP_CHARS * 4))
        goto done;
    fprintf(trace, "HISTORY=%s\nDROPPED=%llu\n", text,
            (unsigned long long)atomic_load(&a->dropped));
    if (!app_wide_to_utf8(a->status.text, text, CAP_CHARS * 4))
        goto done;
    fprintf(trace, "STATUS=%s\n", text);
    fprintf(trace, "PEAK_QUEUED=%u\n", peak_queued);
    for (size_t i = 0; i < measurements; ++i)
        fprintf(trace, "MEASURE index=%zu decode_ms=%.6f completed_after_source_start_ms=%.6f\n",
                i, decode_ms[i], completion_ms[i]);
    result = worker_result ? 7 : 0;
done:
    if (initialized && !app_destroy(a))
        ExitProcess(6);
    free(a);
    free(text);
    free(raw_audio);
    free(processed_audio);
    if (trace && fclose(trace))
        result = 1;
    return result;
}
