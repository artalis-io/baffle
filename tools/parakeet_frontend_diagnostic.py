"""External one-factor NeMo normalization diagnostic; never a Baffle stage.

Pinned sherpa 1.12.40 uses float E[x*x]-E[x]^2. Compare that cancellation-prone
formula with centered population variance on IDENTICAL kaldi-native-fbank 1.22.3
features, model weights and TDT greedy decoding. This is not a NeMo-reference
frontend or a production ASR runtime. Original sherpa results stay authoritative.
"""
import argparse, hashlib, json, pathlib, time
import numpy as np
import kaldi_native_fbank as knf
import onnxruntime as ort
from inference_contention import load_asr


def normalize(x,stable):
    mean=x.mean(axis=0)
    variance=((x-mean)**2).mean(axis=0) if stable else np.maximum((x*x).mean(axis=0)-mean*mean,0)
    return (x-mean)/(np.sqrt(variance)+np.float32(1e-5)),variance


def features(x,linear=False,window="povey",high_freq=-400):
    opts=knf.FbankOptions(); opts.frame_opts.dither=0; opts.frame_opts.snip_edges=False
    opts.frame_opts.remove_dc_offset=False
    opts.frame_opts.window_type=window
    opts.mel_opts.low_freq=0; opts.mel_opts.high_freq=high_freq
    opts.mel_opts.num_bins=128; opts.mel_opts.is_librosa=True
    opts.use_log_fbank=not linear
    f=knf.OnlineFbank(opts); f.accept_waveform(16000,x); f.input_finished()
    return np.stack([f.get_frame(i) for i in range(f.num_frames_ready)]).astype(np.float32)


class Decoder:
    def __init__(self,models):
        d=models/"parakeet"; options=ort.SessionOptions()
        options.intra_op_num_threads=2; options.inter_op_num_threads=1
        self.sessions=[ort.InferenceSession(str(d/(s+".int8.onnx")),sess_options=options,
                        providers=["CPUExecutionProvider"]) for s in ("encoder","decoder","joiner")]
        meta=self.sessions[0].get_modelmeta().custom_metadata_map
        self.blank=int(meta["vocab_size"]); self.layers=int(meta["pred_rnn_layers"]);self.hidden=int(meta["pred_hidden"])
        self.tokens={int(line.rsplit(maxsplit=1)[1]):line.rsplit(maxsplit=1)[0]
                     for line in (d/"tokens.txt").read_text(encoding="utf-8").splitlines()}

    @staticmethod
    def run(session,values): return session.run(None,{node.name:value for node,value in zip(session.get_inputs(),values)})

    def decode(self,f):
        encoder,decoder,joiner=self.sessions
        enc,length=self.run(encoder,[f.T[None].copy(),np.array([len(f)],dtype=np.int64)])
        states=[np.zeros((self.layers,1,self.hidden),dtype=np.float32) for _ in range(2)]
        def prediction(token,states):
            out=self.run(decoder,[np.array([[token]],dtype=np.int32),np.array([1],dtype=np.int32),*states])
            return out[0],out[2:]
        pred,states=prediction(self.blank,states); tokens=[]; t=0; same_frame=0; steps=0
        while t<int(length[0]):
            steps+=1
            if steps>int(length[0])*6: raise ValueError("bounded TDT decode limit")
            logits=self.run(joiner,[enc[:,:,t:t+1].copy(),pred])[0].reshape(-1)
            token=int(np.argmax(logits[:self.blank+1])); skip=int(np.argmax(logits[self.blank+1:]))
            if token!=self.blank:
                tokens.append(token); pred,states=prediction(token,states); same_frame+=1
            if skip>0: same_frame=0
            if same_frame>=5 or (token==self.blank and skip==0): same_frame=0;skip=1
            t+=skip
        text="".join(self.tokens[t] for t in tokens).replace("▁"," ").strip()
        return text,tokens


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for key in ("models","input","results","report"):p.add_argument(key,type=pathlib.Path)
    a=p.parse_args();manifest=json.loads((a.input/"manifest.json").read_text())
    decoder=Decoder(a.models); sherpa=load_asr("parakeet",a.models);rows=[]
    for job in manifest["jobs"]:
        if job["name"] not in ("normal","quiet-near","through-cancel"):continue
        for variant in ("mic","baffle","pipeline"):
            path=a.input/job["mic"] if variant=="mic" else a.results/(job["name"]+"-"+variant+".f32")
            x=np.fromfile(path,dtype="<f4");first,last=job["asr_interval_frames"];x=x[first:last].copy()
            stream=sherpa.create_stream();stream.accept_waveform(16000,x);sherpa.decode_stream(stream)
            f=features(x)
            power=features(x,True)
            for method,data,stable in (("legacy-E2-minus-mean2-f32",f,False),("centered-population-f32",f,True),
                    ("additive-log-guard-centered-f32",np.log(power+np.float32(2**-24)),True),
                    ("hann-only-centered-f32",features(x,window="hann"),True),
                    ("nyquist-only-centered-f32",features(x,high_freq=8000),True)):
                start=time.perf_counter();z,var=normalize(data,stable);text,tokens=decoder.decode(z)
                rows.append({"case":job["name"],"variant":variant,"method":method,
                    "text":text,"tokens":tokens,"sherpa_original":stream.result.text,"original_pcm_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
                    "feature_max_abs":float(np.max(np.abs(z))),"variance_zero_bins":int(np.sum(var==0)),
                    "decode_ms":(time.perf_counter()-start)*1000})
    a.report.write_text(json.dumps({"scope":"External frontend attribution only; no Baffle or official-runtime correction implied",
        "versions":{"kaldi-native-fbank":"1.22.3","onnxruntime":ort.__version__},"results":rows},indent=2)+"\n",encoding="utf-8")
