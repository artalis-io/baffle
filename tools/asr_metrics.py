"""Deterministic paired ASR edit counts, independent of models and DSP."""
import re
import unicodedata


def words(text):
    return re.findall(r"[^\W_]+(?:'[^\W_]+)?",unicodedata.normalize("NFC",text).casefold())


def edits(reference,hypothesis):
    """Unit-cost Levenshtein; ties prefer substitution, deletion, insertion."""
    previous=[(j,0,0,j) for j in range(len(hypothesis)+1)]
    for i,x in enumerate(reference,1):
        row=[(i,0,i,0)]
        for j,y in enumerate(hypothesis,1):
            if x==y:row.append(previous[j-1]);continue
            choices=[]
            for old,k in ((previous[j-1],1),(previous[j],2),(row[j-1],3)):
                v=list(old);v[0]+=1;v[k]+=1;choices.append(tuple(v))
            row.append(min(enumerate(choices),key=lambda p:(p[1][0],p[0]))[1])
        previous=row
    distance,s,d,i=previous[-1]
    return {"errors":distance,"substitutions":s,"deletions":d,"insertions":i,
            "reference_length":len(reference),"rate":distance/len(reference) if reference else None}


def metrics(reference,hypothesis):
    a,b=words(reference),words(hypothesis)
    return {"word":edits(a,b),"character":edits(list("".join(a)),list("".join(b))),
            "empty":not b,"first_word_correct":bool(b) and b[0]==a[0] if a else None}
