set -eu
cd /tmp/baffle-c11-20261121
speex=/tmp/baffle.fVOhT0/build-aec/_deps/baffle_speex_source-src
cmake -S . -B build-adapter -DBF_WITH_HAIL=ON -DBF_WITH_SPEEXDSP=ON -DBF_SPEEXDSP_SOURCE_DIR="$speex" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/tmp/baffle.fVOhT0/build-installed > adapter-build.log 2>&1
cmake --build build-adapter --parallel 6 >> adapter-build.log 2>&1
cmake --install build-adapter --prefix /tmp/baffle-c11-20261121/sdk >> adapter-build.log 2>&1
cmake -S examples/hail-consumer -B build-hail-consumer '-DCMAKE_PREFIX_PATH=/tmp/baffle-c11-20261121/sdk;/tmp/baffle.fVOhT0/build-installed' > hail-build.log 2>&1
cmake --build build-hail-consumer --parallel 6 >> hail-build.log 2>&1
ctest --test-dir build-hail-consumer --output-on-failure > hail-test.log 2>&1
tail -5 hail-test.log
export PKG_CONFIG_PATH=/tmp/baffle-acoustic-tools/webrtc-build/meson-private
export LD_LIBRARY_PATH=/tmp/baffle-acoustic-tools/installed/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
cmake -S tools/acoustic-runner -B build-webrtc -DBF_COMPARE_WEBRTC=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/tmp/baffle-c11-20261121/sdk > webrtc-build.log 2>&1
cmake --build build-webrtc --parallel 6 >> webrtc-build.log 2>&1
build-webrtc/baffle_acoustic_runner webrtc 16000 /tmp/baffle-acoustic-tools/word-input-final/normal-mic.f32 /tmp/baffle-acoustic-tools/word-input-final/normal-ref.f32 webrtc.f32 webrtc.csv webrtc.json > webrtc-test.log 2>&1