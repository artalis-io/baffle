# Real-world full-duplex convergence, 2026-11-20

**Verdict: PARTIAL. Production readiness and ABI stabilization remain open.**
This work adds executable acquisition, replay, reviewed-onset admission and actual
inference contention tools. It does not change the DSP core, public headers,
Hail adapter, default AEC or original acceptance fixture. The measured tradeoffs
do not justify changing a default algorithm. PR #1 remains draft and unmerged.

Evidence: [retained receipts](evidence/2026-11-20-real-world/README.md), including
raw call timings, failures, model/fixture hashes and exact executed commands.
The previous [AEC correction report](aec-quality.md) remains historical evidence.
Acquisition/reproduction instructions are in the
[external tools guide](../tools/acoustic-runner/README.md).

## Baseline verification

The inspected baseline is `86feaf956543de06565551591ee3abe6db28ceb8`.
Its completed CI run `44755515542` passed all 27 jobs. Replaying the unchanged
controlled fixture reproduced near-component SNR improvement **14.603375 dB**
against the unchanged **8 dB** threshold, projection **0.988724**, and far-only
ERLE **25.831140 dB**. The historical **6.648426 dB FAIL** remains retained; this
task did not produce the earlier DC-rejection correction. Near-component SNR
improvement is not ERLE. The original fixture source SHA256 remains
`fa6531a93ab34b138fd8931d6cfb7f6c0acb8c54d4acfb89ead7c2c2b5ca4225`.

The reported 0.9612–0.9811 holdout projections, stereo-reference workspaces
204,896/596,576 bytes and 16/48 kHz AEC P99 0.0635/0.1557 ms are prior retained
measurements, not newly executed inference-contention trials. The new paced
pipeline has mono render reference and includes noise suppression/AGC/VAD;
its timing and workspace must not be compared as the same configuration.

## Real recordings and controlled root-cause experiments

Four fixed Microsoft AEC Challenge real test recordings remain separate from
synthetic mixtures. Source pin, Git LFS checksums and dataset terms are retained
in the manifests; source/derived audio is not redistributed. Three have far-only
intervals; the double-talk recording has no clean near oracle and cannot establish
speech preservation. Both backends receive identical original PCM at native
48 kHz or identical externally resampled 16 kHz. Evaluation stays at seconds
6..22, with no favorable segments removed.

At 48 kHz, the original-reference far-only microphone/output energy ratios are:

| Recording prefix | Baffle, dB | Full WebRTC AEC3, dB |
|---|---:|---:|
| 00113069 | 0.80 | 23.06 |
| 00dPPLGk, moving path | -0.01 | 3.03 |
| 02462e78 | 6.60 | 31.36 |

This ratio is apparent far-only ERLE; ambient noise and nonlinear processing are
included. It does not isolate linear echo power or establish perceptual quality.
AEC3's moving-path result remains weak. No new physical room measurement was
made on this host.

Experiments isolate one factor at a time:

* **Fixed delay:** six one-second windows in training seconds 0..6 estimate a
  robust fixed lag. Shifting only the reference generally worsens the held-out
  results. Simple constant misalignment is not demonstrated as the sole cause.
* **Time-varying lag:** the same training windows fit an affine lag; held-out
  data do not determine the correction. Equivalent slopes are about 234, 6938
  and 125 ppm, with weighted coherence 0.486, 0.015 and 0.154. The moving-path
  case's very low coherence makes oscillator attribution especially unsupported.
  These are waveform diagnostics, **not measured device clock drift**.
* **Filter capacity:** changing only the 200 ms tail to 40 or 500 ms does not
  improve all rooms. At 16 kHz, 40/500 ms yield 5.18/0.61, 0.00/-0.06 and
  6.93/7.85 dB respectively. A longer tail is not a demonstrated universal fix.
* **Linear cancellation versus suppression:** separately patched AEC3 linear
  output at 16 kHz yields about 4.51/0.52/5.98 dB, while full output gives
  23.91/3.34/40.07 dB. Much of the full-output advantage comes after linear
  cancellation. It must be assessed with near-end losses, not far-only scores.
* **Clock/drivers/nonlinearity:** recordings lack independent clocks, room
  calibration, exact loudspeaker chain and processing settings. Reverberation,
  nonlinear response, noise and motion remain plausible limitations, not
  individually confirmed hardware defects. No unsupported normalization or
  drift-compensation change was added.

Implementation references are the existing AEC alignment/adaptive-state paths in
`src/aec.c` and `src/speex/mdf_baffle.c`, and the new external analysis in
`tools/room_replay.py`.
The experiment does not establish one dominant physical cause; Baffle's real-room
gap is confirmed, but its causal decomposition remains incomplete.

