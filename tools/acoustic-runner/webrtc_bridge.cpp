/* Optional foreign WebRTC C++ API bridge only; not linked into Baffle core. */
#include "webrtc_bridge.h"
#include <api/audio/audio_processing.h>
#include <api/scoped_refptr.h>
#include <array>
#include <algorithm>
#include <new>
struct BaffleQaWebrtc {
    rtc::scoped_refptr<webrtc::AudioProcessing> apm;
    webrtc::StreamConfig stream;
    uint32_t rate;
    bool linear;
    BaffleQaWebrtc(uint32_t r, bool l) : stream(static_cast<int>(r), 1), rate(r), linear(l) {}
};
int baffle_qa_webrtc_open(uint32_t rate, int linear, BaffleQaWebrtc **out)
{
    if (!out)
        return 0;
    *out = nullptr;
#ifndef BF_COMPARE_WEBRTC_LINEAR
    if (linear)
        return 0;
#endif
    if (linear && rate != 16000)
        return 0;
    BaffleQaWebrtc *s = nullptr;
    try {
        s = new BaffleQaWebrtc(rate, linear != 0);
        s->apm = webrtc::AudioProcessingBuilder().Create();
        if (!s->apm) {
            delete s;
            return 0;
        }
        webrtc::AudioProcessing::Config c;
        c.echo_canceller.enabled = true;
        c.echo_canceller.mobile_mode = false;
        c.echo_canceller.enforce_high_pass_filtering = false;
        c.echo_canceller.export_linear_aec_output = linear != 0;
        c.high_pass_filter.enabled = false;
        c.noise_suppression.enabled = false;
        c.gain_controller1.enabled = false;
        c.gain_controller2.enabled = false;
        s->apm->ApplyConfig(c);
        webrtc::ProcessingConfig p;
        p.input_stream() = p.output_stream() = p.reverse_input_stream() =
            p.reverse_output_stream() = s->stream;
        if (s->apm->Initialize(p)) {
            delete s;
            return 0;
        }
        *out = s;
        return 1;
    } catch (...) {
        delete s;
        return 0;
    }
}
int baffle_qa_webrtc_process(BaffleQaWebrtc *s, const float *mic, const float *ref, float *out)
{
    if (!s || !mic || !ref || !out)
        return 0;
    try {
        float reverse[480] = {0};
        const float *fp[] = {ref}, *np[] = {mic};
        float *rp[] = {reverse}, *op[] = {out};
        if (s->apm->ProcessReverseStream(fp, s->stream, s->stream, rp) ||
            s->apm->set_stream_delay_ms(0) || s->apm->ProcessStream(np, s->stream, s->stream, op))
            return 0;
        if (s->linear) {
            std::array<float, 160> linear{};
            if (!s->apm->GetLinearAecOutput(rtc::ArrayView<std::array<float, 160>>(&linear, 1)))
                return 0;
            std::copy(linear.begin(), linear.end(), out);
        }
        return 1;
    } catch (...) {
        return 0;
    }
}
void baffle_qa_webrtc_close(BaffleQaWebrtc *s)
{
    delete s;
}
