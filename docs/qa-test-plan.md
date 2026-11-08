# Apollo v2 — QA Test Plan (Agamemnon, QA lead)

**Product under test:** Apollo v2 — DNA-to-DNA sample matching plugin (JUCE).
**Design source of truth:** `/Users/doug0/muse/apollo/dna-match-design.md`
**Build owner:** Hephaestus. **Debug:** Daedalus. **E2E:** Ariadne. **Lead:** Doofy.

**Scope of v2 (from the brief):**
- DNA extraction on the input bus: FFT → log-spaced band energies → low/mid/high
  macro bands, 50–200 ms envelope smoothing, loudness-normalized (tone, not level).
- Weighted cosine DNA-to-DNA similarity between live input and library samples.
- Built-in demo sample library (synthetic WAVs with known spectral profiles).
- UI: two small side-by-side DNA screens (TRACK DNA live, SAMPLE DNA of the match),
  three Ableton-style rotary knobs (LOW/MID/HIGH, 0–100%, vertical drag), match-score
  readout near the SAMPLE DNA screen. NO full-screen visualizer.

**Explicit non-goals (do NOT test, do NOT file bugs against):**
- EQ transformation of samples (the match-EQ "apply" half).
- Genre/style classification. Full user-folder library indexing (v3).

---

## Test environment

- Mac: macbook-pro-3 (arm64), GarageBand (latest installed), plus the Standalone
  build for headless-ish audio tests.
- Test audio: the synthetic demo WAVs Hephaestus generates (bass-heavy, bright,
  mid-focused, noise/texture). QA needs their generation parameters/labels —
  request from Hephaestus before running Group C.
- All measurements on Release builds. Debug builds are not shippable evidence.

---

## Group A — Build integrity (Hephaestus delivers, Agamemnon verifies)

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| A1 | Clean Release build (`cmake --build build --config Release`) | 0 errors. All warnings listed in BUILD_LOG.md; no new warnings vs v1 except documented ones | Any error, or an undocumented new warning |
| A2 | All three formats produced | `Apollo.vst3`, `Apollo.component`, Standalone `.app` exist in build output | Any format missing |
| A3 | Install to standard paths | VST3 at `~/Library/Audio/Plug-Ins/VST3/`, AU at `~/Library/Audio/Plug-Ins/Components/` | Installed elsewhere or not installed |
| A4 | AU loads in GarageBand | Plugin appears in GarageBand's AU list, instantiates on a track, editor window opens | Not listed, crash on instantiate, blank editor |
| A5 | Standalone launches | Standalone app opens, selects audio device, processes mic/file input for 60 s without crash | Crash, no audio, device init failure |
| A6 | auval attempt documented | `auval -v` run attempted; result (pass/fail/output) recorded in BUILD_LOG.md. v1 precedent: auval couldn't see the component over SSH — GarageBand load (A4) is the binding criterion, auval is informational | Not attempted / not documented |

---

## Group B — DNA extraction stability

Setup: insert Apollo on a track playing a steady 4-bar loop, no other processing.

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| B1 | Fingerprint stability | Capture DNA at t=10 s, 20 s, 30 s of the same loop. Cosine distance between any two captures < 0.05 | Distance ≥ 0.05 — DNA jitters on static material |
| B2 | Loudness invariance ("tone, not level") | Same loop at 0 dB and at −6 dB input gain. Cosine distance between the two DNAs < 0.10 | Distance ≥ 0.10 — level leaks into the fingerprint |
| B3 | Silence behavior | Mute the input for 10 s. DNA must freeze at last value or decay gracefully — no NaN, no inf, no UI garbage, no crash | NaN/inf in fingerprint, frozen UI, crash |
| B4 | Transient robustness | Play a loop with a loud one-shot hit every 4 bars. DNA sampled just after the hit vs mid-bar: cosine distance < 0.15 (smoothing must absorb transients) | Distance ≥ 0.15 — transients dominate DNA |
| B5 | Window refinement | DNA captured after 4 bars vs after 30 s of the same evolving loop: distance < 0.10, and the 30-s capture is *closer* to a 60-s capture than the 4-bar one is (longer listening refines) | No refinement, or divergence |

