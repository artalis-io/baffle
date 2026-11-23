# Native Win32 live transcription

Standalone Windows x64 application: installed Hail capture -> installed Baffle
NS (optional AGC) -> sherpa-onnx Parakeet CPU inference -> Win32 controls. No Python,
Tk, browser, .NET, GPU, child capture executable or cloud service at runtime.
The application is an optional external consumer, not a speech runtime added
to the Baffle core. Only installed public Hail/Baffle headers are used.

The application, caption logic and capture helper are C11. The executable statically
links the C/C++ runtime and all non-system libraries. Sherpa-onnx and ONNX Runtime
remain third-party C++ implementations, called through the sherpa C API; this is
not a complete C11 ASR engine rewrite.
Models remain ordinary adjacent files:

```
BaffleLive.exe
models/parakeet/encoder.int8.onnx
models/parakeet/decoder.int8.onnx
models/parakeet/joiner.int8.onnx
models/parakeet/tokens.txt
```

Keep model attribution and third-party notices with any distributed copy. The
local test package also contains license reference files and the original model
card. Binary dependencies are pinned to sherpa-onnx v1.12.40, source commit
`d795ccd164bd2360ffda6c3431b1316e2c898be2`, Windows static MT Release no-TTS.
The prebuilt package SHA256 is
`dc8d5efa6eb404a8166aed06de36a547ade17729fe1e39fc4bf0af08b9b91efa`.
The package's ONNX Runtime is prebuilt; this application does not rebuild or
independently qualify that dependency. Reference notices supplied from the
previous local ONNX Runtime installation are labeled as reference material,
not an exact dependency audit for a public release.

Open the EXE, select the microphone and click **Start**. Shure is selected
when present. **Stop** or closing the window stops capture. No audio or
transcript file is saved during ordinary dictation. `--models DIRECTORY` overrides
the model location. Parakeet v3 automatically recognizes its supported languages,
including Hungarian; no language-forcing parameter is invented.

### Speech-preservation revision

The English interface shows one continuous **Live transcript** containing the
entire current hypothesis, including provisional words. It scrolls to new text;
completed utterances remain visible and are also added to **Transcript history**.
Empty/whitespace recognition updates preserve the most recent partial, including
a carried rolling-window hypothesis. Nonempty corrections still replace text;
utterance resets clear processing context. Agreement remains internal and is not
a calibrated confidence measure. The former split prefix/suffix display could
make partial sentences look truncated.

The built-in charcoal/green/amber appearance uses standard controls and a small
GDI color/bevel treatment inspired by classic audio players. No bitmap assets,
external theme runtime or Winamp skin-file compatibility is introduced.

At eight seconds, audio is **retained across the window boundary**, not cleared.
After six seconds, a 250 ms low-energy pause may move the window earlier. The
last two words plus at least 600 ms context are protected using validated model
token times; at least two seconds are normally retained, with a three-second
fallback when timestamps cannot be matched to the returned text. This adds no
new DSP buffering; each decode still has at most eight seconds of input.

Overlap removal requires at least two matching words in audio estimated to
precede the carried-prefix boundary. Ambiguous/no-timestamp overlap is kept,
possibly duplicating words, instead of deleting potentially new speech. Word
starts strictly before the carried-prefix boundary count as overlap candidates;
model times remain estimates, not physical timing guarantees. The live transcript
can revise if a later decode disagrees.
Capture gaps retain already recognized text with a visible gap marker, reset
the decode context, and never stitch across epochs. Stop drains the bounded
pending queue and finalizes remaining speech before retiring its owners.

The processing selector offers raw, NS-only and NS+AGC. After the first
speaker-confirmed Hungarian paired recording, the application defaults to
**NS-only**. NS remains 18 dB; AGC remains available at its unchanged -18 dBFS
target. These are application choices, not changes to Baffle algorithm defaults.

The original paired fixture yielded 7/26 normalized word errors raw, 4/26 NS,
and 11/26 NS+AGC (literal counts 8/26, 4/26, 12/26). NFC/case/punctuation
normalization retains accents; the normalized counts additionally equate `3`
with `három`. All original evidence is retained locally. Re-decoding the
identical PCM reproduced all three hypotheses. Constant-gain controls changed
recognition without clipping, indicating input-level sensitivity, not proof of
an AGC implementation defect. One recording is not general Hungarian acceptance.

A source audit then found that the earlier comparison omitted VAD feedback
that the live capture profile still enabled. The same raw recording was replayed
with that feedback: 7/26 raw, 6/26 NS+VAD, 9/26 NS+AGC+VAD. The caption UI does
not use VAD activity for endpointing; its endpointing uses raw energy. The native
caption profile therefore explicitly disables this unused feedback, and both
the live and paired NS/AGC paths now use identical stage masks (2 and 6). This
matches the best observed isolated NS profile without changing Baffle's pipeline
or the legacy capture-tool defaults. `--compare-vad-feedback` retains a
reproducible diagnostic control for the historical live profile. No words are
filtered or corrected from an expected transcript.

