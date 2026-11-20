/* Offline / paced qualification, never linked into Baffle core. Input/output:
 * little-endian IEEE F32 mono, caller-aligned equal-rate reference. No inferred
 * physical timestamps. All file I/O/allocation occurs outside measured calls. */
#include <baffle.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <array>
#include <cstdlib>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#endif
#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#include <dlfcn.h>
#endif
#ifdef BF_COMPARE_WEBRTC
#include <api/audio/audio_processing.h>
#include <api/scoped_refptr.h>
#endif
using Clock = std::chrono::steady_clock;
class WaitPolicy {
#ifdef _WIN32
    HANDLE timer=nullptr;
#endif
public:
    explicit WaitPolicy(bool highres) {
        if (!highres) return;
#ifdef _WIN32
        timer=CreateWaitableTimerExW(nullptr,nullptr,0x00000002u,TIMER_ALL_ACCESS);
        if (!timer) throw std::runtime_error("high-resolution private timer unavailable");
#else
        throw std::runtime_error("highres wait policy is Windows-only");
#endif
    }
    ~WaitPolicy() {
#ifdef _WIN32
        if (timer) CloseHandle(timer);
#endif
    }
    void wait(Clock::time_point release) {
#ifdef _WIN32
        if (timer) {
            auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(release-Clock::now()).count();
            if (remaining<=0) return;
            LARGE_INTEGER due; due.QuadPart=-(remaining+99)/100;
            if (!SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(timer,INFINITE)!=WAIT_OBJECT_0)
                throw std::runtime_error("private timer wait failed");
            return;
        }
#endif
        std::this_thread::sleep_until(release);
    }
};
static void check(bf_result r) { if (r != BF_OK) throw std::runtime_error("Baffle status " + std::to_string(r)); }
static uint32_t number(const char *s, uint32_t max)
{
    size_t end;
    auto v = std::stoull(s, &end);
    if (s[0] < '0' || s[0] > '9' || s[end] || v > max) throw std::runtime_error("invalid integer");
    return static_cast<uint32_t>(v);
}
static std::vector<float> load(const char *path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    auto bytes = f.tellg();
    if (!f || bytes <= 0 || bytes > 256*1024*1024 || bytes % 4) throw std::runtime_error("invalid F32 input size");
    std::vector<float> v(static_cast<size_t>(bytes)/4);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(v.data()), bytes);
    if (!f) throw std::runtime_error("input read");
    for (float x : v) if (!std::isfinite(x) || std::fabs(x)>16) throw std::runtime_error("invalid PCM");
    return v;
}
static int64_t rss_kib()
{
#ifdef __linux__
    std::ifstream f("/proc/self/statm");
    int64_t virtual_pages=0, resident=0;
    f >> virtual_pages >> resident;
    return resident * sysconf(_SC_PAGESIZE) / 1024;
#else
    return -1;
#endif
}
struct Receipt {
    int64_t release, start, end, allocations;
    uint32_t flags, priming, discarded, vad_active;
    float vad_probability, gain;
    uint64_t aec_resets;
};
struct Transport { uint64_t frame,capture_epoch,reference_epoch,generation; uint32_t missing; };
static bf_audio_buffer view(float *data, uint32_t rate, uint32_t frames, uint64_t domain)
{
    bf_audio_buffer b = BF_INIT(bf_audio_buffer);
    b.format = BF_INIT(bf_pcm_format);
    b.timing = BF_INIT(bf_audio_timing);
    b.format.sample_rate=rate; b.format.channels=1; b.format.sample_format=BF_PCM_F32;
    b.format.storage=BF_INTERLEAVED; b.format.channel_layout=BF_LAYOUT_MONO;
    b.data=data; b.frames=b.capacity_frames=frames;
    b.timing.stream_id=domain; b.timing.clock_domain=domain; b.timing.epoch=1;
    b.timing.uncertainty_ns=UINT64_MAX;
    return b;
}
int main(int argc, char **argv) try
{
    if (argc < 8 || argc > 10) throw std::runtime_error(
        "usage: runner baffle|pipeline|aec3|aec3-linear rate mic.f32 ref.f32 out.f32 receipts.csv summary.json [paced_seconds:1..600 [default|highres]]");
    uint16_t endian=1;
    if (*reinterpret_cast<unsigned char *>(&endian)!=1) throw std::runtime_error("little-endian host required");
    std::string backend=argv[1];
    uint32_t tail_ms=backend=="baffle-40ms" ? 40u:backend=="baffle-500ms" ? 500u:200u;
    uint32_t rate=number(argv[2],48000), seconds=argc>=9 ? number(argv[8],600) : 0;
    bool highres=argc==10 && std::string(argv[9])=="highres";
    if (argc==10 && !highres && std::string(argv[9])!="default") throw std::runtime_error("unknown wait policy");
    if ((rate!=8000 && rate!=16000 && rate!=32000 && rate!=48000) || (argc>=9 && !seconds))
        throw std::runtime_error("unsupported rate/duration");
    const uint32_t frames=backend=="ns5" ? rate/200:rate/100;
    if(backend=="ns5"&&seconds) throw std::runtime_error("ns5 is offline delay/quality diagnostic only");
    const bool pipeline_backend=backend=="pipeline"||backend=="aec-ns"||backend=="aec-ns-agc"||
        backend=="aec-vad"||backend=="ns-only"||backend=="ns-zero"||backend=="ns5";
    uint32_t stages=BF_STAGE_ALL;
    if(backend=="aec-ns") stages=BF_STAGE_AEC|BF_STAGE_NOISE;
    if(backend=="aec-ns-agc") stages=BF_STAGE_AEC|BF_STAGE_NOISE|BF_STAGE_AGC;
    if(backend=="aec-vad") stages=BF_STAGE_AEC|BF_STAGE_VAD;
    if(backend=="ns-only"||backend=="ns-zero"||backend=="ns5") stages=BF_STAGE_NOISE;
    const uint32_t pipeline_delay=pipeline_backend&&(stages&BF_STAGE_NOISE) ? frames:0;
    WaitPolicy waiter(highres);
    auto mic=load(argv[3]), ref=load(argv[4]);
    if (mic.size()!=ref.size()) throw std::runtime_error("input duration mismatch");
    size_t calls=seconds ? static_cast<size_t>(seconds)*100 : (mic.size()+frames-1)/frames;
    std::vector<Transport> transport;
    std::string event_path;
#ifdef _MSC_VER
    char *environment=nullptr; size_t environment_size=0;
    if(_dupenv_s(&environment,&environment_size,"BF_QUALIFICATION_TRANSPORT_EVENTS"))
        throw std::runtime_error("transport environment read");
    if(environment) {event_path=environment;std::free(environment);}
#else
    if(const char *path=std::getenv("BF_QUALIFICATION_TRANSPORT_EVENTS")) event_path=path;
#endif
    if(!event_path.empty()) {
        if(seconds||backend.rfind("aec3",0)==0) throw std::runtime_error("transport injection supports offline public Baffle API only");
        std::ifstream file(event_path); if(!file) throw std::runtime_error("transport event input");
        Transport event{};
        while(file>>event.frame>>event.capture_epoch>>event.reference_epoch>>event.generation>>event.missing) {
            if(transport.size()>=128 || event.frame>=mic.size() || event.frame%frames || event.missing>1 ||
               (!transport.empty()&&event.frame<=transport.back().frame)) throw std::runtime_error("bounded ordered frame-aligned events required");
            transport.push_back(event);
        }
        if(!file.eof()) throw std::runtime_error("malformed transport event");
    }
    std::vector<float> result(seconds ? 0 : mic.size()+pipeline_delay);
    std::vector<Receipt> receipts(calls);
    float near[480]={0}, far[480]={0}, out[480]={0}, reverse[480]={0};
    (void)reverse;
    int64_t before=rss_kib();
    bf_aec *aec=nullptr;
    bf_pipeline *pipeline=nullptr;
    size_t bytes=0;
    std::vector<unsigned char> storage;
    auto memory=[&]() -> void * { storage.resize(bytes+BF_ALIGNMENT); return reinterpret_cast<void *>(
        (reinterpret_cast<uintptr_t>(storage.data())+BF_ALIGNMENT-1u)&~static_cast<uintptr_t>(BF_ALIGNMENT-1u)); };
#ifdef BF_COMPARE_WEBRTC
    rtc::scoped_refptr<webrtc::AudioProcessing> apm;
    webrtc::StreamConfig stream(static_cast<int>(rate),1);
    std::array<float,160> linear{};
#endif
    if (backend=="baffle"||backend=="baffle-40ms"||backend=="baffle-500ms") {
        bf_aec_config c=BF_INIT(bf_aec_config);
        c.sample_rate=rate; c.frame_frames=frames; c.capture_channels=c.reference_channels=1;
        c.tail_ms=tail_ms; c.backend=BF_AEC_SPEEXDSP;
        check(bf_aec_size(&c,&bytes)); check(bf_aec_init(memory(),bytes,&c,&aec));
    } else if (pipeline_backend) {
        bf_pipeline_config c=BF_INIT(bf_pipeline_config);
        c.sample_rate=rate; c.frame_frames=frames; c.channels=c.reference_channels=1;
        c.stages=stages; c.aec_tail_ms=200; c.noise_strength_db=backend=="ns-zero" ? 0.0f:18.0f; c.agc_target_dbfs=-18;
        check(bf_pipeline_size(&c,&bytes)); check(bf_pipeline_init(memory(),bytes,&c,&pipeline));
    } else if (backend=="aec3" || backend=="aec3-linear") {
#ifdef BF_COMPARE_WEBRTC
#ifndef BF_COMPARE_WEBRTC_LINEAR
        if (backend=="aec3-linear") throw std::runtime_error("linear export requires the separately patched diagnostic package");
#endif
        apm=webrtc::AudioProcessingBuilder().Create();
        if (!apm) throw std::runtime_error("AEC3 creation");
        webrtc::AudioProcessing::Config c;
        c.echo_canceller.enabled=true; c.echo_canceller.mobile_mode=false;
        c.echo_canceller.enforce_high_pass_filtering=false;
        c.echo_canceller.export_linear_aec_output=backend=="aec3-linear";
        if (backend=="aec3-linear" && rate!=16000) throw std::runtime_error("linear diagnostic requires 16k");
        c.high_pass_filter.enabled=false; c.noise_suppression.enabled=false;
        c.gain_controller1.enabled=false; c.gain_controller2.enabled=false;
        apm->ApplyConfig(c);
        webrtc::ProcessingConfig p;
        p.input_stream()=p.output_stream()=p.reverse_input_stream()=p.reverse_output_stream()=stream;
        if (apm->Initialize(p)) throw std::runtime_error("AEC3 initialization");
#else
        throw std::runtime_error("AEC3 unavailable: configure BF_COMPARE_WEBRTC against external package");
#endif
    } else throw std::runtime_error("unknown backend");
    int64_t initialized=rss_kib();
    void (*alloc_begin)()=nullptr;
    unsigned long long (*alloc_end)()=nullptr;
#ifdef __linux__
    alloc_begin=reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT,"bf_qa_alloc_begin"));
    alloc_end=reinterpret_cast<unsigned long long (*)()>(dlsym(RTLD_DEFAULT,"bf_qa_alloc_end"));
