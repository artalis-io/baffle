# AEC convergence evidence

`summary.json` records source/build provenance, acceptance decisions and artifact
hashes. The source report is [AEC convergence](../../aec-quality.md). Historical
failed reports under the earlier dated directories remain unchanged.

- `original*.json` and `original-reproduction.txt`: the unchanged 8 dB target
  fails at baseline `29feb81`; the later reproduction uses only the extended
  offline CLI harness against that original DSP source.
- `corrected-final.json`: the same fixture/metric/threshold now passes.
- `diagnostics.json`, `notch20.json`, `notch40.json`: one-factor experiments.
  `baseline-harness.patch` and `notch-experiment.patch` reproduce the isolated
  source trees from `29feb81`. The notch experiment uses
  `-DBF_EXPERIMENT_NOTCH_HZ=20` or `40`; it is not a production tuning API.
- `holdout-*-final.json`: both attributed OSR voice roles, all scenarios/hops,
  full-interval scores, all one-second power windows and coarse settling receipts.
  The ARM64 report predates the derived settling fields but retains the same
  source audio, scenarios, whole-interval metrics and raw windows.
- `real-recorded-corrected.json`: weak, uncalibrated real-acoustic outcomes are
  retained separately from the improved simulated-echo result.
- `perf-raw-*.csv/json`, `baffle-quality-arm64-contention.csv/json`: final paired
  Windows and corrected ARM64 processing costs, executable hashes, commands and
  host metadata. Four SHA256 workers are a CPU proxy, not speech inference.
- `raw.zip`: unsorted per-frame costs, sanitizer/fuzzer/analyzer logs and earlier
  exploratory reports. Exploratory idle runs overlapped other builds and are
  excluded from causal comparisons. Early draft near-only SNR improvements used
  an invalid finite floor; corrected reports mark them unavailable. Their raw
  error/projection metrics and original reports remain retained.

Extract `raw.zip` to a temporary directory, then recompute the final percentiles:

```sh
python3 tools/check_latency_receipts.py docs/evidence/2026-11-20-aec/perf-raw-baseline-contention.csv extracted/raw-baseline-contention.csv
python3 tools/check_latency_receipts.py docs/evidence/2026-11-20-aec/perf-raw-corrected-contention.csv extracted/raw-corrected-contention.csv
python3 tools/check_latency_receipts.py docs/evidence/2026-11-20-aec/baffle-quality-arm64-contention.csv extracted/baffle-quality-arm64-raw.csv
```

Each final campaign has 19 cases and 190,000 unsorted costs. CSV formatting
precision is explicit in the checker; no DSP threshold is relaxed. Thread CPU
accounting is measured separately and cannot be inferred from wall-time costs.
There is no confidence interval or causal speedup claim from one before/after
pair with unpinned CPU clocks/affinity.

Sanitizers, bounded heap/import guards and fuzzing establish their stated
verification scope, not WCET or acoustic quality. CI snapshots identify exact
older revisions and must not be interpreted as completion of later queued jobs.
Windows SDK identity is recorded by file hashes; its source commit is not
embedded. Linux installed-Hail builds use pinned `7ef6142`.

No Microsoft source/derived audio is redistributed. The separately attributed
OSR source fixtures are in `tests/fixtures/osr`. Physical audio, annotated speech
onset, Hungarian/WER, actual inference contention, prolonged paced deployment
soak and ABI stabilization remain outstanding. Overall acceptance is PARTIAL.
