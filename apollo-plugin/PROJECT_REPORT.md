# Apollo — DNA-to-DNA Sample Search Plugin

**One-liner:** A VST3/AU audio plugin that captures the "DNA of a track" from live audio and ranks your sample library by tonal similarity — search by tone, not by file name.

## 1. Background

Apollo started from Doug's locked product decision (2026-10-06): a plugin "match" means **DNA-to-DNA similarity**, where the DNA of a track is its captured spectral/tonal fingerprint. The concept is inspired by the match-EQ capture workflow (e.g. iZotope Ozone): capture a reference's frequency profile, compare against it. Apollo takes the *capture-and-compare* half of match EQ and points it at a sample library instead of a single track.

This decision superseded the earlier 2026-09-20 transformation specs (groove-lock, spectral glue, level-ride) as the product definition: Apollo is **search/discovery, not transformation**. The transform/apply half of match EQ is an explicit non-goal for v1.

## 2. Problem it solves

Producers pick samples by file name, tags, or auditioning one by one. None of that answers the real question: *which of my samples will sit in this mix?* Apollo answers it acoustically — play your track, and it ranks your library by DNA closeness: which kicks share your low-end DNA, which hats share your top-end DNA.

The differentiator vs. match-EQ plugins: those match one fixed whole-curve profile. Apollo splits DNA into frequency bands and lets the producer weight **which part of the tone** the match cares about — solo the low band to find kicks that sit in your mix's pocket, match the mids to find loops that won't fight your vocals, ignore the highs when they don't matter. No match-EQ plugin does band-isolated search across a library.

## 3. Methodology

**The fingerprint (`Source/Analysis/DnaFingerprint.h`).** A `DnaFingerprint` holds 16 log-spaced sub-band energies (40 Hz–20 kHz), aggregated into three macro bands — LOW < 250 Hz, MID 250–4000 Hz, HIGH > 4000 Hz. The matching vector is the macro bands in dB, **mean-subtracted** (loudness-normalized), so DNA-to-DNA comparison measures *tone*, not level. A quiet sample is comparable to a loud master.

**Extraction (`DnaExtractor`).** Windowed FFT frames (4096-point, Hann window, 50% overlap) → power spectrum → 16 log-spaced sub-bands → macro-band aggregation → loudness normalization. A silence gate (RMS below −60 dBFS) returns "no DNA", so silence never overwrites a good capture. In v2.4 the extractor also exposes linear-domain band powers (`computeLinearBandPower`) so the loop-scan accumulator and the energy tracker share one FFT pass instead of re-running it.

**Similarity metric (`DnaLibrary::weightedCosine`).** Weighted cosine similarity on the mean-normalized macro-band dB vectors. The producer's band weights (the LOW/MID/HIGH knobs, 0–100%) scale each band's contribution — e.g. low=1.0, mid=0.5, high=0.0 means "match the bass, don't care about the rest." Scores are published as 0–100%.

**Library indexing (`DnaLibrary`).** Samples are analyzed **once at import**; DNA is cached per sample in memory. No per-search recomputation. The v2 scope is a built-in demo library: `scanAndAnalyze()` looks for `~/Music/Apollo/DemoLibrary/*.wav` (generated deterministically by `tools/make_demo_library.py` — 9 synthesized WAVs, 16-bit mono, 44.1 kHz, 4 s each, seed 1234), extracts each file's DNA on the analysis thread, and caches it. Full user-folder indexing is a v3 feature.

**Stability.** Published DNAs are envelope-smoothed across analysis passes (alpha 0.5, analysis thread wakes ~2×/second, settles in about a second) so the ranking doesn't jitter with every transient. The live display path is separate: a lightly-smoothed spectrum at ~12.5 fps (fast-attack 0.55 / medium-release 0.10 envelope) that moves with the music — display-only, it never feeds matching.

**Loop scan (v2.4).** A rolling few-second window can catch an unrepresentative moment (the breakdown instead of the drop). SCAN accumulates the query DNA across a fixed bar count (2/4/8, host tempo via `AudioPlayHead`, 120 BPM fallback) in the linear power domain, converting to dB + loudness-normalizing at the end. The time-integrated DNA latches as the query. Exactly one query mode is ever visible: LIVE / FROZEN / SCANNED.

**Energy-aware recommendations (v2.4).** An RMS-loudness + brightness envelope tracker feeds a slope-heuristic section classifier (STEADY / BUILDUP / PEAK / BREAKDOWN, debounced over 2 consecutive readings). Non-steady sections apply a **bounded multiplicative boost** (×1.25, documented cap ≤2×) to role-tagged library entries — roles come from a closed vocabulary (`riser`, `roll`, `hats`, `impact`, `pad`, `texture`, `neutral`) assigned via `roles.txt` in the demo folder. DNA similarity stays the base score; the boost is never a filter, and with an empty boost list (STEADY or frozen) ranking is bit-identical to v2.3.

## 4. How it works

