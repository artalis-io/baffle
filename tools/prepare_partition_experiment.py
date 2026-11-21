"""Prepare ONE private derived MDF candidate in an isolated existing worktree.

Upstream BSD notices remain intact. Original hash-pinned inputs are not changed.
This is qualification tooling, never a public backend/configuration option.
"""
import argparse,hashlib,json,pathlib,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('speex',type=pathlib.Path);p.add_argument('worktree',type=pathlib.Path);a=p.parse_args()
root=a.worktree.resolve()
if root==pathlib.Path.cwd().resolve() or not (root/'.git').is_file():raise ValueError('isolated worktree required')
if subprocess.check_output(['git','-C',str(root),'status','--porcelain'],text=True).strip():raise ValueError('clean worktree required')
source=a.speex/'libspeexdsp/mdf.c';raw=source.read_bytes();data=raw.replace(b'\r\n',b'\n')
if hashlib.sha256(data).hexdigest()!='a096297873dfd0a07509028d2cdd02d12edf8f1d03319fcecedda003d18d22a8':raise ValueError('upstream pin mismatch')
old=b'MULT16_16_Q15(QCONST16(.1f,15),max_sum)';new=b'MULT16_16_Q15(QCONST16(.3f,15),max_sum)'
if data.count(old)!=1:raise ValueError('patch must identify exactly one partition floor')
target=root/'src/speex/mdf_partition_experiment.c';target.write_bytes(data.replace(old,new))
wrapper=root/'src/speex/mdf_baffle.c';text=wrapper.read_text()
if text.count('#include "mdf.c"')!=1:raise ValueError('wrapper include')
wrapper.write_text(text.replace('#include "mdf.c"','#include "mdf_partition_experiment.c"'))
notice=root/'cmake/SpeexDSP.cmake';text=notice.read_text();notice.write_text(text.replace('Sources remain unmodified;','EXPERIMENT ONLY: a licensed derived MDF changes partition exploration 0.1 to 0.3; original inputs remain hash-pinned.'))
(root/'partition-experiment.json').write_text(json.dumps({'base':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),'parameter':'partition exploration floor only','old':.1,'new':.3,'upstream_lf_sha256':hashlib.sha256(data).hexdigest(),'upstream_file_sha256':hashlib.sha256(raw).hexdigest(),'derived_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'license':'original BSD notices retained in derived source, COPYING unchanged','production_option':False},indent=2)+'\n')
print('Prepared isolated partition-floor candidate; original source and production tree unchanged')
