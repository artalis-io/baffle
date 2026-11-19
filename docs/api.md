# Public C API and ownership

ABI zero is provisional. Initialize every public record (including nested
formats/timing) with BF_INIT. Required prefixes cannot be shortened; larger
unknown tails are ignored and output size/version headers are preserved.

F32 is normalized full scale +/-1 for speech algorithms. PCM conversion also
accepts S16, planar/interleaved and 1..32 indexed discrete channels. Named mono
and stereo layouts constrain channel counts; discrete physical positions remain
unknown. Buffer pointers/plane arrays are borrowed for a call, never retained.
Capacity bounds are frames, not scalar samples. No packed records or alignment
assumptions on sample pointers. Except explicitly supported exact in-place paths,
input and output storage must not overlap.

Context storage is caller-owned, BF_ALIGNMENT aligned, sized by its query and
valid until all users are quiescent. One processing owner; reset/configure/query
must not race it. No hidden mutex, worker, allocator or global session state.
An external backend may allocate only during its explicit initialization and
retirement; processing-time allocation must be separately audited and tested.

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
