/* Private hooks for the pinned floating-point MDF/smallft subset. No Hail/OS
 * dependency; all allocations draw from the caller's setup-only bounded arena. */
#include <stddef.h>
#define OVERRIDE_SPEEX_ALLOC
#define OVERRIDE_SPEEX_ALLOC_SCRATCH
#define OVERRIDE_SPEEX_FREE
#define OVERRIDE_SPEEX_FREE_SCRATCH
#define OVERRIDE_SPEEX_REALLOC
#define OVERRIDE_SPEEX_WARNING
#define OVERRIDE_SPEEX_WARNING_INT
#define OVERRIDE_SPEEX_NOTIFY
#define OVERRIDE_SPEEX_PUTC
void *baffle_speex_allocate(int bytes);
static inline void *speex_alloc(int n)
{
    return baffle_speex_allocate(n);
}
static inline void *speex_alloc_scratch(int n)
{
    return baffle_speex_allocate(n);
}
static inline void speex_free(void *p)
{
    (void)p;
}
static inline void speex_free_scratch(void *p)
{
    (void)p;
}
static inline void *speex_realloc(void *p, int n)
{
    (void)p;
    (void)n;
    return NULL;
}
static inline void speex_warning(const char *s)
{
    (void)s;
}
static inline void speex_warning_int(const char *s, int n)
{
    (void)s;
    (void)n;
}
static inline void speex_notify(const char *s)
{
    (void)s;
}
static inline void _speex_putc(int n, void *p)
{
    (void)n;
    (void)p;
}
