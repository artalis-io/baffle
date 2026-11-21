# External acoustic qualification

This separate C/C++17 project links installed public Baffle headers/libraries.
Optional Hail acquisition and WebRTC comparison do not become core dependencies.
Python, models, WebRTC and raw recordings are external qualification tools only.

```sh
cmake -S . -B build-aec -DCMAKE_BUILD_TYPE=Release -DBF_WITH_SPEEXDSP=ON -DBF_BUILD_TOOLS=ON
cmake --build build-aec --parallel
cmake --install build-aec --prefix "$PWD/installed"
cmake -S tools/acoustic-runner -B acoustic-tools -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/installed"
cmake --build acoustic-tools --parallel
python3 tests/acoustic_tools.py acoustic-tools/baffle_acoustic_runner acoustic-tools/baffle_vad_trace
```

On Windows use `--config Release` and executables in `Release/` with Visual Studio;
put installed DLLs on PATH for shared builds. With Ninja select both C and C++
compilers explicitly. Test static/shared separately. Optional
`-DBF_QUALIFY_HAIL=ON` requires installed Hail and adds `baffle_hail_acquire`.
Pass its path as the last smoke-test argument for virtual cancellation checks.

## Replay and pacing

```
baffle_acoustic_runner BACKEND RATE MIC.f32 REF.f32 OUT.f32 CALLS.csv SUMMARY.json [SECONDS [default|highres]]
```

BACKEND is `baffle` (AEC only), `pipeline` (AEC, NS, AGC, VAD), `aec3`, or
diagnostic `baffle-40ms` / `baffle-500ms`. Inputs are little-endian float32 mono,
same length/rate, maximum 256 MiB each, supported rates 8/16/32/48 kHz. Calls are
10 ms with terminal zero padding trimmed. Pipeline offline output includes a
10 ms zero priming prefix and the flushed final real samples: N+RATE/100 frames.
This explicit delay must be included in scoring. Paced output is not exported.
The pipeline smoke test checks the last real impulse survives a partial ending.

Paced runs preallocate receipts, warm 100 silent calls, then release at 10 ms
intervals. CSV separates execution, release/start lateness and completion deadline
misses. Windows `highres` selects a private high-resolution waitable timer;
it does not change system timer resolution, process priority or power policy.
Default standard sleeps are retained, including their failed deadline evidence.
Peak delay from an independent impulse is a diagnostic, not device latency or a
claim of constant broadband group delay. Files, RSS queries and exports occur
outside processing. The Linux preload probe counts malloc/calloc/realloc during
selected calls; it does not observe frees or every allocator and proves no
universal real-time guarantee.

Offline `BF_QUALIFICATION_TRANSPORT_EVENTS` names up to 128 ordered frame-aligned
records `frame capture_epoch reference_epoch generation missing`. It is a test
control, not a new public library API. AEC3 comparison refuses these Baffle
transport events rather than pretending to implement equivalent lifecycle rules.

## Room recordings and independently calibrated scores

Review source dataset terms before downloading. Do not publish source or derived
audio without appropriate rights. The original controlled acceptance is unchanged.

```sh
python3 -m pip install -r tools/requirements-qualification.txt
python3 tools/recorded_aec.py build-aec/baffle_qualify build-recorded recorded.json --fetch
python3 tools/controlled_aec.py build-aec/baffle_qualify build-recorded controlled.json
python3 tools/room_replay.py prepare build-recorded rooms
python3 tools/room_replay.py replay acoustic-tools/baffle_acoustic_runner rooms results --backends baffle
```

The source pins/hashes are in the manifests. Fixed offset and training-affine
variants fit only seconds 0..6; scoring remains 6..22. Waveform-lag variation is
not an oscillator measurement. `calibrated_backend_scores.py --help` supplies a
second, independent impulse-peak compensated score while retaining raw scores.
Never substitute the new score for the original 8 dB acceptance fixture.

## Optional WebRTC AEC3 comparison

