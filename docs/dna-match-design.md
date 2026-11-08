# Apollo "DNA" Match — locked design (2026-10-06)

**Decision locked by Doug:** Apollo's definition of "match" is **DNA-to-DNA similarity**.
The DNA of a track is its captured spectral/tonal fingerprint. Apollo captures the
DNA of live input audio and ranks the sample library by how closely each sample's
DNA matches it. Market it as **"the DNA of a track."**

This supersedes the 2026-09-20 `decision-specs.md` transformation specs (groove-lock,
spectral glue, level-ride) as the *product definition* — Apollo is search/discovery,
not transformation. The transform/apply half of match EQ is an explicit non-goal
for now (possible later lane, not v1).

## Inspiration
Match EQ workflow (e.g. iZotope Ozone): capture a reference track's frequency
profile, save it, compare/apply it to your audio. Apollo takes the **capture and
compare** half and points it at a sample library: capture DNA from what you're
playing, match it against the DNA of every sample you own.

## The differentiator
Match EQ plugins match one fixed whole-curve profile. Apollo splits DNA into
frequency bands and matches DNA-to-DNA **from live audio across a library** —
search by tone, not by file name. Band isolation (see below) is the thing no
match EQ plugin does for search.

## In the studio (producer language)
You play your track and Apollo listens, capturing its DNA. Then it ranks your
samples: which kicks share your low-end DNA? Which hats share your top-end DNA?
Solo the low band to find kicks that sit in your mix's pocket; match the mids
to find loops that won't fight your vocals; ignore the highs when they don't
matter. The producer steers *which part of the tone* the match cares about.

## Under the hood (DSP)
- **Capture (live input):** short rolling analysis window on the input bus.
  Per match-EQ practice, a profile stabilizes after ~20–30 s of audio; for
  Apollo's search use-case, shorter windows (a few bars) give a usable DNA,
  and longer listening refines it.
- **Capture (library samples):** analyzed once at import; DNA cached per
  sample in the library database. No per-search recomputation.
- **DNA feature vector:** per-band spectral energies. User-facing split is
  three macro bands — **low / mid / high** — each independently matchable.
  Internally, finer resolution (8–32 log-spaced bands, per `dsp-survey.md`
  §3) can be computed and aggregated up to the macro bands for stability.
- **Normalization:** loudness-normalize before comparing (compare *tone*,
  not level). Short-term LUFS normalization keeps a quiet sample comparable
  to a loud master.
- **Similarity metric:** weighted distance (cosine or Euclidean) across band
  energies. Producer band weights scale each band's contribution — e.g.
  low=1.0, mid=0.5, high=0.0 means "match the bass, don't care about the rest."
- **Composition with existing scope:** DNA is the *tonal* dimension of match.
  It composes with the already-scoped dimensions — beat/tempo and key — and
  with "complementary sound" (deliberate contrast, e.g. bright sample over a
  dark loop) as a negative-weight or contrast mode on the same metric.
- **Smoothing:** per-band envelope followers (50–200 ms, same family as the
  old Spec 2) so DNA doesn't jitter with every transient.

## The demo
Play a dark, warm loop. Apollo surfaces samples whose DNA matches — A/B
against a naive keyword/tag search, where the "matching" samples audibly
belong and the tag-matched ones don't. Then solo the low band and watch the
kick ranking reshuffle: the demo *shows* the band control doing something.

## UI design (locked by Doug 2026-10-06, Malström-inspired)
- Inspiration: Reason's Malström synth — compact hardware-instrument layout: small
  displays, dense labeled sections, everything visible at once.
