/* Gate J external experiment. Original code, MIT; not part of Baffle's API.
 * One owner, bounded 10 ms blocks, all vectors planned before processing. */
#include <complex>
#include <memory>
class ResidualExperiment {
    using Z=std::complex<float>;
    uint32_t frames_,n_=1,seen_=0;
    bool enabled_;
    uint64_t resets_=0;
    bf_aec *aec_=nullptr;
    size_t aec_bytes_=0;
    std::vector<unsigned char> arena_;
    std::vector<Z> x_,y_,cross_,twiddle_;
    std::vector<float> px_,py_,gain_,residual_;
    void fft(std::vector<Z>& x,bool inverse) {
        for(uint32_t i=1,j=0;i<n_;++i) {
            uint32_t bit=n_>>1; for(;j&bit;bit>>=1) j^=bit; j^=bit;
            if(i<j) std::swap(x[i],x[j]);
        }
        for(uint32_t len=2;len<=n_;len*=2) for(uint32_t first=0;first<n_;first+=len)
            for(uint32_t k=0;k<len/2;++k) {
                Z w=twiddle_[k*(n_/len)];if(inverse)w=std::conj(w);
                Z u=x[first+k],v=x[first+k+len/2]*w;
                x[first+k]=u+v;x[first+k+len/2]=u-v;
            }
        if(inverse) for(auto& v:x)v/=static_cast<float>(n_);
    }
    void reset() {
        seen_=0;std::fill(cross_.begin(),cross_.end(),Z{});
        std::fill(px_.begin(),px_.end(),0);std::fill(py_.begin(),py_.end(),0);
        std::fill(gain_.begin(),gain_.end(),1);
    }
public:
    ResidualExperiment(uint32_t rate,bool enabled):frames_(rate/100),enabled_(enabled) {
        while(n_<frames_)n_*=2;
        x_.resize(n_);y_.resize(n_);cross_.resize(n_);twiddle_.resize(n_/2);
        px_.resize(n_);py_.resize(n_);gain_.resize(n_,1);residual_.resize(frames_);
        constexpr double pi=3.14159265358979323846;
        for(uint32_t k=0;k<n_/2;++k)twiddle_[k]=Z(static_cast<float>(std::cos(-2*pi*k/n_)),static_cast<float>(std::sin(-2*pi*k/n_)));
        bf_aec_config c=BF_INIT(bf_aec_config);c.sample_rate=rate;c.frame_frames=frames_;
        c.capture_channels=c.reference_channels=1;c.tail_ms=200;c.backend=BF_AEC_SPEEXDSP;
        check(bf_aec_size(&c,&aec_bytes_));arena_.resize(aec_bytes_+BF_ALIGNMENT);
        void *p=reinterpret_cast<void *>((reinterpret_cast<uintptr_t>(arena_.data())+BF_ALIGNMENT-1u)&~static_cast<uintptr_t>(BF_ALIGNMENT-1u));
        check(bf_aec_init(p,aec_bytes_,&c,&aec_));
    }
    ~ResidualExperiment(){if(aec_)(void)bf_aec_deinit(aec_);}
    size_t bytes() const {return sizeof(*this)+aec_bytes_+BF_ALIGNMENT+(3*n_+n_/2)*sizeof(Z)+3*n_*sizeof(float)+frames_*sizeof(float);}
    bf_aec_info process(float *mic,const bf_audio_timing& nt,const float *ref,const bf_audio_timing *rt) {
        bf_aec_info info=BF_INIT(bf_aec_info);
        check(bf_aec_process(aec_,mic,&nt,ref,rt,residual_.data(),&info));
        if(info.resets!=resets_ || (info.flags&BF_REFERENCE_GENERATION_CHANGED) || !ref)reset();
        resets_=info.resets;
        double reference_energy=0;
        if(ref)for(uint32_t i=0;i<frames_;++i)reference_energy+=static_cast<double>(ref[i])*ref[i];
        if(!enabled_||reference_energy<=1e-20) {reset();std::copy(residual_.begin(),residual_.end(),mic);return info;}
        std::fill(x_.begin(),x_.end(),Z{});std::fill(y_.begin(),y_.end(),Z{});
        double echo=0;
        for(uint32_t i=0;i<frames_;++i) {
            float e=mic[i]-residual_[i];x_[i]=e;y_[i]=residual_[i];echo+=static_cast<double>(e)*e;
        }
        if(echo<=1e-20) {reset();std::copy(residual_.begin(),residual_.end(),mic);return info;}
        fft(x_,false);fft(y_,false);
        constexpr float alpha=.904837418f; /* exp(-10/100 ms) */
        if(seen_<20)++seen_;
        for(uint32_t k=0;k<n_;++k) {
            px_[k]=alpha*px_[k]+(1-alpha)*std::norm(x_[k]);
            py_[k]=alpha*py_[k]+(1-alpha)*std::norm(y_[k]);
            cross_[k]=alpha*cross_[k]+(1-alpha)*std::conj(x_[k])*y_[k];
            double den=static_cast<double>(px_[k])*py_[k];
            float coherence=den>1e-30 ? static_cast<float>(std::min(1.0,static_cast<double>(std::norm(cross_[k]))/den)):0;
            float target=seen_>=20 ? std::sqrt(std::max(0.0f,1-coherence)):1;
            float smooth=target<gain_[k] ? .135335283f:.818730753f; /* 5/50 ms */
            gain_[k]=smooth*gain_[k]+(1-smooth)*target;
            y_[k]*=gain_[k];
        }
        fft(y_,true);
        for(uint32_t i=0;i<frames_;++i)mic[i]=y_[i].real();
        return info;
    }
};
