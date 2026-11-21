"""Run the optional installed-Hail acquisition tool with owner-declared provenance.

Never changes endpoint settings or guesses a physical route. Raw PCM stays local.
Failure receipts are written even when native capture fails. An acquisition is
not calibrated acoustic qualification; presentation/silence remain unavailable.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("executable",type=pathlib.Path); p.add_argument("protocol",type=pathlib.Path)
    p.add_argument("output",type=pathlib.Path)
    a=p.parse_args(); protocol=json.loads(a.protocol.read_text(encoding="utf-8"))
    required=("fixture_kind","backend","capture_key","playback_key","stimulus_f32","duration_s",
              "hardware","os_driver","route_description","room_description","distance_m","playback_level",
              "capture_processing","calibration_receipt","cancel_ms","cancel_behavior")
    for key in required:
        if key not in protocol: raise SystemExit("missing provenance field: "+key)
    if protocol["fixture_kind"] not in ("physical-uncalibrated","electrical-loopback","software-loopback","virtual"):
        raise SystemExit("explicit fixture kind required")
    if (protocol["backend"]==1)!=(protocol["fixture_kind"]=="virtual"):
        raise SystemExit("virtual backend/kind mismatch")
    if protocol["fixture_kind"]!="virtual" and not all(protocol[k] for k in ("hardware","os_driver","route_description","room_description")):
        raise SystemExit("native acquisition requires declared hardware/route/room")
    a.output.mkdir(parents=True,exist_ok=True); prefix=a.output/"acquisition"
    stimulus=pathlib.Path(protocol["stimulus_f32"]).resolve()
    command=[str(a.executable.resolve()),str(protocol["backend"]),protocol["capture_key"],protocol["playback_key"],
             str(stimulus),str(protocol["duration_s"]),str(prefix),str(protocol["cancel_ms"]),protocol["cancel_behavior"]]
    report={"protocol":protocol,"command":command,"executable_sha256":hashlib.sha256(a.executable.read_bytes()).hexdigest(),
            "stimulus_sha256":hashlib.sha256(stimulus.read_bytes()).hexdigest(),
            "device_presentation_completion":None,"acoustic_silence":None,
            "qualification":"UNQUALIFIED: acquisition only; calibration and independent analysis required"}
    receipt=a.output/"receipt.json"; receipt.write_text(json.dumps(report,indent=2)+"\n")
    try:
        run=subprocess.run(command,capture_output=True,text=True,timeout=protocol["duration_s"]+30)
        report.update(returncode=run.returncode,stdout=run.stdout,stderr=run.stderr)
    except subprocess.TimeoutExpired as exc:
        report.update(returncode=None,error=str(exc),timeout=True)
    report["files"]={f.name:{"bytes":f.stat().st_size,"sha256":hashlib.sha256(f.read_bytes()).hexdigest()}
                     for f in a.output.glob("acquisition.*")}
    receipt.write_text(json.dumps(report,indent=2)+"\n")
    if report["returncode"]!=0: raise SystemExit("Acquisition failed; receipt retained")


if __name__=="__main__": main()
