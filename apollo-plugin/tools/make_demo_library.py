#!/usr/bin/env python3
"""
make_demo_library.py — generate Apollo v2's built-in demo sample library.

Nine synthetic 4-second loops with clearly different tonal DNAs, written to
~/Music/Apollo/DemoLibrary/*.wav. Apollo scans this folder on startup,
extracts each file's DNA once, and ranks the set DNA-to-DNA against the
live input.

v2.4: roles.txt maps each file to an energy role (closed vocabulary —
see tools/DEMO_LIBRARY.md) used by the energy-aware recommendations.

Stdlib only (wave, struct, math, random, os). Deterministic: random seeded.
"""

import math
import os
import random
import struct
import wave

SR = 44100
DUR = 4.0
N = int(SR * DUR)
OUT_DIR = os.path.expanduser("~/Music/Apollo/DemoLibrary")
rng = random.Random(1234)


def write_wav(name, samples):
    peak = max(abs(s) for s in samples) or 1.0
    gain = 0.89 / peak
    path = os.path.join(OUT_DIR, name)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = struct.pack("<%dh" % N,
                             *[int(max(-1.0, min(1.0, s * gain)) * 32767)
                               for s in samples])
        w.writeframes(frames)
    print("wrote", path)


def sine(f, t):
    return math.sin(2.0 * math.pi * f * t)


def noise_burst(n):
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def highpass(x):
    """Crude first-difference highpass: emphasizes the top end."""
    return [x[i] - x[i - 1] for i in range(len(x))]


def lowpass(x, window=32):
    """Crude moving-average lowpass: emphasizes the low end."""
    out = []
    acc = 0.0
    for i, v in enumerate(x):
        acc += v
        if i >= window:
            acc -= x[i - window]
        out.append(acc / min(i + 1, window))
    return out


def t_at(i):
    return i / SR


# --- 1. deep-bass-loop: sub + low mids, kick thumps at 120 BPM ---------------
bass = [0.0] * N
for i in range(N):
    t = t_at(i)
    v = 0.55 * sine(55, t) + 0.30 * sine(82.5, t) + 0.20 * sine(110, t)
    # kick thump every 0.5 s: decaying 60 Hz sine
    beat = t % 0.5
    v += math.exp(-beat * 16.0) * sine(60, beat) * 0.9
    bass[i] = v

