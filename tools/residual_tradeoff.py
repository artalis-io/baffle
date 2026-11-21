"""Gate J fixed comparisons; numerical diagnostics are not perceptual labels."""
import argparse,csv,hashlib,json,os,pathlib,subprocess
import numpy as np
from scipy.signal import coherence,welch
from asr_metrics import words

def load(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,x):p.write_text(json.dumps(x,indent=2)+'\n')

def split_control(runner,inputs,baseline,output):
    output.mkdir(parents=True,exist_ok=False);rows=[]
    for j in load(inputs/'manifest.json')['jobs']:
        env=os.environ.copy();env.pop('BF_QUALIFICATION_TRANSPORT_EVENTS',None)
        if 'events' in j:env['BF_QUALIFICATION_TRANSPORT_EVENTS']=str((inputs/j['events']).resolve())
        stem=output/j['name'];command=[str(runner.resolve()),'residual-control',str(j['rate']),str(inputs/j['mic']),str(inputs/j['reference']),str(stem)+'.f32',str(stem)+'.csv',str(stem)+'.json']
        subprocess.run(command,env=env,check=True,capture_output=True)
        actual=sha(pathlib.Path(str(stem)+'.f32'));expected=sha(baseline/(j['name']+'-pipeline.f32'))
        rows.append({'case':j['name'],'command':command,'pcm_sha256':actual,'baseline_sha256':expected,'equal':actual==expected})
    dump(output/'identity.json',{'results':rows,'all_equal':all(r['equal'] for r in rows)})
    if not all(r['equal'] for r in rows):raise ValueError('Split control changed PCM')

def compare(baseline,candidate,output,stage='pipeline'):
    a=load(baseline)['results'];b=load(candidate)['results'];lookup={(r['case'],r['recognizer']):r for r in a if r['stage']==stage}
    rows=[]
    for r in b:
        if r['stage']!=stage:continue
        old=lookup[r['case'],r['recognizer']]
        rows.append({'case':r['case'],'scenario':r['scenario'],'recognizer':r['recognizer'],
            'baseline_word_errors':old['word']['errors'],'candidate_word_errors':r['word']['errors'],
            'reference_words':r['word']['reference_length'],'baseline_character_errors':old['character']['errors'],
            'candidate_character_errors':r['character']['errors'],'reference_characters':r['character']['reference_length'],
            'baseline_empty':old['empty'],'candidate_empty':r['empty'],
            'new_blank':bool(r['reference']) and r['empty'] and not old['empty'],
            'added_word_errors':r['word']['errors']-old['word']['errors'],
            'first_word_regression':old['first_word_correct'] is True and r['first_word_correct'] is not True,
            'baseline_hypothesis':old['hypothesis'],'candidate_hypothesis':r['hypothesis'],
            'baseline_vad_active_blocks':old['vad_active_blocks'],'candidate_vad_active_blocks':r['vad_active_blocks'],
            'committed_word_count':None})
    groups=[]
    for kind in ('whisper','parakeet'):
        for scenario in sorted({r['scenario'] for r in rows}):
            xs=[r for r in rows if r['recognizer']==kind and r['scenario']==scenario]
            groups.append({'recognizer':kind,'scenario':scenario,'cases':len(xs),
                **{key:sum(r[key] for r in xs) for key in ('reference_words','reference_characters','baseline_word_errors','candidate_word_errors','baseline_character_errors','candidate_character_errors','new_blank','first_word_regression')},
                'baseline_nonempty':sum(not r['baseline_empty'] for r in xs),'candidate_nonempty':sum(not r['candidate_empty'] for r in xs)})
    dump(output,{'baseline':str(baseline),'candidate':str(candidate),'stage':stage,'results':rows,'groups':groups,'reviewed_phoneme_preservation':None})

