---
name: c-audit
description: Audit Baffle C11 DSP, optional public-Hail adapter, tools and native consumers for numerical and memory safety, bounded real-time processing, ownership, concurrency and build integrity. Use for a requested C source audit or hardening review.
---

# Baffle C audit

Adapted from Hail's `.claude/skills/c-audit/SKILL.md`, retaining its source-first
safety categories while replacing device/backend ownership with DSP contracts.
Target `$ARGUMENTS`, default `src/`, `include/`, `adapters/`, tests, fuzzing,
native examples, tools, CMake and CI. Read `docs/architecture.md`, `docs/api.md`,
`docs/verification.md` and relevant gate evidence. Historical results are not
verification of a changed checkout. Write `docs/c-audit-YYYY-MM-DD.md`.

Audit before fixes. Apply only authorized, focused corrections; preserve failed
evidence and add regressions for demonstrated defects. Do not change numerical
thresholds, processor defaults or public layout merely to make the audit pass.

| Area | Baffle checks |
|---|---|
| Storage | Checked size/alignment arithmetic; exact queried caller arenas; no process-time allocation or lazy initialization; natural PCM alignment versus raw byte buffers; no hidden heap ownership in Speex hooks. |
| PCM | Frames versus scalar samples/bytes; 1–32 discrete channels, format/storage/layout validation, capacity and aliasing contracts, finite/subnormal handling before persistent-state mutation. |
| Numerical state | Near-zero regularization, overflow, finite coefficients, filter-state bounds, rounding and clipping telemetry, SRC phase accounting, FFT bounds, gain/NS/VAD initialization, transactional error paths. |
| Real time | Bound each loop by validated geometry, FIR/FFT/filter limits or explicit work budgets. No allocation, blocking locks, I/O, worker waits, logging or uncontrolled callbacks in core processing. Distinguish application inference/file work from device/DSP paths. |
| Concurrency | One owner per DSP context; no reset/deinit racing process. Release/acquire SPSC publication, wrap arithmetic and ownership of both counters. OS thread create/join results and partial startup retirement must be checked. |
| Timing | Clock identity/origin/epoch remain distinct from playback generation; unknown host time/uncertainty stay unknown; source phase and group delay are explicit; no unrelated-clock subtraction. |
| Lifecycle | Missing-reference bypass, generation/capture transitions, pending NS/SRC drain, bounded history loss, restart and native AEC recovery. No callback/worker aliases may survive arena release. |
| Boundary | Core must not own devices, speech models, transcripts or conversation policy. Optional installed-Hail consumers use public headers only. C ABI calls do not make third-party C++ implementation into C11. |
| Consumers/tools | Bounded parsing/files/windows/history; UTF-8/UTF-16 validation; no overwritten evidence; matching live/replay stage masks and feedback; no PCM gating or speaker attribution implied by VAD. |
| Build | C11 without accidental C++ compilation; strict warnings; C++ ABI compatibility probes and unavoidable foreign shims explicitly identified; static/shared exports, installed consumers, dependency/runtime inventory. |

Use `rg` to locate risky arithmetic, copying, parsing, allocations, ignored
returns, mutable globals and platform/model references. Follow callers and
actual guards before assigning severity; matching `memcpy` or an SDK release
function is not itself a defect. Do not add blanket SAFE_FREE/CLAMP macros.

Run existing unit/integration/installed-consumer checks, allocation guards,
ASan/UBSan/TSan, fuzzing and static analysis where supported. Classify each as
locally executed, remote executed, compiled only or unavailable. A sanitizer
pass proves neither bounded callback duration nor speech quality. No physical
recording/playback is implied by a source audit: use deterministic virtual tests
unless hardware use is independently authorized and needed, then stop it.

Report revision plus dirty-tree scope, source anchors, severity, reproduction,
smallest correction, verification and remaining risks. Distinguish confirmed
defects, source-confirmed error paths, numerical limitations and hypotheses.
Keep ABI provisional and physical/acoustic/ASR qualification separate.
