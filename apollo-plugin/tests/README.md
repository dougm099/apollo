# Apollo DNA engine — headless tests

`test_dna.cpp` exercises Apollo v2's DNA analysis engine
(`Source/Analysis/`: `DnaExtractor`, `DnaLibrary`, `MatchAnalyzer`,
`DnaFingerprint`) with **no DAW, no GUI and no audio hardware**.
Synthetic signals (sines / silence) are pushed through the real engine —
including the background analysis thread, the demo-library scan, freeze,
and the loop-scan state machine.

## Build & run (one command)

```bash
./build.sh && ./build/test_dna
```

`build.sh` compiles the five JUCE modules the engine needs
(`juce_core`, `juce_events`, `juce_audio_basics`, `juce_audio_formats`,
`juce_dsp` — plus JUCE's bundled libFLAC/zlib, exactly like the plugin's
CMake build), the three Analysis sources, and the harness, then links a
console test runner. JUCE comes from the plugin's own fetched copy
(`../build/_deps/juce-src`), so no extra download. JUCE module objects are
cached in `build/`; `./build.sh clean` wipes everything for a from-scratch
build.

Raw equivalent (what `build.sh` runs — module `.mm` wrappers are JUCE's
Apple entry points, one TU per module as JUCE requires):

```bash
DEFS="-DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -DJUCE_VST3_CAN_REPLACE_VST2=0 \
      -DJUCE_DISPLAY_SPLASH_SCREEN=0 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DNDEBUG"
M=../build/_deps/juce-src/modules
for m in juce_core juce_events juce_audio_basics juce_audio_formats juce_dsp; do
  clang++ -std=c++17 -O1 $DEFS -I$M -I. -I../Source -c $M/$m/$m.mm -o build/$m.o
done
# + libFLAC amalgamations (as C), juce_core's CompilationTime + zlib,
# + ../Source/Analysis/{DnaExtractor,DnaLibrary,MatchAnalyzer}.cpp, + test_dna.cpp
# link with: -framework Cocoa -framework IOKit -framework Accelerate \
#   -framework AudioToolbox -framework CoreAudio -framework QuartzCore -framework Security
```

## What it covers (311 assertions, ~20 test groups)

- **DnaFingerprint** — sub-band edge geometry, macro-band boundary mapping
- **DnaExtractor** — silence gate (-60 dBFS), degenerate inputs, tone→band
  mapping (55 Hz→low, 440 Hz→mid, 10 kHz→high), bit-identical determinism,
  loudness invariance (cosine = 1.0 across a 20 dB level change), tone
  separation, and the `computeLinearBandPower` /
  `fingerprintFromLinearPower` split agreeing exactly with `extractDna`
- **DnaLibrary** — demo-folder scan (9 files, idempotent), `roles.txt`
  parsing incl. the `neutral` fallback, self-similarity ranking at exactly
  100 %, near-duplicate retrieval, band weights steering the rank order,
  the all-zero-weights → equal-weights guard, empty-boost bit-identical
  scores, the energy-role boost (multiplicative, ≤2×, capped at 100,
  never a filter), boost-factor clamping, empty-library safety
- **MatchAnalyzer** — live DNA capture on both buses, listen-source
  selection, freeze (snapshot holds while the live input changes;
  inert with no DNA), loop scan (arm → active → auto-stop → latch,
  1-based bar counter, scan DNA ≈ direct extraction, toggle-off clears),
  the silent auto-stop one-way disarm signal, band-weight clamping,
  status/debug text, energy-section names

## Notes

- `DnaLibrary` reads `~/Music/Apollo/DemoLibrary` **read-only** (the scan
  never writes). Library tests SKIP (counted, non-failing) if the folder
  isn't installed as documented in `../tools/DEMO_LIBRARY.md`. A runtime
  `$HOME` redirect is not possible — macOS's `NSHomeDirectory()` ignores
  it (verified empirically).
- The 55 Hz ranking test documents a real tie: `full-mix-loop` and
  `deep-bass-loop` score 97.250 % vs 97.166 % — genuinely ambiguous at
  3-macro-band resolution, correctly asserted as "low-leaning loops surface".
- The scan-vs-direct similarity is ~0.947, not 1.0: the scan averages two
  phase realizations of the non-bin-centered test tone. Inherent to
  windowed-FFT analysis, not a defect — the dominant band is stable.

## Python tool test

`../tools/test_make_demo_library.py` (pytest) covers the demo-library
generator: file set, WAV format/duration, peak normalization, byte-identical
determinism, `roles.txt` vocabulary, and tone-class distinctness. It runs
the generator with `HOME` pointed at a temp dir — the real Music folder is
never touched.
