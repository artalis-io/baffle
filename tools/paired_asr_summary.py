"""Aggregate paired edit counts and preregistered engineering checks, no tuning."""
import argparse,json,pathlib


def group(s):
    if s.startswith("near-"):return "near-only"
    if s in ("far-only","silence"):return s
    if s in ("cancel-span","post-cancel","render-restart"):return "cancellation"
    if s in ("capture-epoch","missing-reference"):return "recovery"
    return "doubletalk"


def summarize(records,policy):
    aggregates=[];checks=[]
    for kind in ("whisper","parakeet"):
        for stage in ("raw","baffle","aec-ns","aec-ns-agc","pipeline"):
            selected=[r for r in records if r["recognizer"]==kind and r["stage"]==stage]
            for name in ("near-only","doubletalk","cancellation","recovery","far-only","silence"):
                rows=[r for r in selected if group(r["scenario"])==name]
                if not rows:continue
                row={"recognizer":kind,"stage":stage,"group":name,"cases":len(rows),
                    "empty_hypotheses":sum(r["empty"] for r in rows),
                    "new_empty_speech_vs_raw":sum(bool(r["reference"]) and r["empty"] and not r["raw_already_empty"] for r in rows),
                    "first_word_accuracy":sum(bool(r["first_word_correct"]) for r in rows)/len(rows) if rows[0]["reference"] else None,
                    "false_transcription_cases":sum(not r["empty"] for r in rows) if not rows[0]["reference"] else None}
                for metric in ("word","character"):
                    counts={k:sum(r[metric][k] for r in rows) for k in ("errors","substitutions","deletions","insertions","reference_length")}
                    counts["rate"]=counts["errors"]/counts["reference_length"] if counts["reference_length"] else None
                    row[metric]=counts
                aggregates.append(row)
            if stage=="raw":continue
            regressions=[r["case"] for r in selected if group(r["scenario"])=="near-only" and r["added_word_errors_vs_raw"]>policy["near_only_max_added_word_errors_per_case"]]
            new_blanks=[r["case"] for r in selected if r["reference"] and r["empty"] and not r["raw_already_empty"]]
            dt=[r for r in selected if group(r["scenario"])=="doubletalk"]
            raw_dt=[r for r in records if r["recognizer"]==kind and r["stage"]=="raw" and group(r["scenario"])=="doubletalk"]
            dt_better=sum(r["word"]["errors"] for r in dt)<sum(r["word"]["errors"] for r in raw_dt)
            false_cases=sum(not r["empty"] for r in selected if r["scenario"]=="far-only")
            checks.append({"recognizer":kind,"stage":stage,"near_only_regressions":regressions,"new_blank_speech_cases":new_blanks,
                "near_nonregression":"FAIL" if regressions else "PASS",
                "no_new_blanks":"FAIL" if len(new_blanks)>policy["maximum_added_empty_speech_cases"] else "PASS",
                "pooled_doubletalk_error_reduction":"PASS" if dt_better else "FAIL",
                "far_only_zero_transcription":"PASS" if false_cases<=policy["far_only_target_nonempty"] else "FAIL"})
    return {"groups":aggregates,"engineering_checks":checks,"gate_h":"PARTIAL",
        "limits":"Conservative zero-added-error/blank checks; not app-approved onset or physical qualification. No WER/CER is defined for empty references; insertions and false-transcription counts remain explicit."}


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for k in ("manifest","results","output"):p.add_argument(k,type=pathlib.Path)
    a=p.parse_args();m=json.loads(a.manifest.read_text());r=json.loads(a.results.read_text())
    a.output.write_text(json.dumps(summarize(r["results"],m["policy"]),indent=2)+"\n")
