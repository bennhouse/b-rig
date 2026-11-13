#include "PluginEditor.h"

namespace
{
    float paramValue (BSpaceProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
}

juce::Rectangle<float> RoomPlan::roomRect() const
{
    // la stanza in scala, proporzioni vere, centrata (fronte = in alto)
    const auto area = getLocalBounds().toFloat().reduced (40.0f, 34.0f);
    const float W = paramValue (proc, BSpaceIDs::width), D = paramValue (proc, BSpaceIDs::depth);
    const float scale = juce::jmin (area.getWidth() / W, area.getHeight() / D);
    return juce::Rectangle<float> (W * scale, D * scale).withCentre (area.getCentre());
}

juce::Point<float> RoomPlan::toScreen (juce::Point<float> m) const
{
    const auto r = roomRect();
    const float W = paramValue (proc, BSpaceIDs::width), D = paramValue (proc, BSpaceIDs::depth);
    return { r.getX() + m.x / W * r.getWidth(), r.getY() + m.y / D * r.getHeight() };
}

int RoomPlan::itemAt (juce::Point<float> p) const
{
    const auto r = roomRect();
    const juce::Point<float> amp { r.getX() + paramValue (proc, BSpaceIDs::srcX) * r.getWidth(), r.getY() + paramValue (proc, BSpaceIDs::srcY) * r.getHeight() };
    const juce::Point<float> lis { r.getX() + paramValue (proc, BSpaceIDs::lisX) * r.getWidth(), r.getY() + paramValue (proc, BSpaceIDs::lisY) * r.getHeight() };
    if (p.getDistanceFrom (amp) < 16.0f) return 1;
    if (p.getDistanceFrom (lis) < 16.0f) return 2;
    return 0;
}

void RoomPlan::setParam (const char* id, float v)
{
    auto* a = proc.apvts.getParameter (id);
    a->setValueNotifyingHost (a->convertTo0to1 (v));
}

void RoomPlan::mouseMove (const juce::MouseEvent& e)
{
    const int h = itemAt (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h != 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void RoomPlan::mouseDown (const juce::MouseEvent& e)
{
    dragging = itemAt (e.position);
    if (dragging == 0) return;
    for (auto* id : dragging == 1 ? std::array<const char*, 2> { BSpaceIDs::srcX, BSpaceIDs::srcY } : std::array<const char*, 2> { BSpaceIDs::lisX, BSpaceIDs::lisY })
        proc.apvts.getParameter (id)->beginChangeGesture();
}

void RoomPlan::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == 0) return;
    const auto r = roomRect();
    const float x = (e.position.x - r.getX()) / r.getWidth(), y = (e.position.y - r.getY()) / r.getHeight();
    setParam (dragging == 1 ? BSpaceIDs::srcX : BSpaceIDs::lisX, juce::jlimit (0.03f, 0.97f, x));
    setParam (dragging == 1 ? BSpaceIDs::srcY : BSpaceIDs::lisY, juce::jlimit (0.03f, 0.97f, y));
    repaint();
}

void RoomPlan::mouseUp (const juce::MouseEvent&)
{
    if (dragging == 0) return;
    for (auto* id : dragging == 1 ? std::array<const char*, 2> { BSpaceIDs::srcX, BSpaceIDs::srcY } : std::array<const char*, 2> { BSpaceIDs::lisX, BSpaceIDs::lisY })
        proc.apvts.getParameter (id)->endChangeGesture();
    dragging = 0;
}

void RoomPlan::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (itemAt (e.position) != 1) return;
    wheelAcc += w.deltaY;
    if (std::abs (wheelAcc) < 0.05f) return;
    float a = paramValue (proc, BSpaceIDs::angle) + (wheelAcc > 0 ? 5.0f : -5.0f);
    if (a > 180.0f) a -= 360.0f;
    if (a < -180.0f) a += 360.0f;
    auto* p = proc.apvts.getParameter (BSpaceIDs::angle);
    p->beginChangeGesture(); setParam (BSpaceIDs::angle, a); p->endChangeGesture();
    wheelAcc = 0.0f;
    repaint();
}

