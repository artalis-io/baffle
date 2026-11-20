"""Gate H paired public-API DSP / fixed offline-ASR evaluation. External QA only.

Ground truth is the pinned provider transcript, never recognizer predictions.
Constructed insertion times are NOT independently reviewed phoneme onsets.
No normalization, endpointing, VAD gating or score-optimized delay is applied.
"""
import argparse,csv,hashlib,json,os,pathlib,subprocess,time
import numpy as np
import soundfile as sf
from inference_contention import load_asr,versions
from asr_metrics import words,edits,metrics

STAGES=("raw","baffle","aec-ns","aec-ns-agc","pipeline")
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,x):p.write_text(json.dumps(x,indent=2)+"\n",encoding="utf-8")
def audio_stats(x,rate):
    finite=np.isfinite(x); clean=x[finite].astype(np.float64)
    return {"rate":rate,"channels":1,"frames":len(x),"duration_s":len(x)/rate,
        "nonfinite_samples":int(np.sum(~finite)),"rms":float(np.sqrt(np.mean(clean*clean))),
        "peak":float(np.max(np.abs(clean))),"silence_ratio_below_1e_5_fs":float(np.mean(np.abs(clean)<1e-5)),
        "samples_above_unity":int(np.sum(np.abs(clean)>1))}


def prepare(models,seeded,output):
    output.mkdir(parents=True,exist_ok=True);manifest=output/"manifest.json"
    if manifest.exists():
        cached=json.loads(manifest.read_text())
        for j in cached["jobs"]:
            for key in ("mic","reference","oracle"):
                if sha(output/j[key])!=j[key+"_sha256"]:raise ValueError("immutable fixture identity mismatch")
        return
    # Preregister conservative transport/intelligibility checks before decoding.
    # These are engineering checks, NOT owner-approved VAD/phoneme budgets.
    policy={"near_only_max_added_word_errors_per_case":0,"maximum_added_empty_speech_cases":0,
            "doubletalk_requires_pooled_error_reduction":True,"far_only_target_nonempty":0,
            "phone_onset_policy":None,"physical_or_conversational_acceptance":False}
    far=np.fromfile(seeded/"normal-ref.f32",dtype="<f4").astype(np.float64)/.25
    jobs=[];sources=[]
    cases=[("near-normal",.2,0,8,None), ("near-quiet",.025,0,8,None),("near-loud",.65,0,8,None),
           ("doubletalk",.2,.25,8,None),("quiet-doubletalk",.025,.5,8,None),("loud-doubletalk",.65,.25,8,None),
           ("early",.2,.25,.5,None),("cancel-span",.15,.3,8,10),("post-cancel",.15,.3,10.02,10),
           ("render-restart",.15,.3,8,10),("capture-epoch",.15,.3,8,None),("missing-reference",.15,.3,8,None),
           ("far-only",0,.25,8,None),("silence",0,0,8,None)]
    transcripts=(models/"whisper/test_wavs/trans.txt").read_text(encoding="utf-8")
    for source_id in (0,1):
        source=models/f"whisper/test_wavs/{source_id}.wav";near,rate=sf.read(source,dtype="float64")
        if rate!=16000 or near.ndim!=1:raise ValueError("pinned 16k mono source required")
        text=next(line.split(" ",1)[1] for line in transcripts.splitlines() if line.startswith(str(source_id)+".wav "))
        near/=np.max(np.abs(near));sources.append({"id":source_id,"sha256":sha(source),"reference":text})
        for scenario,near_peak,far_peak,start,cancel in cases:
            n=max(24*rate,int(start*rate)+len(near)+rate)
            n=(n+159)//160*160;reference=np.resize(far,n)*far_peak
            if cancel is not None:
                if scenario=="render-restart":reference[int(cancel*rate):]*=-1
                else:reference[int(cancel*rate):]=0
            target=np.zeros(n);onset=int(round(start*rate));target[onset:onset+len(near)]=near*near_peak
            echo=np.zeros(n)
            for delay,gain in ((48,.6),(320,.2),(1280,-.1)):echo[delay:]+=reference[:-delay]*gain
            mic=target+echo
            if np.max(np.abs(mic))>=1:raise ValueError("clipped fixture")
            name=f"source{source_id}-{scenario}";j={"name":name,"scenario":scenario,"rate":rate,
                "reference_transcript":text if near_peak else "","source_id":source_id,
                "constructed_insertion_frame_not_reviewed_onset":onset,"asr_frames":[max(0,onset-8000),onset+len(near)+8000],
                "near_peak":near_peak,"far_peak":far_peak,"cancel_s":cancel,"frames":n}
            for key,x in (("mic",mic),("reference",reference),("oracle",target)):
                path=output/(name+"-"+key+".f32");x.astype("<f4").tofile(path);j[key]=path.name;j[key+"_sha256"]=sha(path)
            events=None
            if scenario in ("cancel-span","post-cancel"):events="0 1 1 1 0\n160000 1 1 2 0\n"
            if scenario=="render-restart":events="0 1 1 1 0\n160000 1 2 2 0\n"
            if scenario=="capture-epoch":events="0 1 1 1 0\n160000 2 1 1 0\n"
            if scenario=="missing-reference":events="0 1 1 1 0\n160000 1 1 1 1\n161600 1 1 1 0\n"
            if events:
                path=output/(name+"-events.txt");path.write_text(events);j["events"]=path.name;j["events_sha256"]=sha(path)
            jobs.append(j)
    dump(manifest,{"scope":"Recorded human speech + real Piper TTS; simulated linear echo, not physical acoustic or reviewed onset",
        "model_source_revision":"d026532c022fa99fd789d6b32446a1df7b6bfc43","transcript_sha256":sha(models/"whisper/test_wavs/trans.txt"),
        "far_cache_manifest_sha256":sha(seeded/"manifest.json"),"far_pcm_sha256":sha(seeded/"normal-ref.f32"),
        "preparation_script_sha256":sha(pathlib.Path(__file__)),"policy":policy,"sources":sources,"jobs":jobs})


