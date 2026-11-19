#include "baffle_internal.h"
struct bf_channel_delay { uint32_t magic,channels,length,index,delays[BF_MAX_CHANNELS]; float *history; };
bf_result bf_channel_matrix(const bf_audio_buffer *in,bf_audio_buffer *out,const float *matrix,float limit)
{
    bf_result r=bf_pcm_validate(in); if(r!=BF_OK) return r;
    r=bf_pcm_validate(out); if(r!=BF_OK) return r;
    if(!matrix || !isfinite(limit) || limit<=0 || limit>16 ||
       (in->format.storage==BF_INTERLEAVED && out->format.storage==BF_INTERLEAVED && in->data==out->data) ||
       in->format.sample_rate!=out->format.sample_rate) return BF_ERROR_INVALID;
    if(out->capacity_frames<in->frames) return BF_ERROR_CAPACITY;
    r=baffle_finite(in,BF_NONFINITE_REJECT); if(r!=BF_OK) return r;
    for(size_t i=0;i<(size_t)in->format.channels*out->format.channels;++i) if(!isfinite(matrix[i]) || fabsf(matrix[i])>16) return BF_ERROR_INVALID;
    for(uint32_t i=0;i<in->frames;++i) for(uint32_t o=0;o<out->format.channels;++o) {
        double sum=0;
        for(uint32_t c=0;c<in->format.channels;++c) sum+=(double)baffle_clean(baffle_sample(in,i,c))*matrix[(size_t)o*in->format.channels+c];
        baffle_store(out,i,o,(float)fmax(-(double)limit,fmin((double)limit,sum)));
    }
    out->frames=in->frames; baffle_record(&out->timing,&in->timing,sizeof(in->timing)); return BF_OK;
}
bf_result bf_channel_delay_size(uint32_t channels,uint32_t max_delay,size_t *bytes)
{
    size_t n;
    if(!bytes || !channels || channels>BF_MAX_CHANNELS || max_delay>BF_MAX_FRAMES) return BF_ERROR_INVALID;
    *bytes=baffle_align(sizeof(bf_channel_delay));
    if(!baffle_mul((size_t)(max_delay+1u)*channels,sizeof(float),&n) || !baffle_add(bytes,n)) return BF_ERROR_OVERFLOW;
    return BF_OK;
}
bf_result bf_channel_delay_init(void *p,size_t bytes,uint32_t channels,uint32_t max_delay,const uint32_t *delays,bf_channel_delay **out)
{
    size_t need; bf_result r;
    if(!out || !delays) return BF_ERROR_INVALID; *out=NULL;
    r=bf_channel_delay_size(channels,max_delay,&need); if(r!=BF_OK) return r;
    for(uint32_t c=0;c<channels;++c) if(delays[c]>max_delay) return BF_ERROR_INVALID;
    if(!baffle_storage(p,bytes,need)) return BF_ERROR_CAPACITY;
    memset(p,0,need); bf_channel_delay *s=p; s->magic=0x4246444cu; s->channels=channels;s->length=max_delay+1u;
    memcpy(s->delays,delays,channels*sizeof(uint32_t)); s->history=(float *)((unsigned char *)p+baffle_align(sizeof(*s)));
    *out=s; return BF_OK;
}
bf_result bf_channel_delay_reset(bf_channel_delay *s)
{
    if(!s || s->magic!=0x4246444cu) return BF_ERROR_INVALID;
    memset(s->history,0,(size_t)s->length*s->channels*sizeof(float));s->index=0;return BF_OK;
}
bf_result bf_channel_delay_process(bf_channel_delay *s,const float *in,float *out,uint32_t frames)
{
    if(!s || s->magic!=0x4246444cu || frames>BF_MAX_FRAMES || (frames && (!in || !out))) return BF_ERROR_INVALID;
    for(size_t i=0;i<(size_t)frames*s->channels;++i) if(!isfinite(in[i])) return BF_ERROR_NONFINITE;
    for(uint32_t i=0;i<frames;++i) {
        for(uint32_t c=0;c<s->channels;++c) {
            s->history[(size_t)s->index*s->channels+c]=baffle_clean(in[(size_t)i*s->channels+c]);
            uint32_t read=(s->index+s->length-s->delays[c])%s->length;
            out[(size_t)i*s->channels+c]=s->history[(size_t)read*s->channels+c];
        }
        s->index=(s->index+1u)%s->length;
    }
    return BF_OK;
}
