# Apollo v2.4 — QA Report (Agamemnon, QA lead — Gate G3)

**Build under test:** commit `c6b33b5` — "v2.4: loop scan (fixed bar-count capture) + energy-aware recommendations"
**Date:** 2026-10-06 · **Method:** independent Python replica of the DSP written
from the C++ source (extractor, matcher, scan accumulation, classifier) +
source-level verification of state machines, threading-relevant paths, and UI
wiring + artifact checks on the Mac. The replica is NOT copied from
Hephaestus's — it cross-checks his. Script:
`~/workspace/apollo-basis/v24-verification/qa_v24.py` (VM).
**G2 status:** Daedalus cleared with NO blockers; his 2 should-fix items
(scan-scalar atomics, silent-auto-stop disarm) are being fixed by Hephaestus
in parallel as v2.4.1 — neither touches matching math (Daedalus, verified),
so all numeric results below stand. Targeted re-check of the fixed areas
follows v2.4.1; NOT a full re-run.

## Overall verdict: CONDITIONAL PASS

All 40+ statically/numerically verifiable criteria **PASS**. Zero genuine
bugs found — nothing to send to Daedalus. Remaining items are PENDING-DOUG
(host behavior no SSH session can exercise) or PENDING-ARiadne (G4 E2E).
**Cleared for G4.**

---

## Group A — Build integrity: PASS (static) / PENDING-DOUG (host)

| ID | Result |
|----|--------|
| A1 | PASS — clean Release, 0 errors (Hephaestus build log; Daedalus G2 reviewed the diff) |
| A2 | PASS — `AU/`, `Standalone/`, `VST3/` all in `build/Apollo_artefacts/Release/` |
| A3 | PASS — AU installed `~/Library/Audio/Plug-Ins/Components/` (binary 21:34 PT, 22.9 MB); VST3 installed `~/Library/Audio/Plug-Ins/VST3/` (binary 21:46 PT, 23.5 MB) |
| A4 | PENDING-DOUG — GarageBand load (v2.3 loaded fine; same component ID = upgrade) |
| A5 | PENDING-DOUG — Standalone launch (must not be launched over SSH) |
| A6 | PASS (informational) — auval attempted, documented in BUILD_LOG.md ("component not visible to the system registrar yet" — known v1 precedent; GarageBand is the binding criterion) |

## Group B — DNA extraction stability: PASS (regression, numeric)

Extractor refactor (v2.4) is a pure code motion: `extractDna` =
`computeLinearBandPower` + `fingerprintFromLinearPower`, math identical
(Daedalus verified line-by-line; the removed `bandDb` local was write-only).
v2.1 numeric results (B1 <0.05 drift, B2 0.000 dB @ −12 dB, B3/B4) inherit.
Replica re-verified the documented expected vectors to 0.1 dB on all 9 files.

## Group C — Band knobs: PASS (regression, numeric spot-check)

`weightedCosine` byte-unchanged (diff-verified); knob→weight path untouched.
Spot-checks via replica: 100/0/0 → deep-bass-loop 100.0% ✓; 0/0/100 →
deep-bass-loop 100.0% ✓ (matches the v2.1 corrected C3 expectation — the plan's
original bright-hats expectation was wrong, the code is right); 0/0/0 guard
holds (ranking == 100/100/100). C5 knob feel: PENDING-DOUG.

## Group D — Demo-library correctness: PASS (numeric)

| ID | Result |
|----|--------|
| D1 | PASS — self-match 100.00% on all 9 files (5 original + riser/rolling-hats/impact/pad) |
| D2 | PENDING-DOUG (timing) — tone classes verified statically (pairwise macro separations > 15 dB) |
| D3 | PENDING-DOUG (timing) — scan is one-shot on analysis thread; code path reviewed |

## Group E — UI compliance: PASS (static)

v2.3 directives unchanged (two small DNA screens, three rotary knobs, no
full-screen visualizer). v2.4 additions source-verified: SCAN toggle +
bar-count selector (`scanBarsBox`, 2/4/8) + FREEZE in one CAPTURE cluster
(`PluginEditor.h:290-294`); ENERGY readout rides the status line; amber dot
marks boosted rows (Ariadne R3). Feel: PENDING-DOUG / G4.

## Group F — Soak: PENDING-DOUG + static support

