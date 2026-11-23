set -eu
cd /tmp/baffle-c11-20261121
export LD_LIBRARY_PATH=/tmp/hail-gate-e-final.lTzsbV/compiler/usr/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
clang=/tmp/hail-gate-e-final.lTzsbV/compiler/usr/bin/clang-18
speex=/tmp/baffle.fVOhT0/build-aec/_deps/baffle_speex_source-src
cmake -S . -B build-clang-tsan -DCMAKE_C_COMPILER="$clang" -DBF_WITH_SPEEXDSP=ON -DBF_SPEEXDSP_SOURCE_DIR="$speex" -DBF_SANITIZER=thread -DCMAKE_BUILD_TYPE=Debug > clang-tsan-build.log 2>&1
cmake --build build-clang-tsan --parallel 6 >> clang-tsan-build.log 2>&1
ctest --test-dir build-clang-tsan --output-on-failure > clang-tsan-test.log 2>&1 || true
tail -5 clang-tsan-test.log
cmake -S . -B build-analysis -DCMAKE_C_COMPILER=gcc -DBF_WITH_SPEEXDSP=ON -DBF_SPEEXDSP_SOURCE_DIR="$speex" -DBF_BUILD_TESTS=OFF -DBF_BUILD_BENCHMARKS=OFF -DCMAKE_C_FLAGS=-fanalyzer > analysis.log 2>&1
cmake --build build-analysis --target baffle --parallel 6 >> analysis.log 2>&1
cmake -S . -B build-fuzz -DCMAKE_C_COMPILER="$clang" -DBF_WITH_SPEEXDSP=ON -DBF_SPEEXDSP_SOURCE_DIR="$speex" -DBF_BUILD_FUZZER=ON -DBF_BUILD_TESTS=OFF -DBF_BUILD_BENCHMARKS=OFF > fuzz-build.log 2>&1
cmake --build build-fuzz --parallel 6 >> fuzz-build.log 2>&1
python3 tools/fuzz_corpus.py build-fuzz/corpus
for name in transport pipeline aec; do
 build-fuzz/baffle_fuzz_$name build-fuzz/corpus/$name -runs=10000 -max_len=4096 -seed=721 > fuzz-$name.log 2>&1
 tail -2 fuzz-$name.log
done
cmake -S tests/consumer -B build-consumer -DBF_TEST_CXX_COMPAT=ON -DCMAKE_PREFIX_PATH=/tmp/baffle-c11-20261121/sdk > consumer-build.log 2>&1
cmake --build build-consumer --parallel 6 >> consumer-build.log 2>&1
ctest --test-dir build-consumer --output-on-failure > consumer-test.log 2>&1
tail -5 consumer-test.log
python3 tools/check_realtime_symbols.py build-release/libbaffle.a > realtime-symbols.log