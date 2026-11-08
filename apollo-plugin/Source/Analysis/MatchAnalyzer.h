#pragma once

#include <JuceHeader.h>
#include <deque>
#include "DnaExtractor.h"
#include "DnaFingerprint.h"
#include "DnaLibrary.h"

//==============================================================================
/**
    MatchAnalyzer — Apollo's analysis engine (v2: DNA matching).

    Threading contract (unchanged from v1):
      * The audio thread ONLY copies samples into lock-free FIFOs.
        It never allocates, never blocks, never runs DSP here.
      * A background thread consumes the FIFOs and runs the analysis.
      * Results are published through atomics / a spin-locked snapshot,
        so the audio thread and the UI can read a coherent view at any time.

    v2 additions:
      * DnaExtractor captures the "DNA" (spectral fingerprint) of the master
        (sidechain) and sample (main input) buses from each analysis window.
      * Published DNAs are envelope-smoothed across analysis passes so the
        match ranking doesn't jitter with every transient.
      * DnaLibrary holds the demo sample set (~/Music/Apollo/DemoLibrary)
        and ranks it DNA-to-DNA against the live query DNA with the
        producer's band weights (weighted cosine similarity on
        loudness-normalized macro-band dB).
      * The query DNA comes from the sidechain ("Master") or the main input
        ("Track"), selected by the listenSource parameter.
      * v2.3: the TRACK/SIDECHAIN DNA screen is a LIVE display — a separate
        lightly-smoothed spectrum (~12.5 fps, fast-attack/medium-release
        envelope) that moves with the music. Matching still uses the stable
        envelope-smoothed DNAs above; the live path never feeds ranking.
      * FREEZE: APVTS bool "freezeDna" captures the stable query DNA at the
        freeze moment; rankLibrary() then matches against the snapshot while
        the TRACK DNA screen shows it static with a FROZEN indicator.
        Unfreezing resumes live capture seamlessly (the smoother kept running
        underneath). With no valid DNA yet the request is ignored — the button
        stays inert.
      * v2.4: SCAN — APVTS bool "scanLoop" + int "scanBars" (2/4/8). Arming
        accumulates the query DNA across a fixed bar count (host tempo via
        AudioPlayHead, 120 BPM fallback) then auto-stops; the time-integrated
        DNA becomes a latched query. FREEZE pressed mid-scan finalizes the
        partial accumulation into the frozen snapshot; SCAN pressed while
        frozen unfreezes first. Exactly one query mode is ever visible:
        LIVE / FROZEN / SCANNED.
      * v2.4: energy-aware recommendations — an RMS + brightness envelope
        tracker feeds a slope-heuristic section classifier
        (STEADY/BUILDUP/PEAK/BREAKDOWN). Non-STEADY sections apply a bounded
        multiplicative boost (≤ 2×, documented) to role-tagged library
        entries. DNA similarity stays the base score; with an empty boost
        list (STEADY or frozen) the ranking is bit-identical to v2.3.

    Tempo/key/loudness/brightness stubs from v1 are kept for future use.
*/
class MatchAnalyzer : private juce::Thread
{
public:
    MatchAnalyzer();
    ~MatchAnalyzer() override;

    /** Must be called from prepareToPlay. Starts the analysis thread. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Clears FIFOs and published results. */
    void reset();

    //--- Audio-thread feed --------------------------------------------------
    void pushMasterBlock (const float* left, const float* right, int numSamples);
    void pushSampleBlock (const float* left, const float* right, int numSamples);

    //--- DNA controls (set from the audio thread / parameter changes) -------
    void setBandWeights (float low, float mid, float high);
    void setListenToSidechain (bool shouldListenToSidechain);

    //--- Published results (lock-free reads) --------------------------------
    struct RankedMatch
    {
        juce::String name;
        float scorePct = 0.0f;
        bool boosted = false; // earned the energy-role boost this pass
    };

    /** Ranked demo-library matches, best DNA match first. */
    std::vector<RankedMatch> getRankedMatches();

    /** Copy of the current query DNA (selected listen source), for the UI. */
    DnaFingerprint getQueryDna();

