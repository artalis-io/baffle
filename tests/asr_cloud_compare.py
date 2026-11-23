"""Cloud benchmark boundary tests: no network, models or real credentials."""
import hashlib
import importlib.util
import json
import os
import pathlib
import sys
import tempfile
import types
import wave
from unittest.mock import patch

root = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
import asr_cloud_compare as bench

with tempfile.TemporaryDirectory(prefix='baffle-cloud-') as temporary:
    directory = pathlib.Path(temporary)
    wav = directory/'sample.wav'
    with wave.open(str(wav), 'wb') as output:
        output.setnchannels(1); output.setsampwidth(2); output.setframerate(16000)
        output.writeframes(bytes(64000))
    job = {'wav': wav.name, 'wav_sha256': hashlib.sha256(wav.read_bytes()).hexdigest(),
           'public_upload_allowed': True, 'duration_s': 2}
    manifest = {'dataset': 'google/fleurs', 'revision': bench.DATASET_REVISION, 'jobs': [job]}
    assert bench.validate_cloud_jobs(directory, manifest, 1, 1) == [job]
    for mutation in ('private', 'path_escape', 'hash_change', 'duration_nan', 'wrong_duration', 'budget'):
        bad = json.loads(json.dumps(manifest))
        if mutation == 'private': bad['jobs'][0]['public_upload_allowed'] = False
        if mutation == 'path_escape': bad['jobs'][0]['wav'] = '../sample.wav'
        if mutation == 'hash_change': bad['jobs'][0]['wav_sha256'] = '0'*64
        if mutation == 'duration_nan': bad['jobs'][0]['duration_s'] = float('nan')
        if mutation == 'wrong_duration': bad['jobs'][0]['duration_s'] = 1
        try:
            bench.validate_cloud_jobs(directory, bad, .001 if mutation == 'budget' else 1, 1)
        except ValueError:
            pass
        else:
            raise AssertionError(mutation)
    with patch.dict(os.environ, {}, clear=True), patch.object(bench.urllib.request, 'build_opener') as opener:
        try: bench.cloud(types.SimpleNamespace())
        except ValueError as e: assert 'OPENAI_API_KEY' in str(e)
        else: raise AssertionError('Missing credential must fail before input/network')
        opener.assert_not_called()
    good = {'engine':'gpt-transcribe', 'language':'hu', 'condition':'clean', 'stage':'raw',
            'error_status':None, 'wall_ms':20, 'cpu_ms':1, 'metrics':bench.metrics('árvíz', 'árvíz')}
    bad = dict(good, error_status='http_500', metrics=None)
    summary = bench.aggregate([good, bad])[0]
    assert summary['successful'] == 1 and summary['failures'] == 1
    assert summary['word_error_rate'] == 0 and summary['empty_speech_hypotheses'] == 0
    assert bench.metrics('őr', 'or')['word']['errors'] == 1
    assert bench.metrics('ÁRVÍZTŰRŐ', 'árvíztűrő')['word']['errors'] == 0
    body, boundary = bench.multipart([('model','gpt-transcribe'), ('languages[]','hu')], wav)
    assert b'name="languages[]"' in body and wav.read_bytes() in body
    assert body.endswith(('--'+boundary+'--\r\n').encode())
    try: bench.NoRedirect().redirect_request(types.SimpleNamespace(full_url=bench.ENDPOINT),None,302,'',{},'https://elsewhere.invalid')
    except bench.urllib.error.HTTPError: pass
    else: raise AssertionError('Credential-bearing redirect accepted')
print('PASS fixture provenance/hash/path/budget guards, missing-key no-network behavior, language/metrics, multipart and redirect rejection')
