# Engineering gates

| Gate | Status | Evidence required |
| --- | --- | --- |
| A: foundation | PARTIAL | Build/install/consumer execution and cross-platform CI |
| B: PCM/timing/state | Planned | Versioned metadata, explicit ownership and discontinuities |
| C: numerical DSP | Planned | Streaming SRC, channel processing, numerical/quality regressions |
| D: speech conditioning | Planned | VAD/AGC/NS quality and state transitions |
| E: AEC | Planned | Mature backend, double-talk, path change, delay/quality measurements |
| F: duplex integration | Planned | Installed-Hail-only consumer and bounded transport stress |
| G: convergence | Planned | Sanitizers/fuzzing/analysis/performance and physical qualification |

Production readiness is not asserted. Retain failures and distinguish synthetic
quality, software integration, physical acoustic evidence and deployment readiness.
