"""External Gate I hint/order experiments; no tuning of Baffle or recognizers.

The foreground hint is explicitly supplied, not inferred from an ASR result.
Order experiments use identical AEC-only PCM, then public AGC/NS processors.
"""
import argparse,hashlib,json,pathlib,subprocess,time
import numpy as np
import soundfile as sf
from inference_contention import load_asr,versions
from asr_metrics import metrics


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def run(probe,mode,rate,pcm,stem):
    command=[str(probe.resolve()),mode,str(rate),str(pcm),str(stem)+".f32",str(stem)+".json"]
    subprocess.run(command,check=True,capture_output=True)
    receipt=json.loads(pathlib.Path(str(stem)+".json").read_text())
    return np.fromfile(str(stem)+".f32",dtype="<f4"),receipt,command


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for k in ("before","after","models","input","baseline_results","output"):p.add_argument(k,type=pathlib.Path)
    a=p.parse_args()
    if a.output.exists() and any(a.output.iterdir()):raise ValueError("fresh output directory required")
    a.output.mkdir(parents=True,exist_ok=True);rows=[]
    models={k:load_asr(k,a.models) for k in ("whisper","parakeet")}
    manifest=json.loads((a.input/"manifest.json").read_text());jobs=manifest["jobs"]
    def score(job,name,audio,reference,extra):
        for kind,model in models.items():
            start=time.perf_counter();stream=model.create_stream();stream.accept_waveform(16000,audio);model.decode_stream(stream)
            rows.append({"case":job,"variant":name,"recognizer":kind,"text":stream.result.text,
                **metrics(reference,stream.result.text),"decode_ms":(time.perf_counter()-start)*1000,**extra})
    for source in (0,1):
        x,rate=sf.read(a.models/f"whisper/test_wavs/{source}.wav",dtype="float32")
        if rate!=16000:raise ValueError("rate")
        reference=next(j["reference_transcript"] for j in jobs if j["source_id"]==source and j["near_peak"])
        for peak in (.025,.2,.65):
            name=f"protected-source{source}-{peak}";xlevel=x*(peak/np.max(np.abs(x)))
            pcm=a.output/(name+".f32");xlevel.astype("<f4").tofile(pcm)
            score(name,"raw",xlevel,reference,{"pcm_sha256":sha(pcm),"hint_scope":"Caller explicitly protects the complete foreground; no learned noise"})
            for label,probe in (("before",a.before),("after",a.after)):
                y,r,command=run(probe,"protected",16000,pcm,a.output/(name+"-"+label))
                aligned=y[r["algorithmic_delay_frames"]:]
                if len(aligned)!=len(xlevel):raise ValueError("EOS")
                error=float(np.sum((aligned.astype(float)-xlevel)**2)/np.sum(xlevel.astype(float)**2))
                score(name,label,aligned,reference,{"relative_error_energy":error,"near_projection":float(np.dot(aligned.astype(float),xlevel)/np.dot(xlevel.astype(float),xlevel)),
                    "output_sha256":sha(pathlib.Path(str(a.output/(name+"-"+label))+".f32")),"native":r,"command":command})
    # All scenarios except capture-epoch are used: the plain PCM order probe
    # deliberately has no capture-timing reset adapter. Do not mislabel that case.
    for job in jobs:
        if job["scenario"]=="capture-epoch":continue
        pcm=a.baseline_results/(job["name"]+"-baffle.f32")
        y,r,command=run(a.after,"agc-ns",16000,pcm,a.output/(job["name"]+"-aec-agc-ns"))
        first,last=job["asr_frames"];d=r["algorithmic_delay_frames"]
        score(job["name"],"aec-agc-ns",y[first+d:last+d].copy(),job["reference_transcript"],{"native":r,"command":command,"aec_input_sha256":sha(pcm)})
    (a.output/"results.json").write_text(json.dumps({"versions":versions(),"protocol":"Fixed hint semantics and order comparisons, no model-specific gain or default DSP change",
        "before_probe_sha256":sha(a.before),"after_probe_sha256":sha(a.after),"results":rows},indent=2)+"\n")
