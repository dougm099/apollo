# BUILD_LOG.md — Apollo first build (Hephaestus, 2026-09-20)

Machine: `Mac.lan` (macbook-pro-3) · macOS 26.5.2 · arm64 (Apple Silicon)
Built via SSH from Muse's VM using `/home/hatch/workspace/ssh-mac.sh '<command>'`.

## Toolchain (as found / as fixed)

| Tool | Status |
|---|---|
| Xcode (full Xcode.app) | Present — Apple clang 21.0.0 (`xcode-select -p` → `/Applications/Xcode.app/Contents/Developer`) |
| CMake | **Missing** — installed CMake 3.31.6 user-local at `~/muse/shared/tools/cmake-3.31.6-macos-universal/CMake.app` (official macos-universal tarball from Kitware GitHub releases; no Homebrew, no system-wide changes) |
| git | Present — 2.50.1 (Apple Git-155) |
| JUCE | 9.0.2, fetched automatically by CMake FetchContent from github.com/juce-framework/JUCE.git |

CMake binary used for all commands below:
`~/muse/shared/tools/cmake-3.31.6-macos-universal/CMake.app/Contents/bin/cmake`

## Commands run (in order) and outcomes

1. `ls ~/muse/apollo/` — skeleton present (copied earlier): `CMakeLists.txt`, `Source/`,
   `.gitignore`, `README.md`.
2. `xcode-select -p && xcrun clang --version && cmake --version && git --version` —
   clang OK, git OK, **cmake not found**.
3. `curl -sSL -o cmake.tar.gz https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-macos-universal.tar.gz && tar xzf cmake.tar.gz`
   in `~/muse/shared/tools/` — OK (78 MB). `bin/cmake --version` → `cmake version 3.31.6`.
4. `cmake -B build -DCMAKE_BUILD_TYPE=Release` — **OK** (44–47 s). JUCE 9.0.2 cloned via
   FetchContent, juceaide built, build files written to `build/`.
5. `cmake --build build --config Release -j $(sysctl -n hw.ncpu)` — **FAILED at link**
   (all 3 targets: VST3, AU, Standalone):
   `Undefined symbols for architecture arm64: "createPluginFilter()", referenced from: ...`
   Root cause: `Source/PluginProcessor.cpp` never defined JUCE's required plugin
   factory function.
6. **Fix 1 (trivial, no DSP change):** appended the standard factory to
   `Source/PluginProcessor.cpp`:
   `juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ApolloAudioProcessor(); }`
   Synced the fixed file to the Mac. Rebuilt — **OK**, all targets linked.
7. Noticed binaries were `arm64` **only** (`lipo -archs`), but `CMakeLists.txt`
   documents a universal (arm64+x86_64) build. Root cause: `CMAKE_OSX_ARCHITECTURES`
   / `CMAKE_OSX_DEPLOYMENT_TARGET` were `set()` **after** `project()` — CMake reads
   them during `project()`'s toolchain setup, so the values were silently ignored.
8. **Fix 2 (trivial build-system fix):** moved the two `set()` calls **before**
   `project()` in `CMakeLists.txt` (with a comment explaining why). Synced to Mac,
   wiped `build/`, reconfigured, rebuilt — **OK**.
9. `lipo -archs build/Apollo_artefacts/Release/VST3/Apollo.vst3/Contents/MacOS/Apollo`
   → `x86_64 arm64` ✅
10. `COPY_PLUGIN_AFTER_BUILD` installed:
    - `~/Library/Audio/Plug-Ins/VST3/Apollo.vst3` ✅
    - `~/Library/Audio/Plug-Ins/Components/Apollo.component` ✅
    - Standalone app at `build/Apollo_artefacts/Release/Standalone/Apollo.app` ✅
11. `codesign -dv` on both bundles — ad-hoc signed, identifier `com.apolloaudio.apollo` ✅
12. `nm -gU` on the AU binary — exports `_ApolloAUFactory` and `_JuceAUFactory` ✅
13. `auval -v aufx Apl1 Aplo` — **component not visible to the system registrar yet**
    (`didn't find the component`), even after `killall AudioComponentRegistrar` +
    rescan. The bundle, codes, and entry points are all correct, so this is an
    in-session registration timing issue, not a build problem.