Use official `webrtc-audio-processing` 2.1 (BSD-3-Clause), SHA256
`ae9302824b2038d394f10213cab05312c564a038434269f11dbf68f511f9f9fe`, from
https://freedesktop.org/software/pulseaudio/webrtc-audio-processing/.
Build with Meson Release and install to a separate prefix. Our ARM64 comparison
used GCC 13, Abseil 20240722.0 fallback and NEON. Set PKG_CONFIG_PATH and the
runtime library path for that prefix, then configure this project with
`-DBF_COMPARE_WEBRTC=ON`. Older system 0.3.1 is not AEC3. Both backends receive
identical 10 ms mono float PCM; APM high-pass, AGC and NS are disabled. The full
AEC3 output includes its residual suppression. Compare near-end preservation,
cost and delay as well as far-only attenuation.

The 2.1 linear-export path crashes because APM does not propagate its export
setting to AEC3. `webrtc-2.1-linear-output.patch` is a one-line diagnostic fix for
a **separate** upstream source/build/install. Apply using `git apply` in that
source directory, then build this tool in another directory against its pkgconfig
with `BF_COMPARE_WEBRTC_LINEAR=ON`. Only that executable admits `aec3-linear`.
Never enable linear export against the unpatched package. Retained backtrace and
patch distinguish patched linear results from the main unpatched comparison.

The tested APM uses blocking mutexes and internal allocation during setup; it
does not meet Baffle's caller-arena/nonblocking contract as a direct replacement.
No WebRTC backend was added to the core. No GPU path is used.

## Actual inference contention and transcript fixtures

`inference_contention.py --help` expects externally downloaded pinned Whisper,
Parakeet and Piper models in the directory layout recorded in the evidence
manifest. It uses CPU providers with two inference threads, 3 s rolling offline
ASR windows every 0.5 s, and sentence-incremental Piper. These are actual inference
calls, **not native streaming recognizers or a connected audio conversation**.
Late ASR requests drop older pending windows; modeled TTS occupancy is not a
device queue. Run `--seconds 30 --wait-policy highres` on Windows, then
`--seconds 120 --workloads whisper+parakeet+piper` for a combined trial.
`llm_contention.py` sends bounded streaming requests only to an existing local
server; it does not deploy/reconfigure services. Tokens and generated chunks
are reported separately. Inference licenses apply independently of Baffle.

`word_qualification.py prepare MODELS DIR` creates a seeded, immutable fixture
from a pinned human-speech WAV with provider word transcript and real Piper TTS
through a simulated 3/20/80 ms FIR. Prepare twice in distinct directories and
compare all hashes. Cached fixtures are never silently replaced. Replay with
`--backends baffle aec3 pipeline`, then `word_qualification.py score MODELS DIR
RESULTS REPORT`. English word recognition is not a phone-intelligibility test;
Hungarian, low-energy onset labels and physical room speech remain outstanding.
`asr_boundary_diagnostics.py` changes one input factor at a time with fresh
recognizers. Those interventions never replace the original full-window WER.
`cancellation_tail_scores.py INPUT_DIR RESULTS_DIR REPORT` retains complete
known-oracle windows before, during and after the simulated 80 ms echo tail.
Pipeline scores explicitly account for its 10 ms delay; raw gain-sensitive
projection/error includes AGC and is not first-phoneme or acoustic-tail proof.

## Acquisition and reviewed onset admission

First enumerate with `baffle_hail_acquire BACKEND --list`. Native recording requires
explicit persistent endpoint keys; default endpoints are permitted only for the
virtual self-test. Device names do not establish a physical speaker/mic route.

```
baffle_hail_acquire BACKEND CAPTURE_KEY PLAYBACK_KEY STEREO_48K.f32 SECONDS PREFIX [CANCEL_MS [stop|restart]]
python3 tools/acquire_room.py EXE PROTOCOL.json OUTPUT_DIR
```

The wrapper requires explicit fixture kind, endpoints, hardware/driver, route,
room, distance, playback level, driver processing and calibration receipt (null
when unavailable); see its required-field list. It never changes mute, volume,
default devices or processing settings. Maximum recording is 120 s, mono 48 kHz
capture/stereo 48 kHz reference. Raw PCM stays local. JSONL retains each leg's
clock domain, epoch, frame position, timestamp validity/uncertainty and generation.
Events 2/3/4 denote request/generation/flush acknowledgement; event 5 is stopped.
Neither acknowledgement nor stop is a measurement of device presentation or
acoustic silence. No clock identity or precise timing is invented.

