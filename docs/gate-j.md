# Gate J: bounded residual-echo trade-off, 2026-11-21

**Decision: retain the baseline. Gate J: PARTIAL.** The fixed coherence candidate
fails both assistant-only and critical speech-preservation requirements. It is
retained only as an external experiment, never selected by Baffle's library or
public API. No production DSP source, public record, default order, NS delay,
mandatory dependency or Hail source changes. The speech runtime must also handle
transcript commitment and assistant/user attribution; VAD/RMS are not that policy.
This does not make acoustic suppression unnecessary or establish production readiness.

## Baseline and method

Baseline `34a3345` passed all 27 CI jobs in run `44843287527`. Fresh replay of
the unchanged original test reproduces **14.603375 dB** near-component SNR
improvement against **8 dB** and separate far-only ERLE **25.831140 dB**.
Fresh fixed-model assistant-only decoding reproduces full-pipeline Whisper
**11/11, 426 inserted words** and Parakeet **9/11, 392 words**. This is different
from the original synthetic metric and from transcript commitment.

Gate I's 16/48 kHz P99 .1368/.3868 ms, 48 kHz maximum .7202 ms and zero observed
misses over 78,000 calls remain historical finite-trial measurements. Fresh
paired resource trials below do not replace them or expect identical wall times.
The installed Hail interface and provisional ABI are unchanged.

The [fixed protocol](gate-j-protocol.md) precedes candidate scoring; no parameter
search followed failures. A is existing AEC→NS→AGC→VAD. B adds an isolated
reference-aware coherence mask **between AEC and NS**, using the estimated removed
echo `microphone−AEC` rather than assuming direct render/microphone zero lag.
Smoothed cross/auto spectra estimate C and use `sqrt(max(0,1−C))` amplitude gain.
Uncorrelated near speech is expected to reduce coherence, not guaranteed safe.
Statistics are 100 ms, admission 20 blocks, gain attack/release 5/50 ms. There is
no tuned gain floor or blanket playback-activity attenuation. Missing/inactive
reference bypasses and clears spectral state. Per-block FFT/IFFT adds **zero
queued frames**, but rectangular blocks can introduce boundary/spectral distortion.
The 200 ms admission is adaptation history, not buffered output latency.

The split public-API control with mask disabled exactly matches baseline PCM on
all **28 speech + 11 assistant-only cases**, including capture epochs, reference
loss, cancellation and generation restart. That prevents attributing a changed
AEC or pipeline configuration to the postfilter. No third candidate was justified:
stronger NS and reversed ordering already showed unfavorable Gate I trade-offs.

## Failure modes and assistant-only outcomes

The [per-case evidence](evidence/2026-11-21-gate-j/README.md) includes microphone,
render, AEC, full and candidate power/peaks, spectral bands and hashes, VAD traces,
both hypotheses, AEC reset counts, flags and explicit unavailable timing. All
11 scenarios and complete registered source intervals remain; no favorable
segment selection or transcript filtering is applied.

| Far-only simulation | AEC mic/output, dB | A full, dB | B full, dB |
|---|---:|---:|---:|
| Stationary base | 38.40 | 22.95 | 24.30 |
| Quiet reference | 24.68 | 7.23 | 9.42 |
| Long tail (300 ms tap vs 200 ms filter) | 12.96 | -3.67 | -3.43 |
| Moved path | 6.14 | -2.08 | -2.31 |
| Startup | 13.35 | -.15 | 2.67 |
| Immediate restart | 15.08 | 2.70 | 5.77 |

These known-far-only power ratios include all residual/noise and downstream gain;
they are not clean-near preservation metrics. AGC can restore residual energy:
the stationary case's 38.40 dB AEC ratio becomes 22.95 dB in A. Recognizable
speech can survive high attenuation; energy alone does not predict ASR rejection.
Long tails/path changes dominate some failures, while the whole-window Welch
coherence of removed echo and residual is only about .003/.006 there. A short
coherence mask does not explain away nonstationary/delayed residuals. This is a
diagnostic interpretation, not proof of an independently identified physical
driver or oscillator defect. Nonlinear and added-delay simulations remain distinct
from actual room recordings. Noise, distortion and unknown clock/driver provenance
prevent uniquely attributing the real recordings to those simulated factors.

| Fixed 11 cases | A nonempty / words | B nonempty / words |
|---|---:|---:|
| Whisper | 11 / 426 | 11 / 479 |
| Parakeet | 9 / 392 | 10 / 386 |

B fails the predeclared incidence targets (≤5 Whisper, ≤4 Parakeet) and Whisper
word-count nonregression. Slightly better echo power and fewer Parakeet words
do not compensate. All far-only references are empty: WER/CER are undefined;
recognized words are **not committed words**. No conversation runtime is present,
so false committed-word counts remain null rather than invented zero.

## Speech preservation and cancellation

Fixed Whisper tiny.en / Parakeet TDT v3 int8 CPU models, versions, decoder settings,
transcripts and complete intervals are unchanged from Gate H/I. Near-only uses
six source/level cases. Double-talk uses six quiet/normal/loud cases.

| Group | Whisper A→B errors / words | Parakeet A→B errors / words |
|---|---:|---:|
| Near-only | 10→10 / 198 | 3→3 / 198 |
| Double-talk | 9→9 / 198 | 127→88 / 198 |
| Early/cancellation/restart/epoch/reference loss | 29→29 / 396 | 56→53 / 396 |

