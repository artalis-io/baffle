#include "captions.h"
#include <stdio.h>
#include <stdlib.h>
static int check(int ok, const char *text)
{
    if (!ok)
        fprintf(stderr, "FAIL: %s\n", text);
    return ok;
}
#define CHECK(x, m)                                                                                \
    do {                                                                                           \
        if (!check((x), (m)))                                                                      \
            goto done;                                                                             \
    } while (0)
int main(void)
{
    int result = 1;
    CapAgreement *a = calloc(1, sizeof(*a));
    CapScratch *s = calloc(1, sizeof(*s));
    CapView *v = calloc(1, sizeof(*v));
    CapText *out = calloc(1, sizeof(*out)), *prefix = calloc(1, sizeof(*prefix));
    CHECK(a && s && v && out && prefix, "storage");
    CapSpan x = {0, 10};
    CHECK(cap_word_equal(L"ÁRVÍZTŰRŐ!", x, L"árvíztűrő", (CapSpan){0, 9}),
          "Hungarian accent-aware equality");
    CHECK(cap_agreement_update(a, L"Kérlek állítsd le a zenét", v, s) && !v->stable.length,
          "one hypothesis not stable");
    CHECK(cap_agreement_update(a, L"Kérlek állítsd le a zenét", v, s) && !v->stable.length,
          "two hypotheses not stable");
    CHECK(cap_agreement_update(a, L"Kérlek állítsd le a zenét", v, s) &&
              !wcscmp(v->stable.text, L"Kérlek állítsd le") && !wcscmp(v->draft.text, L"a zenét"),
          "three-hypothesis agreement holds last two");
    CHECK(cap_agreement_update(a, L"Kérlek indítsd el a zenét", v, s) &&
              !wcscmp(v->stable.text, L"Kérlek") && !wcscmp(v->draft.text, L"indítsd el a zenét"),
          "later correction stays visible");
    cap_agreement_reset(a);
    CHECK(cap_agreement_update(a, L"Kérlek állítsd le a zenét", v, s) && !v->stable.length,
          "no agreement across reset");
    CHECK(cap_hold_tail(L"Kérlek állítsd le a zenét mert", prefix, s) &&
              cap_stitch(prefix->text, L"le a zenét, mert kérdésem van", 2, out, s) &&
              !wcscmp(out->text, L"Kérlek állítsd le a zenét, mert kérdésem van"),
          "unfinished words cross overlap");
    CHECK(cap_stitch(L"Igen", L"igen, folytasd", 0, out, s) &&
              !wcscmp(out->text, L"Igen igen, folytasd"),
          "single repetition is retained");
    CHECK(cap_stitch(L"Igen kérem", L"igen kérem folytasd", 0, out, s) &&
              !wcscmp(out->text, L"Igen kérem igen kérem folytasd"),
          "two-word repetition without evidence retained");
    CHECK(cap_stitch(L"Igen kérem", L"igen kérem folytasd", 1, out, s) &&
              !wcscmp(out->text, L"Igen kérem igen kérem folytasd"),
          "insufficient overlap retained");
    CHECK(cap_stitch(L"Érdekel A KÖVETKEZŐ", L"a következő kérdés", 2, out, s) &&
              !wcscmp(out->text, L"Érdekel a következő kérdés"),
          "Hungarian overlap");
    CHECK(cap_stitch(L"Nem egyezik", L"ez az átfedés", 0, out, s) &&
              !wcscmp(out->text, L"Nem egyezik ez az átfedés"),
          "unmatched speech not deleted");
    CHECK(cap_hold_tail(L"két szó", out, s) && !out->length, "short speech pending");
    CHECK(cap_retain_from(128000, 7) == 96000 && cap_retain_from(128000, 5) <= 70400,
          "slow tail protected");
    CHECK(cap_retain_from(128000, -1) == 80000 && cap_retain_from(128000, 0) == 4000 &&
              cap_retain_from(8000, 0) == 0,
          "fallback/progress/short bounds");
    CHECK(cap_hold_tail(L"első második harmadik negyedik ötödik hatodik", prefix, s) &&
              cap_stitch(prefix->text, L"harmadik negyedik ötödik hatodik hetedik nyolcadik", 2,
                         out, s),
          "first roll");
    CHECK(cap_hold_tail(out->text, prefix, s) &&
              cap_stitch(prefix->text, L"ötödik hatodik hetedik nyolcadik kilencedik tizedik", 2,
                         out, s) &&
              !wcscmp(out->text, L"első második harmadik negyedik ötödik hatodik hetedik nyolcadik "
                                 L"kilencedik tizedik"),
          "multi-roll continuity");
    cap_clear(out);
    cap_clear(prefix);
    CHECK(cap_accept_hypothesis(L"", L"Please stop the", 0, prefix, out, s) &&
              !wcscmp(out->text, L"Please stop the"),
          "partial sentence appears immediately");
    CHECK(cap_accept_hypothesis(L"", L"", 0, prefix, out, s) &&
              !wcscmp(out->text, L"Please stop the"),
          "blank update preserves partial sentence");
    CHECK(cap_accept_hypothesis(L"", L" \t\r\n", 0, prefix, out, s) &&
              !wcscmp(out->text, L"Please stop the"),
          "whitespace update preserves partial");
    CHECK(cap_accept_hypothesis(L"", L"Please stop the music", 0, prefix, out, s) &&
              !wcscmp(out->text, L"Please stop the music"),
          "partial sentence grows");
    CHECK(cap_accept_hypothesis(L"", L"Please start the music", 0, prefix, out, s) &&
              !wcscmp(out->text, L"Please start the music"),
          "nonempty correction still replaces");
    CHECK(cap_set(prefix, L"Previous rolling window"), "carry setup");
    cap_clear(out);
    CHECK(cap_accept_hypothesis(L"Previous rolling", L"", 0, prefix, out, s) &&
              !wcscmp(out->text, L"Previous rolling window"),
          "blank after roll retains full carry");
    cap_clear(out);
    cap_clear(prefix);
    CHECK(cap_accept_hypothesis(L"", L"", 0, prefix, out, s) && !out->length,
          "reset does not resurrect previous speech");
    puts("C11 caption boundary/stability regressions passed");
    result = 0;
done:
    free(a);
    free(s);
    free(v);
    free(out);
    free(prefix);
    return result;
}
