# Apollo v2.4.1 — G4 E2E + Usability Report (Ariadne)

**Build under test:** commit `a458258` — "v2.4.1: G5 fix loop — scanLock/energy
atomic races cleaned, silent auto-stop disarm (Daedalus G2 should-fix)"
(installed AU + VST3 binaries timestamped 2026-10-06 21:46 PT, verified on disk).
**Date:** 2026-10-06 · **Gate:** G4 (E2E + usability), after G3 CONDITIONAL PASS
(Agamemnon, all 40+ numeric criteria green, report: `qa-report-v24.md`).

**Method:** static verification from the v2.4.1 source (state machines, UI
wiring, geometry, labels, tooltips) + numeric inheritance from G3's
independent replica. No pixel was clicked and no knob was dragged — SSH gives
no host automation. Everything below marked PENDING-DOUG needs his hands in a
real host; everything marked PASS was verified in code, not eyeballed.

**v2.4.1 delta vs G3's build (c6b33b5):** 4 files, +95/−33 — Daedalus's two
should-fix items only (scanLock now covers the scalar writes G2-SF1; silent
auto-stop raises a one-way disarm so the SCAN button can't go dead G2-SF2;
energy envelope scalars made atomic). Matching math untouched — G3's numeric
results stand without re-run. I diff-verified the delta contains no behavior
change outside those fixes.

---

## Per-group verdicts (T1–T12)

### T1 — Load & UI layout: PASS (static) / PENDING-DOUG (host)
- Geometry verified in `resized()`: 640×500, two small DNA screens side by side,
  three rotary knobs, CAPTURE cluster, match list, bottom row — all bounds
  accounted, no overlap, no wrapping.
- v2.4 additions present: SCAN toggle + `scanBarsBox` (2/4/8) + FREEZE in one
  framed CAPTURE cluster (`PluginEditor.cpp:376-381`, frame + "CAPTURE" label);
  ENERGY readout rides the status line (state text, not a new widget).
- DNA DBG demoted to a 46px button in the list header (was bottom row) —
  bottom row is back to source selector + Bypass only.
- PENDING-DOUG: actual GarageBand load (v2.3 loaded fine; same component ID).

### T2 — DNA capture: PASS (static) / PENDING-DOUG (host)
- Live display path verified: analysis thread wakes every 80 ms (~12.5 fps),
  cheap display-only spectrum, fast-attack/medium-release envelope; silence
  decays to −90 dB floor instead of freezing. Matching DNA path untouched.
- PENDING-DOUG: bars-to-curve animation feel, settle-without-jitter at real pixels.

### T3 — Match ranking: PASS (numeric, inherited) / PENDING-DOUG (feel)
- G3: self-match 100.00% on all 9 files; reshuffle logic unchanged from v2.1.
- PENDING-DOUG: watching the ranking reshuffle live between loops.

### T4 — Band knobs: PASS (numeric, inherited) / PENDING-DOUG (feel)
- G3: `weightedCosine` byte-unchanged; 100/0/0 and 0/0/100 spot-checks pass;
  0/0/0 guard holds (ranking == 100/100/100).
- Knobs are `RotaryVerticalDrag`, 0–100% text boxes, double-click reset to
  100% (v2.2), real APVTS params (host-automatable).
- PENDING-DOUG: drag feel, live reshuffle responsiveness.

### T5 — DNA screens: PASS (static) / PENDING-DOUG (host)
- Both screens render Catmull-Rom-smoothed EQ curves (v2.3), macro-band
  colored (amber/green/blue), fill + gridlines.
- Mode suffix on the track label: exactly one of `• FROZEN` / `• SCANNING` /
  `• SCANNED`, precedence frozen > scanning > scanned (verified in
  `timerCallback`, `PluginEditor.cpp:265-271`).
- Clickable rows drive the SAMPLE DNA screen; FOLLOW TOP resumes tracking.
- PENDING-DOUG: at-real-pixels legibility, curve smoothness in motion.

### T6 — Tone, not level: PASS (numeric, inherited)
- G3: 0.000 dB drift at −12 dB (criterion ±1 dB). Loudness normalization intact.

### T7 — Robustness: PASS (static) / PENDING-DOUG (soak)
- Audio thread still allocation-free (Daedalus G2 re-verified); scan/energy
  state machines hold no lock pairs that can deadlock; FIFOs drop, never block.
- PENDING-DOUG: 5-minute soak, bypass/un-bypass, 10× transport cycling.

