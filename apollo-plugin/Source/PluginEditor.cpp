#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath> // std::round

//==============================================================================
ApolloAudioProcessorEditor::ApolloAudioProcessorEditor (ApolloAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    titleLabel.setText ("APOLLO  —  DNA MATCH", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (22.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    statusLabel.setText ("LIBRARY: SCANNING...", juce::dontSendNotification);
    statusLabel.setFont (juce::Font (11.0f));
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8a93a8));
    addAndMakeVisible (statusLabel);

    //--- DNA screens ------------------------------------------------------------
    auto setupScreenLabel = [this] (juce::Label& l, const char* name)
    {
        l.setText (name, juce::dontSendNotification);
        l.setFont (juce::Font (12.0f, juce::Font::bold));
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, juce::Colour (0xffaab2c8));
        addAndMakeVisible (l);
    };
    setupScreenLabel (trackDnaLabel, "TRACK DNA");
    setupScreenLabel (sampleDnaLabel, "SAMPLE DNA");
    addAndMakeVisible (trackScreen);
    addAndMakeVisible (sampleScreen);

    matchReadout.setText ("TOP MATCH   —", juce::dontSendNotification);
    matchReadout.setFont (juce::Font (13.0f, juce::Font::bold));
    matchReadout.setJustificationType (juce::Justification::centredLeft);
    matchReadout.setColour (juce::Label::textColourId, juce::Colour (0xff7fd08a));
    addAndMakeVisible (matchReadout);

    //--- Band match-amount knobs (the core interaction) --------------------------
    bandLabel.setText ("BAND MATCH AMOUNT", juce::dontSendNotification);
    bandLabel.setFont (juce::Font (12.0f, juce::Font::bold));
    bandLabel.setJustificationType (juce::Justification::centred);
    bandLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaab2c8));
    addAndMakeVisible (bandLabel);

    auto setupKnob = [this] (juce::Slider& k, juce::Label& l, const char* name,
                             const char* paramId, const char* tooltip,
                             std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        l.setText (name, juce::dontSendNotification);
        l.setFont (juce::Font (13.0f, juce::Font::bold));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);

        k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
        k.setDoubleClickReturnValue (true, 1.0); // Ableton muscle memory: reset to 100%
        k.setTooltip (tooltip);
        k.setLookAndFeel (&knobLookAndFeel);
        addAndMakeVisible (k);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.apvts, paramId, k);
    };

    setupKnob (lowKnob,  lowLabel,  "LOW",  "dnaLow",
               "How much the LOW end counts toward the DNA match (automatable)", lowAttachment);
    setupKnob (midKnob,  midLabel,  "MID",  "dnaMid",
               "How much the MID range counts toward the DNA match (automatable)", midAttachment);
    setupKnob (highKnob, highLabel, "HIGH", "dnaHigh",
               "How much the HIGH end counts toward the DNA match (automatable)", highAttachment);

    // Knob parameters are 0..1; show 0..100 %.
    auto percentText  = [] (double v) { return juce::String ((int) std::round (v * 100.0)); };
    auto percentValue = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
    for (auto* k : { &lowKnob, &midKnob, &highKnob })
    {
        k->textFromValueFunction = percentText;
        k->valueFromTextFunction = percentValue;
        k->setTextValueSuffix ("%");
    }

    //--- Listen source ------------------------------------------------------------
    sourceLabel.setText ("Listen to:", juce::dontSendNotification);
    sourceLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (sourceLabel);

    sourceBox.addItem ("Track", 1);
    sourceBox.addItem ("Sidechain", 2);
    sourceBox.setSelectedId (1, juce::dontSendNotification);
    sourceBox.setTooltip ("Listen source. \"Sidechain\" needs a host with sidechain routing "
                          "(e.g. Ableton Live) — in GarageBand, use \"Track\".");
    addAndMakeVisible (sourceBox);

    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.apvts, "listenSource", sourceBox);

    //--- Freeze: capture the DNA reference (match-EQ "capture" step) ------------
    freezeButton.setButtonText ("FREEZE");
    freezeButton.setToggleState (false, juce::dontSendNotification);
    freezeButton.setTooltip ("Capture the current DNA as the match reference — instant, "
                             "and it stays until you unfreeze. "
                             "Matching runs against the frozen fingerprint until "
                             "you unfreeze. (Automatable host parameter.)");
    freezeButton.onClick = [this]
    {
        // Route through the APVTS parameter so hosts see and automate it; the
        // analyzer owns the authoritative frozen state (timerCallback syncs
        // this button from analyzer.isFrozen()).
        if (auto* param = processorRef.apvts.getParameter ("freezeDna"))
            param->setValueNotifyingHost (freezeButton.getToggleState() ? 1.0f : 0.0f);
        // FREEZE mid-SCAN: the analyzer finalizes the partial scan into the
        // frozen snapshot and cancels the scan; drop the scan toggle too so
        // the SCAN button, the parameter and the analyzer agree (Ariadne R1).
        if (freezeButton.getToggleState())
            if (auto* sp = processorRef.apvts.getParameter ("scanLoop"))
                sp->setValueNotifyingHost (0.0f);
    };
    addAndMakeVisible (freezeButton);

    //--- CAPTURE cluster (v2.4, Ariadne R2): FREEZE + SCAN + bar-count, one
    //    visual group with one label. SCAN carries its time semantics in the
    //    label ("SCAN 4 BARS"); FREEZE is instant, SCAN is a countdown — the
    //    two behaviors feel different by construction (Ariadne R1).
    captureLabel.setText ("CAPTURE", juce::dontSendNotification);
    captureLabel.setFont (juce::Font (10.0f, juce::Font::bold));
    captureLabel.setJustificationType (juce::Justification::centred);
    captureLabel.setColour (juce::Label::textColourId, juce::Colour (0xff6b7285));
    addAndMakeVisible (captureLabel);

    scanButton.setButtonText ("SCAN 4 BARS");
    scanButton.setToggleState (false, juce::dontSendNotification);
    scanButton.setTooltip ("Capture the DNA across a fixed bar count at the host tempo, "
                           "then auto-stop — the time-integrated fingerprint becomes "
                           "the match query. No second press needed. "
                           "(Automatable host parameter.)");
    scanButton.onClick = [this]
    {
        // Route through the APVTS parameter so hosts see and automate it; the
        // analyzer owns the authoritative scan state (timerCallback syncs
        // this button from analyzer.isScanActive()/isScanLatched()).
        if (auto* param = processorRef.apvts.getParameter ("scanLoop"))
            param->setValueNotifyingHost (scanButton.getToggleState() ? 1.0f : 0.0f);
        // SCAN while FROZEN: unfreeze first — exactly one visible query mode
        // (the analyzer also unfreezes as a backstop, Ariadne R1).
        if (scanButton.getToggleState())
            if (auto* fp = processorRef.apvts.getParameter ("freezeDna"))
                fp->setValueNotifyingHost (0.0f);
    };
    addAndMakeVisible (scanButton);

    scanBarsBox.addItem ("2 bars", 1);
    scanBarsBox.addItem ("4 bars", 2);
    scanBarsBox.addItem ("8 bars", 3);
    scanBarsBox.setSelectedId (2, juce::dontSendNotification);
    scanBarsBox.setTooltip ("Bar count for the next loop scan. Duration = bars × 4 beats "
                            "at the host tempo (120 BPM if the host reports none).");
    addAndMakeVisible (scanBarsBox);

    scanBarsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.apvts, "scanBars", scanBarsBox);

    //--- Ranked match list ---------------------------------------------------------
    listLabel.setText ("DNA MATCHES  (live)", juce::dontSendNotification);
    listLabel.setFont (juce::Font (12.0f, juce::Font::bold));
    listLabel.setJustificationType (juce::Justification::centredLeft);
    listLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaab2c8));
    addAndMakeVisible (listLabel);

    matchList.setModel (&listModel);
    matchList.setRowHeight (24);
    matchList.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0b0d11));
    matchList.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff262b36));
    addAndMakeVisible (matchList);

    //--- Follow-top toggle: clicking a row inspects that candidate --------------
    followTopButton.setButtonText ("FOLLOW TOP");
    followTopButton.setToggleState (true, juce::dontSendNotification);
    followTopButton.setTooltip ("When on, SAMPLE DNA tracks the live #1 match. "
                                "Click any row to inspect that candidate instead.");
    followTopButton.onClick = [this]
    {
        if (followTopButton.getToggleState())
            matchList.deselectAllRows();
    };
    addAndMakeVisible (followTopButton);

    listModel.onRowClicked = [this]
    {
        // A row click means "inspect this candidate": stop following the leader.
        followTopButton.setToggleState (false, juce::dontSendNotification);
    };

    //--- Bypass ----------------------------------------------------------------------
    bypassButton.setButtonText ("Bypass");
    addAndMakeVisible (bypassButton);

    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, "bypass", bypassButton);

    //--- DNA debug readout (QA hook: fingerprint vectors, not eyeballing) ----------
    // Demoted to a small header button (Ariadne R2): it's a QA tool, not a
    // performance control.
    dbgButton.setButtonText ("DBG");
    dbgButton.setTooltip ("DNA debug readout (QA tool): query fingerprint, scan state, "
                          "energy classifier internals, library scores.");
    addAndMakeVisible (dbgButton);

    dbgView.setMultiLine (true, false);
    dbgView.setReadOnly (true);
    dbgView.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.0f, 0));
    dbgView.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0b0d11));
    dbgView.setColour (juce::TextEditor::textColourId, juce::Colour (0xff9fe8b0));
    dbgView.setVisible (false);
    addAndMakeVisible (dbgView);

    setSize (640, 500);
    startTimer (80); // ~12.5 fps: the TRACK DNA screen is a live display
}

