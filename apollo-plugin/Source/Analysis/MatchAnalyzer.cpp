#include "MatchAnalyzer.h"
#include <cmath>   // std::sqrt, std::log10
#include <cstring> // std::memcpy

//==============================================================================
MatchAnalyzer::MatchAnalyzer()
    : juce::Thread ("Apollo Analysis Thread")
{
}

MatchAnalyzer::~MatchAnalyzer()
{
    stopThread (1000);
}

void MatchAnalyzer::prepare (double newSampleRate, int maxBlockSize)
{
    signalThreadShouldExit();
    stopThread (1000);

    sampleRate = newSampleRate;
    ringSize   = static_cast<int> (newSampleRate * ringSeconds) + maxBlockSize;

    masterRing.setSize (1, ringSize, false, false, true);
    sampleRing.setSize (1, ringSize, false, false, true);
    masterFifo.setTotalSize (ringSize);
    sampleFifo.setTotalSize (ringSize);

    masterDisplayRing.setSize (1, displayRingSize, false, false, true);
    sampleDisplayRing.setSize (1, displayRingSize, false, false, true);
    masterDisplayFifo.setTotalSize (displayRingSize);
    sampleDisplayFifo.setTotalSize (displayRingSize);
    liveScratch.setSize (1, displayRingSize, false, false, true);

    reset();

    startThread(); // default priority; analysis is not latency-critical
}

void MatchAnalyzer::reset()
{
    masterFifo.reset();
    sampleFifo.reset();
    masterDisplayFifo.reset();
    sampleDisplayFifo.reset();

    {
        juce::SpinLock::ScopedLockType sl (dnaLock);
        masterDna = DnaFingerprint();
        sampleDna = DnaFingerprint();
    }

    {
        juce::SpinLock::ScopedLockType sl (liveLock);
        liveMasterDna = DnaFingerprint();
        liveSampleDna = DnaFingerprint();
    }

    freezeRequested.store (false, std::memory_order_relaxed);
    frozen.store (false, std::memory_order_relaxed);
    {
        juce::SpinLock::ScopedLockType sl (frozenLock);
        frozenDna = DnaFingerprint();
    }

    //--- v2.4 scan + energy state ---------------------------------------------
    scanRequested.store (false, std::memory_order_relaxed);
    scanBarsParam.store (4, std::memory_order_relaxed);
    hostBpm.store (0.0f, std::memory_order_relaxed);
    hostBeatsPerBar.store (4, std::memory_order_relaxed);
    scanActive.store (false, std::memory_order_relaxed);
    scanLatched.store (false, std::memory_order_relaxed);
    scanBarsArmed.store (4, std::memory_order_relaxed);
    scanSeenWanted = false;
    scanAccum.fill (0.0);
    scanWindows = 0;
    scanTotalSamples = 0;
    scanStartSec = 0.0;
    scanStopSec = 0.0;
    scanDeadlineSec = 0.0;
    scanDurationSec = 0.0;
    {
        juce::SpinLock::ScopedLockType sl (scanLock);
        scanDna = DnaFingerprint();
    }
    energyHistMaster.clear();
    energyHistTrack.clear();
    energySection.store (0, std::memory_order_relaxed);
    energyRawPrev = 0;
    energyLastRmsDb.store (-70.0f, std::memory_order_relaxed);
    energyLastBright.store (0.0f, std::memory_order_relaxed);
    energyLastESlope.store (0.0f, std::memory_order_relaxed);
    energyLastBSlope.store (0.0f, std::memory_order_relaxed);

    masterTempoBpm.store (0.0f, std::memory_order_relaxed);
    sampleTempoBpm.store (0.0f, std::memory_order_relaxed);
    masterLufs.store (-70.0f, std::memory_order_relaxed);
    sampleLufs.store (-70.0f, std::memory_order_relaxed);
    masterBrightness.store (0.0f, std::memory_order_relaxed);
    sampleBrightness.store (0.0f, std::memory_order_relaxed);
    masterKeyRoot.store (-1, std::memory_order_relaxed);
    masterKeyIsMinor.store (false, std::memory_order_relaxed);
}

//==============================================================================
// Audio-thread feed. Copy-only: mix to mono into the ring, never allocate,
// never block. If the ring is full we drop the block — a missed analysis
// window is always preferable to a dropout.

void MatchAnalyzer::pushMasterBlock (const float* left, const float* right, int numSamples)
{
    if (ringSize == 0 || left == nullptr || right == nullptr)
        return;

    int start1, size1, start2, size2;
    masterFifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    if (size1 + size2 < numSamples)
    {
        masterFifo.finishedWrite (0); // ring full: drop, don't block
        return;
    }

    float* dst = masterRing.getWritePointer (0);
    for (int i = 0; i < size1; ++i) dst[start1 + i] = 0.5f * (left[i]          + right[i]);
    for (int i = 0; i < size2; ++i) dst[start2 + i] = 0.5f * (left[size1 + i]  + right[size1 + i]);

    masterFifo.finishedWrite (size1 + size2);

    // Display tap: copy-only mirror into the live-display ring. Same lock-free
    // pattern as above — never allocates, never blocks. If the ring is full
    // we drop the block; a missed display frame is invisible.
    {
        int d1, ds1, d2, ds2;
        masterDisplayFifo.prepareToWrite (numSamples, d1, ds1, d2, ds2);
        if (ds1 + ds2 >= numSamples)
        {
            float* dd = masterDisplayRing.getWritePointer (0);
            for (int i = 0; i < ds1; ++i) dd[d1 + i] = 0.5f * (left[i]        + right[i]);
            for (int i = 0; i < ds2; ++i) dd[d2 + i] = 0.5f * (left[ds1 + i] + right[ds1 + i]);
            masterDisplayFifo.finishedWrite (ds1 + ds2);
        }
        else
        {
            masterDisplayFifo.finishedWrite (0);
        }
    }
}

