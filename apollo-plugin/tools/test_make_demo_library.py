#!/usr/bin/env python3
"""Tests for tools/make_demo_library.py — Apollo v2's demo sample generator.

The script writes to ~/Music/Apollo/DemoLibrary (via os.path.expanduser, which
honors $HOME). Every test runs the script as a subprocess with HOME pointed at
a temp dir, so Doug's real Music folder is never touched.

Covers: file set + WAV format, 4.0 s duration, peak normalization to 0.89,
determinism (seeded RNG -> byte-identical regeneration), roles.txt vocabulary,
and a smoke check that the documented tone classes are actually distinct.
"""

import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import wave

import pytest

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "make_demo_library.py")
SR = 44100
DUR_S = 4.0
N = int(SR * DUR_S)

EXPECTED_WAVS = [
    "deep-bass-loop.wav",
    "bright-hats-loop.wav",
    "mid-keys-loop.wav",
    "air-texture.wav",
    "full-mix-loop.wav",
    "riser-swell.wav",
    "rolling-hats.wav",
    "impact-hit.wav",
    "soft-pad.wav",
]
ROLE_VOCABULARY = {"riser", "roll", "hats", "impact", "pad", "texture", "neutral"}
EXPECTED_ROLES = {
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


def run_generator(home):
    env = dict(os.environ, HOME=home)
    return subprocess.run([sys.executable, SCRIPT], env=env, capture_output=True,
                          text=True, timeout=600)


def demo_dir(home):
    return os.path.join(home, "Music", "Apollo", "DemoLibrary")


def read_mono_i16(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 1, path
        assert w.getsampwidth() == 2, path
        assert w.getframerate() == SR, path
        n = w.getnframes()
        raw = w.readframes(n)
    return n, struct.unpack("<%dh" % n, raw)


@pytest.fixture()
def generated(tmp_path):
    home = str(tmp_path / "home")
    proc = run_generator(home)
    assert proc.returncode == 0, proc.stderr[-2000:]
    return home


def test_file_set(generated):
    d = demo_dir(generated)
    for name in EXPECTED_WAVS:
        assert os.path.isfile(os.path.join(d, name)), name
    assert os.path.isfile(os.path.join(d, "roles.txt"))
    assert os.path.isfile(os.path.join(d, "README.txt"))


def test_wav_format_and_duration(generated):
    d = demo_dir(generated)
    for name in EXPECTED_WAVS:
        n, _ = read_mono_i16(os.path.join(d, name))
        assert n == N, f"{name}: {n} frames != {N} (4.0 s @ 44.1 kHz)"


def test_peak_normalized(generated):
    # write_wav peak-normalizes to 0.89 -> max |int16| == int(0.89*32767),
    # allowing a couple LSB of float rounding.
    d = demo_dir(generated)
    for name in EXPECTED_WAVS:
        _, samples = read_mono_i16(os.path.join(d, name))
        peak = max(abs(s) for s in samples)
        assert 29160 <= peak <= 29163, f"{name}: peak {peak}"


def test_deterministic(tmp_path):
    # Seeded RNG: regenerating into the same folder is byte-identical.
    home = str(tmp_path / "home")
    assert run_generator(home).returncode == 0
    d = demo_dir(home)

    def hashes():
        out = {}
        for name in sorted(os.listdir(d)):
            with open(os.path.join(d, name), "rb") as f:
                out[name] = hashlib.sha256(f.read()).hexdigest()
        return out

    first = hashes()
    assert run_generator(home).returncode == 0
    assert hashes() == first


def test_roles_txt(generated):
    with open(os.path.join(demo_dir(generated), "roles.txt")) as f:
        lines = [ln.strip() for ln in f if ln.strip()]
    assert len(lines) == 9
    assert lines == sorted(lines)  # written sorted
    seen = {}
    for line in lines:
        name, role = line.split("=")
        seen[name] = role
        assert role in ROLE_VOCABULARY, line
    assert seen == EXPECTED_ROLES


def _highpass_ratio(samples):
    # Energy of first-difference highpassed signal / total energy.
    total = 0.0
    high = 0.0
    prev = 0.0
    for s in samples:
        v = s / 32768.0
        total += v * v
        d = v - prev
        high += d * d
        prev = v
    return high / total if total > 0 else 0.0


def test_tone_classes_are_distinct(generated):
    # Smoke check on the documented tone classes: the HIGH-dominant hats
    # loop must carry far more top-end energy than the LOW-dominant bass loop.
    d = demo_dir(generated)
    _, bass = read_mono_i16(os.path.join(d, "deep-bass-loop.wav"))
    _, hats = read_mono_i16(os.path.join(d, "bright-hats-loop.wav"))
    rb = _highpass_ratio(bass)
    rh = _highpass_ratio(hats)
    print(f"\nhighpass energy ratio: bass={rb:.4f} hats={rh:.4f}")
    assert rh > 3.0 * rb
