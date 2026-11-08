# Daedalus — Gate 2 debug review: Apollo v2.4 (commit c6b33b5)

**Date:** 2026-10-06 · **Reviewer:** Daedalus (debug engineer) · **Gate:** G2 of the v2.4 gauntlet
**Scope:** full diff of `c6b33b5` (12 files, +1054/−70) — scan accumulation + fixed
bar-count timing, energy classifier, role tags + boost, new APVTS params
(scanLoop/scanBars/freezeDna), CAPTURE cluster UI, 80 ms UI timer, display-only
spectrum path.
**Method:** full diff read of MatchAnalyzer.{h,cpp}, DnaExtractor.{h,cpp},
DnaLibrary.{h,cpp}, PluginProcessor.cpp, PluginEditor.{h,cpp}; lock-ordering
audit; audio-thread audit; divide-by-zero audit; cross-check vs the v2 debug
playbook. No code was modified by the reviewer.

## Verdict: CLEAR FOR QA (no blockers)

Two should-fix items below — both real, neither crashes, neither corrupts the
matching path (which is properly synchronized and bit-identical to v2.3 where
it must be). Route them to Hephaestus for the G5 fix loop; QA's numeric pass
can proceed in parallel.

---

## SHOULD-FIX 1 — Cross-thread data races on scan scalars + energy debug floats

**Severity:** should-fix (real UB; benign in practice — worst case is one
flickering status/debug frame; no audio impact, no crash vector).

**Where:**
- `Source/Analysis/MatchAnalyzer.cpp:766` — `getScanCurrentBar()` is
  `const noexcept`, reads plain non-atomic `scanStartSec` / `scanDurationSec`
  with **no lock**. Called from `getDnaStatusText()` (line 339) on the UI
  timer thread (80 ms). The analysis thread writes both at lines 689/694
  (`updateScanState`) with **no lock** either.
- `getDnaDebugString()` (UI thread) reads `scanStartSec`, `scanStopSec`,
  `scanWindows`, `scanTotalSamples` *under* `scanLock` — but the writer
  (`updateScanState`) never takes `scanLock` for those members, so the lock
  is theater: no happens-before edge exists. Same race.
- `energyLastRmsDb`, `energyLastBright`, `energyLastESlope`, `energyLastBSlope`
  (plain `float` members): written on the analysis thread
  (`updateEnergy`, `classifySection`), read on the UI thread in
  `getDnaDebugString()` with no lock at all.

**Why it matters:** formally UB per the C++ memory model (and exactly the
pattern Hephaestus asked me to verify — it is *not* clean). On ARM64 a torn
8-byte double read is unlikely but the compiler is free to reorder/cache.
TSan would flag every one of these.

**Concrete fix (Hephaestus):**
1. Make `scanLock` mutable (`MatchAnalyzer.h:265`) and take
   `juce::SpinLock::ScopedLockType` inside `getScanCurrentBar()`.
2. In `updateScanState()`, extend the existing `scanLock` scope to cover the
   scalar writes (`scanDurationSec`, `scanStartSec`, `scanStopSec`,
   `scanDeadlineSec`, `scanWindows`, `scanTotalSamples`) — the lock is
   already taken two lines later for the `scanDna` reset; just widen it.
   (`scanAccum` needs no lock: analysis-thread-only on both sides.)
3. Make the four `energyLast*` members `std::atomic<float>`
   (lock-free on x86-64/ARM64; single-writer/single-reader).

---

## SHOULD-FIX 2 — SCAN button goes dead after a silent auto-stop

**Severity:** should-fix (usability dead-end; no crash).

**Where:** `Source/Analysis/MatchAnalyzer.cpp` (`updateScanState`, auto-stop
branch) + `Source/PluginEditor.cpp` (timerCallback SCAN sync).

**Repro:** arm SCAN during silence (e.g. a silent count-in) and let it run the
full bar count. `scanWindows == 0`, so nothing latches: `scanActive=false`,
`scanLatched=false`. But the APVTS `scanLoop` parameter is still `1.0`
(nothing ever clears it — the analyzer can't write parameters, and the
`else if (!wanted)` branch never runs because `wanted` stayed true, so
`scanSeenWanted` stays true).

