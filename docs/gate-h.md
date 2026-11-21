# Gate H: paired speech intelligibility and interruption, 2026-11-20

**PARTIAL.** Paired recognition is reproducible and double-talk improves markedly,
but near-only regressions, new Parakeet blanks and assistant-only false transcripts
remain. Independently reviewed first-phone/syllable evidence is unavailable.
No production-readiness or ABI-stability claim follows. PR #1 stays draft.

## Baseline and protocol

Baseline `a231983` completed all **27 CI jobs**, run `44803787522`. The original
unchanged 8 dB test reproduces **14.603375 dB**, far-only ERLE **25.831140 dB**.
The earlier 6.648426 dB failure and all prior evidence remain unchanged. The
120-second baseline had P99 .145002 ms, maximum .3173 ms, zero completion misses,
136,832-byte mono pipeline workspace and explicit 10 ms NS buffering. Hail
installed-consumer tests were 3/3; ABI 0.1 was and remains provisional.

New [evidence](evidence/2026-11-20-gate-h/README.md) separates pre-correction
measurement, telemetry correction, actual inference replay and human review.
The original synthetic test has not been edited or replaced.

`tools/paired_asr.py` processes **28 fixed cases × 5 stages × 2 recognizers = 280
evaluations**. Two recorded English utterances and independent provider word
transcripts come from the pinned sherpa Whisper export. Actual seeded Piper
speech feeds a simulated 3/20/80 ms FIR. These are recorded human speech with
**simulated echo**, not new physical room recordings. Existing weak real-room
results are not superseded. Corpus size is small; no population confidence or
Hungarian/short-command/fricative coverage claim is made.

Cases include normal/quiet/loud near-only and double-talk, early entry, speech
through cancellation, post-cancellation entry, immediate render restart, capture
epoch change, missing reference, far-only and silence. Whole utterances remain
intact. Each stage uses identical input and the same complete source interval,
including its difficult context. Stages are raw, AEC, AEC+NS, AEC+NS+AGC and full
AEC+NS+AGC+VAD. VAD does **not** gate PCM; in the full pipeline its probability
also feeds subsequent NS/AGC decisions, so its PCM need not equal the fourth stage.

Only the public reported NS delay is compensated when selecting ASR windows.
The earlier `word_qualification.py` raw common-index protocol and its results
remain historical; this paired protocol does not silently replace that evidence.
Whisper tiny.en int8 and Parakeet TDT v3 0.6b int8 use sherpa-onnx/core 1.12.40,
CPU/two threads, greedy decoding, 16 kHz mono F32 and fresh offline streams.
There is no per-case gain normalization, VAD gating or endpoint selection. The
constructor's displayed default feature dimension is 80; pinned native model
metadata explicitly overrides it to **128**. Sample-rate/channel confusion is
not demonstrated as the cause of blanks.

Before recognition or tuning, the immutable fixture manifest preregistered
conservative engineering checks: zero added near-only word errors per case,
zero newly blank speech cases, pooled double-talk error reduction and zero
assistant-only transcriptions. These operationalize nonregression conservatively;
they are not owner-approved phoneme/VAD budgets. No thresholds were relaxed and
no DSP algorithm was tuned on this evaluation set.

## Paired WER/CER and failures

Values are pooled **WER / CER percentages**. Edit distance reports separate
substitution, deletion and insertion counts; both rates use summed reference
lengths, not the mean of utterance rates. Text is NFC/casefolded; punctuation is
removed, internal apostrophes retained, and CER excludes whitespace. Insertions
can make WER exceed 100%. Empty references have **undefined WER/CER**; their
insertion counts and false-transcription events are reported separately.

| Stage | Whisper near-only | Whisper double-talk | Parakeet near-only | Parakeet double-talk |
|---|---:|---:|---:|---:|
| Raw | 4.55 / .35 | 110.98 / 73.27 | 1.52 / .35 | 107.20 / 76.73 |
| AEC | 4.55 / .35 | 4.55 / .35 | 1.52 / .35 | 26.89 / 22.92 |
| AEC+NS | 4.55 / .35 | 6.06 / 1.30 | 25.76 / 23.76 | 12.12 / 8.13 |
| AEC+NS+AGC | 4.55 / .35 | 6.44 / 1.47 | 1.52 / .35 | 23.86 / 22.15 |
| Full pipeline | 5.05 / .58 | 4.55 / .35 | 1.52 / .35 | 57.20 / 54.67 |

Whisper full-pipeline near-only has **one additional word error** in the quiet
second source: the strict nonregression check **FAILS**. AEC+NS adds a deletion/
substitution in the first quiet source and a blank 48-word hypothesis in the
second under Parakeet: that stage also **FAILS** near-only nonregression. Full
Parakeet near-only matches raw pooled WER but has **four newly blank speech
cases elsewhere**; passing near-only aggregate alone is insufficient.