Acquire far-only, near-only and double-talk, independently reviewed consonant
and vowel starts, low/loud playback and near speech, distances, quiet/noisy/
reverberant rooms, moved echo paths, cancellation and immediate restart. Keep a
separate dry near speech or independent human review where clean-oracle metrics
are required. Measure physical tail on a calibrated recorder, not queue events.
Driver processing settings and calibration must be known before attributing
nonlinearity or drift. Tuning and holdout recordings must be disjoint.

Generate a VAD trace with `baffle_vad_trace RATE PROCESSED.f32 TRACE.csv`.
Fill `tests/fixtures/onset-review.example.json` with independently reviewed
onset/phone/syllable labels, uncertainty, processed-audio review and downstream
approved VAD/WER limits, then run `onset_qualification.py MANIFEST REPORT`.
The supplied template intentionally returns UNQUALIFIED. Correlation, first-word
recognition and near projection cannot certify first-phoneme preservation.

## Gate H paired intelligibility

Additional offline backends are `aec-ns`, `aec-ns-agc`, `aec-vad`, `ns-only`,
`ns-zero` and `ns5`. They select existing public pipeline stage masks; no new
library API exists. `aec-vad` has no PCM gating. NS backends export the explicit
one-hop prefix plus the final real samples. `ns5` is an offline NS-only 5 ms
probe, not a 5 ms AEC pipeline. Receipts add priming/discarded frames, gain,
VAD and reset diagnostics. Inactive AGC is unity and unavailable VAD probability
is -1; the earlier captured CSV's inactive zero fields must not be interpreted
as measured attenuation/activity. Check the stage mask.

Keep the previously documented model pins and download the second independent
provider recording alongside its transcript:

```sh
hf download csukuangfj/sherpa-onnx-whisper-tiny.en test_wavs/1.wav --revision d026532c022fa99fd789d6b32446a1df7b6bfc43 --local-dir models/whisper
python tools/word_qualification.py prepare models seeded
python tools/paired_asr.py prepare models seeded paired-input
python tools/paired_asr.py run acoustic-tools/baffle_acoustic_runner acoustic-tools/baffle_vad_trace models paired-input paired-results
python tools/paired_asr_summary.py paired-input/manifest.json paired-results/results.json paired-summary.json
python tools/paired_signal_scores.py paired-input paired-results signal-scores.json
python tools/blank_asr_matrix.py models paired-input paired-results blank-diagnostics.json
python tools/ns_delay_qualification.py acoustic-tools/baffle_acoustic_runner models paired-input ns-experiment
python tools/prepare_onset_review.py paired-input paired-results human-review
```

Use fresh result/review directories; prior results and human annotation are not
overwritten. Audio remains local, with original and processed identities retained.
The preparation's zero-added-error/blank and false-transcript checks are
conservative engineering targets fixed before decoding, not application-approved
phone-onset limits. No VAD gate, gain normalization or endpoint tuning is applied
to the paired ASR input. CER excludes whitespace; WER/CER are undefined for empty
references, whose false transcripts/insertions are counted explicitly.

`parakeet_frontend_diagnostic.py MODELS SEEDED OLD_RESULTS REPORT` is a separate
optional diagnostic using kaldi-native-fbank 1.22.3 and ONNX Runtime. It compares
individual feature/normalization choices with fixed weights/TDT greedy decoding.
It is not a validated NVIDIA frontend or shipped ASR backend. Its Apache-2.0
adaptation notice/license is separate from Baffle's MIT core. Negative diagnostic
results remain retained; no model-specific gain workaround was added to DSP.

See [Gate H](../../docs/gate-h.md) for the measured failures, calibrated-onset
limits and actual workload timings. PCM, models and human review files are not
automatically published or included in ordinary CI.

## Gate I preservation and acoustic diagnostics

The runner also accepts `agc-only`, `aec-agc` and `ns-agc`. Select all eight
variants explicitly; the old five-stage command default is unchanged:

