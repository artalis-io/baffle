# Gate K: adaptive AEC robustness and path tracking, 2026-11-21

**PARTIAL.** Three source-level correctness defects are fixed: incomplete
multichannel reset, unreported native recovery, and signed schedule-counter
overflow. Two isolated adaptation candidates are rejected. Ordinary mono
convergence, real-room rejection and conversational qualification are not claimed
solved. Public records/exports, default AEC parameters, NS/AGC/VAD ordering,
10 ms NS buffer, Hail and mandatory dependencies remain unchanged. PR #1 stays
draft; ABI remains provisional.

## Baseline and source audit

Baseline `96e88d4` completed 27/27 CI jobs in run `44881606913`. Fresh original
fixture replay reproduces **14.603375 dB** near-component SNR improvement against
the unchanged **8 dB** requirement; separate far-only ERLE is **25.831140 dB**.
Fresh paired complete-pipeline double-talk WER is **4.55% Whisper / 57.20%
Parakeet**, using the unchanged Gate H eight-case grouping. Assistant-only remains
**11/11 Whisper, 9/11 Parakeet** (426 / 392 false recognized words). These metrics
are different from FIR ERLE, coefficient misalignment and transcript commitment.

The reviewed implementation is the hash-pinned SpeexDSP 1.2.1 **floating-point
AUMDF**, `libspeexdsp/mdf.c`, included by `src/speex/mdf_baffle.c`. Sources and
BSD notices remain pinned/unmodified in production. Baffle supplies allocation
hooks and the existing 20 Hz notch-radius configuration. The backend maintains
foreground/background banks, proportionate partition updates, foreground
promotion/backtracking and a variable frequency-domain learning rate based on
reference excitation, residual/error and leakage estimates. It has continuous
double-talk-aware adaptation, not a calibrated binary near-speaker classifier.
Regularization/power floors, saturation gating and a native recovery guard are
already implemented; this gate does not replace them with a new framework.

The [fixed protocol](gate-k-protocol.md) separates lifecycle fixes from adaptation
experiments, including its source-driven second-candidate addendum before that
candidate's evaluation. The [evidence ledger](evidence/2026-11-21-gate-k/README.md)
retains source/SDK/binary/model identities, failed reproductions, inputs/outputs,
IR curves, ASR edits, timing receipts and exact commands. Models, source recordings
and PCM remain in local caches with checksums and unchanged license requirements.

## Confirmed defects and corrections

### Multichannel reset left a trained acoustic path alive

The upstream reset clears `N*M` filter coefficients and `N*(M+1)` reference
history values, although allocations are `N*M*C*K` and `N*(M+1)*K`. Trained
foreground/background banks and render history beyond those mono ranges survive.
The public-API regression trains two capture/two reference channels, resets,
then feeds a silent microphone with a valid new reference. Baseline synthesizes
old-path output with **1.24207785632** summed squared energy; corrected output
is exactly zero. This is a state-lifetime defect, not insufficient ERLE tuning.

`baffle_speex_reset_complete` calls the upstream reset and clears all configured
coefficient/reference banks and multichannel residual/playback history. It is
used by explicit and timing-driven Baffle resets. Regression coverage includes
1×2, 2×1, 2×2 and 8×2 at 16/48 kHz, explicit reset, capture-epoch change and
missing-reference recovery: **24 trained cases**, with no old-path output.
Mono adaptive scheduling is preserved; no constructor-equivalence claim is made
for every private learning prior. Reset work is bounded by configured state,
including the full channel count; it allocates nothing.

### Native recovery was invisible to public consumers

On the deterministic abrupt-path fixture, native `cancel_count` falls to zero
at source frame **199840**, reflecting Speex's recovery guard. Baseline reports
`resets=0, flags=0`. Corrected Baffle reports **`resets=1,
BF_AUDIO_DISCONTINUITY`**. The guard is triggered by finite changing-path input;
this is not being labeled a NaN or a Hail/device fault.

A constant-time post-call check detects native recovery, completes reset through
the same channel-correct path and increments existing reset telemetry. Source
clock/domain/epoch are retained. A successful call carrying that flag indicates
adaptive-state recovery, not physical silence, device loss or a newly fabricated
clock mapping. The pipeline propagates AEC flags and preserves near-leg
NS/AGC/VAD state on reference-only recovery. Native output is not silently treated
as continuous healthy adaptation. The regression fails against the old wrapper
and passes with the correction, without weakening existing assertions.

### Continuous operation could overflow a signed schedule counter

Speex increments signed `int cancel_count`, used for the AUMDF modulo constraint
schedule. A 32-bit boundary is reached after roughly **248.6 days at 10 ms**
(497.1 days at 20 ms), independently of sample rate. An accelerated valid-state
boundary reproduces signed overflow under UBSan at upstream `mdf.c:707`; baseline
Windows also exhibits negative counter values. This is not a calendar-duration
hardware measurement.

Before cancellation, the integration normalizes a near-limit counter to a
positive equivalent modulo `M−1`. Coefficients, timing and adaptive state are not
reset. Tests verify bounded positive values, preserved modulo phase and no false
public reset. The normal path adds only a bounded integer check. No output zeroing,
allocation, lock, worker or public field is introduced to hide the problem.

