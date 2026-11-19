# Baffle architecture

Independent axes: PCM representation, timing observations, processor state,
storage, numerical algorithms, execution kernels, optional external backends,
and optional transport adapters. No device, networking or inference ownership.

Each context has one processing owner. Initialization and backend destruction
are control operations. Processing borrows caller buffers and uses preallocated,
bounded state, with no worker threads. Configure/reset must not race processing;
the caller quiesces all aliases before releasing storage.

Scalar C is the reference path. SIMD/GPU kernels require measured end-to-end
benefit; neither is a prerequisite. Favor lower buffering over isolated throughput.

Timestamp domains are opaque and never implicitly comparable. Epoch changes
invalidate adaptive assumptions. Render-reference generations are distinct from
clock epochs; playback cancellation does not prove acoustic silence.

## Implemented axes

| Axis | Implementation |
| --- | --- |
| PCM and metadata | [pcm.c](../src/pcm.c), public versioned records |
| Independent clock observations | [clock.c](../src/clock.c) |
| Nominal SRC and adjustable drift | [resampler.c](../src/resampler.c), rational phase and bounded FIR |
| Explicit matrices/integer delays | [channels.c](../src/channels.c) |
| AGC/VAD | [conditioning.c](../src/conditioning.c) |
| Spectral suppression | [noise.c](../src/noise.c) |
| Optional mature AEC | [aec.c](../src/aec.c), isolated pinned Speex subset |
| Stage composition | [pipeline.c](../src/pipeline.c) |
| Optional transport integration | [adapters/hail.c](../adapters/hail.c), public headers only |

Pipeline order is AEC -> NS -> AGC -> VAD. AEC receives unconditioned microphone
audio; NS precedes gain to avoid first boosting noise. VAD observes conditioned
audio and supplies previous-hop confidence to NS/AGC. Synthetic combination and
reconstruction tests exercise this order; recorded-speech experiments have not
established it as optimal. Individual processors permit another order. AEC/NS
transforms are not shared because their windows/representations differ.

## State and discontinuities

| Event | Implemented response |
| --- | --- |
| New capture stream | Reset NS/VAD/AGC; AEC resets its stream history |
| Capture epoch/domain/gap/frame jump | Reset NS/VAD; preserve AGC gain; AEC retrains |
| Reference epoch/domain/gap/frame jump | AEC retrains; adapter resets SRC and invalidates reference history |
| Missing/unusable reference | Explicit bypass; capture continues; retrain on reference return |
| Generation only | Preserve learned path, report transition; history retains generation tags |
| Format/rate/layout change | Quiesce, retire, query and reinitialize; no silent reinterpretation |
| Explicit stage reset | Reset selected state; NS buffered hop discarded |
| Flush | SRC drains FIR tail; pipeline drains one NS hop; reset before more processing |
| Deinit | Retire AEC/pipeline/adapter before freeing storage or borrowed stream |

`bf_process_info.discarded_frames` reports the NS hop discarded on automatic
capture reset. Adapter `discarded_capture_frames` additionally counts discarded
partial framing. Raw Hail loss metadata remains separate. Generation changes do
not imply a capture clock reset or acoustic silence.

## Structural real-time bounds

No application callback is invoked. Loops are bounded by validated configuration
and frame counts. PCM/matrix work is O(frames * input_channels * output_channels),
delay O(frames * channels), and SRC O(output * channels * taps + accepted input *
channels), with at most 1536 taps. NS FFT length is at most 2048/channel. AEC uses
10/20 ms hops, 1..8 microphone and 1..2 reference channels, and 40..500 ms tails.
MDF work is bounded by these dimensions. These are structural bounds, not WCET
measurements for every supported shape.

State/scratch are queried before initialization. Speex setup hooks allocate only
inside a bounded caller arena; process-time hooks cannot allocate. The core has
no worker queues. Adapter history is configured and bounded: fullness drops
oldest references with telemetry. Output-capacity backpressure returns before
capture consumption. Hail's bounded queues determine device overruns/underruns;
the application chooses loss policy.

Only AEC reference conversion adds the exact causal SRC alignment delay, rounded
up to capture frames. NS adds its documented one-hop delay. No extra 10 ms queue
exists between stages. Optimization must account for framing/algorithm delay,
not only throughput. Beamforming, neural stages, SIMD and GPU remain absent.
