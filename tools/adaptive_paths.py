"""Deterministic Gate K AEC path tracking; synthetic, not physical room claims."""
import argparse,csv,hashlib,json,pathlib,subprocess
import numpy as np
from scipy.signal import lfilter

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,x):p.write_text(json.dumps(x,indent=2)+'\n')
def read(p):return json.loads(p.read_text())

def prepare(models,out):
    if out.exists():raise ValueError('fresh immutable directory required')
    out.mkdir(parents=True);jobs=[]
    import soundfile as sf
    voice,voice_rate=sf.read(models/'whisper/test_wavs/1.wav',dtype='float32')
    assert voice_rate==16000
    cases=['stationary','abrupt','gradual','gain-change','delay-change','long-tail','quiet-ref','very-quiet-ref','noise','doubletalk','quiet-doubletalk','loud-doubletalk','near-only','reference-gap','drift-positive','drift-negative','holdout-reflections','holdout-noise']
    for name in cases:
        rate=16000;n=24*rate;rng=np.random.default_rng(714 if name.startswith('holdout') else 713)
        far=lfilter([1],[1,-.6],rng.uniform(-1,1,n));far*=.2/np.max(np.abs(far))
        if name=='quiet-ref':far*=.025
        if name=='very-quiet-ref':far*=.0025
        a=np.zeros(5600);a[48]=.6;a[320]=.2;a[1280]=-.1
        z=np.zeros_like(a);z[83]=.4;z[376]=-.3;z[1900]=.15
        if name=='long-tail':a[4800]=.15;z=a.copy()
        if name=='holdout-reflections':z[:]=0;z[960]=.45;z[2510]=-.22
        if name=='gain-change':z=a*1.5
        if name=='delay-change':z=np.r_[np.zeros(1600),a[:-1600]]
        if name not in ('abrupt','gradual','gain-change','delay-change','doubletalk','quiet-doubletalk','loud-doubletalk','holdout-reflections'):z=a.copy()
        fade=np.clip((np.arange(n)/rate-8)/8,0,1) if name=='gradual' else (np.arange(n)>=12*rate).astype(float)
        drift=200 if name=='drift-positive' else -200 if name=='drift-negative' else 0
        played=np.interp(np.arange(n)*(1+drift/1e6),np.arange(n),far) if drift else far
        echo=np.convolve(played,a)[:n]*(1-fade)+np.convolve(played,z)[:n]*fade
        near=np.zeros(n)
        if name in ('doubletalk','quiet-doubletalk','loud-doubletalk','near-only'):
            peak=.025 if name=='quiet-doubletalk' else .5 if name=='loud-doubletalk' else .15
            signal=voice.astype(float)*peak/max(abs(voice));start=8*rate;stop=18*rate
            near[start:stop]=np.resize(signal,stop-start)
        if name=='near-only':far[:]=0;echo[:]=0
        noise=rng.normal(0,.005 if name in ('noise','holdout-noise') else 0,n)
        mic=echo+near+noise
        stem=out/name
        for suffix,x in [('mic',mic),('ref',far),('near',near),('a',a),('z',z)]:x.astype('<f4').tofile(str(stem)+'-'+suffix+'.f32')
        job={'name':name,'rate':rate,'hop_ms':10,'tail_ms':200,'duration_s':24,'event_s':8 if name=='gradual' else 12,
             'moving':name in ('abrupt','gradual','gain-change','delay-change','holdout-reflections'),
             'holdout':name.startswith('holdout'),'gap':name=='reference-gap','drift_ppm':drift,
             'scope':'synthetic FIR with deterministic colored excitation; recorded near speech, not a room recording',
             'a':name+'-a.f32','z':name+'-z.f32','fade':'linear8..16' if name=='gradual' else 'step12'}
        for suffix in ('mic','ref','near'):job[suffix]=name+'-'+suffix+'.f32';job[suffix+'_sha256']=sha(out/job[suffix])
        jobs.append(job)
    # Same signals, legal 20 ms quantum and tail-capacity controls.
    for name,hop,tail in [('abrupt',20,200),('long-tail',10,500),('abrupt',10,40)]:
        old=next(j for j in jobs if j['name']==name);j=old.copy();j.update(name=f'{name}-hop{hop}-tail{tail}',hop_ms=hop,tail_ms=tail,moving=False);jobs.append(j)
    dump(out/'manifest.json',{'seed':713,'holdout_seed':714,'voice_sha256':sha(models/'whisper/test_wavs/1.wav'),'jobs':jobs,'physical':False})

