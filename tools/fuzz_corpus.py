"""Optional QA seed generation, including nonfinite and partial-block inputs."""
import pathlib
import struct
import sys
import math

root = pathlib.Path(sys.argv[1])
for harness in ("transport", "pipeline", "aec"):
    target = root / harness
    target.mkdir(parents=True, exist_ok=True)
    for index, word in enumerate((0, 1, 0x3dcccccd, 0x7fc00000, 0x7f800000, 0xff800000, 0x41800000)):
        for channels in (1, 2, 8, 32):
            prefix = bytes((channels-1, 159, 14, 1, 20, 250, 7, 11))
            payload = (struct.pack("<I", word) * (160 * channels))[:4088]
            (target / f"{index}-{channels}").write_bytes(prefix + payload)
# Retain VAD activity while resetting NS: exercise foreground-protected bootstrap
# through the real pipeline, including the first subsequent protected frame.
prefix=bytes((0,159,10,0,18,0,2,128))
payload=b"".join(struct.pack("<f",.2*math.sin(2*math.pi*300*i/16000)) for i in range(160))
(root/"pipeline"/"protected-ns-reset").write_bytes(prefix+payload)
