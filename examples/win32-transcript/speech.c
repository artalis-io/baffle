/* External native application inference and file/control work. Never called
 * from Hail's audio callback or the Baffle processing/capture consumer path. */
#include "../../tools/common/demo.h"
#include "speech.h"
#include <baffle.h>
#include <sherpa-onnx/c-api/c-api.h>
#include <bcrypt.h>
#include <wchar.h>
#include <wctype.h>
#include <shlobj.h>
const wchar_t *const app_trial_prompt =
    L"Most magyarul beszélek, természetes hangerővel. Kérlek, állítsd le a zenét, mert szeretnék "
    L"feltenni egy másik kérdést. A következő találkozó csütörtökön délután három órakor lesz. "
    L"Árvíztűrő tükörfúrógép.";
struct Speech {
    const SherpaOnnxOfflineRecognizer *handle;
    char files[4][APP_PATH * 4];
    char assembled[CAP_CHARS * 4];
    CapText pieces;
    CapScratch scratch;
};
static int fail(char *error, size_t capacity, const char *message)
{
    if (error && capacity)
        snprintf(error, capacity, "%s", message);
    return 0;
}
int app_utf8_to_wide(const char *input, wchar_t *out, size_t capacity)
{
    if (!input || !out || capacity > INT_MAX || !capacity)
        return 0;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, out, (int)capacity);
    if (!n) {
        out[0] = 0;
        return 0;
    }
    return 1;
}
int app_wide_to_utf8(const wchar_t *input, char *out, size_t capacity)
{
    if (!input || !out || !capacity || capacity > INT_MAX)
        return 0;
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1, out, (int)capacity, NULL,
                                NULL);
    if (!n) {
        out[0] = 0;
        return 0;
    }
    return 1;
}
int app_exe_dir(wchar_t *path, size_t capacity)
{
    if (!path || !capacity || capacity > UINT32_MAX)
        return 0;
    DWORD n = GetModuleFileNameW(NULL, path, (DWORD)capacity);
    if (!n || n >= capacity)
        return 0;
    wchar_t *end = wcsrchr(path, L'\\');
    if (!end)
        return 0;
    *end = 0;
    return 1;
}
int app_join_path(wchar_t *out, size_t capacity, const wchar_t *dir, const wchar_t *name)
{
    if (!out || !dir || !name)
        return 0;
    size_t a = wcslen(dir), b = wcslen(name);
    if (a >= capacity || b >= capacity - a || a + b + 1 >= capacity)
        return 0;
    memmove(out, dir, a * sizeof(wchar_t));
    out[a] = L'\\';
    memcpy(out + a + 1, name, (b + 1) * sizeof(wchar_t));
    return 1;
}
FILE *app_file(const wchar_t *path, const wchar_t *mode)
{
    FILE *f = NULL;
    if (_wfopen_s(&f, path, mode))
        return NULL;
    return f;
}
int app_file_exists(const wchar_t *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}
int app_make_directory(const wchar_t *path)
{
    int result = SHCreateDirectoryExW(NULL, path, NULL);
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS && result != ERROR_FILE_EXISTS)
        return 0;
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
int app_empty_directory(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    if (a == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    wchar_t *pattern = malloc(APP_PATH * sizeof(wchar_t));
    if (!pattern)
        return 0;
    int ok = app_join_path(pattern, APP_PATH, path, L"*");
    WIN32_FIND_DATAW data;
    HANDLE h = ok ? FindFirstFileW(pattern, &data) : INVALID_HANDLE_VALUE;
    free(pattern);
    if (h == INVALID_HANDLE_VALUE)
        return ok && GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        if (wcscmp(data.cFileName, L".") && wcscmp(data.cFileName, L"..")) {
            ok = 0;
            break;
        }
    } while (FindNextFileW(h, &data));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES)
        ok = 0;
    if (!FindClose(h))
        ok = 0;
    return ok;
}
static uint16_t u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
int app_read_wave(const wchar_t *path, float **audio, size_t *frames, char *error, size_t capacity)
{
    FILE *f = app_file(path, L"rb");
    float *samples = NULL;
    unsigned char *data = NULL;
    int ok = 0;
    if (!audio || !frames) {
        if (f)
            fclose(f);
        return fail(error, capacity, "WAV outputs");
    }
    *audio = NULL;
    *frames = 0;
    if (!f)
        return fail(error, capacity, "Could not open WAV");
    if (fseek(f, 0, SEEK_END))
        goto done;
    long length = ftell(f);
    if (length < 44 || length > 10 * 1024 * 1024 || fseek(f, 0, SEEK_SET))
        goto done;
    data = malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, f) != (size_t)length)
        goto done;
    if (memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4))
        goto done;
    uint64_t limit = (uint64_t)u32(data + 4) + 8;
    if (limit > (uint64_t)length || limit < 12)
        goto done;
    uint32_t format = 0, channels = 0, rate = 0, bits = 0, align = 0;
    size_t offset = 12, start = 0, bytes = 0;
    int fmt_seen = 0, data_seen = 0;
    while (offset < limit) {
        if (limit - offset < 8)
            goto done;
        uint32_t chunk = u32(data + offset + 4);
        uint64_t next = (uint64_t)offset + 8 + chunk + (chunk & 1u);
        if (next > limit)
            goto done;
        if (!memcmp(data + offset, "fmt ", 4)) {
            if (fmt_seen || chunk < 16)
                goto done;
            fmt_seen = 1;
            const unsigned char *p = data + offset + 8;
            format = u16(p);
            channels = u16(p + 2);
            rate = u32(p + 4);
            align = u16(p + 12);
            bits = u16(p + 14);
            if (channels != 1 || rate != 16000 ||
                !((format == 1 && bits == 16) || (format == 3 && bits == 32)) ||
                align != bits / 8 || u32(p + 8) != rate * align)
                goto done;
        } else if (!memcmp(data + offset, "data", 4)) {
            if (data_seen)
                goto done;
            data_seen = 1;
            start = offset + 8;
            bytes = chunk;
        }
        offset = (size_t)next;
    }
    if (!fmt_seen || !data_seen || !bytes || !align || bytes % align ||
        bytes / align > SPEECH_SAMPLES)
        goto done;
    size_t n = bytes / align;
    samples = malloc(n * sizeof(float));
    if (!samples)
        goto done;
    for (size_t i = 0; i < n; ++i) {
        if (format == 1) {
            int16_t v;
            uint16_t word = u16(data + start + i * 2);
            memcpy(&v, &word, sizeof(v));
            samples[i] = (float)v / 32768.0f;
        } else
            memcpy(&samples[i], data + start + i * 4, 4);
        if (!isfinite(samples[i]) || fabsf(samples[i]) > 16)
            goto done;
    }
    *audio = samples;
    *frames = n;
    samples = NULL;
    ok = 1;
