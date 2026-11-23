"""External paired batch-ASR benchmark; no inference/cloud dependency in Baffle.

Public FLEURS only. Generated clean/noise/linear-echo conditions are not physical
room recordings. No reference transcript is ever passed to an ASR provider.
"""
import argparse
import csv
import hashlib
import io
import json
import os
import pathlib
import platform
import subprocess
import threading
import time
import urllib.error
import urllib.request
import uuid

import numpy as np
import soundfile as sf
from asr_metrics import metrics

DATASET_REVISION = "70bb2e84b976b7e960aa89f1c648e09c59f894dd"
MODEL_REVISION = "8f3c18b358db4d1f2fc1eae49d75cd20989e4309"
LICENSE = "CC-BY-4.0"
ENDPOINT = "https://api.openai.com/v1/audio/transcriptions"

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, url):
        # A provider redirect must not forward the authorization header elsewhere.
        raise urllib.error.HTTPError(request.full_url, code, "Redirect rejected", {}, None)



def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dump(path, value):
    # A result campaign is always a fresh directory; subsequent checkpoints belong to it.
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf8")


def fresh(path):
    if path.exists() and any(path.iterdir()):
        raise ValueError("Use a fresh directory; previous evidence is retained")
    path.mkdir(parents=True, exist_ok=True)


def checked_file(root, name, digest):
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file() or sha(path) != digest:
        raise ValueError("Fixture path/hash mismatch")
    return path


def aggregate(rows):
    out = []
    groups = sorted({(r["engine"], r["language"], r["condition"], r["stage"]) for r in rows})
    for engine, language, condition, stage in groups:
        group = [r for r in rows if (r["engine"], r["language"], r["condition"], r["stage"]) ==
                 (engine, language, condition, stage)]
        good = [r for r in group if not r.get("error_status")]
        g = {"engine": engine, "language": language, "condition": condition, "stage": stage,
             "requests": len(group), "successful": len(good), "failures": len(group) - len(good)}
        for key in ("word", "character"):
            errors = sum(r["metrics"][key]["errors"] for r in good)
            count = sum(r["metrics"][key]["reference_length"] for r in good)
            g[key + "_errors"] = errors
            g[key + "_reference_length"] = count
            g[key + "_error_rate"] = errors / count if count else None
        for field in ("wall_ms", "cpu_ms", "real_time_factor", "dsp_plus_decode_ms"):
            values = [r[field] for r in good if r.get(field) is not None]
            g[field] = ({"p50": float(np.percentile(values, 50)),
                         "p95": float(np.percentile(values, 95)),
                         "p99": float(np.percentile(values, 99)), "max": max(values)} if values else None)
        g["empty_speech_hypotheses"] = sum(r["metrics"]["empty"] for r in good)
        g["first_word_correct"] = sum(bool(r["metrics"]["first_word_correct"]) for r in good)
        out.append(g)
    return out


