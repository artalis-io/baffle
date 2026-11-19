#include <baffle.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
int main(void)
{
    float left[]={-1,0,.5f,1},right[]={.25f,-.25f,0,-.5f};
    void *planes[]={left,right}; int16_t interleaved[8]={0};
    bf_audio_buffer in=BF_INIT(bf_audio_buffer),out=BF_INIT(bf_audio_buffer);
    bf_pcm_format f=BF_INIT(bf_pcm_format); bf_audio_timing t=BF_INIT(bf_audio_timing);
    f.sample_rate=48000; f.channels=2; f.sample_format=BF_PCM_F32; f.storage=BF_PLANAR; f.channel_layout=BF_LAYOUT_STEREO;
    t.clock_domain=11; t.epoch=1; t.flags=BF_TIME_HOST_VALID; t.host_time_ns=1000000000; t.uncertainty_ns=UINT64_MAX;
    in.format=f; in.timing=t; in.planes=planes; in.frames=in.capacity_frames=4;
    out.format=f; out.format.storage=BF_INTERLEAVED; out.format.sample_format=BF_PCM_S16; out.timing=t;
    out.data=interleaved; out.capacity_frames=4;
    CHECK(bf_pcm_convert(&in,&out,0)==BF_OK);
    CHECK(interleaved[0]==INT16_MIN && interleaved[1]==8192 && interleaved[4]==16384 && interleaved[6]==INT16_MAX);
    CHECK(out.timing.host_time_ns==t.host_time_ns && out.timing.uncertainty_ns==UINT64_MAX);
    left[3]=NAN; memset(interleaved,0x5a,sizeof(interleaved));
    CHECK(bf_pcm_convert(&in,&out,0)==BF_ERROR_NONFINITE && interleaved[0]==0x5a5a);
    CHECK(bf_pcm_convert(&in,&out,BF_NONFINITE_ZERO)==BF_OK && interleaved[6]==0 && (out.timing.flags&BF_AUDIO_SANITIZED));
    CHECK(bf_timing_advance(&t,48,48000,&out.timing)==BF_OK && out.timing.host_time_ns==1001000000);
    t.host_time_ns=UINT64_MAX; CHECK(bf_timing_advance(&t,48,48000,&out.timing)==BF_ERROR_OVERFLOW);
    in.format.channels=33; CHECK(bf_pcm_validate(&in)==BF_ERROR_INVALID);
    {
        unsigned char *memory=malloc(bf_clock_tracker_size()+BF_ALIGNMENT); bf_clock_tracker *tracker;
        void *aligned=(void *)(((uintptr_t)memory+BF_ALIGNMENT-1u)&~(uintptr_t)(BF_ALIGNMENT-1u));
        bf_clock_estimate e=BF_INIT(bf_clock_estimate);
        CHECK(memory && bf_clock_tracker_init(aligned,bf_clock_tracker_size(),&tracker)==BF_OK);
        t.host_time_ns=1000000000; t.frame_position=0;
        CHECK(bf_clock_tracker_observe(tracker,&t,48000,&e)==BF_OK && !e.valid);
        t.host_time_ns+=1000000000; t.frame_position=48048;
        CHECK(bf_clock_tracker_observe(tracker,&t,48000,&e)==BF_OK && e.valid && e.uncertain && fabs(e.observed_ppm-1000)<1e-6);
        ++t.epoch; CHECK(bf_clock_tracker_observe(tracker,&t,48000,&e)==BF_OK && !e.valid);
        t.host_time_ns+=1000000000; t.frame_position+=47952; t.uncertainty_ns=1;
        CHECK(bf_clock_tracker_observe(tracker,&t,48000,&e)==BF_OK && e.valid && fabs(e.observed_ppm+1000)<1e-6);
        free(memory);
    }
    puts("PCM conversion, transactional errors, metadata, independent-clock slopes and epochs passed");
    return 0;
}
