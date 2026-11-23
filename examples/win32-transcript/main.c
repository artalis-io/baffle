/* Native C11 Win32 consumer. Foreign inference is used only through its C API. */
#include "../../tools/common/demo.h"
#include "../../tools/acoustic-runner/hail_owner.h"
#include "app.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <sherpa-onnx/c-api/c-api.h>
#include <wchar.h>
typedef struct NativeWindow {
    NativeApp app;
    ha_device_info devices[HA_MAX_DEVICES];
    uint32_t device_count;
    HWND window, combo, button, mode, trial_button, status, meter, stable, draft, history, footer;
    HFONT normal, large, title;
    HBRUSH background, panel;
    int closing, was_finished, showing_trial;
    CapText current[4], shown[4];
} NativeWindow;
static int enumerate(NativeWindow *state, char *error, size_t capacity)
{
    ha_context_config config = HA_INIT(ha_context_config);
    config.backend = HA_BACKEND_WASAPI;
    ha_context *c = NULL;
    ha_stream *none = NULL;
    if (ha_context_create(&config, &c) != HA_OK) {
        snprintf(error, capacity, "WASAPI enumeration context");
        return 0;
    }
    ha_device_info devices[HA_MAX_DEVICES];
    for (unsigned i = 0; i < HA_MAX_DEVICES; ++i)
        devices[i] = (ha_device_info)HA_INIT(ha_device_info);
    uint32_t n = 0;
    ha_result r = ha_context_enumerate(c, devices, HA_MAX_DEVICES, &n), retired = HA_OK;
    int closed = demo_hail_retire(&c, &none, 5000, &retired);
    if (r != HA_OK || !closed) {
        snprintf(error, capacity, "WASAPI enumeration/retirement failed");
        return 0;
    }
    for (uint32_t i = 0; i < n; ++i)
        if (devices[i].direction == HA_CAPTURE)
            state->devices[state->device_count++] = devices[i];
    return 1;
}
static HWND control(NativeWindow *s, const wchar_t *type, const wchar_t *text, DWORD style, int id,
                    HFONT font)
{
    HWND h = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, s->window,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (h)
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    return h;
}
static int create_controls(NativeWindow *s)
{
    s->background = CreateSolidBrush(RGB(33, 34, 42));
    s->panel = CreateSolidBrush(RGB(10, 17, 15));
    s->normal = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Consolas");
    s->large = CreateFontW(-23, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Consolas");
    s->title = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Consolas");
    if (!s->normal || !s->large || !s->title || !s->background || !s->panel)
        return 0;
    if (!control(s, L"STATIC", L"BAFFLE", 0, 100, s->title) ||
        !control(s, L"STATIC", L"Live transcription", 0, 101, s->normal))
        return 0;
    s->combo =
        control(s, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 102, s->normal);
    size_t selected = 0;
    wchar_t name[256];
    if (!s->combo)
        return 0;
    for (uint32_t i = 0; i < s->device_count; ++i) {
        if (!app_utf8_to_wide(s->devices[i].name, name, 256))
            return 0;
        if (SendMessageW(s->combo, CB_ADDSTRING, 0, (LPARAM)name) == CB_ERRSPACE)
            return 0;
        if (wcsstr(name, L"Shure"))
            selected = i;
    }
    if (s->device_count)
        SendMessageW(s->combo, CB_SETCURSEL, selected, 0);
    s->button = control(s, L"BUTTON", L"Start", BS_OWNERDRAW | WS_TABSTOP, 103, s->normal);
    s->mode = control(s, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 104, s->normal);
    const wchar_t *modes[] = {L"Raw microphone", L"Noise reduction", L"Noise reduction + leveling"};
    if (!s->button || !s->mode)
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (SendMessageW(s->mode, CB_ADDSTRING, 0, (LPARAM)modes[i]) == CB_ERRSPACE)
            return 0;
    SendMessageW(s->mode, CB_SETCURSEL, 1, 0);
    s->trial_button = control(s, L"BUTTON", L"Record comparison (20 s)", BS_OWNERDRAW | WS_TABSTOP,
                              114, s->normal);
    s->status = control(s, L"STATIC", L"Ready", 0, 105, s->normal);
    s->meter = control(s, PROGRESS_CLASSW, L"", 0, 106, s->normal);
    if (!control(s, L"STATIC", L"", 0, 112, s->normal))
        return 0;
    s->stable = control(s, L"EDIT", L"",
                        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER, 113,
                        s->normal);
    if (!control(s, L"STATIC", L"LIVE TRANSCRIPT", 0, 107, s->normal))
        return 0;
    s->draft = control(s, L"EDIT", L"Press Start and speak naturally.",
                       ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER, 108,
                       s->large);
    if (!control(s, L"STATIC", L"TRANSCRIPT HISTORY", 0, 109, s->normal))
        return 0;
    s->history = control(s, L"EDIT", L"",
                         ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER, 110,
                         s->normal);
    s->footer =
        control(s, L"STATIC", L"Speak naturally. Pause to finish a sentence.", 0, 111, s->normal);
    if (!s->trial_button || !s->status || !s->meter || !s->stable || !s->draft || !s->history ||
        !s->footer)
        return 0;
    SetWindowTheme(s->meter, L"", L"");
    SendMessageW(s->meter, PBM_SETBKCOLOR, 0, RGB(10, 17, 15));
    SendMessageW(s->meter, PBM_SETBARCOLOR, 0, RGB(146, 230, 90));
    SendMessageW(s->meter, PBM_SETRANGE32, 0, 1000);
    ShowWindow(GetDlgItem(s->window, 112), SW_HIDE);
    ShowWindow(s->stable, SW_HIDE);
    SendMessageW(s->history, EM_SETLIMITTEXT, 128000, 0);
    SendMessageW(s->stable, EM_SETLIMITTEXT, 64000, 0);
    SendMessageW(s->draft, EM_SETLIMITTEXT, 64000, 0);
    return SetTimer(s->window, 1, 100, NULL) != 0;
}
static void place(NativeWindow *s, int id, int x, int y, int w, int h)
{
    MoveWindow(GetDlgItem(s->window, id), x, y, w, h, TRUE);
}
static void layout(NativeWindow *s)
{
    RECT r;
    GetClientRect(s->window, &r);
    int w = r.right, h = r.bottom, p = 18, live = (h - 350) / 2;
    if (live < 120)
        live = 120;
    place(s, 100, p, 12, w - 2 * p, 34);
    place(s, 101, p, 49, w - 2 * p, 23);
    place(s, 102, p, 88, w - 180 - 2 * p, 250);
    place(s, 103, w - p - 158, 86, 158, 32);
    place(s, 104, p, 130, w - 302 - 2 * p, 200);
    place(s, 114, w - p - 280, 128, 280, 32);
    place(s, 105, p, 174, w - 2 * p, 23);
    place(s, 106, p, 204, w - 2 * p, 9);
    place(s, 107, p, 231, w - 2 * p, 23);
    place(s, 108, p, 258, w - 2 * p, live);
    place(s, 113, p, 258, w - 2 * p, live);
    place(s, 109, p, 278 + live, w - 2 * p, 23);
    place(s, 110, p, 305 + live, w - 2 * p, h - live - 355);
    place(s, 111, p, h - 34, w - 2 * p, 22);
}
static void toggle(NativeWindow *s, int trial)
{
    if (!atomic_load_explicit(&s->app.finished, memory_order_acquire) || s->app.worker) {
        app_request_stop(&s->app);
        EnableWindow(s->button, FALSE);
        app_status(&s->app, L"Stopping microphone…");
        return;
    }
    LRESULT index = SendMessageW(s->combo, CB_GETCURSEL, 0, 0),
            mode = SendMessageW(s->mode, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR || index < 0 || (uint64_t)index >= s->device_count || mode < 0 ||
        mode > 2 || atomic_load(&s->app.fatal))
        return;
    size_t n = strlen(s->devices[(size_t)index].persistent_key);
    if (n >= sizeof(s->app.key))
        return;
    memcpy(s->app.key, s->devices[(size_t)index].persistent_key, n + 1);
    s->app.processing = (int)mode;
    s->app.trial = trial;
    if (!app_start(&s->app))
        return;
    EnableWindow(s->combo, FALSE);
    EnableWindow(s->mode, FALSE);
    EnableWindow(s->trial_button, FALSE);
    SetWindowTextW(s->button, L"Stop");
    s->was_finished = 0;
}
static void show_text(HWND h, CapText *cached, const CapText *text)
{
    if (wcscmp(cached->text, text->text)) {
        SetWindowTextW(h, text->text);
        (void)cap_set(cached, text->text);
        int id = GetDlgCtrlID(h);
        if (id == 108 || id == 110 || id == 113) {
            SendMessageW(h, EM_SETSEL, text->length, text->length);
            SendMessageW(h, EM_SCROLLCARET, 0, 0);
        }
    }
}
static void tick(NativeWindow *s)
{
    app_snapshot(&s->app, &s->current[0], &s->current[1], &s->current[2], &s->current[3]);
    int finished = atomic_load_explicit(&s->app.finished, memory_order_acquire),
        done = finished && app_join_finished(&s->app);
    int showing = s->app.trial && !done;
    if (showing != s->showing_trial) {
        s->showing_trial = showing;
        SetWindowTextW(GetDlgItem(s->window, 107),
                       showing ? L"READ-ALOUD SAMPLE" : L"LIVE TRANSCRIPT");
        SetWindowTextW(s->footer, showing ? L"This comparison records 20 seconds and saves audio."
                                          : L"Speak naturally. Pause to finish a sentence.");
        ShowWindow(s->stable, showing ? SW_SHOW : SW_HIDE);
        ShowWindow(s->draft, showing ? SW_HIDE : SW_SHOW);
    }
    if (atomic_load(&s->app.receiving) && !wcscmp(s->current[0].text, L"Opening microphone…"))
        (void)cap_set(&s->current[0], L"Listening");
    if (s->app.trial && atomic_load(&s->app.receiving)) {
        uint32_t seconds = atomic_load(&s->app.trial_frames) / 16000;
        if (seconds > 20)
            seconds = 20;
        swprintf(s->current[0].text, CAP_CHARS, L"RECORDING — read the sample aloud · %u s left",
                 20 - seconds);
        s->current[0].length = wcslen(s->current[0].text);
    }
    uint64_t lost = atomic_load(&s->app.lost), dropped = atomic_load(&s->app.dropped);
    if (lost || dropped) {
        size_t n = wcslen(s->current[0].text);
        if (n + 100 < CAP_CHARS) {
            swprintf(s->current[0].text + n, CAP_CHARS - n,
                     L" · dropped: %llu samples / %llu blocks", (unsigned long long)lost,
                     (unsigned long long)dropped);
            s->current[0].length = wcslen(s->current[0].text);
        }
    }
    show_text(s->status, &s->shown[0], &s->current[0]);
    show_text(s->stable, &s->shown[1], &s->current[1]);
    show_text(s->draft, &s->shown[2], &s->current[2]);
    if (wcscmp(s->shown[3].text, s->current[3].text)) {
        show_text(s->history, &s->shown[3], &s->current[3]);
        SendMessageW(s->history, EM_SETSEL, s->current[3].length, s->current[3].length);
        SendMessageW(s->history, EM_SCROLLCARET, 0, 0);
    }
    SendMessageW(s->meter, PBM_SETPOS, done ? 0 : atomic_load(&s->app.meter), 0);
    if (done && !s->was_finished) {
        if (wcsstr(s->current[0].text, L"stopping") || wcsstr(s->current[0].text, L"Stop") ||
            !wcsncmp(s->current[0].text, L"Listening", 9))
            app_status(&s->app, L"Stopped");
        int enabled = !atomic_load(&s->app.fatal);
        EnableWindow(s->combo, enabled);
        EnableWindow(s->mode, enabled);
        EnableWindow(s->trial_button, enabled);
        EnableWindow(s->button, enabled);
        SetWindowTextW(s->button, L"Start");
        s->was_finished = 1;
    }
    if (s->closing && done)
        DestroyWindow(s->window);
}
static LRESULT CALLBACK procedure(HWND h, UINT message, WPARAM w, LPARAM l)
{
    NativeWindow *s = (NativeWindow *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        s = (NativeWindow *)((CREATESTRUCTW *)l)->lpCreateParams;
        s->window = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
    }
    if (!s)
        return DefWindowProcW(h, message, w, l);
    switch (message) {
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, s->background ? s->background : (HBRUSH)GetStockObject(BLACK_BRUSH));
        DrawEdge((HDC)w, &r, EDGE_RAISED, BF_RECT);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w;
        int id = GetDlgCtrlID((HWND)l);
        int panel = id == 108 || id == 110 || id == 113 || id == 102 || id == 104 ||
                    message == WM_CTLCOLORLISTBOX;
        SetBkColor(dc, panel ? RGB(10, 17, 15) : RGB(33, 34, 42));
        SetTextColor(dc, panel || id == 100 ? RGB(166, 238, 116)
                         : id == 105        ? RGB(239, 193, 103)
                                            : RGB(191, 194, 205));
        return (LRESULT)(panel ? s->panel : s->background);
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *item = (DRAWITEMSTRUCT *)l;
        if (item->CtlType != ODT_BUTTON)
            return DefWindowProcW(h, message, w, l);
        RECT r = item->rcItem;
        FillRect(item->hDC, &r, s->background);
        DrawEdge(item->hDC, &r, item->itemState & ODS_SELECTED ? EDGE_SUNKEN : EDGE_RAISED,
                 BF_RECT);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC,
                     item->itemState & ODS_DISABLED ? RGB(108, 111, 122) : RGB(212, 222, 205));
        SelectObject(item->hDC, s->normal);
        wchar_t text[128];
        GetWindowTextW(item->hwndItem, text, 128);
        DrawTextW(item->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (item->itemState & ODS_FOCUS) {
            InflateRect(&r, -4, -4);
            DrawFocusRect(item->hDC, &r);
        }
        return TRUE;
    }
    case WM_CREATE:
        return create_controls(s) ? 0 : -1;
    case WM_SIZE:
        layout(s);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)l)->ptMinTrackSize = (POINT){800, 650};
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == 103 && HIWORD(w) == BN_CLICKED)
            toggle(s, 0);
        if (LOWORD(w) == 114 && HIWORD(w) == BN_CLICKED)
            toggle(s, 1);
        return 0;
    case WM_TIMER:
        tick(s);
        return 0;
    case WM_CLOSE:
        s->closing = 1;
        app_request_stop(&s->app);
        app_status(&s->app, L"Stopping…");
        EnableWindow(s->button, FALSE);
        tick(s);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(h, message, w, l);
    }
}
static int capture_test(FILE *report, char *error, size_t capacity)
{
    NativeWindow *s = calloc(1, sizeof(*s));
    if (!s)
        return 1;
    int ok = enumerate(s, error, capacity);
    char key[HA_DEVICE_KEY_CAPACITY] = {0};
    if (ok)
        for (uint32_t i = 0; i < s->device_count; ++i)
            if (strstr(s->devices[i].name, "Shure")) {
                memcpy(key, s->devices[i].persistent_key, sizeof(key));
                break;
            }
    free(s);
    if (!ok || !*key) {
        snprintf(error, capacity, "No Shure capture endpoint");
        return 1;
    }
    // Callbacks below are file-scope C functions (no C++ lambdas/closures).
    extern int app_capture_test_run(const char *, FILE *, char *, size_t);
    return app_capture_test_run(key, report, error, capacity);
}
typedef struct CaptureTestState {
    uint32_t blocks, nonfinite, silent_blocks;
    uint64_t lost, nonzero_samples;
    float max_input_rms, max_input_peak;
} CaptureTestState;
static int capture_test_sink(const BaffleLiveBlock *b, void *p)
{
    CaptureTestState *s = p;
    ++s->blocks;
    s->lost = b->capture_lost;
    int nonzero = 0;
    if (isfinite(b->rms))
        s->max_input_rms = fmaxf(s->max_input_rms, b->rms);
    for (unsigned i = 0; i < 160; ++i) {
        if (!isfinite(b->raw[i]) || !isfinite(b->processed[i]))
            ++s->nonfinite;
        if (isfinite(b->raw[i])) {
            s->max_input_peak = fmaxf(s->max_input_peak, fabsf(b->raw[i]));
            if (b->raw[i] != 0) {
                ++s->nonzero_samples;
                nonzero = 1;
            }
        }
    }
    if (!nonzero)
        ++s->silent_blocks;
    return 1;
}
static int never_stop(void *p)
{
    (void)p;
    return 0;
}
int app_capture_test_run(const char *key, FILE *report, char *error, size_t capacity)
{
    CaptureTestState s = {0};
    for (unsigned i = 0; i < 3; ++i)
        if (baffle_live_run(HA_BACKEND_WASAPI, key, 1, capture_test_sink, never_stop, &s, error,
                            capacity, 0, 0))
            return 1;
    if (report)
        fprintf(report,
                "cycles=3\nblocks=%u\nnonfinite=%u\nlost=%llu\n"
                "silent_blocks=%u\nnonzero_samples=%llu\nmax_input_rms=%.9g\nmax_input_peak=%.9g\n",
                s.blocks, s.nonfinite, (unsigned long long)s.lost, s.silent_blocks,
                (unsigned long long)s.nonzero_samples, (double)s.max_input_rms,
                (double)s.max_input_peak);
    return !s.blocks || s.nonfinite || s.lost ? 2 : 0;
}
static void report_failure(const wchar_t *report_path, const char *error)
{
    if (!*report_path)
        return;
    wchar_t *path = calloc(APP_PATH, sizeof(wchar_t)), *parent = calloc(APP_PATH, sizeof(wchar_t));
    if (!path || !parent)
        goto done;
    size_t n = wcslen(report_path);
    if (n >= APP_PATH)
        goto done;
    memcpy(path, report_path, (n + 1) * sizeof(wchar_t));
    if (app_file_exists(path)) {
        if (n + 40 >= APP_PATH)
            goto done;
        swprintf(path + n, APP_PATH - n, L".failure-%llu.txt",
                 (unsigned long long)GetTickCount64());
    }
    memcpy(parent, path, (wcslen(path) + 1) * sizeof(wchar_t));
    wchar_t *end = wcsrchr(parent, L'\\');
    if (end) {
        *end = 0;
        if (!app_make_directory(parent))
            goto done;
    }
    FILE *f = app_file(path, L"wbx");
    if (f) {
        fprintf(f, "error=%s\n", error);
        (void)fclose(f);
    }
done:
    free(path);
    free(parent);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    (void)previous;
    (void)command;
    int result = 1, argc = 0, headless = 0, mode = 0, feedback = 0;
    char error[APP_ERROR] = {0};
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    NativeWindow *state = NULL;
    FILE *report = NULL;
    Speech *speech = NULL;
    SpeechResult *decoded = NULL;
    float *audio = NULL;
    size_t frames = 0;
    wchar_t *models = calloc(APP_PATH, sizeof(wchar_t)), *wave = calloc(APP_PATH, sizeof(wchar_t)),
            *report_path = calloc(APP_PATH, sizeof(wchar_t)),
            *directory = calloc(APP_PATH, sizeof(wchar_t));
    CapText *comparison = NULL;
    if (!argv || !models || !wave || !report_path || !directory) {
        snprintf(error, sizeof(error), "Application memory");
        goto done;
    }
    if (!app_exe_dir(directory, APP_PATH) ||
        !app_join_path(models, APP_PATH, directory, L"models\\parakeet")) {
        snprintf(error, sizeof(error), "Executable/model path");
        goto done;
    }
    for (int i = 1; i < argc; ++i) {
        const wchar_t *arg = argv[i];
        if (!wcscmp(arg, L"--models")) {
            if (++i >= argc)
                goto arguments;
            size_t n = wcslen(argv[i]);
            if (n >= APP_PATH)
                goto arguments;
            memcpy(models, argv[i], (n + 1) * sizeof(wchar_t));
        } else if (!wcscmp(arg, L"--report")) {
            if (++i >= argc)
                goto arguments;
            DWORD n = GetFullPathNameW(argv[i], APP_PATH, report_path, NULL);
            if (!n || n >= APP_PATH)
                goto arguments;
        } else if (!wcscmp(arg, L"--queue-test")) {
            headless = 1;
            if (mode)
                goto arguments;
            mode = 1;
        } else if (!wcscmp(arg, L"--wave-test")) {
            headless = 1;
            if (mode)
                goto arguments;
            mode = 5;
        } else if (!wcscmp(arg, L"--capture-test")) {
            headless = 1;
            if (mode)
                goto arguments;
            mode = 2;
        } else if (!wcscmp(arg, L"--self-test") || !wcscmp(arg, L"--compare-wav")) {
            headless = 1;
            if (mode || ++i >= argc)
                goto arguments;
            mode = !wcscmp(arg, L"--self-test") ? 3 : 4;
            size_t n = wcslen(argv[i]);
            if (n >= APP_PATH)
                goto arguments;
            memcpy(wave, argv[i], (n + 1) * sizeof(wchar_t));
        } else if (!wcscmp(arg, L"--compare-vad-feedback"))
            feedback = 1;
        else
            goto arguments;
    }
    if ((feedback && mode != 4) || (!mode && *report_path))
        goto arguments;
    if (headless && *report_path && app_file_exists(report_path)) {
        snprintf(error, sizeof(error), "Report already exists; choose a fresh output path");
        goto done;
    }
    if (mode == 1 || mode == 2 || mode == 5) {
        if (*report_path) {
            report = app_file(report_path, L"wbx");
            if (!report) {
                snprintf(error, sizeof(error), "Report open failed");
                goto done;
            }
        }
        result = mode == 1   ? app_queue_test(report)
                 : mode == 5 ? app_wave_test()
                             : capture_test(report, error, sizeof(error));
        goto done;
    }
    if (mode == 3 || mode == 4) {
        if (!*report_path) {
            snprintf(error, sizeof(error), "Headless inference requires --report");
            goto done;
        }
        if (mode == 4) {
            memcpy(directory, report_path, (wcslen(report_path) + 1) * sizeof(wchar_t));
            wchar_t *end = wcsrchr(directory, L'\\');
            if (!end)
                goto arguments;
            *end = 0;
            if (!app_empty_directory(directory)) {
                snprintf(error, sizeof(error),
                         "Use a fresh comparison directory; previous results are retained");
                goto done;
            }
        }
        if (!app_read_wave(wave, &audio, &frames, error, sizeof(error)) ||
            !speech_open(models, &speech, error, sizeof(error)))
            goto done;
        if (mode == 3) {
            decoded = calloc(1, sizeof(*decoded));
            uint64_t begin, end;
            if (!decoded || !demo_now(&begin) ||
                !speech_decode(speech, audio, frames, decoded, error, sizeof(error)) ||
                !demo_now(&end) || end < begin)
                goto done;
            char *text = malloc(CAP_CHARS * 4);
            if (!text) {
                snprintf(error, sizeof(error), "ASR text memory");
                goto done;
            }
            int ok = app_wide_to_utf8(decoded->text.text, text, CAP_CHARS * 4);
            if (ok)
                report = app_file(report_path, L"wbx");
            if (report)
                fprintf(report,
                        "runtime=%s\nsource=%s\nprovider=cpu\nthreads=2\ntext=%s\nvalidated_last_"
                        "two_start_seconds=%.9g\ndecode_ms=%llu\n",
                        SherpaOnnxGetVersionStr(), SherpaOnnxGetGitSha1(), text,
                        decoded->last_two_start, (unsigned long long)((end - begin) / 1000000u));
            free(text);
            if (!report) {
                snprintf(error, sizeof(error), "ASR report output");
                goto done;
            }
            result = decoded->text.length ? 0 : 2;
        } else {
            comparison = calloc(1, sizeof(*comparison));
            _Atomic int stop = 0;
            if (!comparison || !speech_compare(speech, audio, frames, directory, &stop, feedback,
                                               comparison, error, sizeof(error)))
                goto done;
            char *text = malloc(CAP_CHARS * 4);
            if (!text)
                goto done;
            int ok = app_wide_to_utf8(comparison->text, text, CAP_CHARS * 4);
            if (ok)
                report = app_file(report_path, L"wbx");
            if (report)
                fprintf(report, "%s\n", text);
            free(text);
            if (!report) {
                snprintf(error, sizeof(error), "Comparison summary output");
                goto done;
            }
            result = 0;
        }
        goto done;
    }
    (void)SetProcessDPIAware();
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_PROGRESS_CLASS};
    if (!InitCommonControlsEx(&common)) {
        snprintf(error, sizeof(error), "Common controls");
        goto done;
    }
    state = calloc(1, sizeof(*state));
    if (!state) {
        snprintf(error, sizeof(error), "Window state memory");
        goto done;
    }
    app_init(&state->app);
    state->was_finished = 1;
    memcpy(state->app.models, models, (wcslen(models) + 1) * sizeof(wchar_t));
    if (!enumerate(state, error, sizeof(error)))
        goto done;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = procedure;
    wc.hInstance = instance;
    wc.lpszClassName = L"BaffleNativeTranscript";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    if (!RegisterClassW(&wc)) {
        snprintf(error, sizeof(error), "Window class registration");
        goto done;
    }
    HWND window =
        CreateWindowExW(0, wc.lpszClassName, L"Baffle", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                        CW_USEDEFAULT, 1040, 800, NULL, NULL, instance, state);
    if (!window) {
        snprintf(error, sizeof(error), "Window creation");
        goto done;
    }
    BOOL dark = TRUE;
    (void)DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG msg;
    int message;
    while ((message = (int)GetMessageW(&msg, NULL, 0, 0)) > 0)
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    result = message < 0 ? 1 : 0;
    goto done;