## Result

**BUILD SUCCEEDED.** All three formats (VST3, AU, Standalone) compile, link as
universal binaries, and install to the user's plug-in folders. No DSP code was
changed — only the two trivial build fixes above (missing factory function,
CMake variable ordering).

## Repro (future builds)

```sh
export PATH=$HOME/muse/shared/tools/cmake-3.31.6-macos-universal/CMake.app/Contents/bin:$PATH
cd ~/muse/apollo/apollo-plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j $(sysctl -n hw.ncpu)
```

## Notes / gotchas

- Do NOT launch the Standalone `.app` binary headless over SSH (`--help` or
  otherwise) — it blocks waiting on audio/GUI and hangs the session.
- First-time hosts (Ableton Live, auval) may need a plug-in rescan or
  logout/login before the new AU/VST3 appears in their lists.
- CMake lives user-local under `~/muse/shared/tools/`; delete that folder to
  fully undo the only system change this build made.


---

# BUILD_LOG.md — Apollo v2: DNA matching (Hephaestus, 2026-10-06)

Machine: `macbook-pro-3` · macOS 26.5.2 · arm64. Built via SSH from Muse's VM
using `/home/hatch/workspace/ssh-mac.sh '<command>'`. Same CMake 3.31.6 at
`~/muse/shared/tools/cmake-3.31.6-macos-universal/`, JUCE 9.0.2 via FetchContent.

v1 (skeleton) committed to git first: `d7552a4 "v1: JUCE skeleton (pre-DNA)"`
so v1 is recoverable (`git init` done 2026-10-06; repo is
`/Users/doug0/muse/apollo/apollo-plugin/`).

## What v2 implements (per dna-match-design.md — "match" = DNA-to-DNA)

1. **DNA extraction** (`Source/Analysis/DnaExtractor.{h,cpp}`): 4096-pt Hann FFT
   (50% overlap, `juce::dsp::FFT`) on the analysis thread → power spectrum →
   16 log-spaced sub-bands (40 Hz–20 kHz) → aggregated to macro LOW (<250 Hz) /
   MID (250–4000 Hz) / HIGH (>4000 Hz) → loudness-normalized (mean-subtracted
   dB, so DNA compares *tone*, not level). Silence gate at −60 dBFS: a silent
   window never overwrites a good DNA.
2. **Envelope smoothing** (`MatchAnalyzer::updateDna`): published DNAs are
   smoothed across analysis passes (α=0.5, thread wakes 2×/s) so the ranking
   doesn't jitter with every transient.
3. **Band-weight knobs** (editor, Ableton-style rotary `RotaryVerticalDrag`,
   custom `KnobLookAndFeel`): LOW / MID / HIGH 0–100 %, defaults 100/100/100.
   Doug's example (20/100/10) is fully supported — weights feed the matcher live.
4. **DNA-to-DNA similarity** (`DnaLibrary::rankAgainst`): weighted cosine
   similarity on the mean-normalized macro-band dB vectors.
5. **Demo library** (`tools/make_demo_library.py`, stdlib only): 5 synthetic
   4 s loops with distinct DNAs → `~/Music/Apollo/DemoLibrary/*.wav`
   (deep-bass-loop, bright-hats-loop, mid-keys-loop, air-texture, full-mix-loop).
   The plugin scans this folder once on the analysis thread at startup and
   caches each file's DNA. Verified distinct via offline band-energy check.
6. **Editor UI** (Malström-inspired, compact, no full-screen visualizer):
   two small DNA screens side by side — TRACK DNA (live query) | SAMPLE DNA
   (current top match), each 16 bars coloured by macro band — plus TOP MATCH
   readout (name + score %) under the sample screen, the three band knobs as
   the prominent core interaction, live ranked match list, source selector
   (Track/Sidechain), bypass. Dark hardware vibe.