*How to measure:* Hephaestus must expose DNA readback for QA — either a debug
log line printing the fingerprint vector per capture, or a temporary test hook.
If no hook exists, B1–B5 degrade to visual check of the TRACK DNA screen bars
(stable bars = pass); file a P1 for the missing hook either way.

---

## Group C — Band knobs affect ranking (the core interaction)

Setup: demo library with ≥4 samples of known, distinct spectral character
(e.g. bass-heavy kick loop, bright hat loop, mid-focused chord loop, broadband
noise loop). Input: a mid-focused loop.

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| C1 | Baseline ranking sane | Knobs at 100/100/100 (LOW/MID/HIGH). Top-ranked sample is the mid-focused loop (the tone match), not the bass-heavy or bright one | Wrong top match — similarity metric is broken |
| C2 | Knobs reshuffle ranking (Doug's scenario) | Move knobs to 20/100/10. Ranking MUST change vs C1 — the mid-focused sample stays top or rises, the bright sample drops | Identical ranking — knobs are decorative |
| C3 | Single-band isolation | Knobs at 100/0/0. Top match MUST be the bass-heavy sample regardless of its mids/highs. Repeat at 0/0/100 → top match MUST be the bright sample | Wrong top match — band weights don't isolate |
| C4 | Knob response monotonic | Sweep MID knob 0→100 in 25 % steps (others fixed at 50). The mid-focused sample's score must be non-decreasing across the sweep | Score jumps erratically — weighting is unstable |
| C5 | Real-time playability | Drag knobs while audio plays. Ranking updates within 1 s, no audio dropout, no UI freeze | Lag > 1 s, dropouts, freeze |
| C6 | Knob extremes | All knobs at 0 %. Plugin must not divide by zero / crash; ranking falls back to unweighted (documented behavior) or holds last ranking | Crash, NaN scores |

---

## Group D — Demo-library match correctness

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| D1 | Self-match | Feed each demo sample's own audio as input (one at a time). Top-1 match must be that same sample, score ≥ 0.95 | Self not top-1 — extraction or caching is wrong |
| D2 | Tone-class match | Feed a *different* loop of the same tone class as a demo sample (e.g. a different bass-heavy loop not in the library). Top-1 must be the same-tone-class demo sample | Cross-class top-1 — DNA doesn't generalize |
| D3 | Library DNA cached | Second scan / second launch: DNA load time for the demo library < 2 s (proves caching, no recompute per launch) | Recomputes every launch |
| D4 | Contrast sanity (if shipped) | If a "contrast" toggle exists: enabling it must invert preference (bright input → dark top match). If not shipped, skip — do not file | Inverted ranking wrong or toggle does nothing |

---

## Group E — UI layout compliance (Doug's locked directives)

Inspect the editor window in GarageBand at default size.

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| E1 | Two DNA screens, side by side | Exactly two DNA displays, horizontally adjacent, labeled "TRACK DNA" (live) and "SAMPLE DNA" (match) | One screen, stacked vertically, or mislabeled |
| E2 | Screens are small | Each DNA screen ≤ ~1/3 of the editor's width. The editor is a compact instrument panel, not a visualizer window | Any DNA display dominating the editor (> 1/2 width) |
| E3 | No full-screen visualizer | No spectrum/spectrogram/waveform view larger than the two DNA screens combined | Full-screen visualizer present — direct violation of Doug's directive |
| E4 | Three rotary knobs | Three knobs labeled LOW, MID, HIGH; rotary, vertical-drag, 0–100 % range with visible value readout | Sliders instead of knobs, wrong labels, wrong range |
| E5 | Knob drag feel | Click-drag vertically on each knob: value changes smoothly, no jumps; double-click (or specified gesture) resets to default | Jumpy, unresponsive, or no reset |
| E6 | Match-score readout | Winning sample name + numeric score visible adjacent to the SAMPLE DNA screen, updating live | Missing, stale, or misplaced |
| E7 | Compact instrument feel | Whole editor visible without scrolling at default size; Malström-like density (no vast empty panels) | Scrolling required or > 40 % empty space |

---

## Group F — Soak / stability (no-crash)

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| F1 | 30-min soak | Continuous loop playback with Apollo analyzing + an LFO/script wiggling all three knobs 0–100 %. No crash, no hang, audio keeps flowing | Any crash or hang |
| F2 | Memory stability | Sample the plugin process RSS at 0, 10, 20, 30 min during F1. Growth < 50 MB over the run (DNA history must be bounded) | Unbounded growth — leak |
| F3 | Rapid preset/knob abuse | 50 rapid knob sweeps 0→100→0 in succession. No crash, final state sane | Crash or stuck state |
| F4 | Host transport abuse | Start/stop GarageBand transport 20× with plugin instantiated. No crash, DNA resumes cleanly each start | Crash or DNA stuck on stale data |

---

## Group G — Build hygiene & v1 regression

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| G1 | No duplicate translation units | `Source/DnaLibrary.cpp` AND `Source/Analysis/DnaLibrary.cpp` both exist (observed 2026-10-06). Exactly one may be compiled; the other must be removed or excluded from the build, documented in BUILD_LOG.md | Both compiled (ODR violation risk) or unexplained |
| G2 | v1 regression | Everything v1 did (instantiate, editor opens, dual input routing, background analysis thread starts) still works | Any v1 behavior broken |
| G3 | BUILD_LOG.md current | v2 section added: what was built, commands run, outcomes, known issues, auval result | Stale — still describes only v1 |

---

## Bug reporting

File format (one per bug, to Doofy → triage to Hephaestus/Daedalus):

```
[SEV] E-ID or C-ID — one-line summary
Repro: exact steps (input audio, knob positions, host)
Expected: (from this plan)
Observed: (what actually happened, with numbers)
Build: git sha or build date, Release
```

Severity: **P0** crash/hang/data loss · **P1** feature broken per this plan ·
**P2** wrong result but workaround exists · **P3** cosmetic.

## Sign-off criteria (v2 ships to Doug when ALL hold)

1. Every test above is PASS, or explicitly WAIVED by Doug (waivers recorded here).
2. Zero open P0/P1 bugs. P2s documented as known issues in BUILD_LOG.md.
3. Doug's three UI directives verified by name: two small screens, three rotary
   knobs, no full-screen visualizer (E1–E3).
4. The demo from the brief works end to end: dark warm loop → tone-matching
   samples surface → low-band isolation reshuffles kick ranking (C2/C3 + D2).
5. QA report delivered to Doofy with per-test results; E2E (Ariadne) GarageBand
   script executed and attached.

---

# v2.4 acceptance criteria (Agamemnon, QA lead — added 2026-10-06, Doug: "flawless")

**Product delta under test:** v2.4 = v2.3 + (A) loop SCAN with 2/4/8 bar-count
selector + (B) energy-aware recommendations (energy tracker → section
classifier → role-tag boost). All v2.1/v2.3 groups (A–G) remain in force;
anything below that contradicts a v2.1 PASS is a v2.4 regression bug, not a
waiver. **Nothing ships to Doug with a known FAIL.**

**New test audio needed from Hephaestus before the pass:** role-tagged demo
WAVs (riser, rolling-hats, impact, pad) + their synthesis parameters and role
assignments, documented in `tools/DEMO_LIBRARY.md`. Plus the two P1 hooks:
(H-a) scan start/stop timestamps + captured sample count in the DNA DBG log;
(I-a) classifier section + envelope values in the DNA DBG log.

---

## Group H — Loop scan (bar-count capture)

Setup: GarageBand projects at known tempos (120 BPM and 100 BPM). Demo WAVs
with known loop lengths. DNA DBG readout on.

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| H1 | Capture duration = N bars at 120 BPM | 4-bar scan captures 8.0 s ± 0.25 s of audio. Repeat at 2 bars (4.0 s ± 0.25 s) and 8 bars (16.0 s ± 0.25 s). Duration from DBG timestamps or captured-sample count | Off by > 0.25 s — bar math wrong |
| H2 | Non-120 tempo honored | Project at 100 BPM, 4-bar scan = 9.6 s ± 0.25 s | Off — host BPM not read via AudioPlayHead |
| H3 | 120 BPM fallback | Host reports no tempo (Standalone, transport stopped): 4-bar scan captures 8.0 s ± 0.5 s, no crash, no hang | Crash/hang, or wildly wrong duration |
| H4 | Status progression | During scan, status reads "SCANNING…" with bar counter X/N progressing 1→N, then returns to match state on auto-stop | No status, frozen counter, never auto-stops |
| H5 | Auto-stop, no second press | After exactly N bars the capture stops by itself and the integrated DNA becomes the query (ranking populates) | Keeps capturing, or needs manual stop |
| H6 | Integrated DNA ≈ full-file DNA | SCAN a full 4-bar loop; cosine distance between the scanned DNA and the library's cached full-file DNA of the same WAV < 0.10 (same accumulation math) | Distance ≥ 0.10 — scan path diverges from file analysis |
| H7 | Scan with no audio | Arm SCAN with input silent for the whole capture: ends gracefully — no crash, no NaN, no fabricated ranking (status shows no-signal / keeps prior query) | Crash, NaN DNA, bogus ranking |
| H8 | SCAN composes with FREEZE | SCAN a loop, then FREEZE: frozen DNA equals the scanned integrated DNA (cosine distance < 0.02). Unfreeze returns to live capture | Freeze captures something else — state confusion |
| H9 | scanBars extremes | scanBars=2 on a 100 BPM project = 4.8 s ± 0.25 s; scanBars=8 at 120 BPM = 16.0 s ± 0.25 s | Wrong at extremes — bar math only works at defaults |

---

## Group I — Energy classifier

Setup: synthesized test signals (Hephaestus script or QA-generated):
- **buildup:** noise with RMS rising linearly over 8 bars + rising high-band ratio
- **peak:** sustained loud broadband loop
- **breakdown:** quiet sparse loop (−20 dB vs peak)
- **steady:** flat loop, constant level and spectrum

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| I1 | Buildup detected | Rising-noise signal → classifier reports BUILDUP within 4 bars of onset | Reports STEADY/PEAK — slope detection broken |
| I2 | Peak detected | Sustained loud loop → PEAK within 4 bars | Not PEAK |
| I3 | Breakdown detected | Quiet loop → BREAKDOWN within 4 bars | Not BREAKDOWN |
| I4 | Steady default, no false positives | Flat loop held 16 bars → STEADY throughout, never a transient false section | False section on flat material |
| I5 | STEADY regression — the critical one | With classifier at STEADY, ranking of all demo WAVs is bit-identical to the v2.1 QA-verified ranking (scores equal to 2 decimals) | Any score change — energy path leaks into the base ranking |
| I6 | Buildup boosts roles, bounded | During BUILDUP, a riser/hats-tagged sample's rank improves vs its STEADY rank; the boost factor is documented and bounded (≤ 2× score multiplier); the STEADY #1 never drops below rank 3 on boost alone | No boost (feature dead), unbounded boost (ranking hijacked), top match buried |
| I7 | Boost is never a filter | During BUILDUP, the best-DNA-similarity neutral sample still appears in the list | Neutral samples vanish — boost became a hard filter |
| I8 | Section transitions | Play breakdown → buildup → peak in sequence: classifier walks BREAKDOWN → BUILDUP → PEAK, no stuck state, no crash | Stuck section, crash on transition |
| I9 | ENERGY readout truthful | UI ENERGY readout always matches the classifier state in the DBG log (sampled 5× during I8) | Readout disagrees with internal state |

---

## Group J — Role tags

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| J1 | Every library entry tagged | All demo WAVs (5 existing + new role WAVs) carry a valid role tag from the documented vocabulary; no empty/unknown tags | Untagged or mistagged entry |
| J2 | New role WAVs self-match | riser, rolling-hats, impact, pad each self-match at ≥ 95% (same bar as D2) | Below — bad synthesis or bad DNA |
| J3 | Neutral never penalized | A neutral-tagged sample's score is identical with the energy boost active vs inactive (boost touches only role-appropriate tags) | Neutral score changes — boost leaks |
| J4 | Tag vocabulary documented | Full role-tag set + per-file assignments in `tools/DEMO_LIBRARY.md` | Undocumented tags — QA can't verify J1/J3 |

---

## Group K — New APVTS parameters (v2.4)

| ID | Test | Pass criterion | Fail = |
|----|------|----------------|--------|
| K1 | scanBars host-visible, correct range | "scanBars" appears in the host automation list; values map to {2, 4, 8} bars | Missing, or wrong range |
| K2 | scanBars drives capture | Automate scanBars=2 → 2-bar capture (H1 math); scanBars=8 → 8-bar capture | Parameter decorative — capture ignores it |
| K3 | Scan arm/disarm host-visible | The scan trigger parameter (name per implementation) is a host-visible bool; automating it arms/disarms a scan | Missing — can't be driven from the host |
| K4 | freezeDna regression | freezeDna still behaves per v2.3 (freeze holds stable DNA, FROZEN indicator, inert with no DNA) — v2.4 didn't break it | Broken by v2.4 changes |
| K5 | Automation-safe under load | Rapid automation of scanBars + band knobs during playback: no crash, no stuck scan state, no stuck freeze | Crash or stuck state |

---

## v2.4 lifecycle gates (Doug: "flawless" — full software lifetime, in order)

**G1 — Build (Hephaestus).** Clean Release, zero errors, AU+VST3 installed,
committed, BUILD_LOG.md entry. Delivers to G2.

**G2 — Debug review (Daedalus).** Reads the v2.4 diff cold: new failure modes,
threading contract re-verified (audio thread still allocation-free;
scan/energy state machines can't deadlock `dnaLock`/`liveLock`), v1/v2.3
do-not-regress list honored. Findings go back to Hephaestus BEFORE QA —
QA never tests a build with known debug findings open.

**G3 — QA pass (Agamemnon, this plan).** Groups A–K. Every FAIL gets a bug
report in the plan's format and returns to G1. G3 repeats until zero FAILs.
**Rule: NOTHING advances to G4 with a known FAIL or an unmeasured criterion.**

**G4 — E2E + usability (Ariadne).** GarageBand E2E script extended for v2.4
(SCAN flow, ENERGY readout, freeze/scan composition) + usability re-audit of
new UI (bar-count selector, ENERGY readout, SCAN button placement). Findings
→ G1.

**G5 — Fix loop.** Any G3/G4 finding returns to G1; the gate that found it
re-verifies the fix. Loop until G3 and G4 are both clean on the SAME build.

**G6 — Doug on-device acceptance.** GarageBand run (his checklist) + Ableton
Live VST3 run when he opens Live. Only G6 closes the release.

**Ship rule (binding):** a build ships to Doug only when G1–G5 are all green
on that exact build. A "small" post-QA change is a new build and re-enters
at G2. No exceptions — that's what "flawless" costs.

## v2.4 sign-off criteria (append to the v2 sign-off block)

6. Groups H–K all PASS on the shipping build; the two P1 hooks (H-a, I-a)
   exist and were used for measurement (no eyeballed numbers).
7. Daedalus's v2.4 debug review is filed with zero open findings.
8. Ariadne's v2.4 E2E extension executed; usability re-audit clean.
9. The v2.4 demo works end to end: SCAN a 4-bar loop → integrated DNA
   matches → trigger a buildup → ENERGY reads BUILDUP → rolling-hats/riser
   rise in the ranking.