void RoomPlan::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int item = itemAt (e.position);
    if (item == 0) return;
    // l'ampli torna al suo posto e dritto verso l'ascoltatore; l'ascoltatore al suo posto
    std::vector<const char*> ids;
    if (item == 1) ids = { BSpaceIDs::srcX, BSpaceIDs::srcY, BSpaceIDs::angle };
    else ids = { BSpaceIDs::lisX, BSpaceIDs::lisY };
    for (auto* id : ids)
    {
        auto* p = proc.apvts.getParameter (id);
        p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture();
    }
    repaint();
}

void RoomPlan::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto all = getLocalBounds().toFloat();
    g.setColour (gridLine);
    outline (g, all);
    const auto prm = proc.currentParams();
    const auto geo = BSpaceProcessor::geometry (prm);
    const auto r = roomRect();

    // stanza: griglia a 1 m (o 5 m nelle stanze grandi), pareti
    const float step = prm.w > 14.0f || prm.d > 18.0f ? 5.0f : 1.0f;
    g.setColour (gridLine);
    for (float m = step; m < prm.w; m += step) g.fillRect (std::round (toScreen ({ m, 0.0f }).x), r.getY(), kLine, r.getHeight());
    for (float m = step; m < prm.d; m += step) g.fillRect (r.getX(), std::round (toScreen ({ 0.0f, m }).y), r.getWidth(), kLine);
    g.setColour (text);
    outline (g, r);

    const juce::Point<float> S { prm.sx * prm.w, prm.sy * prm.d }, L { prm.lx * prm.w, prm.ly * prm.d };
    const auto s = toScreen (S), l = toScreen (L);

    // cono dell'ampli (larghezza secondo la sorgente) orientato con "angle"
    auto toward = l - s;
    const float tl = toward.getDistanceFromOrigin();
    toward = tl > 1.0f ? toward / tl : juce::Point<float> { 0.0f, 1.0f };
    const float ang = juce::degreesToRadians (prm.angleDeg);
    const juce::Point<float> facing { toward.x * std::cos (ang) - toward.y * std::sin (ang), toward.x * std::sin (ang) + toward.y * std::cos (ang) };
    const float heading = std::atan2 (facing.y, facing.x);
    constexpr float kSpread[3] = { 0.55f, 0.9f, juce::MathConstants<float>::pi };
    const float spread = kSpread[prm.source];
    const float coneLen = juce::jmin (r.getWidth(), r.getHeight()) * 0.45f;
    {
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        juce::Path cone;
        if (prm.source == 2) cone.addEllipse (s.x - coneLen * 0.5f, s.y - coneLen * 0.5f, coneLen, coneLen);
        else
        {
            cone.startNewSubPath (s);
            for (int i = 0; i <= 24; ++i)
            {
                const float t = heading - spread + 2.0f * spread * (float) i / 24.0f;
                cone.lineTo (s.x + coneLen * std::cos (t), s.y + coneLen * std::sin (t));
            }
            cone.closeSubPath();
        }
        g.setColour (accent.withAlpha (0.08f));
        g.fillPath (cone);
        g.restoreState();
    }

    // percorsi: diretto pieno, rimbalzi sulle pareti più tenui (intensità = guadagno)
    for (int k = 1; k <= 4; ++k)
    {
        const auto& p = geo.paths[(size_t) k];
        const auto b = toScreen (p.bounce);
        juce::Path path; path.startNewSubPath (s); path.lineTo (b); path.lineTo (l);
        g.setColour (accent.withAlpha (0.18f + 0.5f * juce::jlimit (0.0f, 1.0f, p.gain)));
        g.strokePath (path, juce::PathStrokeType (1.0f));
        g.fillEllipse (b.x - 2.5f, b.y - 2.5f, 5.0f, 5.0f);
    }
    g.setColour (accent);
    g.drawLine ({ s, l }, 1.5f);

    // ampli: rettangolo orientato
    {
        juce::Path amp;
        amp.addRectangle (-9.0f, -6.0f, 18.0f, 12.0f);
        amp.applyTransform (juce::AffineTransform::rotation (heading + juce::MathConstants<float>::halfPi).translated (s));
        g.setColour (background);
        g.fillPath (amp);
        g.setColour (hover == 1 || dragging == 1 ? labelHover : accent);
        g.strokePath (amp, juce::PathStrokeType (1.5f));
        g.fillEllipse (s.x + facing.x * 6.0f - 2.0f, s.y + facing.y * 6.0f - 2.0f, 4.0f, 4.0f);   // il fronte
    }
    // ascoltatore: testa con orecchie (guarda verso il fronte della stanza)
    {
        g.setColour (background);
        g.fillEllipse (l.x - 7.0f, l.y - 7.0f, 14.0f, 14.0f);
        g.setColour (hover == 2 || dragging == 2 ? labelHover : valueText);
        g.drawEllipse (l.x - 7.0f, l.y - 7.0f, 14.0f, 14.0f, 1.5f);
        g.fillRect (l.x - 10.0f, l.y - 2.0f, 3.0f, 4.0f);
        g.fillRect (l.x + 7.0f, l.y - 2.0f, 3.0f, 4.0f);
        g.fillRect (l.x - 1.0f, l.y - 10.0f, 2.0f, 3.0f);
    }

    const float tagH = capHeight (kTagSize, Face::SansSemiBold);
    drawTag (g, "front", r.getCentreX(), r.getY() - 8.0f, juce::Justification::horizontallyCentred);
    // i materiali della stanza scelta
    drawAtBaseline (g, BSpaceProcessor::kRooms[prm.room].material, all.getRight() - 10.0f, all.getY() + 8.0f + tagH, info, kInfoSize,
                    juce::Justification::right, Face::SansRegular);
    drawAtBaseline (g, "drag the amp and the listener, scroll on the amp to turn it, double-click to reset", all.getX() + 10.0f, all.getY() + 8.0f + tagH, info, kInfoSize,
                    juce::Justification::left, Face::SansRegular);
    const float base = all.getBottom() - 10.0f;
    drawAtBaseline (g, "direct " + num (geo.paths[0].dist, 1) + " m", all.getX() + 10.0f, base, info, kAxisSize);
    float firstRefl = 1.0e9f;
    for (int k = 1; k < BSpaceProcessor::kTaps; ++k) firstRefl = juce::jmin (firstRefl, geo.paths[(size_t) k].delayS);
    if (! prm.distComp) firstRefl -= geo.paths[0].delayS;
    drawAtBaseline (g, "first reflection +" + num (1000.0 * firstRefl, 1) + " ms", all.getCentreX(), base, info, kAxisSize, juce::Justification::horizontallyCentred);
    drawAtBaseline (g, "tail " + num (geo.rt60, 2) + " s", all.getRight() - 10.0f, base, info, kAxisSize, juce::Justification::right);
}

