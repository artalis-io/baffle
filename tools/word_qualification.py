"""Known-transcript human speech mixed with actual Piper TTS and simulated echo.

Provider word labels are independent of both tested recognizers. No phone/onset
labels are inferred. This is ASR WER on a simulated echo, not physical acoustics
or proof of first-phoneme intelligibility. Source/derived audio stays local.
"""
import argparse
import hashlib
import json
import pathlib
import re
import time

import numpy as np
import soundfile as sf
from inference_contention import load_asr,versions


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def prepare(models,directory):
    from piper import PiperVoice
    from piper.config import PiperConfig
    import onnxruntime as ort
    directory.mkdir(parents=True,exist_ok=True)
    if (directory/"manifest.json").exists():
        cached=json.loads((directory/"manifest.json").read_text())
        for job in cached["jobs"]:
            for key in ("mic","reference","oracle"):
                if sha(directory/job[key])!=job[key+"_sha256"]: raise ValueError("cached immutable fixture mismatch")
        print("Reusing verified immutable fixture; choose a new directory for another preparation")
        return
    source=models/"whisper/test_wavs/0.wav"; transcript=models/"whisper/test_wavs/trans.txt"
    near,rate=sf.read(source,dtype="float64")
    if rate!=16000 or near.ndim!=1: raise ValueError("pinned 16k mono source required")
    text=next(x[6:].strip() for x in transcript.read_text(encoding="utf-8").splitlines() if x.startswith("0.wav "))
    path=models/"piper/en/en_US/lessac/low/en_US-lessac-low.onnx"
    ort.set_seed(20261120)
    opts=ort.SessionOptions(); opts.intra_op_num_threads=2; opts.inter_op_num_threads=1
    tts=PiperVoice(session=ort.InferenceSession(str(path),sess_options=opts,providers=["CPUExecutionProvider"]),
        config=PiperConfig.from_dict(json.loads(pathlib.Path(str(path)+".json").read_text(encoding="utf-8"))))
    chunks=[c.audio_float_array for c in tts.synthesize("I can help you plan the next step. Tell me what you would like to change. We can continue when you are ready.")]
    far=np.concatenate(chunks).astype(np.float64); far/=max(np.max(np.abs(far)),1e-9)
    near/=max(np.max(np.abs(near)),1e-9); n=24*rate
    jobs=[]
    for name,near_peak,far_peak,cancel in [("normal",.2,.25,None),("quiet-near",.025,.5,10.),("through-cancel",.15,.3,10.),
                                          ("epoch-flush",.15,.3,10.),("epoch-restart",.15,.3,10.)]:
        reference=np.tile(far,int(np.ceil(n/len(far))))[:n]*far_peak
        if cancel is not None:
            if name=="epoch-restart": reference[int(cancel*rate):]*=-1
            else: reference[int(cancel*rate):]=0
        target=np.zeros(n); start=8*rate; target[start:start+len(near)]=near*near_peak
        echo=np.zeros(n)
        for delay,gain in [(48,.6),(320,.2),(1280,-.1)]: echo[delay:]+=reference[:-delay]*gain
        microphone=target+echo
        if np.max(np.abs(microphone))>=1: raise ValueError("mixture would clip")
        mp=directory/(name+"-mic.f32"); rp=directory/(name+"-ref.f32"); op=directory/(name+"-oracle.f64")
        microphone.astype("<f4").tofile(mp); reference.astype("<f4").tofile(rp); target.astype("<f8").tofile(op)
        jobs.append({"name":name,"rate":rate,"frames":n,"mic":mp.name,"reference":rp.name,"oracle":op.name,
            "mic_sha256":sha(mp),"reference_sha256":sha(rp),"oracle_sha256":sha(op),
            "scope":"simulated-echo-provider-transcript","far_only_interval_s":[6,8],"near_interval_s":[8,8+len(near)/rate],
            "asr_interval_frames":[start-8000,start+len(near)+8000],"reference_transcript":text,
            "metadata":{"cancel_s":cancel,"tail_taps":[[48,.6],[320,.2],[1280,-.1]],"near_peak":near_peak,"far_peak":far_peak,
                        "phoneme_onset_labels":None}})
        if name.startswith("epoch-"):
            events=directory/(name+"-events.txt")
            events.write_text("0 1 1 1 0\n160000 1 2 2 0\n")
            jobs[-1].update(transport_events=events.name,transport_events_sha256=sha(events),
                event_semantics="Capture epoch unchanged; render epoch/generation advance at cancellation; acoustic FIR tail continues")
    (directory/"manifest.json").write_text(json.dumps({"jobs":jobs,"source_sha256":sha(source),"transcript_sha256":sha(transcript),
        "preparation_version":2,"onnx_random_seed":20261120,"preparation_script_sha256":sha(pathlib.Path(__file__)),
        "source_revision":"HF d026532c022fa99fd789d6b32446a1df7b6bfc43 test_wavs/0.wav + trans.txt",
        "tts_model_sha256":sha(path),"annotation_scope":"Provider word transcript only, no independently timed phoneme labels",
        "no_physical_measurement":True},indent=2)+"\n")


