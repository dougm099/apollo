#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ApolloAudioProcessor::ApolloAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Sample In", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Master In", juce::AudioChannelSet::stereo(), true) // sidechain bus
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ApolloParams", createParameterLayout())
{
}

ApolloAudioProcessor::~ApolloAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ApolloAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // DNA band weights: how much each part of the tone counts in the match.
    // low=1, mid=0.5, high=0 means "match the bass, ignore the rest".
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "dnaLow", "DNA Low",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "dnaMid", "DNA Mid",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "dnaHigh", "DNA High",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "listenSource", "Listen Source",
        juce::StringArray { "Track", "Sidechain" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        "bypass", "Bypass", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        "freezeDna", "Freeze DNA", false));

    // v2.4 loop scan: arm toggle + fixed bar-count capture (2/4/8 bars).
    layout.add (std::make_unique<juce::AudioParameterBool> (
        "scanLoop", "Scan Loop", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "scanBars", "Scan Bars",
        juce::StringArray { "2 bars", "4 bars", "8 bars" }, 1)); // default: 4 bars

    return layout;
}

const juce::String ApolloAudioProcessor::getName() const { return JucePlugin_Name; }

bool ApolloAudioProcessor::acceptsMidi() const               { return false; }
bool ApolloAudioProcessor::producesMidi() const              { return false; }
bool ApolloAudioProcessor::isMidiEffect() const              { return false; }
double ApolloAudioProcessor::getTailLengthSeconds() const   { return 0.0; }

int ApolloAudioProcessor::getNumPrograms()                  { return 1; }
int ApolloAudioProcessor::getCurrentProgram()               { return 0; }
void ApolloAudioProcessor::setCurrentProgram (int)           {}
const juce::String ApolloAudioProcessor::getProgramName (int) { return {}; }
void ApolloAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void ApolloAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analyzer.prepare (sampleRate, samplesPerBlock);
}

void ApolloAudioProcessor::releaseResources()
{
    analyzer.reset();
}

bool ApolloAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Main and sidechain buses must agree (both stereo or both mono).
    // Ableton Live exposes the second input bus as the VST3 sidechain input.
    const auto mainIn  = layouts.getChannelSet (true,  0);
    const auto sideIn  = layouts.getChannelSet (true,  1);
    const auto mainOut = layouts.getChannelSet (false, 0);

    if (mainIn.isDisabled() || mainOut.isDisabled())
        return false;

    if (! sideIn.isDisabled() && sideIn != mainIn)
        return false;

    return mainOut == mainIn;
}

//==============================================================================
void ApolloAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int mainIns    = getChannelCountOfBus (true, 0);
    const int sideIns    = getChannelCountOfBus (true, 1);

    // Defensive: clear output channels beyond the inputs (hosts vary).
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const bool bypassed = *apvts.getRawParameterValue ("bypass") > 0.5f;

    // Push the producer's DNA controls to the analyzer (atomics: real-time safe).
    analyzer.setBandWeights (*apvts.getRawParameterValue ("dnaLow"),
                             *apvts.getRawParameterValue ("dnaMid"),
                             *apvts.getRawParameterValue ("dnaHigh"));
    analyzer.setListenToSidechain ((int) *apvts.getRawParameterValue ("listenSource") == 1);
    analyzer.setFreezeRequested (*apvts.getRawParameterValue ("freezeDna") > 0.5f);

    // v2.4 loop scan controls (atomics: real-time safe).
    analyzer.setScanRequested (*apvts.getRawParameterValue ("scanLoop") > 0.5f);
    {
        const int barsIdx = (int) *apvts.getRawParameterValue ("scanBars");
        analyzer.setScanBars (barsIdx == 0 ? 2 : (barsIdx == 2 ? 8 : 4));
    }

    // v2.4 host tempo for bar math (scan duration, classifier window).
    // AudioPlayHead access is real-time safe here (the host owns the object).
    {
        double bpm = 0.0;
        int beatsPerBar = 4;
        if (auto* playHead = getPlayHead())
        {
            if (auto pos = playHead->getPosition())
            {
                bpm = pos->getBpm().orFallback (0.0);
                if (auto ts = pos->getTimeSignature())
                    beatsPerBar = juce::jmax (1, ts->numerator);
            }
        }
        // bpm <= 0 (no host tempo — e.g. the Standalone, where JUCE never sets
        // a playhead) -> the analyzer's 120 BPM fallback. Verified exercisable:
        // see BUILD_LOG.md v2.4 (JUCE 9.0.2 standalone client source).
        analyzer.setHostTempo ((float) bpm, beatsPerBar);
    }

    if (! bypassed)
    {
        // --- Analysis feed ------------------------------------------------
        // Audio thread: copy only. Never allocate, never block, never do DSP.
        const float* sampleL = buffer.getReadPointer (0);
        const float* sampleR = mainIns > 1 ? buffer.getReadPointer (1) : sampleL;

        if (sideIns > 0)
        {
            // Input channels are laid out [main bus][sidechain bus].
            const float* masterL = buffer.getReadPointer (mainIns);
            const float* masterR = sideIns > 1 ? buffer.getReadPointer (mainIns + 1) : masterL;
            analyzer.pushMasterBlock (masterL, masterR, numSamples);
        }
        analyzer.pushSampleBlock (sampleL, sampleR, numSamples);

        // v2 is analysis-only: audio passes through untouched. The DNA match
        // list in the editor is the product (search/discovery, no transform).
    }
}

//==============================================================================
bool ApolloAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* ApolloAudioProcessor::createEditor()
{
    return new ApolloAudioProcessorEditor (*this);
}

//==============================================================================
void ApolloAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos (destData, true);
    apvts.state.writeToStream (mos);
}

void ApolloAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
        tree.isValid())
        apvts.replaceState (tree);
}

//==============================================================================
// This creates new instances of the plugin. Required by the JUCE hosting code.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ApolloAudioProcessor();
}
