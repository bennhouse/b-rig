#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kC = 343.0f;   // velocità del suono, m/s
    constexpr const char* kIds[13] = { BSpaceIDs::room, BSpaceIDs::mix, BSpaceIDs::source, BSpaceIDs::distComp, BSpaceIDs::width, BSpaceIDs::depth,
                                       BSpaceIDs::ceiling, BSpaceIDs::angle, BSpaceIDs::air, BSpaceIDs::srcX, BSpaceIDs::srcY, BSpaceIDs::lisX, BSpaceIDs::lisY };
    constexpr float kLineRatio[4] = { 0.713f, 0.907f, 1.093f, 1.289f };   // rapporti tra le 4 linee (lunghezze prime tra loro)
}

BSpaceProcessor::BSpaceProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BSpace", createLayout())
{
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    for (auto& l : lines) l.assign (kLine, 0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout BSpaceProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BSpaceIDs::room, 1 }, "Room", StringArray { "Studio", "Live room", "Garage", "Club", "Hall", "Church" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BSpaceIDs::mix, 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BSpaceIDs::source, 1 }, "Source", StringArray { "Cab 4x12", "Combo", "Omni" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BSpaceIDs::distComp, 1 }, "Distance compensation", true));
    auto metres = [] (float lo, float hi, float def, const char* name, const char* id)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.1f, 0.6f), def,
                                                      AudioParameterFloatAttributes().withLabel ("m"));
    };
    l.add (metres (2.5f, 30.0f, 5.5f, "Width", BSpaceIDs::width));
    l.add (metres (2.5f, 40.0f, 7.0f, "Depth", BSpaceIDs::depth));
    l.add (metres (2.2f, 12.0f, 3.0f, "Ceiling", BSpaceIDs::ceiling));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BSpaceIDs::angle, 1 }, "Angle", NormalisableRange<float> (-180.0f, 180.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("deg")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BSpaceIDs::air, 1 }, "Air", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    auto pos = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.03f, 0.97f, 0.001f), def);
    };
    l.add (pos (BSpaceIDs::srcX, "Amp X", 0.5f));
    l.add (pos (BSpaceIDs::srcY, "Amp Y", 0.25f));
    l.add (pos (BSpaceIDs::lisX, "Listener X", 0.5f));
    l.add (pos (BSpaceIDs::lisY, "Listener Y", 0.7f));
    return l;
}

bool BSpaceProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && out == juce::AudioChannelSet::stereo();
}

void BSpaceProcessor::applyRoom (int s)
{
    const auto& r = kRooms[juce::jlimit (0, kNumRooms - 1, s)];
    for (auto [id, v] : { std::pair<const char*, float> { BSpaceIDs::width, r.w }, { BSpaceIDs::depth, r.d }, { BSpaceIDs::ceiling, r.h } })
    {
        auto* p = apvts.getParameter (id);
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture();
    }
}

BSpaceProcessor::Params BSpaceProcessor::currentParams() const
{
    Params p;
    p.w = raw[4]->load(); p.d = raw[5]->load(); p.h = raw[6]->load(); p.angleDeg = raw[7]->load(); p.air = raw[8]->load() / 100.0f;
    p.sx = raw[9]->load(); p.sy = raw[10]->load(); p.lx = raw[11]->load(); p.ly = raw[12]->load();
    p.room = juce::jlimit (0, kNumRooms - 1, (int) raw[0]->load()); p.source = juce::jlimit (0, 2, (int) raw[2]->load());
    p.distComp = raw[3]->load() > 0.5f;
    return p;
}

