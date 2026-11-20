"""Device-free executable regression for external qualification tools.

No models/downloads/hardware and no performance acceptance from shared CI hosts.
The deliberately unreviewed onset manifest must never issue a qualification PASS.
"""
import array
import csv
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import types

root=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/"tools"))
import onset_qualification
import inference_contention

runner,vad=map(lambda x:pathlib.Path(x).resolve(),sys.argv[1:3])
parent=root/"build-acoustic-tests"; parent.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="owned-",dir=parent) as temporary:
    d=pathlib.Path(temporary).resolve()
    assert d.is_relative_to(parent.resolve()) # Scope automatic recursive cleanup.
    # A ready signal is insufficient: a later worker failure must fail the trial.
    worker=types.SimpleNamespace(exitcode=0); receipt=d/"whisper.json"
    receipt.write_text(json.dumps({"error":"late inference failure"}))
    assert inference_contention.worker_receipt_error(["whisper"],[worker],d)
    receipt.write_text(json.dumps({"events":[]}))
    assert inference_contention.worker_receipt_error(["whisper"],[worker],d)
    receipt.write_text(json.dumps({"events":[{"decode_ms":1}]}))
    assert inference_contention.worker_receipt_error(["whisper"],[worker],d) is None
    mic=d/"mic.f32"; ref=d/"ref.f32"; out=d/"out.f32"; raw=d/"raw.csv"; summary=d/"summary.json"
    x=array.array("f",[0]*161); x[-1]=.2
    if sys.byteorder!="little": x.byteswap()
    mic.write_bytes(x.tobytes()); ref.write_bytes(bytes(161*4))
    base=[str(runner),"baffle","16000",str(mic),str(ref),str(out),str(raw),str(summary)]
    subprocess.run(base,check=True)
    assert out.stat().st_size==161*4
    receipts=list(csv.DictReader(raw.open())); assert len(receipts)==2
    assert all(int(r["processing_ns"])>=0 for r in receipts)
    pipeline=base.copy(); pipeline[1]="pipeline"
    subprocess.run(pipeline,check=True)
    assert out.stat().st_size==(161+160)*4
    samples=array.array("f"); samples.frombytes(out.read_bytes())
    if sys.byteorder!="little": samples.byteswap()
    assert all(v==0 for v in samples[:160])
    assert abs(samples[-1])>1e-5 # EOS must deliver the final real input impulse.
    trace=d/"vad.csv"; subprocess.run([str(vad),"16000",str(out),str(trace)],check=True)
    rows=list(csv.DictReader(trace.open())); assert sum(int(r["frames"]) for r in rows)==321
    before=hashlib.sha256(trace.read_bytes()).hexdigest()
    assert subprocess.run([str(vad),"16000",str(out),str(trace)],capture_output=True).returncode!=0
    assert hashlib.sha256(trace.read_bytes()).hexdigest()==before
    invalid=d/"invalid.f32"; invalid.write_bytes(b"abc")
    bad=base.copy(); bad[3]=str(invalid)
    assert subprocess.run(bad,capture_output=True).returncode!=0
    case=json.loads((root/"tests/fixtures/onset-review.example.json").read_text())
    result=onset_qualification.score(case)
    assert result["verdict"]=="UNQUALIFIED" and result["measurements"] is None
    if len(sys.argv)>3:
        acquire=str(pathlib.Path(sys.argv[3]).resolve()); stereo=d/"stereo.f32"; stereo.write_bytes(bytes(48000*8))
        for behavior in ("stop","restart"):
            prefix=d/behavior
            subprocess.run([acquire,"1","-","-",str(stereo),"1",str(prefix),"100",behavior],check=True)
            metadata=[json.loads(line) for line in pathlib.Path(str(prefix)+".jsonl").read_text().splitlines()]
            session=metadata[0]; assert session["virtual"] and session["capture_frames"]>0
            assert session["capture_lost_frames"]==0 and session["reference_lost_frames"]==0
            capture=[r for r in metadata if r["type"]=="block" and r["leg"]==0]
            assert sum(r["frames"] for r in capture)==session["capture_frames"]
            assert len({r["epoch"] for r in capture})==1
            events=[r for r in metadata if r["type"]=="event"]
            assert any(r["state"]==4 and r["generation"]==2 and r["result"]==0 for r in events)
print("PASS raw replay/EOS, VAD receipts, retained-output guard and unreviewed-onset rejection; optional virtual cancellation")
