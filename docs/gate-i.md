# Gate I: acoustic reliability and speech preservation, 2026-11-21

**PARTIAL, with remaining failed acoustic/recognition requirements.** One narrow
NS contract defect is corrected. Default near-only regressions and assistant-only
false transcripts are not solved. No new suppressor, default ordering, gain
policy, calibrated probability, public record or mandatory dependency is added.
PR #1 remains draft; ABI is provisional.

## Baseline and executed scope

Baseline `2e59db181bb03430a7735ed5e9ad1360c3bf93ca` completed **27/27 CI jobs**
in run `44831903238`. The unchanged original fixture reproduces **14.603375 dB**
against **8 dB**, with far-only ERLE **25.831140 dB**. Earlier failed evidence is
not replaced. Gate H's 4.55% Whisper / 57.20% Parakeet double-talk WER, 0.155 ms
combined-load P99, .661 ms maximum, 136,832-byte mono workspace and 10 ms NS hop
remain historical measurements, not physical qualification.

New [retained evidence](evidence/2026-11-21-gate-i/README.md) contains:

* Before/after **448 + 448** evaluations: all 28 unchanged Gate H cases, eight
  requested variants, two fixed recognizers. Raw/AEC/NS/AGC/AEC+NS/AEC+AGC/NS+AGC/
  full pipeline are compared. No difficult cases are omitted from that matrix.
* 36 protected-startup evaluations and 52 order evaluations. The plain PCM order
  probe intentionally excludes capture-epoch cases because it has no capture
  reset adapter; those remain in the complete matrix. Its matched NS→AGC control
  is the retained same-PCM/configuration Gate H `aec-ns-agc` evaluation.
* 88 simulated assistant-only evaluations: levels, path length, nonlinear echo,
  path change, 100 ms added delay, startup, cancellation tail, restart and missing
  reference. These use real Piper TTS through explicitly simulated acoustics.
* 48 ASR evaluations on three fixed real far-only recordings at two rates and
  four variants; 72 native room replay conditions include the existing training-
  fitted alignment variants. Two rates are not six independent rooms.

Recognizers remain pinned Whisper tiny.en int8 and Parakeet TDT v3 0.6b int8,
sherpa-onnx/core 1.12.40, CPU/two threads, identical greedy decoding and complete
source intervals. Publisher transcripts are independent of both tested models
and hash-verified; they were not newly hand-annotated here. Original/processed
audio stays in the local immutable caches, with public checksums and commands.
No model output is used as ground truth. Hungarian and independently reviewed
phone-level evidence remain unavailable.

## Confirmed NS defect and correction

In [`bf_noise_process`](../src/noise.c), the first spectrum initialized noise to
`power * .01` **even when the caller requested frozen learning**. Subsequent
updates respected foreground protection, leaving that unwanted speech-derived
estimate in place. The documented freeze contract was therefore incomplete on
init/reset. This is independent of ASR or echo-path assumptions.

The regression in [`tests/conditioning.c`](../tests/conditioning.c) fails before
correction. Bootstrap now follows the existing foreground/freeze condition and
starts with zero noise when no valid noise observation exists. It changes no
threshold, stage order or resource plan. Frozen learning still permits suppression
using an already learned noise estimate; it does not generally bypass NS.

The no-prior-noise frozen path now reconstructs the protected input to floating-
point WOLA precision, including after reset. In the first recorded source its
relative error falls from about **2.04e-5 to 1.31e-14**. This is source-fidelity
evidence, not a perceptual or universal ASR score. Tests cover 16 kHz mono and
48 kHz stereo; original random fixture state and acceptance assertions remain
unchanged. Pipeline fuzzing adds a seed retaining VAD activity while resetting NS.

An important counterexample is retained: at one source/level Parakeet had zero
word errors on the buggy attenuated output but one on raw and corrected output.
The correction restores raw recognition rather than that accidental benefit.
It is **not an ASR optimization or a blanket recognition nonregression PASS**.
No gain compensation or model-specific filter is introduced to keep that score.

The ordinary pipeline initially supplies an unknown activity hint. On the entire
unchanged matrix, all **224 distinct PCM results and all 448 hypotheses** match
before/after. The fix therefore closes protected bootstrap correctness, not the
existing default-pipeline speech failures.

## Near-only preservation and NS/AGC interaction

Pooled near-only WER percentages across six cases:

| Variant | Whisper | Parakeet | Parakeet blanks |
|---|---:|---:|---:|
| Raw | 4.55 | 1.52 | 0 |
| AEC | 4.55 | 1.52 | 0 |
| NS | 4.55 | 25.25 | 1 |
| AGC | 4.55 | 3.54 | 0 |
| AEC+NS | 4.55 | 25.76 | 1 |
| AEC+AGC | 4.55 | 3.54 | 0 |
| NS+AGC | 4.55 | 1.52 | 0 |
| Full pipeline | 5.05 | 1.52 | 0 |

