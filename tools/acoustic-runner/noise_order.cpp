/* Installed-public-API offline NS hint / AGC->NS experiment, never core code. */
#include <baffle.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
static void check(bf_result r) {if(r!=BF_OK) throw std::runtime_error("Baffle status "+std::to_string(r));}
static void *arena(std::vector<unsigned char>& storage,size_t bytes) {
    storage.resize(bytes+BF_ALIGNMENT);
    return reinterpret_cast<void *>((reinterpret_cast<uintptr_t>(storage.data())+BF_ALIGNMENT-1u)&~static_cast<uintptr_t>(BF_ALIGNMENT-1u));
}
int main(int argc,char **argv) try {
    if(argc!=6) throw std::runtime_error("mode: protected|agc-ns, rate, input.f32, output.f32, receipt.json");
    std::string mode=argv[1];size_t end=0;unsigned long parsed=std::stoul(argv[2],&end);
    if(argv[2][end] || (parsed!=16000&&parsed!=48000) || (mode!="protected"&&mode!="agc-ns")) throw std::runtime_error("configuration");
    uint32_t rate=static_cast<uint32_t>(parsed),f=rate/100;
    std::ifstream file(argv[3],std::ios::binary|std::ios::ate);auto length=file.tellg();
    if(!file||length<=0||length>256*1024*1024||length%4) throw std::runtime_error("input size");
    std::vector<float> input(static_cast<size_t>(length)/4);file.seekg(0);file.read(reinterpret_cast<char *>(input.data()),length);
    if(!file) throw std::runtime_error("input read");
    for(float x:input) if(!std::isfinite(x)||std::fabs(x)>16) throw std::runtime_error("invalid PCM");
    bf_noise_config config=BF_INIT(bf_noise_config);config.sample_rate=rate;config.channels=1;config.frame_frames=f;config.strength_db=18;
    size_t bytes;check(bf_noise_size(&config,&bytes));std::vector<unsigned char> ns_memory,agc_memory;
    bf_noise *ns=nullptr;check(bf_noise_init(arena(ns_memory,bytes),bytes,&config,&ns));
    bf_agc *agc=nullptr;
    if(mode=="agc-ns") {
        bf_agc_config c=BF_INIT(bf_agc_config);c.sample_rate=rate;c.channels=1;c.max_frames=f;
        c.target_dbfs=-18;c.min_gain_db=-20;c.max_gain_db=20;c.attack_ms=5;c.release_ms=250;c.gate_dbfs=-60;c.limit_dbfs=-1;
        check(bf_agc_init(arena(agc_memory,bf_agc_size()),bf_agc_size(),&c,&agc));
    }
    size_t calls=(input.size()+f-1)/f;std::vector<float> output(input.size()+f,0),block(f),processed(f);
    std::vector<int64_t> times(calls+1);float maximum_gain=1;
    for(size_t call=0;call<=calls;++call) {
        std::fill(block.begin(),block.end(),0.0f);size_t first=call*f;
        if(call<calls) std::copy_n(input.begin()+static_cast<ptrdiff_t>(first),std::min<size_t>(f,input.size()-first),block.begin());
        auto start=std::chrono::steady_clock::now();
        if(agc&&call<calls) {bf_level_info info=BF_INIT(bf_level_info);check(bf_agc_process(agc,block.data(),block.data(),f,-1,&info));maximum_gain=std::max(maximum_gain,info.gain);}
        check(bf_noise_process(ns,block.data(),processed.data(),mode=="protected"||call==calls ? 1.0f:-1.0f));
        times[call]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        if(first<output.size()) std::copy_n(processed.begin(),std::min<size_t>(f,output.size()-first),output.begin()+static_cast<ptrdiff_t>(first));
    }
    std::ofstream out(argv[4],std::ios::binary),receipt(argv[5]);out.write(reinterpret_cast<const char *>(output.data()),static_cast<std::streamsize>(output.size()*4));
    receipt<<"{\"mode\":\""<<mode<<"\",\"rate\":"<<rate<<",\"frames\":"<<input.size()<<",\"algorithmic_delay_frames\":"<<f
        <<",\"workspace_bytes\":"<<bytes+(agc ? bf_agc_size():0)<<",\"max_agc_gain\":"<<maximum_gain<<",\"processing_ns\":[";
    for(size_t i=0;i<times.size();++i) {if(i)receipt<<',';receipt<<times[i];}receipt<<"]}\n";
    if(!out||!receipt)throw std::runtime_error("output write");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
