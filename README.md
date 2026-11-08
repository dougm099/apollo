# Apollo

A VST3/AU audio plugin that captures the **DNA of a track** from live audio
and ranks your sample library by tonal similarity — search by tone, not by
file name.

Play your track and Apollo answers the question sample browsers can't: *which
of my samples will sit in this mix?* It captures a spectral/tonal fingerprint
(the DNA) from the live input and scores every sample in your library against
it. Three band-weight knobs (LOW / MID / HIGH) control which part of the tone
the match cares about — solo the low band to find kicks that sit in your mix's
pocket, match the mids to find loops that won't fight your vocals.

## What's inside

| Path | Notes |
|---|---|
| `apollo-plugin/Source/` | JUCE plugin source: processor, editor, analysis |
| `apollo-plugin/Source/Analysis/` | the DSP — `DnaFingerprint`, `DnaExtractor`, `DnaLibrary`, `MatchAnalyzer` |
| `apollo-plugin/tests/` | C++ test sources (`test_dna`, `test_features`) |
| `apollo-plugin/tools/` | `make_demo_library.py` — generates the deterministic 9-sample demo library |
| `apollo-plugin/PROJECT_REPORT.md` | full write-up: background, methodology, how it works |
| `docs/` | design and verification docs (see below) |

**How the matching works** (`docs/dna-match-design.md`): 16 log-spaced
sub-band energies (40 Hz–20 kHz) aggregated into LOW/MID/HIGH macro bands,
loudness-normalized so comparison measures tone, not level. Weighted cosine
similarity against the library, published as 0–100% scores. The audio thread
only copies samples into lock-free FIFOs; a background thread runs the
analysis — audio passes through untouched. In v2 Apollo is an analyzer; the
ranked match list is the product.

## Build

Prerequisites (macOS): Xcode Command Line Tools, CMake ≥ 3.22, Git.

```bash
cd apollo-plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

First configure downloads JUCE — give it time. The VST3 bundle lands in
`~/Library/Audio/Plug-Ins/VST3/`, the AU in `~/Library/Audio/Plug-Ins/Components/`.

## Run it

**Ableton Live 10.1+:** rescan plug-ins, drop Apollo on a track, route the
master bus into its sidechain input, and flip the query source to Sidechain.

**GarageBand** (no sidechain routing): drop Apollo on the track itself — the
default query source is the track input.

Press FREEZE to latch the captured DNA as the reference (the match-EQ
"capture" step); SCAN accumulates DNA across 2/4/8 bars at host tempo so a
breakdown doesn't skew the query.

## Docs

| Doc | What it is |
|---|---|
| `apollo-plugin/PROJECT_REPORT.md` | complete project write-up |
| `docs/dna-match-design.md` | the DNA-to-DNA product definition (locked 2026-10-06) |
| `docs/decision-specs.md` | the decision aid that led to the DNA definition |
| `docs/debug-playbook.md` / `docs/debug-review-v24.md` | debugging notes and v2.4 review |
| `docs/qa-test-plan.md` / `docs/qa-report-v24.md` | QA plan and results |
| `docs/e2e-test-script.md` / `docs/e2e-report-v24.md` | end-to-end test script and results |
| `docs/usability-audit.md` | usability findings |

## Status

v2.4.1: all gates green — build, debug review, QA (40+ criteria), E2E +
usability, fix loop. The remaining gate is on-device acceptance in a real
session (GarageBand + Ableton). See `docs/e2e-report-v24.md`.

## Before distributing

JUCE needs a licence-tier decision (the free Starter tier covers up to $20k
revenue), and Steinberg's VST3 License Agreement must be signed before
shipping a closed-source VST3.
