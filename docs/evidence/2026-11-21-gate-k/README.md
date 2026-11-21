# Gate K evidence ledger

**PARTIAL.** Three correctness fixes are adopted; both isolated adaptation
candidates are rejected. See [the report](../../gate-k.md) and
[fixed protocol with its source-driven follow-up](../../gate-k-protocol.md).
No physical/live/phone-onset acceptance or ABI freeze is claimed.

## Source and positive corrections

Baseline `96e88d4` is recorded with 27/27 completed CI jobs in `baseline-ci.json`.
`000d1d4` completes multichannel reset and reports native recovery. `7a25c35`
prevents signed AUMDF schedule overflow while preserving modulo phase.
`17aea11` is the source used for local ARM64 sanitizer verification (same
production callbacks as final). `f4f0fc7` adds native kernel fuzz instrumentation;
later changes are qualification/reporting only. `environment.json` identifies
SDKs, hashes, compiler/flags, model pins and source/binary distinctions.

* `reset-before.log`: actual public-API trained 2×2 reset failure, old-path energy
  1.24207785632 with a silent microphone/new valid reference. Corrected output is
  exactly zero. Extended tests cover 24 trained shape/rate/lifecycle combinations.
* `recovery-regression-before-fixed.log`: old wrapper fails the actual public
  reporting assertion on a native recovery. The earlier missing-binary setup
  log is not an algorithm result. Corrected regression logs and core suites pass.
* The abrupt-path CSV at source frame 199840 changes from reset=0/flags=0 to
  reset=1/discontinuity=16, with identical PCM. Private `cancel_count=0` after a
  cancellation call identifies native recovery. Other counter decreases can also
  be explicit lifecycle resets and must not all be labeled numerical divergence.
* `counter-before.csv` shows negative signed-counter behavior on Windows.
  `baffle-k-counter-before-ubsan.log` reproduces real signed overflow at upstream
  `mdf.c:707`, with expected nonzero exit recorded separately. This is an
  accelerated valid-state boundary, not a 248-day calendar test or passing
  sanitizer run. Corrected tests verify positive bounded values, unchanged modulo
  phase and no artificial adaptive reset.
* `corrected-identity.json`: 78 mono AEC/full outputs match fresh baseline speech/
  far-only PCM. `corrected-room-identity.json`: 48 real-room/control outputs match
  original Gate I. All 24 primary/extra-rate path outputs also match. Only metadata
  recovery reporting changes for these mono streams. Multichannel reset PCM
  intentionally changes to remove stale-path output.

## Adaptive/path and speech comparisons

`paths-manifest.json` freezes 21 configurations, including 2 holdout FIR/noise
seeds. `extra48-manifest.json` freezes 3 band-limited offline SRC challenges;
these are not newly acquired 48k rooms. Each variant has score JSON, exact native
commands/binary hashes in `*-runs.json`, and raw counters/curves in
`paths-raw.tgz`. Coefficients are background IR, not the foreground output bank.
Raw-FIR error includes frontend conditioning, S16 effects and unmodeled tail;
drift/unexcited comparisons are unavailable, never precise hardware delay.

`partition-experiment.json` / `partition.diff` describe the one-expression .1→.3
partition exploration change. `readiness-experiment.json` / `readiness.diff`
describe the independent leakage-gate removal. Original normalized source pins
and BSD notices are preserved. Both are worktree-only derivatives; neither is a
library option, backend or adopted production update.

`candidate-decision.json` retains the unchanged ≥20% moving-path median target:
both improve 0%, partition worsens the reflection holdout 3.5→5.5 s, readiness
worsens gradual recovery 2→2.5 s. Partition has an isolated extra48 abrupt gain
5→3 s, which does not override other failures. Readiness changes the private mode
bit on stationary data without establishing better cancellation/reliability.
No internal mode is exposed as a calibrated probability or user-speech flag.

Raw ASR JSON includes **846 new recognitions** across raw/AEC/full, fixed models,
the original 28 speech and 11 assistant-only cases, recorded rooms and four new
moving-path mixtures. Independent provider transcripts and source/model pins are
unchanged. New mixtures reuse near/reference assets; they are not new blind
speech or physical room recordings. No hypothesis supplies ground truth.

