"""Synthesise the demo's looping test sounds (48 kHz mono, seamless loops). Run with system Python + numpy/scipy."""
import numpy as np, os
from scipy.io import wavfile
from scipy.signal import butter, lfilter
SR = 48000
OUT = os.path.join(os.path.dirname(__file__), '..', 'SourceAssets', 'Audio')
rng = np.random.default_rng(7)

def place(buf, ev, start):
    """Add event at start (samples), wrapping around the loop end so the loop is seamless."""
    n = len(buf); i = start % n
    end = i + len(ev)
    if end <= n: buf[i:end] += ev
    else:
        k = n - i; buf[i:] += ev[:k]; place(buf, ev[k:], 0)

def bp(x, lo, hi, order=2):
    b, a = butter(order, [lo / (SR / 2), hi / (SR / 2)], btype='band'); return lfilter(b, a, x)
def lp(x, f, order=2):
    b, a = butter(order, f / (SR / 2)); return lfilter(b, a, x)
def hp(x, f, order=2):
    b, a = butter(order, f / (SR / 2), btype='high'); return lfilter(b, a, x)
def env(n, att, dec):
    t = np.arange(n) / SR; return np.minimum(1, t / max(att, 1e-4)) * np.exp(-t / dec)
def write(name, x, peak=0.85):
    x = x / (np.max(np.abs(x)) + 1e-9) * peak
    wavfile.write(os.path.join(OUT, name + '.wav'), SR, (x * 32767).astype(np.int16)); print(name, len(x) / SR, 's')

