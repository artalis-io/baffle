"""Supplement raw scores using ONLY independent near-impulse peak observations.

Never searches a delay maximizing speech quality. Preserves original raw scores.
Peak delay is not exact broadband phase/group delay or physical timestamp truth.
The original Baffle 8 dB acceptance program is entirely separate and unchanged.
"""
import argparse
import hashlib
import json
import pathlib
import numpy as np


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest",type=pathlib.Path); p.add_argument("original_report",type=pathlib.Path)
    p.add_argument("output_files",type=pathlib.Path); p.add_argument("report",type=pathlib.Path)
    p.add_argument("--linear-report",type=pathlib.Path)
    a=p.parse_args(); manifest=json.loads(a.manifest.read_text()); original=json.loads(a.original_report.read_text())
    results=original["results"]+(json.loads(a.linear_report.read_text())["results"] if a.linear_report else [])
    jobs={j["name"]:j for j in manifest["jobs"]}; calibration={}
    for r in results:
        if r["scope"]=="synthetic-latency-probe":
            job=jobs[r["job"]]; d=r["metrics"]["observed_near_peak_offset_frames"]
            if not 0<=d<job["rate"]//10 or r["metrics"]["near_peak"]<1e-5: raise ValueError("unusable independent peak probe")
            calibration[(r["backend"],job["rate"])]=dict(delay_frames=d,probe=r["job"],output_sha256=r["output_sha256"])
    scored=[]
    for r in results:
        job=jobs[r["job"]]
        if "oracle" not in job: continue
        measured=calibration[(r["backend"],job["rate"])]; d=measured["delay_frames"]
        path=a.output_files/(r["job"]+"-"+r["backend"]+".f32")
        if not path.exists(): continue
        if hashlib.sha256(path.read_bytes()).hexdigest()!=r["output_sha256"]: raise ValueError("output identity mismatch")
        x=np.fromfile(a.manifest.parent/job["mic"],dtype="<f4").astype(np.float64)
        oracle=np.fromfile(a.manifest.parent/job["oracle"],dtype="<f8")
        y=np.fromfile(path,dtype="<f4").astype(np.float64)
        first,last=[int(t*job["rate"]) for t in job["near_interval_s"]]
        if last+d>len(y): raise ValueError("full scored input interval unavailable")
        target=oracle[first:last]; before=x[first:last]-target; shifted=y[first+d:last+d]
        snri=10*np.log10(np.sum(before**2)/max(np.sum((shifted-target)**2),1e-30))
        scored.append({"job":r["job"],"backend":r["backend"],"legacy_raw":r["metrics"],"independent_peak_calibration":measured,
            "peak_calibrated_component_snr_improvement_db":float(snri),
            "peak_calibrated_near_projection":float(np.dot(target,shifted)/np.dot(target,target)),
            "input_interval_unchanged_frames":[first,last]})
    a.report.write_text(json.dumps({"original_report_sha256":hashlib.sha256(a.original_report.read_bytes()).hexdigest(),
        "method":"Fixed independent impulse-peak compensation; original raw metrics retained, no universal acceptance claim",
        "not_physical_or_phoneme_qualification":True,"results":scored},indent=2)+"\n")
