"""Known-oracle simulated FIR cancellation windows, not acoustic silence or phones.

Keeps the complete windows, including the 80 ms echo tail. Does not choose a
delay to optimize scores: pipeline's documented 10 ms buffering is explicit.
No universal cancellation-quality acceptance threshold is invented.
"""
import argparse
import hashlib
import json
import pathlib
import numpy as np


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("input","results","report"): p.add_argument(name,type=pathlib.Path)
    a=p.parse_args(); manifest=json.loads((a.input/"manifest.json").read_text())
    native=json.loads((a.results/"report.json").read_text())
    admitted={(r["job"],r["backend"]):r for r in native["results"] if "metrics" in r}
    output=[]
    for job in manifest["jobs"]:
        cancel=job.get("metadata",{}).get("cancel_s")
        if cancel is None: continue
        for key in ("mic","reference","oracle"):
            if sha(a.input/job[key])!=job[key+"_sha256"]: raise ValueError("fixture identity mismatch")
        rate=job["rate"]; oracle=np.fromfile(a.input/job["oracle"],dtype="<f8")
        for backend in ("baffle","pipeline"):
            receipt=admitted.get((job["name"],backend))
            if receipt is None: continue
            file=a.results/(job["name"]+"-"+backend+".f32")
            if sha(file)!=receipt["output_sha256"]: raise ValueError("output identity mismatch")
            audio=np.fromfile(file,dtype="<f4").astype(np.float64)
            delay=rate//100 if backend=="pipeline" else 0
            for name,first,last in (("before",cancel-.1,cancel),("fir-tail",cancel,cancel+.08),
                                    ("after-tail",cancel+.08,cancel+.58)):
                start,end=int(round(first*rate)),int(round(last*rate))
                x=oracle[start:end]; y=audio[start+delay:end+delay]
                if len(x)!=len(y) or not len(x): raise ValueError("incomplete evaluation window")
                energy=float(np.dot(x,x)); error=float(np.dot(y-x,y-x))
                output.append({"case":job["name"],"backend":backend,"window":name,
                    "input_frames":[start,end],"documented_delay_frames":delay,
                    "near_projection":float(np.dot(y,x)/energy) if energy>1e-20 else None,
                    "near_error_snr_db":float(10*np.log10(energy/max(error,1e-30))) if energy>1e-20 else None,
                    "near_power":energy/len(x),"output_power":float(np.mean(y*y)),
                    "output_sha256":sha(file)})
    a.report.write_text(json.dumps({"manifest_sha256":sha(a.input/"manifest.json"),
        "native_report_sha256":sha(a.results/"report.json"),"results":output,
        "qualification":"SIMULATED FIR only; projection/energy cannot establish onset intelligibility or acoustic silence",
        "acceptance_threshold":None},indent=2)+"\n")
