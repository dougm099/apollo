# Apollo v2.4 — GarageBand E2E Test Script

**Author:** Ariadne (E2E feature tester, Apollo dev team — lead: Doofy)
**Date:** 2026-10-06
**Status:** DRAFT v2.4 — run after Hephaestus delivers the v2.4 build AND Agamemnon clears QA. Covers v2.1–v2.3 flows (T1–T8) plus new v2.4 flows: T9 (SCAN), T10 (SCAN vs FREEZE), T11 (energy recommendations), T12 (bar-count selector).
**Design source:** `/Users/doug0/muse/apollo/dna-match-design.md`

## What this tests
Apollo v2.4's whole job: capture the **DNA** (spectral/tonal fingerprint) of live
input audio, and rank a sample library **DNA-to-DNA** by tonal similarity —
with three Ableton-style rotary knobs (LOW / MID / HIGH, 0–100%) that control
how much each band counts toward the match. This script is the human pass in
GarageBand. Each step has an **expected result**; mark ✅/❌ as you go.
v2.4 adds: SCAN (capture a whole loop's DNA over a fixed 2/4/8 bars) and energy-aware recommendations (the plugin reads the track's energy — buildup, peak, breakdown — and boosts samples that fit the moment).

## Prerequisites
- [ ] Apollo v2.4 AU installed (`~/Library/Audio/Plug-Ins/Components/Apollo.component`)
- [ ] Demo library present: `~/Music/Apollo/DemoLibrary/` with the 5 original WAVs
  (`deep-bass-loop.wav`, `bright-hats-loop.wav`, `mid-keys-loop.wav`,
  `air-texture.wav`, `full-mix-loop.wav`) PLUS 4 new role-tagged v2.4 WAVs
  (riser, rolling-hats, impact, soft pad — exact filenames per Hephaestus's
  `tools/make_demo_library.py`; confirm before running)
- [ ] GarageBand open, empty project, one track ready

---

## T1 — Load & UI layout
**1.1** Add Apollo to a track (plug-in slot → Audio Units → Apollo).
> **Expected:** Plugin window opens showing: two small DNA screens side by side
> (left = TRACK DNA, right = SAMPLE DNA), three rotary knobs labeled
> LOW / MID / HIGH, and a match ranking list with a score readout near the
> SAMPLE DNA screen. No visual takes up the whole screen — compact,
> Malström-style instrument layout.

**1.2** Confirm the knob style.
> **Expected:** Knobs are rotary, Ableton-style — drag **vertically** to turn,
> values read 0–100%.

---

## T2 — DNA capture from live audio
**2.1** Import `deep-bass-loop.wav` onto the track and press play.
> **Expected:** TRACK DNA screen comes alive — bars appear and settle within a
> few bars. Bars do **not** jitter wildly on every transient (envelope
> smoothing is working).

**2.2** Stop the transport for 5 seconds, then play again.
> **Expected:** TRACK DNA pauses when stopped, resumes when playing. No crash,
> no stuck/frozen bars.

---

## T3 — Match ranking (tonal similarity)
**3.1** With `deep-bass-loop.wav` playing, read the match ranking.
> **Expected:** `deep-bass-loop.wav` ranks at or near the **top** (it should
> match its own DNA best — sanity check). `bright-hats-loop.wav` ranks near
> the **bottom** (opposite tone).

**3.2** Now play `bright-hats-loop.wav` instead.
> **Expected:** Ranking reshuffles — `bright-hats-loop.wav` jumps to the top,
> `deep-bass-loop.wav` drops to the bottom. SAMPLE DNA screen now shows the
> new top match's fingerprint, and it visually resembles the TRACK DNA screen
> (both tall on the high side).

**3.3** Play `full-mix-loop.wav` (full-spectrum).
> **Expected:** Ranking spreads out — no single extreme winner; scores sit
> closer together than in 3.1/3.2, since a full mix is tonally "average".

---

## T4 — Band knobs: the core differentiator
**4.1** Set knobs to Doug's example: **LOW 20% / MID 100% / HI 10%**. Play
`mid-keys-loop.wav`.
> **Expected:** Ranking favors midrange similarity — `mid-keys-loop.wav` on
> top, even though its low/high content differs from other samples. This is
> the "match the mids, ignore the rest" behavior.

**4.2** Without stopping: turn MID to 0% and LOW to 100% (HI stays 10%).
> **Expected:** Ranking reshuffles **live** toward low-end similarity —
> `deep-bass-loop.wav` jumps to the top. This is the money demo: the knobs
> audibly/visibly change *what "match" means*.