## Convergence, delay and path experiments

The fixed primary matrix has 21 configurations: stationary, abrupt/gradual/gain/
delay changes, short/long tails, two quiet reference levels, noise, near-only,
quiet/normal/loud overlap with a path change, reference gap, signed 200 ppm drift,
two predefined extra FIR/noise seeds, and legal quantum/tail controls. Three
additional non-tuning 48 kHz cases use explicitly band-limited offline SRC of
stationary/abrupt/double-talk data. They are not new 48 kHz room recordings.

Private static-only qualification compiles the actual AEC wrapper and queries
background impulse responses/adaptation counters. It intentionally accesses
private state for backend diagnosis, **not** as a DSP integration example or new
installed API. Snapshots occur outside processing calls. The independent Hail
consumer continues to use installed public headers/libraries only.

ERLE is reported only in known far-only windows. During overlap, near-component
projection and raw-component SNR improvement are separate. Background IR
misalignment includes notch conditioning, S16 quantization and out-of-capacity
taps; band-limited excitation limits coefficient identifiability. Drift has no
single static FIR target and is explicitly unavailable for that comparison.
Unexcited/unreached values remain null. Snapshot work is not callback timing.

Settling uses two consecutive excited, fully post-event 500 ms windows ≥20 dB,
the existing synthetic unit target. Gradual movement ends at 16 s; the reference
gap ends at 10.2 s. Earlier draft scores that used the movement start or partial
post-gap windows are retained, along with the initial truncated-IR draft. These
method corrections change no PCM, recognizer result, original 8 dB calculation
or acceptance threshold, and do not turn the candidate into a PASS.

| 16 kHz in-capacity path | Baseline/corrected | Partition candidate | Readiness candidate |
|---|---:|---:|---:|
| Abrupt change | 3.0 s | 3.0 s | 3.0 s |
| After gradual movement | 2.0 s | 2.0 s | 2.5 s |
| Gain change | 1.5 s | 1.5 s | 1.5 s |
| Added 100 ms delay | 3.5 s | 3.5 s | 3.5 s |
| Predefined reflection holdout | 3.5 s | 5.5 s | 3.5 s |

Initial stationary settling is 2.0 s. During gradual motion the worst 500 ms
far-only ERLE falls to about **11.56 dB**. The 20 ms quantum has different tracking
behavior; abrupt recovery is 4.5 s. A 300 ms reflection cannot fit a 200 ms filter:
20 dB remains unreached, while the existing 500 ms configuration reaches it.
This does not justify changing every application's filter length. Quiet/noisy
and uncompensated drift cases retain failures rather than being omitted. The
extra band-limited 48 kHz abrupt case improves 5→3 s with the partition candidate,
while readiness stays at 5 s. This isolated gain does not override the 16 kHz
holdout and speech regressions.

`bf_aec_estimate_delay` remains a dominant **learned background tap** query. It is
not a delay search, confidence score, acoustic/device latency measurement or
automatic reference shift. The synchronous backend requires aligned equal-rate
frames. The existing Hail adapter/clock trackers and SRC handle declared rate and
drift mapping before AEC where valid timing exists. No unrelated-domain timestamp
comparison, new clock abstraction, global per-frame search or hidden queue is
added. Unknown uncertainty remains unknown. Variable native callback sizes require
bounded assembly into a context's configured quantum; rate/quantum changes require
compatible context recreation, not silent renegotiation.

## Adaptation candidates and ASR protection

The first adaptation candidate changes the partition exploration floor from
0.1 to 0.3 of the largest partition norm, keeping normalization, total learning
equations, notch, delay, foreground/backtracking and all conditioning parameters
fixed. It is prepared only in a separate worktree from verified upstream input,
retains BSD notices, and has no production selection/API option. It tests whether
late, previously weak partitions learn a moved path too slowly. No postfilter
or stronger residual suppression is involved.

Median moving-path recovery improves **0%**, missing the predeclared 20% target;
the reflection holdout worsens by 2 s. The original synthetic metric rises only
to **14.670661 dB**, which is not the objective and does not rescue the trade-off.
Its ordinary numerical/unit suite passes, demonstrating why return codes and
synthetic thresholds alone are insufficient.

Tracing then establishes that `adapted` is not a calibrated convergence state:
the stationary baseline ends with `adapted=0`, `sum_adapt=600`, leak .005 despite
good cancellation; very quiet excitation has no adaptation, while noisy data can
enter `adapted=1` without reaching 20 dB. Proportionate weights are recomputed only
in the adapted branch, limiting the first candidate's relevance on some cases.
No such bit is exposed as a user/convergence probability.

The independently preregistered follow-up removes only the leak>.03 readiness
clause, retaining the accumulated-adaptation budget and original .1 partition
floor. It enters variable-rate mode on the stationary fixture (`adapted=1`,
sum 20.25), but still improves median primary moving-path recovery **0%** and
worsens post-gradual recovery 2→2.5 s. The original score remains **14.603375 dB**.
This is not a demonstrated algorithm correction. It is rejected without tuning
thresholds or combining it with the first candidate.