7. **GarageBand path**: default listen source is the main input ("Track") —
   GarageBand has no sidechain routing, so the plugin listens to the track
   it's inserted on. Sidechain ("Master") selectable for Ableton.

v2 is analysis-only: audio passes through untouched (search/discovery, no EQ
transform — explicit non-goal).

## Commands run (in order) and outcomes

1. `git init && git add -A && git commit "v1: JUCE skeleton (pre-DNA)"` → OK (d7552a4)
2. New sources copied from VM (`Source/Analysis/Dna*.{h,cpp}`, rewritten
   `MatchAnalyzer`, `PluginProcessor`, `PluginEditor`, `CMakeLists.txt`,
   `tools/make_demo_library.py`) → OK
3. `python3 tools/make_demo_library.py` → 5 WAVs + README in
   `~/Music/Apollo/DemoLibrary/` → OK
4. Offline DNA-distinctness check (`/tmp/check_dna.py`) → all 5 loops show
   clearly different low/mid/high profiles → OK
5. `cmake -B build -DCMAKE_BUILD_TYPE=Release` → OK (reused cached JUCE)
6. `cmake --build build --config Release -j $(sysctl -n hw.ncpu)` →
   **FAILED first attempt**: `juce::Slider` in JUCE 9 has no
   `setTextValueFunction`/`setValueFromTextFunction` (removed in JUCE 8;
   now public `textFromValueFunction`/`valueFromTextFunction` members).
   Fixed in `PluginEditor.cpp` (+ int64/sign-conversion warning cleanups in
   `DnaLibrary.cpp`). Rebuilt → **OK**: `Built target Apollo`,
   `Apollo_Standalone`, `Apollo_AU`, `Apollo_VST3`, zero errors.
7. Install (COPY_PLUGIN_AFTER_BUILD): `~/Library/Audio/Plug-Ins/Components/Apollo.component`
   and `~/Library/Audio/Plug-Ins/VST3/Apollo.vst3` refreshed 2026-10-06 20:45 → OK
8. `auval -v aufx Apl1 Aplo` → **FAIL: "didn't find the component"** (same as
   v1 over SSH; ~2 min spent, not pursued). Doug verifies in GarageBand —
   v1 loaded there fine, so this is an SSH-session component-registry quirk,
   not a build problem.

## Version

0.2.0 (`com.apolloaudio.apollo`, `Aplo`/`Apl1` unchanged). NOTE: GarageBand
caches AU info — rescan plug-ins (or restart GarageBand) to pick up v2.

## Known v2 limitations (for v3)

- Demo library is fixed (5 synthetic WAVs); no user-folder indexing yet.
- Tempo/key/loudness/brightness estimators are still v1 stubs.
- No "contrast" (complementary-match) toggle yet — same metric, later UI.
- DNA screen shows sub-band shape only; no numeric readout.


---

## v2.1 — QA hooks + Daedalus debug fixes (Hephaestus, 2026-10-06 ~20:50)

Committed as a follow-up on top of the v2 build (same binaries line).

### Daedalus's 4 findings (from debug review of the v2 tree)

1. **Stray duplicate `Source/DnaLibrary.cpp` — FIXED.** An early `scp`
   flattened `Source/Analysis/DnaLibrary.cpp` into `Source/`. It was never
   compiled (`CMakeLists.txt` uses explicit `target_sources`, so only
   `Source/Analysis/DnaLibrary.cpp` builds), but it was a duplicate-symbol
   landmine. Deleted via `git rm`, committed as `38dda3b`. Verified gone.
2. **All knobs at 0% → every score reads exactly 50% — FIXED.** Root cause:
   `weightedCosine` returns 0 on the zero-weight vector, and
   `(0*0.5+0.5)*100 = 50`. Guard added in `DnaLibrary::rankAgainst`: when
   wLow+wMid+wHigh < 1e-6, fall back to equal weights (1,1,1) so the list
   stays meaningful instead of flat-50%.
