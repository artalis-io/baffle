# Baffle axis audit — 2026-11-21

Scope: base `b20eaf7` plus the working-tree C11 migration and native speech demo.
Applied skill: [Baffle axis-audit](../.claude/skills/axis-audit/SKILL.md), adapted
from Hail's skill without changing the Hail copy. The [C audit](c-audit-2026-11-21.md)
and [evidence](evidence/2026-11-21-c11-audit/summary.json) provide executed checks
and safety findings. Existing gate failures remain part of the acceptance record.

## Actual responsibilities and dependencies

| Axis | Owner/seam | Assessment |
| --- | --- | --- |
| PCM representation | `include/baffle.h`, `src/pcm.c` | Versioned geometry, borrowed interleaved/planar PCM; no device/model tensors |
| Time/provenance | `bf_audio_timing`, `src/clock.c` | Stream/domain/epoch/generation distinct; unknown uncertainty explicit |
| Storage | Per-processor query/init, private Speex arena hooks | Caller-owned bounded state; no core allocator/thread ownership |
| SRC/drift | `src/resampler.c` | Bounded FIR/phase, explicit source coordinates/group delay, limited ratio slew |
| Channels | `src/channels.c` | Explicit matrix/delay; discrete ordering does not imply microphone geometry |
| Conditioning | `src/conditioning.c`, `src/noise.c` | Signal processing; heuristic VAD score is not speaker attribution |
| AEC backend | `src/aec.c`, `src/speex/`, `cmake/SpeexDSP.cmake` | Optional pinned C backend, private symbols/arena; no foreign public types |
| Composition | `src/pipeline.c` | Explicit stages/order, one owner, per-stage receipts and NS delay |
| Audio transport | Hail; optional `adapters/hail.c` | Adapter borrows a public stream; core has no Hail requirement |
| Speech/UI | `examples/win32-transcript/`, external evaluation tools | Model decoding, segmentation and caption policy stay outside core |
| Foreign comparison | Optional WebRTC C++ shim | Test/tool-only dependency, not a new production backend |

Default dependency path is `C application -> Baffle C11 -> platform C math`.
Optional AEC adds the pinned C Speex subset. Optional Hail integration adds only
the installed public Hail target. The Win32 example separately composes Hail,
Baffle and sherpa's C API; its third-party inference implementation is C++.
No core dependency on Hull, ASR/TTS models, Python, GPU, UI or network transport.

## Trace 1: PCM, state and ownership

`bf_pcm_validate` checks nested record versions, channel/rate/format geometry,
capacity and timestamp overflow. `baffle_sample/store` use byte copies for external
PCM. Processing contexts borrow input/output for the call; state/scratch reside in
the queried arena until explicit deinit and caller quiescence. Each context has
one owner; reset/process concurrency is not supported or implied by thread tests.

SRC accepts bounded fragments and reports consumed/produced frames, source phase,
priming and group delay. Backpressure returns pending work instead of allocating
another queue. Channels remain an independent representation transform, not an
implicit beamformer. Exact permitted in-place operations and disjointness remain
public caller obligations, not arbitrary pointer-access validation.

Finding: no demonstrated cross-axis defect requiring a core abstraction change.
The checked size/alignment plan is shared internally; no duplicate allocator or
new buffer framework was added during migration.

## Trace 2: transport, clocks and render reference

`bf_hail_translate_timing` preserves stream identity, opaque device clock domain,
epoch, generation, sample position, host validity and uncertainty. The adapter
frames capture, resamples reference and retains bounded per-frame provenance.
Reference selection uses valid host-time observations and an explicit delay with
uncertainty checks; it does not subtract unrelated device-clock sample positions.
SRC delay/priming and fitted interpolation error remain visible rather than
fabricating sample-perfect independent-device synchronization.

Missing/unusable reference produces explicit AEC bypass. Reference epoch/gap
changes invalidate reference SRC/history and retrain AEC. Generation-only changes
retain the learned room path and tagged history; the selector rejects a mixed-
generation block. Capture epochs reset NS/VAD and retrain AEC while preserving AGC
where the existing documented policy allows it. Format changes require retirement
and reinitialization; no silent state-machine replacement was introduced.

