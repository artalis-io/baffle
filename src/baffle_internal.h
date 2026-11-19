#ifndef BAFFLE_INTERNAL_H
#define BAFFLE_INTERNAL_H
#include <baffle.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include <limits.h>
#define BAFFLE_PI 3.14159265358979323846
static inline int baffle_header(const void *p, size_t size)
{
    uint32_t h[2];
    if (!p)
        return 0;
    memcpy(h, p, sizeof(h));
    return h[0] >= size && h[1] == BF_STRUCT_VERSION;
}
#define BAFFLE_HEADER(p) baffle_header((p), sizeof(*(p)))
static inline int baffle_storage(void *p, size_t bytes, size_t need)
{
    return p && (uintptr_t)p % BF_ALIGNMENT == 0 && bytes >= need;
}
static inline size_t baffle_align(size_t n)
{
    return (n + BF_ALIGNMENT - 1u) & ~(size_t)(BF_ALIGNMENT - 1u);
}
static inline int baffle_add(size_t *size, size_t n)
{
    if (n > SIZE_MAX - *size)
        return 0;
    *size += n;
    return 1;
}
static inline int baffle_mul(size_t a, size_t b, size_t *out)
{
    if (b && a > SIZE_MAX / b)
        return 0;
    *out = a * b;
    return 1;
}
static inline void baffle_record(void *out, const void *value, size_t n)
{
    memcpy((unsigned char *)out + 8, (const unsigned char *)value + 8, n - 8);
}
static inline float baffle_clean(float x)
{
    return !isfinite(x) || (x != 0.0f && fabsf(x) < FLT_MIN) ? 0.0f : x;
}
float baffle_sample(const bf_audio_buffer *b, uint32_t frame, uint32_t channel);
void baffle_store(bf_audio_buffer *b, uint32_t frame, uint32_t channel, float value);
bf_result baffle_finite(const bf_audio_buffer *b, uint32_t policy);
#endif
