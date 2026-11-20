/* Standalone raw acquisition through installed public Hail only. It never
 * changes endpoint levels/defaults. No PCM/file writes in device callbacks.
 * Data and metadata are bounded/preallocated, written after callback retirement. */
#include <hail.h>
#include <hail/virtual.h>
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
using Clock=std::chrono::steady_clock;
static void require(bool value,const char *what) { if(!value) throw std::runtime_error(what); }
static uint32_t integer(const char *text,uint32_t maximum) {
    size_t end=0; auto v=std::stoull(text,&end);
    require(text[0]>='0'&&text[0]<='9'&&!text[end]&&v<=maximum,"invalid integer argument");
    return static_cast<uint32_t>(v);
}
static std::string quoted(const std::string &s) {
    std::string out="\""; const char *hex="0123456789abcdef";
    for(unsigned char c:s) { if(c=='"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];} else out+=static_cast<char>(c); }
    return out+'"';
}
static void poll(ha_context *c) { uint32_t n; require(ha_context_poll(c,nullptr,0,&n)==HA_OK,"control poll"); }
static ha_stream_status status(ha_stream *s) { ha_stream_status v=HA_INIT(ha_stream_status); require(ha_stream_get_status(s,&v)==HA_OK,"status"); return v; }
struct Block { ha_block_info info; uint64_t file_frame; uint32_t direction; };
struct Event { int64_t elapsed_ns; uint32_t state; uint64_t generation; int32_t result; };
struct HailOwner {
    ha_context *context=nullptr; ha_stream *stream=nullptr;
    ~HailOwner() {
        if(stream) {
            (void)ha_stream_close_begin(stream);
            for(unsigned i=0;i<5000&&stream;++i) {
                uint32_t n=0; (void)ha_context_poll(context,nullptr,0,&n);
                auto result=ha_stream_close_finish(&stream);
                if(result!=HA_PENDING&&result!=HA_OK) break;
                if(stream) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        if(context&&!stream) for(unsigned i=0;i<5000&&context;++i) {
            auto result=ha_context_destroy(&context);
            if(result!=HA_PENDING&&result!=HA_OK) break;
            if(context) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};
int main(int argc,char **argv) try {
    if(argc!=3 && (argc<7||argc>9)) throw std::runtime_error(
        "usage: acquire backend --list | backend capture-key playback-key stereo48k.f32 seconds prefix [cancel_ms [stop|restart]]");
    uint32_t backend=integer(argv[1],HA_BACKEND_ALSA);
    ha_context_config cc=HA_INIT(ha_context_config); cc.backend=backend;
    HailOwner owner; ha_context *&context=owner.context;
    require(ha_context_create(&cc,&context)==HA_OK,"context create");
    if(argc==3) {
        require(std::string(argv[2])=="--list","expected --list");
        ha_device_info devices[HA_MAX_DEVICES]; for(auto &d:devices) d=HA_INIT(ha_device_info);
        uint32_t n=0; require(ha_context_enumerate(context,devices,HA_MAX_DEVICES,&n)==HA_OK,"enumeration");
        for(uint32_t i=0;i<n;++i) std::cout<<"{\"direction\":"<<devices[i].direction<<",\"name\":"<<quoted(devices[i].name)
            <<",\"key\":"<<quoted(devices[i].persistent_key)<<"}\n";
        require(ha_context_destroy(&context)==HA_OK,"context destroy"); return 0;
    }
    uint32_t seconds=integer(argv[5],120); require(seconds>=1,"duration 1..120 seconds");
    int64_t cancel_ms=argc>=8 ? (std::string(argv[7])=="-1" ? -1:static_cast<int64_t>(integer(argv[7],120000))):-1;
    std::string cancel_behavior=argc==9 ? argv[8]:"restart";
    require(cancel_behavior=="restart"||cancel_behavior=="stop","cancel behavior");
    require(cancel_ms<static_cast<int64_t>(seconds)*1000,"cancel must be within run");
    std::string prefix=argv[6];
    std::ifstream input(argv[4],std::ios::binary|std::ios::ate); auto bytes=input.tellg();
    require(input&&bytes>0&&bytes<=256*1024*1024&&bytes%8==0,"stereo F32 input size");
    std::vector<float> stimulus(static_cast<size_t>(bytes)/4); input.seekg(0);
    input.read(reinterpret_cast<char *>(stimulus.data()),bytes); require(static_cast<bool>(input),"read stimulus");
    for(float x:stimulus) require(std::isfinite(x)&&std::fabs(x)<=1,"stimulus must be finite within full scale");
    ha_stream_config sc=HA_INIT(ha_stream_config); sc.directions=HA_CAPTURE|HA_PLAYBACK;
    sc.capture_format=HA_FORMAT_INIT(48000,1,HA_PCM_F32); sc.playback_format=HA_FORMAT_INIT(48000,2,HA_PCM_F32);
    sc.capture_device=HA_INIT(ha_device_selector); sc.playback_device=HA_INIT(ha_device_selector);
    auto select=[&](ha_device_selector &d,const char *key) { if(backend==HA_BACKEND_VIRTUAL&&std::string(key)=="-") return;
        size_t n=std::strlen(key); require(n&&n<HA_DEVICE_KEY_CAPACITY,"exact endpoint key required");
        d.kind=HA_SELECT_PERSISTENT_KEY; d.key_bytes=static_cast<uint32_t>(n); std::memcpy(d.key,key,n+1); };
    select(sc.capture_device,argv[2]); select(sc.playback_device,argv[3]);
    sc.quantum_frames=480; sc.capture_slots=sc.playback_slots=sc.reference_slots=32;
    ha_stream *&stream=owner.stream; require(ha_stream_open(context,&sc,&stream)==HA_OK,"stream open");
    ha_stream_info stream_info=HA_INIT(ha_stream_info);
    require(ha_stream_get_info(stream,&stream_info)==HA_OK,"stream identity/format query");
    ha_context_status context_info=HA_INIT(ha_context_status);
    require(ha_context_get_status(context,&context_info)==HA_OK,"backend query");
    size_t capacity=static_cast<size_t>(seconds+2)*48000, record_capacity=static_cast<size_t>(seconds+2)*2000;
    std::vector<float> microphone(capacity), reference(capacity*2);
    std::vector<Block> blocks(record_capacity*2); size_t mc=0,rc=0,bc=0;
    std::vector<Event> events(4096); size_t ec=0;
    float playback[480*2], virtual_capture[480]={0}, played[480*2];
    uint64_t submitted=0, generation=1; size_t offset=480; bool canceled=false, pending=false;
    require(ha_stream_start(stream)==HA_PENDING,"start");
    auto startup=Clock::now(); while(status(stream).state!=HA_RUNNING) { poll(context);
        require(Clock::now()-startup<std::chrono::seconds(5),"start timeout"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    auto origin=Clock::now(); uint64_t tick=0; uint32_t last_flush=0;
    auto elapsed=[&]() {return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-origin).count();};
    auto record_event=[&](uint32_t state,ha_result result) { require(ec<events.size(),"event bound"); events[ec++]={elapsed(),state,generation,result}; };
    record_event(1,HA_OK);
    auto drain=[&]() {
        auto state=status(stream).state;
        if(state!=HA_RUNNING&&state!=HA_STOPPING) return;
        for(uint32_t leg=0;leg<2;++leg) for(unsigned i=0;i<64;++i) {
            auto &count=leg ? rc:mc; uint32_t channels=leg ? 2:1;
            require(count+480<=capacity,"recording sample bound");
            uint32_t n=0; ha_block_info info=HA_INIT(ha_block_info);
            float *dst=leg ? reference.data()+count*2:microphone.data()+count;
            ha_result r=leg ? ha_stream_read_reference(stream,dst,480,&n,&info):ha_stream_read(stream,dst,480,&n,&info);
            if(r==HA_WOULD_BLOCK||r==HA_BUSY) break;
            require(r==HA_OK,"recording read"); if(!n) break;
            require(info.frames==n,"metadata/PCM span mismatch");
            require(bc<blocks.size(),"metadata record bound"); blocks[bc++]={info,count,leg}; count+=n;
            (void)channels;
        }
    };
    while(Clock::now()-origin<std::chrono::seconds(seconds)) {
        poll(context); auto st=status(stream); require(st.state==HA_RUNNING,"device/stream failed");
        if(cancel_ms>=0&&!canceled&&elapsed()>=cancel_ms*1000000) {
            record_event(2,HA_PENDING);
            require(ha_stream_flush_playback(stream,HA_FLUSH_NATIVE_REQUIRED,&generation)==HA_PENDING,"flush request");
            record_event(3,HA_PENDING); canceled=pending=true; offset=480;
        }
        if(pending&&st.flush_completed_generation==generation&&st.flush_result!=HA_PENDING) {
            record_event(4,st.flush_result); pending=false; last_flush=static_cast<uint32_t>(st.flush_result);
            require(st.flush_result==HA_OK,"native flush unsupported/failed");
        }
        if(!pending&&(!canceled||cancel_behavior=="restart")) for(unsigned writes=0;writes<4;++writes) {
            if(offset==480) { for(size_t i=0;i<960;++i) playback[i]=stimulus[(submitted*2+i)%stimulus.size()]; offset=0; }
            uint32_t n=0; auto r=ha_stream_write(stream,playback+offset*2,static_cast<uint32_t>(480-offset),generation,&n);
            if(r==HA_BUSY||r==HA_WOULD_BLOCK) break;
            require(r==HA_OK,"playback submit"); submitted+=n; offset+=n; if(offset!=480) break;
        }
        if(backend==HA_BACKEND_VIRTUAL) {
            ha_virtual_step_info step=HA_INIT(ha_virtual_step_info); step.capture_data=virtual_capture;
            step.capture_frames=step.playback_frames=480; step.advance_ns=10000000;
            ha_block_info receipt=HA_INIT(ha_block_info);
            require(ha_virtual_step(stream,&step,played,480,&receipt)==HA_OK,"virtual self-test step");
        }
        drain();
        if(backend==HA_BACKEND_VIRTUAL) std::this_thread::sleep_until(origin+std::chrono::milliseconds(++tick*10));
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    drain(); auto final=status(stream); require(ha_stream_stop(stream)==HA_PENDING,"stop");
    auto stopped=Clock::now(); while(status(stream).state!=HA_STOPPED) { poll(context); drain();
        require(Clock::now()-stopped<std::chrono::seconds(5),"stop timeout"); }
    drain(); record_event(5,HA_OK);
    require(ha_stream_close_begin(stream)==HA_PENDING,"close begin");
    ha_result close=HA_PENDING; for(unsigned i=0;i<5000&&close==HA_PENDING;++i) {poll(context); close=ha_stream_close_finish(&stream);}
    require(close==HA_OK,"close retirement timeout"); require(ha_context_destroy(&context)==HA_OK,"context retirement");
    std::ofstream mic(prefix+".capture.f32",std::ios::binary), ref(prefix+".reference.f32",std::ios::binary), metadata(prefix+".jsonl");
    mic.write(reinterpret_cast<const char *>(microphone.data()),static_cast<std::streamsize>(mc*4));
    ref.write(reinterpret_cast<const char *>(reference.data()),static_cast<std::streamsize>(rc*8));
    metadata<<"{\"type\":\"session\",\"backend\":"<<context_info.backend<<",\"stream_id\":"<<stream_info.stream_id
        <<",\"clock_relationship\":"<<stream_info.clock_relationship<<",\"conversion\":"<<stream_info.conversion
        <<",\"virtual\":"<<(backend==HA_BACKEND_VIRTUAL ? "true":"false")
        <<",\"capture_rate\":48000,\"capture_channels\":1,\"render_rate\":48000,\"render_channels\":2,\"capture_frames\":"<<mc
        <<",\"reference_frames\":"<<rc<<",\"capture_lost_frames\":"<<final.capture_lost_frames<<",\"playback_underrun_frames\":"<<final.playback_underrun_frames
        <<",\"reference_lost_frames\":"<<final.reference_lost_frames<<",\"flush_result\":"<<last_flush
        <<",\"cancel_behavior\":"<<quoted(cancel_behavior)<<",\"acoustic_calibration\":null}\n";
    for(size_t i=0;i<bc;++i) {auto b=blocks[i];auto t=b.info;
        metadata<<"{\"type\":\"block\",\"leg\":"<<b.direction<<",\"file_frame\":"<<b.file_frame<<",\"first_frame\":"<<t.first_frame
            <<",\"frames\":"<<t.frames<<",\"clock_domain\":"<<t.clock_domain<<",\"epoch\":"<<t.clock_epoch<<",\"generation\":"<<t.generation
            <<",\"host_time_ns\":"<<t.host_time_ns<<",\"device_position\":"<<t.device_position<<",\"uncertainty_ns\":"<<t.uncertainty_ns
            <<",\"flags\":"<<t.flags<<",\"lost_frames\":"<<t.lost_frames<<",\"frame_time_numerator\":"<<t.frame_time_numerator
            <<",\"frame_time_denominator\":"<<t.frame_time_denominator<<",\"time_remainder\":"<<t.time_remainder<<"}\n"; }
    for(size_t i=0;i<ec;++i) {auto e=events[i];metadata<<"{\"type\":\"event\",\"state\":"<<e.state<<",\"elapsed_ns\":"<<e.elapsed_ns
        <<",\"generation\":"<<e.generation<<",\"result\":"<<e.result<<"}\n";}
    require(mic&&ref&&metadata,"recording file write");
    std::cout<<"Captured "<<mc<<" frames; reference "<<rc<<" frames. No acoustic silence or timing calibration inferred.\n";
    return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<'\n'; return 1;}
