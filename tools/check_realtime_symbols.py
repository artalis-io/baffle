"""Conservative Linux static-object import check; not a WCET proof."""
import re
import subprocess
import sys

output = subprocess.check_output(["nm", "-u", sys.argv[1]], text=True)
symbols = set(re.findall(r"\bU\s+(\w+)", output))
forbidden = {"malloc", "calloc", "realloc", "free", "aligned_alloc", "printf", "fprintf",
             "fwrite", "fread", "fopen", "puts", "write", "read", "open", "socket", "send",
             "recv", "pthread_mutex_lock", "pthread_cond_wait", "sleep", "nanosleep"}
if symbols & forbidden:
    raise SystemExit(f"Unexpected heap/blocking imports: {sorted(symbols & forbidden)}")
print("PASS: no listed heap, file/network, logging or blocking imports in library objects")
