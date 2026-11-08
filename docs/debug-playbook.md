# Apollo v2 — Debug Playbook (Daedalus, debug engineer)

Audience: anyone debugging the Apollo v2 build on Doug's Mac (macbook-pro-3).
All Mac work runs over SSH via `~/workspace/ssh-mac.sh '<command>'` from the VM.
Repo: `/Users/doug0/muse/apollo/apollo-plugin/`.

---

## 0. The 60-second build sanity loop

```sh
export PATH=$HOME/muse/shared/tools/cmake-3.31.6-macos-universal/CMake.app/Contents/bin:$PATH
cd ~/muse/apollo/apollo-plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release -j $(sysctl -n hw.ncpu)
lipo -archs build/Apollo_artefacts/Release/AU/Apollo.component/Contents/MacOS/Apollo
ls ~/Music/Apollo/DemoLibrary/*.wav
```

Expected: build OK, `x86_64 arm64`, 5 demo WAVs. Anything else → find your
symptom in §2.

---

## 1. v1 failure modes — institutional memory (from BUILD_LOG.md, 2026-09-20)

| # | Failure | Root cause | Fix (already in tree — do not regress) |
|---|---------|-----------|----------------------------------------|
| 1 | Link error: `Undefined symbols for architecture arm64: "createPluginFilter()"` on all 3 targets | `PluginProcessor.cpp` never defined JUCE's plugin factory | Factory fn appended at bottom of `Source/PluginProcessor.cpp` — keep it there |
| 2 | Binaries came out arm64-only despite "universal" intent | `CMAKE_OSX_ARCHITECTURES` was `set()` **after** `project()` — CMake reads it during `project()`'s toolchain setup, silently ignored | The two `set()` calls now live **before** `project()` in CMakeLists.txt (with a comment) — never move them below |
| 3 | `auval -v aufx Apl1 Aplo` → "didn't find the component" even though bundle, codesign, and factory exports were all correct | In-session AudioComponentRegistrar timing, not a build problem | `killall AudioComponentRegistrar`, rescan, or logout/login. Over SSH this may STILL not show — Doug verifying in GarageBand is the ground truth (he confirmed v1 loads 2026-10-06) |
| 4 | SSH session hangs | Launching the Standalone `.app` headless over SSH blocks on audio/GUI | NEVER launch the Standalone binary over SSH, not even `--help` |
| 5 | `cmake: command not found` | CMake was never installed system-wide | User-local at `~/muse/shared/tools/cmake-3.31.6-macos-universal/` — export PATH per §0 |
| 6 | CMakeLists edits seem to have no effect | Stale CMake cache in `build/` | `rm -rf build` then reconfigure from scratch |

---

## 2. v2 failure points — verified by reading the current source

### F1. Stray duplicate `Source/DnaLibrary.cpp` ⚠️ LIVE LANDMINE
`Source/DnaLibrary.cpp` (141 lines) is a near-duplicate of
`Source/Analysis/DnaLibrary.cpp`, **missing `getTopEntry()`**.
Today it's harmless: CMakeLists.txt uses explicit `target_sources`, so only
the `Analysis/` copy compiles. But the moment anyone adds the stray file to
the build (or switches to a glob), you get duplicate-symbol **linker errors**
(ODR violation) — or worse, the wrong copy wins.
**Diagnose:** `diff Source/DnaLibrary.cpp Source/Analysis/DnaLibrary.cpp`
**Fix:** `rm Source/DnaLibrary.cpp` — it is dead weight. Do it now, don't defer.

### F2. Status shows "LIBRARY: EMPTY — run tools/make_demo_library.py"
`DnaLibrary::scanAndAnalyze` looks in `~/Music/Apollo/DemoLibrary/*.wav`
(only `.wav`, non-recursive). Empty = folder missing, no WAVs, or unreadable files.
**Diagnose:** `ls -la ~/Music/Apollo/DemoLibrary/`
**Fix:** `cd ~/muse/apollo/apollo-plugin && python3 tools/make_demo_library.py`
(stdlib only, deterministic, writes 5 synthetic loops). Then re-instantiate the
plugin — the scan runs once on the analysis thread at startup.

### F3. All band knobs at 0% → every match reads exactly 50%
`weightedCosine()` returns 0.0 when the weighted norms are ~0, and
`scorePct = (0*0.5+0.5)*100 = 50%`. So "20/100/10" is fine, but 0/0/0 shows a
wall of identical 50% scores — looks broken, isn't a crash.
**Diagnose:** user reports "all matches stuck at 50%".
**Fix (code):** in `rankAgainst`, if all three weights are 0, fall back to
equal weights (or refuse: show "SELECT A BAND" in the readout). One-line guard.

### F4. DNA screens jitter while the match ranking stays stable
`MatchAnalyzer::updateDna` envelope-smooths the `macro[3]` bands across passes
(alpha 0.5) but assigns `subBands[16]` **raw** from each fresh extraction.
`DnaScreen::paint` draws the raw `subBands` — so the visuals flicker with every
transient while the ranking doesn't move. Cosmetic, but Doug will notice.
**Fix:** apply the same envelope follower to `subBands`, or draw the macro
bands in the screens.

### F5. auval still can't see the component over SSH
Same as v1 §1-#3. The AU is fine; the registrar session over SSH isn't.
**Do not** burn more than ~15 min on this. **Fix:** `killall AudioComponentRegistrar`
and ask Doug to open GarageBand — his load test is the acceptance check.

