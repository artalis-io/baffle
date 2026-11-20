# Reproducible speech and acoustic qualification

No physical acoustic, recorded-speech corpus, audible-tail or device-timestamp
accuracy claim is made by this repository's synthetic tests. The following work
is required before deployment readiness, independently of Hail's own Gate E.

## Offline AEC fixture tooling

```sh
cmake -S . -B build-quality -DCMAKE_BUILD_TYPE=Release -DBF_WITH_SPEEXDSP=ON -DBF_BUILD_TOOLS=ON
cmake --build build-quality --parallel
build-quality/baffle_qualify microphone.wav aligned-render-reference.wav residual.wav 64000 64000
python3 tests/wav_qualification.py build-quality/baffle_qualify
```

The last two arguments are independently annotated far-end-only interval start
and length in frames, not seconds. Both WAVs must be PCM16 mono/stereo, 8..48 kHz
divisible by 100, identical rates/lengths, and already aligned by a documented
measurement. The tool never infers an alignment or invents clock observations.
It exercises only AEC, trims terminal padding, writes real output frames and
reports 10log10(microphone_energy/residual_energy) on that interval. Without a
valid nonzero far-only annotation ERLE is unavailable. ERLE on double-talk is
not a speech-preservation metric. Files are limited to 256 MiB each; loading and
file I/O are offline control work, outside DSP processing.

An optional seventh argument, `processing_rate`, applies Baffle's own streaming
SRC identically to both legs before AEC. Interval arguments remain in original
input-frame units and are converted by the nominal ratio. The FIR delay is
reported; no source clock/drift calibration is inferred. No artificial SRC tail
is appended. Both even/odd partial endings are regression-tested at 16->8 kHz.

The Python regression generates explicitly synthetic broadband echo, tests ERLE
>20 dB after training, verifies a 17-frame partial ending and rejects malformed/
mismatched files. It is not a recorded acoustic fixture.

## Exploratory real-recording evidence