done:
    free(samples);
    free(data);
    if (fclose(f)) {
        if (ok) {
            free(*audio);
            *audio = NULL;
            *frames = 0;
        }
        ok = 0;
    }
    if (!ok)
        fail(error, capacity,
             "Invalid/truncated/unsupported WAV: require mono 16 kHz finite PCM, 0..20 seconds");
    return ok;
}
int app_write_wave(const wchar_t *path, const float *audio, size_t frames)
{
    if (!audio || !frames || frames > SPEECH_SAMPLES)
        return 0;
    FILE *f = app_file(path, L"wbx");
    if (!f)
        return 0;
    uint32_t bytes = (uint32_t)(frames * 4), riff = bytes + 36, fmt = 16, rate = 16000,
             byte_rate = 64000;
    uint16_t format = 3, channels = 1, align = 4, bits = 32;
    int ok = fwrite("RIFF", 4, 1, f) == 1 && fwrite(&riff, 4, 1, f) == 1 &&
             fwrite("WAVEfmt ", 8, 1, f) == 1 && fwrite(&fmt, 4, 1, f) == 1 &&
             fwrite(&format, 2, 1, f) == 1 && fwrite(&channels, 2, 1, f) == 1 &&
             fwrite(&rate, 4, 1, f) == 1 && fwrite(&byte_rate, 4, 1, f) == 1 &&
             fwrite(&align, 2, 1, f) == 1 && fwrite(&bits, 2, 1, f) == 1 &&
             fwrite("data", 4, 1, f) == 1 && fwrite(&bytes, 4, 1, f) == 1 &&
             fwrite(audio, 4, frames, f) == frames;
    if (fclose(f))
        ok = 0;
    return ok;
}
int speech_open(const wchar_t *models, Speech **out, char *error, size_t capacity)
{
    if (!out)
        return fail(error, capacity, "Recognizer output");
    *out = NULL;
    Speech *s = calloc(1, sizeof(*s));
    wchar_t *path = malloc(APP_PATH * sizeof(wchar_t));
    if (!s || !path) {
        free(s);
        free(path);
        return fail(error, capacity, "Recognizer working memory");
    }
    const wchar_t *names[] = {L"encoder.int8.onnx", L"decoder.int8.onnx", L"joiner.int8.onnx",
                              L"tokens.txt"};
    int ok = 0;
    for (size_t i = 0; i < 4; ++i)
        if (!app_join_path(path, APP_PATH, models, names[i]) || !app_file_exists(path) ||
            !app_wide_to_utf8(path, s->files[i], sizeof(s->files[i])))
            goto done;
    SherpaOnnxOfflineRecognizerConfig c = {0};
    c.feat_config.sample_rate = 16000;
    c.feat_config.feature_dim = 80;
    c.model_config.transducer.encoder = s->files[0];
    c.model_config.transducer.decoder = s->files[1];
    c.model_config.transducer.joiner = s->files[2];
    c.model_config.tokens = s->files[3];
    c.model_config.num_threads = 2;
    c.model_config.provider = "cpu";
    c.model_config.model_type = "nemo_transducer";
    c.decoding_method = "greedy_search";
    c.max_active_paths = 4;
    s->handle = SherpaOnnxCreateOfflineRecognizer(&c);
    if (!s->handle)
        goto done;
    *out = s;
    s = NULL;
    ok = 1;
done:
    free(path);
    speech_close(s);
    if (!ok)
        fail(error, capacity, "Missing model files or Parakeet initialization failed");
    return ok;
}
void speech_close(Speech *s)
{
    if (s) {
        if (s->handle)
            SherpaOnnxDestroyOfflineRecognizer(s->handle);
        free(s);
    }
}
static int hexbyte(const char *p)
{
    int a = -1, b = -1;
    if (p[0] >= '0' && p[0] <= '9')
        a = p[0] - '0';
    else if (p[0] >= 'A' && p[0] <= 'F')
        a = p[0] - 'A' + 10;
    else if (p[0] >= 'a' && p[0] <= 'f')
        a = p[0] - 'a' + 10;
    if (p[1] >= '0' && p[1] <= '9')
        b = p[1] - '0';
    else if (p[1] >= 'A' && p[1] <= 'F')
        b = p[1] - 'A' + 10;
    else if (p[1] >= 'a' && p[1] <= 'f')
        b = p[1] - 'a' + 10;
    return a < 0 || b < 0 ? -1 : a * 16 + b;
}
int speech_decode(Speech *s, const float *samples, size_t count, SpeechResult *out, char *error,
                  size_t capacity)
{
    if (!s || !s->handle || !samples || !out || !count || count > SPEECH_SAMPLES)
        return fail(error, capacity, "ASR geometry");
    for (size_t i = 0; i < count; ++i)
        if (!isfinite(samples[i]) || fabsf(samples[i]) > 16)
            return fail(error, capacity, "Invalid ASR PCM");
    const SherpaOnnxOfflineStream *stream = SherpaOnnxCreateOfflineStream(s->handle);
    if (!stream)
        return fail(error, capacity, "ASR stream allocation");
    SherpaOnnxAcceptWaveformOffline(stream, 16000, samples, (int32_t)count);
    SherpaOnnxDecodeOfflineStream(s->handle, stream);
    const SherpaOnnxOfflineRecognizerResult *r = SherpaOnnxGetOfflineStreamResult(stream);
    int ok = 0;
    out->last_two_start = -1;
    out->word_count = 0;
    if (!r || !r->text)
        goto done;
    size_t text_bytes = 0;
    while (text_bytes <= 32768 && r->text[text_bytes])
        ++text_bytes;
    if (text_bytes > 32768 || !app_utf8_to_wide(r->text, out->text.text, CAP_CHARS))
        goto done;
    out->text.length = wcslen(out->text.text);
    ok = 1;
    if (r->timestamps && r->tokens_arr && r->count > 0 && r->count <= 16384) {
        size_t length = 0, words = 0;
        int in_word = 0, valid = 1;
        double previous = -1;
        for (int32_t i = 0; i < r->count && valid; ++i) {
            double t = r->timestamps[i];
            const char *token = r->tokens_arr[i];
            if (!token || !isfinite(t) || t < 0 || t < previous || t > (double)count / 16000 + .2) {
                valid = 0;
                break;
            }
            previous = t;
            size_t n = 0;
            while (n <= 32768 && token[n])
                ++n;
            if (n > 32768) {
                valid = 0;
                break;
            }
            char fallback = 0;
            if (n == 6 && !memcmp(token, "<0x", 3) && token[5] == '>') {
                int b = hexbyte(token + 3);
                if (b < 0) {
                    valid = 0;
                    break;
                }
                fallback = (char)b;
                token = &fallback;
                n = 1;
            }
            for (size_t j = 0; j < n; ++j) {
                unsigned char c = (unsigned char)token[j];
                if (j + 2 < n && c == 0xe2u && (unsigned char)token[j + 1] == 0x96u &&
                    (unsigned char)token[j + 2] == 0x81u) {
                    c = ' ';
                    j += 2;
                }
                int whitespace = c == ' ' || c == '\n' || c == '\t';
                if (whitespace)
                    in_word = 0;
                else if (!in_word) {
                    if (words >= CAP_WORDS) {
                        valid = 0;
                        break;
                    }
                    out->word_starts[words++] = t;
                    in_word = 1;
                }
                if (length + 1 >= sizeof(s->assembled)) {
                    valid = 0;
                    break;
                }
                s->assembled[length++] = (char)c;
            }
        }
        s->assembled[length] = 0;
        if (valid && app_utf8_to_wide(s->assembled, s->pieces.text, CAP_CHARS) &&
            cap_tokenize(out->text.text, &s->scratch.a) &&
            cap_tokenize(s->pieces.text, &s->scratch.b)) {
            if (words == s->scratch.a.count && words == s->scratch.b.count && words >= 2) {
                for (size_t i = 0; i < words; ++i)
                    if (!cap_word_equal(out->text.text, s->scratch.a.spans[i], s->pieces.text,
                                        s->scratch.b.spans[i])) {
                        valid = 0;
                        break;
                    }
                if (valid) {
                    out->word_count = words;
                    out->last_two_start = out->word_starts[words - 2];
                }
            }
        }
    }
done:
    if (r)
        SherpaOnnxDestroyOfflineRecognizerResult(r);
    SherpaOnnxDestroyOfflineStream(stream);
    if (!ok)
        fail(error, capacity, "ASR result failed/invalid UTF-8/too long");
    return ok;
}
size_t speech_overlap_words(const SpeechResult *r, double prefix_until)
{
    size_t n = 0;
    /* Count starts before the carried-prefix boundary. Subtracting 120 ms
     * excluded a valid last overlapping word and made an exact multiword match
     * impossible, duplicating recognized phrases on rolling windows. Text still
     * requires at least two matching words; no starts at/after the boundary count.
     * Model times remain estimates, not device or phoneme timing guarantees. */
    while (n < r->word_count && r->word_starts[n] < prefix_until)
        ++n;
    return n;
}
int speech_condition(const float *raw, size_t frames, uint32_t stages, float **out, char *error,
                     size_t capacity)
{
    if (!raw || !out || !frames || frames > SPEECH_SAMPLES)
        return fail(error, capacity, "Comparison input bounds");
    *out = NULL;
    bf_pipeline_config c = BF_INIT(bf_pipeline_config);
    c.sample_rate = 16000;
    c.frame_frames = 160;
    c.channels = c.reference_channels = 1;
    c.stages = stages;
    c.noise_strength_db = 18;
    c.agc_target_dbfs = -18;
    size_t bytes = 0;
    demo_arena arena = {0};
    bf_pipeline *p = NULL;
    float *audio = calloc(frames + 160, sizeof(float));
    int ok = 0;
    if (!audio || bf_pipeline_size(&c, &bytes) != BF_OK ||
        !demo_arena_init(&arena, bytes, BF_ALIGNMENT) ||
        bf_pipeline_init(arena.aligned, bytes, &c, &p) != BF_OK)
        goto done;
    float input[160] = {0}, output[160] = {0};
    bf_audio_buffer in = BF_INIT(bf_audio_buffer), b = BF_INIT(bf_audio_buffer);
    in.format = (bf_pcm_format)BF_INIT(bf_pcm_format);
    in.format.sample_rate = 16000;
    in.format.channels = 1;
    in.format.sample_format = BF_PCM_F32;
    in.format.storage = BF_INTERLEAVED;
    in.format.channel_layout = BF_LAYOUT_MONO;
    in.timing = (bf_audio_timing)BF_INIT(bf_audio_timing);
    in.timing.stream_id = in.timing.clock_domain = in.timing.epoch = 1;
    in.timing.uncertainty_ns = UINT64_MAX;
    in.data = input;
    in.frames = in.capacity_frames = 160;
    b = in;
    b.data = output;
    size_t emitted = 0;
    for (size_t i = 0; i < frames; i += 160) {
        size_t n = frames - i;
        if (n > 160)
            n = 160;
        memset(input, 0, sizeof(input));
        memcpy(input, raw + i, n * sizeof(float));
        in.timing.frame_position = i;
        in.timing.flags = n < 160 ? BF_AUDIO_PADDING : 0;
        bf_process_info info = BF_INIT(bf_process_info);
        if (bf_pipeline_process(p, &in, NULL, &b, &info) != BF_OK || b.frames > 160 ||
            b.frames > frames + 160 - emitted)
            goto done;
        memcpy(audio + emitted, output, b.frames * sizeof(float));
        emitted += b.frames;
    }
    bf_process_info info = BF_INIT(bf_process_info);
    if (bf_pipeline_flush(p, &b, &info) != BF_OK || b.frames > 160 ||
        b.frames > frames + 160 - emitted)
        goto done;
    memcpy(audio + emitted, output, b.frames * sizeof(float));
    emitted += b.frames;
    if (emitted < frames)
        goto done;
    if (bf_pipeline_deinit(p) != BF_OK)
        goto done;
    p = NULL;
    *out = audio;
    audio = NULL;
    ok = 1;
done:
    if (p)
        (void)bf_pipeline_deinit(p);
    demo_arena_free(&arena);
    free(audio);
    if (!ok)
        fail(error, capacity, "Comparison processing/drain failed");
    return ok;
}
static int pcm_hash(const float *audio, size_t frames, char out[65])
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char *state = NULL;
    int ok = 0;
    DWORD bytes = 0, received = 0;
    unsigned char digest[32];
    if (frames > SPEECH_SAMPLES ||
        BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
        goto done;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&bytes, sizeof(bytes), &received, 0) <
            0 ||
        !bytes || bytes > 1048576)
        goto done;
    state = malloc(bytes);
    if (!state || BCryptCreateHash(alg, &hash, state, bytes, NULL, 0, 0) < 0 ||
        BCryptHashData(hash, (PUCHAR)(uintptr_t)audio, (ULONG)(frames * 4), 0) < 0 ||
        BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0)
        goto done;
    const char *hex = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = 0;
    ok = 1;
