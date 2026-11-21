# Gate J evidence ledger

**Decision: retain A (baseline); reject B. Gate J: PARTIAL.**
See [the report](../../gate-j.md) and [fixed protocol](../../gate-j-protocol.md).
Nothing in this experiment changes the production DSP, public API, adapter or
original AEC acceptance threshold. No physical or phone-onset PASS is claimed.

## Revisions, methods and results

* Baseline `34a3345`; `baseline-ci.json` records its 27 successful jobs.
  `baseline-controlled.json` reproduces the unchanged 14.603375 dB / 8 dB test
  and separate far-only ERLE. `baseline-reproduction.json` compares fresh PCM
  and hypotheses against the retained Gate I assistant-only result.
* `0cb12c6` freezes the candidate and initial protocol. `5c4ac73` corrects workflow
  indentation and adds per-case analysis without changing its algorithm.
  `adaa5ab` uses typed float constants for strict MSVC; `literal-identity.json`
  proves all 39 scored candidate PCM outputs remain byte-identical. `b1f63d5`
  documents rejection and adds signal diagnostics. Measurement source/runner/SDK
  hashes, compiler flags and fixed decoder settings are in `environment.json`;
  exact measured binary hashes remain in each raw result/report.
* `baseline-far.json`, `candidate-far.json`, `candidate-speech.json` and
  `recorded-asr.json` contain **246 new fixed-model evaluations** total. Existing
  Gate I baseline speech/room evaluations remain in their original ledger and
  are reused only with matching PCM identities; they are not claimed as fresh
  decoding. Recognizers and provider transcripts are pinned and unchanged.
* `split-speech.json` / `split-far.json`: all 28 + 11 public-API mask-disabled
  controls match the baseline full PCM, including injected transport transitions.
* `far-comparison.json`: B fails incidence targets (Whisper 11/11, Parakeet 10/11)
  and raises Whisper false words from 426 to 479. The lower Parakeet word total
  (392→386) is not a rejection-incidence PASS. Commitment counts remain null.
* `speech-comparison.json`: pooled near-only errors are unchanged; pooled Parakeet
  double-talk improves, but individual restart/reference-loss/epoch regressions,
  including a new Parakeet blank and first-word loss, reject B. WER/CER edits and
  hypotheses remain per-case; critical failures are not averaged away.
* `failure-characterization.json`: raw/reference/AEC/full/candidate power, peaks,
  band spectra and known-far-only ratios; Welch coherence of removed echo and
  residual; VAD, AEC resets and flags. Input domains 1/2 are distinct and physical
  timestamps/mapping unavailable. Spectral coherence is not a calibrated
  probability of user speech or AEC reliability. No driver or oscillator defect
  is uniquely inferred from these signals.
* `speech-signals.json`, `candidate-preservation.json`, `candidate-recovery.json`:
  numerical oracle projection/error, declared spectral diagnostics, activity and
  digital recovery/tails. These are not independently reviewed first-phoneme,
  audible-tail or real device recovery measurements. Preserve the interpretation
  limits in each method field.
* `recorded-replay.json`: existing real recordings plus separately labeled
  simulated controls/alignment fits. `recorded-asr.json` scores only original
  real far-only intervals, 6..22 s, at 16/48 kHz. Full Whisper words rise 228→235;
  Parakeet 179→180. No new calibrated physical acquisition or blind room holdout
  exists. Previously inspected recordings are non-tuning comparisons, not new
  independent generalization evidence.

## Resources and safety

`performance-summary.json` and `performance-raw.tgz` retain all eight 30-second
alone/combined CPU inference trials: **24,000 measured 10 ms calls**, no observed
execution/completion misses. Baseline and candidate use identical input/rate and
Whisper+Parakeet+Piper configuration. Actual execution, release lateness, sampled
CPU/RSS/GPU and modeled queue occupancy are distinct. CPU follows psutil's
100%-per-logical-core convention; native Windows RSS -1 is unavailable and
sample maxima are not exact peaks. ASR is rolling offline re-decoding and Piper
sentence incremental; these are not a connected live conversational system.

The candidate adds zero queued frames; NS remains 10 ms. Planned workspace is
147,984 / 403,984 bytes versus baseline 136,832 / 381,312 at 16/48 kHz. It includes
preallocated state/scratch and excludes allocator/runtime overhead. No GPU DSP
operation, mandatory library dependency or hidden worker is introduced.

Windows static/shared core is 7/7 each; installed-public-Hail consumer 3/3.
`numerical*.log` / `msvc.log` cover 16/48 kHz deterministic split identity,
zero-reference bypass, finite partial EOS and epoch/generation/reference-loss
events. ARM64 core ASan/UBSan and TSan are 8/8 each. Separate runner sanitizer
logs cover the candidate; `baffle-j-alloc.csv` measures zero allocation calls in
200 active-reference processing calls. Sanitizer/allocator checks are finite
evidence, not real-time or production-quality proofs. Core fuzz/static analysis
coverage remains distinct from candidate-specific coverage; no libFuzzer result
is claimed for the experimental mask.

`retained-setup-failures.json`, workflow/strict-MSVC CI receipts and the raw
Windows job log retain unsuccessful setup/build attempts. No compiler warning
was suppressed or assertion weakened. Superseded/cancelled matrices are not
counted as complete passes. Baseline completed CI and later experiment/archival
CI are separate; final branch checks remain directly inspectable on PR #1.

## Reproduction and asset retention

`commands.json` is the top-level reproduction ledger; spawned native commands
are preserved verbatim in JSON results and performance reports. Fresh output
directories are required. `diagnostic-raw.tgz` holds CSV/JSON/log receipts, while
`performance-raw.tgz` contains raw call/resource/worker observations. Both exclude
PCM, recordings and weights. `private-audio-inventory.json` identifies original
and processed local PCM by SHA256. Original raw assets, transcripts, model pins,
license notices and manifests remain in the unchanged Gate H/I caches/ledgers.
No ASR output is used as a transcript oracle and no assets are republished merely
because a hash is available. Optional Python/inference packages are external QA
dependencies, not Baffle requirements.

`SHA256SUMS` covers recorded file bytes including native log newlines, except
itself. Source/fixture/acceptance changes are not hidden behind passing reruns.
No near-end word loss is accepted to obtain better echo-only scores. The failed
candidate remains isolated; remaining assistant-only transcript commitment and
speaker attribution also require the separate speech runtime's explicit policy.
