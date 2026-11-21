"""Bounded local LLM request load during paced native DSP replay.

Uses an already-running server, never starts/reconfigures/stops that service.
This is LLM contention only, not ASR/TTS or a connected acoustic conversation.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import threading
import time
import urllib.request


def requests(endpoint,model,seconds,stop,events):
    deadline=time.monotonic()+seconds
    while not stop.is_set() and time.monotonic()<deadline:
        start=time.perf_counter(); first=None; chunks=0; usage=None; error=None
        payload={"model":model,"messages":[{"role":"user","content":"Explain acoustic echo cancellation and double-talk in a few sentences."}],
                 "max_tokens":128,"stream":True,"stream_options":{"include_usage":True},
                 "chat_template_kwargs":{"enable_thinking":False}}
        request=urllib.request.Request(endpoint+"/chat/completions",json.dumps(payload).encode(),{"Content-Type":"application/json"})
        try:
            with urllib.request.urlopen(request,timeout=20) as response:
                for line in response:
                    if stop.is_set() or time.monotonic()>=deadline: break
                    if not line.startswith(b"data: ") or line.strip()==b"data: [DONE]": continue
                    item=json.loads(line[6:]); usage=item.get("usage") or usage
                    for choice in item.get("choices",[]):
                        delta=choice.get("delta",{})
                        if delta.get("content") or delta.get("reasoning_content"):
                            if first is None: first=time.perf_counter()
                            chunks+=1
        except Exception as exc: error=str(exc)
        events.append({"start_perf_s":start,"first_generated_chunk_ms":(first-start)*1000 if first else None,
                       "duration_ms":(time.perf_counter()-start)*1000,"generated_chunks_not_tokens":chunks,
                       "server_usage":usage,"error":error})
        if error: break


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__); p.add_argument("runner",type=pathlib.Path)
    p.add_argument("mic",type=pathlib.Path); p.add_argument("reference",type=pathlib.Path); p.add_argument("output",type=pathlib.Path)
    p.add_argument("--endpoint",default="http://127.0.0.1:8086/v1"); p.add_argument("--model",default="qwen38-fp4")
    p.add_argument("--seconds",type=int,default=30); a=p.parse_args()
    if not a.endpoint.startswith("http://127.0.0.1:") or not 5<=a.seconds<=120: p.error("bounded localhost benchmark only")
    a.output.mkdir(parents=True,exist_ok=True)
    models=json.load(urllib.request.urlopen(a.endpoint+"/models",timeout=3))
    if a.model not in [m["id"] for m in models["data"]]: raise SystemExit("requested served model unavailable")
    for scenario in ("alone","with-llm"):
        d=a.output/scenario; d.mkdir(exist_ok=True); stop=threading.Event(); events=[]; observations=[]
        command=[str(a.runner.resolve()),"pipeline","16000",str(a.mic.resolve()),str(a.reference.resolve()),
                 str(d/"unused.f32"),str(d/"dsp.csv"),str(d/"native.json"),str(a.seconds)]
        thread=threading.Thread(target=requests,args=(a.endpoint,a.model,a.seconds,stop,events)) if scenario=="with-llm" else None
        try:
            if thread: thread.start()
            with (d/"native.log").open("w") as log:
                native=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
                start=time.monotonic()
                while native.poll() is None:
                    gpu=subprocess.run(["nvidia-smi","--query-gpu=index,name,utilization.gpu,memory.used,power.draw",
                         "--format=csv,noheader,nounits"],capture_output=True,text=True,timeout=3)
                    observations.append({"elapsed_s":time.monotonic()-start,"gpu":gpu.stdout.strip()})
                    if time.monotonic()-start>a.seconds+30: native.terminate(); raise TimeoutError("native trial bound")
                    time.sleep(.5)
                if native.returncode: raise RuntimeError("native replay failed; log retained")
        finally:
            stop.set()
            if thread: thread.join(timeout=25)
            (d/"workload.json").write_text(json.dumps({"scenario":scenario,"model":a.model,"endpoint":a.endpoint,
                "command":command,"runner_sha256":hashlib.sha256(a.runner.read_bytes()).hexdigest(),
                "requests":events,"gpu_observations":observations,
                "scope":"Existing local LLM plus paced DSP; no ASR/TTS/device or acoustic latency claim"},indent=2)+"\n")
