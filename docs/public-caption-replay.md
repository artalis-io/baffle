# Automated public caption replay — 2026-11-23

**Public recorded speech now drives unattended native regression tests. No user
recording, microphone, playback device or API credential is needed.** This is
software recognition/caption testing, not physical acoustic qualification.

The pinned [Google FLEURS corpus](https://huggingface.co/datasets/google/fleurs)
is CC-BY-4.0. We reused four English and four Hungarian recordings with publisher
transcripts, selected before recognition in the earlier
[public ASR pilot](asr-cloud-comparison.md). They are not necessarily eight speakers
or unseen training examples. No new independent human transcript/onset review is
claimed. Cases and source/model/WAV hashes are retained.

## What ran

- The saved C++ caption worker with its 128-slot queue.
- The early C11 worker with a matching 128-slot queue, before later UI/queue changes.
- The current C11 worker with a 512-slot queue.
- Whole-file recognition through saved C++ and current C11 applications.
- Whole-file padding and input-level controls.

All use the same local Parakeet INT8 model, sherpa-onnx 1.12.40, CPU/two threads.
Raw 16 kHz mono PCM deliberately bypasses DSP to isolate the caption wrapper.
The replay replaces only acquisition, feeding immutable file PCM in nominal
160-frame/10 ms blocks, with one-second digital pre/post padding. Actual application
queues, energy gate, endpointing, eight-second windows, overlap and native ASR run.
This is a test-only source seam, not a Hail backend test. Native code remains C11;
only the isolated historical reference requires C++.

There were 24 paced replays and 40 canonical whole-file/control executions.
Nominal pacing and ordinary desktop scheduling do not force identical decode
boundaries. Therefore every decoded PCM window has a SHA256 and is compared by
actual input identity. Queue-drop counts describe these runs, not universal
C-versus-C++ performance or hard deadlines.

## Accuracy findings

Word-error **counts**, with case/punctuation normalization and accents preserved.
Number-word/digit and Hungarian compound spacing differences count as errors;
WER is not a perceptual speech-quality metric. Empty speech recognition is retained
as deletions, not excluded. The normalized column is an offline 0.5-peak control,
not a proposed live processing stage.

| Case | Reference words | Whole C11 | Live C++/128 | Early C11/128 | Current C11/512 | Whole padded C11 | Padded + level C11 |
|---|---:|---:|---:|---:|---:|---:|---:|
| en-01 | 19 | 1 | 4 | 4 | 4 | 1 | 1 |
| en-02 | 21 | 2 | 2 | 2 | 2 | 1 | 1 |
| en-03 | 33 | 2 | 21 | 21 | 21 | 2 | 1 |
| en-04 | 15 | 1 | 11 | 11 | 11 | 1 | 1 |
| hu-01 | 17 | 17 | 4 | 4 | 4 | 17 | 5 |
| hu-02 | 25 | 6 | 11 | 11 | 11 | 8 | 10 |
| hu-03 | 14 | 5 | 6 | 8 | 6 | 4 | 4 |
| hu-04 | 27 | 6 | 11 | 11 | 11 | 7 | 6 |

- Whole-file C++/C11 text agreed on **8/8** recordings;
  padded whole-file text agreed on **8/8**.
- Current C11 and saved C++ final live text agreed on
  **7/8** recordings.
- Across **35 shared exact-PCM windows**, there were
  **0 recognizer-text mismatches**.
  This does not reproduce an intrinsic C11 recognition regression on this corpus.
- Observed dropped blocks: C++/128 **1**, early C11/128 **57**, current C11/512
  **0**. Early C11's extra loss changed the hu-03 transcript. Different source
  pacing/scheduling and decoder stalls remain confounders; the counts do not
  establish a compiler/language cause. The existing queue/gap fix remains useful.

## Concrete wrapper defect reproduced without a person speaking

The en-03 whole recording has **2** word errors; all three live wrappers have
**21**, despite no dropped blocks. Its raw RMS is approximately 0.000792 and
about **98% of 10 ms blocks are below the fixed 0.002 trigger**. This is not a
claim that 98% of PCM was discarded: active context retains some low-energy
frames. But the source code treats sustained low energy as silence, finalizes
context after 900 ms, and keeps only 250 ms before a later trigger. Quiet,
recognizable speech can therefore disappear from the ASR windows or lose context.
The three fragmentary histories and exact input windows are retained.

`run_live` in [app.c](../examples/win32-transcript/app.c) uses **raw** RMS for this
policy, even when ASR receives processed PCM. Gain/NS changes downstream do not
make this gate a reliable speech detector. This wrapper is shared by the old
C++ and C11 designs. Fixing the language conversion alone cannot close it.

There is also model/input sensitivity: hu-01 is blank as an unpadded or padded
whole recording in both applications, but has four live word errors. The offline
level control removes the blank, yielding five errors. Yet leveling hu-02 worsens
its padded result from eight to ten errors. Padding alone also has mixed outcomes.
No universal gain, threshold or padding change is justified from these controls.
Peak normalization looks at the entire recording; it must not be inserted into a
live path as hidden buffering.

The production gate/model/DSP parameters have **not** been tuned to these eight
fixtures. They now provide reproducible failures for a conservative speech-
preservation correction and an independent holdout evaluation. No production,
phoneme-onset, model-selection or physical barge-in PASS is claimed.

## Maintained tooling and reproduction

The external Python QA driver needs `numpy` and `soundfile`; these are not native
application dependencies. Local driver/parser checks used
`build-acoustic/venv/Scripts/python.exe`. The system Python lacks `soundfile`;
use the QA environment for the Python commands below.

[replay_test.c](../examples/win32-transcript/replay_test.c) builds as an optional
C11 console diagnostic with the Win32 example's existing installed dependencies:

```powershell
cmake -S examples/win32-transcript -B build-c11-audit/win32 -DBF_BUILD_CAPTION_REPLAY=ON
cmake --build build-c11-audit/win32 --target BaffleReplay --parallel
# Bounded mono 16k files; equal frame counts, up to 20 seconds:
BaffleReplay.exe raw.wav asr-input.wav PATH/TO/models/parakeet NEW-trace.txt
python tests/public_caption_replay.py
```

The diagnostic runs the actual application worker, with a private file-fed source;
it does not introduce replay/device/model ownership into Baffle's core. Per-call
SHA256 logging is diagnostic overhead, not a device callback or CPU benchmark.
The hardened current build also passed a digital-silence/clean-retirement smoke
without text, drops or microphone access. Native example tests passed 3/3.

[public_caption_replay.py](../tools/public_caption_replay.py) prepares input from
[a pinned public manifest](evidence/2026-11-23-asr-comparison/manifest.json), executes
both whole-file applications and all three live profiles, and retains model hashes,
receipts, window identities, word/character edits, drops and blank hypotheses.
Its `--stage ns` mode can separately test the prepared fixed NS waveform; this
raw baseline does not qualify live NS adaptation or capture hardware.

The full historical comparison requires locally saved C++/early-C11 snapshots and
binaries under `build-public-live/` / `build-c11-audit/`; these are not fresh-checkout
artifacts. Current C11 replay is maintained in source and can be rebuilt using the
example's documented SDK/model prerequisites. Local reproduction commands:

```powershell
python tools/public_caption_replay.py --public-manifest build-asr-comparison/fixtures/manifest.json --models build-acoustic/models/parakeet --cpp-exe build-c11-audit/baseline-binaries/BaffleLive-cpp.exe --c-exe build-c11-audit/win32/BaffleLive.exe --cpp-replay build-public-live/build/replay_cpp.exe --c128-replay build-public-live/build/replay_c128.exe --c-replay build-public-live/build/native-app/BaffleReplay.exe --stage raw --output NEW-raw-campaign
# Optional offline controls using completed campaign rows, never live normalization:
python tools/public_caption_replay.py --public-manifest build-asr-comparison/fixtures/manifest.json --models build-acoustic/models/parakeet --cpp-exe build-c11-audit/baseline-binaries/BaffleLive-cpp.exe --c-exe build-c11-audit/win32/BaffleLive.exe --controls-from NEW-raw-campaign --output NEW-controls
```

Two harness failures were retained and corrected: the initial driver rejected a
legitimate empty transcription (exit 2), and the temporary controls driver visited
an incomplete manifest case twice. Exclusive-output guards prevented overwrites.
The maintained parser now records blanks as recognition failures, and controls
use completed rows with hash-checked deduplication. All eight recordings remain
in the matrix; Hungarian runs and remaining controls continued in fresh directories.
The original interrupted English checkpoint manifest contains an uncompleted
Hungarian entry, so its final checkpoint hash is historical rather than the current
manifest hash. Canonical combined results independently verify every receipt and
supplied input hash. No failed evidence was replaced with a favorable rerun.

[Retained results](evidence/2026-11-23-public-caption-replay/summary.json) contain
64 scored executions, window identities, references, every hypothesis, metadata
and the initial driver failures. Public WAV/model files and historical executable
snapshots stay in ignored caches. No private user recording, new remote CI run,
cloud recognition or physical acoustic measurement is part of this experiment.

The subsequent [quiet-continuation correction](quiet-caption-fix.md) retains this
baseline and the rejected candidates, and adds fresh holdout and default-NS checks.
