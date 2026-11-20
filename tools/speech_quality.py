"""Recorded-source, simulated-acoustic AEC evaluation. No downloads or models.

OSR voices are evaluation holdouts, separate from the Microsoft tuning fixture.
Scores use the original near component including any recording noise. This is
not physical acoustics, perceptual quality, clean studio speech or ASR WER.
"""
import argparse
import array
import hashlib
import json
import math
import pathlib
import subprocess

import recorded_aec as wav

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = [
    ("OSR_us_000_0010_8k.wav", "a4bf9becd046d7aedb6d05b6e12347a6294a44f74d263089c636fb0a2b1e6561"),
    ("OSR_us_000_0030_8k.wav", "d3e1cfba98a44ddfc6e99508e663794c8dc41bdb2baced224f779c453143b622"),
]


def power(x):
    return sum(v*v for v in x)


def db_ratio(a, b):
    return 10*math.log10(max(a, 1e-30)/max(b, 1e-30))


def preservation(target, output):
    n = len(target)
    tm, om = sum(target)/n, sum(output)/n
    t = [v-tm for v in target]
    o = [v-om for v in output]
    gain = sum(a*b for a,b in zip(t,o))/max(power(t),1e-30)
    return {"projection": gain, "raw_error_snr_db": db_ratio(power(target), power([a-b for a,b in zip(target,output)])),
            "si_sdr_db": db_ratio(gain*gain*power(t), power([b-gain*a for a,b in zip(t,o)]))}


def run(tool, directory):
    sources, identities = [], []
    for name, digest in FIXTURES:
        p = ROOT/"tests/fixtures/osr"/name
        if hashlib.sha256(p.read_bytes()).hexdigest() != digest:
            raise ValueError("fixture checksum mismatch")
        rate, samples = wav.read(p)
        if rate != 8000:
            raise ValueError("expected pinned 8 kHz source")
        peak = max(abs(s) for s in samples)
        sources.append([v/peak for v in samples])
        identities.append({"file":name,"sha256":digest,"frames":len(samples),"rate":rate})
    rate, n = 8000, 24*8000
    # Fixed before inspecting holdout outcomes; no score-driven segmentation.
    scenarios = [("near-only",.2,0,0), ("doubletalk",.2,.25,0),
                 ("low-near",.02,.25,0), ("high-near",.6,.15,0),
                 ("room-noise-path-change",.2,.25,.001),
                 ("playback-gap",.2,.25,0)]
    cases = []
    for swap in (0,1):
        for name, near_peak, far_peak, noise_level in scenarios:
            far = [sources[1-swap][i % len(sources[1-swap])]*far_peak for i in range(n)]
            if name == "playback-gap":
                far[12*rate:14*rate] = [0.]*(2*rate)
            target = [sources[swap][(i-8*rate) % len(sources[swap])]*near_peak
                      if 8*rate <= i < 18*rate else 0. for i in range(n)]
            taps = [(48,.6),(109,-.15)]
            if name == "room-noise-path-change":
                taps += [(160,.1),(233,-.07),(371,.05),(517,-.03),(683,.02)]
            echo = [sum(g*far[i-delay-(80 if name == "room-noise-path-change" and i >= 18*rate else 0)]
                        for delay,g in taps if i >= delay+(80 if name == "room-noise-path-change" and i >= 18*rate else 0))
                    for i in range(n)]
            rng = 721
            capture = []
            for i in range(n):
                rng = (1664525*rng+1013904223) & 0xffffffff
                capture.append(target[i]+echo[i]+noise_level*((rng>>8)/8388608-1))
            if max(abs(v) for v in capture) >= .99:
                raise ValueError("fixture would clip")
            key = f"voice{swap}-{name}"
            mic, ref, out = [directory/(key+suffix+".wav") for suffix in ("-mic","-ref","-out")]
            wav.write(mic,rate,array.array("h",(round(v*32768) for v in capture)))
            wav.write(ref,rate,array.array("h",(round(v*32768) for v in far)))
            for hop in (10,20):
                command = [str(tool.resolve()),str(mic),str(ref),str(out),str(6*rate),str(2*rate),str(rate),"200",str(hop)]
                result = subprocess.run(command, capture_output=True, text=True, check=True)
                output_rate, pcm = wav.read(out)
                if output_rate != rate or len(pcm) != n:
                    raise ValueError("output accounting mismatch")
                output = [v/32768 for v in pcm]
                begin,end = 8*rate,18*rate
                quality = preservation(target[begin:end],output[begin:end])
                before = preservation(target[begin:end],capture[begin:end])
                quality["raw_snr_improvement_db"] = (quality["raw_error_snr_db"]-before["raw_error_snr_db"]
                                                       if far_peak or noise_level else None)
                # All one-second windows retained, including weak/silent source periods.
                windows = []
                for sec in range(24):
                    a,b = sec*rate,(sec+1)*rate
                    residual = power(output[a:b])/rate
                    windows.append({"second":sec,"input_power":power(capture[a:b])/rate,
                                    "output_power":residual,
                                    "far_only_mic_to_output_ratio_db":db_ratio(power(capture[a:b]),power(output[a:b]))
                                      if far_peak and not 8 <= sec < 18 else None})
                # First 100 ms whose near component has mean square above -60 dBFS.
                onset = next((i for i in range(begin,end-800+1,80) if power(target[i:i+800])/800 > 1e-6),None)
                quality["first_energy_100ms"] = preservation(target[onset:onset+800],output[onset:onset+800]) if onset is not None else None
                quality["first_energy_frame"] = onset
                cases.append({"case":key,"hop_ms":hop,"near_peak":near_peak,"far_peak":far_peak,
                              "noise_peak":noise_level,"taps":taps,"path_step_samples_at_18s":80 if name == "room-noise-path-change" else 0,
                              "doubletalk_interval_frames":[begin,end],"quality":quality,
                              "near_projection_within_original_bounds":.8<=quality["projection"]<=1.2,
                              "windows":windows,"command":command,"native_stdout":result.stdout,
                              "capture_sha256":hashlib.sha256(mic.read_bytes()).hexdigest(),
                              "reference_sha256":hashlib.sha256(ref.read_bytes()).hexdigest(),
                              "output_sha256":hashlib.sha256(out.read_bytes()).hexdigest()})
    return {"scope":"Recorded English OSR source components with simulated echo/noise; not physical acoustics",
            "fixtures":identities,"tool_sha256":hashlib.sha256(tool.read_bytes()).hexdigest(),
            "holdout_policy":"20 Hz policy fixed before inspecting OSR results; no tuning on these cases",
            "onset_note":"First energy >-60dBFS window can include pre-speech noise/DC; not annotated speech onset, VAD or ASR accuracy",
            "far_only_note":"Mic/output power ratio includes injected and recorded noise; total output power is not isolated residual echo",
            "physical_qualification":False,"hungarian": "unavailable: no reviewed fixture included",
            "asr_wer":"unavailable: no external recognizer configured","cases":cases}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("tool",type=pathlib.Path)
    p.add_argument("directory",type=pathlib.Path)
    p.add_argument("report",type=pathlib.Path)
    p.add_argument("--assert-preservation",action="store_true")
    args = p.parse_args()
    args.directory.mkdir(parents=True,exist_ok=True)
    report = run(args.tool,args.directory)
    args.report.write_text(json.dumps(report,indent=2)+"\n")
    if args.assert_preservation and not all(c["near_projection_within_original_bounds"] for c in report["cases"]):
        raise SystemExit("Recorded-source projection outside unchanged .8..1.2 bounds; report retained")


if __name__ == "__main__":
    main()