Cannot soak headless over SSH. Static: audio thread still allocation-free
(Daedalus G2 re-verified); scan/energy state machines can't deadlock
`dnaLock`/`liveLock`; FIFOs drop rather than block.

## Group G — Hygiene: PASS

| ID | Result |
|----|--------|
| G1 | PASS — stray `Source/DnaLibrary.cpp` still deleted; exactly one compiled |
| G2 | PASS — v1 behaviors intact (audio passthrough untouched) |
| G3 | PASS — BUILD_LOG.md carries the full v2.4 entry (4 mentions, incl. auval + JUCE standalone note) |

## Group H — Loop scan: PASS (8/9 numeric+source, H4/H5 source)

| ID | Result |
|----|--------|
| H1 | PASS — 4 bars @120 BPM = 8.00 s; 2 bars = 4.00 s; 8 bars = 16.00 s (formula-verified, tol 0.25 s) |
| H2 | PASS — 4 bars @100 BPM = 9.60 s (host BPM via AudioPlayHead, verified in `processBlock`) |
| H3 | PASS — no host tempo → 120 BPM fallback = 8.00 s, no crash path (Standalone is the normal case here) |
| H4 | PASS (source) — status reads `SCANNING… bar X/N` (`getDnaStatusText`, `getScanCurrentBar`), then returns to match state |
| H5 | PASS (source) — auto-stop at `scanDeadlineSec`, no second press; latched DNA becomes the query |
| H6 | PASS — scanned DNA vs cached full-file DNA: cosine distance **0.0000** on all 3 test files (criterion < 0.10). The math is equivalent by construction (linear-power averaging + identical tail, constant offset normalized away) |
| H7 | PASS (source) — silent scan: `scanWindows == 0` → nothing latched, falls back to live DNA, no NaN |
| H8 | PASS (source) — post-scan FREEZE copies `scanDna` under `scanLock` → frozen DNA **is** the scanned DNA (distance 0 < 0.02 by construction); freeze-mid-scan finalizes partial accumulation; SCAN-while-FROZEN unfreezes first (exactly one visible mode) |
| H9 | PASS — 2 bars @100 BPM = 4.80 s; 8 bars @120 BPM = 16.00 s |

**P1 hooks (H-a):** present and used — scan state, bars_armed, t_start/t_stop,
captured_windows, captured_samples, host_bpm in `getDnaDebugString`. No
eyeballed numbers in H1–H3/H5/H6.

**Known v2.4.1 items (Daedalus, in fix):** scan-scalar atomics (UI-thread reads
of `scanStartSec`/`scanDurationSec` without lock — benign, worst case one
flickering status frame); silent-auto-stop leaves SCAN button dead until
reload (repro: arm during silence, full bar count). Neither touches matching.

## Group I — Energy classifier: PASS (numeric simulation + source)

Replica: 480 ms cadence, least-squares slopes over 2-bar window, n≥5,
2-consecutive debounce — faithful to `classifySection`.

| ID | Result |
|----|--------|
| I1 | PASS — rising-noise buildup → BUILDUP at t=2.88 s (< 4 bars) |
| I2 | PASS — sustained loud loop → PEAK at t=2.40 s |
| I3 | PASS — quiet loop → BREAKDOWN at t=2.40 s |
| I4 | PASS — flat loop 16 bars → STEADY throughout, zero false sections |
| I5 | **PASS (code-proven + numeric)** — see note below |
| I6 | PASS — 9 queries × 3 sections: STEADY #1 never below rank 3 (worst observed: rank 3); boost factor = 1.25 ≤ 2× (`jlimit(1,2,·)` in code); boosted scores `min(100, score×1.25)` |
| I7 | PASS — all 9 entries present under every section boost (boost never hides) |
| I8 | PASS — breakdown→buildup→peak walk: t=4.32 → 10.08 → 24.48, no stuck state |
| I9 | PASS (source) — ENERGY readout is `energySectionName(getEnergySection())` on the status line: truthful by construction |