done:
    if (hash && BCryptDestroyHash(hash) < 0)
        ok = 0;
    if (alg && BCryptCloseAlgorithmProvider(alg, 0) < 0)
        ok = 0;
    free(state);
    return ok;
}
static int append(CapText *out, const wchar_t *text)
{
    size_t n = wcslen(text);
    if (n >= CAP_CHARS - out->length)
        return 0;
    memcpy(out->text + out->length, text, (n + 1) * sizeof(wchar_t));
    out->length += n;
    return 1;
}
int speech_compare(Speech *s, const float *raw, size_t frames, const wchar_t *directory,
                   const _Atomic int *stop, int feedback, CapText *display, char *error,
                   size_t capacity)
{
    int ok = 0;
    float *audio = NULL;
    FILE *report = NULL;
    wchar_t *path = malloc(APP_PATH * sizeof(wchar_t));
    SpeechResult *r = calloc(1, sizeof(*r));
    char *prompt = malloc(CAP_CHARS * 4);
    char hash[65];
    if (!raw || frames < 1600 || frames > SPEECH_SAMPLES || !path || !r || !prompt)
        goto done;
    for (size_t i = 0; i < frames; ++i)
        if (!isfinite(raw[i]))
            goto done;
    if (!app_make_directory(directory) || !app_join_path(path, APP_PATH, directory, L"raw.wav") ||
        !app_write_wave(path, raw, frames) ||
        !app_join_path(path, APP_PATH, directory, L"comparison.txt"))
        goto done;
    report = app_file(path, L"wbx");
    if (!report || !pcm_hash(raw, frames, hash) ||
        !app_wide_to_utf8(app_trial_prompt, prompt, CAP_CHARS * 4))
        goto done;
    fprintf(report,
            "scope=same captured PCM, offline paired ASR, not a speech-quality "
            "qualification\ndemo_revision=c11-ns-default\nsource_pcm_sha256=%s\nsample_rate="
            "16000\nframes=%zu\nsherpa=%s\nprovider=cpu\nthreads=2\ndecoding=greedy_search\nns_db="
            "18\nagc_dbfs=-18\nns_delay_samples=160 (drained/aligned before "
            "scoring)\nstage_masks=raw:0,ns:%u,ns-agc:%u\nVAD=%s; no PCM "
            "gating\nsuggested_hungarian_prompt_not_ground_truth=%s\nWER_CER=unavailable until "
            "delivered transcript independently checked\n",
            hash, frames, SherpaOnnxGetVersionStr(), BF_STAGE_NOISE | (feedback ? BF_STAGE_VAD : 0),
            BF_STAGE_NOISE | BF_STAGE_AGC | (feedback ? BF_STAGE_VAD : 0),
            feedback ? "diagnostic live-feedback control"
                     : "disabled, matches native caption profile",
            prompt);
    const wchar_t *labels[] = {L"RAW MICROPHONE", L"NOISE REDUCTION", L"NOISE REDUCTION + LEVELING"};
    const wchar_t *files[] = {L"raw.wav", L"ns.wav", L"ns-agc.wav"};
    const char *names[] = {"raw", "ns", "ns-agc"};
    cap_clear(display);
    for (unsigned mode = 0; mode < 3; ++mode) {
        if (atomic_load_explicit(stop, memory_order_relaxed)) {
            fail(error, capacity, "Comparison canceled; partial evidence retained");
            goto done;
        }
        const float *samples = raw;
        if (mode) {
            if (!speech_condition(raw, frames,
                                  BF_STAGE_NOISE | (feedback ? BF_STAGE_VAD : 0) |
                                      (mode == 2 ? BF_STAGE_AGC : 0),
                                  &audio, error, capacity) ||
                !app_join_path(path, APP_PATH, directory, files[mode]) ||
                !app_write_wave(path, audio, frames))
                goto done;
            samples = audio;
        }
        double power = 0, peak = 0;
        size_t clipped = 0;
        for (size_t i = 0; i < frames; ++i) {
            float x = samples[i];
            if (!isfinite(x))
                goto done;
            power += (double)x * x;
            peak = fmax(peak, fabs(x));
            if (fabsf(x) >= 1)
                ++clipped;
        }
        uint64_t begin, end;
        if (!demo_now(&begin) || !speech_decode(s, samples, frames, r, error, capacity) ||
            !demo_now(&end) || end < begin || !pcm_hash(samples, frames, hash) ||
            !app_wide_to_utf8(r->text.text, prompt, CAP_CHARS * 4))
            goto done;
        fprintf(report,
                "\n[%s]\npcm_sha256=%s\nrms=%.9g\npeak=%.9g\nclipped_samples=%zu\nframes=%"
                "zu\ndecode_ms=%llu\ntext=%s\n",
                names[mode], hash, sqrt(power / (double)frames), peak, clipped, frames,
                (unsigned long long)((end - begin) / 1000000u), prompt);
        if (fflush(report) || ferror(report))
            goto done;
        if (display->length && !append(display, L"\r\n\r\n"))
            goto done;
        if (!append(display, labels[mode]) || !append(display, L":\r\n") ||
            !append(display, r->text.text))
            goto done;
        free(audio);
        audio = NULL;
    }
    ok = 1;
done:
    if (report && fclose(report))
        ok = 0;
    free(audio);
    free(path);
    free(r);
    free(prompt);
    if (!ok && error && capacity && !*error)
        fail(error, capacity, "Comparison storage, geometry or file operation failed");
    return ok;
}

