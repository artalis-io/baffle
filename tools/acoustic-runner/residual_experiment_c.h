#ifndef BAFFLE_RESIDUAL_EXPERIMENT_C_H
#define BAFFLE_RESIDUAL_EXPERIMENT_C_H
#include <baffle.h>
typedef struct ResidualExperiment ResidualExperiment;
bf_result residual_open(uint32_t rate, int enabled, ResidualExperiment **out);
void residual_close(ResidualExperiment *s);
size_t residual_bytes(const ResidualExperiment *s);
bf_result residual_process(ResidualExperiment *s, float *mic, const bf_audio_timing *nt,
                           const float *ref, const bf_audio_timing *rt, bf_aec_info *info);
#endif