def replay(tool,inputs,out):
    if out.exists():raise ValueError('fresh results directory required')
    out.mkdir(parents=True);rows=[]
    for j in read(inputs/'manifest.json')['jobs']:
        for key in ('mic','ref','near'):assert sha(inputs/j[key])==j[key+'_sha256']
        stem=out/j['name'];cmd=[str(tool.resolve()),str(j['rate']),str(j['hop_ms']),str(j['tail_ms']),str(inputs/j['mic']),str(inputs/j['ref']),str(stem)+'.f32',str(stem)+'.csv',str(stem)+'-coeff.f32',str(int(j['gap']))]
        result=subprocess.run(cmd,capture_output=True,text=True)
        rows.append({'name':j['name'],'command':cmd,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
        dump(out/'runs.json',{'tool_sha256':sha(tool),'manifest_sha256':sha(inputs/'manifest.json'),'results':rows})
        if result.returncode:raise ValueError('trace failed; retained run receipt')

def score(inputs,outputs,path):
    results=[]
    for j in read(inputs/'manifest.json')['jobs']:
        rate=j['rate'];f=rate*j['hop_ms']//1000
        mic=np.fromfile(inputs/j['mic'],dtype='<f4').astype(float);near=np.fromfile(inputs/j['near'],dtype='<f4').astype(float)
        y=np.fromfile(outputs/(j['name']+'.f32'),dtype='<f4').astype(float);ref=np.fromfile(inputs/j['ref'],dtype='<f4').astype(float)
        assert len(y)==len(mic) and np.isfinite(y).all()
        curves=[]
        for start in range(0,len(y),rate//2):
            last=min(start+rate//2,len(y));x=mic[start:last];z=y[start:last];v=near[start:last]
            active=np.any(abs(v)>0)
            erle=None if active or float(x@x)<1e-20 else float(10*np.log10((x@x)/max(z@z,1e-30)))
            curves.append({'end_s':last/rate,'far_only_erle_db':erle,'output_power':float(np.mean(z*z)),'excited':float(np.mean(ref[start:last]**2))>1e-12,'near_projection':float(v@z/(v@v)) if active else None,'near_component_snr_improvement_db':float(10*np.log10(max(np.sum((x-v)**2),1e-30)/max(np.sum((z-v)**2),1e-30))) if active else None})
        def settle(event):
            count=0
            for r in curves:
                if r['end_s']-.5<event-1e-9:continue
                count=count+1 if r['excited'] and r['far_only_erle_db'] is not None and r['far_only_erle_db']>=20 else 0
                if count==2:return r['end_s']-event
            return None
        trace=list(csv.DictReader((outputs/(j['name']+'.csv')).open()))
        impulse_frames=((rate*j['tail_ms']//1000+f-1)//f)*f
        coeff=np.fromfile(outputs/(j['name']+'-coeff.f32'),dtype='<f4').reshape(-1,impulse_frames).astype(float)
        a=np.fromfile(inputs/j['a'],dtype='<f4');z=np.fromfile(inputs/j['z'],dtype='<f4');mis=[]
        snapshots=[t for t in trace if int(t['coefficient_snapshot'])]
        for h,t in zip(coeff,snapshots):
            sec=(int(t['frame_position'])+int(t['frames']))/rate
            fade=np.clip((sec-8)/8,0,1) if j['fade']=='linear8..16' else float(sec>=12)
            truth=np.zeros(max(impulse_frames,len(a)));true=(1-fade)*a+fade*z;truth[:len(true)]=true
            estimated=np.zeros_like(truth);estimated[:len(h)]=h
            power=float(truth@truth)
            comparable=j['drift_ppm']==0 and np.any(ref!=0)
            mis.append({'time_s':sec,'raw_fir_misalignment_db':float(10*np.log10(max(np.sum((estimated-truth)**2),1e-30)/power)) if power>0 and comparable else None,'coefficient_norm':float(np.linalg.norm(h)),'dominant_tap':int(t['dominant_tap']) if int(t['delay_valid'])==1 else None,'raw_dominant_tap_error_frames':int(t['dominant_tap'])-int(np.argmax(abs(truth))) if int(t['delay_valid'])==1 and comparable else None})
        event=16 if j['fade']=='linear8..16' else 10.2 if j['gap'] else j['event_s']
        moving_windows=[r['far_only_erle_db'] for r in curves if 8<r['end_s']<=16 and r['far_only_erle_db'] is not None]
        results.append({'case':j['name'],'moving':j['moving'],'holdout':j['holdout'],'initial_20db_s':settle(0),'recovery_event_s':event,'post_event_20db_s':settle(event),'gradual_tracking_worst_500ms_erle_db':min(moving_windows) if j['fade']=='linear8..16' and moving_windows else None,'curves':curves,'background_ir':mis,'backend_counter_restarts':sum(int(trace[i]['cancel_count'])<int(trace[i-1]['cancel_count']) for i in range(1,len(trace))),'max_screwed_up':max(float(t['screwed_up']) for t in trace),'note':'Background filter only. Raw-FIR misalignment includes capture DC conditioning, quantization and out-of-capacity taps; it is not pure solver error or calibrated physical delay. Recovery uses fully post-event windows; gradual movement ends at 16s, gap at10.2s; other stationary cases use a12s observation checkpoint.'})
    dump(path,{'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'results':results,'method':'Two consecutive excited 500 ms known-far-only windows >=20dB; overlap is explicitly unavailable for ERLE; background coefficient snapshots are offline and never included in callback cost.'})

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='mode',required=True)
    x=s.add_parser('prepare');x.add_argument('models',type=pathlib.Path);x.add_argument('output',type=pathlib.Path)
    x=s.add_parser('replay')
    for key in ('tool','input','output'):x.add_argument(key,type=pathlib.Path)
    x=s.add_parser('score')
    for key in ('input','results','output'):x.add_argument(key,type=pathlib.Path)
    x=p.parse_args()
    if x.mode=='prepare':prepare(x.models,x.output)
    elif x.mode=='replay':replay(x.tool,x.input,x.output)
    else:score(x.input,x.results,x.output)