3. **DNA screens would jitter while ranking stays stable — FIXED.** Root
   cause: `MatchAnalyzer::updateDna` smoothed `macro[]` but assigned
   `subBands[]` raw, and the DNA screens paint the sub-bands. Now both are
   envelope-smoothed (α=0.5 per analysis pass).
4. **Tuning risk (not a blocker, kept):** with 16 log bands over 40 Hz–20 kHz,
   the bottom bands cover only 2–4 FFT bins at 44.1 kHz, so the LOW knob may
   move rankings weakly. Kept FFT order 12 per Daedalus's scoping ("bump
   later if QA flags it"). The LOW macro aggregates 5 sub-bands, which
   mitigates the coarseness.

### Agamemnon's QA hooks (P1)

1. **DNA readback hook.** `MatchAnalyzer::getDnaDebugString()` returns a
   multi-line text dump: band weights, query DNA (macro + all 16 sub-band dB,
   validity flag), and every library entry's macro DNA + current score.
   Surfaced in the editor via a **DNA DBG** toggle (bottom-right): when on,
   a monospace readout swaps in over the match list, refreshed 2×/s. Lets QA
   measure extraction stability, loudness invariance, and ranking correctness
   from numbers, not eyeballing. Backed by `DnaLibrary::getDebugSnapshot()`.
2. **Demo WAV documentation.** `tools/DEMO_LIBRARY.md`: per-file synthesis
   parameters, tone-class labels, expected DNA vectors (from a faithful
   Python replica of the extractor, `/tmp/dna_expected.py`), and 6 runnable
   labeled tests (T1 self-match, T2 loudness invariance, T3 band isolation,
   T4 zero-weight guard, T5 extraction stability, T6 silence behavior).
   Note: `mid-keys-loop.wav` was regenerated an octave up (440/554/659/880 Hz)
   after the first expected-DNA run showed its 220 Hz fundamental landing in
   the LOW macro band (<250 Hz) — it now reads cleanly MID-dominant
   (macro [−7.4, +30.1, −22.7]).

### Build

`cmake --build build --config Release` → clean, all targets
(Apollo, Standalone, AU, VST3), zero errors. Installed via
COPY_PLUGIN_AFTER_BUILD; verified the installed AU binary contains the new
"APOLLO DNA DEBUG" / "DNA DBG" strings. GarageBand rescan needed to pick up
the new build.

## v2.2 — usability must-fixes (Ariadne audit, 2026-10-06 ~20:55 PT)

All three BLOCKER items from usability-audit.md, plus the two tooltip hints.

1. **Clickable match-list rows + FOLLOW TOP** (PluginEditor.h/.cpp, MatchAnalyzer.h/.cpp):
   - MatchListModel gained listBoxItemClicked -> onRowClicked callback; clicking a row
     turns FOLLOW TOP off so SAMPLE DNA shows the clicked candidate instead of the
     live leader. Re-enabling FOLLOW TOP deselects rows and resumes tracking.
   - New MatchAnalyzer::getRankedEntryDna(rankIndex, dna, name) backed by
     DnaLibrary::getDebugSnapshot() (ranked order). TOP MATCH readout still follows #1.
   - FOLLOW TOP toggle placed in the match-list header row (right side, 130px).
2. **Sidechain auto-disable in GarageBand** (PluginEditor.cpp timerCallback): each tick
   checks processorRef.getChannelCountOfBus(true, 1); when 0, the "Sidechain" combo
   item is disabled via setItemEnabled and the selector falls back to Track
   (sendNotification so the apvts param follows). Runtime query = live feature in
   Ableton, no dead end in GarageBand.
3. **Double-click knob reset** (setupKnob): k.setDoubleClickReturnValue(true, 1.0) on
   all three band knobs — Ableton muscle memory, resets to 100%.
4. **Tooltips**: juce::TooltipWindow member added (enables setTooltip); knob tooltips
   note "(automatable)"; sourceBox tooltip explains sidechain host-routing.

Build notes: one self-inflicted compile error on first attempt — setupKnob call sites
passed (attachment, tooltip) while the lambda signature is (tooltip, attachment);
swapped, rebuilt clean. Clean Release build, zero errors; AU + VST3 reinstalled
(binary timestamps 20:55 PT, new strings verified in the AU binary).

