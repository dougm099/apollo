#include "DnaExtractor.h"
#include <algorithm> // std::min
#include <cmath>     // std::sqrt, std::log10

//==============================================================================
DnaExtractor::DnaExtractor()
    : fft (fftOrder),
      hann ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false),
      fftData ((size_t) fftSize * 2, 0.0f)
{
}

bool DnaExtractor::computeLinearBandPower (const float* mono, int numSamples,
                                           double sampleRate,
                                           std::array<double, DnaFingerprint::numSubBands>& outPower,
                                           double& outRms)
{
    outPower.fill (0.0);
    outRms = 0.0;

    if (mono == nullptr || numSamples < fftSize || sampleRate <= 0.0)
        return false;

    //--- Silence gate: RMS below -60 dBFS means "no DNA here" ----------------
    double sumSq = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sumSq += (double) mono[i] * (double) mono[i];

    const double rms = std::sqrt (sumSq / (double) numSamples);
    outRms = rms;
    if (rms < 0.001) // -60 dBFS
        return false;

    //--- Accumulate power spectrum over overlapping frames --------------------
    const int hop = fftSize / 2;

    std::array<double, DnaFingerprint::numSubBands> bandPower {};
    bandPower.fill (0.0);
    int frames = 0;

    const float binWidthHz = (float) sampleRate / (float) fftSize;

    for (int pos = 0; pos + fftSize <= numSamples && frames < maxFrames; pos += hop, ++frames)
    {
        std::copy (mono + pos, mono + pos + fftSize, fftData.begin());
        hann.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // After performFrequencyOnlyForwardTransform, fftData[i] holds the
        // magnitude of bin i for i in [0, fftSize/2].
        const int numBins = fftSize / 2;
        for (int b = 0; b < DnaFingerprint::numSubBands; ++b)
        {
            const float loHz = DnaFingerprint::subBandEdgeHz (b);
            const float hiHz = DnaFingerprint::subBandEdgeHz (b + 1);
            const int loBin = juce::jmax (1, (int) (loHz / binWidthHz));
            const int hiBin = juce::jmin (numBins - 1, (int) (hiHz / binWidthHz));

            double p = 0.0;
            for (int bin = loBin; bin <= hiBin; ++bin)
            {
                const double mag = (double) fftData[(size_t) bin];
                p += mag * mag;
            }
            bandPower[(size_t) b] += p / (double) juce::jmax (1, hiBin - loBin + 1);
        }
    }

    if (frames == 0)
        return false;

    outPower = bandPower;
    return true;
}

DnaFingerprint DnaExtractor::fingerprintFromLinearPower (
    const std::array<double, DnaFingerprint::numSubBands>& avgPower)
{
    DnaFingerprint out;

    //--- dB conversion, macro aggregation, loudness normalization -------------
    std::array<double, 3> macroSum {};
    std::array<int, 3>    macroCount {};

    for (int b = 0; b < DnaFingerprint::numSubBands; ++b)
    {
        const float db = 10.0f * std::log10 ((float) avgPower[(size_t) b] + 1e-12f);
        out.subBands[(size_t) b] = db;

        const float centerHz = 0.5f * (DnaFingerprint::subBandEdgeHz (b)
                                     + DnaFingerprint::subBandEdgeHz (b + 1));
        const int m = DnaFingerprint::macroBandForHz (centerHz);
        macroSum[(size_t) m]   += db;
        macroCount[(size_t) m] += 1;
    }

    float macroDb[3] = { 0.0f, 0.0f, 0.0f };
    for (int m = 0; m < 3; ++m)
        macroDb[m] = macroCount[(size_t) m] > 0
                   ? (float) (macroSum[(size_t) m] / (double) macroCount[(size_t) m])
                   : -120.0f;

    // Loudness normalization: subtract the mean so DNA compares tone, not level.
    const float mean = (macroDb[0] + macroDb[1] + macroDb[2]) / 3.0f;
    for (int m = 0; m < 3; ++m)
        out.macro[(size_t) m] = macroDb[m] - mean;

    out.valid = true;
    return out;
}

bool DnaExtractor::extractDna (const float* mono, int numSamples, double sampleRate,
                               DnaFingerprint& out)
{
    out.valid = false;

    std::array<double, DnaFingerprint::numSubBands> power {};
    double rms = 0.0;
    if (! computeLinearBandPower (mono, numSamples, sampleRate, power, rms))
        return false;

    out = fingerprintFromLinearPower (power);
    return true;
}
