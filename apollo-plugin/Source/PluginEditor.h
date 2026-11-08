#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "Analysis/DnaFingerprint.h"

//==============================================================================
/** A small Malström-style DNA display: smooth EQ-filter curve through the 16
    sub-band energies, stroke coloured by macro band (low = amber, mid = green,
    high = blue), with a translucent fill and faint gridlines. Deliberately
    compact — the match-EQ look, not a spectrum analyzer. */
class DnaScreen : public juce::Component
{
public:
    DnaScreen() = default;

    void setDna (const DnaFingerprint& newDna)
    {
        dna = newDna;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0b0d11));
        g.setColour (juce::Colour (0xff262b36));
        g.drawRect (getLocalBounds(), 1);

        if (! dna.valid)
        {
            g.setColour (juce::Colour (0xff3a4152));
            g.setFont (juce::Font (16.0f));
            g.drawText ("—", getLocalBounds(), juce::Justification::centred, true);
            return;
        }

        const int n = DnaFingerprint::numSubBands;
        const float W = (float) getWidth();
        const float H = (float) getHeight();

        float maxDb = -120.0f;
        for (float db : dna.subBands)
            maxDb = juce::jmax (maxDb, db);
        const float floorDb = maxDb - 42.0f;

        auto bandColour = [] (int i) -> juce::Colour
        {
            const float centerHz = 0.5f * (DnaFingerprint::subBandEdgeHz (i)
                                         + DnaFingerprint::subBandEdgeHz (i + 1));
            switch (DnaFingerprint::macroBandForHz (centerHz))
            {
                case 0:  return juce::Colour (0xffe0a060); // low: amber
                case 1:  return juce::Colour (0xff7fd08a); // mid: green
                default: return juce::Colour (0xff6fb7d8); // high: blue
            }
        };

        // Band points — x is log-spaced by construction, so equal pixel spacing
        // is the correct EQ-style axis.
        juce::Point<float> pts[16];
        for (int i = 0; i < n; ++i)
        {
            const float norm = juce::jlimit (0.0f, 1.0f,
                                            (dna.subBands[(size_t) i] - floorDb) / 42.0f);
            pts[i] = { (i + 0.5f) * (W / (float) n),
                       H - 5.0f - norm * (H - 10.0f) };
        }

        // Faint EQ-style gridlines
        g.setColour (juce::Colour (0xff1b1e26));
        for (float f : { 0.25f, 0.5f, 0.75f })
            g.drawHorizontalLine ((int) (H * f), 4.0f, W - 4.0f);

        // Catmull-Rom smoothing through the band points — no jagged polylines.
        auto catmull = [] (juce::Point<float> p0, juce::Point<float> p1,
                           juce::Point<float> p2, juce::Point<float> p3, float t)
        {
            const float t2 = t * t, t3 = t2 * t;
            return juce::Point<float> (
                0.5f * ((2*p1.x) + (-p0.x+p2.x)*t + (2*p0.x-5*p1.x+4*p2.x-p3.x)*t2
                        + (-p0.x+3*p1.x-3*p2.x+p3.x)*t3),
                0.5f * ((2*p1.y) + (-p0.y+p2.y)*t + (2*p0.y-5*p1.y+4*p2.y-p3.y)*t2
                        + (-p0.y+3*p1.y-3*p2.y+p3.y)*t3));
        };

        std::vector<juce::Point<float>> smooth;
        smooth.reserve ((size_t) (n - 1) * 12 + 1);
        for (int i = 0; i < n - 1; ++i)
        {
            const auto p0 = pts[juce::jmax (0, i - 1)];
            const auto p1 = pts[i];
            const auto p2 = pts[i + 1];
            const auto p3 = pts[juce::jmin (n - 1, i + 2)];
            for (int j = 0; j < 12; ++j)
                smooth.push_back (catmull (p0, p1, p2, p3, (float) j / 12.0f));
        }
        smooth.push_back (pts[n - 1]);

        // Translucent fill under the curve
        juce::Path fill;
        fill.startNewSubPath (smooth.front());
        for (size_t i = 1; i < smooth.size(); ++i)
            fill.lineTo (smooth[i]);
        fill.lineTo (smooth.back().x, H - 5.0f);
        fill.lineTo (smooth.front().x, H - 5.0f);
        fill.closeSubPath();
        g.setColour (juce::Colour (0x1c7fd08a));
        g.fillPath (fill);

        // Macro-band coloured stroke segments
        for (size_t i = 0; i + 1 < smooth.size(); ++i)
        {
            const float frac = (float) i / (float) (smooth.size() - 1);
            const int band = juce::jlimit (0, n - 1, (int) (frac * (float) n));
            g.setColour (bandColour (band));
            g.drawLine (smooth[i].x, smooth[i].y, smooth[i+1].x, smooth[i+1].y, 2.5f);
        }
    }

private:
    DnaFingerprint dna;
};

//==============================================================================
/** Ableton-flavoured rotary knob: dark body, arc track, value arc, pointer. */
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = rotaryStartAngle
                          + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xff2a2f3a));
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             rotaryStartAngle, angle, true);
        g.setColour (juce::Colour (0xff8fd49a));
        g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));

        const float bodyR = radius * 0.68f;
        g.setColour (juce::Colour (0xff1e222b));
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (juce::Colour (0xff3a4152));
        g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        const float pa = angle - juce::MathConstants<float>::halfPi;
        g.setColour (juce::Colour (0xffe8ecf4));
        g.drawLine (centre.x, centre.y,
                    centre.x + std::cos (pa) * bodyR * 0.85f,
                    centre.y + std::sin (pa) * bodyR * 0.85f,
                    2.0f);
    }
};

