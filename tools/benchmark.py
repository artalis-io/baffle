"""Optional QA launcher; the native benchmark and library do not require Python."""
import argparse
import hashlib
import json
import multiprocessing as mp
import os
import pathlib
import platform
import subprocess
import time


def load(stop):
    payload = bytes(1024 * 256)
    while not stop.is_set():
        hashlib.sha256(payload).digest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--iterations", type=int, default=10000)
    parser.add_argument("--workers", type=int, default=0)
    args = parser.parse_args()
    if not 0 <= args.workers <= 64:
        parser.error("workers must be 0..64")
    stop = mp.Event()
    workers = [mp.Process(target=load, args=(stop,)) for _ in range(args.workers)]
    try:
        for worker in workers:
            worker.start()
        start = time.monotonic()
        with args.output.open("w", encoding="utf-8", newline="") as output:
            subprocess.run([str(args.executable.resolve()), str(args.iterations)],
                           stdout=output, check=True)
        metadata = {"platform": platform.platform(), "machine": platform.machine(),
                    "processor": platform.processor(), "logical_cpus": os.cpu_count(),
                    "proxy_cpu_workers": args.workers, "iterations": args.iterations,
                    "elapsed_seconds": time.monotonic() - start,
                    "load_kind": "SHA256 CPU proxy; no ASR/TTS/LLM or GPU workload",
                    "executable_sha256": hashlib.sha256(args.executable.read_bytes()).hexdigest()}
        args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2)+"\n")
    finally:
        stop.set()
        for worker in workers:
            worker.join(timeout=10)
            if worker.is_alive():
                worker.terminate()
                worker.join()


if __name__ == "__main__":
    main()
