"""Offline tool regression on SYNTHETIC echo, never acoustic qualification."""
import array
import pathlib
import re
import subprocess
import sys
import tempfile
import wave


def write(path, samples, rate=16000):
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, rate, len(samples), "NONE", "not compressed"))
        pcm = array.array("h", samples)
        if sys.byteorder != "little":
            pcm.byteswap()
        output.writeframes(pcm.tobytes())


tool = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="baffle-wav-") as directory:
    root = pathlib.Path(directory)
    far, near = [], []
    rng = 42
    for i in range(8 * 16000 + 17):
        rng = (rng * 1664525 + 1013904223) & 0xffffffff
        far.append((rng >> 17) - 16384)
        near.append(round(.6 * far[i-32] if i >= 32 else 0) +
                    round(.2 * far[i-93] if i >= 93 else 0))
    write(root / "capture.wav", near)
    write(root / "reference.wav", far)
    args = [tool, str(root / "capture.wav"), str(root / "reference.wav"), str(root / "out.wav")]
    result = subprocess.run(args + ["64000", "64000"], capture_output=True, text=True, check=True)
    erle = float(re.search(r"annotated_far_only_erle_db=([-\d.]+)", result.stdout)[1])
    assert erle > 20, result.stdout
    with wave.open(str(root / "out.wav"), "rb") as output:
        assert output.getnframes() == len(near)
        assert output.getframerate() == 16000
    write(root / "reference.wav", far, rate=48000)
    assert subprocess.run(args, capture_output=True).returncode != 0
    (root / "reference.wav").write_bytes(b"RIFF" + bytes(40))
    assert subprocess.run(args, capture_output=True).returncode != 0
    print(f"PASS SYNTHETIC WAV tool ERLE={erle:.3f} dB; partial EOS and invalid files rejected")