```sh
python tools/paired_asr.py run acoustic-tools/baffle_acoustic_runner acoustic-tools/baffle_vad_trace models paired-input eight-stage --stages raw baffle ns-only agc-only aec-ns aec-agc ns-agc pipeline
python tools/paired_asr_summary.py paired-input/manifest.json eight-stage/results.json eight-stage-summary.json
python tools/speech_preservation.py paired-input eight-stage preservation.json
python tools/gate_i_experiments.py before-tools/baffle_noise_order acoustic-tools/baffle_noise_order models paired-input eight-stage hint-order
python tools/assistant_only_fixtures.py seeded assistant-input
python tools/paired_asr.py run acoustic-tools/baffle_acoustic_runner acoustic-tools/baffle_vad_trace models assistant-input assistant-results --stages raw baffle aec-ns pipeline
python tools/acoustic_recovery.py assistant-input assistant-results recovery.json
python tools/room_replay.py replay acoustic-tools/baffle_acoustic_runner room-input room-results --backends baffle aec-ns pipeline
python tools/recorded_far_asr.py models room-input room-results recorded-far-asr.json
```

Build before/after tools against their respective installed SDKs, not private
source headers. `baffle_noise_order protected RATE INPUT.f32 OUTPUT.f32 RECEIPT`
explicitly freezes learning on foreground with no prior noise; `agc-ns` changes
the ordering of the existing processors. Both accept mono 16/48 kHz, preallocate
DSP memory, and retain the one-hop prefix plus real final samples. File and
receipt work is outside processing calls. The order probe has no capture-epoch
adapter: those cases remain in the complete eight-stage matrix, not the matched
order comparison. Frozen learning does not bypass an existing noise estimate.

The spectral diagnostic is 16 kHz only and does not certify phoneme preservation.
Recovery curves use declared digital thresholds and complete integer-frame
windows; they do not measure audible tails or provide convergence probabilities.
Assistant-only recognized words are not committed words. Input/output PCM stays
local; retained hashes and commands permit comparison without redistributing it.

`inference_contention.py` additionally accepts `--sample-rate 48000`; default is
16000. Supply matching DSP microphone/reference PCM while keeping ASR input at
the fixed 16 kHz model rate. Use separate fresh directories for 30-second single
workloads and longer combined trials. ASR remains rolling offline re-decoding,
not a validated streaming engine. See [Gate I](../../docs/gate-i.md) and its
command ledger for the executed configurations, failed cases and limitations.

## Gate J isolated residual experiment

`baffle_residual_runner` is external QA, not a Baffle backend or installed target.
It has the same PCM/receipt arguments as the main runner. `pipeline` selects the
fixed rejected coherence candidate; `residual-control` disables its mask and
verifies the split public-API AEC+conditioning path against the original runner.
No candidate is automatically selected by Baffle. Do not use the experiment as
a qualified echo suppressor. See the [fixed protocol](../../docs/gate-j-protocol.md)
and [decision](../../docs/gate-j.md).

```sh
python tests/residual_tools.py acoustic-tools/baffle_acoustic_runner acoustic-tools/baffle_residual_runner
python tools/residual_tradeoff.py control acoustic-tools/baffle_residual_runner paired-input eight-stage split-control
python tools/paired_asr.py run acoustic-tools/baffle_residual_runner acoustic-tools/baffle_vad_trace models paired-input residual-speech --stages raw pipeline
python tools/residual_tradeoff.py compare eight-stage/results.json residual-speech/results.json comparison.json
python tools/residual_tradeoff.py signal paired-input eight-stage residual-speech signal-comparison.json
```

Use the unchanged 11 assistant-only inputs and separate recorded room replays as
documented in the Gate J ledger. `recorded_far_asr.py --stages raw pipeline`
selects only those variants; its original four-stage default is unchanged.
`residual_tradeoff.py characterize INPUT BASELINE CANDIDATE REPORT` requires the
baseline's `raw`, `baffle` and `pipeline` receipts; it retains per-case spectra,
VAD, hypotheses, resets and explicitly unavailable commitment/timing evidence.
