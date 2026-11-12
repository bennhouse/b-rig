#pragma once
// Interfaccia b-aftercab: minimal, geometrica, solo linee continue da 1 px, font Inter.
//
//   b-aftercab            delverb [ ] [room|plate|hall]  del < 61 / 39 > rev  mix < 20 % >  [sync]
//                      comp [ ] [rhythm|lead|clean]  thr < -12,0 db >  gr 0,0 db
//   ┌──────────────────────────────────────────────────────────────────────────┐
//     eq [ ]  precision [ ]  phase [zero|linear]  quality [low|mid|high]   mono > stereo
//     griglia EQ (20 Hz - 20 kHz, ±10 / ±4 dB)
//   │                          ┌───────────────────────────────────────────────┘
//   │ band 3 [on] freq < > q < >│ stato           spread [ ] < 15,42 ms > [=]
//   └──────────────────────────┘
//
// Catena audio: EQ -> comp -> spread -> delverb. Lo spread compare solo con ingresso mono e uscita stereo.

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UI/AftercabWidgets.h"
#include "UI/EqDisplay.h"

class BAftercabEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BAftercabEditor (BAftercabProcessor&);
    ~BAftercabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateForMode();
    juce::String statusText() const;
    juce::Rectangle<int> eqBox() const;
    juce::Rectangle<int> navbar() const;
    juce::Rectangle<int> bandTab() const;
    int framePad() const;
    juce::Rectangle<int> headerRow (int index) const;
    int idealHeight() const;
    void bindBand (int band);   // collega i controlli della banda selezionata
    int boundBand = -1;

    BAftercabProcessor& proc;
    BAftercabProcessor::Mode shownMode;

    ui::PixelCheckbox eqCheck, precisionCheck, spreadCheck;
    ui::SegmentedChoice phaseChoice, qualityChoice;
    ui::EqDisplay eqDisplay;
    ui::PixelCheckbox bandOnCheck, revCheck;
    ui::SegmentedChoice revTypeChoice;
    ui::ValueBox freqBox, gainBox, qBox, delayBox, revMixBox, revBalanceBox;
    ui::SmartPhaseButton smartButton;
    ui::TempoBox tempoBox;
    ui::TapButton tapButton;
    ui::PixelCheckbox compCheck;
    ui::OutputSection outSection;
    ui::SegmentedChoice compModeChoice;
    ui::ValueBox compThrBox;
    float compMeterDb = 0.0f;                          // gain reduction mostrata (con caduta lenta)
    static constexpr float kMeterRangeDb = 12.0f;      // quadrato pieno = 12 dB di riduzione (scala logaritmica)
    void updateSpreadEnablement();
    juce::Rectangle<int> modeArea;
    void updateDelverbEnablement();

    struct TextItem { juce::String text; juce::Rectangle<int> area; bool navbar; };
    std::vector<TextItem> labels;
    juce::Rectangle<int> statusArea;
    juce::Rectangle<int> navRow, tabRect;
    juce::int64 lastSignature = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BAftercabEditor)
    ui::AboutOverlay about { *this, "aftercab" };   // clic sul logo: pannello about (ultimo membro: sta sopra a tutto)
};
