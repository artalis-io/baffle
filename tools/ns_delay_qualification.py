"""One-hop NS latency/quality experiment. No pipeline/default change or tuning."""
import argparse,hashlib,json,pathlib,subprocess,time
import numpy as np
from inference_contention import load_asr
from asr_metrics import metrics


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for k in ("runner","models","paired_input","output"):p.add_argument(k,type=pathlib.Path)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);rows=[]
    def run(name,x,rate,backend):
        stem=a.output/(name+"-"+backend);mic=pathlib.Path(str(stem)+"-mic.f32");ref=pathlib.Path(str(stem)+"-ref.f32")
        x.astype("<f4").tofile(mic);np.zeros(len(x),dtype="<f4").tofile(ref)
        command=[str(a.runner.resolve()),backend,str(rate),str(mic),str(ref),str(stem)+".f32",str(stem)+".csv",str(stem)+".json"]
        subprocess.run(command,check=True,capture_output=True)
        info=json.loads(pathlib.Path(str(stem)+".json").read_text());d=info["added_pipeline_delay_frames"]
        y=np.fromfile(str(stem)+".f32",dtype="<f4");assert len(y)==len(x)+d
        return y,d,info,command
    for rate in (16000,48000):
        x=np.zeros(rate*2+17,dtype=np.float32);x[rate//2]=.2
        for backend in ("ns-zero","ns-only","ns5"):
            y,d,info,command=run("impulse-"+str(rate),x,rate,backend)
            peak=int(np.argmax(np.abs(y)))-rate//2
            assert peak==d and d==rate//(200 if backend=="ns5" else 100)
            error=float(np.sum((y[d:]-x)**2)/np.sum(x*x))
            if backend=="ns-zero":assert error<1e-10
            rows.append({"case":"impulse","rate":rate,"backend":backend,"reported_delay_frames":d,
                "observed_peak_delay_frames":peak,"relative_error":error,"native":info,"command":command})
    manifest=json.loads((a.paired_input/"manifest.json").read_text())
    asr={k:load_asr(k,a.models) for k in ("whisper","parakeet")}
    for job in manifest["jobs"]:
        if job["scenario"]!="near-normal":continue
        x=np.fromfile(a.paired_input/job["mic"],dtype="<f4")
        rng=np.random.default_rng(20261120);noisy=x+rng.normal(0,.008,len(x)).astype(np.float32)
        first,last=job["asr_frames"]
        for backend in ("raw","ns-only","ns5"):
            if backend=="raw":y=noisy;d=0;info=None;command=None
            else:y,d,info,command=run(job["name"],noisy,16000,backend)
            window=y[first+d:last+d].copy();target=x[first:last]
            row={"case":job["name"],"backend":backend,"native":info,"command":command,
                "noise_seed":20261120,"noise_stddev":.008,"pcm_sha256":hashlib.sha256(y.tobytes()).hexdigest(),
                "near_error_snr_db":float(10*np.log10(np.sum(target.astype(float)**2)/np.sum((window-target).astype(float)**2))),"asr":{}}
            for kind,model in asr.items():
                start=time.perf_counter();stream=model.create_stream();stream.accept_waveform(16000,window);model.decode_stream(stream)
                row["asr"][kind]={"text":stream.result.text,**metrics(job["reference_transcript"],stream.result.text),"decode_ms":(time.perf_counter()-start)*1000}
            rows.append(row)
    (a.output/"report.json").write_text(json.dumps({"scope":"NS-only offline hop-size experiment; speech+synthetic noise, not an AEC pipeline change",
        "runner_sha256":hashlib.sha256(a.runner.read_bytes()).hexdigest(),"results":rows},indent=2)+"\n")
