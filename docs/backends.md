# Algorithm backend assessment

WebRTC AEC3 is a strong candidate for modern speech applications, with explicit
delay, near-end and clock-drift components. Its upstream GN/C++ build and broad
dependency surface require an isolated integration; do not silently import a
whole WebRTC runtime into the C11 core. Review its BSD license and PATENTS grant.
Source: https://webrtc.googlesource.com/src/+/refs/heads/main/modules/audio_processing/aec3/

SpeexDSP provides a mature CPU C MDF echo canceller, preprocessing and SRC under
a BSD-style license. It is a simpler first optional backend; it is not AEC3 and
does not make a new wrapper physically qualified. Its synchronous AEC requires
aligned equal-rate frames; Baffle must explicitly align/delay and compensate
independent clock drift before passing references to it. Calls on one state
must be serialized; no processing-time mutex is added by Baffle.
Source/manual: https://speex.org/docs/manual/speex-manual.pdf

No external DSP source is copied into the foundation. Optional installed backend
discovery is explicit. Neural NS/VAD, GPU, SIMD and beamforming remain unimplemented
until justified by measured quality/latency tradeoffs.

Namespace reconnaissance: sibling Hail/Hull/Keel public headers have no bf_ names.
LibBF already uses the generic bf_ family (https://bellard.org/libbf/). Baffle
avoids its arithmetic/context identifiers and uses DSP-specific symbol names;
prefix ownership is not claimed globally. Export lists must be checked exactly.
