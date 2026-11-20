# Engineering gates and acceptance evidence

Overall verdict: **PARTIAL; not production-qualified and ABI not frozen**.
Working code exists for every required DSP/transport area. Recorded-speech and
physical acoustic evidence, actual inference contention and deployment soak are
outstanding. Green CI does not close these requirements.

## Gate matrix

| Gate | Verdict | Source, executable evidence and limits |
| --- | --- | --- |
| A: repository/ABI foundation | PASS for foundation | Public repository, MIT/NOTICE, C11 core, static/shared CMake install/export, installed C/C++ consumers, CI. Foundation `78ce726` built/pushed before DSP. ABI stability remains a release blocker. |
| B: PCM/timing/state | PASS for implemented contract | [header](../include/baffle.h), [PCM](../src/pcm.c), [clock](../src/clock.c), [metadata tests](../tests/pcm.c), [pipeline tests](../tests/pipeline.c): ownership, rational timing, independent clocks/changing slope, epochs, transactional errors, reset and exact EOS. |
| C: numerical DSP | PASS for scalar baseline | [SRC](../src/resampler.c), [channels](../src/channels.c), [numerical tests](../tests/numerical.c): split invariance, +/-drift, common/extreme rates, 32-channel isolation, analytic tone/stopband grid, bounded tail drain. No SIMD/GPU claim. |
| D: conditioning | PARTIAL | Real [NS](../src/noise.c)/[AGC/VAD](../src/conditioning.c), [quality tests](../tests/conditioning.c): synthetic reduction/reconstruction, clipping, gain, onset/hangover pass. Real speech/noise corpus accuracy, distortion/pumping and stage-order comparison remain unqualified. |
| E: AEC | PARTIAL | Pinned mature SpeexDSP MDF, [AEC tests](../tests/aec.c), reference alignment, synthetic double-talk/path change/delay, WAV tooling and four exploratory real traces. Weak recorded energy reduction; calibrated quality/double-talk/tail qualification remains open. |
| F: duplex integration | PARTIAL overall; deterministic transport PASS | [adapter](../adapters/hail.c), [installed-only consumer](../examples/hail-consumer/main.c), 700 modeled seconds of both transport/full pipeline, rates/drift/epochs/generations/loss/backpressure/lifecycle/short EOS. Native recovery/alignment remains hardware qualification. |
| G: convergence | PARTIAL | Local Windows/Linux ARM64 tests, optional AEC, static/shared, ASan/UBSan/TSan, heap guard, fuzz/analysis, performance and CPU contention proxy. Physical tests, actual inference contention, prolonged paced soak, Windows ARM64 and final ABI policy remain outstanding. |

PASS is scoped to the listed baseline, not universal algorithm quality, every
rate's complete response curve or a stable ABI release.

## Numerical and quality assertions

- SRC 48->16k: 1 kHz analytic relative-error power <1e-8; 12 kHz stopband power
  <1e-8. At 44.1/48->16k, 100/1000/3000/6000 Hz gain power .98..1.02;
  9/12/18 kHz stopband power <1e-6. Observed minimum rejection on this Windows
  grid exceeded 111 dB; this is not a whole-band/arbitrary-ratio guarantee.
  Selected 8/16/44.1/48/96/192k pairs have exact accounting/fragmentation tests,
  +/-1000 ppm slew and bitwise chunk invariance.
- NS: zero-strength WOLA reconstructs within a fixed error bound; known-noise
  learning gives >6 dB reduction; voiced-tone projection .85..1.1. Observed
  ~9.42 dB reduction, ~.990 tone projection and -138.98 dB reconstruction are
  synthetic measurements, not speech-quality scores.
- AGC: target RMS within .005, causal -1 dBFS peak limit, silence handling.
  VAD: no active frames on deterministic white noise; voiced-tone onset/hangover.
  Its `probability` field is uncalibrated confidence.
- AEC: broadband echo taps at 32/93, known near-end tone, moved path at 83.
  Assertions: ERLE >20 dB, near-end projection .8..1.2, recovered ERLE >15 dB,
  dominant tap within two frames. Observed 34.841 dB, .91382, 40.702 dB, delays
  32/83 on Windows. This does not prove real double-talk speech preservation.
  Common-rate/hop/channel/tail extremes are initialized, processed, reset and
  retired with bounded arena assertions; quality/performance at every extreme
  is not qualified. Exact S16 conversion endpoints have clipping-flag regression.
- WAV tool: generated eight-second echo, annotated far-only ERLE >20 dB, exact
  17-frame partial ending, malformed/rate mismatch errors. Observed ~42.03 dB;
  provenance is explicitly synthetic.
