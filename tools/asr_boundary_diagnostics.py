"""One-factor ASR boundary/level controls. Never replaces full-window WER.

Fresh decoder, whole-window gain and zeroed PRE-speech context are diagnostic
interventions. The difficult original context and failed hypotheses stay retained.
No DSP/ASR production parameter is selected from these outcomes.
"""
import argparse
import hashlib
import json
import pathlib
import numpy as np
from inference_contention import load_asr
from word_qualification import wer

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("models","input","results","report"): p.add_argument(name,type=pathlib.Path)
    a=p.parse_args(); manifest=json.loads((a.input/"manifest.json").read_text()); output=[]
    for job in manifest["jobs"]:
        if job["name"] not in ("normal","through-cancel"): continue
        path=a.results/(job["name"]+"-baffle.f32")
        audio=np.fromfile(path,dtype="<f4"); first,last=job["asr_interval_frames"]
        x=audio[first:last].copy()
        no_context=x.copy(); no_context[:8000]=0
        for intervention,y in (("fresh-original-full-window",x),("whole-window-gain-0.1",x*.1),
                                ("only-pre-speech-context-zeroed",no_context)):
            model=load_asr("parakeet",a.models); stream=model.create_stream(); stream.accept_waveform(16000,y)
            model.decode_stream(stream); text=stream.result.text
            output.append({"case":job["name"],"intervention":intervention,"hypothesis":text,
                "wer":wer(job["reference_transcript"],text),"source_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
                "window_sha256":hashlib.sha256(y.tobytes()).hexdigest()})
    a.report.write_text(json.dumps({"scope":"Attribution only; original full-window evaluation remains authoritative",
        "results":output},indent=2)+"\n")