#endif
    bf_audio_buffer n=view(near,rate,frames,1), r=view(far,rate,frames,2), o=view(out,rate,frames,1);
    size_t transport_index=0; bool missing_reference=false;
    Receipt observed{};
    auto process=[&](uint64_t position) -> uint32_t {
        observed={}; observed.gain=1;
        n.timing.frame_position=r.timing.frame_position=position;
        n.timing.flags=r.timing.flags=0;
        if(transport_index<transport.size()&&transport[transport_index].frame==position) {
            auto e=transport[transport_index++];
            if(e.capture_epoch!=n.timing.epoch) n.timing.flags|=BF_AUDIO_DISCONTINUITY;
            if(e.reference_epoch!=r.timing.epoch) r.timing.flags|=BF_AUDIO_DISCONTINUITY;
            n.timing.epoch=e.capture_epoch; r.timing.epoch=e.reference_epoch; r.timing.generation=e.generation;
            missing_reference=e.missing!=0;
        }
        if (aec) {
            bf_aec_info info=BF_INIT(bf_aec_info);
            check(bf_aec_process(aec,near,&n.timing,missing_reference ? nullptr:far,missing_reference ? nullptr:&r.timing,out,&info));
            observed.aec_resets=info.resets;
            return info.flags;
        }
        if (pipeline) {
            bf_process_info info=BF_INIT(bf_process_info);
            check(bf_pipeline_process(pipeline,&n,missing_reference ? nullptr:&r,&o,&info));
            observed.priming=info.priming; observed.discarded=info.discarded_frames;
            observed.vad_active=info.vad.active; observed.vad_probability=info.vad.probability;
            observed.gain=info.level.gain; observed.aec_resets=info.aec.resets;
            if (!o.frames) std::fill(out,out+frames,0.0f);
            return info.flags;
        }
#ifdef BF_COMPARE_WEBRTC
        const float *fp[]={far}, *np[]={near};
        float *rp[]={reverse}, *op[]={out};
        if (apm->ProcessReverseStream(fp,stream,stream,rp) || apm->set_stream_delay_ms(0) ||
            apm->ProcessStream(np,stream,stream,op)) throw std::runtime_error("AEC3 processing");
        if (backend=="aec3-linear") {
            if (!apm->GetLinearAecOutput(rtc::ArrayView<std::array<float,160>>(&linear,1)))
                throw std::runtime_error("AEC3 linear output unavailable");
            std::copy(linear.begin(),linear.end(),out);
        }
#endif
        return 0;
    };
    // Paced transport trial warms state before the measured interval; offline
    // quality replay always starts cold and never omits scored samples.
    uint64_t position=0;
    if (seconds) for (unsigned i=0;i<100;++i) { process(position); position+=frames; }
    auto origin=Clock::now();
    auto store_pipeline=[&]() {
        if(!o.frames) return;
        size_t source=static_cast<size_t>(o.timing.frame_position);
        if(source>=mic.size()) return;
        size_t count=std::min<size_t>(o.frames,mic.size()-source);
        std::copy(out,out+count,result.begin()+static_cast<ptrdiff_t>(source+pipeline_delay));
    };
    for (size_t call=0;call<calls;++call) {
        size_t offset=call*frames;
        size_t count=seconds ? frames : std::min<size_t>(frames,mic.size()-offset);
        std::fill(near,near+frames,0.0f); std::fill(far,far+frames,0.0f);
        for (size_t i=0;i<count;++i) { near[i]=mic[(offset+i)%mic.size()]; far[i]=ref[(offset+i)%ref.size()]; }
        auto release=seconds ? origin+std::chrono::milliseconds(call*10) : Clock::now();
        if (seconds) waiter.wait(release);
        auto start=Clock::now();
        if (alloc_begin && alloc_end) alloc_begin();
        uint32_t flags=process(position);
        int64_t allocations=alloc_begin && alloc_end ? static_cast<int64_t>(alloc_end()) : -1;
        auto end=Clock::now();
        auto ns=[&](Clock::time_point t) { return std::chrono::duration_cast<std::chrono::nanoseconds>(t-origin).count(); };
        observed.release=ns(release); observed.start=ns(start); observed.end=ns(end);
        observed.allocations=allocations; observed.flags=flags; receipts[call]=observed;
        for (uint32_t i=0;i<frames;++i) if (!std::isfinite(out[i])) throw std::runtime_error("nonfinite output");
        if (!seconds) {
            if(pipeline) store_pipeline();
            else std::copy(out,out+count,result.begin()+static_cast<ptrdiff_t>(offset));
        }
        position+=frames;
    }
    if(pipeline&&!seconds) {
        // EOS control outside timed process calls. Retain an explicit priming
        // prefix and the final real source samples, trimming terminal padding.
        bool drained=false;
        for(unsigned i=0;i<3;++i) {
            bf_process_info info=BF_INIT(bf_process_info);
            check(bf_pipeline_flush(pipeline,&o,&info));
            if(!o.frames) {drained=true;break;}
            store_pipeline();
        }
        if(!drained) throw std::runtime_error("bounded pipeline drain failed");
    }
    int64_t final_rss=rss_kib(), peak=-1;
