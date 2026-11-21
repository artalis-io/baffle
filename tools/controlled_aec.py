"""Optional recorded-speech / synthetic-echo diagnostic, not acoustic qualification.

Uses the same pinned dataset/cache rules as recorded_aec.py. The original near
recording, including its noise, is a known pre-mixture component. A separate
zero-reference control measures the backend's own signal conditioning.
"""
import argparse
import array
import hashlib
import json
import math
import pathlib
import subprocess

import recorded_aec as recorded


def measure(tool, cache, fetch):
    near_path, near_origin = recorded.fetch(
        "nearend-singletalk/03d6ced2-3cc1-4b32-bcea-770fe2d260ff_nearend-singletalk_mic.wav",
        cache, fetch)
    far_path, far_origin = recorded.fetch(
        "farend-singletalk/00113069-3e44-460a-be8d-9c252979a801_farend-singletalk_lpb.wav",
        cache, fetch)
    rate, near = recorded.read(near_path)
    far_rate, original_far = recorded.read(far_path)
    if rate != 48000 or far_rate != rate:
        raise ValueError("expected fixed 48k fixture")
    n = min(len(original_far), 24 * rate)
    far_scale = .25 / max(abs(v)/32768 for v in original_far)
    near_scale = .20 / max(abs(v)/32768 for v in near)
    far = [v/32768*far_scale for v in original_far[:n]]
    oracle = [near[(i-8*rate+5*rate) % len(near)]/32768*near_scale
              if 8*rate <= i < 18*rate else 0 for i in range(n)]
    echo = [.6*(far[i-32] if i >= 32 else 0)-.15*(far[i-93] if i >= 93 else 0)
            for i in range(n)]
    capture = [oracle[i]+echo[i] for i in range(n)]
    if max(abs(v) for v in capture) >= .5:
        raise ValueError("fixture must not clip")

    def quantize(values):
        return array.array("h", (max(-32768, min(32767, round(v*32768))) for v in values))

    def process(name, microphone, reference):
        capture_path, reference_path = cache/(name+"-mic.wav"), cache/(name+"-ref.wav")
        output_path = cache/(name+"-out.wav")
        recorded.write(capture_path, rate, quantize(microphone))
        recorded.write(reference_path, rate, quantize(reference))
        subprocess.run([str(tool.resolve()), str(capture_path), str(reference_path),
                        str(output_path)], check=True, capture_output=True, text=True)
        output_rate, pcm = recorded.read(output_path)
        if output_rate != rate or len(pcm) != n:
            raise ValueError("output format/count mismatch")
        return pcm

    output = process("controlled-doubletalk", capture, far)
    baseline = process("controlled-near", oracle, array.array("h", [0])*n)
    begin, end = 8*rate, 18*rate
    power = sum(v*v for v in oracle[begin:end])
    projection = sum(output[i]/32768*oracle[i] for i in range(begin,end))/power
    before = sum((capture[i]-oracle[i])**2 for i in range(begin,end))
    after = sum((output[i]/32768-oracle[i])**2 for i in range(begin,end))
    control_error = sum((baseline[i]/32768-oracle[i])**2 for i in range(begin,end))
    relative = sum(((output[i]-baseline[i])/32768)**2 for i in range(begin,end))
    far_input = sum(echo[i]**2 for i in range(6*rate,8*rate))
    far_output = sum((output[i]/32768)**2 for i in range(6*rate,8*rate))
    improvement = 10*math.log10(before/after)
    return {
        "scope": "CONTROLLED SAME-CLOCK synthetic linear echo with recorded speech; not physical acoustics",
        "upstream_pin": recorded.PIN, "near_source": near_origin, "far_source": far_origin,
        "native_tool_sha256": hashlib.sha256(tool.read_bytes()).hexdigest(),
        "rate": rate, "frames": n, "doubletalk_interval": [begin,end],
        "near_source_offset_frames": 5*rate, "far_peak": .25, "near_peak": .2,
        "echo_taps": [[32,.6],[93,-.15]], "aec_tail_ms": 200,
        "oracle": "Known original near recording including noise, before mixture/file quantization",
        "speech_component_projection": projection,
        "component_snr_before_db": 10*math.log10(power/before),
        "component_snr_after_db": 10*math.log10(power/after),
        "snr_improvement_db": improvement,
        "far_only_erle_db": 10*math.log10(far_input/far_output),
        "zero_reference_control_snr_db": 10*math.log10(power/control_error),
        "error_snr_relative_to_backend_processed_oracle_db": 10*math.log10(power/relative),
        "initial_raw_targets": {"projection": [.8,1.2], "snr_improvement_db_min": 8},
        "raw_target_met": .8 <= projection <= 1.2 and improvement >= 8,
        "diagnostic_note": "Raw targets are retained; control isolates Speex DC-notch/preemphasis/deemphasis response",
        "physical_acoustic_qualification": False, "no_audio_redistributed": True,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("tool", type=pathlib.Path)
    p.add_argument("cache", type=pathlib.Path)
    p.add_argument("report", type=pathlib.Path)
    p.add_argument("--fetch", action="store_true")
    p.add_argument("--assert-raw-target", action="store_true")
    args = p.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    report = measure(args.tool, args.cache, args.fetch)
    args.report.write_text(json.dumps(report, indent=2)+"\n")
    if args.assert_raw_target and not report["raw_target_met"]:
        raise SystemExit("Raw-component target not met; see the retained result and control diagnostic")


if __name__ == "__main__":
    main()
