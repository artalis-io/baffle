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

## Selected implementation

`BF_WITH_SPEEXDSP=ON` compiles only floating-point MDF, fftwrap and smallft from
SpeexDSP 1.2.1 commit `1b28a0f61bc31162979e1f26f3981fc3637095c8`.
[CMake pins](../cmake/SpeexPins.cmake) verify LF-normalized hashes of the selected
source/header files, including an explicit source-directory override. This is
source integration, not discovery of an arbitrary system Speex binary. No Hail,
preprocessor, jitter-buffer or networking code is included. Default builds have
no third-party download or link dependency.

[Private allocation hooks](../src/speex/os_support_custom.h) draw only from the
initializing context's bounded arena. The thread-local pointer is admitted only
for setup; processing cannot allocate. Free/realloc/logging hooks are isolated;
the selected call paths need no realloc. The arena plan accounts for TWO_PATH
coefficient banks and FFT caches. All upstream global symbols are renamed using
[the explicit symbol list](../cmake/speex-symbols.txt), and hidden in shared
libraries. This permits an application to use its own separate Speex library.

The portable C backend avoided importing WebRTC's larger GN/C++ dependency
surface. AEC3 remains a potential independently selectable backend if recorded
double-talk and changing-room tests demonstrate a quality need. Speex has a
mature adaptive algorithm, not automatic production qualification for Baffle.
There is no neural runtime, GPU, custom simplistic AEC fallback or production
claim based only on synthetic ERLE.

Speex receives saturated S16 samples through Baffle's F32 API: quantization and
clipping are observable limitations. It requires externally aligned 10/20 ms
blocks at equal rates, 8..48 kHz divisible by 100, 1..8 capture/1..2 reference
channels, 40..500 ms tails. Multichannel delay readback is unsupported because
the pinned upstream impulse-response accessor is not multichannel-safe. Mono
readback reports the dominant adapted tap, not measured hardware latency.

Its BSD-style COPYING is installed as `SpeexDSP-COPYING`; upstream per-file
copyright/license notices remain in the pinned source. Baffle's MIT license
does not relicense Speex. WebRTC BSD/PATENTS were evaluated, but no WebRTC code
is integrated. Neural NS/VAD, explicit SIMD and beamforming remain unimplemented.
Compiler auto-vectorization may occur in optimized scalar C builds; no runtime
dispatch or measured SIMD-versus-scalar speedup is claimed.

Namespace reconnaissance: sibling Hail/Hull/Keel public headers have no bf_ names.
LibBF already uses the generic bf_ family (https://bellard.org/libbf/). Baffle
avoids its arithmetic/context identifiers and uses DSP-specific symbol names;
prefix ownership is not claimed globally. Export lists must be checked exactly.
