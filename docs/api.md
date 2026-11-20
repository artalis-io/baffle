# Public C API and ownership

## Signal interpretation and validity

Baffle reports signal measurements, not user/speaker attribution or transcript
commitment. The legacy VAD field named `probability` is a bounded **heuristic
activity score**, not a statistically calibrated probability that a user speaks.
Assistant residual echo can activate it. Do not interpret it as `is_user_speaking`.
Reference levels describe supplied digital PCM at its declared provenance, not
actual acoustic output. Queue invalidation or flush acknowledgement does not
establish acoustic silence or remove an existing room tail.

| Existing signal | Meaning and validity |
|---|---|
| `bf_aec_info.input_rms` | Current capture block, including near speech, echo and noise; not isolated near-end energy. Valid after a successful AEC process call. |
| `bf_aec_info.output_rms` | Current AEC output, including near speech and remaining noise/echo; not residual-echo-only energy. |
| Reference PCM / `BF_REFERENCE_MISSING` | Reference activity can be measured from the public PCM. Missing reference means unavailable and bypass/frozen adaptation, not silence at the loudspeaker. |
| `reference_generation`, `resets`, AEC flags | Current admitted reference generation, cumulative reset count and observed transitions. Neither zero resets nor a dominant adapted tap proves convergence/reliability. |
| `bf_level_info` | AGC input/output RMS, final linked gain and number of samples affected by its limiter. Valid when AGC ran; limiter engagement is distinct from digital/physical clipping. |
| `bf_vad_info` | Signal activity score, hangover decision and tracked noise RMS. Valid for an enabled VAD with real processed frames; zero-frame calls advance no classifier evidence. |
| `BF_AUDIO_CLIPPED` | Observable digital saturation/clamping, including backend S16 and output conversion; not proof about analog microphone/speaker clipping or nonlinear driver processing. |
| Process flags / `discarded_frames` | Current processing events and buffered capture discarded during reset; keep source timing and epoch with each emitted block. |

In the pipeline, **AEC info belongs to the current consumed input**, even while
NS is priming. AGC/VAD info belongs to the emitted (possibly delayed) output and
is invalid when the stage is disabled or no output frames are emitted. Flush
can emit a pending NS block but consumes no new capture/reference: its default
AEC record is not a current reference observation. Use stage configuration,
`consumed_frames`, `priming` and output `frames` to determine validity. Inactive
record zeros must not be treated as measured silence. AEC and AGC RMS from the
same call can describe different source frames and cannot simply be divided
to obtain ERLE. Even aligned ratios require a known far-only interval.

`bf_aec_estimate_delay` reports a dominant learned mono filter tap, not calibrated
acoustic propagation or device latency. Capture/output timing validity and
uncertainty remain independent of signal measurements. This API exposes no
calibrated AEC convergence/reliability or residual-echo probability. Such signals
would require separate ground-truth validation; no speculative fields are added.
Public PCM permits inexpensive application/DSP-side level measurements without
expanding the provisional ABI. Existing record layouts and 45 exports remain
unchanged in Gate I.

## PCM and ownership

ABI zero is provisional. Initialize every public record (including nested
formats/timing) with BF_INIT. Required prefixes cannot be shortened; larger
unknown tails are ignored and output size/version headers are preserved.

F32 is normalized full scale +/-1 for speech algorithms. PCM conversion also
accepts S16, planar/interleaved and 1..32 indexed discrete channels. Named mono
and stereo layouts constrain channel counts; discrete physical positions remain
unknown. Buffer pointers/plane arrays are borrowed for a call, never retained.
Capacity bounds are frames, not scalar samples. PCM conversion uses memcpy for
unaligned samples; primitive `float *` interfaces need natural float alignment.
Except explicitly supported exact in-place paths,
input and output storage must not overlap.

Context storage is caller-owned, BF_ALIGNMENT aligned, sized by its query and
valid until all users are quiescent. One processing owner; reset/configure/query
must not race it. No hidden mutex, worker, allocator or global session state.
The selected AEC backend allocates only from the caller's setup arena. Processing
and reset are exercised by allocation guards; retirement frees no heap storage.

Unknown host time never becomes valid by interpolation. bf_timing_advance keeps
domains/epochs/generations and uncertainty; valid host interpolation is flagged.
Unknown uncertainty is UINT64_MAX, not zero. Frame position is stream-local;
host/device times are separate. Clock IDs are scoped to the originating source
(e.g. Hail context), never globally unique merely because numeric IDs match.

bf_clock_tracker observes one domain/epoch at a time. It resets on source/rate/
epoch changes, gaps or nonmonotonic observations, and marks unknown/large
uncertainty explicitly. Slopes require at least 100ms; they include timestamp
estimation error. Separate trackers observe separate clocks; their raw device
positions/origins are never subtracted. No implicit ratio controller is applied.

All processing rejects NaN/infinity before state mutation by default. Explicit
ZERO policy sanitizes them; subnormals are flushed explicitly without changing
the host's floating-point control mode. S16 conversion saturates and uses the
current standard rounding mode; F32 remains preferred between stages.

## Timing and sample provenance

Host time describes the first source sample under its provider's contract. It
does not become a submission, callback-entry, presentation or acoustic timestamp.
Host/device validity bits are independent; invalid zero is not an observation.
ESTIMATED/INTERPOLATED never promises precise presentation. IDs are scoped to a
source context, not globally unique; unrelated providers need separate namespaces.

