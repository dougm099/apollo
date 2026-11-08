#pragma once

#include <JuceHeader.h>
#include "DnaFingerprint.h"

//==============================================================================
/**
    DnaExtractor — captures the DNA of a mono audio window.

    Pipeline: windowed FFT frames (4096, Hann, 50% overlap) -> power spectrum
    -> 16 log-spaced sub-bands (40 Hz – 20 kHz) -> macro low/mid/high
    aggregation -> loudness normalization (mean-subtracted dB).

    Runs on the analysis thread only. Returns false when the window is
    effectively silent (below the energy floor), so silence never
    overwrites a good DNA.

    v2.4: computeLinearBandPower exposes the linear-domain band powers (and
    window RMS) so the loop-scan accumulator and the energy tracker can share
    one FFT pass with the DNA path instead of re-running it.
*/
class DnaExtractor
{
public:
    DnaExtractor();

    bool extractDna (const float* mono, int numSamples, double sampleRate,
                     DnaFingerprint& out);

    /** Linear-domain per-band mean power (+ window RMS). Same silence gate as
        extractDna. False = silent/too short; outPower/outRms still zeroed. */
    bool computeLinearBandPower (const float* mono, int numSamples,
                                 double sampleRate,
                                 std::array<double, DnaFingerprint::numSubBands>& outPower,
                                 double& outRms);

    /** dB tail shared by extractDna and the scan finalizer: linear powers ->
        per-band dB -> macro aggregation -> loudness normalization. */
    static DnaFingerprint fingerprintFromLinearPower (
        const std::array<double, DnaFingerprint::numSubBands>& avgPower);

private:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int maxFrames = 96; // bounds per-window CPU

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> hann;
    std::vector<float> fftData; // 2 * fftSize

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DnaExtractor)
};
