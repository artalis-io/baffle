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