### Paired Hungarian check

**Record comparison (20 s)** explicitly records and saves a local test.
Read the displayed Hungarian text when the recording counter starts. The
application records one bounded raw signal, retires capture, then runs raw,
NS-only and NS+AGC through the same CPU recognizer and the same stage configuration
used by live captions. Results appear under recent
speech and in an adjacent `test-results/<unique-run>/` folder containing three
16 kHz float WAV files and `comparison.txt`. SHA256 identifies the exact source
PCM and each variant. NS's one-hop tail is flushed and the variants have identical
frame counts, so output delay/padding cannot bias the comparison.

The suggested prompt is not assumed to be the delivered transcript. WER/CER
remain unavailable until a human verifies what was actually said; no Hungarian
quality improvement is claimed from an English fixture or text-only tests.
Comparison uses bounded complete-clip offline decoding (up to 20 s), independently
of the live caption window scheduler, to isolate preprocessing effects.

An existing recording can be replayed without touching the microphone:

```
BaffleLive.exe --models PATH/TO/models --compare-wav mono16k.wav --report NEW-EMPTY-DIRECTORY/results.txt
```

Use a fresh results directory: prior measurements are not overwritten.

## Build

Use CMake 3.20+, an x64 ClangCL C11 toolchain (Visual Studio Build Tools), installed static `/MT`
Hail/Baffle packages, matching sherpa source headers and static MT libraries:

```
cmake -S examples/win32-transcript -B build-win32 -A x64 -DCMAKE_PREFIX_PATH=PATH/TO/SDK -DSHERPA_SOURCE_DIR=PATH/TO/sherpa-source -DSHERPA_STATIC_DIR=PATH/TO/static-MT-package
cmake --build build-win32 --config Release --parallel
```

Both Hail and Baffle must be configured with
`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` and built static. The application
needs no SpeexDSP because it has no render/reference leg and does not perform
AEC. Enabling AEC would require a real aligned render reference, not zero PCM.
This is a listening/caption demonstration; it does not speak replies.

`build.ps1` reproduces these steps using sibling Hail source and optional existing
model files. Third-party binaries are downloaded only during this explicit build.

## Execution and ownership

Hail device callbacks remain bounded. An application capture thread reads 10 ms
mono 16 kHz blocks and runs Baffle with preallocated state. The UI thread never
calls inference. A separate inference owner consumes a 512-slot SPSC queue;
the capture producer drops incoming blocks when full and reports their count.
Capture/sample-coordinate loss or epoch changes clear the decode context.
No application inference wait or display mutex is used by the capture sink.
The Hail capture queue has 32 slots. The caption window retains 60 segments,
limited to 64,000 UTF-16 units. Closing waits for owners to finish, without
destroying their state while a decode/callback is active. Sessions have an
eight-hour capture limit.

ASR uses **repeated bounded offline decoding**, not a native streaming model:
at most eight seconds of audio per decode, nominal 600 ms refresh, 250 ms onset
context and 900 ms silence endpointing. Inference duration adds to refresh time.
These are demonstration policies, not validated speech endpointing or speaker
attribution. Overlap reduces forced-boundary truncation; remaining recognition
errors, model timing errors and duplicate words still require speech evaluation.
NS adds its existing 10 ms buffer. Queues and batching add latency; no measured
end-to-end latency guarantee is made. Playback speech can appear in captions.

## Verification commands

These explicitly requested checks write receipts; ordinary GUI operation does not:

```
BaffleLive.exe --queue-test --report queue.txt
BaffleLive.exe --capture-test --report capture.txt
BaffleLive.exe --models PATH/TO/models --self-test mono16k.wav --report recognition.txt
```

Queue testing exercises one producer and consumer with 200,000 attempted blocks
and verifies ordering and data integrity. Capture testing opens/processes/closes
the detected Shure three times. ASR testing performs actual native CPU inference
on the supplied audio, with no Python. Neither test certifies acoustic AEC,
first-phoneme preservation, real-time deadlines or recognition quality.

Local checks on 2026-11-21: clang-cl 19.1.5 `/O2 /MT /W4 /WX`, successful native
ASR, queue integrity, three Shure capture cycles with zero lost/nonfinite samples,
and two GUI start/stop cycles. Import-table and loaded-module inspection found
only Windows system DLLs, with no Python/sherpa/ONNX/VC-runtime DLL beside the EXE.
No new remote CI, clean-machine, sanitizer or acoustic qualification is claimed
for this Windows-only demonstration. ABI of the underlying libraries remains
provisional. Model files and local build evidence are not committed.

