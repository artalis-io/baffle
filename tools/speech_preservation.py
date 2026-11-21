"""Gain-sensitive and gain-adjusted near-only signal diagnostics, not phones.

Whole identical source intervals retained. Log spectra use 20 ms Hann windows,
10 ms hop, 512-point FFT, 300..7500 Hz and power floor 1e-12 FS^2. A best scalar
gain is a diagnostic, not a changed waveform or intelligibility qualification.
"""
import argparse,csv,hashlib,json,pathlib
import numpy as np


def spectrum(x,rate):
    frame=rate//50;hop=rate//100
    starts=range(0,len(x),hop);padded=np.pad(x,(0,frame))
    windows=np.stack([padded[s:s+frame] for s in starts])*np.hanning(frame)
    return np.abs(np.fft.rfft(windows,n=512))**2


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for k in ("input","results","output"):p.add_argument(k,type=pathlib.Path)
    a=p.parse_args();jobs={j["name"]:j for j in json.loads((a.input/"manifest.json").read_text())["jobs"]}
    records=json.loads((a.results/"results.json").read_text())["results"];rows=[];seen=set()
    for r in records:
        j=jobs[r["case"]];key=(r["case"],r["stage"])
        if not j["scenario"].startswith("near-") or key in seen:continue
        seen.add(key);rate=j["rate"]
        if rate!=16000:raise ValueError("This registered diagnostic uses 16 kHz/512-point spectra")
        first,last=j["asr_frames"];d=r["documented_delay_frames"]
        path=a.input/j["mic"] if r["stage"]=="raw" else a.results/(r["case"]+"-"+r["stage"]+".f32")
        if hashlib.sha256(path.read_bytes()).hexdigest()!=r["pcm_sha256"]:raise ValueError("PCM identity")
        oracle=a.input/j["oracle"]
        if hashlib.sha256(oracle.read_bytes()).hexdigest()!=j["oracle_sha256"]:raise ValueError("oracle identity")
        x=np.fromfile(oracle,dtype="<f4")[first:last].astype(float)
        y=np.fromfile(path,dtype="<f4")[first+d:last+d].astype(float)
        if len(x)!=len(y):raise ValueError("window coverage")
        energy=float(np.dot(x,x));gain=float(np.dot(x,y)/energy)
        error=float(np.dot(y-x,y-x)/energy)
        aligned=y/gain if gain>1e-8 else y
        adjusted_error=float(np.dot(aligned-x,aligned-x)/energy)
        sx,sy=spectrum(x,rate),spectrum(aligned,rate);freq=np.fft.rfftfreq(512,1/rate)
        mask=(freq>=300)&(freq<=7500)
        lsd=float(np.sqrt(np.mean((10*np.log10(np.maximum(sy[:,mask],1e-12))-10*np.log10(np.maximum(sx[:,mask],1e-12)))**2)))
        row={"case":j["name"],"stage":r["stage"],"near_projection":gain,"relative_error_energy":error,
             "gain_adjusted_relative_error":adjusted_error,"gain_adjusted_log_spectral_rms_db":lsd,
             "output_peak":float(np.max(np.abs(y))),"samples_above_unity":int(np.sum(np.abs(y)>1)),
             "phoneme_labels":None,"speech_onset_preservation":None}
        if r["stage"]!="raw":
            trace=list(csv.DictReader((a.results/(j["name"]+"-"+r["stage"]+".csv")).open()))
            row["digital_clipping_flag_calls"]=sum(bool(int(v["flags"])&2048) for v in trace)
            if r["native"]["stage_mask"]&4:
                gains=np.array([float(v["gain"]) for v in trace]);row["agc_gain_range"]=[float(min(gains)),float(max(gains))]
                row["maximum_adjacent_gain_step"]=float(np.max(np.abs(np.diff(gains))))
        rows.append(row)
    a.output.write_text(json.dumps({"method":__doc__,"results":rows},indent=2)+"\n")