def prepare(a):
    import pyarrow.parquet as pq
    from scipy.signal import lfilter
    fresh(a.output)
    sources = []
    for language, config in (("en", "en_us"), ("hu", "hu_hu")):
        path = next((a.dataset / "parquet-data" / config).glob("test-*.parquet"))
        seen = set()
        for batch in pq.ParquetFile(path).iter_batches(batch_size=32):
            for index, row in enumerate(batch.to_pylist()):
                if row["id"] in seen:
                    continue
                x, rate = sf.read(io.BytesIO(row["audio"]["bytes"]), dtype="float32")
                # Fixed operational bounds, selected before recognition; no score-based exclusion.
                if rate != 16000 or x.ndim != 1 or not 2 <= len(x) / rate <= 20:
                    continue
                seen.add(row["id"])
                name = f'{language}-{len(seen):02d}'
                source = a.output / (name + "-source.wav")
                source.write_bytes(row["audio"]["bytes"])
                sources.append({"name": name, "language": language, "dataset_id": row["id"],
                                "gender_label": row["gender"], "source": source.name,
                                "source_sha256": sha(source), "publisher_reference": row["raw_transcription"],
                                "audio": x})
                if len(seen) == a.clips_per_language:
                    break
            if len(seen) == a.clips_per_language:
                break
        if len(seen) != a.clips_per_language:
            raise ValueError("Insufficient unique bounded fixtures")
    jobs = []
    for source_index, source in enumerate(sources):
        near = np.pad(source["audio"].astype(np.float64), (8000, 8000))
        far_source = sources[(source_index + a.clips_per_language) % len(sources)]["audio"]
        for condition in ("clean", "noise-10db", "simulated-echo-0db"):
            rng = np.random.default_rng(23000 + source_index)
            reference = np.zeros(len(near))
            mic = near.copy()
            if condition == "noise-10db":
                noise = lfilter([1.0], [1.0, -.85], rng.standard_normal(len(near)))
                target = np.sqrt(np.mean(source["audio"].astype(np.float64) ** 2)) / np.sqrt(10)
                mic += noise * target / np.sqrt(np.mean(noise ** 2))
            if condition == "simulated-echo-0db":
                reference = np.resize(far_source, len(near)).astype(np.float64)
                echo = np.zeros(len(near))
                for delay, gain in ((48, .6), (320, .2), (1280, -.1)):
                    echo[delay:] += reference[:-delay] * gain
                scale = np.sqrt(np.mean(near ** 2) / max(np.mean(echo ** 2), 1e-20))
                reference *= scale
                mic += echo * scale
            gain = min(1.0, .95 / max(float(np.max(np.abs(mic))), float(np.max(np.abs(reference))), 1e-20))
            mic *= gain
            reference *= gain
            stem = a.output / (source["name"] + "-" + condition)
            raw_wav = pathlib.Path(str(stem) + "-raw.wav")
            sf.write(raw_wav, mic, 16000, subtype="PCM_16")
            # All consumers see the exact same encoded/decoded PCM, including quantization.
            raw, rate = sf.read(raw_wav, dtype="float32")
            mic_path = pathlib.Path(str(stem) + "-mic.f32")
            ref_path = pathlib.Path(str(stem) + "-ref.f32")
            raw.astype("<f4").tofile(mic_path)
            reference.astype("<f4").tofile(ref_path)
            processed_path = pathlib.Path(str(stem) + "-processed.f32")
            timing_path = pathlib.Path(str(stem) + "-timings.csv")
            native_path = pathlib.Path(str(stem) + "-native.json")
            backend = "aec-ns" if condition == "simulated-echo-0db" else "ns-only"
            env = os.environ.copy()
            env.pop("BF_QUALIFICATION_TRANSPORT_EVENTS", None)
            before = time.perf_counter()
            subprocess.run([str(a.runner.resolve()), backend, "16000", str(mic_path.resolve()),
                            str(ref_path.resolve()), str(processed_path.resolve()),
                            str(timing_path.resolve()), str(native_path.resolve())],
                           check=True, capture_output=True, env=env)
            subprocess_ms = (time.perf_counter() - before) * 1000
            native = json.loads(native_path.read_text())
            delay = native["added_pipeline_delay_frames"]
            output = np.fromfile(processed_path, dtype="<f4")[delay:delay + len(raw)]
            if len(output) != len(raw) or not np.all(np.isfinite(output)):
                raise ValueError("Incomplete/nonfinite DSP output")
            dsp_wav = pathlib.Path(str(stem) + "-baffle.wav")
            sf.write(dsp_wav, output, rate, subtype="PCM_16")
            timings = list(csv.DictReader(timing_path.open()))
            processing_ns = [int(r["processing_ns"]) for r in timings]
            base = {"case": source["name"] + "-" + condition, "language": source["language"],
                    "condition": condition, "source": source["name"], "duration_s": len(raw) / rate,
                    "reference": source["publisher_reference"], "gain_to_avoid_clipping": gain,
                    "public_upload_allowed": True, "native_backend": backend,
                    "dsp": {"native": native, "subprocess_ms_including_init": subprocess_ms,
                            "execution_sum_ms": sum(processing_ns) / 1e6,
                            "p99_ms": float(np.percentile(processing_ns, 99)) / 1e6,
                            "max_ms": max(processing_ns) / 1e6,
                            "algorithmic_delay_ms": delay / 16}}
            for stage, path in (("raw", raw_wav), ("baffle", dsp_wav)):
                jobs.append({**base, "stage": stage, "wav": path.name, "wav_sha256": sha(path)})
            print(base["case"], flush=True)
    for source in sources:
        del source["audio"]
    dump(a.output / "manifest.json", {
        "dataset": "google/fleurs", "revision": DATASET_REVISION, "license": LICENSE,
        "attribution": "FLEURS, Google Research; https://huggingface.co/datasets/google/fleurs",
        "scope": "Public recorded read speech; added noise and linear echo are simulated",
        "selection": f"first {a.clips_per_language} unique IDs per language, 2..20 s, 16k mono",
        "policy": "Frozen pilot; no tuning, reference prompts or expected-word hints; paired identical WAVs",
        "dataset_parquet_sha256": {p.name + ':' + p.parent.name: sha(p)
                                   for p in (a.dataset / "parquet-data").glob("*/test-*.parquet")},
        "runner_sha256": sha(a.runner), "sources": sources, "jobs": jobs})