**Per-case failures override pooled improvements.** B adds two Whisper word errors
on source0 capture epoch and one on source0 missing reference. Parakeet adds one
on source0 render restart and **16** on source0 missing reference, including a
new blank and first-word loss. Thus the hard preservation requirement fails even
though pooled Parakeet double-talk improves. CER, edits, first words, individual
hypotheses, VAD activity, near projection and gain-adjusted spectral diagnostics
are retained. Near-only bypass is bit-identical when reference PCM is inactive;
this does not guarantee preservation during reference-active double-talk.

AEC timing/generation transitions clear the candidate estimator; missing reference
bypasses it. Near-leg NS/AGC/VAD is not reset by reference-only changes. Different
preceding PCM can still change that near-leg adaptive history and later recognition
after bypass. Passing reset/determinism tests does not establish successful ASR
recovery. Capture continues; no logical flush is claimed to stop acoustic tails.
The existing adapter supports independent clocks/rates. This experiment consumes
declared same-rate aligned PCM only; it does not estimate clock drift or silently
compare different clocks. Rate changes require context recreation. No clock-reset
or real device recovery acquisition was added.

Independent phone/syllable labels, low-energy consonant review and Hungarian
qualification are still unavailable. Projection/correlation and first-word scores
cannot substitute for those labels. Their absence alone prevents a production
onset PASS; the observed regressions already reject B without that evidence.

## Real-room comparison

Three previously retained real far-only recordings at 16/48 kHz are evaluated
after freezing B, with identical 6..22 s intervals and only documented NS delay
compensation. They are **non-tuning comparisons, not new blind rooms**. No new
physical acquisition, calibrated playback level, driver processing or clock
measurement is claimed. Original/fitted-alignment replays remain separate.

| Recorded comparison, six conditions | A nonempty / words | B nonempty / words |
|---|---:|---:|
| Whisper | 6 / 228 | 6 / 235 |
| Parakeet | 4 / 179 | 4 / 180 |

Both word-count comparisons worsen. Energy attenuation in selected rooms cannot
justify promotion when recorded assistant-only recognition does not improve.
Available room sources include a moved path, but lack enough calibrated provenance
to isolate loudspeaker nonlinearity, reverberation, gain and oscillator effects.
Neither the existing English pair nor previously inspected rooms are an independent
new holdout; generalization claims are intentionally limited.

## Resources, verification and decision

Eight fresh 30-second paired alone/combined CPU Whisper+Parakeet+Piper trials
contain **24,000 ten-millisecond calls**, with zero observed completion/execution
misses. Complete timing receipts are retained in the evidence ledger.

| Combined CPU inference | P50 / P95 / P99 / max, ms | Planned workspace |
|---|---|---:|
| A, 16 kHz | .07420 / .11350 / .13791 / .22720 | 136,832 B |
| B, 16 kHz | .09110 / .13900 / .16630 / .22940 | 147,984 B |
| A, 48 kHz | .19335 / .28341 / .35691 / .50870 | 381,312 B |
| B, 48 kHz | .21835 / .32141 / .38072 / .49920 | 403,984 B |

B costs about 20.6% / 6.7% more P99 in these paired trials and 11,152 / 22,672
additional planned bytes, with zero added queued frames and unchanged 10 ms NS
delay. Planned state/scratch counts exclude allocator overhead; sampled process
RSS includes the executable/runtime and is not the DSP arena size. No isolated
kernel or inference-latency speedup is inferred from these short measurements.
Execution, scheduling,
sampled process CPU/RSS, declared NS delay and modeled queue occupancy are separate.
The candidate uses bounded preallocated spectral arrays and a separate public AEC
context; it adds no buffered hop or mandatory GPU work. A 200-call ARM64 allocation
probe observes zero processing allocations. This is finite evidence, not a proof
of every platform's deadline behavior. Single-owner contexts prohibit concurrent
reset/process/destruction.

Source verification covers zero-reference bypass, split PCM identity, finite
partial EOS and deterministic epoch/generation/reference-loss transitions at
16/48 kHz, ARM64 ASan/UBSan and TSan plus unchanged core suites and installed Hail.
The CI workflow indentation mistake and strict MSVC int-to-float template warning
are retained as failed tooling evidence. Typed float literals correct the latter
without suppressing warnings or changing the algorithm. Numerical fuzzing/static
analysis of the unchanged core is distinct from the new runner's deterministic
and sanitizer coverage; it does not imply candidate-specific fuzz coverage.

No new runtime telemetry fields are required: existing PCM, reference availability,
generation/reset flags, VAD heuristic and levels suffice for the experiment.
Internal coherence is not a calibrated AEC reliability or user-speech probability.
The unsuccessful candidate stays in optional external QA and is never installed
as a Baffle backend. Do not adopt a stronger mask or tune on these evaluation
cases to rescue it. Continue the separate speech runtime with explicit commitment
and attribution policy, while retaining acoustic and live-conversation blockers.

**Gate J: PARTIAL; B rejected, A retained.** The bounded comparison establishes a
negative engineering decision, not a claim that residual suppression is solved
or that no better algorithm exists. Production acoustic readiness remains open.
