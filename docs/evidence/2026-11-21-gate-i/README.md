# Gate I evidence ledger

**PARTIAL.** See [the engineering report](../../gate-i.md). This archive retains
failures and source identities; it does not certify production acoustic quality.

## Source, inputs and metric interpretation

Baseline is `2e59db1`. The production correction is `d01af75`; fuzz additions
are `46d118a`, with final regression/telemetry documentation source `387eca5`.
`summary.json` identifies the archive's parent revision. Documentation-only
archive commits follow those measured binaries. `environment.json` records
before/after SDK and runner hashes, compiler/flags and model identity references.
Model pins and original corpus licenses/provenance are unchanged from
[the preceding acoustic evidence](../2026-11-20-real-world/) and
[Gate H](../2026-11-20-gate-h/). No new physical measurements were made.

`commands.json` supplies reproducible top-level command arrays. Exact spawned
native commands also appear in individual results/reports. Local caches and
output directories must be preserved; reruns require fresh output directories.
Optional Python/model packages are QA dependencies, never Baffle dependencies.

* `baseline-controlled.json`: original near-component SNR improvement
  **14.603375 dB**, unchanged **8 dB** requirement; far-only ERLE is separate.
* `freeze-before.log`: expected regression failure with the original core.
  `freeze-after.log` and `final-core.log` retain corrected results.
* `ablation-before.json` / `ablation-after.json`: 448 hypotheses per side, eight
  stages, 28 unchanged cases. `default-equivalence.json` verifies all 224 distinct
  PCM hashes and all 448 hypotheses match. Summaries preserve failed near-only,
  blank and false-transcript checks; their `gate_h` label denotes the inherited
  scoring protocol, not the Gate I verdict.
* `hint-order.json` / `experiment-summary.json`: protected-startup fidelity and
  ordering. All corrected protected hypotheses match raw after normalization,
  including one Parakeet error that the old attenuation happened to avoid.
  Neither probe order uses VAD feedback; capture-epoch cases are excluded only
  from this plain-PCM probe, never from the complete stage matrix.
* `preservation.json`: declared gain/error/spectral diagnostics on full identical
  source intervals. No new independent phone annotations or perceptual claim.
* `assistant-only.json` / `assistant-manifest.json`: 11 simulated far-only cases.
  `recorded-far-asr.json`: three real recorded far-only sources at two rates.
  Empty reference WER is undefined; recognized insertions are not committed
  words. No transcript attribution/commitment policy is implemented here.
* `room-replay.json`: original and documented-delay-compensated mic/output power
  ratios are separate. Ratios include ambient noise and do not establish speech
  quality, oscillator drift, or calibrated physical ERLE.
* `recovery-final.json`: authoritative integer-frame, complete-window diagnostic.
  `recovery.json` retains the earlier floating-grid draft, including its end-grid
  limitation. It is not a separate qualified run and is not substituted for old
  PCM or hypotheses. The 20 dB/two-window diagnostic and 1/32768 FS digital-tail
  meter are not physical acceptance, audible silence or device flush latency.

## Raw receipts and private audio

`ablation-raw.tgz` contains CSV/JSON/log receipts from both complete matrices.
`acoustic-raw.tgz` contains protected/order, assistant-only and room replay
receipts. `performance-raw.tgz` contains all 14 timing/worker/resource trials.
These archives contain **no PCM, recordings or model weights**.

Original and processed PCM remains in the local immutable caches, identified by
`private-audio-inventory.json`, manifests and per-result SHA256s. Publishing hashes
does not grant redistribution rights; original download licenses still apply.
Reprepare inputs from pinned source assets and compare hashes before scoring.
No recording, failed hypothesis or difficult interval is deleted to obtain a PASS.

`performance-summary.json` aggregates 78,000 paced 10 ms calls at 16/48 kHz,
including 300/120-second combined CPU Whisper+Parakeet+Piper trials. Execution,
release lateness and 10 ms NS delay remain separate. Native Windows RSS `-1`
means unavailable; psutil sample maxima are not exact peaks. CPU percentages
use psutil conventions (100% per logical core). GPU sampling observes the host,
not DSP attribution. TTS queue occupancy is modeled; ASR is rolling offline
re-decoding. These trials are not a connected/live/physical conversational test.

## Verification and limitations

Windows static/shared core: 7/7 each; installed Hail: 3/3. MSVC and clang-cl
installed-tool smoke passes. ARM64 final source ASan/UBSan and TSan: 8/8 each;
installed-public-tool sanitizer smoke passes. Separate retained fuzz runs cover
30,000 pipeline, 30,000 transport and 10,000 AEC iterations. GCC analysis is
scoped to Baffle, not inference libraries. The first shared SDK CTest invocation
had tests disabled; `shared-no-tests.log` is **not a pass**. The test-enabled
shared suite is recorded separately. Use `--no-tests=error` on future runs.

`final-implementation-ci.json` records all 27 successful jobs for `387eca5`,
including Linux x86-64/ARM64, Windows and macOS ARM64/Intel configurations.
`software-ci.json` is the earlier corrected core/fuzz result. These are distinct
from any later archive/documentation commit's CI. Sanitizers and CI do not prove
callback deadlines, room quality or reviewed interruption intelligibility.

Remaining blockers include default quiet-speech regressions, assistant-only
transcripts, weak real-room AEC, missing reviewed first-phone/first-syllable and
Hungarian evidence, calibrated physical acquisition and live conversational
qualification. No public record layouts/exports, default ordering, NS buffer or
gain targets change; ABI remains provisional. Hail source remains unchanged.

`SHA256SUMS` covers the recorded bytes of every file except itself, including
native log newlines. The repository disables text normalization for this ledger.