Fresh baseline/candidate ASR uses raw, AEC-only and complete pipeline with fixed
Whisper/Parakeet configurations and independent provider transcripts. It covers
the unchanged 28 speech scenarios, 11 assistant-only cases, real-room far-only
replays and four new moving-path mixtures using the same near/reference assets.
The extra mixtures are non-tuning challenges, not new blind speech or rooms.
**846 new recognitions** are retained; no ASR output supplies ground truth.

The partition candidate adds full-pipeline word errors on cancellation/restart,
capture-epoch and early-overlap cases. AEC-only Parakeet outcomes include new
blank/large-error regressions. Far-only full Whisper remains 11/11 nonempty
(426→383 words), while Parakeet worsens **9/11→10/11, 392→414 words**. Real-room
full false words worsen **228→230 Whisper / 179→184 Parakeet**. These are recognized
words, not committed conversation turns. No runtime attribution/gating policy is
implemented inside Baffle to remove unfavorable outcomes.

The readiness follow-up introduces no added word/blank/first-word regression in
these speech comparisons, but also no assistant-only or real-room recognition
improvement: full false counts/words remain 11/11 and 426 for Whisper, 9/11 and
392 for Parakeet; recorded words remain 228 / 179. Preserving those outcomes
does not compensate for the missing acoustic benefit or establish generalization.

The accepted fixes preserve **78** mono AEC/full outputs across the original
speech/event and far-only matrices, plus **48** real-room/control outputs and
all **24** primary/extra-rate path outputs. Original baseline hypotheses are reused only for identical PCM
and pinned configurations, not claimed as new decodes. Native recovery metadata
changes are intentional. Reviewed first-phone/syllable and live barge-in evidence
remain unavailable; waveform projection does not qualify them.

## Numerical, resource and interoperability evidence

An accelerated **1,800-second virtual-audio** stress run processes **180,000**
frames with zero nonfinite output and all **28** native recoveries publicly
reported. Signals include varying levels, noise, overlap, drift and path changes.
This is not a 30-minute real-time/device soak or evidence of universally adequate
convergence. The signed-limit test accelerates the counter separately.

Matched 16/48 kHz AEC-only/full trials use actual CPU Whisper+Parakeet+Piper,
plus a 120-second corrected full-pipeline trial: **17 trials, 60,000 calls**,
zero observed 10 ms completion/execution misses. Corrected full P99 is about
**.1163 / .2780 ms** in the paired 30-second trials; 16 kHz's longer trial is
**.1468 ms**, maximum **.2544 ms**. Corrected AEC-only P99 is **.0812 / .2125 ms**.
Workspace remains **121,968 / 348,528 bytes AEC**, **136,832 / 381,312 full**.
Readiness-candidate full P99 is .1521 / .3333 ms, with the same workspace; it
does not establish a quality benefit worth additional ordinary work. All raw
baseline/candidate distributions and shorter/longer trial distinctions remain.
No extra algorithmic delay or buffering is introduced. Runtime variation is not
a claimed speedup. Raw distributions, sampled CPU/RSS, worker receipts and
scheduling lateness remain separate; ASR is rolling offline re-decoding, Piper
sentence incremental, not a connected live conversational runtime.

Windows static is 8/8 including the private recovery test, shared 7/7; the newly
installed corrected Hail consumer is 3/3. ARM64 ASan/UBSan and TSan are 9/9,
including no-allocation and private recovery checks; MSVC Debug static is 8/8.
Before-fix UBSan failure is
retained. Fuzz evidence distinguishes backend-disabled runs from native AEC;
they are not interchangeable. Kernel object libraries now receive their own
fuzzer/ASan/UBSan instrumentation, and CI adds native pipeline fuzzing plus
Speex-enabled GCC analysis. No warning or assertion is suppressed to make a pass.
Final cross-platform status is recorded separately from local checks.

Hail source, independence, buffer ownership, separate clocks/epochs, render
generation semantics, bounded backpressure and application-driven device recovery
are preserved. AEC recovery does not invalidate source clock provenance or imply
acoustic silence. No ABI freeze or physical-device certification is made.

Reproduce physical qualification using the existing installed-public-Hail
acquisition protocol in [the tools guide](../tools/acoustic-runner/README.md).
Record explicit endpoints, route, driver processing, distance, playback level,
calibration and timing uncertainty before far-only/near-only/overlap runs. Move
the path at annotated times, include cancellation plus residual tails, and replay
the same complete intervals through each frozen implementation. Agree route-
specific timing/quality budgets before scoring; the synthetic 20 dB settling
diagnostic is not a universal room acceptance threshold. Independent near-end
transcripts and reviewed onset labels remain required. Queue invalidation/flush
acknowledgement is not measured acoustic silence.

**Gate K: PARTIAL.** Adopt the three reproducible correctness fixes, retain the
existing adaptive parameters, and reject both adaptation candidates. Ordinary mono
path tracking, real-room delay/drift robustness, low-level excitation, nonlinear
echo and independently reviewed interruption quality remain qualification work.
