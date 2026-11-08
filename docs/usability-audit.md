# Apollo v2 — Usability & Interactivity Audit

**Auditor:** Ariadne (E2E feature tester, Apollo dev team — lead: Doofy)
**Date:** 2026-10-06
**Scope:** `Source/PluginEditor.h` / `Source/PluginEditor.cpp` (the entire UI),
`Source/PluginProcessor.cpp` (parameter definitions), `MatchAnalyzer::getDnaStatusText()` (feedback states).
**Method:** full code read of the shipped v2.1 tree. **Hard boundary:** no GUI
automation is possible over SSH — nothing here was clicked, dragged, or
seen at real pixels. Every finding below names the exact file/location so it
is verifiable, and the feel items only Doug's hands can judge are in the
checklist at the end.

## Headline verdict

**Yes — usable enough to put in front of Doug, after the three must-fix items.**
The core loop is wired end to end: play audio → "DNA LIVE" → bars animate →
ranking populates → dragging a knob reshuffles the list live. The feedback
states cover every condition (scanning / empty / no signal / live), the
knobs are a good size (~73px rotary, not fiddly), and the layout honors all
of Doug's directives (two small screens, three rotary knobs, compact,
no full-screen visualizer). The gaps are interactivity depth, not structure:
the match list is the biggest one — right now it is a read-only scoreboard.

---

## BLOCKER — fix before Doug's demo

### B1. Match-list rows are not clickable — SAMPLE DNA always follows #1
**Location:** `PluginEditor.h` — `MatchListModel` (no `listBoxItemClicked` /
`selectedRowsChanged` override); `PluginEditor.cpp` `timerCallback()` always
calls `analyzer.getTopMatchDna()`.
**Problem:** A producer's first instinct on seeing a ranked list is to click
candidate #2 or #3 to inspect it. Today a click does nothing visible, and the
SAMPLE DNA screen is permanently slaved to the top match. For a
*search/discovery* product, inspecting candidates IS the job.
**Fix (Hephaestus):** on row click, store the selected entry index; drive
`sampleScreen` from the selected entry's DNA instead of the top match's, and
add a small "FOLLOW TOP" toggle (or click the selected row again to
re-follow). Keep `matchReadout` showing the #1 match regardless.

### B2. The Sidechain source option is a dead end in GarageBand
**Location:** `PluginEditor.cpp` — `sourceBox` ("Track"/"Sidechain");
`PluginProcessor.cpp` `processBlock()` — `pushMasterBlock` only runs
`if (sideIns > 0)`.
**Problem:** GarageBand exposes no sidechain bus, so choosing "Sidechain"
parks the UI on "NO SIGNAL — PLAY AUDIO" forever with no explanation of why.
A confused user can strand themselves here and conclude the plugin is broken.
**Fix (Hephaestus):** in `resized()`/`timerCallback()`, disable the
"Sidechain" combo item (or the whole `sourceBox`) when the sidechain input
bus is not active — `processorRef.getChannelCountOfBus(true, 1) == 0` — and
fall back to "Track". One line of honesty in the status text beats a trap.

### B3. Knobs ignore double-click reset (Ableton muscle memory)
**Location:** `PluginEditor.cpp` — `setupKnob` lambda.
**Problem:** Every Ableton user double-clicks a knob to reset it. Today
double-click does nothing (`setDoubleClickReturnValue` is never called), so
recovering Doug's 20/100/10 demo setup after experimenting means re-dragging
three knobs by hand.
**Fix (Hephaestus):** `k.setDoubleClickReturnValue (true, 1.0);` in
`setupKnob`. One line.

---

## POLISH — small, should-fix