The default full Whisper extra word error and quiet NS/Parakeet blank remain.
NS alone reproduces a blank without AEC; AGC alone also changes recognition.
No rate, channel, nonfinite, clipping, text-adapter or VAD-gating defect is shown.
This does not exonerate DSP: valid attenuation and spectral changes can harm a
recognizer. In the quiet second source, NS-only near projection is .7363 and
gain-adjusted log-spectral RMS difference is 9.646 dB under the explicitly floored
diagnostic. Those measurements do not establish which phonemes remain audible.
The prior unclipped gain/context controls recover some hypotheses, but the exact
feature/model cause is still unresolved. Both DSP effects and model sensitivity
remain part of the compatibility limitation.

On matched non-capture-epoch speech cases, AEC→AGC→NS gives pooled WER **6.20%
Whisper / 14.05% Parakeet**, versus **5.92% / 13.22%** for AEC→NS→AGC. It does
not provide broad evidence for changing order. Both matched order probes exclude
VAD feedback; they do not represent every full-pipeline context. AGC attack/release is separately
verified at 16/48 kHz against constant-level analytic 5/250 ms responses, with
the limiter inactive; known nonspeech prevents gain increases. Existing limiter
tests and output measurements remain. Gain holding/increases can still amplify
residual echo; VAD is not a speaker classifier.

Keep the existing ordering and **10 ms NS buffer**. No new noise-strength or
gain target was chosen on the evaluation holdout. Gate H's 5 ms experiment and
its worse Whisper WER remain historical evidence against a latency-only change.

## Assistant-only rejection and real rooms

All assistant-only references are empty; WER/CER are undefined. We count nonempty
hypotheses, inserted words and window-normalized recognized-word rates instead.
**Committed-word rate is unavailable:** no transcript commitment/speaker policy
exists in these DSP tools, and none was added to Baffle.

| Simulated cases, nonempty hypotheses / 11 | Raw | AEC | AEC+NS | Full |
|---|---:|---:|---:|---:|
| Whisper | 11 | 11 | 11 | 11 |
| Parakeet | 10 | 8 | 4 | 9 |

Full-pipeline inserted words total 426 Whisper / 392 Parakeet versus 615 / 626
raw; event rejection remains unreliable. NS helps some Parakeet far-only cases
while the same stage harms quiet near recognition. It cannot be promoted as a
safe residual suppressor from these echo-only outcomes. Startup/tail/reference-
gap failures remain retained, without application-level rejection filters.

On the three real far-only recordings at 16/48 kHz, Whisper is nonempty in **6/6**
conditions for every variant. Inserted words rise from 148 raw to 228 full.
Parakeet remains nonempty in **4/6** conditions (192 raw words, 179 full). These
are small, labeled far-only intervals, not calibrated room or deployment studies.

At 48 kHz, apparent far-only microphone/output ratios are:

| Recording prefix | AEC, dB | AEC+NS, dB | Full, dB |
|---|---:|---:|---:|
| 00113069 | .80 | 3.46 | 11.26 |
| 00dPPLGk, moving path | -.01 | 2.31 | -2.67 |
| 02462e78 | 6.60 | 9.13 | 12.37 |

NS figures compensate only the documented hop; raw common-index metrics remain
separate in the replay report. Ambient noise and processing are included. In the
moving-path case, full processing amplifies output power relative to microphone
input. Better energy attenuation in other rooms does not imply fewer false
transcripts or preservation of user speech.

Controlled hypotheses and conclusions:

| Hypothesis / one changed variable | Evidence and interpretation |
|---|---|
| Constant/affine reference misalignment | Existing fits use only seconds 0..6, score 6..22; replayed variants do not demonstrate a universal correction. |
| Tail capacity | The new simulated 300 ms tap exceeds the configured 200 ms filter; no universal 500 ms default is inferred from it. Earlier longer-tail room regressions remain. |
| Nonlinearity / path movement / delay | Separate cubic, moved-path and 100 ms-delay simulations retain failures; simulations do not identify actual driver/loudspeaker causes. |
| NS as a residual suppressor | Some far-only attenuation/counts improve, but near-only recognition regresses; reject blanket stronger suppression/default change. |
| Clock drift / microphone gain / driver processing | Physical provenance remains insufficient to isolate these causes; waveform lag slopes are not oscillator measurements. |

No new physical recording or calibrated speaker/microphone route was obtained.
The existing public-Hail acquisition tooling and physical protocol remain usable.
Real-room AEC robustness is still a production blocker, not closed by this fix.