void MatchAnalyzer::pushSampleBlock (const float* left, const float* right, int numSamples)
{
    if (ringSize == 0 || left == nullptr || right == nullptr)
        return;

    int start1, size1, start2, size2;
    sampleFifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    if (size1 + size2 < numSamples)
    {
        sampleFifo.finishedWrite (0);
        return;
    }

    float* dst = sampleRing.getWritePointer (0);
    for (int i = 0; i < size1; ++i) dst[start1 + i] = 0.5f * (left[i]          + right[i]);
    for (int i = 0; i < size2; ++i) dst[start2 + i] = 0.5f * (left[size1 + i]  + right[size1 + i]);

    sampleFifo.finishedWrite (size1 + size2);

    // Display tap: copy-only mirror into the live-display ring (see above).
    {
        int d1, ds1, d2, ds2;
        sampleDisplayFifo.prepareToWrite (numSamples, d1, ds1, d2, ds2);
        if (ds1 + ds2 >= numSamples)
        {
            float* dd = sampleDisplayRing.getWritePointer (0);
            for (int i = 0; i < ds1; ++i) dd[d1 + i] = 0.5f * (left[i]        + right[i]);
            for (int i = 0; i < ds2; ++i) dd[d2 + i] = 0.5f * (left[ds1 + i] + right[ds1 + i]);
            sampleDisplayFifo.finishedWrite (ds1 + ds2);
        }
        else
        {
            sampleDisplayFifo.finishedWrite (0);
        }
    }
}

//==============================================================================
// DNA controls.

void MatchAnalyzer::setBandWeights (float low, float mid, float high)
{
    bandWLow.store  (juce::jlimit (0.0f, 1.0f, low),  std::memory_order_relaxed);
    bandWMid.store  (juce::jlimit (0.0f, 1.0f, mid),  std::memory_order_relaxed);
    bandWHigh.store (juce::jlimit (0.0f, 1.0f, high), std::memory_order_relaxed);
}

void MatchAnalyzer::setListenToSidechain (bool shouldListenToSidechain)
{
    listenToSidechain.store (shouldListenToSidechain, std::memory_order_relaxed);
}

std::vector<MatchAnalyzer::RankedMatch> MatchAnalyzer::getRankedMatches()
{
    std::vector<RankedMatch> out;
    for (const auto& e : library.getSnapshot())
    {
        RankedMatch r;
        r.name     = e.name;
        r.scorePct = e.scorePct;
        r.boosted  = e.boosted;
        out.push_back (r);
    }
    return out;
}

DnaFingerprint MatchAnalyzer::getQueryDna()
{
    juce::SpinLock::ScopedLockType sl (dnaLock);
    return listenToSidechain.load (std::memory_order_relaxed) ? masterDna : sampleDna;
}

bool MatchAnalyzer::getTopMatchDna (DnaFingerprint& dna, juce::String& name)
{
    return library.getTopEntry (dna, name);
}

bool MatchAnalyzer::getRankedEntryDna (int rankIndex, DnaFingerprint& dna, juce::String& name)
{
    if (rankIndex < 0)
        return false;

    const auto snap = library.getDebugSnapshot(); // ranked order: DNA + name + score
    if ((size_t) rankIndex >= snap.size())
        return false;

    dna  = snap[(size_t) rankIndex].dna;
    name = snap[(size_t) rankIndex].name;
    return true;
}