def characterize(inputs,baseline,candidate,output):
    rows=[];a=load(baseline/'results.json')['results'];b=load(candidate/'results.json')['results']
    for j in load(inputs/'manifest.json')['jobs']:
        rate=j['rate'];first,last=j['asr_frames'];n=last-first
        mic=np.fromfile(inputs/j['mic'],dtype='<f4')[first:last].astype(float)
        ref=np.fromfile(inputs/j['reference'],dtype='<f4')[first:last].astype(float)
        aec=np.fromfile(baseline/(j['name']+'-baffle.f32'),dtype='<f4')[first:last].astype(float)
        signals={'mic':mic,'reference':ref,'aec':aec}
        for name,folder in [('baseline',baseline),('candidate',candidate)]:
            path=folder/(j['name']+'-pipeline.f32');signals[name]=np.fromfile(path,dtype='<f4')[first+rate//100:last+rate//100].astype(float)
        row={'case':j['name'],'scope':'simulated acoustic far-only','rate':rate,'source_frames':[first,last],
             'input_timing':'Public runner distinct clock domains 1/2; timestamp validity unavailable, uncertainty UINT64_MAX. Same-rate fixture, not a physical clock mapping.',
             'events':j.get('events'),'measurements':{},'hypotheses':{},'aec_state':{}}
        for name,x in signals.items():
            assert len(x)==n
            freq,power=welch(x,fs=rate,nperseg=512,noverlap=256)
            row['measurements'][name]={'rms':float(np.sqrt(np.mean(x*x))),'peak':float(np.max(np.abs(x))),
                'nonfinite_samples':int(np.sum(~np.isfinite(x))),'bands_power':{str(lo)+'-'+str(hi):float(np.trapezoid(power[(freq>=lo)&(freq<hi)],freq[(freq>=lo)&(freq<hi)])) for lo,hi in [(0,300),(300,3000),(3000,8001)]},
                'source_window_sha256':hashlib.sha256(x.tobytes()).hexdigest()}
            if name not in ('mic','reference'):
                p=float(np.mean(x*x));row['measurements'][name]['far_only_mic_output_ratio_db']=float(10*np.log10(np.mean(mic*mic)/max(p,1e-30)))
        _,c=coherence(mic-aec,aec,fs=rate,nperseg=512,noverlap=256)
        row['removed_echo_to_residual_welch_mean_coherence']=float(np.nanmean(c))
        for label,records,folder in [('baseline',a,baseline),('candidate',b,candidate)]:
            row['hypotheses'][label]=[{k:r[k] for k in ('recognizer','hypothesis','empty','vad_active_blocks','word')} for r in records if r['case']==j['name'] and r['stage']=='pipeline']
            trace=list(csv.DictReader((folder/(j['name']+'-pipeline.csv')).open()))
            row['aec_state'][label]={'final_resets':int(trace[-1]['aec_resets']),
                'observed_flags':sorted({int(t['flags']) for t in trace}),'processing_vad_active_blocks':sum(int(t['vad_active']) for t in trace)}
        rows.append(row)
    dump(output,{'method':'Whole registered ASR source windows; 512-point Welch/Hann, 50% overlap. Coherence is a spectral diagnostic, not calibrated user evidence. Mic/output ratios require known absent near speech. Far-only hypotheses are not commitment.','results':rows})

def signal_scores(inputs,baseline,candidate,output):
    rows=[]
    for j in load(inputs/'manifest.json')['jobs']:
        if not j['reference_transcript']:continue
        first,last=j['asr_frames'];rate=j['rate'];delay=rate//100
        near=np.fromfile(inputs/j['oracle'],dtype='<f4')[first:last].astype(float)
        assert sha(inputs/j['oracle'])==j['oracle_sha256']
        energy=float(near@near)
        for label,folder in [('baseline',baseline),('candidate',candidate)]:
            path=folder/(j['name']+'-pipeline.f32')
            y=np.fromfile(path,dtype='<f4')[first+delay:last+delay].astype(float)
            assert len(y)==len(near)
            projection=float(near@y/energy)
            freq,sp=welch(near,fs=rate,nperseg=512,noverlap=256)
            _,sy=welch(y/max(abs(projection),1e-8),fs=rate,nperseg=512,noverlap=256)
            mask=(freq>=300)&(freq<=7500)
            trace=list(csv.DictReader((folder/(j['name']+'-pipeline-vad.csv')).open()))
            act=[int(v['first_frame']) for v in trace if int(v['active'])]
            rows.append({'case':j['name'],'scenario':j['scenario'],'variant':label,'pcm_sha256':sha(path),
                'near_projection':projection,'near_relative_error_energy':float(np.sum((y-near)**2)/energy),
                'gain_adjusted_welch_log_spectral_rms_db':float(np.sqrt(np.mean((10*np.log10(np.maximum(sp[mask],1e-12))-10*np.log10(np.maximum(sy[mask],1e-12)))**2))),
                'peak':float(np.max(np.abs(y))),'above_unity':int(np.sum(np.abs(y)>1)),
                'whole_record_vad_active_blocks':sum(int(v['active']) for v in trace),'first_active_trace_frame':act[0] if act else None,
                'reviewed_phoneme_onset_delay':None,'asr_onset_delay':None})
    dump(output,{'method':'Identical registered source intervals. Raw near-component projection/error includes AGC and remaining echo, not isolated near gain. Gain-adjusted 512-point Welch spectrum is not perceptual/phone preservation. First active VAD can be echo, not user onset. No reviewed onset or streaming ASR latency is invented.','results':rows})

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='mode',required=True)
    x=s.add_parser('control')
    for k in ('runner','input','baseline','output'):x.add_argument(k,type=pathlib.Path)
    x=s.add_parser('compare')
    for k in ('baseline','candidate','output'):x.add_argument(k,type=pathlib.Path)
    x.add_argument('--stage',choices=('baffle','pipeline'),default='pipeline')
    x=s.add_parser('characterize')
    for k in ('input','baseline','candidate','output'):x.add_argument(k,type=pathlib.Path)
    x=s.add_parser('signal')
    for k in ('input','baseline','candidate','output'):x.add_argument(k,type=pathlib.Path)
    x=p.parse_args()
    if x.mode=='control':split_control(x.runner,x.input,x.baseline,x.output)
    elif x.mode=='compare':compare(x.baseline,x.candidate,x.output,x.stage)
    elif x.mode=='characterize':characterize(x.input,x.baseline,x.candidate,x.output)
    else:signal_scores(x.input,x.baseline,x.candidate,x.output)
