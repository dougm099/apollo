#pragma once
// Minimal JuceHeader for the headless DNA test harness.
//
// Only the modules Apollo's Analysis engine actually uses:
//   juce_core          String / File / SpinLock / Time / math helpers
//   juce_events        Thread (MatchAnalyzer's background analysis thread)
//   juce_audio_basics  AudioBuffer / AbstractFifo (lock-free audio feed)
//   juce_audio_formats WAV demo-library loading (DnaLibrary::scanAndAnalyze)
//   juce_dsp           FFT + Hann window (DnaExtractor)
//
// Deliberately NOT the plugin's generated JuceHeader.h — that one pulls in
// the GUI, the plugin client and the audio devices. Like the generated one,
// this header does `using namespace juce` (the Analysis sources rely on it,
// e.g. bare int64 in DnaLibrary.cpp).
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

using namespace juce;