BSpaceEditor::BSpaceEditor (BSpaceProcessor& p)
    : ShellEditor (p, p.apvts, "space", 960, 600), plan (p)
{
    using namespace ui;
    // la stanza: misure e materiali insieme
    setHeaderChoice ("room", BSpaceIDs::room, { "studio", "live", "garage", "club", "hall", "church" });
    headerChoice->onUserChange = [&p] (int seg) { p.applyRoom (seg); };
    addChoice ("source", BSpaceIDs::source, { "cab 4x12", "combo", "omni" });
    addCheck ("dist comp", BSpaceIDs::distComp);
    addValue ("mix", BSpaceIDs::mix, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "100 %" });
    newRow();
    auto metres = [] (float v) { return num (v, 1) + " m"; };
    auto metreStep = [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; };
    addValue ("width", BSpaceIDs::width, metres, metreStep, { "30.0 m" });
    addValue ("depth", BSpaceIDs::depth, metres, metreStep, { "40.0 m" });
    addValue ("ceiling", BSpaceIDs::ceiling, metres, [] (float v, int d) { return std::round (v * 10.0f + (float) d) / 10.0f; }, { "12.0 m" });
    addValue ("angle", BSpaceIDs::angle, [] (float v) { return juce::String (juce::roundToInt (v)) + " deg"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "-180 deg" });
    addValue ("air", BSpaceIDs::air, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "100 %" });
    infoText = "stereo out";
    setDisplay (plan);
    resized();
}
