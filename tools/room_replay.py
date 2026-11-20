"""External, optional NumPy/SciPy real-recording diagnostics and backend replay.

Training: first six seconds only. Evaluation: unchanged seconds 6..22. A fitted
waveform-lag slope is NOT a hardware clock measurement. No room/noise/nonlinear
cause is inferred from weak correlation alone. No source audio is redistributed.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess

import numpy as np
from scipy import signal
import recorded_aec as recordings


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def read(p):
    rate,x=recordings.read(p)
    return rate,np.asarray(x,dtype=np.float64)/32768


def lag(mic,ref,rate,start):
    a=int(start*rate); n=rate; last=rate//2
    m=mic[a:a+n+last]; r=ref[a:a+n]
    if len(m)!=n+last or len(r)!=n:
        raise ValueError("short analysis interval")
    m=m-np.mean(m); r=r-np.mean(r)
    c=signal.correlate(m,r,mode="valid",method="fft")
    energy=np.convolve(m*m,np.ones(1),mode="valid")
    sums=np.r_[0,np.cumsum(energy)]
    scores=np.abs(c)/np.sqrt(np.maximum((sums[n:]-sums[:-n])*np.sum(r*r),1e-30))
    index=int(np.argmax(scores))
    return {"start_s":start,"lag_samples":index,"lag_ms":index/rate*1000,
            "absolute_correlation":float(scores[index])}


def prepare(cache,directory):
    directory.mkdir(parents=True,exist_ok=True)
    jobs=[]; diagnostics=[]
    def save(name,rate,mic,far,scope,oracle=None,metadata=None):
        mi=directory/(name+"-mic.f32"); re=directory/(name+"-ref.f32")
        np.asarray(mic,dtype="<f4").tofile(mi); np.asarray(far,dtype="<f4").tofile(re)
        job={"name":name,"rate":rate,"mic":mi.name,"reference":re.name,
             "frames":len(mic),"scope":scope,"mic_sha256":digest(mi),"reference_sha256":digest(re),
             "far_only_interval_s":[6,22] if scope=="recorded-far-only" else None,
             "metadata":metadata or {}}
        if oracle is not None:
            path=directory/(name+"-oracle.f64"); np.asarray(oracle,dtype="<f8").tofile(path)
            job["oracle"]=path.name; job["oracle_sha256"]=digest(path)
            job["near_interval_s"]=[8,18]
        jobs.append(job)
    for scene,name in recordings.CASES:
        mp,mo=recordings.fetch(f"{scene}/{name}_mic.wav",cache,False)
        rp,ro=recordings.fetch(f"{scene}/{name}_lpb.wav",cache,False)
        rate,m=read(mp); rr,r=read(rp)
        if rr!=rate: raise ValueError("source rate mismatch")
        count=min(len(m),len(r)); m=m[:count]; r=r[:count]
        # 16k analysis only; outputs at both rates below use the same fit in seconds.
        m16=signal.resample_poly(m,16000,rate); r16=signal.resample_poly(r,16000,rate)
        fit=None
        if scene=="farend-singletalk":
            windows=[lag(m16,r16,16000,i) for i in range(6)]
            t=np.arange(6)+.5; y=np.array([w["lag_samples"]/16000 for w in windows])
            slope=float(np.median([(y[j]-y[i])/(t[j]-t[i]) for i in range(6) for j in range(i+1,6)]))
            intercept=float(np.median(y-slope*t)); fixed=float(np.median(y))
            freq,coh=signal.coherence(r16[:96000],m16[:96000],fs=16000,nperseg=1024)
            _,p=signal.welch(r16[:96000],fs=16000,nperseg=1024)
            band=(freq>=200)&(freq<=3400)
            fit={"training_windows":windows,"fixed_lag_s":fixed,"affine_intercept_s":intercept,
                 "affine_slope_s_per_s":slope,"waveform_lag_slope_equivalent_ppm":slope*1e6,
                 "weighted_training_coherence_200_3400":float(np.sum(coh[band]*p[band])/max(np.sum(p[band]),1e-30)),
                 "heldout_diagnostics_only":[lag(m16,r16,16000,i) for i in (8,16)],
                 "source_microphone":mo,"source_reference":ro,
                 "limitations":"Lag variation can be drift, movement, noise or nonlinear effects; no clock/calibration attribution"}
            diagnostics.append({"case":name,**fit})
        for output_rate in (16000,48000):
            mic=signal.resample_poly(m,output_rate,rate) if output_rate!=rate else m
            far=signal.resample_poly(r,output_rate,rate) if output_rate!=rate else r
            variants=[("original",far)]
            if fit:
                pos=np.arange(len(far),dtype=np.float64); seconds=pos/output_rate
                variants += [("fixed-offset",np.interp(pos-fixed*output_rate,pos,far,left=0,right=0)),
                             ("training-affine",np.interp(pos-(intercept+slope*seconds)*output_rate,pos,far,left=0,right=0))]
            for variant,reference in variants:
                save(f"{name}-{output_rate}-{variant}",output_rate,mic,reference,
                     "recorded-far-only" if fit else "recorded-doubletalk-no-clean-oracle",
                     metadata={"training_fit":fit,"variant":variant,"source_microphone":mo,"source_reference":ro})
    # Original controlled input and oracle remain separate from the real rooms.
    rate,m=read(cache/"controlled-doubletalk-mic.wav")
    _,r=read(cache/"controlled-doubletalk-ref.wav")
    _,near=read(cache/"03d6ced2-3cc1-4b32-bcea-770fe2d260ff_nearend-singletalk_mic.wav")
    scale=.2/np.max(np.abs(near)); oracle=np.zeros(len(m),dtype=np.float64)
    indices=np.arange(10*rate)+5*rate
    oracle[8*rate:18*rate]=near[indices%len(near)]*scale
    for output_rate in (16000,48000):
        convert=lambda x: signal.resample_poly(x,output_rate,rate) if output_rate!=rate else x
        save(f"controlled-{output_rate}",output_rate,convert(m),convert(r),"simulated-echo-recorded-components",convert(oracle))
    # Independently known peak delay, not chosen by optimizing a speech score.
    for output_rate in (16000,48000):
        x=np.zeros(2*output_rate); x[output_rate]=.1
        save(f"near-impulse-{output_rate}",output_rate,x,np.zeros(len(x)),"synthetic-latency-probe")
    manifest={"source_pin":recordings.PIN,"training_interval_s":[0,6],"evaluation_interval_s":[6,22],
              "numpy":np.__version__,"scipy":__import__('scipy').__version__,
              "no_physical_measurement":True,"jobs":jobs,"diagnostics":diagnostics}
    (directory/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    return manifest


def replay(runner,directory,output,backends):
    output.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((directory/"manifest.json").read_text()); results=[]
    for job in manifest["jobs"]:
        for key in ("mic","reference"):
            if digest(directory/job[key]) != job[key+"_sha256"]: raise ValueError("input hash mismatch")
        mic=np.fromfile(directory/job["mic"],dtype="<f4"); rate=job["rate"]
        for backend in backends:
            stem=output/(job["name"]+"-"+backend)
            command=[str(runner.resolve()),backend,str(rate),str(directory/job["mic"]),str(directory/job["reference"]),
                     str(stem)+".f32",str(stem)+".csv",str(stem)+".json"]
            run=subprocess.run(command,capture_output=True,text=True,check=True)
            out=np.fromfile(str(stem)+".f32",dtype="<f4").astype(np.float64)
            if len(out)!=len(mic): raise ValueError("backend frame accounting")
            metrics={}
            if job["far_only_interval_s"]:
                a,b=[int(t*rate) for t in job["far_only_interval_s"]]
                metrics["far_only_mic_to_output_ratio_db"]=float(10*np.log10(np.sum(mic[a:b].astype(np.float64)**2)/max(np.sum(out[a:b]**2),1e-30)))
                metrics["total_output_power"]=float(np.mean(out[a:b]**2))
            if "oracle" in job:
                oracle=np.fromfile(directory/job["oracle"],dtype="<f8"); a,b=[int(t*rate) for t in job["near_interval_s"]]
                target=oracle[a:b]; error=out[a:b]-target; before=mic[a:b].astype(np.float64)-target
                metrics.update(raw_component_snr_improvement_db=float(10*np.log10(np.sum(before**2)/max(np.sum(error**2),1e-30))),
                               near_projection=float(np.dot(out[a:b],target)/np.dot(target,target)))
            if job["scope"]=="synthetic-latency-probe":
                metrics["observed_near_peak_offset_frames"]=int(np.argmax(np.abs(out)))-rate
                metrics["near_peak"]=float(np.max(np.abs(out)))
            receipt=np.genfromtxt(str(stem)+".csv",delimiter=",",names=True)
            costs=receipt["processing_ns"]/1e6
            metrics["execution_ms"]={"p50":float(np.percentile(costs,50)),"p95":float(np.percentile(costs,95)),
                                      "p99":float(np.percentile(costs,99)),"max":float(np.max(costs))}
            known=receipt["allocation_calls"]>=0
            metrics["observed_allocation_calls"]=int(np.sum(receipt["allocation_calls"][known])) if np.any(known) else None
            results.append({"job":job["name"],"scope":job["scope"],"backend":backend,"metrics":metrics,
                            "command":command,"stderr":run.stderr,"native_summary":json.loads(pathlib.Path(str(stem)+".json").read_text()),
                            "output_sha256":digest(pathlib.Path(str(stem)+".f32")),"raw_receipts_sha256":digest(pathlib.Path(str(stem)+".csv"))})
    report={"runner_sha256":digest(runner),"manifest_sha256":digest(directory/"manifest.json"),
            "metric_note":"Raw common-frame indexing; no output delay shifting or perceptual/phoneme inference",
            "results":results}
    (output/"report.json").write_text(json.dumps(report,indent=2)+"\n")


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__); commands=p.add_subparsers(dest="action",required=True)
    a=commands.add_parser("prepare"); a.add_argument("cache",type=pathlib.Path); a.add_argument("directory",type=pathlib.Path)
    a=commands.add_parser("replay"); a.add_argument("runner",type=pathlib.Path); a.add_argument("directory",type=pathlib.Path); a.add_argument("output",type=pathlib.Path)
    a.add_argument("--backends",nargs="+",default=["baffle","aec3"])
    args=p.parse_args()
    if args.action=="prepare": prepare(args.cache,args.directory)
    else: replay(args.runner,args.directory,args.output,args.backends)