### T8 — FREEZE: PASS (static) / PENDING-DOUG (interaction)
- Freeze captures the last stable query DNA; status `DNA FROZEN`; track screen
  holds the frozen curve with FROZEN indicator; ranking locks against the
  frozen reference.
- Inert with no valid DNA: request ignored, status `NO DNA TO FREEZE — PLAY
  AUDIO`, no crash, no garbage.
- Unfreeze resumes live capture seamlessly.
- PENDING-DOUG: the freeze-moment gesture feel.

### T9 — SCAN: PASS (static + numeric inherited)
- Auto-stop at `scanDeadlineSec`, no second press (G3 H5); bar-count math
  verified (G3 H1/H2/H9: 4 bars @120 = 8.00 s, @100 = 9.60 s; 2/8 bars scale).
- 120 BPM fallback when the host reports no tempo (`computeScanDurationSec`).
- Silent scan: zero windows → nothing latched, falls back to live DNA, no NaN
  (G3 H7); v2.4.1 adds the disarm signal so the SCAN button can't go dead
  after a silent auto-stop (verified in diff + `timerCallback`).
- Scanned DNA ≡ file DNA (cosine distance 0.0000, G3 H6) — the scan genuinely
  integrates.
- PENDING-DOUG: `SCANNING… bar X/N` advancing live; auto-stop timing at
  100/140 BPM; scanned-vs-frozen comparison on varied material.

### T10 — SCAN vs FREEZE: PASS (static) / PENDING-DOUG (first-timer read)
- Exactly-one-mode: status line shows one of `DNA FROZEN` / `SCANNING… bar
  X/N` / `DNA SCANNED (N BARS)` / `DNA LIVE` (precedence chain in
  `getDnaStatusText`, verified); track label carries the matching suffix.
- SCAN while FROZEN: unfreezes first (button + analyzer backstop) — mode
  flips FROZEN → SCANNING, never stacks.
- FREEZE mid-SCAN: analyzer finalizes the partial scan into the frozen
  snapshot and cancels the scan; SCAN toggle dropped so button, parameter,
  and analyzer agree.
- Toggling SCAN off after a latched scan clears the latch → back to LIVE
  (verified falling-edge path). A return path always exists.
- Labels carry time semantics: `SCAN 4 BARS` (dynamic with selector) vs
  `FREEZE`; tooltips explain both before first press.