    /** Live, lightly-smoothed display spectrum for the selected listen source
        (feeds the TRACK/SIDECHAIN DNA screen at ~12.5 fps). Display-only:
        the MATCHING path keeps using the stable getQueryDna(). */
    DnaFingerprint getLiveDna();

    /** Freeze control (APVTS "freezeDna", forwarded per block). When frozen,
        rankLibrary() matches against the captured snapshot and the TRACK DNA
        screen shows it static. The analyzer owns the authoritative state —
        the editor syncs its button from isFrozen(). */
    void setFreezeRequested (bool wanted);
    bool isFrozen() const noexcept { return frozen.load (std::memory_order_relaxed); }
    /** The frozen reference fingerprint (valid only when isFrozen()). */
    DnaFingerprint getFrozenDna();

    //--- Loop scan (v2.4) ------------------------------------------------------
    /** Arm/disarm (APVTS "scanLoop", forwarded per block). Rising edge arms a
        fixed bar-count capture; the analysis thread auto-stops at the
        deadline and latches the integrated DNA. */
    void setScanRequested (bool wanted);
    /** Bar count for the next scan (APVTS "scanBars": 2, 4 or 8). */
    void setScanBars (int bars);
    /** Host tempo for bar math (forwarded per block from AudioPlayHead).
        bpm <= 0 means "no host tempo" -> 120 BPM fallback. */
    void setHostTempo (float bpm, int beatsPerBar);
    bool isScanActive()  const noexcept { return scanActive.load (std::memory_order_relaxed); }
    bool isScanLatched() const noexcept { return scanLatched.load (std::memory_order_relaxed); }
    int  getScanBarsArmed() const noexcept { return scanBarsArmed.load (std::memory_order_relaxed); }
    /** 1-based bar counter for the "SCANNING… bar X/N" status. */
    int  getScanCurrentBar() const noexcept;
    /** The accumulating (active) or final (latched) scan DNA. Valid only when
        active or latched. */
    DnaFingerprint getScanDna();
    /** One-way disarm signal: true once when a scan auto-stops with zero
        valid windows (APVTS scanLoop stuck at 1.0). The editor consumes it
        and resets the parameter. Load + clear, relaxed. */
    bool takeScanDisarmRequest();
    /** One-way disarm signal: true once when a freeze request is rejected
        due to silence/no valid DNA (APVTS freezeDna stuck at 1.0). */
    bool takeFreezeDisarmRequest();

    //--- Energy section (v2.4) ---------------------------------------------------
    enum class EnergySection { Steady = 0, Buildup = 1, Peak = 2, Breakdown = 3 };
    EnergySection getEnergySection() const noexcept
        { return (EnergySection) energySection.load (std::memory_order_relaxed); }
    static const char* energySectionName (EnergySection s) noexcept;

    /** QA hook: multi-line text dump of the query DNA, band weights, and every
        library entry's DNA + current score. Used to measure extraction
        stability, loudness invariance, and ranking correctness (no eyeballing).
        v2.4 also logs scan state and the energy classifier internals.
        Shown in the editor via the DNA DBG toggle. */
    juce::String getDnaDebugString();

    /** DNA + name of the current top-ranked library entry. False if none. */
    bool getTopMatchDna (DnaFingerprint& dna, juce::String& name);

    /** DNA + name of the ranked entry at rankIndex (0 = best). False if out of range. */
    bool getRankedEntryDna (int rankIndex, DnaFingerprint& dna, juce::String& name);

    /** One-line status for the UI ("DNA LIVE — TRACK", "NO SIGNAL", ...). */
    juce::String getDnaStatusText();

