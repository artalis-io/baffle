# Public-speech ASR efficiency pilot — 2026-11-23

**PARTIAL: local runs complete; the GPT comparison has not run because no OpenAI
API credential is configured. No cloud/local winner is established.**

This is an external qualification harness, not an inference or cloud dependency
added to Baffle. The native application and DSP remain independent of it.

## Executed comparison

Eight publisher-transcribed [FLEURS](https://huggingface.co/datasets/google/fleurs)
recordings: four English and four Hungarian. Revision
`70bb2e84b976b7e960aa89f1c648e09c59f894dd`, CC-BY-4.0, attribution: Google Research / FLEURS.
Selection was frozen before decoding: first four distinct sentence IDs per language
with mono 16 kHz audio lasting 2..20 s. These are eight recordings, not necessarily
eight independent speakers. Public transcripts are independent of the tested
recognizers, but were not newly reviewed by a human here. No private microphone
recordings are included or approved for cloud upload.

Each has an unmodified-recording control, deterministic colored noise at 10 dB
SNR, and simulated linear playback echo at 0 dB near/echo ratio. The echo uses a
different public speech clip as the render reference, with taps at 3/20/80 ms.
There is 500 ms pre/post padding, no VAD gating, and no expected-text prompt.
A per-case gain prevents clipping and is retained in the manifest. All consumers
receive the identical mono 16 kHz PCM16 WAV for each stage; the documented NS hop
is removed from offline file alignment, its final tail is drained, and frame
counts stay equal. Live algorithmic delay is still **10 ms**.

Two paired stages: raw and Baffle. Baffle is NS-only on control/noise and AEC+NS
on echo, with 18 dB NS, 200 ms AEC tail, AGC/VAD disabled. The raw echo recognizer
has no render reference: its near-end-target WER measures unwanted playback
contamination, **not** general correctness at transcribing all audible speakers.
The optional GPT+Baffle cell will use exactly the same processed WAV as Whisper.

Whisper-small multilingual INT8 uses fixed `en`/`hu` language, transcribe task,
greedy decoding, CPU/two configured threads. Whisper-tiny.en INT8 is an English-only
size/speed control; no Hungarian claim is made for it. Each runs two repetitions
of fresh-stream batch recognition, after an unscored model warm-up. Total: **144
measured local decoder calls**, 96 small and 48 tiny.en. This is not the existing
Win32 rolling-window caption scheduler or streaming ASR.

## Accuracy

Pooled edit rates, not an average of per-clip percentages. Case/punctuation are
normalized, Hungarian accents retained, and digit/number-word representations are
not equated. Each row represents four unique recordings, each repeated twice;
repetition does not increase the independent sample size. Empty hypotheses,
first-word results and S/D/I counts are retained per call.

| Model/language | Condition | Raw WER | Baffle WER | Raw CER | Baffle CER |
|---|---|---:|---:|---:|---:|
| Whisper-small EN | clean | 9.1% | 10.2% | 3.0% | 4.3% |
| Whisper-small EN | noise-10db | 13.6% | 13.6% | 6.0% | 5.8% |
| Whisper-small EN | simulated-echo-0db | 50.0% | 28.4% | 30.7% | 17.3% |
| Whisper-small HU | clean | 49.4% | 45.8% | 12.1% | 9.7% |
| Whisper-small HU | noise-10db | 57.8% | 55.4% | 16.7% | 15.8% |
| Whisper-small HU | simulated-echo-0db | 94.0% | 59.0% | 73.4% | 17.4% |
| Whisper-tiny.en EN | clean | 10.2% | 9.1% | 6.3% | 4.5% |
| Whisper-tiny.en EN | noise-10db | 25.0% | 26.1% | 13.1% | 12.3% |
| Whisper-tiny.en EN | simulated-echo-0db | 96.6% | 105.7% | 65.3% | 72.1% |

Do not interpret the echo column as physical room/AEC qualification. Preserve
unfavorable per-case results; there was no suppression/gain/model tuning against
these samples. Public benchmark training contamination cannot be ruled out.
Identical hypotheses across the two repetitions: small
48/48,
tiny.en 24/24.

## Local computation and resources

Windows 11 x64, Ryzen 9 5950X (16 cores/32 logical CPUs), sherpa-onnx
`1.12.40`, CPU provider. Background desktop activity was not
controlled; no deliberate inference contention was added. CPU time includes the
benchmark process; core equivalents mean CPU-seconds / wall-seconds, not percent
of the entire 32-thread host. GPU activity/power were not sampled.

Times below are **warm-model fresh-stream batch decode milliseconds per complete
recording**, including the native inference call and wrapper. They exclude model
loading, WAV reads, DSP preprocessing and network. Clip durations vary; RTF
normalizes execution seconds by audio seconds. Small distributions are descriptive,
not hard deadlines, confidence intervals or first-word/display latency.

| Model | Language | Stage | Calls | P50 ms | P95 ms | P99 ms | Max ms | Median RTF | CPU core equivalents |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| small | en | raw | 24 | 2165 | 3315 | 3335 | 3336 | 0.231 | 1.96 |
| small | en | baffle | 24 | 2102 | 3429 | 3541 | 3564 | 0.222 | 1.96 |
| small | hu | raw | 24 | 4522 | 8953 | 9358 | 9374 | 0.335 | 1.97 |
| small | hu | baffle | 24 | 4212 | 6676 | 6709 | 6715 | 0.333 | 1.97 |
| tiny.en | en | raw | 24 | 463 | 658 | 679 | 685 | 0.047 | 1.82 |
| tiny.en | en | baffle | 24 | 420 | 925 | 1013 | 1029 | 0.043 | 1.84 |

Observed process RSS peaks: small **1242 MiB**,
tiny.en **428 MiB**. These include
Python QA, runtime, weights and allocator caches; they are not the DSP workspace
or a clean native-EXE footprint. Initialization times/configurations are separate
in the retained results.

Baffle native processing is separately timed per 10 ms frame. Across the prepared
files, the largest per-file P99 was **0.0244 ms NS-only**
and **0.0548 ms AEC+NS**. Largest observed calls:
0.0738/0.1242 ms respectively.
Caller workspaces are **14,576 / 136,544 bytes** respectively. Per-file timing
CSVs and initialization/subprocess wall time remain in the local fixture cache.
`dsp_plus_decode_ms` is a sum of separately measured DSP/decode execution, not a
measured end-to-end device or conversational latency. No energy/total operating
cost estimate is fabricated.

## GPT side: ready to run, not measured

The current [OpenAI file transcription guide](https://developers.openai.com/api/docs/guides/speech-to-text)
recommends [`gpt-transcribe`](https://developers.openai.com/api/docs/models/gpt-transcribe).
It accepts the completed file via `/v1/audio/transcriptions`; the harness supplies
`languages[]=en|hu`, no reference text, no keyword hints, and no automatic retries.
The documented model is an alias without an independently pinned snapshot here;
request date, returned model identity if present, usage and request IDs are retained.
This API benchmark is not a measurement of ChatGPT's internal voice pipeline.

The validated plan is 48 calls, **9.638 uploaded audio minutes**.
At the model page's checked $0.0045/min rate, the duration-only estimate is
**$0.0434**, not an invoice or a measured charge.
The client defaults cap the run at 48 calls/20 minutes; credential, path, hash,
public-fixture, geometry, duration and budget checks precede any request. It rejects
authorization-bearing redirects. It never reads browser/Codex login tokens.

Cloud request latency will include network upload, service processing and response;
provider CPU/GPU/memory/power and server-only compute remain **unobservable**. Client
CPU/memory cannot establish provider hardware efficiency. A file request's final
transcript latency does not establish streaming partial-word or barge-in latency.
No request has been sent and no GPT accuracy, latency or cost result exists yet.

## Reproduction

Fixture/model downloads use the Hugging Face CLI and explicit revisions:

```powershell
hf download google/fleurs --type dataset --revision 70bb2e84b976b7e960aa89f1c648e09c59f894dd --include 'parquet-data/hu_hu/test-*' --include 'parquet-data/en_us/test-*' --local-dir build-asr-comparison/dataset
hf download csukuangfj/sherpa-onnx-whisper-small small-encoder.int8.onnx small-decoder.int8.onnx small-tokens.txt --revision 8f3c18b358db4d1f2fc1eae49d75cd20989e4309 --local-dir build-asr-comparison/models/whisper-small
```

Use a separate QA environment containing NumPy, SciPy, SoundFile, PyArrow,
psutil and sherpa-onnx 1.12.40. The executable runner builds against installed
public Baffle; its SHA256 is in the fixture manifest. Paths below are this host's
working commands; choose fresh output directories to retain earlier evidence:

```powershell
python tools/asr_cloud_compare.py prepare --dataset build-asr-comparison/dataset --runner build-c11-audit/tools-hail/baffle_acoustic_runner.exe --output build-asr-comparison/fixtures --clips-per-language 4
python tools/asr_cloud_compare.py local --input build-asr-comparison/fixtures --output build-asr-comparison/whisper-small --model-dir build-asr-comparison/models/whisper-small --model small --threads 2 --repeats 2
python tools/asr_cloud_compare.py local --input build-asr-comparison/fixtures --output build-asr-comparison/whisper-tiny --model-dir build-acoustic/models/whisper --model tiny.en --threads 2 --repeats 2
python tests/asr_cloud_compare.py
# Once OPENAI_API_KEY is available in this process's environment:
python tools/asr_cloud_compare.py cloud --input build-asr-comparison/fixtures --output build-asr-comparison/gpt-transcribe --model gpt-transcribe --max-minutes 20 --max-requests 48
```

[Retained evidence](evidence/2026-11-23-asr-comparison/summary.json) includes file/model
hashes, source identities, publisher references, every hypothesis/error count,
CPU/wall/memory measurements and aggregate statistics. Public audio and model
weights remain in ignored `build-asr-comparison/`; no user audio is used. Guard
regressions ran without network/credentials, including error/failure accounting.

The experiment remains **PARTIAL** until actual GPT results are available. Even
then, eight read-speech clips and synthetic mixtures will not establish live
conversational, physical acoustic, energy or general Hungarian production readiness.
