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

The Python regression generates explicitly synthetic broadband echo, tests ERLE
>20 dB after training, verifies a 17-frame partial ending and rejects malformed/
mismatched files. It is not a recorded acoustic fixture. No license-vetted real
recording was supplied or incorporated; acquiring one remains outstanding.

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