arguments:
    snprintf(error, sizeof(error), "Unknown, conflicting or incomplete command line");
done:
    if (argv)
        LocalFree(argv);
    if (report) {
        int bad = ferror(report);
        if (fclose(report))
            bad = 1;
        if (bad)
            result = 1;
        report = NULL;
    }
    speech_close(speech);
    free(audio);
    free(decoded);
    free(comparison);
    if (state) {
        app_request_stop(&state->app);
        if (state->app.worker)
            (void)WaitForSingleObject(state->app.worker, 5000);
        if (app_destroy(&state->app)) {
            if (state->background)
                DeleteObject(state->background);
            if (state->panel)
                DeleteObject(state->panel);
            if (state->normal)
                DeleteObject(state->normal);
            if (state->large)
                DeleteObject(state->large);
            if (state->title)
                DeleteObject(state->title);
            free(state);
        } else
            result = 1;
    }
    if (result && *error) {
        if (headless)
            report_failure(report_path ? report_path : L"", error);
        else {
            wchar_t text[APP_ERROR];
            if (!app_utf8_to_wide(error, text, APP_ERROR))
                wcscpy_s(text, APP_ERROR, L"Unknown error");
            MessageBoxW(NULL, text, L"Baffle — error", MB_OK | MB_ICONERROR);
        }
    }
    free(models);
    free(wave);
    free(report_path);
    free(directory);
    return result;
}