ApolloAudioProcessorEditor::~ApolloAudioProcessorEditor()
{
    stopTimer();
    matchList.setModel (nullptr);
    for (auto* k : { &lowKnob, &midKnob, &highKnob })
        k->setLookAndFeel (nullptr);
}

//==============================================================================
void ApolloAudioProcessorEditor::timerCallback()
{
    auto& analyzer = processorRef.getAnalyzer();

    statusLabel.setText (analyzer.getDnaStatusText(), juce::dontSendNotification);

    const int source = (int) *processorRef.apvts.getRawParameterValue ("listenSource");
    const bool isFrz = analyzer.isFrozen();
    freezeButton.setToggleState (isFrz, juce::dontSendNotification);
    if (analyzer.takeFreezeDisarmRequest())
        if (auto* param = processorRef.apvts.getParameter ("freezeDna"))
            param->setValueNotifyingHost (0.0f);

    //--- v2.4 scan sync ------------------------------------------------------------
    // Silent auto-stop disarm (G2-SF2): if a scan ran its full bar count with
    // zero valid windows, the APVTS scanLoop param is stuck at 1.0 with no
    // falling edge and every later click is a swallowed no-op. The analyzer
    // raises scanDisarmNeeded exactly once; we clear the parameter here on
    // the message thread. Never reset on the bare paramOn && !scanOn
    // condition — that would clobber a fresh arm in the <=80 ms before the
    // analysis thread sees the rising edge.
    if (analyzer.takeScanDisarmRequest())
        if (auto* param = processorRef.apvts.getParameter ("scanLoop"))
            param->setValueNotifyingHost (0.0f);
    const bool scanOn = analyzer.isScanActive() || analyzer.isScanLatched();
    scanButton.setToggleState (scanOn, juce::dontSendNotification);

    // SCAN button carries its time semantics in the label (Ariadne R1).
    const int barsIdx = (int) *processorRef.apvts.getRawParameterValue ("scanBars");
    const int bars = barsIdx == 0 ? 2 : (barsIdx == 2 ? 8 : 4);
    if (bars != lastScanBarsShown)
    {
        lastScanBarsShown = bars;
        scanButton.setButtonText ("SCAN " + juce::String (bars) + " BARS");
    }

    //--- TRACK DNA screen: frozen > scanned > live ----------------------------------
    juce::String trackMode;
    if (isFrz)
        trackMode = "  •  FROZEN";
    else if (analyzer.isScanActive())
        trackMode = "  •  SCANNING";
    else if (analyzer.isScanLatched())
        trackMode = "  •  SCANNED";

    trackDnaLabel.setText ((source == 1 ? "SIDECHAIN DNA" : "TRACK DNA") + trackMode,
                           juce::dontSendNotification);

    if (isFrz)
        trackScreen.setDna (analyzer.getFrozenDna()); // frozen reference: static curve
    else if (scanOn)
        trackScreen.setDna (analyzer.getScanDna()); // accumulating/final scan curve
    else
        trackScreen.setDna (analyzer.getLiveDna()); // live display path; matching keeps getQueryDna()

    DnaFingerprint topDna;
    juce::String topName;
    const auto matches = analyzer.getRankedMatches();

    // TOP MATCH readout always follows the live #1, whatever is inspected.
    if (analyzer.getTopMatchDna (topDna, topName) && ! matches.empty())
    {
        matchReadout.setText ("TOP MATCH   " + topName + "   ·   "
                              + juce::String ((int) std::round (matches.front().scorePct)) + "%",
                              juce::dontSendNotification);
    }
    else
    {
        matchReadout.setText ("TOP MATCH   —", juce::dontSendNotification);
    }

    // SAMPLE DNA screen: track the live leader, or the clicked row.
    const int selectedRow = matchList.getSelectedRow();
    DnaFingerprint sampleDna;
    juce::String sampleName;
    if ((followTopButton.getToggleState() || selectedRow < 0)
        && analyzer.getTopMatchDna (sampleDna, sampleName))
    {
        sampleScreen.setDna (sampleDna);
    }
    else if (selectedRow >= 0 && analyzer.getRankedEntryDna (selectedRow, sampleDna, sampleName))
    {
        sampleScreen.setDna (sampleDna);
    }
    else
    {
        sampleScreen.setDna (DnaFingerprint());
    }

    //--- Sidechain availability: GarageBand exposes no sidechain bus ------------
    const bool hasSidechain = processorRef.getChannelCountOfBus (true, 1) > 0;
    sourceBox.setItemEnabled (2, hasSidechain);
    if (! hasSidechain && sourceBox.getSelectedId() == 2)
        sourceBox.setSelectedId (1, juce::sendNotification); // fall back to Track

    listModel.setEntries (matches);
    matchList.updateContent();
    matchList.repaint();

    //--- Debug readout ------------------------------------------------------------
    const bool showDbg = dbgButton.getToggleState();
    dbgView.setVisible (showDbg);
    matchList.setVisible (! showDbg);
    listLabel.setVisible (! showDbg);
    if (showDbg)
        dbgView.setText (analyzer.getDnaDebugString(), false);
}