## Recovery and signal telemetry

The existing simultaneous epoch/generation correction remains intact. Default
capture/reference epoch, generation-only cancellation, immediate restart and
reference-loss cases retain identical PCM and recognition in the before/after
matrix. Playback-only transitions do not reset near-leg NS/AGC/VAD; capture epoch
change discards one pending NS hop and reports it. Sample-rate changes still
require explicit compatible context recreation; no hidden reconfiguration.

`acoustic_recovery.py` retains 100 ms far-only energy curves and coarse post-event
settling against the existing synthetic 20 dB unit target: two excited one-second
windows, reported at window end. This is **posthoc diagnostic resolution**, not
runtime buffering, a calibrated convergence state or a new physical threshold.
Unreached and unexcited conditions stay explicit. Cancellation ends excitation,
so no convergence time is invented. Digital tails use a declared 1/32768 FS
meter threshold, include NS delay, and do **not** establish acoustic/audible
silence or backend flush latency. Input/reference/output PCM and event metadata
remain separate from Hail's queue invalidation and flush acknowledgement.

The simulated cancellation tail is 80 ms at the microphone and approximately
200 ms after AEC / 210 ms after full processing at that digital threshold. For
the moved path, the coarse 20 dB settling diagnostic reaches 10 s for AEC and
8 s for AEC+NS; full processing never reaches it in the retained interval.
After immediate generation restart the respective values are 5 / 4 / 10 s.
These results retain slow recovery rather than treating successful API calls as
acoustic recovery. The earlier floating-grid diagnostic draft is retained as
`recovery.json`; `recovery-final.json` uses complete integer-frame windows.

The [API signal-validity contract](api.md#signal-interpretation-and-validity)
documents existing RMS, VAD heuristic, limiter/clipping, missing-reference,
generation/reset and discontinuity signals. AEC input/output RMS includes near
speech and noise; it is not isolated echo energy. Current-input AEC telemetry and
delayed-output AGC/VAD telemetry must not be combined as though sample-aligned.
Inactive/priming/flush records have explicit interpretation. No new fields are
needed for levels available from public PCM; no unvalidated convergence or
`is_user_speaking` signal is added. Forty-five exports and record layouts remain
unchanged, without freezing the ABI.

## Resources, verification and remaining work

Fresh CPU-only trials use 10 ms mono capture/reference at 16 and 48 kHz, six
30-second inference configurations per rate, then simultaneous Whisper+Parakeet+
Piper for **300 s / 120 s**. All **78,000 measured calls** have zero observed
execution/completion misses against the existing 10 ms frame budget.

| Combined load | P50 / P95 / P99 / max execution, ms | DSP workspace |
|---|---|---:|
| 16 kHz, 300 s | .07205 / .11010 / .13680 / .32420 | 136,832 bytes |
| 48 kHz, 120 s | .20060 / .29890 / .38680 / .72020 | 381,312 bytes |

Execution, release lateness, declared **10 ms** NS delay, sampled CPU/RSS/GPU and
modeled queue occupancy remain distinct. Workspace includes preallocated DSP
working storage; offline files/receipts/models are separate. These finite trials
are not a deployment soak, live capture-to-ASR measurement or speedup claim.
ASR is rolling offline re-decoding and Piper sentence incremental, not native
streaming or a complete conversational runtime. No GPU operation or dependency
is introduced in DSP. No mandatory Hail/Hull/inference dependency is added.

The C correction changes only first-spectrum noise initialization; steady-state
computation, arenas, queues, threading and algorithmic delay remain. Single-owner
context rules still prohibit reset/process/destruction races. Available checks
include Windows static/shared core 7/7 and installed Hail 3/3, ARM64 ASan/UBSan and
TSan 8/8, installed-public-tool sanitizer smoke, 30,000 pipeline and 30,000 transport
and 10,000 AEC fuzz iterations, GCC analysis and MSVC installed-tool smoke.
The final implementation/test source `387eca5` completed all 27 cross-platform
CI jobs in run `44840433515`; documentation/evidence revisions are recorded
separately. A shared SDK directory had tests disabled; that empty run is retained
and is not counted as a pass. The actual test-enabled shared suite passed 7/7.
Hail source is unchanged. Passing sanitizers/CI does not prove acoustic reliability.

**Gate I: PARTIAL.** The frozen-learning contract and its fidelity are corrected,
without a default PCM/ASR change in the retained matrix. Default quiet-speech
failures, residual assistant transcripts, real-room robustness and independent
phone-level/physical/live evidence remain open. Do not promote reversed ordering,
stronger suppression or a model-specific gain trick from these results. Broader
untuned speech/room holdouts and a validated inference frontend remain necessary.
