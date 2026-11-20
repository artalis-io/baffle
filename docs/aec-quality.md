# AEC quality convergence, 2026-11-20

Acceptance remains **PARTIAL**. The original 8 dB requirement now passes on its
unchanged fixture. This closes a near-end conditioning limitation, not physical
acoustic, perceptual, ASR or general production qualification.

## Original failure and metric

Baseline PR #1 was `feat/dsp-engine` at `29feb81`. The live 27-job matrix then
had eight completed successes and 19 queued jobs; it was not a complete pass.
The archived earlier 24-job pass concerns a different source revision.

`tools/controlled_aec.py` is unchanged. It constructs 24 seconds of 48 kHz PCM
from two pinned Microsoft recordings, with synthetic same-clock echo taps
32@0.6 and 93@-0.15. The original near recording (including its noise) is the
known component on seconds 8..18. Reference/capture peaks are below clipping.

The acceptance number is:

`10 log10(sum((microphone - near)^2) / sum((output - near)^2))`

over that complete double-talk interval. It is **raw-component SNR improvement**,
not ERLE, SI-SDR, perceptual quality or a pure residual-echo measurement.
The original threshold remains >=8 dB, with near-component projection .8..1.2.
Far-only ERLE is scored separately on seconds 6..8. A zero-reference near-only
control measures signal conditioning even without echo.

| Unchanged controlled fixture | Baseline | Corrected |
| --- | ---: | ---: |
| Raw-component SNR improvement, dB | 6.6484 FAIL | 14.6034 PASS |
| Near-component projection | .90837 | .98872 |
| Output error SNR, dB | 7.9572 | 15.9121 |
| Zero-reference near-only error SNR, dB | 8.1676 | 17.8510 |
| Far-only ERLE, dB | 25.8454 | 25.8311 |

The original failed report and new report are separate artifacts. No segments,
thresholds, voices, gains, timestamps or echo taps were changed to obtain this
comparison. Earlier diagnostics relative to a backend-processed near oracle are
retained, but do not substitute for the raw-component target.

## Root cause and one-factor isolation

