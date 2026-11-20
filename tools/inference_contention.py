"""External CPU inference contention, not a core dependency or live conversation.

Whisper/Parakeet use fresh bounded 3 s windows every 0.5 s via sherpa-onnx's
offline recognizers. This is buffered re-decoding, NOT native streaming. Piper
yields sentence audio; no device playback or acoustic stop time is inferred.
"""
import argparse
import csv
import hashlib
import importlib.metadata
import json
import multiprocessing as mp
import os
import pathlib
import queue
import shutil
import subprocess
import time
import traceback


def versions():
    return {p:importlib.metadata.version(p) for p in ("sherpa-onnx","sherpa-onnx-core","piper-tts","onnxruntime","numpy","scipy","psutil")}


def worker(kind,models,wav_path,seconds,begin,stop,ready,path):
    os.environ["OMP_NUM_THREADS"]="2"; os.environ["OPENBLAS_NUM_THREADS"]="1"
    events=[]
    try:
        import numpy as np
        import soundfile as sf
        from scipy.signal import resample_poly
        if kind in ("whisper","parakeet"):
            import sherpa_onnx
            d=pathlib.Path(models)/kind
            if kind=="whisper":
                model=sherpa_onnx.OfflineRecognizer.from_whisper(encoder=str(d/"tiny.en-encoder.int8.onnx"),
                    decoder=str(d/"tiny.en-decoder.int8.onnx"),tokens=str(d/"tiny.en-tokens.txt"),num_threads=2,provider="cpu")
            else:
                model=sherpa_onnx.OfflineRecognizer.from_transducer(encoder=str(d/"encoder.int8.onnx"),
                    decoder=str(d/"decoder.int8.onnx"),joiner=str(d/"joiner.int8.onnx"),
                    tokens=str(d/"tokens.txt"),num_threads=2,model_type="nemo_transducer",provider="cpu")
            audio,rate=sf.read(wav_path,dtype="float32")
            if audio.ndim!=1: raise ValueError("mono fixture required")
            if rate!=16000: audio=resample_poly(audio,16000,rate).astype(np.float32)
            def decode(x):
                stream=model.create_stream(); stream.accept_waveform(16000,x); model.decode_stream(stream)
                return stream.result.text
            decode(audio[:48000]) # Setup/lazy graph warm-up, outside measurement.
        else:
            import onnxruntime as ort
            from piper import PiperVoice
            from piper.config import PiperConfig
            d=pathlib.Path(models)/"piper/en/en_US/lessac/low/en_US-lessac-low.onnx"
            opts=ort.SessionOptions(); opts.intra_op_num_threads=2; opts.inter_op_num_threads=1
            model=PiperVoice(session=ort.InferenceSession(str(d),sess_options=opts,providers=["CPUExecutionProvider"]),
                config=PiperConfig.from_dict(json.loads(pathlib.Path(str(d)+".json").read_text(encoding="utf-8"))))
            list(model.synthesize("Ready."))
        ready.put({"kind":kind,"ready":True,"pid":os.getpid()})
        begin.wait(); origin=time.perf_counter(); due=origin; audio_available=origin
        while not stop.is_set() and time.perf_counter()-origin<seconds+3:
            if stop.wait(max(0,due-time.perf_counter())): break
            start=time.perf_counter(); skipped=0
            if kind in ("whisper","parakeet"):
                skipped=max(0,int((start-due)/.5)); due+=skipped*.5
                # Latest immutable window only: late workers drop pending requests.
                offset=int((due-origin)*16000)%len(audio)
                x=audio[(np.arange(48000)+offset)%len(audio)]
                text=decode(x); end=time.perf_counter()
                events.append({"window_ready_s":due-origin,"start_s":start-origin,"end_s":end-origin,
                    "decode_ms":(end-start)*1000,"partial_result_latency_ms":(end-due)*1000,
                    "window_audio_s":3,"dropped_window_requests":skipped,"hypothesis":text[:1024]})
                due+=.5
            else:
                if start<audio_available: due=audio_available; continue
                first=None; frames=0; rate=0
                for chunk in model.synthesize("The assistant is speaking while you begin your reply."):
                    if first is None: first=time.perf_counter()
                    frames+=chunk.audio_float_array.size; rate=chunk.sample_rate
                    if frames>rate*10: raise ValueError("bounded TTS output exceeded")
                end=time.perf_counter(); audio_available=end+frames/rate
                events.append({"start_s":start-origin,"first_audio_ms":(first-start)*1000,"end_s":end-origin,
                    "generated_frames":frames,"sample_rate":rate,"modeled_audio_queue_frames":frames,
                    "note":"One sentence retained; modeled drain, no actual playback queue"})
                due=audio_available
        path.write_text(json.dumps({"kind":kind,"provider":"cpu","threads":2,"events":events},indent=2)+"\n")
    except BaseException:
        failure={"kind":kind,"ready":False,"error":traceback.format_exc()}
        path.write_text(json.dumps(failure,indent=2)+"\n"); ready.put(failure)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("runner",type=pathlib.Path); p.add_argument("models",type=pathlib.Path)
    p.add_argument("audio",type=pathlib.Path); p.add_argument("mic",type=pathlib.Path); p.add_argument("reference",type=pathlib.Path)
    p.add_argument("output",type=pathlib.Path); p.add_argument("--seconds",type=int,default=30)
    p.add_argument("--wait-policy",choices=["default","highres"],default="default")
    p.add_argument("--workloads",nargs="+",default=["alone","whisper","parakeet","piper","whisper+piper","parakeet+piper"])
    args=p.parse_args()
    if not 5<=args.seconds<=300: p.error("duration must be 5..300 seconds")
    import psutil
    import numpy as np
    args.output.mkdir(parents=True,exist_ok=True)
    for scenario in args.workloads:
        kinds=[] if scenario=="alone" else scenario.split("+")
        if len(set(kinds))!=len(kinds) or any(k not in ("whisper","parakeet","piper") for k in kinds): p.error("invalid workload")
        directory=args.output/scenario; directory.mkdir(exist_ok=True)
        begin=mp.Event(); stop=mp.Event(); ready=mp.Queue()
        workers=[mp.Process(target=worker,args=(k,str(args.models.resolve()),str(args.audio.resolve()),args.seconds,
                 begin,stop,ready,directory/(k+".json"))) for k in kinds]
        samples=[]; process=None; statuses=[]; error=None
        command=[str(args.runner.resolve()),"pipeline","16000",str(args.mic.resolve()),str(args.reference.resolve()),
                 str(directory/"unused.f32"),str(directory/"dsp.csv"),str(directory/"native.json"),str(args.seconds),args.wait_policy]
        try:
            for w in workers: w.start()
            for _ in workers:
                status=ready.get(timeout=90); statuses.append(status)
                if not status["ready"]: raise RuntimeError(status["error"])
            with (directory/"native.log").open("w") as log:
                begin.set(); start=time.perf_counter()
                process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
                observed=[psutil.Process(process.pid)]+[psutil.Process(w.pid) for w in workers]
                next_gpu=start
                while process.poll() is None:
                    now=time.perf_counter(); row={"time_s":now-start,"processes":[]}
                    for proc in observed:
                        try: row["processes"].append({"pid":proc.pid,"cpu_percent":proc.cpu_percent(),
                            "rss_bytes":proc.memory_info().rss,"threads":proc.num_threads()})
                        except psutil.NoSuchProcess: pass
                    if now>=next_gpu and shutil.which("nvidia-smi"):
                        gpu=subprocess.run(["nvidia-smi","--query-gpu=index,name,utilization.gpu,memory.used,power.draw",
                            "--format=csv,noheader,nounits"],capture_output=True,text=True,timeout=3)
                        row["gpu_observation"]=gpu.stdout.strip(); next_gpu=now+1
                    samples.append(row)
                    if now-start>args.seconds+30: raise TimeoutError("native replay exceeded bound")
                    time.sleep(.25)
                if process.returncode: raise RuntimeError("native runner failed; native.log retained")
        except BaseException:
            error=traceback.format_exc()
        finally:
            stop.set(); begin.set()
            if process is not None and process.poll() is None: process.terminate(); process.wait(timeout=10)
            for w in workers:
                w.join(timeout=10)
                if w.is_alive(): w.terminate(); w.join(timeout=5)
        report={"scenario":scenario,"command":command,"versions":versions(),"worker_status":statuses,
            "runner_sha256":hashlib.sha256(args.runner.read_bytes()).hexdigest(),"error":error,
            "limitations":["ASR is bounded-window offline re-decoding, not native streaming",
                "Piper increments by sentence; modeled queue is not device occupancy",
                "Concurrent inference replay, not a connected live conversational/audio device pipeline"],
            "samples":samples}
        if not error:
            rows=list(csv.DictReader((directory/"dsp.csv").open()))
            def distribution(key,scale):
                x=np.array([float(r[key])*scale for r in rows])
                return {"p50":float(np.percentile(x,50)),"p95":float(np.percentile(x,95)),
                        "p99":float(np.percentile(x,99)),"max":float(np.max(x))}
            report["execution_ms"]=distribution("processing_ns",1e-6)
            report["scheduling_lateness_ms"]=distribution("start_lateness_ns",1e-6)
            report["completion_deadline_misses"]=sum(int(r["completion_deadline_miss"]) for r in rows)
            report["execution_deadline_misses"]=sum(int(r["processing_ns"])>10000000 for r in rows)
            report["native"]=json.loads((directory/"native.json").read_text())
        (directory/"report.json").write_text(json.dumps(report,indent=2)+"\n")
        print(scenario,"FAIL" if error else "complete",flush=True)
        if error: raise SystemExit(error)


if __name__=="__main__": main()