#ifdef __linux__
    struct rusage usage{}; if (!getrusage(RUSAGE_SELF,&usage)) peak=usage.ru_maxrss;
#endif
    if (aec) check(bf_aec_deinit(aec));
    if (pipeline) check(bf_pipeline_deinit(pipeline));
    std::ofstream output(argv[5],std::ios::binary), raw(argv[6]), summary(argv[7]);
    output.write(reinterpret_cast<const char *>(result.data()),static_cast<std::streamsize>(result.size()*4));
    raw << "frame,release_ns,start_ns,end_ns,processing_ns,start_lateness_ns,completion_deadline_miss,allocation_calls,flags,priming,discarded_frames,vad_active,vad_probability,gain,aec_resets\n";
    uint32_t misses=0, execution_misses=0;
    for (size_t i=0;i<calls;++i) {
        auto t=receipts[i]; bool missed=seconds && t.end>t.release+10000000;
        misses+=missed; execution_misses+=(t.end-t.start)>10000000;
        raw << i << ',' << t.release << ',' << t.start << ',' << t.end << ',' << t.end-t.start << ','
            << (seconds ? std::max<int64_t>(0,t.start-t.release):0) << ',' << missed << ',' << t.allocations << ',' << t.flags
            << ',' << t.priming << ',' << t.discarded << ',' << t.vad_active << ',' << t.vad_probability
            << ',' << t.gain << ',' << t.aec_resets << '\n';
    }
    summary << "{\"rate\":"<<rate<<",\"frames\":"<<frames<<",\"calls\":"<<calls
            <<",\"wait_policy\":\""<<(highres ? "private-windows-highres" : "standard-sleep")<<"\""
            <<",\"paced_seconds\":"<<seconds<<",\"workspace_bytes\":"<<bytes
            <<",\"baffle_tail_ms\":"<<(backend.rfind("baffle",0)==0 ? tail_ms:0)
            <<",\"rss_before_init_kib\":"<<before<<",\"rss_after_init_kib\":"<<initialized
            <<",\"rss_after_process_kib\":"<<final_rss<<",\"process_peak_rss_kib\":"<<peak
            <<",\"completion_deadline_misses\":"<<misses<<",\"execution_deadline_misses\":"<<execution_misses
            <<",\"added_pipeline_delay_frames\":"<<pipeline_delay<<",\"stage_mask\":"<<(pipeline_backend ? stages:BF_STAGE_AEC)<<"}\n";
    if (!output || !raw || !summary) throw std::runtime_error("output write");
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
