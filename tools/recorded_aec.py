"""Optional offline QA on pinned upstream real recordings; never redistributes audio.

Review upstream dataset terms before --fetch. Python/network are not core or CI
dependencies. These files have no clean near-end oracle or device timestamps.
"""
import argparse
import array
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import urllib.request
import wave

PIN = "8f861f0f8ffca26a3d1d16f17f2594ea89abe7ac"
CASES = [
    ("farend-singletalk", "00113069-3e44-460a-be8d-9c252979a801_farend-singletalk"),
    ("farend-singletalk", "00dPPLGk2UutnxcOfe5zTw_farend-singletalk-with-movement"),
    ("farend-singletalk", "02462e78-f05c-4ea4-be89-f69f283f6d1c_farend-singletalk"),
    ("doubletalk", "00113069-3e44-460a-be8d-9c252979a801_doubletalk"),
]


def fetch(path, cache, enabled):
    relative = f"datasets/test_set_icassp2022/{path}"
    raw = f"https://raw.githubusercontent.com/microsoft/AEC-Challenge/{PIN}/{relative}"
    pointer_path = cache / (pathlib.Path(path).name + ".lfs")
    if pointer_path.exists():
        pointer = pointer_path.read_text()
    else:
        if not enabled:
            raise SystemExit("pinned pointer missing: --fetch is required once")
        with urllib.request.urlopen(raw, timeout=30) as response:
            pointer = response.read(4096).decode()
        pointer_path.write_text(pointer)
    digest = re.search(r"oid sha256:([0-9a-f]{64})", pointer)[1]
    size = int(re.search(r"size (\d+)", pointer)[1])
    if size > 16 * 1024 * 1024:
        raise ValueError("fixture exceeds bounded 16 MiB download")
    local = cache / pathlib.Path(path).name
    if not local.exists():
        if not enabled:
            raise SystemExit("fixture missing: explicitly --fetch after reviewing upstream terms")
        media = f"https://media.githubusercontent.com/media/microsoft/AEC-Challenge/{PIN}/{relative}"
        with urllib.request.urlopen(media, timeout=30) as response:
            content = response.read(size + 1)
        if len(content) != size or hashlib.sha256(content).hexdigest() != digest:
            raise ValueError("LFS content does not match pinned pointer")
        local.write_bytes(content)
    if local.stat().st_size != size or hashlib.sha256(local.read_bytes()).hexdigest() != digest:
        raise ValueError("cached fixture hash mismatch")
    return local, {"path": relative, "sha256": digest, "bytes": size, "upstream_pointer": raw}


def read(path):
    with wave.open(str(path), "rb") as f:
        if f.getnchannels() != 1 or f.getsampwidth() != 2:
            raise ValueError("fixture must be mono PCM16")
        rate = f.getframerate()
        pcm = array.array("h", f.readframes(f.getnframes()))
    if sys.byteorder != "little":
        pcm.byteswap()
    return rate, pcm


def write(path, rate, pcm):
    copy = array.array("h", pcm)
    if sys.byteorder != "little":
        copy.byteswap()
    with wave.open(str(path), "wb") as f:
        f.setparams((1, 2, rate, len(copy), "NONE", "not compressed"))
        f.writeframes(copy.tobytes())


def coarse_delay(microphone, reference, rate, start_seconds=0):
    # First five seconds only: independent from the scored interval. Block-mean
    # 1 kHz correlation is a coarse signal lag estimate, not a clock calibration.
    stride = rate // 1000
    microphone = microphone[start_seconds*rate:]
    reference = reference[start_seconds*rate:]
    mic = [sum(microphone[i:i+stride])/stride for i in range(0, min(len(microphone), 6*rate)-stride, stride)]
    ref = [sum(reference[i:i+stride])/stride for i in range(0, min(len(reference), 5*rate)-stride, stride)]
    power = sum(x*x for x in ref)
    scores = []
    for lag in range(501):
        n = min(len(ref), len(mic)-lag)
        cross = sum(ref[i]*mic[i+lag] for i in range(n))
        mpower = sum(mic[i+lag]**2 for i in range(n))
        scores.append(abs(cross)/(max(power*mpower, 1e-30)**.5))
    lag = max(range(len(scores)), key=scores.__getitem__)
    return {"window_start_seconds": start_seconds,
            "coarse_positive_lag_ms": lag, "absolute_correlation": scores[lag],
            "resolution_ms": 1, "searched_ms": [0, 500], "input_was_shifted": False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("tool", type=pathlib.Path)
    p.add_argument("cache", type=pathlib.Path)
    p.add_argument("report", type=pathlib.Path)
    p.add_argument("--fetch", action="store_true")
    p.add_argument("--processing-rate", type=int, choices=[8000, 16000, 48000])
    args = p.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    report = {"dataset": "Microsoft AEC Challenge ICASSP2022 real test recordings",
              "upstream_commit": PIN, "terms": "https://github.com/microsoft/AEC-Challenge#dataset-licenses",
              "no_audio_redistributed": True, "cases": []}
    for scenario, name in CASES:
        microphone, mi = fetch(f"{scenario}/{name}_mic.wav", args.cache, args.fetch)
        reference, ri = fetch(f"{scenario}/{name}_lpb.wav", args.cache, args.fetch)
        rate, near = read(microphone)
        far_rate, far = read(reference)
        if rate != far_rate:
            raise ValueError("mismatched rates")
        count = min(len(near), len(far))
        # Explicit tail crop only; preserve index zero and do not align using
        # test-score feedback. Unavailable provider timing stays unavailable.
        capture_path, reference_path = args.cache / "capture-cropped.wav", args.cache / "reference-cropped.wav"
        write(capture_path, rate, near[:count])
        write(reference_path, rate, far[:count])
        command = [str(args.tool.resolve()), str(capture_path), str(reference_path), str(args.cache / (name+"-out.wav"))]
        start, frames = 6*rate, min(count, 22*rate)-6*rate
        if scenario == "farend-singletalk":
            command += [str(start), str(frames)]
            if args.processing_rate:
                command.append(str(args.processing_rate))
        result = subprocess.run(command, text=True, capture_output=True, check=True)
        erle = re.search(r"annotated_far_only_erle_db=([-\d.]+)", result.stdout)
        report["cases"].append({"scenario": name, "microphone": mi, "reference": ri,
            "rate": rate, "input_frames": [len(near), len(far)], "common_frames": count,
            "tail_crop_frames": [len(near)-count, len(far)-count],
            "coarse_lag_diagnostics": [coarse_delay(near, far, rate, second) for second in (0, 8, 16)],
            "processing_rate": (args.processing_rate or rate) if scenario == "farend-singletalk" else rate,
            "far_only_interval": [start, frames] if erle else None,
            "far_only_energy_reduction_db": float(erle[1]) if erle else None,
            "clean_near_end_preservation": "unavailable: no clean near-end oracle",
            "native_tool_stdout": result.stdout})
    args.report.write_text(json.dumps(report, indent=2)+"\n")


if __name__ == "__main__":
    main()
