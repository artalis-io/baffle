#include "captions.h"
#include <string.h>
#include <wctype.h>
#include <math.h>
static int space(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || iswspace(c);
}
static int punctuation(wchar_t c)
{
    return c && wcschr(L".,!?;:\"'„”()[]{}…", c) != NULL;
}
static wchar_t lower(wchar_t c)
{
    const wchar_t *upper = L"ÁÉÍÓÖŐÚÜŰ", *map = L"áéíóöőúüű", *p = wcschr(upper, c);
    return p ? map[p - upper] : (wchar_t)towlower(c);
}
void cap_clear(CapText *out)
{
    out->length = 0;
    out->text[0] = 0;
}
int cap_set(CapText *out, const wchar_t *text)
{
    if (!out || !text)
        return 0;
    size_t n = 0;
    while (n < CAP_CHARS && text[n])
        ++n;
    if (n >= CAP_CHARS)
        return 0;
    memmove(out->text, text, (n + 1) * sizeof(wchar_t));
    out->length = n;
    return 1;
}
int cap_tokenize(const wchar_t *text, CapTokens *out)
{
    if (!text || !out)
        return 0;
    size_t pos = 0;
    out->count = 0;
    while (pos < CAP_CHARS && text[pos]) {
        while (pos < CAP_CHARS && text[pos] && space(text[pos]))
            ++pos;
        if (pos == CAP_CHARS)
            return 0;
        if (!text[pos])
            break;
        size_t first = pos;
        while (pos < CAP_CHARS && text[pos] && !space(text[pos]))
            ++pos;
        if (out->count >= CAP_WORDS || pos >= CAP_CHARS)
            return 0;
        out->spans[out->count++] = (CapSpan){(uint32_t)first, (uint32_t)pos};
    }
    return pos < CAP_CHARS;
}
int cap_word_equal(const wchar_t *a, CapSpan x, const wchar_t *b, CapSpan y)
{
    while (x.first < x.last && punctuation(a[x.first]))
        ++x.first;
    while (x.last > x.first && punctuation(a[x.last - 1]))
        --x.last;
    while (y.first < y.last && punctuation(b[y.first]))
        ++y.first;
    while (y.last > y.first && punctuation(b[y.last - 1]))
        --y.last;
    if (x.last - x.first != y.last - y.first)
        return 0;
    while (x.first < x.last)
        if (lower(a[x.first++]) != lower(b[y.first++]))
            return 0;
    return 1;
}
static size_t span_chars(const CapTokens *t, size_t first, size_t last)
{
    size_t n = 0;
    for (size_t i = first; i < last; ++i)
        n += (size_t)(t->spans[i].last - t->spans[i].first) + (i > first ? 1u : 0u);
    return n;
}
static int append_words(CapText *out, const wchar_t *text, const CapTokens *t, size_t first,
                        size_t last)
{
    for (size_t i = first; i < last; ++i) {
        CapSpan s = t->spans[i];
        size_t n = (size_t)(s.last - s.first), extra = out->length ? 1u : 0u;
        if (n + extra >= CAP_CHARS - out->length)
            return 0;
        if (extra)
            out->text[out->length++] = L' ';
        memcpy(out->text + out->length, text + s.first, n * sizeof(wchar_t));
        out->length += n;
        out->text[out->length] = 0;
    }
    return 1;
}
int cap_stitch(const wchar_t *prefix, const wchar_t *next, size_t overlap_words, CapText *out,
               CapScratch *scratch)
{
    if (!prefix || !next || !out || !scratch)
        return 0;
    if (!*prefix)
        return cap_set(out, next);
    CapTokens *a = &scratch->a, *b = &scratch->b;
    if (!cap_tokenize(prefix, a) || !cap_tokenize(next, b))
        return 0;
    size_t limit = a->count;
    if (limit > b->count)
        limit = b->count;
    if (limit > overlap_words)
        limit = overlap_words;
    if (limit > 48)
        limit = 48;
    size_t match = 0;
    for (size_t n = 2; n <= limit; ++n) {
        int same = 1;
        for (size_t i = 0; i < n; ++i)
            if (!cap_word_equal(prefix, a->spans[a->count - n + i], next, b->spans[i])) {
                same = 0;
                break;
            }
        if (same)
            match = n;
    }
    size_t last = a->count - match, first = 0, total = span_chars(a, 0, last),
           next_chars = span_chars(b, 0, b->count);
    if (total && next_chars)
        ++total;
    total += next_chars;
    while (total > 64000 && first < last) {
        size_t removed = a->spans[first].last - a->spans[first].first;
        ++first;
        if (first < last || next_chars)
            ++removed;
        total -= removed;
    }
    cap_clear(out);
    return append_words(out, prefix, a, first, last) && append_words(out, next, b, 0, b->count);
}
int cap_accept_hypothesis(const wchar_t *prefix, const wchar_t *next, size_t overlap_words,
                          const CapText *carry, CapText *text, CapScratch *scratch)
{
    if (!prefix || !next || !carry || !text || !scratch)
        return 0;
    size_t first = 0;
    while (first < CAP_CHARS && next[first] && space(next[first]))
        ++first;
    if (first == CAP_CHARS)
        return 0;
    if (!next[first])
        return text->length || !carry->length ? 1 : cap_set(text, carry->text);
    return cap_stitch(prefix, next, overlap_words, text, scratch);
}
int cap_hold_tail(const wchar_t *text, CapText *out, CapScratch *scratch)
{
    if (!cap_tokenize(text, &scratch->a))
        return 0;
    cap_clear(out);
    size_t n = scratch->a.count;
    return append_words(out, text, &scratch->a, 0, n > 2 ? n - 2 : 0);
}
void cap_agreement_reset(CapAgreement *state)
{
    cap_clear(&state->history[0]);
    cap_clear(&state->history[1]);
    state->count = 0;
}
static size_t common(const wchar_t *text, const CapTokens *current, const wchar_t *previous,
                     CapTokens *tokens)
{
    if (!cap_tokenize(previous, tokens))
        return 0;
    size_t i = 0;
    while (i < current->count && i < tokens->count &&
           cap_word_equal(text, current->spans[i], previous, tokens->spans[i]))
        ++i;
    return i;
}
int cap_agreement_update(CapAgreement *state, const wchar_t *text, CapView *out,
                         CapScratch *scratch)
{
    CapTokens *a = &scratch->a, *b = &scratch->b;
    if (!cap_tokenize(text, a))
        return 0;
    size_t agreed = 0;
    if (state->count == 2) {
        agreed = common(text, a, state->history[0].text, b);
        size_t other = common(text, a, state->history[1].text, b);
        if (agreed > other)
            agreed = other;
    }
    size_t available = a->count > 2 ? a->count - 2 : 0;
    if (agreed > available)
        agreed = available;
    if (!cap_set(&state->history[0], state->history[1].text) || !cap_set(&state->history[1], text))
        return 0;
    if (state->count < 2)
        ++state->count;
    cap_clear(&out->stable);
    cap_clear(&out->draft);
    return append_words(&out->stable, text, a, 0, agreed) &&
           append_words(&out->draft, text, a, agreed, a->count);
}
size_t cap_retain_from(size_t frames, double last_two_start_seconds)
{
    if (frames <= 16000)
        return 0;
    double seconds = (double)frames / 16000, cut = seconds - 2;
    if (isfinite(last_two_start_seconds) && last_two_start_seconds >= 0 &&
        last_two_start_seconds <= seconds)
        cut = fmin(cut, last_two_start_seconds - .6);
    else
        cut = seconds - 3;
    cut = fmax(.25, fmin(cut, seconds - .5));
    return (size_t)(cut * 16000) / 160 * 160;
}
