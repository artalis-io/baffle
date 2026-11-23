# Quiet caption continuation correction — 2026-11-23

In **Raw microphone** mode, the caption worker now uses Baffle's existing
lightweight VAD to keep an active sentence alive when the speaker becomes quiet.
NS and NS+AGC keep their original endpointing: processed-profile experiments
introduced extra wrong Hungarian words and a false click transcription, so the
new detector was not enabled there. The default NS selection is unchanged.
Startup still uses the original
raw-RMS 0.002 trigger: broader onset changes failed first-word checks and were
rejected. This improves quiet continuations; it does not solve entirely quiet
speech that never crosses the initial trigger.

## Release result

On the 16 final speech recordings (344 reference words), Raw-mode word errors
fell **122 → 92**. English improved **70 → 40**; Hungarian stayed **52 → 52**.
NS-mode errors were **141 → 140**, with Hungarian also unchanged. No final
speech case worsened, and there were no new first-word losses, blanks or dropped
blocks. These are literal publisher-transcript comparisons, not a claim that
Hungarian recognition or first-phoneme preservation is solved.

Recorded speech followed by synthetic colored noise/hum improved **22 → 4**
errors in Raw mode; the processed profile stayed **22 → 22**. Isolated Raw
click/noise false captions remain **2 → 2**; NS controls remained **0 → 0**.
The narrow correction is adopted; broader onset/processed-signal changes are
withheld. General acoustic and speech readiness remains **PARTIAL**.

The local [BaffleLive.exe](../build-native-transcript/portable/BaffleLive.exe)
was updated at its existing path. Reopen the app and select **Raw microphone**
to use quiet-continuation handling. Existing windows keep their previously
loaded build. The prior executable was retained outside the portable directory.
[Package identity](evidence/2026-11-23-quiet-caption-fix/package-update.json)
records both hashes. No user recording or microphone session was required.

No Baffle/Hail public API, capture callback, model, NS/AGC strength or feedback,
queue capacity or caption-window size changed. The detector is private to one
caption worker, allocated before capture, reset on actual capture discontinuity,
preserved across ordinary utterance completion and freed when the caption worker
retires. Device/capture callbacks never borrow its state.
It observes the same PCM as ASR and adds no PCM gain, gating or transformation.
It has no onset/hangover lookahead and does not classify a conversational speaker.

Endpoint evidence and rolling-window pauses are separate counters. Quiet activity
may defer an endpoint; the six-second/250 ms rolling-pause decision still uses
the original raw-energy evidence. Coupling the two moved window boundaries and
regressed some NS results. Restoring the original rolling policy reduced the
targeted hu-01 NS result from 15 back to the baseline's 12 errors without changing
PCM or thresholds. The deterministic state test guards this separation.

A separate overlap defect was corrected: subtracting 120 ms from the carried
prefix boundary sometimes excluded the last matching word and prevented an exact
multiword stitch. Only word starts strictly before the existing boundary count;
at least two matching words are still required. No fuzzy or unconditional word
deletion was added. Token times remain estimates, not phoneme/device timestamps.

## Method and retained failures

Eight original English/Hungarian FLEURS recordings serve as regression inputs.
The first eight additional publisher-ID holdouts rejected the expanded-onset
candidate. A second eight-recording holdout exposed NS/window-policy trade-offs.
After separating endpoint and rolling decisions, the next four unique eligible
IDs per language (skipping both previous holdout sets) were frozen before
recognition as the final Raw-algorithm holdout. Restricting detector activation
to Raw did not tune its parameters or input; the release replays the same frozen
inputs with an explicit raw/ns caption-profile argument. Historical folder names
such as `accepted-ns` identify experiments, not adoption decisions.
No model hints, transcript prompts, level normalization or score-based exclusions
were used. All failed candidates, per-window PCM hashes and hypotheses remain in
[retained evidence](evidence/2026-11-23-quiet-caption-fix/summary.json).

Previously completed baselines were reused only after matching executable,
model and input hashes, and every copied receipt is marked with its origin. This
saves redundant native inference; reused rows are not counted as new executions.
Timing comparisons include historical baselines on an ordinary desktop and do
not isolate detector overhead from window mix and scheduling noise.