juce::String MatchAnalyzer::getDnaDebugString()
{
    juce::String s;
    const DnaFingerprint query = getQueryDna();

    s << "APOLLO DNA DEBUG\n";
    s << "weights L/M/H = " << juce::String (bandWLow.load (std::memory_order_relaxed), 2)
      << " / " << juce::String (bandWMid.load (std::memory_order_relaxed), 2)
      << " / " << juce::String (bandWHigh.load (std::memory_order_relaxed), 2) << "\n";
    s << "query valid=" << (query.valid ? "yes" : "NO") << "  macro(dB,mean-norm) = ["
      << juce::String (query.macro[0], 1) << ", "
      << juce::String (query.macro[1], 1) << ", "
      << juce::String (query.macro[2], 1) << "]\n";
    s << "query sub16(dB) = [";
    for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
    {
        if (i > 0)
            s << ", ";
        s << juce::String (query.subBands[(size_t) i], 1);
    }
    s << "]\n";
    s << "--- library (" << juce::String (library.size()) << ") ---\n";

    for (const auto& e : library.getDebugSnapshot())
    {
        s << e.name << "  score=" << juce::String (e.scorePct, 1) << "%  macro=["
          << juce::String (e.dna.macro[0], 1) << ", "
          << juce::String (e.dna.macro[1], 1) << ", "
          << juce::String (e.dna.macro[2], 1) << "]"
          << "  role=" << e.role
          << (e.boosted ? "  BOOSTED" : "") << "\n";
    }

    //--- v2.4 scan observability (QA hook H1–H3/H5: measurable, not eyeballed)
    {
        juce::SpinLock::ScopedLockType sl (scanLock);
        s << "--- scan ---\n";
        s << "state=";
        if (scanActive.load (std::memory_order_relaxed))       s << "ACTIVE";
        else if (scanLatched.load (std::memory_order_relaxed)) s << "LATCHED";
        else                                                  s << "IDLE";
        s << "  bars_armed=" << juce::String (scanBarsArmed.load (std::memory_order_relaxed))
          << "  t_start=" << juce::String (scanStartSec, 3)
          << "  t_stop=" << juce::String (scanStopSec, 3)
          << "  captured_windows=" << juce::String (scanWindows)
          << "  captured_samples=" << juce::String (scanTotalSamples)
          << "  host_bpm=" << juce::String (hostBpm.load (std::memory_order_relaxed), 1)
          << (hostBpm.load (std::memory_order_relaxed) > 0.0f ? "" : " (120 fallback)")
          << "\n";
        s << "scan_dna valid=" << (scanDna.valid ? "yes" : "NO")
          << "  macro=[" << juce::String (scanDna.macro[0], 1) << ", "
          << juce::String (scanDna.macro[1], 1) << ", "
          << juce::String (scanDna.macro[2], 1) << "]\n";
    }

    //--- v2.4 classifier observability (QA hook I-a: section + envelopes)
    s << "--- energy ---\n";
    s << "section=" << energySectionName (getEnergySection())
      << "  rms=" << juce::String (energyLastRmsDb.load (std::memory_order_relaxed), 1) << "dB"
      << "  bright=" << juce::String (energyLastBright.load (std::memory_order_relaxed), 3)
      << "  eSlope=" << juce::String (energyLastESlope.load (std::memory_order_relaxed), 2) << "dB/s"
      << "  bSlope=" << juce::String (energyLastBSlope.load (std::memory_order_relaxed), 3) << "/s\n";
    return s;
}

const char* MatchAnalyzer::energySectionName (EnergySection s) noexcept
{
    switch (s)
    {
        case EnergySection::Buildup:   return "BUILDUP";
        case EnergySection::Peak:      return "PEAK";
        case EnergySection::Breakdown: return "BREAKDOWN";
        default:                        return "STEADY";
    }
}

juce::String MatchAnalyzer::getDnaStatusText()
{
    juce::String status;

    const bool toSidechain = listenToSidechain.load (std::memory_order_relaxed);
    bool queryValid = false;
    {
        juce::SpinLock::ScopedLockType sl (dnaLock);
        queryValid = (toSidechain ? masterDna : sampleDna).valid;
    }

    // Persistent query-mode indicator: exactly one of LIVE / FROZEN / SCANNED
    // is ever visible (Ariadne R1). ENERGY rides the status line as state text.
    juce::String mode;
    if (frozen.load (std::memory_order_relaxed))
        mode = "DNA FROZEN";
    else if (scanActive.load (std::memory_order_relaxed))
        mode = "SCANNING… bar " + juce::String (getScanCurrentBar())
             + "/" + juce::String (scanBarsArmed.load (std::memory_order_relaxed));
    else if (scanLatched.load (std::memory_order_relaxed))
        mode = "DNA SCANNED (" + juce::String (scanBarsArmed.load (std::memory_order_relaxed)) + " BARS)";
    else if (! library.isReady())
        mode = "LIBRARY: SCANNING...";
    else if (library.size() == 0)
        mode = "LIBRARY: EMPTY — run tools/make_demo_library.py";
    else if (! queryValid && freezeRequested.load (std::memory_order_relaxed))
        mode = "NO DNA TO FREEZE — PLAY AUDIO";
    else if (! queryValid)
        mode = "NO SIGNAL — PLAY AUDIO";
    else
        mode = "DNA LIVE";

    status = mode + "  •  ENERGY: " + juce::String (energySectionName (getEnergySection()))
           + "  •  LIB: " + juce::String (library.size());
    return status;
}

//==============================================================================
// Background analysis thread.

void MatchAnalyzer::run()
{
    // One-time demo-library scan on this thread (disk I/O stays off the
    // audio thread and off prepareToPlay).
    if (! libraryScanned)
    {
        library.scanAndAnalyze (extractor);
        libraryScanned = true;
    }

    const int windowSamples = static_cast<int> (sampleRate * 4.0);
    juce::AudioBuffer<float> window (1, windowSamples);

    int wakeCount = 0;

    while (! threadShouldExit())
    {
        wait (80); // ~12.5 fps display cadence; the full matching pass stays ~2 Hz

        updateLiveDisplay(); // cheap display-only spectrum; matching untouched
        updateScanState (juce::Time::getMillisecondCounterHiRes() / 1000.0);

        if (++wakeCount % 6 != 0)
            continue; // full analysis + ranking every ~480 ms, exactly as before

        for (int pass = 0; pass < 2; ++pass)
        {
            auto& fifo = (pass == 0) ? masterFifo : sampleFifo;
            auto& ring = (pass == 0) ? masterRing : sampleRing;

            int targetSamples = windowSamples;
            if (scanActive.load (std::memory_order_relaxed))
            {
                juce::SpinLock::ScopedLockType sl (scanLock);
                if (scanDurationSec > 0.0)
                    targetSamples = juce::jmax (4096, (int) juce::jmin ((double) windowSamples, scanDurationSec * sampleRate));
            }

            if (fifo.getNumReady() < targetSamples)
                continue;

            int start1, size1, start2, size2;
            fifo.prepareToRead (targetSamples, start1, size1, start2, size2);

            const float* src = ring.getReadPointer (0);
            float* dst = window.getWritePointer (0);
            std::memcpy (dst, src + start1, (size_t) size1 * sizeof (float));
            if (size2 > 0)
                std::memcpy (dst + size1, src + start2, (size_t) size2 * sizeof (float));

            fifo.finishedRead (size1 + size2);

            analyseWindow (pass == 0, dst, size1 + size2);
        }

        rankLibrary();
    }
}

