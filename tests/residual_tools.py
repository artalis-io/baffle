"""Device-free Gate J experiment contracts; no models or acoustic PASS claim."""
import array,csv,json,math,os,pathlib,subprocess,sys,tempfile
base,candidate=map(lambda p:pathlib.Path(p).resolve(),sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix='baffle-residual-') as tmp:
    d=pathlib.Path(tmp)
    for rate in (16000,48000):
        frames=rate//100;n=rate//2+3
        mic=d/'mic.f32';ref=d/'ref.f32'
        x=array.array('f',(.1*math.sin(2*math.pi*317*i/rate) for i in range(n)))
        x.tofile(mic.open('wb'))
        def run(exe,mode,label,events=None):
            stem=d/label;env=os.environ.copy();env.pop('BF_QUALIFICATION_TRANSPORT_EVENTS',None)
            if events:env['BF_QUALIFICATION_TRANSPORT_EVENTS']=str(events)
            subprocess.run([str(exe),mode,str(rate),str(mic),str(ref),str(stem)+'.f32',str(stem)+'.csv',str(stem)+'.json'],env=env,check=True,capture_output=True)
            pcm=pathlib.Path(str(stem)+'.f32').read_bytes();y=array.array('f');y.frombytes(pcm)
            assert len(y)==n+frames and all(math.isfinite(v) for v in y)
            info=json.loads(pathlib.Path(str(stem)+'.json').read_text());assert info['added_pipeline_delay_frames']==frames
            return pcm,list(csv.DictReader(pathlib.Path(str(stem)+'.csv').open()))
        array.array('f',[0]*n).tofile(ref.open('wb'))
        raw,_=run(base,'pipeline','base');split,_=run(candidate,'residual-control','split');filtered,_=run(candidate,'pipeline','zero-ref')
        assert raw==split==filtered # No reference activity is not evidence of echo.
        array.array('f',(.15*math.sin(2*math.pi*521*i/rate) for i in range(n))).tofile(ref.open('wb'))
        events=d/'events.txt';events.write_text(f'{10*frames} 1 1 2 0\n{20*frames} 1 1 2 1\n{25*frames} 1 2 3 0\n{30*frames} 2 2 3 0\n')
        raw,a=run(base,'pipeline','base-events',events);split,b=run(candidate,'residual-control','split-events',events)
        assert raw==split and [r['aec_resets'] for r in a]==[r['aec_resets'] for r in b]
        first,a=run(candidate,'pipeline','candidate-events',events);second,b=run(candidate,'pipeline','repeat',events)
        assert first==second and [r['aec_resets'] for r in a]==[r['aec_resets'] for r in b]
        # Explicit recreation at another rate rather than hidden reconfiguration.
print('PASS 16/48k split identity, zero-reference bypass, finite partial EOS, deterministic generation/epoch/missing-reference recovery')