Rational nanoseconds-per-source-frame numerator/denominator plus remainder
preserve fractional timing across split buffers. Both zero select nominal
1e9/sample_rate. Incomplete pairs and out-of-range remainders are invalid.
Positive prefix advancement clears first-sample gap/discontinuity flags while
preserving identity, epoch, generation and uncertainty. It also clears
`lost_frames`, which counts missing source frames immediately before the original
first frame; this receipt must not be counted again for suffixes. Zero advancement
preserves it. Unknown loss count is UINT64_MAX with the gap flag. Overflow returns an error.

Clock trackers require 100 ms observation intervals and use rolling one-second
anchors. Cached slopes bridge shorter intervals. Identity/rate/epoch/gap or
nonmonotonic observations reset the tracker. Two separate trackers observe
capture/reference clocks; the adapter derives relative rate, clamps +/-2000 ppm
and slews SRC at most one ppm/output sample. Unknown/large uncertainty is not
silently promoted to certainty.

SRC source phase is reported before subtracting its FIR delay. Reference
alignment subtracts that delay; unsupported priming has no valid host time.
Derived observations are flagged estimated/interpolated and raw Hail source
metadata remains in status. No device clock reading is manufactured.
An aligned reference block's rational period fits its observed endpoint times.
Its uncertainty additionally covers the largest intermediate fitting error and
provider uncertainty, including nonconstant periods during drift slew. This
interpolation contract applies within the admitted block; do not extrapolate its
period beyond that block. Strict alignment can bypass a block whose fitted
uncertainty exceeds the configured budget.

## Output, drain and reset

Pipeline input is exactly `frame_frames`. With NS, the first result has zero
frames and no valid timing; later results carry the preceding input's metadata.
Priming is not EOS. Pipeline flush emits the final real hop once, then zero.
Adapter flush also pads a short final hop, marks padding and trims to real frames;
it internally resolves priming, so zero output really means drained. Stop and
drain Hail capture before adapter flush; flush does not read Hail queues.

Process info describes work admitted this call, including AEC reference status,
consumption, priming, algorithm delay and discarded buffered frames. The AEC
receipt is not an NS-delayed output generation receipt. Output PCM timing
describes its actual source block. Format/rate/layout changes require quiescent
retirement and reinitialization. Explicit resets select stages; see architecture.

Primitives accept finite headroom +/-16. S16 conversion/AEC saturation is explicit
and clipping is flagged. Named mono/stereo validate count; discrete order does
not imply physical geometry. Store array calibration externally; no automatic
downmix or beamforming discards it.

For direct AEC, a missing reference may also have NULL reference timing. It
bypasses the backend, preserves F32 headroom and reports reference generation
zero as unavailable, with `BF_REFERENCE_MISSING`. It cannot establish a generation
change or S16 clipping. Present references require timing. Returning after a
missing reference resets adaptation; absent PCM never represents acoustic silence.

## Optional Hail ownership and alignment

`BF_WITH_HAIL=ON` requires installed Hail 0.1.0. Independent consumers use
`find_package(BaffleHail 0.1.0 EXACT CONFIG REQUIRED)` and `Baffle::hail_adapter`.
The core package has no Hail dependency. `examples/hail-consumer` includes neither
private source tree and is configured after installation.

The adapter owns one serialized capture/reference read position and copies into
bounded state. It never writes playback, controls/polls devices, waits for workers
or infers speech. Playback producers obey Hail ownership rules separately. Quiesce
pump/status/flush before deinit; deinit before `ha_stream_close_finish` frees the
borrowed stream. No Hail lease/sample pointer survives a call; processed PCM stays
in caller output memory after teardown.

Strict alignment needs valid host observations within configured uncertainty.
Missing/unsupported alignment bypasses AEC explicitly and preserves capture.
`allow_uncertain_reference` is an explicit opt-in; it preserves unknown uncertainty.
`reference_delay_ns` is caller calibration, not a measured acoustic delay by
default. References are Hail's selected render PCM, with Hail's pre-mixer/device
limitations. Cancellation acknowledgements never establish acoustic silence.

Shared adapter builds are qualified with a matching shared Hail installation.
Keep Baffle's core and adapter libraries together; the adapter uses `$ORIGIN` on
ELF and `@loader_path` on macOS to resolve its core without a direct application
reference. Windows consumers must place both packages' DLL directories on the
loader search path. Separate dependency prefixes still require the application's
normal platform loader configuration. Static/shared mixed-Hail configurations
are not part of the qualified matrix.

## ABI assessment

Names remain `bf_`/`BF_`; OS/Speex types are private. Fixed-width metadata,
size_t queries, natural layout and opaque state support native C11/C++ consumers.
No ABI is frozen: 0.1 development extended timing/process records. Headers and
binaries must come from the same revision. Larger tails are ignored, but the
currently required prefixes cannot be shortened; this is not complete backward
optional-tail negotiation.

Before stability: finalize optional-tail rules, record layouts across supported
toolchains, define upgrade policy and execute cross-version compatibility tests.
Windows ARM64, 32-bit and big-endian ABI execution remain outstanding. Exact
shared exports match declarations; private Speex symbols are hidden/renamed.
Reconnaissance found no exact `bf_`/`BF_` identifier collision with LibBF's
2025-06-03 public header. This checks one snapshot, not global prefix ownership.