void MatchAnalyzer::analyseWindow (bool isMaster, const float* mono, int numSamples)
{
    const float tempo      = estimateTempoBpm (mono, numSamples, sampleRate);
    const float lufs       = estimateMomentaryLufs (mono, numSamples, sampleRate);
    const float brightness = estimateBrightness (mono, numSamples, sampleRate);

    bool isMinor = false;
    const int key = estimateKeyRoot (mono, numSamples, sampleRate, isMinor);

    // Publish: only overwrite tempo/key when we have a confident reading,
    // so a bad window can't clobber a good estimate.
    if (isMaster)
    {
        if (tempo > 0.0f)
            masterTempoBpm.store (tempo, std::memory_order_relaxed);
        masterLufs.store (lufs, std::memory_order_relaxed);
        masterBrightness.store (brightness, std::memory_order_relaxed);
        if (key >= 0)
        {
            masterKeyRoot.store (key, std::memory_order_relaxed);
            masterKeyIsMinor.store (isMinor, std::memory_order_relaxed);
        }
    }
    else
    {
        if (tempo > 0.0f)
            sampleTempoBpm.store (tempo, std::memory_order_relaxed);
        sampleLufs.store (lufs, std::memory_order_relaxed);
        sampleBrightness.store (brightness, std::memory_order_relaxed);
    }

    updateDnaFromLinear (isMaster, mono, numSamples);
}

void MatchAnalyzer::updateDnaFromLinear (bool isMaster, const float* mono, int numSamples)
{
    // One shared FFT pass (v2.4): the linear band powers feed the stable DNA,
    // the scan accumulator and the energy tracker — no re-running the FFT.
    std::array<double, DnaFingerprint::numSubBands> linPow {};
    double rms = 0.0;
    const bool ok = extractor.computeLinearBandPower (mono, numSamples, sampleRate, linPow, rms);

    if (ok)
    {
        DnaFingerprint fresh = DnaExtractor::fingerprintFromLinearPower (linPow);

        juce::SpinLock::ScopedLockType sl (dnaLock);
        DnaFingerprint& pub = isMaster ? masterDna : sampleDna;

        if (! pub.valid)
        {
            pub = fresh; // first reading: snap to it
        }
        else
        {
            // Envelope follower: smooth across analysis passes so the ranking
            // doesn't jitter with every transient. Macro AND sub-bands are both
            // smoothed — the DNA screens paint the sub-bands, so raw sub-bands
            // would make the screens jitter while the ranking stays stable
            // (Daedalus).
            const float a = dnaSmoothingAlpha;
            for (int i = 0; i < DnaFingerprint::numMacroBands; ++i)
                pub.macro[(size_t) i] = a * fresh.macro[(size_t) i]
                                      + (1.0f - a) * pub.macro[(size_t) i];
            for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
                pub.subBands[(size_t) i] = a * fresh.subBands[(size_t) i]
                                         + (1.0f - a) * pub.subBands[(size_t) i];
            pub.valid = true;
        }
    }
    // else: silent window — keep the last good DNA (unchanged v2.3 behavior).

    updateEnergy (isMaster, rms, linPow, ok);

    if (scanActive.load (std::memory_order_relaxed))
        accumulateScanWindow (isMaster, linPow, ok);
}

void MatchAnalyzer::rankLibrary()
{
    if (! library.isReady() || library.size() == 0)
        return;

    const bool toSidechain = listenToSidechain.load (std::memory_order_relaxed);
    const bool isFrz = frozen.load (std::memory_order_relaxed);
    const bool isLatched = scanLatched.load (std::memory_order_relaxed);

    // Query-mode precedence (exactly one visible mode, Ariadne R1):
    // FROZEN > SCANNED > LIVE.
    DnaFingerprint query;
    if (isFrz)
    {
        juce::SpinLock::ScopedLockType sl (frozenLock);
        query = frozenDna; // frozen reference: matching holds still while browsing
    }
    else if (isLatched)
    {
        juce::SpinLock::ScopedLockType sl (scanLock);
        query = scanDna; // time-integrated scan DNA
    }
    else
    {
        juce::SpinLock::ScopedLockType sl (dnaLock);
        query = toSidechain ? masterDna : sampleDna;
    }

    if (! query.valid)
        return;

    // Energy-role boost (v2.4): the classifier runs every pass so the ENERGY
    // readout stays live, but the boost applies only when the query is not
    // frozen — with an empty role list (STEADY) the scores below are
    // bit-identical to the v2.3 path (QA I5).
    const int section = classifySection();
    juce::StringArray boostRoles;
    if (! isFrz)
        boostRoles = rolesForSection (section);

    library.rankAgainst (query,
                         bandWLow.load (std::memory_order_relaxed),
                         bandWMid.load (std::memory_order_relaxed),
                         bandWHigh.load (std::memory_order_relaxed),
                         boostRoles, energyBoostFactor);
}