### P1. No tooltips anywhere — first-time discoverability
**Location:** `PluginEditor.cpp` constructor (no `setTooltip` calls).
**Problem:** The 10-second test mostly passes ("NO SIGNAL — PLAY AUDIO"
tells you what to do), but nothing explains what the knobs *do*. A
first-timer has to guess that LOW/MID/HIGH weight the match.
**Fix:** `lowKnob.setTooltip ("How much the LOW end counts toward the match")`
(etc.), plus tooltips on the DNA screens ("Live fingerprint of what you're
playing" / "Fingerprint of the selected match").

### P2. "LIBRARY: EMPTY — run tools/make_demo_library.py" is dev-speak in a user UI
**Location:** `MatchAnalyzer.cpp` `getDnaStatusText()`.
**Problem:** A producer has no idea what `tools/make_demo_library.py` is.
**Fix:** user-facing text: `"LIBRARY: EMPTY — no samples found"`. Keep the
script path in the QA docs, not on screen.

### P3. All-knobs-at-zero silently falls back to equal weights
**Location:** `DnaLibrary::rankAgainst` (v2.1 guard) + no UI indication.
**Problem:** The behavior is correct (no flat-50% bug), but the user gets no
signal that their 0/0/0 setting was overridden — the list just looks
"normal", which is mildly dishonest UI.
**Fix (cheap):** when the weight sum is ~0, show `"TOP MATCH (all bands off — using equal weights)"`
in `matchReadout`, or flash the BAND MATCH AMOUNT label. Optional; the
silent fallback is acceptable if Hephaestus is short on time.

### P4. "DNA DBG" ships in the user-facing UI
**Location:** `PluginEditor.cpp` — `dbgButton`, bottom row.
**Problem:** It's a QA hook with a jargon label sitting next to Bypass.
Harmless, but it cheapens the instrument feel.
**Fix:** keep the hook, relabel to `"DNA"` with a tooltip ("Show fingerprint
vectors (debug)"), or hide it unless Shift is held. Doug's call.

### P5. Knob % text box is small for precise entry
**Location:** `PluginEditor.cpp` — `setTextBoxStyle (TextBoxBelow, false, 56, 18)`.
**Problem:** 56×18px at 12–13pt is tappable but cramped for typing "20".
Fine on desktop, worth a glance on Doug's screen.
**Fix:** widen to 64×20 if layout allows; verify at real pixels (Doug).

---

## V3 IDEAS — do not build now, but don't lose

- **V1. Audition the match:** click a candidate to *hear* it (needs a sample
  playback engine; v2 is analysis-only by design).
- **V2. Freeze TRACK DNA:** snapshot the live fingerprint so you can keep
  comparing while the track keeps playing / while you tweak knobs.
- **V3. Contrast mode:** "find me something *different*" (same metric,
  inverted) — already in the design brief as a later toggle.
- **V4. Numeric dB readout on the DNA screens** (bars only today).
- **V5. Knob gestures:** fine-drag with Shift, MIDI-learn the three knobs.

---

## What's already good (don't regress)

- Feedback states are complete: SCANNING → EMPTY → NO SIGNAL → DNA LIVE,
  each with the source and library count. This is the strongest part of the UI.
- Knob hit targets (~73px rotary in a 93×120 cell) are comfortably large;
  `RotaryVerticalDrag` matches Ableton; the % text box is editable for exact entry.
- Timer-driven refresh (2 Hz) + analysis-thread ranking means knob drags
  reshuffle the list within ~1 s with no audio-thread risk.
- Producer language throughout ("TRACK DNA", "BAND MATCH AMOUNT",
  "TOP MATCH") — no DSP jargon on screen except P2/P4 above.
- Layout honors every locked directive: two small screens (~160px each),
  three knobs, 640×500 compact window, no full-screen visualizer.

## DAW-SPECIFIC SECTION — GarageBand vs Ableton Live
(Doug's directive 2026-10-06: usable in both. Ableton Live is NOT installed
on the Mac — every Live item below is code-verified only; the feel-check
stays on Doug's human checklist.)

### GarageBand (AU — `~/Library/Audio/Plug-Ins/Components/Apollo.component`)
- **Format fit: good.** AU is GB's native format; the installed binary is
  fresh (2026-10-06 20:48). Doug already verified v1 loads; v2 is the same
  component ID (correct — it's an upgrade).
- **No sidechain routing in GB — this is B2, and it's GB-specific.** The
  "Listen to:" combo must behave as Track-only here: disable the Sidechain
  item when `getChannelCountOfBus(true, 1) == 0`, default to Track. Selecting
  Sidechain in GB strands the UI on "NO SIGNAL — PLAY AUDIO" with no way out
  except knowing to switch back.
- **Fixed 640×500 window: fine.** GB hosts AU views at fixed size; the layout
  is complete at exactly this size (verified in `resized()` — nothing
  clipped, ~4 match-list rows visible).
- **AU parameters:** dnaLow/dnaMid/dnaHigh are real `AudioParameterFloat`s, so
  they appear in hosts that expose generic AU params. GB's own automation UI
  for third-party AUs is limited — nothing to fix in the plugin.

### Ableton Live (VST3 — `~/Library/Audio/Plug-Ins/VST3/Apollo.vst3`)
- **Format fit: good, UNTESTED on-device.** VST3 binary installed and fresh
  (2026-10-06 20:48). Live is not on the Mac, so first load is PENDING-DOUG.
- **Sidechain routing EXISTS in Live — SIDECHAIN DNA becomes a live feature.**
  `isBusesLayoutSupported()` accepts the second input bus, and Live exposes
  it as the sidechain source. But the UI gives zero guidance: the combo just
  says "Track / Sidechain". A Live user won't know they must route audio to
  Apollo's sidechain input *in Live's UI* before the option means anything.
  **Fix (polish, Hephaestus):** tooltip on `sourceBox`: "In Ableton: route
  audio to Apollo's sidechain input, then select Sidechain here." + the
  auto-disable from B2 covers the GB case.
- **Knob automation: works, unhinted.** dnaLow/dnaMid/dnaHigh are APVTS float
  params named "DNA Low/Mid/High" — Live will offer them in automation lanes
  and they drive the matcher live via `processBlock`. Nothing in the UI hints
  this is possible. **Fix (polish):** append "(automatable)" to the knob
  tooltips from P1. This is a selling point for the Live crowd — don't hide it.
- **HiDPI: no code action.** JUCE renders in logical points and the host
  supplies the scale factor; the custom knob strokes scale with the graphics
  context. The 640×500 fixed size is small but deliberately compact
  (Malström-style). Live users can't enlarge it — acceptable, note for the
  human checklist.
- **VST3 note:** `listenSource` is a Choice param — automatable/steppable in
  Live, harmless.

### DAW-assumption flags (UI assuming host capabilities)
1. **B2 restated:** source selector assumes a sidechain *might* exist; in GB
   it never does → disable, don't just leave it selectable.
2. **No assumption of resizability** — editor is fixed-size (no
   `setResizable` call found). Correct for both hosts; do not "fix" by making
   it resizable without a layout reflow plan.
3. **No host-specific code paths** — the same binary serves both DAWs, which
   is right; all host differences are handled by bus queries at runtime.

## v2.4 additions — pre-build design review + verify-on-landing
(Ariadne, 2026-10-06. Status: Hephaestus is building v2.4 NOW. Nothing below
was seen at real pixels — these are risks flagged from the v2.4 spec so the
build can avoid them, plus verification steps for Doug's run. On-device feel
stays Doug's gate, as always.)

### State visibility: three query modes, exactly one must be obvious
v2.4 has THREE query modes: LIVE (rolling), FROZEN (moment), SCANNED
(duration). v2.3 already handles LIVE/FROZEN with the FROZEN indicator. The
new risk: after a SCAN completes, does the UI show "this ranking comes from a
4-bar scan" as clearly as FROZEN does? If the scanned state looks identical
to live, the user will misread the ranking. **Requirement:** a persistent,
unmissable mode indicator — LIVE / FROZEN / SCANNED (4 bars) — most naturally
in the status line, which already carries this kind of state. Verify in
T10.2/T10.3.

### R1 (blocker-risk): SCAN vs FREEZE confusion
Two capture buttons, adjacent, similar job. The spec distinguishes them
(instant moment vs timed duration), but if they look like twins the
distinction lives only in the manual. **Requirements:** (a) labels must carry
the time semantics — "FREEZE" alone doesn't say "moment"; the SCAN button
should show its bar count ("SCAN 4 BARS" or a visible 2/4/8 next to it);
(b) the behaviors must FEEL different — FREEZE is instant with a persistent
state, SCAN has a visible countdown then auto-stops; (c) the undefined
interactions (SCAN while FROZEN, FREEZE mid-SCAN) must do something sane and
visible, never silently stack or silently ignore. Verify T10.1–T10.3
on-device.

### R2 (blocker-risk): control proliferation — instrument vs cockpit
Count the bottom-area controls after v2.4: source selector, Bypass, DNA DBG,
FOLLOW TOP, FREEZE, SCAN + bar-count selector, ENERGY readout. That's 7–8
controls around a 640×500 instrument. Malström works because every control
earns its place and they're grouped by function. **Requirements:** group
FREEZE / SCAN / bar-count into one labeled CAPTURE cluster; keep FOLLOW TOP
with the match list (it belongs to the list, not the capture group); demote
DNA DBG per P4 (it's still sitting in the bottom row — now more urgent with
three new controls). If the bottom row wraps or crowds at real pixels, that's
the signal to cut or regroup — never to shrink hit targets.

### R3: ENERGY readout trust
A heuristic classifier WILL be wrong sometimes — the spec is honest about
this. The danger isn't the misread, it's the CONFIDENT presentation of a
misread. "ENERGY: BUILDUP" stated as fact over a breakdown teaches the user
the plugin is unreliable. **Requirements:** (a) make the boost's effect
visible (badge on boosted rows — the spec has this; verify it's actually
noticeable, T11.1); (b) STEADY must be truly neutral — verify T11.2, ideally
numerically (ranking with classifier on + steady loop == ranking with the
boost path disabled); (c) a misread must never HIDE the honest DNA matches —
boost-only, verified in T11.3.

### R4: bar-count selector discoverability
"2/4/8" is meaningless without tempo context, and the selector is a new
concept (every previous capture control was instant). The "SCANNING… bar X/N"
countdown is the right mitigation — it teaches the semantics live, on first
use. Flag for the build: the selector needs a label and tooltip BEFORE first
press (T12.1), not just the countdown after. Edge to document: what N means
if the user changes tempo mid-scan (document, don't necessarily handle).

### R5: scan + knobs interaction
Changing LOW/MID/HIGH mid-scan: the accumulating curve is DNA (pre-weight),
the ranking applies weights — so knob turns should affect the final ranking
but not the curve. That's correct behavior, but is it what a user expects?
Minor; verify no crash and a sane result (T11.4 covers the analogous energy
case; extend it to mid-scan knob turns if time allows).

### What I'd cut or rearrange (opinion — Doug's call)
1. Demote DNA DBG out of the bottom row (P4 stands, now urgent with three
   new controls arriving).
2. Group capture as one cluster — [FREEZE] [SCAN ▾ 4 bars] with a small
   "CAPTURE" mini-label — instead of two lookalike buttons floating
   separately.
3. Put the ENERGY readout IN the status line ("DNA LIVE • ENERGY: BUILDUP •
   LIB: 9") rather than a new widget — it's state, not a control.
4. If space gets tight: FOLLOW TOP could become click-again-to-unfollow on
   the selected row instead of a separate toggle — but it's shipped and
   understood, so don't churn it in v2.4.

### v2.4 verification checklist (for the build review + Doug's run)
- [ ] Mode indicator always shows exactly one of LIVE / FROZEN / SCANNED (+bars).
- [ ] SCAN while FROZEN and FREEZE mid-SCAN do something sane and visible.
- [ ] SCANNING countdown advances and auto-stops at N bars at 100 and 140 BPM.
- [ ] ENERGY boost is visible (badges) and STEADY is provably neutral.
- [ ] Bottom row: no wrapping, no shrunken hit targets; capture controls grouped.
- [ ] R1–R5 re-checked at real pixels; on-device feel items stay Doug's gate.

## Boundary statement

No pixel was clicked and no knob was dragged in this audit — SSH gives no
host automation. Geometry, event wiring, and the state machine were verified
in code; drag feel, animation smoothness, and real-pixel legibility are
Doug's gate (see checklist).