BSpaceProcessor::Geometry BSpaceProcessor::geometry (const Params& p)
{
    using P = juce::Point<float>;
    Geometry g;
    const P S { p.sx * p.w, p.sy * p.d }, L { p.lx * p.w, p.ly * p.d };
    // sorgenti speculari e immagini dell'ascoltatore per le 4 pareti: sinistra (x=0), destra (x=W), fronte (y=0), retro (y=D)
    const P imgS[5] = { S, { -S.x, S.y }, { 2.0f * p.w - S.x, S.y }, { S.x, -S.y }, { S.x, 2.0f * p.d - S.y } };
    const P imgL[5] = { L, { -L.x, L.y }, { 2.0f * p.w - L.x, L.y }, { L.x, -L.y }, { L.x, 2.0f * p.d - L.y } };
    auto toward = L - S;
    if (toward.getDistanceFromOrigin() < 1.0e-3f) toward = { 0.0f, 1.0f };
    toward /= toward.getDistanceFromOrigin();
    const float ang = juce::degreesToRadians (p.angleDeg);
    const P facing { toward.x * std::cos (ang) - toward.y * std::sin (ang), toward.x * std::sin (ang) + toward.y * std::cos (ang) };
    constexpr float kTop[3] = { 12000.0f, 10000.0f, 16000.0f }, kDir[3] = { 1.0f, 0.7f, 0.0f };
    const float dz = kEarHeight - kSourceHeight, dzCeil = 2.0f * p.h - kEarHeight - kSourceHeight, dzFloor = kEarHeight + kSourceHeight;
    const auto& room = kRooms[juce::jlimit (0, kNumRooms - 1, p.room)];

    float d0 = 1.0f;
    for (int k = 0; k < kTaps; ++k)
    {
        auto& path = g.paths[(size_t) k];
        const int img = k >= 5 ? 0 : k;   // soffitto e pavimento: stessa posizione in pianta, cambia l'altezza
        const P v = imgS[img] - L;
        const float horiz = v.getDistanceFromOrigin();
        const float vert = k == 5 ? dzCeil : k == 6 ? dzFloor : dz;
        path.dist = juce::jmax (0.3f, std::sqrt (horiz * horiz + vert * vert));
        if (k == 0) d0 = path.dist;
        path.azimuth = horiz > 1.0e-3f ? std::atan2 (v.x, -v.y) : 0.0f;
        // da dove parte il suono dall'ampli: verso l'immagine dell'ascoltatore oltre quella parete
        auto dep = imgL[img] - S;
        const float dl = dep.getDistanceFromOrigin();
        dep = dl > 1.0e-3f ? dep / dl : toward;
        path.offAxisDeg = juce::radiansToDegrees (std::acos (juce::jlimit (-1.0f, 1.0f, dep.x * facing.x + dep.y * facing.y)));
        const float t = std::pow (path.offAxisDeg / 180.0f, 0.8f) * kDir[p.source];
        const float fcDir = kTop[p.source] * std::pow (1800.0f / 12000.0f, t);
        const float fcAir = 20000.0f / (1.0f + p.air * path.dist / 8.0f);
        // il materiale: ogni rimbalzo perde le alte sopra la frequenza della superficie che lo rimanda
        const float fcMat = k == 0 ? 20000.0f : k == 6 ? room.floorHz : room.wallHz;
        path.cutoffHz = juce::jmin (fcDir, fcAir, fcMat);
        // punto di rimbalzo: dove la linea immagine -> ascoltatore attraversa la parete
        path.bounce = S;
        if (k >= 1 && k <= 4)
        {
            const P a = imgS[k];
            float tt = 0.5f;
            if (k == 1) tt = (0.0f - a.x) / (L.x - a.x);
            if (k == 2) tt = (p.w - a.x) / (L.x - a.x);
            if (k == 3) tt = (0.0f - a.y) / (L.y - a.y);
            if (k == 4) tt = (p.d - a.y) / (L.y - a.y);
            path.bounce = a + (L - a) * juce::jlimit (0.0f, 1.0f, tt);
        }
    }
    for (int k = 0; k < kTaps; ++k)
    {
        auto& path = g.paths[(size_t) k];
        path.gain = k == 0 ? 1.0f : d0 / juce::jmax (0.5f, path.dist) * (k == 6 ? room.floorReflect : room.reflect);
        path.delayS = (p.distComp ? path.dist - d0 : path.dist) / kC;
    }
    // coda: Sabine e distanza critica
    const float V = p.w * p.d * p.h, Sa = 2.0f * (p.w * p.d + p.w * p.h + p.d * p.h);
    g.mfp = 4.0f * V / Sa;
    g.rt60 = juce::jlimit (0.12f, 4.0f, 0.161f * V / (Sa * room.absorb) * (1.0f - 0.25f * p.air));
    const float rc = 0.057f * std::sqrt (V / g.rt60);
    g.tailGain = juce::jlimit (0.03f, 1.2f, 0.7f * d0 / rc);
    g.wallDampHz = room.wallHz * (1.0f - 0.4f * p.air);
    // ampli vicino a una parete: sotto i 160 Hz la parete rimanda l'onda quasi in fase e i bassi crescono
    // (fino a circa +6 dB contro una parete dura, di più in un angolo); oltre un metro l'effetto sparisce
    float boost = 0.0f;
    for (float dw : { S.x, p.w - S.x, S.y, p.d - S.y }) boost += std::exp (-juce::jmax (0.0f, dw) / 0.6f);
    g.bassBoost = juce::jlimit (0.0f, 1.5f, 0.75f * room.reflect * boost);
    return g;
}

void BSpaceProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    std::fill (buf.begin(), buf.end(), 0.0f);
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    w = lw = 0;
    for (auto& t : ears) for (auto& e : t) e.z = 0.0f;
    damp.fill (0.0f);
    smooth = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
    bassA = std::exp (-juce::MathConstants<float>::twoPi * kBassHz / (float) fs); bassZ = 0.0f; tailZ = 0.0f;
    first = true; lastSig = 0; ctl = 0;
}

// solo quando cambia un parametro (controllato ogni 64 campioni): nuovi valori da inseguire
void BSpaceProcessor::recompute() noexcept
{
    const auto g = geometry (currentParams());
    const float twoPiOverFs = juce::MathConstants<float>::twoPi / (float) fs;
    float energy = 0.0f;
    for (int k = 0; k < kTaps; ++k)
    {
        const auto& path = g.paths[(size_t) k];
        const float s = std::sin (path.azimuth), as = std::abs (s);
        const float itd = 0.0006f * s * (float) fs;   // + = la sorgente è a destra: l'orecchio sinistro la sente dopo
        for (int e = 0; e < 2; ++e)
        {
            const bool far = (e == 0 && s > 0.0f) || (e == 1 && s < 0.0f);
            auto& ear = ears[(size_t) k][(size_t) e];
            ear.tDelay = juce::jlimit (0.0f, (float) (kBuf - 8), path.delayS * (float) fs + (far ? std::abs (itd) : 0.0f));
            ear.tGain = path.gain * (far ? 1.0f - 0.3f * as : 1.0f);
            const float fc = far ? path.cutoffHz * (1.0f - as) + juce::jmin (path.cutoffHz, 1500.0f) * as : path.cutoffHz;
            ear.tLpA = std::exp (-twoPiOverFs * juce::jmin (fc, 0.45f * (float) fs));
        }
        if (k > 0) energy += path.gain * path.gain;
    }
    const float mfpSamples = g.mfp / kC * (float) fs;
    float gAvg = 0.0f;
    for (size_t i = 0; i < 4; ++i)
    {
        tLineLen[i] = juce::jlimit (64.0f, (float) (kLine - 8), mfpSamples * kLineRatio[i] * 1.6f);
        tLineG[i] = std::pow (10.0f, -3.0f * tLineLen[i] / (g.rt60 * (float) fs));
        gAvg += tLineG[i] * 0.25f;
    }
    tDampA = std::exp (-twoPiOverFs * g.wallDampHz);
    tSend = g.tailGain * std::sqrt (juce::jmax (0.01f, 1.0f - gAvg * gAvg));
    tNorm = 1.0f / std::sqrt (1.0f + 0.5f * (energy + g.tailGain * g.tailGain));
    tBass = g.bassBoost;
    if (first)
    {
        for (auto& t : ears) for (auto& e : t) { e.delay = e.tDelay; e.gain = e.tGain; e.lpA = e.tLpA; }
        lineLen = tLineLen; lineG = tLineG; dampA = tDampA; send = tSend; norm = tNorm; bass = tBass;
        mixG = raw[1]->load() / 100.0f;
        first = false;
    }
}

void BSpaceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    const int inCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    if (buffer.getNumChannels() < 2) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const float a = smooth;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        if (ctl-- <= 0)
        {
            ctl = 63;
            juce::int64 sig = 17;
            for (auto* r : raw) sig = sig * 31 + (juce::int64) (r->load() * 1000.0f);
            if (sig != lastSig || first) { lastSig = sig; recompute(); }
        }
        const float dry = inCh > 1 ? 0.5f * (L[i] + R[i]) : L[i];
        // rinforzo dei bassi delle pareti vicine: sulla sorgente, quindi in tutti i percorsi
        bass += a * (tBass - bass);
        bassZ = dry + bassA * (bassZ - dry);
        const float x = dry + bass * bassZ;
        buf[(size_t) w] = x;

        float out[2] = { 0.0f, 0.0f };
        for (auto& tap : ears)
            for (int e = 0; e < 2; ++e)
            {
                auto& ear = tap[(size_t) e];
                ear.delay += a * (ear.tDelay - ear.delay);
                ear.gain += a * (ear.tGain - ear.gain);
                ear.lpA += a * (ear.tLpA - ear.lpA);
                const float pos = (float) w - ear.delay + (float) kBuf;
                const int i0 = (int) pos; const float f = pos - (float) i0;
                const float s0 = buf[(size_t) (i0 & (kBuf - 1))], s1 = buf[(size_t) ((i0 + 1) & (kBuf - 1))];
                const float r = s0 + f * (s1 - s0);
                ear.z = r + ear.lpA * (ear.z - r);
                out[e] += ear.gain * ear.z;
            }

        // coda: 4 linee, smorzamento delle pareti, Hadamard (somme e differenze)
        dampA += a * (tDampA - dampA); send += a * (tSend - send); norm += a * (tNorm - norm);
        float rd[4];
        for (size_t k = 0; k < 4; ++k)
        {
            lineLen[k] += a * (tLineLen[k] - lineLen[k]);
            lineG[k] += a * (tLineG[k] - lineG[k]);
            const float pos = (float) lw - lineLen[k] + (float) kLine;
            const int i0 = (int) pos; const float f = pos - (float) i0;
            const auto& ln = lines[k];
            const float s0 = ln[(size_t) (i0 & (kLine - 1))], s1 = ln[(size_t) ((i0 + 1) & (kLine - 1))];
            rd[k] = s0 + f * (s1 - s0);
            damp[k] = rd[k] + dampA * (damp[k] - rd[k]);
        }
        // la coda prende il suono già filtrato dai materiali (in una stanza trattata la coda è scura, nel cemento brillante)
        const float uIn = send * 0.5f * (out[0] + out[1]);
        tailZ = uIn + dampA * (tailZ - uIn);
        const float u = tailZ;
        const float d0 = damp[0] * lineG[0], d1 = damp[1] * lineG[1], d2 = damp[2] * lineG[2], d3 = damp[3] * lineG[3];
        lines[0][(size_t) lw] = 0.5f * (d0 + d1 + d2 + d3) + u;
        lines[1][(size_t) lw] = 0.5f * (d0 - d1 + d2 - d3) + u;
        lines[2][(size_t) lw] = 0.5f * (d0 + d1 - d2 - d3) - u;
        lines[3][(size_t) lw] = 0.5f * (d0 - d1 - d2 + d3) - u;
        lw = (lw + 1) & (kLine - 1);
        w = (w + 1) & (kBuf - 1);

        // mix: 0 = il segnale che entra, 100 = solo la stanza (diretto compreso)
        mixG += a * (raw[1]->load (std::memory_order_relaxed) * 0.01f - mixG);
        L[i] = dry + mixG * (norm * (out[0] + 0.7f * (damp[0] + damp[2])) - dry);
        R[i] = dry + mixG * (norm * (out[1] + 0.7f * (damp[1] + damp[3])) - dry);
        peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
    }
    for (auto& t : ears) for (auto& e : t) if (std::abs (e.z) < 1.0e-20f) e.z = 0.0f;
    outLevel.store (juce::jmax (peak, 0.9f * outLevel.load (std::memory_order_relaxed)), std::memory_order_relaxed);
}

void BSpaceProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void BSpaceProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto t = juce::ValueTree::fromXml (*xml);
    // progetti di prima: "space" (studio, garage, club, hall) diventa la stanza; i materiali ora seguono la stanza
    const auto oldSpace = t.getChildWithProperty ("id", "space");
    if (oldSpace.isValid() && ! t.getChildWithProperty ("id", BSpaceIDs::room).isValid())
    {
        constexpr int kMap[4] = { 0, 2, 3, 4 };
        juce::ValueTree r ("PARAM");
        r.setProperty ("id", BSpaceIDs::room, nullptr);
        r.setProperty ("value", kMap[juce::jlimit (0, 3, (int) (float) oldSpace["value"])], nullptr);
        t.appendChild (r, nullptr);
    }
    for (const char* gone : { "space", "walls" })
        if (auto c = t.getChildWithProperty ("id", gone); c.isValid()) t.removeChild (c, nullptr);
    apvts.replaceState (t);
}

juce::AudioProcessorEditor* BSpaceProcessor::createEditor() { return new BSpaceEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BSpaceProcessor(); }