## WebRTC AEC3 comparison and backend decision

The actual comparison uses official `webrtc-audio-processing` **2.1**, GCC 13
Release/NEON on Linux ARM64, mono 10 ms frames. APM high-pass, NS and AGC are
disabled; AEC3 residual suppression remains enabled. No GPU is used. Package,
Abseil, build logs and hashes are retained. System 0.3.1 was not mislabeled AEC3.

An independent near-impulse probe observes full-output peak delay of 128 samples
at 16 kHz (8 ms) and 430 at 48 kHz (8.96 ms); Baffle direct AEC is zero for that
probe. Raw common-index scores remain in the report. A second score compensates
only that independently measured peak delay, without optimizing delay on speech:

| Controlled double-talk | Baffle | AEC3 full | AEC3 linear diagnostic |
|---|---:|---:|---:|
| 16 kHz near-component improvement, dB | 11.66 | 6.77 | 10.08 |
| 16 kHz near projection | 0.984 | 0.820 | 0.954 |
| 48 kHz near-component improvement, dB | 14.60 | 5.64 | not measured |
| 48 kHz near projection | 0.989 | 0.759 | not measured |

These supplemental scores do not replace the original test or impose its 8 dB
threshold on a different configuration. Peak delay is not a full group-delay
characterization. Raw unaligned AEC3 SNR scores are negative largely because of
output delay and are retained rather than presented as an algorithm failure.

On the controlled input, Baffle/AEC3 P99 execution is 0.0269/0.1538 ms at 16 kHz,
0.0894/0.2207 ms at 48 kHz. Coarse initialization RSS deltas are 504/1576 KiB
and 728/1812 KiB, respectively; these are process-page measurements, not exact
context footprints. The selected call-allocation probe observed zero
malloc/calloc/realloc calls after initialization, with its scope limitations.

Upstream APM 2.1 linear-output export crashes in `BlockFramer` because its export
flag does not reach `EchoCanceller3Config`. The retained backtrace reproduces
this independently of the allocation probe. A one-line patch in a **separate
external build** enables the diagnostic; full AEC3 results use the original
unpatched build. Patched linear output adds a measured 4 ms peak delay at 16 kHz.
This is an upstream comparison-path defect, not a Baffle processing defect.

**Decision: retain the existing backend.** Full AEC3 materially improves far-only
recordings, but default near-end preservation is weaker, its measured delay is
material to a conversational workload, and APM capture/render paths acquire
blocking mutexes. A direct adapter would violate Baffle's nonblocking,
caller-arena contract. An independently audited AEC3-core adapter would require
additional work; the current evidence does not justify making it mandatory or
shipping it as a qualified optional backend. BSD licensing permits evaluation;
maintenance, Abseil/C++ build complexity and upstream patching remain costs.

## Speech, interruption and cancellation evidence

Pinned provider-transcript human English speech is mixed with actual Piper TTS
through a clearly labeled simulated 3/20/80 ms FIR. These are not room recordings.
Five cases cover normal double-talk, quiet near speech, speech through playback
cessation, render-epoch flush and immediate new generation. Capture epoch stays
unchanged. The FIR tail continues after reference cessation; cancellation is not
modeled as immediate acoustic silence. AEC3 refuses Baffle lifecycle events
rather than claiming an equivalent unimplemented adapter.

Unseeded Piper initially generated different far waveforms on later preparation.
Both earlier versions and their failed results are retained. Preparation v2 uses
an explicit ONNX random seed, immutable cached fixtures and complete hashes;
two fresh preparations matched. Cross-version passing runs are not DSP fixes.

On the reproducible seeded fixture:

| Case | Whisper Baffle / pipeline WER | Parakeet Baffle / pipeline WER |
|---|---:|---:|
| normal | 0 / 0 | 1.000 / 1.000, blank |
| quiet near | 0 / 0 | 0 / 0 |
| through cancellation | 0 / 0 | 0 / 0 |
| render epoch flush | 0 / 0 | 1.000 / 1.000, blank |
| immediate new generation | 0 / 0.056 | 0.111 / 0.111 |

Default AEC3 Whisper WER is 0.333/0.278/0.111 on the first three cases; its
Parakeet WER is 0.222/0.278/0.000. No conclusion that either backend universally
preserves speech follows. In an earlier unchanged cached fixture, direct Baffle
also produced blank Parakeet outputs while Whisper read the same speech. Fresh
decoder controls reproduce the blank; whole-window gain reduction changes the
recognition, and removing only pre-speech context changes one case. This is
confirmed model-input sensitivity, not stale recognizer state. No fixed gain
workaround or tuned AGC default was introduced to pass one recognizer.

