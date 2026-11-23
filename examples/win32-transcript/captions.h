#ifndef BAFFLE_CAPTIONS_H
#define BAFFLE_CAPTIONS_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
/* Private application text state. All storage is bounded and caller-owned. */
#define CAP_CHARS 65536u
#define CAP_WORDS 32768u
typedef struct CapText {
    wchar_t text[CAP_CHARS];
    size_t length;
} CapText;
typedef struct CapSpan {
    uint32_t first, last;
} CapSpan;
typedef struct CapTokens {
    CapSpan spans[CAP_WORDS];
    size_t count;
} CapTokens;
typedef struct CapScratch {
    CapTokens a, b;
} CapScratch;
typedef struct CapView {
    CapText stable, draft;
} CapView;
typedef struct CapAgreement {
    CapText history[2];
    unsigned count;
} CapAgreement;
int cap_set(CapText *out, const wchar_t *text);
void cap_clear(CapText *out);
int cap_tokenize(const wchar_t *text, CapTokens *out);
int cap_word_equal(const wchar_t *a, CapSpan x, const wchar_t *b, CapSpan y);
int cap_stitch(const wchar_t *prefix, const wchar_t *next, size_t overlap_words, CapText *out,
               CapScratch *scratch);
/* Empty decoder updates do not revoke an already displayed partial. Reset the
 * caller-owned text at utterance boundaries; nonempty corrections replace it. */
int cap_accept_hypothesis(const wchar_t *prefix, const wchar_t *next, size_t overlap_words,
                          const CapText *carry, CapText *text, CapScratch *scratch);
int cap_hold_tail(const wchar_t *text, CapText *out, CapScratch *scratch);
void cap_agreement_reset(CapAgreement *state);
int cap_agreement_update(CapAgreement *state, const wchar_t *text, CapView *out,
                         CapScratch *scratch);
size_t cap_retain_from(size_t frames, double last_two_start_seconds);
#endif
