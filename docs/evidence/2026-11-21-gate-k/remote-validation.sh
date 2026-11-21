#!/bin/sh
set -eu
cd /tmp/baffle.fVOhT0
compiler=/tmp/hail-gate-e-final.lTzsbV/compiler/usr
export LD_LIBRARY_PATH="$compiler/lib/aarch64-linux-gnu:${LD_LIBRARY_PATH:-}"
speex=/tmp/baffle.fVOhT0/build-aec/_deps/baffle_speex_source-src
for mode in asan tsan; do
  sanitizer=address
  if [ "$mode" = tsan ]; then sanitizer=thread; fi
  cmake -S . -B "build-k-$mode" -DCMAKE_C_COMPILER="$compiler/bin/clang-18" -DCMAKE_CXX_COMPILER="$compiler/bin/clang++-18" -DCMAKE_BUILD_TYPE=Debug -DBF_WITH_SPEEXDSP=ON -DBF_BUILD_TOOLS=ON -DBF_SANITIZER="$sanitizer" -DBF_SPEEXDSP_SOURCE_DIR="$speex" > "/tmp/baffle-k-$mode-config.log" 2>&1
  cmake --build "build-k-$mode" --parallel 4 > "/tmp/baffle-k-$mode-build.log" 2>&1
  ctest --test-dir "build-k-$mode" --output-on-failure --no-tests=error > "/tmp/baffle-k-$mode-tests.log" 2>&1
done
before=/tmp/baffle-k-counter-before-17aea11
git worktree add --detach "$before" 000d1d4 > /tmp/baffle-k-counter-worktree.log 2>&1
cp tools/adaptive_trace.c "$before/tools/adaptive_trace.c"
cat >> "$before/src/speex/mdf_baffle.c" <<'EOF'
#include <limits.h>
#ifdef BF_AEC_DIAGNOSTICS
void baffle_speex_counter_limit(SpeexEchoState *s) { s->cancel_count=INT_MAX-1; }
#endif
EOF
cmake -S "$before" -B build-k-counter-before -DCMAKE_C_COMPILER="$compiler/bin/clang-18" -DCMAKE_BUILD_TYPE=Debug -DBF_WITH_SPEEXDSP=ON -DBF_BUILD_TOOLS=ON -DBF_BUILD_TESTS=OFF -DBF_BUILD_BENCHMARKS=OFF -DBF_SANITIZER=undefined -DBF_SPEEXDSP_SOURCE_DIR="$speex" > /tmp/baffle-k-counter-before-config.log 2>&1
cmake --build build-k-counter-before --parallel 4 > /tmp/baffle-k-counter-before-build.log 2>&1
python3 - <<'PY'
import array,math
for name,scale in [('mic',.1),('ref',.2)]:
 with open('/tmp/baffle-k-counter-'+name+'.f32','wb') as f:array.array('f',(scale*math.sin(i*.13) for i in range(1600))).tofile(f)
PY
set +e
UBSAN_OPTIONS=halt_on_error=1 build-k-counter-before/baffle_adaptive_trace 16000 10 200 /tmp/baffle-k-counter-mic.f32 /tmp/baffle-k-counter-ref.f32 /tmp/baffle-k-counter-before.f32 /tmp/baffle-k-counter-before.csv /tmp/baffle-k-counter-before-coeff.f32 0 counter-limit > /tmp/baffle-k-counter-before-ubsan.log 2>&1
status=$?
set -e
test "$status" -ne 0
rg -q 'signed integer overflow' /tmp/baffle-k-counter-before-ubsan.log
echo "EXPECTED baseline signed overflow reproduced; exit=$status" > /tmp/baffle-k-counter-before-result.log
cmake --build build-fuzz --parallel 4 > /tmp/baffle-k-fuzz-build.log 2>&1
python3 tools/fuzz_corpus.py build-k-corpus
build-fuzz/baffle_fuzz_pipeline build-k-corpus/pipeline -runs=30000 -max_len=4096 -seed=722 > /tmp/baffle-k-pipeline-fuzz.log 2>&1
build-fuzz/baffle_fuzz_transport build-k-corpus/transport -runs=30000 -max_len=4096 -seed=721 > /tmp/baffle-k-transport-fuzz.log 2>&1
build-fuzz/baffle_fuzz_aec build-k-corpus/aec -runs=10000 -max_len=4096 -seed=723 > /tmp/baffle-k-aec-fuzz.log 2>&1
cmake -S . -B build-k-analysis -DCMAKE_BUILD_TYPE=Debug -DBF_BUILD_TESTS=OFF -DBF_BUILD_BENCHMARKS=OFF -DCMAKE_C_FLAGS=-fanalyzer > /tmp/baffle-k-analysis-config.log 2>&1
cmake --build build-k-analysis --parallel 4 > /tmp/baffle-k-analysis.log 2>&1
echo 'Completed fixed-source sanitizers, expected-before overflow, fuzzing and core analysis'