- Four pinned Microsoft real recordings were also exercised; three far-only
  energy reductions were .0101..6.6208 dB at 48k and -.0087..6.6474 dB after
  nominal 16k SRC. These are weak exploratory outcomes, with uncalibrated delay/
  drift/noise and no clean near-end oracle. They do not meet production AEC
  qualification. Details, limitations and commands are in qualification.md.

## Executed verification

Windows x86-64 clang-cl 19.1.5 Release optional AEC: 7/7 tests static/shared;
independent installed Hail/PCM consumers passed. Earlier Debug core checks passed.
The installed Hail consumer also drives callbacks/writes/cancellation on one
thread while another owns Baffle capture/reference processing. ASan/UBSan across
both installed libraries passed the full sequential and concurrent fixtures.
Linux ARM64 GCC 13.3 optional AEC: static 8/8 (includes heap guard), shared 7/7,
45 exact public exports and no listed heap/blocking/logging imports. Installed
Hail fixture passed using Hail `7ef6142ba4b8c8be15d1e7d704502db01abc5113`.
Earlier exploratory ARM64 integration used pre-merge Hail `0da95aac`; the pinned
reproduction supersedes it.

Linux ARM64 Clang 18.1.3 ASan+UBSan and TSan each passed 8/8, including eight
concurrent independent contexts and heap guard. Clang's isolated runtime was
used. Earlier Hail GCC ARM64 TSan initialization failure is not a Baffle race
finding. Sanitizers do not prove deadlines or concurrent safety on one context;
concurrent calls on a context remain disallowed.
The installed Hail/Baffle callback-driver/DSP consumer also passed a Clang TSan
run with both libraries instrumented, not only independent-context unit tests.

The shared adapter's minimal consumer deliberately imports no core Baffle
function. A Linux ARM64 `--as-needed` reproduction failed to load the co-located
core before the packaging correction. The fixed adapter also passed all three
installed consumer tests after installation at a second prefix; `ldd` resolved
the core/adapter/Hail from that prefix. Paired shared DLLs passed the same three
installed consumers locally on Windows x86-64. CI now covers paired static/shared Hail
and Baffle on Windows, Linux and Apple Silicon, in addition to the sanitizer leg.

Two Clang libFuzzer harnesses completed 30,000 runs each, seeds 721/722,
max_len 4096, no finding (initial campaigns started without a corpus). CI also
uses generated float/nonfinite/channel/boundary seeds. GCC 13.3 `-fanalyzer`
passed the core and offline tool build. Optional Speex is hash-pinned and
sanitizer-instrumented; core-only analysis is not third-party static analysis.