//==============================================================================
// Live display spectrum (display-only; never feeds matching).
//
// The analysis thread wakes ~12.5x/second. Each wake drains the small display
// rings and runs ONE 4096-pt FFT per bus on the newest audio, then folds it
// into a fast-attack/medium-release envelope per sub-band. The TRACK DNA
// screen paints this, so the EQ curve visibly moves with the music while the
// stable matching DNA (2 Hz, heavily smoothed) stays exactly as Agamemnon
// verified it.

void MatchAnalyzer::setFreezeRequested (bool wanted)
{
    // Audio thread: atomic store only. The analysis thread owns the
    // freeze/unfreeze transition (see updateFreezeState).
    freezeRequested.store (wanted, std::memory_order_relaxed);
}

DnaFingerprint MatchAnalyzer::getFrozenDna()
{
    juce::SpinLock::ScopedLockType sl (frozenLock);
    return frozenDna;
}

DnaFingerprint MatchAnalyzer::getLiveDna()
{
    juce::SpinLock::ScopedLockType sl (liveLock);
    return listenToSidechain.load (std::memory_order_relaxed) ? liveMasterDna : liveSampleDna;
}

void MatchAnalyzer::updateLiveDisplay()
{
    updateFreezeState(); // owns the freeze/unfreeze transition (analysis thread)
    updateLiveBus (masterDisplayFifo, masterDisplayRing, liveMasterDna);
    updateLiveBus (sampleDisplayFifo, sampleDisplayRing, liveSampleDna);
}

void MatchAnalyzer::updateFreezeState()
{
    const bool wanted = freezeRequested.load (std::memory_order_relaxed);
    const bool isF = frozen.load (std::memory_order_relaxed);

    if (wanted && ! isF)
    {
        // Capture the query DNA — the match-EQ "capture reference" step.
        DnaFingerprint q;
        bool got = false;

        const bool scanWasActive = scanActive.load (std::memory_order_relaxed);
        if (scanWasActive && scanWindows > 0)
        {
            // FREEZE mid-SCAN: finalize the partial accumulation into the
            // frozen snapshot and cancel the scan — exactly one visible query
            // mode, never silent stacking (Ariadne R1). The editor also drops
            // the scanLoop parameter (see freezeButton.onClick).
            std::array<double, DnaFingerprint::numSubBands> avg {};
            for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
                avg[(size_t) i] = scanAccum[(size_t) i] / (double) scanWindows;
            q = DnaExtractor::fingerprintFromLinearPower (avg);
            got = true;
        }
        else if (scanLatched.load (std::memory_order_relaxed))
        {
            // Post-scan FREEZE (H8): the frozen DNA equals the scanned
            // integrated DNA — never a confused mix.
            juce::SpinLock::ScopedLockType sl (scanLock);
            q = scanDna;
            got = q.valid;
        }
        else
        {
            juce::SpinLock::ScopedLockType sl (dnaLock);
            q = listenToSidechain.load (std::memory_order_relaxed) ? masterDna : sampleDna;
            got = q.valid;
        }

        if (scanWasActive)
        {
            scanActive.store (false, std::memory_order_relaxed);
            scanLatched.store (false, std::memory_order_relaxed);
        }

        if (got)
        {
            juce::SpinLock::ScopedLockType sl (frozenLock);
            frozenDna = q;
            frozen.store (true, std::memory_order_relaxed);
        }
        else
        {
            // No valid DNA yet — request ignored, signal disarm for sticky APVTS parameter
            freezeDisarmNeeded.store (true, std::memory_order_relaxed);
        }
    }
    else if (! wanted && isF)
    {
        frozen.store (false, std::memory_order_relaxed);
        // updateDna kept smoothing underneath, so live capture resumes
        // seamlessly — no jump on unfreeze.
    }
}

//==============================================================================
// Loop scan (v2.4): fixed bar-count capture with auto-stop.
//
// State machine (analysis thread owns it; audio thread only flips the
// scanRequested atomic):
//   IDLE --(rising edge of wanted)--> ACTIVE --(deadline)--> LATCHED
//     ^                                |  (user toggles off = cancel/clear)
//     +--------------------------------+
// A latched scan never re-arms itself (edge-triggered): the user toggles the
// SCAN button off and on for a fresh capture. SCAN while FROZEN unfreezes
// first (visible mode change, never silent stacking); FREEZE mid-SCAN
// finalizes the partial accumulation into the frozen snapshot.

void MatchAnalyzer::setScanRequested (bool wanted)
{
    scanRequested.store (wanted, std::memory_order_relaxed);
}

void MatchAnalyzer::setScanBars (int bars)
{
    scanBarsParam.store (bars == 2 ? 2 : (bars == 8 ? 8 : 4), std::memory_order_relaxed);
}

void MatchAnalyzer::setHostTempo (float bpm, int beatsPerBar)
{
    hostBpm.store (bpm, std::memory_order_relaxed);
    hostBeatsPerBar.store (beatsPerBar, std::memory_order_relaxed);
}

double MatchAnalyzer::computeScanDurationSec (int bars, float bpm, int beatsPerBar) noexcept
{
    // 120 BPM fallback: no host tempo (bpm <= 0) — e.g. the Standalone, where
    // JUCE never sets a playhead (setPlayHead is iOS-only in the standalone
    // client), so the fallback is the *normal* path there, not an edge case.
    const float useBpm = bpm > 0.0f ? bpm : 120.0f;
    const int bpb = beatsPerBar > 0 ? beatsPerBar : 4;
    return (double) bars * (double) bpb * 60.0 / (double) useBpm;
}

