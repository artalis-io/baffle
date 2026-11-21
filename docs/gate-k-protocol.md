# Gate K bounded adaptive-filter investigation

Baseline `96e88d4`, 2026-11-21. Preserve the original 8 dB fixture and all failed
Gate H/I/J evidence. Residual suppression, pipeline order, NS/AGC/VAD parameters
and public API are not tuning variables in this gate.

1. Characterize the unmodified pinned SpeexDSP 1.2.1 AUMDF backend. It uses
   foreground/background filter banks, variable frequency-domain learning rate
   based on leak/residual energy, excitation and saturation safeguards, and
   proportionate partition updates. It does not use an explicit user-speech
   classifier. `bf_aec_estimate_delay` queries the dominant **background learned
   tap**, not an active delay search or applied reference shift.
2. Independently reproduce/fix multichannel reset ranges if the public-API
   trained-reset/silent-microphone test fails. Do not conflate complete state
   invalidation with faster ordinary mono adaptation.
3. One isolated adaptation candidate: change the partition-weight exploration
   floor from 0.1 to 0.3 of the largest partition norm, preserving normalization,
   learning-rate equations, foreground/backtracking, notch and regularization.
   Hypothesis: after a moved path, a formerly weak later partition is learned
   too slowly. More exploration may improve that case at the expense of steady
   misadjustment/double-talk. No delay/gating/step-size change is combined with it.

Use deterministic stationary/abrupt/gradual/delay/gain/tail/level/noise fixtures,
near-only and quiet/normal/loud overlap, path movement during overlap, reference
gaps and signed drift. Include 10/20 ms legal quanta and 16/48 kHz. A fixed context
does not accept arbitrary callback sizes: assembly/rate/drift mapping belongs
before synchronous AEC, including the optional installed Hail adapter.

Private static-only qualification may inspect background impulse responses and
internal adaptation counters; it is deliberately not a public DSP integration
fixture or new API. Snapshots happen outside processing calls. Output ERLE,
background coefficient misalignment and dominant-tap error are distinct metrics;
DC conditioning means the learned response need not exactly equal the raw room
FIR. No timestamps from independent domains are directly compared.

## Acceptance fixed before adaptation trials

* Regressions for confirmed lifecycle defects must fail before and pass after,
  retaining original unit/ASR thresholds. No stale trained path may survive reset.
* Adaptation candidate: improve median moving-path settling by at least 20%
  across predefined in-capacity FIR cases using two consecutive excited 500 ms
  windows above the existing synthetic 20 dB target. Unreached counts as failure,
  never omitted. Report worst windows, not only end-of-run ERLE.
* No new nonfinite output, backend divergence/reset, significant steady-state
  misadjustment or quiet/near-only error. No additional blank, word error or
  first-word loss per speech case versus baseline. Original 8 dB still applies.
* Score full fixed Whisper/Parakeet conditions, both AEC and full pipeline.
  Real recordings are non-tuning comparisons, not new blind/calibrated rooms.
  Predefine extra FIR/noise seeds as non-tuning holdouts; do not retune afterward.
* Keep 10 ms NS delay, no new queue/worker, existing mono P99 ≤2.5 ms and workspace
  <512 KiB at 16 kHz / <1 MiB at 48 kHz, zero observed 10 ms completion misses.
* Independent reviewed phoneme/onset and physical/live qualification remain open.
  Do not adopt the adaptation candidate if the complete trade-off is unconvincing.

If rejected, retain only justified correctness fixes in production and isolate
the unsuccessful candidate in reproducible external qualification tooling.