- PENDING-DOUG: 10.1 — the genuine first-timer read ("what do you think each
  does?"). The code does everything it can; only a fresh pair of eyes can
  grade this.

### T11 — Energy recommendations: PASS (static + numeric inherited)
- G3: classifier simulation (BUILDUP @2.88 s, PEAK @2.40 s, BREAKDOWN @2.40 s,
  STEADY 16 bars with zero false sections); boost factor 1.25 ≤ 2×, scores
  capped at 100; STEADY neutrality proven by construction (empty role list →
  v2.1-identical path); boost never hides entries (all 9 present under every
  section).
- Boost visible: amber dot on boosted rows (`PluginEditor.h:248-253`).
- ENERGY readout: `ENERGY: BUILDUP/PEAK/BREAKDOWN/STEADY` on the status line —
  truthful by construction.
- v2.4.1 made the envelope scalars atomic (UI-thread reads) — display-only,
  no classifier change.
- PENDING-DOUG: musical rightness (does a buildup suggesting rolling hats
  feel right?); misread resilience on odd arrangements (11.3).

### T12 — Bar-count selector: PASS (static) / PENDING-DOUG (discoverability)
- Selector present: `scanBarsBox` {"2 bars","4 bars","8 bars"}, default 4,
  68px wide (fits "8 bars"), tooltip states the duration math including the
  120 BPM fallback. SCAN button label tracks the selection (`SCAN 4 BARS`).
- Host-visible APVTS `AudioParameterChoice`; `barsIdx 0→2, 2→8, else→4`
  drives `computeScanDurationSec`.
- PENDING-DOUG: 12.1 find-it-without-help; 12.3 tempo tracking (math verified,
  wall-clock needs the host).

---

## Usability re-audit verdict (v2.4 section of `usability-audit.md`)

**R1 (SCAN/FREEZE distinction) — MITIGATED in built code.** Labels carry time
semantics (`SCAN 4 BARS` vs `FREEZE`); behaviors feel different by
construction (instant + persistent vs countdown + auto-stop); cross-case
interactions are sane and visible (unfreeze-first, finalize-partial); the
mode indicator shows exactly one mode in two places (status line + track
label). The remaining risk is purely perceptual (T10.1) — Doug's gate.

**R2 (control proliferation) — MITIGATED in built code.** FREEZE + SCAN +
bar-count live in one framed CAPTURE cluster with a single 10px label; FOLLOW
TOP stays with the match list where it belongs; DNA DBG demoted out of the
bottom row to a 46px header button; bottom row is source selector + Bypass
only. Fixed 640×500, every bound accounted — no wrapping, no shrunken hit
targets (buttons 28px tall).

**R3 (ENERGY trust) — MITIGATED.** Boost visible (amber dot); STEADY proven
neutral; boost-only, never a filter.

**R4 (bar-count discoverability) — MITIGATED as far as code can.** Label +
tooltip before first press; countdown teaches semantics live. Real
discoverability is Doug's gate (T12.1).

**R5 (scan + knobs) — CORRECT BY CONSTRUCTION.** Knobs weight the ranking,
not the accumulating curve; no crash path; sane result.

**New-UI heuristic re-check:** SCAN button (128px, clear verb + count),
bar-count selector (compact, adjacent, labeled by its items), ENERGY readout
(state text in the status line — right call, it's not a control), mode
indicator (unmissable, dual-placed). No new issues found. The one thing I'd
still watch at real pixels: whether `SCANNING… bar X/N` at 11px is legible
enough mid-performance — flagged, not blocking.

---

## Bugs for Daedalus

**None.** Zero genuine bugs found. The v2.4.1 delta was diff-verified to
contain only his two should-fix items (already fixed by Hephaestus); no new
issues introduced. Do not dispatch him.

---

## Final human checklist — Doug's GarageBand + Ableton run
*(The 8 highest-signal hands-on checks. Everything else in the script is
covered above or is lower-signal.)*

1. **The v2.4 money demo (GarageBand):** rescan plug-ins, play
   `deep-bass-loop.wav`, press **SCAN** with 4 bars → status shows
   `SCANNING… bar X/4`, auto-stops after ~8 s → `DNA SCANNED (4 BARS)`.
   Self-match should sit at ~100%. This validates the whole v2.4 premise.
2. **Knob reshuffle (GarageBand):** set LOW 20 / MID 100 / HIGH 10, then flip
   to LOW 100 / MID 0 — the ranking must visibly reshuffle *while* you drag.
   This is the core differentiator; judge the feel, not just the result.
3. **Buildup → rolling hats (GarageBand):** loop `riser-swell.wav` (or any
   rising loop) → ENERGY should walk to **BUILDUP** and rolling-hats/riser
   rows should rise with amber dots. Does it feel musically right?
4. **FREEZE lock (GarageBand):** freeze mid-loop on deep-bass, switch the
   track to bright-hats → ranking must NOT follow the hats. Unfreeze → live
   resumes. Then try SCAN while FROZEN and FREEZE mid-SCAN — the mode
   indicator must always show exactly one state.
5. **EQ curves in motion (GarageBand):** watch the TRACK DNA curve while music
   plays — it should dance (~12 fps), settle without jitter, and decay
   gracefully on stop. SAMPLE DNA stays static. If it looks frozen or
   strobe-y, that's a bug — report it.
6. **Clickable rows + FOLLOW TOP (GarageBand):** click match #3 → SAMPLE DNA
   switches to it; re-enable FOLLOW TOP → tracks the leader again.
   Double-click a knob → resets to 100%.
7. **First-timer read (GarageBand, no docs):** look at FREEZE vs SCAN and say
   what each does before pressing. If you guess wrong, tell us exactly what
   misled you — that's a design bug, not your fault.
8. **Ableton Live:** load the VST3 → correct scale, no crash; route audio to
   Apollo's sidechain input → select Sidechain → label flips to SIDECHAIN
   DNA and goes live; draw MID-knob automation in a lane → ranking follows
   the automation. (Live is not on the Mac — this whole check is yours.)

**Bugs found:** none (this report) — log any from the run above with repro steps.

---

## Overall verdict: E2E CONDITIONAL PASS — cleared for G6

Every statically verifiable behavior across T1–T12 **passes**; every numeric
criterion is green via G3 inheritance; the v2.4.1 delta is clean; the
usability re-audit confirms R1/R2 mitigations are in the built code with no
new issues. Zero bugs for Daedalus. The remaining risk is confined to host
behavior no SSH session can exercise — instantiation, drag feel, animation
smoothness, first-timer readability, and the Ableton run.

**The build is cleared for G6 (Doug on-device).** His acceptance run is the
final gate; the 8 checks above are the highest-signal path through it.
