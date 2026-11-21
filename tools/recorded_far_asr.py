"""False near-speech transcripts on fixed recorded far-only intervals.

Recognition words are not committed words. No speaker-attribution or transcript
rejection policy is implemented. Original acoustic and fixed interval unchanged.
"""
import argparse,csv,hashlib,json,pathlib,time
import numpy as np
from scipy.signal import resample_poly
from inference_contention import load_asr,versions
from asr_metrics import words

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for k in ("models","rooms","results","output"):p.add_argument(k,type=pathlib.Path)
    p.add_argument('--stages',nargs='+',choices=('raw','baffle','aec-ns','pipeline'),default=('raw','baffle','aec-ns','pipeline'))
    a=p.parse_args();jobs=json.loads((a.rooms/"manifest.json").read_text())["jobs"]
    model={k:load_asr(k,a.models) for k in ("whisper","parakeet")};rows=[]
    native=json.loads((a.results/"report.json").read_text())
    for j in jobs:
        if j["scope"]!="recorded-far-only" or not j["name"].endswith("-original"):continue
        for stage in a.stages:
            if stage=="raw":path=a.rooms/j["mic"];delay=0
            else:
                path=a.results/(j["name"]+"-"+stage+".f32")
                receipt=next(r for r in native["results"] if r["job"]==j["name"] and r["backend"]==stage)
                delay=receipt["native_summary"]["added_pipeline_delay_frames"]
            rate=j["rate"];first,last=6*rate,22*rate
            x=np.fromfile(path,dtype="<f4")[first+delay:last+delay].copy()
            if len(x)!=last-first:raise ValueError("incomplete fixed interval")
            y=resample_poly(x,16000,rate).astype(np.float32) if rate!=16000 else x
            for kind,recognizer in model.items():
                begin=time.perf_counter();stream=recognizer.create_stream();stream.accept_waveform(16000,y);recognizer.decode_stream(stream)
                text=stream.result.text;tokens=words(text)
                rows.append({"job":j["name"],"stage":stage,"recognizer":kind,"text":text,"false_transcript":bool(tokens),
                    "recognized_false_words":len(tokens),"recognized_false_words_per_minute":len(tokens)*60/16,
                    "committed_word_rate":None,"decode_ms":(time.perf_counter()-begin)*1000,"source_rate":rate,
                    "decoder_rate":16000,"pcm_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
                    "window_sha256":hashlib.sha256(y.tobytes()).hexdigest(),"output_power":float(np.mean(x.astype(float)**2)),
                    "source_interval_seconds":[6,22],"documented_delay_frames":delay})
    a.output.write_text(json.dumps({"scope":"Real recorded far-only audio; no independently calibrated hardware or runtime commitment",
        "versions":versions(),"results":rows},indent=2)+"\n")
