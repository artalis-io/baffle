"""Known-absent-near VAD candidates and numerical transition steps, not phones."""
import argparse,pathlib,json,csv,hashlib
import numpy as np
p=argparse.ArgumentParser(description=__doc__)
for k in ('input','results','output'):p.add_argument(k,type=pathlib.Path)
a=p.parse_args();m=json.loads((a.input/'manifest.json').read_text());rows=[]
for j in m['jobs']:
 for stage in ('raw','baffle','aec-ns','aec-ns-agc','pipeline'):
  stem=a.results/(j['name']+'-'+stage);trace=list(csv.DictReader(pathlib.Path(str(stem)+'-vad.csv').open()))
  raw=a.input/j['mic'] if stage=='raw' else pathlib.Path(str(stem)+'.f32')
  x=np.fromfile(raw,dtype='<f4');delay=j['rate']//100 if stage in ('aec-ns','aec-ns-agc','pipeline') else 0
  active=sum(int(r['active']) for r in trace);rises=sum(int(r['active']) and (i==0 or not int(trace[i-1]['active'])) for i,r in enumerate(trace))
  row={'case':j['name'],'scenario':j['scenario'],'stage':stage,'vad_blocks':len(trace),'vad_active_blocks':active,'vad_rises':rises,'pcm_sha256':hashlib.sha256(raw.read_bytes()).hexdigest()}
  if j['scenario'] in ('far-only','silence'):row['known_no_near_speech_false_vad_rises']=rises
  if j['scenario'].startswith('near-'):row['whole_utterance_detection_missed']=active==0
  if j['scenario'] in ('cancel-span','post-cancel','render-restart','capture-epoch','missing-reference'):
   rate=j['rate'];first=int(9.9*rate)+delay;last=int(10.2*rate)+delay;window=x[first:last].astype(float)
   row['transition_window']={'source_seconds':[9.9,10.2],'peak_abs':float(np.max(np.abs(window))),'max_adjacent_sample_step':float(np.max(np.abs(np.diff(window)))),'not_audible_click_or_phoneme_qualification':True}
   if stage!='raw':
    native=list(csv.DictReader(pathlib.Path(str(stem)+'.csv').open()))
    row['transport']={'priming_calls':sum(int(r['priming']) for r in native),'discarded_frames':sum(int(r['discarded_frames']) for r in native),'aec_resets_last':int(native[-1]['aec_resets']),'agc_peak_gain':max(float(r['gain']) for r in native) if stage in ('aec-ns-agc','pipeline') else None}
  rows.append(row)
a.output.write_text(json.dumps({'scope':'Known absent-near VAD candidates, coarse whole-utterance detection and sample-step measurements; no reviewed phone timing or audible-click claim','results':rows},indent=2)+'\n')
print('Supplementary VAD/transport rows',len(rows))