## v2.3 — EQ-curve DNA screens + live display + FREEZE (2026-10-06 ~21:13–21:20 PT, Doug directives)

### A. EQ-curve DNA screens
Doug: the bar rendering "doesn't look like an eq filter" — approved a curve-rendered
mockup ("yes, switch to the EQ curves").

- `DnaScreen::paint` (Source/PluginEditor.h) rewritten: smooth EQ-filter curve through
  the 16 log-spaced sub-band points instead of vertical bars. Catmull-Rom interpolation
  (12 subdivisions/segment) — no jagged polylines. Stroke coloured by macro band along
  x (LOW amber 0xe0a060 / MID green 0x7fd08a / HIGH 0x6fb7d8, same palette), translucent
  green fill under the curve (alpha 0x1c), 3 faint horizontal gridlines, dark screen
  background unchanged (0x0b0d11). Same dB normalization as the bars (maxDb - 42 floor).
- Applied to both screens (TRACK/SIDECHAIN DNA + SAMPLE DNA) — single shared component.
- Added `#include <vector>` (explicit).

### B. Live animated TRACK DNA display
Doug: the EQ curve must visibly MOVE with the music as it scans — like a live EQ
display, not a static fingerprint. The 2 Hz heavily-smoothed matching DNA looked frozen.

- Separate live display path in MatchAnalyzer: the analysis thread now wakes every
  80 ms (~12.5 fps) and runs a cheap display-only spectrum — ONE 4096-pt FFT per bus
  on the newest audio from new small display rings (16k samples; the audio thread
  mirrors blocks into them copy-only, drop-if-full — same lock-free pattern, no
  allocation, no DSP on the audio thread), folded into a fast-attack (0.55) /
  medium-release (0.10) envelope per sub-band. Silence decays the curve to a -90 dB
  floor instead of freezing it.
- The full matching pass (4 s windows, envelope-smoothed DNA, ranking) still runs
  every ~480 ms EXACTLY as before — Agamemnon's verified numbers are untouched.
  The live path never feeds ranking (separate `liveLock`, never held with `dnaLock`).
- Editor timer 500 ms -> 80 ms; TRACK DNA screen paints `getLiveDna()`. SAMPLE DNA
  screen unchanged (static stored reference). DNA DBG readout still uses the stable
  `getQueryDna()`.

### C. FREEZE — capture the DNA reference
Doug: the user needs a FREEZE option — the match-EQ "capture reference" step.

- New APVTS bool parameter "freezeDna" (host-visible/automatable), forwarded per
  block via `analyzer.setFreezeRequested()` (atomic store only — audio thread safe).
- FREEZE toggle button under the TRACK DNA screen. Clicks route through the parameter
  (`setValueNotifyingHost`); the analyzer owns the authoritative state and the editor
  syncs the button from `analyzer.isFrozen()` every tick.
- On freeze the analysis thread snapshots the last STABLE query DNA (the matching
  fingerprint, not the live display one); `rankLibrary()` matches against the snapshot
  while frozen. The track screen shows the frozen curve static with a "• FROZEN"
  indicator on the label; status line shows "DNA FROZEN".
- Unfreezing resumes live capture seamlessly (`updateDna` kept smoothing underneath).
- Edge cases: freeze with no valid DNA is ignored — the button stays inert and the
  status reads "NO DNA TO FREEZE — PLAY AUDIO". The frozen snapshot is source-
  agnostic once captured (flipping Track/Sidechain mid-freeze keeps the reference).
- QA note: the default unfrozen `rankLibrary()` path is bit-identical to v2.1/v2.2
  (same `dnaLock` branch); freeze only activates on explicit user action.

### Build
Clean Release build, zero errors/warnings; AU + VST3 reinstalled via
COPY_PLUGIN_AFTER_BUILD (binaries ~21:20 PT).

## v2.4 — loop scan + energy-aware recommendations (2026-10-06, Hephaestus)