def local(a):
    import psutil
    import sherpa_onnx
    fresh(a.output)
    manifest = json.loads((a.input / "manifest.json").read_text(encoding="utf8"))
    process = psutil.Process()
    rows, configs = [], []
    for language in ("en", "hu"):
        if a.model == "tiny.en" and language != "en":
            continue
        began = time.perf_counter()
        recognizer = sherpa_onnx.OfflineRecognizer.from_whisper(
            encoder=str(a.model_dir / (a.model + "-encoder.int8.onnx")),
            decoder=str(a.model_dir / (a.model + "-decoder.int8.onnx")),
            tokens=str(a.model_dir / (a.model + "-tokens.txt")), language=language,
            task="transcribe", num_threads=a.threads, provider="cpu", decoding_method="greedy_search")
        config = {"language": language, "initialization_ms": (time.perf_counter() - began) * 1000,
                  "rss_after_init_bytes": process.memory_info().rss,
                  "config": str(recognizer.config)}
        configs.append(config)
        jobs = [j for j in manifest["jobs"] if j["language"] == language]
        # Warm-up is an unscored fresh stream; it does not adapt a persistent speech context.
        warm, _ = sf.read(checked_file(a.input, jobs[0]["wav"], jobs[0]["wav_sha256"]), dtype="float32")
        stream = recognizer.create_stream()
        stream.accept_waveform(16000, warm[:16000])
        recognizer.decode_stream(stream)
        for repeat in range(a.repeats):
            for job in jobs:
                path = checked_file(a.input, job["wav"], job["wav_sha256"])
                x, rate = sf.read(path, dtype="float32")
                peak = [process.memory_info().rss]
                stopped = threading.Event()
                def sample():
                    while not stopped.wait(.01):
                        peak[0] = max(peak[0], process.memory_info().rss)
                sampler = threading.Thread(target=sample)
                sampler.start()
                cpu = time.process_time()
                begin = time.perf_counter()
                try:
                    stream = recognizer.create_stream()
                    stream.accept_waveform(rate, x)
                    recognizer.decode_stream(stream)
                    text = stream.result.text
                    wall_ms = (time.perf_counter() - begin) * 1000
                    cpu_ms = (time.process_time() - cpu) * 1000
                finally:
                    stopped.set()
                    sampler.join()
                dsp_ms = job["dsp"]["execution_sum_ms"] if job["stage"] == "baffle" else 0
                row = {"engine": "whisper-" + a.model, "repeat": repeat, **job,
                       "hypothesis": text, "error_status": None, "metrics": metrics(job["reference"], text),
                       "wall_ms": wall_ms, "cpu_ms": cpu_ms,
                       "cpu_percent_of_one_core": cpu_ms / wall_ms * 100,
                       "rss_sampled_peak_bytes": peak[0], "dsp_plus_decode_ms": wall_ms + dsp_ms,
                       "real_time_factor": wall_ms / (job["duration_s"] * 1000),
                       "provider_gpu_measurement": None, "client_execution_provider": "cpu"}
                rows.append(row)
                print(job["case"], job["stage"], f'{wall_ms:.1f} ms', row["metrics"]["word"]["errors"], flush=True)
                dump(a.output / "results.json", {"manifest_sha256": sha(a.input / "manifest.json"),
                     "model": a.model, "model_revision": MODEL_REVISION if a.model == "small" else "existing pinned tiny.en",
                     "model_sha256": {p.name: sha(p) for p in a.model_dir.glob(a.model + "-*")} if len(rows) == 1 else model_hashes,
                     "platform": platform.platform(), "threads": a.threads, "sherpa_version": sherpa_onnx.__version__,
                     "configs": configs, "rows": rows, "aggregate": aggregate(rows)})
                if len(rows) == 1:
                    model_hashes = json.loads((a.output / "results.json").read_text(encoding="utf8"))["model_sha256"]
        del recognizer


def validate_cloud_jobs(root, manifest, max_minutes, max_requests):
    if not np.isfinite(max_minutes) or max_minutes <= 0 or max_requests <= 0:
        raise ValueError("Positive finite request/audio budgets required")
    if manifest.get("dataset") != "google/fleurs" or manifest.get("revision") != DATASET_REVISION:
        raise ValueError("Cloud benchmark accepts only the pinned public FLEURS pilot")
    jobs = manifest["jobs"]
    if not jobs or any(not np.isfinite(j["duration_s"]) or j["duration_s"] <= 0 for j in jobs):
        raise ValueError("Invalid audio durations")
    if len(jobs) > max_requests or sum(j["duration_s"] for j in jobs) / 60 > max_minutes:
        raise ValueError("Requested cloud run exceeds explicit request/audio budget")
    for j in jobs:
        if j.get("public_upload_allowed") is not True:
            raise ValueError("Private/unapproved fixture must not be uploaded")
        path = checked_file(root, j["wav"], j["wav_sha256"])
        if path.stat().st_size > 25_000_000:
            raise ValueError("Audio exceeds file request limit")
        info = sf.info(path)
        if info.samplerate != 16000 or info.channels != 1 or info.frames > 21 * 16000 or \
                abs(info.frames / info.samplerate - j["duration_s"]) > 1e-6:
            raise ValueError("WAV duration/geometry disagrees with the approved request budget")
    return jobs


