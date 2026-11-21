"""Strict admission/scoring for independently reviewed interruption labels.

This deliberately does NOT generate phone labels from energy/correlation/ASR.
An unreviewed template or missing application policy can never produce PASS.
All intelligibility judgments must come from independent review, not projection.
"""
import argparse
import csv
import hashlib
import json
import pathlib
import math


def admission(case):
    reasons=[]
    if case.get("fixture_kind") not in ("physical","recorded-acoustic","simulated-with-recorded-source","synthetic-self-test"):
        reasons.append("fixture provenance kind unavailable")
    if case.get("pcm_format")!="f32le-mono": reasons.append("canonical PCM declaration unavailable")
    rate=case.get("sample_rate")
    if isinstance(rate,bool) or rate not in (8000,16000,32000,48000): reasons.append("supported sample rate unavailable")
    for key in ("source","processed","vad_trace"):
        entry=case.get(key,{})
        path=pathlib.Path(entry.get("path",""))
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=entry.get("sha256"):
            reasons.append(key+" identity unavailable/mismatched")
        elif key!="vad_trace" and (path.stat().st_size==0 or path.stat().st_size%4 or path.stat().st_size>256*1024*1024):
            reasons.append(key+" canonical PCM size invalid")
    labels=case.get("labels",{})
    if labels.get("method") not in ("human-reviewed","independent-reviewed-corpus") or not labels.get("reviewer"):
        reasons.append("independent label review unavailable")
    values=[labels.get(k) for k in ("onset_frame","first_phone_end_frame","first_syllable_end_frame")]
    if any(not isinstance(v,int) or isinstance(v,bool) for v in values) or not (0<=values[0]<values[1]<=values[2]):
        reasons.append("ordered phoneme/syllable labels unavailable")
    elif pathlib.Path(case.get("source",{}).get("path","")).is_file() and values[2]>pathlib.Path(case["source"]["path"]).stat().st_size//4:
        reasons.append("labels exceed declared F32 source duration")
    if not labels.get("evidence_identity") or labels.get("uncertainty_frames") is None:
        reasons.append("annotation provenance/uncertainty unavailable")
    elif not isinstance(labels["uncertainty_frames"],int) or isinstance(labels["uncertainty_frames"],bool) or labels["uncertainty_frames"]<0:
        reasons.append("invalid annotation uncertainty")
    if labels.get("no_near_speech_before_onset") is not True:
        reasons.append("far-only prefix is not independently verified")
    policy=case.get("application_policy",{})
    numeric=lambda v:isinstance(v,(float,int)) and not isinstance(v,bool) and math.isfinite(v) and v>=0
    if not policy.get("approved_by") or not all(numeric(policy.get(k)) for k in ("maximum_vad_delay_ms","maximum_asr_wer_delta")):
        reasons.append("downstream-approved onset budget unavailable")
    review=case.get("independent_output_review",{})
    if not review.get("reviewer") or not all(isinstance(review.get(k),bool) for k in ("first_phone_preserved","first_syllable_preserved")):
        reasons.append("independent processed phoneme/syllable review unavailable")
    if case.get("fixture_kind")=="physical" and not case.get("calibration_receipt_sha256"):
        reasons.append("physical clock/route calibration unavailable")
    asr=case.get("independent_asr_evaluation") or {}
    if not all(asr.get(k) for k in ("model_revision","reference_transcript_sha256","execution_evidence_identity")) or not all(numeric(asr.get(k)) for k in ("baseline_wer","processed_wer")):
        reasons.append("independent recognizer/transcript evaluation unavailable")
    return reasons


def score(case):
    reasons=admission(case)
    if reasons: return {"verdict":"UNQUALIFIED","reasons":reasons,"measurements":None}
    rate=case["sample_rate"]; onset=case["labels"]["onset_frame"]
    rows=list(csv.DictReader(open(case["vad_trace"]["path"])))
    expected=0
    for row in rows:
        if int(row["first_frame"])!=expected or int(row["frames"])<=0 or int(row["active"]) not in (0,1):
            return {"verdict":"UNQUALIFIED","reasons":["invalid VAD coverage/identity"],"measurements":None}
        expected+=int(row["frames"])
    if expected!=pathlib.Path(case["processed"]["path"]).stat().st_size//4:
        return {"verdict":"UNQUALIFIED","reasons":["VAD coverage does not match processed PCM"],"measurements":None}
    previous=[r for r in rows if int(r["first_frame"])+int(r["frames"])<=onset]
    false_events=sum(int(r["active"]) and (i==0 or not int(previous[i-1]["active"])) for i,r in enumerate(previous))
    active_at_onset=bool(previous and int(previous[-1]["active"]))
    detected=next((int(r["first_frame"])+int(r["frames"]) for r in rows
                   if int(r["first_frame"])+int(r["frames"])>onset and int(r["active"])),None)
    delay=(detected-onset)*1000/rate if detected is not None and not active_at_onset else None
    review=case["independent_output_review"]
    upper=delay+case["labels"]["uncertainty_frames"]*1000/rate if delay is not None else None
    asr=case["independent_asr_evaluation"]
    passed=(upper is not None and upper<=case["application_policy"]["maximum_vad_delay_ms"] and
            review["first_phone_preserved"] and review["first_syllable_preserved"] and not false_events)
    passed=passed and asr["processed_wer"]-asr["baseline_wer"]<=case["application_policy"]["maximum_asr_wer_delta"]
    verdict=("PASS" if passed else "FAIL") if case["fixture_kind"]!="synthetic-self-test" else "SELFTEST_ONLY"
    return {"verdict":verdict,"reasons":[],"measurements":{
        "vad_detection_delay_ms_frame_end":delay,"vad_delay_upper_bound_ms":upper,"vad_already_active_at_onset":active_at_onset,
        "false_bargein_events_before_onset":false_events,"first_phone_preserved":review["first_phone_preserved"],
        "first_syllable_preserved":review["first_syllable_preserved"],
        "asr_word_error_impact":case.get("independent_asr_evaluation"),
        "note":"Energy/projection cannot establish phoneme intelligibility"}}


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("case",type=pathlib.Path); p.add_argument("report",type=pathlib.Path)
    a=p.parse_args(); case=json.loads(a.case.read_text(encoding="utf-8"))
    result=score(case); a.report.write_text(json.dumps(result,indent=2)+"\n")
    if result["verdict"]!="PASS": raise SystemExit(result["verdict"]+": report retained")
