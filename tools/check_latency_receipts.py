"""Recompute native benchmark percentiles/deadline counts from per-frame receipts.

CSV summary rounds milliseconds to four decimals; raw nanoseconds are rounded
integers. The 0.000051 ms comparison bound covers those two output formats, not
a relaxed DSP or deadline target. Thread CPU accounting cannot be reconstructed
from wall-time receipts and is deliberately not inferred here.
"""
import csv
import math
import pathlib
import sys

summary, raw = map(pathlib.Path,sys.argv[1:3])
keys = ("case","rate","channels","sessions","frame_ms")
rows = list(csv.DictReader(summary.open()))
expected = {tuple(r[k] for k in keys):r for r in rows}
if len(expected) != len(rows):
    raise SystemExit("duplicate case identity")
values = {key:[] for key in expected}
with raw.open() as source:
    for r in csv.DictReader(source):
        key = tuple(r[k] for k in keys)
        v = int(r["processing_ns"])
        if key not in expected or v < 0 or int(r["iteration"]) != len(values[key]):
            raise SystemExit("invalid receipt identity/order/cost")
        if len(values[key]) >= int(expected[key]["iterations"]):
            raise SystemExit("too many receipts")
        values[key].append(v)
for key,r in expected.items():
    v = values[key]
    n = int(r["iterations"])
    if n != len(v) or n < 2:
        raise SystemExit("receipt count mismatch")
    misses = sum(t > int(r["frame_ms"])*1000000 for t in v)
    v.sort()
    for name,index in (("p50_ms",n//2),("p95_ms",(n-1)*95//100),
                       ("p99_ms",(n-1)*99//100),("max_ms",n-1)):
        if not math.isfinite(float(r[name])) or abs(float(r[name])-v[index]/1e6) > .000051:
            raise SystemExit("summary percentile does not match raw receipts")
    if misses != int(r["deadline_misses"]):
        raise SystemExit("deadline count does not match raw receipts")
print(f"PASS {len(rows)} cases, {sum(len(v) for v in values.values())} raw costs: percentiles and deadline counts reproduced")