Finding: generation, epoch and acoustic tail are correctly separate concerns.
The acquisition receipt defect [CA-02](c-audit-2026-11-21.md#ca-02--medium-flush-evidence-could-imply-an-acknowledgement)
was in measurement evidence, not the core timing model. Fixed requested/ACK fields
do not assert device presentation completion or acoustic silence.

## Trace 3: stage order and feedback

Pipeline order remains AEC -> NS -> AGC -> VAD. NS emits the previous hop; its
algorithmic delay is reported independently of execution cost. AEC receipts apply
to reference/input admitted this call; AGC/VAD receipts apply to emitted PCM. NS
priming does not expose fabricated timestamps. Flush drains the last retained hop;
gaps report discarded frames/reset state. No new buffering was introduced.

VAD is not always observational: its previous-hop score feeds NS/AGC when enabled.
Therefore adding a VAD stage can change PCM. The native Hungarian demo deliberately
uses NS only (mask 2) by default and optional NS+AGC (mask 6), with no VAD feedback,
matching its paired replay. The separate legacy Python live tool uses its own
explicit feedback-enabled profile; it is not a numerical oracle for the GUI.

The pre-audit live/replay mismatch was already corrected in the working tree.
It is documented here as a consequential coupling, not a newly reproduced core
algorithm defect. Preserve explicit masks/feedback in future evaluation receipts.
Do not move model-specific gain tuning or transcript rejection into Baffle.

## Trace 4: application execution and retirement

The native application capture owner initializes DSP, reads public Hail frames,
then publishes bounded frame copies through an acquire/release SPSC queue. Its
inference owner may allocate/decode text; UI locks protect text only. Neither
inference nor UI is called by a Hail device callback. Queue overflow is observable
loss; the caption consumer marks a gap and resets its bounded context.

Caption windows, pre-roll, overlap, agreement and history have explicit fixed
capacities. Model token timestamps constrain text overlap only; they are not
microphone timestamps, ASR confidence or independent onset annotations. The
application performs rolling offline decoding, not a native streaming model.
Ordinary dictation stores no audio; explicit paired trials save local WAV evidence.

[CA-01](c-audit-2026-11-21.md#ca-01--medium-retirement-success-was-not-established)
fixes retirement error propagation in acquisition/live ownership. A worker's
`finished` publication is not sufficient proof that its thread has exited;
Win32 handle completion is checked before reuse/free. Incomplete SDK retirement
prevents further sessions. This is bounded error handling, not automatic recovery.
The retirement helper belongs to tools, not the core or installed adapter.

## Trace 5: language, backend and build independence

Owned concurrency tests and QA frontends no longer impose C++ on normal C builds
([CA-03](c-audit-2026-11-21.md#ca-03--low-c-only-default-build-required-c-through-tests)).
Root and ordinary installed tools build as C11 without enabling a C++ compiler.
The optional C++ consumer probe intentionally tests public C linkage; optional
WebRTC comparison intentionally translates a foreign C++ API into a private C seam.
There is no attempt to hide these dependencies behind a misleading all-C label.

MSVC's C11 atomics flag is consumer-specific. The build helper uses ClangCL because
Hail's own lock-free contract is independent of Baffle's algorithm/backend choice.
No new public fields, backend dispatch framework, stream lifecycle or transport
queue were introduced. Public ABI remains provisional; static/shared and foreign
caller tests establish current compatibility, not ABI release stability.

## Verdict and remaining qualifications

Architecture review: **PASS within the audited software scope**, with the concrete
ownership/evidence/build defects corrected. The result is not a Gate H/I/J/K or
production acoustic PASS. Default core/optional transport/foreign speech consumer
remain independent, and first-party native C++ has been confined to the two
explicit foreign boundaries rather than spreading into algorithms or scheduling.

Windows and Linux ARM64 tests, installed consumers, sanitizer runs, fuzzing,
static analysis and bit-exact port comparisons support software correctness.
GCC TSan initialization failure is retained; Clang TSan passes separately. There
is no new remote CI, macOS/x86-64 Linux or Windows ARM64 execution evidence.

Remaining limits: weak real-room AEC, assistant-only false transcripts, reviewed
interruption-onset/phoneme evidence, independent Hungarian holdouts, physical
clock/reference/cancellation-tail qualification, full workload deadline budgets,
clean-machine distribution and final ABI policy. NS's one-hop delay remains.
No source audit or green CI closes these acoustic/application acceptance gaps.
