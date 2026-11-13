#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    enum { P_STYLE, P_THR, P_RATIO, P_ATK, P_REL, P_AUTO, P_KNEE, P_SCHP, P_MIXON, P_MIX, P_LOW, P_MID, P_HIGH, P_MAKEUP, P_OUT };
    const char* kIds[15] = { BCompIDs::style, BCompIDs::threshold, BCompIDs::ratio, BCompIDs::attack, BCompIDs::release, BCompIDs::autoGain,
                             BCompIDs::knee, BCompIDs::scHp, BCompIDs::mixOn, BCompIDs::mix, BCompIDs::low, BCompIDs::mid, BCompIDs::high,
                             BCompIDs::makeup, BCompIDs::output };
    inline float coefFor (double ms, double fs) { return (float) std::exp (-1.0 / (juce::jmax (0.01, ms) * 0.001 * fs)); }
    inline float toDb (float g) { return 20.0f * std::log10 (g + 1.0e-9f); }
}

BCompProcessor::BCompProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BComp", createLayout())
{
    for (int i = 0; i < 15; ++i) p[i] = apvts.getRawParameterValue (kIds[i]);
    for (const char* id : { BCompIDs::threshold, BCompIDs::ratio, BCompIDs::attack, BCompIDs::release, BCompIDs::knee })
        styleMemory.add (apvts.getParameter (id), [id] (int st) { return styleDefault (id, st); });
}

// default di ogni stile (quelli dei parametri sono del clean, lo stile di partenza)
float BCompProcessor::styleDefault (const char* id, int st)
{
    struct D { const char* id; float v[kStyles]; };
    static const D table[] = {
        //                         clean   opto    fet     squash  multi
        { BCompIDs::threshold, { -18.0f, -20.0f, -16.0f, -28.0f, -20.0f } },
        { BCompIDs::ratio,     {   3.0f,   4.0f,   8.0f,  12.0f,   4.0f } },
        { BCompIDs::attack,    {  10.0f,  15.0f,   1.0f,   0.5f,   8.0f } },
        { BCompIDs::release,   { 120.0f, 300.0f,  80.0f, 200.0f, 150.0f } },
        { BCompIDs::knee,      {   6.0f,  10.0f,   2.0f,   4.0f,   6.0f } },
    };
    for (const auto& d : table) if (juce::String (d.id) == id) return d.v[juce::jlimit (0, kStyles - 1, st)];
    return 0.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout BCompProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel (unit))); };
    auto skewed = [] (float lo, float hi, float step, float centre) { NormalisableRange<float> r (lo, hi, step); r.setSkewForCentre (centre); return r; };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BCompIDs::style, 1 }, "Style", StringArray { "Clean", "Opto", "FET", "Squash", "Multi" }, 0));
    f (BCompIDs::threshold, "Threshold", { -60.0f, 0.0f, 0.1f }, -18.0f, "dB");
    f (BCompIDs::ratio, "Ratio", skewed (1.0f, 20.0f, 0.1f, 4.0f), 3.0f, ":1");
    f (BCompIDs::attack, "Attack", skewed (0.1f, 100.0f, 0.1f, 10.0f), 10.0f, "ms");
    f (BCompIDs::release, "Release", skewed (10.0f, 2000.0f, 1.0f, 200.0f), 120.0f, "ms");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BCompIDs::autoGain, 1 }, "Auto gain", true));
    f (BCompIDs::knee, "Knee", { 0.0f, 18.0f, 0.1f }, 6.0f, "dB");
    f (BCompIDs::scHp, "Sidechain HP", skewed (20.0f, 400.0f, 1.0f, 100.0f), 80.0f, "Hz");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BCompIDs::mixOn, 1 }, "Parallel", false));
    f (BCompIDs::mix, "Mix", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");
    f (BCompIDs::low, "Low", { 0.0f, 100.0f, 1.0f }, 100.0f, "%");
    f (BCompIDs::mid, "Mid", { 0.0f, 100.0f, 1.0f }, 70.0f, "%");
    f (BCompIDs::high, "High", { 0.0f, 100.0f, 1.0f }, 40.0f, "%");
    f (BCompIDs::makeup, "Makeup", { -12.0f, 24.0f, 0.1f }, 0.0f, "dB");
    f (BCompIDs::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, "dB");
    return l;
}

bool BCompProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

float BCompProcessor::gainDb (float x, float t, float r, float w) noexcept
{
    const float over = x - t;
    if (w > 0.0f && 2.0f * std::abs (over) <= w)
    {
        const float a = over + 0.5f * w;
        return (1.0f / r - 1.0f) * a * a / (2.0f * w);
    }
    return over > 0.0f ? over * (1.0f / r - 1.0f) : 0.0f;
}

