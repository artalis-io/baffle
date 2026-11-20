"""Export paired raw/processed audition WAVs and UNREVIEWED onset manifests.

No energy detector, forced aligner or model invents phone annotations. Human
review must supply labels, evidence, uncertainty, intelligibility judgments and
application policy before onset_qualification.py can issue PASS.
"""
import argparse,hashlib,json,pathlib,shutil,wave
import numpy as np


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for k in ("input","results","output"):p.add_argument(k,type=pathlib.Path)
    a=p.parse_args()
    if a.output.exists() and any(a.output.iterdir()):raise ValueError("Use a fresh review directory; never replace human annotations")
    a.output.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((a.input/"manifest.json").read_text());index=[]
    template=json.loads((pathlib.Path(__file__).resolve().parents[1]/"tests/fixtures/onset-review.example.json").read_text())
    for job in manifest["jobs"]:
        if not job["near_peak"]:continue
        directory=a.output/job["name"];directory.mkdir(exist_ok=True)
        source=a.input/job["mic"];processed=a.results/(job["name"]+"-pipeline.f32")
        trace=a.results/(job["name"]+"-pipeline-vad.csv")
        case=json.loads(json.dumps(template));case["scenario"]=job["scenario"]
        for key,path in (("source",source),("processed",processed),("clean_near_component",a.input/job["oracle"]),("vad_trace",trace)):
            target=directory/path.name;shutil.copyfile(path,target)
            case[key]={"path":str(target.resolve()),"sha256":hashlib.sha256(target.read_bytes()).hexdigest()}
            if key=="vad_trace":continue
            samples=np.fromfile(target,dtype="<f4");clipped=int(np.sum(np.abs(samples)>1))
            with wave.open(str(directory/(key+"-audition.wav")),"wb") as w:
                w.setnchannels(1);w.setsampwidth(2);w.setframerate(job["rate"])
                w.writeframes(np.rint(np.clip(samples,-1,32767/32768)*32768).astype("<i2").tobytes())
            case[key]["audition_note"]={"format":"PCM16 quantized audition only; metrics use original F32","clipped_samples":clipped}
        case["reference_transcript"]=job["reference_transcript"]
        case["constructed_insertion_frame_NOT_an_annotation"]=job["constructed_insertion_frame_not_reviewed_onset"]
        case["algorithmic_delay_frames"]=job["rate"]//100
        (directory/"review.json").write_text(json.dumps(case,indent=2)+"\n")
        index.append({"case":job["name"],"manifest":str(directory/"review.json"),"status":"UNREVIEWED"})
    (a.output/"index.json").write_text(json.dumps({"reviews":index,"instruction":"Listen to raw/processed audio; independently annotate phone/syllable starts/ends and source provenance. Constructed insertion is not speech onset. No PASS without review and approved policy."},indent=2)+"\n")
