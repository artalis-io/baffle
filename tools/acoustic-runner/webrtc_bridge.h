#ifndef BAFFLE_QA_WEBRTC_BRIDGE_H
#define BAFFLE_QA_WEBRTC_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Only this foreign-API shim needs C++; the runner and owned DSP remain C11. */
typedef struct BaffleQaWebrtc BaffleQaWebrtc;
int baffle_qa_webrtc_open(uint32_t rate, int linear, BaffleQaWebrtc **out);
int baffle_qa_webrtc_process(BaffleQaWebrtc *, const float *mic, const float *ref, float *out);
void baffle_qa_webrtc_close(BaffleQaWebrtc *);
#ifdef __cplusplus
}
#endif
#endif