### A. Loop scan — fixed bar-count capture
Doug refined the spec mid-build: SCAN captures a FIXED bar count (2/4/8,
selectable), not press-again-to-stop.

- New APVTS bool `scanLoop` + int `scanBars` (choice "2/4/8 bars", default 4),
  both host-visible/automatable. Bar-count selector is a compact combo in the
  CAPTURE cluster; the SCAN button label carries its time semantics and updates
  with the selector ("SCAN 2 BARS" / "SCAN 4 BARS" / "SCAN 8 BARS").
- On SCAN press the processor reads host tempo via `AudioPlayHead`
  (`positionInfo->getBpm()`, host time signature numerator when available,
  4/4 assumed); capture duration = bars × beatsPerBar × 60/bpm. Auto-stops at
  the deadline and latches the integrated DNA — no second press.
- **120 BPM fallback** (bpm ≤ 0 → 120): verified EXERCISABLE, not just coded —
  in the JUCE Standalone on desktop, `setPlayHead` is iOS-only
  (`juce_StandaloneFilterWindow.h`, `#if JucePlugin_Enable_IAA && JUCE_IOS`),
  so `getPlayHead()` is always null there and the fallback is the *normal*
  Standalone path. DBG shows `host_bpm=0.0 (120 fallback)`.
- Accumulation in linear power domain (one shared FFT per window feeds the
  stable DNA, the scan accumulator and the energy tracker — no re-running the
  FFT), dB + loudness-normalize at the end via
  `DnaExtractor::fingerprintFromLinearPower` (refactored: `extractDna` split
  into `computeLinearBandPower` + static `fingerprintFromLinearPower`,
  bit-identical math).
- State machine (analysis thread owns it; edge-triggered on the scanLoop
  param): IDLE → ACTIVE → LATCHED (auto-stop). Latched scan never re-arms
  itself; toggle off clears. While scanning: status "SCANNING… bar X/N",
  track screen shows the accumulating curve live.
- Query-mode precedence (exactly one visible mode, Ariadne R1): FROZEN >
  SCANNED > LIVE. SCAN while FROZEN unfreezes first (editor clears the
  freezeDna param; analyzer backstops). FREEZE mid-SCAN finalizes the partial
  accumulation into the frozen snapshot and cancels the scan (editor drops the
  scanLoop param). Post-scan FREEZE snapshots the SCANNED DNA (H8).
- Scan observability (Agamemnon P1): DNA DBG logs scan state, bars armed,
  t_start/t_stop, captured window + sample counts, host BPM, and the scan DNA
  macro — H1–H3/H5 are measurable, not eyeballed.
- Offline verification (`workspace/apollo-basis/v24-verification/replica.py`,
  faithful Python replica of the extractor): **H6** — scanned DNA vs file DNA
  cosine distance < 0.10 for all 9 demo files (worst 0.0094). Found and fixed
  pre-QA: `mid-keys-loop.wav` had a loop-seam phase jump (554.37/659.26 Hz not
  integer-cycle-per-4s) whose transient dominated the scan's quiet bands
  (0.1791); fixed to integer-cycle 554/659 Hz (inaudible, ~1 cent), now 0.0000.

### B. Energy-aware recommendations
- Energy tracker on the analysis thread: per-window RMS loudness (dBFS) +
  brightness (high-band energy ratio) envelopes, ~8-bar history per bus.
- Section classifier v1 (slope heuristics over a 2-bar window, debounced —
  2 consecutive readings to confirm): BUILDUP (energy slope > +1 dB/s AND
  brightness rising), PEAK (mean energy > −16 dBFS), BREAKDOWN (mean <
  −30 dBFS), STEADY (default → pure DNA similarity). Misreads accepted per
  spec — the output is only a recommendation boost.
- Role tags: closed vocabulary `riser/roll/hats/impact/pad/texture/neutral`
  in `roles.txt` (one `file.wav=role` per line; missing/unknown → neutral),
  documented with per-file assignments in `tools/DEMO_LIBRARY.md`.
  Section → boost mapping: BUILDUP → riser/roll/hats; PEAK →
  impact/texture/hats; BREAKDOWN → pad/texture; STEADY → none.