/* Native, model/device-free parser regression. Only this unique temp file is removed. */
int app_wave_test(void)
{
    wchar_t parent[MAX_PATH], path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, parent) || !GetTempFileNameW(parent, L"bfw", 0, path))
        return 1;
    int result = 1;
    unsigned char good[48] = {'R',  'I',  'F', 'F', 40, 0,    0, 0, 'W', 'A', 'V', 'E',
                              'f',  'm',  't', ' ', 16, 0,    0, 0, 3,   0,   1,   0,
                              0x80, 0x3e, 0,   0,   0,  0xfa, 0, 0, 4,   0,   32,  0,
                              'd',  'a',  't', 'a', 4,  0,    0, 0, 0,   0,   0,   0x3e};
    for (unsigned scenario = 0; scenario < 56; ++scenario) {
        unsigned char data[48];
        memcpy(data, good, sizeof(data));
        size_t bytes = sizeof(data);
        int expected = scenario == 0 || scenario == 55;
        if (scenario >= 1 && scenario <= 48)
            bytes = scenario - 1;
        else if (scenario == 49)
            memset(data + 4, 255, 4); /* RIFF overflow/truncation */
        else if (scenario == 50)
            data[22] = 2; /* stereo rejected */
        else if (scenario == 51)
            data[24] = 0; /* unsupported rate */
        else if (scenario == 52) {
            data[46] = 0xc0;
            data[47] = 0x7f;
        } /* NaN */
        else if (scenario == 53) {
            data[46] = 0x80;
            data[47] = 0x7f;
        } /* Inf */
        else if (scenario == 54)
            data[40] = 3; /* partial scalar */
        else if (scenario == 55) {
            data[20] = 1;
            data[28] = 0;
            data[29] = 0x7d;
            data[32] = 2;
            data[34] = 16;
        } /* valid PCM16: two samples */
        FILE *f = app_file(path, L"wb");
        if (!f)
            goto done;
        int written = fwrite(data, 1, bytes, f) == bytes;
        if (fclose(f) || !written)
            goto done;
        float *audio = NULL;
        size_t frames = 0;
        char error[APP_ERROR] = {0};
        int ok = app_read_wave(path, &audio, &frames, error, sizeof(error));
        int pass = ok == expected &&
                   (ok ? audio && frames == (scenario == 55 ? 2u : 1u) : !audio && !frames);
        free(audio);
        if (!pass)
            goto done;
    }
    result = 0;
done:
    if (!DeleteFileW(path))
        result = 1;
    return result;
}