# --- 2. bright-hats-loop: highpassed noise 8ths + high shimmer ---------------
hats = [0.0] * N
eighth = int(SR * 0.25)
burst = highpass(noise_burst(eighth))
for i in range(N):
    t = t_at(i)
    pos = i % eighth
    env = math.exp(-pos / (SR * 0.03))
    v = 0.0
    if i % (eighth // 2) < eighth:  # 8th-note grid
        v = burst[pos] * env * 0.7
    v += 0.12 * sine(8000, t) + 0.08 * sine(12000, t)
    hats[i] = v

# --- 3. mid-keys-loop: A-major-ish stack, tremolo, mid-focused ------------------
# Fundamentals sit in the MID macro band (250-4000 Hz) so the loop reads as
# mid-dominant on the DNA screens.
# NOTE (v2.4): frequencies are integer-cycle-per-4s (440/554/659/880) so the
# file loops SEAMLESSLY — a phase jump at the loop seam would inject a
# broadband transient that dominates the scan's quiet bands and breaks H6
# (scanned DNA vs file DNA). 554 vs 554.37 Hz is ~1 cent: inaudible.
keys = [0.0] * N
for i in range(N):
    t = t_at(i)
    trem = 0.7 + 0.3 * sine(5, t)
    v = (0.40 * sine(440, t) + 0.30 * sine(554, t)
         + 0.30 * sine(659, t) + 0.15 * sine(880, t)) * trem
    keys[i] = v

# --- 4. air-texture: lowpassed noise swell, no strong tones ------------------
raw = noise_burst(N)
smooth = lowpass(raw, window=64)
texture = [0.0] * N
for i in range(N):
    t = t_at(i)
    swell = 0.5 + 0.5 * sine(0.25, t)  # slow 4 s swell
    texture[i] = smooth[i] * swell * 1.4

# --- 5. full-mix-loop: a bit of everything (broadband) ------------------------
mix = [0.0] * N
for i in range(N):
    t = t_at(i)
    beat = t % 0.5
    v = (0.35 * sine(55, t)
         + 0.25 * sine(220, t) + 0.20 * sine(329.63, t)
         + math.exp(-beat * 16.0) * sine(60, beat) * 0.5)
    pos = i % (eighth // 2)
    env = math.exp(-pos / (SR * 0.03))
    v += burst[pos % len(burst)] * env * 0.25
    v += 0.06 * sine(8000, t)
    mix[i] = v


# --- 6. riser-swell (v2.4, role "riser"): rising filtered noise + rising tone -
# 4 s of building energy — the canonical BUILDUP recommendation demo.
riser = [0.0] * N
rnoise = highpass(noise_burst(N))
for i in range(N):
    t = t_at(i)
    k = t / DUR  # 0 -> 1 over the file
    # rising brightness: a 200->2000 Hz sweep plus highpassed noise whose
    # level ramps up; overall amplitude ramps so energy slope reads positive.
    sweep = sine(200.0 + 1800.0 * k, t) * 0.25 * k
    riser[i] = (rnoise[i] * 0.6 * k + sweep) * (0.15 + 0.85 * k * k)

# --- 7. rolling-hats (v2.4, role "roll"): driving 16ths at ~140 BPM ----------
# Denser and more insistent than bright-hats-loop — the "rolling hi hats"
# Doug asked the buildup recommendation to surface.
rolling = [0.0] * N
sixteenth = int(SR * 60.0 / 140.0 / 4.0)
rburst = highpass(noise_burst(sixteenth))
for i in range(N):
    t = t_at(i)
    pos = i % sixteenth
    env = math.exp(-pos / (SR * 0.02))
    accent = 1.0 if (i // sixteenth) % 4 == 0 else 0.65
    rolling[i] = rburst[pos] * env * 0.8 * accent + 0.10 * sine(9000, t)

# --- 8. impact-hit (v2.4, role "impact"): one big downbeat, long tail --------
impact = [0.0] * N
crash = highpass(noise_burst(N))
for i in range(N):
    t = t_at(i)
    boom = math.exp(-t * 6.0) * (0.9 * sine(50, t) + 0.4 * sine(75, t))
    hit = math.exp(-t * 18.0) * crash[i] * 0.5
    tail = math.exp(-t * 1.2) * 0.12 * sine(110, t)
    impact[i] = boom + hit + tail

# --- 9. soft-pad (v2.4, role "pad"): gentle lowpassed F-major-ish wash -------
pad = [0.0] * N
padraw = lowpass(noise_burst(N), window=128)
for i in range(N):
    t = t_at(i)
    attack = min(1.0, t / 1.5)  # slow 1.5 s attack
    chord = (0.30 * sine(174.61, t) + 0.25 * sine(220.0, t)
             + 0.22 * sine(261.63, t) + 0.10 * sine(349.23, t))
    wash = padraw[i] * 0.25
    pad[i] = (chord * 0.8 + wash) * attack * (0.85 + 0.15 * sine(0.4, t))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    write_wav("deep-bass-loop.wav", bass)
    write_wav("bright-hats-loop.wav", hats)
    write_wav("mid-keys-loop.wav", keys)
    write_wav("air-texture.wav", texture)
    write_wav("full-mix-loop.wav", mix)
    # --- v2.4: energy-role-tagged additions ---------------------------------
    write_wav("riser-swell.wav", riser)
    write_wav("rolling-hats.wav", rolling)
    write_wav("impact-hit.wav", impact)
    write_wav("soft-pad.wav", pad)

    # Closed role vocabulary (see tools/DEMO_LIBRARY.md): the plugin reads
    # this file on startup. One "file.wav=role" per line; unknown or missing
    # entries fall back to "neutral".
    roles = {
        "deep-bass-loop.wav": "neutral",
        "bright-hats-loop.wav": "hats",
        "mid-keys-loop.wav": "neutral",
        "air-texture.wav": "texture",
        "full-mix-loop.wav": "neutral",
        "riser-swell.wav": "riser",
        "rolling-hats.wav": "roll",
        "impact-hit.wav": "impact",
        "soft-pad.wav": "pad",
    }
    with open(os.path.join(OUT_DIR, "roles.txt"), "w") as f:
        for name in sorted(roles):
            f.write("%s=%s\n" % (name, roles[name]))
    print("wrote roles.txt")

    with open(os.path.join(OUT_DIR, "README.txt"), "w") as f:
        f.write(
            "Apollo v2 demo library — synthetic loops with distinct tonal DNAs.\n"
            "Apollo scans this folder on startup and ranks these DNA-to-DNA\n"
            "against the live input. Regenerate with:\n"
            "  python3 /Users/doug0/muse/apollo/apollo-plugin/tools/make_demo_library.py\n"
            "roles.txt maps each file to an energy role (riser/roll/hats/impact/\n"
            "pad/texture/neutral) used by the v2.4 energy-aware recommendations.\n")


if __name__ == "__main__":
    main()
