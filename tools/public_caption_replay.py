"""Public recorded-speech replay of native caption applications; external QA.

Uses pinned public FLEURS fixtures already prepared by asr_cloud_compare.py.
No microphone, playback device or cloud call is used. Native replay applications
run actual model/worker/window/stitching code with a paced file-fed source seam.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import time

import soundfile as sf
from asr_metrics import metrics


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf8")


def checked(root, name, expected):
    p = (root / name).resolve()
    if not p.is_relative_to(root.resolve()) or sha(p) != expected:
        raise ValueError("Public fixture identity mismatch")
    return p


def report_text(text):
    match = re.search(r'^text=(.*?)(?=^[a-z_]+=|\Z)', text, re.M | re.S)
    return match.group(1).strip() if match else ""


def trace_text(text):
    # C history is a multiline field; the old C++ app emits one HISTORY per segment.
    parts = re.findall(r'^HISTORY=(.*?)(?=^(?:HISTORY|DROPPED|STATUS)=|\Z)', text, re.M | re.S)
    hypothesis = " ".join(p.strip() for p in parts)
    hypothesis = re.sub(r'\[(?:audio gap|hangkimaradás)\]', '', hypothesis).strip()
    drops = re.search(r'^DROPPED=(\d+)', text, re.M)
    decodes = []
    for m in re.finditer(r'^DECODE frames=(\d+) sha256=([a-f0-9]{64}) last_two=([^ ]+) text=(.*?)(?=^(?:DECODE frames=|HISTORY=|DROPPED=|STATUS=)|\Z)', text, re.M | re.S):
        decodes.append({"frames": int(m[1]), "pcm_sha256": m[2], "last_two_start": float(m[3]), "hypothesis": m[4].strip()})
    if not drops or not re.search(r"^STATUS=", text, re.M):
        raise ValueError("Incomplete native replay receipt")
    return hypothesis, int(drops[1]), decodes


def execute(command, report, timeout):
    if report.exists():
        raise ValueError("Previous native report must be retained")
    begin = time.perf_counter()
    result = subprocess.run([str(x) for x in command], capture_output=True, timeout=timeout)
    allowed = (0, 2) if "--self-test" in [str(x) for x in command] else (0,)
    if result.returncode not in allowed or not report.exists():
        raise RuntimeError(f'Native diagnostic failed: exit={result.returncode}, report={report.name}')
    return report.read_text(encoding="utf8"), (time.perf_counter() - begin) * 1000


def run(a):
    if a.output.exists() and any(a.output.iterdir()):
        raise ValueError("Use a fresh campaign directory")
    a.output.mkdir(parents=True, exist_ok=True)
    input_dir = a.public_manifest.parent
    public = json.loads(a.public_manifest.read_text(encoding="utf8"))
    if public.get("dataset") != "google/fleurs" or public.get("revision") != "70bb2e84b976b7e960aa89f1c648e09c59f894dd":
        raise ValueError("Expected the pinned public FLEURS pilot")
    manifest = {"public_manifest_sha256": sha(a.public_manifest), "stage": a.stage,
                "source_license": public["license"], "source_revision": public["revision"],
                "recordings": [], "executables": {k: {"path": str(p.resolve()), "sha256": sha(p)} for k, p in
                 (("cpp", a.cpp_exe), ("c11", a.c_exe), ("replay_cpp128", a.cpp_replay),
                  ("replay_c128", a.c128_replay), ("replay_current512", a.c_replay))},
                "models": {p.name: sha(p) for p in a.models.glob('*') if p.is_file() and p.suffix in ('.onnx','.txt')},
                "scope": "Raw or fixed NS PCM, paced at 160 samples/10 ms into actual native caption workers. Capture seam replaced; no hardware/cloud.",
                "references": "Independent publisher transcripts; not newly reviewed by a human; numeric/compound formatting retained in error counts"}
    public["sources"] = [s for s in public["sources"] if s["language"] in a.languages]
    manifest["selected_languages"] = a.languages
    rows = []
    for source in public["sources"]:
        if not re.fullmatch(r"(?:en|hu)-[0-9]{2}", source["name"]):
            raise ValueError("Invalid public case identifier")
        jobs = {j["stage"]: j for j in public["jobs"] if j["source"] == source["name"] and j["condition"] == 'clean'}
        raw, rate = sf.read(checked(input_dir, jobs['raw']['wav'], jobs['raw']['wav_sha256']), dtype='float32')
        stage = 'baffle' if a.stage == 'ns' else 'raw'
        supplied, other_rate = sf.read(checked(input_dir, jobs[stage]['wav'], jobs[stage]['wav_sha256']), dtype='float32')
        if rate != 16000 or other_rate != rate or len(raw) != len(supplied):
            raise ValueError("Matched 16k PCM required")
        # Remove only the preparation's documented 500 ms pad at each end.
        raw = raw[8000:-8000]
        supplied = supplied[8000:-8000]
        if not 0 < len(raw) <= 320000:
            raise ValueError("Native replay accepts bounded 0..20 second source clips")
        case_dir = a.output / source["name"]
        case_dir.mkdir()
        raw_path, supplied_path = case_dir/'raw.wav', case_dir/'input.wav'
        sf.write(raw_path, raw, rate, subtype='PCM_16')
        sf.write(supplied_path, supplied, rate, subtype='PCM_16')
        identity = {"case": source['name'], "language": source['language'], "stage": a.stage,
                    "reference": source['publisher_reference'], "source_dataset_id": source['dataset_id'],
                    "source_pcm_sha256": sha(raw_path), "supplied_wav_sha256": sha(supplied_path),
                    "frames": len(raw), "duration_s": len(raw)/rate}
        manifest['recordings'].append(identity)
        dump(a.output/'manifest.json', manifest)
        for name, exe in (("whole_cpp", a.cpp_exe), ("whole_c11", a.c_exe)):
            receipt = case_dir/(name+'.txt')
            text, wall = execute([exe, '--models', a.models.resolve(), '--self-test', supplied_path.resolve(), '--report', receipt.resolve()], receipt, 60)
            hypothesis = report_text(text)
            if not re.search(r"^text=", text, re.M):
                raise ValueError("Whole-file receipt has no transcription result")
            rows.append({**identity, "path": name, "hypothesis": hypothesis,
                         "metrics": metrics(identity['reference'], hypothesis), "process_wall_ms_including_model_init": wall,
                         "receipt": str(receipt.relative_to(a.output)), "receipt_sha256": sha(receipt)})
        for name, exe in (("live_cpp128", a.cpp_replay), ("live_c128", a.c128_replay), ("live_current512", a.c_replay)):
            receipt = case_dir/(name+'.txt')
            command=[exe, raw_path.resolve(), supplied_path.resolve(), a.models.resolve(), receipt.resolve()]
            if name=='live_current512' and a.current_profile_auto:
                command.append('raw' if a.stage=='raw' else 'ns')
            text, wall = execute(command, receipt, 90)
            hypothesis, dropped, decodes = trace_text(text)
            rows.append({**identity, "path": name, "hypothesis": hypothesis,
                         "metrics": metrics(identity['reference'], hypothesis), "dropped_blocks": dropped,
                         "decode_windows": decodes, "process_wall_ms_including_model_init": wall,
                         "receipt": str(receipt.relative_to(a.output)), "receipt_sha256": sha(receipt)})
            print(source['name'], name, 'word_errors=', rows[-1]['metrics']['word']['errors'], 'dropped=', dropped, flush=True)
        dump(a.output/'results.json', {"manifest_sha256": sha(a.output/'manifest.json'), "rows": rows})
    paired = []
    for source in public['sources']:
        selected = {r['path']: r for r in rows if r['case'] == source['name']}
        pair = {"case": source['name'], "whole_file_hypotheses_identical": selected['whole_cpp']['hypothesis']==selected['whole_c11']['hypothesis'],
                "cpp_vs_matched_c128_text_identical": selected['live_cpp128']['hypothesis']==selected['live_c128']['hypothesis'],
                "cpp_vs_current_text_identical": selected['live_cpp128']['hypothesis']==selected['live_current512']['hypothesis']}
        by_input = {}
        for path in ('live_cpp128','live_c128','live_current512'):
            for call in selected[path]['decode_windows']:
                by_input.setdefault(call['pcm_sha256'], []).append((path,call['hypothesis']))
        cross = [calls for calls in by_input.values() if len({p for p,_ in calls})>1]
        pair['shared_exact_audio_windows'] = len(cross)
        pair['shared_window_hypothesis_mismatches'] = sum(len({t for _,t in calls})>1 for calls in cross)
        paired.append(pair)
    dump(a.output/'results.json', {"manifest_sha256": sha(a.output/'manifest.json'), "rows": rows, "paired": paired})
    print(json.dumps(paired,indent=2),flush=True)


def control_inputs(campaigns):
    """Only completed cases; a crashed campaign may have one extra manifest entry."""
    selected = {}
    for campaign in campaigns:
        results = json.loads((campaign / 'results.json').read_text(encoding='utf8'))
        for row in results['rows']:
            if not re.fullmatch(r"(?:en|hu)-[0-9]{2}", row["case"]):
                raise ValueError("Invalid replay case identifier")
            key = (row['case'], row['stage'])
            if key in selected:
                if selected[key][0]['supplied_wav_sha256'] != row['supplied_wav_sha256']:
                    raise ValueError('Cannot deduplicate different input PCM')
                continue
            wav = (campaign / row['case'] / 'input.wav').resolve()
            if not wav.is_relative_to(campaign.resolve()) or sha(wav) != row['supplied_wav_sha256']:
                raise ValueError('Completed replay input hash/path mismatch')
            selected[key] = (row, wav)
    return list(selected.values())


def run_controls(a):
    import numpy as np
    if a.output.exists() and any(a.output.iterdir()):
        raise ValueError('Use a fresh controls directory')
    selected = control_inputs(a.controls_from)
    if a.cases:
        requested = set(a.cases)
        if requested - {r['case'] for r, _ in selected}:
            raise ValueError('Unknown control case')
        selected = [(r, w) for r, w in selected if r['case'] in requested]
    a.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for original, path in selected:
        x, rate = sf.read(path, dtype='float32')
        total = ((len(x) + 159) // 160 + 200) * 160
        padded = np.pad(x, (16000, total - 16000 - len(x)))
        if rate != 16000 or x.ndim != 1 or len(padded) > 320000:
            raise ValueError('Control exceeds native mono 16k/20-second WAV limit')
        directory = a.output / (original['case'] + '-' + original['stage'])
        directory.mkdir()
        baseline, scaled = directory / 'padded.wav', directory / 'level.wav'
        sf.write(baseline, padded, rate, subtype='PCM_16')
        gain = .5 / max(float(np.max(np.abs(padded))), 1e-20)
        sf.write(scaled, padded * gain, rate, subtype='PCM_16')
        for profile, exe, wav in [('padded_cpp', a.cpp_exe, baseline), ('padded_c11', a.c_exe, baseline),
                                   ('level_c11', a.c_exe, scaled)]:
            receipt = directory / (profile + '.txt')
            text, wall = execute([exe, '--models', a.models.resolve(), '--self-test', wav.resolve(),
                                  '--report', receipt.resolve()], receipt, 60)
            hypothesis = report_text(text)
            row = {k: original[k] for k in ('case', 'language', 'stage', 'reference', 'source_dataset_id')}
            row.update(path=profile, hypothesis=hypothesis, metrics=metrics(row['reference'], hypothesis),
                       wav_sha256=sha(wav), frames=len(padded), gain=gain if profile == 'level_c11' else 1,
                       wall_ms_including_model_init=wall, receipt=str(receipt.relative_to(a.output)))
            rows.append(row)
            print(row['case'], profile, row['metrics']['word']['errors'], 'blank=', not hypothesis, flush=True)
            dump(a.output / 'results.json', {'scope': 'Exploratory one-second digital padding and offline peak normalization, not a proposed live DSP stage',
                                            'rows': rows})


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--public-manifest', type=pathlib.Path, required=True)
    p.add_argument('--models', type=pathlib.Path, required=True)
    p.add_argument('--cpp-exe', type=pathlib.Path, required=True)
    p.add_argument('--c-exe', type=pathlib.Path, required=True)
    p.add_argument('--cpp-replay', type=pathlib.Path)
    p.add_argument('--c128-replay', type=pathlib.Path)
    p.add_argument('--c-replay', type=pathlib.Path)
    p.add_argument('--current-profile-auto', action='store_true', help='Select raw/ns caption policy with current replay builds; omit for archived binaries')
    p.add_argument('--stage', choices=('raw','ns'), default='raw')
    p.add_argument('--languages', nargs='+', choices=('en','hu'), default=['en','hu'])
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--controls-from', type=pathlib.Path, nargs='+')
    p.add_argument('--cases', nargs='+')
    args = p.parse_args()
    if args.controls_from:
        run_controls(args)
    else:
        if args.cases:
            p.error("Case selection is only valid for input-control recovery")
        if not all((args.cpp_replay, args.c128_replay, args.c_replay)):
            p.error('Replay requires all three native replay executable paths')
        run(args)