**4.3** Set all three knobs to 0%.
> **Expected:** Ranking goes flat/undefined (no band contributes) — plugin must
> NOT crash or divide by zero. Restoring any knob above 0% brings the ranking
> back.

**4.4** Knob feel: drag each knob vertically through its full range, fast and slow.
> **Expected:** Smooth 0–100% travel, values update live, ranking responds in
> real time. No jumps, no stuck values.

---

## T5 — The two DNA screens
**5.1** While audio plays, watch both screens for 30 seconds.
> **Expected:** TRACK DNA follows the live input; SAMPLE DNA shows the
> currently selected/top-match sample's DNA and updates when the ranking
> leader changes.

**5.2** Select a different sample in the ranking list (if clickable).
> **Expected:** SAMPLE DNA screen switches to that sample's fingerprint;
> score readout updates.

---

## T6 — Tone, not level (loudness normalization)
**6.1** Play `deep-bass-loop.wav` at normal track volume, note the ranking.
Lower the track fader by ~10 dB, note the ranking again.
> **Expected:** Ranking is **unchanged** — Apollo matches tone, not loudness.

---

## T7 — Robustness (must not crash)
**7.1** Bypass Apollo (GarageBand plug-in bypass) mid-playback, then un-bypass.
> **Expected:** Audio passes through clean when bypassed; DNA capture resumes
> on un-bypass. No clicks, glitches, or crashes.

**7.2** While playing, twist all three knobs rapidly at once.
> **Expected:** UI stays responsive; no crash, no audio dropout.

**7.3** Stop/start the transport 10 times quickly.
> **Expected:** No crash, no stuck UI state.

**7.4** Remove Apollo from the track entirely.
> **Expected:** Clean removal. GarageBand stays stable; reopening the project
> does not complain about a missing/crashing plug-in.

**7.5** Play continuously for 5 minutes with the plugin window open.
> **Expected:** No slowdown, no UI freeze, no runaway memory (check Activity
> Monitor only if something feels wrong).

---

## T8 — FREEZE reference capture (shipped v2.3; covered here because T10 needs it)

**8.1** Play `deep-bass-loop.wav`. Mid-loop, press FREEZE.
> **Expected:** TRACK DNA screen holds the frozen curve static with a visible
> FROZEN indicator; status reads "DNA FROZEN" (or equivalent); the ranking
> LOCKS — switch the track to `bright-hats-loop.wav` while frozen and the
> ranking must NOT reshuffle toward the hats (it's still matching the frozen
> bass DNA).

**8.2** Press FREEZE again (unfreeze) while `bright-hats-loop.wav` plays.
> **Expected:** Live capture resumes — the curve animates again, the FROZEN
> indicator clears, and the ranking follows the live input (hats rise to top).

**8.3** Stop the transport (no signal) and press FREEZE.
> **Expected:** Button stays inert — nothing freezes, no crash, no garbage
> DNA. Status should hint why ("NO DNA TO FREEZE — PLAY AUDIO" or equivalent).

---

## T9 — SCAN: whole-loop capture (v2.4)

**9.1** Set the bar-count selector to **4**. Play a loop and press SCAN mid-loop.
> **Expected:** Status shows "SCANNING… bar X/4" with X visibly advancing;
> the TRACK DNA screen shows the curve accumulating live; after exactly
> 4 bars the scan auto-stops (no second press needed) and the ranking
> settles, reflecting the whole 4 bars — not just the final moment.

**9.2** Repeat with **2** bars, then **8** bars.
> **Expected:** The 2-bar scan finishes noticeably faster than the 8-bar one;
> each auto-stops at its own count (status shows bar X/2, bar X/8). No hangs,
> no overruns past N.

**9.3** Whole-loop vs moment: on a loop with audible internal variation
(if none handy, use `full-mix-loop.wav`), run a 4-bar SCAN and note the top
match; then FREEZE a single moment of the same loop and note the top match
(compare the DNA vectors in DNA DBG if you want numbers).
> **Expected:** The two queries can legitimately differ (duration DNA ≠
> moment DNA) — that's the point of SCAN. Both rankings must look sane (no
> random reshuffles, no crashes). If they're identical on strongly varied
> material, flag it: the scan may not actually be integrating.

**9.4** With the transport stopped, press SCAN.
> **Expected:** Defined, non-destructive behavior — either the button stays
> inert with a hint, or it scans silence and reports no DNA. Must not crash,
> hang, or corrupt the current query.

---

## T10 — SCAN vs FREEZE: can a first-timer tell them apart?

