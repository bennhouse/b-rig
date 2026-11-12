#pragma once
// Interfaccia b-space (accento lavanda):
//   b-space                                                  space [studio|garage|club|hall]
//   ┌──────────────────────────────────────────────────────────────────────────────────┐
//     walls [treated|wood|brick|concrete]  source [cab 4x12|combo|omni]  dist comp [■]     stereo
//     width < 5.5 m >  depth < 7.0 m >  ceiling < 3.0 m >  angle < 0 deg >  air < 50 % >
//     pianta della stanza: l'ampli (trascinabile, rotella = lo ruota) col suo cono, l'ascoltatore
//     (trascinabile) e i percorsi del suono diretto e dei 4 rimbalzi sulle pareti
//   └──────────────────────────────────────────────────────────────────────────────────┘
#include "PluginProcessor.h"
#include "smp/Shell.h"

class RoomPlan final : public juce::Component
{
public:
    explicit RoomPlan (BSpaceProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = 0; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;   // sull'ampli o sull'ascoltatore: torna al suo posto di partenza
private:
    juce::Rectangle<float> roomRect() const;
    juce::Point<float> toScreen (juce::Point<float> metres) const;
    int itemAt (juce::Point<float>) const;   // 1 = ampli, 2 = ascoltatore
    void setParam (const char* id, float v);
    BSpaceProcessor& proc;
    int hover = 0, dragging = 0;
    float wheelAcc = 0.0f;
};

class BSpaceEditor final : public ui::ShellEditor
{
public:
    explicit BSpaceEditor (BSpaceProcessor&);
private:
    RoomPlan plan;
};
