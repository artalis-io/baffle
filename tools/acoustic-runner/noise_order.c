/* Installed-public-API offline NS hint / AGC->NS experiment. */
#include "../common/demo.h"
#include <baffle.h>
#define REQUIRE(x, m)                                                                              \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            error = (m);                                                                           \
            goto done;                                                                             \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    const char *error = NULL;
    int result = 1;
    float *input = NULL, *output = NULL, *block = NULL, *processed = NULL;
    int64_t *times = NULL;
    size_t length = 0, bytes = 0;
    uint32_t rate = 0, f = 0;
    demo_arena ns_memory = {0}, agc_memory = {0};
    FILE *receipt = NULL;
    bf_noise *ns = NULL;
    bf_agc *agc = NULL;
    REQUIRE(argc == 6, "mode: protected|agc-ns, rate, input.f32, output.f32, receipt.json");
    REQUIRE((!strcmp(argv[1], "protected") || !strcmp(argv[1], "agc-ns")) &&
                demo_number(argv[2], 48000, &rate) && (rate == 16000 || rate == 48000),
            "configuration");
    f = rate / 100;
    REQUIRE(demo_load_f32(argv[3], 256u * 1024u * 1024u, &input, &length), "input size/read");
    for (size_t i = 0; i < length; ++i)
        REQUIRE(isfinite(input[i]) && fabsf(input[i]) <= 16, "invalid PCM");
    bf_noise_config config = BF_INIT(bf_noise_config);
    config.sample_rate = rate;
    config.channels = 1;
    config.frame_frames = f;
    config.strength_db = 18;
    REQUIRE(bf_noise_size(&config, &bytes) == BF_OK &&
                demo_arena_init(&ns_memory, bytes, BF_ALIGNMENT),
            "noise plan/storage");
    REQUIRE(bf_noise_init(ns_memory.aligned, bytes, &config, &ns) == BF_OK, "noise init");
    if (!strcmp(argv[1], "agc-ns")) {
        bf_agc_config c = BF_INIT(bf_agc_config);
        c.sample_rate = rate;
        c.channels = 1;
        c.max_frames = f;
        c.target_dbfs = -18;
        c.min_gain_db = -20;
        c.max_gain_db = 20;
        c.attack_ms = 5;
        c.release_ms = 250;
        c.gate_dbfs = -60;
        c.limit_dbfs = -1;
        REQUIRE(demo_arena_init(&agc_memory, bf_agc_size(), BF_ALIGNMENT) &&
                    bf_agc_init(agc_memory.aligned, bf_agc_size(), &c, &agc) == BF_OK,
                "AGC init");
    }
    size_t calls = (length + f - 1) / f;
    output = (float *)calloc(length + f, sizeof(float));
    block = (float *)calloc(f, sizeof(float));
    processed = (float *)calloc(f, sizeof(float));
    times = (int64_t *)calloc(calls + 1, sizeof(int64_t));
    REQUIRE(output && block && processed && times, "working memory");
    float maximum_gain = 1;
    for (size_t call = 0; call <= calls; ++call) {
        memset(block, 0, f * sizeof(float));
        size_t first = call * f;
        if (call < calls) {
            size_t n = length - first;
            if (n > f)
                n = f;
            memcpy(block, input + first, n * sizeof(float));
        }
        uint64_t start, end;
        REQUIRE(demo_now(&start), "monotonic clock");
        if (agc && call < calls) {
            bf_level_info info = BF_INIT(bf_level_info);
            REQUIRE(bf_agc_process(agc, block, block, f, -1, &info) == BF_OK, "AGC process");
            maximum_gain = fmaxf(maximum_gain, info.gain);
        }
        REQUIRE(bf_noise_process(ns, block, processed,
                                 !strcmp(argv[1], "protected") || call == calls ? 1.0f : -1.0f) ==
                    BF_OK,
                "noise process");
        REQUIRE(demo_now(&end) && end >= start && end - start <= INT64_MAX,
                "monotonic processing interval");
        times[call] = (int64_t)(end - start);
        if (first < length + f) {
            size_t n = length + f - first;
            if (n > f)
                n = f;
            memcpy(output + first, processed, n * sizeof(float));
        }
    }
    REQUIRE(demo_write_f32(argv[4], output, length + f), "output write");
    receipt = demo_fopen(argv[5], "wb");
    REQUIRE(receipt, "receipt open");
    fprintf(receipt,
            "{\"mode\":\"%s\",\"rate\":%u,\"frames\":%zu,\"algorithmic_delay_frames\":%u,"
            "\"workspace_bytes\":%zu,\"max_agc_gain\":%.9g,\"processing_ns\":[",
            argv[1], rate, length, f, bytes + (agc ? bf_agc_size() : 0), (double)maximum_gain);
    for (size_t i = 0; i <= calls; ++i)
        fprintf(receipt, "%s%lld", i ? "," : "", (long long)times[i]);
    fprintf(receipt, "]}\n");
    REQUIRE(!ferror(receipt), "receipt write");
    result = 0;
done:
    if (receipt && fclose(receipt))
        result = 1;
    demo_arena_free(&ns_memory);
    demo_arena_free(&agc_memory);
    free(input);
    free(output);
    free(block);
    free(processed);
    free(times);
    if (error)
        fprintf(stderr, "%s\n", error);
    return result;
}