float BCompProcessor::autoMakeupDb() const
{
    const int st = juce::jlimit (0, kStyles - 1, (int) p[P_STYLE]->load());
    const float t = p[P_THR]->load(), r = p[P_RATIO]->load(), k = p[P_KNEE]->load();
    constexpr float ref = -10.0f;   // una chitarra suonata normalmente, ai picchi
    if (st != multi) return juce::jlimit (0.0f, 24.0f, -gainDb (ref, t, r, k));
    // multi: la media delle tre bande, ognuna con la sua soglia e quanto lavora
    const float amt[3] = { p[P_LOW]->load() / 100.0f, p[P_MID]->load() / 100.0f, p[P_HIGH]->load() / 100.0f };
    float m = 0.0f;
    for (int b = 0; b < 3; ++b) m += -gainDb (ref, t + kBandThr[b], 1.0f + (r - 1.0f) * amt[b], k) / 3.0f;
    return juce::jlimit (0.0f, 24.0f, m);
}

void BCompProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto& f : scHpF) f.reset();
    lastSc = -1.0f;
    for (int c = 0; c < 2; ++c)
    {
        for (auto* s : { &lp1[(size_t) c].a, &lp1[(size_t) c].b }) { s->set (fs, 250.0, 0.70710678); s->reset(); }
        for (auto* s : { &hp1[(size_t) c].a, &hp1[(size_t) c].b }) { s->set (fs, 250.0, 0.70710678); s->reset(); }
        for (auto* s : { &lp2[(size_t) c].a, &lp2[(size_t) c].b }) { s->set (fs, 2500.0, 0.70710678); s->reset(); }
        for (auto* s : { &hp2[(size_t) c].a, &hp2[(size_t) c].b }) { s->set (fs, 2500.0, 0.70710678); s->reset(); }
    }
    det = {}; for (auto& d : bandDet) d = {};
    rmsC = coefFor (10.0, fs);
    peakC = coefFor (10.0, fs);
    optoRel = coefFor (1000.0, fs);
    for (auto* sm : { &makeupSm, &outSm, &mixSm }) sm->reset (fs, 0.03);
    makeupSm.setCurrentAndTargetValue (1.0f); outSm.setCurrentAndTargetValue (1.0f); mixSm.setCurrentAndTargetValue (1.0f);
    histCount = 0; histIn = histOut = histGr = 0.0f;
}

// rilevatore e tempi: level = picco (o RMS per squash) del sidechain, ritorna la riduzione attuale in dB (<= 0)
float BCompProcessor::runDetector (Det& d, float level, float t, float r, float k, float aC, float rC, int st) noexcept
{
    // livello: picco (attacco istantaneo, rilascio fisso di 10 ms: tra due picchi dell'onda non ricade) o, per squash, RMS
    d.env = level > d.env ? level : peakC * d.env + (1.0f - peakC) * level;
    float lv = d.env;
    if (st == squash) { d.rms = rmsC * d.rms + (1.0f - rmsC) * level * level; lv = std::sqrt (d.rms) * 1.41421356f; }
    const float target = gainDb (toDb (lv), t, r, k);
    float rel = rC;
    if (st == opto)
    {
        // cella ottica: più e più a lungo comprime, più lento torna (fino a 4 volte il release)
        d.mem = optoRel * d.mem + (1.0f - optoRel) * juce::jlimit (0.0f, 1.0f, -d.gr / 12.0f);
        rel = std::pow (rC, 1.0f / (1.0f + 3.0f * d.mem));
    }
    d.gr = target < d.gr ? aC * d.gr + (1.0f - aC) * target : rel * d.gr + (1.0f - rel) * target;
    return d.gr;
}

void BCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(), nCh = juce::jmin (2, buffer.getNumChannels());
    if (nCh == 0) return;
    const int st = juce::jlimit (0, kStyles - 1, (int) p[P_STYLE]->load());
    const float t = p[P_THR]->load(), r = p[P_RATIO]->load(), k = st == fet ? juce::jmin (2.0f, p[P_KNEE]->load()) : p[P_KNEE]->load();
    const float atkMs = st == opto ? juce::jmax (10.0f, p[P_ATK]->load()) : p[P_ATK]->load();
    const float aC = coefFor (atkMs, fs), rC = coefFor (p[P_REL]->load(), fs);
    const float sc = p[P_SCHP]->load();
    if (std::abs (sc - lastSc) > 0.5f) { for (auto& f : scHpF) f.set (fs, sc, 0.70710678); lastSc = sc; }
    makeupSm.setTargetValue (juce::Decibels::decibelsToGain (p[P_AUTO]->load() > 0.5f ? autoMakeupDb() : p[P_MAKEUP]->load()));
    outSm.setTargetValue (juce::Decibels::decibelsToGain (p[P_OUT]->load()));
    mixSm.setTargetValue (p[P_MIXON]->load() > 0.5f ? p[P_MIX]->load() / 100.0f : 1.0f);
    const float amt[3] = { p[P_LOW]->load() / 100.0f, p[P_MID]->load() / 100.0f, p[P_HIGH]->load() / 100.0f };
    // multi: per banda soglia spostata, ratio scalata da quanto lavora, basse più lente e alte più veloci
    const float bAtk[3] = { coefFor (atkMs * 1.5f, fs), aC, coefFor (atkMs * 0.5f, fs) };
    const float bRel[3] = { coefFor (p[P_REL]->load() * 1.5f, fs), rC, coefFor (p[P_REL]->load() * 0.7f, fs) };

    float* ch[2] = { buffer.getWritePointer (0), nCh > 1 ? buffer.getWritePointer (1) : nullptr };
    float peakIn = 0.0f, peakOut = 0.0f, maxGr = 0.0f, maxBand[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < n; ++i)
    {
        const float x0 = ch[0][i], x1 = ch[1] != nullptr ? ch[1][i] : x0;
        float y0, y1, grNow;
        if (st != multi)
        {
            const float s0 = scHpF[0].processHP (x0), s1 = ch[1] != nullptr ? scHpF[1].processHP (x1) : s0;
            const float g = runDetector (det, juce::jmax (std::abs (s0), std::abs (s1)), t, r, k, aC, rC, st);
            const float gl = juce::Decibels::decibelsToGain (g);
            y0 = x0 * gl; y1 = x1 * gl;
            if (st == fet)
            {
                // grinta: una saturazione morbida che cresce con la riduzione
                const float drive = 1.0f + 0.06f * -g;
                y0 = std::tanh (y0 * drive) / drive; y1 = std::tanh (y1 * drive) / drive;
            }
            grNow = g;
        }
        else
        {
            float band[2][3];
            for (int c = 0; c < 2; ++c)
            {
                const float x = c == 0 ? x0 : x1;
                const float lo = lp1[(size_t) c].b.processLP (lp1[(size_t) c].a.processLP (x));
                const float rest = hp1[(size_t) c].b.processHP (hp1[(size_t) c].a.processHP (x));
                band[c][0] = lo;
                band[c][1] = lp2[(size_t) c].b.processLP (lp2[(size_t) c].a.processLP (rest));
                band[c][2] = hp2[(size_t) c].b.processHP (hp2[(size_t) c].a.processHP (rest));
            }
            y0 = y1 = 0.0f; grNow = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                const float g = runDetector (bandDet[(size_t) b], juce::jmax (std::abs (band[0][b]), std::abs (band[1][b])),
                                             t + kBandThr[b], 1.0f + (r - 1.0f) * amt[b], k, bAtk[b], bRel[b], clean);
                const float gl = juce::Decibels::decibelsToGain (g);
                y0 += band[0][b] * gl; y1 += band[1][b] * gl;
                maxBand[b] = juce::jmax (maxBand[b], -g);
                grNow = juce::jmin (grNow, g);
            }
        }
        const float mk = makeupSm.getNextValue(), og = outSm.getNextValue(), m = mixSm.getNextValue();
        y0 = og * (x0 + m * (y0 * mk - x0));
        y1 = og * (x1 + m * (y1 * mk - x1));
        ch[0][i] = y0;
        if (ch[1] != nullptr) ch[1][i] = y1;
        const float pi = juce::jmax (std::abs (x0), std::abs (x1)), po = juce::jmax (std::abs (y0), std::abs (y1));
        peakIn = juce::jmax (peakIn, pi); peakOut = juce::jmax (peakOut, po); maxGr = juce::jmax (maxGr, -grNow);
        // storia per il grafico: un punto ogni 256 campioni (picchi e riduzione massima)
        histIn = juce::jmax (histIn, pi); histOut = juce::jmax (histOut, po); histGr = juce::jmax (histGr, -grNow);
        if (++histCount >= 256)
        {
            const int hp = (historyPos.load (std::memory_order_relaxed) + 1) & (kHistory - 1);
            history[(size_t) hp] = { toDb (histIn), toDb (histOut), histGr };
            historyPos.store (hp, std::memory_order_release);
            histCount = 0; histIn = histOut = histGr = 0.0f;
        }
    }
    grDb.store (maxGr, std::memory_order_relaxed);
    inDb.store (toDb (peakIn), std::memory_order_relaxed);
    outDb.store (toDb (peakOut), std::memory_order_relaxed);
    for (int b = 0; b < 3; ++b) bandGr[(size_t) b].store (st == multi ? maxBand[b] : 0.0f, std::memory_order_relaxed);
    for (auto& f : scHpF) f.flush();
    for (int c = 0; c < 2; ++c) for (auto* s : { &lp1[(size_t) c].a, &lp1[(size_t) c].b, &hp1[(size_t) c].a, &hp1[(size_t) c].b,
                                               &lp2[(size_t) c].a, &lp2[(size_t) c].b, &hp2[(size_t) c].a, &hp2[(size_t) c].b }) s->flush();
}

void BCompProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d);
}

void BCompProcessor::setStateInformation (const void* d, int s)
{
    if (auto x = getXmlFromBinary (d, s))
        if (x->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*x));
}

juce::AudioProcessorEditor* BCompProcessor::createEditor() { return new BCompEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BCompProcessor(); }