On cancellation cases, raw→full WER is **79.29→6.57%** for Whisper and
**105.05→3.03%** for Parakeet. On capture/missing-reference recovery it is
**143.94→9.85%** and **116.67→19.70%**. Difficult intervals are not omitted.
Full-pipeline first-word accuracy is 100% on near-only and cancellation for both
models, but only **25%** on double-talk for Parakeet versus 100% for Whisper.
First-word recognition is not proof of first-phoneme preservation.

Far-only input causes false transcripts in both raw cases for both recognizers.
AEC-only produces empty Parakeet results in both, but adding AGC or the full
pipeline again yields nonempty results in both. Whisper remains nonempty even
after AEC: total inserted words fall from 103 raw to 2 full-pipeline words, while
event count stays 2/2. Whisper also returns nonempty output on both **raw silence**
controls; that is a pre-existing recognizer-policy failure, not a DSP regression.
Parakeet silence is correctly empty. This offline harness intentionally does not
hide these cases using a newly selected no-speech threshold or VAD gate.

Known absent-near VAD trace rises fall from 125 raw to 2 AEC-only, but are 48 for
the full pipeline across the two entire far-only recordings. These are candidate
false barge-in signals, not application turn-taking decisions. VAD reports signal
activity and cannot identify the speaker by itself. Counts, audio levels,
nonfinite/clipping checks, decode time and every hypothesis remain in the raw
results. Offline decode time is not ASR onset or first-partial latency; partial
stability remains unavailable in this fixed offline evaluation.

## Parakeet blank isolation

The controlled matrix records original PCM identity, duration, 16 kHz/mono
geometry, RMS/peak, silence ratio, nonfinite count, VAD activity, pinned model and
decoder configuration, text and errors. Inputs are finite; examined blanks are
not clipping, wrong channel count or VAD-gated-away PCM. They also occur on raw
post-cancellation speech before any Baffle processing. Fresh recognizers repeat
the failures, excluding simple cross-stream decoder-state leakage.

One-factor controls preserve the complete source utterance:

* Source0 AEC double-talk: original/fresh recognizer blank; whole-window ×0.1 or
  removal of only the half-second context recovers 0 WER.
* Source0 raw post-cancellation: original and ×0.1 blank; removing only context
  yields 5.56% WER. A universal gain workaround does not solve this case.
* Source1 quiet AEC+NS: original, fresh, ×0.1 and context controls blank;
  **unclipped ×10** yields 2.08% WER. Speech information was not wholly removed.
* Source1 full-pipeline double-talk: original/fresh blank; ×0.1 or removing context
  yields 2.08% WER. Additional zero context also changes the result.

An independent diagnostic decodes the same ONNX weights using explicit TDT
greedy control flow and kaldi-native-fbank 1.22.3. It reproduces the main original
double-talk blank without Baffle, VAD or sherpa's Python result adapter in that
path. Centered variance, additive log guard, Hann window and Nyquist mel edge are
tested separately; none reliably fixes all original cases. The emulation is not
bit-identical to sherpa in every case and is **not** an independently verified
NVIDIA frontend or a replacement ASR runtime.