**Audio pipeline.** The plugin declares two stereo input buses — main ("Sample In", the track) and a second input bus exposed as the VST3 sidechain ("Master In", the master bus). In `processBlock`, the audio thread **only copies samples into lock-free FIFOs** — it never allocates, never blocks, never runs DSP. A background `juce::Thread` (`MatchAnalyzer`) consumes the FIFOs, runs `DnaExtractor` per window, envelope-smooths the result, and re-ranks the library via `rankAgainst()` with the current APVTS band weights. Results publish through atomics and a spin-locked snapshot, so the audio thread and UI read a coherent view at any time. **Audio passes through untouched** — in v2 Apollo is an analyzer; the ranked match list is the product.

**Query source.** The `listenSource` parameter selects sidechain ("Master") or main input ("Track"). GarageBand has no sidechain routing, so the default is Track: put Apollo on a track, play it, and the match list ranks the library by that track's DNA. In Ableton Live 10.1+, switch to Sidechain and route the master bus into the plugin's sidechain input.

**FREEZE.** Captures the stable query DNA at the freeze moment (the match-EQ "capture reference" step); ranking then runs against the snapshot while the TRACK DNA screen shows it static with a FROZEN indicator. Unfreezing resumes live capture seamlessly. Inert when no valid DNA exists yet.

**Key components.**
- `PluginProcessor` — buses, APVTS parameter layout (`dnaLow`/`dnaMid`/`dnaHigh`, `listenSource`, `bypass`, `freezeDna`, `scanLoop`, `scanBars`), per-block forwarding to the analyzer via real-time-safe atomics, state save/restore.
- `MatchAnalyzer` — the analysis engine: FIFO consumption, DNA extraction + smoothing, freeze/scan state machines, energy tracker + section classifier, library ranking, lock-free result publication, plus a `getDnaDebugString()` QA hook.
- `DnaExtractor` / `DnaLibrary` / `DnaFingerprint` — the DSP core described in §3.
- `PluginEditor` — Malström-inspired compact UI: two small DNA viewing screens side by side (TRACK DNA live, SAMPLE DNA for the selected/top match), each rendering the fingerprint as a smooth EQ-filter **curve** through the 16 log-spaced band points (Catmull-Rom smoothed, colored by macro band), three LOW/MID/HIGH rotary knobs (the core interaction), a ranked match list with scores and boost markers, a CAPTURE cluster (SCAN + bar-count selector + FREEZE), source selector, energy/section readout in the status line, and a DNA DBG toggle exposing the QA fingerprint dump.

## 5. Stack

- **Language:** C++17
- **Framework:** JUCE 9.0.2 (fetched via CMake FetchContent; modules used: `juce_audio_utils`, `juce_audio_formats` for WAV demo-library loading, `juce_dsp` for the FFT)
- **Build:** CMake ≥ 3.22 (`juce_add_plugin`), universal binary arm64 + x86_64, macOS 11+ deployment target
- **Targets:** AU, VST3, Standalone (`COPY_PLUGIN_AFTER_BUILD` installs to `~/Library/Audio/Plug-Ins/`)
- **Bundle ID:** `com.apolloaudio.apollo`
- **Demo-library tooling:** Python 3 stdlib only (`tools/make_demo_library.py`)
- **Licensing note:** JUCE's free Starter tier covers up to $20k revenue; Steinberg's VST3 License Agreement must be signed before shipping a closed-source VST3.

## 6. Running it

**Build (macOS):**
```bash
cd apollo-plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Prerequisites: Xcode Command Line Tools, CMake ≥ 3.22, git. First configure downloads JUCE (~a few hundred MB). The VST3 bundle and AU component are copied automatically to `~/Library/Audio/Plug-Ins/`.

**Ableton Live:** Preferences → Plug-Ins → rescan (or restart). Drop Apollo on a track, route the master bus into the plugin's sidechain input (Live 10.1+ exposes the second input bus as the sidechain), open the editor.

**GarageBand:** AU only, no sidechain — insert on a track and it listens to that track.

**Demo content:** `python3 tools/make_demo_library.py` regenerates the 9 sample WAVs + `roles.txt` in `~/Music/Apollo/DemoLibrary/`.

## 7. Status & limitations

**Status: v2.4.1 (2026-10-06).** Full QA gauntlet green — G1 build, G2 debug review CLEAR, G3 QA (40+ criteria) PASS, G4 E2E + usability PASS, G5 fix loop done (cross-thread race cleanup, silent auto-stop disarm). Zero genuine bugs found in the gauntlet. **G6 — Doug's on-device acceptance in GarageBand + Ableton — is pending.**

**Limitations:**
- Demo library only (`~/Music/Apollo/DemoLibrary`); full user-folder indexing with incremental import is a v3 feature.
- Transform/apply (the match-EQ "apply" half) is an explicit non-goal — search first, transformation later, if ever.
- The energy section classifier is v1 slope heuristics and will misread odd arrangements; acceptable because it's a recommendation boost, not a filter.
- Loop scan reads bar length from host tempo and time signature; the 120 BPM fallback applies when the host reports no tempo.
- Tempo/key/loudness/brightness estimators from v1 are kept as stubs for future use, not shown in the v2 UI.
