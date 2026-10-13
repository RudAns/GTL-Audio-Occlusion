"""Turn the engine's recorded footstep into the demo's one-shot SW_Step.wav (system Python + numpy/scipy).
A landing sample is heavier than a walking step: trim the tail, take some of the low boom out, keep the heel and scuff."""
import os
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

HERE = os.path.dirname(__file__)
SRC = os.path.join(HERE, "..", "SourceAssets", "Audio", "Raw", "Footstep_Rock_Land01.wav")
DST = os.path.join(HERE, "..", "SourceAssets", "Audio", "SW_Step.wav")

sr, x = wavfile.read(SRC)
x = x.astype(np.float64)
if x.ndim > 1:
    x = x.mean(axis=1)
x /= np.max(np.abs(x)) + 1e-12

# Remove the sub-bass thump that made the old synthetic step sound like a drum, and lighten the 150-300 Hz body.
x = sosfilt(butter(2, 110 / (sr / 2), btype="high", output="sos"), x)
body = sosfilt(butter(2, [150 / (sr / 2), 320 / (sr / 2)], btype="band", output="sos"), x)
x = x - 0.35 * body

# Keep about 0.22 s and fade the tail.
n = int(0.22 * sr)
x = x[:n]
fade = int(0.06 * sr)
x[-fade:] *= np.linspace(1.0, 0.0, fade) ** 2

x = x / (np.max(np.abs(x)) + 1e-12) * 0.9
wavfile.write(DST, sr, (x * 32767).astype(np.int16))
print("SW_Step", round(len(x) / sr, 3), "s", sr, "Hz")