void MatchAnalyzer::updateScanState (double nowSec)
{
    const bool wanted = scanRequested.load (std::memory_order_relaxed);
    const bool active = scanActive.load (std::memory_order_relaxed);
    const bool latched = scanLatched.load (std::memory_order_relaxed);

    if (wanted && ! scanSeenWanted)
    {
        // Rising edge: arm a fresh capture.
        scanSeenWanted = true;

        // SCAN while FROZEN: unfreeze first — exactly one visible query mode
        // (Ariadne R1). The mode indicator flips FROZEN -> SCANNING, never
        // silent stacking.
        if (frozen.load (std::memory_order_relaxed))
            frozen.store (false, std::memory_order_relaxed);

        const int bars = scanBarsParam.load (std::memory_order_relaxed);
        scanBarsArmed.store (bars, std::memory_order_relaxed);
        {
            // G2-SF1: scanLock covers the scalar writes too — getScanCurrentBar()
            // and getDnaDebugString() read them on the UI thread.
            juce::SpinLock::ScopedLockType sl (scanLock);
            scanDurationSec = computeScanDurationSec (
                bars, hostBpm.load (std::memory_order_relaxed),
                hostBeatsPerBar.load (std::memory_order_relaxed));
            scanStartSec = nowSec;
            scanStopSec = 0.0;
            scanDeadlineSec = nowSec + scanDurationSec;
            scanAccum.fill (0.0);
            scanWindows = 0;
            scanTotalSamples = 0;
            scanDna = DnaFingerprint();
        }
        scanLatched.store (false, std::memory_order_relaxed);
        scanActive.store (true, std::memory_order_relaxed);
    }
    else if (! wanted)
    {
        // Toggle off: cancel an active scan, or clear a latched one.
        scanSeenWanted = false;
        if (active || latched)
        {
            scanActive.store (false, std::memory_order_relaxed);
            scanLatched.store (false, std::memory_order_relaxed);
            juce::SpinLock::ScopedLockType sl (scanLock);
            scanDna = DnaFingerprint();
        }
    }
    else if (active && nowSec >= scanDeadlineSec)
    {
        // Auto-stop: finalize the time-integrated DNA.
        // If the periodic analysis cadence hasn't caught a boundary before the deadline,
        // process any audio that accumulated in the FIFO during the scan:
        if (scanWindows == 0)
        {
            const bool toSidechain = listenToSidechain.load (std::memory_order_relaxed);
            auto& fifo = toSidechain ? masterFifo : sampleFifo;
            auto& ring = toSidechain ? masterRing : sampleRing;

            const int ready = fifo.getNumReady();
            if (ready >= 4096)
            {
                const int n = juce::jmin (ready, ringSize);
                int start1, size1, start2, size2;
                fifo.prepareToRead (n, start1, size1, start2, size2);
                juce::AudioBuffer<float> tempBuf (1, n);
                float* dst = tempBuf.getWritePointer (0);
                const float* src = ring.getReadPointer (0);
                std::memcpy (dst, src + start1, (size_t) size1 * sizeof (float));
                if (size2 > 0)
                    std::memcpy (dst + size1, src + start2, (size_t) size2 * sizeof (float));
                fifo.finishedRead (size1 + size2);

                std::array<double, DnaFingerprint::numSubBands> linPow {};
                double rms = 0.0;
                if (extractor.computeLinearBandPower (dst, n, sampleRate, linPow, rms))
                {
                    accumulateScanWindow (toSidechain, linPow, true);
                }
            }
        }

        {
            juce::SpinLock::ScopedLockType sl (scanLock);
            scanStopSec = nowSec;
            if (scanWindows > 0)
            {
                std::array<double, DnaFingerprint::numSubBands> avg {};
                for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
                    avg[(size_t) i] = scanAccum[(size_t) i] / (double) scanWindows;
                scanDna = DnaExtractor::fingerprintFromLinearPower (avg);
                scanLatched.store (true, std::memory_order_relaxed);
            }
            else
            {
                // Silent auto-stop (G2-SF2): nothing captured, so the APVTS
                // scanLoop param would stay at 1.0 with no falling edge and
                // the SCAN button would go permanently dead. Raise the
                // one-way disarm signal; the editor clears the parameter.
                scanDisarmNeeded.store (true, std::memory_order_relaxed);
            }
        }
        // else: silence the whole time — nothing to latch (H7); the query
        // falls back to the live DNA and the status shows no-signal.
        scanActive.store (false, std::memory_order_relaxed);
    }
}

void MatchAnalyzer::accumulateScanWindow (bool isMaster,
                                          const std::array<double, DnaFingerprint::numSubBands>& linPow,
                                          bool valid)
{
    // Only the query source feeds the scan (same source the ranking uses).
    const bool querySource = (isMaster == listenToSidechain.load (std::memory_order_relaxed));
    if (! querySource || ! valid)
        return;

    for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
        scanAccum[(size_t) i] += linPow[(size_t) i];
    ++scanWindows;
    scanTotalSamples += (int) (sampleRate * 4.0); // analysis window length in samples

    // Publish the accumulating curve for the live "SCANNING…" display.
    std::array<double, DnaFingerprint::numSubBands> avg {};
    for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
        avg[(size_t) i] = scanAccum[(size_t) i] / (double) scanWindows;
    juce::SpinLock::ScopedLockType sl (scanLock);
    scanDna = DnaExtractor::fingerprintFromLinearPower (avg);
}