//==============================================================================
void ApolloAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14161c));

    // Section dividers, Malström-style panels (match resized() geometry).
    g.setColour (juce::Colour (0xff262b36));
    g.drawRect (16, 76, 320, 208, 1);   // DNA screens panel
    g.drawRect (344, 76, 280, 208, 1);  // knobs panel

    // CAPTURE cluster frame: one visual group for FREEZE + SCAN + bar-count
    // (Ariadne R2 — the cluster earns its one label, "CAPTURE").
    g.setColour (juce::Colour (0xff2e3340));
    g.drawRoundedRectangle (16.5f, 236.5f, 319.0f, 47.0f, 6.0f, 1.0f);
}

void ApolloAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    titleLabel.setBounds (area.removeFromTop (34));
    statusLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (6); // y = 76

    //--- DNA + knobs row (208 tall: 76..284) --------------------------------------
    auto topRow = area.removeFromTop (208);

    auto dnaPanel = topRow.removeFromLeft (320);
    const int panelX = dnaPanel.getX();
    auto trackCol  = dnaPanel.removeFromLeft (160);
    auto sampleCol = dnaPanel; // remaining 160

    trackDnaLabel.setBounds (trackCol.removeFromTop (20));
    trackCol.removeFromTop (4);
    trackScreen.setBounds (trackCol.removeFromTop (140));

    sampleDnaLabel.setBounds (sampleCol.removeFromTop (20));
    sampleCol.removeFromTop (4);
    sampleScreen.setBounds (sampleCol.removeFromTop (140));

    //--- CAPTURE cluster: one visual group under the DNA screens -------------------
    // Screens end at y=240; the panel runs to y=284 (44 px for the cluster).
    captureLabel.setBounds (panelX, 240, 320, 12);
    freezeButton.setBounds (panelX,      252, 92,  28);
    scanButton.setBounds   (panelX + 100, 252, 128, 28);
    scanBarsBox.setBounds  (panelX + 236, 252, 68,  28);

    topRow.removeFromLeft (8); // gap between panels
    auto knobPanel = topRow;   // ~292 wide

    bandLabel.setBounds (knobPanel.removeFromTop (20));
    knobPanel.removeFromTop (4);
    auto knobRow = knobPanel.removeFromTop (140);
    const int kw = knobRow.getWidth() / 3;
    auto placeKnob = [&] (juce::Slider& k, juce::Label& l, int x)
    {
        l.setBounds (x, knobRow.getY(), kw, 20);
        k.setBounds (x, knobRow.getY() + 20, kw, knobRow.getHeight() - 20);
    };
    placeKnob (lowKnob,  lowLabel,  knobRow.getX());
    placeKnob (midKnob,  midLabel,  knobRow.getX() + kw);
    placeKnob (highKnob, highLabel, knobRow.getX() + 2 * kw);

    area.removeFromTop (6);

    //--- Match readout + list -------------------------------------------------------
    matchReadout.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    auto listHeader = area.removeFromTop (20);
    followTopButton.setBounds (listHeader.removeFromRight (130));
    listHeader.removeFromRight (8);
    dbgButton.setBounds (listHeader.removeFromRight (46)); // demoted: small QA button
    listHeader.removeFromRight (8);
    listLabel.setBounds (listHeader);
    area.removeFromTop (4);

    auto listArea = area;
    listArea.removeFromBottom (38); // reserve bottom row
    matchList.setBounds (listArea);
    dbgView.setBounds (listArea); // debug readout swaps in over the list

    //--- Bottom row: source selector + bypass -----------------------------------------
    // (DNA DBG is demoted to the list header — a QA tool, not a performance control.)
    auto bottomRow = area.removeFromBottom (30);
    sourceLabel.setBounds (bottomRow.removeFromLeft (90));
    bottomRow.removeFromLeft (8);
    sourceBox.setBounds (bottomRow.removeFromLeft (150));
    bottomRow.removeFromRight (8);
    bypassButton.setBounds (bottomRow.removeFromRight (100));
}
