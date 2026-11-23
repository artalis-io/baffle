#ifndef BAFFLE_NATIVE_APP_H
#define BAFFLE_NATIVE_APP_H
#include "speech.h"
#include "../../tools/common/threads.h"
#include "../../tools/acoustic-runner/live_source.h"
typedef struct AppFrame {
    float samples[160];
    uint64_t position, epoch, lost;
    uint32_t flags;
    float rms;
} AppFrame;
/* 5.12 seconds at 16 kHz/160 frames: bounded headroom for offline decoder stalls. */
#define APP_RING_CAPACITY 512u
_Static_assert((APP_RING_CAPACITY & (APP_RING_CAPACITY - 1u)) == 0,
               "Ring capacity must divide counter wrap");
typedef struct AppRing {
    AppFrame frames[APP_RING_CAPACITY];
    _Atomic uint32_t write, read;
} AppRing;
typedef struct NativeApp {
    wchar_t models[APP_PATH];
    char key[512];
    int processing, trial;
    _Atomic int stop, finished, receiving, fatal;
    _Atomic uint32_t meter, trial_frames;
    _Atomic uint64_t lost, dropped;
    AppRing ring;
    HANDLE worker;
    demo_thread capture;
    CRITICAL_SECTION display;
    CapText status, stable, draft, history;
    float *trial_audio;
    size_t trial_count;
    uint64_t trial_position, trial_epoch;
    int trial_gap;
} NativeApp;
void app_init(NativeApp *app);
int app_destroy(NativeApp *app);
int app_start(NativeApp *app);
int app_join_finished(NativeApp *app);
void app_request_stop(NativeApp *app);
void app_status(NativeApp *app, const wchar_t *text);
void app_snapshot(NativeApp *app, CapText *status, CapText *stable, CapText *draft,
                  CapText *history);
int app_ring_push(AppRing *ring, const AppFrame *frame);
int app_ring_pop(AppRing *ring, AppFrame *frame);
int app_queue_test(FILE *report);
#endif