    // v1 stubs (kept; not shown in the v2 UI)
    float getMasterTempoBpm()   const noexcept { return masterTempoBpm.load (std::memory_order_relaxed); }
    float getSampleTempoBpm()   const noexcept { return sampleTempoBpm.load (std::memory_order_relaxed); }
    float getMasterLufs()       const noexcept { return masterLufs.load (std::memory_order_relaxed); }
    float getSampleLufs()       const noexcept { return sampleLufs.load (std::memory_order_relaxed); }
    float getMasterBrightness() const noexcept { return masterBrightness.load (std::memory_order_relaxed); }
    float getSampleBrightness() const noexcept { return sampleBrightness.load (std::memory_order_relaxed); }
    int   getMasterKeyRoot()    const noexcept { return masterKeyRoot.load (std::memory_order_relaxed); }
    bool  getMasterKeyIsMinor() const noexcept { return masterKeyIsMinor.load (std::memory_order_relaxed); }

private:
    //--- Background analysis thread ------------------------------------------
    void run() override;
    void analyseWindow (bool isMaster, const float* mono, int numSamples);
    void updateDnaFromLinear (bool isMaster, const float* mono, int numSamples);
    void rankLibrary();
    void updateLiveDisplay();
    void updateLiveBus (juce::AbstractFifo& fifo, juce::AudioBuffer<float>& ring,
                        DnaFingerprint& live);
    void updateFreezeState();

    //--- Loop scan (v2.4, analysis thread) -------------------------------------
    void updateScanState (double nowSec);
    void accumulateScanWindow (bool isMaster,
                               const std::array<double, DnaFingerprint::numSubBands>& linPow,
                               bool valid);
    static double computeScanDurationSec (int bars, float bpm, int beatsPerBar) noexcept;

    //--- Energy tracker + classifier (v2.4, analysis thread) ---------------------
    struct EnergySample { double tSec; float rmsDb; float bright; };
    void updateEnergy (bool isMaster, double rms,
                       const std::array<double, DnaFingerprint::numSubBands>& linPow,
                       bool valid);
    int classifySection(); // returns EnergySection as int; debounced; updates the atomic
    static juce::StringArray rolesForSection (int section);

    // v1 placeholder estimators (unchanged)
    float estimateTempoBpm (const float* mono, int numSamples, double sampleRate);
    int   estimateKeyRoot (const float* mono, int numSamples, double sampleRate, bool& isMinor);
    float estimateMomentaryLufs (const float* mono, int numSamples, double sampleRate);
    float estimateBrightness (const float* mono, int numSamples, double sampleRate);

    // Lock-free FIFOs: audio thread writes, analysis thread reads.
    static constexpr int ringSeconds = 8;
    juce::AbstractFifo masterFifo { 1 };
    juce::AbstractFifo sampleFifo { 1 };
    juce::AudioBuffer<float> masterRing, sampleRing; // mono ring buffers
    int ringSize = 0;

    double sampleRate = 44100.0;

    //--- DNA state ------------------------------------------------------------
    DnaExtractor extractor;
    DnaLibrary   library;
    bool libraryScanned = false;

    // Envelope smoothing across analysis passes (alpha per pass; the thread
    // wakes ~2x/second, so alpha 0.5 settles in about a second — no jitter,
    // still tracks a changing mix).
    static constexpr float dnaSmoothingAlpha = 0.5f;

    juce::SpinLock dnaLock;
    DnaFingerprint masterDna; // smoothed, published (sidechain bus)
    DnaFingerprint sampleDna; // smoothed, published (main input bus)

    //--- Live display spectrum (display-only; never feeds matching) -----------
    // Updated ~12.5x/second on the analysis thread: single 4096-pt FFT per bus
    // + fast-attack/medium-release envelope, so the TRACK DNA screen visibly
    // moves with the music. Published under liveLock, which is NEVER held
    // together with dnaLock (same discipline Daedalus verified).
    static constexpr int displayRingSize = 16384;
    juce::AbstractFifo masterDisplayFifo { 1 }, sampleDisplayFifo { 1 };
    juce::AudioBuffer<float> masterDisplayRing, sampleDisplayRing;
    juce::AudioBuffer<float> liveScratch; // sized in prepare()

    juce::SpinLock liveLock;
    DnaFingerprint liveMasterDna; // lightly-smoothed, display-only (sidechain)
    DnaFingerprint liveSampleDna; // lightly-smoothed, display-only (main input)

    // Envelope coefficients per 80 ms display wake.
    static constexpr float liveAttack  = 0.55f; // fast: snaps in ~1/4 s
    static constexpr float liveRelease = 0.10f; // medium: ~0.8 s trail
    static constexpr float liveSilenceFloorDb = -90.0f;

