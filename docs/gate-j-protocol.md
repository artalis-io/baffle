# Gate J preregistered bounded experiment

Baseline `34a3345`, 2026-11-21. This protocol precedes candidate ASR evaluation.
Do not tune on Gate H/I cases or replace their original failed evidence.

Candidate A is the unchanged AEC→NS→AGC→VAD pipeline. Candidate B is an isolated
reference-aware postfilter between AEC and NS. It estimates the coherent part
of the linear residual using the estimated removed echo (`microphone − AEC`),
only when a render reference is available. Per-bin exponentially smoothed
cross/auto spectra give magnitude-squared coherence C; a conservative Wiener
amplitude mask is `sqrt(max(0, 1−C))`. Uncorrelated near speech should reduce C,
but this hypothesis must be tested, not interpreted as speaker attribution.
The echo estimate avoids assuming a zero-lag direct render-to-microphone path.

Use one fixed configuration: 10 ms rectangular blocks, next-power-of-two FFT,
100 ms exponential statistics, 20-block admission after reset, 5 ms gain attack
and 50 ms release. No new gain floor, signal gate, transcript filter or model
normalization is introduced. Per-block FFT/IFFT avoids an extra buffered hop but
may cause boundary/spectral artifacts, which are an explicit rejection risk.
This is a deliberately limited coherence experiment, not production RES.

Reset spectral state on AEC timing/generation reset or missing reference. Missing
reference and no estimated echo bypass the mask. Reference-only resets must not
reset near-leg NS/AGC/VAD. The public-API split control (mask disabled) must match
the original full pipeline's PCM before interpreting candidate comparisons.
No third candidate is initially justified: stronger NS already harms quiet
speech, and reversed ordering failed the matched Gate I comparison. Do not add
a parameter search after observing these outcomes.

## Fixed decision criteria

* Assistant-only: at least halve nonempty hypotheses for **each** recognizer
  on the fixed 11 cases (Whisper ≤5, Parakeet ≤4), without increasing total false
  recognized words. Commitment remains unavailable, never equated to recognition.
* Near-only/double-talk/cancellation: no additional blank or word error on any
  scored speech case relative to A; no first-word regression. Report CER and
  individual failures, not just pooled scores. Original conservative Gate H
  zero-added-error policy is retained. No calibrated phone-onset claim without
  independent review; missing evidence blocks production adoption.
* Known near component: report gain-sensitive projection/error and spectral
  diagnostics on identical windows; these cannot substitute for speech outcomes.
* Recorded holdout: evaluate the three existing real far-only recordings only
  after freezing B; neither recognizer's incidence/word count may worsen.
  They were previously inspected, so this is a **non-tuning comparison**, not
  new blind room evidence. No independent new room/phone holdout is available.
* Resources: preserve existing engineering budgets (mono P99 ≤2.5 ms, zero
  observed 10 ms misses, <512 KiB at 16 kHz / <1 MiB at 48 kHz), no added queued
  frames, and report relative cost plus worst execution under CPU ASR/TTS load.
* Safety: finite output, bounded preallocated state, reset/bypass invariants,
  installed-public-Hail conformance, numerical tests and available sanitizers.

Reject B upon a critical preservation or quality failure. Still retain the fixed
matrix and resource/transition results to explain the trade-off; do not attempt
to rescue it by tuning the test set. Keep unsuccessful code in external QA only.
Passing this experiment is distinct from production acoustic qualification.