DnaFingerprint MatchAnalyzer::getScanDna()
{
    juce::SpinLock::ScopedLockType sl (scanLock);
    return scanDna;
}

bool MatchAnalyzer::takeScanDisarmRequest()
{
    // One-way disarm signal for the G2-SF2 silent auto-stop fix.
    return scanDisarmNeeded.exchange (false, std::memory_order_relaxed);
}

bool MatchAnalyzer::takeFreezeDisarmRequest()
{
    // One-way disarm signal when freeze request is rejected due to no valid DNA.
    return freezeDisarmNeeded.exchange (false, std::memory_order_relaxed);
}

int MatchAnalyzer::getScanCurrentBar() const noexcept
{
    if (! scanActive.load (std::memory_order_relaxed))
        return 0;
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    // G2-SF1: scanStartSec/scanDurationSec are written on the analysis thread.
    const juce::SpinLock::ScopedLockType sl (scanLock);
    const double elapsed = now - scanStartSec;
    const int bars = scanBarsArmed.load (std::memory_order_relaxed);
    if (scanDurationSec <= 0.0 || bars <= 0)
        return 1;
    return juce::jlimit (1, bars, 1 + (int) (elapsed / scanDurationSec * (double) bars));
}

//==============================================================================
// Energy tracker + section classifier (v2.4).
//
// Per analysis window we record RMS loudness (dBFS) and brightness (high-band
// energy ratio) for both buses. The classifier runs on the query source's
// history each ranking pass: least-squares slopes over a 2-bar window.
//   BUILDUP: energy rising (≥ +1 dB/s) AND brightness rising
//   PEAK:    high sustained energy
//   BREAKDOWN: low sustained energy
//   STEADY:  everything else (pure DNA similarity — the v2.3 path)
// v1 heuristics: they will misread odd arrangements (spec is honest about
// this) — the output is only a recommendation boost, never a filter.

void MatchAnalyzer::updateEnergy (bool isMaster, double rms,
                                  const std::array<double, DnaFingerprint::numSubBands>& linPow,
                                  bool valid)
{
    if (! valid)
        return;

    double total = 0.0, high = 0.0;
    for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
    {
        total += linPow[(size_t) i];
        const float cHz = 0.5f * (DnaFingerprint::subBandEdgeHz (i)
                                + DnaFingerprint::subBandEdgeHz (i + 1));
        if (DnaFingerprint::macroBandForHz (cHz) == 2)
            high += linPow[(size_t) i];
    }

    EnergySample s;
    s.tSec   = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    s.rmsDb  = (float) (20.0 * std::log10 (rms + 1e-9));
    s.bright = total > 1e-18 ? (float) (high / total) : 0.0f;

    auto& hist = isMaster ? energyHistMaster : energyHistTrack;
    hist.push_back (s);
    while (hist.size() > 48) // ~23 s at the 480 ms analysis cadence
        hist.pop_front();

    energyLastRmsDb.store (s.rmsDb, std::memory_order_relaxed);
    energyLastBright.store (s.bright, std::memory_order_relaxed);
}

int MatchAnalyzer::classifySection()
{
    const bool toSidechain = listenToSidechain.load (std::memory_order_relaxed);
    auto& hist = toSidechain ? energyHistMaster : energyHistTrack;

    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    while (! hist.empty() && now - hist.front().tSec > 25.0)
        hist.pop_front();

    float bpm = hostBpm.load (std::memory_order_relaxed);
    if (bpm <= 0.0f) bpm = 120.0f;
    int bpb = hostBeatsPerBar.load (std::memory_order_relaxed);
    if (bpb < 1) bpb = 4;
    const double winSec = 2.0 * bpb * 60.0 / (double) bpm; // 2-bar window

    int n = 0;
    double sumT = 0, sumE = 0, sumB = 0, sumTE = 0, sumTB = 0, sumT2 = 0;
    for (auto it = hist.rbegin(); it != hist.rend(); ++it)
    {
        if (now - it->tSec > winSec)
            break;
        ++n;
        sumT += it->tSec; sumE += it->rmsDb; sumB += it->bright;
        sumTE += it->tSec * it->rmsDb; sumTB += it->tSec * it->bright;
        sumT2 += it->tSec * it->tSec;
    }

    int raw = 0; // Steady
    const int minSamples = (winSec < 3.0) ? 3 : 5;
    if (n >= minSamples)
    {
        const double denom = n * sumT2 - sumT * sumT;
        double eSlope = 0.0, bSlope = 0.0;
        if (denom > 1e-9)
        {
            eSlope = (n * sumTE - sumT * sumE) / denom;
            bSlope = (n * sumTB - sumT * sumB) / denom;
        }
        energyLastESlope.store ((float) eSlope, std::memory_order_relaxed);
        energyLastBSlope.store ((float) bSlope, std::memory_order_relaxed);

        const double meanE = sumE / (double) n;
        if (eSlope > 1.0 && bSlope > 0.0)
            raw = 1; // Buildup: energy rising ≥2 bars + brightness rising
        else if (meanE > -16.0)
            raw = 2; // Peak: high sustained energy
        else if (meanE < -30.0)
            raw = 3; // Breakdown: low sustained energy
    }

    // Debounce: a new section needs 2 consecutive raw readings — transient
    // spikes can't flicker the readout or the boost.
    const int confirmed = energySection.load (std::memory_order_relaxed);
    if (raw != confirmed)
    {
        if (raw == energyRawPrev)
            energySection.store (raw, std::memory_order_relaxed);
        energyRawPrev = raw;
    }
    else
    {
        energyRawPrev = raw;
    }
    return energySection.load (std::memory_order_relaxed);
}

