"""Known-far-only curves and digital tails, never acoustic silence/commitment.

Diagnostic settling: two consecutive excited one-second windows reach the
existing synthetic AEC unit target of 20 dB. This is posthoc fixture analysis,
not an API convergence probability or a universal physical acceptance target.
"""
import argparse,csv,json,pathlib
import numpy as np

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for k in ("input","results","output"):p.add_argument(k,type=pathlib.Path)
    a=p.parse_args();jobs={j["name"]:j for j in json.loads((a.input/"manifest.json").read_text())["jobs"]}
    records=json.loads((a.results/"results.json").read_text())["results"];seen=set();rows=[]
    for r in records:
        j=jobs[r["case"]];key=(r["case"],r["stage"])
        if key in seen:continue
        seen.add(key)
        if j["near_peak"]:raise ValueError("ERLE analysis requires known absent near speech")
        rate=j["rate"];delay=r["documented_delay_frames"]
        mic=np.fromfile(a.input/j["mic"],dtype="<f4").astype(float)
        ref=np.fromfile(a.input/j["reference"],dtype="<f4").astype(float)
        path=a.input/j["mic"] if r["stage"]=="raw" else a.results/(j["name"]+"-"+r["stage"]+".f32")
        raw=np.fromfile(path,dtype="<f4").astype(float);out=raw[delay:delay+len(mic)]
        if len(out)!=len(mic):raise ValueError("timing coverage")
        def ratio(first,last):
            i=float(np.sum(mic[first:last]**2));o=float(np.sum(out[first:last]**2))
            return float(10*np.log10(i/max(o,1e-30))) if i>1e-20 else None
        curve=[{"end_s":(first+rate//10)/rate,"mic_output_ratio_db":ratio(first,first+rate//10),
            "output_power":float(np.mean(out[first:first+rate//10]**2))} for first in range(0,len(mic)-rate//10+1,rate//10)]
        trace=list(csv.DictReader((a.results/(j["name"]+"-"+r["stage"]+"-vad.csv")).open()))
        rises=sum(int(v["active"]) and (i==0 or not int(trace[i-1]["active"])) for i,v in enumerate(trace))
        row={"case":j["name"],"stage":r["stage"],"false_near_vad_candidates":rises,"curve":curve,
             "committed_word_rate":None,"documented_ns_delay_frames":delay}
        event={"startup":2,"restart":10,"reference-gap":10.2,"path-change":12}.get(j["name"],0)
        if j["name"]=="cancel-tail":
            cancel=10*rate;indices=np.flatnonzero(np.abs(raw[cancel:])>=1/32768)
            row["digital_tail_after_cancel_ms_above_pcm16_lsb"]=(int(indices[-1])+1)*1000/rate if len(indices) else 0
            row["settling_20db_s"]=None;row["settling_note"]="No continuing reference; do not infer convergence or audible silence"
        else:
            count=0;settled=None
            event_frame=int(round(event*rate))
            for offset in range((len(mic)-event_frame)//rate):
                first=event_frame+offset*rate;last=first+rate
                value=ratio(first,last);excited=float(np.mean(ref[first:last]**2))>1e-12
                count=count+1 if excited and value is not None and value>=20 else 0
                if count>=2:settled=float(offset+1);break
            row["settling_20db_s"]=settled;row["settling_event_s"]=event
            row["settling_note"]="Two consecutive excited one-second windows; null means not reached in retained interval"
        rows.append(row)
    a.output.write_text(json.dumps({"method":__doc__,"digital_tail_threshold_fs":1/32768,"results":rows},indent=2)+"\n")