[Microsoft AEC Challenge](https://github.com/microsoft/AEC-Challenge) provides
real test recordings separately from synthetic data. The optional runner
`tools/recorded_aec.py` selects four fixed ICASSP2022 cases at upstream commit
`8f861f0f8ffca26a3d1d16f17f2594ea89abe7ac`, verifies their Git LFS hashes and
records source lengths, tail crops and commands. Review upstream dataset terms
before opting into download; its code license is not a blanket dataset license.
No audio or derived WAV is redistributed in this repository, and this optional
experiment is excluded from normal builds/CI. Cached runs need no network.

```sh
python3 tools/recorded_aec.py build-quality/baffle_qualify build-recorded report-48k.json --fetch
python3 tools/recorded_aec.py build-quality/baffle_qualify build-recorded report-16k.json --processing-rate 16000
```

Three far-end-only cases are scored on seconds 6..22, with a 200 ms Speex tail.
A fourth double-talk case exercises data delivery/finite output only: it has no
clean near-end oracle, so no speech-preservation score or double-talk ERLE is
reported. Unequal file tails are explicitly cropped to their common prefix;
index zero is unchanged. A 1 kHz block-mean correlation probes positive signal
lags 0..500 ms in windows starting at 0/8/16 seconds; this is a coarse diagnostic,
not device timestamps or calibrated alignment. No waveform shift, drift
correction or score-driven parameter search was applied.

| Fixed far-only case prefix | 48k energy reduction (dB) | Both legs resampled to 16k (dB) |
| --- | --- | --- |
| 00113069 | .8234 | 1.1194 |
| 00dPPLGk, movement | .0101 | -.0087 |
| 02462e78 | 6.6208 | 6.6474 |

These weak results do **not** qualify AEC quality. Lowering the rate alone did
not resolve them. The movement case also has weak/variable correlation, and
the recordings provide no clock observations, clean near-end oracle or calibrated
noise floor. Their full energy ratio is not a pure residual-echo measure. The
next quality work must establish delay/drift alignment and echo/noise ground
truth, then compare cancellation/double-talk performance and consider AEC3 if
needed. Synthetic success cannot override this recorded-evidence limitation.
Full manifests/results are retained in the evidence directory.

## Controlled recorded-speech diagnostic

```sh
python3 tools/controlled_aec.py build-quality/baffle_qualify build-recorded controlled.json --fetch
# Acceptance check retains the original threshold (failed at 29feb81):
python3 tools/controlled_aec.py build-quality/baffle_qualify build-recorded controlled.json --assert-raw-target
```

Two fixed, hash-verified recordings from the same upstream pin provide far-end
speech and a known near component (including its recorded noise). The tool
constructs a same-clock linear echo with taps 32@0.6 and 93@-0.15 at 48 kHz,
adds near speech during seconds 8..18, and runs a 200 ms AEC tail. This removes
unknown relative clocks from this experiment; it does not model a real room or
provide clean speech/perceptual ground truth. No audio is redistributed.

At `29feb81`, the measured near-component projection was **0.9084**, with raw-component SNR
improvement **6.6484 dB**, below the predeclared **8 dB** target. That failure is
retained. Far-only ERLE at seconds 6..8 was **25.8454 dB**. A zero-reference
near-only control had **8.1676 dB** raw-component SNR, exposing the selected
Speex DC-notch/preemphasis/deemphasis response. Error SNR relative to that
backend-processed near control was **20.4415 dB**; this diagnostic does not
replace the failed raw target or establish speech quality. Further recorded
double-talk, moving-path and physical acoustic qualification remains required.

The [AEC convergence report](aec-quality.md) preserves that failure and isolates
the upstream capture DC-notch response. A rate-consistent 20 Hz pole-radius
policy now gives **14.6034 dB** improvement and **.9887** near-component projection
on the unchanged original fixture; the **8 dB** requirement passes. This is not
a physical or perceptual quality pass. Two attributed, redistributable English
OSR source recordings add a separate evaluation holdout suite:

```sh
python3 tools/speech_quality.py build-quality/baffle_qualify build-osr osr.json --assert-preservation
```

Both roles, 10/20 ms frames, near-only/double-talk, low/high near levels, simulated
room taps/noise/path change and playback gaps are retained, with complete-interval
raw SNR/SI-SDR/projection and all one-second power windows. Source conditions and
hashes are in `tests/fixtures/osr/README.md`. Early energy windows include noise
and do not qualify speech onset; no Hungarian, recognizer WER or measured room
response is supplied. The original real acoustic traces remain unqualified.

The offline tool's optional eighth/ninth arguments configure tail length and
10/20 ms framing for controlled diagnostics; existing invocations are unchanged.

## Physical procedure and required records

Use Hail's existing [qualification infrastructure](https://github.com/artalis-io/hail/blob/main/docs/gate-e.md),
[WASAPI](https://github.com/artalis-io/hail/blob/main/docs/wasapi-qualification.md),
[CoreAudio](https://github.com/artalis-io/hail/blob/main/docs/coreaudio-qualification.md),
and [loopback tooling](https://github.com/artalis-io/hail/blob/main/docs/loopback-qualification.md).
Baffle does not acquire devices. Preserve raw capture/reference PCM and Hail
block metadata on an application worker, never filesystem I/O in an audio callback.
Do not substitute Baffle's virtual fixture for native-device measurements.

1. Record OS/build, CPU, device IDs, driver/firmware, backend, rate/channels,
   quantum/queue capacities, Baffle commit/options/tail/NS strength, reference
   provenance and uncertainty. Record clock/domain/epoch/generation sidecars.
2. Calibrate electrical or acoustic loopback with a known signal and independent
   recorder. State cable routing or speaker/mic distance/room, recorder clock,
   correlation method, threshold and uncertainty. Electrical loopback is not
   acoustic cancellation qualification; software loopback is neither.
3. Run at least 30 minutes of continuous duplex per supported deployment shape,
   with and without representative local ASR/TTS/LLM CPU workloads. Record per-hop
   processing and scheduling time, occupancy, losses, drift and memory high-water.
   Report median/P95/P99/max, misses, duration and confidence/limitations.
4. Use documented far-only, near-only and double-talk intervals at several SNRs,
   speaker volumes and distances. Keep clean near-end ground truth where possible.
   Measure far-only ERLE/residual, convergence and path-change recovery. Evaluate
   near-end preservation with aligned clean speech and listening tests, not ERLE.
   Include moved speakers, changing delay, clipping, independent clocks and
   sustained +/-drift. State alignment compensation and metric limitations.
5. Exercise missing references, underruns, capture overrun, epoch reset, stop/
   restart, unplug/replug, default change, permission denial, service restart and
   sleep/resume under the native provider's supported recovery policy. Verify
   fresh timing mappings, bounded memory and no retained borrowed buffers.
6. During double-talk cancel playback repeatedly, including writes/shutdown races
   on the Hail side and immediate new generations. Record separately: logical
   invalidation, native flush acknowledgement, observable final presentation,
   and independently measured electrical/acoustic tail. Confirm capture continuity
   and generation isolation. Never infer acoustic silence from acknowledgement.

Qualify Windows WASAPI, macOS CoreAudio, Linux PipeWire and ALSA separately. No
universal device-latency threshold is imposed without calibrated configuration
evidence. Agree per-device P99 latency/tail budgets before the physical run.
Reference uncertainty must fit the alignment budget; unavailable observations
remain unavailable and should cause explicit bypass under strict policy.

## Speech conditioning and release blockers

Classical VAD confidence is not a calibrated probability. Evaluate real labelled
speech/noise for false positives/negatives, onset/offset, fricatives, whispers,
music and echo during double-talk. NS needs stationary/nonstationary noise and
speech-distortion tests; AGC needs gain pumping, silence/noise and clipping tests
with realistic speech. Compare stage orders on the same corpus, compensating
documented delay. Optional neural stages require an independent measured reason.

Archive fixture licenses/consent, hashes, annotations, calibration, raw outputs
and commands. Physical results must identify what was measured and what remains
estimated. ABI stabilization, Windows ARM64 execution, actual inference contention
and these acoustic/corpus results remain release blockers; green CI alone cannot
close them.
