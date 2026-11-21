"""Fixed one-factor external ASR diagnostics. No DSP gain or decoder tuning."""
import argparse,hashlib,json,pathlib,time
import numpy as np
from inference_contention import load_asr,versions
from paired_asr import audio_stats
from asr_metrics import metrics

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for key in ("models","input","results","report"):p.add_argument(key,type=pathlib.Path)
    a=p.parse_args();jobs={j["name"]:j for j in json.loads((a.input/"manifest.json").read_text())["jobs"]}
    models={k:load_asr(k,a.models) for k in ("whisper","parakeet")};rows=[]
    for case,stage in (("source0-doubletalk","baffle"),("source0-post-cancel","raw"),
                       ("source1-near-quiet","aec-ns"),("source1-doubletalk","pipeline")):
        j=jobs[case];path=a.input/j["mic"] if stage=="raw" else a.results/(case+"-"+stage+".f32")
        delay=160 if stage in ("aec-ns","pipeline") else 0
        first,last=j["asr_frames"];x=np.fromfile(path,dtype="<f4")[first+delay:last+delay].copy()
        controls=[("original",x),("whole-window-gain-0.1",x*.1),
                       ("complete-provider-utterance-without-halfsecond-context",x[8000:-8000]),
                       ("additional-halfsecond-zero-context",np.pad(x,(8000,8000))),
                       ("fresh-recognizer-original",x)]
        if np.max(np.abs(x))*10<=1:controls.append(("whole-window-gain-10-unclipped",x*10))
        for name,y in controls:
            for kind,model in models.items():
                if name=="fresh-recognizer-original":model=load_asr(kind,a.models)
                begin=time.perf_counter();stream=model.create_stream();stream.accept_waveform(16000,y)
                model.decode_stream(stream);text=stream.result.text
                rows.append({"case":case,"stage":stage,"intervention":name,"recognizer":kind,
                    "text":text,**metrics(j["reference_transcript"],text),"input":audio_stats(y,16000),
                    "decode_ms":(time.perf_counter()-begin)*1000,"source_pcm_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
                    "window_sha256":hashlib.sha256(y.tobytes()).hexdigest()})
    a.report.write_text(json.dumps({"scope":"Complete source utterance preserved in all controls; one factor changes, no production default selected",
        "versions":versions(),"decoder_configs":{k:str(v.config) for k,v in models.items()},"results":rows},indent=2)+"\n")
