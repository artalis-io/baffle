"""Predefined assistant-only diagnostics. Simulated acoustics, no user attribution.

All references and microphone signals are retained. Reference gaps change
availability, not the acoustic signal already played. No transcript commitment.
"""
import argparse,hashlib,json,pathlib
import numpy as np
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("seeded",type=pathlib.Path);p.add_argument("output",type=pathlib.Path);a=p.parse_args()
    if a.output.exists() and any(a.output.iterdir()):raise ValueError("fresh immutable output required")
    a.output.mkdir(parents=True,exist_ok=True);rate=16000;n=24*rate
    original=np.fromfile(a.seeded/"normal-ref.f32",dtype="<f4").astype(float)/.25;jobs=[]
    # One variable relative to room-base, except named lifecycle combinations.
    for name in ("room-base","quiet","loud","long-tail","nonlinear","path-change","delay-100ms","startup","cancel-tail","restart","reference-gap"):
        peak=.05 if name=="quiet" else .7 if name=="loud" else .25
        far=original.copy()*peak
        if name=="startup":far=np.r_[np.zeros(2*rate),far[:-2*rate]]
        if name=="cancel-tail":far[10*rate:]=0
        if name=="restart":far[10*rate:]*=-1
        taps=[(48,.6),(320,.2),(1280,-.1)]
        if name=="long-tail":taps.append((4800,.15))
        if name=="delay-100ms":taps=[(d+1600,g) for d,g in taps]
        def convolve(taps):
            y=np.zeros(n)
            for delay,gain in taps:y[delay:]+=far[:-delay]*gain
            return y
        mic=convolve(taps)
        if name=="nonlinear":mic+=.2*mic**3
        if name=="path-change":
            changed=convolve([(1280,.6),(1600,.2),(2560,-.1)]);mic[12*rate:]=changed[12*rate:]
        if np.max(np.abs(mic))>=1:raise ValueError("fixture clipping")
        first,last=(0,6*rate) if name=="startup" else (10*rate,12*rate) if name=="cancel-tail" else (6*rate,22*rate)
        j={"name":name,"scenario":"far-only","source_id":0,"rate":rate,"frames":n,"near_peak":0,"far_peak":peak,
           "reference_transcript":"","asr_frames":[first,last],"constructed_insertion_frame_not_reviewed_onset":0,
           "taps":taps,"scope":"simulated acoustic echo of real TTS; known no near speech"}
        for key,x in (("mic",mic),("reference",far),("oracle",np.zeros(n))):
            path=a.output/(name+"-"+key+".f32");x.astype("<f4").tofile(path);j[key]=path.name;j[key+"_sha256"]=sha(path)
        events=None
        if name in ("cancel-tail","restart"):events="0 1 1 1 0\n160000 1 "+("2" if name=="restart" else "1")+" 2 0\n"
        if name=="reference-gap":events="0 1 1 1 0\n160000 1 1 1 1\n163200 1 1 1 0\n"
        if events:
            path=a.output/(name+"-events.txt");path.write_text(events);j["events"]=path.name;j["events_sha256"]=sha(path)
        jobs.append(j)
    (a.output/"manifest.json").write_text(json.dumps({"scope":"Assistant-only diagnostics, not physical/acoustic calibration or commitment policy",
        "source_sha256":sha(a.seeded/"normal-ref.f32"),"jobs":jobs,"committed_word_rate":None,
        "policy":{"far_only_target_nonempty":0},"preparation_source_sha256":sha(pathlib.Path(__file__))},indent=2)+"\n")