`*-comparison.json` retains per-case WER/CER edits, blanks and first words.
Partition adds 5 AEC-only Parakeet blanks on the original speech matrix, further
moving-path losses and full-pipeline word errors. Readiness adds no word/blank/
first-word regression on these comparisons, but gives no room/assistant-only
recognition benefit. Reused corrected production hypotheses are identified as
identical-PCM reuse, not fresh decoding. Far-only WER is undefined; false words
are recognized insertions, **not committed turns**. No runtime commitment policy
is present. Human-reviewed phones/onsets and Hungarian evidence remain open.

## Numerical, resource and platform evidence

`soak-summary.json` covers 1,800 s of accelerated virtual audio / 180,000 calls, finite
output and 28 reported native recoveries. Source concatenation includes varied
paths/levels/noise/drift/overlap; this is not a 30-minute wall-clock/device soak.
`performance-summary.json` and `performance-raw.tgz` cover 17 paced trials,
**60,000 measured 10 ms calls**, with zero observed completion/execution misses.
They include 16/48 kHz AEC/full under actual CPU Whisper+Parakeet+Piper and a 120 s
corrected full trial. Execution, release lateness, NS 10 ms buffering and modeled
queue occupancy are separate. Process CPU uses 100%-per-logical-core conventions;
sampled RSS maxima are not exact peaks/DSP arenas. Native Windows RSS −1 is
unavailable. Host GPU samples are not DSP attribution. No GPU DSP or connected
native-streaming/live conversational qualification is claimed.

Windows Release static 8/8, shared 7/7, MSVC Debug static 8/8; newly installed public
Hail consumer 3/3. ARM64 ASan/UBSan and TSan 9/9, including no-allocation and private
recovery checks. `shared-exports.txt` confirms the same 45 established `bf_`
exports and no private helper export. ABI remains provisional.

Initial local `build-fuzz` was backend-OFF: its 30,000 transport / 30,000 pipeline
runs are valid only in that scope; its 10,000 AEC inputs take unsupported paths
and are **not AEC verification**. Separate `baffle-k-native-*-fuzz.log` records
**10,000 active AEC and 30,000 active pipeline inputs** with the Speex kernel itself
coverage/ASan/UBSan-instrumented. `native-fuzz-compile-commands.json` proves flags
for the object target. Kernel instrumentation was previously not inherited from
the wrapper target. Default production flags are unchanged. Both fallback-core
and Speex-enabled GCC analysis logs are retained. Fuzzing, static analysis and
sanitizers do not prove deadlines, every acoustic branch or deployment quality.

Baseline and final cross-platform checks are distinct; final PR-head results are
linked from PR #1. Cancelled/partial or backend-disabled results are not promoted
to full/native passes.

## Methods, failures and reproduction

`method-and-setup-findings.json` preserves unsuccessful setup attempts and draft
measurement limitations. Initial IR scoring truncated the oracle; final scoring
pads the estimate and retains out-of-capacity error. Initial recovery grids used
movement start/partial post-gap windows; final scoring uses complete post-event
windows after movement end at 16 s / gap end at 10.2 s. Draft scores remain alongside final JSON.
No PCM, hypothesis, original 8 dB metric or threshold changed to obtain a PASS.

The failed first partition preparation compared raw CRLF bytes against an
approved LF pin. Subsequent `partition-build/partition-paths` are unchanged
controls, **not the candidate**. Actual partition evidence is `partition2-*`.
The corrected preparation follows precisely the existing SpeexPins normalization
and also records the raw file hash. No input pin or warning was weakened.

`commands.json` contains top-level reproduction arrays; exact spawned commands
are in individual results. `remote-validation.sh` retains the executed remote
core/expected-before/fallback-fuzz sequence; the native backend commands are
separately documented. Optional QA/model dependencies remain outside the core.
The static-only private probe includes actual wrapper source solely for backend
diagnosis; it is not a DSP integration fixture or an installed API. Hail/DSP
consumers still use installed public headers/libraries only.

Archives include CSV/JSON/log receipts and no PCM, coefficient binaries, recordings
or model weights. Those remain locally retained and hash-identified in
`private-pcm-inventory.json`. Original fixture/model/transcript identities and
license requirements remain in prior Gate H/I/J ledgers. Do not silently replace
cached inputs or overwrite evidence on rerun. `SHA256SUMS` covers recorded bytes
except itself; text normalization is disabled for the ledger.