**I5 note (critical regression — read carefully):** the v2.4 unboosted path is
**proven identical** to v2.1 by diff: `weightedCosine` byte-unchanged;
`rankAgainst` with empty `boostRoles` executes `(sim*0.5+0.5)*100` exactly as
v2.1 (the boost branch is additive and guarded by `boostRoles.contains`);
extractor refactor math-identical (Daedalus line-by-line). STEADY returns
`{}` from `rolesForSection`, so the v2.4 STEADY ranking **cannot** differ from
v2.1 — same inputs, same outputs, by construction. Numeric cross-check: my
replica reproduces the documented expected DNA vectors exactly and the ranking
order matches the v2.1 report; self-match 100.00% and deep-bass 89.14% match to
2dp. Honest discrepancy: 3 of 5 scores differ from the v2.1 report's C2 table
(air 83.15 vs 72.4, mid 24.02 vs 5.9, bright 17.57 vs 28.3) — investigated: the
documented expected vectors (two independent replicas agree to 0.1 dB) yield MY
numbers, so the v2.1 table was measured under different conditions (likely the
live smoothed query, not file DNA). **This is not a v2.4 regression** — the
code path is provably unchanged. The v2.1 table should be re-baselined, not
the code "fixed."

**P1 hook (I-a):** present and used — section, rms, bright, eSlope, bSlope in
the DBG log.

## Group J — Role tags: PASS

| ID | Result |
|----|--------|
| J1 | PASS — all 9 WAVs tagged from the closed vocabulary (`roles.txt`); no empty/unknown |
| J2 | PASS — riser/rolling-hats/impact/pad self-match 100.00% (≥ 95%) |
| J3 | PASS — neutral scores bit-identical with/without boost (boost touches only boosted roles — code + numeric) |
| J4 | PASS — vocabulary + per-file assignments + section→role mapping + boost factor all documented in `tools/DEMO_LIBRARY.md` |

## Group K — New APVTS parameters: PASS (source)

| ID | Result |
|----|--------|
| K1 | PASS — `scanBars` is an `AudioParameterChoice` {"2 bars","4 bars","8 bars"}, default 4 bars: host-visible |
| K2 | PASS — `barsIdx==0→2, ==2→8, else→4` (`PluginProcessor.cpp:119-122`); drives `computeScanDurationSec` |
| K3 | PASS — `scanLoop` is a host-visible bool; arms/disarms via atomic |
| K4 | PASS — `freezeDna` v2.3 behavior preserved (diff 19c27e0..c6b33b5: freeze branches intact; v2.4 only ADDS scan-aware branches) |
| K5 | PASS (static) — rapid automation: parameter writes are atomic stores on the audio thread; analyzer owns transitions on the analysis thread (Daedalus threading audit). On-device automation feel: PENDING-DOUG |

---

## Bugs for Daedalus

**None.** Zero genuine bugs found. Do not dispatch him. (Daedalus's own 2
should-fix items are already with Hephaestus as v2.4.1.)

## PENDING-DOUG (his acceptance gate — unchanged in kind from v2.1)

GarageBand load + editor feel; Standalone launch; 30-min soak; knob drag
feel; DNA DBG eyeball vs vectors; Ableton Live VST3 run. The v2.4 demo:
**SCAN a 4-bar loop → integrated DNA matches → trigger a buildup → ENERGY
reads BUILDUP → rolling-hats/riser rise in the ranking.**

## Doug's 6-step v2.4 checklist

1. Rescan plug-ins in GarageBand (same component ID — it's an upgrade). Insert Apollo on a track.
2. Play `deep-bass-loop.wav` → expect DNA LIVE, self-match #1 at ~100%.
3. Press **SCAN** (4 bars): status shows `SCANNING… bar X/4`, auto-stops after 8 s at 120 BPM → `DNA SCANNED (4 BARS)`, ranking populates from the integrated DNA.
4. Play a buildup (or the `riser-swell.wav` loop): ENERGY readout should walk toward **BUILDUP**; rolling-hats/riser rows get an amber dot and rise.
5. Press **FREEZE** mid-buildup → `DNA FROZEN`; browse matches against the frozen reference; unfreeze resumes live.
6. Twist the band knobs (try LOW 20 / MID 100 / HIGH 10) and double-click a knob → resets to 100%.

## Sign-off

G3 is **GREEN** on build `c6b33b5` with the noted PENDING-DOUG items (host-only,
same as every prior gate). Per the v2.4 lifecycle: v2.4.1 (Hephaestus's
threading/UI-sync fixes) re-enters at G2→G3-targeted when it lands — the full
pass does NOT need repeating (matching math untouched). **Cleared for G4
(Ariadne E2E + usability).**
