# Evidence ledger: real-world convergence

Overall **PARTIAL**. No production, ABI-stability, human-phone preservation,
calibrated acoustic-tail or complete live conversational qualification claim.
See [engineering report](../../real-world-qualification.md) and
[tool reproduction](../../../tools/acoustic-runner/README.md).

* `summary.json`: source scope, unchanged original threshold and remaining risks.
* `ci-baseline-completed.json`: baseline 86feaf9, all 27 jobs successful in
  [run 44755515542](https://github.com/artalis-io/baffle/actions/runs/44755515542).
  Later corrected-source CI is recorded separately; a queued snapshot is not PASS.
* `baseline-controlled.json`, `baseline-real.json`: re-executed original tool
  receipts; the historical failure remains in the older evidence directory.
* `rooms-manifest.json`, `backend-report.json`: fixed source hashes, training-only
  lag diagnostics, unchanged evaluation intervals, both rates and both backends.
* `linear-report.json`, `tail-report.json`, `calibrated-scores.json`: separately
  patched AEC3 linear diagnostic, one-factor filter-tail alternatives, independent
  impulse-peak compensated scores with raw metrics preserved.
* `webrtc-config.log`, `webrtc-build.log`, `linear-crash.log`: external build
  provenance and independently reproduced upstream linear-export failure.
* `baffle-room-raw.tgz`: per-call CSV, native JSON, source manifests/build logs and
  backend reports. **No source or derived audio**. Verify CSV/output hashes
  against each report; outputs remain in private acquisition caches.
* `word-input*-manifest.json`, `word-seeded-*-manifest.json`, `word-wer*.json`,
  `word-results*-report.json`: all fixture versions, cached earlier failures and
  seeded repeatability. The two unseeded preparations are distinct inputs, not
  before/after DSP implementations. `word-wer-seeded.json` is the reproducible v2
  evaluation. `asr-boundary*.json` retains one-factor controls with fresh decoders;
  these do not replace full-window WER. `cancellation-tail.json` includes complete
  known-oracle FIR tail windows, not phoneme judgments or physical measurements.
* `model-identities.json`: external HF model revision pins, source/word transcript
  and all selected local file checksums. No weights/audio redistributed; no
  blanket commercial license clearance. Tool dependencies are separately pinned.
* `contention-summary.json`: final Windows real ASR/TTS workloads and Spark LLM
  statistics, separate execution/scheduling timing and explicit scope limits.
* `contention-acquisition-raw.tgz`: raw CSV, inference worker events/transcripts,
  resource/GPU samples, earlier failed standard-sleep/Piper runs, final trials,
  two-minute stress, actual LLM requests, virtual stop/restart and earlier failed
  STOPPED-read receipts. PCM and models excluded. Commands include original host
  paths; substitute your own verified caches/install prefixes when reproducing.
* `windows-cpu.json`, `windows-os.json`, `environment.json`: hardware, flags,
  external dependency pins, measured-source revisions and reproducible commands.
* `onset-unqualified-final.json`: deliberate unreviewed admission failure. No
  application-approved onset/WER limits or human phone labels were manufactured.
* `core-final.log`, `hail-final.log`, `tools-msvc.log`, ARM64 sanitizer/tool logs,
  `baffle-room-fuzz.log`, `baffle-room-analysis.log`: locally executed checks.
  Instrumented Baffle/Hail are covered; uninstrumented WebRTC is not thereby
  sanitizer-qualified. `ci-windows-failed.log` retains the strict MSVC harness
  warning fixed with typed float zeros, without warning suppression.

Raw evidence records measured revisions and executable hashes. Later edits to
documentation or setup cannot retroactively qualify an earlier executable.
The archive checksums are listed in `SHA256SUMS`; its own checksum is excluded.
Repeated timings are observations, not confidence intervals or universal bounds.
Physical experiments require a declared route, driver processing and calibration;
enumerating a microphone/headphone endpoint is insufficient.