**10.1** Without reading any docs: look at the FREEZE and SCAN buttons and
say out loud what you think each does.
> **Expected:** FREEZE reads as instant ("hold this moment"), SCAN reads as
> timed ("capture N bars"). If you guess wrong or can't tell, that's a FAIL
> against the design — note exactly what misled you.

**10.2** While FROZEN, press SCAN. Then, mid-SCAN, press FREEZE.
> **Expected:** The UI always shows exactly ONE active query mode (LIVE /
> FROZEN / SCANNING / SCANNED) — never an ambiguous "both engaged" state.
> Whatever the defined interaction is (scan replaces freeze, freeze wins,
> second press ignored…), it must be visible, not silent. If the behavior is
> undefined or invisible, log it as a bug for Hephaestus.

**10.3** Complete a 4-bar SCAN, then press FREEZE on a striking moment.
> **Expected:** The freeze captures a NEW moment query, clearly replacing the
> scanned one (or the UI explicitly says which query is active). No silent
> stacking of two queries.

---

## T11 — Energy-aware recommendations (v2.4)

**11.1** Play a buildup-style loop (rising energy — loop the riser demo WAV
or any rising loop). Watch the ENERGY readout and the ranking.
> **Expected:** ENERGY reads BUILDUP (or equivalent); samples tagged
> riser / rolling-hats get visibly boosted — you can SEE why the ranking
> changed (badge/marker on boosted rows, per the build). The boost should
> feel musically right: a buildup suggesting rolling hats is the demo.

**11.2** Play a steady, even loop (`deep-bass-loop.wav`, no build).
> **Expected:** ENERGY reads STEADY (or neutral) and the ranking matches the
> familiar DNA-only ranking from T3 — energy adds nothing when there's no
> section to call. If the ranking differs from T3 here, the classifier is
> leaking boost into STEADY: flag it.

**11.3** Misread resilience: play something the classifier could get wrong
(e.g. a sparse breakdown with one loud spike, or an odd arrangement).
> **Expected:** Even if the ENERGY label is wrong, the ranking stays
> sensible — energy is a BOOST, never a hard filter. No sample should vanish
> from the list purely because of a misread. Wrong label + sane ranking =
> acceptable; wrong label that hides the honest DNA matches = bug.

**11.4** While an energy boost is active (BUILDUP showing), drag the MID
knob to 0% and back to 100%.
> **Expected:** Knobs still work and compose with the boost — no fighting,
> no crash, ranking responds to both inputs sanely.

---

## T12 — Bar-count selector discoverability (v2.4)

**12.1** Before pressing SCAN: find the 2/4/8 selector without help.
> **Expected:** You find it, and you understand it sets the scan length in
> bars BEFORE pressing SCAN. If you can't find it, or 2/4/8 means nothing to
> you, that's a discoverability FAIL — note what would have helped (label?
> tooltip?).

**12.2** Change the selector to 8, press SCAN.
> **Expected:** The countdown honors the new selection ("SCANNING… bar X/8").

**12.3** Tempo check: set the GarageBand project to 100 BPM, run a 4-bar
SCAN and time it (~9.6 s); repeat at 140 BPM (~6.9 s).
> **Expected:** Scan duration tracks the host tempo (4 bars = 16 beats). If
> both take the same wall-clock time, the plugin isn't reading host tempo —
> flag it.

---

## Results log

| Step | ✅/❌ | Notes |
|------|------|-------|
| 1.1 |      |       |
| 1.2 |      |       |
| 2.1 |      |       |
| 2.2 |      |       |
| 3.1 |      |       |
| 3.2 |      |       |
| 3.3 |      |       |
| 4.1 |      |       |
| 4.2 |      |       |
| 4.3 |      |       |
| 4.4 |      |       |
| 5.1 |      |       |
| 5.2 |      |       |
| 6.1 |      |       |
| 7.1 |      |       |
| 7.2 |      |       |
| 7.3 |      |       |
| 7.4 |      |       |
| 7.5 |      |       |
| 8.1 |      |       |
| 8.2 |      |       |
| 8.3 |      |       |
| 9.1 |      |       |
| 9.2 |      |       |
| 9.3 |      |       |
| 9.4 |      |       |
| 10.1 |      |       |
| 10.2 |      |       |
| 10.3 |      |       |
| 11.1 |      |       |
| 11.2 |      |       |
| 11.3 |      |       |
| 11.4 |      |       |
| 12.1 |      |       |
| 12.2 |      |       |
| 12.3 |      |       |

**Bugs found:** (describe, or "none")

**Overall verdict:** PASS / FAIL — signed ______ date ______