//==============================================================================
/**
    Apollo v2 editor — compact hardware-instrument layout (Malström-inspired).

    Two small DNA screens side by side (TRACK DNA live | SAMPLE DNA of the
    top match), three prominent Ableton-style rotary knobs for the band
    match amounts (LOW / MID / HIGH, 0–100 %), a top-match score readout,
    and the live ranked match list. No full-screen visualizers.
*/
class ApolloAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit ApolloAudioProcessorEditor (ApolloAudioProcessor&);
    ~ApolloAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    //--- ListBox model: one row per ranked library entry ---------------------
    class MatchListModel : public juce::ListBoxModel
    {
    public:
        void setEntries (std::vector<MatchAnalyzer::RankedMatch> newEntries)
        {
            entries = std::move (newEntries);
        }

        /** Set by the editor: a row click means "inspect this candidate". */
        std::function<void()> onRowClicked;

        void listBoxItemClicked (int /*rowNumber*/, const juce::MouseEvent&) override
        {
            if (onRowClicked)
                onRowClicked();
        }

        int getNumRows() override { return (int) entries.size(); }

        void paintListBoxItem (int rowNumber, juce::Graphics& g,
                               int width, int height, bool rowIsSelected) override
        {
            if (rowNumber < 0 || rowNumber >= (int) entries.size())
                return;

            const auto& e = entries[(size_t) rowNumber];

            if (rowIsSelected)
                g.fillAll (juce::Colour (0xff2a2f3a));
            else if (rowNumber % 2 == 0)
                g.fillAll (juce::Colour (0xff1b1e26));

            g.setColour (juce::Colour (0xff8a93a8));
            g.setFont (juce::Font (13.0f));
            g.drawText (juce::String (rowNumber + 1) + ".",
                        10, 0, 30, height, juce::Justification::centredLeft, true);

            g.setColour (rowNumber == 0 ? juce::Colours::white
                                        : juce::Colour (0xffc9cedb));
            g.drawText (e.name, 44, 0, width - 160, height,
                        juce::Justification::centredLeft, true);

            const int barX = width - 110;
            const int barW = 62;
            const int barH = 8;
            const int barY = (height - barH) / 2;
            g.setColour (juce::Colour (0xff333947));
            g.fillRect (barX, barY, barW, barH);
            g.setColour (rowNumber == 0 ? juce::Colour (0xff7fd08a)
                                        : juce::Colour (0xff5a7a68));
            g.fillRect (barX, barY, (int) (barW * juce::jlimit (0.0f, 100.0f, e.scorePct) / 100.0f), barH);

            g.setColour (juce::Colour (0xff8a93a8));
            g.setFont (juce::Font (12.0f));
            g.drawText (juce::String ((int) std::round (e.scorePct)) + "%",
                        barX + barW + 6, 0, 34, height,
                        juce::Justification::centredLeft, true);

            // v2.4 energy boost marker: an amber dot marks rows that earned
            // the energy-role boost this pass (Ariadne R3 — the boost is
            // visible, never hidden).
            if (e.boosted)
            {
                g.setColour (juce::Colour (0xffe0a060));
                g.fillEllipse ((float) (barX + barW + 44), (float) (height / 2 - 4),
                               8.0f, 8.0f);
            }
        }

    private:
        std::vector<MatchAnalyzer::RankedMatch> entries;
    };

    void timerCallback() override;

    ApolloAudioProcessor& processorRef;

    KnobLookAndFeel knobLookAndFeel; // must outlive the knobs

    juce::Label titleLabel;
    juce::Label statusLabel;

    juce::Label trackDnaLabel;
    juce::Label sampleDnaLabel;
    DnaScreen trackScreen;
    DnaScreen sampleScreen;
    juce::Label matchReadout;

    juce::Label bandLabel;
    juce::Label lowLabel, midLabel, highLabel;
    juce::Slider lowKnob, midKnob, highKnob;

    juce::Label sourceLabel;
    juce::ComboBox sourceBox;

    juce::Label listLabel;
    juce::ListBox matchList;
    MatchListModel listModel;
    juce::ToggleButton followTopButton; // off = SAMPLE DNA shows the clicked row
    juce::ToggleButton freezeButton;    // FREEZE: capture the DNA reference (APVTS "freezeDna")
    // v2.4 CAPTURE cluster (Ariadne R2): FREEZE + SCAN + bar-count in one group.
    juce::Label captureLabel;           // "CAPTURE" mini-label over the cluster
    juce::ToggleButton scanButton;      // SCAN: fixed bar-count capture (APVTS "scanLoop")
    juce::ComboBox scanBarsBox;         // 2 / 4 / 8 bars (APVTS "scanBars")
    int lastScanBarsShown = -1;         // keeps the SCAN label fresh without churn
    juce::TooltipWindow tooltipWindow;  // enables setTooltip() hints

    juce::ToggleButton bypassButton;
    juce::ToggleButton dbgButton;   // DNA DBG: shows the QA fingerprint readout
    juce::TextEditor dbgView;       // read-only debug text (hidden unless toggled)

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowAttachment, midAttachment, highAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment, scanBarsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApolloAudioProcessorEditor)
};
