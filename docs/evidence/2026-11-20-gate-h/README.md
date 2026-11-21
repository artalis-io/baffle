# Gate H retained evidence

**PARTIAL**, with explicit failed quality checks; ABI provisional. See the
[report](../../gate-h.md) and [commands](../../../tools/acoustic-runner/README.md#gate-h-paired-intelligibility).

* `baseline-ci.json`: completed a231983 CI, 27/27 successful.
* `software-ci.json`: corrected core/test commit ee1c080, 27/27 successful.
  Later queued/canceled checks are not retroactively labeled passing.
* `paired-input-manifest.json`: immutable preregistered policy, all 28 cases,
  independent provider word references, source/PCM/event hashes, and explicitly
  constructed insertion positions that are **not phoneme labels**.
* `paired-results.json`: all 280 fixed-window recognition results, WER/CER
  edit counts, empty/first-word outcomes, levels, finite/clipping diagnostics,
  model/decoder configuration, latency, hashes and native summaries.
* `paired-summary.json`: weighted sums per recognizer/stage/scenario group and
  conservative checks, including failures. Empty-reference WER/CER stay null.
* `blank-matrix*.json`: original/fresh decoder, context and unclipped gain
  controls. The expanded matrix retains the initial failures rather than
  substituting a successful control as the production result.
* `parakeet-*.json`, `sherpa-tag.json`: exact source/version pointers and optional
  independent frontend/greedy diagnostics. Negative hypotheses are retained;
  no verified NVIDIA-reference or universal numerical-root-cause claim.
* `ns-report.json`: impulse delay at 16/48 kHz, unity reconstruction, 5/10 ms
  NS-only signal and recognition tradeoffs. No pipeline-default change.
* `vad-transients.json`: known absent-near candidate events and coarse utterance
  activity, output sample steps/peaks, priming/discards/reset/gain diagnostics.
  These do not certify human audible clicks, phonemes or actual conversation stops.
* `generation-before.log` / `generation-after.log`: regression fails before the
  simultaneous epoch/generation flag correction and passes afterward.
  `pcm-equivalence.json` verifies exact restart PCM identity across four stages
  and AEC+VAD, while the corrected generation flag is observable.
* `baseline-controlled.json`, `corrected-controlled.json`: unchanged original
  >=8 dB test; both reproduce 14.603375 dB. Earlier failed evidence remains in
  the preceding evidence directories.
* `review-unqualified.json`: exported real audio paths/hashes are valid, but
  missing independent phone/syllable review, ASR review and app policy refuse PASS.
* `performance-summary.json`: six actual CPU-inference trials plus 120 s combined
  load, separate Spark GPU LLM trials, raw resource/queue scope limitations.
* `raw-measurements.tgz`: native CSV/JSON, VAD traces, worker transcripts,
  CPU/RSS/GPU samples, modeled queue observations, NS diagnostics and corrected
  metadata controls. Audio/model assets excluded; no physical measurements.
* `private-audio-inventory.json`: hashes/lengths for original, processed and
  review-audition audio retained under local `build-gate-h/`. Source and derived
  audio remain local, not redistributed under Baffle's MIT license. Review the
  external corpus/model/voice terms before independent reuse.
* `environment.json`: hardware/build/configuration, current installed SDK hashes,
  source revisions and interpreter/library/model provenance. Paired audio was
  collected before the flags-only correction; equivalence proof is separate.
  Later inactive gain/VAD diagnostic defaults do not change those audio samples.
* Core/Hail, MSVC/clang-cl, ARM64 ASan/UBSan/TSan, 10,000-iteration AEC fuzz and
  analysis logs retain locally executed scope. Instrumented Baffle/Hail checks
  do not establish upstream model/WebRTC safety, deadlines or production quality.

All files listed in `SHA256SUMS` preserve captured bytes (including native line
endings); its own checksum is excluded. No old failures are replaced. Reproduction
uses pinned caches and fresh output directories. Two recorded utterances and
simulated acoustic paths do not establish real-room, Hungarian or live
conversational generalization. No independent human annotations were fabricated.
