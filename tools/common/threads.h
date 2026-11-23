#ifndef BAFFLE_DEMO_THREADS_H
#define BAFFLE_DEMO_THREADS_H
/* Application/test portability only. No threads or OS dependencies in core. */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef struct demo_thread {
    HANDLE native;
    int (*fn)(void *);
    void *arg;
    int result;
} demo_thread;
static DWORD WINAPI demo_thread_entry(LPVOID p)
{
    demo_thread *t = (demo_thread *)p;
    t->result = t->fn(t->arg);
    return 0;
}
static inline int demo_thread_start(demo_thread *t, int (*fn)(void *), void *arg)
{
    t->fn = fn;
    t->arg = arg;
    t->result = 0;
    t->native = CreateThread(NULL, 0, demo_thread_entry, t, 0, NULL);
    return t->native ? 0 : -1;
}
static inline int demo_thread_join(demo_thread *t, int *result)
{
    if (!t->native || WaitForSingleObject(t->native, INFINITE) != WAIT_OBJECT_0)
        return -1;
    if (!CloseHandle(t->native))
        return -1;
    t->native = NULL;
    if (result)
        *result = t->result;
    return 0;
}
static inline void demo_thread_yield(void)
{
    (void)SwitchToThread();
}
#else
#include <pthread.h>
#include <sched.h>
typedef struct demo_thread {
    pthread_t native;
    int (*fn)(void *);
    void *arg;
    int result;
} demo_thread;
static void *demo_thread_entry(void *p)
{
    demo_thread *t = (demo_thread *)p;
    t->result = t->fn(t->arg);
    return NULL;
}
static inline int demo_thread_start(demo_thread *t, int (*fn)(void *), void *arg)
{
    t->fn = fn;
    t->arg = arg;
    t->result = 0;
    return pthread_create(&t->native, NULL, demo_thread_entry, t);
}
static inline int demo_thread_join(demo_thread *t, int *result)
{
    int r = pthread_join(t->native, NULL);
    if (!r && result)
        *result = t->result;
    return r;
}
static inline void demo_thread_yield(void)
{
    (void)sched_yield();
}
#endif
#endif