juce::StringArray MatchAnalyzer::rolesForSection (int section)
{
    // Documented role mapping (also in tools/DEMO_LIBRARY.md):
    switch (section)
    {
        case 1:  return { "riser", "roll", "hats" };      // BUILDUP
        case 2:  return { "impact", "texture", "hats" };   // PEAK
        case 3:  return { "pad", "texture" };               // BREAKDOWN
        default: return {};                                 // STEADY: pure DNA
    }
}

void MatchAnalyzer::updateLiveBus (juce::AbstractFifo& fifo, juce::AudioBuffer<float>& ring,
                                   DnaFingerprint& live)
{
    // Drain the display ring; the FFT runs on the NEWEST 4096 samples so the
    // screen tracks what is playing right now.
    static constexpr int need = 4096;
    const int ready = fifo.getNumReady();

    float targetDb[DnaFingerprint::numSubBands];
    bool haveAudio = false;

    if (ready >= need)
    {
        int start1, size1, start2, size2;
        const int n = juce::jmin (ready, displayRingSize);
        fifo.prepareToRead (n, start1, size1, start2, size2);

        const float* src = ring.getReadPointer (0);
        float* dst = liveScratch.getWritePointer (0);
        std::memcpy (dst, src + start1, (size_t) size1 * sizeof (float));
        if (size2 > 0)
            std::memcpy (dst + size1, src + start2, (size_t) size2 * sizeof (float));

        const int got = size1 + size2;
        const int advance = juce::jmax (1, got - (need - 1));
        fifo.finishedRead (advance);

        DnaFingerprint fresh;
        if (extractor.extractDna (dst + got - need, need, sampleRate, fresh))
        {
            for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
                targetDb[i] = fresh.subBands[(size_t) i];
            haveAudio = true;
        }
    }
    else if (ready > 0 && ready < need)
    {
        juce::SpinLock::ScopedLockType sl (liveLock);
        if (live.valid)
            return;
    }

    if (! haveAudio)
    {
        // No fresh audio (or below the silence gate): decay toward the floor
        // so the curve settles flat instead of freezing mid-shape.
        for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
            targetDb[i] = liveSilenceFloorDb;
    }

    juce::SpinLock::ScopedLockType sl (liveLock);

    if (! live.valid && ! haveAudio)
        return; // nothing ever played: keep the placeholder

    if (! live.valid)
    {
        for (int i = 0; i < DnaFingerprint::numSubBands; ++i) // first audio: snap
            live.subBands[(size_t) i] = targetDb[i];
    }
    else
    {
        // Fast attack / medium release on the sub-band dB: the screen feels
        // alive the moment audio plays, without raw-frame jitter.
        for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
        {
            const float cur = live.subBands[(size_t) i];
            const float tgt = targetDb[i];
            const float k = (tgt > cur) ? liveAttack : liveRelease;
            live.subBands[(size_t) i] = cur + k * (tgt - cur);
        }
    }

    // Macro bands: plain mean of member sub-bands (display only; the matching
    // path never reads these).
    {
        float sum[3] = { 0.0f, 0.0f, 0.0f };
        int   cnt[3] = { 0, 0, 0 };
        for (int i = 0; i < DnaFingerprint::numSubBands; ++i)
        {
            const float cHz = 0.5f * (DnaFingerprint::subBandEdgeHz (i)
                                    + DnaFingerprint::subBandEdgeHz (i + 1));
            const int m = DnaFingerprint::macroBandForHz (cHz);
            sum[(size_t) m] += live.subBands[(size_t) i];
            ++cnt[(size_t) m];
        }
        for (int m = 0; m < 3; ++m)
            live.macro[(size_t) m] = cnt[(size_t) m] > 0 ? sum[(size_t) m] / (float) cnt[(size_t) m]
                                                        : liveSilenceFloorDb;
    }

    live.valid = true;
}

//==============================================================================
// v1 PLACEHOLDER algorithm stubs (unchanged). Each is a TODO pointing at the
// survey section with the real algorithm. Signatures are shaped like the
// real implementations so the call sites don't change.

float MatchAnalyzer::estimateTempoBpm (const float*, int, double)
{
    // TODO(dsp-survey.md §1): spectral-flux onset envelope -> autocorrelation
    // over 40–208 BPM, harmonic suppression, confidence gating.
    return 0.0f; // 0 = no confident estimate yet
}

int MatchAnalyzer::estimateKeyRoot (const float*, int, double, bool& isMinor)
{
    // TODO(dsp-survey.md §2): chromagram (dsp::FFT) -> Krumhansl-Schmuckler
    // template match over 24 keys. Return -1 until confident.
    isMinor = false;
    return -1; // -1 = unknown
}

float MatchAnalyzer::estimateMomentaryLufs (const float*, int, double)
{
    // TODO(dsp-survey.md §4): ITU-R BS.1770 — K-weighting -> 400 ms gated
    // blocks. Easiest correct path: libebur128 (MIT).
    return -70.0f; // silence floor
}

float MatchAnalyzer::estimateBrightness (const float*, int, double)
{
    // TODO(dsp-survey.md §3): spectral centroid via dsp::FFT, smoothed.
    return 0.0f; // 0 = unknown
}