def multipart(fields, wav):
    boundary = "baffle-" + uuid.uuid4().hex
    pieces = []
    for name, value in fields:
        pieces.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n'.encode())
    pieces += [f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="sample.wav"\r\nContent-Type: audio/wav\r\n\r\n'.encode(),
               wav.read_bytes(), f'\r\n--{boundary}--\r\n'.encode()]
    return b"".join(pieces), boundary


def cloud(a):
    # Never inspect browser/Codex credentials or print request authorization headers.
    key = os.environ.get("OPENAI_API_KEY")
    if not key:
        raise ValueError("OPENAI_API_KEY is not configured; no request sent")
    manifest = json.loads((a.input / "manifest.json").read_text(encoding="utf8"))
    jobs = validate_cloud_jobs(a.input, manifest, a.max_minutes, a.max_requests)
    fresh(a.output)
    rows = []
    opener = urllib.request.build_opener(NoRedirect())
    for job in jobs:
        fields = [("model", a.model), ("response_format", "json")]
        fields.append(("languages[]" if a.model == "gpt-transcribe" else "language", job["language"]))
        path = checked_file(a.input, job["wav"], job["wav_sha256"])
        body, boundary = multipart(fields, path)
        request = urllib.request.Request(ENDPOINT, data=body, headers={
            "Authorization": "Bearer " + key, "Content-Type": "multipart/form-data; boundary=" + boundary})
        begin, cpu = time.perf_counter(), time.process_time()
        row = {"engine": a.model, **job, "provider_cpu_gpu_memory": None,
               "provider_compute_ms": None, "upload_bytes": len(body), "dsp_plus_decode_ms": None}
        try:
            with opener.open(request, timeout=120) as response:
                result = json.load(response)
                row.update(request_id=response.headers.get("x-request-id"), response_model=result.get("model"),
                           usage=result.get("usage"), hypothesis=result["text"], error_status=None,
                           metrics=metrics(job["reference"], result["text"]))
        except urllib.error.HTTPError as error:
            # Retain status, not raw bodies/headers that might reveal credentials.
            row.update(error_status="http_" + str(error.code), metrics=None)
        except Exception as error:
            row.update(error_status=type(error).__name__, metrics=None)
        row.update(wall_ms=(time.perf_counter() - begin) * 1000,
                   cpu_ms=(time.process_time() - cpu) * 1000)
        row["real_time_factor"] = row["wall_ms"] / (job["duration_s"] * 1000)
        rows.append(row)
        dump(a.output / "results.json", {"manifest_sha256": sha(a.input / "manifest.json"),
             "endpoint": ENDPOINT, "model_requested": a.model, "model_is_alias": True,
             "utc_date": time.strftime("%Y-%m-%d", time.gmtime()), "rows": rows, "aggregate": aggregate(rows),
             "limits": "Request wall time includes network/service; provider compute/memory are unobservable. Not ChatGPT UI."})
        print(job["case"], job["stage"], row["error_status"] or "ok", flush=True)
        if row.get("error_status") in ("http_401", "http_403", "http_429"):
            break


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    q = sub.add_parser("prepare")
    q.add_argument("--dataset", type=pathlib.Path, required=True)
    q.add_argument("--runner", type=pathlib.Path, required=True)
    q.add_argument("--output", type=pathlib.Path, required=True)
    q.add_argument("--clips-per-language", type=int, choices=range(1, 9), default=4)
    q = sub.add_parser("local")
    q.add_argument("--input", type=pathlib.Path, required=True)
    q.add_argument("--output", type=pathlib.Path, required=True)
    q.add_argument("--model-dir", type=pathlib.Path, required=True)
    q.add_argument("--model", choices=("small", "tiny.en"), default="small")
    q.add_argument("--threads", type=int, choices=range(1, 17), default=2)
    q.add_argument("--repeats", type=int, choices=range(1, 6), default=2)
    q = sub.add_parser("cloud")
    q.add_argument("--input", type=pathlib.Path, required=True)
    q.add_argument("--output", type=pathlib.Path, required=True)
    q.add_argument("--model", choices=("gpt-transcribe", "gpt-4o-transcribe"), default="gpt-transcribe")
    q.add_argument("--max-minutes", type=float, default=20)
    q.add_argument("--max-requests", type=int, default=48)
    a = p.parse_args()
    {"prepare": prepare, "local": local, "cloud": cloud}[a.command](a)


if __name__ == "__main__":
    main()