Known-oracle cancellation windows retain the difficult first 80 ms. Direct AEC
near projection across a generation-preserving tail is about 0.979; an explicit
render epoch reset has a transient error increase, and immediate restart yields
about 0.748 projection during that window. Resetting the reference epoch
deliberately retrains adaptive state; a generation-only transition preserves the
path. Full-pipeline raw projections include intentional AGC amplification and
cannot be read as unity-gain AEC distortion. The window reports retain raw energy,
projection and error SNR, with no invented quality threshold. Tail/restart
robustness is a remaining quality risk, not a silent software flush guarantee.

The standalone installed-Hail acquisition tool tests virtual flush/stop and
flush/immediate-restart during continuing capture. Three-second runs retain
144,000 frames per leg, no capture/reference loss, unchanged capture epoch,
new render generation and native-flush acknowledgement. The existing installed
consumer's 3 tests also pass and cover independent clocks/rates, drift,
discontinuities and bounded ownership. Full-width timing validity, uncertainty,
frame positions, domains and epochs are retained by acquisition. Hail source and
adapter contracts remain unchanged. Physical backend flush, device presentation
and acoustic tail remain separate, unmeasured here.

**First-phoneme/syllable preservation remains UNQUALIFIED.** Word recognition and
projection are not substitutes for independently reviewed onset/phone/syllable
labels. The new admission/scoring tool requires those labels, reviewer evidence,
uncertainty, processed-audio review and downstream-approved VAD-delay/WER limits.
The supplied unreviewed template correctly refuses PASS. No application limits
were supplied, so none were invented. Quiet consonants, vowels, different
distances, Hungarian and calibrated real-room interruption remain outstanding.

## Actual inference contention

Windows host: Ryzen 9 5950X, 16 cores/32 threads, Windows 11 Pro build 26300,
128 GiB class memory, RTX 3090 Ti present. Baffle and tested Whisper/Parakeet/Piper
use **CPU**. Optional QA uses Python 3.12, sherpa-onnx/core 1.12.40, Piper 1.3.0,
Piper ONNX Runtime 1.23.2, two inference threads per worker, model pins/checksums
in `model-identities.json`. Baffle is clang-cl 19.1.5 Release `/O2 /Ob2 /DNDEBUG`.
Sherpa uses its bundled runtime; do not infer that it is Piper's ORT version.

Full AEC→NS→AGC→VAD pipeline, 16 kHz mono capture/reference, 160-sample calls:

| Actual concurrent work | Duration | DSP P50 / P95 / P99 / max, ms | Wake-lateness P99, ms | Completion misses |
|---|---:|---|---:|---:|
| none | 30 s | .0590 / .0854 / .1050 / .2028 | .534 | 0 |
| Whisper tiny.en int8 | 30 s | .0637 / .1041 / .1263 / .2443 | .571 | 0 |
| Parakeet TDT 0.6b v3 int8 | 30 s | .0685 / .1143 / .1596 / .3226 | .532 | 0 |
| Piper Lessac low | 30 s | .0621 / .1130 / .1620 / .3395 | .542 | 0 |
| Whisper + Piper | 30 s | .0708 / .1112 / .1337 / .1870 | .580 | 0 |
| Parakeet + Piper | 30 s | .0631 / .0932 / .1128 / .1746 | .527 | 0 |
| Whisper + Parakeet + Piper | 120 s | .0755 / .1146 / .1450 / .3173 | .532 | 0 |

The 10 ms completion deadline includes release-to-completion lateness; execution
misses are separately zero. Context workspace is **136,832 bytes** for this mono
pipeline, plus bounded preallocated harness receipts/input files. NS adds an
explicit **10 ms buffering delay**, independent of the sub-millisecond execution
cost. No claim that buffering is free follows from high throughput.

Whisper and Parakeet run actual fresh **3 s rolling offline windows every 0.5 s**,
with bounded pending requests, not native streaming. Piper produces sentences
incrementally with a modeled one-sentence drain. Raw reports retain first-audio,
partial-result latency, dropped requests, CPU/RSS/GPU observations and modeled
queue occupancy. They are not audio-device occupancy or capture-to-partial
latency. No live Hail→DSP→ASR→TTS conversation was measured.

An initial standard Windows sleep run had about 15.5 ms wake-lateness P99 and
many completion misses despite low DSP execution time. This failure is retained.
A private high-resolution waitable timer fixes harness pacing without global
timer/priority/power changes. A separate Piper UTF-8 configuration-read failure
is retained and corrected in external setup. Neither was a DSP race or CPU
deadline failure. The finite corrected runs are observations, not OS guarantees.

