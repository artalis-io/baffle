#ifndef BAFFLE_DEMO_COMMON_H
#define BAFFLE_DEMO_COMMON_H
/* Bounded external tool utilities; none is linked into the DSP core. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif
static inline FILE *demo_fopen(const char *path, const char *mode)
{
    FILE *f = NULL;
#ifdef _MSC_VER
    if (fopen_s(&f, path, mode))
        return NULL;
#else
    f = fopen(path, mode);
#endif
    return f;
}
static inline int demo_number(const char *text, uint32_t maximum, uint32_t *out)
{
    char *end = NULL;
    unsigned long long value;
    if (!text || !out || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno || !end || *end || value > maximum)
        return 0;
    *out = (uint32_t)value;
    return 1;
}
static inline int demo_now(uint64_t *ns)
{
#ifdef _WIN32
    LARGE_INTEGER count, freq;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0 ||
        !QueryPerformanceCounter(&count) || count.QuadPart < 0)
        return 0;
    uint64_t c = (uint64_t)count.QuadPart, f = (uint64_t)freq.QuadPart, whole = c / f, rest = c % f;
    if (whole > UINT64_MAX / 1000000000u || rest > UINT64_MAX / 1000000000u)
        return 0;
    uint64_t tail = rest * 1000000000u / f;
    if (tail > UINT64_MAX - whole * 1000000000u)
        return 0;
    *ns = whole * 1000000000u + tail;
#else
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) || t.tv_sec < 0 ||
        (uint64_t)t.tv_sec > UINT64_MAX / 1000000000u)
        return 0;
    *ns = (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
#endif
    return 1;
}
static inline int demo_sleep_ms(uint32_t ms)
{
#ifdef _WIN32
    Sleep(ms);
    return 1;
#else
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    for (unsigned tries = 0; tries < 16; ++tries) {
        if (!nanosleep(&t, &t))
            return 1;
        if (errno != EINTR)
            return 0;
    }
    return 0;
#endif
}
typedef struct demo_waiter {
#ifdef _WIN32
    HANDLE timer;
#else
    int unused;
#endif
} demo_waiter;
static inline int demo_wait_init(demo_waiter *w, int highres)
{
    memset(w, 0, sizeof(*w));
#ifdef _WIN32
    if (highres) {
        w->timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002u, TIMER_ALL_ACCESS);
        if (!w->timer)
            return 0;
    }
#else
    if (highres)
        return 0;
#endif
    return 1;
}
static inline int demo_wait_until(demo_waiter *w, uint64_t target)
{
    uint64_t now;
    if (!demo_now(&now))
        return 0;
    if (now >= target)
        return 1;
#ifdef _WIN32
    if (w->timer) {
        uint64_t remaining = target - now;
        if (remaining > INT64_MAX - 99)
            return 0;
        LARGE_INTEGER due;
        due.QuadPart = -(int64_t)((remaining + 99) / 100);
        return SetWaitableTimer(w->timer, &due, 0, NULL, NULL, FALSE) &&
               WaitForSingleObject(w->timer, INFINITE) == WAIT_OBJECT_0;
    }
#else
    (void)w;
#endif
    while (now < target) {
        uint64_t remaining = target - now;
        uint32_t ms = (uint32_t)(remaining / 1000000u);
        if (ms > 1000)
            ms = 1000;
        if (!demo_sleep_ms(ms ? ms : 1) || !demo_now(&now))
            return 0;
    }
    return 1;
}
static inline int demo_wait_close(demo_waiter *w)
{
#ifdef _WIN32
    if (w->timer) {
        if (!CloseHandle(w->timer))
            return 0;
        w->timer = NULL;
    }
#else
    (void)w;
#endif
    return 1;
}
typedef struct demo_arena {
    void *allocation, *aligned;
    size_t bytes;
} demo_arena;
static inline int demo_arena_init(demo_arena *a, size_t bytes, size_t alignment)
{
    if (!alignment || (alignment & (alignment - 1)) || bytes > SIZE_MAX - (alignment - 1))
        return 0;
    a->allocation = malloc(bytes + alignment - 1);
    if (!a->allocation)
        return 0;
    a->aligned = (void *)(((uintptr_t)a->allocation + alignment - 1) & ~(uintptr_t)(alignment - 1));
    a->bytes = bytes;
    return 1;
}
static inline void demo_arena_free(demo_arena *a)
{
    free(a->allocation);
    memset(a, 0, sizeof(*a));
}
static inline int demo_load_f32(const char *path, size_t maximum, float **data, size_t *frames)
{
    FILE *f = demo_fopen(path, "rb");
    long length;
    int ok = 0;
    float *p = NULL;
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) <= 0 || (size_t)length > maximum ||
        length % 4 || fseek(f, 0, SEEK_SET))
        goto done;
    p = (float *)malloc((size_t)length);
    if (!p)
        goto done;
    if (fread(p, 1, (size_t)length, f) != (size_t)length)
        goto done;
    ok = 1;
done:
    if (fclose(f))
        ok = 0;
    if (ok) {
        *data = p;
        *frames = (size_t)length / sizeof(float);
    } else
        free(p);
    return ok;
}
static inline int demo_write_f32(const char *path, const float *data, size_t frames)
{
    if (frames > SIZE_MAX / sizeof(float) || (!data && frames))
        return 0;
    FILE *f = demo_fopen(path, "wb");
    if (!f)
        return 0;
    int ok = !frames || fwrite(data, sizeof(float), frames, f) == frames;
    if (fclose(f))
        ok = 0;
    return ok;
}
static inline int demo_json_string(FILE *f, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (fputc('"', f) == EOF)
        return 0;
    while (*p) {
        unsigned c = *p++;
        if (c == '"' || c == '\\') {
            if (fputc('\\', f) == EOF || fputc((int)c, f) == EOF)
                return 0;
        } else if (c < 32) {
            if (fprintf(f, "\\u%04x", c) < 0)
                return 0;
        } else if (fputc((int)c, f) == EOF)
            return 0;
    }
    return fputc('"', f) != EOF;
}
#endif