**Consequence:** the editor syncs the button OFF (`scanOn = active||latched`
is false) while the parameter reads ON. The user's next click flips the
button OFF→ON and calls `setValueNotifyingHost(1.0f)` on a parameter already
at 1.0 — a no-op (`AudioParameterBool::operator=` has an equality guard, and
even without it the analyzer's edge detector sees no false→true transition).
The button snaps back to OFF on the next timer tick. **The SCAN button is
permanently dead until plugin reload or host automation** — every click is
swallowed.

**Concrete fix (Hephaestus):** one-way disarm signal, parameter write stays
on the message thread:
1. Add `std::atomic<bool> scanDisarmNeeded { false };` to `MatchAnalyzer`.
   Set it `true` in the auto-stop branch when `scanWindows == 0`
   (nothing captured).
2. Add `bool takeScanDisarmRequest()` (load + clear, relaxed).
3. In the editor's `timerCallback` scan-sync section: if
   `analyzer.takeScanDisarmRequest()`, call
   `processorRef.apvts.getParameter("scanLoop")->setValueNotifyingHost(0.0f)`.
   Do NOT do this unconditionally when `paramOn && !scanOn` — that would
   clobber a fresh arm during the ≤80 ms before the analysis thread picks
   up the rising edge. The explicit flag avoids the race.

---

## Verified clean (no action)

- **Static/member name collision** (`scanDurationSec`): resolved. Static is now
  `computeScanDurationSec` (MatchAnalyzer.h:172, .cpp:660); member
  `scanDurationSec` (h:262) is separate. Call site .cpp:689 uses the static.
  The build error Hephaestus hit is genuinely fixed.
- **Lock ordering:** `frozenLock`, `scanLock`, `dnaLock` are taken in separate
  `if/else` branches in `rankLibrary()` — never nested. `liveLock` is
  independent (display path only). The v2 invariant holds: no lock is ever
  held with another.
- **Audio-thread safety:** `processBlock` additions are atomic stores only
  (`setScanRequested`, `setScanBars`, `setHostTempo`) plus an `AudioPlayHead`
  query (host-owned object; `getBpm().orFallback()`, `getTimeSignature()`
  allocate nothing). No locks, no allocation on the audio thread. The v2
  contract holds.
- **Scan accumulation math:** `accumulateScanWindow` divides by `scanWindows`
  only after incrementing it (≥ 1, early-return otherwise); auto-stop guards
  `scanWindows > 0` before averaging; silence-only scans latch nothing and
  fall back to live DNA. `fingerprintFromLinearPower` is algebraically
  identical to the old `extractDna` tail (refactor verified line-by-line;
  the removed `bandDb` local was write-only). `scanTotalSamples +=
  (int)(sampleRate * 4.0)` matches the actual 4 s analysis window
  (MatchAnalyzer.cpp:368). ✓
- **STEADY bit-identical claim (QA I5):** holds. With `boostRoles` empty,
  `rankAgainst` assigns `boosted=false` to all entries and the score math is
  untouched — the `bf` clamp computes but never applies. Frozen queries also
  skip the boost list. ✓
- **Boost bounds:** `bf = jlimit(1.0f, 2.0f, boostFactor)`, actual 1.25×,
  `jmin(100.0f, ...)` cap. Never a filter. ✓
- **Classifier divide-by-zero audit:** `rms + 1e-9` in log10; `total > 1e-18`
  guard on brightness ratio; `n >= 5` sample guard; `denom > 1e-9` guard on
  the least-squares slope. `computeScanDurationSec` guards `bpm <= 0`
  (120 fallback) and `beatsPerBar < 1`. `getScanCurrentBar` guards
  `scanDurationSec <= 0 || bars <= 0`. No unguarded division in new code. ✓
- **Classifier debounce:** traced — requires 2 consecutive identical raw
  readings; transient spikes can't flicker the section or the boost. ✓
- **Role vocabulary consistency:** `roles.txt` values lowercased at parse;
  `rolesForSection` literals lowercase; `StringArray::contains` exact-match
  is therefore consistent. All 9 demo files tagged; the 4 new WAVs
  (riser-swell, rolling-hats, impact-hit, soft-pad) present on disk with
  matching roles. Unknown/missing → "neutral". ✓
- **APVTS wiring:** `scanLoop` (bool), `scanBars` (choice 2/4/8, default idx
  1 = 4 bars) registered; per-block forwarding maps choice idx→bars with
  fallback 4; `setScanBars` clamps defensively a second time. `freezeDna`
  untouched. Editor attachments (`scanBarsAttachment`, scan/freeze button
  param routing) match IDs. SCAN-while-FROZEN unfreezes via parameter AND
  analyzer backstop; FREEZE-mid-SCAN finalizes the partial accumulation
  and drops `scanLoop` — exactly one visible query mode, no silent stacking.
  ✓
- **Query-mode precedence** FROZEN > SCANNED > LIVE correctly implemented;
  SCANNED mode doesn't depend on live-DNA validity. ✓
- **`run()` cadence:** `updateScanState` every 80 ms wake (scan deadline
  checked at 12.5 Hz — responsive auto-stop); full analysis still every
  ~480 ms. ✓

## Playbook cross-check (v2)

- `CMAKE_OSX_ARCHITECTURES` placement, `createPluginFilter()` factory,
  user-local CMake path, `build/` wipe discipline: untouched by v2.4
  (CMakeLists.txt not in the diff). ✓
- auval-over-SSH blindness: unchanged, still PENDING-DOUG. ✓
- Never launch Standalone over SSH: no evidence of violation. ✓
- v1 failure-mode table remains valid institutional memory.

## Handoff for Hephaestus (G5 fix loop)

1. SHOULD-FIX 1: mutable `scanLock` + lock in `getScanCurrentBar()`; widen
   the `scanLock` scope in `updateScanState()` over the scalar writes;
   `energyLast*` → `std::atomic<float>`.
2. SHOULD-FIX 2: `scanDisarmNeeded` atomic flag set on silent auto-stop;
   editor `timerCallback` consumes it and resets the `scanLoop` parameter
   to 0 via `setValueNotifyingHost`. Do not reset the parameter on the
   `paramOn && !scanOn` condition alone (would clobber fresh arms).

Neither fix touches the matching math — Agamemnon's verified numbers are
not at risk. Rebuild + reinstall after the fixes; no new QA numeric gates
needed beyond a re-run of the scan edge cases (silent auto-stop now
disarms cleanly; bar counter reads stable under the lock).

**On-call status:** review complete. Standing by for QA bugs.
