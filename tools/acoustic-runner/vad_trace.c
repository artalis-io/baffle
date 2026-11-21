/* Offline public-API observer; file I/O is outside bf_vad_process. */
#include <baffle.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    if (argc!=4 || !strcmp(argv[2],argv[3]) || argv[1][0]<'0' || argv[1][0]>'9') return 2;
    char *end;
    unsigned long rate=strtoul(argv[1],&end,10);
    if (*end || rate<8000 || rate>48000 || rate%100) return 2;
    FILE *in=NULL,*out=NULL;
#ifdef _MSC_VER
    if (fopen_s(&in,argv[2],"rb")) return 2;
#else
    in=fopen(argv[2],"rb");
#endif
    if (!in) return 2;
    if (fseek(in,0,SEEK_END)) return 2;
    long bytes=ftell(in);
    if (bytes<=0 || bytes>256*1024*1024 || bytes%4) return 2;
    rewind(in);
#ifdef _MSC_VER
    if (fopen_s(&out,argv[3],"wx")) return 2;
#else
    out=fopen(argv[3],"wx");
#endif
    if (!out) return 2; /* Preserve prior review receipts and source aliases. */
    bf_vad_config c=BF_INIT(bf_vad_config);
    c.sample_rate=(uint32_t)rate; c.channels=1; c.max_frames=(uint32_t)rate/100;
    c.onset_ms=20; c.hangover_ms=100; c.threshold=.55f; c.noise_floor_dbfs=-60;
    unsigned char *raw=malloc(bf_vad_size()+BF_ALIGNMENT);
    if (!raw) return 1;
    void *memory=(void *)(((uintptr_t)raw+BF_ALIGNMENT-1u)&~(uintptr_t)(BF_ALIGNMENT-1u));
    bf_vad *vad;
    if (bf_vad_init(memory,bf_vad_size(),&c,&vad)!=BF_OK) return 1;
    float pcm[480]; uint64_t position=0; size_t n;
    fprintf(out,"first_frame,frames,active,confidence,noise_rms\n");
    while ((n=fread(pcm,sizeof(float),c.max_frames,in))!=0) {
        bf_vad_info info=BF_INIT(bf_vad_info);
        if (bf_vad_process(vad,pcm,(uint32_t)n,&info)!=BF_OK) return 1;
        fprintf(out,"%llu,%u,%u,%.9g,%.9g\n",(unsigned long long)position,(unsigned)n,
                info.active,(double)info.probability,(double)info.noise_rms);
        position+=n;
    }
    int failed=ferror(in)||ferror(out);
    free(raw); fclose(in); if (fclose(out)) failed=1;
    return failed ? 1:0;
}
