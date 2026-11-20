# Processing latency and resource evidence

The native [benchmark](../bench/latency.c) measures each synchronous 10 ms call,
after 100 pipeline warm-up calls, with preallocated buffers and no measured I/O.
CSV contains median/P95/P99/max, mean adjacent processing-cost difference,
10 ms deadline misses, wall real-time factor, thread CPU accounting and queried
workspace. Eight-session rows measure the whole serial batch against one 10 ms
deadline. This is unpaced processing cost, not paced device jitter, scheduler
wakeup latency, an inference workload, OS resident-memory peak or physical RTT.

## Reproduction

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DBF_WITH_SPEEXDSP=ON
cmake --build build-perf --parallel
build-perf/baffle_latency 10000 > idle.csv
# Optional QA launcher; not a build/runtime dependency:
python3 tools/benchmark.py build-perf/baffle_latency loaded.csv --iterations 10000 --workers 4
```

The four SHA256 CPU workers are a contention proxy, not Whisper/Parakeet/TTS/LLM
execution. GPU work/services were not touched. CPU time per audio duration is a
normalized single-core cost; CPU utilization during an unpaced run is separately
labelled. Windows GetThreadTimes is coarse: trials under 250 ms report CPU data
as NaN, and small values above 100% in longer raw accounting reflect quantization,
not physically meaningful utilization above one core. Wall percentiles remain
the primary cost measurements. No hard WCET guarantee follows from an observed
maximum or sanitizer success.

## 2026-11-19 measurements

Raw CSV and host metadata: [evidence](evidence/2026-11-19/). Windows: Ryzen 9
5950X, 16 cores/32 threads, Windows 11 build 26300, clang-cl 19.1.5, Release.
Linux: NVIDIA GB10-class host with 10 Cortex-X925/10 Cortex-A725 cores, ARM64,
Linux 7.0.0-1024-nvidia, glibc 2.39, GCC 13.3, Release. CPU affinity/governor were
not pinned, and other system activity was not controlled. Each row has 10,000
iterations (100 seconds of modeled audio); trials were repeated once with the
explicit proxy load. These are initial baselines, not confidence intervals.
The benchmark source revision is `28d3dd8`; the evidence manifest distinguishes
that measured snapshot from subsequent metadata and packaging corrections.

Full pipeline = AEC + 18 dB spectral NS + AGC + VAD, stereo reference, 200 ms AEC
tail. Workspace includes processor state and scratch, excludes caller PCM, test
vectors and Hail adapter history. Allocation guard and object imports found no
heap operations in processing/reset; OS RSS and peak initialization heap are not
claimed as measured quantities.

| Host/load | Capture | Sessions | P50 / P95 / P99 / max (ms) | Workspace bytes | Deadline misses |
| --- | --- | --- | --- | --- | --- |
| Windows, idle | 16k mono | 1 | .0569 / .0624 / .0652 / .1111 | 218128 | 0 |
| Windows, 4 workers | 16k mono | 1 | .0581 / .0639 / .0666 / .1024 | 218128 | 0 |
| Windows, idle | 48k mono | 1 | .1789 / .1866 / .2401 / .4689 | 625168 | 0 |
| Windows, 4 workers | 48k mono | 1 | .1810 / .3507 / .3561 / .4204 | 625168 | 0 |
| Windows, idle | 48k 8ch | 1 | 1.2022 / 1.3142 / 1.6955 / 2.1825 | 3034736 | 0 |
| Windows, 4 workers | 48k 8ch | 1 | 2.1372 / 2.3777 / 2.4163 / 3.2702 | 3034736 | 0 |
| Windows, 4 workers | 16k mono | 8 | .9167 / .9835 / .9960 / 1.1454 | 1745024 | 0 |
| ARM64, idle | 16k mono | 1 | .0303 / .0313 / .0330 / .0728 | 218128 | 0 |
| ARM64, 4 workers | 48k 8ch | 1 | .6717 / .6744 / .6762 / .7267 | 3034736 | 0 |

CSV also records 48k stereo/4-channel, standalone 48->16k SRC, 8->1 matrix, and
single-stage pipeline masks 1=AEC, 2=NS, 4=AGC, 8=VAD. The latter include pipeline
validation/copy overhead and are not pure kernel-only benchmarks. Windows idle
16k AEC/NS/AGC/VAD P99 costs were .0531/.0175/.0043/.0033 ms respectively;
48->16k mono SRC .0456 ms, 8->1 channel matrix .0099 ms. No GPU is selected;
GPU utilization and SIMD speedup are not measured.

Initial engineering budgets for these representative shapes: mono P99 <=2.5 ms,
8-channel P99 <=5 ms, eight 16k sessions <=5 ms per batch, no observed 10 ms misses,
workspace <512 KiB at 16k mono, <1 MiB at 48k mono, <4 MiB at 48k 8ch. These runs
meet those targets; they are not guarantees for every CPU, filter tail, workload
or configuration. Actual concurrent inference and sustained paced deadlines
remain deployment qualification work.

## Buffering and end-to-end latency

SRC 48->16k group delay is 95.5 input frames =1.9896 ms; 16->48k is 31.5 input
frames =1.96875 ms. AGC/limiter and VAD add no lookahead. Synchronous AEC requires
a 10/20 ms input block but adds no Baffle output queue. NS adds one hop (10 ms
in these benchmarks). These are separate from collecting the input hop, transport
queues, device latency and scheduling.

The Hail adapter holds exactly ceil(reference FIR delay in capture frames) when
AEC reference conversion is enabled: 32 samples/2 ms at 16k capture and 48k render.
Combined NS+alignment delay is 192 capture frames/12 ms, before variable hop
assembly/device latency. Disabling AEC removes unused SRC/history work and this
alignment hold. No extra hop was added for an isolated kernel speedup. Hardware
microphone-to-processed-output and physical round-trip latency remain unmeasured.