The pinned [MDF source](https://github.com/xiph/speexdsp/blob/1b28a0f61bc31162979e1f26f3981fc3637095c8/libspeexdsp/mdf.c)
applies `filter_dc_notch16` before near-end preemphasis. Upstream notch radii are
.9 below 12 kHz, .982 below 24 kHz and .992 above. Their nominal pole-radius
parameters correspond to roughly 134, 46 and 61 Hz at 8, 16 and 48 kHz. Radius
is not an exact -3 dB cutoff. This phase/amplitude response persists with an
all-zero reference; removing echo cannot recover the filtered near waveform.

The wrapper now uses `exp(-2*pi*20/sample_rate)` for the **same** DC rejector.
Its numerator still has a double zero at DC, and its pole magnitude
`sqrt(r*r + .7*(1-r)*(1-r))` remains below one for 0<r<1. The parameter provides
a consistent low-frequency policy rather than rate-bucket-dependent speech
distortion. It does not promise unity response at every speech frequency.
Twenty Hz was fixed before inspecting the OSR evaluation recordings; no
per-fixture control or public tuning knob was added.

Only [the integration wrapper](../src/speex/mdf_baffle.c) and its initialization
call in [aec.c](../src/aec.c) change DSP behavior. Upstream source files remain
hash-pinned and unmodified. Adaptive step size, filter length, double-talk logic,
preemphasis/deemphasis, S16 conversion and suppression are unchanged. There is
no new residual suppressor or buffering stage. The wrapper is private/hidden;
no public structure, function or ABI layout changes.

`tools/aec_diagnostics.py` varies one factor at a time, retaining commands and
output hashes. Output-error SNR, rather than the original improvement score, is
listed below; all these experiments use the same raw near-component oracle.

| Hypothesis / reproduction | Expected diagnostic | Observed | Conclusion |
| --- | --- | --- | --- |
| No reference, zero PCM reference | Near-only distortion independent of adaptation | Baseline error SNR 8.1676 dB; corrected 17.8510 dB | Capture conditioning is the dominant original score ceiling. |
| Tail 40 / 200 / 500 ms | Insufficient tail would materially worsen residual echo | Error SNR 8.000 / 7.957 / 7.992 dB | Tail length does not explain the ~8 dB near-error ceiling; far-only convergence differs. |
| 20 ms versus 10 ms hops | Framing defect would change the near distortion | Error SNR 7.998 versus 7.957 dB | No hop-accounting cause confirmed. |
| Reference gain half / double | Gain mismatch should adapt when aligned/unclipped | Error SNR 7.976 / 7.936 dB | Reference amplitude does not explain the ceiling. |
| Reference late 10 ms | A noncausal effective echo path should break cancellation | Error SNR 1.137 dB; far-only ERLE .493 dB | Alignment matters, but the original fixture has no such offset. |
| Reference early 10 ms | Additional causal delay remains within tail | Error SNR 7.916 dB | No hidden original alignment failure demonstrated. |
| Uncompensated +100 ppm reference | Drifting equal-index samples should degrade adaptation | Error SNR 5.861 dB; far-only ERLE 7.965 dB | Drift compensation is required; original fixture has identical clocks. |
| Only DC-notch radius changed | Near-only error should improve with similar echo attenuation | Error SNR 15.912 dB; far-only ERLE 25.831 dB | Confirmed original quality limitation, corrected at integration boundary. |
| Intermediate 40 Hz radius model | Conditioning-only error should change monotonically | Original improvement 9.9971 dB, zero-reference SNR 11.8399 dB | Supports filter-response explanation; not the selected policy. |
| 200 Hz tone plus DC, 8/16/48 kHz, 10/20 ms | Preserve tone while rejecting DC | Original 8 kHz error SNR -1.134 dB failed; corrected six cases pass >13 dB and DC mean <1e-4 | Independent numerical regression of the rate-consistent policy. |

S16 quantization remains a backend limitation, but identical quantization and
unclipped inputs across the notch experiment rule it out as the dominant cause.
Sample-rate mismatch is not present in the original fixture. Different-rate
transport and +/-drift are tested separately through the installed Hail adapter.
No evidence justifies changing MDF precision, adaptation or double-talk settings
for this defect. Longer filters, residual suppressors and AEC3 are not substitutes
for establishing this causal result.

## Recorded-source evaluation and remaining quality limits

Two unchanged, attributed [Open Speech Repository files](../tests/fixtures/osr/README.md)
provide male/female English, 8 kHz mono dry-source components. These are recorded
voices, not guaranteed noise-free studio speech. They are evaluation holdouts,
separate from the earlier Microsoft diagnostic/tuning fixture. The evaluation
uses fixed 24-second scenarios with both role assignments, 10/20 ms hops,
near-only, double-talk, low/high near levels, simulated room taps/background
noise/path change and a two-second playback gap. All input/output hashes,
commands, complete-interval scores and all one-second windows are retained.
Room responses/noise/echo are explicitly simulated, not measured acoustics.

Run `tools/speech_quality.py --assert-preservation` for the unchanged .8..1.2
whole-component projection bounds. This is a gain-preservation regression, not
a complete speech-quality gate. Reports also retain raw error SNR, zero-mean
SI-SDR, output power and far-only microphone/output energy ratios. With noise,
the last ratio includes noise; it is not isolated residual echo. Near-only input
has zero pre-AEC error, so its SNR improvement is unavailable rather than an
invented finite score. High near/weak echo can already have excellent input SNR;
phase distortion can reduce raw SNR despite preserved gain. Those cases remain
in the report, without a universal 8 dB improvement claim for every new scenario.

The first energy-above--60 dBFS 100 ms diagnostic includes pre-speech noise/DC
in these recordings and has weak projections in several cases. It is **not**
annotated user-speech onset or proof that an interruption survives. We retain
this limitation instead of selecting later, better-scoring speech segments.
Annotated clean speech/onsets, Hungarian recordings, ASR WER, calibrated room
responses, real double-talk and acoustic cancellation tails remain outstanding.
No external recognizer or model-based perceptual scorer is configured.

The four existing Microsoft real acoustic traces must still be considered
separately. Their uncertain delay/drift/noise and weak results are not replaced
by the controlled or OSR simulated-room scores. Production acceptance remains
PARTIAL until calibrated, deployment-representative quality is demonstrated.

## Transport and real-time contracts

Capture/reference metadata, epochs and clock domains remain independent. Hail's
installed-public-API fixture covers differing rates, variable quanta, +/-drift,
overrun/underrun, repeated cancellation followed by new PCM, restart/recovery,
bounded backpressure and shutdown. A canceled generation cannot submit stale
queued PCM. Actual previously rendered sound may still have a room tail; a
flush acknowledgement never proves acoustic silence. Generation-only changes
preserve the learned echo path; discontinuity/missing-reference return resets
adaptive state. Missing reference remains F32 passthrough and does not signal
silence. No Hail source or public contract change was required.

One processing owner per context remains mandatory. Reset/deinit require
quiescence; concurrent calls on one context are unsupported. Separate contexts
are concurrent. NaN/Inf regressions and the AEC fuzzer check that rejected input
leaves caller storage and output unchanged. Loops, FFT dimensions, channels and
tails are configuration-bounded; no processing allocation, I/O, lock, callback,
worker, GPU synchronization or lazy initialization was added. The new `expf`
executes only at initialization. Heap guards, import/export checks, sanitizers
and fuzzing are evidence with explicit scope, not a WCET proof.

See [performance](performance.md) for 10/20 ms frame costs, workspace and
deadline evidence. The correction adds zero buffering frames and no process-time
operations; it changes an existing filter coefficient. A 20 ms hop may lower
CPU per audio second but requires more frame assembly than 10 ms. No SIMD or
GPU optimization is justified by this coefficient-only correction.

## Reproduction

```sh
cmake -S . -B build-quality -DCMAKE_BUILD_TYPE=Release -DBF_WITH_SPEEXDSP=ON -DBF_BUILD_TOOLS=ON
cmake --build build-quality --parallel
ctest --test-dir build-quality --output-on-failure
python3 tests/wav_qualification.py build-quality/baffle_qualify
python3 tools/controlled_aec.py build-quality/baffle_qualify build-recorded corrected.json --fetch --assert-raw-target
python3 tools/speech_quality.py build-quality/baffle_qualify build-osr osr.json --assert-preservation
# Same extended offline tool and benchmark can be built against baseline 29feb81:
python3 tools/aec_diagnostics.py baseline/baffle_qualify build-quality/baffle_qualify build-recorded build-diagnostics diagnostics.json
```

The diagnostic baseline changes only the offline CLI/benchmark harness to accept
tail/hop options; the DSP source remains `29feb81`. Exact source hashes, build
flags, executable hashes, original failure and new reports accompany the evidence.