def words(text): return re.findall(r"[^\W_]+(?:'[^\W_]+)?",text.lower(),re.UNICODE)
def wer(reference,hypothesis):
    a,b=words(reference),words(hypothesis); previous=list(range(len(b)+1))
    for i,x in enumerate(a,1):
        current=[i]
        for j,y in enumerate(b,1): current.append(min(current[-1]+1,previous[j]+1,previous[j-1]+(x!=y)))
        previous=current
    return previous[-1]/len(a)


def score(models,directory,results,report):
    manifest=json.loads((directory/"manifest.json").read_text()); output=[]
    native=json.loads((results/"report.json").read_text())
    admitted={(r["job"],r["backend"]):r for r in native["results"] if "metrics" in r}
    for kind in ("whisper","parakeet"):
        model=load_asr(kind,models)
        for job in manifest["jobs"]:
            for key in ("mic","reference","oracle"):
                if sha(directory/job[key])!=job[key+"_sha256"]: raise ValueError("fixture identity mismatch")
            a,b=job["asr_interval_frames"]; rate=job["rate"]
            variants={"near-oracle":np.fromfile(directory/job["oracle"],dtype="<f8"),
                      "microphone":np.fromfile(directory/job["mic"],dtype="<f4")}
            for backend in ("baffle","aec3","pipeline"):
                file=results/(job["name"]+"-"+backend+".f32")
                receipt=admitted.get((job["name"],backend))
                if receipt and file.exists():
                    if sha(file)!=receipt["output_sha256"]: raise ValueError("backend output identity mismatch")
                    variants[backend]=np.fromfile(file,dtype="<f4")
            for variant,audio in variants.items():
                start=time.perf_counter(); stream=model.create_stream(); stream.accept_waveform(rate,audio[a:b].astype(np.float32)); model.decode_stream(stream)
                hypothesis=stream.result.text
                output.append({"case":job["name"],"recognizer":kind,"variant":variant,"reference":job["reference_transcript"],
                    "hypothesis":hypothesis,"wer":wer(job["reference_transcript"],hypothesis),
                    "first_reference_word_recognized":bool(words(hypothesis)) and words(hypothesis)[0]==words(job["reference_transcript"])[0],
                    "decode_ms":(time.perf_counter()-start)*1000,"complete_utterance_window_frames":[a,b],
                    "phoneme_intelligibility":"not established by word recognition"})
    report.write_text(json.dumps({"versions":versions(),"manifest_sha256":sha(directory/"manifest.json"),
        "native_report_sha256":sha(results/"report.json"),
        "unavailable_backend_cases":[r for r in native["results"] if "metrics" not in r],
        "scope":"Provider-transcript WER, simulated echo of real TTS, not physical or human phoneme/onset qualification", "results":output},indent=2)+"\n")


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__); c=p.add_subparsers(dest="command",required=True)
    s=c.add_parser("prepare"); s.add_argument("models",type=pathlib.Path); s.add_argument("directory",type=pathlib.Path)
    s=c.add_parser("score"); s.add_argument("models",type=pathlib.Path); s.add_argument("directory",type=pathlib.Path); s.add_argument("results",type=pathlib.Path); s.add_argument("report",type=pathlib.Path)
    a=p.parse_args()
    if a.command=="prepare": prepare(a.models,a.directory)
    else: score(a.models,a.directory,a.results,a.report)