The failure is isolated to signal-level/context sensitivity in the selected
offline feature/model/greedy-decoding path. Its exact numerical/model cause is
**not fully resolved**. The upstream
[frontend discussion](https://github.com/k2-fsa/sherpa-onnx/pull/4024) is a hypothesis
source, not proof that its proposed change fixes these inputs. No Baffle gain,
NS strength or VAD default was changed to compensate for this model-specific
behavior. A validated reference frontend / FP32 comparison remains follow-up.

## NS delay and onset review

`src/noise.c` uses a two-hop square-root-Hann analysis/synthesis window and
overlap-add. Output corresponds to the previous hop; first output is priming
silence. This is an algorithmic overlap requirement of the selected method,
not an extra pipeline worker, mutex, arbitrary aggregation or GPU wait.
`bf_pipeline_process` reports `algorithmic_delay_frames`, carries source timing
with the delayed block and flushes the last real samples. The impulse experiment
observes exactly **160/480 samples** at 16/48 kHz, matching the public one-hop
contract. Strength-zero reconstruction remains within the existing numerical
tolerance; EOS and partial final frames are covered.

A standalone 5 ms NS configuration measures 80/240 samples. On two recorded-speech
plus deterministic-noise probes it improves error SNR but worsens Whisper WER:
16.67→22.22% and 6.25→8.33% versus 10 ms. This is a small NS-only experiment, not
an integrated 5 ms AEC pipeline or a broad quality benchmark. AEC's supported
10/20 ms geometry and external reblocking would also need review. **Keep the
10 ms pipeline default.** No hidden delay was moved elsewhere. VAD onset/hangover
is decision latency, not PCM buffering; separately used SRC reports its own
group delay, which applications must account for independently.

`prepare_onset_review.py` creates an explicit **UNREVIEWED** audition package with
raw, processed and clean-near audio, original F32 hashes, quantized audition WAVs,
word transcript and review JSON. It refuses to replace existing human review.
Constructed insertion positions are clearly not independent phoneme annotations.
The scorer still requires a reviewer, onset/phone/syllable boundaries, uncertainty,
processed-audio judgments and application-approved VAD/WER budgets. No reviewer
or policy was invented. English phonetic diversity, short commands, Hungarian,
natural distance/room variations and independently measured ASR/VAD onset remain
open. Spectral/energy diagnostics cannot substitute for those judgments.

## Transitions, correction and interoperability

The new paired scenarios retain the complete acoustic FIR tail and ASR context
through generation-only cancellation, render epoch/restart, capture epoch and
temporary reference loss. Supplementary evidence reports output sample steps,
peaks, AGC gain, AEC resets, VAD activity, priming and discarded frames. A sample
step is **not** a human audible-click measurement or an acoustic-tail duration.
Render-only changes do not reset near-leg NS/AGC/VAD: new public-API tests compare
their output and state-derived gain/activity against an uninterrupted control.
Capture epoch change correctly discards one pending NS hop and reprimes. Existing
format checks require explicit context reconfiguration for sample-rate changes.

One confirmed defect was corrected in `src/aec.c`: reset cleared `seen` before
checking a simultaneous valid reference generation transition. The combined
epoch/generation event therefore omitted `BF_REFERENCE_GENERATION_CHANGED`.
The new regression fails before correction and passes afterward. Comparison is
now captured before reset, excludes missing references, and does not change
adaptive weights, reset policy, PCM, buffering or allocations. Existing missing-
reference and generation-only tests remain intact. Public records/layout and
symbols are unchanged; the header only clarifies the flag contract.

Hail installed-consumer tests remain **3/3**, with its independent domains/rates,
epochs, uncertainty, multichannel PCM, bounded ownership, cancellation and recovery
contracts. No Hail source or mandatory dependency was added. Capture continuity
and generation invalidation remain distinct from native flush, device presentation
and acoustic silence. Physical tail and live conversational qualification remain
open; ASR replay does not prove them.

## Performance, safety and acceptance

Actual six-workload Windows trials and 120 s Whisper+Parakeet+Piper repeat the
baseline protocol on the corrected C source. Detailed P50/P95/P99/max, scheduling,
CPU/RSS/GPU samples, worker completion, bounded modeled queue occupancy and raw
timings are retained. The combined run records **P99 .154601 ms**, maximum
**.6611 ms**, wake-lateness P99 **.528301 ms**, zero execution/completion misses
against the existing 10 ms frame deadline, and **136,832 bytes** persistent
pipeline workspace. It still adds 10 ms NS buffering. These are finite observed
values, not hard OS guarantees or a claimed speedup over .145002/.3173 ms.
Three-second rolling offline ASR and sentence-incremental Piper are actual CPU
inference but not a connected live speech loop. A separate existing GPU LLM
service is measured without changing its model/service configuration.
On that Spark replay, alone/with-LLM P99 is .110849/.108446 ms, with zero
completion misses in two 30 s trials; this small difference is not a causal
speedup claim. Windows six-workload P99 ranges .110133–.134707 ms. Hardware,
per-process CPU/RSS, flags, versions and raw observations are in the evidence.

The C correction adds one bounded local boolean comparison, no allocation, lock,
new thread, wait, GPU operation or mutable shared state. Contexts retain one
processing owner; concurrent reset/process/destruction is not supported. Windows
core is 7/7, installed Hail 3/3; ARM64 ASan/UBSan and TSan each 8/8, instrumented
installed-tool smoke, fresh 10,000 AEC fuzz iterations and GCC analysis pass.
The core/API correction commit `ee1c080` also passed all 27 cross-platform CI
jobs. Later tool/documentation checks are recorded separately. New optional
model tools are excluded from ordinary builds and model-free CI smoke does not
download weights. Sanitizers do not prove real-time deadlines or speech quality.

| Gate H requirement | Result |
|---|---|
| Paired ASR reproducibility | Implemented, 280 evaluations and raw audio retained locally |
| Blank hypotheses explained/isolated | Isolated; exact frontend/model cause still open |
| Near-only nonregression | Strict check FAILS for full Whisper and AEC+NS Parakeet |
| Double-talk evaluation | Measured improvement over raw; newly blank cases remain |
| Independent reviewed interruption onset | OPEN |
| NS latency | Measured and documented; default preserved |
| Epoch/restart tests | Implemented; confirmed telemetry defect fixed |
| Resource budget | No observed 10 ms misses in stated actual-contention trials |
| Hail interoperability | Installed consumer remains 3/3 |
| Numerical/concurrency verification | Available checks pass; unsupported concurrency remains prohibited |

**Gate H: PARTIAL**, with explicit failed quality checks. Resolve the retained
model-sensitive cases and assistant-only transcriptions, obtain reviewed onset
labels and application limits, and qualify a connected physical speech loop
before production acceptance. Do not tune the current holdout to manufacture
passing scores or treat timing/ASR improvements as proof of first-phoneme safety.