- The DNA visualization must NOT take up the whole screen (no full-screen
  spectrogram). Instead: **two small DNA viewing screens side by side** —
  left = TRACK DNA (live input fingerprint), right = SAMPLE DNA (selected /
  top-match sample's fingerprint). Each renders the fingerprint as a smooth
  EQ-filter CURVE (not bars) through the 16 log-spaced band points —
  Catmull-Rom smoothed, stroke colored by macro band (amber low / green mid /
  blue high), subtle fill + faint gridlines, match-EQ-display style.
  ~1/4 editor width each.
- **Live + freeze:** the TRACK DNA curve animates live with the music (~10–15
  fps lightly-smoothed display path; matching still uses the stable verified
  DNA). A FREEZE toggle captures the fingerprint at that moment — matching
  then runs against the frozen DNA (the match-EQ "capture reference" step);
  unfreezing resumes live capture. APVTS bool parameter; FROZEN indicator on
  the screen; inert when no valid DNA exists yet.
- **Band match-amount controls:** three ROTARY KNOBS, Ableton-style (vertical-drag
  rotary, 0–100%), labeled LOW / MID / HIGH, controlling each band's contribution
  to the similarity score, playable in real time. Doug's example: 20% low, 100%
  mid, 10% hi. This is the core interaction and the differentiator. (JUCE:
  SliderStyle::RotaryVerticalDrag, Ableton-style look.)
- Match-score readout (winning sample + score) near the SAMPLE DNA screen.

## v2.4 — Loop scan + energy-aware recommendations (specced 2026-10-06, Doug)

### A. Loop scan (SCAN button)
Doug: "drop one sample on one track with the scanner, and the scan and match
is of the time the track is played." The query DNA becomes the
time-integrated fingerprint of the whole played duration, not a rolling
window — a loop's character is the whole loop.
- SCAN button + bar-count selector (2 / 4 / 8 bars, compact combo/segmented
  control, APVTS int "scanBars"). Press SCAN: reads host tempo via
  AudioPlayHead, captures exactly N bars (N * 4 beats * 60/bpm, 4/4; 120 BPM
  fallback when the host reports no tempo), auto-stops, and holds the
  time-integrated DNA as the query. Accumulation in linear power domain,
  dB + loudness-normalize at the end.
- While scanning: status reads "SCANNING… bar X/N", track screen shows the
  accumulating curve live.
- UI as built (v2.4): SCAN ("SCAN 4 BARS" — label carries the bar count) +
  FREEZE + bar-count selector grouped in one CAPTURE cluster; SCAN-while-
  FROZEN unfreezes first, FREEZE-mid-SCAN finalizes the partial scan —
  exactly one visible query mode, never silent stacking.
- Rationale: a 4-second slice can catch an unrepresentative moment (the
  breakdown instead of the drop). Composes with FREEZE (moment DNA) —
  SCAN is duration DNA.

### B. Energy-aware recommendations
Doug: "the plugin should be able to recommend based on the energy of the
track… maybe it's a build up and rolling hi hats might be a good idea."
Matching stops being pure similarity and starts answering "what does this
moment need?"
- Energy tracker (analysis thread): RMS loudness + brightness (high-band
  energy ratio) envelopes over ~8-bar windows.
- Section classifier, heuristics v1:
  - BUILDUP: energy slope positive ≥2 bars + brightness rising → boost
    risers, snare rolls, rolling hats
  - PEAK: high sustained energy → boost impacts, ear candy, complementary
    textures
  - BREAKDOWN: low energy → boost pads, atmospheres, soft keys
  - STEADY: otherwise → pure DNA similarity (current behavior)
- Sample energy-role tags: library entries carry a role tag; demo library
  gains role-tagged samples (riser, rolling-hats, impact, pad — synthesized
  like the existing five).
- UI: small ENERGY readout in the status line as state (e.g. "DNA LIVE •
  ENERGY: BUILDUP • LIB: 9"); when a section is detected, role-appropriate
  samples get a boost in the ranking (amber dot marker on boosted rows).
  DNA similarity stays the base rank — energy is a boost (×1.25, documented,
  ≤2×), never a hard filter. Persistent mode indicator in the status line:
  LIVE / FROZEN / SCANNED.
- Honest caveat: v1 classifier is slope heuristics and will misread odd
  arrangements — acceptable, since it's a recommendation boost. Per-section
  (non-averaged) matching is a later refinement.

## Non-goals (v1)
- Transforming a sample's EQ to force-fit the input (the match-EQ "apply"
  half). Search first; transformation later, if ever.
- Genre/style classification — DNA is acoustic, not cultural.

## Next steps (for Argus)
1. Prototype DNA extraction: FFT → log-spaced band energies → macro
   hi/mid/low aggregation. Validate stability across 4-bar vs 30-s windows.
2. Pick the distance metric (cosine vs Euclidean on log energies) with a
   small labeled test: samples a producer would call "same tone."
3. Band-weight UI in the plugin: three sliders (low/mid/high), plus a
   "contrast" toggle for complementary matching.
4. Offline library indexer: batch DNA extraction + cache; incremental on
   new imports.
5. A/B the demo scenario above against keyword search — that demo is the
   pitch.
