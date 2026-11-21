"""One-factor experiments on the unchanged Microsoft controlled fixture.

First run controlled_aec.py with a reviewed cache. This tool adds no downloads.
The original acceptance metric/threshold stay in controlled_aec.py; these
experiments report output error SNR, with identical microphone/oracle samples.
"""
import argparse
import array
import hashlib
import json
import math
import pathlib
import subprocess

import recorded_aec as wav


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline",type=pathlib.Path)
    p.add_argument("corrected",type=pathlib.Path)
    p.add_argument("cache",type=pathlib.Path)
    p.add_argument("directory",type=pathlib.Path)
    p.add_argument("report",type=pathlib.Path)
    args = p.parse_args()
    args.directory.mkdir(parents=True,exist_ok=True)
    rate, mic = wav.read(args.cache/"controlled-doubletalk-mic.wav")
    rr, far = wav.read(args.cache/"controlled-doubletalk-ref.wav")
    nr, near = wav.read(args.cache/"03d6ced2-3cc1-4b32-bcea-770fe2d260ff_nearend-singletalk_mic.wav")
    if rate != rr or nr != rate or rate != 48000 or len(mic) != len(far):
        raise ValueError("expected original fixture")
    scale = .2/max(abs(v)/32768 for v in near)
    begin,end = 8*rate,18*rate
    oracle = [near[(i-begin+5*rate)%len(near)]/32768*scale for i in range(begin,end)]
    energy = sum(v*v for v in oracle)
    cases = []
    variants = [("baseline",args.baseline,200,10,None),
                ("tail-40",args.baseline,40,10,None),
                ("tail-500",args.baseline,500,10,None),
                ("hop-20",args.baseline,200,20,None),
                ("reference-gain-half",args.baseline,200,10,[round(v*.5) for v in far]),
                ("reference-gain-double",args.baseline,200,10,[v*2 for v in far]),
                ("reference-late-10ms",args.baseline,200,10,[0]*480+list(far[:-480])),
                ("reference-early-10ms",args.baseline,200,10,list(far[480:])+[0]*480),
                ("uncompensated-reference-100ppm",args.baseline,200,10,[]),
                ("capture-notch-only",args.corrected,200,10,None)]
    for name,tool,tail,hop,reference in variants:
        if name == "uncompensated-reference-100ppm":
            reference = []
            for i in range(len(far)):
                x = i*1.0001
                j = int(x)
                reference.append(round(far[j]*(1-(x-j))+far[j+1]*(x-j)) if j+1<len(far) else 0)
        ref = args.cache/"controlled-doubletalk-ref.wav"
        if reference is not None:
            ref = args.directory/(name+"-ref.wav")
            wav.write(ref,rate,array.array("h",reference))
        out = args.directory/(name+"-out.wav")
        command = [str(tool.resolve()),str(args.cache/"controlled-doubletalk-mic.wav"),str(ref),str(out),
                   str(6*rate),str(2*rate),str(rate),str(tail),str(hop)]
        result = subprocess.run(command,capture_output=True,text=True,check=True)
        _, output = wav.read(out)
        error = sum((output[i]/32768-oracle[i-begin])**2 for i in range(begin,end))
        cases.append({"name":name,"tail_ms":tail,"hop_ms":hop,"command":command,
                      "output_error_snr_db":10*math.log10(energy/error),
                      "native_stdout":result.stdout,"tool_sha256":hashlib.sha256(tool.read_bytes()).hexdigest(),
                      "reference_sha256":hashlib.sha256(ref.read_bytes()).hexdigest(),
                      "output_sha256":hashlib.sha256(out.read_bytes()).hexdigest()})
    args.report.write_text(json.dumps({"scope":"One-factor causal diagnostics, not a replacement acceptance test",
          "capture_sha256":hashlib.sha256((args.cache/"controlled-doubletalk-mic.wav").read_bytes()).hexdigest(),
          "interval_frames":[begin,end],"oracle":"unchanged known near component, including recording noise",
          "cases":cases},indent=2)+"\n")


if __name__ == "__main__":
    main()