The speech-preservation revision additionally exercises Hungarian Unicode text,
three-decode agreement/correction, epoch reset, unfinished words, repeated words,
timestamp fallback, bounded overlap and multi-roll continuity via `caption_tests`.
CTest also runs actual concurrent SPSC integrity. Native recorded-English paired
ASR is retained as an integration check, not as Hungarian acceptance evidence.

## C11 implementation audit (2026-11-21)

Owned application/caption/capture sources now compile as C11. CTest additionally
runs `--wave-test`: 56 valid/malformed WAV cases without models or capture. The
final C11 executable matches the prior C++ program's PCM hashes and hypotheses
for two Hungarian and two English recordings across raw/NS/NS+AGC. A fresh native
CPU self-test also passed from a system-only PATH and unrelated working directory.
The GUI initializes and closes without starting capture; imports/loaded modules
remain Windows system libraries. The full [C audit](../../docs/c-audit-2026-11-21.md)
records core sanitizer/fuzz/installed-consumer checks separately from application
checks. The new C11 GUI has not repeated the historical physical capture cycles
listed above, and third-party sherpa/ONNX implementations remain C++.


## Live accuracy investigation (2026-11-21)

Whole-file PCM/hypothesis parity does not establish live-caption parity. An
isolated local diagnostic now paces the same existing Hungarian raw/NS recordings
through the actual saved C++ application loop and current C11 loop, including
endpointing, ring handoff, rolling windows, native model inference and stitching.
Only the capture source is replaced; no physical recording is inferred. Timing
and partial-decode boundaries can vary between executions.

On the second recording both implementations produced identical final words with
zero dropped frames. On the first, the original queues dropped 3 frames (C++) and
8 frames (C11) during decoder stalls; this is one observed run, not a language
performance comparison. Word errors were 7/26 and 6/26 respectively; the second
recording had 6/26 in both. Window-boundary duplication remains reproducible in
both implementations. These recordings do not reproduce the reported C11-specific
recognition degradation, and do not disprove it on new microphone input.

The handoff now holds 512 ten-millisecond frames (5.12 seconds), still bounded,
instead of 128. This is stall headroom, not added algorithmic lookahead or a promise
of zero queueing latency. It cannot solve sustained inference overload. A global
producer drop counter no longer resets context prematurely while earlier valid
frames are still queued: resets follow the consumed frame's actual gap. The
executable regression checks full capacity, bounded rejection and the precise
ordered gap boundary. Model/thread/DSP/window/refresh/endpoint settings are unchanged.

The corrected replays lost zero frames on both recordings, but word errors stayed
6/26 each. That closes an audio-loss defect; it does **not** establish restored
recognition quality. All before/after traces and the historical source harness
remain local under `build-live-ab/`. The subsequent public-corpus replay below
provides unattended regression inputs without requiring another user recording.

## Unattended public speech replay (2026-11-23)

Enable `BF_BUILD_CAPTION_REPLAY=ON` in this example's existing CMake configuration
and build the `BaffleReplay` target. Run:

```powershell
BaffleReplay.exe raw.wav asr-input.wav PATH/TO/models/parakeet NEW-trace.txt raw
```

Both WAVs must be mono 16 kHz with equal frame counts, at most 20 seconds.
Use the same file twice and the `raw` profile to isolate the caption wrapper from
DSP. For an aligned processed waveform, use `ns`; omission preserves the older
diagnostic's processed-profile behavior. The diagnostic
paces file PCM through the actual application worker and records decoded-window
hashes, hypotheses and queue drops. It never opens the microphone. It is diagnostic
tooling, not a callback-performance benchmark.

The [public caption replay report](../../docs/public-caption-replay.md) retains
eight English/Hungarian FLEURS recordings' results and reproduction instructions.
It reproduces quiet-speech context loss shared by the historical C++ and current
C11 wrappers; production energy-gate/model settings have not been tuned to it.

## Quiet continuation correction (2026-11-23)

The [follow-up correction and evidence](../../docs/quiet-caption-fix.md) use the
existing Baffle VAD only in **Raw** mode to retain quiet continuations after
speech has started. NS and NS+AGC retain their existing endpointing: experiments
did not justify enabling the new detector on those processed signals.
Startup still uses the original raw-RMS trigger. Broader onset changes were
rejected after first-word regressions. Entirely subthreshold speech can therefore
still fail to start; this is not general quiet-onset qualification.

The private detector runs on the caption worker's ASR PCM, with no feedback into
NS/AGC and no new PCM transformation. It preserves noise state across utterances
and resets on capture gaps. Its state is allocated before capture and retired
by the caption worker after processing stops. The existing 250 ms preroll, 900 ms endpoint,
600 ms refresh-after-decode interval and eight-second window remain unchanged.
An independent stitch correction stops a timestamp margin from excluding a valid
last overlapping word. Exact multiword evidence is still required before deletion.
Endpoint and rolling-pause counters are independent, so keeping quiet speech
does not also change the original raw-energy rolling-pause rule.