def run(runner,vad,models,input_dir,output):
    output.mkdir(parents=True,exist_ok=True);manifest=json.loads((input_dir/"manifest.json").read_text())
    records=[];asr={kind:load_asr(kind,models) for kind in ("whisper","parakeet")}
    for job in manifest["jobs"]:
        for key in ("mic","reference","oracle"):
            if sha(input_dir/job[key])!=job[key+"_sha256"]:raise ValueError("fixture mismatch")
        for stage in STAGES:
            stem=output/(job["name"]+"-"+stage);native=None;delay=0;raw=input_dir/job["mic"]
            if stage!="raw":
                env=os.environ.copy();env.pop("BF_QUALIFICATION_TRANSPORT_EVENTS",None)
                if "events" in job:
                    if sha(input_dir/job["events"])!=job["events_sha256"]:raise ValueError("event identity mismatch")
                    env["BF_QUALIFICATION_TRANSPORT_EVENTS"]=str((input_dir/job["events"]).resolve())
                command=[str(runner.resolve()),stage,str(job["rate"]),str(raw),str(input_dir/job["reference"]),
                         str(stem)+".f32",str(stem)+".csv",str(stem)+".json"]
                native_run=subprocess.run(command,env=env,capture_output=True,text=True)
                if native_run.returncode:raise ValueError(native_run.stderr)
                native=json.loads(pathlib.Path(str(stem)+".json").read_text());delay=native["added_pipeline_delay_frames"]
                raw=pathlib.Path(str(stem)+".f32")
            x=np.fromfile(raw,dtype="<f4");first,last=job["asr_frames"]
            # Entire identical source interval, corrected only by documented delay.
            window=x[first+delay:last+delay].copy()
            if len(window)!=last-first:raise ValueError("incomplete paired window")
            trace=pathlib.Path(str(stem)+"-vad.csv")
            subprocess.run([str(vad.resolve()),str(job["rate"]),str(raw),str(trace)],check=True,capture_output=True)
            trace_rows=list(csv.DictReader(trace.open()))
            for kind,recognizer in asr.items():
                started=time.perf_counter();stream=recognizer.create_stream()
                stream.accept_waveform(job["rate"],window);recognizer.decode_stream(stream)
                text=stream.result.text;latency=(time.perf_counter()-started)*1000
                records.append({"case":job["name"],"scenario":job["scenario"],"stage":stage,"recognizer":kind,
                    "reference":job["reference_transcript"],"hypothesis":text,"error_status":None,
                    **metrics(job["reference_transcript"],text),"input":audio_stats(window,job["rate"]),
                    "vad_active_blocks":sum(int(r["active"]) for r in trace_rows),"vad_trace_sha256":sha(trace),
                    "decode_ms":latency,"pcm_sha256":sha(raw),"asr_window_sha256":hashlib.sha256(window.tobytes()).hexdigest(),
                    "documented_delay_frames":delay,"native":native,"offline_partial_stability":None})
            print(job["name"],stage,flush=True)
            dump(output/"results.json",{"manifest_sha256":sha(input_dir/"manifest.json"),"versions":versions(),
                "decoder_configs":{k:str(v.config) for k,v in asr.items()},"runner_sha256":sha(runner),"results":records})
    for row in records:
        raw=next(r for r in records if r["case"]==row["case"] and r["recognizer"]==row["recognizer"] and r["stage"]=="raw")
        row["added_word_errors_vs_raw"]=row["word"]["errors"]-raw["word"]["errors"]
        row["raw_already_empty"]=raw["empty"]
    dump(output/"results.json",{"manifest_sha256":sha(input_dir/"manifest.json"),"versions":versions(),
        "decoder_configs":{k:str(v.config) for k,v in asr.items()},"runner_sha256":sha(runner),"results":records,
        "scope":"Fixed complete-utterance offline decode; no VAD gating or endpointing, not streaming/onset/physical qualification"})


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest="command",required=True)
    a=s.add_parser("prepare")
    for key in ("models","seeded","output"):a.add_argument(key,type=pathlib.Path)
    a=s.add_parser("run")
    for key in ("runner","vad","models","input","output"):a.add_argument(key,type=pathlib.Path)
    a=p.parse_args()
    if a.command=="prepare":prepare(a.models,a.seeded,a.output)
    else:run(a.runner,a.vad,a.models,a.input,a.output)