# ---------------- radio music: 16 s, 120 bpm ----------------
L = 16 * SR; music = np.zeros(L); beat = SR // 2
chords = [[57, 60, 64, 67], [53, 57, 60, 64], [55, 59, 62, 65], [52, 55, 59, 64]]
mtof = lambda m: 440 * 2 ** ((m - 69) / 12)
for bar in range(8):
    ch = chords[bar % 4]
    for b in range(4):
        s = (bar * 4 + b) * beat
        if b in (0, 2):   # kick
            n = int(0.35 * SR); t = np.arange(n) / SR
            place(music, np.sin(2 * np.pi * (48 + 110 * np.exp(-t * 28)) * t) * np.exp(-t * 7) * 0.9, s)
        if b in (1, 3):   # snare
            n = int(0.25 * SR); t = np.arange(n) / SR
            place(music, bp(rng.standard_normal(n), 900, 7000) * np.exp(-t * 16) * 0.5 + np.sin(2 * np.pi * 185 * t) * np.exp(-t * 22) * 0.35, s)
        for h in range(2):  # hats
            n = int(0.06 * SR)
            place(music, hp(rng.standard_normal(n), 6000) * env(n, 0.001, 0.015) * 0.22, s + h * beat // 2)
        # electric piano chord stabs on the off-beat
        n = int(0.45 * SR); t = np.arange(n) / SR; v = np.zeros(n)
        for m in ch:
            f = mtof(m)
            v += (np.sin(2 * np.pi * f * t) + 0.35 * np.sin(2 * np.pi * 2 * f * t) + 0.12 * np.sin(2 * np.pi * 3 * f * t)) * np.exp(-t * 4.5)
        place(music, v * 0.09, s + beat // 2)
    # bass
    n = 4 * beat; t = np.arange(n) / SR; f = mtof(ch[0] - 24)
    place(music, np.tanh(2.0 * np.sin(2 * np.pi * f * t)) * 0.28 * np.minimum(1, t * 40) * np.exp(-t * 0.25), bar * 4 * beat)
    # melody
    for k, d in enumerate([0, 3, 2, 4, 7, 4, 2, 0]):
        if (bar + k) % 3 == 2: continue
        n = int(0.22 * SR); t = np.arange(n) / SR; f = mtof(ch[d % 4] + 12)
        place(music, (np.sin(2 * np.pi * f * t) + 0.25 * np.sin(4 * np.pi * f * t)) * env(n, 0.005, 0.12) * 0.12, bar * 4 * beat + k * beat // 2)
write('SW_Radio', music)

# ---------------- workshop machine: 8 s ----------------
L = 8 * SR; t = np.arange(L) / SR
mach = 0.45 * np.sin(2 * np.pi * 100 * t) + 0.25 * np.sin(2 * np.pi * 200 * t) + 0.15 * np.sin(2 * np.pi * 300 * t)
mach *= 1 + 0.15 * np.sin(2 * np.pi * 2 * t)                              # 2 Hz rotation wobble (integer cycles over 8 s)
mach += 0.05 * np.sin(2 * np.pi * 2500 * t + 3 * np.sin(2 * np.pi * 4 * t))  # motor whine
noise = bp(rng.standard_normal(L + SR), 400, 9000)[SR:] * 0.08 * (1 + 0.5 * np.sin(2 * np.pi * 1 * t))
mach += noise
for k in range(4):  # clank every 2 s
    n = int(0.5 * SR); tt = np.arange(n) / SR
    clank = sum(np.sin(2 * np.pi * f * tt) * np.exp(-tt * d) for f, d in [(1730, 9), (2950, 12), (4310, 15), (640, 7)]) * 0.35
    clank += bp(rng.standard_normal(n), 2000, 12000) * np.exp(-tt * 40) * 0.6
    place(mach, clank, k * 2 * SR + int(0.3 * SR))
write('SW_Machine', mach, 0.8)

# ---------------- footsteps: 10 s at 1.6 steps/s ----------------
L = 10 * SR; steps = np.zeros(L)
period = SR / 1.6
for k in range(16):
    n = int(0.3 * SR); tt = np.arange(n) / SR
    heel = np.sin(2 * np.pi * (70 + 40 * np.exp(-tt * 30)) * tt) * np.exp(-tt * 22) * 0.8
    scuff = bp(rng.standard_normal(n), 900, 7000) * np.exp(-tt * 35) * (0.5 + 0.2 * rng.random())
    click = hp(rng.standard_normal(n), 3000) * np.exp(-tt * 120) * 0.35
    place(steps, heel + scuff + click, int(k * period) + int(rng.integers(-800, 800)))
write('SW_Footsteps', steps)


# ---------------- clock: tick-tock every 0.5 s, 4 s ----------------
L = 4 * SR; clock = np.zeros(L)
for k in range(8):
    n = int(0.15 * SR); tt = np.arange(n) / SR
    base = 2400 if k % 2 == 0 else 1900
    tick = sum(np.sin(2 * np.pi * base * r * tt) * np.exp(-tt * d) for r, d in [(1, 60), (1.47, 80), (2.2, 110), (0.3, 40)])
    tick += hp(rng.standard_normal(n), 4000) * np.exp(-tt * 200) * 0.8
    place(clock, tick * 0.6, k * SR // 2)
clock += lp(rng.standard_normal(L), 300) * 0.002
write('SW_Clock', clock, 0.75)

# ---------------- test speaker: pink-noise bursts, 4.8 s ----------------
L = int(4.8 * SR); burst = np.zeros(L)
white = rng.standard_normal(L + SR)
b = [0.049922035, -0.095993537, 0.050612699, -0.004408786]; a = [1, -2.494956002, 2.017265875, -0.522189400]
pink = lfilter(b, a, white)[SR:]
for k in range(4):
    n = int(0.35 * SR); tt = np.arange(n) / SR
    e = np.minimum(1, tt / 0.01) * np.minimum(1, (0.35 - tt) / 0.04)
    place(burst, pink[k * n: k * n + n] * e, int(k * 1.2 * SR))
write('SW_TestBurst', burst, 0.8)
# SW_Step (one-shot footstep) comes from the engine's recorded sample: see Tools/make_step_sound.py.
