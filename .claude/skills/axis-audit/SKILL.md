---
name: axis-audit
description: Audit Baffle DSP architecture for independence of PCM, timing, state, storage, algorithms, execution, optional backends, Hail transport and external speech policy. Trace actual data and ownership and report concrete cross-axis coupling.
---

# Baffle DSP architecture axis audit

Adapted from Hail's `.claude/skills/axis-audit/SKILL.md`. Preserve semantic
contracts above private seams without pretending different algorithms/backends
have identical numerical behavior. Device discovery/scheduling belong to Hail;
model decoding and transcript policy belong to external speech applications.

Target `$ARGUMENTS`, default public/private headers, source, adapters, tests,
tools, examples, build/CI and architecture documentation. Read
`docs/architecture.md`, `docs/api.md`, `docs/verification.md`, relevant gate
reports and actual source. Write `docs/axis-audit.md`, prepending dated passes.
Make only authorized, minimal fixes supported by a reproduced failure or a
source-confirmed contract violation. Do not design a new DSP/plugin framework.

## Responsibility map

| Axis | Baffle seam | Must not absorb |
|---|---|---|
| PCM representation | `bf_audio_buffer`, `pcm.c`, indexed channel metadata | Device leases, model tensors, assumed array geometry, implicit downmix |
| Time/provenance | `bf_audio_timing`, `clock.c`, SRC source phase/delay | Shared-clock assumptions, fabricated precision, generations as clock epochs |
| Processor state | Each context, explicit reset/flush/deinit, continuity checks | Global sessions, universal reset policy, cancellation as acoustic silence |
| Storage | Caller arena queries, bounded scratch/history and Speex setup allocator | Hidden process heap, unbounded transport/inference queues |
| Algorithms | SRC/channels/AGC/NS/VAD/AEC, scalar numerical contracts | Device management, ASR/TTS, transcript commitment or user attribution |
| Execution kernels | Scalar C11 and justified optional optimization | Backend identity defining ownership/timing or GPU as a mandatory dependency |
| External AEC backend | Private pinned Speex adapter and arena hooks | Foreign types/symbols in public headers; backend recovery hidden from metadata |
| Stage composition | `pipeline.c`, explicit masks/order and per-stage receipts | NS delay hidden as execution time; same-call delayed/current metrics conflated |
| Transport adapter | Optional `adapters/hail.c`, installed public Hail contract | Owning/polling devices, automatic recovery policy, model-specific scheduling |
| Speech/UI consumers | Optional native caption application and evaluation tools | Speech dependencies in core; ASR scores represented as algorithm proof |

Axes describe responsibilities, not one vtable per row. A bounded stage may
legitimately consume another stage's previous activity estimate. Audit the
feedback as an explicit dependency: an apparently diagnostic stage can change
PCM through NS/AGC feedback. Live/replay comparisons must use identical masks,
initial state, padding, context, delay and decoder settings before attributing
quality differences to an algorithm.

## Required traces

1. Borrowed PCM -> geometry/finite checks -> processor scratch -> returned PCM:
   owner, lifetime, alignment, aliasing and rejection atomicity at every boundary.
2. Capture/reference -> optional adapter framing/SRC/drift observation -> aligned
   AEC input -> NS-delayed output: retain both source provenance and uncertainty;
   identify which current/delayed input each receipt describes.
3. Missing/gapped/uncertain reference, independent epoch/domain and generation
   changes: AEC retraining versus near-leg state, silence versus unavailable,
   cancellation acknowledgment versus residual acoustic tail.
4. Partial frames, backpressure, EOS and reconfiguration: bounded history loss,
   exact flush accounting, stage reset rules and quiescent retirement. Adapter
   deinit must precede freeing its borrowed Hail stream.
5. Pipeline enable/disable/order/feedback: individual versus composite results,
   disabled telemetry validity, first-hop protection and algorithmic delay.
6. Optional native caption consumer: capture/DSP owner versus inference/UI,
   bounded handoff and gap handling, model-token timing versus device timing,
   transcript agreement versus calibrated confidence, ordinary dictation versus
   explicit local recording. None of these policies belong in the DSP core.

Inspect core headers/source/CMake for OS/device, networking, model and UI symbols;
follow dependencies and actual branches, not just include scans. Verify the
standalone core builds without Hail, sherpa, ONNX, Python or a C++ compiler.
Classify foreign C++ runtimes/shims and deliberate C++ consumer-ABI tests honestly;
do not delete coverage or reclassify first-party algorithms to manufacture C11.

Use numerical, lifecycle, allocation, installed-Hail, concurrency and sanitizer
tests for distinct responsibilities. Keep evidence tiers explicit: implemented,
compiled, exercised, numerically verified, recorded-speech evaluated, physically
qualified, incomplete and intentionally out of scope. Synthetic ERLE does not
prove real-room speech preservation; a recognized prompt is not ground truth;
VAD activity is not a probability of user speech; flush is not acoustic silence.

## Report

Include the actual dependency/ownership map, execution traces, source-anchored
findings with severity and failure scenario, smallest correction, compatibility
impact, verification and residual qualification gaps. Distinguish deliberate
bounded tradeoffs from defects. Do not move ASR/TTS/conversation policy into
Baffle, device ownership out of Hail, or freeze the provisional ABI.