The fixed model is Parakeet TDT v3 INT8 via sherpa-onnx 1.12.40, CPU/two threads.
Both sides use the same paced native replay instrumentation and immutable PCM.
Raw inputs isolate caption logic. NS inputs additionally exercise the default
18 dB NS profile: one second of initial silence is fed through the installed
native processor, then delayed NS output is paired with its corresponding prior
raw block, matching `live_capture.c`'s `previous`/`output` handoff. Algorithmic NS
delay remains 160 frames (10 ms); file alignment does not eliminate it. Final NS test
WAVs preserve the native float32 output bit for bit, unlike the earlier PCM16
diagnostics. Identity checks passed for all 22 final NS waveforms. Earlier PCM16
results stay labeled and retained; they are not rewritten as float-path evidence.
VAD feedback into NS remains disabled. This is file replay, not
physical Shure/device testing or physical acoustic qualification.

Historical condition strings say "physical NS delay" for this algorithmic delay;
that wording was corrected in the maintained driver after evaluation. No device
or acoustic latency was measured. The evaluated driver snapshot is retained as
`qa-driver-used.py.txt`; the wording correction changes no processing or metrics.

References are the public [Google FLEURS](https://huggingface.co/datasets/google/fleurs)
publisher transcripts (CC-BY-4.0, Google Research), not new human review. Literal
NFC/case/punctuation-normalized WER retains accents and number/compound formatting
differences. First-word accuracy is not independently reviewed first-phoneme
preservation. Blank speech results and false non-speech captions remain failures.
The original eight-second window, 250 ms preroll, 900 ms endpoint timeout and
600 ms refresh-after-decode schedule are unchanged.

The archive contains **273 newly executed native runs** plus **57 explicitly
reused baseline rows**. Reused receipts are not independent repetitions. All
rejected candidates remain available alongside the final comparisons.

## Per-case results

Word-error counts; non-speech errors are false ASR words, not committed turns.
Every unfavorable result is retained. See summary JSON for pooled WER, all
first-word checks, blank counts, decoder-call time distributions and queue peaks.

| Input | Case | Baseline errors | Candidate errors | First word baseline / candidate | Drops baseline / candidate |
|---|---|---:|---:|---|---|
| release-raw | click | 1 | 1 | None / None | 0 / 0 |
| release-raw | en-01 | 4 | 4 | True / True | 0 / 0 |
| release-raw | en-02 | 2 | 2 | True / True | 0 / 0 |
| release-raw | en-03 | 21 | 13 | True / True | 0 / 0 |
| release-raw | en-04 | 11 | 1 | True / True | 0 / 0 |
| release-raw | en-holdout-09 | 23 | 11 | False / False | 0 / 0 |
| release-raw | en-holdout-10 | 7 | 7 | True / True | 0 / 0 |
| release-raw | en-holdout-11 | 0 | 0 | True / True | 0 / 0 |
| release-raw | en-holdout-12 | 2 | 2 | True / True | 0 / 0 |
| release-raw | hu-01 | 4 | 4 | True / True | 0 / 0 |
| release-raw | hu-02 | 11 | 11 | False / False | 0 / 0 |
| release-raw | hu-03 | 6 | 6 | False / False | 0 / 0 |
| release-raw | hu-04 | 11 | 11 | True / True | 0 / 0 |
| release-raw | hu-holdout-09 | 10 | 10 | True / True | 0 / 0 |
| release-raw | hu-holdout-10 | 4 | 4 | True / True | 0 / 0 |
| release-raw | hu-holdout-11 | 3 | 3 | True / True | 0 / 0 |
| release-raw | hu-holdout-12 | 3 | 3 | True / True | 0 / 0 |
| release-raw | noise | 1 | 1 | None / None | 0 / 0 |
| release-raw | quiet-noise | 0 | 0 | None / None | 0 / 0 |
| release-raw | silence | 0 | 0 | None / None | 0 / 0 |
| release-ns | click | 0 | 0 | None / None | 0 / 0 |
| release-ns | en-01 | 4 | 4 | True / True | 0 / 0 |
| release-ns | en-02 | 1 | 1 | True / True | 0 / 0 |
| release-ns | en-03 | 24 | 23 | True / True | 0 / 0 |
| release-ns | en-04 | 11 | 11 | True / True | 0 / 0 |
| release-ns | en-holdout-09 | 24 | 24 | False / False | 0 / 0 |
| release-ns | en-holdout-10 | 7 | 7 | True / True | 0 / 0 |
| release-ns | en-holdout-11 | 2 | 2 | True / True | 0 / 0 |
| release-ns | en-holdout-12 | 2 | 2 | True / True | 0 / 0 |
| release-ns | hu-01 | 12 | 12 | True / True | 0 / 0 |
| release-ns | hu-02 | 10 | 10 | False / False | 0 / 0 |
| release-ns | hu-03 | 8 | 8 | False / False | 0 / 0 |
| release-ns | hu-04 | 16 | 16 | True / True | 0 / 0 |
| release-ns | hu-holdout-09 | 11 | 11 | True / True | 0 / 0 |
| release-ns | hu-holdout-10 | 3 | 3 | True / True | 0 / 0 |
| release-ns | hu-holdout-11 | 3 | 3 | True / True | 0 / 0 |
| release-ns | hu-holdout-12 | 3 | 3 | True / True | 0 / 0 |
| release-ns | noise | 0 | 0 | None / None | 0 / 0 |
| release-ns | quiet-noise | 0 | 0 | None / None | 0 / 0 |
| release-ns | silence | 0 | 0 | None / None | 0 / 0 |
| release-background-raw | speech-then-colored-noise | 11 | 3 | True / True | 0 / 0 |
| release-background-raw | speech-then-hum | 11 | 1 | True / True | 0 / 0 |
| release-background-ns | speech-then-colored-noise | 11 | 11 | True / True | 0 / 0 |
| release-background-ns | speech-then-hum | 11 | 11 | True / True | 0 / 0 |

## Latency and verification limits

The detector uses 192 bytes of public-API state plus its aligned allocation and
owner descriptor. In 20,000 calls at each rate on this Ryzen 9 5950X, P99 was
0.4 microseconds at 16 kHz and 0.9 microseconds at 48 kHz; maxima were 16.8 and
5.7 microseconds. These are detector-only measurements with timer/scheduling
noise, not complete ASR latency or universal deadlines. The example runs 16 kHz.
No new lookahead is introduced. Existing NS contributes 10 ms; real decoder stalls
and queue occupancy are separately recorded and remain much larger than VAD cost.

Local native example checks and the owned-source Windows ASan checks each passed
3/3. Existing core tests passed 7/7 and installed Hail consumers passed 3/3.
Receipt-parser checks passed. The recorded ASan smoke completed, but emitted an
`interception_win: unhandled instruction` warning; it is not a fully clean
prebuilt-stack sanitizer qualification. Static analysis
of app/speech/replay sources found a pre-existing unused assignment in history
truncation; it was retained rather than suppressed. Prebuilt inference/SDK internals
were not newly sanitizer-instrumented. No fresh remote CI or other-platform run is
claimed for this Windows-only example correction.

Clicks/noise can still produce false captions, and the original onset gate can
still miss uniformly quiet speech. Full production acoustic/onset readiness stays
**PARTIAL**. No physical capture, calibrated latency or conversational PASS is claimed.

An initial NS preparation used a raw/processed pair offset by one hop. Source
review caught it before NS recognition ran; the unexecuted preparation was kept
locally as `fixtures-final-ns-unexecuted-misaligned`, and input pairs were corrected
to the actual capture-helper contract before evaluation. No scored NS failure was
replaced and no Hail/Baffle processing implementation was changed for this correction.

## Reproduction

The external QA driver needs the existing Python QA environment with numpy,
soundfile and pyarrow. They are not executable runtime dependencies. All corpus
and model/WAV identities, commands, compiler configuration and native receipts
are retained. Historical comparison sources/binaries remain local snapshots;
baseline app/speech source and diagnostic build instructions are also archived
as text in the evidence directory.

```powershell
cmake --build build-c11-audit/win32 --target BaffleLive BaffleReplay --parallel
ctest --test-dir build-c11-audit/win32 --output-on-failure
build-acoustic/venv/Scripts/python.exe tests/public_caption_replay.py
build-acoustic/venv/Scripts/python.exe tools/quiet_caption_check.py prepare --pilot build-asr-comparison/fixtures --dataset build-asr-comparison/dataset --skip-holdouts 8 --output NEW-fixtures
build-acoustic/venv/Scripts/python.exe tools/quiet_caption_check.py condition --fixtures NEW-fixtures --runner build-c11-audit/tools/baffle_acoustic_runner.exe --output NEW-fixtures-ns
build-acoustic/venv/Scripts/python.exe tools/quiet_caption_check.py evaluate --fixtures NEW-fixtures --baseline build-quiet-caption/instrumented/ReplayBaseline.exe --candidate build-quiet-caption/release-candidate/BaffleReplay.exe --candidate-profile-auto --models build-acoustic/models/parakeet --output NEW-raw-results
build-acoustic/venv/Scripts/python.exe tools/quiet_caption_check.py evaluate --fixtures NEW-fixtures-ns --baseline build-quiet-caption/instrumented/ReplayBaseline.exe --candidate build-quiet-caption/release-candidate/BaffleReplay.exe --candidate-profile-auto --models build-acoustic/models/parakeet --output NEW-ns-results
```
