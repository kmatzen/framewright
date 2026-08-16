"""Self-contained smoke test for binary wheels.

Runs with no external tools (no ffmpeg CLI, no fixtures): writes an FFV1
lossless file through the wheel's own writer — FFV1 is compiled into the
LGPL wheel build — reads it back, and checks the pixels round-trip exactly.
Also confirms the documented wheel limitation: H.264 writing is absent
(no GPL encoders bundled) and fails cleanly rather than crashing.
"""

import os
import sys
import tempfile

import numpy as np

import framewright

failures = 0


def check(name, cond):
    global failures
    print(("ok: " if cond else "FAIL: ") + name)
    if not cond:
        failures += 1


with tempfile.TemporaryDirectory() as tmp:
    path = os.path.join(tmp, "roundtrip.mkv")

    w = framewright.VideoWriter()
    check("FFV1 writer opens", w.open(path, codec="ffv1", width=64, height=48, fps=30))
    frame = np.zeros((48, 64, 3), dtype=np.uint8)
    frame[:, :] = (42, 84, 168)
    check("write", w.write(frame))
    check("write 2", w.write(frame))
    w.release()

    r = framewright.VideoReader()
    check("reader opens", r.open(path))
    back = r.read()
    check("read returns frame", back is not None)
    check("FFV1 round-trip exact", back is not None and bool((back[24, 32] == (42, 84, 168)).all()))
    r.close()

    lin = None
    r2 = framewright.VideoReader()
    if r2.open(path):
        lin = r2.read_linear()
    r2.close()
    check("read_linear works", lin is not None and lin.dtype == np.float32)

    # Wheels ship LGPL FFmpeg: H.264/HEVC writing must fail cleanly (use a
    # source install with system FFmpeg for that), never crash.
    w2 = framewright.VideoWriter()
    h264_path = os.path.join(tmp, "unsupported.mp4")
    check("H.264 writer absent fails cleanly",
          w2.open(h264_path, codec="h264", width=64, height=48, fps=30) is False)

print("wheel smoke: " + ("PASSED" if failures == 0 else f"{failures} FAILURES"))
sys.exit(0 if failures == 0 else 1)
