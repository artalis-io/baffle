# Baffle: bounded native DSP for conversational audio

CPU-first C11 audio processing. Baffle processes PCM; Hail acquires/transports
audio; inference interprets/generates speech; Hull orchestrates applications.
No Python, GPU, audio device API or inference framework is required by the core.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/prefix
```

Installed consumers use `find_package(Baffle 0.1.0 EXACT CONFIG REQUIRED)` and
`Baffle::baffle`. ABI 0.1 is provisional. Static and shared builds are supported
(`-DBF_BUILD_SHARED=ON`). Set `BF_BUILD_TESTS=OFF` for a C-only build; the session
stress test uses C++ threads. The library creates no background threads.

Implemented: streaming windowed-sinc resampling and smooth drift adjustment,
channel matrices/delays, linked AGC/limiting, classical VAD, spectral noise
suppression, and optional mature SpeexDSP AEC. All processors use caller-owned
storage, without heap allocation, locks, I/O or worker waits during processing.

Enable the reviewed, pinned CPU AEC explicitly:

```sh
cmake -S . -B build-aec -DCMAKE_BUILD_TYPE=Release -DBF_WITH_SPEEXDSP=ON
cmake --build build-aec --parallel
ctest --test-dir build-aec --output-on-failure
```

This option fetches SpeexDSP 1.2.1 at a pinned commit. Offline builds can supply
`BF_SPEEXDSP_SOURCE_DIR`; selected files must match reviewed hashes. Default
builds need no network. No GPU backend is implemented.

See the independent [installed PCM consumer](examples/pcm-pipeline), optional
[installed-Hail consumer](examples/hail-consumer), [API contract](docs/api.md),
[architecture](docs/architecture.md), [backend assessment](docs/backends.md),
[measurements](docs/performance.md), and [gate evidence](docs/verification.md).
`BF_WITH_HAIL=ON` builds separate `Baffle::hail_adapter` against installed Hail.
The core neither includes nor links Hail.

The unchanged original AEC quality target now passes after correcting capture
DC rejection; [the convergence report](docs/aec-quality.md) retains the original
failure and includes separate recorded-English holdout evidence. Speech conditioning and AEC have deterministic synthetic evidence. Recorded-speech
quality, physical acoustic behavior, device timing and audible interruption tails
remain unqualified. Exploratory real-recording AEC trials were weak and are
documented alongside synthetic results. **This working DSP implementation is not a production-readiness
certification.** See [qualification](docs/qualification.md) for the remaining work.

[Real-world convergence](docs/real-world-qualification.md) adds a controlled AEC3
comparison, seeded transcript fixtures and actual external ASR/TTS/LLM contention
measurements. Real-room cancellation and interruption quality remain PARTIAL;
blank ASR hypotheses, epoch-reset tails and missing reviewed onset labels are
retained. The optional [acoustic tools](tools/acoustic-runner) build against
installed public libraries without changing the core dependencies.

[Gate H](docs/gate-h.md) adds stage-by-stage WER/CER on 280 paired evaluations.
Double-talk improves over raw input, while near-only regressions, model-sensitive
blank hypotheses and assistant-only false transcripts remain explicit failures.
Reviewed first-phone/syllable preservation remains open; no production claim.

MIT licensed; see LICENSE and NOTICE. Hail's engineering conventions are adapted,
not its audio-device implementation.