Remote CI is authoritative at the exact commit/run in the evidence record.
The complete 24-job implementation matrix passed at `bfd782f` in
[run 44621494348](https://github.com/artalis-io/baffle/actions/runs/44621494348).
The later shared-adapter matrix adds three jobs; its current result is available
on [PR #1](https://github.com/artalis-io/baffle/pull/1/checks). Archived evidence
records the source revision for each run; a queued successor is not a completed
result.
Earlier portable run [44608730699](https://github.com/artalis-io/baffle/actions/runs/44608730699)
passed all 11 jobs at `c19af99`. Expanded CI covers optional AEC static/shared on
Windows, Linux x86-64, Apple Silicon and Intel macOS; installed Hail on Windows/
Linux/macOS; sanitizers, seeded fuzz and analysis. Do not infer final-head status
from an earlier green run. The evidence summary identifies completed runs and
separately records any pending successor.

| Platform | Executed scope | Outstanding |
| --- | --- | --- |
| Windows x86-64 | Local clang-cl static/shared/installed consumers; MSVC core/AEC CI | Native acoustic/inference workload |
| Linux ARM64 | GCC/Clang static/shared, sanitizers/fuzz/analysis/installed Hail | Paced deployment soak/acoustics |
| Linux x86-64 | GitHub portable/AEC/sanitizer/Hail jobs | Physical/inference measurements |
| macOS ARM64/Intel | GitHub portable/AEC static/shared/installed consumers | Native acoustic/timing measurements |
| Windows ARM64 | Portable design only; local MSVC ARM64 runtime libraries unavailable | Toolchain/runtime/ABI qualification |
| Embedded/RTOS/UEFI | Core processing design avoids OS APIs | Toolchain/math/TLS/storage/ABI work; no certification |

## Defects and convergence

Assertions were retained while correcting:

1. GCC rejected compressed ambiguous control flow under enforced warnings.
   Owned code was formatted; warning enforcement was preserved.
2. Reference SRC needs future far-end samples for linear-phase delay alignment.
   Installed tests caught all-reference bypass. The adapter now retains exactly
   that delay and asserts useful reference delivery before injected faults.
3. The first drift driver used nominal device frame ratios despite independent
   oscillators, leaving reference behind causally. It now schedules rational
   frame counts/host time using actual clocks. Thresholds were not loosened.
4. Short-session NS priming returned zero before EOS drained. Flush now performs
   at most three internal drain operations; installed tests require all 17 real
   frames, correct timing and no fabricated samples.
5. Rational remainder preservation, sanitized flags and discarded-hop telemetry
   were completed across PCM translation, adapter and pipeline output metadata.
6. A metadata edit briefly stored delayed timing into itself; the existing stage
   timing assertion failed. Saving current timing before shifting fixed it.
7. Default MSVC lacked Hail C atomics; experimental atomics still failed Hail's
   lock-free assertion. Adapter CI builds Hail with supported ClangCL. Baffle's
   own MSVC lanes stay enabled. No Hail code/assertion was changed for convenience.
8. Explicit ZERO policy sanitized NaN but a later range check rejected infinity.
   A new regression reproduced failure against `e110635`; finite-only headroom
   checks now permit sanitized infinities and retain flags through delayed output.
   Fuzzing now checks error-policy semantics instead of only accepting failures.
9. AEC clipping telemetry incorrectly marked exact -1 and missed positive S16
   saturation below +1. Endpoint regressions failed before correction; flags now
   match the actual S16 quantizer. Full per-source BSD/OggVorbis notices are also
   preserved in binary installations, beyond the aggregate Speex COPYING.
10. The sanitizer adapter CI initially installed a no-build-type package using
    Release configuration, omitting imported locations. Explicit matching build/
    install configurations corrected the fixture without changing library code.
11. Prefix advancement cleared the first-sample gap flag but retained its loss
    count, making suffix receipts ambiguous. The regression failed before the
    correction; advancing a positive prefix now clears both, preventing duplicate
    loss accounting while raw source status preserves the original receipt.
12. Aligned reference points reflected drift but their period receipt still used
    nominal rate. A public-consumer interpolation oracle failed under +/-drift.
    The adapter now fits the admitted block's observed interval and includes
    intermediate model error/provider uncertainty in its bound. Raw source timing
    remains separate; no precise physical-device alignment is inferred.
13. Shared adapter-only ELF consumers dropped their direct core dependency under
    `--as-needed`, and the adapter lacked a transitive loader path. A clean build
    of `bfd782f` reproduced the loader failure. Co-located loader paths and an
    independent minimal consumer now cover both normal and relocated installs.

Superseded CI runs were cancelled to avoid redundant matrix work. Failed logs and
verification limits remain in the evidence ledger, not suppressed by tolerances.

## Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBF_WITH_SPEEXDSP=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/prefix
cmake -S tests/consumer -B consumer -DCMAKE_PREFIX_PATH=/your/prefix
cmake --build consumer --parallel
ctest --test-dir consumer --output-on-failure
cmake -S examples/pcm-pipeline -B pcm -DCMAKE_PREFIX_PATH=/your/prefix
cmake --build pcm --parallel
ctest --test-dir pcm --output-on-failure

cmake -S . -B asan -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBF_WITH_SPEEXDSP=ON -DBF_SANITIZER=address
cmake --build asan --parallel
ctest --test-dir asan --output-on-failure
# Repeat separately with BF_SANITIZER=undefined or thread.

cmake -S . -B fuzz -DCMAKE_C_COMPILER=clang -DBF_BUILD_TESTS=OFF -DBF_BUILD_BENCHMARKS=OFF -DBF_BUILD_FUZZER=ON
cmake --build fuzz --parallel
python3 tools/fuzz_corpus.py fuzz/corpus
fuzz/baffle_fuzz_transport fuzz/corpus/transport -runs=30000 -max_len=4096 -seed=721
fuzz/baffle_fuzz_pipeline fuzz/corpus/pipeline -runs=30000 -max_len=4096 -seed=722
```

Hail: install pinned Hail first, configure Baffle `BF_WITH_HAIL=ON` with its prefix,
install Baffle, then configure `examples/hail-consumer` against installed packages.
CI provides platform commands and Windows DLL paths. Use separate build directories
for compiler/shared/sanitizer variants. See [qualification](qualification.md),
[performance](performance.md), and [ABI assessment](api.md).
