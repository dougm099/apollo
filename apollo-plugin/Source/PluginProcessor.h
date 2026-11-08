#pragma once

#include <JuceHeader.h>
#include "Analysis/MatchAnalyzer.h"

//==============================================================================
/**
    Apollo v2 — DNA-to-DNA sample matching processor.

    Signal flow:
        main input (the track) ──► [MatchAnalyzer: DNA capture] ──► main output
        sidechain input (master) ──►┘                    (audio passes through)

    The analyzer continuously captures the DNA (spectral fingerprint) of the
    input and ranks the demo sample library DNA-to-DNA with the producer's
    band weights. Apollo is an analyzer in v2: audio passes through untouched
    (search/discovery, not transformation — the EQ "apply" half is a non-goal).

    In GarageBand there is no sidechain routing, so the default listen source
    is the main input ("Track"): put Apollo on a track, play it, and the
    match list ranks the library by that track's DNA. In hosts with sidechain
    (Ableton Live 10.1+), switch the Source to "Sidechain" to listen to the
    master bus instead.
*/
class ApolloAudioProcessor final : public juce::AudioProcessor
{
public:
    //==========================================================================
    ApolloAudioProcessor();
    ~ApolloAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==========================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    MatchAnalyzer& getAnalyzer() noexcept { return analyzer; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    MatchAnalyzer analyzer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApolloAudioProcessor)
};