- **Boost factor (documented): `energyBoostFactor = 1.25` (≤ 2× per spec) —
  multiplicative, `min(100, score × 1.25)`, a boost never a filter.**
  STEADY/frozen → empty boost list → ranking bit-identical to v2.3
  (Agamemnon's numbers stand). Verified offline: **I6** — STEADY #1 never
  below rank 3 under any section boost (9 queries × 3 sections, all pass);
  **I5** — STEADY ranking order- and score-identical to the unboosted path.
- Classifier observability (Agamemnon P1): DBG logs current section +
  rms/brightness envelope values + energy/brightness slopes.
- UI: ENERGY rides the status line as state text
  ("DNA LIVE • ENERGY: BUILDUP • LIB: 9"); persistent mode indicator
  LIVE/FROZEN/SCANNED (N BARS) in the status line; boosted rows get an amber
  dot in the match list (Ariadne R3 — the boost is visible).

### C. Usability (Ariadne v2.4 audit, baked in — not retrofitted)
- SCAN vs FREEZE confusion (R1): SCAN button shows its bar count; FREEZE is
  instant+persistent, SCAN is countdown+auto-stop — the behaviors feel
  different; undefined interactions resolve to exactly one visible query mode.
- Control proliferation (R2): FREEZE + SCAN + bar-count grouped in one
  CAPTURE cluster (one label, one frame); FOLLOW TOP stays with the match
  list; DNA DBG demoted to a small header button; ENERGY is status-line text,
  not a widget.

### D. Demo library additions (v2.4)
`riser-swell.wav` (riser) — rising noise + 200→2000 Hz sweep, 4 s buildup;
`rolling-hats.wav` (roll) — driving 16ths at ~140 BPM with accents;
`impact-hit.wav` (impact) — 50/75 Hz boom + noise hit, long tail;
`soft-pad.wav` (pad) — soft detuned chord wash, 1.5 s attack.
Existing five tagged: deep-bass-loop neutral, bright-hats-loop hats,
mid-keys-loop neutral, air-texture texture, full-mix-loop neutral.

### Build
Clean Release build, zero errors (one fixed: static-method/member name
collision `scanDurationSec`); AU + VST3 + Standalone reinstalled 2026-10-06
~21:34 PT; demo library regenerated (9 WAVs + roles.txt).

## v2.4.1 — G5 fix loop (Daedalus G2 should-fix items, 2026-10-06)

Both items from debug-review-v24.md implemented; matching math untouched
(Agamemnon G3 QA numbers stand).

1. Cross-thread races cleaned (G2-SF1): scanLock is now mutable and taken in
   getScanCurrentBar(); the rising-edge and auto-stop branches of
   updateScanState() hold scanLock across the scan scalar writes
   (scanDurationSec/scanStartSec/scanStopSec/scanDeadlineSec/scanWindows/
   scanTotalSamples) so getDnaDebugString() reads under a real
   happens-before edge; energyLastRmsDb/Bright/ESlope/BSlope are now
   std::atomic<float> (single-writer analysis thread, single-reader UI).
   scanAccum stays lock-free (analysis-thread-only both sides). No lock is
   ever held with another — the v2 invariant holds.
2. Silent auto-stop disarm (G2-SF2): new std::atomic<bool> scanDisarmNeeded,
   set in the auto-stop branch when scanWindows == 0; the editor timerCallback
   consumes it via takeScanDisarmRequest() and resets the APVTS scanLoop
   parameter with setValueNotifyingHost(0.0f) on the message thread. The
   bare paramOn && !scanOn reset is deliberately NOT used — it would clobber
   a fresh arm in the <=80 ms before the analysis thread sees the rising edge.

Build: clean Release, zero errors (only pre-existing warnings in
DnaLibrary.h/PluginEditor.h). AU + VST3 reinstalled via
COPY_PLUGIN_AFTER_BUILD; takeScanDisarmRequest symbol verified in the
installed AU binary.
