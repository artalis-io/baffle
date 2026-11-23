"""Public native replay receipt regressions. No devices, models or network."""
import pathlib
import sys
root=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from public_caption_replay import trace_text,report_text
h='a'*64
text=f'DECODE frames=160 sha256={h} last_two=-1 text=Kérlek,\nállítsd le.\nHISTORY=Kérlek,\nállítsd le.\n\nHISTORY=Második mondat. [audio gap]\nDROPPED=2\nSTATUS=Listening\n'
hyp,drops,calls=trace_text(text)
assert hyp=='Kérlek,\nállítsd le. Második mondat.' and drops==2
assert calls[0]['hypothesis']=='Kérlek,\nállítsd le.' and calls[0]['pcm_sha256']==h
assert report_text('runtime=1\ntext=Első\nmásodik\ndecode_ms=10\n')=='Első\nmásodik'
assert report_text('runtime=1\ntext=\ndecode_ms=10\n')==''
# Silence/quiet speech with no decoded words is a valid failure-to-recognize result.
assert trace_text('DROPPED=0\nSTATUS=Listening\n')==('',0,[])
assert trace_text('HISTORY=Quiet speech.\nDROPPED=0\nSTATUS=Listening\nPEAK_QUEUED=5\nMEASURE index=0 decode_ms=100.0 completed_after_source_start_ms=900.0\n')[:2]==('Quiet speech.',0)
try:trace_text('HISTORY=incomplete\n')
except ValueError:pass
else:raise AssertionError('Missing receipt footer must fail')
print('PASS multiline Hungarian text, gap markers, exact-window hashes, blank recognition, incomplete-receipt rejection')
# Incomplete campaign metadata and duplicate profile rows must not schedule a case twice.
import tempfile,json,hashlib
from public_caption_replay import control_inputs
parent=root/'build-public-replay-tests';parent.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='owned-',dir=parent) as temporary:
    directory=pathlib.Path(temporary).resolve();assert directory.is_relative_to(parent.resolve())
    first=directory/'first';second=directory/'second'
    for campaign in (first,second):
        (campaign/'en-01').mkdir(parents=True)
        wav=campaign/'en-01/input.wav';wav.write_bytes(b'test fixture')
        row={'case':'en-01','stage':'raw','supplied_wav_sha256':hashlib.sha256(wav.read_bytes()).hexdigest()}
        (campaign/'results.json').write_text(json.dumps({'rows':[row,row]}))
        (campaign/'manifest.json').write_text(json.dumps({'recordings':[row,{'case':'hu-01'}]}))
    assert len(control_inputs([first,second]))==1
    data=json.loads((second/'results.json').read_text());data['rows'][0]['supplied_wav_sha256']='0'*64
    (second/'results.json').write_text(json.dumps(data))
    try:control_inputs([first,second])
    except ValueError:pass
    else:raise AssertionError('Different WAVs cannot be treated as duplicate cases')
print('PASS completed-case deduplication and input identity conflict checks')
