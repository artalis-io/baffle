# Evidence provenance

Measurements were collected on 2026-11-19 UTC and archived/converged on
2026-11-20. `summary.json` records exact source revisions, completed CI and
remaining acceptance limits. Benchmark snapshots are separate from later timing
metadata and shared-library packaging corrections.

- `*-native-final-*.csv/json`: native processing-cost distributions, queried
  workspaces, CPU proxy configuration and executable hashes. No physical I/O or
  actual speech inference workload is represented.
- `recorded-aec-*.json`: four pinned upstream real recordings, verified LFS
  hashes, explicit cropping, coarse lag diagnostics and weak exploratory AEC
  results. No source or derived audio is included. This is not calibrated
  acoustic, clock or near-end preservation qualification.
- `*-original-failure.*`: actual failing regressions before their corrections.
- `shared-relocated-consumer.log` and `shared-minimal-dependencies.txt`: installed
  shared dependency resolution at a different prefix; the minimal executable
  has no direct dependency on the Baffle core.
- `raw.zip`: sanitizer/test logs, seeded fuzz campaigns, analyzer output and
  earlier compiler/CI failures. File names and source revisions in the summary
  identify scope; a passing sanitizer is not a deadline or quality proof.

The complete 24-job source matrix at `bfd782f` is retained separately from the
later 27-job matrix that includes paired shared adapter installations. A queued
CI job is not recorded as a pass. Current checks are visible on PR #1.

No physical-device or audible-tail measurements were performed. Production
readiness and ABI stability are not asserted.
