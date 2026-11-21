"""Private static probe regression; standard Python, no models or NumPy."""
import array,csv,math,pathlib,subprocess,sys,tempfile
tool=pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='baffle-adaptive-') as tmp:
    p=pathlib.Path(tmp);rate=16000;n=24*rate;far=[];last=0
    # Frozen PCG64 state for NumPy seed 713. Integer recurrence avoids making
    # NumPy a core-test dependency; the first raw word is checked as an oracle.
    state=313584384039402944292248474700319038494
    increment=81866568389236448048305662766154596783
    for i in range(n):
        state=(state*47026247687942121848144207491837523525+increment)&((1<<128)-1)
        x=((state>>64)^state)&((1<<64)-1);rot=state>>122
        word=((x>>rot)|(x<<((-rot)&63)))&((1<<64)-1)
        if i==0:assert word==6680392722711066551
        last=((word>>11)*2.0**-53*2-1)+.6*last;far.append(last)
    peak=max(map(abs,far));far=[x*.2/peak for x in far]
    mic=[]
    for i in range(n):
        taps=((48,.6),(320,.2),(1280,-.1)) if i<12*rate else ((83,.4),(376,-.3),(1900,.15))
        mic.append(sum(g*far[i-d] for d,g in taps if i>=d))
    for name,x in [('mic',mic),('ref',far)]:
        with (p/(name+'.f32')).open('wb') as f:array.array('f',x).tofile(f)
    subprocess.run([str(tool),'16000','10','200',str(p/'mic.f32'),str(p/'ref.f32'),str(p/'out.f32'),str(p/'trace.csv'),str(p/'coeff.f32'),'0'],check=True)
    rows=list(csv.DictReader((p/'trace.csv').open()));events=[r for r in rows if int(r['cancel_count'])==0]
    assert events,'Fixture must exercise native recovery, not just successful return codes'
    for r in events:assert int(r['resets'])>0 and int(r['flags'])&16,'Native recovery must be observable through existing public fields'
    values=array.array('f');values.frombytes((p/'out.f32').read_bytes());assert len(values)==n and all(map(math.isfinite,values))
    print('PASS finite abrupt-path recovery; native reset events',len(events),'all publicly reported')
