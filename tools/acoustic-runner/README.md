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
