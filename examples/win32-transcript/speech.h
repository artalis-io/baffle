#ifndef BAFFLE_NATIVE_SPEECH_H
#define BAFFLE_NATIVE_SPEECH_H
#include <windows.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include "captions.h"
#define APP_PATH 32768u
#define APP_ERROR 512u
#define SPEECH_SAMPLES 320000u
typedef struct Speech Speech;
typedef struct SpeechResult {
    CapText text;
    double last_two_start;
    double word_starts[CAP_WORDS];
    size_t word_count;
} SpeechResult;
extern const wchar_t *const app_trial_prompt;
int app_exe_dir(wchar_t *path, size_t capacity);
int app_join_path(wchar_t *out, size_t capacity, const wchar_t *dir, const wchar_t *name);
int app_utf8_to_wide(const char *input, wchar_t *out, size_t capacity);
int app_wide_to_utf8(const wchar_t *input, char *out, size_t capacity);
FILE *app_file(const wchar_t *path, const wchar_t *mode);
int app_file_exists(const wchar_t *path);
int app_make_directory(const wchar_t *path);
int app_empty_directory(const wchar_t *path);
int app_read_wave(const wchar_t *path, float **audio, size_t *frames, char *error, size_t capacity);
int app_write_wave(const wchar_t *path, const float *audio, size_t frames);
int speech_open(const wchar_t *models, Speech **out, char *error, size_t capacity);
void speech_close(Speech *s);
int speech_decode(Speech *s, const float *samples, size_t count, SpeechResult *out, char *error,
                  size_t capacity);
size_t speech_overlap_words(const SpeechResult *result, double prefix_until);
int speech_condition(const float *raw, size_t frames, uint32_t stages, float **out, char *error,
                     size_t capacity);
int speech_compare(Speech *s, const float *raw, size_t frames, const wchar_t *directory,
                   const _Atomic int *stop, int feedback, CapText *display, char *error,
                   size_t capacity);
int app_wave_test(void);
#endif
