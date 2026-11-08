# Apollo plugin — build instructions

## Prerequisites (macOS)

- **Xcode Command Line Tools** (full Xcode.app also works, but CLT is enough
  to *build*; `auval` validation needs full Xcode):
  `xcode-select --install`
- **CMake ≥ 3.22**: `brew install cmake`
- **Git** (to fetch JUCE on first configure)
- **Ableton Live 10.1+** for VST3 sidechain routing

## Build

```bash
cd apollo-plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

First configure downloads JUCE (~a few hundred MB) — give it time. The VST3
bundle is copied automatically to `~/Library/Audio/Plug-Ins/VST3/`, the AU
to `~/Library/Audio/Plug-Ins/Components/`.

## Load it in Ableton Live

1. Live → Preferences → Plug-Ins → rescan (or restart Live).
2. Drop **Apollo** on the track holding your sample.
3. Route the master bus into Apollo's **sidechain input**: in the plugin's
   title bar, open the sidechain chooser ("Sidechain" dropdown) and select
   the master / the source you want Apollo to analyze. Live 10.1+ exposes
   the VST3 sidechain input declared by the plugin's second input bus.
4. Open the editor: the "Match Amount" knob and Bypass are live parameters
   (automation-ready via the APVTS).

> **Note:** this skeleton has not been compiled yet — the first build happens
> on the Mac. If the JUCE pin (`9.0.2`) is stale, check
> `github.com/juce-framework/JUCE/releases` and update `GIT_TAG`.

## Code map

- `Source/PluginProcessor.{h,cpp}` — audio processor: two stereo input buses
  (main = sample, sidechain = master), one stereo output; feeds both into the
  analyzer and applies the (currently placeholder) match processing.
- `Source/PluginEditor.{h,cpp}` — minimal UI: Match Amount knob, Bypass,
  status line.
- `Source/Analysis/MatchAnalyzer.{h,cpp}` — **the module where Apollo's DSP
  lives.** Audio thread pushes samples into lock-free FIFOs; a background
  thread runs analysis; results publish through atomics. The algorithm bodies
  are marked `TODO` and cross-referenced to `../dsp-survey.md` — fill them in
  once a spec is chosen in `../decision-specs.md`.

## Before distributing

Read `../licensing-notes.md`: JUCE needs a licence tier decision (free
Starter covers up to $20k revenue), and Steinberg's VST3 License Agreement
must be signed before shipping a closed-source VST3.