### F6. The LOW knob "does nothing" to the rankings
The 16 log-spaced sub-bands span 40 Hz–20 kHz. At 44.1 kHz with a 4096 FFT,
the bottom bands cover only 2–4 FFT bins each — statistically noisy.
The LOW macro band therefore wobbles; rankings barely respond to the knob.
**Diagnose:** compare ranking reshuffle magnitude across the three knobs with
distinct material.
**Fix (tuning):** raise FFT order to 13 (8192) for finer low-end resolution,
or weight the low macro band's sub-bands by bin count. Acceptable to defer —
flag to QA as a known tuning risk, not a blocker.

### F7. Dropouts / xruns in GarageBand with the plugin open
Threading contract (verified in source): audio thread does **only**
`getRawParameterValue` (atomic), `setBandWeights` (atomic stores), and
lock-free FIFO `prepareToWrite` copies — no allocation, no locks, no DSP.
The FFT, file I/O, and ranking all live on the analysis thread (`wait(500)`).
If dropouts appear, the contract is broken somewhere new.
**Diagnose:** `grep -n "new \|malloc\|std::vector" Source/PluginProcessor.cpp`
— any hit inside `processBlock` is the bug. Also check DiagnosticReports:
`ls -t ~/Library/Logs/DiagnosticReports/ | head`.

### F8. Knob turns but ranking doesn't change
Parameter IDs must match between the layout and the attachments:
`dnaLow`/`dnaMid`/`dnaHigh` (+ `listenSource`, `bypass`). Verified matching
today. If a knob dies after an edit, check the ID strings first — it's always
the ID strings.

### F9. GarageBand shows the plugin but DNA never goes "LIVE"
Chain: silence gate → FIFO → analysis window → DNA. Check in order:
1. Signal present? Status "NO SIGNAL — PLAY AUDIO" means the RMS gate
   (`< 0.001`, i.e. −60 dBFS) rejected the window — quiet material won't
   register. Louden the track or lower the gate in `DnaExtractor::extractDna`.
2. FIFO flowing? `pushSampleBlock` drops blocks when the ring is full rather
   than blocking — sustained drops mean the analysis thread is starved/dead.
3. `listenSource` = "Track" in GarageBand (GB has no sidechain routing; the
   "Sidechain" option is for Ableton). Default is Track — correct.

### F10. Slow configure / "is it hung?"
First configure (or after wiping `build/_deps`) re-clones JUCE 9.0.2 via
FetchContent — 2–4 min of git traffic, not a hang. **Diagnose:** watch
`build/_deps/juce-src` growing. Don't `Ctrl-C` it.

### F11. Reinstall doesn't pick up the new build in GarageBand
AU/VST3 IDs are unchanged from v1 (`Aplo`/`Apl1`, same bundle ID) — correct,
this is an upgrade, not a new plugin. **Fix:** in GarageBand,
Preferences → Audio/MIDI → "Reset Audio Unit cache" (or
`killall -9 AudioComponentRegistrar`), then rescan. If Doug still sees v1
behavior, verify the installed binary's timestamp:
`ls -l ~/Library/Audio/Plug-Ins/Components/Apollo.component/Contents/MacOS/Apollo`.

### F12. Crash on plugin scan / instantiation
**Diagnose (no debugger needed):** `ls -t ~/Library/Logs/DiagnosticReports/ | head -5`
and read the newest `.crash`/`.ips` — the crashed thread's backtrace names the
function. The usual suspects in this codebase, in order: (a) `scanAndAnalyze`
file-read edge cases (corrupt WAV → reader null is guarded; absurd lengths are
guarded); (b) `DnaExtractor` with `numSamples < fftSize` (guarded, returns false);
(c) editor timer firing after analyzer teardown — `stopTimer()` is in the
destructor and attachments are destroyed in the right order (verified).
If the trace points at `juce::SpinLock`, suspect lock inversion: `dnaLock` and
`library.lock` are never held simultaneously in the current code — keep it that way.

---

## 3. Commands cheat sheet (run on the Mac via ssh-mac.sh)

```sh
# Rebuild (always)
export PATH=$HOME/muse/shared/tools/cmake-3.31.6-macos-universal/CMake.app/Contents/bin:$PATH
cd ~/muse/apollo/apollo-plugin && rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release -j $(sysctl -n hw.ncpu)

# Verify install
lipo -archs ~/Library/Audio/Plug-Ins/Components/Apollo.component/Contents/MacOS/Apollo
codesign -dv ~/Library/Audio/Plug-Ins/Components/Apollo.component 2>&1 | head -5
ls -l ~/Library/Audio/Plug-Ins/Components/Apollo.component/Contents/MacOS/Apollo  # timestamp check

# Demo library
ls ~/Music/Apollo/DemoLibrary/
python3 ~/muse/apollo/apollo-plugin/tools/make_demo_library.py

# AU registration (best effort over SSH)
killall AudioComponentRegistrar; auval -v aufx Apl1 Aplo

# Crash logs
ls -t ~/Library/Logs/DiagnosticReports/ | head -5

# NEVER do these over SSH:
# - open the Standalone .app (hangs the session)
# - pkill -f Apollo  (cmdline self-match footgun; use pkill -f 'Apoll[o]' if you must)
```

---

## 4. Open debug debts (for the team)

- [ ] **Delete `Source/DnaLibrary.cpp`** (F1) — Hephaestus, before next build.
- [ ] All-zero band weights guard (F3) — one-line fix in `rankAgainst`.
- [ ] Smooth `subBands` for the screens (F4) — cosmetic, do before Doug's demo.
- [ ] LOW-band tuning validation (F6) — QA: does the LOW knob audibly reshuffle
      kick-heavy vs thin material? If not, bump FFT order.
- [ ] Silence gate level (−60 dBFS) vs real-world quiet loops (F9) — validate in
      the GarageBand test pass; consider −70 dBFS.

*Daedalus, debug engineer — Apollo v2 team. On call for build breaks and QA bugs.*
