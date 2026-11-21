/* Optional glibc-only LD_PRELOAD observation, never a Baffle runtime component.
 * No dlsym/logging/allocation in the observation hooks. Counts all allocation
 * requests while a synchronous processing call is admitted. */
#define _GNU_SOURCE
#include <stdatomic.h>
#include <stddef.h>
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t,size_t);
extern void *__libc_realloc(void *,size_t);
extern void __libc_free(void *);
static _Atomic int active;
static _Atomic unsigned long long calls;
void bf_qa_alloc_begin(void) { atomic_store(&calls,0); atomic_store(&active,1); }
unsigned long long bf_qa_alloc_end(void) { atomic_store(&active,0); return atomic_load(&calls); }
void *malloc(size_t n) { if (atomic_load(&active)) atomic_fetch_add(&calls,1); return __libc_malloc(n); }
void *calloc(size_t n,size_t s) { if (atomic_load(&active)) atomic_fetch_add(&calls,1); return __libc_calloc(n,s); }
void *realloc(void *p,size_t n) { if (atomic_load(&active)) atomic_fetch_add(&calls,1); return __libc_realloc(p,n); }
void free(void *p) { __libc_free(p); }
