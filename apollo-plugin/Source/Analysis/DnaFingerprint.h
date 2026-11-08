#pragma once

#include <array>
#include <cmath>
#include <cstddef>

//==============================================================================
/**
    DnaFingerprint — "the DNA of a track."

    A captured spectral/tonal fingerprint: log-spaced sub-band energies,
    aggregated to three macro bands (low / mid / high), loudness-normalized
    (mean-subtracted dB) so that DNA-to-DNA comparison measures *tone*,
    not level.

    Macro band edges (Hz): LOW < 250, MID 250–4000, HIGH > 4000.
*/
struct DnaFingerprint
{
    static constexpr int numSubBands   = 16;
    static constexpr int numMacroBands = 3; // 0 = low, 1 = mid, 2 = high

    static constexpr float subBandMinHz = 40.0f;
    static constexpr float subBandMaxHz = 20000.0f;
    static constexpr float lowCutHz     = 250.0f;
    static constexpr float highCutHz    = 4000.0f;

    std::array<float, numSubBands>   subBands {}; // raw dB (diagnostic)
    std::array<float, numMacroBands> macro    {}; // mean-normalized dB (matching)
    bool valid = false;

    /** Log-spaced sub-band edge frequencies, edge(0)=minHz, edge(16)=maxHz. */
    static float subBandEdgeHz (int index) noexcept
    {
        const float ratio = subBandMaxHz / subBandMinHz;
        return subBandMinHz * std::pow (ratio, (float) index / (float) numSubBands);
    }

    /** Macro band index for a frequency in Hz: 0 low, 1 mid, 2 high. */
    static int macroBandForHz (float hz) noexcept
    {
        if (hz < lowCutHz)  return 0;
        if (hz < highCutHz) return 1;
        return 2;
    }
};
