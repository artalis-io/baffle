"""Check a shared library exports exactly the functions in the public header."""
import pathlib
import re
import subprocess
import sys

header, library = map(pathlib.Path, sys.argv[1:3])
expected = set(re.findall(r"BF_API\s+(?:const\s+)?\w+\s+(?:\*\s*)?(bf_\w+)\s*\(", header.read_text()))
if sys.platform == "win32":
    output = subprocess.check_output(["dumpbin", "/nologo", "/exports", str(library)], text=True)
    actual = set(re.findall(r"^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\w+)", output, re.M))
elif sys.platform == "darwin":
    output = subprocess.check_output(["nm", "-gU", str(library)], text=True)
    actual = {line.split()[-1].removeprefix("_") for line in output.splitlines() if line.split()}
else:
    output = subprocess.check_output(["nm", "-D", "--defined-only", str(library)], text=True)
    actual = {line.split()[-1] for line in output.splitlines() if line.split()}
if actual != expected:
    raise SystemExit(f"Missing: {sorted(expected-actual)}; unexpected: {sorted(actual-expected)}")
print(f"PASS: exactly {len(expected)} public functions exported; backend symbols hidden")