On the available Linux ARM64 Spark, an existing GPU vLLM service serving
`qwen38-fp4` ran bounded streaming requests without reconfiguration. Version is
`0.1.dev21830+g8244d1c44.d20261108`; configured modelopt mixed weights, BF16
compute and FP8 KV cache are service configuration, not an independently audited
model precision claim. Full-pipeline P99 increased **0.0776→0.1233 ms**, with
zero completion misses in two 30 s trials. Raw server usage, first generated
chunk times and GPU observations are retained. This was actual LLM inference,
not ASR/TTS on that machine, and not the entire conversational stack. No RTX PRO
6000 or Apple Silicon inference-performance measurement is claimed.

## Correctness, real-time safety and platform evidence

No new path enters the core processing calls. Contexts retain a single processing
owner; reset/process/destruction must not race. Native external processing uses
preallocated bounded input/scratch/receipts, no file I/O inside timed calls and
no hidden inference waits. Offline control loops have file/call limits; acquisition
caps duration and queues and retires streams before file export. The full AEC3
APM is explicitly excluded from Baffle's nonblocking core contract.

New tool defects corrected with executable regressions include final pipeline
EOS loss, reading capture after Hail STOPPED, retained VAD-output overwrite,
partial-byte PCM admission, Windows near/far SDK macro collisions, setup-only
environment access and MSVC float initialization warnings. Invalid input must
fail rather than produce a misleading result. Python analysis is not a
real-time processing path. No SIMD or algorithm tuning was added without need.
The contention harness also checks each final worker receipt and exit status;
a worker that becomes ready and subsequently fails cannot qualify a trial.
Late-failure and empty-inference receipts are regression-tested without models.

Locally executed: Windows Release core **7/7**, installed Hail **3/3**; external
installed-header tool smoke under clang-cl and MSVC 19.44; Linux ARM64 core
**8/8 ASan/UBSan and 8/8 TSan**, plus the new external tool smoke against each
instrumented installed SDK; **10,000 AEC fuzz iterations** and a rebuilt GCC
analysis configuration. Selected allocation probes observed zero calls. These
checks neither prove deadlines nor cover every upstream WebRTC path. The separate
upstream linear-export crash remains documented, with a scoped diagnostic patch.

Linux x86-64 and macOS ARM64/x86-64 verification uses remote CI, not local hardware
qualification. The 27-job matrix covers portable/AEC static/shared builds,
installed consumers, Hail, sanitizers, fuzzing and analysis. New model-free tool
smoke is included; models/WebRTC/room downloads are excluded. Baseline CI is
complete; subsequent run status is recorded separately in the evidence ledger,
including canceled superseded runs and the fixed Windows tool build failure.

Public structures, 45 exported functions, caller ownership, integer widths,
version negotiation and error semantics remain unchanged. There is no ABI
freeze. Remaining compatibility and qualification risks in [API](api.md) still
apply; no new performance result removes them.

## Acceptance and deployment blockers

| Requirement | Verdict | Reason |
|---|---|---|
| Original unchanged 8 dB acceptance | PASS | 14.603375 dB reproduced |
| Bounded CPU transport/DSP contracts | Verified within stated tests | Core unchanged; installed consumer, sanitizer, fuzz evidence |
| Recorded room echo robustness | PARTIAL | Weak Baffle results, moving path weak in both; physical causes unresolved |
| Backend choice | Retain existing | AEC3 far-only gain trades against near speech, delay and blocking APM |
| Actual inference contention | PARTIAL | Real CPU ASR/TTS and separate GPU LLM; finite replay trials |
| Word recognition under interruption | PARTIAL | Blank hypotheses/restart errors retained across recognizers |
| First phone/syllable preservation | OPEN | Independent reviewed labels and application limits missing |
| Physical full-duplex/cancellation tail | OPEN | No calibrated route/room recordings acquired on host |
| Connected conversational latency | OPEN | No complete live inference/device chain measured |
| ABI stability / production readiness | OPEN | Quality, physical, duration and compatibility risks remain |

Before deployment: acquire independently calibrated and annotated speaker–mic
sessions in quiet/noisy/reverberant rooms, loud/quiet levels, distances and moved
paths; include Hungarian and low-energy onset cases; establish downstream VAD/WER
limits; investigate repeatable Parakeet blank cases without backend-specific
benchmark tuning; quantify epoch-reset/restart tails; run a connected live
ASR/TTS/LLM pipeline, longer multi-session/48 kHz contention trials and power
measurements on each target. Do not infer audible silence from flush ACK, timing
accuracy from waveforms, or production readiness from green CI.
