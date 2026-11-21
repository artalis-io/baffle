/* Private backend qualification, NOT an installed/public consumer example.
 * Compile the actual wrapper source so IR snapshots can use its opaque backend
 * handle without adding public API. Static-only; source pin/license unchanged.
 * File I/O and coefficient queries are outside bf_aec_process. */
#include "../src/aec.c"
#include <stdio.h>
#include <stdlib.h>
void baffle_speex_diagnostics(SpeexEchoState *, float *);
void baffle_speex_counter_limit(SpeexEchoState *);
static void require(int condition) { if (!condition) { fputs("adaptive trace failure\n", stderr); exit(1); } }
static FILE *open_file(const char *path, const char *mode)
{
#ifdef _MSC_VER
    FILE *f=NULL; if(fopen_s(&f,path,mode))return NULL; return f;
#else
    return fopen(path,mode);
#endif
}
static float *read_pcm(const char *path, size_t *count)
{
    FILE *f = open_file(path, "rb"); require(f != NULL);
    require(fseek(f, 0, SEEK_END) == 0); long n = ftell(f);
    require(n > 0 && n <= 128*1024*1024 && n%4 == 0);
    *count = (size_t)n/4; rewind(f); float *p = malloc((size_t)n); require(p != NULL);
    require(fread(p, 4, *count, f) == *count); fclose(f); return p;
}
int main(int argc, char **argv)
{
    require(argc == 10 || argc == 11);
    const uint16_t endian=1; require(sizeof(float)==4 && *(const unsigned char *)&endian==1);
    uint32_t rate = (uint32_t)strtoul(argv[1], NULL, 10), hop = (uint32_t)strtoul(argv[2], NULL, 10);
    uint32_t tail = (uint32_t)strtoul(argv[3], NULL, 10), frames = rate/1000u*hop;
    require((rate == 16000 || rate == 48000) && (hop == 10 || hop == 20));
    size_t count, refs; float *mic = read_pcm(argv[4], &count), *ref = read_pcm(argv[5], &refs);
    require(count == refs);
    bf_aec_config c = BF_INIT(bf_aec_config); c.sample_rate = rate;c.frame_frames = frames;
    c.capture_channels = c.reference_channels = 1; c.tail_ms = tail; c.backend = BF_AEC_SPEEXDSP;
    size_t bytes; require(bf_aec_size(&c, &bytes) == BF_OK);
    void *raw = malloc(bytes+BF_ALIGNMENT); require(raw != NULL);
    void *mem = (void *)(((uintptr_t)raw+BF_ALIGNMENT-1u)&~(uintptr_t)(BF_ALIGNMENT-1u));
    bf_aec *s; require(bf_aec_init(mem, bytes, &c, &s) == BF_OK);
    if(argc==11) {require(strcmp(argv[10],"counter-limit")==0);baffle_speex_counter_limit(s->echo);}
    FILE *output = open_file(argv[6], "wb"), *trace = open_file(argv[7], "w"), *coeff = open_file(argv[8], "wb");
    require(output && trace && coeff);
    int missing = atoi(argv[9]); /* 0, or one deterministic gap at 10..10.2s. */
    fprintf(trace,"frame_position,frames,input_power,output_power,delay_valid,dominant_tap,adapted,cancel_count,leak_estimate,sum_adapt,saturated,screwed_up,resets,flags,coefficient_snapshot\n");
    float near[960], far[960], out[960];
    bf_audio_timing nt = BF_INIT(bf_audio_timing), ft = BF_INIT(bf_audio_timing);
    nt.stream_id=nt.clock_domain=1;ft.stream_id=ft.clock_domain=2;nt.epoch=ft.epoch=1;
    nt.uncertainty_ns=ft.uncertainty_ns=UINT64_MAX;
    for (size_t first=0;first<count;first+=frames) {
        size_t n=count-first < frames ? count-first:frames;
        memset(near,0,sizeof(near));memset(far,0,sizeof(far));
        memcpy(near,mic+first,n*4);memcpy(far,ref+first,n*4);
        nt.frame_position=ft.frame_position=(uint64_t)first;
        bf_aec_info info = BF_INIT(bf_aec_info);
        int gap=missing && first >= 10u*rate && first < 10u*rate+rate/5u;
        require(bf_aec_process(s,near,&nt,gap ? NULL:far,gap ? NULL:&ft,out,&info)==BF_OK);
        require(fwrite(out,4,n,output)==n);
        float diag[6]; baffle_speex_diagnostics(s->echo,diag);
        int snapshot=(first+frames)%(rate/2u)==0;
        uint32_t delay=0; bf_result valid=snapshot ? bf_aec_estimate_delay(s,&delay):BF_ERROR_STATE;
        if(snapshot) {
            /* estimate_delay filled background IR, not the foreground bank. */
            for(uint32_t i=0;i<s->impulse_frames;++i) {
                float v=(float)s->impulse[i]/32767.0f;require(fwrite(&v,4,1,coeff)==1);
            }
        }
        double input=0,power=0;
        for(size_t i=0;i<n;++i) { require(isfinite(out[i]));input+=(double)near[i]*near[i];power+=(double)out[i]*out[i]; }
        fprintf(trace,"%llu,%zu,%.12g,%.12g,%d,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%llu,%u,%d\n",(unsigned long long)first,n,input/(double)n,power/(double)n,snapshot ? valid==BF_OK:-1,delay,diag[0],diag[1],diag[2],diag[3],diag[4],diag[5],(unsigned long long)info.resets,info.flags,snapshot);
    }
    require(bf_aec_deinit(s)==BF_OK);free(raw);free(mic);free(ref);
    require(fclose(output)==0 && fclose(trace)==0 && fclose(coeff)==0);return 0;
}
