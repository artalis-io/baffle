# Baffle: bounded native DSP for conversational audio

CPU-first C11 audio processing. Baffle processes PCM; Hail acquires/transports
audio; inference interprets/generates speech; Hull orchestrates applications.
No Python, GPU, audio device API or inference framework is required by the core.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/prefix
```

Installed consumers use `find_package(Baffle 0.1.0 EXACT CONFIG REQUIRED)` and
`Baffle::baffle`. ABI 0.1 is provisional. Foundation is implemented; DSP gates
are tracked in [verification](docs/verification.md), not implied by this README.

MIT licensed; see LICENSE and NOTICE. Hail's engineering conventions are adapted,
not its audio-device implementation.