    //--- Freeze state ---------------------------------------------------------
    // The audio thread only flips freezeRequested (atomic store). The analysis
    // thread owns the transition: on request it snapshots the last STABLE
    // query DNA under dnaLock; frozenLock guards the snapshot and is NEVER
    // held together with dnaLock or liveLock.
    std::atomic<bool> freezeRequested { false };
    std::atomic<bool> frozen { false };
    std::atomic<bool> freezeDisarmNeeded { false };
    juce::SpinLock frozenLock;
    DnaFingerprint frozenDna; // captured stable query DNA at the freeze moment

    //--- Loop scan state (v2.4) ---------------------------------------------------
    // Audio thread flips scanRequested / scanBarsParam / hostBpm /
    // hostBeatsPerBar (atomic stores only). The analysis thread owns the
    // arm -> accumulate -> auto-stop -> latch state machine (edge-triggered
    // on scanRequested, so a latched scan never re-arms itself).
    // scanLock guards scanDna and is NEVER held with dnaLock/liveLock/
    // frozenLock. scanActive/scanLatched are atomics so the UI can poll them
    // without locking.
    std::atomic<bool>  scanRequested { false };
    std::atomic<int>   scanBarsParam { 4 };
    std::atomic<float> hostBpm { 0.0f };       // <= 0: no host tempo -> 120 fallback
    std::atomic<int>   hostBeatsPerBar { 4 };
    std::atomic<bool>  scanActive { false };
    std::atomic<bool>  scanLatched { false };
    std::atomic<bool>  scanDisarmNeeded { false }; // G2-SF2: set on silent auto-stop;
                                                   // editor consumes it and clears scanLoop
    std::atomic<int>   scanBarsArmed { 4 };
    bool scanSeenWanted = false;               // rising-edge detector (analysis thread)
    std::array<double, DnaFingerprint::numSubBands> scanAccum {};
    int    scanWindows = 0;
    int    scanTotalSamples = 0;               // QA hook H1: captured sample count
    double scanStartSec = 0.0;                 // QA hook H1: scan start timestamp
    double scanStopSec = 0.0;                  // QA hook H1: scan stop timestamp
    double scanDeadlineSec = 0.0;
    double scanDurationSec = 0.0;
    mutable juce::SpinLock scanLock;   // mutable: getScanCurrentBar() is const
    DnaFingerprint scanDna;                    // accumulating display + latched query

    //--- Energy tracker + section classifier (v2.4) -------------------------------
    // Per-bus envelope history (analysis thread only): RMS loudness + brightness
    // (high-band energy ratio) per analysis window. The classifier runs on the
    // query source's history each ranking pass; the confirmed section is
    // published through energySection (atomic) for the UI. Debounced: a new
    // section needs 2 consecutive raw readings, so transient spikes can't
    // flicker the readout.
    std::deque<EnergySample> energyHistMaster, energyHistTrack;
    std::atomic<int> energySection { 0 };      // EnergySection as int
    int energyRawPrev = 0;
    std::atomic<float> energyLastRmsDb { -70.0f };  // QA hook I-a: latest envelope values
    std::atomic<float> energyLastBright { 0.0f };    // (single-writer analysis thread,
    std::atomic<float> energyLastESlope { 0.0f };    //  single-reader UI thread)
    std::atomic<float> energyLastBSlope { 0.0f };

    // Multiplicative energy-role boost applied to role-appropriate entries.
    // Documented bound: ≤ 2× per the v2.4 spec — a boost, never a filter.
    static constexpr float energyBoostFactor = 1.25f;

    std::atomic<float> bandWLow  { 1.0f };
    std::atomic<float> bandWMid  { 1.0f };
    std::atomic<float> bandWHigh { 1.0f };
    std::atomic<bool>  listenToSidechain { false };

    //--- v1 published stubs ----------------------------------------------------
    std::atomic<float> masterTempoBpm { 0.0f }, sampleTempoBpm { 0.0f };
    std::atomic<float> masterLufs { -70.0f },   sampleLufs { -70.0f };
    std::atomic<float> masterBrightness { 0.0f }, sampleBrightness { 0.0f };
    std::atomic<int>   masterKeyRoot { -1 };
    std::atomic<bool>  masterKeyIsMinor { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchAnalyzer)
};
